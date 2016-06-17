// AvatarBase.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "AvatarBase.h"
#include "AvatarBaseVersion.h"

// TEMP using ExecutiveMission instead of ExecutiveAvatar for now
#include "..\\ExecutiveMission\\ExecutiveMissionVersion.h"

#include "..\\AgentPathPlanner\\AgentPathPlannerVersion.h"

#include "..\\AgentIndividualLearning\\AgentIndividualLearningVersion.h"

using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define LOG_FOR_OFFLINE_SLAM

//*****************************************************************************
// AvatarBase

//-----------------------------------------------------------------------------
// Constructor	
AvatarBase::AvatarBase( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AvatarBase, AgentBase )
	STATE(AvatarBase)->actionInProgress = false;

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarBase" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarBase_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AvatarBase_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(AvatarBase)->avatarExecutiveUUID );
	apb->apbUuidCreate( &STATE(AvatarBase)->avatarUUID ); 
	this->avatarReady = false;

	STATE(AvatarBase)->sensorTypes = 0;

	// generate pfId
	apb->apbUuidCreate( &STATE(AvatarBase)->pfId );
	STATE(AvatarBase)->particleFilterInitialized = false;

	// set initial sigma for particle filter, child classes should set their own values if they need to
	STATE(AvatarBase)->pfNumParticles = 500;
	STATE(AvatarBase)->pfInitSigma[0] = AVATAR_INIT_LINEAR_SIG;
	STATE(AvatarBase)->pfInitSigma[1] = AVATAR_INIT_LINEAR_SIG;  
	STATE(AvatarBase)->pfInitSigma[2] = AVATAR_INIT_ROTATIONAL_SIG;
	STATE(AvatarBase)->pfUpdateSigma[0] = AVATAR_FORWARDV_SIG_EST;
	STATE(AvatarBase)->pfUpdateSigma[1] = AVATAR_TANGENTIALV_SIG_EST;
	STATE(AvatarBase)->pfUpdateSigma[2] = AVATAR_ROTATIONALV_SIG_EST;

	STATE(AvatarBase)->posInitialized = false;

	UuidCreateNil( &STATE(AvatarBase)->mapId );
	UuidCreateNil( &STATE(AvatarBase)->missionRegionId );

	this->motionPlanner = AgentPathPlanner;	// default

	UuidCreateNil( &STATE(AvatarBase)->agentPathPlanner );
	STATE(AvatarBase)->agentPathPlannerSpawned = 0;
	STATE(AvatarBase)->agentPathPlannerStatus = DDBAGENT_STATUS_ERROR;

	UuidCreateNil(&STATE(AvatarBase)->agentIndividualLearning);
	STATE(AvatarBase)->agentIndividualLearningSpawned = 0;
	STATE(AvatarBase)->agentIndividualLearningStatus = DDBAGENT_STATUS_ERROR;

	STATE(AvatarBase)->targetControllerIndex = 0;

	STATE(AvatarBase)->maxLinear = 1.0; // subclasses can update if they want
	STATE(AvatarBase)->maxRotation = 2*fM_PI;
	STATE(AvatarBase)->minLinear = 0;
	STATE(AvatarBase)->minRotation = 0;
	STATE(AvatarBase)->canInteract = true;

	this->retiring = false;

	STATE(AvatarBase)->SLAMmode = SLAM_MODE_JCSLAM;
	STATE(AvatarBase)->SLAMreadingsProcessing = 0;
	STATE(AvatarBase)->SLAMdelayNextAction = false;

	// Prepare callbacks
	this->callback[AvatarBase_CBR_cbRequestExecutiveAvatarId] = NEW_MEMBER_CB(AvatarBase,cbRequestExecutiveAvatarId);
	this->callback[AvatarBase_CBR_convRequestExecutiveAvatarId] = NEW_MEMBER_CB(AvatarBase,convRequestExecutiveAvatarId);
	this->callback[AvatarBase_CBR_convRequestAgentPathPlanner] = NEW_MEMBER_CB(AvatarBase,convRequestAgentPathPlanner);
	this->callback[AvatarBase_CBR_convRequestAgentIndividualLearning] = NEW_MEMBER_CB(AvatarBase, convRequestAgentIndividualLearning);
	this->callback[AvatarBase_CBR_convPathPlannerSetTarget] = NEW_MEMBER_CB(AvatarBase,convPathPlannerSetTarget);
	this->callback[AvatarBase_CBR_convIndividualLearningRequestAction] = NEW_MEMBER_CB(AvatarBase, convIndividualLearningRequestAction);
	this->callback[AvatarBase_CBR_convPFInfo] = NEW_MEMBER_CB(AvatarBase,convPFInfo);
	this->callback[AvatarBase_CBR_convAgentInfo] = NEW_MEMBER_CB(AvatarBase,convAgentInfo);
	this->callback[AvatarBase_CBR_cbPFUpdateTimer] = NEW_MEMBER_CB(AvatarBase,cbPFUpdateTimer);
	this->callback[AvatarBase_CBR_cbRetire] = NEW_MEMBER_CB(AvatarBase,cbRetire);
	this->callback[AvatarBase_CBR_convRequestAgentSpawn] = NEW_MEMBER_CB(AvatarBase,convRequestAgentSpawn);
	
}

//-----------------------------------------------------------------------------
// Destructor
AvatarBase::~AvatarBase() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	// clean up particle filter
	if ( STATE(AvatarBase)->particleFilterInitialized )
		this->destroyParticleFilter( !(this->frozen || this->retiring) );

	// free state
	list<UUID>::iterator iter = this->actionData.begin();
	while ( iter != this->actionData.end() ) {
		freeDynamicBuffer(*iter);
		iter++;
	}
	
}

//-----------------------------------------------------------------------------
// Configure

int AvatarBase::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\AvatarBase %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_NORMAL );
		Log.log( 0, "AvatarBase %.2d.%.2d.%.5d.%.2d", AvatarBase_MAJOR, AvatarBase_MINOR, AvatarBase_BUILDNO, AvatarBase_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;

	Log.log( 0, "AvatarBase::configure: avatarUUID %s, pfId %s", Log.formatUUID(0,&STATE(AvatarBase)->avatarUUID), Log.formatUUID(0,&STATE(AvatarBase)->pfId) );

	return 0;
}

