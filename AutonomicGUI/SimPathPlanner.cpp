// SimPathPlanner.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimPathPlanner.h"
#include "SimAvatar.h"

#include "..\\autonomic\\RandomGenerator.h"

int CELLID( short u, short v ) { // convert coordinate into id (valid for coords -0x8FFF to 0x8FFF)
	int id;
	unsigned short a, b;
	a = u + 0x8FFF;
	b = v + 0x8FFF;
	memcpy( (void*)&id, (void*)&a, sizeof(short) );
	memcpy( ((byte*)&id) + sizeof(short), (void*)&b, sizeof(short) );
	return id;
}

//*****************************************************************************
// SimPathPlanner

//-----------------------------------------------------------------------------
// Constructor	
SimPathPlanner::SimPathPlanner( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {
	//dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	dir[0][0] = 0; dir[0][1] = 1;
	dir[1][0] = 1; dir[1][1] = 0;
	dir[2][0] = 0; dir[2][1] = -1;
	dir[3][0] = -1; dir[3][1] = 0;

	//fromLookUp[4] = { 2, 3, 0, 1 };
	fromLookUp[0] = 2;
	fromLookUp[1] = 3;
	fromLookUp[2] = 0;
	fromLookUp[3] = 1;
}

//-----------------------------------------------------------------------------
// Destructor
SimPathPlanner::~SimPathPlanner() {

	if ( this->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SimPathPlanner::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimPathPlanner %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimPathPlanner" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimPathPlanner::start() {

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->pfState = (float *)malloc(sizeof(float)*3);

	this->haveTarget = false;

	this->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SimPathPlanner::stop() {

	free( this->mapData );
	free( this->mapBuf );
	free( this->pfState );

	return SimAgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int SimPathPlanner::step() {
	return SimAgentBase::step();
}

int SimPathPlanner::configureParameters( DataStream *ds, SimAvatar *avatar ) {

	ds->unpackUUID( &this->ownerId );
	ds->unpackUUID( &this->mapId );
	ds->unpackUUID( &this->regionId );
	ds->unpackUUID( &this->pfId );
	this->maxLinear = ds->unpackFloat32();
	this->maxRotation = ds->unpackFloat32();	

	this->avatar = avatar;

	Log.log( LOG_LEVEL_NORMAL, "SimPathPlanner::configureParameters: ownerId %s mapId %s regionId %s pfId %s", 
		Log.formatUUID( LOG_LEVEL_NORMAL, &this->ownerId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &this->mapId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &this->regionId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &this->pfId ) );

	// get mission region
	sim->ddbGetRegion( &this->regionId, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		this->missionRegion.x = this->ds.unpackFloat32();
		this->missionRegion.y = this->ds.unpackFloat32();
		this->missionRegion.w = this->ds.unpackFloat32();
		this->missionRegion.h = this->ds.unpackFloat32();
	} else {
		return 1;
	}

	// get map resolution
	sim->ddbPOGGetInfo( &this->mapId, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	if ( this->ds.unpackChar() == DDBR_OK ) {
		this->ds.unpackFloat32(); // tilesize
		this->mapResolution = this->ds.unpackFloat32();
		this->ds.unpackInt32(); // stride
	} else {
		return 1;
	}

	// allocate map data
	int mapDivisions = (int)floor(1/this->mapResolution);

	this->mapOffset[0] = -1;
	this->mapOffset[1] = -1;
	this->mapWidth = (int)ceil(this->missionRegion.w*mapDivisions) + 2;
	this->mapHeight = (int)ceil(this->missionRegion.h*mapDivisions) + 2;
	this->mapData = (float *)malloc(sizeof(float)*this->mapWidth*this->mapHeight);
	this->mapBuf = (float *)malloc(sizeof(float)*this->mapWidth*this->mapHeight);

	return 0;
}

int	SimPathPlanner::setTarget( float x, float y, float r, char useRotation ) {

	Log.log( LOG_LEVEL_NORMAL, "SimPathPlanner::setTarget: %f %f %f %d", x, y, r, useRotation ); 

	//if ( this->haveTarget ) {
	//	// clear current actions
	//	this->avatar->clearActions();
	//}

	this->targetX = x;
	this->targetY = y;
	this->targetR = r;
	this->useRotation = useRotation;

	this->noPathCount = 0;

	this->haveTarget = true;

	this->goTarget();

	return 0;
}

int SimPathPlanner::goTarget() {

	Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::goTarget" ); 

	// update map
	DDBRegion reg;
	reg.x = floor(this->missionRegion.x/this->mapResolution)*this->mapResolution;
	reg.y = floor(this->missionRegion.y/this->mapResolution)*this->mapResolution;
	reg.w = ceil((this->missionRegion.x + this->missionRegion.w)/this->mapResolution)*this->mapResolution - reg.x;
	reg.h = ceil((this->missionRegion.y + this->missionRegion.h)/this->mapResolution)*this->mapResolution - reg.y;

	sim->ddbPOGGetRegion( &this->mapId, 
		reg.x - this->mapResolution, 
		reg.y - this->mapResolution, 
		reg.w + 2*this->mapResolution, 
		reg.h + 2*this->mapResolution, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		float x, y, w, h;
		int cw, ch;
		x = this->ds.unpackFloat32();
		y = this->ds.unpackFloat32();
		w = this->ds.unpackFloat32();
		h = this->ds.unpackFloat32();
		this->mapResolution = this->ds.unpackFloat32();

		cw = (int)floor( w / this->mapResolution );
		ch = (int)floor( h / this->mapResolution );

		memcpy( this->mapData, this->ds.unpackData( cw*ch*4 ), cw*ch*4 );

		// check if the target cell is occupied
		bool targetOccupied;
		int r, c;
		c = (int)(this->targetX/this->mapResolution) - this->mapOffset[0];
		r = (int)(this->targetY/this->mapResolution) - this->mapOffset[1];
		if ( this->mapData[r + c*this->mapHeight] < 0.45f ) { // occupied
			targetOccupied = true;
		} else {
			targetOccupied = false;
		}

		// process the new map
		this->processMap();

		if ( targetOccupied ) { // give up
			this->giveUp( SimAvatar::TP_TARGET_OCCUPIED );
			return 0;
		}
	} else {
		return 1;
	}

	// get avatar position
	_timeb tb; // just a dummy
	sim->ddbPFGetInfo( &this->pfId, DDBPFINFO_CURRENT, &tb, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		_timeb *tb;

		if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
			return 1; // what happened here?
		}
		
		tb = (_timeb *)this->ds.unpackData( sizeof(_timeb) );
		memcpy( this->pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );
		
		Log.log( 0, "SimPathPlanner::goTarget: state x %f y %f r %f", this->pfState[0], this->pfState[1], this->pfState[2] );
	
	} else {
		return 1;
	}

	if ( this->checkArrival() ) {
		return 0; // we're done!
	}

	this->planPath();

	return 0;
}

int SimPathPlanner::checkArrival() {
	int arrived = 1;

	if ( fabs(this->pfState[0] - this->targetX) > LINEAR_BOUND )
		arrived = 0;
	if ( fabs(this->pfState[1] - this->targetY) > LINEAR_BOUND )
		arrived = 0;
	if ( this->useRotation ) {
		float dR = this->pfState[2] - this->targetR;
		while ( dR > fM_PI ) dR -= fM_PI*2;
		while ( dR < -fM_PI ) dR += fM_PI*2;
	    if ( fabs(dR) > ROTATIONAL_BOUND )
			arrived = 0;
	}

	if ( arrived ) {
		this->haveTarget = false;
		
		// notify owner
		this->avatar->doneTargetPos( true );
	}

	return arrived;
}

int SimPathPlanner::giveUp( int reason ) {

	this->haveTarget = false;

	// notify owner
	this->avatar->doneTargetPos( false, reason );

	return 0;
}

int SimPathPlanner::processMap() {
	int kernalWidth = 7;
	float kernalVals[] = { 0.05f, 0.1f, 0.2f, 0.3f, 0.2f, 0.1f, 0.05f };
	float *kernal = kernalVals + kernalWidth/2;
	float *cell;
	int r, c, k;
	int startCX, startCY, endCX, endCY;

#ifdef DEBUG_DUMPMAP
	{
		int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "mapdump%d.txt", count );
		fopen_s( &fp, namebuf, "w" );
		if ( fp ) {
			int r, c;
			for ( r=this->mapHeight-1; r>=0; r-- ) {
				for ( c=0; c<this->mapWidth; c++ ) {
					fprintf( fp, "%f\t", this->mapData[r + c*this->mapHeight] );
				}
				fprintf( fp, "\n" );
			}
			fclose( fp );
		}
		count++;
	}
#endif

	// map -> horizontal blur -> mapBuf
	for ( c=0; c<this->mapWidth; c++ ) {
		for ( r=0; r<this->mapHeight; r++ ) {
			cell = this->mapBuf + r + c*this->mapHeight;
			*cell = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( c+k < 0 || c+k >= this->mapWidth ) // we're outside the source
					*cell += kernal[k]*0.5f;
				else
					*cell += kernal[k]*this->mapData[r + (c+k)*this->mapHeight];
			}
		}
	}


	// mapBuf -> vertical blur -> mapData
	// NOTE we are also only taking the blurred value if it is lower than the original map data, this should prevent high confidence vacancies from blurring over low confidence occupancies
	float blurVal;
	for ( c=0; c<this->mapWidth; c++ ) {
		for ( r=0; r<this->mapHeight; r++ ) {
			cell = this->mapData + r + c*this->mapHeight;
			blurVal = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( r+k < 0 || r+k >= this->mapHeight ) // we're outside the source
					blurVal += kernal[k]*0.5f;
				else
					blurVal += kernal[k]*this->mapBuf[r+k + c*this->mapHeight];
			}
			if ( blurVal < *cell )
				*cell = blurVal;
		}
	}

	// draw on mission boundary
	startCX = (int)floor( this->missionRegion.x / this->mapResolution ) - this->mapOffset[0];
	startCY = (int)floor( this->missionRegion.y / this->mapResolution ) - this->mapOffset[1];
	endCX = (int)floor( (this->missionRegion.x + this->missionRegion.w) / this->mapResolution ) - this->mapOffset[0];
	endCY = (int)floor( (this->missionRegion.y + this->missionRegion.h) / this->mapResolution ) - this->mapOffset[1];
	c = startCX-1;
	k = endCX+1;
	for ( r=startCY-1; r<=endCY+1; r++ ) {
		this->mapData[r + c*this->mapHeight] = 0;
		this->mapData[r + k*this->mapHeight] = 0;
	}
	r = startCY-1;
	k = endCY+1;
	for ( c=startCX; c<=endCX; c++ ) {
		this->mapData[r + c*this->mapHeight] = 0;
		this->mapData[k + c*this->mapHeight] = 0;
	}

#ifdef DEBUG_DUMPMAP
	{
		int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "mapprocessed%d.txt", count );
		fopen_s( &fp, namebuf, "w" );
		if ( fp ) {
			int r, c;
			for ( r=this->mapHeight-1; r>=0; r-- ) {
				for ( c=0; c<this->mapWidth; c++ ) {
					fprintf( fp, "%f\t", this->mapData[r + c*this->mapHeight] );
				}
				fprintf( fp, "\n" );
			}
			fclose( fp );
		}
		count++;
	}
#endif

	return 0;
}

