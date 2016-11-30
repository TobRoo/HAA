// AgentSensorSonar.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\fImage.h"

#include "AgentSensorSonar.h"
#include "AgentSensorSonarVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\SupervisorSLAM\\SupervisorSLAMVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentSensorSonar

//-----------------------------------------------------------------------------
// Constructor	
AgentSensorSonar::AgentSensorSonar( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	int i;

	// allocate state
	ALLOCATE_STATE( AgentSensorSonar, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentSensorSonar" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentSensorSonar_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentSensorSonar_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(AgentSensorSonar)->mapId );
	STATE(AgentSensorSonar)->mapResolution = 0;

	
	// Prepare callbacks
	this->callback[AgentSensorSonar_CBR_convRequestMapInfo] = NEW_MEMBER_CB(AgentSensorSonar,convRequestMapInfo);
	this->callback[AgentSensorSonar_CBR_convRequestSensorInfo] = NEW_MEMBER_CB(AgentSensorSonar,convRequestSensorInfo);
	this->callback[AgentSensorSonar_CBR_convRequestPFInfo] = NEW_MEMBER_CB(AgentSensorSonar,convRequestPFInfo);
	this->callback[AgentSensorSonar_CBR_cbRepeatRPFInfo] = NEW_MEMBER_CB(AgentSensorSonar,cbRepeatRPFInfo);
	this->callback[AgentSensorSonar_CBR_convRequestAvatarLoc] = NEW_MEMBER_CB(AgentSensorSonar,convRequestAvatarLoc);
	this->callback[AgentSensorSonar_CBR_cbRepeatRAvatarLoc] = NEW_MEMBER_CB(AgentSensorSonar,cbRepeatRAvatarLoc);
	this->callback[AgentSensorSonar_CBR_convGetAvatarList] = NEW_MEMBER_CB(AgentSensorSonar,convGetAvatarList);
	this->callback[AgentSensorSonar_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(AgentSensorSonar,convGetAvatarInfo);
	this->callback[AgentSensorSonar_CBR_convRequestMap] = NEW_MEMBER_CB(AgentSensorSonar,convRequestMap);
	this->callback[AgentSensorSonar_CBR_convRequestData] = NEW_MEMBER_CB(AgentSensorSonar,convRequestData);

	STATE(AgentSensorSonar)->readingNextId = 0;

	this->densityT = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	this->absoluteT = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	this->integralTarget = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		this->densityR[i] = NewImage( 2*TEMPLATE_SIZE, 2*TEMPLATE_SIZE );
		this->absoluteR[i] = NewImage( 2*TEMPLATE_SIZE, 2*TEMPLATE_SIZE );
	}
	this->mapUpdate = NewImage( 64, 64 );

	this->sharedArraySize = 0;
	this->absoluteOd = NULL;
	this->densityOd = NULL;
	this->obsDensity = NULL;
}

//-----------------------------------------------------------------------------
// Destructor
AgentSensorSonar::~AgentSensorSonar() {
	int i;

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	FreeImage( this->densityT );
	FreeImage( this->absoluteT );
	FreeImage( this->integralTarget );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		FreeImage( this->densityR[i] );
		FreeImage( this->absoluteR[i] );
	}
	FreeImage( this->mapUpdate );

}

//-----------------------------------------------------------------------------
// Configure

int AgentSensorSonar::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\AgentSensorSonar %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentSensorSonar %.2d.%.2d.%.5d.%.2d", AgentSensorSonar_MAJOR, AgentSensorSonar_MINOR, AgentSensorSonar_BUILDNO, AgentSensorSonar_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentSensorSonar::start( char *missionFile ) {
	DataStream lds;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars once DDBE_WATCH_TYPE notification is received
	

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentSensorSonar::stop() {

	// clean up poses
	mapPose::iterator iterP = this->sensorPose.begin();
	while ( iterP != this->sensorPose.end() ) {
		freeDynamicBuffer( iterP->second );
		iterP++;
	}
	this->sensorPose.clear();
	
	// clean up readings.
	mapReadings::iterator iterR = this->readings.begin();
	while ( iterR != this->readings.end() ) {
		if ( !(iterR->second.waiting & WAITING_ON_DATA) ) {
			freeDynamicBuffer( iterR->second.readingRef );
			if ( iterR->second.dataSize )
				freeDynamicBuffer( iterR->second.dataRef );
		}
		if ( !(iterR->second.waiting & WAITING_ON_PF) ) {
			freeDynamicBuffer( iterR->second.pfStateRef );
			freeDynamicBuffer( iterR->second.pfWeightRef );
		}
		if ( !(iterR->second.waiting & WAITING_ON_MAP) ) {
			freeDynamicBuffer( iterR->second.mapDataRef );
		}
		delete iterR->second.avatarLocs;
		iterR++;
	}
	this->readings.clear();

	// clean up buffers
	if ( this->sharedArraySize ) {
		free( this->absoluteOd );
		free( this->densityOd );
		free( this->obsDensity );
		this->sharedArraySize = 0;
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentSensorSonar::step() {
	return AgentBase::step();
}

int AgentSensorSonar::configureParameters( DataStream *ds ) {
	UUID uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorSonar)->ownerId = uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorSonar)->mapId = uuid;

	// get map info
	UUID thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorSonar)->mapId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->backup(); // mapId

	return 0;
}

