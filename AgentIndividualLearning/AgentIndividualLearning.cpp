// AgentIndividualLearning.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"

#include "AgentIndividualLearning.h"
#include "AgentIndividualLearningVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"

using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentIndividualLearning

//-----------------------------------------------------------------------------
// Constructor	

AgentIndividualLearning::AgentIndividualLearning( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	// allocate state
	ALLOCATE_STATE( AgentIndividualLearning, AgentBase )

	// Assign the UUID
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentIndividualLearning" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentIndividualLearning_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentIndividualLearning_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	STATE(AgentIndividualLearning)->initialSetup = 3;
	STATE(AgentIndividualLearning)->parametersSet = false;
	STATE(AgentIndividualLearning)->startDelayed = false;

	STATE(AgentIndividualLearning)->updateId = -1;

	// Prepare callbacks
	this->callback[AgentIndividualLearning_CBR_convAction] = NEW_MEMBER_CB(AgentIndividualLearning, convAction);

}// end constructor

//-----------------------------------------------------------------------------
// Destructor

AgentIndividualLearning::~AgentIndividualLearning() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}// end started if

}// end destructor

//-----------------------------------------------------------------------------
// Configure

int AgentIndividualLearning::configure() {
	// Create logger
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
		sprintf_s( logName, "%s\\AgentIndividualLearning %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentIndividualLearning %.2d.%.2d.%.5d.%.2d", AgentIndividualLearning_MAJOR, AgentIndividualLearning_MINOR, AgentIndividualLearning_BUILDNO, AgentIndividualLearning_EXTEND );
	}// end if

	if ( AgentBase::configure() ){ 
		return 1;
	}// end if
	
	return 0;
}// end configure

int AgentIndividualLearning::configureParameters( DataStream *ds ) {
	ds->unpackUUID( &STATE(AgentIndividualLearning)->ownerId );

	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::configureParameters: ownerId %s", Log.formatUUID(LOG_LEVEL_NORMAL, &STATE(AgentIndividualLearning)->ownerId));

	STATE(AgentIndividualLearning)->initialSetup = 0; // need mission region info and map info

	this->backup(); // initialSetup

	return 0;
}// end configureParameters

int	AgentIndividualLearning::finishConfigureParameters() {

	STATE(AgentIndividualLearning)->parametersSet = true;

	if ( STATE(AgentIndividualLearning)->startDelayed ) {
		STATE(AgentIndividualLearning)->startDelayed = false;
		this->start( STATE(AgentBase)->missionFile );
	}// end if

	this->backup(); // initialSetup

	return 0;
}// end finishConfigureParameters

//-----------------------------------------------------------------------------
// Start

int AgentIndividualLearning::start( char *missionFile ) {
	DataStream lds;
	
	// Delay start if parameters not set
	if ( !STATE(AgentIndividualLearning)->parametersSet ) { // delay start
		strcpy_s( STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), missionFile );
		STATE(AgentIndividualLearning)->startDelayed = true; 
		return 0;
	}// end if

	if ( AgentBase::start( missionFile ) ){
		return 1;
	}// end if

	STATE(AgentBase)->started = false;
	
	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars once DDBE_WATCH_TYPE notification is received

	STATE(AgentBase)->started = true;
	return 0;
}// end start


//-----------------------------------------------------------------------------
// Stop

int AgentIndividualLearning::stop() {

	if ( !this->frozen ) {
		// TODO
	}// end if

	return AgentBase::stop();
}// end stop


//-----------------------------------------------------------------------------
// Step

int AgentIndividualLearning::step() {
	return AgentBase::step();
}// end step

int AgentIndividualLearning::sendAction() {
	// Temporary, just continue sending the same action

	// Formulate action
	std::list<ActionPair> actions;
	ActionPair action;

	// Rotate action
	action.action = AvatarBase_Defs::AA_ROTATE;
	action.val = 0.2;

	// Move action
	//action.action = AvatarBase_Defs::AA_MOVE;
	//action.val = 0.1;

	// Insert actions
	actions.push_back(action);

	// Initiate conversation
	STATE(AgentIndividualLearning)->actionConv = this->conversationInitiate(AgentIndividualLearning_CBR_convAction, DDB_REQUEST_TIMEOUT);
	if (STATE(AgentIndividualLearning)->actionConv == nilUUID) {
		return 1;
	}

	STATE(AgentIndividualLearning)->actionsSent = 0;

	// Send message with action
	this->ds.reset();
	this->ds.packUUID(&STATE(AgentBase)->uuid);
	this->ds.packUUID(&STATE(AgentIndividualLearning)->actionConv);
	this->ds.packInt32(action.action);
	this->ds.packFloat32(action.val);
	this->sendMessageEx(this->hostCon, MSGEX(AvatarBase_MSGS, MSG_ACTION_QUEUE), this->ds.stream(), this->ds.length(), &STATE(AgentIndividualLearning)->ownerId);
	this->ds.unlock();
		
	STATE(AgentIndividualLearning)->actionsSent++;
	Log.log(0, "AgentIndividualLearning::sendAction: Action sent.");

}// end sendAction

