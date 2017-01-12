// AutonomicPlayback.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdio.h>

#include "..\\Autonomic\\autonomic.h"
#include "..\\Autonomic\\AgentHost.h"

#include "time.h"

#include <windows.h>

#ifdef _DEBUG
#using <System.dll>
#define TRACE

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif 

#define TOP_LOG_DIRS 9

int enumerateLogDirectories( char *logPath, WCHAR logDirName[TOP_LOG_DIRS][256], int *logDirCount );
int enumerateLogs( WCHAR *logPath, int page, WCHAR logName[10][256], int *logCount, int *moreLogs );

int _tmain(int argc, _TCHAR* argv[])
{
	int i;
	char buf[MAX_PATH];

#ifdef _DEBUG
	_crtBreakAlloc = -1; // break on Nth memory allocation
#endif

	#if defined(TRACE)
	System::Diagnostics::Trace::Listeners->Add( gcnew System::Diagnostics::TextWriterTraceListener( System::Console::Out ) );
	System::Diagnostics::Trace::AutoFlush = true;
	#endif

	WSADATA wsaData;
	int iResult;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
		printf_s( "WSAStartup failed: %d\n", iResult );
		goto returnFail;
	} else {
		printf_s( "WSAStartup succeeded\n" );
	}

	// -- select agent and playback file --
	char *logPath = "..\\data\\";
	WCHAR logDirName[TOP_LOG_DIRS][256];
	int logDirCount;
	WCHAR logDirPath[MAX_PATH];
	WCHAR logName[10][256];
	int logCount;
	FILE *logF;
	FILE *settingsF;
	char AT_name[256];
	char UUIDString[256];

	int breakOnAlloc = -1;
	int optionNum;

	// get crt break val or use last settings
	printf( "0) Use last settings\n1) _crtBreakAlloc = -1 (disabled)\n2) Enter _crtBreakAlloc\n\nNOTE: To properly use _crtBreakAlloc you should:\n  a) ensure the desired log file is selected (e.g. run once with option 2)\n  b) run once with option 0 to determine the desired break value (because \n     selecting things in this menu unfortunately changes the allocation\n     count)\n  c) use option 2 to set the _crtDebugAlloc value\n  d) run with option 0 again so that the correct allocation count is used\n     from step b)\n\n" );
	do {
		printf( "Select an option: " );
		*buf = getchar();
		while( getchar() != '\n' ); // clear until return
	} while ( buf[0] - '0' < 0 || buf[0] - '0' >= 3 );

	optionNum = buf[0] - '0'; 

	if ( optionNum == 0 ) { // use last settings
		if ( fopen_s( &settingsF, "settings.txt", "r" ) ) {
			printf( "Couldn't open settings file\n" );
			goto returnFail; 
		}
		fscanf_s( settingsF, "%d\n", &breakOnAlloc );
#ifdef _DEBUG
		_crtBreakAlloc = breakOnAlloc;
#endif
		fgetws( logDirPath, MAX_PATH, settingsF );
		logDirPath[wcslen(logDirPath)-1] = '\0'; // strip \n
		fgets( AT_name, 256, settingsF );
		AT_name[strlen(AT_name)-1] = '\0'; // strip \n
		fgets( UUIDString, 256, settingsF ); // no \n here
		fclose( settingsF );
	} else { 

		if ( optionNum == 2 ) { // get _crtBreakAlloc
			printf( "Enter value for _crtBreakAlloc: " );
			while( 1 != scanf_s( "%d", &breakOnAlloc ) ) {
				while( getchar() != '\n' ); // clear until return
			}
			while( getchar() != '\n' ); // clear until return
		}
		
#ifdef _DEBUG
		_crtBreakAlloc = breakOnAlloc;
#endif

		// enumerate log directories
		enumerateLogDirectories( logPath, logDirName, &logDirCount );

		printf( "0) Use last settings\n" );
		for ( i=0; i<logDirCount; i++ ) {
			printf( "%d) %ls\n", i+1, logDirName[i] );
		}

		do {
			printf( "Select a directory: " );
			*buf = getchar();
			while( getchar() != '\n' ); // clear until return
		} while ( buf[0] - '0' < 0 || buf[0] - '0' >= logDirCount+1 );

		i = buf[0] - '0'; // use i to store selection

		if ( i == 0 ) { // use last settings
			if ( fopen_s( &settingsF, "settings.txt", "r" ) ) {
				printf( "Couldn't open settings file\n" );
				goto returnFail; 
			}
			fscanf_s( settingsF, "%d\n", &i ); // throw away
			fgetws( logDirPath, MAX_PATH, settingsF );
			logDirPath[wcslen(logDirPath)-1] = '\0'; // strip \n
			fgets( AT_name, 256, settingsF );
			AT_name[strlen(AT_name)-1] = '\0'; // strip \n
			fgets( UUIDString, 256, settingsF ); // no \n here
			fclose( settingsF );
		} else {
			i--; // set i to the name index

			swprintf_s( logDirPath, MAX_PATH, L"%hs%s\\", logPath, logDirName[i] );

			// enumerate logs
			int page = 0;
			int moreLogs;
			do { 
				enumerateLogs( logDirPath, page, logName, &logCount, &moreLogs );

				system("cls");
				printf( "Page %d\n", page );
				for ( i=0; i<logCount; i++ ) {
					printf( "%d) %.64ls\n", i, logName[i] );
				}
				
				if ( page != 0 ) printf( "p) previous page\n" );
				if ( moreLogs ) printf( "n) next page\n" );

				do {
					printf( "Select a log: " );
					*buf = getchar();
					while( getchar() != '\n' ); // clear until return
				} while ( (buf[0] - '0' < 0 || buf[0] - '0' >= logCount) 
					&& (buf[0] != 'p' || page == 0) 
					&& (buf[0] != 'n' || !moreLogs) );

				if ( buf[0] == 'p' ) {
					page--;
				} else if ( buf[0] == 'n' ) {
					page++;
				} else {
					break;
				}
			} while (1);

			i = buf[0] - '0'; // this is the log we want

			// open the log to parse the agent id
			sprintf_s( buf, MAX_PATH, "%ls%ls", logDirPath, logName[i] );
				
			if ( fopen_s( &logF, buf, "r" ) ) { 
				printf( "Failed to open log file! %s\n", buf );
				goto returnFail; 
			}

			// clear first line
			fgets( buf, MAX_PATH, logF );
			// get agent type
			if ( 5 != fscanf_s( logF, "[%d:%d:%d.%d] %s\n", &i, &i, &i, &i, AT_name, 256 ) ) {
				printf( "Expected agent name\n" );
				goto returnFail; 
			}
			// finish line
			fgets( buf, MAX_PATH, logF );
			// clear agent UUID line
			fgets( buf, MAX_PATH, logF );
			// get playbackfile UUID
			if ( 5 != fscanf_s( logF, "[%d:%d:%d.%d] AgentPlayback UUID %s\n", &i, &i, &i, &i, UUIDString, 256 ) ) {
				printf( "Expected agentplayback UUID\n" );
				goto returnFail; 
			}

			fclose( logF );	
			
			
		}

		// save settings for next time
		if ( fopen_s( &settingsF, "settings.txt", "w" ) ) {
			printf( "Couldn't save settings file\n" );
			goto returnFail; 
		}
		fprintf_s( settingsF, "%d\n%ls\n%s\n%s", breakOnAlloc, logDirPath, AT_name, UUIDString );
		fclose( settingsF );
	}

	if ( optionNum == 2 ) { 
		printf( "\nSettings saved, restart and run with option 0\n\n" );
		goto returnFail; 
	}

			
	
	// -- Startup info --
	char *libraryPath = "library\\"; // library path
	
	sAddressPort ap;
	sprintf_s( ap.address, sizeof(ap.address), "127.0.0.1" );
	sprintf_s( ap.port, sizeof(ap.port), "50000" );

	UUID ticket;
	UuidCreateNil( &ticket );
	int logLevel = 9;
	char logDirectory[MAX_PATH];
	WCHAR baseDirectory[512];

	GetCurrentDirectory( 512, baseDirectory );

	sprintf_s( logDirectory, MAX_PATH, "%ls", baseDirectory );
	
	// strip last directory to reach base path
	i = (int)wcslen(baseDirectory);
	while ( baseDirectory[i] != '\\' ) i--;
	baseDirectory[i] = '\0';

	SetCurrentDirectory( baseDirectory );

	char playbackFile[256];
	sprintf_s( playbackFile, 256, "%lsx %s.apb", logDirPath+1, UUIDString ); // strip off the leading . of ..\\

	if ( strcmp( AT_name, "AgentHost" ) != 0 ) { // regular agent
		WCHAR wbuf[512];

		char AT_object[256]; // agent template object
		sprintf_s( AT_object, 256, "%s.dll", AT_name );


		// -- Load agent DLL --
		HINSTANCE library = NULL;
		
		// set the current directory to the library path in case they need their own dlls
		SetCurrentDirectory( baseDirectory );
		wsprintf( wbuf, _T(".\\%hs%hs\\"), libraryPath, AT_name );
		SetCurrentDirectory( wbuf );
		// load the library
		wsprintf( wbuf, _T("%hs"), AT_object );
		library = LoadLibrary( wbuf );
		SetCurrentDirectory( baseDirectory );
		// set the directory back
		if ( library == NULL ) {
			printf_s( "LoadLibrary failed for %ls\n", wbuf );
			goto returnFail;
		}

		// -- Spawn Agent --
		SpawnFuncPtr sfp;
		sfp = (SpawnFuncPtr)GetProcAddress( library, "Playback" );

		if ( sfp( &ap, &ticket, logLevel, logDirectory, PLAYBACKMODE_PLAYBACK, playbackFile ) ) {
			printf_s( "Failed to spawn agent\n" );
			goto returnFail;
		}

		// -- Clean up -- 
		FreeLibrary( library );
	} else { // host
		int steps = 0;

		AgentHost *pHOST;

		SetCurrentDirectory( baseDirectory );
		
		int runNumber = 1;	//Temporary fix, need to get actual runNumber - this is currently not stored anywhere for playback

		pHOST = new AgentHost( ".\\library\\", LOG_LEVEL_ALL, logDirectory, PLAYBACKMODE_PLAYBACK, playbackFile, runNumber );

		pHOST->configure();

		pHOST->start( NULL, NULL );

		while ( !pHOST->step() )
			steps++;

		pHOST->stop();

		delete pHOST;
	}


	// Clean up Winsock
	WSACleanup();

	#if defined(TRACE)
	System::Diagnostics::Trace::WriteLine( "--- Exit _tmain() for startup thread: AutonomicPlayback ---" );
	_CrtDumpMemoryLeaks();
	#endif

	printf( "Finished. Press ENTER to exit..." );
	getchar();

	return 0;

