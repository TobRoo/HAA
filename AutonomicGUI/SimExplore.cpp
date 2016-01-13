// SimExplore.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimExplore.h"
#include "SimAvatar.h"

#include "..\\autonomic\\RandomGenerator.h"

#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

#define round(val) floor((val) + 0.5f)

//*****************************************************************************
// SimExplore

//-----------------------------------------------------------------------------
// Constructor	
SimExplore::SimExplore( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {

	this->nextSectionId = 0;
	this->sectionIds = new std::list<int>;

	this->mapReady = false;

	memset( this->sections, 0, sizeof(this->sections) );

	this->totalCells = 0;

	float x[5] = { 0.1f, 0.1f, 0.9f, 0.9f, 0.1f };
	float y[5] = { 0.1f, 0.9f, 0.9f, 0.1f, 0.1f };
	this->cellPath = vis->newPath( 5, x, y );

	this->nextAvatarThread = 0;

}

//-----------------------------------------------------------------------------
// Destructor
SimExplore::~SimExplore() {

	if ( this->started ) {
		this->stop();
	}

	// clean up sections
	std::list<int>::iterator iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		delete this->sections[*iterS];
		iterS++;
	}
		
	// free tiles
	while ( !this->tiles.empty() ) {
		free( this->tiles.begin()->second );
		this->tiles.erase( this->tiles.begin() );
	}

	// clean up visualization
	std::list<int>::iterator itC;
	for ( itC = this->cellVisualization.begin(); itC != this->cellVisualization.end(); itC++ ) {
		vis->deleteObject( *itC, true );
	}
	vis->deletePath( this->cellPath );

	delete this->sectionIds;

}

//-----------------------------------------------------------------------------
// Configure

int SimExplore::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimExplore %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimExplore" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimExplore::start() {

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->partitionUpdateTimer = 0;

	this->sharedArrayGaussSize = 0;
	this->sharedArrayGauss = NULL;
	this->sharedArrayPixSize = 0;
	this->sharedArrayPix = NULL;
	
	this->started = true;

	return 0;
}

//-----------------------------------------------------------------------------
// Stop

int SimExplore::stop() {

	// clean up visualization
	std::list<int>::iterator itC;
	for ( itC = this->cellVisualization.begin(); itC != this->cellVisualization.end(); itC++ ) {
		vis->deleteObject( *itC, true );
	}
	this->cellVisualization.clear();

	// clear dirty regions list
	this->dirtyMapRegions.clear();

	// free shared arrays
	if ( this->sharedArrayGaussSize )
		free( this->sharedArrayGauss );
	if ( this->sharedArrayPixSize )
		free( this->sharedArrayPix );

	return SimAgentBase::stop();
}


//-----------------------------------------------------------------------------
// Processing

int SimExplore::addRegion( UUID *id ) {

	// get the region
	sim->ddbGetRegion( id, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread

	if ( DDBR_OK != this->ds.unpackChar() ) {
		return 1;
	} 

	DDBRegion region;

	region.x = this->ds.unpackFloat32();
	region.y = this->ds.unpackFloat32();
	region.w = this->ds.unpackFloat32();
	region.h = this->ds.unpackFloat32();

	this->_addRegion( id, &region );

	return 0;
}

int SimExplore::_addRegion( UUID *id, DDBRegion *region ) {

	// add the region
	this->regions[ *id ] = *region;

	if ( this->mapReady ) {
		// initialize cells
		this->initializeRegion( id );

		this->updatePartitions( true );
	}

	return 0;
}

int SimExplore::initializeRegion( UUID *id ) {
	float x, y, w, h;
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int tileId, lastTileId;
	CELL *tile;
	CELLCOORD cellCoord;
	mapRegions::iterator region;
	int ii, jj; // loop vars

	mapTile::iterator iterTile;

	// get the region
	region = this->regions.find( *id );

	if ( region == this->regions.end() )
		return 1; // region not found!

	// create new section
	int newId = this->getNewSectionId();
	this->sections[newId] = new std::list<CELLCOORD>;
	this->sectionDirty[newId] = true;

	// convert coords and tileSize into cell space
	x = floor(region->second.x/this->mapResolution);
	y = floor(region->second.y/this->mapResolution);
	w = ceil((region->second.x+region->second.w)/this->mapResolution) - x;
	h = ceil((region->second.y+region->second.h)/this->mapResolution) - y;
	tileSize = round(this->mapTileSize/this->mapResolution);

	// increment the total cell count
	this->totalCells += (int)(w*h);

	lastTileId = -1;
	for ( c=0 ; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);
	
			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tiles.find(tileId);
				if ( iterTile == this->tiles.end() ) {
					// this tile doesn't exist yet, so make it!
					tile = (CELL*)malloc(sizeof(CELL)*this->mapStride*this->mapStride);
					if ( !tile ) {
						return 1; // malloc failed
					}
					for ( ii=0; ii<this->mapStride; ii++ ) {
						for ( jj=0; jj<this->mapStride; jj++ ) {
							tile[jj+ii*this->mapStride].type = CT_OUTOFBOUNDS;
							tile[jj+ii*this->mapStride].section = -1;
							tile[jj+ii*this->mapStride].partition = -1;
							tile[jj+ii*this->mapStride].dirty = false;
						}
					}

					this->tiles[tileId] = tile;
				} else {
					tile = iterTile->second;
				}

				lastTileId = tileId;
			}

			if ( tile[j+i*this->mapStride].type == CT_OUTOFBOUNDS ) {
				tile[j+i*this->mapStride].type = CT_UNKNOWN;
				tile[j+i*this->mapStride].section = newId;

				// add to cell list
				cellCoord.x = (x+c)*this->mapResolution;
				cellCoord.y = (y+r)*this->mapResolution;
				this->sections[newId]->push_back( cellCoord );
			}
		}
	}

	if ( !this->sections[newId]->empty() ) {
		// dirty all sections so that they can merge if necessary
		std::list<int>::iterator iterS = this->sectionIds->begin();
		while ( iterS != this->sectionIds->end() ) {
			this->sectionDirty[ *iterS ] = true;
			iterS++;
		}
		
		this->sectionIds->push_back( newId );
	} else {
		delete this->sections[newId];
		this->sections[newId] = NULL;
	}

	// dirty sections 
	if ( this->dirtyRegion( &region->second ) ) { // we have some active sections in this region
		// dirty map
		this->dirtyMap( &region->second );
	}

	this->visualizeSections();

	return 0;
}

