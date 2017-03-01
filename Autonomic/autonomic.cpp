// Autonomic.cpp 
//

#include "stdafx.h"
#include "autonomic.h"


int apCmp( spAddressPort A, spAddressPort B ) {
	sAddressPort _A;
	sAddressPort _B;

	memset( &_A, 0, sizeof(sAddressPort) );
	memset( &_B, 0, sizeof(sAddressPort) );

	if ( !strcmp( A->address, "-1" ) ) sprintf_s( _A.address, sizeof(_A.address), "127.0.0.1" );
	else strcpy_s( _A.address, sizeof(_A.address), A->address );
	strcpy_s( _A.port, sizeof(_A.port), A->port );

	if ( !strcmp( B->address, "-1" ) ) sprintf_s( _B.address, sizeof(_B.address), "127.0.0.1" );
	else strcpy_s( _B.address, sizeof(_B.address), B->address );
	strcpy_s( _B.port, sizeof(_B.port), B->port );

	return memcmp( &_A, &_B, sizeof(sAddressPort) - sizeof(spAddressPort) );
}

//*****************************************************************************
// AgentBase

//-----------------------------------------------------------------------------
// Constructor	
AgentBase::AgentBase( spAddressPort ap, UUID *agentId, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	
	this->CON_GROUP_HIGH = CON_GROUP_COMMON;

	UuidCreateNil( &this->nilUUID );

	// allocate state
	this->state = (State*)malloc( sizeof(State) );

	// uuid
	if ( agentId == NULL )
		UuidCreate( &state->uuid );
	else
		state->uuid = *agentId;

	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentBase_UUID), &typeId );
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentBase" );
	STATE(AgentBase)->agentType.uuid = typeId;
	STATE(AgentBase)->agentType.instance = -1;

	// setup playback mode
	this->apb = NULL;
	if ( playbackMode == PLAYBACKMODE_RECORD ) {
		char playbackFileFull[512];
		RPC_WSTR rpc_wstr;
		UuidCreate( &this->agentplaybackUUID );
		UuidToString( &this->agentplaybackUUID, &rpc_wstr );
		sprintf_s( playbackFileFull, sizeof(playbackFileFull), "%s\\x %ls.apb", logDirectory, rpc_wstr );
		
		this->apb = new AgentPlayback( playbackMode, playbackFileFull );

		apb->apbWrite( this->getUUID(), sizeof(UUID) );
		apb->apbWrite( &STATE(AgentBase)->agentType.uuid, sizeof(UUID) );
	} else if ( playbackMode == PLAYBACKMODE_PLAYBACK ) { 
		UuidCreateNil( &this->agentplaybackUUID );
		this->apb = new AgentPlayback( playbackMode, playbackFile );

		apb->apbRead( &state->uuid, sizeof(UUID) );
		apb->apbRead( &typeId, sizeof(UUID) );
		
		RPC_STATUS Status;
		if ( UuidCompare( &typeId, &STATE(AgentBase)->agentType.uuid, &Status ) )
			throw "Wrong agent type for playback";
	} else { // off
		UuidCreateNil( &this->agentplaybackUUID );
		this->apb = new AgentPlayback( playbackMode, NULL );
	}

	UuidCreateNil( &STATE(AgentBase)->parentId );

	STATE(AgentBase)->configured = false;
	STATE(AgentBase)->started = false;
	STATE(AgentBase)->stopFlag = false;
	STATE(AgentBase)->gracefulExit = false;
	STATE(AgentBase)->simulateCrash = false;

	STATE(AgentBase)->noCrash = true; // sub classes should turn this off if they can handle it

	this->frozen = false;

	if ( ap != NULL ) {
		this->isHost = false;
		memcpy( &this->hostAP, ap, sizeof(this->hostAP) );
		this->hostCon = NULL;
//		this->spawnTicket = *ticket;
	} else {
		this->isHost = true;
	}

	Log.setAgentPlayback( this->apb );
	this->logLevel = logLevel;
	strncpy_s( this->logDirectory, sizeof(this->logDirectory), logDirectory, sizeof(this->logDirectory) );

	// network
	this->addr_result = NULL;
	this->addr_ptr = NULL;
	ZeroMemory( &this->addr_hints, sizeof(this->addr_hints) );

	// connections
	ZeroMemory( this->group, sizeof(this->group) );

	STATE(AgentBase)->haveReturn = 0;

	this->firstTimeout = NULL;

	STATE(AgentBase)->conversationLast = 0;

	STATE(AgentBase)->localMessageTimeout = nilUUID;

	this->_atomicMessageDeliveryId = nilUUID;

	ZeroMemory( &this->cpuUsageLastUpdate, sizeof(_timeb) );
	this->cpuUsageTotalUsed = 0;
	this->cpuUsageTotalElapsed = 0;

	this->recoveryInProgress = false;

	// Prepare callbacks
	this->callback[AgentBase_CBR_cbSimulateCrash] = NEW_MEMBER_CB(AgentBase,cbSimulateCrash);
	this->callback[AgentBase_CBR_cbUpdateCpuUsage] = NEW_MEMBER_CB(AgentBase,cbUpdateCpuUsage);
	this->callback[AgentBase_CBR_cbUpdateConnectionReliability] = NEW_MEMBER_CB(AgentBase,cbUpdateConnectionReliability);
	this->callback[AgentBase_CBR_cbDoRetry] = NEW_MEMBER_CB(AgentBase,cbDoRetry);
	this->callback[AgentBase_CBR_cbConversationTimeout] = NEW_MEMBER_CB(AgentBase,cbConversationTimeout);
	this->callback[AgentBase_CBR_cbConversationErase] = NEW_MEMBER_CB(AgentBase,cbConversationErase);
	this->callback[AgentBase_CBR_cbDelayedMessage] = NEW_MEMBER_CB(AgentBase,cbDelayedMessage);
	this->callback[AgentBase_CBR_cbFDSubject] = NEW_MEMBER_CB(AgentBase,cbFDSubject);
	this->callback[AgentBase_CBR_cbFDObserver] = NEW_MEMBER_CB(AgentBase,cbFDObserver);
	this->callback[AgentBase_CBR_cbWatchHostConnection] = NEW_MEMBER_CB(AgentBase,cbWatchHostConnection);
	this->callback[AgentBase_CBR_cbAtomicMessageTimeout] = NEW_MEMBER_CB(AgentBase,cbAtomicMessageTimeout);
	this->callback[AgentBase_CBR_cbLocalMessage] = NEW_MEMBER_CB(AgentBase,cbLocalMessage);
	this->callback[AgentBase_CBR_cbNotificationFilterExpiry] = NEW_MEMBER_CB(AgentBase,cbNotificationFilterExpiry);

}

//-----------------------------------------------------------------------------
// Destructor
AgentBase::~AgentBase() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	// clean up atomic messages
	while ( !this->atomicMsgs.empty() ) {
		this->_atomicMessageCheckCleanup( &this->atomicMsgs.begin()->second, true );
	}
	
	this->atomicMessageHighestOrder.clear();

	std::map<UUID, std::list<AtomicMessage>, UUIDless>::iterator iQ;
	std::list<AtomicMessage>::iterator iMq;
	for ( iQ = this->atomicMsgsOrderedQueue.begin(); iQ != this->atomicMsgsOrderedQueue.end(); iQ++ ) {
		if ( iQ->second.size() )
			iQ->second.pop_front(); // first one is always handled in atomicMsgs above

		while ( !iQ->second.empty() ) {		
			if ( iQ->second.front().len ) {
				this->freeDynamicBuffer( iQ->second.front().dataRef );
			}
			iQ->second.pop_front();
		}
	}
	this->atomicMsgsOrderedQueue.clear();

	// clean up local messages
	std::list<LocalMessage>::iterator iM;

	for ( iM = this->localMessageQueue.begin(); iM != this->localMessageQueue.end(); iM++ ) {
		// clean up
		if ( iM->len )
			this->freeDynamicBuffer( iM->dataRef );
	}

	this->localMessageQueue.clear();

	// clear conversations
	while ( !this->conversations.empty() ) {
		this->_conversationEnd( (spConversation)this->conversations.begin()->second, true );
	}

	// clean up delayed messages
	std::map<UUID, char, UUIDless>::iterator iBR;
	for ( iBR = this->delayedMessageBufRefs.begin(); iBR != this->delayedMessageBufRefs.end(); iBR++ ) {
		this->freeDynamicBuffer( iBR->first );
	}
	this->delayedMessageBufRefs.clear();

	// close connections
	mapConnection::iterator iterCon = this->connection.begin();
	while ( this->connection.size() ) {
		iterCon = this->connection.begin();

		if ( iterCon->second->state == CON_STATE_LISTENING )
			this->closeListener( iterCon->second );

		else {
			this->closeConnection( iterCon->second );
			this->conDelete( iterCon->second );
		}
	}

	// clear timeouts
	spTimeoutEvent iterTE = this->firstTimeout;
	spTimeoutEvent nextTE;
	while ( iterTE ) {
		nextTE = iterTE->next;
		if ( iterTE->dataLen != 0 ) {
			free( iterTE->data );
		}
		free( iterTE );
		iterTE = nextTE;
	}

	free( this->state );

	Log.log( 0, "AgentBase::~AgentBase: agent shutdown" );


	// clean up callbacks
	mapCallback::iterator iCb;
	for ( iCb = this->callback.begin(); iCb != this->callback.end(); iCb++ ) {
		delete iCb->second;
	}
	this->callback.clear();

	// check dynamic buffers but don't clean them up
	if ( this->stateBuffers.size() ) {
		Log.log( 0, "AgentBase::~AgentBase: Warning! Unclean dynamic state buffers remaining! (i.e. memory leak)" );
	}

	// clean up playback mode
	if ( this->apb )
		delete this->apb;
}

//-----------------------------------------------------------------------------
// Simulate Crash

int AgentBase::simulateCrash() {

	Log.log( 0, "AgentBase::simulateCrash: simulating crash by becomeing unresponsive" );

	STATE(AgentBase)->simulateCrash = false;
	STATE(AgentBase)->stopFlag = true;

	apb->apbSleep( 30000 ); // sleep for 30 seconds

	// force close all connections
	mapConnection::iterator iterCon = this->connection.begin();
	while ( this->connection.size() ) {
		iterCon = this->connection.begin();

		if ( iterCon->second->state == CON_STATE_LISTENING )
			this->closeListener( iterCon->second );

		else {
			this->closeConnection( iterCon->second );
			this->conDelete( iterCon->second );
		}
	}

	Log.log( 0, "AgentBase::simulateCrash: stopping" );

	return 0;
}


//-----------------------------------------------------------------------------
// Configure
 
int AgentBase::configure() {

	Log.log( 0, "Agent UUID %s", Log.formatUUID( 0, this->getUUID() ) );
	Log.log( 0, "AgentPlayback UUID %s", Log.formatUUID( 0, &this->agentplaybackUUID ) );

	if ( !this->isHost ) {
		this->hostCon = this->openConnection( &this->hostAP, NULL, 30 );
		if ( !this->hostCon ) {
			Log.log( 0, "AgentBase::configure: failed to open connection to host" );
			return 1;
		}
		
		if ( -1 == this->watchConnection( this->hostCon, AgentBase_CBR_cbWatchHostConnection ) ) {
			Log.log( 0, "AgentBase::configure: failed to add watcher for host connection" );
			return 1;
		}
	}

	STATE(AgentBase)->configured = true;

	return 0;
}

int AgentBase::setParentId( UUID *id ) {
	STATE(AgentBase)->parentId = *id;
	return 0;
}

int AgentBase::setInstance( char instance ) {
	STATE(AgentBase)->agentType.instance = instance;
	return 0;
}

int AgentBase::calcLifeExpectancy() {

if (STATE(AgentBase)->noCrash || STATE(AgentBase)->stabilityTimeMin == -1)
return 0; // no crashing

float Tmin = STATE(AgentBase)->stabilityTimeMin;
float Tdif = STATE(AgentBase)->stabilityTimeMax - Tmin;
float y = (float)apb->apbUniform01();

//// calculate lifetime based on y = 1 - exp(-(x-Tmin-Tdif)^2/(Tdif/3)^2)
//float b = 2*Tmin + 2*Tdif;
//float c = log(1-y)*Tdif*Tdif/9 + Tmin*Tmin + Tdif*Tdif + 2*Tmin*Tdif;
//float x = (b - sqrt(b*b - 4*c))/2;

// calculate lifetime based on straight line
float x = Tmin + Tdif*y;

Log.log(0, "AgentBase::calcLifeExpectancy: setting crash for T - %f minutes", x);

UUID crashId = this->addTimeout((int)(x * 60 * 1000), AgentBase_CBR_cbSimulateCrash);

return 0;
}

int AgentBase::parseMF_HandleStability(float timeMin, float timeMax) {

	STATE(AgentBase)->stabilityTimeMin = timeMin;
	STATE(AgentBase)->stabilityTimeMax = timeMax;

	return 0;
}

int AgentBase::parseMF_Agent(FILE *fp, AgentType *agentType) {
	char nameBuf[256], *writeHead;
	WCHAR uuidBuf[64];

	agentType->instance = -1;

	writeHead = nameBuf;
	while (EOF != (*writeHead = fgetc(fp))
		&& '=' != *writeHead
		&& '\n' != *writeHead
		&& ';' != *writeHead
		&& '/' != *writeHead)
		writeHead++;
	if (*writeHead == ';') {
		while (EOF != (*writeHead = fgetc(fp))
			&& '\n' != *writeHead)
			writeHead++;
	}
	if (*writeHead == EOF)
		return 1;
	if (*writeHead == '\n')
		return 0;
	if (*writeHead == '/') { // scan instance num
		int instanceInt;
		fscanf_s(fp, "%d=", &instanceInt);
		agentType->instance = (char)instanceInt;
	}

	*writeHead = '\0';

	*uuidBuf = _T('\0');
	fscanf_s(fp, "%ls\n", uuidBuf, 64);
	if (UuidFromString((RPC_WSTR)uuidBuf, &agentType->uuid) != RPC_S_OK) {
		Log.log(0, "AgentBase::parseMF_Agent: bad uuid for %s", nameBuf);
		return 1;
	}

	return 0;
}

