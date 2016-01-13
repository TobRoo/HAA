

#include "stdafx.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "dataStream.h"

DataStream::DataStream() {

	this->locked = 0;
	this->lockedKey = 1;
	this->readMode = DSM_WRITE;

	this->buf = (char *)malloc(sizeof(char)*DS_DEFAULT_BUFFER);
	if ( !this->buf ) {
		throw "DataStream::DataStream: Memory allocation failure!";
	}
	this->bufSize = DS_DEFAULT_BUFFER;

	this->boolBuf = (char *)malloc(sizeof(char)*DS_DEFAULT_BOOLBUFFER);
	if ( !this->boolBuf ) {
		throw "DataStream::DataStream: Memory allocation failure!";
	}
	this->boolBufSize = DS_DEFAULT_BOOLBUFFER;

	this->head = this->buf;

	this->dataLength = 0;
	this->boolLength = 0;
}

DataStream::~DataStream() {
	free( this->buf );
	free( this->boolBuf );
}

char * DataStream::stream() {
	int i;
	int boolChars = (this->boolLength + 7)/8;

	if ( this->readMode != DSM_WRITE ) 
		return this->buf; // already packed

	if ( this->dataLength + boolChars > this->bufSize ) {
		this->increaseBuffer( this->dataLength + boolChars );
	}

	for ( i=boolChars; i>0; i-- ) {
		this->head[boolChars-i] = this->boolBuf[i-1];
	}

	this->dataLength += boolChars;

	this->readMode = DSM_PACKED;

	return this->buf;	
}
int DataStream::length() {
	if ( this->readMode == DSM_WRITE ) {
		return this->dataLength + (this->boolLength + 7)/8;
	} else {
		return this->dataLength;
	}
}

void DataStream::reset() {

	if ( this->locked ) {
		throw "DataStream::reset: Stream already locked!";
	}

	this->lock();

	this->readMode = DSM_WRITE;

	if ( this->bufSize != DS_DEFAULT_BUFFER ) {
		this->buf = (char *)realloc( this->buf, sizeof(char)*DS_DEFAULT_BUFFER );
		if ( !this->buf ) {
			throw "DataStream::reset: Memory reallocation failure!";
		}
		this->bufSize = DS_DEFAULT_BUFFER;
	}
	
	if ( this->boolBufSize != DS_DEFAULT_BOOLBUFFER ) {
		this->boolBuf = (char *)realloc( this->boolBuf, sizeof(char)*DS_DEFAULT_BOOLBUFFER );
		if ( !this->boolBuf ) {
			throw "DataStream::reset: Memory reallocation failure!";
		}
		this->boolBufSize = DS_DEFAULT_BOOLBUFFER;
	}
	

	this->head = this->buf;

	this->dataLength = 0;
	this->boolLength = 0;
}

