// Autonomic.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>


#include "autonomic.h"
#include "agentHost.h"

#include "time.h"

#include <windows.h>

#include "conio.h"

#define MODE_HOST 0

#define STARTUP_MODE MODE_HOST

#ifdef _DEBUG
#using <System.dll>
#define TRACE

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif 

// Global pointers!
AgentHost *pHOST;

BOOL WINAPI EventHandler(DWORD event)
{
    switch(event)
    {
        case CTRL_C_EVENT:
        break;
        case CTRL_BREAK_EVENT:
        	pHOST->prepareStop();
			break;

        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
			pHOST->prepareStop();
			Sleep(2000); // allow 2 seconds for cleanup
			return TRUE;
    }

    return TRUE;
}

int _tmain(int argc, _TCHAR* argv[]) {

#ifdef _DEBUG
	_crtBreakAlloc = -1; // break on Nth memory allocation
#endif

	#if defined(TRACE)
	System::Diagnostics::Trace::Listeners->Add( gcnew System::Diagnostics::TextWriterTraceListener( System::Console::Out ) );
	System::Diagnostics::Trace::AutoFlush = true;
	#endif

	WSADATA wsaData;
	int iResult;

	if (SetConsoleCtrlHandler( (PHANDLER_ROUTINE)EventHandler,TRUE ) == FALSE ) {
        // failed to install or un-install the handler.
        return 1;
    }

	// Init logging
	Logger Log;
	AgentPlayback *apb;
	char logName[256];
	char timeBuf[64];
	time_t t_t;
	struct tm stm;
	time( &t_t );
	localtime_s( &stm, &t_t );
	strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );

	// create the new log directory
	char configPath[64];
	WCHAR logDirectory[512];
	WCHAR dumpDirectory[512];
	char missionPath[256];
	missionPath[0] = 0;

	GetCurrentDirectory( 512, logDirectory );

	if ( argc == 1 ) {
		wsprintf( logDirectory + wcsnlen(logDirectory,512), _T("\\data\\logs %hs"), timeBuf );
	}

	if ( argc >= 2 ) { // config file specified
		sprintf_s( configPath, sizeof(configPath), "%ws", argv[1] );
		// strip out slashes
		char *c = configPath;
		while ( *c != 0 ) {
			if ( *c == '\\' || *c == '/' ) *c = '_';
			c++;
		}
		wsprintf( logDirectory + wcsnlen(logDirectory,512), _T("\\data\\logs %hs %hs"), timeBuf, configPath );
		
		// copy proper configPath again
		sprintf_s( configPath, sizeof(configPath), "%ws", argv[1] ); 
	}
	
	if ( argc >= 3 ) { // mission file specified
		sprintf_s( missionPath, sizeof(missionPath), "%ws", argv[2] );
	} 

	_tmkdir( logDirectory );

	wsprintf( dumpDirectory, _T("%s\\dump"), logDirectory );	
	_tmkdir( dumpDirectory );

	sprintf_s( logName, "%ws\\log %s.txt", logDirectory, timeBuf );

	apb = new AgentPlayback( PLAYBACKMODE_OFF, NULL );
	
	Log.setAgentPlayback( apb );
	Log.setLogMode( LOG_MODE_COUT );
	Log.setLogMode( LOG_MODE_FILE, logName );
	Log.setLogLevel( LOG_LEVEL_VERBOSE );
	Log.log( 0, "Autonomic %d.%d.%d", AUTONOMIC_MAJOR_VERSION, AUTONOMIC_MINOR_VERSION, AUTONOMIC_SUB_VERSION );

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
		Log.log( 0, "WSAStartup failed: %d", iResult );
		return 1;
	} else {
		Log.log( 3, "WSAStartup succeeded" );
	}

	// temp
	int spin = 1;
	int steps = 0;
	
	if ( STARTUP_MODE == MODE_HOST ) { // host
		Log.log( 3, "Starting host mode..." );
		
		char logDir[512];
		
		sprintf_s( logDir, "%ws", logDirectory );

		pHOST = new AgentHost( "library\\", LOG_LEVEL_ALL, logDir, PLAYBACKMODE_DEFAULT, NULL );

		if ( argc >= 2 ) {
			pHOST->configure( configPath );
		} else {
			pHOST->configure();
		}

		pHOST->start( NULL, missionPath );

		while ( !pHOST->step() ) {
			while ( _kbhit() ) {
				char hit = _getch();
				pHOST->keyboardInput( hit );
			}
			steps++;
		}
		
		#if defined(TRACE)
		System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 0 ---" );
		#endif

		pHOST->stop();

		
		#if defined(TRACE)
		System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 1 ---" );
		#endif

		delete pHOST;
		
		#if defined(TRACE)
		System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 2 ---" );
		#endif

		// TEMP DEBUG
//		while (1);

	} else if ( STARTUP_MODE == 1 ) { // test server
		Log.log( 3, "Starting server mode..." );
		//con = agent.openListener( TEMP_PORT );

		while ( spin ) {
			//spin = !agent.step();
		}

	} else { // test client
		Log.log( 3, "Starting client mode..." );
		//con = agent.openConnection();


		while ( spin ) {
			//spin = !agent.step();
		}

	}
	
	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 3 ---" );
	#endif

	delete apb;

	// Clean up Winsock
	WSACleanup();

	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine( "--- Exit _tmain() for startup thread: Autonomic ---" );
	_CrtDumpMemoryLeaks();
	#endif

	printf( "Finished. Press ENTER to exit..." );
	//getchar();

	return 0;
}

