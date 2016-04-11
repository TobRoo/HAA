// Autonomic.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>


#include "autonomic.h"
#include "agentHost.h"

#include "time.h"

#include <windows.h>

#include "conio.h"

#include "mex.h"
#include "atlstr.h"

#include <cstring>
#include <iostream>
#include <string>



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

int mainFunc(int argc, _TCHAR* argv[]) {

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
	mexPrintf("\nBefore init logging.\n");
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
	mexPrintf("\nAfter getcurrentdirectory.\n");
	if ( argc == 1 ) {
		wsprintf( logDirectory + wcsnlen(logDirectory,512), _T("\\data\\logs %hs"), timeBuf );
	}
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	if ( argc >= 2 ) { // config file specified
		sprintf_s( configPath, sizeof(configPath), "%ws", argv[1] );
		mexPrintf("\n Configpath: %s.REMOVETHISWHENDONE \n", configPath);

		// strip out slashes
		char *c = configPath;
		while ( *c != 0 ) {
			if ( *c == '\\' || *c == '/' ) *c = '_';
			c++;
		}
		wsprintf( logDirectory + wcsnlen(logDirectory,512), _T("\\data\\logs %hs %hs"), timeBuf, configPath );
		mexPrintf("\n Configpath: %s.REMOVETHISWHENDONE \n", configPath);

		// copy proper configPath again
		sprintf_s( configPath, sizeof(configPath), "%ws", argv[1] ); 
	}
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	if ( argc >= 3 ) { // mission file specified
		sprintf_s( missionPath, sizeof(missionPath), "%ws", argv[2] );
	} 
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	_tmkdir( logDirectory );

	wsprintf( dumpDirectory, _T("%s\\dump"), logDirectory );	
	_tmkdir( dumpDirectory );

	sprintf_s( logName, "%ws\\log %s.txt", logDirectory, timeBuf );
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	apb = new AgentPlayback( PLAYBACKMODE_OFF, NULL );
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	Log.setAgentPlayback( apb );
	Log.setLogMode( LOG_MODE_COUT );
	Log.setLogMode( LOG_MODE_FILE, logName );
	Log.setLogLevel( LOG_LEVEL_VERBOSE );
	Log.log( 0, "Autonomic %d.%d.%d", AUTONOMIC_MAJOR_VERSION, AUTONOMIC_MINOR_VERSION, AUTONOMIC_SUB_VERSION );
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
		Log.log( 0, "WSAStartup failed: %d", iResult );
		return 1;
	} else {
		Log.log( 3, "WSAStartup succeeded" );
	}
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	// temp
	int spin = 1;
	int steps = 0;
	
	if ( STARTUP_MODE == MODE_HOST ) { // host
		Log.log( 3, "Starting host mode..." );
		
		char logDir[512];
		
		sprintf_s( logDir, "%ws", logDirectory );
		mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
		pHOST = new AgentHost( "library\\", LOG_LEVEL_ALL, logDir, PLAYBACKMODE_DEFAULT, NULL );
		mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
		mexPrintf("\n Configpath: %s.REMOVETHISWHENDONE \n", configPath);
		if ( argc >= 2 ) {
			mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
			pHOST->configure( configPath );
		} else {
			mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
			pHOST->configure();
		}
		mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
		pHOST->start( NULL, missionPath );
		mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
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
		mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
		
		#if defined(TRACE)
		System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 1 ---" );
		#endif

		delete pHOST;
		
		#if defined(TRACE)
		System::Diagnostics::Trace::WriteLine( "--- _tmain(): Stopping 2 ---" );
		#endif

		// TEMP DEBUG
		//while (1);

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
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	delete apb;
	mexPrintf("\n Reached Ln %d, in file %s.REMOVETHISWHENDONE \n", __LINE__, __FILE__);
	// Clean up Winsock
	WSACleanup();

	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine( "--- Exit _tmain() for startup thread: Autonomic ---" );
	_CrtDumpMemoryLeaks();
	#endif

	mexPrintf( "Finished. Press ENTER to exit..." );
	//getchar();

	return 0;
}


