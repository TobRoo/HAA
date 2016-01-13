// SimSensorLandmark.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimSensorLandmark.h"

#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

//*****************************************************************************
// SimSensorLandmark

//-----------------------------------------------------------------------------
// Constructor	
SimSensorLandmark::SimSensorLandmark( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {
	this->nextReadingId = 0;
}

//-----------------------------------------------------------------------------
// Destructor
SimSensorLandmark::~SimSensorLandmark() {

	if ( this->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SimSensorLandmark::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimSensorLandmark %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimSensorLandmark" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimSensorLandmark::start() {

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->obsDensitySize = 0;
	this->obsDensity = NULL;
	this->obsDensitySize2 = 0;
	this->obsDensity = NULL;

	this->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SimSensorLandmark::stop() {

	// clean up readings.
	mapProcessReading::iterator iterR = this->readings.begin();
	while ( iterR != this->readings.end() ) {
		this->finishReading( &iterR->second, 0 );
		iterR++;
	}
	this->readings.clear();
	
	// clean up buffers
	if ( this->obsDensitySize ) {
		free( this->obsDensity );
		this->obsDensitySize = 0;
	}
	if ( this->obsDensitySize2 ) {
		free( this->obsDensity2 );
		this->obsDensitySize2 = 0;
	}

	return SimAgentBase::stop();
}


//-----------------------------------------------------------------------------
// Processing

int SimSensorLandmark::addReading( UUID *sensor, _timeb *tb ) {
	int i;
	CameraPose pose;
	UUID avatar;

	CameraReading reading;

	char dataType;
	char lmCode;
	float lmD, lmA;

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
			pose = *(CameraPose *)this->ds.unpackData( sizeof(CameraPose) );
			this->sensorPose[*sensor] = pose;
		} else {
			return 1;
		}
	} else { 
		pose = this->sensorPose[*sensor];
		avatar = this->sensorAvatar[*sensor];
	}

	// get data
	int dataSize;
	sim->ddbSensorGetData( sensor, tb, &this->ds );
	this->ds.rewind();
	this->ds.unpackInt32(); // thread
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		this->ds.unpackInt32(); // discard reading size
		memcpy( &reading, this->ds.unpackData( sizeof(CameraReading) ), sizeof(CameraReading) );
		dataSize = this->ds.unpackInt32();
		if ( !dataSize )
			return 0; // nothing to process
		// Note: at this point the reading data is at the front of the datastream
	} else {
		return 0;
	}

	// prepare reading
	PROCESS_READING *pr = &this->readings[this->nextReadingId];
	pr->id = this->nextReadingId;
	this->nextReadingId++;
	pr->sensor = *sensor;
	pr->avatar = avatar;
	pr->time = *tb;
	pr->landmarkCount = 0;
	pr->pfState = NULL;
	pr->pfWeight = NULL;
	pr->waitingForPF = 0;
	pr->retries = 0;

	// read landmarks
	while (1) {
		dataType = this->ds.unpackChar();
		if ( dataType == SimCamera_DATA_END )
			break;
		switch ( dataType ) {
		case SimCamera_DATA_LANDMARK:
			lmCode = this->ds.unpackChar();
			lmD = this->ds.unpackFloat32();
			lmA = this->ds.unpackFloat32();
		
			if ( pr->landmarkCount < SimSensorLandmark_MAX_LANDMARKS ) {
				pr->landmark[pr->landmarkCount].code = lmCode;
				pr->landmark[pr->landmarkCount].D = lmD;
				pr->landmark[pr->landmarkCount].A = lmA;

				pr->landmarkCount++;
			}
			break;
		default:
			this->finishReading( pr, 0 ); 
			return 0; // unhandled data type
		};
	}

	if ( !pr->landmarkCount ) { // nothing to do
		this->finishReading( pr, 1 );
		return 0;
	}

	// get landmark info
	RPC_STATUS Status;
	mapLandmark::iterator itL;
	for ( i=0; i<pr->landmarkCount; i++ ) {
		itL = this->landmarks.find( pr->landmark[i].code );
		if ( itL == this->landmarks.end() ) {
			sim->ddbGetLandmark( pr->landmark[i].code, &this->ds );
			this->ds.rewind();
			this->ds.unpackInt32(); // thread
			if ( this->ds.unpackChar() != DDBR_OK ) {
				pr->landmarkCount = i; // up to here are valid when we're deleting the reading
				this->finishReading( pr, 0 );
				return 0;
			}
			this->landmarks[pr->landmark[i].code] = *(DDBLandmark *)this->ds.unpackData( sizeof(DDBLandmark) );
			pr->landmark[i].info = this->landmarks[pr->landmark[i].code];
		} else {
			pr->landmark[i].info = itL->second;
		}

		if ( !UuidIsNil( &pr->landmark[i].info.owner, &Status ) ) { // prepare pf info
			pr->landmark[i].pfState = NULL;
			pr->landmark[i].pfWeight = NULL;

			pr->waitingForPF++;
		}
	}

	// get pfs
	pr->waitingForPF++;
	this->cbRequestPFInfo( &pr->id ); 

	return 0;
}

