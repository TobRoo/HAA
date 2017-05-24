// ExecutiveMission.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "ExecutiveMission.h"
#include "ExecutiveMissionVersion.h"

#include "..\\SupervisorSLAM\\SupervisorSLAMVersion.h"
#include "..\\SupervisorExplore\\SupervisorExploreVersion.h"
#include "..\\SupervisorForage\\SupervisorForageVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
#include "..\\AvatarSurveyor\\AvatarSurveyorVersion.h"
#include "..\\AvatarPioneer\\AvatarPioneerVersion.h"
#include "..\\AvatarX80H\\AvatarX80HVersion.h"
#include "..\\AvatarSimulation\\AvatarSimulationVersion.h"
#include "..\\AgentTeamLearning\AgentTeamLearningVersion.h"

//#include "..\\autonomic\\fImage.h"	//For mapReveal


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// ExecutiveMission

//-----------------------------------------------------------------------------
// Constructor	
ExecutiveMission::ExecutiveMission( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {

	// allocate state
	ALLOCATE_STATE( ExecutiveMission, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "ExecutiveMission" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(ExecutiveMission_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( ExecutiveMission_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(ExecutiveMission)->agentSuperSLAM );
	UuidCreateNil( &STATE(ExecutiveMission)->agentSuperExplore );	

	STATE(ExecutiveMission)->SLAMmode = SLAM_MODE_JCSLAM;

	// Prepare callbacks
	this->callback[ExecutiveMission_CBR_convRequestSupervisorSLAM] = NEW_MEMBER_CB(ExecutiveMission,convRequestSupervisorSLAM);
	this->callback[ExecutiveMission_CBR_convRequestSupervisorExplore] = NEW_MEMBER_CB(ExecutiveMission,convRequestSupervisorExplore);
	this->callback[ExecutiveMission_CBR_convRequestSupervisorForage] = NEW_MEMBER_CB(ExecutiveMission,convRequestSupervisorForage);
	this->callback[ExecutiveMission_CBR_convLandmarkInfo] = NEW_MEMBER_CB(ExecutiveMission,convLandmarkInfo);
	this->callback[ExecutiveMission_CBR_convGetAvatarList] = NEW_MEMBER_CB(ExecutiveMission,convGetAvatarList);
	this->callback[ExecutiveMission_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(ExecutiveMission,convGetAvatarInfo);
	this->callback[ExecutiveMission_CBR_convGetAvatarPos] = NEW_MEMBER_CB(ExecutiveMission,convGetAvatarPos);
	this->callback[ExecutiveMission_CBR_convReachedPoint] = NEW_MEMBER_CB(ExecutiveMission,convReachedPoint);
	this->callback[ExecutiveMission_CBR_convAgentInfo] = NEW_MEMBER_CB(ExecutiveMission,convAgentInfo);
	this->callback[ExecutiveMission_CBR_cbAllocateAvatars] = NEW_MEMBER_CB(ExecutiveMission,cbAllocateAvatars);
	this->callback[ExecutiveMission_CBR_convGetTaskList] = NEW_MEMBER_CB(ExecutiveMission, convGetTaskList);
	this->callback[ExecutiveMission_CBR_convGetTaskInfo] = NEW_MEMBER_CB(ExecutiveMission, convGetTaskInfo);

}

//-----------------------------------------------------------------------------
// Destructor
ExecutiveMission::~ExecutiveMission() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int ExecutiveMission::configure() {

	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\ExecutiveMission %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );



		Log.log( 0, "ExecutiveMission %.2d.%.2d.%.5d.%.2d", ExecutiveMission_MAJOR, ExecutiveMission_MINOR, ExecutiveMission_BUILDNO, ExecutiveMission_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
#ifdef	NO_LOGGING
	Log.setLogMode(LOG_MODE_OFF);
	Log.setLogLevel(LOG_LEVEL_NONE);
#endif	
	return 0;
}


int ExecutiveMission::parseMF_HandleMissionRegion( DDBRegion *region ) { 
	DataStream lds;
	Log.log(0, "ExecutiveMission::parseMF_HandleMissionRegion start");
	// add mission region
	apb->apbUuidCreate( &STATE(ExecutiveMission)->missionRegion );	

	lds.reset();
	lds.packUUID( &STATE(ExecutiveMission)->missionRegion );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();
	Log.log(0, "ExecutiveMission::parseMF_HandleMissionRegion completed");
	return 0; 
}

int ExecutiveMission::parseMF_HandleForbiddenRegion( DDBRegion *region ) { 
	DataStream lds;

	// add mission region
	UUID id;
	apb->apbUuidCreate( &id );	

	this->forbiddenRegions.push_back( id );

	lds.reset();
	lds.packUUID( &id );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();

	return 0; 
}

int ExecutiveMission::parseMF_HandleCollectionRegion( DDBRegion *region ) { 
	DataStream lds;
	UUID id;

	// add collection region
	apb->apbUuidCreate( &id );	

	this->collectionRegions[id] = *region;

	lds.reset();
	lds.packUUID( &id );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();

	return 0; 
}

int ExecutiveMission::parseMF_HandleTravelTarget( float x, float y, float r, bool useRotation ) {

	STATE(ExecutiveMission)->travelTarget[0] = x;
	STATE(ExecutiveMission)->travelTarget[1] = y;
	STATE(ExecutiveMission)->travelTarget[2] = r;
	
	STATE(ExecutiveMission)->travelTargetUseRotation = useRotation;

	return 0;
}

int ExecutiveMission::parseMF_HandleLandmarkFile( char *fileName ) { 
	Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::parseMF_HandleLandmarkFile:DEBUG start");
	this->parseLandmarkFile( fileName );

	return 0; 
}

int ExecutiveMission::parseMF_HandleMapOptions(bool mapReveal, bool mapRandom) {
	Log.log(0, "ExecutiveMission::parseMF_HandleMapOptions running");
	this->mapReveal = mapReveal;
	this->mapRandom = mapRandom;
	Log.log(0, "ExecutiveMission::parseMF_HandleMapOptions: mapReveal is %d, mapRandom is %d", this->mapReveal, this->mapRandom);
	return 0;
}

int ExecutiveMission::parseMF_HandleLearning(bool individualLearning, bool teamLearning)
{
	this->individualLearning = individualLearning;
	this->teamLearning = teamLearning;
	Log.log(0, "ExecutiveMission::parseMF_HandleLearning: individualLearning is %d, teamLearning is %d", this->individualLearning, this->teamLearning);
	return 0;
}

int ExecutiveMission::parseMF_HandleTeamLearning(bool teamLearning)
{
	this->teamLearning = teamLearning;
	return 0;
}

int ExecutiveMission::doMapReveal() {
	Log.log(0, "ExecutiveMission::doMapReveal started");
	DataStream lds;

	char fileBuf[1024];

		//Reload stored map  into DDB
		lds.reset();
		lds.packUUID(&STATE(ExecutiveMission)->pogUUID);
		lds.packFloat32(0.0f);
		lds.packFloat32(0.0f);
		lds.packFloat32(10.0f);
		lds.packFloat32(10.0f);
	//	lds.packString("data\\\randomLayoutFile.ini.");
		sprintf_s(fileBuf, "%s\\randomMap\\randomLayoutFile.ini.", logDirectory);
		lds.packString(fileBuf);

		this->sendMessage( this->hostCon, MSG__DDB_POGLOADREGION, lds.stream(), lds.length() );
		lds.unlock();
		Log.log(0, "ExecutiveMission::doMapReveal load complete");
	return 0;
}




//-----------------------------------------------------------------------------
// Start

int ExecutiveMission::start( char *missionFile ) {
	DataStream lds;
	UUID thread;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;


	if (!this->mapReveal)	//Only explore if map is not revealed, otherwise forage
		STATE(ExecutiveMission)->missionPhase = TASK_EXPLORE;
	else
		STATE(ExecutiveMission)->missionPhase = TASK_FORAGE;


	// watch regions;
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_REGION );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();

	// add map
	apb->apbUuidCreate( &STATE(ExecutiveMission)->pogUUID );

	lds.reset();
	lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
	lds.packFloat32( 10.0f );
	lds.packFloat32( 0.1f );
	Log.log(0, "ExecutiveMission adding POG");
	this->sendMessage( this->hostCon, MSG_DDB_ADDPOG, lds.stream(), lds.length() );
	lds.unlock();

	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();

	// TEMP preload map
	lds.reset();
	lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
	lds.packFloat32( -8.0f );
	lds.packFloat32( -6.0f );
	lds.packFloat32( 20.0f );
	lds.packFloat32( 20.0f );
	lds.packString( "data\\maps\\layout1200x200.txt" );
	//this->sendMessage( this->hostCon, MSG__DDB_POGLOADREGION, lds.stream(), lds.length() );
	lds.unlock();
	
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
	this->sendMessage( this->hostCon, MSG_DDB_STOP_WATCHING_ITEM, lds.stream(), lds.length() );
	lds.unlock();

	// TEMP test dump map
	lds.reset();
	lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
	//lds.packFloat32( -0.1f );
	//lds.packFloat32( -0.1f );
	//lds.packFloat32( 4.0f );
	//lds.packFloat32( 5.4f );
	lds.packFloat32(0.0f);
	lds.packFloat32(0.0f);
	lds.packFloat32(4.0f);
	lds.packFloat32(5.4f);
	lds.packString( "data\\maps\\dump.txt" );
	this->sendMessage( this->hostCon, MSG__DDB_POGDUMPREGION, lds.stream(), lds.length() );
	lds.unlock();






	STATE(ExecutiveMission)->missionStatus = 0;
	STATE(ExecutiveMission)->waitingForAgents = 0;

	// register as landmark watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_LANDMARK );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(ExecutiveMission)->SLAMmode != SLAM_MODE_DELAY ) { // either JC-SLAM or Discard
		// request a SLAM Supervisor
		UUID sSLAMuuid;
		UuidFromString( (RPC_WSTR)_T(SupervisorSLAM_UUID), &sSLAMuuid );
		thread = this->conversationInitiate( ExecutiveMission_CBR_convRequestSupervisorSLAM, REQUESTAGENTSPAWN_TIMEOUT, &sSLAMuuid, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packUUID( &sSLAMuuid );
		lds.packChar( -1 ); // no instance parameters
		lds.packFloat32( 0 ); // affinity
		lds.packChar( DDBAGENT_PRIORITY_CRITICAL );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length() );
		lds.unlock();

		STATE(ExecutiveMission)->waitingForAgents++;
	}

	// request an Explore Supervisor
	UUID sExploreuuid;
	UuidFromString( (RPC_WSTR)_T(SupervisorExplore_UUID), &sExploreuuid );
	thread = this->conversationInitiate( ExecutiveMission_CBR_convRequestSupervisorExplore, REQUESTAGENTSPAWN_TIMEOUT, &sExploreuuid, sizeof(UUID) );
	if ( thread == nilUUID ) {
		return 1;
	}
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( &sExploreuuid );
	lds.packChar( -1 ); // no instance parameters
	lds.packFloat32( 0 ); // affinity
	lds.packChar( DDBAGENT_PRIORITY_CRITICAL );
	lds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length() );
	lds.unlock();
	STATE(ExecutiveMission)->waitingForAgents++;

	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars when DDBE_WATCH_TYPE notficitation is received


	if (this->teamLearning || this->individualLearning)	//If learning is enabled, watch tasks for scenario completion
	{
		// register as task watcher
		lds.reset();
		lds.packUUID(&STATE(AgentBase)->uuid);
		lds.packInt32(DDB_TASK);
		this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
		lds.unlock();
	}



	STATE(AgentBase)->started = true;

	// reregister any avatars we already know about
	std::map<UUID,UUID,UUIDless>::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ )
		this->registerAvatar( (UUID *)&iA->first, &iA->second, &this->avatarTypes[iA->first], this->avatarSensors[iA->first], true );

	this->backup(); // waitingForAgents

	if (this->mapReveal) {
		doMapReveal();
	}


	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int ExecutiveMission::stop() {
	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int ExecutiveMission::step() {

	if ( !STATE(AgentBase)->started )
		return AgentBase::step();

	if ( STATE(ExecutiveMission)->waitingForAgents ) {
		Log.log( 0, "ExecutiveMission::step: waiting for agents %d", STATE(ExecutiveMission)->waitingForAgents );
	} else if ( !STATE(ExecutiveMission)->missionStatus ) {
		DataStream lds;
		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &STATE(ExecutiveMission)->agentSuperExplore );
		lds.unlock();
		STATE(ExecutiveMission)->missionStatus = 1;
		Log.log( 0, "ExecutiveMission::step: mission started!" );
		this->allocateAvatars();
		this->backup(); // missionStatus
	} else if ( STATE(ExecutiveMission)->missionStatus == 1 ) {
		Log.log( 0, "ExecutiveMission::step: mission running!" );
	} else {
		Log.log( 0, "ExecutiveMission::step: mission done!" );
	}

	return AgentBase::step();
}

int ExecutiveMission::parseMF_HandleOptions( int SLAMmode ) {
	STATE(ExecutiveMission)->SLAMmode = SLAMmode;

	return AgentBase::parseMF_HandleOptions( SLAMmode );
}

int ExecutiveMission::parseLandmarkFile( char *filename ) {
	FILE *landmarkF;
	char name[256];
	char *ch;
	int id;
	UUID uuid;
	float x, y, height, elevation;
	int estimatedPos;
	ITEM_TYPES landmarkType;

	//UUID nilId;
	//UuidCreateNil( &nilId );

	int err = 0;

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveMission::parseLandmarkFile: parsing file %s", filename );

	// open file
	if ( fopen_s( &landmarkF, filename, "r" ) ) {
		Log.log( 0, "ExecutiveMission::parseLandmarkFile: failed to open %s", filename );
		return 1;
	}

	while (1) {
		ch = name;
		while ( EOF != (*ch = fgetc( landmarkF )) ) {
			if ( *ch == '\n' ) {
				*ch = 0;
				break;
			}
			ch++;
		}
		if ( *ch == EOF )
			break; // out of landmarks
		if ( 1 != fscanf_s( landmarkF, "id=%d\n", &id ) ) {
			Log.log( 0, "ExecutiveMission::parseLandmarkFile: expected id=<int>, check format (landmark %s)", name );
			err = 1;
			break;
		}
		if ( 5 != fscanf_s( landmarkF, "pose=%f %f %f %f %d\n", &x, &y, &height, &elevation, &estimatedPos ) ) {
			Log.log( 0, "ExecutiveMission::parseLandmarkFile: expected pose=<float> <float> <float> <float> <int>, check format (landmark %s)", name );
			err = 1;
			break;
		}
		if (1 != fscanf_s(landmarkF, "landmark_type=%d\n", &landmarkType)) {
			Log.log(0, "ExecutiveMission::parseLandmarkFile: expected landmark_type=<int>, check format (landmark %s)", name);
			Log.log(0, "ExecutiveMission::parseLandmarkFile: setting default value = 0 (NON_COLLECTABLE)");
			landmarkType = NON_COLLECTABLE;
			//break;
		}


		// create uuid
		apb->apbUuidCreate( &uuid );

		// add to DDB
		this->ds.reset();
		this->ds.packUUID( &uuid );
		this->ds.packUChar( (unsigned char)id );
		this->ds.packUUID( &nilUUID );
		this->ds.packFloat32( height ); 
		this->ds.packFloat32( elevation ); 
		this->ds.packFloat32( x ); 
		this->ds.packFloat32( y ); 
		this->ds.packChar( estimatedPos );
		this->ds.packInt32(landmarkType);
		this->sendMessage( this->hostCon, MSG_DDB_ADDLANDMARK, this->ds.stream(), this->ds.length() );
		this->ds.unlock();


		if (id >= FORAGE_LANDMARK_ID_LOW && id <= FORAGE_LANDMARK_ID_HIGH) {
			Log.log(0, "ExecutiveMission::parseLandmarkFile: adding task at %f %f for team learning agents", x, y);
			DataStream lds;
			UUID taskUUID;
			apb->apbUuidCreate(&taskUUID);
			lds.reset();
			lds.packUUID(&taskUUID);
			lds.packUUID(&uuid);
			lds.packUUID(&nilUUID);		//No agent assigned at creation, assign nilUUID
			lds.packUUID(&nilUUID);		//No avatar assigned at creation, assign nilUUID
			lds.packBool(false);					//Task not completed at creation
			lds.packInt32(landmarkType);
			this->sendMessage(this->hostCon, MSG_DDB_ADDTASK, lds.stream(), lds.length());
			lds.unlock();

		}

		Log.log( LOG_LEVEL_NORMAL, "ExecutiveMission::parseLandmarkFile: added landmark %d (%s): %f %f %f %f %d", id, name, x, y, height, elevation, estimatedPos );
	}

	fclose( landmarkF );

	return err;
}

int ExecutiveMission::registerAvatar( UUID *id, UUID *avatarId, AgentType *agentType, int sensorTypes, bool force ) {

	if ( !force && this->avatars.find( *id ) != this->avatars.end() )
		return 0; // aready know this avatar

	this->avatars[*id] = *avatarId;
	this->avatarTypes[*id] = *agentType;
	this->avatarSensors[*id] = sensorTypes;

	if ( !STATE(AgentBase)->started )
		return 0; // have to come back later to add

	this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_ADD_MAP), (char *)&STATE(ExecutiveMission)->pogUUID, sizeof(UUID), id );
	this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_ADD_REGION), (char *)&STATE(ExecutiveMission)->missionRegion, sizeof(UUID), id );
	
	// Send the collection region(s)
	DataStream lds;
	lds.reset();
	std::map<UUID, DDBRegion, UUIDless>::iterator iR;
	for (iR = this->collectionRegions.begin(); iR != this->collectionRegions.end(); iR++) {
		lds.packBool(1);
		lds.packUUID((UUID *)&iR->first);
		lds.packData(&iR->second, sizeof(DDBRegion));
	}
	lds.packBool(0);
	this->sendMessageEx(this->hostCon, MSGEX(AvatarBase_MSGS, MSG_ADD_REGION), lds.stream(), lds.length(), id);
	lds.unlock();
	
	this->allocateAvatars(); // update allocation

	return 0;
}

