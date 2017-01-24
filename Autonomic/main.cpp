// Autonomic.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>


#include "autonomic.h"
#include "agentHost.h"

#include "time.h"

#include <windows.h>

#include "conio.h"

#include "mapRandom.h"
#include <sstream>

////Def for muting all AgentPlayback and log messages for actual simulation runs
//#define EXPERIMENT_RUN
//
//#ifdef EXPERIMENT_RUN
//#define PLAYBACKMODE_DEFAULT PLAYBACKMODE_OFF
//#define LOG_MODE_COUT LOG_MODE_OFF
//#define LOG_MODE_FILE LOG_MODE_OFF
//#define LOG_MODE_EDIT LOG_MODE_OFF
//#define LOG_LEVEL_NORMAL	LOG_LEVEL_NONE
//#define LOG_LEVEL_VERBOSE	LOG_LEVEL_NONE
//#define LOG_LEVEL_ALL		LOG_LEVEL_NONE
//#endif




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
	switch (event)
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
	System::Diagnostics::Trace::Listeners->Add(gcnew System::Diagnostics::TextWriterTraceListener(System::Console::Out));
	System::Diagnostics::Trace::AutoFlush = true;
#endif

	WSADATA wsaData;
	int iResult;

	if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)EventHandler, TRUE) == FALSE) {
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
	time(&t_t);
	localtime_s(&stm, &t_t);
	strftime(timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm);

	// create the new log directory
	char configPath[64];
	WCHAR logDirectory[512];
	WCHAR dumpDirectory[512];
	char missionPath[256];
	missionPath[0] = 0;
	char learningTempFile[1024];
	sprintf(learningTempFile, "learningData.tmp");


	GetCurrentDirectory(512, logDirectory);

	if (argc == 1) {
		wsprintf(logDirectory + wcsnlen(logDirectory, 512), _T("\\data\\logs %hs"), timeBuf);
	}

	if (argc >= 2) { // config file specified
		sprintf_s(configPath, sizeof(configPath), "%ws", argv[1]);
		// strip out slashes
		char *c = configPath;
		while (*c != 0) {
			if (*c == '\\' || *c == '/') *c = '_';
			c++;
		}
		wsprintf(logDirectory + wcsnlen(logDirectory, 512), _T("\\data\\logs %hs %hs"), timeBuf, configPath);

		// copy proper configPath again
		sprintf_s(configPath, sizeof(configPath), "%ws", argv[1]);
	}

	char runCountString[64];
	int runCount = 1;	//Number of simulations to be run in sequence, 1 is default

	if (argc >= 3) { // runCount specified
		sprintf_s(runCountString, sizeof(runCountString), "%ws", argv[2]);
		runCount = atoi(runCountString);
	}


	if (argc >= 4) { // mission file specified
		sprintf_s(missionPath, sizeof(missionPath), "%ws", argv[3]);
	}

	_tmkdir(logDirectory);

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		Log.log(0, "WSAStartup failed: %d", iResult);
		return 1;
	}
	else {
		Log.log(3, "WSAStartup succeeded");
	}

	// temp
	int spin = 1;
	int steps = 0;






	WCHAR runLogDirectory[512];
	wsprintf(runLogDirectory, _T("%s\\runNumber"), logDirectory);

	for (int runNumber = 1; runNumber <= runCount; runNumber++) {

		wsprintf(logDirectory, _T("%s%d"), runLogDirectory, runNumber);
		_tmkdir(logDirectory);
		wsprintf(dumpDirectory, _T("%s\\dump"), logDirectory);
		_tmkdir(dumpDirectory);

		sprintf_s(logName, "%ws\\log %s.txt", logDirectory, timeBuf);

		apb = new AgentPlayback(PLAYBACKMODE_OFF, NULL);

		Log.setAgentPlayback(apb);
		Log.setLogMode(LOG_MODE_COUT);
		Log.setLogMode(LOG_MODE_FILE, logName);
		Log.setLogLevel(LOG_LEVEL_VERBOSE);
		Log.log(0, "Autonomic %d.%d.%d", AUTONOMIC_MAJOR_VERSION, AUTONOMIC_MINOR_VERSION, AUTONOMIC_SUB_VERSION);



		if (STARTUP_MODE == MODE_HOST) { // host
			Log.log(3, "Starting host mode...");

			char logDir[512];

			sprintf_s(logDir, "%ws", logDirectory);

			pHOST = new AgentHost("library\\", LOG_LEVEL_ALL, logDir, PLAYBACKMODE_DEFAULT, NULL, runNumber);

			if (argc >= 2) {
				pHOST->configure(configPath);
			}
			else {
				pHOST->configure();
			}

			if (argc >= 3) {		//runCount specified

			}

			//if (pHOST->gatherData) {			//This is the host (singular - only one per experiment - currently hostExclusive) - handle saving/loading learning data here

			//}



			if (argc >= 4) { // mission file specified - no need to do this for hostExclusive
			//Check if map should be randomized and/or revealed

				WCHAR randomMapDirectory[512];
				wsprintf(randomMapDirectory, _T("%ws\\randomMap"), logDirectory);
				_tmkdir(randomMapDirectory);

				WCHAR currentDirectory[512];
				char misFile[1024];
				char newMisFile[1024];
				char newPathFile[1024];
				char newLandmarkFile[1024];
				char newTargetFile[1024];
				char newLayoutFile[1024];


				GetCurrentDirectory(512, currentDirectory);					//READ INTO STRING, USE RFIND AND GET THE LAST PART OF THE PATH

				char configEnding[512];		//Second part of the log directory
				char logDirStart[512];		//First part of the log directory



				// Get the log directory, except for the configPath
				char *c = logDir;
				char *c2 = logDirStart;
				while (*c != 0) {
					if (*c == '\\')
						*c2 = '/';
					else
						*c2 = *c;
					if (*c == ']')
						break;
					c++;
					c2++;
				}

				//Get the last part of configPath
				c = configEnding;
				char *charPtr = strstr(configPath, "hostCfgs");
				if (charPtr != NULL) {
					while (*charPtr != 0) {


						if (*charPtr != '\\') {
							*c = *charPtr;
						}
						else {
							*c = '_';
						}

						//Log.log(0, "main:*c is: %c ",*c);

						charPtr++;
						c++;
					}
				}
				else {
					Log.log(0, "main: could not find hostCfgs directory - randomMap and mapReveal will fail. ");
				}

				/*	Log.log(0, "configEnding is: %s", configEnding);
					Log.log(0, "logDirStart is: %s", logDirStart);
					Log.log(0, "timeBuf is: %s", timeBuf);
					Log.log(0, "configPath is: %s", configPath);
					Log.log(0, "logDir is: %s", logDir);*/



					//Log.log(0, "randomMapDirectory is: %ws", randomMapDirectory);

				Log.log(0, "mission Path is: %s", missionPath);
				sprintf_s(misFile, "%s", missionPath);
				Log.log(0, "misFile path is: %s", misFile);
				sprintf_s(newMisFile, "%s%s%s/runNumber%d/randomMap/randomMisFile.ini", logDirStart, " ", configEnding, runNumber); //<- THIS WORKS!
				Log.log(0, "newMisFile path is: %s", newMisFile);
				sprintf_s(newPathFile, "%s%s%s/runNumber%d/randomMap/randomPathFile.path", logDirStart, " ", configEnding, runNumber);
				Log.log(0, "newPathFile path is: %s", newPathFile);
				sprintf_s(newLandmarkFile, "%s%s%s/runNumber%d/randomMap/randomLandmarkFile.ini", logDirStart, " ", configEnding, runNumber);
				Log.log(0, "newLandmarkFile path is: %s", newLandmarkFile);
				sprintf_s(newTargetFile, "%s%s%s/runNumber%d/randomMap/randomTargetFile.ini", logDirStart, " ", configEnding, runNumber);
				Log.log(0, "newTargetFile path is: %s", newTargetFile);
				sprintf_s(newLayoutFile, "%s%s%s/runNumber%d/randomMap/randomLayoutFile.ini", logDirStart, " ", configEnding, runNumber);
				Log.log(0, "newLayoutFile path is: %s", newLayoutFile);
				Log.log(0, "runCount is: %d", runCount);

				if (!mapRandomizer(misFile, newMisFile, newPathFile, newLandmarkFile, newTargetFile, newLayoutFile)) {
					Log.log(0, "Random map selected.");
					pHOST->start(NULL, newMisFile);	//Use new random files
				}
				else {
					pHOST->start(NULL, missionPath);	//Use specified layout
				}
			}
			else {	//No mission file, missionPath is NULL, hostExclusive
				pHOST->start(NULL, missionPath);
			}

			while (!pHOST->step()) {
				while (_kbhit()) {
					char hit = _getch();
					pHOST->keyboardInput(hit);
				}
				steps++;
			}

			#if defined(TRACE)
			System::Diagnostics::Trace::WriteLine("--- _tmain(): Stopping 0 ---");
			#endif

			pHOST->stop();
			pHOST->LearningDataDump();

			#if defined(TRACE)
			System::Diagnostics::Trace::WriteLine("--- _tmain(): Stopping 1 ---");
			#endif


			//if (pHOST->gatherData) {			//This is the host (singular - only one per experiment - currently hostExclusive) - handle saving/loading learning data here
			//	
			//	
			//}




			delete pHOST;

			#if defined(TRACE)
			System::Diagnostics::Trace::WriteLine("--- _tmain(): Stopping 2 ---");
			#endif

			// TEMP DEBUG
			//		while (1);

			}
		else if (STARTUP_MODE == 1) { // test server
			Log.log(3, "Starting server mode...");
			//con = agent.openListener( TEMP_PORT );

			while (spin) {
				//spin = !agent.step();
			}

		}
		else { // test client
			Log.log(3, "Starting client mode...");
			//con = agent.openConnection();


			while (spin) {
				//spin = !agent.step();
			}

		}

		apb->apbSleep(5000);	//For all hosts to shut down before starting next run
	}

	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine("--- _tmain(): Stopping 3 ---");
	#endif

	delete apb;

	// Clean up Winsock
	WSACleanup();

	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine("--- Exit _tmain() for startup thread: Autonomic ---");
	_CrtDumpMemoryLeaks();
	#endif

	printf("Finished. Press ENTER to exit...");
	//getchar();

	return 0;
		}