int AgentSensorSonar::addPose( UUID *sensor, UUID *poseRef ) {
	if ( this->sensorPose.find( *sensor ) != this->sensorPose.end() ) {
		Log.log( 0, "AgentSensorSonar::addPose: we already have this pose!" );
		freeDynamicBuffer( *poseRef );
		return 1;
	}
	
	this->sensorPose[ *sensor ] = *poseRef;

	return 0; // finished
}

int AgentSensorSonar::addReading( UUID *sensor, _timeb *time ) {
	PROCESS_READING pr;
	UUID thread;

	// make sure we have the necessary info
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		if ( iA->second.start.time > time->time || (iA->second.start.time == time->time && iA->second.start.millitm > time->millitm) )
			continue; // too early
		if ( iA->second.retired == 1 && (iA->second.end.time < time->time || (iA->second.end.time == time->time && iA->second.end.millitm < time->millitm)) )
			continue; // properly retired, too late
		
		if ( !iA->second.ready )
			break;
	}

	if ( STATE(AgentSensorSonar)->mapResolution == 0 || this->avatars.size() == 0 ||  iA != this->avatars.end() ) {
		Log.log( 0, "AgentSensorSonar::addReading: info not available yet, fail reading" );

		// notify owner
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packUUID( sensor );
		this->ds.packData( time, sizeof(_timeb) );
		this->ds.packChar( 0 );
		this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorSonar)->ownerId );
		this->ds.unlock();

		return 1;
	}

	pr.id = STATE(AgentSensorSonar)->readingNextId++;
	pr.waiting = 0;
	pr.sensor = *sensor;
	pr.time = *time;
	pr.retries = 0;

	// TEMP
	//Log.log( LOG_LEVEL_VERBOSE, "AgentSensorSonar::addReading: adding reading %d", pr.id );
	//pr.waiting = 0xffffffff;
	//this->readings[pr.id] = pr;
	//this->finishReading( &this->readings[pr.id], 1 );
	//return 0;

	pr.waiting |= WAITING_ON_PF;

	// get avatar and pose, or PF if avatar and pose are already known
	mapPose::iterator iterP = this->sensorPose.find( *sensor );
	if ( iterP == this->sensorPose.end() ) {
		pr.waiting |= WAITING_ON_POSE;
		// request avatar/pose
		thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestSensorInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( sensor );
		this->ds.packInt32( DDBSENSORINFO_PF | DDBSENSORINFO_POSE );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RSENSORINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

	} else { // we know the avatar so we can request the PF right away
		// request PF
		thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID( &this->sensorAvatar[ *sensor ] );
		this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
		this->ds.packData( &pr.time, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	// get avatar locations
	pr.waitingOnAvatars = 0;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		if ( iA->second.start.time > pr.time.time || (iA->second.start.time == pr.time.time && iA->second.start.millitm > pr.time.millitm) )
			continue; // too early
		if ( iA->second.retired == 1 && (iA->second.end.time < pr.time.time || (iA->second.end.time == pr.time.time && iA->second.end.millitm < pr.time.millitm)) )
			continue; // properly retired, too late

		// request PF
		this->ds.reset();
		this->ds.packInt32( pr.id );
		this->ds.packUUID( (UUID *)&iA->first );
		thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		if ( thread == nilUUID ) {
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID( &iA->second.pf );
		if ( iA->second.retired == 2 && (iA->second.end.time < pr.time.time || (iA->second.end.time == pr.time.time && iA->second.end.millitm < pr.time.millitm)) ) {
			// crashed by reading time, just get latest
			this->ds.packInt32( DDBPFINFO_CURRENT );
		} else { 
			// request at reading time
			this->ds.packInt32( DDBPFINFO_MEAN );
		}
		this->ds.packData( &pr.time, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		pr.waitingOnAvatars++;
	}

	// get Data
	pr.waiting |= WAITING_ON_DATA;
	// request data
	thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestData, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( sensor );
	this->ds.packData( time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RSENSORDATA, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// flag map
	pr.waiting |= WAITING_ON_MAP;

	// add reading
	pr.avatarLocs = new std::map<UUID,AVATAR_LOC,UUIDless>;	
	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorSonar::addReading: adding reading %d", pr.id );
	this->readings[pr.id] = pr;

	return 0;
}

int AgentSensorSonar::finishReading( PROCESS_READING *pr, char success ) {

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorSonar::finishReading: %d, success == %d", pr->id, success );

	// DEBUG
	if ( success != 1 )
		int i=0;

	// notify owner
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &pr->sensor );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packChar( success );
	this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorSonar)->ownerId );
	this->ds.unlock();

	// clean up reading
	if ( !(pr->waiting & WAITING_ON_DATA) ) {
		freeDynamicBuffer( pr->readingRef );
		if ( pr->dataSize )
			freeDynamicBuffer( pr->dataRef );
	}
	if ( !(pr->waiting & WAITING_ON_PF) ) {
		freeDynamicBuffer( pr->pfStateRef );
		freeDynamicBuffer( pr->pfWeightRef );
	}
	if ( !(pr->waiting & WAITING_ON_MAP) ) {
		freeDynamicBuffer( pr->mapDataRef );
	}
	delete pr->avatarLocs;

	this->readings.erase( pr->id );

	return 0;
}