/* The gateway function */
void mexFunction(int nlhs, mxArray *plhs[],
	int nrhs, const mxArray *prhs[])
{
//	double *inMatrix;               /* 1xN input matrix */
//	size_t ncols;                   /* size of matrix */
//	double *outMatrix;              /* output matrix */
	//inMatrix = mxGetPr(prhs[1]);

	char *hostConfig, *missionFile;//, *output_buf;
	_TCHAR *hostConfig_t, *missionFile_t;
//	_TCHAR *output_buf_t;
	size_t bufLenHost, bufLenMission;

	/* check for proper number of arguments */
	if (nrhs < 1)
		mexErrMsgIdAndTxt("MATLAB:invalidNumInputs",
			"At least one input required.");
	else if (nlhs > 1)
		mexErrMsgIdAndTxt("MATLAB:maxlhs",
			"Too many output arguments.");

	/* input must be a string */
	if (mxIsChar(prhs[0]) != 1)
		mexErrMsgIdAndTxt("MATLAB:hostConfigNotString",
			"Input must be a string.");

	/* input must be a row vector */
	if (mxGetM(prhs[0]) != 1)
		mexErrMsgIdAndTxt("MATLAB:inputNotVector",
			"Input must be a row vector.");

	/* get the length of the input string */
	bufLenHost = (mxGetM(prhs[0]) * mxGetN(prhs[0])) + 1;

	/* copy the string data from prhs[0] into a C string input_ buf.    */
	hostConfig = mxArrayToString(prhs[0]);

	if (hostConfig == NULL)
		mexErrMsgIdAndTxt("MATLAB:conversionFailed",
			"Could not convert input to string.");

	mexPrintf("\n Contents of hostConfig %s \n", hostConfig);

	if (nrhs == 2) {

		/* input must be a string */
		if (mxIsChar(prhs[1]) != 1)
			mexErrMsgIdAndTxt("MATLAB:missionFileNotString",
				"Input must be a string.");

		/* input must be a row vector */
		if (mxGetM(prhs[1]) != 1)
			mexErrMsgIdAndTxt("MATLAB:inputNotVector",
				"Input must be a row vector.");

		/* get the length of the input string */
		bufLenMission = (mxGetM(prhs[1]) * mxGetN(prhs[1])) + 1;

		/* copy the string data from prhs[0] into a C string input_ buf.    */
		missionFile = mxArrayToString(prhs[1]);

		if (missionFile == NULL)
			mexErrMsgIdAndTxt("MATLAB:conversionFailed",
				"Could not convert input to string.");


		mexPrintf("\n Contents of missionFile %s \n", missionFile);
	}



	// newsize describes the length of the 
	// wchar_t string called wcstring in terms of the number 
	// of wide characters, not the number of bytes.
	size_t newsize = strlen(hostConfig) + 1;

	// The following creates a buffer large enough to contain 
	// the exact number of characters in the original string
	// in the new format. If you want to add more characters
	// to the end of the string, increase the value of newsize
	// to increase the size of the buffer.
	hostConfig_t = (_TCHAR *)mxMalloc(sizeof(_TCHAR)*newsize);// new wchar_t[newsize];

	// Convert char* string to a wchar_t* string.
	size_t convertedChars = 0;
	mbstowcs_s(&convertedChars, hostConfig_t, newsize, hostConfig, _TRUNCATE);
	// Display the result and indicate the type of string that it is.
	wcout << hostConfig_t << _T(" (wchar_t *)") << endl;
//	std::wstring testString(wcstring);
//	mexPrintf("\n testString?: %s \n", testString);
	mexPrintf("\n Contents of hostConfig_t %s \n", hostConfig_t);


	if (nrhs == 2) {
		newsize = strlen(missionFile) + 1;

		// The following creates a buffer large enough to contain 
		// the exact number of characters in the original string
		// in the new format. If you want to add more characters
		// to the end of the string, increase the value of newsize
		// to increase the size of the buffer.
		missionFile_t = (_TCHAR *)mxMalloc(sizeof(_TCHAR)*newsize);// new wchar_t[newsize];

		// Convert char* string to a wchar_t* string.
		convertedChars = 0;
		mbstowcs_s(&convertedChars, missionFile_t, newsize, missionFile, _TRUNCATE);
		// Display the result and indicate the type of string that it is.
		wcout << missionFile_t << _T(" (wchar_t *)") << endl;
		//	std::wstring testString(wcstring);
		//	mexPrintf("\n testString?: %s \n", testString);
		mexPrintf("\n Contents of missionFile_t %s \n", missionFile_t);
	}

	if (nrhs == 1) {
		_TCHAR* input_list[] = { TEXT("MatlabBridge"), hostConfig_t };
		mainFunc(2, input_list);
	}
	else if (nrhs == 2) {
		_TCHAR* input_list[] = { TEXT("MatlabBridge"), hostConfig_t, missionFile_t };
		mainFunc(3, input_list);
		mxFree(missionFile_t);
	}


//	mxFree(hostConfig);
//	mxFree(missionFile);
//	delete missionFile_t;
//	delete hostConfig_t;

	mxFree(hostConfig_t);


	return;
}