int SimPathPlanner::updateWeightedPathLength( TESTCELL *cell, std::map<int,TESTCELL*> *cellListed ) {
	int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	int fromLookUp[4] = { 2, 3, 0, 1 };
	
	int i;
	std::map<int,TESTCELL*>::iterator iterCL;
	float mapVal;
	
	for ( i=0; i<4; i++ ) {
		if ( (iterCL = cellListed->find(CELLID(cell->x+dir[i][0],cell->y+dir[i][1]))) != cellListed->end() ) {
			mapVal = this->mapData[(cell->y+dir[i][1]-this->mapOffset[1]) + (cell->x+dir[i][0]-this->mapOffset[0])*this->mapHeight];
			if ( iterCL->second->wPathLength > cell->wPathLength + CELLWEIGHT(mapVal) ) {
				iterCL->second->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
				iterCL->second->fromDir = fromLookUp[i];
				updateWeightedPathLength( iterCL->second, cellListed );
			}
		}
	}

	return 0;
}

int SimPathPlanner::nextCell( TESTCELL *cell, float targetCX, float targetCY, std::map<int,TESTCELL*> *cellListed, std::multimap<float,TESTCELL*> *frontier, TESTCELL *bestCell, float *bestDistSq ) {
	int i;
	float weight;
	float mapVal, distFactor;
	float xdist, ydist;
	
	std::map<int,TESTCELL*>::iterator iterCL;
	TESTCELL *newCell;

	float curDistSq;

	if ( cell->fromDir == -1 ) { // this is the first cell so we have to list ourselves
		(*cellListed)[CELLID(cell->x,cell->y)] = cell;

		xdist = targetX - (cell->x + 0.5f)*this->mapResolution;
		ydist = targetY - (cell->y + 0.5f)*this->mapResolution;
		*bestDistSq = xdist*xdist + ydist*ydist + 1; // add one because we want this to be immediately replaced
	}
	
	xdist = targetX/this->mapResolution - (cell->x + 0.5f);
	ydist = targetY/this->mapResolution - (cell->y + 0.5f);
	if ( cell->pathLength >= MAX_CELL_PATH_LENGTH 
	  || (*bestDistSq == 0 && cell->pathLength + sqrt(xdist*xdist + ydist*ydist) >= bestCell->pathLength) ) {
		return -1; // give up on this cell
	}

	xdist = targetX - (cell->x + 0.5f)*this->mapResolution;
	ydist = targetY - (cell->y + 0.5f)*this->mapResolution;
	curDistSq = xdist*xdist + ydist*ydist;
	if ( cell->x == (int)floor( targetX / this->mapResolution ) && cell->y == (int)floor( targetY / this->mapResolution ) ) {
		*bestCell = *cell;
		*bestDistSq = 0;
	
		return 0; // we made it!
	} else if ( curDistSq < *bestDistSq ) {
		*bestCell = *cell;
		*bestDistSq = curDistSq;
	}

	// check each direction
	for ( i=0; i<4; i++ ) {
		if ( i == cell->fromDir ) {
			continue;
		} else {
			mapVal = this->mapData[(cell->y+dir[i][1]-this->mapOffset[1]) + (cell->x+dir[i][0]-this->mapOffset[0])*this->mapHeight];
			if ( mapVal <= 0.55f ) { // drive carefully
				continue;
			} else if ( (iterCL = cellListed->find(CELLID(cell->x+dir[i][0],cell->y+dir[i][1]))) != cellListed->end() ) {
				if ( iterCL->second->wPathLength > cell->wPathLength + CELLWEIGHT(mapVal) ) {
					iterCL->second->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
					iterCL->second->fromDir = fromLookUp[i];
					updateWeightedPathLength( iterCL->second, cellListed );
				}
			} else {
				xdist = targetX - (cell->x + dir[i][0] + 0.5f)*this->mapResolution;
				ydist = targetY - (cell->y + dir[i][1] + 0.5f)*this->mapResolution;
				distFactor = 1/max( this->mapResolution/2, sqrt(xdist*xdist + ydist*ydist));
				weight = mapVal * distFactor;
				newCell = new TESTCELL;
				newCell->x = cell->x + dir[i][0];
				newCell->y = cell->y + dir[i][1];
				newCell->pathLength = cell->pathLength + 1;
				newCell->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
				newCell->fromDir = fromLookUp[i];
				(*cellListed)[CELLID(newCell->x,newCell->y)] = newCell;
				frontier->insert( std::pair<float,TESTCELL*>(weight, newCell) );
			}
		}
	}

	return -1;
}