SimExplore::CELL * SimExplore::getCell( float x, float y, bool useRound ) {
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int tileId;
	
	if ( useRound ) {
		x = round(x/this->mapResolution);
		y = round(y/this->mapResolution);
	} else {
		x = floor(x/this->mapResolution);
		y = floor(y/this->mapResolution);
	}
	tileSize = round(this->mapTileSize/this->mapResolution);

	mapTile::iterator iterTile;

	u = (int)floor(x/tileSize);
	i = (int)(x - u*tileSize);
	
	v = (int)floor(y/tileSize);
	j = (int)(y - v*tileSize);

	tileId = DDB_TILE_ID(u,v);
	iterTile = this->tiles.find(tileId);
	if ( iterTile == this->tiles.end() ) { // tile doesn't exist
		return NULL;
	}

	return &iterTile->second[j+i*this->mapStride];
}

int SimExplore::addMap( UUID *id ) {
	this->mapId = *id;

	// get map info
	sim->ddbPOGGetInfo( id, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread

	if ( DDBR_OK != this->ds.unpackChar() ) {
		return 1;
	} 

	this->mapTileSize = this->ds.unpackFloat32();
	this->mapResolution = this->ds.unpackFloat32();
	this->mapStride = this->ds.unpackInt32();

	this->mapReady = true;

	// initialize regions if we have any waiting
	mapRegions::iterator iterR = this->regions.begin();
	while ( iterR != this->regions.end() ) {
		this->initializeRegion( (UUID *)&iterR->first );
		iterR++;
	}

	this->updatePartitions( true );

	return 0;
}

int SimExplore::updateMap( float x, float y, float w, float h, float *data ) {
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int stride; // data stride
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	CELL *tile;

	mapTile::iterator iterTile;

	// convert coords and tileSize into cell space
	x = round(x/this->mapResolution);
	y = round(y/this->mapResolution);
	w = round(w/this->mapResolution);
	h = round(h/this->mapResolution);
	tileSize = round(this->mapTileSize/this->mapResolution);

	stride = (int)h;

	lastTileId = -1;
	for ( c=0; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);

			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tiles.find(tileId);				
				lastTileId = tileId;
			}

			if ( iterTile != this->tiles.end() ) {
				tile = iterTile->second;
				tile[j+i*this->mapStride].occupancy = data[r+c*stride] ;
			} else { // tile doesn't exist, therefor it isn't in a valid region probably this should never happen
				// nothing to do
			}
		}
	}

	return 0;
}

int SimExplore::getNewSectionId() {
	int newId;
	for ( newId=this->nextSectionId; newId<this->nextSectionId + SimExplore_MAX_SECTIONS; newId++ ) {
		if ( this->sections[newId<SimExplore_MAX_SECTIONS ? newId : newId-SimExplore_MAX_SECTIONS] == NULL )
			break;
	}

	if ( newId == this->nextSectionId + SimExplore_MAX_SECTIONS ) {
		Log.log( 0, "SimExplore::getNewSectionId: ran out of section ids!" );	
		return 1;
	}
	this->nextSectionId = (newId + 1) % SimExplore_MAX_SECTIONS;

	return newId % SimExplore_MAX_SECTIONS;
}

// floods from the coordinate x,y and returns the new section id (-1 is invalid)
// unlisted - list of unlisted cells (either newly occupied or unoccupied and so were not added)
int SimExplore::floodSection( float x, float y, std::list<CELLCOORD> *unlisted ) {
	int i;

	// create new section
	i = this->getNewSectionId();
	this->sections[i] = new std::list<CELLCOORD>;
	this->sectionDirty[i] = true;


	// begin flood
	this->_floodSectionRecurse( x, y, i, this->sections[i], unlisted );

	return i;
}

int SimExplore::_floodSectionRecurse( float x, float y, int id, std::list<CELLCOORD> *section, std::list<CELLCOORD> *unlisted ) {
	CELL *cell;
	CELLCOORD cellCoord;
	
	cellCoord.x = x;
	cellCoord.y = y;

	cell = getCell( x, y );

	if ( !cell 
	  || cell->section == id 
	  || cell->type == CT_OUTOFBOUNDS 
	  || cell->type == CT_OCCUPIED 
	  || cell->type == CT_UNREACHABLE ) {
		return 0;
	}

	if ( !this->sectionDirty[cell->section] ) { // we overran into a section that wasn't dirty! 
		this->sectionDirty[cell->section] = true;
		// move it to the end of the list to make sure we update it
		std::list<int>::iterator iterS = this->sectionIds->begin();
		while ( iterS != this->sectionIds->end() ) {
			if ( *iterS == cell->section )
				break;
			iterS++;
		}
		this->sectionIds->erase( iterS );
		this->sectionIds->push_back( cell->section );
	}

	if ( cell->type == CT_UNOCCUPIED ) {
		cell->section = id;
	} else { // CT_UNKNOWN
		if ( cell->occupancy > SimExplore_THRESHHOLD_UNOCCUPIED ) {
			cell->type = CT_UNOCCUPIED;
			cell->section = id;
			unlisted->push_back( cellCoord );
		} else if ( cell->occupancy < SimExplore_THRESHHOLD_OCCUPIED ) {
			cell->type = CT_OCCUPIED;
			cell->section = -1;
			unlisted->push_back( cellCoord );
			return 0; // dead end
		} else if ( cell->activity > SimExplore_THRESHHOLD_ACTIVITY ) {
			// assume UNOCCUPIED?!
			cell->type = CT_UNOCCUPIED;
			cell->section = id;
			unlisted->push_back( cellCoord );
		} else {
			cell->section = id;
			section->push_back( cellCoord );
		}
	}

	// recurse to adjacent cells
	this->_floodSectionRecurse( x, y + this->mapResolution, id, section, unlisted ); // UP
	this->_floodSectionRecurse( x, y - this->mapResolution, id, section, unlisted ); // DOWN
	this->_floodSectionRecurse( x - this->mapResolution, y, id, section, unlisted ); // LEFT
	this->_floodSectionRecurse( x + this->mapResolution, y, id, section, unlisted ); // RIGHT

	return 0;
}