int AvatarBase::parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode ) {
	
	if ( agentType->uuid == STATE(AgentBase)->agentType.uuid 
	  && agentType->instance == STATE(AgentBase)->agentType.instance ) {
		this->setPos( x, y, r );

		STATE(AvatarBase)->retireSet = false; 
		memset( &STATE(AvatarBase)->retireTime, 0, sizeof(_timeb) );

		// set retirement
		if ( duration > 0 ) {
			this->addTimeout( (unsigned int)(duration*60*1000), AvatarBase_CBR_cbRetire );
			STATE(AvatarBase)->retireSet = true;
			
			apb->apb_ftime_s( &STATE(AvatarBase)->retireTime );
			STATE(AvatarBase)->retireTime.time += (int)(duration*60);
			STATE(AvatarBase)->retireTime.millitm += ((int)(duration*60*1000) % 1000);
			if ( STATE(AvatarBase)->retireTime.millitm >= 1000 ) {
				STATE(AvatarBase)->retireTime.millitm -= 1000;
				STATE(AvatarBase)->retireTime.time++;
			}
		}

		STATE(AvatarBase)->retireMode = retireMode;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AvatarBase::start( char *missionFile ) {
	UUID thread;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	STATE(AvatarBase)->haveTarget = false;

	// find the Avatar Executive so we can register
	// TEMP using the Mission Executive instead
	UUID avatarExecType;
	UuidFromString( (RPC_WSTR)_T(ExecutiveMission_UUID), &avatarExecType );
	thread = this->conversationInitiate( AvatarBase_CBR_convRequestExecutiveAvatarId, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &avatarExecType );
	this->ds.packChar( -1 );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RUNIQUEID, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarBase::stop() {
	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarBase::step() {
	return AgentBase::step();
}

int AvatarBase::parseMF_HandleOptions( int SLAMmode ) {
	STATE(AvatarBase)->SLAMmode = SLAMmode;

	return AgentBase::parseMF_HandleOptions( SLAMmode );
}

int AvatarBase::parseMF_HandleLearning(bool individualLearning) {
	
	this->motionPlanner = (individualLearning) ? AgentIndividualLearning : AgentPathPlanner;
	// TODO: handle canInteract

	return 0;
}


int AvatarBase::setAvatarState( bool ready ) {

	this->avatarReady = ready;

	Log.log( 0, "AvatarBase::setAvatarState: set avatar state %d", ready );
	
	if ( this->avatarReady && STATE(AvatarBase)->haveTarget ) {
		this->getActions();
	}

	return 0;
}


//-----------------------------------------------------------------------------
// register avatar

int	AvatarBase::registerAvatar( char *type, float innerRadius, float outerRadius, int capacity, int sensorTypes ) {

	if ( strlen(type) >= 64 ) {
		Log.log( 0, "AvatarBase::registerAvatar: avatar type string too long (>=64 chars)" );
		return 1;
	}

	STATE(AvatarBase)->sensorTypes = sensorTypes;

	_timeb startTime;
	apb->apb_ftime_s( &startTime );

	// register with DDB
	this->ds.reset();
	this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
	this->ds.packString( type );
	this->ds.packInt32( this->avatarReady ? 1 : 0 );
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( &STATE(AvatarBase)->pfId );
	this->ds.packFloat32( innerRadius );
	this->ds.packFloat32( outerRadius );
	this->ds.packData( &startTime, sizeof(_timeb) );
	this->ds.packInt32( capacity ); // capacity
	this->ds.packInt32( sensorTypes );
	this->sendMessage( this->hostCon, MSG_DDB_ADDAVATAR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM REGISTER_AVATAR %s %s %s %f %f %d %d %d.%03d %s",
		Log.formatUUID( 0, &STATE(AvatarBase)->avatarUUID ), 
		Log.formatUUID( 0, this->getUUID() ), Log.formatUUID( 0, &STATE(AvatarBase)->pfId ),
		innerRadius, outerRadius, capacity, sensorTypes,
		(int)startTime.time, (int)startTime.millitm, type );
#endif

	return 0;
}

int AvatarBase::retireAvatar() {
	DataStream lds;

	Log.log( 0, "AvatarBase::retireAvatar: retiring avatar (%d)", STATE(AvatarBase)->retireMode );
	this->retiring = true;

	_timeb endTime;
	apb->apb_ftime_s( &endTime );

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM RETIRE_AVATAR %s %d %d.%03d",
		Log.formatUUID( 0, &STATE(AvatarBase)->avatarUUID ), STATE(AvatarBase)->retireMode,
		(int)endTime.time, (int)endTime.millitm );
#endif

	// update avatar info
	lds.reset();
	lds.packUUID( &STATE(AvatarBase)->avatarUUID );
	lds.packInt32( DDBAVATARINFO_RETIRE );
	lds.packChar( STATE(AvatarBase)->retireMode );
	lds.packData( &endTime, sizeof(_timeb) );
	this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
	lds.unlock();

	// clean up path planner
	if ( STATE(AvatarBase)->agentPathPlannerSpawned ) {
		this->sendMessage( this->hostCon, MSG_AGENT_STOP, &STATE(AvatarBase)->agentPathPlanner );
	}

	// clean up sensor processing agents if we have them
	if ( STATE(AvatarBase)->SLAMmode == SLAM_MODE_DELAY ) {
		mapTypeAgents::iterator iA;
		for ( iA = this->agents.begin(); iA != this->agents.end(); iA++ ) {
			this->sendMessage( this->hostCon, MSG_AGENT_STOP, (UUID *)&iA->first );
		}
	}

	// push out final pf update
	apb->apb_ftime_s( &STATE(AvatarBase)->posT );
	this->updatePFState();

	// shutdown
	this->sendMessage( this->hostCon, MSG_AGENT_SHUTDOWN );
	STATE(AgentBase)->stopFlag = true;

	return 0;
}

int AvatarBase::registerLandmark( UUID *id, unsigned char code, UUID *avatar, float x, float y ) {
	DataStream lds;

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM REGISTER_AVATAR_LANDMARK %s %d %s %f %f",
		Log.formatUUID( 0, id ), code, Log.formatUUID( 0, avatar ),
		x, y );
#endif

	// add to DDB
	lds.reset();
	lds.packUUID( id );
	lds.packUChar( code );
	lds.packUUID( avatar );
	lds.packFloat32( 0 ); 
	lds.packFloat32( 0 ); 
	lds.packFloat32( x ); 
	lds.packFloat32( y ); 
	lds.packChar( 0 );
	this->sendMessage( this->hostCon, MSG_DDB_ADDLANDMARK, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

//-----------------------------------------------------------------------------
// sensor setup

UUID * AvatarBase::parseSensorSonar( FILE *paraF ) {
	UUID *uuid = &this->_parseSensorUuid;
	SonarPose sp;

	if ( 7 != fscanf_s( paraF, "SENSOR_SONAR=%f %f %f %f %f %f %f\n", 
			  &sp.x, &sp.y, &sp.r,
			  &sp.max,
			  &sp.sigma,
			  &sp.alpha, &sp.beta ) ) {
		Log.log( 0, "AvatarBase::parseSensorSonar: expected SENSOR_SONAR=x y r max sigma alpha beta, check format" );
		return NULL;
	}
	
	// generate UUID for sensor
	apb->apbUuidCreate( uuid );

	this->registerSensor( uuid, DDB_SENSOR_SONAR, &STATE(AvatarBase)->avatarUUID, &STATE(AvatarBase)->pfId, &sp, sizeof(SonarPose) );

	return uuid;
}

UUID * AvatarBase::parseSensorCamera( FILE *paraF ) {
	UUID *uuid = &this->_parseSensorUuid;
	CameraPose cp;

	if ( 44 != fscanf_s( paraF, "SENSOR_CAMERA=%f %f %f %f %f %f %d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f\n", 
			  &cp.x, &cp.y, &cp.z, &cp.r, 
			  &cp.sigma, &cp.horizon, &cp.frameH,
			  &cp.H[0], &cp.H[1], &cp.H[2],
			  &cp.H[3], &cp.H[4], &cp.H[5],
			  &cp.H[6], &cp.H[7], &cp.H[8],
			  &cp.G[0], &cp.G[1], &cp.G[2],
			  &cp.G[3], &cp.G[4], &cp.G[5],
			  &cp.G[6], &cp.G[7], &cp.G[8],
			  &cp.planeD,
			  &cp.J[0], &cp.J[1], &cp.J[2],
			  &cp.J[3], &cp.J[4], &cp.J[5],
			  &cp.J[6], &cp.J[7], &cp.J[8],
			  &cp.I[0], &cp.I[1], &cp.I[2],
			  &cp.I[3], &cp.I[4], &cp.I[5],
			  &cp.I[6], &cp.I[7], &cp.I[8] ) ) {
		Log.log( 0, "AvatarBase::parseSensorCamera: expected SENSOR_CAMERA=x y z r sigma horizon frameH H[0] ... H[8] G[0] ... G[8] planeD J[0] ... J[8] I[0] ... I[8], check format" );
		return NULL;
	}
	
	// generate UUID for sensor
	apb->apbUuidCreate( uuid );

	this->registerSensor( uuid, DDB_SENSOR_CAMERA, &STATE(AvatarBase)->avatarUUID, &STATE(AvatarBase)->pfId, &cp, sizeof(CameraPose) );

	return uuid;
}

int AvatarBase::registerSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize ) {
	DataStream lds;

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM REGISTER_SENSOR %s %d %s %s",
		Log.formatUUID( 0, id ), type,
		Log.formatUUID( 0, &STATE(AvatarBase)->avatarUUID ), Log.formatUUID( 0, &STATE(AvatarBase)->pfId ) );
	Log.dataDump( 0, pose, poseSize, "SENSOR_POSE" );
#endif


	// register with DDB
	lds.reset();
	lds.packUUID( id );
	lds.packInt32( type );
	lds.packUUID( avatar );
	lds.packUUID( pf );
	lds.packData( pose, poseSize );
	this->sendMessage( this->hostCon, MSG_DDB_ADDSENSOR, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(AvatarBase)->SLAMmode == SLAM_MODE_DELAY ) {
		// watch sensor
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( id );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
	}

	return 0;

}

int AvatarBase::submitSensorSonar( UUID *sonar, _timeb *t, SonarReading *reading ) {
	DataStream lds;

	if ( STATE(AvatarBase)->SLAMmode == SLAM_MODE_DELAY && STATE(AvatarBase)->SLAMdelayNextAction )
		return 0; // discard sonar to prevent them from building up, since they come in even if we are delaying

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM SONAR_READING %s %d.%03d %f",
		Log.formatUUID( 0, sonar ), (int)t->time, (int)t->millitm,
		reading->value );
#endif

	lds.reset();
	lds.packUUID( sonar );
	lds.packData( t, sizeof(_timeb) );
	lds.packInt32( sizeof(SonarReading) );
	lds.packData( reading, sizeof(SonarReading) );
	lds.packInt32( 0 );
	this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AvatarBase::submitSensorCamera( UUID *camera, _timeb *t, CameraReading *reading, void *data, unsigned int len ) {
	DataStream lds;

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM CAMERA_READING %s %d.%03d %d %d %s",
		Log.formatUUID( 0, camera ), (int)t->time, (int)t->millitm,
		reading->w, reading->h, reading->format );
	Log.dataDump( 0, data, len, "CAMERA_DATA" );
#endif

	lds.reset();
	lds.packUUID( camera );
	lds.packData( t, sizeof(_timeb) );
	lds.packInt32( sizeof(CameraReading) );
	lds.packData( reading, sizeof(CameraReading) );
	lds.packInt32( len );
	lds.packData( data, len );
	this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

// Particle Filter
int AvatarBase::createParticleFilter( int numParticles, float *sigma ) {
	DataStream lds;

	if ( !STATE(AvatarBase)->posInitialized )
		return 1; // we need a position before we create the particle filter!

	// generate initial state
	STATE(AvatarBase)->pfStateRef = newDynamicBuffer(sizeof(float)*numParticles*AVATAR_PFSTATE_SIZE);
	if ( STATE(AvatarBase)->pfStateRef == nilUUID )
		return 1; // malloc failed

	this->generatePFState( sigma );

#ifdef LOG_FOR_OFFLINE_SLAM
	Log.log( 0, "OFFLINE_SLAM REGISTER_PF %s %s %d.%03d %d %f %f %f %f %f %f",
		Log.formatUUID( 0, &STATE(AvatarBase)->pfId ), Log.formatUUID( 0, &STATE(AvatarBase)->avatarUUID ),
		(int)STATE(AvatarBase)->posT.time, (int)STATE(AvatarBase)->posT.millitm, 
		numParticles, STATE(AvatarBase)->posX, STATE(AvatarBase)->posY, STATE(AvatarBase)->posR,
		sigma[0], sigma[1], sigma[2] );
	lds.reset();
	lds.packFloat32( STATE(AvatarBase)->posX );
	lds.packFloat32( STATE(AvatarBase)->posY );
	lds.packFloat32( STATE(AvatarBase)->posR );
	lds.packData( sigma, sizeof(float)*3 );
	Log.dataDump( 0, lds.stream(), lds.length(), "PF_INITALIZATION" );
	lds.unlock();
#endif

	// register with DDB
	lds.reset();
	lds.packUUID( &STATE(AvatarBase)->pfId );
	lds.packUUID( &STATE(AvatarBase)->avatarUUID );
	lds.packInt32( numParticles );
	lds.packData( &STATE(AvatarBase)->posT, sizeof(_timeb) );
	lds.packInt32( AVATAR_PFSTATE_SIZE );
	lds.packData( getDynamicBuffer(STATE(AvatarBase)->pfStateRef), sizeof(float)*numParticles*AVATAR_PFSTATE_SIZE );
	this->sendMessage( this->hostCon, MSG_DDB_ADDPARTICLEFILTER, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(AvatarBase)->SLAMmode == SLAM_MODE_DELAY ) {
		// watch particle filter
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &STATE(AvatarBase)->pfId );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
	}

	STATE(AvatarBase)->particleFilterInitialized = true;

	return 0;
}

int AvatarBase::destroyParticleFilter( bool cleanDDB ) {
	
	if ( cleanDDB ) {
		// remove from DDB
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->pfId );
		this->sendMessage( this->hostCon, MSG_DDB_REMPARTICLEFILTER, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	freeDynamicBuffer( STATE(AvatarBase)->pfStateRef );

	STATE(AvatarBase)->particleFilterInitialized = false;

	return 0;
}

int AvatarBase::generatePFState( float *sigma ) {
	int i;
	float *pstate = (float *)getDynamicBuffer( STATE(AvatarBase)->pfStateRef );

	for ( i=0; i<STATE(AvatarBase)->pfNumParticles; i++ ) {
		pstate[AVATAR_PFSTATE_X] = STATE(AvatarBase)->posX + (float)apb->apbNormalDistribution( 0, sigma[0] );
		pstate[AVATAR_PFSTATE_Y] = STATE(AvatarBase)->posY + (float)apb->apbNormalDistribution( 0, sigma[1] );
		pstate[AVATAR_PFSTATE_R] = STATE(AvatarBase)->posR + (float)apb->apbNormalDistribution( 0, sigma[2] );
		pstate += AVATAR_PFSTATE_SIZE;
	}

	return 0;
}

int AvatarBase::updatePFState() {
	float forwardD, tangentialD, rotationalD;
	float dt;
	float sn, cs;
	float *pState = (float *)getDynamicBuffer( STATE(AvatarBase)->pfStateRef );

	this->resetTimeout( &STATE(AvatarBase)->pfUpdateTimer );

	if ( !STATE(AvatarBase)->particleFilterInitialized )
		return 1;

	dt = (STATE(AvatarBase)->posT.time - STATE(AvatarBase)->lastT.time) + (STATE(AvatarBase)->posT.millitm - STATE(AvatarBase)->lastT.millitm)/1000.0f;
	
	sn = sin( STATE(AvatarBase)->lastR );
	cs = cos( STATE(AvatarBase)->lastR );
	forwardD = (STATE(AvatarBase)->posX - STATE(AvatarBase)->lastX)*cs + (STATE(AvatarBase)->posY - STATE(AvatarBase)->lastY)*sn;
	tangentialD = (STATE(AvatarBase)->posX - STATE(AvatarBase)->lastX)*-sn + (STATE(AvatarBase)->posY - STATE(AvatarBase)->lastY)*cs;
	rotationalD = STATE(AvatarBase)->posR - STATE(AvatarBase)->lastR;

	Log.log( LOG_LEVEL_ALL, "AvatarBase::updatePFState: dt %f forwardD %f tangentialD %f rotationalD %f", dt, forwardD, tangentialD, rotationalD );

	this->_updatePFState( &STATE(AvatarBase)->posT, dt, forwardD, tangentialD, rotationalD );
	
	STATE(AvatarBase)->lastX = STATE(AvatarBase)->posX;
	STATE(AvatarBase)->lastY = STATE(AvatarBase)->posY;
	STATE(AvatarBase)->lastR = STATE(AvatarBase)->posR;
	STATE(AvatarBase)->lastT.time = STATE(AvatarBase)->posT.time;
	STATE(AvatarBase)->lastT.millitm = STATE(AvatarBase)->posT.millitm;

	return 0;
}

int AvatarBase::_updatePFState( _timeb *tb, float dt, float forwardD, float tangentialD, float rotationalD ) {
	int i;
	float pforwardD, ptangentialD, protationalD;
	float sn, cs;
	float *pState = (float *)getDynamicBuffer( STATE(AvatarBase)->pfStateRef );
//	float avgState[3];
//	float avgD[3];
	
	// TODO check to see if we moved far enough to be worth a full update

	if ( dt > 2*PFUPDATE_PERIOD/1000.0f ) { // make sure dt isn't too large, otherwise the particle filter prediction can go crazy
		Log.log( 0, "AvatarBase::_updatePFState: dt too large (%.2f)! capping at %.2f", dt, 2*PFUPDATE_PERIOD/1000.0f );
		dt = 2*PFUPDATE_PERIOD/1000.0f;
	}
	
	if ( forwardD == 0 && tangentialD == 0 && rotationalD == 0 ) { // no change
#ifdef LOG_FOR_OFFLINE_SLAM
		Log.log( 0, "OFFLINE_SLAM PF_UPDATE %s %d.%03d 1",
			Log.formatUUID( 0, &STATE(AvatarBase)->pfId ), (int)tb->time, (int)tb->millitm );
#endif
		// send no change to DDB
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->pfId );
		this->ds.packData( tb, sizeof(_timeb) );
		this->ds.packChar( true );
		this->sendMessage( this->hostCon, MSG_DDB_INSERTPFPREDICTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	} else {
#ifdef LOG_FOR_OFFLINE_SLAM
		Log.log( 0, "OFFLINE_SLAM PF_UPDATE %s %d.%03d 0 %f %f %f %f %f %f %f",
			Log.formatUUID( 0, &STATE(AvatarBase)->pfId ), (int)tb->time, (int)tb->millitm,
			dt, forwardD, tangentialD, rotationalD,
			STATE(AvatarBase)->pfUpdateSigma[0], STATE(AvatarBase)->pfUpdateSigma[1], STATE(AvatarBase)->pfUpdateSigma[2] );
		// dump update in full precision
		this->ds.reset();
		this->ds.packFloat32( dt );
		this->ds.packFloat32( forwardD );
		this->ds.packFloat32( tangentialD );
		this->ds.packFloat32( rotationalD );
		this->ds.packData( STATE(AvatarBase)->pfUpdateSigma, sizeof(float)*3 );
		Log.dataDump( 0, this->ds.stream(), this->ds.length(), "PF_UPDATE_DATA" );
		this->ds.unlock();
#endif

		// DEBUG
	//	memset( avgState, 0, sizeof(float)*3 );
	//	memset( avgD, 0, sizeof(float)*3 );
	//	float fullstate[1500];
	//	memcpy( fullstate, pState, sizeof(float)*3*500 );
		for ( i=0; i<STATE(AvatarBase)->pfNumParticles; i++ ) {
			pforwardD = forwardD + (float)apb->apbNormalDistribution(0,STATE(AvatarBase)->pfUpdateSigma[0])*dt;
			ptangentialD = tangentialD + (float)apb->apbNormalDistribution(0,STATE(AvatarBase)->pfUpdateSigma[1])*dt;
			protationalD = rotationalD + (float)apb->apbNormalDistribution(0,STATE(AvatarBase)->pfUpdateSigma[2])*dt;

			sn = sin( pState[AVATAR_PFSTATE_R] );
			cs = cos( pState[AVATAR_PFSTATE_R] );

			pState[AVATAR_PFSTATE_X] = pState[AVATAR_PFSTATE_X] + cs*pforwardD - sn*ptangentialD;
			pState[AVATAR_PFSTATE_Y] = pState[AVATAR_PFSTATE_Y] + sn*pforwardD + cs*ptangentialD;
			pState[AVATAR_PFSTATE_R] = pState[AVATAR_PFSTATE_R] + protationalD;
			
			// DEBUG
	//		avgState[AVATAR_PFSTATE_X] += pState[AVATAR_PFSTATE_X];
	//		avgState[AVATAR_PFSTATE_Y] += pState[AVATAR_PFSTATE_Y];
	//		avgState[AVATAR_PFSTATE_R] += pState[AVATAR_PFSTATE_R];
	//		avgD[0] += pforwardD;
	//		avgD[1] += ptangentialD;
	//		avgD[2] += protationalD;

			pState += AVATAR_PFSTATE_SIZE;
		}

		// DEBUG unweighted pf average
	//	avgState[AVATAR_PFSTATE_X] /= STATE(AvatarBase)->pfNumParticles;
	//	avgState[AVATAR_PFSTATE_Y] /= STATE(AvatarBase)->pfNumParticles;
	//	avgState[AVATAR_PFSTATE_R] /= STATE(AvatarBase)->pfNumParticles;
	//	avgD[0] /= STATE(AvatarBase)->pfNumParticles;
	//	avgD[1] /= STATE(AvatarBase)->pfNumParticles;
	//	avgD[2] /= STATE(AvatarBase)->pfNumParticles;

		//printf( "AvatarBase::_updatePFState: avgD %f %f %f", avgD[0], avgD[1], avgD[2] );

		// send update to DDB
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->pfId );
		this->ds.packData( tb, sizeof(_timeb) );
		this->ds.packChar( false );
		this->ds.packData( getDynamicBuffer( STATE(AvatarBase)->pfStateRef ), sizeof(float)*STATE(AvatarBase)->pfNumParticles*AVATAR_PFSTATE_SIZE );
		this->sendMessage( this->hostCon, MSG_DDB_INSERTPFPREDICTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 0;
}

int AvatarBase::_resampleParticleFilter( UUID *id, int pNum, float *weights ) {
	DataStream lds;
	int i, *parents, parentNum;
	float cdfOld, cdfNew;
	float particleNumInv;
	float *state, *pState, *parentPState;

	if ( *id != STATE(AvatarBase)->pfId || pNum != STATE(AvatarBase)->pfNumParticles ) {
		Log.log( 0, "AvatarBase::_resampleParticleFilter: bad pf info (%s, %d)", Log.formatUUID(0,id), pNum );
		return 1;
	}

	// create list of parents
	parents = (int *)malloc(sizeof(int)*pNum);
	if ( !parents )
		return 1;

	// create new state
	state = (float *)malloc(sizeof(float)*pNum*AVATAR_PFSTATE_SIZE);
	if ( !state )
		return 1;

	particleNumInv = 1.0f/pNum;
	
	parentNum = 0;
	parentPState = (float *)this->getDynamicBuffer( STATE(AvatarBase)->pfStateRef );
	pState = state;

	cdfOld = *weights;
	cdfNew = (float)apb->apbUniform01()*particleNumInv;

	// identify parents and copy states
	for ( i=0; i<pNum; i++ ) {
		
		while ( cdfOld < cdfNew ) {
			if ( parentNum == pNum-1 )
				break; // don't go passed the last parent
			weights++;
			parentNum++;
			parentPState += AVATAR_PFSTATE_SIZE;
			cdfOld += *weights;
		}

		parents[i] = parentNum;
		memcpy( pState, parentPState, sizeof(float)*AVATAR_PFSTATE_SIZE );

		pState += AVATAR_PFSTATE_SIZE;
		cdfNew += particleNumInv;
	}

	// pack the data stream 
	lds.reset();
	lds.packUUID( id );
	lds.packInt32( pNum );
	lds.packInt32( AVATAR_PFSTATE_SIZE );
	lds.packData( parents, sizeof(int)*pNum );
	lds.packData( state, sizeof(float)*AVATAR_PFSTATE_SIZE*pNum );
	this->sendMessage( this->hostCon, MSG_DDB_SUBMITPFRESAMPLE, lds.stream(), lds.length() );
	lds.unlock();

	// apply locally
	UUID ref = STATE(AvatarBase)->pfStateRef;
	void *buf = getDynamicBuffer( ref );
	memcpy( buf, state, sizeof(float)*AVATAR_PFSTATE_SIZE*STATE(AvatarBase)->pfNumParticles );

	free( parents );
	free( state );

	return 0;
}
/*
int AvatarBase::_lockParticleFilter( UUID *id, UUID *key, UUID *thread, UUID *host ) {

	if ( STATE(AvatarBase)->particleFilterLocked != nilUUID ) {
		// already locked, abort new lock
		this->ds.reset();
		this->ds.packUUID( id );
		this->ds.packUUID( key );
		this->ds.packChar( DDBR_ABORT );
		this->sendMessage( this->hostCon, MSG__DDB_RESAMPLEPF_UNLOCK, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	// lock
	STATE(AvatarBase)->particleFilterLocked = *key;
	STATE(AvatarBase)->particleFilterLockingHost = *host;

	this->ds.reset();
	this->ds.packUUID( thread );
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( key );
	this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), host );
	this->ds.unlock();

	return 0;
}

int AvatarBase::_unlockParticleFilter( DataStream *ds ) {
	UUID id, key;
	char result;

	ds->unpackUUID( &id );
	ds->unpackUUID( &key );
	result = ds->unpackChar();

	if ( STATE(AvatarBase)->particleFilterLocked != key ) {
		return 1; // not locked or wrong key
	}

	if ( result == DDBR_ABORT ) {
		// unlock
		STATE(AvatarBase)->particleFilterLocked = nilUUID;

		// process any held predictions
		ParticleFilter_Prediction pred;
		while( !this->pfHeldPredictions.empty() ) {
			pred = this->pfHeldPredictions.front();
			
			this->_updatePFState( &pred.tb, pred.dt, pred.forwardD, pred.tangentialD, pred.rotationalD );

			this->pfHeldPredictions.pop_front();
		}	

	} else { // DDBR_OK
		_timeb startTime;
		int particleNum, stateSize;
		int *parents;
		float *state;

		startTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		particleNum = ds->unpackInt32();
		stateSize = ds->unpackInt32();
		parents = (int *)ds->unpackData( sizeof(int)*particleNum );
		state = (float *)ds->unpackData( sizeof(float)*stateSize*particleNum );

		// update local state
		UUID ref = STATE(AvatarBase)->pfStateRef;
		void *buf = getDynamicBuffer( ref );
		memcpy( buf, state, sizeof(float)*AVATAR_PFSTATE_SIZE*STATE(AvatarBase)->pfNumParticles );

		// unlock
		STATE(AvatarBase)->particleFilterLocked = nilUUID;

		// process any held predictions
		ParticleFilter_Prediction pred;
		while( !this->pfHeldPredictions.empty() ) {
			pred = this->pfHeldPredictions.front();
			
			this->_updatePFState( &pred.tb, pred.dt, pred.forwardD, pred.tangentialD, pred.rotationalD );

			this->pfHeldPredictions.pop_front();
		}	

	}

	return 0;
}*/

//-----------------------------------------------------------------------------
// Position/Rotation State

int AvatarBase::setPos( float x, float y, float r, _timeb *tb ) {

	if ( tb == NULL ) {
		apb->apb_ftime_s( &STATE(AvatarBase)->posT );
	} else {
		STATE(AvatarBase)->posT.time = tb->time;
		STATE(AvatarBase)->posT.millitm = tb->millitm;
	}
	STATE(AvatarBase)->lastT.time = STATE(AvatarBase)->posT.time;
	STATE(AvatarBase)->lastT.millitm = STATE(AvatarBase)->posT.millitm;

	STATE(AvatarBase)->lastX = STATE(AvatarBase)->posX = x;
	STATE(AvatarBase)->lastY = STATE(AvatarBase)->posY = y;
	STATE(AvatarBase)->lastR = STATE(AvatarBase)->posR = r;

	STATE(AvatarBase)->posInitialized = true;

	if ( STATE(AvatarBase)->particleFilterInitialized ) {
		// TODO send update to DDB, but not if we're recovering
	} else {
		this->createParticleFilter( STATE(AvatarBase)->pfNumParticles, STATE(AvatarBase)->pfInitSigma );

		STATE(AvatarBase)->pfUpdateTimer = this->addTimeout( PFNOCHANGE_PERIOD, AvatarBase_CBR_cbPFUpdateTimer );
		if ( STATE(AvatarBase)->pfUpdateTimer == nilUUID )
			return 1;
	}

	this->backup(); // backup posInitialized, particleFilterInitialized

	return 0;
}

int AvatarBase::updatePos( float dForward, float dTangential, float dRotational, _timeb *tb ) {

	if ( !STATE(AvatarBase)->posInitialized )
		return 1;

	float sn, cs;
	float dx, dy;
	sn = sin( STATE(AvatarBase)->lastR );
	cs = cos( STATE(AvatarBase)->lastR );
	dx = dForward*cs + dTangential*-sn;
	dy = dForward*sn + dTangential*cs;

	STATE(AvatarBase)->posX += dx;
	STATE(AvatarBase)->posY += dy;
	STATE(AvatarBase)->posR += dRotational;

	STATE(AvatarBase)->posT.time = tb->time;
	STATE(AvatarBase)->posT.millitm = tb->millitm;
	
	Log.log( LOG_LEVEL_ALL, "AvatarBase::updatePos delta %f %f %f", dx, dy, dRotational );

	if ( (STATE(AvatarBase)->posT.time - STATE(AvatarBase)->lastT.time)*1000 + (STATE(AvatarBase)->posT.millitm - STATE(AvatarBase)->lastT.millitm) >= PFUPDATE_PERIOD ) {
		if ( STATE(AvatarBase)->particleFilterInitialized ) {
			this->updatePFState();
		}
	}

	return 0;
}

int AvatarBase::setTargetPos( float x, float y, float r, char useRotation, UUID *initiator, int controllerIndex, UUID *thread ) {

	if ( controllerIndex < STATE(AvatarBase)->targetControllerIndex ) {
		Log.log( LOG_LEVEL_NORMAL, "AvatarBase::setTargetPos: ignoring target from %s, controllerIndex %d < current %d", Log.formatUUID(LOG_LEVEL_NORMAL,initiator), controllerIndex, STATE(AvatarBase)->targetControllerIndex );
		return 0; // ignore
	}

	if ( STATE(AvatarBase)->haveTarget ) { // notify old initiator
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->targetThread );
		this->ds.packChar( false );
		this->ds.packInt32( TP_NEW_TARGET );
		this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->targetInitiator );
		this->ds.unlock();

		// clear current actions
		this->clearActions();

		// queue a stop action so that we start fresh
		//this->queueAction( &this->uuid, NEW_MEMBER_CB(SimAvatar,cbActionFinished), 0, AA_STOP );
	}

	STATE(AvatarBase)->haveTarget = true;
	STATE(AvatarBase)->targetX = x;
	STATE(AvatarBase)->targetY = y;
	STATE(AvatarBase)->targetR = r;
	STATE(AvatarBase)->targetUseRotation = useRotation;
	STATE(AvatarBase)->targetInitiator = *initiator;
	STATE(AvatarBase)->targetControllerIndex = controllerIndex;
	STATE(AvatarBase)->targetThread = *thread;

	Log.log( LOG_LEVEL_NORMAL, "AvatarBase::setTargetPos: x %f y %f r %f useRotation %d %s, controllerIndex %d", x, y, r, useRotation, Log.formatUUID(LOG_LEVEL_NORMAL,initiator), controllerIndex );

	this->backup(); // backup target

	if ( !this->avatarReady ) {
		return 0;
	}
	
	this->getActions();

	return 0;
}

//-----------------------------------------------------------------------------
// Actions

int AvatarBase::getActions() {
	UUID thread;

	switch (this->motionPlanner) {
	case AgentPathPlanner:
		{
			if (!STATE(AvatarBase)->agentPathPlannerSpawned) {

				UUID aPathPlanneruuid;
				UuidFromString((RPC_WSTR)_T(AgentPathPlanner_UUID), &aPathPlanneruuid);
				thread = this->conversationInitiate(AvatarBase_CBR_convRequestAgentPathPlanner, REQUESTAGENTSPAWN_TIMEOUT, &aPathPlanneruuid, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				this->ds.reset();
				this->ds.packUUID(this->getUUID());
				this->ds.packUUID(&aPathPlanneruuid);
				this->ds.packChar(-1); // no instance parameters
				this->ds.packFloat32(0); // affinity
				this->ds.packChar(DDBAGENT_PRIORITY_CRITICAL);
				this->ds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length());
				this->ds.unlock();

				STATE(AvatarBase)->agentPathPlannerSpawned = -1; // in progress

			}
			else if (STATE(AvatarBase)->agentPathPlannerSpawned == 1) {

				if (STATE(AvatarBase)->agentPathPlannerStatus == DDBAGENT_STATUS_READY
					|| STATE(AvatarBase)->agentPathPlannerStatus == DDBAGENT_STATUS_FREEZING
					|| STATE(AvatarBase)->agentPathPlannerStatus == DDBAGENT_STATUS_FROZEN
					|| STATE(AvatarBase)->agentPathPlannerStatus == DDBAGENT_STATUS_THAWING) {
					// set path planner target		
					thread = this->conversationInitiate(AvatarBase_CBR_convPathPlannerSetTarget, -1);
					if (thread == nilUUID) {
						return 1;
					}
					this->ds.reset();
					this->ds.packFloat32(STATE(AvatarBase)->targetX);
					this->ds.packFloat32(STATE(AvatarBase)->targetY);
					this->ds.packFloat32(STATE(AvatarBase)->targetR);
					this->ds.packChar(STATE(AvatarBase)->targetUseRotation);
					this->ds.packUUID(&thread);
					this->sendMessageEx(this->hostCon, MSGEX(AgentPathPlanner_MSGS, MSG_SET_TARGET), this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->agentPathPlanner);
					this->ds.unlock();
				}
			}
		}
		break;
	case AgentIndividualLearning:
		{
			if (!STATE(AvatarBase)->agentIndividualLearningSpawned) {
				UUID aAgentIndividualLearninguuid;
				UuidFromString((RPC_WSTR)_T(AgentIndividualLearning_UUID), &aAgentIndividualLearninguuid);
				thread = this->conversationInitiate(AvatarBase_CBR_convRequestAgentIndividualLearning, REQUESTAGENTSPAWN_TIMEOUT, &aAgentIndividualLearninguuid, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				this->ds.reset();
				this->ds.packUUID(this->getUUID());
				this->ds.packUUID(&aAgentIndividualLearninguuid);
				this->ds.packChar(-1); // no instance parameters
				this->ds.packFloat32(0); // affinity
				this->ds.packChar(DDBAGENT_PRIORITY_CRITICAL);
				this->ds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length());
				this->ds.unlock();

				STATE(AvatarBase)->agentIndividualLearningSpawned = -1; // in progress
			}
			else if (STATE(AvatarBase)->agentIndividualLearningSpawned == 1) {
				if (STATE(AvatarBase)->agentIndividualLearningStatus == DDBAGENT_STATUS_READY
					|| STATE(AvatarBase)->agentIndividualLearningStatus == DDBAGENT_STATUS_FREEZING
					|| STATE(AvatarBase)->agentIndividualLearningStatus == DDBAGENT_STATUS_FROZEN
					|| STATE(AvatarBase)->agentIndividualLearningStatus == DDBAGENT_STATUS_THAWING) {

					thread = this->conversationInitiate(AvatarBase_CBR_convIndividualLearningRequestAction, -1);
					if (thread == nilUUID) {
						return 1;
					}
					this->ds.reset();
					this->ds.packFloat32(STATE(AvatarBase)->targetX);
					this->ds.packFloat32(STATE(AvatarBase)->targetY);
					this->ds.packFloat32(STATE(AvatarBase)->targetR);
					this->ds.packChar(STATE(AvatarBase)->targetUseRotation);
					this->ds.packUUID(&thread);
					this->sendMessageEx(this->hostCon, MSGEX(AgentIndividualLearning_MSGS, MSG_SEND_ACTION), this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->agentIndividualLearning);
					this->ds.unlock();
				}
			}
		}
		break;
	default:
		return 1;
	}

	return 0;
}

int AvatarBase::clearActions( int reason, bool aborted ) {

	this->_stopAction();

	list<ActionInfo>::iterator iterAI;
	while ( (iterAI = this->actionQueue.begin()) != this->actionQueue.end() ) {

		// notify director that the action was canceled/aborted
		RPC_STATUS Status;
		if ( !UuidCompare( &iterAI->director, &STATE(AgentBase)->uuid, &Status ) ) { // we are the director
			// TODO
		} else {
			_timeb tb;
			apb->apb_ftime_s( &tb );
			this->ds.reset();
			this->ds.packUUID( &iterAI->thread );
			if ( aborted )	this->ds.packChar( AAR_ABORTED );
			else			this->ds.packChar( AAR_CANCELLED );
			this->ds.packInt32( reason );
			this->ds.packData( &tb, sizeof(_timeb) );
			this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &iterAI->director );
			this->ds.unlock();
		}

		this->actionQueue.pop_front();
	}
	list<UUID>::iterator iter = this->actionData.begin();
	while ( iter != this->actionData.end() ) {
		freeDynamicBuffer(*iter);
		iter++;
	}
	this->actionData.clear();

	return 0;
}

