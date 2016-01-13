
#include "stdafx.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "autonomic.h"

AgentPlayback::AgentPlayback( int playbackMode, char *playbackFile ) {
	
	// randGen
	this->randGen = new RandomGenerator();

	this->callCount = 0;

	// configure playback mode
	this->playbackMode = playbackMode;
	if ( playbackFile )
		strncpy_s( this->playbackFile, sizeof(this->playbackFile), playbackFile, sizeof(this->playbackFile) );

	if ( this->playbackMode == PLAYBACKMODE_RECORD ) { // open file for recording

		if ( fopen_s( &this->playbackFP, this->playbackFile, "wb" ) ) {
			this->playbackMode = PLAYBACKMODE_OFF;
			assert(0); // APB Couldn't open file for writing
			return;
		}

	} else if ( this->playbackMode == PLAYBACKMODE_PLAYBACK ) { // open file for playback
		
		if ( fopen_s( &this->playbackFP, this->playbackFile, "rb" ) ) {
			this->playbackMode = PLAYBACKMODE_OFF;
			assert(0); // APB Couldn't open file for reading
			return;
		}
		
	}

}

AgentPlayback::~AgentPlayback() {
	if ( this->playbackMode == PLAYBACKMODE_RECORD ) { // close file
		fclose( this->playbackFP );
	} else if ( this->playbackMode == PLAYBACKMODE_PLAYBACK ) { // close file
		fclose( this->playbackFP );
	}

	// randGen
	delete this->randGen;
}


// Custom read/writes

int AgentPlayback::apbWrite( void *data, int bytes ) {
	if ( this->playbackMode != PLAYBACKMODE_RECORD )
		return 0;

	fputc( APB_CUSTOM, this->playbackFP );
	fwrite( data, 1, bytes, this->playbackFP );
	fputc( APB_CUSTOM, this->playbackFP );

	return bytes;
}

int AgentPlayback::apbRead( void *data, int bytes ) {
	char c;
	if ( this->playbackMode != PLAYBACKMODE_PLAYBACK ) 
		return 0;

	int readbytes;

	c = fgetc( this->playbackFP ); assert( APB_CUSTOM == c ); // Expected CUSTOM Start
	readbytes = (int)fread( data, 1, bytes, this->playbackFP );
	c = fgetc( this->playbackFP ); assert( APB_CUSTOM == c ); // Expected CUSTOM End
	this->recordCall( c );

	return readbytes;
}


// Special functions

int AgentPlayback::apbString( char *buf, size_t bufSize ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			size_t strl = strlen( buf );

			if ( strl >= 1024 )
				throw "AgentPlayback::apbString(RECORD): string length exceeds max buffer size";

			fputc( APB_STRING, this->playbackFP );
			fwrite( &strl, sizeof(size_t), 1, this->playbackFP );
			fwrite( buf, 1, strl, this->playbackFP );
			fputc( APB_STRING, this->playbackFP );
			return 0;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			char lbuf[1024];
			size_t strl;
			size_t readbytes;

			c = fgetc( this->playbackFP ); assert( APB_STRING == c ); // Expected STRING Start
			fread( &strl, sizeof(size_t), 1, this->playbackFP );
			if ( strl >= 1024 )
				throw "AgentPlayback::apbString(PLAYBACK): string length exceeds max buffer size";
			readbytes = fread( lbuf, 1, strl, this->playbackFP );
			if ( readbytes != strl )
				throw "AgentPlayback::apbString: string not the expected length";
			c = fgetc( this->playbackFP ); assert( APB_STRING == c ); // Expected STRING End
			this->recordCall( c );

			if ( strl >= bufSize )
				throw "AgentPlayback::apbString: string longer than external buffer";

			lbuf[strl] = '\0'; // terminate string

			strncpy_s( buf, bufSize, lbuf, bufSize ); // copy to external buf
		}
	default:
		return 0; // do nothing
	}
}

int AgentPlayback::apbHostStopFlag( int flag ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			fputc( APB_HOSTSTOPFLAG, this->playbackFP );
			fwrite( &flag, 1, sizeof(int), this->playbackFP );
			fputc( APB_HOSTSTOPFLAG, this->playbackFP );
			fflush( this->playbackFP );
			return flag;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			c = fgetc( this->playbackFP ); assert( APB_HOSTSTOPFLAG == c ); // Expected HostStopFlag Start
			fread( &flag, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_HOSTSTOPFLAG == c ); // Expected HostStopFlag End
			this->recordCall( c );
			return flag;
		}
	default:
		return flag;
	}
}