int AgentBase::parseMissionFile(char *misFile) {
	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;

	if (fopen_s(&fp, misFile, "r")) {
		Log.log(0, "AgentBase::parseMissionFile: failed to open %s", misFile);
		return 1; // couldn't open file
	}

	Log.log(0, "AgentBase::parseMissionFile: parsing %s", misFile);
	i = 0;
	while (1) {
		ch = fgetc(fp);

		if (ch == EOF) {
			break;
		}
		else if (ch == '\n') {
			keyBuf[i] = '\0';

			if (i == 0)
				continue; // blank line

			if (!strncmp(keyBuf, "[map_options]", 64)) {
				int mapReveal = 0;
				int mapRandom = 0;
				int numObstacles;
				int numTargets;
				if (fscanf_s(fp, "mapReveal=%d\n", &mapReveal) != 1) { // 0 - Unexplored, 1 - Explored 
					Log.log(0, "AgentBase::parseMissionFile: badly formatted mapReveal");
					break;
				}
				if (fscanf_s(fp, "mapRandom=%d\n", &mapRandom) != 1) { // 0 - Layout from file, 1 - Random 
					Log.log(0, "AgentBase::parseMissionFile: badly formatted mapRandom");
					break;
				}
				if (mapRandom){
					if (fscanf_s(fp, "	numObstacles=%d\n", &numObstacles) != 1) { 
						Log.log(0, "AgentBase::parseMissionFile: badly formatted numObstacles");
						break;
						}
				}
				if (mapRandom) {
					if (fscanf_s(fp, "	numTargets=%d\n", &numTargets) != 1) {
						Log.log(0, "AgentBase::parseMissionFile: badly formatted numTargets");
						break;
					}
				}
				this->parseMF_HandleMapOptions(mapReveal, mapRandom);


			} else if (!strncmp(keyBuf, "[options]", 64)) {
				int SLAMmode;
				if (fscanf_s(fp, "SLAMmode=%d\n", &SLAMmode) != 1) { // 0 - JCSLAM, 1 - Discard, 2 - Delay
					Log.log(0, "AgentBase::parseMissionFile: badly formatted SLAMmode");
					break;
				}

				this->parseMF_HandleOptions( SLAMmode );

			} else if ( !strncmp( keyBuf, "[stability]", 64 ) ) {
				float timeMin, timeMax;
				if ( fscanf_s( fp, "timeminmax=%f %f\n", &timeMin, &timeMax ) != 2 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted stability" );
					break;
				}

				this->parseMF_HandleStability( timeMin, timeMax );

			} else if ( !strncmp( keyBuf, "[agent]", 64 ) ) {
				AgentType agentType;
				if ( this->parseMF_Agent( fp, &agentType ) )
					break;

				this->parseMF_HandleAgent( &agentType );

			} else if ( !strncmp( keyBuf, "[avatar]", 64 ) ) {
				AgentType agentType;
				if ( this->parseMF_Agent( fp, &agentType ) )
					break;

				char fileName[256];
				if ( fscanf_s( fp, "file=%s\n", fileName, 256 ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted file name (avatar)" );
					break;
				}

				float x, y, r;
				if ( fscanf_s( fp, "pose=%f %f %f\n", &x, &y, &r ) != 3 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted pose (avatar)" );
					break;
				}

				float startTime, duration;
				int retireMode;
				if ( fscanf_s( fp, "timecard=%f %f %d\n", &startTime, &duration, &retireMode ) != 3 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted timecard (avatar)" );
					break;
				}

				this->parseMF_HandleAvatar( &agentType, fileName, x, y, r, startTime, duration, (char)retireMode );

			} else if ( !strncmp( keyBuf, "[mission_region]", 64 ) ) {
				DDBRegion region;
				if ( fscanf_s( fp, "region=%f %f %f %f\n", &region.x, &region.y, &region.w, &region.h ) != 4 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted region (mission)" );
					break;
				}
				this->parseMF_HandleMissionRegion( &region );

			} else if ( !strncmp( keyBuf, "[forbidden_region]", 64 ) ) {
				DDBRegion region;
				if ( fscanf_s( fp, "region=%f %f %f %f\n", &region.x, &region.y, &region.w, &region.h ) != 4 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted region (forbidden)" );
					break;
				}

				this->parseMF_HandleForbiddenRegion( &region );

			} else if ( !strncmp( keyBuf, "[collection_region]", 64 ) ) {
				DDBRegion region;
				if ( fscanf_s( fp, "region=%f %f %f %f\n", &region.x, &region.y, &region.w, &region.h ) != 4 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted region (collection)" );
					break;
				}

				this->parseMF_HandleCollectionRegion( &region );

			} else if ( !strncmp( keyBuf, "[travel_target]", 64 ) ) {
				float x, y, r;
				int useRotation;
				if ( fscanf_s( fp, "target=%f %f %f %d\n", &x, &y, &r, &useRotation ) != 4 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted travel target" );
					break;
				}

				this->parseMF_HandleTravelTarget( x, y, r, useRotation > 0 );

			} else if ( !strncmp( keyBuf, "[landmark_file]", 64 ) ) {
				//Log.log(0, "AgentBase::parseMissionFile: DEBUG::LANDMARK FILE");

				char fileName[256];
				if ( fscanf_s( fp, "file=%[^\n]s", fileName, 256 ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted file name (landmark)" );
					break;
				}
				//Log.log(0, "AgentBase::parseMissionFile: DEBUG::LANDMARK FILE:: Scanning completed");
				//Log.log(0, "AgentBase::parseMissionFile: DEBUG::LANDMARK FILE:: File name is: %s \n", fileName);
				this->parseMF_HandleLandmarkFile( fileName );

			} else if ( !strncmp( keyBuf, "[path_file]", 64 ) ) {
				//Log.log(0, "AgentBase::parseMissionFile: DEBUG::PATH FILE");

				char fileName[256];
				if ( fscanf_s( fp, "file=%[^\n]s", fileName, 256 ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted file name (path)" );
					break;
				}

				this->parseMF_HandlePathFile( fileName );

			} else if ( !strncmp( keyBuf, "[offline_SLAM]", 64 ) ) {
				int SLAMmode, particleNum;
				float readingProcessingRate;
				int processingSlots;
				char logPath[MAX_PATH];
				if ( fscanf_s( fp, "SLAMmode=%d\n", &SLAMmode ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted SLAMmode (offline_SLAM)" );
					break;
				}
				if ( fscanf_s( fp, "particleNum=%d\n", &particleNum ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted particleNum (offline_SLAM)" );
					break;
				}
				if ( fscanf_s( fp, "readingProcessingRate=%f\n", &readingProcessingRate ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted readingProcessingRate (offline_SLAM)" );
					break;
				}
				if ( fscanf_s( fp, "processingSlots=%d\n", &processingSlots ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted processingSlots (offline_SLAM)" );
					break;
				}
				if ( fscanf_s( fp, "logPath=%s\n", logPath, MAX_PATH ) != 1 ) {
					Log.log( 0, "AgentBase::parseMissionFile: badly formatted logPath (offline_SLAM)" );
					break;
				}

				this->parseMF_HandleOfflineSLAM( SLAMmode, particleNum, readingProcessingRate, processingSlots, logPath );

			}
			else if (!strncmp(keyBuf, "[learning]", 64)) {
				int individualLearning = 0;
				int teamLearning = 0;
				int individualLearningParseResult;
				int teamLearningParseResult;
				individualLearningParseResult = fscanf_s(fp, "individual=%d\n", &individualLearning);
				Log.log(0, "AgentBase::parseMissionFile: individual learning is %d, team learning is %d", individualLearning, teamLearning);
				teamLearningParseResult = fscanf_s(fp, "team=%d\n", &teamLearning);
				Log.log(0, "AgentBase::parseMissionFile: individual learning is %d, team learning is %d", individualLearning, teamLearning);
				
				if (individualLearningParseResult != 1) {
					Log.log(0, "AgentBase::parseMissionFile: badly formatted learning (individual learning)");
					break;
				}
				if (teamLearningParseResult != 1) {
					Log.log(0, "AgentBase::parseMissionFile: badly formatted learning (team learning)");
					break;
				}
				int runCount;
				this->parseMF_HandleLearning(individualLearning, teamLearning);

				Log.log(0, "AgentBase::parseMissionFile: individual learning is %d, team learning is %d", individualLearning, teamLearning);
	
			}



			else { // unknown key
				fclose( fp );
				Log.log( 0, "AgentBase::parseMissionFile: unknown key: %s", keyBuf );
				return 1;
			}
			
			i = 0;
		} else {
			keyBuf[i] = ch;
			i++;
		}
	}

	fclose( fp );
	return 0;
}


//-----------------------------------------------------------------------------
// Start

int AgentBase::start( char *missionFile ) {

	if ( STATE(AgentBase)->started ) {
		Log.log( 0, "AgentBase::start: Already started!" );
		return 1;
	}

	if ( !STATE(AgentBase)->configured ) {
		Log.log( 0, "AgentBase::start: Not configured" );
		return 1;
	}

	// handle mission file
	char missionBuf[MAX_PATH];
	if ( missionFile ) {
		strcpy_s( missionBuf, sizeof(missionBuf), missionFile );
	} else {
		missionBuf[0] = 0;
	}
	apb->apbString( missionBuf, sizeof(missionBuf) );

	if ( strlen(missionBuf) > 0 ) {
		strcpy_s( STATE(AgentBase)->missionFile, 256, missionBuf );
		Log.log( 0, "AgentBase::start: parsing mission file %s", missionBuf );
		this->parseMissionFile( missionBuf );
	}

	Log.log( 0, "AgentBase::start: Starting!" );

	this->calcLifeExpectancy();

/*	if ( !this->isHost ) {
		// register agent with DDB
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packUUID( this->getParentId() );
		this->ds.packData( &STATE(AgentBase)->agentType, sizeof(AgentType) );
		this->sendMessage( this->hostCon, MSG_DDB_ADDAGENT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}
*/
	UUID id = this->addTimeout( CON_RELIABILITY_INTERVAL, AgentBase_CBR_cbUpdateConnectionReliability );
	if ( id == nilUUID ) {
		Log.log( 0, "AgentBase::start: addTimeout failed" );
		return 1;
	}

	id = this->addTimeout( CPU_USAGE_INTERVAL, AgentBase_CBR_cbUpdateCpuUsage );
	if ( id == nilUUID ) {
		Log.log( 0, "AgentBase::start: addTimeout failed" );
		return 1;
	}
	cbUpdateCpuUsage( NULL ); // run once to get started

	STATE(AgentBase)->started = true;

	// make sure we have at least one backup
	if ( !this->isHost )
		this->backup();

	return 0;
}

//-----------------------------------------------------------------------------
// Stop

int AgentBase::stop() {
	STATE(AgentBase)->started = false;

	// report cpu usage
#ifdef _DEBUG
	char *debugmode = " DEBUG";
#else
	char *debugmode = "";
#endif

	Log.log( 0, "AgentBase::stop: average cpu usage %%%f%s", 100*this->getCpuUsage(), debugmode );

	return 0;
}

int AgentBase::prepareStop() {
	STATE(AgentBase)->stopFlag = true;

	return 0;
}

//-----------------------------------------------------------------------------
// Step

int AgentBase::step() {
	int iResult;
	int err;
	int errlen = sizeof(err);
	struct fd_set fd_read, fd_write, fd_except;
	struct timeval *timeout = NULL;
	_timeb t;
	
	mapConnection::iterator iterCon;
	spConnection con;

	if ( STATE(AgentBase)->stopFlag ) { // we are trying to stop, only process write and except
		
		memset( &fd_read, 0, sizeof(fd_read) );
		memcpy( &fd_write, &this->group[CON_GROUP_WRITE], sizeof(fd_write) );
		memcpy( &fd_except, &this->group[CON_GROUP_EXCEPT], sizeof(fd_except) );

	} else { // process normally (timeouts, read, write, except)

		if ( this->firstTimeout ) {
			apb->apb_ftime_s( &t );
			int dt = (int)( (this->firstTimeout->timeout.time - t.time)*1000 + this->firstTimeout->timeout.millitm - t.millitm );
			if ( dt < 1 ) dt = 1; // safety

			dt += 1; // sleep an extra millisecond to ensure that the timeout will actually be up when we check

			if ( this->group[CON_GROUP_READ].fd_count == 0 
			  && this->group[CON_GROUP_WRITE].fd_count == 0 
			  && this->group[CON_GROUP_EXCEPT].fd_count == 0 ) { // no sockets to process, so just sleep
				apb->apbSleep( dt );
				
				this->checkTimeouts(); // process any outstanding timeouts
				return STATE(AgentBase)->stopFlag;
			}
			// prepare timeval struct for select()
			this->timeVal.tv_sec = dt / 1000;
			this->timeVal.tv_usec = (dt % 1000) * 1000;
			timeout = &this->timeVal;
		} else if ( this->group[CON_GROUP_READ].fd_count == 0 
		  && this->group[CON_GROUP_WRITE].fd_count == 0 
		  && this->group[CON_GROUP_EXCEPT].fd_count == 0 ) { // no sockets to process and no timeouts, I guess we're done
			return 1;
		}

		memcpy( &fd_read, &this->group[CON_GROUP_READ], sizeof(fd_read) );
		memcpy( &fd_write, &this->group[CON_GROUP_WRITE], sizeof(fd_write) );
		memcpy( &fd_except, &this->group[CON_GROUP_EXCEPT], sizeof(fd_except) );
	}

	iResult = apb->apbselect( NULL, &fd_read, &fd_write, &fd_except, timeout );
	if ( iResult == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::step: select failed, %ld", apb->apbWSAGetLastError() );
		return 1;
	} else if ( iResult > 0 ) {
		if ( fd_read.fd_count ) {
			for ( iterCon = this->connection.begin(); iterCon != this->connection.end(); ) {
				con = iterCon->second;
				iterCon++;
				if ( FD_ISSET( con->socket, &fd_read ) ) {
					if ( con->state == CON_STATE_LISTENING ) { // accept a connection
						spConnection newcon = this->acceptConnection( con, NULL );
						Log.log( 3, "AgentBase::step: accepted connection! (%s:%s)", con->ap.address, con->ap.port );
					} else if ( con->state == CON_STATE_CONNECTED ) { // read data
						this->conReceiveData( con );
					} else {
						int iResult = apb->apbrecv( con->socket, con->buf, con->bufMax, NULL );
						Log.log( 3, "AgentBase::step: received %d bytes on disconnected connection? (%s:%s)", iResult, con->ap.address, con->ap.port );	
					}
				}
			}
		}

		if ( fd_write.fd_count ) {
			for ( iterCon = this->connection.begin(); iterCon != this->connection.end(); ) {
				con = iterCon->second;
				iterCon++;
				if ( FD_ISSET( con->socket, &fd_write ) ) {
					if ( con->state == CON_STATE_WAITING ) { // finish connecting
						this->finishConnection( con );
						Log.log( 3, "AgentBase::step: made connection! (%s:%s)", con->ap.address, con->ap.port );
					} else if ( con->sendBuf ) { // we have data waiting to be sent
						this->conSendBuffer( con );
					}
				}
			}
		}

		if ( fd_except.fd_count ) {
			for ( iterCon = this->connection.begin(); iterCon != this->connection.end(); ) {
				con = iterCon->second;
				iterCon++;
				if ( FD_ISSET( con->socket, &fd_except ) ) {
					if ( con->state == CON_STATE_WAITING ) { // connection failed	
						apb->apbgetsockopt( con->socket, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
						Log.log( 3, "AgentBase::step: connection failed (%s:%s), %d", con->ap.address, con->ap.port, err );
						if ( this->retryConnection( con ) ) {
							this->closeConnection( con, false );
						}
					}
				}
			}
		}
	} // else { // timeout occured }

	// clean up connections
	for ( iterCon = this->connection.begin(); iterCon != this->connection.end(); ) {
		con = iterCon->second;
		iterCon++;
		if ( con->markedForDeletion ) {
			this->conDelete( con );
		}
	}

	if ( STATE(AgentBase)->simulateCrash ) {
		this->simulateCrash();
		return 1;
	}

	if ( STATE(AgentBase)->stopFlag ) { // we are trying to stop
		if ( this->group[CON_GROUP_WRITE].fd_count == 0 ) { // we're safe to stop
			return 1; // stop
		} else {
			return 0; // next step
		}
	} else {
		this->checkTimeouts(); // process any outstanding timeouts
		return 0; // next step
	}
}

float AgentBase::getCpuUsage() {
	if ( this->cpuUsageTotalElapsed > 0 )
		return (float)(this->cpuUsageTotalUsed/(double)this->cpuUsageTotalElapsed);
	return 0;
}



//-----------------------------------------------------------------------------
// Dynamic State Buffers

UUID AgentBase::newDynamicBuffer( unsigned int size ) {
	UUID ref;
	StateBuf buf;

	apb->apbUuidCreate( &ref );

	buf.size = size;
	buf.buf = malloc(size);
	if ( !buf.buf ) 
		return nilUUID;

	this->stateBuffers[ref] = buf;

	return ref;
}

int AgentBase::freeDynamicBuffer( UUID ref ) {
	mapStateBuf::iterator it = this->stateBuffers.find( ref );

	if ( it == this->stateBuffers.end() )
		return 1; // not found

	free( it->second.buf );

	this->stateBuffers.erase( it );

	return 0;
}

void * AgentBase::getDynamicBuffer( UUID ref ) {
	mapStateBuf::iterator it = this->stateBuffers.find( ref );

	if ( it == this->stateBuffers.end() )
		return NULL;
	else
		return it->second.buf;
}

//-----------------------------------------------------------------------------
// Network

spConnection AgentBase::openConnection( spAddressPort ap, unsigned int groups, int retries, int protocol ) {
	spConnection con = NULL;
	int i;
	
	// prepare connection
	con = (spConnection)malloc(sizeof(sConnection));
	if ( !con ) {
		Log.log( 0, "AgentBase::openConnection: malloc failed for spConnection." );
		return NULL;
	}
	
	if ( this->conInit( con, ap, protocol, retries ) ) {
		Log.log( 0, "AgentBase::openConnection: conInit failed" );
		return NULL;
	}

	// connect
	if ( this->conConnect( con, protocol ) ) {
		free( con );
		return NULL;
	}

	// finalize
	groups |= CON_GROUPF_WRITE|CON_GROUPF_EXCEPT;
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( groups & (0x1 << i) ) {
			if ( this->conGroupAddTest( con, i ) ) {
				Log.log( 0, "AgentBase::openConnection: conGroupAddTest failed: %d", i );
				apb->apbclosesocket( con->socket );
				free( con );
				return NULL;
			}
		}
	}
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( groups & (0x1 << i) ) {
			this->conGroupAdd( con, i );
		}
	}

	con->state = CON_STATE_WAITING;
	this->conAdd( con );

	return con;
}

int AgentBase::closeConnection( spConnection con, bool force ) {
	int i;

	if ( con->markedForDeletion )
		return 0; // already closed

	// disconnect
	if ( con->state != CON_STATE_DISCONNECTED ) {
		conSetState( con, CON_STATE_DISCONNECTED );
	}

	// allow more retries if not forced
	if ( !force && !this->retryConnection( con ) ) {
		return 0;
	}
	
	// mark for deletion
	con->markedForDeletion = true;
	
	// remove from groups
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( con->groups & (0x1 << i) ) {
			this->conGroupRemove( con, i );
		}
	}

	for ( i=0; i<CON_MAX_WATCHERS; i++ ) {
		if ( con->watchers[i] ) this->callback[con->watchers[i]]->callback( CON_EVT_CLOSED, con );
	}

	this->connectionStatusChanged( con, CS_NO_CONNECTION );

	if ( con->subject->timeout != nilUUID ) {
		this->removeTimeout( &con->subject->timeout );
		con->subject->timeout = nilUUID;
	}
	
	if ( con->observer->timeout != nilUUID ) {
		this->removeTimeout( &con->observer->timeout );
		con->observer->timeout = nilUUID;
	}

	return 0;
}

spConnection AgentBase::getConnection( spAddressPort ap ) {
	mapConnection::iterator iter = this->connection.begin();

	while( iter != this->connection.end() ) {
		if ( !strcmp( iter->second->ap.address, ap->address ) 
		  && !strcmp( iter->second->ap.port, ap->port ) )
			return iter->second;
		iter++;
	}

	return NULL;
}

int AgentBase::connectionStatus( UUID uuid ) {
	mapConnection::iterator iC;

	if ( uuid == *this->getUUID() )
		return CS_TRUSTED;

	for ( iC = this->connection.begin(); iC != this->connection.end(); iC++ ) {
		if ( iC->second->uuid == uuid ) {
			return iC->second->observer->status;
		}
	}

	return CS_NO_CONNECTION;
}

int AgentBase::retryConnection( spConnection con ) {
	
	if ( con->maxRetries != -1 && con->retries >= con->maxRetries - 1 ) {
		Log.log( 3, "AgentBase::retryConnection: connection failed %d times, giving up", con->maxRetries );
		return 1;
	}

	con->retries++;

	if ( con->state != CON_STATE_WAITING )
		this->conSetState( con, CON_STATE_WAITING );

	this->conGroupRemove( con, CON_GROUP_EXCEPT );
	this->conGroupRemove( con, CON_GROUP_WRITE );
	this->conGroupRemove( con, CON_GROUP_READ );

	apb->apbclosesocket( con->socket );

	con->socket = INVALID_SOCKET;

	this->addTimeout( CON_RETRY_DELAY, AgentBase_CBR_cbDoRetry, &con->index, sizeof(UUID), false );
	
	return 0;
}

int AgentBase::finishConnection( spConnection con ) {

	this->conGroupRemove( con, CON_GROUP_EXCEPT );
	this->conGroupRemove( con, CON_GROUP_WRITE );

	if ( this->conGroupAdd( con, CON_GROUP_READ ) ) {
		this->closeConnection( con );
		return 1;
	}

	this->conSetState( con, CON_STATE_CONNECTED );

	this->resetConnectionRetries( con );

	return 0;
}

spConnection AgentBase::acceptConnection( spConnection listener, unsigned int groups ) {
	spConnection con = NULL;
	int i;
	
	// prepare connection
	con = (spConnection)malloc(sizeof(sConnection));
	if ( !con ) {
		Log.log( 0, "AgentBase::acceptConnection: malloc failed for spConnection." );
		return con;
	}

	this->conInit( con, &listener->ap );

	// accept
	con->socket = apb->apbaccept( listener->socket, NULL, NULL );
	if ( con->socket == INVALID_SOCKET ) {
        Log.log( 0, "AgentBase::acceptConnection: accept failed: %d", apb->apbWSAGetLastError());
		this->closeListener( listener );
        free( con );
		return NULL;
    }


	// make socket non-blocking
	u_long enable = 1;
	int iResult = apb->apbioctlsocket( con->socket, FIONBIO, &enable );
	if ( iResult == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::acceptConnection: Error at ioctlsocket(): %ld", apb->apbWSAGetLastError());
		apb->apbclosesocket( con->socket );
		this->closeListener( listener );
		free( con );
		return NULL;
	}
	
	// finalize
	groups |= CON_GROUPF_READ|CON_GROUPF_PASSIVE;
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( groups & (0x1 << i) ) {
			if ( this->conGroupAddTest( con, i ) ) {
				Log.log( 0, "AgentBase::acceptConnection: conGroupAddTest failed: %d", i );
				apb->apbclosesocket( con->socket );
				this->closeListener( listener );
				free( con );
				return NULL;
			}
		}
	}
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( groups & (0x1 << i) ) {
			this->conGroupAdd( con, i );
		}
	}

	this->conSetState( con, CON_STATE_CONNECTED );
	this->conAdd( con );

	return con;
}

void AgentBase::setConnectionMaxRetries( spConnection con, int retries ) {
	con->maxRetries = retries;
	con->retries = 0;
}

void AgentBase::resetConnectionRetries( spConnection con ) {
	con->retries = 0;
}

int	AgentBase::watchConnection( spConnection con, int cbRef ) {
	int i;

	// find empty watcher slot
	for ( i=0; i<CON_MAX_WATCHERS; i++ ) {
		if ( con->watchers[i] == NULL )
			break;
	}

	if ( i == CON_MAX_WATCHERS ) {
		Log.log( 0, "AgentBase::watchConnection: already have maximum watchers, increase CON_MAX_WATCHERS" );
		return -1;
	}

	con->watchers[i] = cbRef;
	return i;
}

int	AgentBase::stopWatchingConnection( spConnection con, int watcher ) {
	
	if ( con->watchers[watcher] == NULL ) {
		Log.log( 0, "AgentBase::stopWatchingConnection: watcher doesn't exist" );
		return 1;
	}

	this->callback[con->watchers[watcher]]->callback( CON_EVT_REMOVED, con );
	con->watchers[watcher] = NULL;

	return 0;
}

int AgentBase::initializeConnectionFailureDetection( spConnection con ) {
	_timeb tb;

	if ( con->state != CON_STATE_CONNECTED )
		return 1; // not ready yet!

	if ( con->subject->count != 0 )
		return 1; // we've already started

	con->subject->period = con->subject->nextPeriod = FD_INITIAL_PERIOD;
	con->subject->count = 1;

	apb->apb_ftime_s( &tb );

	this->ds.reset();
	this->ds.packUInt32( con->subject->count );
	this->ds.packUInt32( con->subject->period );
	this->ds.packData( &tb, sizeof(_timeb) );
	this->sendMessage( con, MSG_FD_ALIVE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	if ( this->isHost ) 
		Log.log( LOG_LEVEL_ALL, "AgentBase::initializeConnectionFailureDetection: MSG_FD_ALIVE %s", Log.formatUUID(LOG_LEVEL_ALL,&con->uuid) );

	con->subject->timeout = this->addTimeout( con->subject->period, AgentBase_CBR_cbFDSubject, &con->index, sizeof(UUID), false );

	return 0;
}

int AgentBase::setConnectionfailureDetectionQOS( spConnection con, unsigned int TDu, unsigned int TMRL, unsigned int TMU ) {

	con->observer->TDu = TDu;
	con->observer->TMRL = TMRL;
	con->observer->TMU = TMU;

	this->conFDEvaluateQOS( con );

	return 0;
}

int AgentBase::connectionStatusChanged( spConnection con, int status ) {

	if ( con->uuid != nilUUID ) {
		// do nothing
	}

	return 0;
}


spConnection AgentBase::openListener( spAddressPort ap ) {
	spConnection con = NULL;
	int iResult;
	
	// prepare connection
	con = (spConnection)malloc(sizeof(sConnection));
	if ( !con ) {
		Log.log( 0, "AgentBase::openListener: malloc failed for spConnection." );
		return NULL;
	}

	this->conInit( con, ap );

	// prepare address
    this->addr_hints.ai_family = AF_INET;
    this->addr_hints.ai_socktype = SOCK_STREAM;
    this->addr_hints.ai_protocol = IPPROTO_TCP;
	//this->addr_hints.ai_flags = AI_PASSIVE;

	iResult = apb->apbgetaddrinfo( ap->address, ap->port, &this->addr_hints, &this->addr_result );
	if ( iResult != 0 ) {
		Log.log( 0, "AgentBase::openListener: getaddrinfo failed: %d", iResult);
		free( con->buf );
		free( con );
		return NULL;
	}

	// create socket
	con->socket = apb->apbsocket( this->addr_result->ai_family, this->addr_result->ai_socktype, this->addr_result->ai_protocol );
	if ( con->socket == INVALID_SOCKET ) {
		Log.log( 0, "AgentBase::openListener: Error at socket(): %ld", apb->apbWSAGetLastError() );
		apb->apbfreeaddrinfo( this->addr_result );
		free( con->buf );
		free( con );
		return NULL;
	}

	// bind socket
	iResult = apb->apbbind( con->socket, this->addr_result->ai_addr, (int)this->addr_result->ai_addrlen );
	if (iResult == SOCKET_ERROR) {
		Log.log( 0, "AgentBase::openListener: bind failed: %d", apb->apbWSAGetLastError() );
		apb->apbclosesocket( con->socket );
		apb->apbfreeaddrinfo( this->addr_result );
		free( con->buf );
		free( con );
		return NULL;
	}
	
	apb->apbfreeaddrinfo( this->addr_result );

	// listen on socket
	if ( apb->apblisten( con->socket, SOMAXCONN ) == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::openListener: Error at listen(): %ld", apb->apbWSAGetLastError() );
		apb->apbclosesocket( con->socket );
		free( con->buf );
		free( con );
		return NULL;
	}

	// finalize
	con->state = CON_STATE_LISTENING;
	if ( this->conGroupAdd( con, CON_GROUP_READ ) ) {
		Log.log( 0, "AgentBase::openListener: conGroupAdd failed" );
		apb->apbclosesocket( con->socket );
		free( con->buf );
		free( con );
		return NULL;
	}
	this->conAdd( con );

	return con;
}

int AgentBase::closeListener( spConnection con ) {
	int i;

	// disconnect
	conSetState( con, CON_STATE_DISCONNECTED );
	
	// remove from groups
	for ( i=0; i<this->CON_GROUP_HIGH; i++ ) {
		if ( con->groups & (0x1 << i) ) {
			this->conGroupRemove( con, i );
		}
	}

	// destroy
	apb->apbclosesocket( con->socket );
	this->conRemove( con );
	free( con->buf );

	if ( con->subject->timeout != nilUUID )
		this->removeTimeout( &con->subject->timeout );
	delete con->subject;
	
	if ( con->observer->timeout != nilUUID )
		this->removeTimeout( &con->observer->timeout );
	delete con->observer;

	free( con );

	return 0;
}

int AgentBase::sendAgentMessage( UUID *agent, unsigned char message, char *data, unsigned int len ) {
	if ( this->isHost ) {
		return 1; // should be handled through the override function
	}

	if ( *agent == *this->getUUID() ) { // this is for us
		this->_queueLocalMessage( message, data, len );
		return 0;
	}
	Log.log(0, "AgentBase::sendAgentMessage");
	if (message == 164) {
		Log.log(0, "AgentBase::sendAgentMessage: Requesting acquiescence");
	}

	return this->sendMessage( this->hostCon, message, data, len, agent );
}

void AgentBase::atomicMessageInit( AtomicMessage *am ) {

	am->id = nilUUID;
	am->placeholder = true;
	am->orderedMsg = false;
	am->queue = nilUUID;

	am->targets.clear();
	am->f = 0;

	am->msg = -1;
	am->len = 0;
	am->dataRef = nilUUID;

	am->cbRef = 0;

	am->retries = 0;

	am->timeout = nilUUID;

	am->phase = AM_PHASE_NULL;
	am->decided = 0;
	am->delivered = 0;
	am->estCommit = 0;
	am->order = 0;
	am->round = -1;
	am->timestamp = 0;

	am->coord = nilUUID;
	am->suspects.clear();
	am->permanentSuspects.clear();
	am->decisions.clear();
	am->msgs.clear();
	am->proposals.clear();
	am->acks.clear();

	am->initiator = nilUUID;
	am->msgsSent = 0;
	am->dataSent = 0;
	am->orderChanges = 0;

	memset( &am->startT, 0, sizeof(_timeb) );
	memset( &am->decidedT, 0, sizeof(_timeb) );
	memset( &am->deliveredT, 0, sizeof(_timeb) );
}

UUID AgentBase::atomicMessage( std::list<UUID> *targets, unsigned char message, char *data, unsigned int len, UUID *ticket, int cbRef, unsigned char msgAbort, char *dataRejected, unsigned int lenAbort ) {
	DataStream lds;
	UUID id;
	std::list<UUID>::iterator iT;
	
	if ( ticket == NULL ) {
		apb->apbUuidCreate( &id );
	} else {
		id = *ticket;
	
		if ( this->atomicMsgs.find( id ) != this->atomicMsgs.end() ) {
			return nilUUID; // msg with this ticket already exists!
		}
	}

	// prepare struct
	AtomicMessage *am = &this->atomicMsgs[id];
	this->atomicMessageInit( am );
	
	am->id = id;
	am->placeholder = false;
	am->targets = *targets;
	am->f = min( 5, (int)targets->size()/2 ); // max number of failures to handle
	am->msg = message;
	am->len = len;
	if ( len ) {
		am->dataRef = this->newDynamicBuffer( len );
		memcpy( this->getDynamicBuffer( am->dataRef ), data, len );
	}

	am->cbRef = cbRef;
	
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		int cs = this->connectionStatus( *iT );
		if ( cs == CS_SUSPECT ) {
			am->suspects.push_back( *iT );
		} else if ( cs == CS_NO_CONNECTION ) {
			am->suspects.push_back( *iT );
			am->permanentSuspects.push_back( *iT );
		}
	}

	// statistics
	am->initiator = *this->getUUID();
	apb->apb_ftime_s( &am->startT );

	// pack message
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( &id );
	lds.packBool( am->orderedMsg );
	lds.packUUID( &am->queue );
	lds.packInt32( (int)am->targets.size() );
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		lds.packUUID( &*iT );
	}
	lds.packInt32( am->f );
	lds.packUChar( am->msg );
	lds.packUInt32( am->len );
	if ( am->len )
		lds.packData( this->getDynamicBuffer( am->dataRef ), am->len );
/*	lds.packUChar( am->msgAbort );
	lds.packUInt32( am->lenAbort );
	if ( am->lenAbort )
		lds.packData( this->getDynamicBuffer( am->dataAbortRef ), am->lenAbort );
*/	lds.packInt32( am->order );
	
	// distribute message
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		if ( *iT != *this->getUUID() ) // don't send to ourselves
			this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG, lds.stream(), lds.length() );
	}
	am->msgsSent += (int)am->targets.size() - 1; // statistics
	am->dataSent += ((int)am->targets.size() - 1) * lds.length();
	lds.unlock();

	// begin step P1
	this->_atomicMessageTask2( am );

	return id;
}

