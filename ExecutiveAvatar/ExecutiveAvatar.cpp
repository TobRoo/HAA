// ExecutiveAvatar.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"

#include "ExecutiveAvatar.h"
#include "ExecutiveAvatarVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// ExecutiveAvatar

//-----------------------------------------------------------------------------
// Constructor	
ExecutiveAvatar::ExecutiveAvatar( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "ExecutiveAvatar" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(ExecutiveAvatar_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

}

//-----------------------------------------------------------------------------
// Destructor
ExecutiveAvatar::~ExecutiveAvatar() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int ExecutiveAvatar::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\ExecutiveAvatar %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "ExecutiveAvatar %.2d.%.2d.%.5d.%.2d", ExecutiveAvatar_MAJOR, ExecutiveAvatar_MINOR, ExecutiveAvatar_BUILDNO, ExecutiveAvatar_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int ExecutiveAvatar::start( char *missionFile ) {
	
	if ( AgentBase::start( missionFile ) ) 
		return 1;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int ExecutiveAvatar::stop() {
	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int ExecutiveAvatar::step() {
	Log.log( 0, "ExecutiveAvatar::step: hello there" );
	return AgentBase::step();
}


//-----------------------------------------------------------------------------
// Callbacks


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	ExecutiveAvatar *agent = (ExecutiveAvatar *)vpAgent;

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
	ExecutiveAvatar *agent = new ExecutiveAvatar( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	ExecutiveAvatar *agent = new ExecutiveAvatar( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CExecutiveAvatarDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CExecutiveAvatarDLL, CWinApp)
END_MESSAGE_MAP()

CExecutiveAvatarDLL::CExecutiveAvatarDLL() {}

// The one and only CExecutiveAvatarDLL object
CExecutiveAvatarDLL theApp;

int CExecutiveAvatarDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CExecutiveAvatarDLL ---\n"));
  return CWinApp::ExitInstance();
}