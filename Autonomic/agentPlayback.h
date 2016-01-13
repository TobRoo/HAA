

#pragma once

#include <sys/timeb.h>
#include <rpc.h>

#include "time.h"

#include "RandomGenerator.h"

enum PLAYBACKMODE {
	PLAYBACKMODE_OFF = 0,
	PLAYBACKMODE_RECORD,
	PLAYBACKMODE_PLAYBACK
};

enum AGENTPLAYBACK_TYPES { // data type flags to help error check, all data is bracketed in type flags
	APB_CUSTOM = 0,
	APB_STRING,
	APB_HOSTSTOPFLAG,
	APB_HOSTSPAWNAGENT,
	APB_UUID,
	APB_FTIME,
	APB_TIME,
	APB_RANDNORMAL,
	APB_RANDUNIFORM,
	APB_SELECT,
	APB_ACCEPT,
	APB_CONNECT,
	APB_SHUTDOWN,
	APB_CLOSESOCKET,
	APB_GETADDRINFO,
	APB_SOCKET,
	APB_BIND,
	APB_LISTEN,
	APB_IOCTLSOCKET,
	APB_GETSOCKOPT,
	APB_RECV,
	APB_SEND,
	APB_WSAGETLASTERROR,
	APB_GETTHREADTIMES,
	APB_GETCURRENTTHREAD,
};

class AgentPlayback {
public:
	AgentPlayback( int playbackMode, char *playbackFile );
	~AgentPlayback();

	int			getPlaybackMode() { return this->playbackMode; };

	// Custom read/writes
	int			apbWrite( void *data, int bytes );
	int			apbRead( void *data, int bytes );

	// Special functions
	int			apbString( char *buf, size_t bufSize ); // read/write a string
	int			apbHostStopFlag( int flag ); // intercept stop flag because Host stop flags can be set externally
	int			apbHostSpawnAgent( SpawnFuncPtr sfp, spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

	// Playback function switches
	RPC_STATUS	apbUuidCreate( UUID *uuid );
	errno_t		apb_ftime_s( _timeb *_Time );
	time_t		apbtime( time_t *_Time );

	double		apbNormalDistribution( double mean, double sigma );
	double		apbUniform01();

	void		apbSleep( DWORD dwMilliseconds );

	int			apbselect( int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const timeval *timeout );
	SOCKET		apbaccept( SOCKET s, sockaddr *addr, int *addrlen );
	int			apbconnect( SOCKET s, const sockaddr *name, int namelen );
	int			apbshutdown( SOCKET s, int how );
	int			apbclosesocket( SOCKET s );
	int			apbgetaddrinfo( const char *nodename, const char *servname, const addrinfo *hints, addrinfo **res );
	void		apbfreeaddrinfo( LPADDRINFO pAddrInfo );
	SOCKET		apbsocket( int af, int type, int protocol );
	int			apbbind( SOCKET s, const sockaddr *name, int namelen );
	int			apblisten( SOCKET s, int backlog );
	int			apbioctlsocket( SOCKET s, long cmd, u_long *argp );
	int			apbgetsockopt( SOCKET s, int level, int optname, char *optval, int *optlen );
	int			apbrecv( SOCKET s, char *buf, int len, int flags );
	int			apbsend( SOCKET s, const char *buf, int len, int flags );
	int			apbWSAGetLastError();

	BOOL		apbGetThreadTimes( HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime );
	HANDLE		apbGetCurrentThread();
	
protected:
	char playbackMode;
	char playbackFile[512]; // current playback file name
	FILE *playbackFP; // current playback file pointer

	RandomGenerator *randGen; // random generator

	char lastEntry; // used for debugging
	unsigned int callCount; 
	void recordCall( char c );

};