UUID AgentBase::atomicMessageOrdered( UUID *queue, std::list<UUID> *targets, unsigned char message, char *data, unsigned int len, UUID *ticket, int cbRef ) {
	DataStream lds;
	UUID id;

	if ( targets->empty() )
		return nilUUID; // no targets!

	if ( ticket == NULL ) {
		apb->apbUuidCreate( &id );
	} else {
		id = *ticket;
	
		if ( this->atomicMsgs.find( id ) != this->atomicMsgs.end() ) {
			return nilUUID; // msg with this ticket already exists!
		}
	}

	// prepare struct
	AtomicMessage am;

	this->atomicMessageInit( &am );

	am.id = id;
	am.orderedMsg = true;
	am.queue = *queue;
	am.targets = *targets;
	am.msg = message;
	am.len = len;
	if ( len ) {
		am.dataRef = this->newDynamicBuffer( len );
		memcpy( this->getDynamicBuffer( am.dataRef ), data, len );
	}

	am.cbRef = cbRef;

	// statistics
	am.initiator = *this->getUUID();
	apb->apb_ftime_s( &am.startT );

	this->atomicMsgsOrderedQueue[am.queue].push_back( am ); // queue message locally

	if ( this->atomicMsgsOrderedQueue[am.queue].size() == 1 ) { // we weren't already processing an ordered message
		this->_atomicMessageSendOrdered( &am.queue );
	}

	return id;
}

UUID AgentBase::atomicMessageRetry( UUID *id, std::list<UUID> *targets ) {
	DataStream lds;
	std::list<UUID>::iterator iT;

	return nilUUID; // DON"T USE THIS!!

	UUID nid;
	AtomicMessage *nam;
	AtomicMessage *am = &this->atomicMsgs[*id];

	apb->apbUuidCreate( &nid );

	// prepare struct
	nam = &this->atomicMsgs[nid];
	this->atomicMessageInit( nam );
	nam->id = am->id;
	nam->placeholder = false;
	nam->orderedMsg = am->orderedMsg;
	nam->queue = am->queue;
	if ( targets ) { // new target list
		nam->targets = *targets;
	} else { // use old targets
		for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ )
			nam->targets.push_back( *iT );
	}
	nam->f = min( 5, (int)(nam->targets.size()/2) ); // max number of failures to handle
	nam->msg = am->msg;
	nam->len = am->len;
	if ( am->len ) {
		nam->dataRef = this->newDynamicBuffer( am->len );
		memcpy( this->getDynamicBuffer( nam->dataRef ), this->getDynamicBuffer( am->dataRef ), am->len );
	}
	nam->cbRef = am->cbRef;
	nam->retries = am->retries + 1;

	// statistics
	nam->initiator = *this->getUUID();
	apb->apb_ftime_s( &nam->startT );
	
	if ( nam->orderedMsg ) {
		for ( iT = nam->targets.begin(); iT != nam->targets.end(); iT++ ) {
			int cs = this->connectionStatus( *iT );
			if ( cs == CS_SUSPECT ) {
				nam->suspects.push_back( *iT );
			} else if ( cs == CS_NO_CONNECTION ) {
				nam->suspects.push_back( *iT );
				nam->permanentSuspects.push_back( *iT );
			}
		}

		// pack message
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packUUID( &nid );
		lds.packBool( nam->orderedMsg );
		lds.packUUID( &nam->queue );
		lds.packInt32( (int)nam->targets.size() );
		for ( iT = nam->targets.begin(); iT != nam->targets.end(); iT++ ) {
			lds.packUUID( &*iT );
		}
		lds.packInt32( nam->f );
		lds.packUChar( nam->msg );
		lds.packUInt32( nam->len );
		if ( nam->len )
			lds.packData( this->getDynamicBuffer( nam->dataRef ), nam->len );
/*		lds.packUChar( nam->msgAbort );
		lds.packUInt32( nam->lenAbort );
		if ( nam->lenAbort )
			lds.packData( this->getDynamicBuffer( nam->dataAbortRef ), nam->lenAbort );
*/		lds.packInt32( nam->order );
		
		// distribute message
		for ( iT = nam->targets.begin(); iT != nam->targets.end(); iT++ ) {
			if ( *iT != *this->getUUID() ) // don't send to ourselves
				this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG, lds.stream(), lds.length() );
		}
		nam->msgsSent += (int)nam->targets.size() - 1; // statistics
		nam->dataSent += ((int)nam->targets.size() - 1) * lds.length();
		lds.unlock();

		// begin step P1
		this->_atomicMessageTask2( nam );
	} else {
		// replace older message
		this->atomicMsgsOrderedQueue[nam->queue].pop_front();
		this->atomicMsgsOrderedQueue[nam->queue].push_front( *nam ); // queue message locally

		this->_atomicMessageSendOrdered( &nam->queue );
	}

	return nid;
}

int AgentBase::_atomicMessageSendOrdered( UUID *queue ) {
	DataStream lds;
	AtomicMessage *amq;
	std::list<UUID>::iterator iT;

	if ( this->atomicMsgsOrderedQueue[*queue].empty() ) 
		return 0; // nothing to send

	amq = &this->atomicMsgsOrderedQueue[*queue].front();

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == amq->id )
			int brk=0;
	}
	
	//Log.log( LOG_LEVEL_ALL, "AgentBase::_atomicMessageSendOrdered: sending message %s", Log.formatUUID( LOG_LEVEL_ALL, &amq->id ) );

	// prepare struct
	AtomicMessage *am = &this->atomicMsgs[amq->id];
	this->atomicMessageInit( am );
	am->id = amq->id;
	am->placeholder = false;
	am->orderedMsg = true;
	am->queue = amq->queue;
	am->targets = amq->targets;
	am->f = min( 5, (int)(amq->targets.size()/2) ); // max number of failures to handle
	am->msg = amq->msg;
	am->len = amq->len;
	am->dataRef = amq->dataRef;

	amq->len = 0; // we've taken over the dataRef now, so clear it from the queue
	amq->dataRef = nilUUID;

	am->cbRef = amq->cbRef;
	am->retries = amq->retries;
	
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		int cs = this->connectionStatus( *iT );
		if ( cs == CS_SUSPECT ) {
			am->suspects.push_back( *iT );
		} else if ( cs == CS_NO_CONNECTION ) {
			am->suspects.push_back( *iT );
			am->permanentSuspects.push_back( *iT );
		}
	}

	// statistics
	am->initiator = amq->initiator;
	am->startT = amq->startT;

	int highO = 0;
	if ( this->atomicMessageHighestOrder.find( *queue ) != this->atomicMessageHighestOrder.end() )
		highO = this->atomicMessageHighestOrder[*queue];
	am->order = highO + 1;
	this->atomicMessageHighestOrder[*queue] = am->order;

	// pack message
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( &am->id );
	lds.packBool( am->orderedMsg );
	lds.packUUID( &am->queue );
	lds.packInt32( (int)am->targets.size() );
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		lds.packUUID( &*iT );
	}
	lds.packInt32( am->f );
	lds.packUChar( am->msg );
	lds.packUInt32( am->len );
	if ( am->len )
		lds.packData( this->getDynamicBuffer( am->dataRef ), am->len );
/*	lds.packUChar( am->msgAbort );
	lds.packUInt32( am->lenAbort );
	if ( am->lenAbort )
		lds.packData( this->getDynamicBuffer( am->dataAbortRef ), am->lenAbort );
*/	lds.packInt32( am->order );
	
	// distribute message
	for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
		if ( *iT != *this->getUUID() ) // don't send to ourselves
			this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG, lds.stream(), lds.length() );
	}
	am->msgsSent += (int)am->targets.size() - 1; // statistics
	am->dataSent += ((int)am->targets.size() - 1) * lds.length();
	lds.unlock();

	int vote = this->atomicMessageEvaluate( &am->id, am->msg, (am->len ? (char *)this->getDynamicBuffer( am->dataRef ) : 0 ), am->len );

	if ( !vote ) { // wat
		this->_atomicMessageDecide( &am->id, this->getUUID(), am->order, am->round, am->estCommit ); // process decision locally

	} else {
		// begin step P1
		this->_atomicMessageTask2( am );
	}

	return 0;
}

int AgentBase::_receiveAtomicMessage( DataStream *ds ) {
	DataStream lds;
	RPC_STATUS Status;
	UUID sender, id, tid;
	int i, count, orderq;
	std::list<UUID>::iterator iT;
	bool newMsg; // weren't already aware of this message

	ds->unpackUUID( &sender );
	ds->unpackUUID( &id );

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == id )
			int i=0;
	}

	AtomicMessage *am;

	newMsg = true;
	if ( this->atomicMsgs.find( id ) != this->atomicMsgs.end() ) {
		if ( this->atomicMsgs[id].orderedMsg && this->atomicMsgs[id].decided ) 
			return 0; // msg already decided, ignore
		newMsg = false;
		am = &this->atomicMsgs[id];
	} else {
		am = &this->atomicMsgs[id];
		this->atomicMessageInit( am );
	}

	
	am->id = id;
	am->placeholder = false;
	am->orderedMsg = ds->unpackBool();
	ds->unpackUUID( &am->queue );

	count = ds->unpackInt32();
	for ( i=0; i<count; i++ ) {
		ds->unpackUUID( &tid );
		am->targets.push_back( tid );
		if ( this->isHost ) { // only hosts keep track of suspects
			int cs = this->connectionStatus( tid );
			if ( cs == CS_SUSPECT ) {
				am->suspects.push_back( tid );
			} else if ( cs == CS_NO_CONNECTION ) {
				am->suspects.push_back( tid );
				am->permanentSuspects.push_back( tid );
			}
		}
	}
	am->f = ds->unpackInt32();

	am->msg = ds->unpackUChar();
	am->len = ds->unpackUInt32();
	if ( am->len ) {
		am->dataRef = this->newDynamicBuffer( am->len );
		memcpy( this->getDynamicBuffer( am->dataRef ), ds->unpackData( am->len ), am->len );
	}
	
	if ( newMsg ) {
		am->order = ds->unpackInt32();
	} else {
		orderq = ds->unpackInt32(); // discard order
	}

	// statistics
	am->initiator = sender;
	apb->apb_ftime_s( &am->startT );

	// confirm order
	mapAtomicMessage::iterator iM;
	if ( am->orderedMsg ) {
		for ( iM = this->atomicMsgs.begin(); iM != this->atomicMsgs.end(); iM++ ) {
			int idHigher = UuidCompare( (UUID *)&iM->first, &am->id, &Status );
			if ( !iM->second.placeholder && !iM->second.delivered && iM->second.orderedMsg && iM->second.queue == am->queue && (iM->second.order > am->order || (iM->second.order == am->order && idHigher == 1)) )
				break; // message has higher order
		}
		if ( iM != this->atomicMsgs.end() ) { // order is not ok
			int highO = 0;
			if ( this->atomicMessageHighestOrder.find( am->queue ) != this->atomicMessageHighestOrder.end() )
				highO = this->atomicMessageHighestOrder[am->queue];
			am->order = highO + 1;
			this->atomicMessageHighestOrder[am->queue] = am->order;

			// clear any messages/proposals/acks we've accumulated
			am->msgs.clear();
			am->proposals.clear();
			am->acks.clear();

			// propose new order
			lds.reset();
			lds.packUUID( &am->id );
			lds.packUUID( &am->queue );
			lds.packUUID( this->getUUID() );
			lds.packInt32( am->order );

			for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
				if ( *iT != *this->getUUID() ) // don't send to ourselves
					this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG_REORDER, lds.stream(), lds.length() );
			}
			am->msgsSent += (int)am->targets.size() - 1; // statistics
			am->dataSent += ((int)am->targets.size() - 1) * lds.length();
			lds.unlock();
		} else { // order is ok
			this->atomicMessageHighestOrder[am->queue] = am->order;
		}
	} 

	// evaluate transaction
	int vote = this->atomicMessageEvaluate( &am->id, am->msg, (am->len ? (char *)this->getDynamicBuffer( am->dataRef ) : 0 ), am->len );

	//Log.log( LOG_LEVEL_ALL, "AgentBase::_receiveAtomicMessage: new message %s, vote %d", Log.formatUUID( LOG_LEVEL_ALL, &id ), vote );

	if ( !vote ) { // abort
		this->_atomicMessageDecide( &am->id, this->getUUID(), am->order, am->round, am->estCommit ); // process decision locally

	} else {
		// begin step P1
		this->_atomicMessageTask2( am );
	}

	return 0;
}

int AgentBase::_atomicMessageCheckOrder( UUID *id, UUID *queue, int order ) {
	DataStream lds;
//	RPC_STATUS Status;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iT;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		// create placeholder
		am = &this->atomicMsgs[*id]; 
		this->atomicMessageInit( am );
		am->id = *id;
		am->placeholder = true;
		am->queue = *queue;
		am->orderedMsg = true;
		am->order = order;
	} else {
		am = &iM->second;
	}

	if ( am->decided )
		return 0; // we're already done

	if ( order > am->order ) {
		am->orderChanges++;

		am->order = order;

		// clear any messages/proposals/acks we've accumulated
		am->msgs.clear();
		am->proposals.clear();
		am->acks.clear();

		if ( am->phase != AM_PHASE_NULL ) {
			// update highest order if necessary
			if ( this->atomicMessageHighestOrder.find( *queue ) == this->atomicMessageHighestOrder.end() 
			  || this->atomicMessageHighestOrder[*queue] < am->order )
				this->atomicMessageHighestOrder[*queue] = am->order;

			// propagate new order
			lds.reset();
			lds.packUUID( &am->id );
			lds.packUUID( &am->queue );
			lds.packUUID( this->getUUID() );
			lds.packInt32( am->order );

			for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
				if ( *iT != *this->getUUID() ) // don't send to ourselves
					this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG_REORDER, lds.stream(), lds.length() );
			}
			am->msgsSent += (int)am->targets.size() - 1; // statistics
			am->dataSent += ((int)am->targets.size() - 1) * lds.length();
			lds.unlock();
					
			// abort task 2
			am->estCommit = false;
			am->round = -1;
			am->timestamp = 0;

			// restart task 2
			/*if ( am->phase == AM_PHASE_C1 ) {
				am->msgs[msg->round].push_back( *msg );
			} else if ( am->phase == AM_PHASE_P2 ) {
				am->proposals[msg->round] = *msg;
			} else if ( am->phase == AM_PHASE_C2 ) {
				am->acks[msg->round].push_back( *msg );
			}*/

			// start task 2
			this->_atomicMessageTask2( am );
		}

	}

	return 0;
}

int AgentBase::_atomicMessageTask2( AtomicMessage *am ) {
	int i;
	std::list<UUID>::iterator iT;

	if ( am->decided )
		return 1; // how did we get here?

	am->phase = AM_PHASE_P1;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == am->id )
			i=0;
	}

	// increment round
	am->round++;
	
	// get coordinator
	i = am->round % (int)am->targets.size();
	iT = am->targets.begin();
	while ( i ) {
		i--;
		iT++;
		if ( iT == am->targets.end() )
			iT = am->targets.begin();
	}
	am->coord = *iT;

	// move to next phase
	if ( am->coord == *this->getUUID() ) {
		am->phase = AM_PHASE_C1;
	} else {
		am->phase = AM_PHASE_P2;
	}

	// send estimate to coordinator (step P1)
	this->ds.reset();
	this->ds.packUUID( &am->id );
	this->ds.packUUID( &am->queue );
	this->ds.packUUID( this->getUUID() );
	this->ds.packInt32( am->order );
	this->ds.packInt32( am->round );
	this->ds.packChar( am->estCommit );
	this->ds.packInt32( am->timestamp );
	this->sendAgentMessage( &am->coord, MSG_ATOMIC_MSG_ESTIMATE, this->ds.stream(), this->ds.length() );
	am->msgsSent += 1;
	am->dataSent += this->ds.length();
	this->ds.unlock();

	//Log.log( LOG_LEVEL_ALL, "%s AgentBase::_atomicMessageTask2: est estimate to coord %s ", Log.formatUUID( LOG_LEVEL_ALL, &am->id ), Log.formatUUID( LOG_LEVEL_ALL, &am->coord ) );

	// check if coord is already suspect
	std::list<UUID>::iterator iS;
	for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
		if ( *iS == am->coord ) {
			break; // match
		}
	}
	if ( iS != am->suspects.end() ) { // we had a match
		this->_atomicMessageStepP2( &am->id ); 
	}

	return 0;
}

int AgentBase::_atomicMessageStepC1( UUID *id, UUID *q, int orderq, int roundq, char estCommitq, int timestampq ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<AtomicMessage_MSG>::iterator iE;
	std::list<UUID>::iterator iS;
	std::list<UUID>::iterator iT;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	if ( q != NULL && orderq < am->order ) {
		return 0; // ignore
	}

	if ( am->decided )
		return 0; // we're already done

	if ( q != NULL && (roundq > am->round || ( roundq == am->round && am->phase == AM_PHASE_C1) )  ) { // new estimate to add, but make sure it isn't from an old round
		AtomicMessage_MSG estMsg;
		estMsg.q = *q;
		estMsg.order = orderq;
		estMsg.round = roundq;
		estMsg.estCommit = estCommitq;
		estMsg.timestamp = timestampq;
		am->msgs[roundq].push_back( estMsg );
	}

	if ( am->coord != *this->getUUID() ) { // we're not the coord of our current round
		return 0;
	}

	// check to see if we have any suspects in our msgs list
	int suspectOverlap = 0;
	if ( am->msgs[am->round].size() >= am->targets.size() - am->f ) {
		for ( iE = am->msgs[am->round].begin(); iE != am->msgs[am->round].end(); iE++ ) {
			for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
				if ( *iS == iE->q ) {
					break; // match
				}
			}
			if ( iS != am->suspects.end() ) { // we had a match
				suspectOverlap++;
			}
		}
	}

	// check to see if we are done waiting
	if ( ( am->msgs[am->round].size() >= am->targets.size() - am->f )
		&& ( am->msgs[am->round].size() + am->suspects.size() - suspectOverlap == am->targets.size() ) ) {
		int newEstCommit;
		int ts = -1;
		
		if ( am->msgs[am->round].size() == am->targets.size() ) {
			newEstCommit = true;
		} else {
			for ( iE = am->msgs[am->round].begin(); iE != am->msgs[am->round].end(); iE++ ) {
				if ( iE->timestamp > ts ) {
					newEstCommit = iE->estCommit;
					ts = iE->timestamp;
				}
			}
		}

		am->msgs.erase( am->round ); // clear estimates for this round

		am->phase = AM_PHASE_P2;

		// send out proposal
		lds.reset();
		lds.packUUID( &am->id );
		lds.packUUID( &am->queue );
		lds.packUUID( this->getUUID() );
		lds.packInt32( am->order );
		lds.packInt32( am->round );
		lds.packChar( newEstCommit );
		for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
			if ( *iT != *this->getUUID() ) // don't send to ourselves because we jump straight there below
				this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG_PROPOSAL, lds.stream(), lds.length() );
		}
		am->msgsSent += (int)am->targets.size() - 1; // statistics
		am->dataSent += ((int)am->targets.size() - 1) * lds.length();
		lds.unlock();

		//Log.log( LOG_LEVEL_ALL, "%s AgentBase::_atomicMessageStepC1: sent proposal %d, round %d", Log.formatUUID( LOG_LEVEL_ALL, &am->id ), am->estCommit, am->round );

		// accept proposal
		this->_atomicMessageStepP2( &am->id, this->getUUID(), am->order, am->round, newEstCommit );
	}

	return 0;
}