int AgentSensorSonar::requestMapRegion( PROCESS_READING *pr ) {

	int border;
	int p;

	float PFbound[4];
	int	  mapDivisions = (int)floor(1/STATE(AgentSensorSonar)->mapResolution);
	
	SonarPose *pose = (SonarPose *)getDynamicBuffer( this->sensorPose[ pr->sensor ] );
	SonarReading *reading = (SonarReading *)getDynamicBuffer( pr->readingRef );
	float scale = this->templateScale( reading->value, pose->max, pose->sigma );
	float *pfState = (float *)getDynamicBuffer( pr->pfStateRef );

	// update PFbound
	// TODO ignore particles with very little weight?
	PFbound[0] = pfStateX(pfState,0);
	PFbound[1] = pfStateY(pfState,0);
	PFbound[2] = 0;
	PFbound[3] = 0;
	for ( p=1; p<pr->pfNum; p++ ) {
		if ( PFbound[0] > pfStateX(pfState,p) ) {
			PFbound[2] = PFbound[0] + PFbound[2] - pfStateX(pfState,p);
			PFbound[0] = pfStateX(pfState,p);
		} else if ( PFbound[0] + PFbound[2] < pfStateX(pfState,p) ) {
			PFbound[2] = pfStateX(pfState,p) - PFbound[0];
		}
		if ( PFbound[1] > pfStateY(pfState,p) ) {
			PFbound[3] = PFbound[1] + PFbound[3] - pfStateY(pfState,p);
			PFbound[1] = pfStateY(pfState,p);
		} else if ( PFbound[1] + PFbound[3] < pfStateY(pfState,p) ) {
			PFbound[3] = pfStateY(pfState,p) - PFbound[1];
		}
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*mapDivisions)/mapDivisions;
	PFbound[0] = floor(PFbound[0]*mapDivisions)/mapDivisions;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*mapDivisions)/mapDivisions;
	PFbound[1] = floor(PFbound[1]*mapDivisions)/mapDivisions;
	PFbound[3] -= PFbound[1];
	
	// prepare map update
	border = (int)ceil(5 + scale*mapDivisions*TEMPLATE_SIZE);
	pr->mapUpdateLoc[0] = PFbound[0] - border/(float)mapDivisions;
	pr->mapUpdateLoc[1] = PFbound[1] - border/(float)mapDivisions;
	pr->mapUpdateSize[0] = (int)(PFbound[2]*mapDivisions)+border*2;
	pr->mapUpdateSize[1] = (int)(PFbound[3]*mapDivisions)+border*2;

	// request map
	UUID thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestMap, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorSonar)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorSonar)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorSonar)->mapResolution );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentSensorSonar::processReading( PROCESS_READING *pr ) {
	int mapDivisions = (int)floor(1/STATE(AgentSensorSonar)->mapResolution);

	SonarReading *reading;
	SonarPose *pose;

	int i, j, p;
	int r, c;
	int div;
	float sn, cs;
	float stateX, stateY, stateR;

	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate
	bool  rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date

	float penalty, support;
	float maxAbsoluteOd, maxDensityOd;
	float absoluteNorm, densityNorm;

	reading = (SonarReading *)getDynamicBuffer( pr->readingRef );
	pose = (SonarPose *)getDynamicBuffer( this->sensorPose[ pr->sensor ] );
	float *pfState = (float *)getDynamicBuffer( pr->pfStateRef );
	float *pfWeight = (float *)getDynamicBuffer( pr->pfWeightRef );
	float *mapData = (float *)getDynamicBuffer( pr->mapDataRef );

	// decide if reading is unobstructed by other avatars
	bool obstructed = false;
	UUID pfId = this->sensorAvatar[pr->sensor];
	float oX, oY, oR; // sensor origin
	float dX, dY, dR, dDSq, rD;
	float theta;
	std::map<UUID,AVATAR_LOC,UUIDless>::iterator iL;
	for ( iL = pr->avatarLocs->begin(); iL != pr->avatarLocs->end(); iL++ ) {
		if ( this->avatars[iL->first].pf == pfId ) { // this is us
			oX = iL->second.x;
			oY = iL->second.y;
			oR = iL->second.r;
			// adjust for sensor pose
			sn = (float)sin(oR);
			cs = (float)cos(oR);
			oX += cs*pose->x - sn*pose->y;
			oY += sn*pose->x + cs*pose->y;
			oR += pose->r;
			break;
		}
	}
	
	for ( iL = pr->avatarLocs->begin(); iL != pr->avatarLocs->end(); iL++ ) {
		if ( this->avatars[iL->first].pf == pfId ) // this is us
			continue;

		// check distance from origin
		dX = iL->second.x - oX;
		dY = iL->second.y - oY;
		dDSq = dX*dX + dY*dY;
		rD = this->avatars[iL->first].outerRadius + reading->value + READING_OBSTRUCTION_THRESHOLD; // reasonable distance
		if ( dDSq < rD*rD ) { // within reasonable distance
			// check if avatar is inside arc
			theta = atan2( dY, dX ) - oR;
			while ( theta > fM_PI ) theta -= 2*fM_PI;
			while ( theta < -fM_PI ) theta += 2*fM_PI;
			if ( fabs(theta) <= pose->alpha ) { // inside arc
				obstructed = true;
				break;
			} else {
				// check distance from sides of cone
				dR = fabs(theta) - pose->alpha;
				if ( sqrt(dDSq)*dR < this->avatars[iL->first].outerRadius + READING_OBSTRUCTION_THRESHOLD ) { // too close
					obstructed = true;
					break;
				}
			}
		}
	}

	if ( obstructed ) {	
		// finish reading
		this->finishReading( pr, 1 );
		return 0;
	}

	// generate template
	float scale = this->templateScale( reading->value, pose->max, pose->sigma );
	if ( this->generateConicTemplates( reading->value, pose->max, pose->sigma, pose->alpha, pose->beta, this->densityT, this->absoluteT ) ) {
		return 1; // error generating template
	}

	// generate particle filter and map updates
	if ( mapUpdate->bufSize < (int)sizeof(float)*(pr->mapUpdateSize[0] * pr->mapUpdateSize[1]) ) {
		// allocate more space for the map update
		ReallocImage( mapUpdate, pr->mapUpdateSize[1], pr->mapUpdateSize[0] );
	} else {
		mapUpdate->rows = pr->mapUpdateSize[1];
		mapUpdate->cols = pr->mapUpdateSize[0];
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	float mapX = pr->mapUpdateLoc[0];
	float mapY = pr->mapUpdateLoc[1];

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
		stateX = pfStateX(pfState,p);
		stateY = pfStateY(pfState,p);
		stateR = pfStateR(pfState,p);
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
			if ( reading->value < pose->max )
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
					penalty = max( 0, mapData[r+c*pr->mapHeight] - 0.5f ) * -Px(absoluteR[div],i,j);
				} else {
					penalty = max( 0, 0.5f - mapData[r+c*pr->mapHeight] ) * Px(absoluteR[div],i,j);
				}
				absoluteOd[p] += penalty;
				// update densityOd, max support
				if ( reading->value < pose->max ) {
					if ( Px(densityR[div],i,j) < 0 ) {
						support = max( 0, 0.5f - mapData[r+c*pr->mapHeight] ) * -Px(densityR[div],i,j);
					} else {
						support = max( 0, mapData[r+c*pr->mapHeight] - 0.5f ) * Px(densityR[div],i,j);
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
		offset[0] = (stateX - pr->mapUpdateLoc[0])*mapDivisions - origin[0]*scale*mapDivisions;
		offset[1] = (stateY - pr->mapUpdateLoc[1])*mapDivisions - origin[1]*scale*mapDivisions;
		if ( reading->value < pose->max )
			ImageAdd( densityR[div], mapUpdate, offset[0], offset[1], scale*mapDivisions, pfWeight[p]*MAP_UPDATE_STRENGTH*(1-MAP_UPDATE_RATIO), 1 );
		ImageAdd( absoluteR[div], mapUpdate, offset[0], offset[1], scale*mapDivisions, pfWeight[p]*MAP_UPDATE_STRENGTH*MAP_UPDATE_RATIO, 0 );

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
			obsDensity[p] = OBS_DENSITY_RATIO*absoluteOd[p]*absoluteNorm 
							+ (1-OBS_DENSITY_RATIO)*densityOd[p]*densityNorm
							+ OBS_DENSITY_AMBIENT;
		}

		// submit particle filter update
		this->ds.reset();
		this->ds.packUUID( &this->sensorAvatar[ pr->sensor ] );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packData( obsDensity, sizeof(float)*pr->pfNum );
		//this->sendMessage( this->hostCon, MSG_DDB_APPLYPFCORRECTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	// submit map update
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorSonar)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorSonar)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorSonar)->mapResolution );
	this->ds.packData( this->mapUpdate->data, sizeof(float)*this->mapUpdate->rows*this->mapUpdate->cols );
	this->sendMessage( this->hostCon, MSG_DDB_APPLYPOGUPDATE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// finish reading
	this->finishReading( pr, 1 );

	return 0;
}

