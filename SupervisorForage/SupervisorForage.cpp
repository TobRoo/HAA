// SupervisorForage.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "SupervisorForage.h"
#include "SupervisorForageVersion.h"

#include "..\\ExecutiveMission\\ExecutiveMissionVersion.h"
#include "..\\AvatarBase\\AvatarBaseVersion.h"
#include "..\\AvatarSimulation\\AvatarSimulationVersion.h"


#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

#define round(val) floor((val) + 0.5f)

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//#define DO_VISUALIZE

//*****************************************************************************
// SupervisorForage

//-----------------------------------------------------------------------------
// Constructor	
SupervisorForage::SupervisorForage( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {

	// allocate state
	ALLOCATE_STATE( SupervisorForage, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "SupervisorForage" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(SupervisorForage_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( SupervisorForage_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	STATE(SupervisorForage)->waitingOnPF = 0;
	STATE(SupervisorForage)->requestAvatarInProgress = false;
	
	STATE(SupervisorForage)->landmark.code = -1; // invalid

	STATE(SupervisorForage)->landmarkCollected = false;
	STATE(SupervisorForage)->landmarkDelivered = false;

	STATE(SupervisorForage)->forageQueued = false;

	STATE(SupervisorForage)->deliveryLocValid = false;

	// Prepare callbacks
	this->callback[SupervisorForage_CBR_convGetAvatarList] = NEW_MEMBER_CB(SupervisorForage,convGetAvatarList);
	this->callback[SupervisorForage_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(SupervisorForage,convGetAvatarInfo);
	this->callback[SupervisorForage_CBR_convGetAvatarPos] = NEW_MEMBER_CB(SupervisorForage,convGetAvatarPos);
	this->callback[SupervisorForage_CBR_convReachedPoint] = NEW_MEMBER_CB(SupervisorForage,convReachedPoint);
	this->callback[SupervisorForage_CBR_convAgentInfo] = NEW_MEMBER_CB(SupervisorForage,convAgentInfo);
	this->callback[SupervisorForage_CBR_convLandmarkInfo] = NEW_MEMBER_CB(SupervisorForage,convLandmarkInfo);
	this->callback[SupervisorForage_CBR_convRequestAvatar] = NEW_MEMBER_CB(SupervisorForage,convRequestAvatar);
	this->callback[SupervisorForage_CBR_convCollectLandmark] = NEW_MEMBER_CB(SupervisorForage,convCollectLandmark);
	this->callback[SupervisorForage_CBR_cbQueueForage] = NEW_MEMBER_CB(SupervisorForage,cbQueueForage);

}

//-----------------------------------------------------------------------------
// Destructor
SupervisorForage::~SupervisorForage() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SupervisorForage::configure() {
	
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
		sprintf_s( logName, "%s\\SupervisorForage %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SupervisorForage %.2d.%.2d.%.5d.%.2d", SupervisorForage_MAJOR, SupervisorForage_MINOR, SupervisorForage_BUILDNO, SupervisorForage_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int SupervisorForage::start( char *missionFile ) {
	DataStream lds;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars when DDBE_WATCH_TYPE notification is received

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SupervisorForage::stop() {

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int SupervisorForage::step() {
	//Log.log( 0, "SupervisorForage::step: hello there" );
	return AgentBase::step();
}

int SupervisorForage::configureParameters( DataStream *ds ) {
	DataStream lds;
	UUID uuid;

	ds->unpackUUID( &uuid );
	STATE(SupervisorForage)->avatarExecId = uuid;

	// watch avatarExec
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &STATE(SupervisorForage)->avatarExecId );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

	ds->unpackUUID( &uuid );
	STATE(SupervisorForage)->landmarkId = uuid;
	STATE(SupervisorForage)->landmark.code = -1; // invalid

	// watch landmark
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &uuid );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we get the landmark data once we recieve the DDBE_WATCH_ITEM notification

	while ( ds->unpackBool() ) {
		ds->unpackUUID( &uuid );
		this->collectionRegions[uuid] = *(DDBRegion *)ds->unpackData( sizeof(DDBRegion) );
	}

	Log.log( 0, "SupervisorForage::configureParameters: configured, landmarkId %s, %d collection regions", Log.formatUUID(0,&STATE(SupervisorForage)->landmarkId), (int)this->collectionRegions.size() );

	this->backup(); // avatarExecId, landmarkId, collectionRegions

	return 0;
}

int SupervisorForage::avatarUpdateController( UUID *avatarId, UUID *controller, int index ) {

	if ( this->avatarInfo.find( *avatarId ) == this->avatarInfo.end() ) { // don't know them yet
		this->avatarInfo[*avatarId].owner = nilUUID;
		this->avatarInfo[*avatarId].pfId = nilUUID;
		this->avatarInfo[*avatarId].controller = nilUUID;
		this->avatarInfo[*avatarId].controllerIndex = 0;
	}

	Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::avatarUpdateController: recieved avatar (%s) controller %s, old controller %s",
		Log.formatUUID(LOG_LEVEL_VERBOSE, avatarId), Log.formatUUID(LOG_LEVEL_VERBOSE, (controller ? controller : &nilUUID)), Log.formatUUID(LOG_LEVEL_VERBOSE, &this->avatarInfo[*avatarId].controller) );

	this->avatarInfo[*avatarId].controller = *controller;
	this->avatarInfo[*avatarId].controllerIndex = index;
	
	if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller == *this->getUUID() && this->avatars.find(this->avatarInfo[*avatarId].owner) == this->avatars.end() ) {
		// is now ours, add avatar
		this->addAvatar( &this->avatarInfo[*avatarId].owner, avatarId );
	} else if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller != *this->getUUID() && this->avatars.find(this->avatarInfo[*avatarId].owner) != this->avatars.end() ) {
		// used to be ours, remove avatar
		this->remAvatar( &this->avatarInfo[*avatarId].owner );
	}

	return 0;
}

int SupervisorForage::addAvatar( UUID *agentId, UUID *avatarId ) {
	DataStream lds;
	AVATAR avatar;

	Log.log( LOG_LEVEL_NORMAL, "SupervisorForage::addAvatar: adding avatar %s (agent %s)", 
		Log.formatUUID(LOG_LEVEL_NORMAL,avatarId), Log.formatUUID(LOG_LEVEL_NORMAL,agentId) );

	avatar.avatarId = *avatarId;
	avatar.pfId = this->avatarInfo[*avatarId].pfId;
	avatar.pfKnown = true; 

	this->avatars[*agentId] = avatar;

	// register as agent watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( agentId );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

	if ( avatars.size() > 1 ) { // we have too many avatars, release some
		std::map<UUID,AVATAR,UUIDless>::iterator iA;
		iA = ++this->avatars.begin();
		for ( ; iA != this->avatars.end(); iA++ ) {
			lds.reset();
			lds.packUUID( this->getUUID() );
			lds.packUUID( (UUID *)&iA->first );
			this->sendMessageEx( this->hostCon, MSGEX(ExecutiveMission_MSGS,MSG_RELEASE_AVATAR), lds.stream(), lds.length(), &STATE(SupervisorForage)->avatarExecId );
			lds.unlock();
		}
	}

	return 0;
}

int SupervisorForage::remAvatar( UUID *agentId ) {
	DataStream lds;

	Log.log( LOG_LEVEL_NORMAL, "SupervisorForage::remAvatar: removing avatar (agent %s)", 
		Log.formatUUID(LOG_LEVEL_NORMAL,agentId) );

	// stop watching agent
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( agentId );
	this->sendMessage( this->hostCon, MSG_DDB_STOP_WATCHING_ITEM, lds.stream(), lds.length() );
	lds.unlock();

	// delete avatar
	std::map<UUID,AVATAR,UUIDless>::iterator iA = this->avatars.find( *agentId );
	if ( iA != this->avatars.end() ) {
		this->avatars.erase( iA );
	}

	// delete status
	std::map<UUID,int,UUIDless>::iterator iS = this->agentStatus.find( *agentId );
	if ( iS != this->agentStatus.end() ) {
		this->agentStatus.erase( iS );
	}

	this->forage();

	return 0;
}

int SupervisorForage::forage() {	
	DataStream lds;
	UUID thread;
	_timeb tb;

	if ( STATE(SupervisorForage)->landmark.code == -1 || STATE(SupervisorForage)->landmarkDelivered )
		return 0; // already done

	// check if we have an avatar assigned
	if ( this->avatars.size() > 0 ) {

		Log.log( 0, "SupervisorForage::forage: requesting avatar position" );
		
		UUID avId = this->avatars.begin()->second.avatarId;
		UUID pfId = this->avatars.begin()->second.pfId;
		// request pf data
		thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT, (void *)&avId, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( &pfId ); 
		lds.packInt32( DDBPFINFO_CURRENT );
		lds.packData( &tb, sizeof(_timeb) ); // dummy
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
		lds.unlock();

		STATE(SupervisorForage)->waitingOnPF++;
		
	} else { // get info and select avatar

		Log.log( 0, "SupervisorForage::forage: preparing to select avatar" );

		std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
		for ( iAI = this->avatarInfo.begin(); iAI != this->avatarInfo.end(); iAI++ ) {
			if ( iAI->second.cargoCount >= iAI->second.capacity )
				continue; // already at capacity

			// request pf data
			thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT, (void *)&iAI->first, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( (UUID *)&iAI->second.pfId ); 
			lds.packInt32( DDBPFINFO_CURRENT );
			lds.packData( &tb, sizeof(_timeb) ); // dummy
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
			lds.unlock();

			STATE(SupervisorForage)->waitingOnPF++;
		}

		if ( !STATE(SupervisorForage)->waitingOnPF ) { // no acceptable avatars
			this->queueForage();
		}

	}

	return 0;
}

int SupervisorForage::queueForage() {

	if ( STATE(SupervisorForage)->forageQueued )
		return 0;

	STATE(SupervisorForage)->forageQueued = true;

	this->addTimeout( 10000, SupervisorForage_CBR_cbQueueForage );

	return 0;
}

int SupervisorForage::selectAvatar() {
	DataStream lds;
	int i;
	float dx, dy, DSq;
	int pri;
	std::list<float> rankDSq;
	std::list<UUID> rank;
	std::list<int> priority;
	std::list<float>::iterator iDSq;
	std::list<UUID>::iterator iRank;
	std::list<int>::iterator iPriority;

	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iAI;
	for ( iAI = this->avatarInfo.begin(); iAI != this->avatarInfo.end(); iAI++ ) {
		if ( !iAI->second.pfValid || iAI->second.cargoCount >= iAI->second.capacity )
			continue; // skip
		
		dx = iAI->second.pfState[0] - STATE(SupervisorForage)->landmark.x;
		dy = iAI->second.pfState[1] - STATE(SupervisorForage)->landmark.y;
		DSq = dx*dx + dy*dy;

		for ( iDSq = rankDSq.begin(), iRank = rank.begin(), iPriority = priority.begin(); iDSq != rankDSq.end(); iDSq++, iRank++, iPriority++ ) {
			if ( DSq < *iDSq )
				break;
		}

		if ( DSq < 1.5*1.5 ) {
			pri = DDBAVATAR_CP_HIGH;
		} else if ( DSq < 2.5*2.5 ) {
			pri = DDBAVATAR_CP_MEDIUM;
		} else {
			pri = DDBAVATAR_CP_LOW;
		}
		rankDSq.insert( iDSq, DSq );
		rank.insert( iRank, iAI->first );
		priority.insert( iPriority, pri );

	}

	if ( rank.size() == 0 ) {
		Log.log( 0, "SupervisorForage::selectAvatar: no potential avatars" );
		this->queueForage();
		return 0;
	}

	Log.log( 0, "SupervisorForage::selectAvatar: requesting avatar" );
		
	// select our top five picks
	UUID thread = this->conversationInitiate( SupervisorForage_CBR_convRequestAvatar, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packChar( ExecutiveMission_Defs::RA_ONE );
	lds.packInt32( min( 5, (int)rank.size() ) );
	for ( iRank = rank.begin(), iPriority = priority.begin(), i = 0; iRank != rank.end() && i < 5; iRank++, iPriority++, i++ ) {
		lds.packUUID( &*iRank );
		lds.packInt32( *iPriority );
	}
	lds.packUUID( &thread );
	this->sendMessageEx( this->hostCon, MSGEX(ExecutiveMission_MSGS,MSG_REQUEST_AVATAR), lds.stream(), lds.length(), &STATE(SupervisorForage)->avatarExecId );
	lds.unlock();

	STATE(SupervisorForage)->requestAvatarInProgress = true;

	return 0;
}

int SupervisorForage::collect() {
	DataStream lds;
	float dx, dy;
	UUID avAgent = this->avatars.begin()->first;
	UUID avId = this->avatars.begin()->second.avatarId;
	UUID thread;

	// see if we are within range of the landmark
	dx = this->avatarInfo[avId].pfState[0] - STATE(SupervisorForage)->landmark.x;
	dy = this->avatarInfo[avId].pfState[1] - STATE(SupervisorForage)->landmark.y;
	if ( dx*dx + dy*dy < COLLECTION_THRESHOLD*COLLECTION_THRESHOLD ) { // should be close enough
		Log.log( 0, "SupervisorForage::collect: collecting landmark at %f %f", STATE(SupervisorForage)->landmark.x, STATE(SupervisorForage)->landmark.y );
		thread = this->conversationInitiate( SupervisorForage_CBR_convCollectLandmark, -1, &avAgent, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUChar( STATE(SupervisorForage)->landmark.code );
		lds.packFloat32( STATE(SupervisorForage)->landmark.x );
		lds.packFloat32( STATE(SupervisorForage)->landmark.y );
		lds.packUUID( this->getUUID() );
		lds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarSimulation_MSGS,MSG_COLLECT_LANDMARK), lds.stream(), lds.length(), &avAgent );
		lds.unlock();

	} else {
		Log.log( 0, "SupervisorForage::collect: moving from %f %f to %f %f", this->avatarInfo[avId].pfState[0], this->avatarInfo[avId].pfState[1], STATE(SupervisorForage)->landmark.x, STATE(SupervisorForage)->landmark.y );
		thread = this->conversationInitiate( SupervisorForage_CBR_convReachedPoint, -1, &avAgent, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packFloat32( STATE(SupervisorForage)->landmark.x );
		lds.packFloat32( STATE(SupervisorForage)->landmark.y );
		lds.packFloat32( 0 );
		lds.packChar( 0 );
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packInt32( this->avatarInfo[avId].controllerIndex );
		lds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), lds.stream(), lds.length(), &avAgent );
		lds.unlock();
	}

	return 0;
}

int SupervisorForage::deliver() {
	DataStream lds;
	UUID avAgent = this->avatars.begin()->first;
	UUID avId = this->avatars.begin()->second.avatarId;
	UUID thread;

	float dx, dy;

	this->calcDeliveryLoc();

	// see if we are within range of the delivery loc
	dx = this->avatarInfo[avId].pfState[0] - STATE(SupervisorForage)->deliveryLoc[0];
	dy = this->avatarInfo[avId].pfState[1] - STATE(SupervisorForage)->deliveryLoc[1];
	if ( dx*dx + dy*dy < DELIVERY_THRESHOLD*DELIVERY_THRESHOLD ) { // should be close enough
		Log.log( 0, "SupervisorForage::deliver: dropping off landmark at %f %f", STATE(SupervisorForage)->deliveryLoc[0], STATE(SupervisorForage)->deliveryLoc[1] );
		
		STATE(SupervisorForage)->landmarkDelivered = true;
		this->backup(); // landmarkDelivered

		// drop off
		lds.reset();
		lds.packUChar( STATE(SupervisorForage)->landmark.code );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarSimulation_MSGS,MSG_DEPOSIT_LANDMARK), lds.stream(), lds.length(), &avAgent );
		lds.unlock();

	/*	// release avatar
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packUUID( &avId );
		this->sendMessageEx( this->hostCon, MSGEX(ExecutiveMission_MSGS,MSG_RELEASE_AVATAR), lds.stream(), lds.length(), &STATE(SupervisorForage)->avatarExecId );
		lds.unlock();

		*/

		// release avatar
		lds.reset();
		lds.packUUID( &avId );
		lds.packInt32( DDBAVATARINFO_CONTROLLER_RELEASE );
		lds.packUUID( this->getUUID() );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
		lds.unlock();

		// shutdown
		this->sendMessage( this->hostCon, MSG_AGENT_SHUTDOWN );
		STATE(AgentBase)->stopFlag = true;

	} else {

		Log.log( 0, "SupervisorForage::deliver: moving from %f %f to %f %f", this->avatarInfo[avId].pfState[0], this->avatarInfo[avId].pfState[1], STATE(SupervisorForage)->deliveryLoc[0], STATE(SupervisorForage)->deliveryLoc[1] );
		thread = this->conversationInitiate( SupervisorForage_CBR_convReachedPoint, -1, &avAgent, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packFloat32( STATE(SupervisorForage)->deliveryLoc[0] );
		lds.packFloat32( STATE(SupervisorForage)->deliveryLoc[1] );
		lds.packFloat32( 0 );
		lds.packChar( 0 );
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packInt32( this->avatarInfo[avId].controllerIndex );
		lds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), lds.stream(), lds.length(), &avAgent );
		lds.unlock();
	}

	return 0;
}