int AvatarBase::abortActions( int reason ) {
	return this->clearActions( reason, true );
}

int AvatarBase::queueAction( UUID *director, UUID *thread, int action, void *data, int len ) {

	ActionInfo AI;
	AI.action = action;
	AI.director = *director;
	AI.thread = *thread;
	UUID bufRef = nilUUID;
	
	if ( !this->avatarReady ) {
		// notify director that the action was canceled/aborted
		RPC_STATUS Status;
		if ( !UuidCompare( director, &STATE(AgentBase)->uuid, &Status ) ) { // we are the director
			// TODO
		} else {
			this->ds.reset();
			this->ds.packUUID( thread );
			this->ds.packChar( AAR_ABORTED );
			this->ds.packInt32( AvatarBase_Defs::TP_NOT_STARTED );
			this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), director );
			this->ds.unlock();
		}

		return 1;
	}

	if ( len ) {
		bufRef = newDynamicBuffer(len);
	
		if ( bufRef == nilUUID )
			return 1;

		memcpy( getDynamicBuffer(bufRef), data, len );
	}

	this->actionQueue.push_back( AI );
	this->actionData.push_back( bufRef );

	if ( !STATE(AvatarBase)->actionInProgress ) {
		this->nextAction();
	}

	return 0;
}

int AvatarBase::nextAction() {
	_timeb tb;
	ActionInfo *pAI;

	if ( STATE(AvatarBase)->actionInProgress ) { // finish the currect action
		pAI = &(*this->actionQueue.begin());

		if ( pAI->action == AA_MOVE
		  || pAI->action == AA_ROTATE ) {
			this->updatePFState(); // update our state
		}
		
		// notify director that the action is complete
		RPC_STATUS Status;
		if ( !UuidCompare( &pAI->director, &STATE(AgentBase)->uuid, &Status ) ) { // we are the director
			// TODO
		} else {
			apb->apb_ftime_s( &tb );
			this->ds.reset();
			this->ds.packUUID( &pAI->thread );
			this->ds.packChar( AAR_SUCCESS );
			this->ds.packData( &tb, sizeof(_timeb) );
			this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &pAI->director );
			this->ds.unlock();
		}

		this->actionQueue.pop_front();
		freeDynamicBuffer( *this->actionData.begin() );
		this->actionData.pop_front();

		STATE(AvatarBase)->actionInProgress = false;
	} 

	if ( STATE(AvatarBase)->SLAMmode == SLAM_MODE_DELAY ) {
		if ( STATE(AvatarBase)->SLAMdelayNextAction )
			return 0; // delay 

		if ( STATE(AvatarBase)->SLAMreadingsProcessing > 0 ) {
			STATE(AvatarBase)->SLAMdelayNextAction = true;
			return 0; // delay
		}
	}
	
	if ( this->actionQueue.begin() != this->actionQueue.end() ) {
		pAI = &(*this->actionQueue.begin());

		if ( pAI->action == AA_MOVE
		  || pAI->action == AA_ROTATE ) {
			_timeb tb;
			apb->apb_ftime_s( &tb );
			this->updatePos( 0, 0, 0, &tb ); // refresh our pos to begin move
		}

		STATE(AvatarBase)->actionInProgress = true;
	}

	return 0;
}