void DataStream::packInt16( short val ) {
	if ( this->dataLength + (int)sizeof(short) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(short) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(short) );
	this->head += sizeof(short);
	this->dataLength += sizeof(short);
}
void DataStream::packUInt16( unsigned short val ) {
	if ( this->dataLength + (int)sizeof(unsigned short) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(unsigned short) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(unsigned short) );
	this->head += sizeof(unsigned short);
	this->dataLength += sizeof(unsigned short);
}
void DataStream::packInt32( int val ) {
	if ( this->dataLength + (int)sizeof(int) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(int) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(int) );
	this->head += sizeof(int);
	this->dataLength += sizeof(int);
}
void DataStream::packUInt32( unsigned int val ) {
	if ( this->dataLength + (int)sizeof(unsigned int) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(unsigned int) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(unsigned int) );
	this->head += sizeof(unsigned int);
	this->dataLength += sizeof(unsigned int);
}
void DataStream::packInt64( __int64 val ) {
	if ( this->dataLength + (int)sizeof(__int64) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(__int64) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(__int64) );
	this->head += sizeof(__int64);
	this->dataLength += sizeof(__int64);
}
void DataStream::packUInt64( unsigned __int64 val ) {
	if ( this->dataLength + (int)sizeof(unsigned __int64) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(unsigned __int64) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(unsigned __int64) );
	this->head += sizeof(unsigned __int64);
	this->dataLength += sizeof(unsigned __int64);
}
void DataStream::packFloat32( float val ) {
	if ( this->dataLength + (int)sizeof(float) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(float) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(float) );
	this->head += sizeof(float);
	this->dataLength += sizeof(float);
}
void DataStream::packChar( char val ) {
	if ( this->dataLength + (int)sizeof(char) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(char) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(char) );
	this->head += sizeof(char);
	this->dataLength += sizeof(char);
}
void DataStream::packUChar( unsigned char val ) {
	if ( this->dataLength + (int)sizeof(unsigned char) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(unsigned char) ) )
			return;
	}
	memcpy( this->head, (char *)&val, sizeof(unsigned char) );
	this->head += sizeof(unsigned char);
	this->dataLength += sizeof(unsigned char);
}
void DataStream::packString( char *ptr ) {
	int length = (int)strlen( ptr ) + 1;
	if ( this->dataLength + length > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + length ) )
			return;
	}
	memcpy( this->head, ptr, sizeof(char)*length );
	this->head += sizeof(char)*length;
	this->dataLength += sizeof(char)*length;
}
void DataStream::packData( void *ptr, int length ) {
	if ( this->dataLength + length > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + length ) )
			return;
	}
	memcpy( this->head, ptr, sizeof(char)*length );
	this->head += sizeof(char)*length;
	this->dataLength += sizeof(char)*length;
}
void DataStream::packBool( bool val ) {
	int offset = this->boolLength/8;
	int shift = this->boolLength%8;
	char key = 0x1;

	if ( offset >= this->boolBufSize ) {
		if ( this->increaseBoolBuffer() )
			return;
	}

	if ( val ) {
		this->boolBuf[offset] |= key << shift;
	} else {
		this->boolBuf[offset] &= ~(key << shift);
	}
	this->boolLength++;
}
void DataStream::packUUID( UUID *uuid ) {
	if ( this->dataLength + (int)sizeof(UUID) > this->bufSize ) {
		if ( this->increaseBuffer( this->dataLength + sizeof(UUID) ) )
			return;
	}
	memcpy( this->head, (char *)uuid, sizeof(UUID) );
	this->head += sizeof(UUID);
	this->dataLength += sizeof(UUID);
}


void DataStream::setData( char *ptr, int length ) {

	if ( this->locked ) {
		throw "DataStream::reset: Stream already locked!";
	}

	this->lock();

	this->readMode = DSM_READ;

	if ( length > DS_DEFAULT_BUFFER ) {
		this->buf = (char *)realloc( this->buf, sizeof(char)*length );
		if ( !this->buf ) {
			throw "DataStream::setData: Memory reallocation failure!";
		}
		this->bufSize = length;
	} else if ( this->bufSize > DS_DEFAULT_BUFFER ) {
		this->buf = (char *)realloc( this->buf, sizeof(char)*DS_DEFAULT_BUFFER );
		if ( !this->buf ) {
			throw "DataStream::reset: Memory reallocation failure!";
		}
		this->bufSize = DS_DEFAULT_BUFFER;
	}
	
	/*if ( length > this->bufSize ) {
		this->increaseBuffer( length );
	}*/

	memcpy( this->buf, ptr, length );
	
	
	if ( this->boolBufSize != DS_DEFAULT_BOOLBUFFER ) {
		this->boolBuf = (char *)realloc( this->boolBuf, sizeof(char)*DS_DEFAULT_BOOLBUFFER );
		if ( !this->boolBuf ) {
			throw "DataStream::reset: Memory reallocation failure!";
		}
		this->boolBufSize = DS_DEFAULT_BOOLBUFFER;
	}
	
	this->head = this->buf;

	this->dataLength = length;
	this->boolLength = 0;
}

void DataStream::rewind() {

	if ( this->readMode == DSM_WRITE ) { // prepare read mode
		int i;
		int boolChars = (this->boolLength + 7)/8;

		if ( this->dataLength + boolChars > this->bufSize ) {
			this->increaseBuffer( this->dataLength + boolChars );
		}

		for ( i=boolChars; i>0; i-- ) {
			this->head[boolChars-i] = this->boolBuf[i-1];
		}

		this->dataLength += boolChars;
	}

	this->readMode = DSM_READ;

	this->head = this->buf;
	this->boolLength = 0;
}

