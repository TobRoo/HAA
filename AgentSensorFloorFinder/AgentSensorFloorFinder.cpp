// AgentSensorFloorFinder.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\fImage.h"

#include "AgentSensorFloorFinder.h"
#include "AgentSensorFloorFinderVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\SupervisorSLAM\\SupervisorSLAMVersion.h"

#include "..\\include\\CxImage\\ximage.h"

#include "..\RoboRealmAPI\RR_API.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define DO_VISUALIZE


void dumpImageBWFloat32( char *name, int w, int h, float *data, float minVal = 0, float maxVal = 1, bool flipH = false, bool flipV = false, bool orderByRows = true, int dataStride = -1 ) {

	float valRange = maxVal - minVal;
	if ( dataStride == -1 ) {
		if ( orderByRows ) dataStride = w;
		else			   dataStride = h;
	}

	CxImage img(w,h,8,CXIMAGE_FORMAT_BMP);
	CxMemFile memfile;
	memfile.Open();

	img.SetGrayPalette();

	bool indexed = img.IsIndexed();
	bool greyscale = img.IsGrayScale();

	img.Encode(&memfile,CXIMAGE_FORMAT_BMP);

	char *imgBuf = (char *)memfile.GetBuffer();
	int imgSize = memfile.Size();
	char *pxBuf = imgBuf + *(int *)(imgBuf+0x000A);

	int imgStride = (imgSize - *(int *)(imgBuf+0x000A))/h;

	int c, r;
	int cc, rr;

	for ( r=0; r<h; r++ ) {
		if ( !flipV ) rr = r;
		else          rr = h-r-1;
		for ( c=0; c<w; c++ ) {
			if ( !flipH ) cc = c;
			else          cc = w-c-1;
			if ( orderByRows ) pxBuf[c+r*imgStride] = (char)( ( (data[rr+cc*dataStride]-minVal)/valRange) * 255 );
			else               pxBuf[c+r*imgStride] = (char)( ( (data[cc*dataStride+rr]-minVal)/valRange) * 255 );
			r=r;
		}
	}

	FILE *fp;
	if ( !fopen_s( &fp, name, "wb" ) ) {

		fwrite( imgBuf, 1, imgSize, fp );

		fclose( fp );
	}

}

//*****************************************************************************
// AgentSensorFloorFinder

//-----------------------------------------------------------------------------
// Constructor	
AgentSensorFloorFinder::AgentSensorFloorFinder( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	int i;

	// allocate state
	ALLOCATE_STATE( AgentSensorFloorFinder, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentSensorFloorFinder" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentSensorFloorFinder_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(AgentSensorFloorFinder)->mapId );

	STATE(AgentSensorFloorFinder)->visHitCount = 0;

	STATE(AgentSensorFloorFinder)->readingNextId = 0;

	// Prepare callbacks
	this->callback[AgentSensorFloorFinder_CBR_convRequestMapInfo] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestMapInfo);
	this->callback[AgentSensorFloorFinder_CBR_convRequestSensorInfo] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestSensorInfo);
	this->callback[AgentSensorFloorFinder_CBR_convRequestPFInfo] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestPFInfo);
	this->callback[AgentSensorFloorFinder_CBR_cbRepeatRPFInfo] = NEW_MEMBER_CB(AgentSensorFloorFinder,cbRepeatRPFInfo);
	this->callback[AgentSensorFloorFinder_CBR_convRequestAvatarLoc] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestAvatarLoc);
	this->callback[AgentSensorFloorFinder_CBR_cbRepeatRAvatarLoc] = NEW_MEMBER_CB(AgentSensorFloorFinder,cbRepeatRAvatarLoc);
	this->callback[AgentSensorFloorFinder_CBR_convGetAvatarList] = NEW_MEMBER_CB(AgentSensorFloorFinder,convGetAvatarList);
	this->callback[AgentSensorFloorFinder_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(AgentSensorFloorFinder,convGetAvatarInfo);
	this->callback[AgentSensorFloorFinder_CBR_convRequestMap] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestMap);
	this->callback[AgentSensorFloorFinder_CBR_convRequestData] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestData);
	this->callback[AgentSensorFloorFinder_CBR_convRequestFreePort] = NEW_MEMBER_CB(AgentSensorFloorFinder,convRequestFreePort);
	this->callback[AgentSensorFloorFinder_CBR_cbQueueNextImage] = NEW_MEMBER_CB(AgentSensorFloorFinder,cbQueueNextImage);

	this->integralTarget = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	this->cameraContours = NewImage( 10, 10 );
	this->cameraT = NewImage( (int)(CAMERA_TEMPLATE_WIDTH*CAMERA_TEMPLATE_DIVISIONS), (int)(CAMERA_TEMPLATE_LENGTH*CAMERA_TEMPLATE_DIVISIONS) );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		this->cameraR[i] = NewImage( (int)(2*CAMERA_TEMPLATE_LENGTH*CAMERA_TEMPLATE_DIVISIONS), (int)(2*CAMERA_TEMPLATE_LENGTH*CAMERA_TEMPLATE_DIVISIONS) );
	}
	this->cameraProcessed = NewImage( (int)(2*CAMERA_TEMPLATE_LENGTH*CAMERA_TEMPLATE_DIVISIONS), (int)(2*CAMERA_TEMPLATE_LENGTH*CAMERA_TEMPLATE_DIVISIONS) );
	this->mapUpdate = NewImage( 64, 64 );

	this->sharedArraySize = 0;
	this->absoluteOd = NULL;
	this->obsDensity = NULL;
}

//-----------------------------------------------------------------------------
// Destructor
AgentSensorFloorFinder::~AgentSensorFloorFinder() {
	int i;

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	FreeImage( this->integralTarget );
	FreeImage( this->cameraContours );
	FreeImage( this->cameraT );
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		FreeImage( this->cameraR[i] );
	}
	FreeImage( this->cameraProcessed );
	FreeImage( this->mapUpdate );
}

//-----------------------------------------------------------------------------
// Configure