int ExecutiveMission::allocateAvatars() {
	DataStream lds;
	std::map<UUID,UUID,UUIDless>::iterator iA;

	int avatarIdle = 0;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		if ( this->avatarSensors[iA->first] != 0 ) { // they have some sensors
			if ( !this->avatarInfo[iA->second].retired && STATE(ExecutiveMission)->missionPhase == TASK_EXPLORE ) {
				if ( STATE(ExecutiveMission)->agentSuperExplore != nilUUID ) { // we have an Explore supervisor	
					if ( this->avatarInfo[iA->second].priority >= DDBAVATAR_CP_IDLE )
						this->_allocateAvatar( (UUID *)&iA->second, &STATE(ExecutiveMission)->agentSuperExplore, DDBAVATAR_CP_IDLE ); // allocate
				}
			} else if ( !this->avatarInfo[iA->second].retired && STATE(ExecutiveMission)->missionPhase == TASK_CONGREGATE ) {
				if ( this->avatarInfo[iA->second].priority >= DDBAVATAR_CP_IDLE )
					this->_allocateAvatar( (UUID *)&iA->second, this->getUUID(), DDBAVATAR_CP_IDLE ); // allocate
			
			} else if ( STATE(ExecutiveMission)->missionPhase == TASK_NONE ) {
				if ( this->avatarInfo[iA->second].priority >= DDBAVATAR_CP_IDLE || this->avatarInfo[iA->second].retired )
					avatarIdle++;
			}
		} else { // blind traveller
			if ( !this->avatarInfo[iA->second].retired )
				this->_allocateAvatar( (UUID *)&iA->second, this->getUUID(), DDBAVATAR_CP_IDLE ); // allocate
		}
	}

	if ( STATE(ExecutiveMission)->missionStatus == 1 && avatarIdle == this->avatars.size() && this->avatars.size() > 0 && !this->teamLearning && !this->individualLearning) { // mission must be over!
		this->missionDone();
	}

	return 0;
}