int SimExplore::nextSearchCell( UUID *id ) {
	int i;
	CELLCOORD coord[4][3]; // store options for new cells
	int count[4][3];
	float dx, dy;
	int quad, range;
	int highQ, highR;
	float highC; // weighted count

	float dist;
	AVATAR *avatar;
	mapAvatar::iterator iterAv = this->avatars.find( *id );
	std::list<CELLCOORD>::iterator iter;

	if ( iterAv == this->avatars.end() ) {
		Log.log( 0, "SimExplore::nextSearchCell: avatar not found!" ); // avatar on break
		return 1;
	}
	avatar = &iterAv->second;

	memset( count, 0, sizeof(int)*4*3 );

	highC = 0;

	iter = avatar->cells.begin();
	while ( iter != avatar->cells.end() ) {
		dx = iter->x - avatar->search.x;
		dy = iter->y - avatar->search.y;
		if ( dx >= 0 ) {
			if ( dy >= 0 ) quad = 0;
			else quad = 1;
		} else {
			if ( dy >= 0 ) quad = 3;
			else quad = 2;
		}

		dist = max( fabs(dx), fabs(dy) );
		if ( dist == 0 ) {
			iter++;
			continue; // skip the old cell
		}

		if ( dist > 2 ) range = 2;
		else if ( dist > 1 ) range = 1;
		else range = 0;

		count[quad][range]++;
		if ( this->randGen->Uniform01() <= 1.0f/count[quad][range] ) // replace cell
				coord[quad][range] = *iter;

		if ( count[quad][range]/pow(3.0f,range) > highC ) {
			highC = count[quad][range]/pow(3.0f,range);
			highQ = quad;
			highR = range;
		}
		
		iter++;
	}

	if ( highC == 0 ) { // no cells!
		// nothing to do, but don't clear searchDirty
		return 0;
	} else {
		avatar->search = coord[highQ][highR];
		avatar->search.x += 0.5f*this->mapResolution;
		avatar->search.y += 0.5f*this->mapResolution;
	}

	// try to find a cell looking at the target
	CELL *cell;
	float lx, ly; // look pos
	float sn, cs; 
	float weight;
	float highX, highY, highA;
	float highW = 0;
	float A, sA = atan2( avatar->search.y - avatar->pfState[1], avatar->search.x - avatar->pfState[0] );
	for ( i=0; i<18; i++ ) {
		A = sA + i*2*fM_PI/18.0f;

		sn = sin( A );
		cs = cos( A );

		lx = floor((avatar->search.x/this->mapResolution) + 0.5f - 3*cs) * this->mapResolution;
		ly = floor((avatar->search.y/this->mapResolution) + 0.5f - 3*sn) * this->mapResolution;

		// get cell
		cell = this->getCell( lx, ly, false );
		if ( !cell
		  || cell->type == CT_OUTOFBOUNDS 
		  || cell->type == CT_UNREACHABLE
		  || cell->type == CT_OCCUPIED
		  || cell->occupancy < 0.45f ) {
			continue;
		} else {
			weight = (2 - fabs( A - sA )/fM_PI) * cell->occupancy;
			if ( weight > highW ) {
				highW = weight;
				highX = lx + 0.5f*this->mapResolution;
				highY = ly + 0.5f*this->mapResolution;
				highA = A;
			}
		}
	}

	// tell the avatar about its new target
	if ( highW == 0 ) {
		// we couldn't find a good cell, have the avatar drive in that direction but don't clean the search
		avatar->agent->setTargetPos( avatar->search.x, avatar->search.y, 0, 0, &this->uuid, NEW_MEMBER_CB(SimExplore,cbTargetPos), avatar->thread );

		Log.log(0, "SimExplore::nextSearchCell: avatar thread %d search %f %f, no safe look", avatar->thread, avatar->search.x, avatar->search.y );
		
		// visualize
		avatar->target.x = avatar->search.x;
		avatar->target.y = avatar->search.y;
	} else {
		avatar->agent->setTargetPos( highX, highY, highA, 1, &this->uuid, NEW_MEMBER_CB(SimExplore,cbTargetPos), avatar->thread );
		avatar->searchDirty = false;
		
		Log.log(0, "SimExplore::nextSearchCell: avatar thread %d search %f %f, look from %f %f", avatar->thread, avatar->search.x, avatar->search.y, highX, highY );

		// visualize
		avatar->target.x = highX;
		avatar->target.y = highY;
	}


	return 0;
}


int SimExplore::addAvatar( UUID *id, char *agentType, SimAvatar *agent ) {
	AVATAR avatar;

	Log.log( LOG_LEVEL_NORMAL, "SimExplore::addAvatar: adding avatar %s (%s)", 
		Log.formatUUID(LOG_LEVEL_NORMAL,id),
		agentType );

	strncpy( avatar.type, agentType, sizeof(avatar.type) );
	avatar.section = -1;
	avatar.searchDirty = true;

	avatar.agent = agent;
	avatar.thread = this->nextAvatarThread;

	this->avatars[*id] = avatar;

	this->avatarThreads[this->nextAvatarThread] = &this->avatars[*id];
	this->nextAvatarThread++;

	// add to visualization
	AVATAR *aPtr = &this->avatars[*id];
	int path = sim->getXPathId();
	float searchC[3] = { 0, 0, 1 };
	float targetC[3] = { 1, 1, 0 };
	aPtr->objR = 0;
	aPtr->objS = this->mapResolution;
	aPtr->searchObj = vis->newDynamicObject( &aPtr->search.x, &aPtr->search.y, &aPtr->objR, &aPtr->objS, path, searchC, 2 );
	aPtr->targetObj = vis->newDynamicObject( &aPtr->target.x, &aPtr->target.y, &aPtr->objR, &aPtr->objS, path, targetC, 2 );

	this->updatePartitions( true );

	return 0;
}