int AgentBase::_atomicMessageStepP2( UUID *id, UUID *coord, int orderc, int roundc, char estCommitc ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iS;
	std::map<int,AtomicMessage_MSG>::iterator iP;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	if ( am->decided )
		return 0; // we're already done

	if ( coord != NULL && orderc < am->order ) {
		return 0; // ignore
	}

	if ( coord ) { // received proposal from coord
		if ( roundc < am->round ) {
			return 0; // old message
		}

		AtomicMessage_MSG prop;
		prop.q = *coord;
		prop.order = orderc;
		prop.round = roundc;
		prop.estCommit = estCommitc;
		am->proposals[roundc] = prop;
		
	}

	// see if we're done waiting
	iP = am->proposals.find( am->round );
	if ( iP != am->proposals.end() ) { // we have a proposal
		
		am->estCommit = iP->second.estCommit;
		am->timestamp = iP->second.round;

		am->proposals.erase( iP ); // clear proposals for this round
		
		//Log.log( LOG_LEVEL_ALL, "%s AgentBase::_atomicMessageStepP2: proposal acknowledged %d, round %d", Log.formatUUID( LOG_LEVEL_ALL, &am->id ), am->estCommit, am->round );

		if ( am->coord == *this->getUUID() ) { // we are the coord
			am->phase = AM_PHASE_C2;

			// acknowledge
			this->_atomicMessageStepC2( &am->id, this->getUUID(), am->order, am->round, true );

		} else { // we are not the coord
			// send ack to coord
			lds.reset();
			lds.packUUID( &am->id );
			lds.packUUID( &am->queue );
			lds.packUUID( this->getUUID() );
			lds.packInt32( am->order );
			lds.packInt32( am->round );
			lds.packChar( true );
			this->sendAgentMessage( &am->coord, MSG_ATOMIC_MSG_ACK, lds.stream(), lds.length() );
			am->msgsSent += 1;
			am->dataSent += lds.length();
			lds.unlock();

			this->_atomicMessageTask2( am ); // return to step P1
		}
	} else { // suspect list may have changed
		// check to see if the current coord is suspect
		for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
			if ( *iS == am->coord ) {
				break; // match
			}
		}
		if ( iS != am->suspects.end() ) { // suspected
			// send nack to coord
			lds.reset();
			lds.packUUID( &am->id );
			lds.packUUID( &am->queue );
			lds.packUUID( this->getUUID() );
			lds.packInt32( am->order );
			lds.packInt32( am->round );
			lds.packChar( false );
			this->sendAgentMessage( &am->coord, MSG_ATOMIC_MSG_ACK, lds.stream(), lds.length() );
			am->msgsSent += 1;
			am->dataSent += lds.length();
			lds.unlock();

			this->_atomicMessageTask2( am ); // return to step P1, note that we can never suspect ourselves so we're definately not the coord
		}
	}

	return 0;
}

int AgentBase::_atomicMessageStepC2( UUID *id, UUID *q, int orderq, int roundq, char ackq ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<AtomicMessage_MSG>::iterator iA;
	std::list<UUID>::iterator iT;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	if ( am->decided )
		return 0; // we're already done

	if ( q != NULL && orderq < am->order ) {
		return 0; // ignore
	}

	if ( (roundq > am->round || ( roundq == am->round && am->phase > AM_PHASE_C1) ) ) { // new ack to add, but make sure it isn't from an old round
		AtomicMessage_MSG ackMsg;
		ackMsg.q = *q;
		ackMsg.round = roundq;
		ackMsg.ack = ackq;
		am->acks[roundq].push_back( ackMsg );
	}

	if ( am->coord != *this->getUUID() ) { // we're not the coord of our current round
		return 0;
	}

	// check to see if we are done waiting
	if ( am->acks[am->round].size() >= am->targets.size() - am->f ) {
		int ackCount = 0;
		
		for ( iA = am->acks[am->round].begin(); iA != am->acks[am->round].end(); iA++ ) {
			if ( iA->ack == 1 ) {
				ackCount++;
			}
		}

		am->acks.erase( am->round ); // clear acks for this round

		//Log.log( LOG_LEVEL_ALL, "%s AgentBase::_atomicMessageStepC2: received %d acks, round %d", Log.formatUUID( LOG_LEVEL_ALL, &am->id ), ackCount, am->round );

		if ( ackCount >= (int)(am->targets.size()) - am->f  ) { // we decided
			this->_atomicMessageDecide( &am->id, this->getUUID(), am->order, am->round, am->estCommit ); // process decision locally

		} else {
			this->_atomicMessageTask2( am ); // return to step P1
		}
	}

	return 0;
}

int AgentBase::_atomicMessageDecide( UUID *id, UUID *q, int orderq, int roundq, char commitq ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iT;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	if ( !am->decided && !am->targets.empty() ) { // we haven't decided and we have the full message info

		//Log.log( LOG_LEVEL_ALL, "AgentBase::_atomicMessageDecide: decided message %s, %d", Log.formatUUID( LOG_LEVEL_ALL, id ), commitq );

		am->decided = true;
		//am->decisions.push_back( *this->getUUID() );

		apb->apb_ftime_s( &am->decidedT ); // statistics

		am->order = orderq;
		am->round = roundq;
		am->estCommit = commitq;

		if ( *q != *this->getUUID() ) 
			am->decisions.push_back( *q );
		
		// rebroadcast decision
		lds.reset();
		lds.packUUID( &am->id );
		lds.packUUID( &am->queue );
		lds.packUUID( this->getUUID() );
		lds.packInt32( am->order );
		lds.packInt32( am->round );
		lds.packChar( am->estCommit );
		for ( iT = am->targets.begin(); iT != am->targets.end(); iT++ ) {
			// we have to send to ourselves so that our DECISION message arrives after any other message we sent to ourselves
			//if ( *iT != *this->getUUID() ) // don't send to ourselves
				this->sendAgentMessage( &*iT, MSG_ATOMIC_MSG_DECISION, lds.stream(), lds.length() );
		}
		am->msgsSent += (int)am->targets.size() - 1; // statistics
		am->dataSent += ((int)am->targets.size() - 1) * lds.length();
		lds.unlock();

		if ( !am->orderedMsg ) { // handle right away
			
			am->decided = true;
			am->delivered = true;

			apb->apb_ftime_s( &am->deliveredT ); // statistics

			this->_atomicMessageDeliveryId = am->id;

			if ( commitq ) { // deliver message
				if ( am->len )
					this->conProcessMessage( NULL, am->msg, (char *)this->getDynamicBuffer( am->dataRef ), am->len );
				else
					this->conProcessMessage( NULL, am->msg, NULL, 0 );
			}/* else if ( am->msgAbort != -1 ) { // deliver abort message
				if ( am->lenAbort )
					this->conProcessMessage( NULL, am->msgAbort, (char *)this->getDynamicBuffer( am->dataAbortRef ), am->lenAbort );
				else
					this->conProcessMessage( NULL, am->msgAbort, NULL, 0 );
			}*/

			// fire callback
			if ( am->cbRef != 0 )
				this->callback[am->cbRef]->callback( commitq, &am->id );
			
			this->atomicMessageDecided( commitq, &am->id );

			this->_atomicMessageDeliveryId = nilUUID;

			// check if we can clean up message
			this->_atomicMessageCheckCleanup( am );

		} else if ( commitq == 0 ) { // aborts can deliver right away

			this->_atomicMessageDeliveryId = am->id;
	
			am->delivered = true;
			
			apb->apb_ftime_s( &am->deliveredT ); // statistics

			// fire callback
			if ( am->cbRef != 0 )
				this->callback[am->cbRef]->callback( commitq, &am->id );
			
			this->atomicMessageDecided( commitq, &am->id );

			this->_atomicMessageDeliveryId = nilUUID;

			this->_atomicMessageAttemptCommit( am->queue ); // this abort might have resolved an order conflict

			if ( this->atomicMsgsOrderedQueue.find( am->queue ) != this->atomicMsgsOrderedQueue.end()
				&& this->atomicMsgsOrderedQueue[am->queue].size() != 0
				&& am->id == this->atomicMsgsOrderedQueue[am->queue].front().id ) { // was a local message
				this->atomicMsgsOrderedQueue[am->queue].pop_front(); // message decided so send the next local ordered message if there is one
				this->_atomicMessageSendOrdered( &am->queue );
			}

			// check if we can clean up message
			this->_atomicMessageCheckCleanup( am );
		} else {
			if ( this->atomicMsgsOrderedQueue.find( am->queue ) != this->atomicMsgsOrderedQueue.end()
				&& this->atomicMsgsOrderedQueue[am->queue].size() != 0
				&& am->id == this->atomicMsgsOrderedQueue[am->queue].front().id ) { // was a local message
				this->atomicMsgsOrderedQueue[am->queue].pop_front(); // message decided so send the next local ordered message if there is one
				this->_atomicMessageSendOrdered( &am->queue );
			}			
			
			this->_atomicMessageAttemptCommit( am->queue ); // try to deliver message if order allows
		}
	} else { // already decided, but count the message, or we don't have the full message info yet, but still need to log the decision
		am->decisions.push_back( *q );	
		
		// check if we can clean up message
		this->_atomicMessageCheckCleanup( am );
	}

	return 0;
}

int AgentBase::_atomicMessageAttemptCommit( UUID queue ) {
	long long lowestOrder; // int lowestOrder;
	mapAtomicMessage::iterator iM;
	AtomicMessage *amLow;

	// DEBUG
//	_timeb attemptTime;
//	apb->apb_ftime_s( &attemptTime );

	while ( this->atomicMsgs.size() ) {
		lowestOrder = MAXINT_PTR;

		for ( iM = this->atomicMsgs.begin(); iM != this->atomicMsgs.end(); iM++ ) {
			if ( !iM->second.orderedMsg || iM->second.queue != queue || iM->second.placeholder || iM->second.delivered )
				continue; // ignore

			if ( lowestOrder > iM->second.order ) {
				amLow = &iM->second;
				lowestOrder = iM->second.order;
			}
		}

		if ( lowestOrder == MAXINT_PTR ) {
			this->atomicMessageHighestOrder.erase( queue );
			return 0; // no ordered messages left in this queue
		}

		if ( !amLow->decided ) {
			return 0; // lowest ordered message is not ready to decide
		}

		this->_atomicMessageDeliveryId = amLow->id;

		amLow->delivered = true;

		// DEBUG AgentPlayback
	//	Log.log( 0, "AgentBase::_atomicMessageAttemptCommit: atomic message delivered %s, attemptTime %d.%d, order %d, atomicMsgs.size() %d", Log.formatUUID(0,&amLow->id), (int)attemptTime.time, (int)attemptTime.millitm, amLow->order, (int)this->atomicMsgs.size() );
		
		apb->apb_ftime_s( &amLow->deliveredT ); // statistics

		// commit message
		if ( amLow->len )
			this->conProcessMessage( NULL, amLow->msg, (char *)this->getDynamicBuffer( amLow->dataRef ), amLow->len );
		else
			this->conProcessMessage( NULL, amLow->msg, NULL, 0 );

		// fire callback
		if ( amLow->cbRef != 0 )
			this->callback[amLow->cbRef]->callback( 1, &amLow->id );
		
		this->atomicMessageDecided( true, &amLow->id );

		this->_atomicMessageDeliveryId = nilUUID;

		// check if we can clean up message
		this->_atomicMessageCheckCleanup( amLow );
	}

	if ( this->atomicMsgs.size() == 0 ) {
		this->atomicMessageHighestOrder.clear(); // reset
	}

	return 0;
}

int AgentBase::_atomicMessageTrust( UUID *id, UUID *suspect ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iS;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	// find the suspect
	for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
		if ( *iS == *suspect ) {
			am->suspects.erase( iS );	
			break;
		}
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentBase::_atomicMessageTrust: msg %s, no longer suspect %s", Log.formatUUID( LOG_LEVEL_VERBOSE, id ), Log.formatUUID( LOG_LEVEL_VERBOSE, suspect ) );

	return 0;
}

int AgentBase::_atomicMessageSuspect( UUID *id, UUID *suspect ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iS;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	// make sure we don't already suspect this person 
	for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
		if ( *iS == *suspect )
			return 0; // already a suspect
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentBase::_atomicMessageSuspect: msg %s, new suspect %s", Log.formatUUID( LOG_LEVEL_VERBOSE, id ), Log.formatUUID( LOG_LEVEL_VERBOSE, suspect ) );

	am->suspects.push_back( *suspect );

	if ( am->coord == *this->getUUID() && am->phase == AM_PHASE_C1 ) { // we're the coord of our current round, check if this changes our status
		this->_atomicMessageStepC1( id );
	} else if ( am->coord == *suspect && am->phase == AM_PHASE_P2 ) { // our coord is suspect
		this->_atomicMessageStepP2( id );
	}

	return 0;
}

int AgentBase::_atomicMessagePermanentlySuspect( UUID *id, UUID *suspect ) {
	DataStream lds;
	mapAtomicMessage::iterator iM;
	AtomicMessage *am;
	std::list<UUID>::iterator iS;

	if ( DEBUG_ATOMIC_MESSAGE ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T(DEBUG_ATOMIC_MESSAGE_ID), &breakId );

		if ( breakId == *id )
			int i=0;
	}

	iM = this->atomicMsgs.find( *id );
	if ( iM == this->atomicMsgs.end() ) {
		return 1; // not found
	}
	am = &iM->second;

	// make sure we don't already permanently suspect this person 
	for ( iS = am->permanentSuspects.begin(); iS != am->permanentSuspects.end(); iS++ ) {
		if ( *iS == *suspect )
			return 0; // already a suspect
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentBase::_atomicMessagePermanentlySuspect: msg %s, new suspect %s", Log.formatUUID( LOG_LEVEL_VERBOSE, id ), Log.formatUUID( LOG_LEVEL_VERBOSE, suspect ) );

	// make sure we don't already suspect this person 
	for ( iS = am->suspects.begin(); iS != am->suspects.end(); iS++ ) {
		if ( *iS == *suspect )
			break; // already a suspect
	}
	if ( iS == am->suspects.end() )
		am->suspects.push_back( *suspect );

	am->permanentSuspects.push_back( *suspect );

	if ( am->coord == *this->getUUID() && am->phase == AM_PHASE_C1 ) { // we're the coord of our current round, check if this changes our status
		this->_atomicMessageStepC1( id );
	} else if ( am->coord == *suspect && am->phase == AM_PHASE_P2 ) { // our coord is suspect
		this->_atomicMessageStepP2( id );
	} else if ( am->delivered ) {
		this->_atomicMessageCheckCleanup( am );
	}

	return 0;
}

int AgentBase::_atomicMessageCheckCleanup( AtomicMessage *am, bool force ) {

	if ( !force && !am->delivered ) {
		return 0; // not delivered, can't cleanup for sure
	}

	// check to see if we have any permanent suspects in our decisions list
	std::list<UUID>::iterator iD;
	std::list<UUID>::iterator iS;
	int suspectOverlap = 0;
	for ( iD = am->decisions.begin(); iD != am->decisions.end(); iD++ ) {
		for ( iS = am->permanentSuspects.begin(); iS != am->permanentSuspects.end(); iS++ ) {
			if ( *iS == *iD ) {
				suspectOverlap++;
				break; // match
			}
		}
	}

	if ( force || am->decisions.size() + am->permanentSuspects.size() - suspectOverlap == am->targets.size() ) {

		if ( am->len ) {
			this->freeDynamicBuffer( am->dataRef );
		}

/*		if ( am->lenAbort ) {
			this->freeDynamicBuffer( am->dataAbortRef );
		}
*/
		this->atomicMsgs.erase( am->id ); // clean!
	}

	return 0;
}

int AgentBase::atomicMessageEvaluate( UUID *id, unsigned char message, char *data, unsigned int len ) {

	return 1; // vote yes
}

int AgentBase::atomicMessageDecided( char commit, UUID *id ) {

	return 0;
}