float AgentSensorSonar::templateScale( float d, float max, float sig ) {
	return 0.7071f * (min( d, max ) + 3*sig*sig)/TEMPLATE_SIZE_DIV2;
}

int AgentSensorSonar::generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT ) {
	int i, j;
	float r, t;
	float coverage;
	float var = sig*sig;
	float norm = ( 1/(sig*sqrt(2*fM_PI)) );
	float expon;
	bool skipDensity;

	if ( d > max ) 
		d = max;

	float scale = templateScale( d, max, sig );
	
	int cols = (int)ceil(0.7071f * TEMPLATE_SIZE);
	densityT->cols = cols;
	absoluteT->cols = cols;

	if ( d < max ) {
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			skipDensity = false;
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );
					
					if ( skipDensity ) {
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
					} else {
						expon = exp( -(d-r)*(d-r)/(2*var) );
						if ( expon < 1E-15 ) {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
							Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
							if ( r < d ) // early exit
								skipDensity = true;
						} else {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) 
								= Px(densityT,TEMPLATE_SIZE_DIV2+i,j) 
								= - scale * 0.45f 
								* ( 1/(2*r*a) ) 
								* norm * expon;
						}
					}

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	} else { // no density
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	}

	return 0;
}


int AgentSensorSonar::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid, thread;

	int offset;
	int type;
	char evt;
	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	offset = 4 + sizeof(UUID)*2 + 1;
	if ( evt == DDBE_WATCH_TYPE ) {
		if ( type == DDB_AVATAR ) {
			// request list of avatars
			UUID thread = this->conversationInitiate( AgentSensorSonar_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}	
			sds.reset();
			sds.packUUID( this->getUUID() ); // dummy id 
			sds.packInt32( DDBAVATARINFO_ENUM );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_AVATAR ) {
		if ( evt == DDBE_ADD ) {
			// add avatar
			this->avatars[uuid].ready = false;
			this->avatars[uuid].start.time = 0;
			this->avatars[uuid].retired = 0;

			// request avatar info
			thread = this->conversationInitiate( AgentSensorSonar_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &uuid ); 
			sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAVATARINFO_RETIRE ) {
				// request avatar info
				thread = this->conversationInitiate( AgentSensorSonar_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				if ( thread == nilUUID ) {
					return 1;
				}
				sds.reset();
				sds.packUUID( &uuid ); 
				sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		} else if ( evt == DDBE_REM ) {
			// TODO
		}
	}
	lds.unlock();
	
	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentSensorSonar::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case AgentSensorProcessing_MSGS::MSG_CONFIGURE:
		lds.setData( data, len );
		this->configureParameters( &lds );
		lds.unlock();
		break;	
	case AgentSensorProcessing_MSGS::MSG_ADD_READING:
		{
			_timeb tb;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			this->addReading( &uuid, &tb );
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool AgentSensorSonar::convRequestMapInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestMapInfo: request timed out" );
		
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) {
		this->ds.unpackFloat32(); // tilesize
		STATE(AgentSensorSonar)->mapResolution = this->ds.unpackFloat32();
		this->ds.unpackInt32(); // stride
		this->ds.unlock();

		this->backup(); // mapResolution
	} else {
		this->ds.unlock();
		Log.log( 0, "AgentSensorSonar::convRequestMapInfo: request failed!" );
		UuidCreateNil( &STATE(AgentSensorSonar)->mapId );
		return 0;
	}

	return 0;
}

bool AgentSensorSonar::convRequestSensorInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	UUID avatar;
	int poseSize;
	UUID poseRef;
	UUID thread;
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::convRequestSensorInfo: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestSensorInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
  
		if ( this->sensorPose.find( pr->sensor ) == this->sensorPose.end() ) { // make sure we didn't already get this pose
			// handle info
			infoFlags = this->ds.unpackInt32();
			if ( infoFlags != (DDBSENSORINFO_PF | DDBSENSORINFO_POSE) ) {
				this->ds.unlock();
				this->finishReading( pr, 0 );
				return 0; // what happened?
			}

			this->ds.unpackUUID( &avatar );
			this->sensorAvatar[ pr->sensor ] = avatar;

			poseSize = this->ds.unpackInt32();
			poseRef = newDynamicBuffer( poseSize );
			if ( poseRef == nilUUID ) { // malloc failed
				this->ds.unlock();
				this->finishReading( pr, 0 );
				return 0;
			}
			memcpy( getDynamicBuffer( poseRef ), this->ds.unpackData( poseSize ), poseSize );

			if ( this->addPose( &pr->sensor, &poseRef ) )
				freeDynamicBuffer( poseRef );
		
			this->backup(); // sensorAvatar, sensorPose
		}
		this->ds.unlock();
			
		pr->waiting &= ~WAITING_ON_POSE;

		// request pf
		thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
		if ( thread == nilUUID ) {
			this->finishReading( pr, 0 );
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID( &avatar );
		this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorSonar::convRequestPFInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	char response;
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::convRequestPFInfo: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestPFInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT) ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		pr->pfNum = this->ds.unpackInt32();
		pr->pfStateSize = this->ds.unpackInt32();
		pr->pfStateRef = newDynamicBuffer( pr->pfNum*pr->pfStateSize*sizeof(float) );
		if ( pr->pfStateRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		pr->pfWeightRef = newDynamicBuffer( pr->pfNum*sizeof(float) );
		if ( pr->pfWeightRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( pr->pfStateRef ), this->ds.unpackData( pr->pfNum*pr->pfStateSize*sizeof(float) ), pr->pfNum*pr->pfStateSize*sizeof(float) );
		memcpy( getDynamicBuffer( pr->pfWeightRef ), this->ds.unpackData( pr->pfNum*sizeof(float) ), pr->pfNum*sizeof(float) );

		this->ds.unlock();

		pr->waiting &= ~WAITING_ON_PF;

		if ( !(pr->waiting & WAITING_ON_DATA) ) { // ready to get the map now
			// request map data
			this->requestMapRegion( pr );
		}

	} else if ( response == DDBR_TOOLATE )  {
		this->ds.unlock();
		// wait 250 ms and try again
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorSonar_CBR_cbRepeatRPFInfo, &pr->id, sizeof(int) );
	} else if ( response == DDBR_TOOEARLY ) {
		this->ds.unlock();
		this->finishReading( pr, -1 ); // permenant failure
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorSonar::cbRepeatRPFInfo( void *vpid ) {
	UUID thread;
	PROCESS_READING *pr;
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)vpid);

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::cbRepeatRPFInfo: reading not found %d", *(int*)vpid );
		return 0;
	}

	pr = &iR->second;

	if ( pr->retries > 6*(int)this->avatars.size() ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	pr->retries++;

	// request PF
	thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &this->sensorAvatar[ pr->sensor ] );
	this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