int AvatarBase::_stopAction() {

	if ( STATE(AvatarBase)->actionTimeout != nilUUID ) {
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = nilUUID;
	}
	STATE(AvatarBase)->actionInProgress = false;

	return 0;
}

int AvatarBase::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

	int type;
	char evt;
	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	if ( evt == DDBE_WATCH_ITEM ) {
		if ( uuid == STATE(AvatarBase)->agentPathPlanner ) {
			// get status
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(AvatarBase)->agentPathPlanner, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &STATE(AvatarBase)->agentPathPlanner );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
		else if (uuid == STATE(AvatarBase)->agentIndividualLearning) {
			// get status
			UUID thread = this->conversationInitiate(AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(AvatarBase)->agentIndividualLearning, sizeof(UUID));
			sds.reset();
			sds.packUUID(&STATE(AvatarBase)->agentIndividualLearning);
			sds.packInt32(DDBAGENTINFO_RSTATUS);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length());
			sds.unlock();
		} else if ( uuid == STATE(AvatarBase)->avatarExecutiveUUID ) {
			// get status
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(AvatarBase)->avatarExecutiveUUID, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( this->agentsProcessing.find(uuid) != this->agentsProcessing.end() ) { // one of our processing agents
			// get status
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_AGENT && uuid == STATE(AvatarBase)->agentPathPlanner ) { // agent path planner
		if ( evt == DDBE_AGENT_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
				
				// get status
				UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &STATE(AvatarBase)->agentPathPlanner );
				sds.packInt32( DDBAGENTINFO_RSTATUS );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	}
	else if (type == DDB_AGENT && uuid == STATE(AvatarBase)->agentIndividualLearning) { // agent path planner
		if (evt == DDBE_AGENT_UPDATE) {
			int infoFlags = lds.unpackInt32();
			if (infoFlags & DDBAGENTINFO_STATUS) { // status changed

												   // get status
				UUID thread = this->conversationInitiate(AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
				sds.reset();
				sds.packUUID(&STATE(AvatarBase)->agentIndividualLearning);
				sds.packInt32(DDBAGENTINFO_RSTATUS);
				sds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length());
				sds.unlock();
			}
		}
	} else if ( type == DDB_AGENT && uuid == STATE(AvatarBase)->avatarExecutiveUUID ) { // agent path planner
		if ( evt == DDBE_AGENT_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
				
				// get status
				UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
				sds.packInt32( DDBAGENTINFO_RSTATUS );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	} else if ( type == DDB_AGENT && this->agentsStatus.find(uuid) != this->agentsStatus.end() ) { // our agent
		if ( evt == DDBE_AGENT_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
				
				// get status
				UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &uuid );
				sds.packInt32( DDBAGENTINFO_RSTATUS );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	} else if ( type & DDB_SENSORS ) { // must be one of our sensors
		if ( evt == DDBE_SENSOR_UPDATE ) {
			_timeb tb;
			READING_TYPE reading;
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			reading.sensor = type;
			reading.phase = 0;
			this->newSensorReading( &reading, &uuid, &tb );
		}
	} else if ( type & DDB_PARTICLEFILTER && evt == DDBE_PF_PREDICTION ) {
		_timeb tb;
		READING_TYPE reading;
		tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
		char nochange = lds.unpackChar();
		lds.unlock();
		if ( !nochange ) {
			reading.sensor = type;
			reading.phase = 0;
			this->newSensorReading( &reading, &uuid, &tb );
		}
	}
	lds.unlock();
	
	return 0;
}

//-----------------------------------------------------------------------------
// SLAM delay mode

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\AgentSensorSonar\\AgentSensorSonarVersion.h"
#include "..\\AgentSensorLandmark\\AgentSensorLandmarkVersion.h"
#include "..\\AgentSensorFloorFinder\\AgentSensorFloorFinderVersion.h"
#include "..\\AgentSensorCooccupancy\\AgentSensorCooccupancyVersion.h"

int AvatarBase::getProcessingPhases( int sensor ) {
	switch ( sensor ) {
	case DDB_INVALID:
	case DDB_PARTICLEFILTER:
	case DDB_SENSOR_SONAR:
		return 1;
	case DDB_SENSOR_CAMERA:
		return 2;
	case DDB_SENSOR_SIM_CAMERA:
		return 2;
	default:
		Log.log( 0, "AvatarBase::getProcessingPhases: unknown sensor type!" );
		return 0;
	}
}

int AvatarBase::requestAgentSpawn( READING_TYPE *type, char priority ) {

	this->agentsSpawning[*type]++; // we're spawning this type of agent

	UUID sAgentuuid;
	UUID thread;
	switch ( type->sensor ) {
	case DDB_PARTICLEFILTER:
		UuidFromString( (RPC_WSTR)_T(AgentSensorCooccupancy_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_SONAR:
		UuidFromString( (RPC_WSTR)_T(AgentSensorSonar_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_CAMERA:
		if ( type->phase == 0 )		UuidFromString( (RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid );
		else						UuidFromString( (RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_SIM_CAMERA:
		if ( type->phase == 0 )		UuidFromString( (RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid );
		else						UuidFromString( (RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid );
		break;
	default:
		Log.log( 0, "AvatarBase::requestAgentSpawn: unknown sensor type!" );
		return NULL;
	}
	thread = this->conversationInitiate( AvatarBase_CBR_convRequestAgentSpawn, REQUESTAGENTSPAWN_TIMEOUT, type, sizeof(READING_TYPE) );
	if ( thread == nilUUID ) {
		return NULL;
	}

	Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::requestAgentSpawn: requesting agent %s %d-%d", Log.formatUUID(LOG_LEVEL_VERBOSE,&sAgentuuid), type->sensor, type->phase );

	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( &sAgentuuid );
	this->ds.packChar( -1 ); // no instance parameters
	this->ds.packFloat32( 0.0f ); // affinity
	this->ds.packChar( priority );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

UUID * AvatarBase::getProcessingAgent( READING_TYPE *type ) {

	// see if we already have agents of this type
	mapTypeAgents::iterator iterTA;
	iterTA = this->agents.find( *type );

	if ( iterTA == this->agents.end() || iterTA->second.empty() ) { // we need to spawn an agent of this type
		mapAgentSpawning::iterator iterSpawning;
		iterSpawning = this->agentsSpawning.find( *type );
		if ( iterSpawning != this->agentsSpawning.end() )
			return NULL; // already spawning
	
		this->requestAgentSpawn( type, DDBAGENT_PRIORITY_CRITICAL );

		return NULL;
	} else { // pick the agent with the least outstanding readings
		int bestReadings = 999999999; // large
		int readings;
		UUID *bestAgent;

		std::list<UUID>::iterator iter;
		iter = iterTA->second.begin();
		while ( iter != iterTA->second.end() ) {
			readings = (int)this->agentQueue[*iter].size();
			if ( readings == 0 )
				return (UUID *)&(*iter);

			if ( readings < bestReadings ) {
				bestReadings = readings;
				bestAgent = (UUID *)&(*iter);
			}

			iter++;
		}

		return bestAgent;
	}
}

int AvatarBase::nextSensorReading( UUID *agent ) {
	bool agentReady = false;
	std::map<UUID,int,UUIDless>::iterator iS = this->agentsStatus.find( *agent );

	if ( iS != this->agentsStatus.end() 
	  && ( iS->second == DDBAGENT_STATUS_READY
	  || iS->second == DDBAGENT_STATUS_FREEZING
	  || iS->second == DDBAGENT_STATUS_FROZEN
	  || iS->second == DDBAGENT_STATUS_THAWING) ) {
		agentReady = true;
	}

	if ( !this->agentsProcessing[*agent] && !this->agentQueue[*agent].empty() && agentReady ) { // assign next reading
		READING_QUEUE rqNext = this->agentQueue[*agent].front();

		Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::nextSensorReading: assign next reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rqNext.uuid) );

		// assign to agent
		this->agentsProcessing[*agent] = true;
		this->ds.reset();
		this->ds.packUUID( &rqNext.uuid );
		this->ds.packData( (void *)&rqNext.tb, sizeof(_timeb) );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_ADD_READING), this->ds.stream(), this->ds.length(), agent );
		this->ds.unlock();
	}

	return 0;
}

int AvatarBase::assignSensorReading( UUID *agent, READING_QUEUE *rq ) {
	
	bool agentReady = false;
	std::map<UUID,int,UUIDless>::iterator iS = this->agentsStatus.find( *agent );

	if ( iS != this->agentsStatus.end() 
	  && ( iS->second == DDBAGENT_STATUS_READY
	  || iS->second == DDBAGENT_STATUS_FREEZING
	  || iS->second == DDBAGENT_STATUS_FROZEN  
	  || iS->second == DDBAGENT_STATUS_THAWING) ) {
		agentReady = true;
	}

	if ( this->agentQueue[*agent].empty() && agentReady ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::assignSensorReading: agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
		// add to agent queue
		this->agentQueue[*agent].push_back( *rq );

		// assign to agent
		this->agentsProcessing[*agent] = true;
		this->ds.reset();
		this->ds.packUUID( &rq->uuid );
		this->ds.packData( (void *)&rq->tb, sizeof(_timeb) );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_ADD_READING), this->ds.stream(), this->ds.length(), agent );
		this->ds.unlock();
	} else { // agent is already processing
		Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::assignSensorReading: queue reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
		// add to agent queue
		this->agentQueue[*agent].push_front( *rq );
	}

	return 0;
}

int AvatarBase::doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success ) {
	READING_QUEUE rq;
	RPC_STATUS Status;

	Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::doneSensorReading: agent %s sensor %s success %d", Log.formatUUID(LOG_LEVEL_VERBOSE,uuid), Log.formatUUID(LOG_LEVEL_VERBOSE,sensor), success );
	
	if ( this->agentQueue[*uuid].empty() )
		return 1; // what happened?

	std::list<READING_QUEUE>::iterator iter = this->agentQueue[*uuid].begin();
	while ( iter != this->agentQueue[*uuid].end() ) { // find the reading
		if ( !UuidCompare( &(*iter).uuid, sensor, &Status ) && (*iter).tb.time == tb->time && (*iter).tb.millitm == tb->millitm )
			break;
		iter++;
	}

	if ( iter == this->agentQueue[*uuid].end() )
		return 1; // couldn't find the reading?

	rq = *iter;
	this->agentQueue[*uuid].erase( iter );

	this->agentsProcessing[*uuid] = false;

	// tell agent to process next assigned reading
	this->nextSensorReading( uuid );

	if ( success == 1 ) { 
		rq.type.phase++;
		
		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
		
	} else if ( success == -1 ) { // permenant failure
		rq.type.phase++;

		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
	} else { // failure
		// assign reading again
		this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
	}

	STATE(AvatarBase)->SLAMreadingsProcessing--;
	if ( STATE(AvatarBase)->SLAMdelayNextAction && 	STATE(AvatarBase)->SLAMreadingsProcessing == 0 ) {
		STATE(AvatarBase)->SLAMdelayNextAction = false;
		this->nextAction();
	}

	return 0;
}

int AvatarBase::newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt ) {
	READING_QUEUE rq;
	UUID *agent;

	Log.log( LOG_LEVEL_VERBOSE, "AvatarBase::newSensorReading: type %d-%d id %s attempt %d", type->sensor, type->phase, Log.formatUUID(LOG_LEVEL_VERBOSE,uuid), attempt );

	if ( attempt > 5 ) // give up on this reading
		return 0;

	STATE(AvatarBase)->SLAMreadingsProcessing++;

	rq.type = *type;
	rq.uuid = *uuid;
	rq.tb = *tb;
	rq.attempt = attempt + 1;

	// get appropriate processing agent
	agent = this->getProcessingAgent( type );

	if ( !agent ) {
		// assign reading to wait queue
		this->typeQueue[*type].push_front( rq );
	} else {
		// assign reading to agent
		this->assignSensorReading( agent, &rq );
	}

	// check if we should backup
/*	this->backupReadingsCount--;
	if ( this->backupReadingsCount == 0 ) {
		this->backupReadingsCount = BACKUP_READINGS_THRESHOLD;
		this->backup();
	}
*/
	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AvatarBase::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case MSG_DDB_PFRESAMPLEREQUEST:
		{
			UUID id;
			int pNum;
			float *weights;
			lds.setData( data, len );
			lds.unpackUUID( &id );
			pNum = lds.unpackInt32();
			weights = (float *)lds.unpackData( sizeof(float)*pNum );
			this->_resampleParticleFilter( &id, pNum, weights );
		}
		break;
	case MSG_DONE_SENSOR_READING:
		{
			UUID sensor;
			_timeb tb;
			char success;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &sensor );
			tb = *(_timeb*)lds.unpackData( sizeof(_timeb) );
			success = lds.unpackChar();
			lds.unlock();
			this->doneSensorReading( &uuid, &sensor, &tb, success );
		}
		break;
	case AvatarBase_MSGS::MSG_ADD_MAP:
		if ( STATE(AvatarBase)->mapId == nilUUID ) {
			STATE(AvatarBase)->mapId = *(UUID *)data;
			this->backup(); // backup mapId
		}
		break;
	case AvatarBase_MSGS::MSG_ADD_REGION:
		if ( STATE(AvatarBase)->missionRegionId == nilUUID ) {
			STATE(AvatarBase)->missionRegionId = *(UUID *)data;
			this->backup(); // backup missionRegionId
		}
		break;
	case AvatarBase_MSGS::MSG_SET_POS:
		{
			float x, y, r;
			lds.setData( data, len );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			lds.unlock();
			if ( !STATE(AvatarBase)->posInitialized )
				this->setPos( x, y, r );
		}
		break;
	case AvatarBase_MSGS::MSG_SET_TARGET:
		{
			float x, y, r;
			char useRotation;
			UUID initiator;
			int index;
			UUID thread;
			lds.setData( data, len );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			useRotation = lds.unpackChar();
			lds.unpackUUID( &initiator );
			index = lds.unpackInt32();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->setTargetPos( x, y, r, useRotation, &initiator, index, &thread );
		}
		break;
	case AvatarBase_MSGS::MSG_ACTION_QUEUE:
		{
			int offset;
			UUID director;
			UUID thread;
			int action;
			lds.setData( data, len );
			lds.unpackUUID( &director );
			lds.unpackUUID( &thread );
			action = lds.unpackInt32();
			offset = sizeof(UUID)*2 + 4;
			lds.unlock();
			this->queueAction( &director, &thread, action, data + offset, len - offset );
		}
		break;
	case AvatarBase_MSGS::MSG_ACTION_CLEAR:
		this->clearActions( *(int *)data );
		break;
	case AvatarBase_MSGS::MSG_ACTION_ABORT:
		this->abortActions( *(int *)data );
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool AvatarBase::cbRequestExecutiveAvatarId( void *NA ) {
	// TEMP using ExecutiveMission id for now
	UUID thread;
	UUID avatarExecType;
	UuidFromString( (RPC_WSTR)_T(ExecutiveMission_UUID), &avatarExecType );
	thread = this->conversationInitiate( AvatarBase_CBR_convRequestExecutiveAvatarId, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &avatarExecType );
	this->ds.packChar( -1 );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RUNIQUEID, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

bool AvatarBase::convRequestExecutiveAvatarId( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	UUID thread;
	char response;

	if ( conv->response == NULL ) { // request timed out
		Log.log( 0, "AvatarBase::convRequestExecutiveAvatarId: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackUUID( &thread ); // thread
	
	response = lds.unpackChar();

	if ( response == 1 ) { // spawned
		lds.unpackUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
		lds.unlock();

		Log.log( 0, "AvatarBase::convRequestExecutiveAvatarId: got ExecuitevAvatar id" );

		// register as agent watcher
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		this->backup(); // backup avatarExecutiveUUID
	} else { // not spawned
		lds.unlock();

		Log.log( 0, "AvatarBase::convRequestExecutiveAvatarId: ExecuitevAvatar not found, trying again shortly" );

		// ask again in a few seconds
		this->addTimeout( 2000, AvatarBase_CBR_cbRequestExecutiveAvatarId );
	}

	return 0;
}

bool AvatarBase::convRequestAgentPathPlanner( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "AvatarBase::convRequestAgentPathPlanner: request spawn timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackBool() ) { // succeeded
		lds.unpackUUID( &STATE(AvatarBase)->agentPathPlanner );
		lds.unlock();
		STATE(AvatarBase)->agentPathPlannerSpawned = 1; // ready

		Log.log( 0, "AvatarBase::convRequestAgentPathPlanner: path planner %s", Log.formatUUID(0,&STATE(AvatarBase)->agentPathPlanner) );

		// register as agent watcher
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &STATE(AvatarBase)->agentPathPlanner );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &STATE(AvatarBase)->agentPathPlanner );

		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &STATE(AvatarBase)->mapId );
		lds.packUUID( &STATE(AvatarBase)->missionRegionId );
		lds.packUUID( &STATE(AvatarBase)->pfId );
		lds.packFloat32( STATE(AvatarBase)->maxLinear );
		lds.packFloat32( STATE(AvatarBase)->maxRotation );
		lds.packFloat32( STATE(AvatarBase)->minLinear );
		lds.packFloat32( STATE(AvatarBase)->minRotation );
		this->sendMessageEx( this->hostCon, MSGEX(AgentPathPlanner_MSGS,MSG_CONFIGURE), lds.stream(), lds.length(), &STATE(AvatarBase)->agentPathPlanner );
		lds.unlock();

		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &STATE(AvatarBase)->agentPathPlanner );
		lds.unlock();

		this->backup(); // backup agentPathPlanner

	} else {
		lds.unlock();

		STATE(AvatarBase)->agentPathPlannerSpawned = 0;
		// TODO try again?
	}

	return 0;
}

bool AvatarBase::convRequestAgentIndividualLearning(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // spawn timed out
		Log.log(0, "AvatarBase::convRequestAgentIndividualLearning: request spawn timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	if (lds.unpackBool()) { // succeeded
		lds.unpackUUID(&STATE(AvatarBase)->agentIndividualLearning);
		lds.unlock();
		STATE(AvatarBase)->agentIndividualLearningSpawned = 1; // ready

		Log.log(0, "AvatarBase::convRequestAgentIndividualLearning: Individual learning agent %s", Log.formatUUID(0, &STATE(AvatarBase)->agentIndividualLearning));

		// register as agent watcher
		lds.reset();
		lds.packUUID(&STATE(AgentBase)->uuid);
		lds.packUUID(&STATE(AvatarBase)->agentIndividualLearning);
		this->sendMessage(this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length());
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		// set parent
		this->sendMessage(this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &STATE(AvatarBase)->agentIndividualLearning);

		lds.reset();
		lds.packUUID(&STATE(AgentBase)->uuid);
		this->sendMessageEx(this->hostCon, MSGEX(AgentIndividualLearning_MSGS, MSG_CONFIGURE), lds.stream(), lds.length(), &STATE(AvatarBase)->agentIndividualLearning);
		lds.unlock();

		lds.reset();
		lds.packString(STATE(AgentBase)->missionFile);
		this->sendMessage(this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &STATE(AvatarBase)->agentIndividualLearning);
		lds.unlock();

		this->backup(); // backup agentPathPlanner

	}
	else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool AvatarBase::convIndividualLearningRequestAction( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "AvatarBase::convIndividualLearningRequestAction: conversation timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() ) { // succeeded
		this->ds.unlock();
		
		// notify initiator
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->targetThread );
		this->ds.packChar( true );
		this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->targetInitiator );
		this->ds.unlock();

	} else {
		int reason = this->ds.unpackInt32();
		this->ds.unlock();
		Log.log(0, "AvatarBase::convIndividualLearningRequestAction: send action fail %d", reason );
		if ( reason == AvatarBase_Defs::TP_NEW_TARGET ) { 
			// we gave them a new target, no reason to do anything
		} else {
			// notify initiator
			this->ds.reset();
			this->ds.packUUID( &STATE(AvatarBase)->targetThread );
			this->ds.packChar( false );
			this->ds.packInt32( reason );
			this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->targetInitiator );
			this->ds.unlock();
		}
	}

	return 0;
}