float SimPathPlanner::calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, std::map<int,TESTCELL*> *cellListed ) {
	float fullLen;
	float curLen = 0, newLen, vertLen, horzLen;
	float wlen = 0;
	float dx, dy;
	float fullx, fully;
	float mapVal;
	int vertEdge, vertStep, horzEdge, horzStep;

	fullx = endX - startX;
	fully = endY - startY;
	fullLen = sqrt( fullx*fullx + fully*fully );

	if ( fullx >= 0 ) {
		horzEdge = 1;
		horzStep = 1;
	} else {
		horzEdge = 0;
		horzStep = -1;
	}
	if ( fully >= 0 ) {
		vertEdge = 1;
		vertStep = 1;
	} else {
		vertEdge = 0;
		vertStep = -1;
	}

	while ( curLen < fullLen ) {
		// make sure this is a valid cell
		if ( cellListed->find(CELLID(cellX,cellY)) == cellListed->end() )
			return -1; // this cell is no good

		mapVal = this->mapData[(cellY-this->mapOffset[1]) + (cellX-this->mapOffset[0])*this->mapHeight];
			
		// figure out where we exit this cell
		// first assume we exit vertically
		if ( fully != 0 ) {
			dy = cellY + vertEdge - startY;
			dx = fullx * dy/fully;
			vertLen = dx*dx + dy*dy;
		} else {
			vertLen = 99999999.0f; // very big
		}
		// the assume we exit horizontally
		if ( fullx != 0 ) {
			dx = cellX + horzEdge - startX;
			dy = fully * dx/fullx;
			horzLen = dx*dx + dy*dy;
		} else {
			horzLen = 99999999.0f; // very big
		}

		// pick the smaller length
		if ( horzLen < vertLen ) {
			cellX += horzStep;
			newLen = sqrt( horzLen );
		} else {
			cellY += vertStep;
			newLen = sqrt( vertLen );
		}

		// make sure we don't pass the end point
		if ( newLen > fullLen )
			newLen = fullLen;

		// add the weight
		wlen += CELLWEIGHT(mapVal)*(newLen-curLen);
		if ( mapVal < 0.5f )
			wlen += 10*this->mapResolution; // big penalty

		curLen = newLen;
	}

	return wlen;
}