int SimExplore::avatarUpdateSection( UUID *id, float x, float y ) {
	mapAvatar::iterator iterA;
	AVATAR *avatar;
	CELL *cell;
	int newSection;

	iterA = this->avatars.find(*id);
	if ( iterA == this->avatars.end() ) {
		return 1; // not found
	}

	avatar = &iterA->second;

	cell = this->getCell( x, y );
	if ( !cell 
	  || cell->type == CT_OUTOFBOUNDS 
	  || cell->type == CT_UNREACHABLE
	  || cell->type == CT_OCCUPIED ) {
		newSection = -1;
	} else {
		newSection = cell->section;
	}

	if ( avatar->section == -1 ) {
		avatar->search.x = x;
		avatar->search.y = y;
	}

	avatar->section = newSection;
	
	return 0;
}

int SimExplore::updatePartitions( bool force ) {

	if ( !this->started )
		return 1;

	if ( !force && this->partitionUpdateTimer ) {
		_timeb tb;
		tb = this->simTime;
		int elapsed = (int)((tb.time - this->lastPartitionUpdate.time)*1000 + tb.millitm - this->lastPartitionUpdate.millitm);
			
		// make sure no update is in progress
		// check to see if enough time has passed since our last update
		if ( 0 // this->partitionUpdateInProgress 
		  || elapsed < SimExplore_UPDATE_RATE_MIN ) {
			this->removeTimeout( this->partitionUpdateTimer );
			this->partitionUpdateTimer = this->addTimeout( max( 100, SimExplore_UPDATE_RATE_MIN - elapsed ), NEW_MEMBER_CB(SimExplore,cbPartitionUpdateTimeout) );
			if ( !this->partitionUpdateTimer ) {
				return 1;
			}
			return 0;
		}
	}
		
	/*if ( this->partitionUpdateInProgress == SimExplore_MAX_SIMULTANEOUS_UPDATES ) {
		return 0; // something is probably wrong here!
	}*/

	// make sure there is something to update
	if ( this->regions.size() == 0 
	  || this->avatars.size() == 0
	  || this->sectionIds->size() == 0 ) {

		return 0;
	}

	if ( this->partitionUpdateTimer ) {
		this->removeTimeout( this->partitionUpdateTimer );
	}
	this->partitionUpdateTimer = this->addTimeout( SimExplore_UPDATE_RATE, NEW_MEMBER_CB(SimExplore,cbPartitionUpdateTimeout) );
	if ( !this->partitionUpdateTimer ) {
		return 1;
	}
	this->lastPartitionUpdate = this->simTime;

	// request map
	std::list<DDBRegion>::iterator iterR = this->dirtyMapRegions.begin();
	while ( iterR != this->dirtyMapRegions.end() ) {
		sim->ddbPOGGetRegion( &this->mapId, iterR->x, iterR->y, iterR->w, iterR->h, &this->ds );
		this->ds.rewind();
		this->ds.unpackInt32(); // thread
		if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
			float x, y, w, h, res;
			int cw, ch;

			x = this->ds.unpackFloat32();
			y = this->ds.unpackFloat32();
			w = this->ds.unpackFloat32();
			h = this->ds.unpackFloat32();
			res = this->ds.unpackFloat32();
			
			cw = (int)floor( w / res );
			ch = (int)floor( h / res );

			this->updateMap( x, y, w, h, (float *)this->ds.unpackData( cw*ch*4 ) );
		} else {
			return 1;
		}

		iterR++;
	}
	this->dirtyMapRegions.clear();
	
	// request avatar positions
	mapAvatar::iterator iterA = this->avatars.begin();
	while ( iterA != this->avatars.end() ) {
		sim->ddbPFGetInfo( (UUID *)&iterA->first, DDBPFINFO_CURRENT, &this->lastPartitionUpdate, &this->ds );
		this->ds.rewind();
		this->ds.unpackInt32(); // thread
		
		if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
			_timeb *tb;
			UUID uuid;
			CELL *cell;
			uuid = iterA->first;

			if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
				return 1; // what happened here?
			}
			
			tb = (_timeb *)this->ds.unpackData( sizeof(_timeb) );
			memcpy( this->avatars[uuid].pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );

			// dirty avatar section
			cell = this->getCell( this->avatars[uuid].pfState[0], this->avatars[uuid].pfState[1], false );
			if ( !cell 
			  || cell->type == CT_OUTOFBOUNDS 
			  || cell->type == CT_OCCUPIED ) {
				// nothing to do
			} else {
				this->sectionDirty[cell->section] = true;
			}

		} else {
			return 1;
		}

		iterA++;
	}	

	// all info is in, update partitions
	this->_updatePartitions();

	return 0;
}