int AgentPlayback::apbHostSpawnAgent( SpawnFuncPtr sfp, spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = sfp( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
			fputc( APB_HOSTSPAWNAGENT, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_HOSTSPAWNAGENT, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_HOSTSPAWNAGENT == c ); // Expected HOSTSPAWNAGENT Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_HOSTSPAWNAGENT == c ); // Expected HOSTSPAWNAGENT End
			this->recordCall( c );
			return ret;
		}
	default:
		return sfp( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	}
}

// Playback function switches

RPC_STATUS AgentPlayback::apbUuidCreate( UUID *uuid ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			RPC_STATUS Status;
			Status = UuidCreate( uuid );
			fputc( APB_UUID, this->playbackFP );
			fwrite( &Status, 1, sizeof(RPC_STATUS), this->playbackFP );
			fwrite( uuid, 1, sizeof(UUID), this->playbackFP );
			fputc( APB_UUID, this->playbackFP );
			fflush( this->playbackFP );
			return Status;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			RPC_STATUS Status;
			c = fgetc( this->playbackFP ); assert( APB_UUID == c ); // Expected Uuid Start
			fread( &Status, 1, sizeof(RPC_STATUS), this->playbackFP );
			fread( uuid, 1, sizeof(UUID), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_UUID == c ); // Expected Uuid End
			this->recordCall( c );
			return Status;
		}
	default:
		return UuidCreate( uuid );
	}
}

errno_t AgentPlayback::apb_ftime_s( _timeb *_Time ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			errno_t err;
			err = _ftime_s( _Time );
			fputc( APB_FTIME, this->playbackFP );
			fwrite( &err, 1, sizeof(errno_t), this->playbackFP );
			fwrite( _Time, 1, sizeof(_timeb), this->playbackFP );
			fputc( APB_FTIME, this->playbackFP );
			fflush( this->playbackFP );
			return err;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			errno_t err;
			c = fgetc( this->playbackFP ); assert( APB_FTIME == c ); // Expected FTIME Start
			fread( &err, 1, sizeof(errno_t), this->playbackFP );
			fread( _Time, 1, sizeof(_timeb), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_FTIME == c ); // Expected FTIME End
			this->recordCall( c );
			return err;
		}
	default:
		return _ftime_s( _Time );
	}
}

time_t AgentPlayback::apbtime( time_t *_Time ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			time_t ret;
			ret = time( _Time );
			fputc( APB_TIME, this->playbackFP );
			fwrite( &ret, 1, sizeof(time_t), this->playbackFP );
			fwrite( _Time, 1, sizeof(time_t), this->playbackFP );
			fputc( APB_TIME, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			time_t ret;
			c = fgetc( this->playbackFP ); assert( APB_TIME == c ); // Expected TIME Start
			fread( &ret, 1, sizeof(time_t), this->playbackFP );
			fread( _Time, 1, sizeof(time_t), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_TIME == c ); // Expected TIME End
			this->recordCall( c );
			return ret;
		}
	default:
		return time( _Time );
	}
}

double AgentPlayback::apbNormalDistribution( double mean, double sigma ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			double ret;
			ret = this->randGen->NormalDistribution( mean, sigma );
			fputc( APB_RANDNORMAL, this->playbackFP );
			fwrite( &ret, 1, sizeof(double), this->playbackFP );
			fputc( APB_RANDNORMAL, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			double ret;
			c = fgetc( this->playbackFP ); assert( APB_RANDNORMAL == c ); // Expected RANDNORMAL Start
			fread( &ret, 1, sizeof(double), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_RANDNORMAL == c ); // Expected RANDNORMAL End
			this->recordCall( c );
			return ret;
		}
	default:
		return this->randGen->NormalDistribution( mean, sigma );
	}
}