int ExecutiveMission::_allocateAvatar( UUID *avatar, UUID *controller, int priority ) {
	DataStream lds;

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveMission::_allocateAvatar: allocate avatar %s to %s",
		Log.formatUUID(LOG_LEVEL_NORMAL,avatar), Log.formatUUID(LOG_LEVEL_NORMAL,controller) );

	if ( this->avatarInfo[*avatar].controller != *controller ) {
		// update DDB
		lds.reset();
		lds.packUUID( avatar );
		lds.packInt32( DDBAVATARINFO_CONTROLLER );
		lds.packUUID( controller );
		lds.packInt32( priority );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
		lds.unlock();
	}
	
	return 0;
}

int ExecutiveMission::avatarResourceRequest( DataStream *ds ) {
	DataStream lds;
	int i;
	UUID agent;
	char mode;
	int count;
	UUID avatarId;
	int pri;
	std::list<UUID> avatarL;
	std::list<int> priorityL;
	UUID thread;

	ds->unpackUUID( &agent );
	mode = ds->unpackChar();
	count = ds->unpackInt32();
	for ( i = 0; i < count; i++ ) {
		ds->unpackUUID( &avatarId );
		pri = ds->unpackInt32();
		avatarL.push_back( avatarId );
		priorityL.push_back( pri );
	}
	ds->unpackUUID( &thread );

	switch ( mode ) {
	case ExecutiveMission_Defs::RA_ONE:
		{
			std::list<UUID>::iterator iA;
			std::list<int>::iterator iP;
			for ( iA = avatarL.begin(), iP = priorityL.begin(); iA != avatarL.end(); iA++, iP++ ) {
				if ( this->avatarInfo.find( *iA ) == this->avatarInfo.end() ) 
					continue; // not found

				if ( this->avatarInfo[*iA].priority > *iP )
					break; // avatar is available
			}
			
			if ( iA == avatarL.end() ) { // didn't get one
				lds.reset();
				lds.packUUID( &thread );
				lds.packChar( DDBR_ABORT );
				this->sendMessage( this->hostCon, MSG_RESPONSE, lds.stream(), lds.length(), &agent );
#ifdef LOG_RESPONSES
				Log.log(LOG_LEVEL_NORMAL, "RESPONSE: Sending message from agent %s to agent %s in conversation %s", Log.formatUUID(0, this->getUUID()), Log.formatUUID(0, &agent), Log.formatUUID(0, &thread));
#endif

				lds.unlock();
			} else { // got one
				std::map<UUID,UUID,UUIDless>::iterator iAA;
				for ( iAA = this->avatars.begin(); iAA != this->avatars.end(); iAA++ ) {
					if ( iAA->second == *iA )
						break;
				}

				if ( iAA == this->avatars.end() ) {
					return 1; // what happened?
				}

				this->_allocateAvatar( (UUID *)&iAA->second, &agent, *iP );

				lds.reset();
				lds.packUUID( &thread );
				lds.packChar( DDBR_OK );
				this->sendMessage( this->hostCon, MSG_RESPONSE, lds.stream(), lds.length(), &agent );
#ifdef LOG_RESPONSES
				Log.log(LOG_LEVEL_NORMAL, "RESPONSE: Sending message from agent %s to agent %s in conversation %s", Log.formatUUID(0, this->getUUID()), Log.formatUUID(0, &agent), Log.formatUUID(0, &thread));
#endif
				lds.unlock();
			}
			
		}
		break;
	default:
		Log.log( 0, "ExecutiveMission::avatarResourceRequest: unhandled mode %d", mode );
	};

	return 0;
}

