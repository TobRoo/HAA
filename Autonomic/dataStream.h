
#include <rpc.h>
#include "DDB.h"
//==============================================================
// -- Acceptable Usage --
// To write: reset()
//
// To read: 
//		Option 1: setData( data, len )
//		Option 2: rewind(), after reset() or setData()
//
// NOTE: After calling stream() or rewind() no writing is
// allowed, even if you were previously in write mode
//==============================================================


#define DS_DEFAULT_BUFFER 1024 // default buffer size
#define DS_DEFAULT_BOOLBUFFER 8 // default bool buffer size

enum DataStream_Mode {
	DSM_WRITE = 0,
	DSM_READ,
	DSM_PACKED,
};

class DataStream {

public:
	DataStream();
	~DataStream();

	char * stream(); // returns a pointer to the beginning of the stream
	int length();    // returns the length of the stream

	void reset();     // resets the data stream, causes stream to lock
	
	void packInt16( short val );
	void packUInt16( unsigned short val );
	void packInt32( int val );
	void packUInt32( unsigned int val );
	void packInt64( __int64 val );
	void packUInt64( unsigned __int64 val );
	void packFloat32( float val );
	void packChar( char val );
	void packUChar( unsigned char val );
	void packString( char *ptr ); // must be null terminated
	void packData( void *ptr, int length );
	void packBool( bool val );
	void packUUID( UUID *uuid );
	void packTaskData(DDBTaskData * taskData);

	void setData( char *ptr, int length ); // sets the stream to incoming data, causes stream to lock
	
	void rewind();        // rewinds the data stream to the beginning

	short			 unpackInt16();
	unsigned short   unpackUInt16();
	int              unpackInt32();
	unsigned int     unpackUInt32();
	__int64          unpackInt64();
	unsigned __int64 unpackUInt64();
	float			 unpackFloat32();
	char             unpackChar();
	unsigned char    unpackUChar();
	char *           unpackString();
	void *			 unpackData( int length );
	bool             unpackBool();
	void			 unpackUUID( UUID *uuid );
	void unpackTaskData(DDBTaskData * taskData);

	void lock(); // locks the stream
	void unlock() { this->locked = false; }; // unlocks stream

private:

	int locked; // lock flag, protects stream from being used for two things at once!
	int lockedKey; // key to the current lock, used for debugging

	char readMode; // mode flag, note that the mode is only loosly enforced so call functions with care!

	char *buf;
	char *head;
	char *boolBuf;

	int bufSize;
	int boolBufSize;

	int dataLength;
	int boolLength;

	int increaseBuffer( int minSize );
	int increaseBoolBuffer();
};