// SupervisorSLAM.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "SupervisorSLAM.h"
#include "SupervisorSLAMVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\AgentSensorSonar\\AgentSensorSonarVersion.h"
#include "..\\AgentSensorLandmark\\AgentSensorLandmarkVersion.h"
#include "..\\AgentSensorFloorFinder\\AgentSensorFloorFinderVersion.h"
#include "..\\AgentSensorCooccupancy\\AgentSensorCooccupancyVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// SupervisorSLAM

//-----------------------------------------------------------------------------
// Constructor	
SupervisorSLAM::SupervisorSLAM( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {

	// allocate state
	ALLOCATE_STATE( SupervisorSLAM, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "SupervisorSLAM" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(SupervisorSLAM_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( SupervisorSLAM_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(SupervisorSLAM)->mapId );

	this->backupReadingsCount = BACKUP_READINGS_THRESHOLD;
	STATE(SupervisorSLAM)->hostGroupSize = 1; // must be at least one
	STATE(SupervisorSLAM)->lastRedundantSpawnRequest.time = 0; // unset
	STATE(SupervisorSLAM)->lastRedundantSpawnRejected.time = 0; // unset

	STATE(SupervisorSLAM)->SLAMmode = SLAM_MODE_JCSLAM;

	// Prepare callbacks
	this->callback[SupervisorSLAM_CBR_convRequestAgentSpawn] = NEW_MEMBER_CB(SupervisorSLAM,convRequestAgentSpawn);
	this->callback[SupervisorSLAM_CBR_convAgentInfo] = NEW_MEMBER_CB(SupervisorSLAM,convAgentInfo);
	this->callback[SupervisorSLAM_CBR_convHostGroupSize] = NEW_MEMBER_CB(SupervisorSLAM,convHostGroupSize);
	
}

//-----------------------------------------------------------------------------
// Destructor
SupervisorSLAM::~SupervisorSLAM() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SupervisorSLAM::configure() {

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
		sprintf_s( logName, "%s\\SupervisorSLAM %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		//Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );

#ifdef	NO_LOGGING
		Log.setLogMode(LOG_MODE_OFF);
		Log.setLogLevel(LOG_LEVEL_NONE);
#endif

		Log.log( 0, "SupervisorSLAM %.2d.%.2d.%.5d.%.2d", SupervisorSLAM_MAJOR, SupervisorSLAM_MINOR, SupervisorSLAM_BUILDNO, SupervisorSLAM_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int SupervisorSLAM::start( char *missionFile ) {
	DataStream lds;
	RPC_STATUS status;

	if ( UuidIsNil( &STATE(SupervisorSLAM)->mapId, &status ) ) {
		Log.log( 0, "AgentSensorFloorFinder::start: MSG_ADD_MAP must be sent before starting!" );
		return 1;
	}

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// initialize processor list

	// register to watch particle filters, sensors, and host group
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_PARTICLEFILTER | DDB_SENSORS | DDB_HOST_GROUP );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	
	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SupervisorSLAM::stop() {

	mapAgentQueue::iterator iQ;
	for ( iQ = this->agentQueue.begin(); iQ != this->agentQueue.end(); iQ++ ) {
		if ( !iQ->second.empty() )
			Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::stop: %d unprocessed readings, agent %s", (int)iQ->second.size(), Log.formatUUID(LOG_LEVEL_VERBOSE,(UUID *)&iQ->first) );
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int SupervisorSLAM::step() {
	return AgentBase::step();
}

int SupervisorSLAM::parseMF_HandleMapOptions(bool mapReveal, bool mapRandom) {
	Log.log(0, "SupervisorSLAM::parseMF_HandleMapOptions running");
	this->mapReveal = mapReveal;
	this->mapRandom = mapRandom;
	Log.log(0, "SupervisorSLAM::parseMF_HandleMapOptions: mapReveal is %d, mapRandom is %d", this->mapReveal, this->mapRandom);
	return 0;
}


int SupervisorSLAM::parseMF_HandleOptions( int SLAMmode ) {
	STATE(SupervisorSLAM)->SLAMmode = SLAMmode;

	return AgentBase::parseMF_HandleOptions( SLAMmode );
}


int SupervisorSLAM::addMap( UUID *map ) {
	STATE(SupervisorSLAM)->mapId = *map;

	this->backup(); // mapId

	return 0;
}

int SupervisorSLAM::getProcessingPhases( int sensor ) {
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
		Log.log( 0, "SupervisorSLAM::getProcessingPhases: unknown sensor type!" );
		return 0;
	}
}

int SupervisorSLAM::requestAgentSpawn( READING_TYPE *type, char priority ) {
		
//	if (!this->mapReveal) {	//Only need sensors if map is not revealed...

		this->agentsSpawning[*type]++; // we're spawning this type of agent

		UUID sAgentuuid;
		UUID thread;
		switch (type->sensor) {
		case DDB_PARTICLEFILTER:
			UuidFromString((RPC_WSTR)_T(AgentSensorCooccupancy_UUID), &sAgentuuid);
			break;
		case DDB_SENSOR_SONAR:
			UuidFromString((RPC_WSTR)_T(AgentSensorSonar_UUID), &sAgentuuid);
			break;
		case DDB_SENSOR_CAMERA:
			if (type->phase == 0)		UuidFromString((RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid);
			else						UuidFromString((RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid);
			break;
		case DDB_SENSOR_SIM_CAMERA:
			if (type->phase == 0)		UuidFromString((RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid);
			else						UuidFromString((RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid);
			break;
		default:
			Log.log(0, "SupervisorSLAM::requestAgentSpawn: unknown sensor type!");
			return NULL;
		}
		thread = this->conversationInitiate(SupervisorSLAM_CBR_convRequestAgentSpawn, REQUESTAGENTSPAWN_TIMEOUT, type, sizeof(READING_TYPE));
		if (thread == nilUUID) {
			return NULL;
		}

		Log.log(LOG_LEVEL_VERBOSE, "SupervisorSLAM::requestAgentSpawn: requesting agent %s %d-%d (thread %s)", Log.formatUUID(LOG_LEVEL_VERBOSE, &sAgentuuid), type->sensor, type->phase, Log.formatUUID(LOG_LEVEL_VERBOSE, &thread));

		this->ds.reset();
		this->ds.packUUID(this->getUUID());
		this->ds.packUUID(&sAgentuuid);
		this->ds.packChar(-1); // no instance parameters
		this->ds.packFloat32(0.0f); // affinity
		this->ds.packChar(priority);
		this->ds.packUUID(&thread);
		this->sendMessage(this->hostCon, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length());
		this->ds.unlock();
//	}
	return 0;
}

UUID * SupervisorSLAM::getProcessingAgent( READING_TYPE *type ) {
	int i;

	// see if we already have agents of this type
	mapTypeAgents::iterator iterTA;
	iterTA = this->agents.find( *type );

	if ( iterTA == this->agents.end() || iterTA->second.empty() ) { // we need to spawn an agent of this type
		mapAgentSpawning::iterator iterSpawning;
		iterSpawning = this->agentsSpawning.find( *type );
		if ( iterSpawning != this->agentsSpawning.end() )
			return NULL; // already spawning
	
		if ( STATE(SupervisorSLAM)->SLAMmode == SLAM_MODE_DISCARD ) { // immediately spawn an agent for every host
			this->requestAgentSpawn( type, DDBAGENT_PRIORITY_CRITICAL );
			for ( i = 1; i < STATE(SupervisorSLAM)->hostGroupSize - 1; i++ ) // -1 for hostExclusive 
				this->requestAgentSpawn( type, DDBAGENT_PRIORITY_REDUNDANT_LOW );
			
		} else { // start with a single agent
			this->requestAgentSpawn( type, DDBAGENT_PRIORITY_CRITICAL );
		}

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

		// if we are seeing lots of backlog consider spawning another agent
		if ( STATE(SupervisorSLAM)->SLAMmode != SLAM_MODE_DISCARD && bestReadings > REDUNDANT_AGENT_QUEUE_THRESHOLD ) { // we're falling behind!
			_timeb tb;
			apb->apb_ftime_s( &tb );
			if ( (int)iterTA->second.size() < STATE(SupervisorSLAM)->hostGroupSize 
			  && STATE(SupervisorSLAM)->lastRedundantSpawnRequest.time + REDUNDANT_AGENT_REQUEST_DELAY < tb.time 
			  && STATE(SupervisorSLAM)->lastRedundantSpawnRejected.time + REDUNDANT_AGENT_REJECTED_DELAY < tb.time ) {
				STATE(SupervisorSLAM)->lastRedundantSpawnRequest = tb;

				Log.log( LOG_LEVEL_NORMAL, "SupervisorSLAM::getProcessingAgent: significant backlog (%d), requesting redundant agent", bestReadings );

				this->requestAgentSpawn( type, DDBAGENT_PRIORITY_REDUNDANT_LOW );
			}
		}

		if ( STATE(SupervisorSLAM)->SLAMmode == SLAM_MODE_DISCARD && bestReadings > 0 ) {
			return NULL; // only one reading at a time!
		}

		return bestAgent;
	}
}

int SupervisorSLAM::removeProcessingAgent( UUID *agent ) {
	
	Log.log( LOG_LEVEL_NORMAL, "SupervisorSLAM::removeProcessingAgent: removing agent and redistributing queue %s", Log.formatUUID(LOG_LEVEL_NORMAL,agent) );

	// find agent and type
	mapTypeAgents::iterator iTA;
	std::list<UUID>::iterator iI;
	for ( iTA = this->agents.begin(); iTA != this->agents.end(); iTA++ ) {
		for ( iI = iTA->second.begin(); iI != iTA->second.end(); iI++ ) {
			if ( *iI == *agent )
				break;
		}
		if ( iI != iTA->second.end() ) 
			break;
	}

	if ( iTA == this->agents.end() ) 
		return 1; // agent not found

	// clear agent
	iTA->second.erase( iI );

	// redistribute queue
	std::list<READING_QUEUE>::iterator iR;
	if ( !this->agentQueue[*agent].empty() ) {
		for ( iR = this->agentQueue[*agent].begin(); iR != this->agentQueue[*agent].end(); iR++ ){ 
			this->newSensorReading( &iR->type, &iR->uuid, &iR->tb, iR->attempt - 1 );
		}
	}

	// clean up
	this->agentsStatus.erase( *agent );
	this->agentsProcessing.erase( *agent );
	this->agentQueue.erase( *agent );

	this->backup(); // agents

	return 0;
}

int SupervisorSLAM::nextSensorReading( UUID *agent ) {
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

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::nextSensorReading: assign next reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rqNext.uuid) );

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

int SupervisorSLAM::assignSensorReading( UUID *agent, READING_QUEUE *rq ) {
	
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
		Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::assignSensorReading: agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
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
		Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::assignSensorReading: queue reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
		// add to agent queue
		this->agentQueue[*agent].push_front( *rq );
	}

	return 0;
}

int SupervisorSLAM::doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success ) {
	READING_QUEUE rq;
	RPC_STATUS Status;

	Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::doneSensorReading: agent %s sensor %s success %d", Log.formatUUID(LOG_LEVEL_VERBOSE,uuid), Log.formatUUID(LOG_LEVEL_VERBOSE,sensor), success );
	
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
		
	} else	if ( success == -1 ) { // permanent failure
		rq.type.phase++;
		
		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
		
	} else { // permanent failure
		// assign reading again
		this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
	}

	return 0;
}

int SupervisorSLAM::newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt ) {
	READING_QUEUE rq;
	UUID *agent;

	Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::newSensorReading: type %d-%d id %s attempt %d", type->sensor, type->phase, Log.formatUUID(LOG_LEVEL_VERBOSE,uuid), attempt );

	if ( attempt > 5 ) // give up on this reading
		return 0;

	rq.type = *type;
	rq.uuid = *uuid;
	rq.tb = *tb;
	rq.attempt = attempt + 1;

	// get appropriate processing agent
	agent = this->getProcessingAgent( type );

	if ( STATE(SupervisorSLAM)->SLAMmode == SLAM_MODE_DISCARD && !agent ) {
		Log.log( LOG_LEVEL_VERBOSE, "SupervisorSLAM::newSensorReading: all processing agents busy, discarding reading" );
		return 0; // discard!
	}

	if ( !agent ) {
		// assign reading to wait queue
		this->typeQueue[*type].push_front( rq );
	} else {
		// assign reading to agent
		this->assignSensorReading( agent, &rq );
	}

	// check if we should backup
	this->backupReadingsCount--;
	if ( this->backupReadingsCount == 0 ) {
		this->backupReadingsCount = BACKUP_READINGS_THRESHOLD;
		this->backup();
	}

	return 0;
}

int SupervisorSLAM::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

	int type;
	READING_TYPE reading;
	char evt;
	_timeb tb;

	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	if ( evt == DDBE_WATCH_ITEM ) {
		if ( this->agentsProcessing.find(uuid) != this->agentsProcessing.end() ) { // one of our processing agents
			// get status
			UUID thread = this->conversationInitiate( SupervisorSLAM_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	} else if ( evt == DDBE_WATCH_TYPE ) {
		if ( type & DDB_HOST_GROUP ) {
			// get group size
			UUID thread = this->conversationInitiate( SupervisorSLAM_CBR_convHostGroupSize, DDB_REQUEST_TIMEOUT );
			sds.reset();
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RHOSTGROUPSIZE, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type & DDB_SENSORS && evt == DDBE_SENSOR_UPDATE ) {
		tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
		lds.unlock();
		reading.sensor = type;
		reading.phase = 0;
		this->newSensorReading( &reading, &uuid, &tb );
	} else if ( type & DDB_PARTICLEFILTER && evt == DDBE_PF_PREDICTION ) {
		tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
		char nochange = lds.unpackChar();
		lds.unlock();
		if ( !nochange ) {
			reading.sensor = type;
			reading.phase = 0;
			this->newSensorReading( &reading, &uuid, &tb );
		}
	} else if ( type == DDB_AGENT && this->agentsStatus.find(uuid) != this->agentsStatus.end() ) { // our agent
		if ( evt == DDBE_AGENT_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
				
				// get status
				UUID thread = this->conversationInitiate( SupervisorSLAM_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &uuid );
				sds.packInt32( DDBAGENTINFO_RSTATUS );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		} else if ( evt == DDBE_REM ) { // agent removed
			this->removeProcessingAgent( &uuid );
		}
	} else if ( type == DDB_HOST_GROUP ) {
		if ( evt == DDBE_UPDATE ) {
			int groupSize = lds.unpackInt32();
			this->updateHostGroup( groupSize );
		}
	}
	lds.unlock();
	
	return 0;
}

int SupervisorSLAM::updateHostGroup( int groupSize ) {
	int i;
	int oldSize = STATE(SupervisorSLAM)->hostGroupSize;

	STATE(SupervisorSLAM)->hostGroupSize = groupSize;

	if ( STATE(SupervisorSLAM)->SLAMmode == SLAM_MODE_DISCARD && oldSize < STATE(SupervisorSLAM)->hostGroupSize ) { // group got bigger, spawn additional agents
		std::map<READING_TYPE,int,READING_TYPEless> agentCount;
		std::map<READING_TYPE,int,READING_TYPEless>::iterator iC;
		mapTypeAgents::iterator iA;
		mapAgentSpawning::iterator iS;
		for ( iA = this->agents.begin(); iA != this->agents.end(); iA++ ) {
			agentCount[iA->first] = (int)iA->second.size();
		}
		for ( iS = this->agentsSpawning.begin(); iS != this->agentsSpawning.end(); iS++ ) {
			agentCount[iS->first] += iS->second;
		}

		for ( iC = agentCount.begin(); iC != agentCount.end(); iC++ ) {
			for ( i = iC->second; i < STATE(SupervisorSLAM)->hostGroupSize - 1; i++ ) { // -1 for hostExclusive
				this->requestAgentSpawn( (READING_TYPE *)&iC->first, DDBAGENT_PRIORITY_REDUNDANT_LOW );
			}
		}
	}

	Log.log( 0, "SupervisorSLAM::updateHostGroup: size %d", STATE(SupervisorSLAM)->hostGroupSize );

	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int SupervisorSLAM::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
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
	case SupervisorSLAM_MSGS::MSG_ADD_MAP:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->addMap( &uuid );
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool SupervisorSLAM::convRequestAgentSpawn( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	READING_TYPE type = *(READING_TYPE *)conv->data;
	
	this->agentsSpawning[type]--; // spawn is over for good or ill

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "SupervisorSLAM::convRequestAgentSpawn: request spawn timed out (thread %s)", Log.formatUUID(0,&conv->thread) );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	if ( lds.unpackBool() ) { // succeeded
		UUID uuid;

		lds.unpackUUID( &uuid );
		lds.unlock();

		Log.log( 0, "SupervisorSLAM::convRequestAgentSpawn: spawn succeeded %d-%d (%s) (thread %s)", type.sensor, type.phase, Log.formatUUID(0, &uuid), Log.formatUUID(0,&conv->thread) );

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
		lds.packUUID( &STATE(SupervisorSLAM)->mapId );
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
	
		apb->apb_ftime_s( &STATE(SupervisorSLAM)->lastRedundantSpawnRejected ); // unset

		Log.log( 0, "SupervisorSLAM::convRequestAgentSpawn: spawn failed %d-%d (thread %s)", type.sensor, type.phase, Log.formatUUID(0,&conv->thread) );
	}

	return 0;
}

bool SupervisorSLAM::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorSLAM::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID agent = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		if ( lds.unpackInt32() != DDBAGENTINFO_RSTATUS ) {
			lds.unlock();
			return 0; // what happened here?
		}

		this->agentsStatus[agent] = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "SupervisorSLAM::convAgentInfo: status %d", this->agentsStatus[agent] );

		if ( this->agentsStatus[agent] != DDBAGENT_STATUS_READY
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_FREEZING
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_FROZEN
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_THAWING ) {
			this->agentsProcessing[agent] = false; // give up
		}

		// try do process a reading
		this->nextSensorReading( &agent );

	} else {
		lds.unlock();

		Log.log( 0, "SupervisorSLAM::convAgentInfo: request failed %d", result );
	}

	return 0;
}

bool SupervisorSLAM::convHostGroupSize( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorSLAM::convHostGroupSize: request timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		int groupSize = lds.unpackInt32();
		lds.unlock();

		this->updateHostGroup( groupSize );
		
	} else {
		lds.unlock();

		Log.log( 0, "SupervisorSLAM::convHostGroupSize: request failed", result );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int SupervisorSLAM::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int SupervisorSLAM::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	SupervisorSLAM::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(SupervisorSLAM);

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

int	SupervisorSLAM::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(SupervisorSLAM);

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

int SupervisorSLAM::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		// get group size
		UUID thread = this->conversationInitiate( SupervisorSLAM_CBR_convHostGroupSize, DDB_REQUEST_TIMEOUT );
		lds.reset();
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RHOSTGROUPSIZE, lds.stream(), lds.length() );
		lds.unlock();

		// get processing agent status and tell them to reset
		std::map<UUID,bool,UUIDless>::iterator iS;
		for ( iS = this->agentsProcessing.begin(); iS != this->agentsProcessing.end(); iS++ ) {
			UUID thread = this->conversationInitiate( SupervisorSLAM_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, (void *)&iS->first, sizeof(UUID) );
			lds.reset();
			lds.packUUID( (UUID *)&iS->first );
			lds.packInt32( DDBAGENTINFO_RSTATUS );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, lds.stream(), lds.length() );
			lds.unlock();

/*			READING_QUEUE rq;
			rq.type.sensor = DDB_INVALID;
			rq.type.phase = 0;
			rq.uuid = nilUUID;
			this->agentQueue[iS->first].push_front( rq );  // tells agent to discard current reading
*/
			this->agentsProcessing[iS->first] = false; // we're starting fresh
		}
	}

	return 0;
}

int SupervisorSLAM::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(SupervisorSLAM)->mapId );

	// agents
	mapTypeAgents::iterator iA;
	for ( iA = this->agents.begin(); iA != this->agents.end(); iA++ ) {
		ds->packBool( 1 );
		ds->packData( (void *)&iA->first, sizeof(READING_TYPE) );
		_WRITE_STATE_LIST( UUID, &iA->second );
	}
	ds->packBool( 0 );

	// agentsProcessing
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

	return AgentBase::writeBackup( ds );
}

int SupervisorSLAM::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(SupervisorSLAM)->mapId );

	// agents
	READING_TYPE keyA;
	while ( ds->unpackBool() ) {
		keyA = *(READING_TYPE *)ds->unpackData( sizeof(READING_TYPE) );
		_READ_STATE_LIST( UUID, &this->agents[keyA] ); // unpack agents
	}

	// agentsProcessing
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
	
	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	SupervisorSLAM *agent = (SupervisorSLAM *)vpAgent;

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
	SupervisorSLAM *agent = new SupervisorSLAM( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	SupervisorSLAM *agent = new SupervisorSLAM( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CSupervisorSLAMDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CSupervisorSLAMDLL, CWinApp)
END_MESSAGE_MAP()

CSupervisorSLAMDLL::CSupervisorSLAMDLL() {}

// The one and only CSupervisorSLAMDLL object
CSupervisorSLAMDLL theApp;

int CSupervisorSLAMDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CSupervisorSLAMDLL ---\n"));
  return CWinApp::ExitInstance();
}