int SimPathPlanner::shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, std::map<int,TESTCELL*> *cellListed ) {
	TESTCELL *baseCell;
	TESTCELL *endCell;
	TESTCELL *prevCell;
	TESTCELL *bestCell = NULL;
	float lastWLength;
	float wLength;
	float interLength;
	int badCells;
		
	std::map<int,TESTCELL*>::iterator iterCL;
	
	iterCL = cellListed->find(CELLID(*baseCellX,*baseCellY));
	baseCell = iterCL->second;
	iterCL = cellListed->find(CELLID(*baseCellX+dir[baseCell->fromDir][0],*baseCellY+dir[baseCell->fromDir][1]));
	endCell = iterCL->second;

	if ( endCell->x == destCellX && endCell->y == destCellY ) { // done already
		*baseCellX = destCellX;
		*baseCellY = destCellY;
		*baseX = destX;
		*baseY = destY;
		return 0;
	}

	lastWLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
	if ( lastWLength == -1 )
		return 0; // this should never happen

	interLength = 0;
	bestCell = endCell;
	badCells = 0;

	prevCell = endCell;
	while ( endCell->x != destCellX || endCell->y != destCellY ) {
		
		iterCL = cellListed->find(CELLID(endCell->x+dir[endCell->fromDir][0],endCell->y+dir[endCell->fromDir][1]));
		endCell = iterCL->second;

		if ( endCell->x == destCellX && endCell->y == destCellY ) { 
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, destX, destY, cellListed );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, destX, destY, cellListed );
		} else {
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
		}

		if ( wLength == -1 || (wLength - (lastWLength + interLength)) > 0.00000001f ) {
			badCells++;
			if ( badCells >= 10 ) // give up
				break;	
		} else {
			lastWLength = wLength;
			interLength = 0;
			bestCell = endCell;
			badCells = 0;
		}
		prevCell = endCell;
	}

	if ( badCells == 0 && endCell->x == destCellX && endCell->y == destCellY ) {
		*baseCellX = destCellX;
		*baseCellY = destCellY;
		*baseX = destX;
		*baseY = destY;
	} else {
		*baseCellX = bestCell->x;
		*baseCellY = bestCell->y;
		*baseX = bestCell->x + 0.5f;
		*baseY = bestCell->y + 0.5f;
	}

	return 0;
}