int ExecutiveMission::avatarResourceRelease( UUID *agent, UUID *avatar ) {
	DataStream lds;

	Log.log( 0, "ExecutiveMission::avatarResourceRelease: releasing avatar %s", Log.formatUUID(0,avatar) );
	
	// update DDB
	lds.reset();
	lds.packUUID( avatar );
	lds.packInt32( DDBAVATARINFO_CONTROLLER_RELEASE );
	lds.packUUID( agent );
	this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
	lds.unlock();

	if ( *agent == STATE(ExecutiveMission)->agentSuperExplore ) { // exploration must be finished!
		if ( STATE(ExecutiveMission)->missionPhase == TASK_EXPLORE )
			STATE(ExecutiveMission)->missionPhase = TASK_NONE;
	}

	this->allocateAvatars();

	return 0;
}

int ExecutiveMission::avatarUpdateController( UUID *avatarId, UUID *controller, int index, int priority ) {

	if ( this->avatarInfo.find( *avatarId ) == this->avatarInfo.end() ) { // don't know them yet
		this->avatarInfo[*avatarId].owner = nilUUID;
		this->avatarInfo[*avatarId].controller = nilUUID;
		this->avatarInfo[*avatarId].controllerIndex = 0;
		this->avatarInfo[*avatarId].priority = DDBAVATAR_CP_UNSET;
		this->avatarInfo[*avatarId].congregated = false;
		this->avatarInfo[*avatarId].retired = false;
	}

	Log.log( LOG_LEVEL_VERBOSE, "ExecutiveMission::avatarUpdateController: recieved avatar (%s) controller %s, old controller %s",
		Log.formatUUID(LOG_LEVEL_VERBOSE, avatarId), Log.formatUUID(LOG_LEVEL_VERBOSE, controller), Log.formatUUID(LOG_LEVEL_VERBOSE, &this->avatarInfo[*avatarId].controller) );

	this->avatarInfo[*avatarId].controller = *controller;
	this->avatarInfo[*avatarId].controllerIndex = index;
	this->avatarInfo[*avatarId].priority = priority;
	
	if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller == *this->getUUID() && this->assignedAvatars.find(this->avatarInfo[*avatarId].owner) == this->assignedAvatars.end() ) {
		// is now ours, add avatar
		this->addAvatarAllocation( &this->avatarInfo[*avatarId].owner, avatarId );
	} else if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller != *this->getUUID() && this->assignedAvatars.find(this->avatarInfo[*avatarId].owner) != this->assignedAvatars.end() ) {
		// used to be ours, remove avatar
		this->remAvatarAllocation( &this->avatarInfo[*avatarId].owner );
	}

	return 0;
}

int ExecutiveMission::addAvatarAllocation( UUID *id, UUID *avatarId ) {
	DataStream lds;

	this->assignedAvatars[*id] = 1;

	// register as agent watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( id );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

	if ( STATE(ExecutiveMission)->missionPhase == TASK_CONGREGATE ) {
		this->taskCongregateUpdateTargets();
	} else { // must be blind traveller
		this->taskBlindTravellerStart( id );
	}

	return 0;
}

int ExecutiveMission::remAvatarAllocation( UUID *id ) {

	this->assignedAvatars.erase( *id );

	return 0;
}

int ExecutiveMission::missionDone() {
	DataStream lds;

	Log.log( 0, "ExecutiveMission::missionDone: mission complete!" );
	STATE(ExecutiveMission)->missionStatus = 2; // done

	// notify host
	lds.reset();
	lds.packChar( 1 ); // success
	this->sendMessage( this->hostCon, MSG_MISSION_DONE, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int ExecutiveMission::taskCongregateStart( DDBLandmark *lm ) {

	Log.log( 0, "ExecutiveMission::taskCongregateStart: congregate at %f %f", lm->x, lm->y );

	STATE(ExecutiveMission)->missionPhase = TASK_CONGREGATE;
	this->backup(); // missionPhase

	this->allocateAvatars();

	this->taskCongregateUpdateTargets();

	return 0;
}

int ExecutiveMission::taskCongregateUpdateTargets() {
	DataStream lds;
	UUID thread;

	int i;
	float sn, cs;
	float x, y, r;
	DDBLandmark lm;

	// find congregate landmark
	std::map<UUID,DDBLandmark,UUIDless>::iterator iL;
	for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
		if ( iL->second.code == CONGREGATE_LANDMARK_ID )
			break;
	}
	lm = iL->second;

	// number of avatars
	int avCount = (int)this->avatarInfo.size();

	// set target of all avatars allocated to us
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
	std::map<UUID,char,UUIDless>::iterator iA;
	for ( iAI = this->avatarInfo.begin(), i = 0; iAI != this->avatarInfo.end(); iAI++, i++ ) {
		if ( (iA = this->assignedAvatars.find( iAI->second.owner )) == this->assignedAvatars.end() ) // not currently controlling them
			continue;

		r = i*2*fM_PI/avCount;
		sn = sin(r);
		cs = cos(r);
		x = lm.x + 0.5f*cs;
		y = lm.y + 0.5f*sn;

		Log.log( 0, "ExecutiveMission::taskCongregateUpdateTargets: assigning target %f %f %f to %s", x, y, r, Log.formatUUID(0,(UUID *)&iA->first) );

		thread = this->conversationInitiate( ExecutiveMission_CBR_convReachedPoint, -1, (void *)&iA->first, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packFloat32( x );
		lds.packFloat32( y );
		lds.packFloat32( r );
		lds.packChar( 1 );
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packInt32( iAI->second.controllerIndex );
		lds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), lds.stream(), lds.length(), (UUID *)&iA->first );
		lds.unlock();

		iA++;
	}

	return 0;
}

int ExecutiveMission::taskCongregateCheckCompletion() {

	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
	for ( iAI = this->avatarInfo.begin(); iAI != this->avatarInfo.end(); iAI++ ) {
		if ( !iAI->second.retired && iAI->second.congregated == false )
			break;
	}

	if ( iAI == this->avatarInfo.end() ) {
		STATE(ExecutiveMission)->missionPhase = TASK_NONE;

		this->missionDone();
	}

	return 0;
}

int ExecutiveMission::taskBlindTravellerStart( UUID *id ) {
	DataStream lds;

	
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
	for ( iAI = this->avatarInfo.begin(); iAI != this->avatarInfo.end(); iAI++ ) {
		if ( iAI->second.owner == *id )
			break;
	}
	if ( iAI == this->avatarInfo.end() ) {
		Log.log( 0, "ExecutiveMission::taskBlindTravellerStart: avatar info not found" );
		return 1;
	}

	STATE(ExecutiveMission)->travelTargetSet = true;

	float x, y, r;
	x = STATE(ExecutiveMission)->travelTarget[0];
	y = STATE(ExecutiveMission)->travelTarget[1];
	r = STATE(ExecutiveMission)->travelTarget[2];
	Log.log( 0, "ExecutiveMission::taskBlindTravellerStart: assigning target %f %f %f (useRotation %d) to %s", x, y, r, STATE(ExecutiveMission)->travelTargetUseRotation, Log.formatUUID(0,id) );

	UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convReachedPoint, -1, (void *)id, sizeof(UUID) );
	if ( thread == nilUUID ) {
		return 1;
	}
	lds.reset();
	lds.packFloat32( x );
	lds.packFloat32( y );
	lds.packFloat32( r );
	lds.packChar( (char)STATE(ExecutiveMission)->travelTargetUseRotation );
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( iAI->second.controllerIndex );
	lds.packUUID( &thread );
	this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), lds.stream(), lds.length(), (UUID *)id );
	lds.unlock();

	return 0;
}