bool AvatarBase::convPathPlannerSetTarget(void *vpConv) {
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // spawn timed out
		Log.log(0, "AvatarBase::convPathPlannerSetTarget: conversation timed out");
		return 0; // end conversation
	}

	this->ds.setData(conv->response, conv->responseLen);
	this->ds.unpackData(sizeof(UUID)); // discard thread

	if (this->ds.unpackChar()) { // succeeded
		this->ds.unlock();
		STATE(AvatarBase)->haveTarget = false;

		Log.log(LOG_LEVEL_NORMAL, "AvatarBase::convPathPlannerSetTarget: target reached");

		// notify initiator
		this->ds.reset();
		this->ds.packUUID(&STATE(AvatarBase)->targetThread);
		this->ds.packChar(true);
		this->sendMessage(this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->targetInitiator);
		this->ds.unlock();

	}
	else {
		int reason = this->ds.unpackInt32();
		this->ds.unlock();
		Log.log(0, "AvatarBase::convPathPlannerSetTarget: target fail %d", reason);
		if (reason == AvatarBase_Defs::TP_NEW_TARGET) {
			// we gave them a new target, no reason to do anything
		}
		else {
			// notify initiator
			this->ds.reset();
			this->ds.packUUID(&STATE(AvatarBase)->targetThread);
			this->ds.packChar(false);
			this->ds.packInt32(reason);
			this->sendMessage(this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->targetInitiator);
			this->ds.unlock();
		}
	}

	return 0;
}