int SimPathPlanner::planPath() {
	float startX, startY;
	int startCX, startCY, endCX, endCY;
	bool nopath = false;

	std::multimap<float,TESTCELL*>::iterator iter;
	std::map<int,TESTCELL*>::iterator iterCL;
	TESTCELL *testCell;
	TESTCELL bestCell;
	WAYPOINT wp;
	std::list<WAYPOINT> waypoints;
	std::list<WAYPOINT>::iterator iterWP;

	Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: startPose %f %f %f endPose %f %f %f %d", this->pfState[0], this->pfState[1], this->pfState[2], this->targetX, this->targetY, this->targetR, this->useRotation ); 

	// plan path
	startX = this->pfState[0];
	startY = this->pfState[1];

	startCX = (int)floor(startX / this->mapResolution);
	startCY = (int)floor(startY / this->mapResolution);
	endCX = (int)floor(this->targetX / this->mapResolution);
	endCY = (int)floor(this->targetY / this->mapResolution);
	
	testCell = new TESTCELL;
	testCell->x = startCX;
	testCell->y = startCY;
	testCell->fromDir = -1;
	testCell->pathLength = 0;
	testCell->wPathLength = 0;

	float bestDistSq = -1;
	this->nextCell( testCell, this->targetX, this->targetY, &this->cellListed, &this->frontier, &bestCell, &bestDistSq );
	// try the next best cell
	while ( !this->frontier.empty() ) {
		iter = --this->frontier.end();
		testCell = iter->second;
		this->frontier.erase( iter );
		this->nextCell( testCell, this->targetX, this->targetY, &this->cellListed, &this->frontier, &bestCell, &bestDistSq );
	}

	if ( bestCell.fromDir != -1 ) {
		float cbaseX, cbaseY; // in cell coordinates
		float cstartX, cstartY; // in cell coordinates
		int baseCX, baseCY;
		cstartX = startX / this->mapResolution;
		cstartY = startY / this->mapResolution;
		baseCX = bestCell.x;
		baseCY = bestCell.y;
		if ( bestCell.x == endCX && bestCell.y == endCY ) {
			cbaseX = this->targetX / this->mapResolution;
			cbaseY = this->targetY / this->mapResolution;
		} else {
			cbaseX = baseCX + 0.5f;
			cbaseY = baseCY + 0.5f;
		}
		while ( baseCX != startCX || baseCY != startCY ) {
			wp.x = cbaseX*this->mapResolution;
			wp.y = cbaseY*this->mapResolution;
			waypoints.push_front( wp );
			shortestPath( &baseCX, &baseCY, &cbaseX, &cbaseY, startCX, startCY, cstartX, cstartY, &this->cellListed );
		}
		wp.x = cbaseX*this->mapResolution;
		wp.y = cbaseY*this->mapResolution;
		waypoints.push_front( wp );
	} else { // we start and end in the same cell
		if ( startCX == endCX && startCY == endCY ) { // our destination is here
			wp.x = this->targetX;
			wp.y = this->targetY;
			waypoints.push_front( wp );
		} else { // no path
			nopath = true;
			/*float len;
			wp.x = this->targetX - startX;
			wp.y = this->targetY - startY;
			len = sqrt( wp.x*wp.x + wp.y*wp.y );

			wp.x = startX + wp.x/len * min( 0.15f, len ); // 15 cm
			wp.y = startY + wp.y/len * min( 0.15f, len ); // 15 cm
			waypoints.push_front( wp );*/
		}
		
		wp.x = startX;
		wp.y = startY;
		waypoints.push_front( wp );
	}

#ifdef DEBUG_DUMPPATH
	{
		int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
		int cellX, cellY, fromDir;
		int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "pathdump%d.txt", count );
		fopen_s( &fp, namebuf, "w" );
		if ( fp ) {
			cellX = bestCell.x;
			cellY = bestCell.y;
			fromDir = bestCell.fromDir;
			fprintf( fp, "%d %d\n", cellX, cellY );
			while ( fromDir != -1 ) {
		
				cellX += dir[fromDir][0];
				cellY += dir[fromDir][1];

				iterCL = cellListed.find(CELLID(cellX,cellY));
				fromDir = iterCL->second->fromDir;
				fprintf( fp, "%d %d\n", cellX, cellY );
			}
			std::list<WAYPOINT>::iterator iter = waypoints.begin();
			while ( iter != waypoints.end() ) {
				fprintf( fp, "%f %f\n", iter->x, iter->y );
				iter++;
			}
			fclose( fp );
		}
		count++;
	}