short DataStream::unpackInt16() {
	short val;
	memcpy( (char *)&val, this->head, sizeof(short) );
	this->head += sizeof(short);
	return val;
}
unsigned short DataStream::unpackUInt16() {
	unsigned short val;
	memcpy( (char *)&val, this->head, sizeof(unsigned short) );
	this->head += sizeof(unsigned short);
	return val;
}
int DataStream::unpackInt32() {
	int val;
	memcpy( (char *)&val, this->head, sizeof(int) );
	this->head += sizeof(int);
	return val;
}
unsigned int DataStream::unpackUInt32() {
	unsigned int val;
	memcpy( (char *)&val, this->head, sizeof(unsigned int) );
	this->head += sizeof(unsigned int);
	return val;
}
__int64 DataStream::unpackInt64() {
	__int64 val;
	memcpy( (char *)&val, this->head, sizeof(__int64) );
	this->head += sizeof(__int64);
	return val;
}
unsigned __int64 DataStream::unpackUInt64() {
	unsigned __int64 val;
	memcpy( (char *)&val, this->head, sizeof(unsigned __int64) );
	this->head += sizeof(unsigned __int64);
	return val;
}
float DataStream::unpackFloat32() {
	float val;
	memcpy( (char *)&val, this->head, sizeof(float) );
	this->head += sizeof(float);
	return val;
}
char DataStream::unpackChar() {
	char val;
	memcpy( (char *)&val, this->head, sizeof(char) );
	this->head += sizeof(char);
	return val;
}
unsigned char DataStream::unpackUChar() {
	unsigned char val;
	memcpy( (char *)&val, this->head, sizeof(unsigned char) );
	this->head += sizeof(unsigned char);
	return val;
}
char * DataStream::unpackString() {
	char *ptr = this->head;
	int length = (int)strlen(this->head) + 1;
	this->head += length;
	return ptr;
}
void * DataStream::unpackData( int length ) {
	void *ptr = (void *)this->head;
	this->head += length;
	return ptr;
}
bool DataStream::unpackBool() {
	int offset = this->dataLength - 1 - this->boolLength/8;
	int shift = this->boolLength%8;
	char key = 0x1;

	this->boolLength++;

	return (this->buf[offset] & (key << shift)) != 0;
}
void DataStream::unpackUUID( UUID *uuid ) {
	memcpy( (char *)uuid, this->head, sizeof(UUID) );
	this->head += sizeof(UUID);
}

int DataStream::increaseBuffer( int minSize ) {
	int newSize = this->bufSize;
	char *newBuf;
	__int64 offset = this->head - this->buf;
	while ( newSize < minSize ) {
		if ( newSize >= 1024*1024  ) {
			newSize += 1024*1024;
		} else {
			newSize *= 2;
		}
	}
	newBuf = (char *)malloc( sizeof(char)*newSize );
	if ( !newBuf ) {
		throw "DataStream::increaseBuffer: Memory reallocation failure!";
		return 1;
	}
	memcpy( newBuf, this->buf, sizeof(char)*this->bufSize );
	this->bufSize = newSize;
	this->head = newBuf + offset;
	free( this->buf );
	this->buf = newBuf;
	return 0;
}

int DataStream::increaseBoolBuffer() {
	this->boolBuf = (char *)realloc( this->boolBuf, sizeof(char)*(this->boolBufSize+1) );
	if ( !this->boolBuf ) {
		throw "DataStream::increaseBoolBuffer: Memory reallocation failure!";
		return 1;
	}
	this->boolBufSize++;
	return 0;
}

void DataStream::lock() {
	this->locked = this->lockedKey;
	
	// DEBUG
//	if ( this->locked == 371 )
//		int i = 1;

	this->lockedKey++;
	if ( !this->lockedKey ) this->lockedKey = 1; // ensure the key is never 0
}