int AgentSensorFloorFinder::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		RPC_WSTR rpc_wstr;
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		UuidToString( &STATE(AgentBase)->uuid, &rpc_wstr );
		sprintf_s( logName, "%s\\AgentSensorFloorFinder %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );

#ifdef	NO_LOGGING
		Log.setLogMode(LOG_MODE_OFF);
		Log.setLogLevel(LOG_LEVEL_NONE);
#endif

		Log.log( 0, "AgentSensorFloorFinder %.2d.%.2d.%.5d.%.2d", AgentSensorFloorFinder_MAJOR, AgentSensorFloorFinder_MINOR, AgentSensorFloorFinder_BUILDNO, AgentSensorFloorFinder_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentSensorFloorFinder::start( char *missionFile ) {
	DataStream lds;
	RPC_STATUS status;

	if ( UuidIsNil( &STATE(AgentSensorFloorFinder)->mapId, &status ) ) {
		Log.log( 0, "AgentSensorFloorFinder::start: MSG_CONFIGURE must be sent before starting!" );
		return 1;
	}

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars once DDBE_WATCH_TYPE notification is received

	// add path
	float x[] = { 0, 1 };
	float y[] = { 0, 0 };
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( VIS_PATH_LINE );
	lds.packInt32( 2 );
	lds.packData( x, 2*sizeof(float) );
	lds.packData( y, 2*sizeof(float) );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDPATH, lds.stream(), lds.length() );
	lds.unlock();

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentSensorFloorFinder::stop() {

	// clean up poses
	mapPose::iterator iterP = this->sensorPose.begin();
	while ( iterP != this->sensorPose.end() ) {
		freeDynamicBuffer( iterP->second );
		iterP++;
	}
	this->sensorPose.clear();

	// clean up roborealm
	mapRoboRealm::iterator iterRR = this->sensorRR.begin();
	while ( iterRR != this->sensorRR.end() ) {
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::stop: skipping closing RR" );
		/*
		int ret = iterRR->second.api->close();
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::stop: closing RR, port %d (result %d)", iterRR->second.port, ret );
		delete iterRR->second.api;
		*/
		this->releasePort( iterRR->second.port );
		
		iterRR++;
	}
	this->sensorRR.clear();

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
		free( this->obsDensity );
		this->sharedArraySize = 0;
	}

	// clear vis
	if ( !this->frozen ) {
		this->visualizeClear();
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_PATH_LINE );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentSensorFloorFinder::step() {
	return AgentBase::step();
}

int AgentSensorFloorFinder::configureParameters( DataStream *ds ) {
	DataStream lds;
	UUID uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorFloorFinder)->ownerId = uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorFloorFinder)->mapId = uuid;

	// get map info
	UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	lds.reset();
	lds.packUUID( &STATE(AgentSensorFloorFinder)->mapId );
	lds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
	lds.unlock();

	this->backup(); // mapId

	return 0;
}

int AgentSensorFloorFinder::addPose( UUID *sensor, UUID *poseRef ) {
	this->sensorPose[ *sensor ] = *poseRef;

	this->initializeRoboRealm( sensor );

	return 0;
}

int AgentSensorFloorFinder::addReading( UUID *sensor, _timeb *time ) {
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

	if ( STATE(AgentSensorFloorFinder)->mapResolution == 0 || this->avatars.size() == 0 ||  iA != this->avatars.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::addReading: info not available yet, fail reading" );

		// notify owner
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packUUID( sensor );
		this->ds.packData( time, sizeof(_timeb) );
		this->ds.packChar( 0 );
		this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorFloorFinder)->ownerId );
		this->ds.unlock();

		return 1;
	}

	pr.id = STATE(AgentSensorFloorFinder)->readingNextId++;
	pr.waiting = 0;
	pr.sensor = *sensor;
	pr.time = *time;
	pr.retries = 0;
 
	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorFloorFinder::addReading: %s", Log.formatUUID(LOG_LEVEL_VERBOSE,&pr.sensor) );

	pr.waiting |= WAITING_ON_PF;

	// get avatar and pose, or PF if avatar and pose are already known
	mapPose::iterator iterP = this->sensorPose.find( *sensor );
	if ( iterP == this->sensorPose.end() ) {
		pr.waiting |= WAITING_ON_POSE;
		// request avatar/pose
		thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestSensorInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
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
		thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
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
		thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
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

	// flag map
	pr.waiting |= WAITING_ON_MAP;
	
	// get Data
	pr.waiting |= WAITING_ON_DATA;
	// request data
	thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestData, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( sensor );
	this->ds.packData( time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RSENSORDATA, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	
	// add reading
	pr.avatarLocs = new std::map<UUID,AVATAR_LOC,UUIDless>;
	this->readings[pr.id] = pr;

	return 0;
}

int AgentSensorFloorFinder::finishReading( PROCESS_READING *pr, char success ) {

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorFloorFinder::finishReading: %s success == %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&pr->sensor), success );

	// notify owner
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &pr->sensor );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packChar( success );
	this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorFloorFinder)->ownerId );
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

int AgentSensorFloorFinder::requestMapRegion( PROCESS_READING *pr ) {

	int border;
	int p;

	float PFbound[4];
	int	  mapDivisions = (int)floor(1/STATE(AgentSensorFloorFinder)->mapResolution);
	
	CameraPose *pose = (CameraPose *)getDynamicBuffer( this->sensorPose[ pr->sensor ] );
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
	border = (int)ceil(5 + mapDivisions*CAMERA_TEMPLATE_LENGTH);
	pr->mapUpdateLoc[0] = PFbound[0] - border/(float)mapDivisions;
	pr->mapUpdateLoc[1] = PFbound[1] - border/(float)mapDivisions;
	pr->mapUpdateSize[0] = (int)(PFbound[2]*mapDivisions)+border*2;
	pr->mapUpdateSize[1] = (int)(PFbound[3]*mapDivisions)+border*2;

	// request map
	UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestMap, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorFloorFinder)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorFloorFinder)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorFloorFinder)->mapResolution );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentSensorFloorFinder::processReading( PROCESS_READING *pr ) {
	ROBOREALM_INSTANCE *rr;

	rr = &this->sensorRR[ pr->sensor ];
	rr->queue.push_back( pr->id );

	this->nextImage( rr );

	return 0;
}