int AgentBase::agentLostNotification( UUID *agent ) {
	
	// check atomic messages
	mapAtomicMessage::iterator iAM;
	std::list<UUID>::iterator iTrg;
	for ( iAM = this->atomicMsgs.begin(); iAM != this->atomicMsgs.end(); iAM++ ) {
		for ( iTrg = iAM->second.targets.begin(); iTrg != iAM->second.targets.end(); iTrg++ ) { // check if this agent was a target
			if ( *iTrg == *agent )
				break;
		}
		if ( iTrg != iAM->second.targets.end() ) { // they were
//			iAM->second.targetsSuspect = true;

			// suspect locally
			this->_atomicMessageSuspect( (UUID *)&iAM->first, agent );
		}
	}

	// check ordered atomic message queues
	std::map<UUID, std::list<AtomicMessage>, UUIDless>::iterator iQ;
	std::list<AtomicMessage>::iterator iMq;
	for ( iQ = this->atomicMsgsOrderedQueue.begin(); iQ != this->atomicMsgsOrderedQueue.end(); iQ++ ) {
		for ( iMq = iQ->second.begin(); iMq != iQ->second.end(); iMq++ ) {
			for ( iTrg = iMq->targets.begin(); iTrg != iMq->targets.end(); iTrg++ ) { // check if this agent was a target
				if ( *iTrg == *agent )
					break;
			}
			if ( iTrg != iMq->targets.end() ) { // they were
//				iMq->targetsSuspect = true;
			}
		}		
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Timeout functions

UUID AgentBase::addTimeout( unsigned int period, int cbRef, void *data, int dataLen, bool stateSafe ) {
	spTimeoutEvent iter;
	spTimeoutEvent prev;
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent = (spTimeoutEvent)malloc(sizeof(sTimeoutEvent));
	if ( !timeoutEvent ) {
		Log.log( 0, "AgentBase::addTimeout: malloc failed" );
		return nilUUID;
	}

	// generate unique id
	apb->apbUuidCreate( &timeoutEvent->id );

	// insert data
	if ( dataLen ) {
		timeoutEvent->data = malloc(dataLen);
		if ( !timeoutEvent->data ) {
			Log.log( 0, "AgentBase::addTimeout: malloc data failed!" );
			free( timeoutEvent );
			return nilUUID;
		}
		memcpy( timeoutEvent->data, data, dataLen );
		timeoutEvent->dataLen = dataLen;
	} else {
		timeoutEvent->data = NULL;
		timeoutEvent->dataLen = 0;
	}

	timeoutEvent->period = period;
	apb->apb_ftime_s( &timeoutEvent->timeout );
	secs = period / 1000;
	msecs = period % 1000;
	timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
	timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
	timeoutEvent->cbRef = cbRef;

	timeoutEvent->stateSafe = stateSafe;

	// insert, always process earlier timeouts first (i.e. insert after timeouts with same time)
	iter = this->firstTimeout;
	prev = NULL;
	while ( iter != NULL ) {
		if ( iter->timeout.time > timeoutEvent->timeout.time )
			break;
		if ( iter->timeout.time == timeoutEvent->timeout.time 
		  && iter->timeout.millitm > timeoutEvent->timeout.millitm )
			break;
		prev = iter;
		iter = iter->next;
	}
	if ( prev ) prev->next = timeoutEvent;
	else this->firstTimeout = timeoutEvent;
	timeoutEvent->next = iter;

	return timeoutEvent->id;
}

int AgentBase::removeTimeout( UUID *id ) {
	spTimeoutEvent iter = this->firstTimeout;
	spTimeoutEvent prev = NULL;
	while ( iter != NULL && iter->id != *id ) {
		prev = iter;
		iter = iter->next;
	}

	if ( iter == NULL ) {
		return 1; // not found
	}

	// remove
	if ( prev ) prev->next = iter->next;
	else this->firstTimeout = iter->next;
	if ( iter->dataLen )
		free( iter->data );
	free( iter );

	return 0;
}

int AgentBase::readTimeout( UUID *id ) {
	_timeb t;

	spTimeoutEvent iter = this->firstTimeout;
	while ( iter != NULL && iter->id != *id ) {
		iter = iter->next;
	}

	if ( iter == NULL )
		return -1; // didn't find the timeout

	apb->apb_ftime_s( &t );

	return max( 0, (int)(iter->timeout.time - t.time)*1000 + iter->timeout.millitm - t.millitm );
}

void AgentBase::resetTimeout( UUID *id ) {
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent;

	spTimeoutEvent iter = this->firstTimeout;
	spTimeoutEvent prev = NULL;
	while ( iter != NULL && iter->id != *id ) {
		prev = iter;
		iter = iter->next;
	}

	if ( iter == NULL )
		return; // didn't find the timeout
	
	// remove from list
	if ( prev ) prev->next = iter->next;
	else this->firstTimeout = iter->next;

	// reset timeout
	timeoutEvent = iter;
	apb->apb_ftime_s( &timeoutEvent->timeout );
	secs = timeoutEvent->period / 1000;
	msecs = timeoutEvent->period % 1000;
	timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
	timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
	
	// reinsert, always process earlier timeouts first (i.e. insert after timeouts with same time)
	iter = this->firstTimeout;
	prev = NULL;
	while ( iter != NULL ) {
		if ( iter->timeout.time > timeoutEvent->timeout.time )
			break;
		if ( iter->timeout.time == timeoutEvent->timeout.time 
		  && iter->timeout.millitm > timeoutEvent->timeout.millitm )
			break;
		prev = iter;
		iter = iter->next;
	}
	if ( prev ) prev->next = timeoutEvent;
	else this->firstTimeout = timeoutEvent;
	timeoutEvent->next = iter;
}

void AgentBase::checkTimeouts() {
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent;
	spTimeoutEvent iter;
	spTimeoutEvent prev;
	_timeb t;

	apb->apb_ftime_s( &t );

	while ( this->firstTimeout ) {
		if ( this->firstTimeout->timeout.time > t.time )
			break;
		if ( this->firstTimeout->timeout.time == t.time 
		  && this->firstTimeout->timeout.millitm >= t.millitm )
			break;
	
		timeoutEvent = this->firstTimeout;
		this->firstTimeout = timeoutEvent->next;
		if ( this->callback[timeoutEvent->cbRef]->callback( timeoutEvent->data ) ) { // repeat timeout event
			secs = timeoutEvent->period / 1000;
			msecs = timeoutEvent->period % 1000;
			timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
			timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
			iter = this->firstTimeout;
			prev = NULL;
			while ( iter != NULL ) { // always process earlier timeouts first (i.e. insert after timeouts with same time)
				if ( iter->timeout.time > timeoutEvent->timeout.time )
					break;
				if ( iter->timeout.time == timeoutEvent->timeout.time 
				  && iter->timeout.millitm > timeoutEvent->timeout.millitm )
					break;
				prev = iter;
				iter = iter->next;
			}
			if ( prev ) prev->next = timeoutEvent;
			else this->firstTimeout = timeoutEvent;
			timeoutEvent->next = iter;
		} else { // remove timeout event
			if ( timeoutEvent->dataLen )
				free( timeoutEvent->data );
			free( timeoutEvent );
		}
	}
}



//-----------------------------------------------------------------------------
// Conversation functions

UUID AgentBase::conversationInitiate( int cbRef, unsigned int period, void *data, int len ) {
	mapConversation::iterator iterC;
	spConversation conv;
	UUID thread;

	apb->apbUuidCreate( &thread );

	conv = (spConversation)malloc(sizeof(sConversation));
	if ( !conv ) {
		Log.log( 0, "AgentBase::conversationInitiate: malloc failed" );
		return nilUUID;
	}

	// init
	conv->thread = thread;
	conv->line = 0;
	conv->cbRef = cbRef;
	if ( len ) {
		conv->data = malloc(len);
		if( !conv->data ) {
			Log.log( 0, "AgentBase::conversationInitiate: malloc failed for data" );
			free( conv );
			return nilUUID;
		}
		memcpy( conv->data, data, len );
	} else {
		conv->data = NULL;
	}
	conv->dataLen = len;
	conv->con = NULL;
	conv->response = NULL;
	conv->responseLen = 0;
	conv->expired = false;

	conv->timeout = this->addTimeout( period, AgentBase_CBR_cbConversationTimeout, &thread, sizeof(UUID) );
	if ( conv->timeout == nilUUID ) {
		Log.log( 0, "AgentBase::conversationInitiate: failed to add timeout" );
		if ( conv->data )
			free( conv->data );
		free( conv );
		return nilUUID;
	}

	this->conversations[thread] = conv;

	return thread;
}

int AgentBase::conversationResponse( UUID *thread, spConnection con, char *response, int len ) {
	mapConversation::iterator iterC = this->conversations.find(*thread);
	spConversation conv;

	if ( iterC == this->conversations.end() ) { // we don't know this conversation?!
		Log.log( 0, "AgentBase::conversationResponse: received response in unknown conversation (%s)", Log.formatUUID(0,thread) );
		return 1;
	}
	
	conv = iterC->second;

	if ( conv->expired ) {
		Log.log( 0, "AgentBase::conversationResponse: received response in expired conversation (%s)", Log.formatUUID(0,thread) );
		return 1;
	}

	conv->con = con;
	conv->response = response;
	conv->responseLen = len;

	if ( this->callback[conv->cbRef]->callback( conv )  ) {
		conv->con = NULL;
		conv->response = NULL;
		conv->responseLen = 0;
		
		this->resetTimeout( &conv->timeout );
	} else {
		if ( response == NULL )
			this->_conversationEnd( conv ); // end conversation but wait before clearing it
		else
			this->_conversationEnd( conv, true ); // end conversation and clear it immediately
	}

	return 0;
}

int AgentBase::conversationFinish( UUID *thread ) {
	mapConversation::iterator iterC = this->conversations.find(*thread);
	spConversation conv;

	if ( iterC == this->conversations.end() ) { // we don't know this conversation?!
		Log.log( 0, "AgentBase::conversationFinish: unknown conversation (%s)", Log.formatUUID(0,thread) );
		return 1;
	}
	
	conv = iterC->second;

	return this->_conversationEnd( conv );
}

int AgentBase::_conversationEnd( spConversation conv, bool immediate ) {
	
	conv->expired = true;

	if ( conv->dataLen ) {
		free( conv->data );
		conv->data = NULL;
		conv->dataLen = 0;
	}

	if ( conv->timeout != nilUUID ) {
		this->removeTimeout( &conv->timeout );
		conv->timeout = nilUUID;
	}

	if ( immediate ) {
		cbConversationErase( (void *)conv );
		return 0;
	}

	// don't erase it for a while since we might still be getting responses
	conv->timeout = this->addTimeout( CONVERSATION_ERASE_DELAY, AgentBase_CBR_cbConversationErase, &conv->thread, sizeof(UUID) );
	if ( conv->timeout == nilUUID ) {
		cbConversationErase( (void *)conv );
		return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Connection functions

int AgentBase::conInit( spConnection con, spAddressPort ap, int protocol, int maxRetries ) {
	con->markedForDeletion = false;
	con->socket = INVALID_SOCKET;
	strcpy_s( con->ap.address, sizeof(con->ap.address), ap->address );
	strcpy_s( con->ap.port, sizeof(con->ap.port), ap->port );
	con->protocol = protocol;
	con->raw = false;
	con->state = CON_STATE_DISCONNECTED;
	UuidCreateNil( &con->uuid );
	con->statistics.connected = false;
	con->statistics.latency = 0;
	con->statistics.reliability = CON_MAX_RELIABILITY_CHECKS;
	con->maxRetries = maxRetries;
	con->retries = 0;
	memset( con->reliabilityCheck, 0, sizeof(con->reliabilityCheck) );
	con->reliabilityInd = 0;
	memset( con->latencyCheck, 0, sizeof(con->latencyCheck) );
	con->latencyInd = -CON_MAX_LATENCY_CHECKS;
	apb->apb_ftime_s( &con->latencyBase );
	con->groups = 0;
	con->bufStart = 0;
	con->bufLen = 0;
	if ( con->protocol == IPPROTO_UDP )
		con->bufMax = CON_INIT_BUFSIZE*16; // extra space for UDP sockets because if the receive buf isn't large enough data can be lost
	else
		con->bufMax = CON_INIT_BUFSIZE;
	con->message = 0;
	con->waitingForData = 0;
	con->sendBuf = NULL;
	memset( con->watchers, 0, sizeof( con->watchers ) );
	
	con->subject = new FDSubject;
	con->subject->timeout = nilUUID;
	con->subject->count = 0;

	con->observer = new FDObserver;
	con->observer->status = CS_SUSPECT;
	con->observer->TDu = FD_DEFAULT_TDu;
	con->observer->TMRL = FD_DEFAULT_TMRL;
	con->observer->TMU = FD_DEFAULT_TMU;
	con->observer->l = 0;
	con->observer->sumD = 0;
	con->observer->d = 0;
	con->observer->r = FD_EVALUATE_QOS;
	con->observer->timeout = nilUUID;

	// allocate buf
	con->buf = (char *)malloc(con->bufMax);
	if ( !con->buf )
		return 1;

	return 0;
}

int AgentBase::conConnect( spConnection con, int protocol ) {
	int iResult;

	// prepare address
	ZeroMemory( &this->addr_hints, sizeof(this->addr_hints) );
	if ( protocol == IPPROTO_TCP ) {
		this->addr_hints.ai_family = AF_INET; //AF_UNSPEC; we don't want AF_INET6
		this->addr_hints.ai_socktype = SOCK_STREAM;
		this->addr_hints.ai_protocol = IPPROTO_TCP;
		this->addr_hints.ai_flags = NULL;
	} else {
		this->addr_hints.ai_family = AF_INET;
		this->addr_hints.ai_socktype = SOCK_DGRAM;
		this->addr_hints.ai_protocol = IPPROTO_UDP;
		this->addr_hints.ai_flags = NULL;
	}

	// Resolve the server address and port
	iResult = apb->apbgetaddrinfo( con->ap.address, con->ap.port, &this->addr_hints, &this->addr_result);
	if ( iResult != 0 ) {
		Log.log( 0, "AgentBase::conConnect: getaddrinfo failed: %d", iResult);
		return 1;
	}

	
	// create socket
	con->socket = apb->apbsocket( this->addr_result->ai_family, this->addr_result->ai_socktype, this->addr_result->ai_protocol);
	if ( con->socket == INVALID_SOCKET ) {
		Log.log( 0, "AgentBase::conConnect: Error at socket(): %ld", apb->apbWSAGetLastError());
		apb->apbfreeaddrinfo( this->addr_result );
		return 1;
	}

	// make socket non-blocking
	u_long enable = 1;
	iResult = apb->apbioctlsocket( con->socket, FIONBIO, &enable );
	if ( iResult == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::conConnect: Error at ioctlsocket(): %ld", apb->apbWSAGetLastError());
		apb->apbclosesocket( con->socket );
		con->socket = INVALID_SOCKET;
		apb->apbfreeaddrinfo( this->addr_result );
		return 1;
	}

	// connect
	iResult = apb->apbconnect( con->socket, this->addr_result->ai_addr, (int)this->addr_result->ai_addrlen );
	if ( iResult == SOCKET_ERROR ) {
		iResult = apb->apbWSAGetLastError();
		if ( iResult != WSAEWOULDBLOCK ) {
			Log.log( 0, "AgentBase::conConnect: Error at connect(): %ld", iResult);
			apb->apbclosesocket( con->socket );
			con->socket = INVALID_SOCKET;
			apb->apbfreeaddrinfo( this->addr_result );
			return 1;
		}
	}

	apb->apbfreeaddrinfo( this->addr_result );

	return 0;
}

int AgentBase::conSetState( spConnection con, unsigned int state ) {
	int i;

	con->state = state;

	if ( state == CON_STATE_CONNECTED )
		con->statistics.connected = true;
	else
		con->statistics.connected = false;
	
	for ( i=0; i<CON_MAX_WATCHERS; i++ )
		if ( con->watchers[i] ) this->callback[con->watchers[i]]->callback( CON_EVT_STATE, con );

	return 0;
}

int AgentBase::conGroupAddTest( spConnection con, unsigned int group ) {
	if ( this->group[group].fd_count == FD_SETSIZE ) {
		return 1;
	}
	
	return 0;
}

int AgentBase::conGroupAdd( spConnection con, unsigned int group ) {
	if ( this->group[group].fd_count == FD_SETSIZE ) {
		Log.log( 0, "AgentBase::conGroupAdd: group %d full, increase FD_SETSIZE", group );
		return 1;
	}

	if ( !(con->groups & (0x1 << group)) ) {
		FD_SET( con->socket, &this->group[group] );
		con->groups |= 0x1 << group;
	}
	
	return 0;
}

int AgentBase::conGroupRemove( spConnection con, unsigned int group ) {
	if ( con->groups & (0x1 << group) ) {
		FD_CLR( con->socket, &this->group[group] );
		con->groups &= ~(0x1 << group);
	}
	return 0;
}

int AgentBase::conAdd( spConnection con ) {
	
	apb->apbUuidCreate( &con->index );
	this->connection[con->index] = con;

	return 0;
}

spConnection AgentBase::conRemove( spConnection con ) {
	return this->conRemove( &con->index );
}

spConnection AgentBase::conRemove( UUID *conIndex ) {
	spConnection con = this->connection[*conIndex];
	
	this->connection.erase( *conIndex );

	return con;
}


int AgentBase::conDelete( spConnection con ) {
	int iResult, ret = 0;
	int err;
	unsigned int oldState = con->state;

	// destroy
	iResult = apb->apbshutdown( con->socket, SD_SEND );
	if ( iResult == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::conDelete: shutdown failed: %d", apb->apbWSAGetLastError() );
		ret = 1;
	}
	
	// make sure we're still blocking
	u_long enable = 1;
	iResult = apb->apbioctlsocket( con->socket, FIONBIO, &enable );
	if ( iResult == SOCKET_ERROR ) {
		Log.log( 0, "AgentBase::conDelete: Error at ioctlsocket(): %ld", apb->apbWSAGetLastError());
	} else {
		int i;
		int maxRetry;
		if ( isHost ) {
			maxRetry = 10;
		} else {
			maxRetry = 10000; // nothing better to do
		}
		for ( i=0; i<maxRetry; i++ ) { // block until we have graceful shutdown or there is an error
			iResult = apb->apbrecv( con->socket, con->buf, con->bufMax, NULL );
			if ( iResult == SOCKET_ERROR ) {
				err = apb->apbWSAGetLastError();
				if ( err != WSAEWOULDBLOCK ) {
					Log.log( 0, "AgentBase::conDelete: graceful exit failed: %d", apb->apbWSAGetLastError() );
					ret = 1;
					break;
				} else {
					apb->apbSleep( 1 ); // don't hog the cpu while we wait
				}
			} else if ( iResult == 0 ) {
				break;
			}
		}
		if ( i == maxRetry ) {
			Log.log( 0, "AgentBase::conDelete: gave up on graceful exit", apb->apbWSAGetLastError() );
		}
	}

	apb->apbclosesocket( con->socket );
	this->conRemove( con );
	if ( con->sendBuf )
		free( con->sendBuf );

	free( con->buf );

	if ( con->subject->timeout != nilUUID )
		this->removeTimeout( &con->subject->timeout );
	delete con->subject;
	
	if ( con->observer->timeout != nilUUID )
		this->removeTimeout( &con->observer->timeout );
	delete con->observer;

	free( con );

	return ret;
}


int AgentBase::conCheckBufSize( spConnection con ) {
	if ( con->bufMax - con->bufStart - con->bufLen == 0 ) { // we're out of room
		con->buf = (char *)realloc( con->buf, con->bufMax * 2 );
		con->bufMax *= 2;
		if ( con->buf == NULL ) {
			this->closeConnection( con );
			Log.log( 0, "AgentBase::conCheckBufSize: realloc failed" );
			return 1;
		}
	}
	return 0;
}

int AgentBase::conReceiveData( spConnection con ) {

	int iResult = apb->apbrecv( con->socket, con->buf + con->bufStart + con->bufLen, con->bufMax - con->bufStart - con->bufLen, NULL );

	apb->apb_ftime_s( &con->recvTime ); // store the recive time

	if ( iResult == SOCKET_ERROR ) {
		iResult = apb->apbWSAGetLastError();
		switch ( iResult ) {
		case WSAEWOULDBLOCK:
			return 0;
		case WSAECONNRESET:
			if ( con->groups & CON_GROUPF_PASSIVE || this->retryConnection( con ) ) {
				this->closeConnection( con );
			}
			return 0;
		case WSAEMSGSIZE:
			if ( con->protocol == IPPROTO_UDP ) {
				this->closeConnection( con );
				Log.log( 0, "AgentBase::conReceiveData: WSAEMSGSIZE error on UDP socket, data may have been lost" );
				return 1;
			}
			con->bufLen = con->bufMax - con->bufStart;
			if ( con->bufStart != 0 ) { // shift buf back to beginning and try to read more data
				int i;
				for ( i=0; i<con->bufLen; i++ )
					con->buf[i] = con->buf[i+con->bufStart];
				con->bufStart = 0;
				return this->conReceiveData( con );
			} else { 
				int ret;
				if ( con->raw )	ret = this->conProcessRaw( con );
				else			ret = this->conProcessStream( con );
				if ( ret ) { // error occured
					this->closeConnection( con );
					Log.log( 0, "AgentBase::conReceiveData: error processing data" );
					return 1;
				} else if ( con->bufLen == con->bufMax ) { // buffer is still too small 
					con->buf = (char *)realloc( con->buf, con->bufMax * 2 );
					con->bufMax *= 2;
					if ( con->buf == NULL ) {
						this->closeConnection( con );
						Log.log( 0, "AgentBase::conReceiveData: realloc failed" );
						return 1;
					}
					return this->conReceiveData( con );
				} else { // we managed to free some space, go get more data
					return this->conReceiveData( con );
				}
			}
		default:
			this->closeConnection( con );
			Log.log( 0, "AgentBase::conReceiveData: unhandled error, %d", iResult );
			return 1; // unhandled error
		}
	} else if ( iResult == 0 ) { // connection closed
		this->closeConnection( con );
		Log.log( 0, "AgentBase::conReceiveData: connection closed (%s)", Log.formatUUID(0,&con->uuid) );
		return 0;
	}
	con->bufLen += iResult;


	int ret;
	if ( con->raw )	ret = this->conProcessRaw( con );
	else			ret = this->conProcessStream( con );
	if ( ret ) { // error occured
		this->closeConnection( con );
		Log.log( 0, "AgentBase::conReceiveData: error processing data" );
		return 1;
	} 

	return ret;
}

int AgentBase::conProcessRaw( spConnection con ) {

	if ( con->protocol == IPPROTO_UDP && con->bufLen > con->bufMax / 2 ) { // play it safe for UDP streams
		con->buf = (char *)realloc( con->buf, con->bufMax * 2 );
		con->bufMax *= 2;
		if ( con->buf == NULL ) {
			this->closeConnection( con );
			Log.log( 0, "AgentBase::conProcessRaw: realloc failed" );
			return 1;
		}
	}

	if ( con->bufLen == 0 ) { // buffer is empty
		con->bufStart = 0;
	} else if ( con->bufStart > con->bufMax / 2 ) { // shift buffer back to beginning
		int i;
		for ( i=0; i<con->bufLen; i++ )
			con->buf[i] = con->buf[i+con->bufStart];
		con->bufStart = 0;
	} else {
		return this->conCheckBufSize( con ); // make sure we have space available
	}

	return 0;
}

int AgentBase::conProcessStream( spConnection con ) {
	unsigned int msgSize;
	
	if ( con->waitingForData == -1 ) { // msg size in char
		if ( con->bufLen ) {
			con->waitingForData = (unsigned char)*(con->buf+con->bufStart);
			con->bufStart++;
			con->bufLen--;
		} else {
			return this->conCheckBufSize( con ); // make sure we have space available and wait for more data
		}
	} else if ( con->waitingForData == -2 ) { // msg size in unsigned int
		if ( con->bufLen >= 4 ) {
			con->waitingForData = *(unsigned int *)(con->buf+con->bufStart);
			con->bufStart += 4;
			con->bufLen -= 4;
		} else {
			return this->conCheckBufSize( con ); // make sure we have space available and wait for more data
		}
	} else if ( con->waitingForData == -3 ) { // if byte == 0xFF then size follows in an int, else size is the byte
		if ( con->bufLen ) {
			unsigned char size = (unsigned char)*(con->buf+con->bufStart);
			con->bufStart++;
			con->bufLen--;
			if ( size == 0xFF ) {
				con->waitingForData = -2;
				return this->conProcessStream( con );
			} else if ( size > 0 ) {
				con->waitingForData = size;
			} else {
				this->conProcessMessage( con, con->message, NULL, 0 );
				con->lastMessage = con->message;
				con->lastMsgSize = 0;
				con->waitingForData = 0;
				goto doneMessage;
			}
		} else {
			return this->conCheckBufSize( con ); // make sure we have space available and wait for more data
		}
	}
	
	if ( con->waitingForData ) {
		if ( con->bufLen >= con->waitingForData ) {
			this->conProcessMessage( con, con->message, con->buf+con->bufStart, con->waitingForData );
			con->lastMessage = con->message;
			con->lastMsgSize = con->waitingForData;
			con->bufStart += con->waitingForData;
			con->bufLen -= con->waitingForData;
			con->waitingForData = 0;
		} else {
			return this->conCheckBufSize( con ); // make sure we have space available and wait for more data
		}
	} else {
		con->message = (unsigned char)*(con->buf+con->bufStart);
		con->bufStart++;
		con->bufLen--;
		if ( con->message < MSG_COMMON )
			msgSize = MSG_SIZE[con->message];
		else
			msgSize = -3;

		if ( msgSize == 0 ) {
			this->conProcessMessage( con, con->message, NULL, 0 );
			con->lastMessage = con->message;
			con->lastMsgSize = 0;
		} else {
			con->waitingForData = msgSize;
		}
	}

doneMessage:

	if ( !con->bufLen ) { // reset bufStart
		con->bufStart = 0;
		return 0;
	}
	
	if ( con->bufStart > con->bufMax / 2 ) { // shift buffer back to beginning
		int i;
		for ( i=0; i<con->bufLen; i++ )
			con->buf[i] = con->buf[i+con->bufStart];
		con->bufStart = 0;
	}

	return this->conProcessStream( con );
}

int AgentBase::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	STATE(AgentBase)->haveReturn = false; // clear the return address flag

	switch ( message ) {
	case MSG_ACK:
		// do nothing
		break;
	case MSG_RACK:
		// send ACK
		this->sendMessage( con, MSG_ACK );
		break;
	case MSG_ACKEX:
		// TODO: do something
		break;
	case MSG_RACKEX:
		// send RACKEX
		this->sendMessage( con, MSG_ACKEX, data, len );
		break;
	case MSG_FORWARD:
		{
			unsigned char msg;
			int offset, msgSize;
			UUID uuid;
			lds.setData( data, min( len, sizeof(UUID)*2 + 1 + 1 + 1 + 4 ) ); // we need at most this many bytes to sort this out
			lds.unpackUUID( &uuid );
			if ( STATE(AgentBase)->uuid == uuid ) { // message is for us
				STATE(AgentBase)->haveReturn = lds.unpackChar();
				offset = sizeof(UUID) + 1;
				if ( STATE(AgentBase)->haveReturn ) {
					lds.unpackUUID( &STATE(AgentBase)->returnAddress );
					offset += sizeof(UUID);
				}

				// figure out message and process it
				msg = (unsigned char)lds.unpackChar();
				offset++;
				if ( msg < MSG_COMMON )
					msgSize = MSG_SIZE[msg];
				else
					msgSize = -3;

				if ( msgSize < (unsigned int)-3 ) {
					return this->conProcessMessage( con, msg, data + offset, msgSize );
				} else if ( msgSize == -1 ) {
					return this->conProcessMessage( con, msg, data + offset + 1, (unsigned char)lds.unpackChar() );
				} else if ( msgSize == -2 ) {
					return this->conProcessMessage( con, msg, data + offset + 4, (unsigned int)lds.unpackInt32() );
				} else { // msgSize == -3
					msgSize = (unsigned char)lds.unpackChar();
					if ( msgSize < 0xFF ) {
						return this->conProcessMessage( con, msg, data + offset + 1, msgSize );
					} else {
						return this->conProcessMessage( con, msg, data + offset + 1 + 4, (unsigned int)lds.unpackInt32() );
					}
				} 
			}
			lds.unlock();
		}
		break;
	case MSG_RESPONSE:
		{
			UUID thread = *(UUID*)data;
			this->conversationResponse( &thread, con, data, len );
		}
		break;
	case MSG_ATOMIC_MSG:
		{
			lds.setData( data, len );
			this->_receiveAtomicMessage( &lds );
			lds.unlock();
		}
		break;
	case MSG_ATOMIC_MSG_REORDER:
		{
			UUID uuid, queue, q;
			int order;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &queue );
			lds.unpackUUID( &q );
			order = lds.unpackInt32();
			lds.unlock();
			this->_atomicMessageCheckOrder( &uuid, &queue, order );
		}
		break;
	case MSG_ATOMIC_MSG_ESTIMATE:
		{
			UUID uuid, queue, q;
			int order;
			int round;
			char estCommit;
			int timestamp;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &queue );
			lds.unpackUUID( &q );
			order = lds.unpackInt32();
			round = lds.unpackInt32();
			estCommit = lds.unpackChar();
			timestamp = lds.unpackInt32();
			if ( queue != nilUUID )
				this->_atomicMessageCheckOrder( &uuid, &queue, order );
			this->_atomicMessageStepC1( &uuid, &q, order, round, estCommit, timestamp );
			lds.unlock();
		}
		break;
	case MSG_ATOMIC_MSG_PROPOSAL:
		{
			UUID uuid, queue, q;
			int order;
			int round;
			char estCommit;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &queue );
			lds.unpackUUID( &q );
			order = lds.unpackInt32();
			round = lds.unpackInt32();
			estCommit = lds.unpackChar();
			if ( queue != nilUUID )
				this->_atomicMessageCheckOrder( &uuid, &queue, order );
			this->_atomicMessageStepP2( &uuid, &q, order, round, estCommit );
			lds.unlock();
		}
		break;
	case MSG_ATOMIC_MSG_ACK:
		{
			UUID uuid, queue, q;
			int order;
			int round;
			char ack;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &queue );
			lds.unpackUUID( &q );
			order = lds.unpackInt32();
			round = lds.unpackInt32();
			ack = lds.unpackChar();
			if ( queue != nilUUID )
				this->_atomicMessageCheckOrder( &uuid, &queue, order );
			this->_atomicMessageStepC2( &uuid, &q, order, round, ack );
			lds.unlock();
		}
		break;
	case MSG_ATOMIC_MSG_DECISION:
		{
			UUID uuid, queue, q;
			int order;
			int round;
			char commit;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &queue );
			lds.unpackUUID( &q );
			order = lds.unpackInt32();
			round = lds.unpackInt32();
			commit = lds.unpackChar();
			if ( queue != nilUUID )
				this->_atomicMessageCheckOrder( &uuid, &queue, order );
			this->_atomicMessageDecide( &uuid, &q, order, round, commit );
			lds.unlock();
		}
		break;
	case MSG_ATOMIC_MSG_SUSPECT:
		{
			UUID uuid, suspect;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &suspect );
			lds.unlock();
			this->_atomicMessageSuspect( &uuid, &suspect );
		}
		break;
	case MSG_FD_ALIVE:
		{
			unsigned int count;
			unsigned int period;
			_timeb sendTime;
			lds.setData( data, len );
			count = lds.unpackUInt32();
			period = lds.unpackUInt32();
			sendTime = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			this->conFDAlive( con, count, period, &sendTime );
		}
		break;
	case MSG_FD_QOS:
		{
			unsigned int newPeriod;
			lds.setData( data, len );
			newPeriod = lds.unpackUInt32();
			this->conFDQOS( con, newPeriod );
		}
		break;
	case MSG_AGENT_START:
		{
			char *missionFile;
			lds.setData( data, len );
			missionFile = lds.unpackString();
			this->start( missionFile );
			lds.unlock();
		}
		break;
	case MSG_AGENT_STOP:
		if ( !this->isHost )
			this->sendMessage( this->hostCon, MSG_AGENT_SHUTDOWN );
		STATE(AgentBase)->stopFlag = true;
		break;
	case MSG_AGENT_LOST:
		this->agentLostNotification( (UUID *)data );
		break;
	case MSG_AGENT_FREEZE:
		this->freeze( (UUID *)data );
		break;
	case MSG_AGENT_THAW:
		lds.setData( data, len );
		this->thaw( &lds );
		lds.unlock();
		break;
	case MSG_AGENT_RESUME:
		lds.setData( data, len );
		this->resume( &lds );
		lds.unlock();
		break;
	case MSG_AGENT_RECOVER:
		lds.setData( data, len );
		this->recover( &lds );
		lds.unlock();
		break;
	case MSG_AGENT_RECOVER_FINISH:
		this->recoveryFinish();
		break;
	case MSG_AGENT_SIMULATECRASH:
		STATE(AgentBase)->simulateCrash = true;
		break;
	case MSG_AGENT_SETPARENT:
		this->setParentId( (UUID *)data );
		break;
	case MSG_AGENT_INSTANCE:
		this->setInstance( *(char *)data );
		break;
	case MSG_DDB_NOTIFY:
		{
//			UUID key = *(UUID*)data;
//			std::map<UUID, char, UUIDless>::iterator iN = this->notificationFilter.find( key );
//			if ( iN == this->notificationFilter.end() ) { // haven't had this one
				this->ddbNotification( data, len );
//				this->notificationFilter[key] = 1;
//				this->addTimeout( DDB_NOTIFICATION_FILTER_EXPIRY, AgentBase_CBR_cbNotificationFilterExpiry, &key, sizeof(UUID) );
//			}
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}