#endif

	// cleanup 
	while ( !this->cellListed.empty() ) {
		iterCL = this->cellListed.begin();
		free( iterCL->second );
		this->cellListed.erase( iterCL );
	}

	// formulate actions
	std::list<ActionPair> actions;
	ActionPair action;
	float newR, curR, dR;
	float dx, dy, dD;

	// TODO if the end point is just a short way behind us consider backing up

	curR = this->pfState[2];
	iterWP = waypoints.begin();
	wp = *iterWP;
	iterWP++;
	if ( !nopath
	  && (fabs(this->pfState[0] - this->targetX) > LINEAR_BOUND || fabs(this->pfState[1] - this->targetY) > LINEAR_BOUND) ) { // move only if we need to
		do {
			Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: waypoint %f %f %f to %f %f", wp.x, wp.y, curR, iterWP->x, iterWP->y ); 

			// do rotation
			newR = atan2( iterWP->y - wp.y, iterWP->x - wp.x );
			dR = newR - curR;
			while ( dR > fM_PI ) dR -= 2*fM_PI;
			while ( dR < -fM_PI ) dR += 2*fM_PI;
			while ( fabs(dR) > this->maxRotation ) {
				action.action = AA_ROTATE;
				if ( dR > 0 ) {
					action.val = this->maxRotation;
					dR -= this->maxRotation;
				} else {
					action.val = -this->maxRotation;
					dR += this->maxRotation;
				}
				actions.push_back( action );
				Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_ROTATE %f", action.val ); 
			}
			if ( fabs(dR) > 0.01f ) {
				Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_ROTATE %f", dR ); 
				action.action = AA_ROTATE;
				action.val = dR;
				actions.push_back( action );
			}
			curR = newR;

			// do move(s)
			dx = iterWP->x - wp.x;
			dy = iterWP->y - wp.y;
			dD = sqrt( dx*dx + dy*dy );
			while ( fabs(dD) > this->maxLinear ) {
				action.action = AA_MOVE;
				if ( dD > 0 ) {
					action.val = this->maxLinear;
					dD -= this->maxLinear;
				} else {
					action.val = -this->maxLinear;
					dD += this->maxLinear;
				}
				actions.push_back( action );
				Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_MOVE %f", action.val ); 
			}
			if ( fabs(dD) > 0.005f ) {
				Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_MOVE %f", dD ); 
				action.action = AA_MOVE;
				action.val = dD;
				actions.push_back( action );
			}
			wp = *iterWP;

			iterWP++;
		} while ( iterWP != waypoints.end() );
	}

	if ( !nopath
	  && this->useRotation
	  && fabs(wp.x - this->targetX) <= LINEAR_BOUND 
	  && fabs(wp.y - this->targetY) <= LINEAR_BOUND ) { // do the final rotation if we're at our end point
		newR = this->targetR;
		dR = newR - curR;
		while ( dR > fM_PI ) dR -= 2*fM_PI;
		while ( dR < -fM_PI ) dR += 2*fM_PI;
		while ( fabs(dR) > this->maxRotation ) {
			action.action = AA_ROTATE;
			if ( dR > 0 ) {
				action.val = this->maxRotation;
				dR -= this->maxRotation;
			} else {
				action.val = -this->maxRotation;
				dR += this->maxRotation;
			}
			actions.push_back( action );
			Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_ROTATE %f", action.val ); 
		}
		if ( fabs(dR) > 0.01f ) {
			Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_ROTATE %f", dR ); 
			action.action = AA_ROTATE;
			action.val = dR;
			actions.push_back( action );
		}
		curR = newR;
	}

	if ( nopath ) { // we couldn't find a path, rotate randomly and wait a bit and hopefully more sensor readings will be processed
		Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: no path %d", this->noPathCount ); 
		if ( this->noPathCount > NOPATH_GIVEUP_COUNT ) {
			this->giveUp( SimAvatar::TP_TARGET_UNREACHABLE );
			return 0;
		}
		
		action.action = AA_ROTATE;
		action.val = (float)randGen->Uniform01() - 0.5f;
		actions.push_back( action );
		Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_ROTATE %f", action.val ); 
		Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::planPath: AA_WAIT %f", NOPATH_DELAY ); 
		action.action = AA_WAIT;
		action.val = NOPATH_DELAY;
		actions.push_back( action );
		this->noPathCount++;
	} else {
		this->noPathCount = 0;
	}

	// send actions
	std::list<ActionPair>::iterator iterA = actions.begin();
	int lastAction = -1;
	this->actionsSent = 0;
	while ( iterA != actions.end() && this->actionsSent < ACTION_HORIZON ) {
		if ( actionsSent > 1 && lastAction == AA_ROTATE && iterA->action == AA_MOVE )
			iterA->val = min( 0.05f, iterA->val ); // only move a small amount immediatly after a rotate to give sensors a chance to catch up

		this->avatar->queueAction( &this->uuid, NEW_MEMBER_CB(SimPathPlanner,cbActionFinished), this->actionsSent, iterA->action, &iterA->val, sizeof(float) );
		this->actionsSent++;

		if ( actionsSent > 1 && lastAction == AA_ROTATE && iterA->action == AA_MOVE )
			break; 
		lastAction = iterA->action;

		iterA++;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool SimPathPlanner::cbActionFinished( void *vpDS ) {
	DataStream *ds = (DataStream *)vpDS;

	int thread = ds->unpackInt32();
	char result = ds->unpackChar();

	if ( result == AAR_SUCCESS ) { // success

	} else if ( result == AAR_GAVEUP ) { // gave up, try new path

	} else { // cancelled or aborted, we're done
		int reason = ds->unpackInt32();

		this->haveTarget = false;

		Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::cbActionFinished: action cancelled, reason %d thread %d", reason, thread );
		return 0;
	}

	this->actionsSent--;
	
	Log.log( LOG_LEVEL_VERBOSE, "SimPathPlanner::cbActionFinished: action complete, waiting for %d more actions (thread %d)", this->actionsSent, thread );

	if ( this->actionsSent ) {
		// waiting for more
	} else {
		this->goTarget(); // plan the next step
	}

	return 0;
}