int SimSensorLandmark::processReading( PROCESS_READING *pr ) {
	RPC_STATUS Status;
	int i;

	int p, q;

	CameraPose *pose;

	float sn, cs;
	float px, py; // landmark pos in particle frame
	float lx, ly; // landmark pos in origin frame
	float temp; 
	float landmarkH; // landmark heading from particle
	float lH; // landmark heading from origin frame
	float errX, errY; // error vector from estimated pos to real pos

	float sigY;
	float normX, normY;
	float invSigXX2, invSigYY2;

	pose = &this->sensorPose[ pr->sensor ];

	// make sure arrays are big enough
	if ( this->obsDensitySize < pr->pfNum ) {
		if ( this->obsDensitySize ) {
			free( this->obsDensity );
		}
		this->obsDensitySize = pr->pfNum;
		this->obsDensity = (float *)malloc( sizeof(float)*this->obsDensitySize );
		if ( !this->obsDensity ) {
			this->obsDensitySize = 0;
			return 1; // malloc failed
		}
	}

	for ( i=0; i<pr->landmarkCount; i++ ) {

		// calculate landmark pos (camera frame)
		sn = (float)sin(pr->landmark[i].A);
		cs = (float)cos(pr->landmark[i].A);
		
		px = pr->landmark[i].D*cs;
		py = pr->landmark[i].D*sn;
		landmarkH = atan2( py, px ); // should equal pr->landmark[i].A but calculate again because that's what we do in the real version

		// calculate sigmas and other constants
		sigY = SimSensorLandmark_OBS_SIGMA_Y * max( 0.1f, sqrt(px*px + py*py) );  // scale based on distance from the camera
		normX = 1/(SimSensorLandmark_OBS_SIGMA_X*sqrt(2*fM_PI)); 
		normY = 1/(sigY*sqrt(2*fM_PI));
		invSigXX2 = 1/(2 * SimSensorLandmark_OBS_SIGMA_X*SimSensorLandmark_OBS_SIGMA_X);
		invSigYY2 = 1/(2 * sigY*sigY);

		// transform to particle frame
		sn = (float)sin(pose->r);
		cs = (float)cos(pose->r);
		temp = px*cs - py*sn;
		py = px*sn + py*cs;
		px = temp + pose->x;
		py = py + pose->y;
		landmarkH += pose->r;

		if ( UuidIsNil( &pr->landmark[i].info.owner, &Status ) ) { // stationary update
			// loop through particles
			for ( p=0; p<pr->pfNum; p++ ) {

				// transform to origin frame
				sn = (float)sin(pfStateR(pr,p));
				cs = (float)cos(pfStateR(pr,p));
				lx = px*cs - py*sn + pfStateX(pr,p);
				ly = px*sn + py*cs + pfStateY(pr,p);
				lH = landmarkH + pfStateR(pr,p);

				// calculate err vector in origin frame
				errX = lx - pr->landmark[i].info.x;
				errY = ly - pr->landmark[i].info.y;

				// rotate into estimate frame
				sn = (float)sin(-lH);
				cs = (float)cos(lH);
				temp = errX*cs - errY*sn;
				errY = errX*sn + errY*cs;
				errX = temp;

				// calculate observation density 
				obsDensity[p] = SimSensorLandmark_OBS_DENSITY_AMBIENT
					+ normX * exp( -errX*errX * invSigXX2 )
					* normY * exp( -errY*errY * invSigYY2 );
			}

			// submit particle filter update
			sim->ddbApplyPFCorrection( &this->sensorAvatar[ pr->sensor ], &pr->time, obsDensity );

		} else { // filter-filter update
			// make sure arrays are big enough
			if ( this->obsDensitySize2 < pr->landmark[i].pfNum ) {
				if ( this->obsDensitySize2 ) {
					free( this->obsDensity2 );
				}
				this->obsDensitySize2 = pr->landmark[i].pfNum;
				this->obsDensity2 = (float *)malloc( sizeof(float)*this->obsDensitySize2 );
				if ( !this->obsDensity2 ) {
					this->obsDensitySize2 = 0;
					return 1; // malloc failed
				}
			}

			// zero observation densities
			memset( obsDensity, 0, sizeof(float)*pr->pfNum );
			memset( obsDensity2, 0, sizeof(float)*pr->landmark[i].pfNum );

			// move particle origin to landmark origin
			for ( q=0; q<pr->landmark[i].pfNum; q++ ) {
				sn = (float)sin(pfStateR((&pr->landmark[i]),q));
				cs = (float)cos(pfStateR((&pr->landmark[i]),q));
				pfStateX((&pr->landmark[i]),q) += pr->landmark[i].info.x*cs - pr->landmark[i].info.y*sn;
				pfStateY((&pr->landmark[i]),q) += pr->landmark[i].info.x*sn + pr->landmark[i].info.y*cs;
			}

			// loop through our particles
			for ( p=0; p<pr->pfNum; p++ ) {
				// transform to origin frame
				sn = (float)sin(pfStateR(pr,p));
				cs = (float)cos(pfStateR(pr,p));
				lx = px*cs - py*sn + pfStateX(pr,p);
				ly = px*sn + py*cs + pfStateY(pr,p);
				lH = landmarkH + pfStateR(pr,p);

				// loop through landmark particles
				for ( q=0; q<pr->landmark[i].pfNum; q++ ) {
					// calculate err vector in origin frame
					errX = lx - pfStateX((&pr->landmark[i]),q);
					errY = ly - pfStateY((&pr->landmark[i]),q);

					// rotate into estimate frame
					sn = (float)sin(-lH);
					cs = (float)cos(lH);
					temp = errX*cs - errY*sn;
					errY = errX*sn + errY*cs;
					errX = temp;

					// calculate observation density 
					temp = normX * exp( -errX*errX * invSigXX2 )
						* normY * exp( -errY*errY * invSigYY2 ); 
					obsDensity[p] += pr->landmark[i].pfWeight[q] * (temp + SimSensorLandmark_OBS_DENSITY_AMBIENT);
					obsDensity2[q] += pr->pfWeight[p] * (temp + SimSensorLandmark_OBS_DENSITY_AMBIENT_OTHER);
				}
			}

			// submit our particle filter update
			sim->ddbApplyPFCorrection( &this->sensorAvatar[ pr->sensor ], &pr->time, obsDensity );

			// submit landmark particle filter update
			sim->ddbApplyPFCorrection( &pr->landmark[i].info.owner, &pr->time, obsDensity2 );
		}
	}

	// finish reading
	this->finishReading( pr, 1 );
	
	return 0;
}