int ExecutiveMission::taskBlindTravellerWander( UUID *id ) {
	DataStream lds;

	STATE(ExecutiveMission)->travelTargetSet = false;

	// figure out pfId
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iA;
	for ( iA = this->avatarInfo.begin(); iA != this->avatarInfo.end(); iA++ ) {
		if ( iA->second.owner == *id )
			break;
	}

	if ( iA == this->avatarInfo.end() ) {
		Log.log( 0, "ExecutiveMission::taskBlindTravellerWander: pfId not found" );
		this->taskBlindTravellerStart( id );
		return 0;
	}

	Log.log( 0, "ExecutiveMission::taskBlindTravellerWander: requesting pf data (%s)", Log.formatUUID(0,&iA->second.pfId) );
	// request pf data
	UUID thread;
	_timeb tb;
	thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT, (void *)id, sizeof(UUID) );
	if ( thread == nilUUID ) {
		return 1;
	}
	lds.reset();
	lds.packUUID( &iA->second.pfId ); 
	lds.packInt32( DDBPFINFO_CURRENT );
	lds.packData( &tb, sizeof(_timeb) ); // dummy
	lds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}
	
int ExecutiveMission::taskBlindTravellerFinish() {

	Log.log( 0, "ExecutiveMission::taskBlindTravellerFinish: reached target!" );

	STATE(ExecutiveMission)->missionPhase = TASK_NONE;

	this->missionDone();

	return 0;
}

int ExecutiveMission::taskForageStart(UUID *id, DDBLandmark *lm) {
	DataStream lds;

	if (!this->teamLearning) {	//Only spawn forage supervisors if team learning is not enabled
	Log.log(0, "ExecutiveMission::taskForageStart: retrieve landmark at %f %f", lm->x, lm->y);

	// request SupervisorForage
	UUID sForageuuid;
	UuidFromString((RPC_WSTR)_T(SupervisorForage_UUID), &sForageuuid);
	UUID thread = this->conversationInitiate(ExecutiveMission_CBR_convRequestSupervisorForage, REQUESTAGENTSPAWN_TIMEOUT, id, sizeof(UUID));
	if (thread == nilUUID) {
		return 1;
	}
	lds.reset();
	lds.packUUID(this->getUUID());
	lds.packUUID(&sForageuuid);
	lds.packChar(-1); // no instance parameters
	lds.packFloat32(0); // affinity
	lds.packChar(DDBAGENT_PRIORITY_CRITICAL);
	lds.packUUID(&thread);
	this->sendMessage(this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length());
	lds.unlock();
	}
	else {			//Add task to DDB, to be discovered by team learning agents
		//Log.log(0, "ExecutiveMission::taskForageStart: adding task at %f %f for team learning agents", lm->x, lm->y);
		//UUID *taskUUID;
		//UUID *nilUUIDPointer;
		//*nilUUIDPointer = nilUUID;
		//apb->apbUuidCreate(taskUUID);
		//lds.reset();
		//lds.packUUID(taskUUID);
		//lds.packUUID(id);
		//lds.packUUID(nilUUIDPointer);		//No avatar assigned at creation, assign nilUUID
		//lds.packBool(0);					//Task not completed at creation
		//lds.packInt32(lm->landmarkType);
		//this->sendMessage(this->hostCon, MSG_DDB_ADDTASK, lds.stream(), lds.length());
		//lds.unlock();
	}

	return 0;
}


int ExecutiveMission::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

	int type;
	char evt;
	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	if ( evt == DDBE_WATCH_TYPE ) {
		if ( type == DDB_AVATAR ) {
			// request list of avatars
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
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
		if (type == DDB_TASK) {
			// request list of tasks
			UUID thread = this->conversationInitiate(ExecutiveMission_CBR_convGetTaskList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(this->getUUID()); // dummy id, getting the full list of tasks anyway
			sds.packUUID(&thread);
			sds.packBool(true);			   //true == send list of tasks, otherwise only info about a specific task
			this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}

	} else if ( evt == DDBE_WATCH_ITEM ) {
		if ( this->assignedAvatars.find(uuid) != this->assignedAvatars.end() ) { // one of our avatar agents
			// get status
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_LANDMARK ) { 
		if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBLANDMARKINFO_POS ) {
				std::map<UUID,DDBLandmark,UUIDless>::iterator iL = this->landmarks.find( uuid );
				if ( iL == this->landmarks.end() || iL->second.code == CONGREGATE_LANDMARK_ID ) { // need updated info
					// get landmark
					UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
					sds.reset();
					sds.packUUID( &uuid );
					sds.packUUID( &thread );
					sds.packBool(false);
					this->sendMessage( this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length() );
					sds.unlock();
				}
			}
		}
	} else if ( type == DDB_AGENT ) {
		if ( this->assignedAvatars.find(uuid) != this->assignedAvatars.end() ) { // our agent
			if ( evt == DDBE_AGENT_UPDATE ) {
				int infoFlags = lds.unpackInt32();
				if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
					
					// get status
					UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
					sds.reset();
					sds.packUUID( &uuid );
					sds.packInt32( DDBAGENTINFO_RSTATUS );
					sds.packUUID( &thread );
					this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
					sds.unlock();
				}
			}
		} else { // must be holdout from an earlier crash
			// stop watching agent
			sds.reset();
			sds.packUUID( &STATE(AgentBase)->uuid );
			sds.packUUID( &uuid );
			this->sendMessage( this->hostCon, MSG_DDB_STOP_WATCHING_ITEM, sds.stream(), sds.length() );
			sds.unlock();
		}
	} else if ( type == DDB_AVATAR ) {
		if ( evt == DDBE_ADD ) {
			// get info
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &uuid ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( evt == DDBE_REM ) {
			this->avatarUpdateController( &uuid, &nilUUID, 0, DDBAVATAR_CP_UNSET );
		} else if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAVATARINFO_CONTROLLER ) {
				// get info
				UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				if ( thread == nilUUID ) {
					return 1;
				}
				sds.reset();
				sds.packUUID( &uuid ); 
				sds.packInt32( DDBAVATARINFO_RCONTROLLER );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
			if ( infoFlags & DDBAVATARINFO_RETIRE ) {
				Log.log( 0, "ExecutiveMission::ddbNotification: avatar retired %s", Log.formatUUID(0,&uuid) );
				this->_allocateAvatar( &uuid, &nilUUID, DDBAVATAR_CP_HIGH ); // nobody can use this avatar anymore
				this->avatarInfo[uuid].retired = true;
			}
		}
	}
	else if (type == DDB_TASK) {
		Log.log(0, "ExecutiveMission::ddbNotification: DDB_TASK");
		// request task info
		UUID thread = this->conversationInitiate(ExecutiveMission_CBR_convGetTaskInfo, DDB_REQUEST_TIMEOUT);
		if (thread == nilUUID) {
			return 1;
		}
		sds.reset();
		sds.packUUID(&uuid); // dummy id, getting the full list of tasks anyway
		sds.packUUID(&thread);
		sds.packBool(false);			   //true == send list of tasks, otherwise only info about a specific task
		this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
		sds.unlock();
	}



	lds.unlock();
	
	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int ExecutiveMission::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case MSG_AGENT_GREET: // expected custom data: [UUID avatarId, int sensorTypes]
		{
			UUID avatarId;
			AgentType agentType;
			int sensorTypes;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			agentType = *(AgentType *)lds.unpackData( sizeof(AgentType) );
			// custom data
			lds.unpackUUID( &avatarId );
			sensorTypes = lds.unpackInt32();
			lds.unlock();
			this->registerAvatar( &uuid, &avatarId, &agentType, sensorTypes );
		}
		break;
	case ExecutiveMission_MSGS::MSG_REQUEST_AVATAR:
		lds.setData( data, len );
		this->avatarResourceRequest( &lds );
		lds.unlock();
		break;
	case ExecutiveMission_MSGS::MSG_RELEASE_AVATAR:
		{
			UUID agent;
			lds.setData( data, len );
			lds.unpackUUID( &agent );
			lds.unpackUUID( &uuid );
			this->avatarResourceRelease( &agent, &uuid );
			lds.unlock();
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool ExecutiveMission::convRequestSupervisorSLAM( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "ExecutiveMission::convRequestSupervisorSLAM: request spawn timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackBool() ) { // succeeded
		lds.unpackUUID( &STATE(ExecutiveMission)->agentSuperSLAM );
		lds.unlock();
		STATE(ExecutiveMission)->waitingForAgents--;

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &STATE(ExecutiveMission)->agentSuperSLAM );

		lds.reset();
		lds.packUUID( &STATE(ExecutiveMission)->pogUUID );
		this->sendMessageEx( this->hostCon, MSGEX(SupervisorSLAM_MSGS,MSG_ADD_MAP), lds.stream(), lds.length(), &STATE(ExecutiveMission)->agentSuperSLAM );
		lds.unlock();

		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &STATE(ExecutiveMission)->agentSuperSLAM );
		lds.unlock();

		this->backup(); // agentSuperSLAM
	} else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

