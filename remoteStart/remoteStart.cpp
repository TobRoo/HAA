// remoteStart.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include "time.h"
#include <sys/timeb.h>


#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include "conio.h"

#include <list>

#using <System.dll> 
//using namespace System;
using namespace System::Diagnostics;
using namespace System::ComponentModel;


int stop; // stop flag

BOOL WINAPI EventHandler(DWORD event)
{
    switch(event)
    {
        case CTRL_C_EVENT:
			break;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
			stop = 1;
			return TRUE;
    }

    return TRUE;
}

int sendCmd( char *addressBuf, char *portBuf, char *cmdBuf ) {
	int iResult;
	struct addrinfo *addr_result, addr_hints;
		
	// connect
	printf( "connecting to %s:%s\n", addressBuf, portBuf );
	ZeroMemory( &addr_hints, sizeof(addr_hints) );
	addr_hints.ai_family = AF_INET; //AF_UNSPEC; we don't want AF_INET6
	addr_hints.ai_socktype = SOCK_STREAM;
	addr_hints.ai_protocol = IPPROTO_TCP;
	addr_hints.ai_flags = NULL;
	
	// Resolve the server address and port
	iResult = getaddrinfo( addressBuf, portBuf, &addr_hints, &addr_result);
	if ( iResult != 0 ) {
		printf( "getaddrinfo failed: %d\n", iResult);
		return 1;
	}

	// create socket
	SOCKET sock = socket( addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
	if ( sock == INVALID_SOCKET ) {
		printf( "Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo( addr_result );
		return 1;
	}

	// connect
	iResult = connect( sock, addr_result->ai_addr, (int)addr_result->ai_addrlen );
	if ( iResult == SOCKET_ERROR ) {
		iResult = WSAGetLastError();
		if ( iResult != WSAEWOULDBLOCK ) {
			printf( "Error at connect(): %ld\n", iResult);
			closesocket( sock );
			freeaddrinfo( addr_result );
			return 1;
		}
	}

	freeaddrinfo( addr_result );

	// send command
	printf( "sending cmd: %s\n", cmdBuf );
	int bufLen = (int)strlen(cmdBuf);
	iResult = send( sock, cmdBuf, bufLen, NULL );
	if ( iResult == SOCKET_ERROR ) {
		int err = WSAGetLastError();
		if ( err == WSAEWOULDBLOCK ) {
			iResult = 0; // no data sent
		} else { // unhandled error
			printf( "socket error, %d", err );
		}
	} 
	if ( iResult != bufLen ) {		
		printf( "wasn't able to send entire message %d vs %d", iResult, bufLen );
	}

	// disconnect
	printf( "disconnecting\n" );
	closesocket( sock );

	return 0;
}

int parseBatch( char *batchFile, WCHAR *baseDir ) {
	int iResult;
	int timeout;
	FILE *batchF = NULL;
	FILE *tempF = NULL;

	// open batch file
	iResult = fopen_s( &batchF, batchFile, "r" );
	if ( iResult ) {
		printf( "Error opening batch file %s for reading\n", batchFile );
		return 0;
	}

	// open temp file
	iResult = fopen_s( &tempF, "tempBatch.tmp", "w" );
	if ( iResult ) {
		printf( "Error opening temp file tempBatch.tmp for writing\n" );
		return 0;
	}

	// scan lines until we get to a fresh test
	char ln[MAX_PATH];
	char cmd[MAX_PATH];
	bool hit = false;
	while ( fgets( ln, MAX_PATH, batchF ) ) {
		if ( ln[0] != '[' ) {
			hit = true;
			break;
		} else {
			fprintf_s( tempF, "%s", ln );
		}
	}
	if ( !hit ) {
		fclose( batchF );
		fclose( tempF );
		DeleteFile( L"tempBatch.tmp" );

		return 0;
	}
	
	// update current line
	char formatBuf[64];
	time_t t_t;
	_timeb tb;
	struct tm stm;
	time( &t_t );
	_ftime_s( &tb );
	localtime_s( &stm, &t_t );
	strftime( formatBuf, 64, "%y.%m.%d %H:%M:%S", &stm );
	
	fprintf_s( tempF, "[%s.%3d] %s", formatBuf, tb.millitm, ln );

	float ftimeout;
	int ip1, ip2, ip3, ip4, port;
	char addressBuf[64];
	char portBuf[64];
	bool remoteStartFile = true;
	if ( 6 == sscanf_s( ln, "%f %d.%d.%d.%d:%d", &ftimeout, &ip1, &ip2, &ip3, &ip4, &port ) ) { // single line command
		sscanf_s( ln, "%f %[^:]:%s %[^\n]", &ftimeout, addressBuf, 64, portBuf, 64, cmd, MAX_PATH );
		remoteStartFile = false;
	} else { // remote start file
		sscanf_s( ln, "%f %s", &ftimeout, cmd, MAX_PATH );
	}

	timeout = (int)(ftimeout*60);
	//sprintf_s( cmd, MAX_PATH, "%s", ln );
	//cmd[strlen(cmd)-1] = 0; // trim off new line character

	// copy remaining lines
	while ( fgets( ln, MAX_PATH, batchF ) ) {
		fprintf_s( tempF, "%s", ln );
	}

	// swap in temp file
	fclose( batchF );
	fclose( tempF );
	WCHAR WbatchFile[MAX_PATH];
	swprintf_s( WbatchFile, MAX_PATH, L"%hs", batchFile );
	DeleteFile( WbatchFile );
	rename( "tempBatch.tmp", batchFile );

	if ( remoteStartFile ) { // pass to a remoteStart instance
		// execute command
		printf( "batch executing %s, %d second timeout\n", cmd, timeout );
		WCHAR appBuf[MAX_PATH];
		WCHAR argBuf[MAX_PATH];

		swprintf_s( appBuf, MAX_PATH, L"remoteStartD.exe" );
		swprintf_s( argBuf, MAX_PATH, L"%hs", cmd );

		HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );
	} else { // single command, handle ourselves	
		printf( "executing %s, %d second timeout\n", cmd, timeout );
		sendCmd( addressBuf, portBuf, cmd );
	}

	return timeout;
}

int printAckSummary( char ackSummary[64][MAX_PATH], int ackCount ) {
	int i;
	printf( "\n--- Ack Summary --------------------------------------------\n" );
	for ( i=0; i<ackCount; i++ ) {
		printf( "%s\n", ackSummary[i] );
	}
	printf( "------------------------------------------------------------\n\n" );

	return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int i;

	WSADATA wsaData;
	int iResult;
	struct addrinfo *addr_result, addr_hints;

	char rbuf[1024];

	WCHAR baseDir[1024];
	GetCurrentDirectory( 1024, baseDir );

	stop = 0;
	
	if (SetConsoleCtrlHandler( (PHANDLER_ROUTINE)EventHandler,TRUE ) == FALSE ) {
        // failed to install or un-install the handler.
        goto retError;
    }

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
		printf( "WSAStartup failed: %d\n", iResult );
		goto retError;
	} else {
		printf( "WSAStartup succeeded\n" );
	}

	if ( argc == 2 && argv[1][0] == '-' ) { // mission done
		// get local address and tell them mission done
		char *address = "127.0.0.1";
		char *port = "49999";

		// detect addresses
		int count = 0;
		char buf10[10][256];
		char hostname[256];
		int iResult = gethostname(hostname, sizeof(hostname));
		if (iResult != 0) {
			printf("Error getting hostname: %u\n", WSAGetLastError());
		} else {
			hostent* pHostent = gethostbyname(hostname);
			if (pHostent==NULL) {
				printf("Error getting hostent: %u\n", WSAGetLastError());
			} else {
				hostent& he = *pHostent;
				sprintf_s( buf10[count], sizeof(buf10[0]), "%s", he.h_name );
				count++;

				sockaddr_in sa;
				for (int nAdapter=0; he.h_addr_list[nAdapter]; nAdapter++) {
					memcpy ( &sa.sin_addr.s_addr, he.h_addr_list[nAdapter],he.h_length);
					sprintf_s( buf10[count], sizeof(buf10[0]), "%s", inet_ntoa(sa.sin_addr) );
					count++;
					if ( count >= sizeof(buf10)/sizeof(buf10[0]) )
						break;
				}
			}
		}
		
		address = buf10[count-1]; // take last address

		// connect
		printf( "connecting to %s:%s\n", address, port );
		ZeroMemory( &addr_hints, sizeof(addr_hints) );
		addr_hints.ai_family = AF_INET; //AF_UNSPEC; we don't want AF_INET6
		addr_hints.ai_socktype = SOCK_STREAM;
		addr_hints.ai_protocol = IPPROTO_TCP;
		addr_hints.ai_flags = NULL;
		
		// Resolve the server address and port
		iResult = getaddrinfo( address, port, &addr_hints, &addr_result);
		if ( iResult != 0 ) {
			printf( "getaddrinfo failed: %d\n", iResult);
			goto retError;
		}

		// create socket
		SOCKET sock = socket( addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
		if ( sock == INVALID_SOCKET ) {
			printf( "Error at socket(): %ld\n", WSAGetLastError());
			freeaddrinfo( addr_result );
			goto retError;
		}

		// connect
		iResult = connect( sock, addr_result->ai_addr, (int)addr_result->ai_addrlen );
		if ( iResult == SOCKET_ERROR ) {
			iResult = WSAGetLastError();
			if ( iResult != WSAEWOULDBLOCK ) {
				printf( "Error at connect(): %ld\n", iResult);
				closesocket( sock );
				freeaddrinfo( addr_result );
				goto retError;
			}
		}

		freeaddrinfo( addr_result );

		// send command
		char *cmdBuf = "mission_done";
		printf( "sending cmd: %s\n", cmdBuf );
		int bufLen = (int)strlen(cmdBuf);
		iResult = send( sock, cmdBuf, bufLen, NULL );
		if ( iResult == SOCKET_ERROR ) {
			int err = WSAGetLastError();
			if ( err == WSAEWOULDBLOCK ) {
				iResult = 0; // no data sent
			} else { // unhandled error
				printf( "socket error, %d", err );
			}
		} 
		if ( iResult != bufLen ) {		
			printf( "wasn't able to send entire message %d vs %d", iResult, bufLen );
		}

		// disconnect
		printf( "disconnecting\n" );
		closesocket( sock );

	} else if ( argc == 1 ) { // listener
		int batchTimeout;
		bool batchRunning = false;
		char batchFile[MAX_PATH];
		char batchGroup[64];
		int batchCount;

		std::list<SOCKET> connections;
		std::list<SOCKET>::iterator iC;
		std::list<SOCKET>::iterator iCLast;
		
		struct fd_set fd_read, fd_write, fd_except;
		struct fd_set fd_readers;

		ZeroMemory( &addr_hints, sizeof(addr_hints) );
		addr_hints.ai_family = AF_INET;
		addr_hints.ai_socktype = SOCK_STREAM;
		addr_hints.ai_protocol = IPPROTO_TCP;

		char *address = "127.0.0.1";
		char *port = "49999";

		// detect addresses
		int count = 0;
		char buf10[10][256];
		char hostname[256];
		int iResult = gethostname(hostname, sizeof(hostname));
		if (iResult != 0) {
			printf("Error getting hostname: %u\n", WSAGetLastError());
		} else {
			hostent* pHostent = gethostbyname(hostname);
			if (pHostent==NULL) {
				printf("Error getting hostent: %u\n", WSAGetLastError());
			} else {
				hostent& he = *pHostent;
				sprintf_s( buf10[count], sizeof(buf10[0]), "%s", he.h_name );
				count++;

				sockaddr_in sa;
				for (int nAdapter=0; he.h_addr_list[nAdapter]; nAdapter++) {
					memcpy ( &sa.sin_addr.s_addr, he.h_addr_list[nAdapter],he.h_length);
					sprintf_s( buf10[count], sizeof(buf10[0]), "%s", inet_ntoa(sa.sin_addr) );
					count++;
					if ( count >= sizeof(buf10)/sizeof(buf10[0]) )
						break;
				}
			}
		}
		
		address = buf10[count-1]; // take last address
		iResult = getaddrinfo( address, port, &addr_hints, &addr_result );
		if ( iResult != 0 ) {
			printf( "getaddrinfo failed: %d\n", iResult );
			goto retOk;
		}

		// create socket
		SOCKET sock = socket( addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol );
		if ( sock == INVALID_SOCKET ) {
			printf( "Error at socket(): %ld\n", WSAGetLastError() );
			freeaddrinfo( addr_result );
			goto retOk;
		}

		// bind socket
		iResult = bind( sock, addr_result->ai_addr, (int)addr_result->ai_addrlen );
		if (iResult == SOCKET_ERROR) {
			printf( "bind failed: %d\n", WSAGetLastError() );
			closesocket( sock );
			freeaddrinfo( addr_result );
			goto retOk;
		}
		
		freeaddrinfo( addr_result );

		// listen on socket
		if ( listen( sock, SOMAXCONN ) == SOCKET_ERROR ) {
			printf( "Error at listen(): %ld\n", WSAGetLastError() );
			closesocket( sock );
			goto retOk;
		}

		// add to fd_set
		memset( &fd_readers, 0, sizeof(fd_readers) );
		FD_SET( sock, &fd_readers );

		connections.push_back( sock );

		printf( "Listening on %s:%s\n", address, port );

		int ackCount = 0;
		int ackWaiting = 0;
		char ackSummary[64][MAX_PATH];

		// run
		timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		while ( !stop ) {
			memcpy( &fd_read, &fd_readers, sizeof(fd_read) );
			memset( &fd_write, 0, sizeof(fd_write) );
			memset( &fd_except, 0, sizeof(fd_except) );

			iResult = select( NULL, &fd_read, &fd_write, &fd_except, &timeout );
			if ( iResult == SOCKET_ERROR ) {
				printf( "select failed, %ld\n", WSAGetLastError() );
				break;
			} else if ( iResult > 0 ) {
				if ( fd_read.fd_count ) {
					SOCKET con;
					for ( iC = connections.begin(); iC != connections.end(); ) {
						con = *iC;
						iCLast = iC;
						iC++;
						if ( FD_ISSET( con, &fd_read ) ) {
							if ( con == sock ) { // this is our listener
								// accept connection
								SOCKET acc = accept( sock, NULL, NULL );
								if ( acc == INVALID_SOCKET ) {
									printf( "accept failed: %d\n", WSAGetLastError());
									break;
								}
								FD_SET( acc, &fd_readers );
								connections.push_back( acc );
								printf( "accepted connection!\n" );
							} else { // read data
								iResult = recv( con, rbuf, sizeof(rbuf), NULL );
								if ( iResult == SOCKET_ERROR ) {
									iResult = WSAGetLastError();
									switch ( iResult ) {
									case WSAEWOULDBLOCK:
										continue;
									case WSAECONNRESET:
										// close connection
										FD_CLR( con, &fd_readers );
										connections.erase( iCLast );
										continue;
									case WSAEMSGSIZE:
										printf( "recv: rbuf too small\n" );
										break;
									default:
										printf( "recv: unhandled error, %d\n", iResult );
										// close connection
										FD_CLR( con, &fd_readers );
										connections.erase( iCLast );
										continue;
									}
								} else if ( iResult == 0 ) { // connection closed
									printf( "connection closed\n" );
									// close connection
									FD_CLR( con, &fd_readers );
									connections.erase( iCLast );
									continue;
								}
					
								rbuf[iResult] = 0; // end string

								if ( !strncmp( rbuf, "run", 3 ) ) {
									printf( "running %s\n", rbuf+4 );

									WCHAR appBuf[MAX_PATH];
									WCHAR argBuf[MAX_PATH];

									i = 4;
									while ( rbuf[i] != ' ' && rbuf[i] != 0 ) { 
										appBuf[i-4] = rbuf[i];
										i++;
									}
									appBuf[i-4] = 0;
									if ( rbuf[i] != 0 )
										swprintf_s( argBuf, MAX_PATH, L"%hs", rbuf+i+1 );
									else
										argBuf[0] = 0;
							
									HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );
								} else if ( !strncmp( rbuf, "kill", 4 ) ) {
									System::String ^procName = gcnew System::String(rbuf+5);

									// check that this is an acceptable process to kill!
									if ( !(
										!System::String::Compare( procName, "AutonomicD" )
										|| !System::String::Compare( procName, "Autonomic" )
										|| !System::String::Compare( procName, "spinner" ) ) ) 
										continue;

									array<Process^>^localByName = Process::GetProcessesByName( procName );
									
									for ( i = 0; i < localByName->Length; i++ ) {
										System::Console::WriteLine( "closing {0}\n", localByName[i]->MainWindowTitle );

										localByName[i]->CloseMainWindow(); // attempt to close gracefully
										localByName[i]->WaitForExit( 5000 ); // give the process 5 seconds to exit
										if ( !localByName[i]->HasExited ) {
											printf( "...taking too long, attempting to kill\n" );
											localByName[i]->Kill(); // force kill
										}
									}
								} else if ( !strncmp( rbuf, "batch", 5 ) ) {
									printf( "starting batch %s\n", rbuf+6 );

									sprintf_s( batchFile, MAX_PATH, "%s", rbuf+6 );

									if ( !strncmp( batchFile + 12, "group", 5 ) ) {
										strncpy_s( batchGroup, 64,  batchFile + 12, 7 );
									} else {
										batchGroup[0] = 0;
									}

									batchTimeout = parseBatch( batchFile, baseDir );
									batchRunning = (batchTimeout > 0);
									if ( !batchRunning )
										printf( "finished batch %s\n", batchFile );
									else {
										batchCount = 1;
									}
								} else if ( !strncmp( rbuf, "mission_done", 11 ) ) {
									if ( batchRunning ) {
										printf( "batch entry %d: mission finished, moving to next entry\n", batchCount );
										batchCount++;

										printf( "sleeping 120 seconds\n" );
										Sleep( 120000 );

										WCHAR appBuf[MAX_PATH];
										WCHAR argBuf[MAX_PATH];

										swprintf_s( appBuf, MAX_PATH, L"remoteStartD.exe" );
										swprintf_s( argBuf, MAX_PATH, L"remoteStart\\%hsKillAutonomicD.cfg", batchGroup );

										HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );
					
										printf( "sleeping 120 seconds\n" );
										Sleep( 120000 );

										swprintf_s( argBuf, MAX_PATH, L"remoteStart\\%hsClearAPBFiles.cfg", batchGroup );

										hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );
										
										
										printf( "sleeping 120 seconds\n" );
										Sleep( 120000 );

										batchTimeout = parseBatch( batchFile, baseDir );
										batchRunning = (batchTimeout > 0 );
										if ( !batchRunning )
											printf( "finished batch %s\n", batchFile );
									}
								} else if ( !strncmp( rbuf, "ack", 3 ) ) {
									printf( "%s\n", rbuf );
									strcpy_s( ackSummary[ackCount], MAX_PATH, rbuf + 4 );
									ackCount++;
									ackWaiting--;
									if ( !ackWaiting ) {
										printAckSummary( ackSummary, ackCount );
										ackCount = 0;
									}
								} else if ( !strncmp( rbuf, "ping", 4 ) ) { // request to perform a ping
									char addressBuf[64];
									char portBuf[64];
									char shareName[MAX_PATH];
									char cmdBuf[MAX_PATH];

									sscanf_s( rbuf, "ping %[^:]:%s %[^\n]", addressBuf, 64, portBuf, 64, shareName, MAX_PATH );

									// prepare doping command
									
									sprintf_s( cmdBuf, 64, "doping %s:%s %s", address, port, shareName );
									if ( !sendCmd( addressBuf, portBuf, cmdBuf ) ) {
										ackWaiting++; // ping was successfully sent
									} else { // connection failed
										sprintf_s( ackSummary[ackCount], MAX_PATH, "%s:%s connection FAILED", addressBuf, portBuf );
										ackCount++;
									}

								} else if ( !strncmp( rbuf, "doping", 6 ) ) { // recived a ping
									char addressBuf[64];
									char portBuf[64];
									char shareName[MAX_PATH];
									char accessResult[64];
									char cmdBuf[MAX_PATH];

									sscanf_s( rbuf, "doping %[^:]:%s %[^\n]", addressBuf, 64, portBuf, 64, shareName, MAX_PATH );

									// test share
									WIN32_FIND_DATA ffd;
									TCHAR szDir[MAX_PATH];
									HANDLE hFind = INVALID_HANDLE_VALUE;
									DWORD dwError = 0;

									swprintf_s( szDir, MAX_PATH, L"%hs\\*", shareName );
									hFind = FindFirstFile(szDir, &ffd);
									if (INVALID_HANDLE_VALUE == hFind) {
										sprintf_s( accessResult, 64, "FAILED" );
									} else {
										sprintf_s( accessResult, 64, "OK" );
									} 
									
									
									// send ack
									printf( "sending ack\n" );
									sprintf_s( cmdBuf, 64, "ack %s:%s access %s: %s", address, port, accessResult, shareName );
									sendCmd( addressBuf, portBuf, cmdBuf );

								}
							}
						}
					}
				}
			} else { // timeout, check for input
				while ( _kbhit() ) {
					char hit = _getch();
					if ( hit == 27 ) // escape
						stop = 1;
				}
				if ( batchRunning ) {
					batchTimeout--;
					if ( batchTimeout < 0 ) {
						// time's up
						printf( "batch entry %d: mission timed out, moving to next entry\n", batchCount );
						batchCount++;

						WCHAR appBuf[MAX_PATH];
						WCHAR argBuf[MAX_PATH];

						swprintf_s( appBuf, MAX_PATH, L"remoteStartD.exe" );
						swprintf_s( argBuf, MAX_PATH, L"remoteStart\\%hsKillAutonomicD.cfg", batchGroup );

						HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );

						printf( "sleeping 120 seconds\n" );
						Sleep( 120000 );

						swprintf_s( argBuf, MAX_PATH, L"remoteStart\\%hsClearAPBFiles.cfg", batchGroup );

						hi = ShellExecute( NULL, NULL, appBuf, argBuf, baseDir, SW_SHOW );
						
						printf( "sleeping 120 seconds\n" );
						Sleep( 120000 );

						batchTimeout = parseBatch( batchFile, baseDir );
						batchRunning = (batchTimeout > 0);
						if ( !batchRunning )
							printf( "finished batch %s\n", batchFile );
					}
				}
			}

		}


		printf( "Shutting down\n" );
		for ( iC = connections.begin(); iC != connections.end(); iC++ ) {
			closesocket( *iC );
		}
		connections.clear();

	} else { // launcher
		FILE *configF = NULL;
		char configPath[64];
		
		sprintf_s( configPath, sizeof(configPath), "%ws", argv[1] );

		// parse config file
		iResult = fopen_s( &configF, configPath, "r" );
		if ( iResult ) {
			printf( "Error opening config file %s for reading\n", configPath );
			WCHAR cwd[1024];
			GetCurrentDirectory( 1024, cwd );
			printf( "cwd: %ws, configPath len: %d", cwd, strlen(configPath) );
			goto retError;
		}

		char c, *wr;
		char addressBuf[64];
		char portBuf[64];
		char cmdBuf[MAX_PATH];
		while ( 1 ) {
			// scan address
			wr = addressBuf;
			while ( (c = fgetc( configF )) != ':' && c != EOF && c != '\n' ) {
				*wr = c;
				wr++;
			}
			if ( c != ':' )
				break; // EOF or bad format
			*wr = 0;

			// scan port
			wr = portBuf;
			while ( (c = fgetc( configF )) != ' ' && c != EOF && c != '\n' ) {
				*wr = c;
				wr++;
			}
			if ( c != ' ' )
				break; // bad format
			*wr = 0;

			// scan cmd
			wr = cmdBuf;
			while ( (c = fgetc( configF )) != EOF && c != '\n' ) {
				*wr = c;
				wr++;
			}
			*wr = 0;

			// connect
			printf( "connecting to %s:%s\n", addressBuf, portBuf );
			ZeroMemory( &addr_hints, sizeof(addr_hints) );
			addr_hints.ai_family = AF_INET; //AF_UNSPEC; we don't want AF_INET6
			addr_hints.ai_socktype = SOCK_STREAM;
			addr_hints.ai_protocol = IPPROTO_TCP;
			addr_hints.ai_flags = NULL;
			
			// Resolve the server address and port
			iResult = getaddrinfo( addressBuf, portBuf, &addr_hints, &addr_result);
			if ( iResult != 0 ) {
				printf( "getaddrinfo failed: %d\n", iResult);
				goto retError;
			}

			// create socket
			SOCKET sock = socket( addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
			if ( sock == INVALID_SOCKET ) {
				printf( "Error at socket(): %ld\n", WSAGetLastError());
				freeaddrinfo( addr_result );
				goto retError;
			}

			// connect
			iResult = connect( sock, addr_result->ai_addr, (int)addr_result->ai_addrlen );
			if ( iResult == SOCKET_ERROR ) {
				iResult = WSAGetLastError();
				if ( iResult != WSAEWOULDBLOCK ) {
					printf( "Error at connect(): %ld\n", iResult);
					closesocket( sock );
					freeaddrinfo( addr_result );
					continue;
				}
			}

			freeaddrinfo( addr_result );

			// send command
			printf( "sending cmd: %s\n", cmdBuf );
			int bufLen = (int)strlen(cmdBuf);
			iResult = send( sock, cmdBuf, bufLen, NULL );
			if ( iResult == SOCKET_ERROR ) {
				int err = WSAGetLastError();
				if ( err == WSAEWOULDBLOCK ) {
					iResult = 0; // no data sent
				} else { // unhandled error
					printf( "socket error, %d", err );
				}
			} 
			if ( iResult != bufLen ) {		
				printf( "wasn't able to send entire message %d vs %d", iResult, bufLen );
			}

			// disconnect
			printf( "disconnecting\n" );
			closesocket( sock );

		}
		
		fclose( configF );
	}


	// Clean up Winsock
	WSACleanup();

retOk:
	printf( "Finished. Press ENTER to exit..." );
	//_getch();

	return 0;

retError:
	printf( "Finished with error. Press ENTER to exit..." );
	//_getch(); 

	return 1;
}