int SimSensorLandmark::finishReading( PROCESS_READING *pr, bool success ) {
	int i;
	RPC_STATUS Status;

	// clean up landmark hits
	for ( i=0; i<pr->landmarkCount; i++ ) {
		if ( !UuidIsNil( &pr->landmark[i].info.owner, &Status ) ) { 
			if ( pr->landmark[i].pfState )
				free( pr->landmark[i].pfState );
			if ( pr->landmark[i].pfWeight )
				free( pr->landmark[i].pfWeight );
		}
	}

	if ( pr->pfState )
		free( pr->pfState );
	if ( pr->pfWeight )
		free( pr->pfWeight );

	this->readings.erase( pr->id );

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool SimSensorLandmark::cbRequestPFInfo( void *vpReadingId ) {
	int i;
	RPC_STATUS Status;
	int readingId = *(int *)vpReadingId;
	PROCESS_READING *pr;
	int infoFlags;
	char response;

	pr = &this->readings[readingId];

	if ( pr->pfState == NULL ) { // we need to get our pf
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

			pr->waitingForPF--;
		} else if ( response != DDBR_TOOLATE )  {
			this->finishReading( pr, 0 ); // error
			return 0;
		}
	}

	for ( i=0; i<pr->landmarkCount; i++ ) {
		if ( UuidIsNil( &pr->landmark[i].info.owner, &Status ) )
			continue; // no pf for this landmark

		if ( pr->landmark[i].pfState != NULL )
			continue; // already have this pf

		sim->ddbPFGetInfo( &pr->landmark[i].info.owner, DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT, &pr->time, &this->ds );
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

			pr->landmark[i].pfNum = this->ds.unpackInt32();
			pr->landmark[i].pfStateSize = this->ds.unpackInt32();
			pr->landmark[i].pfState = (float *)malloc( pr->landmark[i].pfNum*pr->landmark[i].pfStateSize*sizeof(float) );
			if ( pr->landmark[i].pfState == NULL ) {
				this->finishReading( pr, 0 );
				return 0;
			}
			pr->landmark[i].pfWeight = (float *)malloc( pr->landmark[i].pfNum*sizeof(float) );
			if ( pr->landmark[i].pfWeight == NULL ) {
				this->finishReading( pr, 0 );
				return 0;
			}

			memcpy( pr->landmark[i].pfState, this->ds.unpackData( pr->landmark[i].pfNum*pr->landmark[i].pfStateSize*sizeof(float) ), pr->landmark[i].pfNum*pr->landmark[i].pfStateSize*sizeof(float) );
			memcpy( pr->landmark[i].pfWeight, this->ds.unpackData( pr->landmark[i].pfNum*sizeof(float) ), pr->landmark[i].pfNum*sizeof(float) );

			pr->waitingForPF--;
		} else if ( response != DDBR_TOOLATE )  {
			this->finishReading( pr, 0 ); // error
			return 0;
		}
	}

	if ( !pr->waitingForPF ) {
		this->processReading( pr );
	} else {
		// wait 250 ms and try again
		pr->retries++;
		if ( pr->retries < 10 )
			this->addTimeout( SimSensorLandmark_REPEAT_RPFINFO_PERIOD, NEW_MEMBER_CB(SimSensorLandmark, cbRequestPFInfo), &readingId, sizeof(int) );
		else
			this->finishReading( pr, 0 );
	}

	return 0;
}