bool AgentSensorSonar::convRequestAvatarLoc( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	char response;
	mapReadings::iterator iR;
	int rId;
	UUID aId;

	this->ds.setData( (char *)conv->data, conv->dataLen );
	rId = this->ds.unpackInt32();
	this->ds.unpackUUID( &aId );
	this->ds.unlock();

	iR = this->readings.find( rId );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::convRequestAvatarLoc: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestAvatarLoc: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags == DDBPFINFO_CURRENT ) {
			this->ds.unpackData( sizeof(_timeb) ); // discard time
		} else if ( infoFlags != DDBPFINFO_MEAN ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		// unpack state
		float *state = (float *)this->ds.unpackData( sizeof(float)*3 );
		(*pr->avatarLocs)[aId].x = state[0];
		(*pr->avatarLocs)[aId].y = state[1];
		(*pr->avatarLocs)[aId].r = state[2];

		this->ds.unlock();

		pr->waitingOnAvatars--;

		if ( !pr->waiting && !pr->waitingOnAvatars ) { // ready to process reading
			this->processReading( pr );
		}

	} else if ( response == DDBR_TOOLATE )  {
		this->ds.unlock();
		// wait 250 ms and try again
		this->ds.reset();
		this->ds.packInt32( pr->id );
		this->ds.packUUID( &aId );
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorSonar_CBR_cbRepeatRAvatarLoc, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorSonar::cbRepeatRAvatarLoc( void *vpid ) {
	UUID thread;
	PROCESS_READING *pr;
	int rId;
	UUID aId;

	this->ds.setData( (char *)vpid, sizeof(int) + sizeof(UUID) );
	rId = this->ds.unpackInt32();
	this->ds.unpackUUID( &aId );
	this->ds.unlock();

	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)vpid);

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::cbRepeatRAvatarLoc: reading not found %d", *(int*)vpid );
		return 0;
	}

	pr = &iR->second;

	if ( pr->retries > 6*(int)this->avatars.size() ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	pr->retries++;

	// request PF
	this->ds.reset();
	this->ds.packInt32( rId );
	this->ds.packUUID( &aId );
	thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &this->avatars[aId].pf );
	this->ds.packInt32( DDBPFINFO_MEAN );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}