int SimExplore::_updatePartitions() {
	int newSection, searchSection;
	CELL *cell;
	CELLCOORD *cellCoord;
	mapAvatar::iterator iterA;
	AVATAR *avatar;
	std::list<int>::iterator iterS, iterSTemp;
	int sId, newId; // section ids
	std::list<int> *newSections = new std::list<int>;
	std::list<CELLCOORD> unlisted;
	std::list<CELLCOORD>::iterator iterCC;

	bool repartitioned = false;
	
	// this is where I should start working
	// when should this function be called?

	// update sections
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		sId = *iterS;
		if ( this->sectionDirty[sId] ) {
			while ( !this->sections[sId]->empty() ) {
				cellCoord = &(*this->sections[sId]->begin());
				
				// make sure the seed cell hasn't been claimed by another section!
				cell = getCell( cellCoord->x, cellCoord->y );
				if ( !cell || cell->section != sId ) {
					this->sections[sId]->pop_front();
					continue;
				}

				unlisted.clear();
				newId = this->floodSection( cellCoord->x, cellCoord->y, &unlisted );
				if ( newId == -1 ) {
					return 1; // ran out of ids!?
				}
				
				if ( this->sections[newId]->empty() ) { // seed cell didn't grow, try again
					delete this->sections[newId];
					this->sections[newId] = NULL;

					this->sections[sId]->pop_front();
				} else if ( this->sections[newId]->size() + unlisted.size() != this->sections[sId]->size() ) { // there are still unaccounted for cells in the old section
					newSections->push_back( newId );

					std::list<CELLCOORD> *remainder = new std::list<CELLCOORD>;

					iterCC = this->sections[sId]->begin();
					while ( iterCC != this->sections[sId]->end() ) {
						cell = this->getCell( iterCC->x, iterCC->y );
						if ( cell->section == sId )
							remainder->push_back( *iterCC );
						iterCC++;
					}

					delete this->sections[sId];
					this->sections[sId] = remainder;
				} else { // all cells are accounted for
					newSections->push_back( newId );
					this->sections[sId]->clear();
				}
			}
			delete this->sections[sId];
			this->sections[sId] = NULL;
			iterSTemp = iterS;
			iterS++;
			this->sectionIds->erase( iterSTemp );
		} else {
			iterS++;
		}
	}
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) { // we need to loop through again to collect clean sections instead of doing it in the first loop in case any became dirty and got moved to the end of the list for reprocessing
		newSections->push_back( *iterS );
		iterS++;
	}
	delete this->sectionIds;
	this->sectionIds = newSections;
	
	// update avatar sections
	iterA = this->avatars.begin();
	while ( iterA != this->avatars.end() ) {
		avatar = &iterA->second;
		// get pos cell
		cell = this->getCell( avatar->pfState[0], avatar->pfState[1], false );
		if ( !cell 
		  || cell->type == CT_OUTOFBOUNDS 
		  || cell->type == CT_OCCUPIED ) {
			newSection = -1;
		} else {
			newSection = cell->section;
		}

		if ( avatar->section != -1 ) { // we already had a section
			// get search cell
			cell = this->getCell( avatar->search.x, avatar->search.y, false );
			if ( !cell 
			  || cell->type == CT_OUTOFBOUNDS 
			  || cell->type == CT_UNREACHABLE
			  || cell->type == CT_OCCUPIED ) {
				searchSection = -1;
			} else {
				searchSection = cell->section;
			}

			if ( searchSection != newSection ) {
				this->sectionDirty[newSection] = true; // make sure we repartition this section
				avatar->searchDirty = true;
				avatar->search.x = avatar->pfState[0];
				avatar->search.y = avatar->pfState[1];
			}
		} else if ( newSection != -1 ) { // we didn't have a section but now we do
			this->sectionDirty[newSection] = true; // make sure we repartition this section
			avatar->searchDirty = true;
			avatar->search.x = avatar->pfState[0];
			avatar->search.y = avatar->pfState[1];
		} else {
			// we don't have a section, move to a clear area
			// TODO
		}

		avatar->section = newSection;

		iterA++;
	}

	// update partitions
	int numAvatars;
	int cellsRemaining = 0;
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		sId = *iterS;

		// get the number of avatars in this section
		iterA = this->avatars.begin();
		numAvatars = 0;
		while ( iterA != this->avatars.end() ) {
			if ( iterA->second.section == sId ) {	
				numAvatars++;
			}
			iterA++;
		}

		if ( numAvatars > 0 ) {
			cellsRemaining += (int)this->sections[sId]->size();
		}

		if ( this->sectionDirty[sId] ) {
			this->calculatePartitions( sId );
			this->sectionDirty[sId] = false;
			repartitioned = true;
		}
		iterS++;
	}

	// check our search threshold
	if ( cellsRemaining / (float)this->totalCells < 0.1f ) {
		// we're done!
		// TODO
		sim->TogglePause();
	} else {
		// update search cells
		iterA = this->avatars.begin();
		while ( iterA != this->avatars.end() ) {
			avatar = &iterA->second;

			// get search cell
			cell = this->getCell( avatar->search.x, avatar->search.y, false );

			if ( avatar->searchDirty ) {
				this->nextSearchCell( (UUID *)&iterA->first );
			} else if ( !cell 
			  || cell->type != CT_UNKNOWN ) {
				// cell is not what it should be, get new cell
				this->nextSearchCell( (UUID *)&iterA->first );
			} else if ( cell->section != avatar->section || cell->partition != avatar->partition ) {
				// cell no longer belongs to us, get new cell
				this->nextSearchCell( (UUID *)&iterA->first );
			}

			iterA++;
		}
	}

	if ( repartitioned )
		this->visualizeSections();

	return 0;
}