int AgentSensorFloorFinder::nextImage( ROBOREALM_INSTANCE *rr ) {
	int readingId;
	PROCESS_READING *pr;
	int mapDivisions = (int)floor(1/STATE(AgentSensorFloorFinder)->mapResolution);
	
	CameraPose *pose;

	int i, j, p, a;
	int r, c;
	int div;
	float sn, cs;
	float stateX, stateY, stateR;
	float avgX, avgY, avgR;
	float dx, dy;

	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate
	bool  rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date

	float penalty;
	float maxAbsoluteOd;
	float absoluteNorm;

	if ( !rr->ready || rr->queue.empty() )
		return 0;

	readingId = rr->queue.front();
	
	mapReadings::iterator iR;

	iR = this->readings.find( readingId );

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::nextImage: reading not found %d", readingId );
		rr->queue.pop_front();
		return this->nextImage( rr );
	}

	pr = &iR->second;

	float *mapData = (float *)getDynamicBuffer( pr->mapDataRef );

	pose = (CameraPose *)getDynamicBuffer( this->sensorPose[ pr->sensor ] );

	CameraReading *reading = (CameraReading *)getDynamicBuffer( pr->readingRef );
	char *data = (char *)getDynamicBuffer( pr->dataRef );
	float *pfState = (float *)getDynamicBuffer( pr->pfStateRef );
	float *pfWeight = (float *)getDynamicBuffer( pr->pfWeightRef );

	// generate template
	std::list<HIT> hits;
	if ( this->generateCameraTemplate( reading, pose, rr, data, pr->dataSize, this->cameraT, this->cameraContours, &hits ) ) {
		// finish reading
		mapReadings::iterator iterR = this->readings.find( readingId );
		if ( iterR == this->readings.end() )
			return 1; // how did this happen?

		this->finishReading( &iterR->second, 0 );

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

	// clear the rotationDivision flags
	memset( rotationDivision, 0, sizeof(rotationDivision) );
	
	// make sure arrays are big enough
	if ( this->sharedArraySize < pr->pfNum ) {
		if ( this->sharedArraySize ) {
			free( this->absoluteOd );
			free( this->obsDensity );
		}
		this->sharedArraySize = pr->pfNum;
		this->absoluteOd = (float *)malloc( sizeof(float)*this->sharedArraySize );		
		if ( !this->absoluteOd ) {
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
		this->obsDensity = (float *)malloc( sizeof(float)*this->sharedArraySize );
		if ( !this->obsDensity ) {
			free( this->absoluteOd );
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
	}

	// prepare avatar locs
	float avatarLocs[64][2];
	float avatarObstructionSq[64];
	int avatarCount = 0;
	std::map<UUID,AVATAR_LOC,UUIDless>::iterator iL;
	for ( iL = pr->avatarLocs->begin(); iL != pr->avatarLocs->end(); iL++ ) {
		if ( this->avatars[iL->first].pf == this->sensorAvatar[ pr->sensor ] ) // this is us
			continue; // skip		
		avatarLocs[avatarCount][0] = iL->second.x/STATE(AgentSensorFloorFinder)->mapResolution;
		avatarLocs[avatarCount][1] = iL->second.y/STATE(AgentSensorFloorFinder)->mapResolution;
		avatarObstructionSq[avatarCount] = (this->avatars[iL->first].outerRadius + READING_OBSTRUCTION_THRESHOLD)/STATE(AgentSensorFloorFinder)->mapResolution;
		avatarObstructionSq[avatarCount] = avatarObstructionSq[avatarCount]*avatarObstructionSq[avatarCount];
		avatarCount++;	
		if ( avatarCount == 64 )
			break;
	}

	// for each particle
	avgX = avgY = avgR = 0;
	maxAbsoluteOd = 0;
	for ( p=0; p<pr->pfNum; p++ ) {
		// calculate average
		avgX += pfStateX(pfState,p)*pfWeight[p];
		avgY += pfStateY(pfState,p)*pfWeight[p];
		avgR += pfStateR(pfState,p)*pfWeight[p];

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
			RotateImageEx( cameraT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), cameraR[div], integralTarget, 0 );
			rotationDivision[div] = true;
		}
		origin[0] = cameraR[div]->cols/2.0f - cameraT->cols/2.0f*(float)cos(div*ROTATION_RESOLUTION);
		origin[1] = cameraR[div]->rows/2.0f - cameraT->cols/2.0f*(float)sin(div*ROTATION_RESOLUTION);

	
		// process and compare against map
		CopyImageEx( cameraR[div], cameraProcessed );
		offset[0] = (stateX - pr->mapUpdateLoc[0])*mapDivisions - origin[0]/CAMERA_TEMPLATE_DIVISIONS*mapDivisions;
		offset[1] = (stateY - pr->mapUpdateLoc[1])*mapDivisions - origin[1]/CAMERA_TEMPLATE_DIVISIONS*mapDivisions;
		absoluteOd[p] = 0;
		for ( j=0; j<cameraProcessed->cols; j++ ) {
			c = (int)(j*mapDivisions/(float)CAMERA_TEMPLATE_DIVISIONS + offset[0]);
			if ( c < 0 || c >= pr->mapWidth )
				continue; // outside the mapData
			for ( i=0; i<cameraProcessed->rows; i++ ) {
				r = (int)(i*mapDivisions/(float)CAMERA_TEMPLATE_DIVISIONS + offset[1]);
				if ( r < 0 || r >= pr->mapHeight )
					continue; // outside the mapData

				// blank out area around avatars
				for ( a = 0; a < avatarCount; a++ ) {
					dx = avatarLocs[a][0] - (pr->mapUpdateLoc[0]*mapDivisions + c); // avatar loc - cell position
					dy = avatarLocs[a][1] - (pr->mapUpdateLoc[1]*mapDivisions + r);
					if ( dx*dx + dy*dy < avatarObstructionSq[a] ) { // too close to avatar
						Px(cameraProcessed,i,j) = 0; // blank
					}
				}
		
				// update absoluteOd, total penalty
				if ( Px(cameraProcessed,i,j) < 0 ) {
					penalty = max( 0, mapData[r+c*pr->mapHeight] - 0.5f ) * -Px(cameraProcessed,i,j);
				} else {
					penalty = max( 0, 0.5f - mapData[r+c*pr->mapHeight] ) * Px(cameraProcessed,i,j);
				}
				absoluteOd[p] += penalty;
			}
		}
		maxAbsoluteOd = max( maxAbsoluteOd, absoluteOd[p] );

		// TEMP
		//offset[0] = (stateX - mapLoc[0])*mapDivisions;
		//offset[1] = (stateY - mapLoc[1])*mapDivisions;
		//map[(int)offset[0]][(int)offset[1]] = 0;

		// stamp on update
		float absoluteScale = MAP_UPDATE_STRENGTH;
		ImageAdd( cameraProcessed, mapUpdate, offset[0], offset[1], mapDivisions/(float)CAMERA_TEMPLATE_DIVISIONS, pfWeight[p]*absoluteScale, 0 );
		
		// TEMP
		//this->visualizer->deletefImage( this->mapUpdateImage );
		//this->mapUpdateImage = this->visualizer->newfImage( mapUpdateLoc[0], mapUpdateLoc[1], 1.0f/mapDivisions, 0, 50, this->mapUpdate );
		//offset[0] = (stateX - mapUpdateLoc[0])*mapDivisions;
		//offset[1] = (stateY - mapUpdateLoc[1])*mapDivisions;
		//Px(mapUpdate,(int)offset[1],(int)offset[0]) = 1;
	}

	// compute observation density
	if ( maxAbsoluteOd > 0 ) {
		absoluteNorm = 1/maxAbsoluteOd;
	
		for ( p=0; p<pr->pfNum; p++ ) {
			obsDensity[p] = absoluteOd[p]*absoluteNorm;
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
	this->ds.packUUID( &STATE(AgentSensorFloorFinder)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorFloorFinder)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorFloorFinder)->mapResolution );
	this->ds.packData( this->mapUpdate->data, sizeof(float)*this->mapUpdate->rows*this->mapUpdate->cols );
	this->sendMessage( this->hostCon, MSG_DDB_APPLYPOGUPDATE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// visualize
	this->visualizeHits( avgX, avgY, avgR, pose, &hits );

	// finish reading
	mapReadings::iterator iterR = this->readings.find( readingId );
	if ( iterR == this->readings.end() )
		return 1; // how did this happen?

	this->finishReading( &iterR->second, 1 );

	if ( !rr->queue.empty() ) // queue next image after processing new messages
		this->addTimeout( 0, AgentSensorFloorFinder_CBR_cbQueueNextImage, &rr->sensor, sizeof(UUID) );

	return 0;
}

int charHexToInt( char *hex ) {
	char high, low;
	if ( hex[0] >= 'a' )		high = hex[0] - 'a' + 10;
	else if ( hex[0] >= 'A' )	high = hex[0] - 'A' + 10;
	else						high = hex[0] - '0';
	if ( hex[1] >= 'a' )		low = hex[1] - 'a' + 10;
	else if ( hex[1] >= 'A' )	low = hex[1] - 'A' + 10;
	else						low = hex[1] - '0';
	return high*16 + low;
}

void applyHomography( float *H, float px, float py, float *qx, float *qy ) {
	float norm = 1.0f/(H[6]*px + H[7]*py + H[8]);
	*qx = (H[0]*px + H[1]*py + H[2])*norm;
	*qy = (H[3]*px + H[4]*py + H[5])*norm;
}

int AgentSensorFloorFinder::generateCameraTemplate( CameraReading *reading, CameraPose *pose, ROBOREALM_INSTANCE *rr, char *imageData, int imageSize, FIMAGE *cameraT, FIMAGE *cameraContours, std::list<HIT> *hits ) {
	unsigned char pixels[256000];
	char colourStr[256];
	unsigned char high; // highest pixel value we expect in the processed image
	int width, height, pstride;
	int r, c;
	float var = pose->sigma*pose->sigma; // variances (in floor space)
	float expon;
	float norm;
	int bmpoffset;

	bool simStream;

	// flag roborealm instance as unavailable
	rr->ready = false;
	
	// fake the normal distribution norm so that anything below 0.1f has a peak at 1, 
	// and everything larger than that is smaller
	if ( pose->sigma < 0.1f ) {
		norm = 1.0f;
	} else {
		norm = 0.1f/pose->sigma;
	}	

	// make sure horizon is set correctly
	if ( rr->horizon != (int)(reading->h*pose->horizon) ) {
		char buf[256];	
		rr->horizon = (int)(reading->h*pose->horizon);
		sprintf_s( buf, 256, "%d", rr->horizon );
		rr->api->setVariable( "CROPHORIZON", buf );
	}

	// setImage in RR
	if ( imageSize == 0 ) 
		return 1; // where's the image?

	if ( !strcmp( reading->format, "bmp" ) ) {
		bmpoffset = *(int *)(imageData+0x000A);
		rr->api->setImage( (unsigned char *)imageData + bmpoffset, reading->w, reading->h,  true );
	} else if ( !strcmp( reading->format, "jpg" ) ) {
		CxImage image((BYTE*)imageData,imageSize,CXIMAGE_FORMAT_JPG);
		CxMemFile memfile;
		memfile.Open();
		image.Encode(&memfile,CXIMAGE_FORMAT_BMP);
		BYTE *encbuffer = memfile.GetBuffer();
		
		bmpoffset = *(int *)(encbuffer+0x000A);
		rr->api->setImage( (unsigned char *)encbuffer + bmpoffset, reading->w, reading->h,  true );

		image.FreeMemory(encbuffer);
		memfile.Close();
	} else if ( !strcmp( reading->format, "stream" ) ) { // simulation input
		simStream = true;
	} else {
		return 1; // unsupported format
	}

	if ( !simStream ) {
		// getImage from RR
		if ( !rr->api->getImage( "processed", pixels, &width, &height, sizeof(pixels) ) ) {
			return 1; // get image failed
		}
		rr->api->getVariable( "COLOR_SAMPLE", colourStr, sizeof(colourStr) );
	}

	// flag roborealm instance as available
	rr->ready = true;
	rr->queue.pop_front(); // remove reading from queue now that RR has processed it

	// prepare camera contours
	if ( !simStream ) { // process image
		high = (charHexToInt(colourStr) + charHexToInt(colourStr+2) + charHexToInt(colourStr+4))/3;

		// create vertical line contours
		// Notes:
		// 1. pixel format is RGB horizontal from top left corner
		// 2. the R value of the first pixel gets overwritten with \0 by RR due to a bug
		// 3. the FLOORFINDER script scales the image horizontally to 10% and rotates it 270 degrees
		//    (i.e. for vertical camera lines just read along the pixels)
		if ( width*height > cameraContours->bufSize/(int)sizeof(float) ) { // we need more space
			ReallocImage( cameraContours, height, width );
		} else {
			cameraContours->rows = height;
			cameraContours->cols = width;
			cameraContours->stride = cameraContours->rows;
		}
		memset( cameraContours->data, 0, width*height*sizeof(float) );

		float val;
		float cutoffC, cutoffX, cutoffY, cutoffU, cutoffV, cutoffD;	// cutoff pixel coordinates (top of the hill)
		float startC, startX, startY, startU, startV, startD;		// start of the hill and gaussian
		float endC, endX, endY, endU, endV, endD;					// end of the gaussian (hill ends at cutoff)
		float X, Y, U, V, D;										// current pixel
		float gauss, hill;
		bool foundhit; // did we find a hit?
		HIT hitP;
		for ( r=3; r<cameraContours->rows-3; r++ ) { // skip three rows at each end to compensate for bad lighting/distortion at edge of camera
			
			pstride = (height-r-1)*width;
			// search for the cut off
			foundhit = false;
			for ( c=0; c<cameraContours->cols; c++ ) {
				val = pixels[3*(c+pstride)+1]/(float)high;
				if ( val < 0.5f ) { // cut off
					foundhit = true;
				//	Log.log(0, "AgentSensorFloorFinder::generateCameraTemplate:: hit row %d col %d px %d %d", r, c, r*10+5, c );
					break;
				}
				Px(cameraContours,r,c) = 0.10f * val; // 0 < Px < 0.10f
			}

			// make sure the row is valid
			if ( c == 0 ) { // skip this row
				continue;
			}

			// figure out start and end cols for the gaussian and hill
			cutoffC = (float)c;
			cutoffU = (height/2.0f - r)/FLOORFINDER_SCALE*(float)pose->frameH/reading->h;  // cut off pixel in homography image frame
			cutoffV = (c - reading->h/2.0f)*(float)pose->frameH/reading->h;
			applyHomography( pose->G, cutoffU, cutoffV, &cutoffX, &cutoffY ); // cutoff pixel in floor frame
			cutoffD = sqrt( cutoffX*cutoffX + cutoffY*cutoffY ); // distance from camera to cutoff pixel

			startD = max( cutoffD - 3*pose->sigma, 0.01f );
			startX = cutoffX/cutoffD * startD; // start pixel in floor frame
			startY = cutoffY/cutoffD * startD;
			applyHomography( pose->H, startX, startY, &startU, &startV ); // start pixel in homography image frame
			//startR = height/2.0f - (startU*FLOORFINDER_SCALE*(float)reading->h/pose->frameH); 
			startC = max( 0, reading->h/2.0f + (startV*(float)reading->h/pose->frameH) );  // start pixel in contour frame
			if ( foundhit ) {
				endD = cutoffD + 3*pose->sigma;
				endX = cutoffX/cutoffD * endD; // start pixel in floor frame
				endY = cutoffY/cutoffD * endD;
				applyHomography( pose->H, endX, endY, &endU, &endV ); // start pixel in homography image frame
				//endR = height/2.0f - (endU*FLOORFINDER_SCALE*(float)reading->h/pose->frameH); 
				endC = min( cameraContours->cols, reading->h/2.0f + (endV*(float)reading->h/pose->frameH) ); // end pixel in contour frame
			} else {
				endC = (float)(cameraContours->cols - 1); // stop on the last pixel
			}

			if ( cutoffD > CAMERA_TEMPLATE_LENGTH ) { // ignore this hit because it is too far away
				foundhit = false;
			}

			// scale by hill, apply gaussian
			for ( c=(int)ceil(startC); c<=endC; c++ ) {
				U = (height/2.0f - r)/FLOORFINDER_SCALE*(float)pose->frameH/reading->h;  // current pixel in homography image frame
				V = (c - reading->h/2.0f)*(float)pose->frameH/reading->h;
				applyHomography( pose->G, U, V, &X, &Y ); // current pixel in floor frame
				D = sqrt( X*X + Y*Y );
				
				if ( c < cutoffC ) {
					hill = (cutoffD - D)/(cutoffD - startD);
					Px(cameraContours,r,c) *= hill;
				}

				if ( foundhit ) {
					expon = exp( -(D-cutoffD)*(D-cutoffD)/(2*var) );
					gauss = expon*norm;
					
					if ( val < 0.25f )
						Px(cameraContours,r,c) += -gauss*0.45f;
					else // 0.25 < val < 0.5
						Px(cameraContours,r,c) += -(1-val+0.25f)*gauss*0.45f;
				}
			}

			// record hit
			if ( foundhit ) {
				hitP.sx = startX;
				hitP.sy = startY;
				hitP.ex = cutoffX;
				hitP.ey = cutoffY;
				hits->push_back(hitP);
			}
		}

		// scan through pixels of cameraT
		int Utemp, Vtemp;
		float interpA, interpB, interpC, interpD, interpH, interpV;
		for ( r=0; r<cameraT->rows; r++ ) {
			for ( c=0; c<cameraT->cols; c++ ) {
				X = c/(float)CAMERA_TEMPLATE_DIVISIONS;								// pixel in floor frame
				Y = r/(float)CAMERA_TEMPLATE_DIVISIONS - CAMERA_TEMPLATE_WIDTH/2.0f;	
				applyHomography( pose->H, X, Y, &U, &V );			// pixel in homography image frame
				U = height/2.0f - (U*FLOORFINDER_SCALE*(float)reading->h/pose->frameH);  // pixel in cameraContours frame
				V = reading->h/2.0f + (V*(float)reading->h/pose->frameH);
				D = sqrt( U*U + V*V );

				// four point interpolate value
				interpA = interpB = interpC = interpD = 0;
				Utemp = (int)floor( U );
				Vtemp = (int)floor( V );
				interpH = V - Vtemp; // distance from bl corner to U,V
				interpV = U - Utemp; 
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpA = Px(cameraContours,Utemp,Vtemp);
				Vtemp++;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpB = Px(cameraContours,Utemp,Vtemp);
				Utemp++;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpC = Px(cameraContours,Utemp,Vtemp);
				Vtemp--;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpD = Px(cameraContours,Utemp,Vtemp);

				
				if ( r < 0 || r > cameraT->rows || c < 0 || c >= cameraT->cols ) // TEMP
					r=r;
				Px(cameraT,r,c) = (interpA*interpH + interpD*(1-interpH))*interpV + (interpB*interpH + interpC*(1-interpH))*(1-interpV);
			}
		}
	} else { // parse simualtion image stream
		float hit[10];
		float rot[10];
		float alpha;
		char dataType;
		
		this->ds.setData( imageData, imageSize );

		while (1) {
			dataType = this->ds.unpackChar();
			if ( dataType == SimCamera_DATA_END )
				break;
			switch ( dataType ) {
			case SimCamera_DATA_LANDMARK:
				this->ds.unpackUChar();		// skip
				this->ds.unpackFloat32();
				this->ds.unpackFloat32();
				break;
			case SimCamera_DATA_FLOORFINDER:
				alpha = this->ds.unpackFloat32();
				for ( r=0; r<10; r++ ) {
					hit[r] = this->ds.unpackFloat32();
					rot[r] = (r-4.5f)*alpha/5;
				}
				break;
			default:
				Log.log( 0, "AgentSensorFloorFinder::generateCameraTemplate: unhandled data type in simStream %d", dataType );
				return 1; // unhandled data type
			};
		}

		this->ds.unlock();

		// create vertical line contours
		width = (int)(CAMERA_TEMPLATE_DIVISIONS*CAMERA_TEMPLATE_LENGTH);
		height = 10;
		if ( width*height > cameraContours->bufSize/(int)sizeof(float) ) { // we need more space
			ReallocImage( cameraContours, height, width );
		} else {
			cameraContours->rows = height;
			cameraContours->cols = width;
			cameraContours->stride = cameraContours->rows;
		}
		memset( cameraContours->data, 0, width*height*sizeof(float) );

		float cutoffC, cutoffX, cutoffY, cutoffD;	// cutoff pixel coordinates (top of the hill)
		float startC, startX, startY, startD;		// start of the hill and gaussian
		float endC, endX, endY, endD;					// end of the gaussian (hill ends at cutoff)
		float X, Y, U, V, D;										// current pixel
		float gauss, hill;
		bool foundhit; // did we find a hit?
		HIT hitP;
		for ( r=0; r<cameraContours->rows; r++ ) {

			// search for the cut off
			foundhit = false;
			for ( c=0; c<cameraContours->cols; c++ ) {
				if ( hit[r] > 0 && hit[r] <= c/(float)CAMERA_TEMPLATE_DIVISIONS  ) { // cut off
					foundhit = true;
				//	Log.log(0, "AgentSensorFloorFinder::generateCameraTemplate:: hit row %d col %d px %d %d", r, c, r*10+5, c );
					break;
				} else {
					Px(cameraContours,r,c) = 0.05f; // 0 < Px < 0.10f
				}
			}
			
			// make sure the row is valid
			if ( c == 0 ) { // skip this row
				continue;
			}

			// figure out start and end cols for the gaussian and hill
			cutoffC = (float)c;
			cutoffD = c/(float)CAMERA_TEMPLATE_DIVISIONS; // distance from camera to cutoff pixel
			cutoffX = cutoffD * cos( rot[r] );
			cutoffY = cutoffD * sin( rot[r] );

			startD = max( cutoffD - 3*pose->sigma, 0.01f );
			startX = cutoffX/cutoffD * startD; // start pixel in floor frame
			startY = cutoffY/cutoffD * startD;
			startC = 0;  // start pixel in contour frame
			if ( foundhit ) {
				endD = cutoffD + 3*pose->sigma;
				endX = cutoffX/cutoffD * endD; // start pixel in floor frame
				endY = cutoffY/cutoffD * endD;
				endC = min( endD*CAMERA_TEMPLATE_DIVISIONS, cameraContours->cols - 1); // end pixel in contour frame
			} else {
				endC = (float)(cameraContours->cols - 1); // stop on the last pixel
			}

			if ( cutoffD > CAMERA_TEMPLATE_LENGTH ) { // ignore this hit because it is too far away
				foundhit = false;
			}

			// scale by hill, apply gaussian
			for ( c=(int)ceil(startC); c<=endC; c++ ) {
				D = c/(float)CAMERA_TEMPLATE_DIVISIONS; 

				if ( c < cutoffC ) {
					hill = (cutoffD - D)/(cutoffD - startD);
					Px(cameraContours,r,c) *= hill;
				}

				if ( foundhit ) {
					expon = exp( -(D-cutoffD)*(D-cutoffD)/(2*var) );
					gauss = expon*norm;
					
					Px(cameraContours,r,c) += -gauss*0.45f;
				}
			}

			// record hit
			if ( foundhit ) {
				hitP.sx = startX;
				hitP.sy = startY;
				hitP.ex = cutoffX;
				hitP.ey = cutoffY;
				hits->push_back(hitP);
			}
		}

		// TEMP
		//dumpImageBWFloat32( "cameraContours.bmp", cameraContours->cols, cameraContours->rows, cameraContours->data, -0.5f, 1.0f, false, true, false );
		
		// scan through pixels of cameraT
		int Utemp, Vtemp;
		float interpA, interpB, interpC, interpD, interpH, interpV;
		for ( r=0; r<cameraT->rows; r++ ) {
			for ( c=0; c<cameraT->cols; c++ ) {
				X = c/(float)CAMERA_TEMPLATE_DIVISIONS;								// pixel in floor frame
				Y = r/(float)CAMERA_TEMPLATE_DIVISIONS - CAMERA_TEMPLATE_WIDTH/2.0f;	
				V = sqrt( X*X + Y*Y )*CAMERA_TEMPLATE_DIVISIONS;
				U = 5 * atan2( Y, X )/alpha + 5; 

				// four point interpolate value
				interpA = interpB = interpC = interpD = 0;
				Utemp = (int)floor( U );
				Vtemp = (int)floor( V );
				interpH = V - Vtemp; // distance from bl corner to U,V
				interpV = U - Utemp; 
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpA = Px(cameraContours,Utemp,Vtemp);
				Vtemp++;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpB = Px(cameraContours,Utemp,Vtemp);
				Utemp++;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpC = Px(cameraContours,Utemp,Vtemp);
				Vtemp--;
				if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
					interpD = Px(cameraContours,Utemp,Vtemp);

				Px(cameraT,r,c) = (interpA*interpH + interpD*(1-interpH))*interpV + (interpB*interpH + interpC*(1-interpH))*(1-interpV);
			}
		}

		// TEMP
		//dumpImageBWFloat32( "cameraT.bmp", cameraT->cols, cameraT->rows, cameraT->data, -0.5f, 1.0f, false, true, false );

	}

	return 0;
}

int AgentSensorFloorFinder::initializeRoboRealm( UUID *sensor ) {
	
	this->sensorRR[ *sensor ].sensor = *sensor;
	this->sensorRR[ *sensor ].ready = false;

	// request free port
	UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestFreePort, DDB_REQUEST_TIMEOUT, sensor, sizeof(UUID) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RFREEPORT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}


int AgentSensorFloorFinder::releasePort( int port ) {
	this->sendMessage( this->hostCon, MSG_RELEASEPORT, (char *)&port, 4 );
	return 0;
}

int AgentSensorFloorFinder::ddbNotification( char *data, int len ) {
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
			UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
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
			thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
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
				thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
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
// Visualize

int AgentSensorFloorFinder::visualizeClear() {
#ifdef DO_VISUALIZE
	int i;

	for ( i = VIS_FIRST_HIT; i < VIS_FIRST_HIT + STATE(AgentSensorFloorFinder)->visHitCount; i++ ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( i );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	STATE(AgentSensorFloorFinder)->visHitCount = 0;

#endif
	return 0;
}

int AgentSensorFloorFinder::visualizeHits( float x, float y, float r, CameraPose *pose, std::list<HIT> *hits ) {
#ifdef DO_VISUALIZE
	std::list<HIT>::iterator it;
	int objId;

	float sn, cs;
	float objx, objy, objr, objs;

	this->visualizeClear();

	// shift avatar pose to camera pose
	sn = (float)sin(r);
	cs = (float)cos(r);
	x += cs*pose->x - sn*pose->y;
	y += sn*pose->x + cs*pose->y;
	r += pose->r;

	STATE(AgentSensorFloorFinder)->visHitCount = (int)hits->size();

	sn = (float)sin(r); // new sn, cs from avatar r + camera r
	cs = (float)cos(r);
	
	objId = VIS_FIRST_HIT;
	for ( it = hits->begin(); it != hits->end(); it++, objId++ ) {
		
		it->ex -= it->sx; // subtract start from end for later calculations
		it->ey -= it->sy;

		objx = x + it->sx*cs - it->sy*sn;
		objy = y + it->sx*sn + it->sy*cs;
		objr = r + atan2( it->ey, it->ex );
		objs = sqrt( it->ex*it->ex + it->ey*it->ey );	

		this->ds.reset();
		this->ds.packUUID( this->getUUID() ); // agent
		this->ds.packInt32( objId ); // objectId
		this->ds.packFloat32( objx ); // x
		this->ds.packFloat32( objy ); // y
		this->ds.packFloat32( objr ); // r
		this->ds.packFloat32( objs ); // s
		this->ds.packInt32( 1 ); // path count
		this->ds.packInt32( VIS_PATH_LINE ); // path id
		this->ds.packFloat32( 1 ); // r
		this->ds.packFloat32( 0.5f ); // g
		this->ds.packFloat32( 0 ); // b
		this->ds.packFloat32( 1 ); // lineWidth
		this->ds.packBool( 0 ); // solid
		this->ds.packString( "Hit" );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

#endif

	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentSensorFloorFinder::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
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

bool AgentSensorFloorFinder::convRequestMapInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestMapInfo: request timed out" );
		
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) {
		this->ds.unpackFloat32(); // tilesize
		STATE(AgentSensorFloorFinder)->mapResolution = this->ds.unpackFloat32();
		this->ds.unpackInt32(); // stride
		this->ds.unlock();

		this->backup(); // mapResolution
	} else {
		this->ds.unlock();
		Log.log( 0, "AgentSensorFloorFinder::convRequestMapInfo: request failed!" );
		UuidCreateNil( &STATE(AgentSensorFloorFinder)->mapId );
		return 0;
	}

	return 0;
}

bool AgentSensorFloorFinder::convRequestSensorInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	UUID avatar;
	int poseSize;
	UUID poseRef;
	UUID thread;

	pr = &this->readings[ *(int*)conv->data ];

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestSensorInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
  
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
		this->ds.unlock();
	
		this->addPose( &pr->sensor, &poseRef );

		this->backup(); // sensorPose, sensorAvatar
		
		pr->waiting &= ~WAITING_ON_POSE;

		// request pf
		thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
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

bool AgentSensorFloorFinder::convRequestPFInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	char response;

	mapReadings::iterator iter = this->readings.find( *(int*)conv->data );
	if ( iter == this->readings.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestPFInfo: reading not found (timed out == %d)", conv->response == NULL ? 1 : 0 );
		return 0;
	}

	pr = &iter->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestPFInfo: request timed out" );
		
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
			freeDynamicBuffer( pr->pfStateRef );
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
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorFloorFinder_CBR_cbRepeatRPFInfo, &pr->id, sizeof(int) );
	} else if ( response == DDBR_TOOEARLY ) {
		this->ds.unlock();
		this->finishReading( pr, -1 ); // permenant failure
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorFloorFinder::cbRepeatRPFInfo( void *vpid ) {
	UUID thread;
	PROCESS_READING *pr;
	
	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)vpid);

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::cbRepeatRPFInfo: reading not found %d", *(int*)vpid );
		return 0;
	}

	pr = &iR->second;

	if ( pr->retries > 6*(int)this->avatars.size() ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	pr->retries++;

	// request PF
	thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
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


bool AgentSensorFloorFinder::convRequestAvatarLoc( void *vpConv ) {
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
		Log.log( 0, "AgentSensorFloorFinder::convRequestAvatarLoc: reading not found %d", *(int*)conv->data );
		return 0; // end conversation
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestAvatarLoc: request timed out" );
		
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
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorFloorFinder_CBR_cbRepeatRAvatarLoc, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorFloorFinder::cbRepeatRAvatarLoc( void *vpid ) {
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
		Log.log( 0, "AgentSensorFloorFinder::cbRepeatRAvatarLoc: reading not found %d", *(int*)vpid );
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
	thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
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


bool AgentSensorFloorFinder::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentSensorFloorFinder::convGetAvatarList: timed out" );
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
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorFloorFinder::convGetAvatarList: recieved %d avatars", count );

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
			thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
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

bool AgentSensorFloorFinder::convGetAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentSensorFloorFinder::convGetAvatarInfo: timed out" );
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

		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorFloorFinder::convGetAvatarInfo: recieved avatar (%s) pf id: %s", 
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


bool AgentSensorFloorFinder::convRequestMap( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;

	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data);

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestMap: reading not found %d", *(int*)conv->data );
		return 0;
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestMap: request timed out" );
		
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
		STATE(AgentSensorFloorFinder)->mapResolution = this->ds.unpackFloat32();

		cw = (int)floor( w / STATE(AgentSensorFloorFinder)->mapResolution );
		ch = (int)floor( h / STATE(AgentSensorFloorFinder)->mapResolution );

		pr->mapDataRef = newDynamicBuffer(cw*ch*4);
		if ( pr->mapDataRef == nilUUID ) {	
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		pr->mapHeight = ch;
		pr->mapWidth = cw;

		memcpy( getDynamicBuffer( pr->mapDataRef ), this->ds.unpackData( cw*ch*4 ), cw*ch*4 );
		this->ds.unlock();

		pr->waiting &= ~WAITING_ON_MAP;

		// see if we're ready to process
		if ( !pr->waiting && !pr->waitingOnAvatars )
			this->processReading( pr );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorFloorFinder::convRequestData( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;

	mapReadings::iterator iR;

	iR = this->readings.find( *(int*)conv->data);

	if ( iR == this->readings.end() ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestData: reading not found %d", *(int*)conv->data );
		return 0;
	}

	pr = &iR->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestData: request timed out" );
		
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
		pr->dataRef = newDynamicBuffer( pr->dataSize );
		if ( pr->dataRef == nilUUID ) {
			this->ds.unlock();
			freeDynamicBuffer( pr->readingRef );
			this->finishReading( pr, 0 );
			return 0;
		}
		memcpy( getDynamicBuffer( pr->dataRef ), this->ds.unpackData( pr->dataSize ), pr->dataSize );
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


bool AgentSensorFloorFinder::convRequestFreePort( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	UUID *sensor;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorFloorFinder::convRequestFreePort: request timed out" );

		return 0; // end conversation
	}
	
	sensor = (UUID *)conv->data;

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() ) { // successful
		char buf[256];
	
		this->sensorRR[ *sensor ].port = this->ds.unpackInt32();
		this->ds.unlock();

		// start roborealm
		this->sensorRR[ *sensor ].api = new RR_API();
		if ( this->sensorRR[ *sensor ].api == NULL ) {
			this->releasePort( this->sensorRR[ *sensor ].port );

			// TODO cleanup
			return 0;
		}

		Log.log( 0, "AgentSensorFloorFinder::convRequestFreePort: skipping RoboRealm" );
		/* don't use RR for now because it has problems closing
		sprintf_s( buf, 256, "-api_port %d", this->sensorRR[ *sensor ].port );
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: opening instance of RoboRealm" );
		int ret = this->sensorRR[ *sensor ].api->open( _T("RoboRealm\\RoboRealm.exe"), buf, this->sensorRR[ *sensor ].port );
		if ( !ret ) {
			Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: open RR failed, port %d", this->sensorRR[ *sensor ].port );
		} else if ( ret == 2 ) {
			Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: assumed control of already open RR, port %d", this->sensorRR[ *sensor ].port );
		} else {
			Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: open RR succeeded, port %d", this->sensorRR[ *sensor ].port );
		}
		bool retB = this->sensorRR[ *sensor ].api->connect( "localhost", this->sensorRR[ *sensor ].port );
		if ( !retB ) {
			Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: failed to connect to RR, port %d (%s)", this->sensorRR[ *sensor ].port, this->sensorRR[ *sensor ].api->getLastError()  );
		}
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: connected" );
		
		retB = this->sensorRR[ *sensor ].api->loadProgram( "RRscripts\\FLOORFINDER256BMP.robo" );

		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: load program success = %d", retB );

		this->sensorRR[ *sensor ].horizon = (int)(256*this->sensorRR[ *sensor ].horizon);
		sprintf_s( buf, 256, "%d", this->sensorRR[ *sensor ].horizon );
		retB = this->sensorRR[ *sensor ].api->setVariable( "CROPHORIZON", buf );
		
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: setVariable success = %d", retB );
		*/

		this->sensorRR[ *sensor ].ready = true;

		// process any queued data
		this->nextImage( &this->sensorRR[ *sensor ] );

		Log.log( LOG_LEVEL_NORMAL, "AgentSensorFloorFinder::convRequestFreePort: done" );

	} else {
		// TODO cleanup?
	}

	return 0;
}

bool AgentSensorFloorFinder::cbQueueNextImage( void *vpSensor ) {
	mapRoboRealm::iterator iRR = this->sensorRR.find( *(UUID *)vpSensor );
	
	if ( iRR == this->sensorRR.end() )
		return 0; // sensor not found?

	this->nextImage( &iRR->second );

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AgentSensorFloorFinder::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AgentSensorFloorFinder::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	AgentSensorFloorFinder::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentSensorFloorFinder);

	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorPose );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorAvatar );
	
	// robo realm instance
	mapRoboRealm::iterator iRR;
	for ( iRR = this->sensorRR.begin(); iRR != this->sensorRR.end(); iRR++ ) {
		ds->packBool( 1 );
		ds->packUUID( (UUID *)&iRR->first );
		_WRITE_STATE_LIST( int, &iRR->second.queue );
	}
	ds->packBool( 0 );

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

int	AgentSensorFloorFinder::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentSensorFloorFinder);
	
	_READ_STATE_MAP( UUID, UUID, &this->sensorPose );
	_READ_STATE_MAP( UUID, UUID, &this->sensorAvatar );

	// robo realm instance
	UUID keyRR;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &keyRR );
		this->initializeRoboRealm( &keyRR );
		_READ_STATE_LIST( int, &this->sensorRR[keyRR].queue ); // unpack queue
	}

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