double AgentPlayback::apbUniform01() {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			double ret;
			ret = this->randGen->Uniform01();
			fputc( APB_RANDUNIFORM, this->playbackFP );
			fwrite( &ret, 1, sizeof(double), this->playbackFP );
			fputc( APB_RANDUNIFORM, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			double ret;
			c = fgetc( this->playbackFP ); assert( APB_RANDUNIFORM == c ); // Expected RANDUNIFORM Start
			fread( &ret, 1, sizeof(double), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_RANDUNIFORM == c ); // Expected RANDUNIFORM End
			this->recordCall( c );
			return ret;
		}
	default:
		return this->randGen->Uniform01();
	}
}

void AgentPlayback::apbSleep( DWORD dwMilliseconds ) {
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		Sleep( dwMilliseconds );
		break;
	case PLAYBACKMODE_PLAYBACK:
		// nothing
		break;
	default:
		Sleep( dwMilliseconds );
	}

	return;
}

int AgentPlayback::apbselect( int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const timeval *timeout ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = select( nfds, readfds, writefds, exceptfds, timeout );
			fputc( APB_SELECT, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fwrite( &readfds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			fwrite( readfds->fd_array, 1, sizeof(SOCKET)*readfds->fd_count, this->playbackFP );
			fwrite( &writefds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			fwrite( writefds->fd_array, 1, sizeof(SOCKET)*writefds->fd_count, this->playbackFP );
			fwrite( &exceptfds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			fwrite( exceptfds->fd_array, 1, sizeof(SOCKET)*exceptfds->fd_count, this->playbackFP );
			fputc( APB_SELECT, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_SELECT == c ); // Expected SELECT Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			fread( &readfds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			memset( readfds->fd_array, 0, sizeof(readfds->fd_array) );
			fread( readfds->fd_array, 1, sizeof(SOCKET)*readfds->fd_count, this->playbackFP );
			fread( &writefds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			memset( writefds->fd_array, 0, sizeof(readfds->fd_array) );
			fread( writefds->fd_array, 1, sizeof(SOCKET)*writefds->fd_count, this->playbackFP );
			fread( &exceptfds->fd_count, 1, sizeof(unsigned int), this->playbackFP );
			memset( exceptfds->fd_array, 0, sizeof(readfds->fd_array) );
			fread( exceptfds->fd_array, 1, sizeof(SOCKET)*exceptfds->fd_count, this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_SELECT == c ); // Expected SELECT End
			this->recordCall( c );
			return ret;
		}
	default:
		return select( nfds, readfds, writefds, exceptfds, timeout );
	}
}

SOCKET AgentPlayback::apbaccept( SOCKET s, sockaddr *addr, int *addrlen ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			SOCKET ret;
			ret = accept( s, addr, addrlen );
			fputc( APB_ACCEPT, this->playbackFP );
			fwrite( &ret, 1, sizeof(SOCKET), this->playbackFP );
			assert( addr == NULL && addrlen == NULL ); // AgentPlayback::apbaccept: non-NULL addr and addrlen are not supported
			fputc( APB_ACCEPT, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			SOCKET ret;
			c = fgetc( this->playbackFP ); assert( APB_ACCEPT == c ); // Expected ACCEPT Start
			fread( &ret, 1, sizeof(SOCKET), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_ACCEPT == c ); // Expected ACCEPT End
			this->recordCall( c );
			return ret;
		}
	default:
		return accept( s, addr, addrlen );
	}
}

int AgentPlayback::apbconnect( SOCKET s, const sockaddr *name, int namelen ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = connect( s, name, namelen );
			fputc( APB_CONNECT, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_CONNECT, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_CONNECT == c ); // Expected CONNECT Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_CONNECT == c ); // Expected CONNECT End
			this->recordCall( c );
			return ret;
		}
	default:
		return connect( s, name, namelen );
	}
}

int AgentPlayback::apbshutdown( SOCKET s, int how ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = shutdown( s, how );
			fputc( APB_SHUTDOWN, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_SHUTDOWN, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_SHUTDOWN == c ); // Expected SHUTDOWN Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_SHUTDOWN == c ); // Expected SHUTDOWN End
			this->recordCall( c );
			return ret;
		}
	default:
		return shutdown( s, how );
	}
}

int AgentPlayback::apbclosesocket( SOCKET s ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = closesocket( s );
			fputc( APB_CLOSESOCKET, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_CLOSESOCKET, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_CLOSESOCKET == c ); // Expected CLOSESOCKET Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_CLOSESOCKET == c ); // Expected CLOSESOCKET End
			this->recordCall( c );
			return ret;
		}
	default:
		return closesocket( s );
	}
}