bool AvatarBase::convPFInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AvatarBase::convPFInfo: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		_timeb *tb;
	
		if ( lds.unpackInt32() != (DDBPFINFO_USECURRENTTIME | DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE | DDBPFINFO_MEAN) ) {
			lds.unlock();
			return 0; // what happened here?
		}

		tb = (_timeb *)lds.unpackData( sizeof(_timeb) );
		STATE(AvatarBase)->pfNumParticles = lds.unpackInt32();

		// create state buf
		STATE(AvatarBase)->pfStateRef = this->newDynamicBuffer( STATE(AvatarBase)->pfNumParticles * AVATAR_PFSTATE_SIZE * sizeof(float) );
		if ( STATE(AvatarBase)->pfStateRef == nilUUID ) {
			return 0; // malloc
		}
		memcpy( this->getDynamicBuffer(STATE(AvatarBase)->pfStateRef), lds.unpackData(STATE(AvatarBase)->pfNumParticles * AVATAR_PFSTATE_SIZE * sizeof(float)), STATE(AvatarBase)->pfNumParticles * AVATAR_PFSTATE_SIZE * sizeof(float) );
		float *pos = (float *)lds.unpackData( AVATAR_PFSTATE_SIZE * sizeof(float) );
		
		// set pos
		this->setPos( pos[0], pos[1], pos[2] );

		Log.log( 0, "AvatarBase::convPFInfo: state x %f y %f r %f", pos[0], pos[1], pos[2] );

		lds.unlock();

		this->recoveryCheck( &this->AvatarBase_recoveryLock ); // clear recovery lock

	} else {
		lds.unlock();

		Log.log( 0, "AvatarBase::convPFInfo: request failed %d", result );

		// get try again
		_timeb tb; // dummy
		UUID thread = this->conversationInitiate( AvatarBase_CBR_convPFInfo, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( &STATE(AvatarBase)->pfId );
		lds.packInt32( DDBPFINFO_USECURRENTTIME | DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE | DDBPFINFO_MEAN );
		lds.packData( &tb, sizeof(_timeb) );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
		lds.unlock();
	}

	return 0;
}

