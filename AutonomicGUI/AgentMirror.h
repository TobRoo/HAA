// AgentMirror.h : main header file for the AgentMirror
//

#pragma once

#include "..\\autonomic\\autonomic.h"

#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\DDBStore.h"

#include "ThinSocket.h"

#include <list>
#include <map>

class DDBStore;

typedef std::map<_Callback *, int>	mapDDBCBReferences;
typedef std::map<int, std::list<_Callback *> *> mapDDBWatcherCBs;
typedef std::map<UUID, std::list<_Callback *> *, UUIDless> mapDDBItemWatcherCBs;

class AgentMirror {
public:
	AgentMirror( ThinSocket *hostSocket );
	~AgentMirror();

	Logger Log;	// log class

	virtual int configure();	// initial configuration
	virtual int start();		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	int conReceiveData();

protected:

	AgentPlayback *apb; // dummy agent playback

	UUID agentUuid; // agent uuid 
	AgentType agentType; // agent type

	ThinSocket *hostSocket; // host socket
	sConnection hostCon; // host connection, used for recieving data only

	char		   haveReturn;    // is the return address valid?
	UUID		   returnAddress; // return address of the last forwarded message (if available)


	bool configured;	// configuration has been run
	bool started;		// agent has been started

	DataStream ds; // shared DataStream object, for temporary, single thread, use only!
	DataStream ds2; // sometimes you need two!

	UUID nilUUID; // comes in handy

	int conCheckBufSize();
	int conProcessStream();
	virtual int conProcessMessage( unsigned char message, char *data, unsigned int len );
	int sendMessage( unsigned char message, char *data = NULL, unsigned int len = 0 );
	int sendMessageEx( MSGEXargs, char *data = NULL, unsigned int len = 0 );
	int conSendMessage( unsigned char message, char *data, unsigned int len, unsigned int msgSize );

	// DDB
	DDBStore	*dStore; // stores all the DDB data
	mapDDBCBReferences  DDBCBReferences; // map of reference counts for each callback
	mapDDBWatcherCBs	DDBWatcherCBs; // map of watcher lists by data type
	mapDDBItemWatcherCBs DDBItemWatcherCBs; // map of watcher lists by item

public:
	int ddbAddWatcherCB( _Callback *cb, int type );
	int ddbAddWatcherCB( _Callback *cb, UUID *item );
	int ddbRemWatcherCB( _Callback *cb ); // remove agent from all items/types
	int ddbRemWatcherCB( _Callback *cb, int type );
	int ddbRemWatcherCB( _Callback *cb, UUID *item );
	
protected:
	int ddbClearWatcherCBs( UUID *id );
	int _ddbClearWatcherCBs();
	int _ddbNotifyWatcherCBs( int type, char evt, UUID *id, void *data = NULL, int len = 0 );

	int _ddbParseEnumerate( DataStream *ds ); 

	int ddbAddAgent( UUID *id, UUID *parentId, AgentType *agentType );
	int ddbRemoveAgent( UUID *id );
	
	int ddbVisAddPath( UUID *agentId, int id, int count, float *x, float *y );
	int ddbVisRemovePath( UUID *agentId, int id );
	int ddbVisExtendPath( UUID *agentId, int id, int count, float *x, float *y );
	int ddbVisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y );
	int ddbVisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name );
	int ddbVisRemoveObject( UUID *agentId, int id );
	int ddbVisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s );
	int ddbVisSetObjectVisible( UUID *agentId, int id, char visible );

	int ddbAddRegion( UUID *id, float x, float y, float w, float h );
	int ddbRemoveRegion( UUID *id );

	int ddbAddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, bool estimatedPos, ITEM_TYPES landmarkType );
	int ddbRemoveLandmark( UUID *id );
	int ddbLandmarkSetInfo( DataStream *ds );

	int ddbAddPOG( UUID *id, float tileSize, float resolution );
	int ddbRemovePOG( UUID *id );
	int ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data );
	int ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename );

	int ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize );
	int ddbRemoveParticleFilter( UUID *id );
	int ddbApplyPFResample( DataStream *ds );
	int ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange = false );
	int ddbProcessPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity );
	
	int ddbAddAvatar( UUID *id, char *type, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes );
	int ddbRemoveAvatar( UUID *id );
	int ddbAvatarSetInfo( UUID *id, int infoFlags, DataStream *ds );
	
	int ddbAddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize );
	int ddbRemoveSensor( UUID *id );
	int ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data = NULL, int dataSize = 0 );
	
private:



public:
	DECLARE_CALLBACK_CLASS( AgentMirror )

};