int AgentSensorFloorFinder::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	// clean up visualization
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packChar( 0 ); // keep paths
	this->sendMessage( this->hostCon, MSG_DDB_VIS_CLEAR_ALL, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		// request list of avatars
		UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();

		// initialize roborealm
		mapPose::iterator iP;
		for ( iP = this->sensorPose.begin(); iP != this->sensorPose.end(); iP++ ) {
			this->initializeRoboRealm( (UUID *)&iP->first );
		}

		// make sure we have map resolution
		if ( STATE(AgentSensorFloorFinder)->mapResolution == 0 ) {
			// get map info
			UUID thread = this->conversationInitiate( AgentSensorFloorFinder_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &STATE(AgentSensorFloorFinder)->mapId );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	return 0;
}

int AgentSensorFloorFinder::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(AgentSensorFloorFinder)->ownerId );

	ds->packUUID( &STATE(AgentSensorFloorFinder)->mapId );
	ds->packFloat32( STATE(AgentSensorFloorFinder)->mapResolution );
	
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

int AgentSensorFloorFinder::readBackup( DataStream *ds ) {
	
	ds->unpackUUID( &STATE(AgentSensorFloorFinder)->ownerId );

	ds->unpackUUID( &STATE(AgentSensorFloorFinder)->mapId );
	STATE(AgentSensorFloorFinder)->mapResolution = ds->unpackFloat32();
	
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
	AgentSensorFloorFinder *agent = (AgentSensorFloorFinder *)vpAgent;

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
	AgentSensorFloorFinder *agent = new AgentSensorFloorFinder( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AgentSensorFloorFinder *agent = new AgentSensorFloorFinder( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CAgentSensorFloorFinderDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAgentSensorFloorFinderDLL, CWinApp)
END_MESSAGE_MAP()

CAgentSensorFloorFinderDLL::CAgentSensorFloorFinderDLL() {}

// The one and only CAgentSensorFloorFinderDLL object
CAgentSensorFloorFinderDLL theApp;

int CAgentSensorFloorFinderDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentSensorFloorFinderDLL ---\n"));
  return CWinApp::ExitInstance();
}