bool ExecutiveMission::convRequestSupervisorExplore( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "ExecutiveMission::convRequestSupervisorExplore: request spawn timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackBool() ) { // succeeded
		lds.unpackUUID( &STATE(ExecutiveMission)->agentSuperExplore );
		lds.unlock();

		STATE(ExecutiveMission)->waitingForAgents--;

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &STATE(ExecutiveMission)->agentSuperExplore );

		// set region to explore
		lds.reset();
		lds.packUUID( &STATE(ExecutiveMission)->missionRegion );
		lds.packChar( 0 );
		this->sendMessageEx( this->hostCon, MSGEX(SupervisorExplore_MSGS,MSG_ADD_REGION), lds.stream(), lds.length(), &STATE(ExecutiveMission)->agentSuperExplore );
		lds.unlock();

		// set forbidden regions
		std::list<UUID>::iterator iR;
		for ( iR = this->forbiddenRegions.begin(); iR != this->forbiddenRegions.end(); iR++ ) {
			lds.reset();
			lds.packUUID( &*iR );
			lds.packChar( 1 );
			this->sendMessageEx( this->hostCon, MSGEX(SupervisorExplore_MSGS,MSG_ADD_REGION), lds.stream(), lds.length(), &STATE(ExecutiveMission)->agentSuperExplore );
			lds.unlock();
		}

		// set map
		this->sendMessageEx( this->hostCon, MSGEX(SupervisorExplore_MSGS,MSG_ADD_MAP), (char *)&STATE(ExecutiveMission)->pogUUID, sizeof(UUID), &STATE(ExecutiveMission)->agentSuperExplore );

		// set avatar exec
		this->sendMessageEx( this->hostCon, MSGEX(SupervisorExplore_MSGS,MSG_AVATAR_EXEC), (char *)this->getUUID(), sizeof(UUID), &STATE(ExecutiveMission)->agentSuperExplore );

		this->backup(); // agentSuperExplore
	} else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool ExecutiveMission::convRequestSupervisorForage( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "ExecutiveMission::convRequestSupervisorForage: request spawn timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackBool() ) { // succeeded
		UUID agentId, landmarkId;
		lds.unpackUUID( &agentId );
		lds.unlock();

		landmarkId = *(UUID *)conv->data;

		this->agentSuperForage[agentId] = landmarkId;

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &agentId );

		// configure parameters
		lds.reset();
		lds.packUUID( this->getUUID() ); // use us as the avatarExec for now
		lds.packUUID( &landmarkId );
		std::map<UUID,DDBRegion,UUIDless>::iterator iR;
		for ( iR = this->collectionRegions.begin(); iR != this->collectionRegions.end(); iR++ ) {
			lds.packBool( 1 );
			lds.packUUID( (UUID *)&iR->first );
			lds.packData( &iR->second, sizeof(DDBRegion) );
		}
		lds.packBool( 0 );
		this->sendMessageEx( this->hostCon, MSGEX(SupervisorForage_MSGS,MSG_CONFIGURE), lds.stream(), lds.length(), &agentId );
		lds.unlock();

		// start
		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &agentId );
		lds.unlock();

		this->backup(); // agentSuperForage
	} else {
		lds.unlock();

		Log.log( 0, "ExecutiveMission::convRequestSupervisorForage: spawn failed!" );
		
		// TODO try again?
	}

	return 0;
}

bool ExecutiveMission::convLandmarkInfo( void *vpConv ) {
	DataStream lds;
	UUID id;
	spConversation conv = (spConversation)vpConv;
	char response;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "ExecutiveMission::convLandmarkInfo: request timed out" );
		return 0; // end conversation
	}

	id = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		DDBLandmark landmark;

		landmark = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
		lds.unlock();

		if ( (unsigned char)landmark.code == CONGREGATE_LANDMARK_ID ) {
			std::map<UUID,DDBLandmark,UUIDless>::iterator iL = this->landmarks.find( id );
			if ( iL == this->landmarks.end() ) { // don't know them yet
				this->landmarks[id] = landmark;
				this->backup(); // landmarks
				this->taskCongregateStart( &landmark ); // start congregate task
			} else { // we already know them, check if there location has changed significantly
				if ( fabs(iL->second.x - landmark.x) > 0.1f || fabs(iL->second.y - landmark.y) > 0.1f ) {
					this->landmarks[id] = landmark;				
					this->backup(); // landmarks
					this->taskCongregateStart( &landmark ); // restart at new location		
				}
			}
		
		} else if ( (unsigned char)landmark.code >= FORAGE_LANDMARK_ID_LOW && (unsigned char)landmark.code <= FORAGE_LANDMARK_ID_HIGH ) {
			std::map<UUID,DDBLandmark,UUIDless>::iterator iL = this->landmarks.find( id );
			if ( iL == this->landmarks.end() ) { // don't know them yet
				this->landmarks[id] = landmark;
				this->backup(); // landmarks
				this->taskForageStart( &id, &landmark ); // start forage task
			} // else { // we already know them, ignore

		} else {
			this->landmarks[id] = landmark;
			this->backup(); // landmarks
		}

		this->backup(); // landmark
	} else {
		lds.unlock();
	}

	return 0;
}

bool ExecutiveMission::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "ExecutiveMission::convGetAvatarList: timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID avatarId;
		UUID agentId;
		AgentType agentType;
		int i, count;

		if ( lds.unpackInt32() != DDBAVATARINFO_ENUM ) {
			lds.unlock();
			return 0; // what happened here?
		}
		
		count = lds.unpackInt32();
		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveMission::convGetAvatarList: recieved %d avatars", count );

		for ( i=0; i<count; i++ ) {
			lds.unpackUUID( &avatarId );
			lds.unpackString(); // avatar type
			lds.unpackUUID( &agentId );
			lds.unpackUUID( &agentType.uuid );
			agentType.instance = lds.unpackInt32();

			// request further info
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &avatarId ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER );
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

