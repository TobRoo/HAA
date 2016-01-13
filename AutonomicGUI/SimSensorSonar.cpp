// SimSensorSonar.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimSensorSonar.h"

#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

//*****************************************************************************
// SimSensorSonar

//-----------------------------------------------------------------------------
// Constructor	
SimSensorSonar::SimSensorSonar( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {
	this->nextReadingId = 0;

	this->mapDataSize = 0;
	this->mapData = NULL;
}

//-----------------------------------------------------------------------------
// Destructor
SimSensorSonar::~SimSensorSonar() {

	if ( this->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SimSensorSonar::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimSensorSonar %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimSensorSonar" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimSensorSonar::start() {
	int i;

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->densityT = NewImage( SimSensorSonar_TEMPLATE_SIZE, SimSensorSonar_TEMPLATE_SIZE );
	this->absoluteT = NewImage( SimSensorSonar_TEMPLATE_SIZE, SimSensorSonar_TEMPLATE_SIZE );
	this->integralTarget = NewImage( SimSensorSonar_TEMPLATE_SIZE, SimSensorSonar_TEMPLATE_SIZE );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		this->densityR[i] = NewImage( 2*SimSensorSonar_TEMPLATE_SIZE, 2*SimSensorSonar_TEMPLATE_SIZE );
		this->absoluteR[i] = NewImage( 2*SimSensorSonar_TEMPLATE_SIZE, 2*SimSensorSonar_TEMPLATE_SIZE );
	}
	this->mapUpdate = NewImage( 64, 64 );

	this->sharedArraySize = 0;
	this->absoluteOd = NULL;
	this->densityOd = NULL;
	this->obsDensity = NULL;
	
	this->started = true;

	return 0;
}

int SimSensorSonar::setMap( UUID *id ) {

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

int SimSensorSonar::stop() {
	int i;

	// clean up buffers
	if ( this->mapDataSize )
		free( this->mapData );

	FreeImage( this->densityT );
	FreeImage( this->absoluteT );
	FreeImage( this->integralTarget );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		FreeImage( this->densityR[i] );
		FreeImage( this->absoluteR[i] );
	}
	FreeImage( this->mapUpdate );

	if ( this->sharedArraySize ) {
		free( this->absoluteOd );
		free( this->densityOd );
		free( this->obsDensity );
		this->sharedArraySize = 0;
	}

	return SimAgentBase::stop();
}


//-----------------------------------------------------------------------------
// Processing

int SimSensorSonar::addReading( UUID *sensor, _timeb *tb ) {
	SonarPose pose;
	UUID avatar;

	// get pose
	mapPose::iterator iterP = this->sensorPose.find( *sensor );
	if ( iterP == this->sensorPose.end() ) {
		// get pose
		sim->ddbSensorGetInfo( sensor, DDBSENSORINFO_AVATAR | DDBSENSORINFO_POSE, &this->ds );

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
		}
	} else { 
		pose = this->sensorPose[*sensor];
		avatar = this->sensorAvatar[*sensor];
	}

	// get PF
	PROCESS_READING *pr = &this->readings[this->nextReadingId];
	pr->id = this->nextReadingId;
	this->nextReadingId++;
	pr->sensor = *sensor;
	pr->avatar = avatar;
	pr->time = *tb;
	pr->pfState = NULL;
	pr->pfWeight = NULL;
	pr->retries = 0;

	this->cbRequestPFInfo( &pr->id ); 

	return 0;
}