int SimExplore::calculatePartitions( unsigned char sectionId ) {
	int i, j, partition, step;
	int n; // number of avatars/patitions
	int m; // number of cells
	
	// get the section
	std::list<CELLCOORD> *section = this->sections[sectionId];
	std::list<CELLCOORD>::iterator iterC;
	m = (int)section->size();

	// get the number of avatars in this section
	mapAvatar::iterator iterAv = this->avatars.begin();
	n = 0;
	while ( iterAv != this->avatars.end() ) {
		if ( iterAv->second.section == sectionId ) {
			iterAv->second.partition = n;		
			n++;
		}
		iterAv++;
	}

	if ( n == 0 ) { // no avatars!
		return 0;
	}

	if ( n == 1 ) { // one avatar, our job is easy
		// find the avatar
		iterAv = this->avatars.begin();
		while ( iterAv != this->avatars.end() ) {
			if ( iterAv->second.section == sectionId )
				break;
			iterAv++;
		}

		iterAv->second.cells.clear();

		// iterate through cells and assign partitions
		iterC = section->begin();
		for ( j=0; j<m; j++, iterC++ ) {
			partition = 0;

			getCell( iterC->x, iterC->y )->partition = partition;

			// add to avatar's list
			iterAv->second.cells.push_back( *iterC );
		}
		return 0;
	}

	// prepare arrays
	int requiredSize;
	
	float *mu, *var, *mix, *nmu, *nvar;
	float *norm, *invVarVar2;
	requiredSize = (2*n + 2*n + n + 2*n + 2*n + 2*n + 2*n) * sizeof(float);
	if ( this->sharedArrayGaussSize < requiredSize ) {
		if ( this->sharedArrayGauss )
			free( this->sharedArrayGauss );
		this->sharedArrayGaussSize = 0;

		this->sharedArrayGauss = (float *)malloc( requiredSize );
		if ( !this->sharedArrayGauss ) {
			return 1;
		}
		this->sharedArrayGaussSize = requiredSize;
	}
	mu = this->sharedArrayGauss;
	var = mu + 2*n;
	mix = var + 2*n;
	nmu = mix + n;
	nvar = nmu + 2*n;
	norm = nvar + 2*n;
	invVarVar2 = norm + 2*n;

	float px;
	float *pxi, *pix, *sumPix, *split, *nsplit;
	requiredSize = (n + n*m + n + n + n) * sizeof(float);
	if ( this->sharedArrayPixSize < requiredSize ) {
		if ( this->sharedArrayPix )
			free( this->sharedArrayPix );
		this->sharedArrayPixSize = 0;

		this->sharedArrayPix = (float *)malloc( requiredSize );
		if ( !this->sharedArrayPix ) {
			return 1;
		}
		this->sharedArrayPixSize = requiredSize;
	}
	pxi = this->sharedArrayPix;
	pix = pxi + n;
	sumPix = pix + n*m;
	split = sumPix + n;
	nsplit = split + n;

	std::list<CELLCOORD> **cells = (std::list<CELLCOORD> **)malloc( n * sizeof(std::list<CELLCOORD> *) );
	if ( !cells ) {
		return 1;
	}

	float change, sizeDif, tarSize, val;
	tarSize = m/(float)n; // target partition size

	// initialize distributions
	iterAv = this->avatars.begin();
	i = 0;
	while ( iterAv != this->avatars.end() ) {
		if ( iterAv->second.section == sectionId ) {
			mu[2*i] = iterAv->second.pfState[0];
			mu[2*i+1] = iterAv->second.pfState[1];
			var[2*i] = 0.5f;
			var[2*i+1] = 0.5f;
			mix[i] = 0.5f;

			iterAv->second.cells.clear();
			cells[i] = &iterAv->second.cells;
			i++;
		}
		iterAv++;
	}

	memset( split, 0, sizeof(float)*n );

	for ( step = 0; step<500; step++ ) {
		// -- E Step --
		// prepare distribution constants
		for ( i=0; i<2*n; i++ ) {
			norm[i] = 1/(sqrt(2*fM_PI)*var[i]);
			invVarVar2[i] = 1/(2*var[i]*var[i]);
		}

		memset( nsplit, 0, sizeof(float)*n );
		memset( nmu, 0, sizeof(float)*2*n );
		memset( nvar, 0, sizeof(float)*2*n );

		// iterate through cells
		iterC = section->begin();
		for ( j=0; j<m; j++, iterC++ ) {
			// calculate pxi
			px = 0;
			for ( i=0; i<n; i++ ) {
				pxi[i] = norm[2*i] * exp( -(mu[2*i] - iterC->x)*(mu[2*i] - iterC->x) * invVarVar2[2*i] )
				       * norm[2*i+1] * exp( -(mu[2*i+1] - iterC->y)*(mu[2*i+1] - iterC->y) * invVarVar2[2*i+1] );
				px += pxi[i]*mix[i];
			}

			// calculate pix
			for ( i=0; i<n; i++ ) {
				pix[i+j*n] = mix[i] * pxi[i] / px;
				nsplit[i] += pix[i+j*n];
				nmu[2*i] += pix[i+j*n] * iterC->x;		// sum up the numerator first
				nmu[2*i+1] += pix[i+j*n] * iterC->y;
			}
		}

		// check improvement
		change = 0;
		sizeDif = 0;
		for ( i=0; i<n; i++ ) {
			change += (split[i] - nsplit[i])*(split[i] - nsplit[i]);
			sizeDif += fabs(nsplit[i] - tarSize);
		}
		if ( change < 0.001f ) {
			break; // wasn't much improvement
		}
		sizeDif /= n;
		if ( sizeDif < max( 0.5f, m*0.01f ) ) {
			break; // we're close enough
		}
		memcpy( split, nsplit, sizeof(float)*n );

		// -- M Step --
		for ( i=0; i<n; i++ ) {
			// a) new mix
			mix[i] = mix[i]*(split[i]>tarSize ? 0.9f : 1.5f );

			// b) new mu
			nmu[2*i] /= split[i];
			nmu[2*i+1] /= split[i];
			mu[2*i] = (mu[2*i] + nmu[2*i]) * 0.5f;
			mu[2*i+1] = (mu[2*i+1] + nmu[2*i+1]) * 0.5f;
		}

		// c) new var
		// iterate through cells
		iterC = section->begin();
		for ( j=0; j<m; j++, iterC++ ) {
			// iterate through partition
			for ( i=0; i<n; i++ ) {
				val = (iterC->x - mu[2*i]);
				nvar[2*i] += pix[i+j*n] * val*val;		// sum up the numerator first
				
				val = (iterC->y - mu[2*i+1]);
				nvar[2*i+1] += pix[i+j*n] * val*val;
			}
		}
		
		for ( i=0; i<n; i++ ) {
			nvar[2*i] = sqrt(nvar[2*i] / split[i]);
			nvar[2*i+1] = sqrt(nvar[2*i+1] / split[i]);
			var[2*i] = (var[2*i] + nvar[2*i]) * 0.5f;
			var[2*i+1] = (var[2*i+1] + nvar[2*i+1]) * 0.5f;
		}
	}

	// iterate through cells and assign partitions
	iterC = section->begin();
	for ( j=0; j<m; j++, iterC++ ) {
		// iterate through partitions to find the max
		val = 0;
		partition = 0;
		for ( i=0; i<n; i++ ) {
			if ( val < pix[i+j*n] ) {
				val = pix[i+j*n];
				partition = i;
			}
		}

		getCell( iterC->x, iterC->y )->partition = partition;

		// add to avatar's list
		cells[partition]->push_back( *iterC );
	}

	// free arrays
	free( cells );

	return 0;
}