int AgentBase::sendRaw( spConnection con, char *data, unsigned int len ) {
	char *bufStart;
	unsigned int bufLen;

	if ( con->sendBuf ) { // data already waiting, add to queue
		bufLen = con->sendBufLen + len;

		if ( con->sendBufMax < (int)bufLen ) {
			con->sendBuf = (char *)realloc( con->sendBuf, bufLen + 1024 );
			con->sendBufMax = bufLen + 1024;
			if ( !con->sendBuf ) {
				this->closeConnection( con );
				Log.log( 0, "AgentBase::sendRaw: extend sendBuf, realloc failed" );
				return 1;
			}
		}
		bufStart = con->sendBuf;
	} else {
		bufStart = data;
		bufLen = len;
	}

	// send buffer
	return this->conSendBuffer( con, bufStart, bufLen );
}

int AgentBase::sendMessage( spConnection con, unsigned char message, UUID *forwardAddress, UUID *returnAddress ) {
	return this->sendMessage( con, message, NULL, 0, forwardAddress, returnAddress );
}

int AgentBase::sendMessage( spConnection con, unsigned char message, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	unsigned int msgSize;
	char header[64], *headerPtr = header;
	unsigned int headerLen = 0;

	if ( message >= MSG_COMMON ) {
		Log.log( 0, "AgentBase::sendMessage: message out of range (%d >= MSG_COMMON), perhaps you should be using sendMessageEx?", message );
		return 1;
	}

	msgSize = MSG_SIZE[message];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentBase::sendMessage: message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	if ( con == NULL || con->markedForDeletion )
		return 0; // no connection to send on

	if ( forwardAddress ) { // construct the forwarding header
		unsigned int combinedLen;

		// start off headerLen as just the addresses, then add msg id and msg size later
		if ( !returnAddress )
			headerLen = sizeof(UUID) + 1;
		else
			headerLen = sizeof(UUID) + 1 + sizeof(UUID);

		if ( msgSize == -1 ) {
			combinedLen = headerLen + 1 + 1 + len;
		} else if ( msgSize == -2 ) {
			combinedLen = headerLen + 1 + 4 + len;
		} else if ( msgSize == -3 ) {
			if ( len < 0xFF ) combinedLen = headerLen + 1 + 1 + len;
			else combinedLen = headerLen + 1 + 5 + len;
		} else {
			combinedLen = headerLen + 1 + len;
		}

		// msg id
		*headerPtr = MSG_FORWARD;
		headerPtr++;
		headerLen++;
		
		// msg size -3 mode
		if ( combinedLen < 0xFF ) {
			*headerPtr = combinedLen;
			headerPtr++;
			headerLen++;
		} else {
			*headerPtr = (unsigned char)0xFF;
			headerPtr++;
			memcpy( headerPtr, (void *)&combinedLen, 4 );
			headerPtr += 4;
			headerLen += 5;
		}

		// msg data
		memcpy( headerPtr, forwardAddress, sizeof(UUID) );
		headerPtr += sizeof(UUID);
		if ( returnAddress ) {
			*headerPtr = true;
			memcpy( headerPtr + 1, returnAddress, sizeof(UUID) );
		} else {
			*headerPtr = false;
		}
	}

	return this->conSendMessage( con, message, data, len, msgSize, header, headerLen );
}

int AgentBase::sendMessage( UUID *group, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	unsigned int msgSize;
	char header[64], *headerPtr = header;
	unsigned int headerLen = 0;

	if ( message >= MSG_COMMON ) {
		Log.log( 0, "AgentBase::sendMessage(group): message out of range (%d >= MSG_COMMON), perhaps you should be using sendMessageEx?", message );
		return 1;
	}

	msgSize = MSG_SIZE[message];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentBase::sendMessage(group): message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	// pack MSG__GROUP_SEND
	lds.reset();
	lds.packUUID( group );
	lds.packUChar( message );
	lds.packUInt32( len );
	lds.packData( data, len );
	lds.packUInt32( msgSize );

	if ( this->isHost ) { // host
		// process the message
		this->conProcessMessage( NULL, MSG__GROUP_SEND, lds.stream(), lds.length() );
	} else { // agent
		// send it to the host
		this->sendMessage( this->hostCon, MSG__GROUP_SEND, lds.stream(), lds.length() );
	}
	lds.unlock();

	return 0;
}

int AgentBase::sendMessageEx( spConnection con, MSGEXargs, UUID *forwardAddress, UUID *returnAddress ) {
	return this->sendMessageEx( con, MSGEXpassargs, NULL, 0, forwardAddress, returnAddress );
}

int AgentBase::sendMessageEx( spConnection con, MSGEXargs, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	unsigned int msgSize;
	char header[64], *headerPtr = header;
	unsigned int headerLen = 0;
	
	if ( message >= msg_last ) {
		Log.log( 0, "AgentBase::sendMessageEx: message out of range (%d >= msg_last)", message );
		return 1;
	} else if ( message < msg_first ) {
		Log.log( 0, "AgentBase::sendMessageEx: message out of range (%d < msg_first)", message );
		return 1;
	}

	msgSize = msg_size[message-msg_first];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentBase::sendMessageEx: message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	if ( con == NULL || con->markedForDeletion )
		return 0; // no connection to send on

	msgSize = -3; // all message ex are sent with size -3

	if ( forwardAddress ) { // construct the forwarding header
		unsigned int combinedLen;

		// start off headerLen as just the addresses, then add msg id and msg size later
		if ( !returnAddress )
			headerLen = sizeof(UUID) + 1;
		else
			headerLen = sizeof(UUID) + 1 + sizeof(UUID);

		if ( msgSize == -1 ) {
			combinedLen = headerLen + 1 + 1 + len;
		} else if ( msgSize == -2 ) {
			combinedLen = headerLen + 1 + 4 + len;
		} else if ( msgSize == -3 ) {
			if ( len < 0xFF ) combinedLen = headerLen + 1 + 1 + len;
			else combinedLen = headerLen + 1 + 5 + len;
		} else {
			combinedLen = headerLen + 1 + len;
		}

		// msg id
		*headerPtr = MSG_FORWARD;
		headerPtr++;
		headerLen++;
		
		// msg size -3 mode
		if ( combinedLen < 0xFF ) {
			*headerPtr = combinedLen;
			headerPtr++;
			headerLen++;
		} else {
			*headerPtr = (unsigned char)0xFF;
			headerPtr++;
			memcpy( headerPtr, (void *)&combinedLen, 4 );
			headerPtr += 4;
			headerLen += 5;
		}

		// msg data
		memcpy( headerPtr, forwardAddress, sizeof(UUID) );
		headerPtr += sizeof(UUID);
		if ( returnAddress ) {
			*headerPtr = true;
			memcpy( headerPtr + 1, returnAddress, sizeof(UUID) );
		} else {
			*headerPtr = false;
		}
	}

	return conSendMessage( con, message, data, len, msgSize, header, headerLen );
}

int AgentBase::sendMessageEx( UUID *group, MSGEXargs, char *data, unsigned int len ) {
	DataStream lds;
	unsigned int msgSize;
	
	if ( message >= msg_last ) {
		Log.log( 0, "AgentBase::sendMessageEx(group): message out of range (%d >= msg_last)", message );
		return 1;
	} else if ( message < msg_first ) {
		Log.log( 0, "AgentBase::sendMessageEx(group): message out of range (%d < msg_first)", message );
		return 1;
	}

	msgSize = msg_size[message-msg_first];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentBase::sendMessageEx(group): message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	msgSize = -3; // all message ex are sent with size -3

	// pack MSG__GROUP_SEND
	lds.reset();
	lds.packUUID( group );
	lds.packUChar( message );
	lds.packUInt32( len );
	lds.packData( data, len );
	lds.packUInt32( msgSize );

	if ( this->isHost ) { // host
		// process the message
		this->conProcessMessage( NULL, MSG__GROUP_SEND, lds.stream(), lds.length() );
	} else { // agent
		// send it to the host
		this->sendMessage( this->hostCon, MSG__GROUP_SEND, lds.stream(), lds.length() );
	}
	lds.unlock();

	return 0;
}

int AgentBase::replyLastMessage( spConnection con, unsigned char message, UUID *returnAddress ) {
	return this->replyLastMessage( con, message, NULL, 0, returnAddress );
}

int AgentBase::replyLastMessage( spConnection con, unsigned char message, char *data, unsigned int len, UUID *returnAddress ) {
		
	if ( !STATE(AgentBase)->haveReturn ) {
		Log.log( 0, "AgentBase::replyLastMessage: return address isn't available" );
		return 1;
	}

	// NOTE that STATE(AgentBase)->returnAddress is the address that we are replying to, while returnAddress is the return address for this message!
	return sendMessage( con, message, data, len, &STATE(AgentBase)->returnAddress, returnAddress );
}

int AgentBase::replyLastMessageEx( spConnection con, MSGEXargs, UUID *returnAddress ) {
	return this->replyLastMessageEx( con, MSGEXpassargs, NULL, 0, returnAddress );
}

int AgentBase::replyLastMessageEx( spConnection con, MSGEXargs, char *data, unsigned int len, UUID *returnAddress ) {
		
	if ( !STATE(AgentBase)->haveReturn ) {
		Log.log( 0, "AgentBase::replyLastMessageEx: return address isn't available" );
		return 1;
	}

	// NOTE that STATE(AgentBase)->returnAddress is the address that we are replying to, while returnAddress is the return address for this message!
	return sendMessageEx( con, MSGEXpassargs, data, len, &STATE(AgentBase)->returnAddress, returnAddress );
}

UUID AgentBase::delayMessage( unsigned int delay, spConnection con, unsigned char message, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_NORMAL, delay, &con->index, message, NULL, 0, forwardAddress, returnAddress, NULL, 0, 0 );
}

UUID AgentBase::delayMessage( unsigned int delay, spConnection con, unsigned char message, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_NORMAL, delay, &con->index, message, data, len, forwardAddress, returnAddress, NULL, 0, 0 );
}

UUID AgentBase::delayMessage( unsigned int delay, UUID *group, unsigned char message, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_GROUP, delay, group, message, data, len, forwardAddress, returnAddress, NULL, 0, 0 );
}

UUID AgentBase::delayMessageEx( unsigned int delay, spConnection con, MSGEXargs, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_EX, delay, &con->index, message, NULL, 0, forwardAddress, returnAddress, msg_size, msg_first, msg_last );
}

UUID AgentBase::delayMessageEx( unsigned int delay, spConnection con, MSGEXargs, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_EX, delay, &con->index, message, data, len, forwardAddress, returnAddress, msg_size, msg_first, msg_last );
}

UUID AgentBase::delayMessageEx( unsigned int delay, UUID *group, MSGEXargs, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress ) {
	return this->_delayMessage( DMT_EX_GROUP, delay, group, message, data, len, forwardAddress, returnAddress, msg_size, msg_first, msg_last );
}

int AgentBase::delayMessageAbort( UUID *messageId ) {
	spTimeoutEvent iter = this->firstTimeout;
	spTimeoutEvent prev = NULL;
	while ( iter != NULL && iter->id != *messageId ) {
		prev = iter;
		iter = iter->next;
	}

	if ( iter == NULL ) {
		return 1; // not found
	}

	// free message data
	if ( iter->dataLen != sizeof(DelayedMessage) ) {
		return 1; // something wrong with the timeout
	}

	DelayedMessage *dm = (DelayedMessage *)iter->data;
	if ( dm->len )
		freeDynamicBuffer( dm->bufRef );
	
	// remove timeout
	this->removeTimeout( messageId );

	return 0;
}

UUID AgentBase::_delayMessage( int type, unsigned int delay, UUID *conId, unsigned char message, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress, unsigned int *msg_size, unsigned int msg_first, unsigned int msg_last ) {
	DelayedMessage dm;

	if ( len ) {
		dm.bufRef = newDynamicBuffer(len);
		if ( dm.bufRef == nilUUID ) {
			return nilUUID; // malloc failed
		}

		this->delayedMessageBufRefs[dm.bufRef] = true;

		memcpy( getDynamicBuffer(dm.bufRef), data, len );
	}

	dm.type = type;

	if ( type == DMT_NORMAL || type == DMT_EX ) {
		if ( this->connection[*conId] == this->hostCon ) { // special case
			dm.conId = nilUUID; 
		} else {
			dm.conId = *conId;
		}

		if ( this->connection.find( *conId ) == this->connection.end() ) {
			return nilUUID; // bad connection
		}
		dm.toId = this->connection[*conId]->uuid;
	} else { // DMT_GROUP, DMT_GROUP_EX
		dm.conId = *conId;
		dm.toId = nilUUID;
	}

	dm.message = message;
	dm.len = len;
	
	if ( forwardAddress )
		dm.forwardAddress = *forwardAddress;
	else
		UuidCreateNil( &dm.forwardAddress );
	
	if ( returnAddress )
		dm.returnAddress = *returnAddress;
	else
		UuidCreateNil( &dm.returnAddress );

	dm.msg_size = msg_size;
	dm.msg_first = msg_first;
	dm.msg_last = msg_last;

	return this->addTimeout( delay, AgentBase_CBR_cbDelayedMessage, &dm, sizeof(DelayedMessage) );
}

int AgentBase::_queueLocalMessage( unsigned char message, char *data, unsigned int len ) {
	LocalMessage M;

	M.message = message;
	M.len = len;
	if ( len ) {
		M.dataRef = this->newDynamicBuffer( len );
		memcpy( this->getDynamicBuffer( M.dataRef ), data, len );
	}

	this->localMessageQueue.push_back( M );

	if ( STATE(AgentBase)->localMessageTimeout == nilUUID )
		STATE(AgentBase)->localMessageTimeout = this->addTimeout( 0, AgentBase_CBR_cbLocalMessage );

	return 0;
}


int AgentBase::conIncreaseSendBuffer( spConnection con, unsigned int minSize ) {
	unsigned int newSize = con->sendBufMax;
	char *newBuf;
	while ( newSize < minSize ) {
		if ( newSize >= 1024*1024  ) {
			newSize += 1024*1024;
		} else {
			newSize *= 2;
		}
	}
	newBuf = (char *)malloc( sizeof(char)*newSize );
	if ( !newBuf ) {
		this->closeConnection( con );
		throw "AgentBase::conIncreaseSendBuffer: Memory reallocation failure!";
		return 1;
	}
	memcpy( newBuf, con->sendBuf, sizeof(char)*con->sendBufLen );
	con->sendBufMax = newSize;
	free( con->sendBuf );
	con->sendBuf = newBuf;
	return 0;
}

int AgentBase::conSendMessage( spConnection con, unsigned char message, char *data, unsigned int len, unsigned int msgSize, char *header, unsigned int headerLen ) {
	char buf[256], *bufPtr, *bufStart, *tempBuf = NULL;
	unsigned int bufLen;
	int ret;

	if ( con == NULL || con->markedForDeletion ) {
		//Log.log( 0, "AgentBase::conSendMessage: can't send on a null connection pointer!" );
		return 1;
	}

	if ( msgSize == -3 && len < 0xFF ) 
		msgSize = -1;

	if ( con->sendBuf ) { // data already waiting, add to queue
		bufLen = con->sendBufLen + headerLen + 1; // add char for message id		
		if ( msgSize == -1 ) {
			bufLen += 1 + len; // message size contained in char
		} else if ( msgSize == -2 ) {
			bufLen += 4 + len; // message size contained in unsigned int
		} else if ( msgSize == -3 ) {
			bufLen += 1 + 4 + len; // message size contained in byte flag + unsigned int
		} else {
			bufLen += msgSize;
		}

		if ( con->sendBufMax < (int)bufLen ) {
			if ( this->conIncreaseSendBuffer( con, bufLen ) )
				return 1;
		}
		bufPtr = con->sendBuf + con->sendBufLen;
		bufStart = con->sendBuf;
	} else {
		bufPtr = bufStart = buf;
		bufLen = headerLen + 1;
		if ( msgSize == -1 ) {
			bufLen += 1 + len; // message size contained in char
		} else if ( msgSize == -2 ) {
			bufLen += 4 + len; // message size contained in unsigned int
		} else if ( msgSize == -3 ) {
			bufLen += 1 + 4 + len; // message size contained in byte flag + unsigned int
		} else {
			bufLen += msgSize;
		}
		if ( bufLen > sizeof( buf ) ) {
			bufPtr = bufStart = tempBuf = (char *)malloc( bufLen );
			if ( !bufPtr ) {
				Log.log( 0, "AgentBase::conSendMessage: tempBuf, malloc failed" );
				return 1;
			}
		}
	}
	
	// write header
	if ( headerLen ) {
		memcpy( bufPtr, header, headerLen );
		bufPtr += headerLen;
	}

	// write msg id
	*bufPtr = message;
	bufPtr++;

	if ( msgSize == -1 ) { // write msg size as unsigned char
		*bufPtr = (unsigned char)len;
		bufPtr++;
		msgSize = len;
	} else if ( msgSize == -2 ) { // write msg size as unsigned int
		memcpy( bufPtr, &len, 4 );
		bufPtr += 4;
		msgSize = len;
	} else if ( msgSize == -3 ) { // write msg size as byte flag + unsigned int
		*bufPtr = (unsigned char)0xFF;
		bufPtr++;
		memcpy( bufPtr, &len, 4 );
		bufPtr += 4;
		msgSize = len;
	}

	// write data
	if ( msgSize )
		memcpy( bufPtr, data, msgSize );

	// send buffer
	ret = this->conSendBuffer( con, bufStart, bufLen );

	if ( tempBuf )
		free( tempBuf );

	return ret;
}