bool ExecutiveMission::convGetAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "ExecutiveMission::convGetAvatarInfo: timed out" );
		return 0; // end conversation
	}

	UUID avId = *(UUID *)conv->data;

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID agent, pfId, controller;
		int index, priority;
		int infoFlags = this->ds.unpackInt32();

		if ( infoFlags == DDBAVATARINFO_RCONTROLLER ) { 
			this->ds.unpackUUID( &controller );
			index = this->ds.unpackInt32();
			priority = this->ds.unpackInt32();
			this->ds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "ExecutiveMission::convGetAvatarInfo: recieved avatar (%s) controller: %s, index %d, priority %d", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&controller), index, priority );

			this->avatarUpdateController( &avId, &controller, index, priority );

			this->allocateAvatars();

		} else if ( infoFlags == (DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER) ) {
			this->ds.unpackUUID( &agent );
			this->ds.unpackUUID( &pfId );
			this->ds.unpackUUID( &controller );
			index = this->ds.unpackInt32();
			priority = this->ds.unpackInt32();
			this->ds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "ExecutiveMission::convGetAvatarInfo: recieved avatar (%s) agent: %s, controller: %s, index %d, prioriy %d", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&controller), index, priority );

			this->avatarInfo[avId].owner = agent;
			this->avatarInfo[avId].pfId = pfId;

			this->avatarUpdateController( &avId, &controller, index, priority );

			this->allocateAvatars();

		} else {
			this->ds.unlock();
			return 0; // what happened here?
		}

	} else {
		this->ds.unlock();

		// TODO try again?
	}

	return 0;
}

bool ExecutiveMission::convGetAvatarPos( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "ExecutiveMission::convGetAvatarPos: timed out" );
		return 0; // end conversation
	}

	UUID uuid = *(UUID *)conv->data;

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		_timeb *tb;
		float pfState[3];

		if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		tb = (_timeb *)this->ds.unpackData( sizeof(_timeb) );
		memcpy( pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );

		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveMission::convGetAvatarPos: recieved avatar pos %s (%f %f)", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&uuid), pfState[0], pfState[1] );

		std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
		for ( iAI = this->avatarInfo.begin(); iAI != this->avatarInfo.end(); iAI++ ) {
			if ( iAI->second.owner == uuid )
				break;
		}
		if ( iAI == this->avatarInfo.end() ) {
			Log.log( 0, "ExecutiveMission::convGetAvatarPos: avatar info not found (%s)", Log.formatUUID(0,&uuid) );
			return 0;
		}

		float x, y, r;
		x = pfState[0] + (float)(apb->apbUniform01()-0.5f)*6; // current +/- 3 m
		y = pfState[1] + (float)(apb->apbUniform01()-0.5f)*6;
		r = 0;
		Log.log( 0, "ExecutiveMission::convGetAvatarPos: assigning target %f %f %f to %s", x, y, r, Log.formatUUID(0,&uuid) );

		UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convReachedPoint, -1, (void *)&uuid, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 0;
		}
		lds.reset();
		lds.packFloat32( x );
		lds.packFloat32( y );
		lds.packFloat32( r );
		lds.packChar( 0 );
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packInt32( iAI->second.controllerIndex );
		lds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), lds.stream(), lds.length(), (UUID *)&uuid );
		lds.unlock();

	} else { // failed, assign travel target again
		this->taskBlindTravellerStart( &uuid );
	}

	return 0;
}

bool ExecutiveMission::convReachedPoint( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "ExecutiveMission::convReachedPoint: timed out" );
		return 0; // end conversation
	}

	UUID id;
	id = *(UUID *)conv->data;

	this->ds.setData( conv->response, conv->responseLen );
	
	UUID thread;
	this->ds.unpackUUID( &thread );
	char success = this->ds.unpackChar();

	int reason;
	if ( !success ) {
		reason = this->ds.unpackInt32();
		this->ds.unlock();
		Log.log(0, "ExecutiveMission::convReachedPoint: avatar thread %s target fail %d", Log.formatUUID( 0, &thread), reason );
		if ( reason == AvatarBase_Defs::TP_NEW_TARGET ) // we gave them a new target, no reason to do anything
			return 0;

		if ( this->avatarSensors[id] != 0 ) { // congregate
			// can't reach target at the moment, hand off to the explore supervisor for a while
			this->_allocateAvatar( &this->avatars[id], &STATE(ExecutiveMission)->agentSuperExplore, DDBAVATAR_CP_IDLE ); // allocate

			this->addTimeout( 30000, ExecutiveMission_CBR_cbAllocateAvatars ); // re allocate after a while to try again
		} else { // blind traveller
			if ( STATE(ExecutiveMission)->travelTargetSet ) { // failed to reach target
				this->taskBlindTravellerWander( &id );
			} else { // we were wandering
				this->taskBlindTravellerStart( &id );
			}
		}
	} else {
		this->ds.unlock();

		if ( this->avatarSensors[id] != 0 ) { // congregate
			Log.log(0, "ExecutiveMission::convReachedPoint: avatar thread %s target success", Log.formatUUID( 0, &thread) );
			this->avatarInfo[this->avatars[id]].congregated = true;

			this->taskCongregateCheckCompletion();
		} else { // blind traveller
			if ( STATE(ExecutiveMission)->travelTargetSet ) { // we reached the target
				this->taskBlindTravellerFinish();
			} else { // we were wandering
				this->taskBlindTravellerStart( &id );
			}
		}
	}

	return 0;
}

bool ExecutiveMission::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "ExecutiveMission::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID agent = *(UUID *)conv->data;

	if ( this->assignedAvatars.find( agent ) == this->assignedAvatars.end() ) {
		return 0; // no longer our avatar
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		if ( lds.unpackInt32() != DDBAGENTINFO_RSTATUS ) {
			lds.unlock();
			return 0; // what happened here?
		}

		this->agentStatus[agent] = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "ExecutiveMission::convAgentInfo: status %d", this->agentStatus[agent] );

		if ( STATE(ExecutiveMission)->missionPhase == TASK_CONGREGATE )
			this->taskCongregateUpdateTargets();

	} else {
		lds.unlock();

		Log.log( 0, "ExecutiveMission::convAgentInfo: request failed %d", result );
	}

	return 0;
}

/* convGetTaskList
*
* Callback for being a task watcher. Called from ddbNotification when the event is
* DDBE_WATCH_TYPE and the type DDB_TASK.
*
* Loops through all known tasks, and checks completion status.
*/
bool ExecutiveMission::convGetTaskList(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		int i, count;

		if (lds.unpackBool() != true) {	//True if we requested a list of tasks as opposed to just one
			lds.unlock();
			return 0; // what happened here?
		}

		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "ExecutiveMission::convGetTaskList: Notified of %d tasks", count);

		UUID taskIdIn;
		DDBTask newTask;
		bool scenarioCompleted = true;

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&taskIdIn);
			newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));
			Log.log(0, "Task: %s, agent:%s, avatar:%s, type: %d completed:%d", Log.formatUUID(0, &taskIdIn), Log.formatUUID(0, &newTask.agentUUID), Log.formatUUID(0, &newTask.avatar), newTask.type, newTask.completed);

			if (!newTask.completed) {
				Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskList: task not finished");
				scenarioCompleted = false;
			}
			
			}
		lds.unlock();
		if (!scenarioCompleted) {
			Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskList: there are unfinished tasks...");
			return 0;
		}

		Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskList: no unfinished tasks, mission done!");
		this->missionDone();

	}
	else {
		lds.unlock();
		// TODO try again?
	}


	return 0;
}

/* convGetTaskInfo
*
* Callback for task updates. Called from ddbNotification when the type is DDB_TASK.
*
* Checks completion status, requests a full list of tasks if the task has been completed (check for scenario completion).
*/
bool ExecutiveMission::convGetTaskInfo(void * vpConv)
{
	Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo");
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo: response == DDBR_OK");
		if (lds.unpackBool() == true) {	//True if we requested a list of tasks as opposed to just one
			Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo: enumTasks is true, but should not be.");
			lds.unlock();
			return 0; // what happened here?
		}

		UUID taskIdIn;
		DDBTask newTask;

		lds.unpackUUID(&taskIdIn);
		newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));

		if (!newTask.completed) {
			Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo: task not completed...");
			lds.unlock();
			return 0;
		}

		lds.unlock();
		Log.log(LOG_LEVEL_NORMAL, "ExecutiveMission::convGetTaskInfo:  task completed, checking full list for scenario completion...");
		
		// request list of tasks
		UUID thread = this->conversationInitiate(ExecutiveMission_CBR_convGetTaskList, DDB_REQUEST_TIMEOUT);
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(this->getUUID()); // dummy id, getting the full list of tasks anyway
		lds.packUUID(&thread);
		lds.packBool(true);			   //true == send list of tasks, otherwise only info about a specific task
		this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, lds.stream(), lds.length());
		lds.unlock();
	}
	else {
		lds.unlock();
		// TODO try again?
	}


	return 0;
}


