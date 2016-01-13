
#include "stdio.h"
#include <rpc.h>

#define LOG_MODE_OFF		0
#define LOG_MODE_COUT		(1 << 0)
#define LOG_MODE_FILE		(1 << 1)
#define LOG_MODE_EDIT		(1 << 2)

#define LOG_LEVEL_NONE		-1
#define LOG_LEVEL_NORMAL	3
#define LOG_LEVEL_VERBOSE	6
#define LOG_LEVEL_ALL		9

class AgentPlayback;

class Logger {
	
public:
	Logger();
	~Logger();

	void setAgentPlayback( AgentPlayback *apb ) { this->apb = apb; };

	int setLogMode( int mode, void *ptr = NULL );
	int getLogMode() { return logMode; };

	void unsetLogMode( int mode );
	
	void setLogLevel( int level );
	int getLogLevel() { return logLevel; };
	void setTimeStamp( bool state );

	void flush();

	int log( int level, char *msg, ... );

	char * formatUUID( int level, UUID *uuid );

	int dataDump( int level, void *data, int size, char *name = NULL );

private:
	int logMode;
	int logLevel;
	bool timeStamp;

	AgentPlayback *apb;

	va_list valist;
	char timeBuf[64];

	RPC_WSTR rpc_wstr;
	int uuidBufInd;
	char uuidBuf[60][40];

	FILE *file;
	char filename[MAX_PATH];

};