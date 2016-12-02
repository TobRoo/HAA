

#include "stdafx.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "dataStream.h"
#include "DDB.h"

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

void DataStream::packTaskData(DDBTaskData *taskData) {
	int length;		//Total size of the taskData in bytes
	int tauLength = taskData->tau.size()*(sizeof(UUID) + sizeof(float));
	int motLength = taskData->motivation.size()*(sizeof(UUID) + sizeof(float));
	int impLength = taskData->impatience.size()*(sizeof(UUID) + sizeof(float));
	int attLength = taskData->attempts.size()*(sizeof(UUID) + sizeof(int));
	int meanLength = taskData->mean.size()*(sizeof(UUID) + sizeof(float));
	int stddevLength = taskData->stddev.size()*(sizeof(UUID) + sizeof(float));

	length = sizeof(UUID) + sizeof(UUID) + tauLength + motLength + impLength + attLength + meanLength + stddevLength + sizeof(taskData->psi) + sizeof(taskData->updateTime);

	if (this->dataLength + length > this->bufSize) {
		if (this->increaseBuffer(this->dataLength + length))
			return;
	}


	//First pack the taskId
	memcpy(this->head, (char *)&taskData->taskId, sizeof(UUID));
	this->head += sizeof(UUID);
	this->dataLength += sizeof(UUID);

	//Then pack the agentId

	memcpy(this->head, (char *)&taskData->agentId, sizeof(UUID));
	this->head += sizeof(UUID);
	this->dataLength += sizeof(UUID);

	size_t mapSize = taskData->tau.size();

	//Store the size of the maps (should be equal size for all maps) for later unpacking 
	//Then, go through the maps, packing values

	memcpy(this->head, (char *)&mapSize, sizeof(size_t));	//should be the same as the length of the other maps
	this->head += sizeof(size_t);
	this->dataLength += sizeof(size_t);

	std::map<UUID, float, UUIDless>::iterator floatIter;
	std::map<UUID, int, UUIDless>::iterator intIter;
	if (mapSize > 0){	//Throws exception otherwise
		for (floatIter = taskData->tau.begin(); floatIter != taskData->tau.end(); floatIter++) {
			memcpy(this->head, (char *)&floatIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&floatIter->second, sizeof(float));
			this->head += sizeof(float);
			this->dataLength += sizeof(float);
		}

		for (floatIter = taskData->motivation.begin(); floatIter != taskData->motivation.end(); floatIter++) {
			memcpy(this->head, (char *)&floatIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&floatIter->second, sizeof(float));
			this->head += sizeof(float);
			this->dataLength += sizeof(float);
		}
		for (floatIter = taskData->impatience.begin(); floatIter != taskData->impatience.end(); floatIter++) {
			memcpy(this->head, (char *)&floatIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&floatIter->second, sizeof(float));
			this->head += sizeof(float);
			this->dataLength += sizeof(float);
		}
		for (intIter = taskData->attempts.begin(); intIter != taskData->attempts.end(); intIter++) {
			memcpy(this->head, (char *)&intIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&intIter->second, sizeof(int));
			this->head += sizeof(int);
			this->dataLength += sizeof(int);
		}
		for (floatIter = taskData->mean.begin(); floatIter != taskData->mean.end(); floatIter++) {
			memcpy(this->head, (char *)&floatIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&floatIter->second, sizeof(float));
			this->head += sizeof(float);
			this->dataLength += sizeof(float);
		}
		for (floatIter = taskData->stddev.begin(); floatIter != taskData->stddev.end(); floatIter++) {
			memcpy(this->head, (char *)&floatIter->first, sizeof(UUID));
			this->head += sizeof(UUID);
			this->dataLength += sizeof(UUID);

			memcpy(this->head, (char *)&floatIter->second, sizeof(float));
			this->head += sizeof(float);
			this->dataLength += sizeof(float);
		}
		
	}
	//Pack psi
	memcpy(this->head, (char *)&taskData->psi, sizeof(unsigned int));
	this->head += sizeof(unsigned int);
	this->dataLength += sizeof(unsigned int);

	//Pack updateTime
	memcpy(this->head, (char *)&taskData->updateTime, sizeof(_timeb));
	this->head += sizeof(_timeb);
	this->dataLength += sizeof(_timeb);

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
void DataStream::unpackTaskData(DDBTaskData *taskData) {

	//Unpack taskId
	memcpy((char *)&taskData->taskId, this->head, sizeof(UUID));
	this->head += sizeof(UUID);

	//Unpack agentId
	memcpy((char *)&taskData->agentId, this->head, sizeof(UUID));
	this->head += sizeof(UUID);

	//Get the size of the maps
	size_t mapSize;
	memcpy((char *)&mapSize, this->head, sizeof(size_t));
	this->head += sizeof(size_t);

	UUID key;
	float floatValue;
	int intValue;
	if (mapSize > 0) {
		//Unpack and populate tau
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&floatValue, this->head, sizeof(float));
			this->head += sizeof(float);
			taskData->tau[key] = floatValue;
		}
		//Unpack and populate motivation
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&floatValue, this->head, sizeof(float));
			this->head += sizeof(float);
			taskData->motivation[key] = floatValue;
		}

		//Unpack and populate impatience
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&floatValue, this->head, sizeof(float));
			this->head += sizeof(float);
			taskData->impatience[key] = floatValue;
		}

		//Unpack and populate attempts
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&intValue, this->head, sizeof(int));
			this->head += sizeof(int);
			taskData->attempts[key] = intValue;
		}

		//Unpack and populate mean
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&floatValue, this->head, sizeof(float));
			this->head += sizeof(float);
			taskData->mean[key] = floatValue;
		}

		//Unpack and populate stddev
		for (int i = 0; i < mapSize; i++) {
			memcpy((char *)&key, this->head, sizeof(UUID));
			this->head += sizeof(UUID);
			memcpy((char *)&floatValue, this->head, sizeof(float));
			this->head += sizeof(float);
			taskData->stddev[key] = floatValue;
		}
	}

	//Unpack psi
	memcpy((char *)&taskData->psi, this->head, sizeof(int));
	this->head += sizeof(int);

	//Unpack updateTime
	memcpy((char *)&taskData->updateTime, this->head, sizeof(_timeb));
	this->head += sizeof(_timeb);
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