bool AvatarBase::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AvatarBase::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID id = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		if ( lds.unpackInt32() != DDBAGENTINFO_RSTATUS ) {
			lds.unlock();
			return 0; // what happened here?
		}

		int status = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "AvatarBase::convAgentInfo: status %d (%s)", status, Log.formatUUID(0,&id) );

		if (id == STATE(AvatarBase)->agentPathPlanner) {
			STATE(AvatarBase)->agentPathPlannerStatus = status;

			if (STATE(AvatarBase)->haveTarget && this->avatarReady)
			{
				this->getActions();
			}
		} else if (id == STATE(AvatarBase)->agentIndividualLearning) {
			STATE(AvatarBase)->agentIndividualLearningStatus = status;
			this->getActions();
		} else if ( id == STATE(AvatarBase)->avatarExecutiveUUID ) {
			if ( status == DDBAGENT_STATUS_READY ) {
				// register with executive
				this->ds.reset();
				this->ds.packUUID( &STATE(AgentBase)->uuid );
				this->ds.packData( &STATE(AgentBase)->agentType, sizeof(AgentType) );
				this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
				this->ds.packInt32( STATE(AvatarBase)->sensorTypes );
				this->sendMessage( this->hostCon, MSG_AGENT_GREET, this->ds.stream(), this->ds.length(), &STATE(AvatarBase)->avatarExecutiveUUID );
				this->ds.unlock();
			}
		} else if ( this->agentsProcessing.find(id) != this->agentsProcessing.end() ) {
			this->agentsStatus[id] = status;

			if ( this->agentsStatus[id] != DDBAGENT_STATUS_READY
			  && this->agentsStatus[id] != DDBAGENT_STATUS_FREEZING
			  && this->agentsStatus[id] != DDBAGENT_STATUS_FROZEN
			  && this->agentsStatus[id] != DDBAGENT_STATUS_THAWING ) {
				this->agentsProcessing[id] = false; // give up
			}

			// try do process a reading
			this->nextSensorReading( &id );
		}

	} else {
		lds.unlock();

		Log.log( 0, "AvatarBase::convAgentInfo: request failed %d", result );
	}

	return 0;
}

bool AvatarBase::cbPFUpdateTimer( void *NA ) {

	apb->apb_ftime_s( &STATE(AvatarBase)->posT );

	this->updatePFState();
	
	return 1; // repeat
}

bool AvatarBase::cbRetire( void *NA ) {

	this->retireAvatar();

	return 0;
}

bool AvatarBase::convRequestAgentSpawn( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	READING_TYPE type = *(READING_TYPE *)conv->data;
	
	this->agentsSpawning[type]--; // spawn is over for good or ill

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "AvatarBase::convRequestAgentSpawn: request spawn timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	if ( lds.unpackBool() ) { // succeeded
		UUID uuid;

		lds.unpackUUID( &uuid );
		lds.unlock();

		Log.log( 0, "AvatarBase::convRequestAgentSpawn: spawn succeeded %d-%d (%s)", type.sensor, type.phase, Log.formatUUID(0, &uuid) );

		// add this agent to our list
		this->agents[type].push_back( uuid );
		this->agentsProcessing[uuid] = false;

		// register as agent watcher
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &uuid );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &uuid );

		// configure agent
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &STATE(AvatarBase)->mapId );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_CONFIGURE), lds.stream(), lds.length(), &uuid );
		lds.unlock();

		// start the agent
		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &uuid );
		lds.unlock();

		// if we have readings waiting then assign them
		mapTypeQueue::iterator iterTQ = this->typeQueue.find( type );
		if ( iterTQ != this->typeQueue.end() ) {
			while ( !iterTQ->second.empty() ) {
				this->assignSensorReading( &uuid, &iterTQ->second.front() );
				iterTQ->second.pop_front();
			}			
		}
		
		this->backup(); // agents

	} else {
		lds.unlock();
	
		Log.log( 0, "AvatarBase::convRequestAgentSpawn: spawn failed %d-%d", type.sensor, type.phase );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// State functions

int AvatarBase::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AvatarBase::thaw( DataStream *ds, bool resumeReady ) {
	int ret = AgentBase::thaw( ds, resumeReady );

	return ret;
}

int	AvatarBase::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AvatarBase);

	// pack actions
	_WRITE_STATE_LIST( ActionInfo, &this->actionQueue );
	_WRITE_STATE_LIST( UUID, &this->actionData );

	// agents
	mapTypeAgents::iterator iA;
	for ( iA = this->agents.begin(); iA != this->agents.end(); iA++ ) {
		ds->packBool( 1 );
		ds->packData( (void *)&iA->first, sizeof(READING_TYPE) );
		_WRITE_STATE_LIST( UUID, &iA->second );
	}
	ds->packBool( 0 );

	//  agentsSpawning, agentsStatus, agentsProcessing
	_WRITE_STATE_MAP_LESS( READING_TYPE, int, READING_TYPEless, &this->agentsSpawning );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->agentsStatus );
	_WRITE_STATE_MAP_LESS( UUID, bool, UUIDless, &this->agentsProcessing );

	// typeQueue
	mapTypeQueue::iterator iTQ;
	for ( iTQ = this->typeQueue.begin(); iTQ != this->typeQueue.end(); iTQ++ ) {
		ds->packBool( 1 );
		ds->packData( (void *)&iTQ->first, sizeof(READING_TYPE) );
		_WRITE_STATE_LIST( READING_QUEUE, &iTQ->second );
	}
	ds->packBool( 0 );

	// agentQueue
	mapAgentQueue::iterator iAQ;
	for ( iAQ = this->agentQueue.begin(); iAQ != this->agentQueue.end(); iAQ++ ) {
		ds->packBool( 1 );
		ds->packData( (void *)&iAQ->first, sizeof(UUID) );
		_WRITE_STATE_LIST( READING_QUEUE, &iAQ->second );
	}
	ds->packBool( 0 );

	return AgentBase::writeState( ds, false );;
}