int AgentPlayback::apbgetaddrinfo( const char *nodename, const char *servname, const addrinfo *hints, addrinfo **res ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = getaddrinfo( nodename, servname, hints, res );
			fputc( APB_GETADDRINFO, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			if ( ret == 0 ) { // success
				// NOTE we assume you always use the first address even there are potentially more than one returned
				addrinfo ai = **res;
				ai.ai_canonname = NULL;
				ai.ai_addr = NULL;
				ai.ai_next = NULL;
				fwrite( &ai, 1, sizeof(addrinfo), this->playbackFP );
			}
			fputc( APB_GETADDRINFO, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_GETADDRINFO == c ); // Expected GETADDRINFO Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			if ( ret == 0 ) { // success
				// malloc an addrinfo
				*res = (addrinfo *)malloc(sizeof(addrinfo));
				fread( *res, 1, sizeof(addrinfo), this->playbackFP );
			}
			c = fgetc( this->playbackFP ); assert( APB_GETADDRINFO == c ); // Expected GETADDRINFO End
			this->recordCall( c );
			return ret;
		}
	default:
		return getaddrinfo( nodename, servname, hints, res );
	}
}

void AgentPlayback::apbfreeaddrinfo( LPADDRINFO pAddrInfo ) {
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		freeaddrinfo( pAddrInfo );
		break;
	case PLAYBACKMODE_PLAYBACK:
		free( pAddrInfo );
		break;
	default:
		freeaddrinfo( pAddrInfo );
	}

	return;
}

SOCKET AgentPlayback::apbsocket( int af, int type, int protocol ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			SOCKET ret;
			ret = socket( af, type, protocol );
			fputc( APB_SOCKET, this->playbackFP );
			fwrite( &ret, 1, sizeof(SOCKET), this->playbackFP );
			fputc( APB_SOCKET, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			SOCKET ret;
			c = fgetc( this->playbackFP ); assert( APB_SOCKET == c ); // Expected SOCKET Start
			fread( &ret, 1, sizeof(SOCKET), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_SOCKET == c ); // Expected SOCKET End
			this->recordCall( c );
			return ret;
		}
	default:
		return socket( af, type, protocol );
	}
}

int AgentPlayback::apbbind( SOCKET s, const sockaddr *name, int namelen ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = bind( s, name, namelen );
			fputc( APB_BIND, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_BIND, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_BIND == c ); // Expected BIND Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_BIND == c ); // Expected BIND End
			this->recordCall( c );
			return ret;
		}
	default:
		return bind( s, name, namelen );
	}
}

int AgentPlayback::apblisten( SOCKET s, int backlog ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = listen( s, backlog );
			fputc( APB_LISTEN, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_LISTEN, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_LISTEN == c ); // Expected LISTEN Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_LISTEN == c ); // Expected LISTEN End
			this->recordCall( c );
			return ret;
		}
	default:
		return listen( s, backlog );
	}
}

int AgentPlayback::apbioctlsocket( SOCKET s, long cmd, u_long *argp ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = ioctlsocket( s, cmd, argp );
			fputc( APB_IOCTLSOCKET, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fwrite( argp, 1, sizeof(u_long), this->playbackFP );
			fputc( APB_IOCTLSOCKET, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_IOCTLSOCKET == c ); // Expected IOCTLSOCKET Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			fread( argp, 1, sizeof(u_long), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_IOCTLSOCKET == c ); // Expected IOCTLSOCKET End
			this->recordCall( c );
			return ret;
		}
	default:
		return ioctlsocket( s, cmd, argp );
	}
}

int AgentPlayback::apbgetsockopt( SOCKET s, int level, int optname, char *optval, int *optlen ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = getsockopt( s, level, optname, optval, optlen );
			fputc( APB_GETSOCKOPT, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fwrite( optlen, 1, sizeof(int), this->playbackFP );
			fwrite( optval, 1, sizeof(char)**optlen, this->playbackFP );
			fputc( APB_GETSOCKOPT, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			int optlenStored;
			c = fgetc( this->playbackFP ); assert( APB_GETSOCKOPT == c ); // Expected GETSOCKOPT Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			fread( &optlenStored, 1, sizeof(int), this->playbackFP );
			assert( *optlen >= optlenStored ); // make sure the buffer is big enough, should always be the case anyway
			fread( optval, 1, sizeof(char)*optlenStored, this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_GETSOCKOPT == c ); // Expected GETSOCKOPT End
			this->recordCall( c );
			return ret;
		}
	default:
		return getsockopt( s, level, optname, optval, optlen );
	}
}