int SupervisorForage::calcDeliveryLoc( bool force ) {
	float dx, dy, DSq, bestDSq;

	if ( STATE(SupervisorForage)->deliveryLocValid && !force )
		return 0;

	UUID avId = this->avatars.begin()->second.avatarId;

	STATE(SupervisorForage)->deliveryLocValid = true;

	// choose delivery loc
	bestDSq = 99999999.0f;
	std::map<UUID,DDBRegion,UUIDless>::iterator iR;
	std::map<UUID,DDBRegion,UUIDless>::iterator bestR;
	for ( iR = this->collectionRegions.begin(); iR != this->collectionRegions.end(); iR++ ) {
		dx = this->avatarInfo[avId].pfState[0] - (iR->second.x + iR->second.w*0.5f);
		dy = this->avatarInfo[avId].pfState[1] - (iR->second.y + iR->second.h*0.5f);
		DSq = dx*dx + dy*dy;
		if ( DSq < bestDSq ) {
			bestDSq = DSq;
			bestR = iR;
		}
	}

	float buffer = min( bestR->second.w*0.5f, min( bestR->second.h*0.5f, 0.2f ) ); // extra buffer to make sure we're inside
	STATE(SupervisorForage)->deliveryLoc[0] = bestR->second.x + buffer + (float)apb->apbUniform01()*(bestR->second.w-(buffer*2));
	STATE(SupervisorForage)->deliveryLoc[1] = bestR->second.y + buffer + (float)apb->apbUniform01()*(bestR->second.h-(buffer*2));

	return 0;
}