int	AvatarBase::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AvatarBase);

	// unpack actions
	_READ_STATE_LIST( ActionInfo, &this->actionQueue );
	_READ_STATE_LIST( UUID, &this->actionData );

	// agents
	READING_TYPE keyA;
	while ( ds->unpackBool() ) {
		keyA = *(READING_TYPE *)ds->unpackData( sizeof(READING_TYPE) );
		_READ_STATE_LIST( UUID, &this->agents[keyA] ); // unpack agents
	}

	//  agentsSpawning, agentsStatus, agentsProcessing
	_READ_STATE_MAP( READING_TYPE, int, &this->agentsSpawning );
	_READ_STATE_MAP( UUID, int, &this->agentsStatus );
	_READ_STATE_MAP( UUID, bool, &this->agentsProcessing );

	// typeQueue
	READING_TYPE keyTQ;
	while ( ds->unpackBool() ) {
		keyTQ = *(READING_TYPE *)ds->unpackData( sizeof(READING_TYPE) );
		_READ_STATE_LIST( READING_QUEUE, &this->typeQueue[keyTQ] ); // unpack types
	}

	// agentQueue
	UUID keyAQ;
	while ( ds->unpackBool() ) {
		keyAQ = *(UUID *)ds->unpackData( sizeof(UUID) );
		_READ_STATE_LIST( READING_QUEUE, &this->agentQueue[keyAQ] ); // unpack types
	}

	return AgentBase::readState( ds, false );
}


int AvatarBase::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		if ( STATE(AvatarBase)->avatarExecutiveUUID == nilUUID ) { // don't know the avatar executive yet
			// find the Avatar Executive so we can register
			// TEMP using the Mission Executive instead
			UUID avatarExecType;
			UuidFromString( (RPC_WSTR)_T(ExecutiveMission_UUID), &avatarExecType );
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convRequestExecutiveAvatarId, 30000 );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &avatarExecType );
			lds.packChar( -1 );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_RUNIQUEID, lds.stream(), lds.length() );
			lds.unlock();
		} else { // know the avatar executive, check if we have all necessary info
			if ( !STATE(AvatarBase)->posInitialized
			  || STATE(AvatarBase)->mapId == nilUUID 
			  || STATE(AvatarBase)->missionRegionId == nilUUID ) {
				// register
				lds.reset();
				lds.packUUID( &STATE(AgentBase)->uuid );
				lds.packData( &STATE(AgentBase)->agentType, sizeof(AgentType) );
				lds.packUUID( &STATE(AvatarBase)->avatarUUID );
				lds.packInt32( STATE(AvatarBase)->sensorTypes );
				this->sendMessage( this->hostCon, MSG_AGENT_GREET, lds.stream(), lds.length(), &STATE(AvatarBase)->avatarExecutiveUUID );
				lds.unlock();
			}
		}

		// get path planner status
		if ( STATE(AvatarBase)->agentPathPlanner != nilUUID ) {
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(AvatarBase)->agentPathPlanner, sizeof(UUID) );
			lds.reset();
			lds.packUUID( &STATE(AvatarBase)->agentPathPlanner );
			lds.packInt32( DDBAGENTINFO_RSTATUS );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
		// get avatar executive status
		if ( STATE(AvatarBase)->avatarExecutiveUUID != nilUUID ) {
			UUID thread = this->conversationInitiate( AvatarBase_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(AvatarBase)->avatarExecutiveUUID, sizeof(UUID) );
			lds.reset();
			lds.packUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
			lds.packInt32( DDBAGENTINFO_RSTATUS );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	if ( STATE(AvatarBase)->retireSet ) {
		unsigned int dt;
		_timeb t;
		apb->apb_ftime_s( &t );
		dt = (unsigned int)((STATE(AvatarBase)->retireTime.time - t.time)*1000 + (STATE(AvatarBase)->retireTime.millitm - t.millitm));
		this->addTimeout( dt, AvatarBase_CBR_cbRetire );
	}

	return 0;
}

int AvatarBase::writeBackup( DataStream *ds ) {

	// avatar
	ds->packUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
	ds->packUUID( &STATE(AvatarBase)->avatarUUID );
	ds->packData( &STATE(AvatarBase)->retireTime, sizeof(_timeb) );
	ds->packChar( STATE(AvatarBase)->retireMode );
	ds->packBool( STATE(AvatarBase)->retireSet );
	ds->packInt32( STATE(AvatarBase)->sensorTypes );

	// particle filter
	ds->packUUID( &STATE(AvatarBase)->pfId );
	ds->packBool( STATE(AvatarBase)->particleFilterInitialized );
	ds->packData( STATE(AvatarBase)->pfInitSigma, sizeof(float)*AVATAR_PFSTATE_SIZE );
	ds->packData( STATE(AvatarBase)->pfUpdateSigma, sizeof(float)*AVATAR_PFSTATE_SIZE );

	// position
	ds->packBool( STATE(AvatarBase)->posInitialized );
	ds->packFloat32( STATE(AvatarBase)->posX );
	ds->packFloat32( STATE(AvatarBase)->posY );
	ds->packFloat32( STATE(AvatarBase)->posR );
	
	// map and target
	ds->packUUID( &STATE(AvatarBase)->mapId );
	ds->packUUID( &STATE(AvatarBase)->missionRegionId );
	ds->packUUID( &STATE(AvatarBase)->agentPathPlanner );
	ds->packUUID(&STATE(AvatarBase)->agentIndividualLearning);
	ds->packBool( STATE(AvatarBase)->haveTarget );
	ds->packFloat32( STATE(AvatarBase)->targetX );
	ds->packFloat32( STATE(AvatarBase)->targetY );
	ds->packFloat32( STATE(AvatarBase)->targetR );
	ds->packChar( STATE(AvatarBase)->targetUseRotation );
	ds->packUUID( &STATE(AvatarBase)->targetInitiator );
	ds->packInt32( STATE(AvatarBase)->targetControllerIndex );
	ds->packUUID( &STATE(AvatarBase)->targetThread );

	ds->packFloat32( STATE(AvatarBase)->maxLinear );
	ds->packFloat32( STATE(AvatarBase)->maxRotation );
	ds->packFloat32( STATE(AvatarBase)->minLinear );
	ds->packFloat32( STATE(AvatarBase)->minRotation );

	return AgentBase::writeBackup( ds );
}

int AvatarBase::readBackup( DataStream *ds ) {
	DataStream lds;

	// avatar
	ds->unpackUUID( &STATE(AvatarBase)->avatarExecutiveUUID );
	ds->unpackUUID( &STATE(AvatarBase)->avatarUUID );
	STATE(AvatarBase)->retireTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
	STATE(AvatarBase)->retireMode = ds->unpackChar();
	STATE(AvatarBase)->retireSet = ds->unpackBool();
	STATE(AvatarBase)->sensorTypes = ds->unpackInt32();

	// particle filter
	ds->unpackUUID( &STATE(AvatarBase)->pfId );
	STATE(AvatarBase)->particleFilterInitialized = ds->unpackBool();
	
	memcpy_s( STATE(AvatarBase)->pfInitSigma, sizeof(float)*AVATAR_PFSTATE_SIZE, ds->unpackData( sizeof(float)*AVATAR_PFSTATE_SIZE ), sizeof(float)*AVATAR_PFSTATE_SIZE );
	memcpy_s( STATE(AvatarBase)->pfUpdateSigma, sizeof(float)*AVATAR_PFSTATE_SIZE, ds->unpackData( sizeof(float)*AVATAR_PFSTATE_SIZE ), sizeof(float)*AVATAR_PFSTATE_SIZE );
	
	// position
	STATE(AvatarBase)->posInitialized = ds->unpackBool();
	STATE(AvatarBase)->posX = ds->unpackFloat32();
	STATE(AvatarBase)->posY = ds->unpackFloat32();
	STATE(AvatarBase)->posR = ds->unpackFloat32();
	apb->apb_ftime_s( &STATE(AvatarBase)->posT ); // use current time for now
	
	// map and target
	ds->unpackUUID( &STATE(AvatarBase)->mapId );
	ds->unpackUUID( &STATE(AvatarBase)->missionRegionId );
	ds->unpackUUID( &STATE(AvatarBase)->agentPathPlanner );
	if ( STATE(AvatarBase)->agentPathPlanner != nilUUID )
		STATE(AvatarBase)->agentPathPlannerSpawned = 1; // we have a path planner
	ds->unpackUUID(&STATE(AvatarBase)->agentIndividualLearning);
	if (STATE(AvatarBase)->agentIndividualLearning != nilUUID)
		STATE(AvatarBase)->agentIndividualLearningSpawned = 1; // we have a path planner
	STATE(AvatarBase)->haveTarget = ds->unpackBool();
	STATE(AvatarBase)->targetX = ds->unpackFloat32();
	STATE(AvatarBase)->targetY = ds->unpackFloat32();
	STATE(AvatarBase)->targetR = ds->unpackFloat32();
	STATE(AvatarBase)->targetUseRotation = ds->unpackChar();
	ds->unpackUUID( &STATE(AvatarBase)->targetInitiator );
	STATE(AvatarBase)->targetControllerIndex = ds->unpackInt32();
	ds->unpackUUID( &STATE(AvatarBase)->targetThread );

	STATE(AvatarBase)->maxLinear = ds->unpackFloat32();
	STATE(AvatarBase)->maxRotation = ds->unpackFloat32();
	STATE(AvatarBase)->minLinear = ds->unpackFloat32();
	STATE(AvatarBase)->minRotation = ds->unpackFloat32();

	if ( STATE(AvatarBase)->particleFilterInitialized ) { // particle filter is up, so get our most recent pos from there
		// we have tasks to take care of before we can resume
		apb->apbUuidCreate( &this->AvatarBase_recoveryLock );
		this->recoveryLocks.push_back( this->AvatarBase_recoveryLock );

		// get pf info
		_timeb tb; // dummy
		UUID thread = this->conversationInitiate( AvatarBase_CBR_convPFInfo, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( &STATE(AvatarBase)->pfId );
		lds.packInt32( DDBPFINFO_USECURRENTTIME | DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE | DDBPFINFO_MEAN );
		lds.packData( &tb, sizeof(_timeb) );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
		lds.unlock();
	}
	
	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

#ifdef AVATARBASE_DLL

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarBase *agent = (AvatarBase *)vpAgent;

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
	AvatarBase *agent = new AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarBase *agent = new AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

#endif