bool ExecutiveMission::cbAllocateAvatars( void *NA ) {

	Log.log(0, "ExecutiveMission::cbAllocateAvatars" );

	this->allocateAvatars();
	
	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int ExecutiveMission::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int ExecutiveMission::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	ExecutiveMission::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(ExecutiveMission);

	_WRITE_STATE_LIST( UUID, &this->forbiddenRegions );

	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->avatars );
	_WRITE_STATE_MAP_LESS( UUID, AgentType, UUIDless, &this->avatarTypes );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->avatarSensors );
	_WRITE_STATE_MAP_LESS( UUID, DDBLandmark, UUIDless, &this->landmarks );

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->collectionRegions );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->agentSuperForage );

	_WRITE_STATE_MAP_LESS( UUID, AVATAR_INFO, UUIDless, &this->avatarInfo );
	_WRITE_STATE_MAP_LESS( UUID, char, UUIDless, &this->assignedAvatars );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->agentStatus );

	ds->packBool(this->mapReveal);			
	ds->packBool(this->mapRandom);			
	ds->packBool(this->individualLearning);
	ds->packBool(this->teamLearning);		


	return AgentBase::writeState( ds, false );;
}

int	ExecutiveMission::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(ExecutiveMission);

	_READ_STATE_LIST( UUID, &this->forbiddenRegions );

	_READ_STATE_MAP( UUID, UUID, &this->avatars );
	_READ_STATE_MAP( UUID, AgentType, &this->avatarTypes );
	_READ_STATE_MAP( UUID, int, &this->avatarSensors );
	_READ_STATE_MAP( UUID, DDBLandmark, &this->landmarks );
	
	_READ_STATE_MAP( UUID, DDBRegion, &this->collectionRegions );
	_READ_STATE_MAP( UUID, UUID, &this->agentSuperForage );

	_READ_STATE_MAP( UUID, AVATAR_INFO, &this->avatarInfo );
	_READ_STATE_MAP( UUID, char, &this->assignedAvatars );
	_READ_STATE_MAP( UUID, int, &this->agentStatus );

	this->mapReveal = ds->unpackBool();
	this->mapRandom = ds->unpackBool();
	this->individualLearning = ds->unpackBool();
	this->teamLearning = ds->unpackBool();


	return AgentBase::readState( ds, false );
}

int ExecutiveMission::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		bool needSLAM = false;
		bool needExplore = false;

		if ( STATE(ExecutiveMission)->waitingForAgents == 2 ) {
			needSLAM = needExplore = true;
		} else if ( STATE(ExecutiveMission)->waitingForAgents == 1 ) {
			if ( STATE(ExecutiveMission)->agentSuperSLAM == nilUUID ) {
				needSLAM = true;
			} else {
				needExplore = true;
			}
		}

		if ( needSLAM ) {
			// request a SLAM Supervisor
			UUID sSLAMuuid;
			UuidFromString( (RPC_WSTR)_T(SupervisorSLAM_UUID), &sSLAMuuid );
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convRequestSupervisorSLAM, REQUESTAGENTSPAWN_TIMEOUT, &sSLAMuuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( this->getUUID() );
			lds.packUUID( &sSLAMuuid );
			lds.packChar( -1 ); // no instance parameters
			lds.packFloat32( 0 ); // affinity
			lds.packChar( DDBAGENT_PRIORITY_CRITICAL );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length() );
			lds.unlock();
		}

		if ( needExplore ) {
			// request an Explore Supervisor
			UuidCreateNil( &STATE(ExecutiveMission)->agentSuperExplore );
			UUID sExploreuuid;
			UuidFromString( (RPC_WSTR)_T(SupervisorExplore_UUID), &sExploreuuid );
			UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convRequestSupervisorExplore, REQUESTAGENTSPAWN_TIMEOUT, &sExploreuuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( this->getUUID() );
			lds.packUUID( &sExploreuuid );
			lds.packChar( -1 ); // no instance parameters
			lds.packFloat32( 0 ); // affinity
			lds.packChar( DDBAGENT_PRIORITY_CRITICAL );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length() );
			lds.unlock();
		}

		// request list of avatars
		UUID thread = this->conversationInitiate( ExecutiveMission_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();
	}

	return 0;
}

int ExecutiveMission::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(ExecutiveMission)->pogUUID );

	ds->packUUID( &STATE(ExecutiveMission)->missionRegion );

	ds->packInt32( STATE(ExecutiveMission)->missionStatus );
	ds->packInt32( STATE(ExecutiveMission)->missionPhase );
	ds->packInt32( STATE(ExecutiveMission)->waitingForAgents );
	ds->packUUID( &STATE(ExecutiveMission)->agentSuperSLAM );
	ds->packUUID( &STATE(ExecutiveMission)->agentSuperExplore );

	ds->packData( STATE(ExecutiveMission)->travelTarget, sizeof(float)*3 );
	ds->packBool( STATE(ExecutiveMission)->travelTargetUseRotation );

	_WRITE_STATE_LIST( UUID, &this->forbiddenRegions );

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->collectionRegions );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->agentSuperForage );
	
	_WRITE_STATE_MAP_LESS( UUID, DDBLandmark, UUIDless, &this->landmarks );

	ds->packBool(this->mapReveal);
	ds->packBool(this->mapRandom);
	ds->packBool(this->individualLearning);
	ds->packBool(this->teamLearning);

	return AgentBase::writeBackup( ds );
}

int ExecutiveMission::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(ExecutiveMission)->pogUUID );

	ds->unpackUUID( &STATE(ExecutiveMission)->missionRegion );

	STATE(ExecutiveMission)->missionStatus = ds->unpackInt32();
	STATE(ExecutiveMission)->missionPhase = ds->unpackInt32();
	STATE(ExecutiveMission)->waitingForAgents = ds->unpackInt32();
	ds->unpackUUID( &STATE(ExecutiveMission)->agentSuperSLAM );
	ds->unpackUUID( &STATE(ExecutiveMission)->agentSuperExplore );

	memcpy( STATE(ExecutiveMission)->travelTarget, ds->unpackData( sizeof(float)*3 ), sizeof(float)*3 );
	STATE(ExecutiveMission)->travelTargetUseRotation = ds->unpackBool();

	_READ_STATE_LIST( UUID, &this->forbiddenRegions );

	_READ_STATE_MAP( UUID, DDBRegion, &this->collectionRegions );
	_READ_STATE_MAP( UUID, UUID, &this->agentSuperForage );
	
	_READ_STATE_MAP( UUID, DDBLandmark, &this->landmarks );

	this->mapReveal = ds->unpackBool();
	this->mapRandom = ds->unpackBool();
	this->individualLearning = ds->unpackBool();
	this->teamLearning = ds->unpackBool();

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	ExecutiveMission *agent = (ExecutiveMission *)vpAgent;

	if ( agent->configure() ) {
		delete agent;
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();
	delete agent;

	return 0;
}

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	ExecutiveMission *agent = new ExecutiveMission( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	ExecutiveMission *agent = new ExecutiveMission( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CExecutiveMissionDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CExecutiveMissionDLL, CWinApp)
END_MESSAGE_MAP()

CExecutiveMissionDLL::CExecutiveMissionDLL() {}

// The one and only CExecutiveMissionDLL object
CExecutiveMissionDLL theApp;

int CExecutiveMissionDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CExecutiveMissionDLL ---\n"));
  return CWinApp::ExitInstance();
}