int SimExplore::notifyMap( UUID *item, char evt, char *data, int len ) {
	DDBRegion newRegion;
	DDBRegion activeR;

	if ( evt != DDBE_POG_UPDATE && evt != DDBE_POG_LOADREGION ) {
		return 0; // nothing to do
	}

	newRegion.x = ((float *)data)[0];
	newRegion.y = ((float *)data)[1];
	newRegion.w = ((float *)data)[2];
	newRegion.h = ((float *)data)[3];

	// trim area to active regions
	mapRegions::iterator iter = this->regions.begin();
	while ( iter != this->regions.end() ) {
		activeR.x = max( newRegion.x, iter->second.x );
		activeR.w = min( newRegion.x + newRegion.w, iter->second.x + iter->second.w ) - activeR.x;
		activeR.y = max( newRegion.y, iter->second.y );
		activeR.h = min( newRegion.y + newRegion.h, iter->second.y + iter->second.h ) - activeR.y;

		if ( activeR.w > 0 && activeR.h > 0 ) {
			// dirty sections
			if ( this->dirtyRegion( &activeR ) ) { // we have some active sections in this region
				// dirty map
				this->dirtyMap( &activeR );
			}
		}

		iter++;
	}
	
	// see if we should update sections now
	// TODO

	return 0;
}

int SimExplore::dirtyRegion( DDBRegion *region ) {
	float x, y, w, h;
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int tileId, lastTileId;
	CELL *tile;
	mapTile::iterator iterTile;

	// log if we actually dirtied any sections
	int sectionDirtied = 0;

	// convert coords and tileSize into cell space
	x = floor(region->x/this->mapResolution);
	y = floor(region->y/this->mapResolution);
	w = ceil((region->x+region->w)/this->mapResolution) - x;
	h = ceil((region->y+region->h)/this->mapResolution) - y;
	tileSize = round(this->mapTileSize/this->mapResolution);

	lastTileId = -1;
	for ( c=0 ; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);
	
			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tiles.find(tileId);
				if ( iterTile == this->tiles.end() ) {
					tile = NULL; // this tile doesn't exist
				} else {
					tile = iterTile->second;
				}

				lastTileId = tileId;
			}

			if ( tile && tile[j+i*this->mapStride].type == CT_UNKNOWN && tile[j+i*this->mapStride].section != -1 ) {
				this->sectionDirty[ tile[j+i*this->mapStride].section ] = true;
				sectionDirtied = 1;
			}
		}
	}

	return sectionDirtied;
}