int SimSensorSonar::processReading( PROCESS_READING *pr ) {
	
	int mapDivisions = (int)floor(1/this->mapResolution);

	SonarReading reading;
	SonarPose *pose;

	int border;
	int i, j, p;
	int r, c;
	int div;
	float sn, cs;
	float stateX, stateY, stateR;

	float PFbound[4];
	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	int   mapUpdateSize[2]; // width,height (cols,rows)
	bool  rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date

	float penalty, support;
	float maxAbsoluteOd, maxDensityOd;
	float absoluteNorm, densityNorm;

	float scale;

	// get data
	sim->ddbSensorGetData( &pr->sensor, &pr->time, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		this->ds.unpackInt32(); // discard reading size
		memcpy( &reading, this->ds.unpackData( sizeof(SonarReading) ), sizeof(SonarReading) );
	} else {
		this->finishReading( pr, 0 );
		return 0;
	}

	pose = &this->sensorPose[ pr->sensor ];

	// generate template
	if ( this->generateConicTemplates( reading.value, pose->max, pose->sigma, pose->alpha, pose->beta, this->densityT, this->absoluteT, &scale ) ) {
		return 1; // error generating template
	}

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
	
	// prepare map update
	border = (int)ceil(5 + scale*mapDivisions*SimSensorSonar_TEMPLATE_SIZE);
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

	// clear the rotationDivision flags
	memset( rotationDivision, 0, sizeof(rotationDivision) );
	
	// make sure arrays are big enough
	if ( this->sharedArraySize < pr->pfNum ) {
		if ( this->sharedArraySize ) {
			free( this->absoluteOd );
			free( this->densityOd );
			free( this->obsDensity );
		}
		this->sharedArraySize = pr->pfNum;
		this->absoluteOd = (float *)malloc( sizeof(float)*this->sharedArraySize );		
		if ( !this->absoluteOd ) {
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
		this->densityOd = (float *)malloc( sizeof(float)*this->sharedArraySize );
		if ( !this->densityOd ) {
			free( this->absoluteOd );
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
		this->obsDensity = (float *)malloc( sizeof(float)*this->sharedArraySize );
		if ( !this->obsDensity ) {
			free( this->absoluteOd );
			free( this->densityOd );
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
	}

	// for each particle
	maxAbsoluteOd = 0;
	maxDensityOd = 0;
	for ( p=0; p<pr->pfNum; p++ ) {
		if ( p == 5 )
			p=p;
		stateX = pfStateX(pr,p);
		stateY = pfStateY(pr,p);
		stateR = pfStateR(pr,p);
		// adjust for sensor pose
		sn = (float)sin(stateR);
		cs = (float)cos(stateR);
		stateX += cs*pose->x - sn*pose->y;
		stateY += sn*pose->x + cs*pose->y;
		stateR += pose->r;
		while ( stateR < 0 ) stateR += 2*fM_PI;
		while ( stateR > 2*fM_PI ) stateR -= 2*fM_PI;
		div = (int)(ROTATION_DIVISIONS*(stateR + ROTATION_RESOLUTION*0.5)/(2*fM_PI)) % ROTATION_DIVISIONS;
		// rotate
		if ( rotationDivision[div] == false ) {
			if ( reading.value < pose->max )
				RotateImageEx( densityT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), densityR[div], integralTarget, 0 );
			RotateImageEx( absoluteT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), absoluteR[div], integralTarget, 0 );
			rotationDivision[div] = true;
		}
		origin[0] = absoluteR[div]->cols/2.0f - absoluteT->cols/2.0f*(float)cos(div*ROTATION_RESOLUTION);
		origin[1] = absoluteR[div]->rows/2.0f - absoluteT->cols/2.0f*(float)sin(div*ROTATION_RESOLUTION);
		
		// compare against map
		offset[0] = (stateX - mapX)*mapDivisions - origin[0]*scale*mapDivisions;
		offset[1] = (stateY - mapY)*mapDivisions - origin[1]*scale*mapDivisions;
		absoluteOd[p] = 0;
		densityOd[p] = 0;
		for ( j=0; j<absoluteR[div]->cols; j++ ) {
			c = (int)(j*scale*mapDivisions + offset[0]);
			for ( i=0; i<absoluteR[div]->rows; i++ ) {
				r = (int)(i*scale*mapDivisions + offset[1]);
				// update absoluteOd, total penalty
				if ( Px(absoluteR[div],i,j) < 0 ) {
					penalty = max( 0, this->mapData[r+c*this->mapHeight] - 0.5f ) * -Px(absoluteR[div],i,j);
				} else {
					penalty = max( 0, 0.5f - this->mapData[r+c*this->mapHeight] ) * Px(absoluteR[div],i,j);
				}
				absoluteOd[p] += penalty;
				// update densityOd, max support
				if ( reading.value < pose->max ) {
					if ( Px(densityR[div],i,j) < 0 ) {
						support = max( 0, 0.5f - this->mapData[r+c*this->mapHeight] ) * -Px(densityR[div],i,j);
					} else {
						support = max( 0, this->mapData[r+c*this->mapHeight] - 0.5f ) * Px(densityR[div],i,j);
					}
					densityOd[p] = max( densityOd[p], support );
				}
			}
		}
		maxAbsoluteOd = max( maxAbsoluteOd, absoluteOd[p] );
		maxDensityOd = max( maxDensityOd, densityOd[p] );

		// TEMP
		//offset[0] = (state.x - mapLoc[0])*mapDivisions;
		//offset[1] = (state.y - mapLoc[1])*mapDivisions;
		//map[(int)offset[0]][(int)offset[1]] = 0;

		// stamp on update
		offset[0] = (stateX - mapUpdateLoc[0])*mapDivisions - origin[0]*scale*mapDivisions;
		offset[1] = (stateY - mapUpdateLoc[1])*mapDivisions - origin[1]*scale*mapDivisions;
		if ( reading.value < pose->max )
			ImageAdd( densityR[div], mapUpdate, offset[0], offset[1], scale*mapDivisions, pr->pfWeight[p]*SimSensorSonar_MAP_UPDATE_STRENGTH*(1-SimSensorSonar_MAP_UPDATE_RATIO), 1 );
		ImageAdd( absoluteR[div], mapUpdate, offset[0], offset[1], scale*mapDivisions, pr->pfWeight[p]*SimSensorSonar_MAP_UPDATE_STRENGTH*SimSensorSonar_MAP_UPDATE_RATIO, 0 );

		// TEMP
		//this->visualizer->deletefImage( this->mapUpdateImage );
		//this->mapUpdateImage = this->visualizer->newfImage( mapUpdateLoc[0], mapUpdateLoc[1], 1.0f/mapDivisions, 0, 50, this->mapUpdate );
		//offset[0] = (state.x - mapUpdateLoc[0])*mapDivisions;
		//offset[1] = (state.y - mapUpdateLoc[1])*mapDivisions;
		//Px(mapUpdate,(int)offset[1],(int)offset[0]) = 1;
	}

	// compute observation density
	if ( maxAbsoluteOd > 0 ) absoluteNorm = 1/maxAbsoluteOd;
	else absoluteNorm = 1;
	if ( maxDensityOd > 0 ) densityNorm = 1/maxDensityOd;
	else densityNorm = 1;
	if ( maxAbsoluteOd > 0 || maxDensityOd > 0 ) {
		for ( p=0; p<pr->pfNum; p++ ) {
			obsDensity[p] = SimSensorSonar_OBS_DENSITY_RATIO*absoluteOd[p]*absoluteNorm 
							+ (1-SimSensorSonar_OBS_DENSITY_RATIO)*densityOd[p]*densityNorm
							+ SimSensorSonar_OBS_DENSITY_AMBIENT;
		}

		// submit particle filter update
		sim->ddbApplyPFCorrection( &this->sensorAvatar[ pr->sensor ], &pr->time, obsDensity );
	}

	// submit map update
	sim->ddbApplyPOGUpdate( &this->mapId, mapUpdateLoc[0], mapUpdateLoc[1], mapUpdateSize[0]*this->mapResolution, mapUpdateSize[1]*this->mapResolution, this->mapUpdate->data );
	
	// finish reading
	this->finishReading( pr, 1 );
	
	return 0;
}


int SimSensorSonar::finishReading( PROCESS_READING *pr, bool success ) {

	if ( pr->pfState )
		free( pr->pfState );
	if ( pr->pfWeight )
		free( pr->pfWeight );

	this->readings.erase( pr->id );

	return 0;
}

int SimSensorSonar::generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale ) {
	int i, j;
	float r, t;
	float coverage;
	float var = sig*sig;
	float norm = ( 1/(sig*sqrt(2*fM_PI)) );
	float expon;
	bool skipDensity;

	if ( d > max ) 
		d = max;

	*scale = 0.7071f * (d + 3*var)/SimSensorSonar_TEMPLATE_SIZE_DIV2;
	
	int cols = (int)ceil(0.7071f * SimSensorSonar_TEMPLATE_SIZE);
	densityT->cols = cols;
	absoluteT->cols = cols;

	if ( d < max ) {
		for ( i=0; i<SimSensorSonar_TEMPLATE_SIZE_DIV2; i++ ) {
			skipDensity = false;
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;
						
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );
					
					if ( skipDensity ) {
						Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;
					} else {
						expon = exp( -(d-r)*(d-r)/(2*var) );
						if ( expon < 1E-15 ) {
							Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
							Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;
							if ( r < d ) // early exit
								skipDensity = true;
						} else {
							Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) 
								= Px(densityT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) 
								= - *scale * 0.45f 
								* ( 1/(2*r*a) ) 
								* norm * expon;
						}
					}

					if ( r > d ) {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	} else { // no density
		for ( i=0; i<SimSensorSonar_TEMPLATE_SIZE_DIV2; i++ ) {
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );

					if ( r > d ) {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,SimSensorSonar_TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool SimSensorSonar::cbRequestPFInfo( void *vpReadingId ) {
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
			this->addTimeout( SimSensorSonar_REPEAT_RPFINFO_PERIOD, NEW_MEMBER_CB(SimSensorSonar, cbRequestPFInfo), &readingId, sizeof(int) );
		else
			this->finishReading( pr, 0 );
	} else {
		this->finishReading( pr, 0 );
	}

	return 0;
}