bool AgentSensorSonar::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentSensorSonar::convGetAvatarList: timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID thread;
		UUID avatarId;
		UUID agentId;
		AgentType agentType;
		int i, count;

		if ( lds.unpackInt32() != DDBAVATARINFO_ENUM ) {
			lds.unlock();
			return 0; // what happened here?
		}
		
		count = lds.unpackInt32();
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorSonar::convGetAvatarList: recieved %d avatars", count );

		for ( i=0; i<count; i++ ) {
			lds.unpackUUID( &avatarId );
			lds.unpackString(); // avatar type
			lds.unpackUUID( &agentId );
			lds.unpackUUID( &agentType.uuid );
			agentType.instance = lds.unpackInt32();

			// add avatar
			this->avatars[avatarId].ready = false;
			this->avatars[avatarId].start.time = 0;
			this->avatars[avatarId].retired = 0;

			// request avatar info
			thread = this->conversationInitiate( AgentSensorSonar_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( (UUID *)&avatarId ); 
			sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		}

		lds.unlock();

	} else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool AgentSensorSonar::convGetAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentSensorSonar::convGetAvatarInfo: timed out" );
		return 0; // end conversation
	}

	UUID avId = *(UUID *)conv->data;
	
	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID pfId;
		char retired;
		float innerR, outerR;
		_timeb startTime, endTime;

		if ( this->ds.unpackInt32() != (DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD) ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		this->ds.unpackUUID( &pfId );
		innerR = this->ds.unpackFloat32();
		outerR = this->ds.unpackFloat32();
		startTime = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
		retired = this->ds.unpackChar();
		if ( retired )
			endTime = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorSonar::convGetAvatarInfo: recieved avatar (%s) pf id: %s", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&pfId) );

		this->avatars[avId].pf = pfId;
		this->avatars[avId].start = startTime;
		if ( retired )
			this->avatars[avId].end = endTime;
		this->avatars[avId].retired = retired;
		this->avatars[avId].innerRadius = innerR;
		this->avatars[avId].outerRadius = outerR;

		this->avatars[avId].ready = true;

	} else {
		this->ds.unlock();

		// TODO try again?
	}

	return 0;
}