int AgentPlayback::apbrecv( SOCKET s, char *buf, int len, int flags ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = recv( s, buf, len, flags );
			fputc( APB_RECV, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			if ( ret != SOCKET_ERROR )
				fwrite( buf, 1, ret, this->playbackFP );
			fputc( APB_RECV, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_RECV == c ); // Expected RECV Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			if ( ret != SOCKET_ERROR )
				fread( buf, 1, ret, this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_RECV == c ); // Expected RECV End
			this->recordCall( c );
			return ret;
		}
	default:
		return recv( s, buf, len, flags );
	}
}

int AgentPlayback::apbsend( SOCKET s, const char *buf, int len, int flags ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = send( s, buf, len, flags );
			fputc( APB_SEND, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_SEND, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_SEND == c ); // Expected SEND Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_SEND == c ); // Expected SEND End
			this->recordCall( c );
			return ret;
		}
	default:
		return send( s, buf, len, flags );
	}
}

int AgentPlayback::apbWSAGetLastError() {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			int ret;
			ret = WSAGetLastError();
			fputc( APB_WSAGETLASTERROR, this->playbackFP );
			fwrite( &ret, 1, sizeof(int), this->playbackFP );
			fputc( APB_WSAGETLASTERROR, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			int ret;
			c = fgetc( this->playbackFP ); assert( APB_WSAGETLASTERROR == c ); // Expected WSAGETLASTERROR Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_WSAGETLASTERROR == c ); // Expected WSAGETLASTERROR End
			this->recordCall( c );
			return ret;
		}
	default:
		return WSAGetLastError();
	}
}

BOOL AgentPlayback::apbGetThreadTimes( HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime ) {
	char c;
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			BOOL ret;
			ret = GetThreadTimes( hThread, lpCreationTime, lpExitTime, lpKernelTime, lpUserTime );
			fputc( APB_GETTHREADTIMES, this->playbackFP );
			fwrite( &ret, 1, sizeof(BOOL), this->playbackFP );
			fwrite( lpCreationTime, 1, sizeof(FILETIME), this->playbackFP );
			fwrite( lpExitTime, 1, sizeof(FILETIME), this->playbackFP );
			fwrite( lpKernelTime, 1, sizeof(FILETIME), this->playbackFP );
			fwrite( lpUserTime, 1, sizeof(FILETIME), this->playbackFP );
			fputc( APB_GETTHREADTIMES, this->playbackFP );
			fflush( this->playbackFP );
			return ret;
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			BOOL ret;
			c = fgetc( this->playbackFP ); assert( APB_GETTHREADTIMES == c ); // Expected GETTHREADTIMES Start
			fread( &ret, 1, sizeof(int), this->playbackFP );
			fread( lpCreationTime, 1, sizeof(FILETIME), this->playbackFP );
			fread( lpExitTime, 1, sizeof(FILETIME), this->playbackFP );
			fread( lpKernelTime, 1, sizeof(FILETIME), this->playbackFP );
			fread( lpUserTime, 1, sizeof(FILETIME), this->playbackFP );
			c = fgetc( this->playbackFP ); assert( APB_GETTHREADTIMES == c ); // Expected GETTHREADTIMES End
			this->recordCall( c );
			return ret;
		}
	default:
		return GetThreadTimes( hThread, lpCreationTime, lpExitTime, lpKernelTime, lpUserTime );
	}
}

HANDLE AgentPlayback::apbGetCurrentThread() {
	switch ( this->playbackMode ) {
	case PLAYBACKMODE_RECORD:
		{
			// nothing to record
			return GetCurrentThread();
		}
	case PLAYBACKMODE_PLAYBACK:
		{
			return NULL;
		}
	default:
		return GetCurrentThread();
	}
}

void AgentPlayback::recordCall( char c ) {

	// DEBUG
	if ( this->callCount == 21659 )
		int i = 0;

	this->lastEntry = c;
	this->callCount++;


}