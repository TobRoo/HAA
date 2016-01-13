// SimCooccupancy.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimCooccupancy.h"

#include <boost/math/special_functions/erf.hpp>

#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

//*****************************************************************************
// SimCooccupancy

//-----------------------------------------------------------------------------
// Constructor	
SimCooccupancy::SimCooccupancy( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {
	this->nextReadingId = 0;

	this->mapDataSize = 0;
	this->mapData = NULL;
}

//-----------------------------------------------------------------------------
// Destructor
SimCooccupancy::~SimCooccupancy() {

	if ( this->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SimCooccupancy::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimCooccupancy %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimCooccupancy" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimCooccupancy::start() {

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->mapUpdate = NewImage( 64, 64 );

	this->sharedArraySize = 0;
	this->obsDensity = NULL;
	
	this->started = true;

	return 0;
}

int SimCooccupancy::setMap( UUID *id ) {

	this->mapId = *id;

	// get map resolution
	sim->ddbPOGGetInfo( id, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	if ( this->ds.unpackChar() == DDBR_OK ) {
		this->ds.unpackFloat32(); // tilesize
		this->mapResolution = this->ds.unpackFloat32();
		this->ds.unpackInt32(); // stride
	} else {
		UuidCreateNil( &this->mapId );
		return 1;
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SimCooccupancy::stop() {

	// clean up buffers
	if ( this->mapDataSize )
		free( this->mapData );

	FreeImage( this->mapUpdate );

	if ( this->sharedArraySize ) {
		free( this->obsDensity );
		this->sharedArraySize = 0;
	}

	return SimAgentBase::stop();
}


//-----------------------------------------------------------------------------
// Processing

int SimCooccupancy::addReading( UUID *avatar, _timeb *tb ) {

	// make sure we have the radius
	mapRadius::iterator iterR = this->avatarRadius.find( *avatar );
	if ( iterR == this->avatarRadius.end() ) {
		// get radius
		
		// TEMP for now just assume a radius of 0.15 m
		this->avatarRadius[*avatar] = 0.15f;

		/*sim->ddbSensorGetInfo( sensor, DDBSENSORINFO_AVATAR | DDBSENSORINFO_POSE, &this->ds );

		this->ds.rewind();
		this->ds.unpackInt32(); // discard thread
		if ( this->ds.unpackChar() == DDBR_OK ) {
			this->ds.unpackInt32(); // discard infoFlags
			this->ds.unpackUUID( &avatar );
			this->sensorAvatar[*sensor] = avatar;
			this->ds.unpackInt32(); // discard poseSize
			pose = *(SonarPose *)this->ds.unpackData( sizeof(SonarPose) );
			this->sensorPose[*sensor] = pose;
		} else {
			return 1;
		} */

	}

	// get PF
	PROCESS_READING *pr = &this->readings[this->nextReadingId];
	pr->id = this->nextReadingId;
	this->nextReadingId++;
	pr->avatar = *avatar;
	pr->time = *tb;
	pr->pfState = NULL;
	pr->pfWeight = NULL;
	pr->retries = 0;

	this->cbRequestPFInfo( &pr->id ); 

	return 0;
}

int SimCooccupancy::processReading( PROCESS_READING *pr ) {
	
	int mapDivisions = (int)floor(1/this->mapResolution);

	int border;
	int p;
	int r, c;
	float stateX, stateY;

	float PFbound[4];
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	int   mapUpdateSize[2]; // width,height (cols,rows)
	float minObsDensity; 

	float radius; // avatar radius
	
	// generate particle filter and map updates
	// update PFbound
	// TODO ignore particles with very little weight?
	PFbound[0] = pfStateX(pr,0);
	PFbound[1] = pfStateY(pr,0);
	PFbound[2] = 0;
	PFbound[3] = 0;
	for ( p=1; p<pr->pfNum; p++ ) {
		if ( PFbound[0] > pfStateX(pr,p) ) {
			PFbound[2] = PFbound[0] + PFbound[2] - pfStateX(pr,p);
			PFbound[0] = pfStateX(pr,p);
		} else if ( PFbound[0] + PFbound[2] < pfStateX(pr,p) ) {
			PFbound[2] = pfStateX(pr,p) - PFbound[0];
		}
		if ( PFbound[1] > pfStateY(pr,p) ) {
			PFbound[3] = PFbound[1] + PFbound[3] - pfStateY(pr,p);
			PFbound[1] = pfStateY(pr,p);
		} else if ( PFbound[1] + PFbound[3] < pfStateY(pr,p) ) {
			PFbound[3] = pfStateY(pr,p) - PFbound[1];
		}
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*mapDivisions)/mapDivisions;
	PFbound[0] = floor(PFbound[0]*mapDivisions)/mapDivisions;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*mapDivisions)/mapDivisions;
	PFbound[1] = floor(PFbound[1]*mapDivisions)/mapDivisions;
	PFbound[3] -= PFbound[1];

	radius = this->avatarRadius[pr->avatar];
	
	// prepare map update
	border = (int)ceil(mapDivisions*radius);
	mapUpdateLoc[0] = PFbound[0] - border/(float)mapDivisions;
	mapUpdateLoc[1] = PFbound[1] - border/(float)mapDivisions;
	mapUpdateSize[0] = (int)(PFbound[2]*mapDivisions)+border*2;
	mapUpdateSize[1] = (int)(PFbound[3]*mapDivisions)+border*2;
	if ( mapUpdate->bufSize < (int)sizeof(float)*(mapUpdateSize[0] * mapUpdateSize[1]) ) {
		// allocate more space for the map update
		ReallocImage( mapUpdate, mapUpdateSize[1], mapUpdateSize[0] );
	} else {
		mapUpdate->rows = mapUpdateSize[1];
		mapUpdate->cols = mapUpdateSize[0];
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	float mapX = mapUpdateLoc[0];
	float mapY = mapUpdateLoc[1];

	// get map
	sim->ddbPOGGetRegion( &this->mapId, mapUpdateLoc[0], mapUpdateLoc[1], mapUpdateSize[0]*this->mapResolution, mapUpdateSize[1]*this->mapResolution, &this->ds );
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

		// make sure mapData is big enough
		if ( cw*ch*4 > this->mapDataSize ) {
			if ( this->mapData )
				free( this->mapData );
			this->mapData = (float *)malloc(cw*ch*4);
			if ( !this->mapData ) {
				this->mapDataSize = 0;			
				this->finishReading( pr, 0 );
				return 1;
			}
			this->mapDataSize = cw*ch*4;
		}

		memcpy( this->mapData, this->ds.unpackData( cw*ch*4 ), cw*ch*4 );
		this->mapHeight = ch;
	} else {
		this->finishReading( pr, 0 );
		return 0;
	}

	// make sure arrays are big enough
	if ( this->sharedArraySize < pr->pfNum ) {
		if ( this->sharedArraySize ) {
			free( this->obsDensity );
		}
		this->sharedArraySize = pr->pfNum;
		this->obsDensity = (float *)malloc( sizeof(float)*this->sharedArraySize );
		if ( !this->obsDensity ) {
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
	}
	
	float sigma = radius/3;
	float invSqrt2SigSq = 1 / sqrt(2*sigma*sigma);
	float CcdfA, CcdfB;
	float RcdfA, RcdfB;
	float vacancy;

	// for each particle
	minObsDensity = 0;
	for ( p=0; p<pr->pfNum; p++ ) {
		stateX = pfStateX(pr,p);
		stateY = pfStateY(pr,p);

		// iterate through cells, calculate vacancy and compare against map compare against map
		obsDensity[p] = 0;
		CcdfB = 0.5f * ( 1 + boost::math::erf( (mapX - stateX) * invSqrt2SigSq ));
		for ( c=0; c<mapUpdate->cols; c++ ) {
			CcdfA = CcdfB;
			CcdfB = 0.5f * ( 1 + boost::math::erf( (mapX + (c+1)*mapResolution - stateX) * invSqrt2SigSq ));
			
			RcdfB = 0.5f * ( 1 + boost::math::erf( (mapY - stateY) * invSqrt2SigSq ));
			for ( r=0; r<mapUpdate->rows; r++ ) {
				RcdfA = RcdfB;
				RcdfB = 0.5f * ( 1 + boost::math::erf( (mapY + (r+1)*mapResolution - stateY) * invSqrt2SigSq ));

				vacancy = (CcdfB - CcdfA)/mapResolution * (RcdfB - RcdfA)/mapResolution;

				Px(mapUpdate,r,c) += pr->pfWeight[p] * vacancy * 0.49f * SimCooccupancy_MAP_UPDATE_STRENGTH;

				// total penalty
				if ( this->mapData[r+c*this->mapHeight] < 0.5f ) {
					obsDensity[p] -= vacancy * (0.5f - this->mapData[r+c*this->mapHeight]);
				}
			}
		}

		minObsDensity = min( minObsDensity, obsDensity[p] );
	}

	// compute observation density
	if ( minObsDensity < 0 ) {
		for ( p=0; p<pr->pfNum; p++ ) { // shift observation densities positive and add ambient
			obsDensity[p] += -minObsDensity + SimCooccupancy_OBS_DENSITY_AMBIENT;
		}

		// submit particle filter update
		sim->ddbApplyPFCorrection( &pr->avatar, &pr->time, obsDensity );
	}

	// submit map update
	sim->ddbApplyPOGUpdate( &this->mapId, mapUpdateLoc[0], mapUpdateLoc[1], mapUpdateSize[0]*this->mapResolution, mapUpdateSize[1]*this->mapResolution, this->mapUpdate->data );

	// finish reading
	this->finishReading( pr, 1 );
	
	return 0;
}


int SimCooccupancy::finishReading( PROCESS_READING *pr, bool success ) {

	if ( pr->pfState )
		free( pr->pfState );
	if ( pr->pfWeight )
		free( pr->pfWeight );

	this->readings.erase( pr->id );

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool SimCooccupancy::cbRequestPFInfo( void *vpReadingId ) {
	int readingId = *(int *)vpReadingId;
	PROCESS_READING *pr;
	int infoFlags;
	char response;

	pr = &this->readings[readingId];

	sim->ddbPFGetInfo( &pr->avatar, DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT, &pr->time, &this->ds );
	this->ds.rewind();

	this->ds.unpackInt32(); // thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT) ) {
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		pr->pfNum = this->ds.unpackInt32();
		pr->pfStateSize = this->ds.unpackInt32();
		pr->pfState = (float *)malloc( pr->pfNum*pr->pfStateSize*sizeof(float) );
		if ( pr->pfState == NULL ) {
			this->finishReading( pr, 0 );
			return 0;
		}
		pr->pfWeight = (float *)malloc( pr->pfNum*sizeof(float) );
		if ( pr->pfWeight == NULL ) {
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( pr->pfState, this->ds.unpackData( pr->pfNum*pr->pfStateSize*sizeof(float) ), pr->pfNum*pr->pfStateSize*sizeof(float) );
		memcpy( pr->pfWeight, this->ds.unpackData( pr->pfNum*sizeof(float) ), pr->pfNum*sizeof(float) );

		this->processReading( pr );

	} else if ( response == DDBR_TOOLATE )  {
		// wait 250 ms and try again
		pr->retries++;
		if ( pr->retries < 10 )
			this->addTimeout( SimCooccupancy_REPEAT_RPFINFO_PERIOD, NEW_MEMBER_CB(SimCooccupancy, cbRequestPFInfo), &readingId, sizeof(int) );
		else
			this->finishReading( pr, 0 );
	} else {
		this->finishReading( pr, 0 );
	}

	return 0;
}