bool SupervisorForage::checkReady( UUID *agent ) {
	if ( this->agentStatus.find(*agent) != this->agentStatus.end()
			&& (this->agentStatus[*agent] == DDBAGENT_STATUS_READY
			|| this->agentStatus[*agent] == DDBAGENT_STATUS_FREEZING
			|| this->agentStatus[*agent] == DDBAGENT_STATUS_FROZEN
			|| this->agentStatus[*agent] == DDBAGENT_STATUS_THAWING) )
			return true;

	return false;
}

int SupervisorForage::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

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
			UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
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
	} else if ( evt == DDBE_WATCH_ITEM ) {
		if ( STATE(SupervisorForage)->landmarkId == uuid ) {
			// get landmark
			UUID thread = this->conversationInitiate( SupervisorForage_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( uuid == STATE(SupervisorForage)->avatarExecId || this->avatars.find(uuid) != this->avatars.end() ) { // one of our avatar agents
			// get status
			UUID thread = this->conversationInitiate( SupervisorForage_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_AGENT ) {
		if ( this->avatars.find(uuid) != this->avatars.end() || uuid == STATE(SupervisorForage)->avatarExecId ) { // our agent
			if ( evt == DDBE_AGENT_UPDATE ) {
				int infoFlags = lds.unpackInt32();
				if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
					
					// get status
					UUID thread = this->conversationInitiate( SupervisorForage_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
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
			UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &uuid ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER | DDBAVATARINFO_RSENSORTYPES | DDBAVATARINFO_RCAPACITY | DDBAVATARINFO_RCARGO  );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( evt == DDBE_REM ) {
			this->avatarUpdateController( &uuid, &nilUUID, 0 );
		} else if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAVATARINFO_CONTROLLER ) {
				// get info
				UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
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
			if ( infoFlags & DDBAVATARINFO_CARGO ) {
				// get info
				UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				if ( thread == nilUUID ) {
					return 1;
				}
				sds.reset();
				sds.packUUID( &uuid ); 
				sds.packInt32( DDBAVATARINFO_RCARGO );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	} else if ( type == DDB_LANDMARK && STATE(SupervisorForage)->landmarkId == uuid ) {
		if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBLANDMARKINFO_POS ) {
				// get landmark
				UUID thread = this->conversationInitiate( SupervisorForage_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &uuid );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	}
	lds.unlock();
	
	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int SupervisorForage::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case SupervisorForage_MSGS::MSG_CONFIGURE:
		lds.setData( data, len );
		this->configureParameters( &lds );
		lds.unlock();
		break;	
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool SupervisorForage::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorForage::convGetAvatarList: timed out" );
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
		Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarList: recieved %d avatars", count );

		for ( i=0; i<count; i++ ) {
			lds.unpackUUID( &avatarId );
			lds.unpackString(); // avatar type
			lds.unpackUUID( &agentId );
			lds.unpackUUID( &agentType.uuid );
			agentType.instance = lds.unpackInt32();

			// request further info
			UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &avatarId ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER | DDBAVATARINFO_RSENSORTYPES | DDBAVATARINFO_RCAPACITY | DDBAVATARINFO_RCARGO  );
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

bool SupervisorForage::convGetAvatarInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorForage::convGetAvatarInfo: timed out" );
		return 0; // end conversation
	}

	UUID avId = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID agent, pfId, controller;
		int index, priority;
		int infoFlags = lds.unpackInt32();

		if ( infoFlags == DDBAVATARINFO_RCONTROLLER ) {
			lds.unpackUUID( &controller );
			index = lds.unpackInt32();
			priority = lds.unpackInt32();
			lds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarInfo: recieved avatar (%s) controller: %s, index %d, priority %d", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&controller), index, priority );

			this->avatarUpdateController( &avId, &controller, index );

		} else if ( infoFlags == DDBAVATARINFO_RCARGO ) {
			this->avatarInfo[avId].cargoCount = lds.unpackInt32();
			lds.unpackData( sizeof(char)*this->avatarInfo[avId].cargoCount );

			lds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarInfo: recieved avatar (%s) cargoCount: %d", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), this->avatarInfo[avId].cargoCount );

		} else if ( infoFlags == (DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER | DDBAVATARINFO_RSENSORTYPES | DDBAVATARINFO_RCAPACITY | DDBAVATARINFO_RCARGO ) ) {
			lds.unpackUUID( &agent );
			lds.unpackUUID( &pfId );
			lds.unpackUUID( &controller );
			index = lds.unpackInt32();
			priority = lds.unpackInt32();
			this->avatarInfo[avId].sensorTypes = lds.unpackInt32();
			this->avatarInfo[avId].capacity = lds.unpackInt32();
			this->avatarInfo[avId].cargoCount = lds.unpackInt32();
			lds.unpackData( sizeof(char)*this->avatarInfo[avId].cargoCount );

			lds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarInfo: recieved avatar (%s) agent: %s, pf id: %s", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&pfId) );

			this->avatarInfo[avId].owner = agent;
			this->avatarInfo[avId].pfId = pfId;
			this->avatarInfo[avId].pfValid = false;

			this->avatarUpdateController( &avId, &controller, index );

		} else {
			lds.unlock();
			return 0; // what happened here?
		}

	} else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool SupervisorForage::convGetAvatarPos( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorForage::convGetAvatarPos: timed out" );
		return 0; // end conversation
	}

	UUID uuid = *(UUID *)conv->data;

	if ( this->avatarInfo.find( uuid ) == this->avatarInfo.end() ) {
		return 0; 
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		_timeb *tb;

		if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		tb = (_timeb *)this->ds.unpackData( sizeof(_timeb) );
		memcpy( this->avatarInfo[uuid].pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );

		this->ds.unlock();

		this->avatarInfo[uuid].pfValid = true; // we have received the state at least once

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarPos: recieved avatar pos %s (%f %f)", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&uuid), this->avatarInfo[uuid].pfState[0], this->avatarInfo[uuid].pfState[1] );

		STATE(SupervisorForage)->waitingOnPF--;

		if ( !STATE(SupervisorForage)->waitingOnPF ) {
			if ( this->avatars.size() > 0 ) { // must be trying to collect/deliver
				if ( !STATE(SupervisorForage)->landmarkCollected ) {
					if ( this->checkReady(&this->avatarInfo[uuid].owner) ) // ready
						this->collect();
				} else {
					if ( this->checkReady(&this->avatarInfo[uuid].owner) 
						&& this->checkReady(&STATE(SupervisorForage)->avatarExecId) ) // Avatar and ExecutiveAvatar are ready
					this->deliver();
				}
			} else { // must be trying to get an avatar
				this->selectAvatar();
			}
		}

	} else { // failed
		_timeb tb; // dummy
		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorForage::convGetAvatarPos: failed to get avatar pos %s, retrying in %d", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&uuid), SupervisorForage_AVATAR_POS_RETRY );

		UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT + SupervisorForage_AVATAR_POS_RETRY, &uuid, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		
		this->ds.reset();
		this->ds.packUUID( (UUID *)&this->avatars[uuid].pfId ); 
		this->ds.packInt32( DDBPFINFO_CURRENT );
		this->ds.packData( &tb, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->delayMessage( SupervisorForage_AVATAR_POS_RETRY, this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 0;
}

bool SupervisorForage::convReachedPoint( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorForage::convReachedPoint: timed out" );
		return 0; // end conversation
	}

	UUID id;
	id = *(UUID *)conv->data;

	if ( this->avatars.find( id ) == this->avatars.end() ) {
		return 0; // no longer our avatar
	}

	this->ds.setData( conv->response, conv->responseLen );
	
	UUID thread;
	this->ds.unpackUUID( &thread );
	char success = this->ds.unpackChar();

	int reason;
	if ( !success ) {
		reason = this->ds.unpackInt32();
		this->ds.unlock();
		Log.log(0, "SupervisorForage::convReachedPoint: avatar thread %s target fail %d", Log.formatUUID( 0, &thread), reason );
		if ( reason == AvatarBase_Defs::TP_NEW_TARGET ) { // we gave them a new target, no reason to do anything
			return 0;
		} else {
			if ( STATE(SupervisorForage)->landmarkCollected ) { // must have failed to reach our delivery loc
				STATE(SupervisorForage)->deliveryLocValid = false;
			}
			this->forage();
		}

	} else {
		this->ds.unlock();
		Log.log(0, "SupervisorForage::convReachedPoint: avatar thread %s target success", Log.formatUUID( 0, &thread) );

		this->forage();
	}

	return 0;
}

bool SupervisorForage::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorForage::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID agent = *(UUID *)conv->data;

	if ( agent != STATE(SupervisorForage)->avatarExecId && this->avatars.find( agent ) == this->avatars.end() ) {
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

		int status = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "SupervisorForage::convAgentInfo: status %d (%s)", status, Log.formatUUID(0,&agent) );

		bool wasready, isready;

		wasready = checkReady( &agent );
		
		this->agentStatus[agent] = status;

		isready = checkReady( &agent );

		if ( !wasready && isready ) 
			this->forage();

	} else {
		lds.unlock();

		Log.log( 0, "SupervisorForage::convAgentInfo: request failed %d", result );
	}

	return 0;
}

bool SupervisorForage::convLandmarkInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char response;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorForage::convLandmarkInfo: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		DDBLandmark lm;
		lm = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
		lds.unlock();

		if ( STATE(SupervisorForage)->landmark.code == -1 || fabs(lm.x - STATE(SupervisorForage)->landmark.x) > 0.01f || fabs(lm.y - STATE(SupervisorForage)->landmark.y) > 0.01f ) {
			STATE(SupervisorForage)->landmark = lm;
			if ( STATE(AgentBase)->started ) 
				this->forage();
		}
	} else {
		lds.unlock();
	}

	return 0;
}

bool SupervisorForage::convRequestAvatar( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char response;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorForage::convRequestAvatar: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	STATE(SupervisorForage)->requestAvatarInProgress = false;

	response = lds.unpackChar();
	lds.unlock();

	if ( response == DDBR_OK ) { // succeeded
		// wait for avatar controller updates
		Log.log( 0, "SupervisorForage::convRequestAvatar: request succeeded" );
	} else {		
		Log.log( 0, "SupervisorForage::convRequestAvatar: request failed" );
		this->queueForage();
	}

	return 0;
}

bool SupervisorForage::convCollectLandmark( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char success;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorForage::convCollectLandmark: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	success = lds.unpackChar();
	lds.unlock();

	if ( success ) { // succeeded
		Log.log( 0, "SupervisorForage::convCollectLandmark: success" );
		STATE(SupervisorForage)->landmarkCollected = true;
		this->backup(); // landmarkCollected
		this->forage();
	} else {		
		Log.log( 0, "SupervisorForage::convCollectLandmark: failed" );
		this->forage();
	}

	return 0;
}

bool SupervisorForage::cbQueueForage( void *NA ) {

	Log.log(0, "SupervisorForage::cbQueueForage" );

	STATE(SupervisorForage)->forageQueued = false;

	this->forage();
	
	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int SupervisorForage::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int SupervisorForage::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	SupervisorForage::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(SupervisorForage);

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->collectionRegions );
	

	// avatars
	_WRITE_STATE_MAP_LESS( UUID, AVATAR_INFO, UUIDless, &this->avatarInfo );
	_WRITE_STATE_MAP_LESS( UUID, AVATAR, UUIDless, &this->avatars );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->agentStatus );

	return AgentBase::writeState( ds, false );;
}