int SimExplore::dirtyMap( DDBRegion *region ) {
	DDBRegion reg;

	reg.x = floor(region->x/this->mapResolution)*this->mapResolution;
	reg.y = floor(region->y/this->mapResolution)*this->mapResolution;
	reg.w = ceil((region->x + region->w)/this->mapResolution)*this->mapResolution - reg.x;
	reg.h = ceil((region->y + region->h)/this->mapResolution)*this->mapResolution - reg.y;

	this->_dirtyMap( &reg, this->dirtyMapRegions.begin(), this->dirtyMapRegions.end() );
	
	// delete any regions that are flagged
	std::list<DDBRegion>::iterator iterLast;
	std::list<DDBRegion>::iterator iter = this->dirtyMapRegions.begin();
	while ( iter != this->dirtyMapRegions.end() ) {
		iterLast = iter;
		iter++;
		if ( iterLast->w == 0 )
			this->dirtyMapRegions.erase( iterLast );
	}

	return 0;
}
int SimExplore::_dirtyMap( DDBRegion *region, std::list<DDBRegion>::iterator iter, std::list<DDBRegion>::iterator insertAt ) {
	DDBRegion cutL, cutT, cutB, cutR, grow;
	float Asx, Aex, Asy, Aey;
	float Bsx, Bex, Bsy, Bey;

	Bsx = region->x;
	Bex = region->x + region->w;
	Bsy = region->y;
	Bey = region->y + region->h;

	// see if we overlap with a current region
	while ( iter != this->dirtyMapRegions.end() ) {
		if ( iter->w == 0 ) {
			iter++;
			continue; // must be flaged for deletion
		}

		Asx = iter->x;
		Aex = iter->x + iter->w;
		Asy = iter->y;
		Aey = iter->y + iter->h;

		// check if corner of A is inside B
		if ( Asx >= Bsx && Asx < Bex
		  && Asy >= Bsy && Asy < Bey )
			break;
		// check if corner of B is inside A
		if ( Bsx >= Asx && Bsx < Aex
		  && Bsy >= Asy && Bsy < Aey )
			break;
		// check if horizontal edges intersect vertical edges
		if ( Asy >= Bsy && Asy < Bey 
		  && ( (Bsx >= Asx && Bsx < Aex) 
		    || (Bex >= Asx && Bex < Aex) ) )
			break;
		if ( Aey >= Bsy && Aey < Bey 
		  && ( (Bsx >= Asx && Bsx < Aex) 
		    || (Bex >= Asx && Bex < Aex) ) )
			break;
		// check if vertical edges intersect horizontal edges
		if ( Asx >= Bsx && Asx < Bex 
		  && ( (Bsy >= Asy && Bsy < Aey) 
		    || (Bey >= Asy && Bey < Aey) ) )
			break;
		if ( Aex >= Bsx && Aex < Bex 
		  && ( (Bsy >= Asy && Bsy < Aey) 
		    || (Bey >= Asy && Bey < Aey) ) )
			break;

		iter++;
	}

	if ( iter == this->dirtyMapRegions.end() ) { // no overlap, add the region
		if ( insertAt != this->dirtyMapRegions.end() )
			this->dirtyMapRegions.insert( insertAt, *region );
		else
			this->dirtyMapRegions.push_back( *region );
		return 0;
	}

	// we overlap, figure out grows and cuts
	grow.x = min( Asx, Bsx );
	grow.w = max( Aex, Bex ) - grow.x;
	grow.y = min( Asy, Bsy );
	grow.h = max( Aey, Bey ) - grow.y;

	if ( grow.x == iter->x && grow.y == iter->y && grow.w == iter->w && grow.h == iter->h ) {
		return 0; // we're entirely contained, nothing to do
	}

	if ( Bsx < Asx ) {	
		cutL.x = Bsx;
		cutL.w = Asx - Bsx;
		cutL.y = region->y;
		cutL.h = region->h;
	} else {
		cutL.w = cutL.h = 0;
	}
	if ( Bsy < Asy ) {
		cutB.x = max( Asx, Bsx );
		cutB.w = min( Aex, Bex ) - cutB.x;
		cutB.y = Bsy;
		cutB.h = Asy - Bsy;
	} else {
		cutB.w = cutB.h = 0;
	}
	if ( Aey < Bey ) {
		cutT.x = max( Asx, Bsx );
		cutT.w = min( Aex, Bex ) - cutT.x;
		cutT.y = Aey;
		cutT.h = Bey - Aey;
	} else {
		cutT.w = cutT.h = 0;
	}
	if ( Aex < Bex ) {
		cutR.x = Aex;
		cutR.w = Bex - Aex;
		cutR.y = region->y;
		cutR.h = region->h;
	} else {
		cutR.w = cutR.h = 0;
	}

	// decide to grow or cut
	// NOTE: make sure cuts are inserted before the next iter so that they don't try to overlap each other
	// unless they grow, in which case they should be inserted at the end
	float cutSize = cutL.w*cutL.h + cutB.w*cutB.h + cutT.w*cutT.h + cutR.w*cutR.h;
	if ( grow.w*grow.h - cutSize - iter->w*iter->h <= cutSize ) { // empty space < cut sections, so just grow
		iter->w = 0; // flag for deletion
		this->_dirtyMap( &grow, this->dirtyMapRegions.begin(), this->dirtyMapRegions.end() );
	} else { // add all cuts individually
		iter++; // start at the next region
		if ( cutL.w && cutL.h )
			this->_dirtyMap( &cutL, iter, iter );
		if ( cutB.w && cutB.h )
			this->_dirtyMap( &cutB, iter, iter );
		if ( cutT.w && cutT.h )
			this->_dirtyMap( &cutT, iter, iter );
		if ( cutR.w && cutR.h )
			this->_dirtyMap( &cutR, iter, iter );
	}

	return 0;
}

int SimExplore::visualizeSections() {
	float colour[10][3] = { { 1, 0, 0 }, 
							{ 0, 1, 0 }, 
							{ 0, 0, 1 }, 
							{ 1, 1, 0 }, 
							{ 1, 0, 1 }, 
							{ 0, 1, 1 }, 
							{ 1, 1, 1 }, 
							{ 0, 0, 0 }, 
							{ 0.5f, 0, 0 }, 
							{ 0, 0.5f, 0 } 
							};

	std::map<char,int> partitionColours;
	std::map<char,int>::iterator itClr;
	int colourInd;
	int nextColourInd = 0;

	// clean up old stuff
	std::list<int>::iterator itC;
	for ( itC = this->cellVisualization.begin(); itC != this->cellVisualization.end(); itC++ ) {
		vis->deleteObject( *itC, true );
	}
	this->cellVisualization.clear();

	// iterate through sections
	CELL *cell;
	std::list<CELLCOORD> *section;
	std::list<int>::iterator itS;
	std::list<CELLCOORD>::iterator itP;
	for ( itS = this->sectionIds->begin(); itS != this->sectionIds->end(); itS++ ) {
		section = this->sections[*itS];

		partitionColours.clear();

		// iterate through partitions
		for ( itP = section->begin(); itP != section->end(); itP++ ) {
			cell = this->getCell( itP->x, itP->y );
			itClr = partitionColours.find( cell->partition );
			if ( itClr == partitionColours.end() ) {
				colourInd = nextColourInd % 10;
				partitionColours[cell->partition] = colourInd;
				nextColourInd++;
			} else {
				colourInd = itClr->second;
			}
			// vis cell
			this->cellVisualization.push_back( vis->newStaticObject( itP->x, itP->y, 0, this->mapResolution, this->cellPath, colour[colourInd], 1 ) );
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool SimExplore::cbPartitionUpdateTimeout( void *NA ) {
	this->updatePartitions();
	return 0;
}

bool SimExplore::cbTargetPos( void *pDS ) {
	DataStream *ds = (DataStream *)pDS;

	int thread = ds->unpackInt32();
	bool success = ds->unpackBool();

	int reason;
	if ( !success ) {
		reason = ds->unpackInt32();
		Log.log(0, "SimExplore::cbTargetPos: avatar thread %d target fail %d", thread, reason );
		if ( reason == SimAvatar::TP_NEW_TARGET ) // we gave them a new target, no reason to do anything
			return 0;
	} else {
		Log.log(0, "SimExplore::cbTargetPos: avatar thread %d target success", thread );
	}

	// find avatar
	AVATAR *avatar;
	avatar = this->avatarThreads[thread];
	avatar->searchDirty = true;

	this->updatePartitions( true );

	return 0;
}