//-----------------------------------------------------------------------------
// Process message

int AgentIndividualLearning::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	if (!AgentBase::conProcessMessage(con, message, data, len)) // message handled
		return 0;

	switch (message) {
	case AgentIndividualLearning_MSGS::MSG_CONFIGURE:
		lds.setData(data, len);
		this->configureParameters(&lds);
		lds.unlock();
		break;
	case AgentIndividualLearning_MSGS::MSG_SEND_ACTION:
	{
		this->sendAction();
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks

bool AgentIndividualLearning::convAction(void *vpConv) {
	spConversation conv = (spConversation)vpConv;
	UUID thread;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convAction: request timed out");
		return 0; // end conversation
	}

	this->ds.setData(conv->response, conv->responseLen);
	this->ds.unpackUUID(&thread); // thread
	char result = this->ds.unpackChar();
	_timeb tb;
	if (result == AAR_SUCCESS) {
		tb = *(_timeb *)this->ds.unpackData(sizeof(_timeb));
	}
	else {
		this->ds.unpackInt32(); // reason
		tb = *(_timeb *)this->ds.unpackData(sizeof(_timeb));
	}
	this->ds.unlock();

	if (thread != STATE(AgentIndividualLearning)->actionConv)
		return 0; // ignore

	STATE(AgentIndividualLearning)->actionCompleteTime = tb;

	STATE(AgentIndividualLearning)->actionsSent--;


	if (result == AAR_SUCCESS) {
		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convAction: action complete, requesting another action", Log.formatUUID(LOG_LEVEL_VERBOSE, &thread));
		this->sendAction();
	}
}// end convAction

//-----------------------------------------------------------------------------
// State functions

int AgentIndividualLearning::freeze( UUID *ticket ) {
	return AgentBase::freeze( ticket );
}// end freeze

int AgentIndividualLearning::thaw( DataStream *ds, bool resumeReady ) {
	return AgentBase::thaw( ds, resumeReady );
}// end thaw

int	AgentIndividualLearning::writeState( DataStream *ds, bool top ) {
	if ( top ) _WRITE_STATE(AgentIndividualLearning);

	return AgentBase::writeState( ds, false );;
}// end writeState

int	AgentIndividualLearning::readState( DataStream *ds, bool top ) {
	if ( top ) _READ_STATE(AgentIndividualLearning);

	return AgentBase::readState( ds, false );
}// end readState

int AgentIndividualLearning::recoveryFinish() {
	if ( AgentBase::recoveryFinish() ) 
		return 1;

	return 0;
}// end recoveryFinish

int AgentIndividualLearning::writeBackup( DataStream *ds ) {

	// configuration
	ds->packUUID( &STATE(AgentIndividualLearning)->ownerId );

	ds->packBool( STATE(AgentIndividualLearning)->parametersSet );
	ds->packBool( STATE(AgentIndividualLearning)->startDelayed );
	ds->packInt32( STATE(AgentIndividualLearning)->initialSetup );

	return AgentBase::writeBackup( ds );
}// end writeBackup

int AgentIndividualLearning::readBackup( DataStream *ds ) {
	DataStream lds;

	// configuration
	ds->unpackUUID( &STATE(AgentIndividualLearning)->ownerId );

	STATE(AgentIndividualLearning)->parametersSet = ds->unpackBool();
	STATE(AgentIndividualLearning)->startDelayed = ds->unpackBool();
	STATE(AgentIndividualLearning)->initialSetup = ds->unpackInt32();


	if ( STATE(AgentIndividualLearning)->initialSetup == 3 ) {
		return 1; // not enough info to recovery
	} else if ( STATE(AgentIndividualLearning)->initialSetup == 2
		     || STATE(AgentIndividualLearning)->initialSetup == 1 ) {
		
		// we have tasks to take care of before we can resume
		apb->apbUuidCreate( &this->AgentIndividualLearning_recoveryLock );
		this->recoveryLocks.push_back( this->AgentIndividualLearning_recoveryLock );

		STATE(AgentIndividualLearning)->initialSetup = 2; // need mission region info and map info
		
	} else {
		this->finishConfigureParameters();
	}

	return AgentBase::readBackup( ds );
}// end readBackup


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AgentIndividualLearning *agent = (AgentIndividualLearning *)vpAgent;

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
	AgentIndividualLearning *agent = new AgentIndividualLearning( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
}// end Spawn

int Playback( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentIndividualLearning *agent = new AgentIndividualLearning( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
}// end Playback


// CAgentPathPlannerDLL: Override ExitInstance for debugging memory leaks
CAgentIndividualLearningDLL::CAgentIndividualLearningDLL() {}

// The one and only CAgentPathPlannerDLL object
CAgentIndividualLearningDLL theApp;

int CAgentIndividualLearningDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentIndividualLearning ---\n"));
  return CWinApp::ExitInstance();
}