returnFail:
	printf( "Finished. Press ENTER to exit..." );
	getchar();

	return 1;
}



int enumerateLogDirectories( char *logPath, WCHAR logDirName[TOP_LOG_DIRS][256], int *logDirCount ) {
	int i;

	WCHAR tempName[TOP_LOG_DIRS][256];
	int tempNameInd = 0;

	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError=0;
	WCHAR Wbuf[MAX_PATH];

	*logDirCount = 0; // none found so far

	wsprintf( Wbuf, _T("%hs*"), logPath );
	hFind = FindFirstFile( Wbuf, &ffd);
	
	if ( hFind == INVALID_HANDLE_VALUE ) {
		dwError = GetLastError();
		if ( dwError != ERROR_FILE_NOT_FOUND ) {
			return 1; // some error!
		}

		return 0; // no folders
	}

	do {
		if ( ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) { // found a directory, see if it's a log directory
			if ( wcsncmp( ffd.cFileName, L"logs", 4 ) )
				continue;

			wcscpy_s( tempName[tempNameInd], 256, ffd.cFileName );
			tempNameInd = (tempNameInd + 1) % TOP_LOG_DIRS;
			(*logDirCount)++;
		}
	} while (FindNextFile(hFind, &ffd) != 0);
	
	dwError = GetLastError();
	if ( dwError != ERROR_NO_MORE_FILES ) {
		return 1; // some error!
	}

	FindClose( hFind );

	for ( i=0; i<min(TOP_LOG_DIRS,*logDirCount); i++ ) {
		tempNameInd = (TOP_LOG_DIRS + tempNameInd - 1) % TOP_LOG_DIRS;
		wcscpy_s( logDirName[i], 256, tempName[tempNameInd] );
	}

	if ( *logDirCount > TOP_LOG_DIRS )
		*logDirCount = TOP_LOG_DIRS;
		
	return 0;
}


