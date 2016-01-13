
#include "stdafx.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "autonomic.h"
//#include "Logger.h"

#include "time.h"
#include "stdarg.h"
#include "errno.h"

Logger::Logger() {

	this->logMode = LOG_MODE_OFF;
	this->logLevel = LOG_LEVEL_NORMAL;
	this->timeStamp = true;

	this->file = NULL;

	this->uuidBufInd = 0;

}

Logger::~Logger() {
	// clean up
	if ( this->logMode & LOG_MODE_FILE ) {
		fprintf( this->file, "Logger::~Logger: shutdown..." );
		fclose( this->file );
		this->file = NULL;
	}
}

int Logger::setLogMode( int mode, void *ptr ) {
	char timeBuf[64];
	char formatBuf[64];

	if ( this->timeStamp ) {
		time_t t_t;
		_timeb tb;
		struct tm stm;
		apb->apbtime( &t_t );
		apb->apb_ftime_s( &tb );
		localtime_s( &stm, &t_t );
		strftime( formatBuf, 64, "%H:%M:%S", &stm );
		sprintf_s( timeBuf, 64, "[%s.%3d] ", formatBuf, tb.millitm );
	}

	// clean up old mode
	if ( (this->logMode & LOG_MODE_FILE) && (mode & LOG_MODE_FILE) ) {
		if ( this->timeStamp ) fprintf( this->file, timeBuf );
		fprintf( this->file, "Changing Log File..." );
		fclose( this->file );
		this->file = NULL;
	}

	// set up new mode
	if ( mode == LOG_MODE_FILE ) {
		int ret;
		// check if file exists
		ret = fopen_s( &this->file, (char *)ptr, "r" );
		if ( ret != ENOENT ) { // file exists
			int i;
			int ext;
			char buf[256];

			ext = (int)strlen((char *)ptr)-1;
			while ( ((char *)ptr)[ext] != '.' && ext > 0 ) ext--;
			i = 1;
			do {
				if ( this->file )
					fclose( this->file );
				strncpy_s( buf, 256, (char *)ptr, ext );
				sprintf_s( buf + ext, 256 - ext, "[%d]%s", i, ((char *)ptr) + ext );
				ret = fopen_s( &this->file, buf, "r" );
				i++;
			} while ( ret != ENOENT && i < 100 );

			ret = fopen_s( &this->file, buf, "w" );
			strcpy_s( this->filename, MAX_PATH, buf );
		} else {
			ret = fopen_s( &this->file, (char *)ptr, "w" );
			strcpy_s( this->filename, MAX_PATH, (char *)ptr );
		}
		if ( ret ) {
			this->logMode &= ~LOG_MODE_FILE;
			return 1;
		}
	}
	
	this->logMode |= mode;

	return 0;
}

void Logger::unsetLogMode( int mode ) {
	char timeBuf[64];
	char formatBuf[64];

	if ( this->timeStamp ) {
		time_t t_t;
		_timeb tb;
		struct tm stm;
		apb->apbtime( &t_t );
		apb->apb_ftime_s( &tb );
		localtime_s( &stm, &t_t );
		strftime( formatBuf, 64, "%H:%M:%S", &stm );
		sprintf_s( timeBuf, 64, "[%s.%3d] ", formatBuf, tb.millitm );
	}

	if ( mode & LOG_MODE_FILE ) {
		if ( this->timeStamp ) fprintf( this->file, timeBuf );
		fprintf( this->file, "Closing Log File..." );
		fclose( this->file );
		this->file = NULL;
	}

	this->logMode &= ~mode;
}

void Logger::setLogLevel( int level ) {
	this->log( this->logLevel, "Setting log level: %d", level );
	this->logLevel = level;
}

void Logger::setTimeStamp( bool state ) {
	this->timeStamp = state;
}

void Logger::flush() {
	if ( this->logMode & LOG_MODE_FILE ) {
		fflush( this->file );
	}
}

int Logger::log( int level, char *msg, ... ) {
	uuidBufInd = 0;

	if ( this->logMode == LOG_MODE_OFF ) 
		return 0;
	if ( this->logLevel < level ) 
		return 0;

    va_start( valist, msg );

	char formatBuf[64];

	if ( this->timeStamp ) {
		time_t t_t;
		_timeb tb;
		struct tm stm;
		apb->apbtime( &t_t );
		apb->apb_ftime_s( &tb );
		localtime_s( &stm, &t_t );
		strftime( formatBuf, 64, "%H:%M:%S", &stm );
		sprintf_s( timeBuf, 64, "[%s.%3d] ", formatBuf, tb.millitm );
	}

	if ( this->logMode & LOG_MODE_COUT ) {
		if ( this->timeStamp ) printf( timeBuf );
		vprintf_s( msg, valist );
		printf( "\n" );
	}
	if ( this->logMode & LOG_MODE_FILE ) {
		if ( this->timeStamp ) fprintf( this->file, timeBuf );
		vfprintf_s( this->file, msg, valist );
		fprintf( this->file, "\n" );
		this->flush();
	}

	return 0;
}

char * Logger::formatUUID( int level, UUID *uuid ) {
	int ind = uuidBufInd++;

	if ( ind >= 60 )
		return NULL;

	if ( this->logMode == LOG_MODE_OFF ) 
		return NULL;
	if ( this->logLevel < level ) 
		return NULL;
	
	UuidToString( uuid, &rpc_wstr );
	sprintf_s( uuidBuf[ind], 40, "%ls", rpc_wstr );
	RpcStringFree( &rpc_wstr );

	return uuidBuf[ind];
}

int Logger::dataDump( int level, void *data, int size, char *name ) {

	if ( !(this->logMode & LOG_MODE_FILE) ) 
		return 0;
	if ( this->logLevel < level ) 
		return 0;



	if ( ! name )
		fprintf( this->file, "DATABLOCK_START %d\n", size );
	else
		fprintf( this->file, "DATABLOCK_START %d %s\n", size, name );

	fclose( this->file );
	fopen_s( &this->file, this->filename, "ab" );
	fwrite( data, 1, size, this->file );
	fprintf( this->file, "\nDATABLOCK_END\n" );
	fclose( this->file );
	fopen_s( &this->file, this->filename, "a" );
	
	return 0;
}