int AgentBase::conSendBuffer( spConnection con ) {
	return this->conSendBuffer( con, con->sendBuf, con->sendBufLen );
}

int AgentBase::conSendBuffer( spConnection con, char *buf, unsigned int bufLen ) {
	int iResult;

	iResult = apb->apbsend( con->socket, buf, bufLen, NULL );
	if ( iResult == SOCKET_ERROR ) {
		int err = apb->apbWSAGetLastError();
		if ( err == WSAEWOULDBLOCK ) {
			iResult = 0; // no data sent
		} else { // unhandled error
			//this->conReceiveData( con ); // last chance to get data off the connection
			//this->closeConnection( con );
			
			// try leaving connection alone, only close on read errors so we can be sure we've received as much data as possible?
			Log.log( 0, "AgentBase::conSendBuffer: socket error, %d", err );
			return 1; // unhandled error
		}
	} 
	if ( iResult != bufLen ) {		
		bufLen -= iResult;
		if ( con->sendBuf ) { // we've already got a send buf
			if ( iResult ) {
				unsigned int i;
				for ( i=0; i<bufLen; i++ )
					con->sendBuf[i] = con->sendBuf[i+iResult]; // shift data to front of buffer
			}
			con->sendBufLen = bufLen;
		} else {
			con->sendBuf = (char *)malloc( bufLen + 1024 );
			con->sendBufLen = bufLen;
			con->sendBufMax = bufLen + 1024;
			if ( !con->sendBuf ) {
				this->closeConnection( con );
				Log.log( 0, "AgentBase::conSendBuffer: sendBuf, malloc failed" );
				return 1;
			}
			memcpy( con->sendBuf, buf+iResult, bufLen );
			
			if ( this->conGroupAdd( con, CON_GROUP_WRITE ) ) { // add connection to write group
				this->closeConnection( con );
				return 1;
			}
		}
	} else if ( con->sendBuf ) { // sendBuf is now empty
		free( con->sendBuf );
		con->sendBuf = NULL;
		con->sendBufLen = 0;

		this->conGroupRemove( con, CON_GROUP_WRITE ); // remove from write group
	}

	// Log.log( 0, "AgentBase::conSendBuffer: con %s, uuid %s, iResult %d, bufLen %d, sendBuf %d, sendBufLen %d", Log.formatUUID( 0, &con->index ), Log.formatUUID( 0, &con->uuid ), iResult, bufLen, (con->sendBuf == NULL ? 0:1), con->sendBufLen );
				
	return 0;
}


int AgentBase::conStartLatencyCheck( spConnection con, _timeb *tb ) {
	_timeb tblocal;
	int ticket;

	if ( tb == NULL ) {
		apb->apb_ftime_s( &tblocal );
		tb = &tblocal;
	}

	ticket = con->latencyInd;
	if ( ticket < 0 ) 
		ticket = CON_MAX_LATENCY_CHECKS + ticket;
	
	if ( con->latencyCheck[ticket] < 0 ) // no more tickets!
		return -1;

	con->latencyInd++;
	if ( con->latencyInd >= CON_MAX_LATENCY_CHECKS )
		con->latencyInd = 0;
	
	con->latencyCheck[ticket] = -(int)((tb->time-con->latencyBase.time)*1000 + tb->millitm-con->latencyBase.millitm);

	return ticket;
}

int AgentBase::conFinishLatencyCheck( spConnection con, int ticket, _timeb *tb ) {
	_timeb tblocal;
	int i, count, latency;

	if ( tb == NULL ) {
		apb->apb_ftime_s( &tblocal );
		tb = &tblocal;
	}

	if ( ticket < 0 || con->latencyCheck[ticket] >= 0 ) // invalid ticket!
		return 1;

	con->latencyCheck[ticket] += (int)((tb->time-con->latencyBase.time)*1000 + tb->millitm-con->latencyBase.millitm);
	
	count = 0;
	latency = 0;
	for ( i=0; i<CON_MAX_LATENCY_CHECKS; i++ ) {
		if ( con->latencyCheck[i] > 0 ) {
			count++;
			latency += con->latencyCheck[i];
		}
	}
	if ( count > 0 )
		con->statistics.latency = latency/count;

	// Log.log( 0, "AgentBase::conFinishLatencyCheck: latency = %d", con->statistics.latency );

	return 0;
}

int AgentBase::conFDAlive( spConnection con, unsigned int count, unsigned int period, _timeb *sendTime ) {
	_timeb recvTime, curTime;
	unsigned int D, lastD;
	int avgD;
	unsigned int dT;

	// clear timeout
	if ( con->observer->timeout != nilUUID ) {
		this->removeTimeout( &con->observer->timeout );
		con->observer->timeout = nilUUID;
	}

	recvTime = con->recvTime;

	D = (int)(1000*(recvTime.time - sendTime->time) + (recvTime.millitm - sendTime->millitm));

	con->observer->D.push_back( D );
	if ( con->observer->D.size() > FD_MESSAGE_SAMPLES ) {
		lastD = con->observer->D.front();
		con->observer->D.pop_front();
	} else {
		lastD = 0;
	}
	con->observer->sumD += D - lastD;

	con->observer->r--;
	//Log.log( 0, "AgentBase::conFDAlive: DEBUG observer->r %d", con->observer->r );
	if ( con->observer->r <= 0 ) {
		this->conFDEvaluateQOS( con );
	}

	if ( count > con->observer->l ) {
		con->observer->d += count - con->observer->l - 1;

		con->observer->l = count;

		apb->apb_ftime_s( &curTime );

		// calculate time until expected arrival of the next message
		avgD = con->observer->sumD / (int)con->observer->D.size();
		dT = (unsigned int)(avgD + period + 1000*(sendTime->time - curTime.time) + (sendTime->millitm - curTime.millitm));
		
		// add in the leeway
		unsigned int alpha;
		alpha = con->observer->TDu - period;
		dT += alpha;

		if ( dT < 0 ) { // suspect!
			if ( con->observer->status != CS_SUSPECT ) {
				Log.log( 0, "AgentBase::conFDAlive: connection suspect (%s)", Log.formatUUID( 0, &con->uuid ) );
				con->observer->status = CS_SUSPECT;
				this->connectionStatusChanged( con, con->observer->status );
			}
		} else { // ok!
			if ( con->observer->status != CS_TRUSTED ) {
				Log.log( 0, "AgentBase::conFDAlive: connection trusted (%s)", Log.formatUUID( 0, &con->uuid ) );
				con->observer->status = CS_TRUSTED;
				this->connectionStatusChanged( con, con->observer->status );
			}
			con->observer->timeout = this->addTimeout( dT, AgentBase_CBR_cbFDObserver, &con->index, sizeof(UUID), false );
		}
	}


	return 0;
}

int AgentBase::conFDQOS( spConnection con, unsigned int newPeriod ) {

	if ( con->subject->nextPeriod != newPeriod ) {
		//Log.log( 0, "AgentBase::conFDQOS: setting period to %d (%s)", newPeriod, Log.formatUUID( 0, &con->uuid ) );
		
		con->subject->nextPeriod = newPeriod;
	}

	return 0;
}

int AgentBase::conFDEvaluateQOS( spConnection con ) {
	unsigned int newPeriod;

	if ( con->observer->l == 0 || con->observer->D.size() < FD_MESSAGE_SAMPLES ) // not ready
		return 1;

	// reset counter
	con->observer->r = FD_EVALUATE_QOS; 

	// calculate new period
	std::list<int>::iterator iD;
	float pL, ED, VD;
	float gamma;
	unsigned int nmax;
	unsigned int nhigh, nlow, ncur, nlast;
	float fn, sq;
	int i, imax;

	// base parameters
	pL = con->observer->d/(float)con->observer->l;
	
	ED = con->observer->sumD/(float)con->observer->D.size();
	VD = 0;
	for ( iD = con->observer->D.begin(); iD != con->observer->D.end(); iD++ ) {
		VD += (*iD - ED)*(*iD - ED);
	}
	VD /= con->observer->D.size();

	// Step 1
	gamma = (1 - pL)*con->observer->TDu*con->observer->TDu/(VD + con->observer->TDu*con->observer->TDu);
	nmax = (unsigned int)min( gamma*con->observer->TMU, con->observer->TDu);
	if ( nmax == 0 ) {
		return 1; // unable to achieve QOS
	}

	// Step 2, binary search
	nhigh = nmax;
	nlow = 1;
	ncur = nmax;
	nlast = nmax;
	while ( 1 ) {
		imax = (int)(con->observer->TDu/ncur) - 1;
		fn = (float)ncur;
		for ( i=1; i<=imax; i++ ) {
			sq = (float)(con->observer->TDu - i*ncur);
			sq *= sq;
			fn *= (VD + sq)/(VD + pL*sq);
		}

		if ( fn < con->observer->TMRL ) { // too high
			nhigh = ncur;
			ncur = (ncur + nlow)/2;
		} else { // low enough, so try higher
			nlow = ncur;
			ncur = (ncur + nhigh)/2;
		}
		
		if ( ncur == nlast )
			break;
		nlast = ncur;
	}
	// use nlow since that is our best successful try
	newPeriod = nlow;

//	Log.log( 0, "AgentBase::conFDEvaluateQOS: sending MSG_FD_QOS, period %d (%s)", newPeriod, Log.formatUUID( 0, &con->uuid ) );

	// send message
	this->sendMessage( con, MSG_FD_QOS, (char *)&newPeriod, sizeof(unsigned int) );

	return 0;
}

int AgentBase::conFDSuspect( spConnection con ) {

/*	if ( con == this->hostCon ) {
		// host is suspect, stop
		STATE(AgentBase)->stopFlag = true;
	}
*/
	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks


ULONGLONG ftSubtractTimes(const FILETIME& ftA, const FILETIME& ftB) {
	LARGE_INTEGER a, b;
	a.LowPart = ftA.dwLowDateTime;
	a.HighPart = ftA.dwHighDateTime;
	
	b.LowPart = ftB.dwLowDateTime;
	b.HighPart = ftB.dwHighDateTime;
	
	return a.QuadPart - b.QuadPart;
}

bool AgentBase::cbUpdateCpuUsage( void *NA ) {
	
	_timeb endTime;
	FILETIME ftThreadCreation, ftThreadExit, ftThreadKernelEnd, ftThreadUserEnd;

	if ( this->cpuUsageLastUpdate.time == 0 ) { // first time
		apb->apb_ftime_s( &this->cpuUsageLastUpdate );
	
		if ( !apb->apbGetThreadTimes( apb->apbGetCurrentThread(), &ftThreadCreation, &ftThreadExit, &ftThreadKernelEnd, &ftThreadUserEnd ) ) {
			Log.log( 0, "AgentBase::cbUpdateCpuUsage: error getting thread times" );
			ZeroMemory( &this->cpuUsageLastUpdate, sizeof(_timeb) );
			return 1;
		}

		this->cpuUsageKernalStart = ftThreadKernelEnd;
		this->cpuUsageUserStart = ftThreadUserEnd;
	
		return 1;
	}

	apb->apb_ftime_s( &endTime );
	if ( !apb->apbGetThreadTimes( apb->apbGetCurrentThread(), &ftThreadCreation, &ftThreadExit, &ftThreadKernelEnd, &ftThreadUserEnd ) ) {
		Log.log( 0, "AgentBase::cbUpdateCpuUsage: error getting thread times" );
		ZeroMemory( &this->cpuUsageLastUpdate, sizeof(_timeb) );
		return 1;
	}

	ULONGLONG difKernel = ftSubtractTimes( ftThreadKernelEnd, this->cpuUsageKernalStart );
	ULONGLONG difUser = ftSubtractTimes( ftThreadUserEnd, this->cpuUsageUserStart );

	ULONGLONG difTotal = difKernel + difUser;

	unsigned int usedMillis = (unsigned int)(difTotal*1E-4); // convert to milliseconds
	unsigned int elapsedMillis = (unsigned int)((endTime.time - this->cpuUsageLastUpdate.time)*1000 + (endTime.millitm - this->cpuUsageLastUpdate.millitm));

	this->cpuUsageTotalUsed += usedMillis;
	this->cpuUsageTotalElapsed += elapsedMillis;

	Log.log( 0, "AgentBase::cbUpdateCpuUsage: used %d, elapsed %d, %%%f", usedMillis, elapsedMillis, usedMillis/(float)elapsedMillis );

	this->cpuUsageLastUpdate = endTime;
	this->cpuUsageKernalStart = ftThreadKernelEnd;
	this->cpuUsageUserStart = ftThreadUserEnd;

	if ( !this->isHost ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packUInt32( usedMillis );
		this->sendMessage( this->hostCon, MSG_AGENT_PROCESS_COST, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 1;
}

bool AgentBase::cbUpdateConnectionReliability( void *NA ) {
	char check;

	mapConnection::iterator iter = this->connection.begin();
	while ( iter != this->connection.end() ) {
		check = !(iter->second->state == CON_STATE_CONNECTED || iter->second->state == CON_STATE_LISTENING);
		if ( check != iter->second->reliabilityCheck[iter->second->reliabilityInd] ) {
			iter->second->reliabilityCheck[iter->second->reliabilityInd] = check;
			if ( check ) iter->second->statistics.reliability--;
			else         iter->second->statistics.reliability++;
		}
		iter->second->reliabilityInd++;
		if ( iter->second->reliabilityInd >= CON_MAX_RELIABILITY_CHECKS )
			iter->second->reliabilityInd = 0;
		iter++;
	}
	
	return 1;
}

bool AgentBase::cbDoRetry( void *vpindex ) {
	mapConnection::iterator iC = this->connection.find( *(UUID *)vpindex );
	if ( iC == this->connection.end() ) {
		return 0; // not found	
	}
	spConnection con = iC->second;

	if ( this->conConnect( con ) ) {
		this->closeConnection( con );
		return 0;
	}

	if ( this->conGroupAdd( con, CON_GROUP_EXCEPT ) ) {
		this->closeConnection( con );
		return 0;
	}
	if ( this->conGroupAdd( con, CON_GROUP_WRITE ) ) {
		this->closeConnection( con );
		return 0;
	}

	return 0;
}

bool AgentBase::cbConversationTimeout( void *vpthread ) {
	spConversation conv = this->conversations[ *(UUID *)vpthread ];
	this->conversationResponse( &conv->thread, NULL, NULL, 0 );

	return 0;
}

bool AgentBase::cbConversationErase( void *vpthread ) {
	spConversation conv = this->conversations[ *(UUID *)vpthread ];

	this->conversations.erase( conv->thread );
	free( conv );

	return 0;
}

bool AgentBase::cbDelayedMessage( void *vpDelayedMessage ) {
	DelayedMessage *dm = (DelayedMessage *)vpDelayedMessage;
	mapConnection::iterator iC;
	spConnection con = NULL;

	switch ( dm->type ) {
	case DMT_NORMAL:
		if ( dm->conId == nilUUID ) { // special, means it was the host connection
			con = this->hostCon;
		} else {
			iC = this->connection.find( dm->conId );
			if ( iC != this->connection.end() ) { // connection is still good
				con = iC->second;
			}
		}
		if ( con ) {
			this->sendMessage( con, dm->message, (char *)getDynamicBuffer(dm->bufRef), dm->len, 
				( dm->forwardAddress == nilUUID ? NULL : &dm->forwardAddress ),
				( dm->returnAddress == nilUUID ? NULL : &dm->returnAddress ) );
		} else {
			Log.log( 0, "AgentBase::cbDelayedMessage: DMT_NORMAL could not find valid connection" );
		}
		break;
	case DMT_GROUP:
		this->sendMessage( &dm->conId, dm->message, (char *)getDynamicBuffer(dm->bufRef), dm->len );
		break;
	case DMT_EX:
		if ( dm->conId == nilUUID ) { // special, means it was the host connection
			con = this->hostCon;
		} else {
			iC = this->connection.find( dm->conId );
			if ( iC != this->connection.end() ) { // connection is still good
				con = iC->second;
			}
		}
		if ( con ) {
			this->sendMessageEx( con, dm->message, dm->msg_size, dm->msg_first, dm->msg_last, (char *)getDynamicBuffer(dm->bufRef), dm->len,
				( dm->forwardAddress == nilUUID ? NULL : &dm->forwardAddress ),
				( dm->returnAddress == nilUUID ? NULL : &dm->returnAddress ) );
		} else {
			Log.log( 0, "AgentBase::cbDelayedMessage: DMT_EX could not find valid connection" );
		}
		break;
	case DMT_EX_GROUP:
		this->sendMessageEx( &dm->conId, dm->message, dm->msg_size, dm->msg_first, dm->msg_last, (char *)getDynamicBuffer(dm->bufRef), dm->len );
		break;
	default:
		break; // how did this happen?
	};

	// free message data
	if ( dm->len ) {
		freeDynamicBuffer( dm->bufRef );
		this->delayedMessageBufRefs.erase( dm->bufRef );
	}

	return 0;
}

bool AgentBase::cbFDSubject( void *vpindex ) {
	mapConnection::iterator iC;
	spConnection con;
	_timeb tb;

	iC = this->connection.find( *(UUID *)vpindex );
	if ( iC == this->connection.end() ) {
		Log.log( 0, "AgentBase::cbFDSubject: connection not found (%s)", Log.formatUUID( 0, (UUID *)vpindex ) );
		return 0;
	}
	con = iC->second;

	con->subject->count++;

	apb->apb_ftime_s( &tb );

	this->ds.reset();
	this->ds.packUInt32( con->subject->count );
	this->ds.packUInt32( con->subject->nextPeriod );
	this->ds.packData( &tb, sizeof(_timeb) );
	this->sendMessage( con, MSG_FD_ALIVE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	
	//if ( this->isHost ) 
	//	Log.log( LOG_LEVEL_ALL, "AgentBase::cbFDSubject: MSG_FD_ALIVE %s", Log.formatUUID(LOG_LEVEL_ALL,&con->uuid) );

	if ( con->subject->period == con->subject->nextPeriod ) { // same period
		return 1; // repeat
	} else {
		con->subject->period = con->subject->nextPeriod;
		con->subject->timeout = this->addTimeout( con->subject->period, AgentBase_CBR_cbFDSubject, &con->index, sizeof(UUID), false );
		return 0;	
	}
}


bool AgentBase::cbFDObserver( void *vpindex ) {
	mapConnection::iterator iC;
	spConnection con;

	iC = this->connection.find( *(UUID *)vpindex );
	if ( iC == this->connection.end() ) {
		Log.log( 0, "AgentBase::cbFDObserver: connection not found (%s)", Log.formatUUID( 0, (UUID *)vpindex ) );
		return 0;
	}
	con = iC->second;

	Log.log( 0, "AgentBase::cbFDObserver: connection suspect (%s)", Log.formatUUID( 0, &con->uuid ) );
	con->observer->status = CS_SUSPECT;
	con->observer->timeout = nilUUID;

	this->connectionStatusChanged( con, CS_SUSPECT );

	return 0;
}

bool AgentBase::cbWatchHostConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	
	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { // connected to a host, introduce ourselves
			
			// introduce ourselves to the host
			this->ds.reset();
			this->ds.packUUID( &STATE(AgentBase)->uuid );
			this->sendMessage( con, MSG_AGENT_REGISTER, this->ds.stream(), this->ds.length() ); // introduce ourselves
			this->ds.unlock();
			
			// start failure detection
			this->initializeConnectionFailureDetection( con );

			this->backup(); // first backup
		} else if ( con->state == CON_STATE_DISCONNECTED ) {
			Log.log( 0, "AgentBase::cbWatchHostConnection: connection to host closed, shutting down" );
			this->hostCon = NULL;
			STATE(AgentBase)->stopFlag = true;
		}
	}

	return 0;
}


bool AgentBase::cbAtomicMessageTimeout( void *vpid ) {
	UUID id = *(UUID *)vpid;
	
	Log.log( 0, "AgentBase::cbAtomicMessageTimeout: atomic message expired! %s", Log.formatUUID(0,&id) );

	this->_atomicMessageDecide( &id, this->getUUID(), -1, -1, 0 ); // decide abort

	return 0;
}

bool AgentBase::cbLocalMessage( void *NA ) {
	std::list<LocalMessage>::iterator iM;
	std::list<LocalMessage> queue = this->localMessageQueue;

	this->localMessageQueue.clear();
	STATE(AgentBase)->localMessageTimeout = nilUUID;

	for ( iM = queue.begin(); iM != queue.end(); iM++ ) {
		// deliver message
		if ( iM->len ) 
			this->conProcessMessage( NULL, iM->message, (char *)this->getDynamicBuffer( iM->dataRef ), iM->len );
		else	
			this->conProcessMessage( NULL, iM->message, NULL, 0 );
		
		// clean up
		if ( iM->len )
			this->freeDynamicBuffer( iM->dataRef );
	}

	return 0;
}

bool AgentBase::cbNotificationFilterExpiry( void *vpkey ) {
	this->notificationFilter.erase( *(UUID*)vpkey );

	return 0;
}

//-----------------------------------------------------------------------------
// State functions

int AgentBase::freeze( UUID *ticket ) {
	DataStream lds, sds;
	_timeb tb;

	Log.log( 0, "AgentBase::freeze: starting freeze! %s", Log.formatUUID(0,ticket) );

	// get current time and start package
	apb->apb_ftime_s( &tb );

	lds.packUUID( ticket );
	lds.packData( &tb, sizeof(_timeb) );

	// write state
	this->writeState( &sds );
	
	Log.log( 0, "AgentBase::freeze: got state, %d bytes", sds.length() );

	lds.packUInt32( sds.length() );
	lds.packData( sds.stream(), sds.length() );

	// send state to host
	this->sendMessage( this->hostCon, MSG_AGENT_STATE, lds.stream(), lds.length() );
	lds.unlock();

	// prepare to quit
	this->frozen = true;
	STATE(AgentBase)->stopFlag = true;

	return 0;
}

int AgentBase::thaw( DataStream *ds, bool resumeReady ) {
	DataStream sds;
	unsigned int stateSize;

	ds->unpackUUID( &this->thawTicket );

	// read state
	stateSize = ds->unpackUInt32();
	sds.setData( (char *)ds->unpackData( stateSize ), stateSize );
	sds.rewind();
	this->readState( &sds );
	sds.unlock();

	Log.log( 0, "AgentBase::thaw: state read stateSize %d", stateSize );

	if ( resumeReady ) {
		Log.log( 0, "AgentBase::thaw: ready to resume" );

		this->sendMessage( this->hostCon, MSG_AGENT_RESUME_READY, (char *)&this->thawTicket, sizeof(UUID) );
	}

	return 0;
}

int AgentBase::resume( DataStream *ds ) {
	unsigned char msg;
	unsigned int msgLen;
	char *msgData;
	int msgCount = 0;

	cbUpdateCpuUsage( NULL ); // run once to get started

	// parse message queue
	while ( ds->unpackBool() ) {
		msgCount++;

		msg = ds->unpackUChar();
		msgLen = ds->unpackUInt32();
		if ( msgLen )
			msgData = (char *)ds->unpackData( msgLen );

		this->conProcessMessage( this->hostCon, msg, msgData, msgLen ); // we're a normal agent so it's always the hostCon
	}

	Log.log( 0, "AgentBase::resume: resuming, msg queue %d", msgCount );

	return 0;
}

int	AgentBase::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentBase);

	// pack dynamic state buffers
	mapStateBuf::iterator iB;
	for ( iB = this->stateBuffers.begin(); iB != this->stateBuffers.end(); iB++ ) {
		ds->packBool( 1 );

		ds->packUUID( (UUID *)&iB->first );
		ds->packUInt32( iB->second.size );
		ds->packData( iB->second.buf, iB->second.size );
	}
	ds->packBool( 0 ); // done

	// pack timeouts
	__int64 dt;
	_timeb curTime;
	apb->apb_ftime_s( &curTime );
	spTimeoutEvent to = this->firstTimeout;
	while ( to != NULL ) {
		if ( to->stateSafe ) {
			ds->packBool( 1 );
			
			ds->packUUID( &to->id );
			ds->packUInt32( to->period );

			// calculate expiry time
			dt = (__int64)( (to->timeout.time - curTime.time)*1000 + (to->timeout.millitm - curTime.millitm) );
			ds->packInt64( dt );

			// pack callback
			ds->packInt32( to->cbRef );

			// NOTE: to->next is handled on the READ side

			// pack data
			ds->packInt32( to->dataLen );
			if( to->dataLen )
				ds->packData( to->data, to->dataLen );

		}

		to = to->next;
	}
	ds->packBool( 0 ); // done

	// pack local messages
	_WRITE_STATE_LIST( LocalMessage, &this->localMessageQueue );

	// pack atomic messages
	mapAtomicMessage::iterator iM;
	std::map<int,std::list<AtomicMessage_MSG>>::iterator iMM;
	for ( iM = this->atomicMsgs.begin(); iM != this->atomicMsgs.end(); iM++ ) {
		ds->packBool( 1 ); 

		ds->packUUID( &iM->second.id );
		ds->packBool( iM->second.placeholder );
		ds->packBool( iM->second.orderedMsg );
		ds->packUUID( &iM->second.queue );
//		ds->packBool( iM->second.targetsSuspect );
		
		_WRITE_STATE_LIST( UUID, &iM->second.targets );
		ds->packInt32( iM->second.f );
		ds->packUChar( iM->second.msg );
		ds->packUInt32( iM->second.len );
		ds->packUUID( &iM->second.dataRef );

		ds->packInt32( iM->second.cbRef );
		ds->packInt32( iM->second.retries );

/*		ds->packUChar( iM->second.msgAbort );
		ds->packUInt32( iM->second.lenAbort );
		ds->packUUID( &iM->second.dataAbortRef );
*/		
		ds->packUUID( &iM->second.timeout );

		ds->packChar( iM->second.phase );
		ds->packChar( iM->second.decided );
		ds->packChar( iM->second.delivered );
		ds->packChar( iM->second.estCommit );
		ds->packInt32( iM->second.order );
		ds->packInt32( iM->second.round );
		ds->packInt32( iM->second.timestamp );
		
		ds->packUUID( &iM->second.coord );
		_WRITE_STATE_LIST( UUID, &iM->second.suspects );
		_WRITE_STATE_LIST( UUID, &iM->second.permanentSuspects );
		_WRITE_STATE_LIST( UUID, &iM->second.decisions );
		for ( iMM = iM->second.msgs.begin(); iMM != iM->second.msgs.end(); iMM++ ) {
			ds->packBool( 1 );
			ds->packInt32( iMM->first );
			_WRITE_STATE_LIST( AtomicMessage_MSG, &iMM->second );
		}
		ds->packBool( 0 ); // done
		_WRITE_STATE_MAP( int, AtomicMessage_MSG, &iM->second.proposals );
		for ( iMM = iM->second.acks.begin(); iMM != iM->second.acks.end(); iMM++ ) {
			ds->packBool( 1 );
			ds->packInt32( iMM->first );
			_WRITE_STATE_LIST( AtomicMessage_MSG, &iMM->second );
		}
		ds->packBool( 0 ); // done
		
	}
	ds->packBool( 0 ); // done

	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->atomicMessageHighestOrder );

	// pack atomic message ordered queue
	std::map<UUID, std::list<AtomicMessage>, UUIDless>::iterator iQ;
	std::list<AtomicMessage>::iterator iMq;
	for ( iQ = this->atomicMsgsOrderedQueue.begin(); iQ != this->atomicMsgsOrderedQueue.end(); iQ++ ) {
		ds->packBool( 1 ); 
		ds->packUUID( (UUID *)&iQ->first );

		for ( iMq = iQ->second.begin(); iMq != iQ->second.end(); iMq++ ) {
			ds->packBool( 1 );
			ds->packUUID( &iMq->id );
			ds->packBool( iMq->orderedMsg );
			ds->packUUID( &iM->second.queue );
//			ds->packBool( iM->second.targetsSuspect );
			
			_WRITE_STATE_LIST( UUID, &iMq->targets );
			ds->packUChar( iMq->msg );
			ds->packUInt32( iMq->len );
			ds->packUUID( &iMq->dataRef );

			ds->packInt32( iMq->cbRef );
		}
		ds->packBool( 0 ); // done
		
	}
	ds->packBool( 0 ); // done

	// pack conversations
	mapConversation::iterator iConv;
	for ( iConv = this->conversations.begin(); iConv != this->conversations.end(); iConv++ ) {
		ds->packBool( 1 ); 

		ds->packUUID( &iConv->second->thread );
		ds->packUUID( &iConv->second->timeout );
		ds->packInt32( iConv->second->line );
		ds->packInt32( iConv->second->cbRef );
		ds->packInt32( iConv->second->dataLen );
		if ( iConv->second->dataLen ) {
			ds->packData( iConv->second->data, iConv->second->dataLen );
		}
		// skip con
		ds->packInt32( iConv->second->responseLen );
		if ( iConv->second->responseLen ) {
			ds->packData( iConv->second->response, iConv->second->responseLen );
		}
		ds->packBool( iConv->second->expired );
	}
	ds->packBool( 0 ); // done

	// pack delayed message buf refs
	_WRITE_STATE_MAP_LESS( UUID, char, UUIDless, &this->delayedMessageBufRefs );

	// pack duplicate notification filter
	_WRITE_STATE_MAP_LESS( UUID, char, UUIDless, &this->notificationFilter );

	return 0;
}