int enumerateLogs( WCHAR *logPath, int page, WCHAR logName[10][256], int *logCount, int *moreLogs ) {
	int hits;

	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError=0;
	WCHAR Wbuf[MAX_PATH];

	*logCount = 0; // none found so far
	*moreLogs = 0;

	wsprintf( Wbuf, _T("%s*.txt"), logPath );
	hFind = FindFirstFile( Wbuf, &ffd);
	
	if ( hFind == INVALID_HANDLE_VALUE ) {
		dwError = GetLastError();
		if ( dwError != ERROR_FILE_NOT_FOUND ) {
			return 1; // some error!
		}

		return 0; // no folders
	}

	hits = 0;
	do {
		if ( !(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) { // found a file
			if ( hits < page*10 ) {
				hits++;
			} else {
				if ( *logCount == 10 ) {
					(*logCount)++;
					break;
				}
				wcscpy_s( logName[*logCount], 256, ffd.cFileName );
				(*logCount)++;
			}
		}
	} while (FindNextFile(hFind, &ffd) != 0);
	
	if ( *logCount == 11 ) { // we broke early
		*moreLogs = 1;			
		*logCount = 10;
	} else {
		dwError = GetLastError();
		if ( dwError != ERROR_NO_MORE_FILES ) {
			return 1; // some error!
		}
	}

	FindClose( hFind );
		
	return 0;
}