int	SupervisorForage::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(SupervisorForage);

	_READ_STATE_MAP( UUID, DDBRegion, &this->collectionRegions );
	
	// avatars
	_READ_STATE_MAP( UUID, AVATAR_INFO, &this->avatarInfo );
	_READ_STATE_MAP( UUID, AVATAR, &this->avatars );
	_READ_STATE_MAP( UUID, int, &this->agentStatus );

	return AgentBase::readState( ds, false );
}

int SupervisorForage::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		// request list of avatars
		UUID thread = this->conversationInitiate( SupervisorForage_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();

		// get avatar executive status
		thread = this->conversationInitiate( SupervisorForage_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &STATE(SupervisorForage)->avatarExecId, sizeof(UUID) );
		lds.reset();
		lds.packUUID( &STATE(SupervisorForage)->avatarExecId );
		lds.packInt32( DDBAGENTINFO_RSTATUS );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, lds.stream(), lds.length() );
		lds.unlock();

		// get landmark
		thread = this->conversationInitiate( SupervisorForage_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &STATE(SupervisorForage)->landmarkId, sizeof(UUID) );
		lds.reset();
		lds.packUUID( &STATE(SupervisorForage)->landmarkId );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RLANDMARK, lds.stream(), lds.length() );
		lds.unlock();
	}

	return 0;
}