bool AgentSensorSonar::convRequestMap( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::convRequestMap: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestMap: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		float x, y, w, h;
		int cw, ch;
		x = this->ds.unpackFloat32();
		y = this->ds.unpackFloat32();
		w = this->ds.unpackFloat32();
		h = this->ds.unpackFloat32();
		STATE(AgentSensorSonar)->mapResolution = this->ds.unpackFloat32();

		cw = (int)floor( w / STATE(AgentSensorSonar)->mapResolution );
		ch = (int)floor( h / STATE(AgentSensorSonar)->mapResolution );

		pr->waiting &= ~WAITING_ON_MAP;

		pr->mapDataRef = newDynamicBuffer(cw*ch*4);
		if ( pr->mapDataRef == nilUUID ) {	
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( pr->mapDataRef ), this->ds.unpackData( cw*ch*4 ), cw*ch*4 );
		this->ds.unlock();

		pr->mapHeight = ch;

		// see if we're ready to process
		if ( !pr->waiting && !pr->waitingOnAvatars )
			this->processReading( pr );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorSonar::convRequestData( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorSonar::convRequestData: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorSonar::convRequestData: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		pr->readingSize = this->ds.unpackInt32();
		pr->readingRef = newDynamicBuffer( pr->readingSize );
		if ( pr->readingRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		memcpy( getDynamicBuffer( pr->readingRef ), this->ds.unpackData( pr->readingSize ), pr->readingSize );

		pr->dataSize = this->ds.unpackInt32();
		if ( pr->dataSize ) {
			pr->dataRef = newDynamicBuffer( pr->dataSize );
			if ( pr->dataRef == nilUUID ) {
				this->ds.unlock();
				freeDynamicBuffer( pr->readingRef );
				this->finishReading( pr, 0 );
				return 0;
			}
			memcpy( getDynamicBuffer( pr->dataRef ), this->ds.unpackData( pr->dataSize ), pr->dataSize );
		}
		this->ds.unlock();

		pr->waiting &= ~WAITING_ON_DATA;

		if ( !(pr->waiting & WAITING_ON_PF) ) { // ready to get the map now
			// request map data
			this->requestMapRegion( pr );
		}

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AgentSensorSonar::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AgentSensorSonar::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	AgentSensorSonar::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentSensorSonar);

	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorPose );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorAvatar );
	mapReadings::iterator iR;
	for ( iR = this->readings.begin(); iR != this->readings.end(); iR++ ) {
		ds->packBool( 1 );
		ds->packInt32( iR->first ); 
		ds->packData( (void *)&iR->second, sizeof(PROCESS_READING) ); 
		_WRITE_STATE_MAP_LESS( UUID, AVATAR_LOC, UUIDless, iR->second.avatarLocs );
	}
	ds->packBool( 0 );

	_WRITE_STATE_MAP_LESS( UUID, AVATAR_INFO, UUIDless, &this->avatars );

	return AgentBase::writeState( ds, false );;
}