int	AgentBase::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentBase);

	// unpack dynamic state buffers
	UUID bufRef;
	StateBuf buf;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &bufRef );
		buf.size = ds->unpackUInt32();

		buf.buf = malloc( buf.size );
		if ( !buf.buf ) {
			Log.log( 0, "AgentBase::readState: malloc failed" );
			return 1;
		}

		memcpy_s( buf.buf, buf.size, ds->unpackData( buf.size ), buf.size );

		this->stateBuffers[bufRef] = buf;
	}

	// unpack timeouts
	spTimeoutEvent iter;
	spTimeoutEvent prev;
	__int64 secs, msecs;
	spTimeoutEvent timeoutEvent;
	_timeb curTime;
	apb->apb_ftime_s( &curTime );
	__int64 dt;
	while ( ds->unpackBool() ) {
		timeoutEvent = (spTimeoutEvent)malloc(sizeof(sTimeoutEvent));
		if ( !timeoutEvent ) {
			Log.log( 0, "AgentBase::readState: malloc failed" );
			return 1;
		}

		// unpack id
		ds->unpackUUID( &timeoutEvent->id );

		timeoutEvent->period = ds->unpackUInt32();

		// set time
		timeoutEvent->timeout = curTime;
		dt = ds->unpackInt64();
		secs = dt / 1000;
		msecs = dt % 1000;	
		if ( dt >= 0 ) {
			timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
			timeoutEvent->timeout.millitm = (unsigned short)(timeoutEvent->timeout.millitm + msecs) % 1000;
		} else {
			if ( timeoutEvent->timeout.millitm + msecs < 0 ) {
				timeoutEvent->timeout.time -= 1;
				timeoutEvent->timeout.millitm += 1000;
			}
			timeoutEvent->timeout.time += secs;
			timeoutEvent->timeout.millitm += (short)msecs;
		}

		// unpack callback
		timeoutEvent->cbRef = ds->unpackInt32();

		// unpack data
		timeoutEvent->dataLen = ds->unpackInt32();
		if ( timeoutEvent->dataLen ) {
			timeoutEvent->data = malloc(timeoutEvent->dataLen);
			if ( !timeoutEvent->data ) {
				Log.log( 0, "AgentBase::readState: malloc data failed!" );
				free( timeoutEvent );
				return 1;
			}
			memcpy_s( timeoutEvent->data, timeoutEvent->dataLen, ds->unpackData(timeoutEvent->dataLen), timeoutEvent->dataLen );
		} else {
			timeoutEvent->data = NULL;
		}

		timeoutEvent->stateSafe = true; // wouldn't be in the state if it wasn't state safe

		// insert, always process earlier timeouts first (i.e. insert after timeouts with same time)
		iter = this->firstTimeout;
		prev = NULL;
		while ( iter != NULL ) {
			if ( iter->timeout.time > timeoutEvent->timeout.time )
				break;
			if ( iter->timeout.time == timeoutEvent->timeout.time 
			  && iter->timeout.millitm > timeoutEvent->timeout.millitm )
				break;
			prev = iter;
			iter = iter->next;
		}
		if ( prev ) prev->next = timeoutEvent;
		else this->firstTimeout = timeoutEvent;
		timeoutEvent->next = iter;
	}

	// unpack local messages
	_READ_STATE_LIST( LocalMessage, &this->localMessageQueue );

	// unpack atomic messages
	AtomicMessage *am;
	while ( ds->unpackBool() ) {
		UUID id;
		int r;
		ds->unpackUUID( &id );

		am = &this->atomicMsgs[id];

		am->id = id;
		am->placeholder = ds->unpackBool();
		am->orderedMsg = ds->unpackBool();
		ds->unpackUUID( &am->queue );
//		am->targetsSuspect = ds->unpackBool();

		_READ_STATE_LIST( UUID, &am->targets );
		am->f = ds->unpackInt32();
		am->msg = ds->unpackUChar();
		am->len = ds->unpackUInt32();
		ds->unpackUUID( &am->dataRef );

		am->cbRef = ds->unpackInt32();
		am->retries = ds->unpackInt32();
		
/*		am->msgAbort = ds->unpackUChar();
		am->lenAbort = ds->unpackUInt32();
		ds->unpackUUID( &am->dataAbortRef );
*/
		ds->unpackUUID( &am->timeout );

		am->phase = ds->unpackChar();
		am->decided = ds->unpackChar();
		am->delivered = ds->unpackChar();
		am->estCommit = ds->unpackChar();
		am->order = ds->unpackInt32();
		am->round = ds->unpackInt32();
		am->timestamp = ds->unpackInt32();

		ds->unpackUUID( &am->coord );
		_READ_STATE_LIST( UUID, &am->suspects );
		_READ_STATE_LIST( UUID, &am->permanentSuspects );
		_READ_STATE_LIST( UUID, &am->decisions );
		while ( ds->unpackBool() ) {
			r = ds->unpackInt32();
			_READ_STATE_LIST( AtomicMessage_MSG, &am->msgs[r] );
		}
		_READ_STATE_MAP( int, AtomicMessage_MSG, &am->proposals );
		while ( ds->unpackBool() ) {
			r = ds->unpackInt32();
			_READ_STATE_LIST( AtomicMessage_MSG, &am->acks[r] );
		}
	}

	_READ_STATE_MAP( UUID, int, &this->atomicMessageHighestOrder );

	// unpack atomic message ordered queue
	AtomicMessage amq;
	UUID queueId;
	while ( ds->unpackBool() ) {

		ds->unpackUUID( &queueId );

		while ( ds->unpackBool() ) {
			ds->unpackUUID( &amq.id );
			amq.orderedMsg = ds->unpackBool();
			ds->unpackUUID( &amq.queue );
//			amq.targetsSuspect = ds->unpackBool();

			amq.targets.clear();
			_READ_STATE_LIST( UUID, &amq.targets );
			amq.msg = ds->unpackUChar();
			amq.len = ds->unpackUInt32();
			ds->unpackUUID( &amq.dataRef );

			amq.cbRef = ds->unpackInt32();
			
			this->atomicMsgsOrderedQueue[queueId].push_back( amq );
		}
	}

	// unpack conversations
	spConversation conv;
	while ( ds->unpackBool() ) {
		conv = (spConversation)malloc(sizeof(sConversation));
		if ( !conv ) {
			Log.log( 0, "AgentBase::readState: malloc failed" );
			return 1;
		}
		
		ds->unpackUUID( &conv->thread );
		ds->unpackUUID( &conv->timeout );
		conv->line = ds->unpackInt32();
		conv->cbRef = ds->unpackInt32();
		conv->dataLen = ds->unpackInt32();
		if ( conv->dataLen ) {
			conv->data = malloc(conv->dataLen);
			if ( !conv->data ) {
				Log.log( 0, "AgentBase::readState: malloc data failed!" );
				free( conv );
				return 1;
			}
			memcpy_s( conv->data, conv->dataLen, ds->unpackData( conv->dataLen ), conv->dataLen );
		} else {
			conv->data = NULL;
		}
		conv->con = NULL;
		conv->responseLen = ds->unpackInt32();
		if ( conv->responseLen ) {
			conv->response = (char *)malloc(conv->responseLen);
			if ( !conv->response ) {
				Log.log( 0, "AgentBase::readState: malloc data failed!" );
				free( conv->data );
				free( conv );
				return 1;
			}
			memcpy_s( conv->response, conv->responseLen, ds->unpackData( conv->responseLen ), conv->responseLen );
		} else {
			conv->response = NULL;
		}
		conv->expired = ds->unpackBool();

		// insert
		this->conversations[conv->thread] = conv;
	}

	// unpack delayed message buf refs
	_READ_STATE_MAP( UUID, char, &this->delayedMessageBufRefs );

	// unpack duplicate notification filter
	_READ_STATE_MAP( UUID, char, &this->notificationFilter );

	return 0;
}

int AgentBase::backup() {
	UUID ticket;
	DataStream lds, sds;
	_timeb tb;

	if ( this->recoveryInProgress )
		return 0; // no need to backup now

	apb->apbUuidCreate( &ticket );

	Log.log( 0, "AgentBase::backup: starting backup %s", Log.formatUUID(0,&ticket) );

	// get current time and start package
	apb->apb_ftime_s( &tb );

	lds.packUUID( &ticket );
	lds.packData( &tb, sizeof(_timeb) );

	// write state
	this->writeBackup( &sds );
	
	Log.log( 0, "AgentBase::backup: got backup, %d bytes", sds.length() );

	lds.packUInt32( sds.length() );
	lds.packData( sds.stream(), sds.length() );

	// send backup to host
	this->sendMessage( this->hostCon, MSG_AGENT_BACKUP, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentBase::recover( DataStream *ds ) {
	DataStream sds;
	unsigned int backupSize;
	int result;

	this->recoveryInProgress = true; // starting

	ds->unpackUUID( &this->recoveryTicket );

	// read backup
	backupSize = ds->unpackUInt32();
	sds.setData( (char *)ds->unpackData( backupSize ), backupSize );
	sds.rewind();
	result = this->readBackup( &sds );
	sds.unlock();

	Log.log( 0, "AgentBase::recover: backup read backupSize %d", backupSize );

	if ( result ) { // failed
		this->recoveryFailed();
	} else {
		// check if we're ready to finish
		this->recoveryCheck( &nilUUID );
	}

	return 0;
}

int AgentBase::recoveryFinish() {

	Log.log( 0, "AgentBase::recoveryFinish: recovery success" );

	this->calcLifeExpectancy();

	if ( STATE(AgentBase)->started ) { // redo relevant start items
		UUID id = this->addTimeout( CON_RELIABILITY_INTERVAL, AgentBase_CBR_cbUpdateConnectionReliability );
		if ( id == nilUUID ) {
			Log.log( 0, "AgentBase::recoveryFinish: addTimeout failed" );
			return 1;
		}

		id = this->addTimeout( CPU_USAGE_INTERVAL, AgentBase_CBR_cbUpdateCpuUsage );
		if ( id == nilUUID ) {
			Log.log( 0, "AgentBase::recoveryFinish: addTimeout failed" );
			return 1;
		}
		cbUpdateCpuUsage( NULL ); // run once to get started
	}

	this->recoveryInProgress = false; // done

	return 0;
}

int AgentBase::recoveryFailed() {
	DataStream lds;

	// tell the host we couldn't recover
	lds.reset();
	lds.packUUID( &this->recoveryTicket );
	lds.packInt32( 1 ); // failed
	this->sendMessage( this->hostCon, MSG_AGENT_RECOVERED, lds.stream(), lds.length() );
	lds.unlock();

	Log.log( 0, "AgentBase::recoveryFailed: recovery failed" );

	this->recoveryInProgress = false; // failed

	return 0;
}

int	AgentBase::recoveryCheck( UUID *key ) {
	
	if ( *key != nilUUID ) { // clear lock
		std::list<UUID>::iterator iI;
		for ( iI = this->recoveryLocks.begin(); iI != this->recoveryLocks.end(); iI++ ) {
			if ( *iI == *key ) {
				this->recoveryLocks.erase( iI );
				break;
			}
		}
	}

	
	Log.log( 0, "AgentBase::recoveryCheck: %d locks", (int)this->recoveryLocks.size() );

	if ( this->recoveryLocks.empty() ) {
		DataStream lds;

		// tell the host we've recovered
		lds.reset();
		lds.packUUID( &this->recoveryTicket );
		lds.packInt32( 0 ); // success
		this->sendMessage( this->hostCon, MSG_AGENT_RECOVERED, lds.stream(), lds.length() );
		lds.unlock();
		
		Log.log( 0, "AgentBase::recoveryCheck: recovery ready" );
	}

	return 0;
}

int AgentBase::writeBackup( DataStream *ds ) {
		
	// write relevant state data
	ds->packUUID( &STATE(AgentBase)->parentId );

	ds->packInt32( STATE(AgentBase)->agentType.instance );

	ds->packString( STATE(AgentBase)->missionFile );

	ds->packBool( STATE(AgentBase)->configured );
	ds->packBool( STATE(AgentBase)->started );

	ds->packFloat32( STATE(AgentBase)->stabilityTimeMin );
	ds->packFloat32( STATE(AgentBase)->stabilityTimeMax );

	return 0;
}

int AgentBase::readBackup( DataStream *ds ) {
	
	// read state data
	ds->unpackUUID( &STATE(AgentBase)->parentId );

	STATE(AgentBase)->agentType.instance = ds->unpackInt32();

	strcpy_s( STATE(AgentBase)->missionFile, 256, ds->unpackString() );

	STATE(AgentBase)->configured = ds->unpackBool();
	STATE(AgentBase)->started = ds->unpackBool();

	
	STATE(AgentBase)->stabilityTimeMin = ds->unpackFloat32();
	STATE(AgentBase)->stabilityTimeMax = ds->unpackFloat32();

	return 0;
}


//-----------------------------------------------------------------------------
// Old unused state functions

int AgentBase_writeStub( AgentBase::State * state, DataStream * ds, bool climb ) {
	return 0;
}

int AgentBase_readStub( AgentBase::State * state, DataStream * ds, unsigned int age, bool climb ) {
	//_timeb curTime;
	//unsigned short ageMilli = age%1000; 
	// TODO if we still need this function make sure the time call is recorded/played back
	// apb->apb_ftime_s( &curTime );

	//state->lastStub.time    = curTime.time - age/1000 - (curTime.millitm<ageMilli ? 1:0);
	//state->lastStub.millitm = (curTime.millitm - ageMilli) + (curTime.millitm<ageMilli ? 1000:0);

	return 0;
}

int AgentBase_writeState( AgentBase::State * state, DataStream * ds ) {
	AgentBase_writeStub( state, ds, false );

	return 0;
}

int AgentBase_readState( AgentBase::State * state, DataStream * ds, unsigned int age ) {
	//_timeb curTime;
	//unsigned short ageMilli = age%1000; 
	// TODO if we still need this function make sure the time call is recorded/played back
	// apb->apb_ftime_s( &curTime );

	//AgentBase_readStub( state, ds, false );

	//state->lastState.time    = curTime.time - age/1000 - (curTime.millitm<ageMilli ? 1:0);
	//state->lastState.millitm = (curTime.millitm - ageMilli) + (curTime.millitm<ageMilli ? 1000:0);

	return 0;
}

AgentBase::State * AgentBase_CreateState( UUID *uuid, int size ) {
	AgentBase::State *newState = (AgentBase::State *)malloc( size );
	if ( !newState ) {
		printf( "AgentBase_CreateState: malloc failed, %d", size );
		return NULL;
	}

	memcpy( &newState->uuid, uuid, sizeof(UUID) );
	newState->lastStub.time = 0;   // unset
	newState->lastState.time = 0;  // unset

	return newState;
}

void AgentBase_DeleteState( AgentBase::State *state, AgentBase *owner ) {
	free( state );
}