int SupervisorForage::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(SupervisorForage)->avatarExecId );
	ds->packBool( STATE(SupervisorForage)->landmarkCollected );
	ds->packBool( STATE(SupervisorForage)->landmarkDelivered );
	ds->packUUID( &STATE(SupervisorForage)->landmarkId );

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->collectionRegions );

	return AgentBase::writeBackup( ds );
}

int SupervisorForage::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(SupervisorForage)->avatarExecId );
	STATE(SupervisorForage)->landmarkCollected = ds->unpackBool();
	STATE(SupervisorForage)->landmarkDelivered = ds->unpackBool();
	ds->unpackUUID( &STATE(SupervisorForage)->landmarkId );

	_READ_STATE_MAP( UUID, DDBRegion, &this->collectionRegions );

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	SupervisorForage *agent = (SupervisorForage *)vpAgent;

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
	SupervisorForage *agent = new SupervisorForage( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	SupervisorForage *agent = new SupervisorForage( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CSupervisorForageDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CSupervisorForageDLL, CWinApp)
END_MESSAGE_MAP()

CSupervisorForageDLL::CSupervisorForageDLL() {}

// The one and only CSupervisorForageDLL object
CSupervisorForageDLL theApp;

int CSupervisorForageDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CSupervisorForageDLL ---\n"));
  return CWinApp::ExitInstance();
}