int	AgentSensorSonar::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentSensorSonar);

	_READ_STATE_MAP( UUID, UUID, &this->sensorPose );
	_READ_STATE_MAP( UUID, UUID, &this->sensorAvatar );
	int prId;
	PROCESS_READING pr;
	while ( ds->unpackBool() ) {
		prId = ds->unpackInt32();
		pr = *(PROCESS_READING *)ds->unpackData( sizeof(PROCESS_READING) );
		pr.avatarLocs = new std::map<UUID,AVATAR_LOC,UUIDless>;
		_READ_STATE_MAP( UUID, AVATAR_LOC, pr.avatarLocs );
		this->readings[prId] = pr;
	}

	_READ_STATE_MAP( UUID, AVATAR_INFO, &this->avatars );

	return AgentBase::readState( ds, false );
}


int AgentSensorSonar::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		// request list of avatars
		UUID thread = this->conversationInitiate( AgentSensorSonar_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();

		// make sure we have map resolution
		if ( STATE(AgentSensorSonar)->mapResolution == 0 ) {
			// get map info
			UUID thread = this->conversationInitiate( AgentSensorSonar_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &STATE(AgentSensorSonar)->mapId );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	return 0;
}

int AgentSensorSonar::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(AgentSensorSonar)->ownerId );

	ds->packUUID( &STATE(AgentSensorSonar)->mapId );
	ds->packFloat32( STATE(AgentSensorSonar)->mapResolution );

	mapPose::iterator iP;
	for ( iP = this->sensorPose.begin(); iP != this->sensorPose.end(); iP++ ) {
		ds->packBool( 1 );
		ds->packUUID( (UUID *)&iP->first );
		ds->packInt32( this->stateBuffers[iP->second].size );
		ds->packData( this->stateBuffers[iP->second].buf, this->stateBuffers[iP->second].size );
	}
	ds->packBool( 0 );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorAvatar );
	
	return AgentBase::writeBackup( ds );
}

int AgentSensorSonar::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(AgentSensorSonar)->ownerId );

	ds->unpackUUID( &STATE(AgentSensorSonar)->mapId );
	STATE(AgentSensorSonar)->mapResolution = ds->unpackFloat32();

	UUID sensorId;
	int size;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &sensorId );
		size = ds->unpackInt32();
		this->sensorPose[sensorId] = this->newDynamicBuffer( size );
		memcpy( this->getDynamicBuffer( this->sensorPose[sensorId] ), ds->unpackData( size ), size );
	}
	_READ_STATE_MAP( UUID, UUID, &this->sensorAvatar );

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AgentSensorSonar *agent = (AgentSensorSonar *)vpAgent;

	if ( agent->configure() ) {
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();
	delete agent;

	return 0;
}

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentSensorSonar *agent = new AgentSensorSonar( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	HANDLE hThread;
	DWORD  dwThreadId;

	hThread = CreateThread( 
        NULL,                   // default security attributes
        0,                      // use default stack size  
        RunThread,				// thread function name
        agent,					// argument to thread function 
        0,                      // use default creation flags 
        &dwThreadId);			// returns the thread identifier 

	if ( hThread == NULL ) {
		delete agent;
		return 1;
	}

	return 0;
}

int Playback( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentSensorSonar *agent = new AgentSensorSonar( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	if ( agent->configure() ) {
		delete agent;
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();

	printf_s( "Playback: agent finished\n" );

	delete agent;

	return 0;
}


// CAgentSensorSonarDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAgentSensorSonarDLL, CWinApp)
END_MESSAGE_MAP()

CAgentSensorSonarDLL::CAgentSensorSonarDLL() {}

// The one and only CAgentSensorSonarDLL object
CAgentSensorSonarDLL theApp;

int CAgentSensorSonarDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentSensorSonarDLL ---\n"));
  return CWinApp::ExitInstance();
}