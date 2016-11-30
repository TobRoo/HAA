// AgentMirror.cpp
//

#include "stdafx.h"

#include "AgentMirror.h"
#include "AgentMirrorVersion.h"

#include "..\\autonomic\\AgentHostVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentMirror

//-----------------------------------------------------------------------------
// Constructor	

AgentMirror::AgentMirror( ThinSocket *hostSocket ) {
	
	sprintf_s( this->agentType.name, sizeof(this->agentType.name), "AgentMirror" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentMirror_UUID), &typeId );
	this->agentType.uuid = typeId;
	this->agentType.instance = -1;

	this->hostSocket = hostSocket;
	hostCon.socket = INVALID_SOCKET;
	hostCon.raw = false;
	hostCon.buf = (char *)malloc(CON_INIT_BUFSIZE);
	if ( !hostCon.buf ) {
		// NOOOOO!
	}
	hostCon.bufStart = 0;
	hostCon.bufLen = 0;
	hostCon.bufMax = CON_INIT_BUFSIZE;
	hostCon.waitingForData = 0;

	this->configured = false;
	this->started = false;

	UuidCreateNil( &nilUUID );

	this->apb = new AgentPlayback( PLAYBACKMODE_OFF, NULL );
	this->dStore = new DDBStore( this->apb, &this->Log );

	// uuid
	this->apb->apbUuidCreate( &agentUuid );

	Log.setAgentPlayback( this->apb );

}

//-----------------------------------------------------------------------------
// Destructor

AgentMirror::~AgentMirror() {

	if ( this->started ) {
		this->stop();
	}

	this->_ddbClearWatcherCBs();

	delete this->dStore;
	delete this->apb;

	// free hostCon buffer
	free( hostCon.buf );
}

//-----------------------------------------------------------------------------
// Configure

int AgentMirror::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log AgentMirror %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentMirror %.2d.%.2d.%.5d.%.2d", AgentMirror_MAJOR, AgentMirror_MINOR, AgentMirror_BUILDNO, AgentMirror_EXTEND );
	}
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentMirror::start() {

	// introduce ourselves to the host
	this->ds.reset();
	this->ds.packUUID( &this->agentUuid );
	this->ds.packData( &this->agentType, sizeof(AgentType) );
	this->sendMessageEx( MSGEX(AgentHost_MSGS,MSG_MIRROR_REGISTER), this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	
	this->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentMirror::stop() {

	this->started = false;

	return 0;
}


//-----------------------------------------------------------------------------
// Step

int AgentMirror::step() {
	//Log.log( 0, "AgentMirror::step: hello there" );
	return 0;
}


//-----------------------------------------------------------------------------
// DDB

int AgentMirror::ddbAddWatcherCB( _Callback *cb, int type ) {
	int i, flag;
	mapDDBWatcherCBs::iterator watchers;
	std::list<_Callback *> *watcherList;

	if ( this->DDBCBReferences.find( cb ) == this->DDBCBReferences.end() ) {
		this->DDBCBReferences[cb] = 0; // new callback
	}

	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatcherCBs.find( flag );
			if ( watchers == this->DDBWatcherCBs.end() ) {
				watcherList = new std::list<_Callback *>;
				this->DDBWatcherCBs[flag] = watcherList;
			} else {
				watcherList = watchers->second;
			}
			watcherList->push_back( cb );
			this->DDBCBReferences[cb]++;
		}
	}

	if ( this->DDBCBReferences[cb] == 0 ) {
		delete cb;
		this->DDBCBReferences.erase( cb );
	}

	return 0;
}

int AgentMirror::ddbAddWatcherCB( _Callback *cb, UUID *item ) {
	mapDDBItemWatcherCBs::iterator watchers = this->DDBItemWatcherCBs.find( *item );
	std::list<_Callback *> *watcherList;

	if ( this->DDBCBReferences.find( cb ) == this->DDBCBReferences.end() ) {
		this->DDBCBReferences[cb] = 1; // new callback
	} else {
		this->DDBCBReferences[cb]++; // new reference
	}

	if ( watchers == this->DDBItemWatcherCBs.end() ) {
		watcherList = new std::list<_Callback *>;
		this->DDBItemWatcherCBs[*item] = watcherList;
	} else {
		watcherList = watchers->second;
	}
	watcherList->push_back( cb );


	return 0;
}

int AgentMirror::ddbRemWatcherCB( _Callback *cb ) {
	mapDDBWatcherCBs::iterator watchers;
	mapDDBItemWatcherCBs::iterator itemWatchers;
	std::list<_Callback *>::iterator iter;
	
	if ( this->DDBCBReferences.find( cb ) == this->DDBCBReferences.end() ) {
		return 1; // cb not found
	}

	// check our lists by type
	this->ddbRemWatcherCB( cb, 0xFFFF );

	// check our lists by item
	itemWatchers = this->DDBItemWatcherCBs.begin();
	while ( itemWatchers != this->DDBItemWatcherCBs.end() ) {
		iter = itemWatchers->second->begin();
		while ( iter != itemWatchers->second->end() ) {
			if ( (*iter) == cb ) {
				this->ddbRemWatcherCB( cb, (UUID *)&itemWatchers->first );
				break;
			}
			iter++;
		}
		itemWatchers++;
	}

	return 0;
}

int AgentMirror::ddbRemWatcherCB( _Callback *cb, int type ) {
	int i, flag;
	mapDDBWatcherCBs::iterator watchers;
	std::list<_Callback *>::iterator iter;
	
	if ( this->DDBCBReferences.find( cb ) == this->DDBCBReferences.end() ) {
		return 1; // cb not found
	}

	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatcherCBs.find( flag );

			if ( watchers == this->DDBWatcherCBs.end() ) {
				continue; // no watchers for this type!
			}

			iter = watchers->second->begin();
			while ( iter != watchers->second->end() ) {
				if ( (*iter) == cb ) {
					watchers->second->erase( iter );
					this->DDBCBReferences[cb]--;
					break;
				}
				iter++;
			}
		}
	}

	if ( this->DDBCBReferences[cb] == 0 ) {
		delete cb;
		this->DDBCBReferences.erase( cb );
	}

	return 1; // watcher not found!
}

int AgentMirror::ddbRemWatcherCB( _Callback *cb, UUID *item ) {
	mapDDBItemWatcherCBs::iterator watchers = this->DDBItemWatcherCBs.find( *item );
	std::list<_Callback *>::iterator iter;

	if ( this->DDBCBReferences.find( cb ) == this->DDBCBReferences.end() ) {
		return 1; // cb not found
	}

	if ( watchers == this->DDBItemWatcherCBs.end() ) {
		return 1; // no watchers for this item!
	}

	iter = watchers->second->begin();
	while ( iter != watchers->second->end() ) {
		if ( (*iter) == cb ) {
			watchers->second->erase( iter );
			this->DDBCBReferences[cb]--;
			break;
		}
		iter++;
	}

	if ( this->DDBCBReferences[cb] == 0 ) {
		delete cb;
		this->DDBCBReferences.erase( cb );
	}

	return 1; // watcher not found!
}

int AgentMirror::ddbClearWatcherCBs( UUID *id ) {
	mapDDBItemWatcherCBs::iterator watchers = this->DDBItemWatcherCBs.find( *id );

	if ( watchers == this->DDBItemWatcherCBs.end() ) {
		return 0; // no watchers for this item
	}

	while ( !watchers->second->empty() ) {
		this->DDBCBReferences[watchers->second->front()]--;
		if ( this->DDBCBReferences[watchers->second->front()] == 0 ) {
			delete watchers->second->front();
			this->DDBCBReferences.erase( watchers->second->front() );
			watchers->second->pop_front();
		}
	}

	delete watchers->second;
	
	this->DDBItemWatcherCBs.erase( watchers );

	return 0;
}

int AgentMirror::_ddbClearWatcherCBs() {
	mapDDBWatcherCBs::iterator watchers;
	mapDDBItemWatcherCBs::iterator iwatchers;
	mapDDBCBReferences::iterator refs;
	std::list<_Callback *>::iterator iter;

	while ( !this->DDBWatcherCBs.empty() ) {
		watchers = this->DDBWatcherCBs.begin();
		delete watchers->second;
		this->DDBWatcherCBs.erase( watchers );
	}

	while ( !this->DDBItemWatcherCBs.empty() ) {
		iwatchers = this->DDBItemWatcherCBs.begin();
		delete iwatchers->second;
		this->DDBItemWatcherCBs.erase( iwatchers );
	}

	while ( !this->DDBCBReferences.empty() ) {
		refs = this->DDBCBReferences.begin();
		delete refs->first;
		this->DDBCBReferences.erase( refs );
	}

	return 0;
}

int AgentMirror::_ddbNotifyWatcherCBs( int type, char evt, UUID *id, void *data, int len ) {
	DataStream lds;
	std::list<_Callback *>::iterator iter;

	// by type
	mapDDBWatcherCBs::iterator watchers = this->DDBWatcherCBs.find( type );
	if ( watchers != this->DDBWatcherCBs.end() && !watchers->second->empty() ) {
		lds.reset();
		lds.packInt32( type );
		lds.packUUID( id );
		lds.packChar( evt );
		if ( len )
			lds.packData( data, len );
		iter = watchers->second->begin();
		while ( iter != watchers->second->end() ) {
			lds.rewind();
			(*iter)->callback( &lds );
			iter++;
		}
		lds.unlock();
	}

	// by item
	mapDDBItemWatcherCBs::iterator iwatchers = this->DDBItemWatcherCBs.find( *id );
	if ( iwatchers != this->DDBItemWatcherCBs.end() && !iwatchers->second->empty() ) {
		if ( watchers == this->DDBWatcherCBs.end() || watchers->second->empty() ) { // data hasn't been packed yet
			lds.reset();
			lds.packInt32( type );
			lds.packUUID( id );
			lds.packChar( evt );
			if ( len )
				lds.packData( data, len );
		}
		iter = iwatchers->second->begin();
		while ( iter != iwatchers->second->end() ) {
			lds.rewind();
			(*iter)->callback( &lds );
			iter++;
		}
		lds.unlock();
	}

	return 0;
}

int AgentMirror::_ddbParseEnumerate( DataStream *ds ) {
	int ret;
	int type;
	UUID parsedId;

	while ( (type = ds->unpackInt32()) != DDB_INVALID ) {
		
		switch ( type ) {
		case DDB_AGENT:
			if ( (ret = this->dStore->ParseAgent( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_REGION:
			if ( (ret = this->dStore->ParseRegion( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_REGION, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_LANDMARK:
			if ( (ret = this->dStore->ParseLandmark( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_LANDMARK, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_MAP_PROBOCCGRID:
			if ( (ret = this->dStore->ParsePOG( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_MAP_PROBOCCGRID, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_PARTICLEFILTER:
			if ( (ret = this->dStore->ParseParticleFilter( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_AVATAR:
			if ( (ret = this->dStore->ParseAvatar( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( DDB_AVATAR, DDBE_ADD, &parsedId );
			}
			break;
		case DDB_SENSOR_SONAR:
		case DDB_SENSOR_CAMERA:
		case DDB_SENSOR_SIM_CAMERA:
			if ( (ret = this->dStore->ParseSensor( ds, &parsedId )) > 0 )
				return 1;
			if ( ret == 0 ) { // notify watchers
				this->_ddbNotifyWatcherCBs( type, DDBE_ADD, &parsedId );
			}
			break;
		default:
			// Unhandled type?!
			return 1;
		}
	}

	return 0;
}

int AgentMirror::ddbAddAgent( UUID *id, UUID *parentId, AgentType *agentType ) {

	this->dStore->AddAgent( id, parentId, agentType->name, &agentType->uuid, agentType->instance, &nilUUID, 0, 0, -1, -1 ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveAgent( UUID *id ) {

	this->dStore->RemoveAgent( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbVisAddPath( UUID *agentId, int id, int count, float *x, float *y ) {
	// verify valid id (i.e. doesn't exist yet)
	if ( this->dStore->VisValidPathId( agentId, id ) )
		return 1; // path with this id already exists

	this->dStore->VisAddPath( agentId, id, count, x, y ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_ADDPATH, agentId, &id, sizeof(int) );

	return 0;
}

int AgentMirror::ddbVisRemovePath( UUID *agentId, int id ) {
	// verify valid id
	if ( !this->dStore->VisValidPathId( agentId, id ) )
		return 1; // path not found

	this->dStore->VisRemovePath( agentId, id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_REMPATH, agentId, &id, sizeof(int) );

	return 0;
}

int AgentMirror::ddbVisExtendPath( UUID *agentId, int id, int count, float *x, float *y ) {
	
	return 0;
}

int AgentMirror::ddbVisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y ) {
	
	return 0;
}

int AgentMirror::ddbVisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	int i;

	//Log.log( LOG_LEVEL_VERBOSE, "AgentMirror::ddbVisAddStaticObject: %s DDBE_VIS_ADDOBJECT objectId %d, name %s x %f y %f r %f", Log.formatUUID(LOG_LEVEL_VERBOSE,agentId), id, name, x, y, r );

	// verify valid id (i.e. doesn't exist yet)
	if ( this->dStore->VisValidObjectId( agentId, id ) )
		return 1; // object with this id already exists

	// verify paths exist
	for ( i=0; i<count; i++ ) {
		if ( !this->dStore->VisValidPathId( agentId, paths[i] ) )
			return 1; // path doesn't exist
	}

	this->dStore->VisAddStaticObject( agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_ADDOBJECT, agentId, &id, sizeof(int) );

	return 0;
}


int AgentMirror::ddbVisRemoveObject( UUID *agentId, int id ) {
	
	//Log.log( LOG_LEVEL_VERBOSE, "AgentMirror::ddbVisRemoveObject: %s DDBE_VIS_REMOBJECT objectId %d", Log.formatUUID(LOG_LEVEL_VERBOSE,agentId), id );

	// verify valid id
	if ( !this->dStore->VisValidObjectId( agentId, id ) )
		return 1; // object not found

	this->dStore->VisRemoveObject( agentId, id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_REMOBJECT, agentId, &id, sizeof(int) );

	return 0;
}

int AgentMirror::ddbVisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s )  {
	DataStream ds;
	
	// verify valid id
	if ( !this->dStore->VisValidObjectId( agentId, id ) )
		return 1; // object not found

	this->dStore->VisUpdateObject( agentId, id, x, y, r, s ); // update locally

	// notify watchers
	ds.reset();
	ds.packInt32( id );
	ds.packChar( MSG_DDB_VIS_UPDATEOBJECT );
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_UPDATEOBJECT, agentId, ds.stream(), ds.length() );
	ds.unlock();

	return 0;
}

int AgentMirror::ddbVisSetObjectVisible( UUID *agentId, int id, char visible )  {
	DataStream ds;

	// verify valid id
	if ( !this->dStore->VisValidObjectId( agentId, id ) )
		return 1; // object not found

	this->dStore->VisSetObjectVisible( agentId, id, visible ); // set locally

	// notify watchers
	ds.reset();
	ds.packInt32( id );
	ds.packChar( MSG_DDB_VIS_SETOBJECTVISIBLE );
	this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_UPDATEOBJECT, agentId, ds.stream(), ds.length() );
	ds.unlock();

	return 0;
}

int AgentMirror::ddbAddRegion( UUID *id, float x, float y, float w, float h ) {

	this->dStore->AddRegion( id, x, y, w, h ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_REGION, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveRegion( UUID *id ) {

	this->dStore->RemoveRegion( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_REGION, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbAddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, bool estimatedPos, ITEM_TYPES landmarkType ) {

	this->dStore->AddLandmark( id, code, owner, height, elevation, x, y, estimatedPos, landmarkType); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_LANDMARK, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveLandmark( UUID *id ) {

	this->dStore->RemoveLandmark( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_LANDMARK, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbLandmarkSetInfo( DataStream *ds ) {
	UUID sender, uuid;
	int infoFlags;

	ds->unpackUUID( &sender );
	ds->unpackUUID( &uuid );
	infoFlags = ds->unpackInt32();
	
	infoFlags = dStore->LandmarkSetInfo( &uuid, infoFlags, ds );

	if ( infoFlags )
		this->_ddbNotifyWatcherCBs( DDB_LANDMARK, DDBE_UPDATE, &uuid, &infoFlags, sizeof(int) );

	return 0;
}

int AgentMirror::ddbAddPOG( UUID *id, float tileSize, float resolution ) {

	// verify that tileSize and resolution are valid
	if ( (float)floor(tileSize/resolution) != (float)(tileSize/resolution) )
		return 1; // tileSize must be an integer multiple of resolution

	this->dStore->AddPOG( id, tileSize, resolution ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_MAP_PROBOCCGRID, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemovePOG( UUID *id ) {

	this->dStore->RemovePOG( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_MAP_PROBOCCGRID, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data ) {
	int updateSize;

	// verify that the cooridates are valid
	updateSize = this->dStore->POGVerifyRegion( id, x, y, w, h );
	if ( !updateSize ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	this->dStore->ApplyPOGUpdate( id, x, y, w, h, data ); // add locally

	// notify watchers (by type and POG uuid)
	this->ds.reset();
	this->ds.packFloat32( x );
	this->ds.packFloat32( y );
	this->ds.packFloat32( w );
	this->ds.packFloat32( h );
	this->_ddbNotifyWatcherCBs( DDB_MAP_PROBOCCGRID, DDBE_POG_UPDATE, id, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentMirror::ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename ) {
	FILE *fp;

	// verify that the cooridates are valid
	if ( !this->dStore->POGVerifyRegion( id, x, y, w, h ) ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	// make sure the file exists
	if ( fopen_s( &fp, filename, "r" ) ) {
		return 1; 
	}
	fclose( fp );

	this->dStore->POGLoadRegion( id, x, y, w, h, filename ); // add locally

	// notify watchers (by type and POG uuid)
	this->ds.reset();
	this->ds.packFloat32( x );
	this->ds.packFloat32( y );
	this->ds.packFloat32( w );
	this->ds.packFloat32( h );
	this->_ddbNotifyWatcherCBs( DDB_MAP_PROBOCCGRID, DDBE_POG_LOADREGION, id, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentMirror::ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize ) {

	this->dStore->AddParticleFilter( id, owner, numParticles, startTime, startState, stateSize ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveParticleFilter( UUID *id ) {

	this->dStore->RemoveParticleFilter( id );

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbApplyPFResample( DataStream *ds ) {
	DataStream lds;
	UUID id;
	int particleNum, stateSize;
	int *parents;
	float *state;

	ds->unpackUUID( &id );
	particleNum = ds->unpackInt32();
	stateSize = ds->unpackInt32();
	parents = (int *)ds->unpackData( sizeof(int)*particleNum );
	state = (float *)ds->unpackData( sizeof(float)*stateSize*particleNum );

	this->dStore->ResamplePF_Apply( &id, parents, state );

	// notify watchers
	float effectiveParticleNum;
	_timeb *tb;
	dStore->PFGetInfo( &id, DDBPFINFO_USECURRENTTIME, NULL, &lds, &nilUUID, &effectiveParticleNum );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread
	if ( lds.unpackChar() == DDBR_OK ) {
		tb = (_timeb *)lds.unpackData(sizeof(_timeb));
		this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_PF_RESAMPLE, &id, (char *)tb, sizeof(_timeb) );
	}
	lds.unlock();

	return 0;
}

int AgentMirror::ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange ) {

	// TODO increment age

	this->dStore->InsertPFPrediction( id, tb, state, nochange ); // add locally

	// notify watchers (by type and pf uuid)
	this->ds2.reset();
	this->ds2.packData( tb, sizeof(_timeb) );
	this->ds2.packChar( (char)nochange );
	this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_PF_PREDICTION, id, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

	return 0;
}

int AgentMirror::ddbProcessPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity ) {

	// TODO increment age

	this->dStore->ApplyPFCorrection( id, regionAge, tb, obsDensity ); // add locally

	// notify watchers (by type and pf uuid)
	this->_ddbNotifyWatcherCBs( DDB_PARTICLEFILTER, DDBE_PF_CORRECTION, id, (char *)tb, sizeof(_timeb) );

	return 0;
}

int AgentMirror::ddbAddAvatar( UUID *id, char *type, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes ) {

	AgentType agentType;
	// mirrors don't know agent types
	UuidCreateNil( &agentType.uuid );
	agentType.instance = -1;
	sprintf_s( agentType.name, "mirrors don't know agent types" );
	this->dStore->AddAvatar( id, type, &agentType, status, agent, pf, innerRadius, outerRadius, startTime, capacity, sensorTypes ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AVATAR, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveAvatar( UUID *id ) {

	this->dStore->RemoveAvatar( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( DDB_AVATAR, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbAvatarSetInfo( UUID *id, int infoFlags, DataStream *ds ) {

	infoFlags = this->dStore->AvatarSetInfo( id, infoFlags, ds ); // update locally

	// notify watchers
	if ( infoFlags )
		this->_ddbNotifyWatcherCBs( DDB_AVATAR, DDBE_UPDATE, id, &infoFlags, sizeof(int) );

	return 0;
}

int AgentMirror::ddbAddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize ) {

	this->dStore->AddSensor( id, type, avatar, pf, pose, poseSize ); // add locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( type, DDBE_ADD, id );

	return 0;
}

int AgentMirror::ddbRemoveSensor( UUID *id ) {
	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;

	this->dStore->RemoveSensor( id ); // remove locally

	// notify watchers
	this->_ddbNotifyWatcherCBs( type, DDBE_REM, id );
	this->ddbClearWatcherCBs( id );

	return 0;
}

int AgentMirror::ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data, int dataSize ) {
	
	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;

	// TODO increment age

	this->dStore->InsertSensorReading( id, tb, reading, readingSize, data, dataSize ); // add locally

	// notify watchers (by type and sensor uuid)
	this->_ddbNotifyWatcherCBs( type, DDBE_SENSOR_UPDATE, id, (void *)tb, sizeof(_timeb) );

	return 0;
}

//-----------------------------------------------------------------------------
// Networking

int AgentMirror::conCheckBufSize() {
	if ( hostCon.bufMax - hostCon.bufStart - hostCon.bufLen == 0 ) { // we're out of room
		hostCon.buf = (char *)realloc( hostCon.buf, hostCon.bufMax * 2 );
		hostCon.bufMax *= 2;
		if ( hostCon.buf == NULL ) {
			Log.log( 0, "AgentMirror::conCheckBufSize: realloc failed" );
			return 1;
		}
	}
	return 0;
}

int AgentMirror::conReceiveData() {

	int iResult = this->hostSocket->Receive( hostCon.buf + hostCon.bufStart + hostCon.bufLen, hostCon.bufMax - hostCon.bufStart - hostCon.bufLen );

	_ftime_s( &hostCon.recvTime ); // store the recive time

	if ( iResult == SOCKET_ERROR ) {
		iResult = WSAGetLastError();
		switch ( iResult ) {
		case WSAEWOULDBLOCK:
			return 0;
		case WSAECONNRESET:
			// anything to do here?
			return 0;
		case WSAEMSGSIZE:
			hostCon.bufLen = hostCon.bufMax - hostCon.bufStart;
			if ( hostCon.bufStart != 0 ) { // shift buf back to beginning and try to read more data
				int i;
				for ( i=0; i<hostCon.bufLen; i++ )
					hostCon.buf[i] = hostCon.buf[i+hostCon.bufStart];
				hostCon.bufStart = 0;
				return this->conReceiveData();
			} else { 
				int ret;
				ret = this->conProcessStream();
				if ( ret ) { // error occured
					Log.log( 0, "AgentMirror::conReceiveData: error processing data" );
					return 1;
				} else if ( hostCon.bufLen == hostCon.bufMax ) { // buffer is still too small 
					hostCon.buf = (char *)realloc( hostCon.buf, hostCon.bufMax * 2 );
					hostCon.bufMax *= 2;
					if ( hostCon.buf == NULL ) {
						Log.log( 0, "AgentMirror::conReceiveData: realloc failed" );
						return 1;
					}
					return this->conReceiveData();
				} else { // we managed to free some space, go get more data
					return this->conReceiveData();
				}
			}
		default:
			Log.log( 0, "AgentMirror::conReceiveData: unhandled error, %d", iResult );
			return 1; // unhandled error
		}
	} else if ( iResult == 0 ) { // connection closed
		Log.log( 0, "AgentMirror::conReceiveData: connection closed" );
		return 0;
	}
	hostCon.bufLen += iResult;

	return this->conProcessStream();
}

int AgentMirror::conProcessStream() {
	unsigned int msgSize;
	
	while ( 1 ) {
		if ( hostCon.waitingForData == -1 ) { // msg size in char
			if ( hostCon.bufLen ) {
				hostCon.waitingForData = (unsigned char)*(hostCon.buf+hostCon.bufStart);
				hostCon.bufStart++;
				hostCon.bufLen--;
			} else {
				return this->conCheckBufSize(); // make sure we have space available and wait for more data
			}
		} else if ( hostCon.waitingForData == -2 ) { // msg size in unsigned int
			if ( hostCon.bufLen >= 4 ) {
				hostCon.waitingForData = *(unsigned int *)(hostCon.buf+hostCon.bufStart);
				hostCon.bufStart += 4;
				hostCon.bufLen -= 4;
			} else {
				return this->conCheckBufSize(); // make sure we have space available and wait for more data
			}
		} else if ( hostCon.waitingForData == -3 ) { // if byte == 0xFF then size follows in an int, else size is the byte
			if ( hostCon.bufLen ) {
				unsigned char size = (unsigned char)*(hostCon.buf+hostCon.bufStart);
				hostCon.bufStart++;
				hostCon.bufLen--;
				if ( size == 0xFF ) {
					hostCon.waitingForData = -2;
					return this->conProcessStream();
				} else if ( size > 0 ) {
					hostCon.waitingForData = size;
				} else {
					this->conProcessMessage( hostCon.message, NULL, 0 );
					hostCon.waitingForData = 0;
					goto doneMessage;
				}
			} else {
				return this->conCheckBufSize(); // make sure we have space available and wait for more data
			}
		}
		
		if ( hostCon.waitingForData ) {
			if ( hostCon.bufLen >= hostCon.waitingForData ) {
				this->conProcessMessage( hostCon.message, hostCon.buf+hostCon.bufStart, hostCon.waitingForData );
				hostCon.bufStart += hostCon.waitingForData;
				hostCon.bufLen -= hostCon.waitingForData;
				hostCon.waitingForData = 0;
			} else {
				return this->conCheckBufSize(); // make sure we have space available and wait for more data
			}
		} else {
			hostCon.message = (unsigned char)*(hostCon.buf+hostCon.bufStart);
			hostCon.bufStart++;
			hostCon.bufLen--;
			if ( hostCon.message < MSG_COMMON )
				msgSize = MSG_SIZE[hostCon.message];
			else
				msgSize = -3;

			if ( msgSize == 0 ) {
				this->conProcessMessage( hostCon.message, NULL, 0 );
			} else {
				hostCon.waitingForData = msgSize;
			}
		}

	doneMessage:

		if ( !hostCon.bufLen ) { // reset bufStart
			hostCon.bufStart = 0;
			return 0;
		}
		
		if ( hostCon.bufStart > hostCon.bufMax / 2 ) { // shift buffer back to beginning
			int i;
			for ( i=0; i<hostCon.bufLen; i++ )
				hostCon.buf[i] = hostCon.buf[i+hostCon.bufStart];
			hostCon.bufStart = 0;
		}
	}

	return 0; // should never get here
}

int AgentMirror::conProcessMessage( unsigned char message, char *data, unsigned int len ) {
	UUID uuid;
	DataStream lds;
	this->haveReturn = false; // clear the return address flag

	switch ( message ) {
	case MSG_ACK:
		// do nothing
		break;
	case MSG_RACK:
		// send ACK
		this->sendMessage( MSG_ACK );
		break;
	case MSG_ACKEX:
		// TODO: do something
		break;
	case MSG_RACKEX:
		// send RACKEX
		this->sendMessage( MSG_ACKEX, data, len );
		break;
	case MSG_FORWARD:
		{
			unsigned char msg;
			int offset, msgSize;
			UUID uuid;
			lds.setData( data, min( len, sizeof(UUID)*2 + 1 + 1 + 1 + 4 ) ); // we need at most this many bytes to sort this out
			lds.unpackUUID( &uuid );
			if ( agentUuid == uuid ) { // message is for us
				this->haveReturn = lds.unpackChar();
				offset = sizeof(UUID) + 1;
				if ( this->haveReturn ) {
					lds.unpackUUID( &this->returnAddress );
					offset += sizeof(UUID);
				}

				// figure out message and process it
				msg = (unsigned char)lds.unpackChar();
				offset++;
				if ( msg < MSG_COMMON )
					msgSize = MSG_SIZE[msg];
				else
					msgSize = -3;

				if ( msgSize < (unsigned int)-3 ) {
					return this->conProcessMessage( msg, data + offset, msgSize );
				} else if ( msgSize == -1 ) {
					return this->conProcessMessage( msg, data + offset + 1, (unsigned char)lds.unpackChar() );
				} else if ( msgSize == -2 ) {
					return this->conProcessMessage( msg, data + offset + 4, (unsigned int)lds.unpackInt32() );
				} else { // msgSize == -3
					msgSize = (unsigned char)lds.unpackChar();
					if ( msgSize < 0xFF ) {
						return this->conProcessMessage( msg, data + offset + 1, msgSize );
					} else {
						return this->conProcessMessage( msg, data + offset + 1 + 4, (unsigned int)lds.unpackInt32() );
					}
				} 
			}
			lds.unlock();
		}
		break;
	case MSG_RESPONSE:
		break;
	case MSG_AGENT_START:
		break;
	case MSG_AGENT_STOP:
		break;
	case MSG_AGENT_INSTANCE:
		break;

/*	case MSG__DDB_RESAMPLEPF_UNLOCK:
		{
			lds.setData( data, len );
			this->ddbResampleParticleFilter_Unlock( &lds );
			lds.unlock();
		}
		break;
*/
	// AgentHost messages
	case AgentHost_MSGS::MSG_RSUPERVISOR_REFRESH:
	case AgentHost_MSGS::MSG_HOST_INTRODUCE:
	case AgentHost_MSGS::MSG_HOST_SHUTDOWN:
	case AgentHost_MSGS::MSG_HOST_LABEL:
	case AgentHost_MSGS::MSG_RHOST_LABEL:
	case AgentHost_MSGS::MSG_HOST_STUB:
	case AgentHost_MSGS::MSG_RHOST_STUB:
	case AgentHost_MSGS::MSG_OTHER_STATUS:
	case AgentHost_MSGS::MSG_ROTHER_STATUS:
	case AgentHost_MSGS::MSG_AGENTSPAWNPROPOSAL:
	case AgentHost_MSGS::MSG_RAGENTSPAWNPROPOSAL:
	case AgentHost_MSGS::MSG_ACCEPTAGENTSPAWNPROPOSAL:
	case AgentHost_MSGS::MSG_AGENTSPAWNSUCCEEDED:
	case AgentHost_MSGS::MSG_AGENTSPAWNFAILED:
	case AgentHost_MSGS::MSG_AGENT_KILL:
	case AgentHost_MSGS::MSG_MIRROR_REGISTER:
		break; // ignore

	// Distribute messages
	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_AGENT:
	case AgentHost_MSGS::MSG_DISTRIBUTE_REM_AGENT:
	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_UNIQUE:
	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_MIRROR:
	case AgentHost_MSGS::MSG_DISTRIBUTE_REM_MIRROR:
	case OAC_DDB_WATCH_TYPE:
	case OAC_DDB_WATCH_ITEM:
	case OAC_DDB_STOP_WATCHING_TYPE:
	case OAC_DDB_STOP_WATCHING_ITEM:
	case OAC_DDB_CLEAR_WATCHERS:
		break; // ignore
	case AgentHost_MSGS::MSG_DDB_ENUMERATE:
		lds.setData( data, len );
		this->_ddbParseEnumerate( &lds );
		lds.unlock();
		break;
	case OAC_DDB_ADDAGENT:
		{
			UUID parentId;
			AgentType *agentType;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &parentId );
			agentType = (AgentType *)lds.unpackData( sizeof(AgentType) );
			this->ddbAddAgent( &uuid, &parentId, agentType );
			lds.unlock();
		}
		break;
	case OAC_DDB_REMAGENT:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemoveAgent( &uuid );
		}
		break;
	case OAC_DDB_AGENTSETINFO:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			int infoFlags = lds.unpackInt32();
			infoFlags = this->dStore->AgentSetInfo( &uuid, infoFlags, &lds );
			lds.unlock();
			if ( infoFlags ) {
				// notify watchers
				this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_AGENT_UPDATE, &uuid, (void *)&infoFlags, sizeof(int) );
			}
		}
		break;
	case OAC_DDB_VIS_ADDPATH:
		{
			UUID agentId;
			int id, count;
			float *x, *y;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			count = lds.unpackInt32();
			x = (float *)lds.unpackData(count*sizeof(float));
			y = (float *)lds.unpackData(count*sizeof(float));
			this->ddbVisAddPath( &agentId, id, count, x, y );
			lds.unlock();
		}
		break;
	case OAC_DDB_VIS_REMPATH:
		{
			UUID agentId;
			int id;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();
			this->ddbVisRemovePath( &agentId, id );
		}
		break;
	case OAC_DDB_VIS_EXTENDPATH:
	case OAC_DDB_VIS_UPDATEPATH:
		break; // TODO
	case OAC_DDB_VIS_ADDOBJECT:
		{
			int i;
			UUID agentId;
			int id;
			float x, y, r, s;
			int count;
			int *paths;
			float **colours, *lineWidths;
			bool solid;
			char *name;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			count = lds.unpackInt32();
			paths = (int *)lds.unpackData(count*sizeof(int));
			colours = (float **)malloc(count*sizeof(float*));
			if ( !colours ) { // malloc failed!
				lds.unlock();
				return 0;
			}
			for ( i=0; i<count; i++ ) {
				colours[i] = (float *)lds.unpackData(sizeof(float)*3);
			}
			lineWidths = (float *)lds.unpackData(count*sizeof(float));
			solid = lds.unpackBool();
			name = lds.unpackString();
			this->ddbVisAddStaticObject( &agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name );
			lds.unlock();
			free( colours );
		}
		break;
	case OAC_DDB_VIS_REMOBJECT:
		{
			UUID agentId;
			int id;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();
			this->ddbVisRemoveObject( &agentId, id );
		}
		break;
	case OAC_DDB_VIS_UPDATEOBJECT:
		{
			UUID agentId;
			int id;
			float x, y, r, s;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			lds.unlock();
			this->ddbVisUpdateObject( &agentId, id, x, y, r, s );
		}
		break;
	case OAC_DDB_VIS_SETOBJECTVISIBLE:
		{
			int objectId;
			char visible;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			objectId = lds.unpackInt32();
			visible = lds.unpackChar();
			lds.unlock();
			this->ddbVisSetObjectVisible( &uuid, objectId, visible );
		}
		break;
	case OAC_DDB_VIS_CLEAR_ALL:
		{
			UUID sender;
			char clearPaths;
			std::list<int> objects;
			std::list<int> paths;
			std::list<int>::iterator iI;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			clearPaths = lds.unpackChar();
			lds.unlock();
			this->dStore->VisListObjects( &uuid, &objects );
			if ( clearPaths )
				this->dStore->VisListPaths( &uuid, &paths );
			this->dStore->VisClearAll( &uuid, clearPaths );
			for ( iI = objects.begin(); iI != objects.end(); iI++ ) {
				this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_REMOBJECT, &uuid, &*iI, sizeof(int) );
			}
			if ( clearPaths ) {
				for ( iI = paths.begin(); iI != paths.end(); iI++ ) {
					this->_ddbNotifyWatcherCBs( DDB_AGENT, DDBE_VIS_REMPATH, &uuid, &*iI, sizeof(int) );
				}
			}
		}
		break;
	case OAC_DDB_ADDREGION:
		{
			float x, y, w, h;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			this->ddbAddRegion( &uuid, x, y, w, h );
			lds.unlock();
		}
		break;
	case OAC_DDB_REMREGION:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemoveRegion( &uuid );
		}
		break;
	case OAC_DDB_ADDLANDMARK:
		{
			unsigned char code;
			UUID owner;
			float height, elevation, x, y;
			char estimatedPos;
			UUID sender;
			ITEM_TYPES landmarkType;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			code = lds.unpackUChar();
			lds.unpackUUID( &owner );
			height = lds.unpackFloat32();
			elevation = lds.unpackFloat32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			estimatedPos = lds.unpackChar();
			landmarkType = (ITEM_TYPES) lds.unpackInt32();
			this->ddbAddLandmark( &uuid, code, &owner, height, elevation, x, y, (estimatedPos ? true : false), landmarkType );
			lds.unlock();
		}
		break;
	case OAC_DDB_REMLANDMARK:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &sender );
			lds.unlock();
			this->ddbRemoveLandmark( (UUID *)data );
		}
		break;
	case OAC_DDB_LANDMARKSETINFO:
		lds.setData( data, len );
		this->ddbLandmarkSetInfo( &lds );
		lds.unlock();
		break;
	case OAC_DDB_ADDPOG: 
		{
			float tileSize, resolution;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tileSize = lds.unpackFloat32();
			resolution = lds.unpackFloat32();
			this->ddbAddPOG( &uuid, tileSize, resolution );
			lds.unlock();
		}
		break;
	case OAC_DDB_REMPOG:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemovePOG( &uuid );
		}
		break;
	case OAC_DDB_APPLYPOGUPDATE:
		{
			float x, y, w, h;
			int updateSize;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			updateSize = lds.unpackInt32();
			this->ddbApplyPOGUpdate( &uuid, x, y, w, h, (float *)lds.unpackData( updateSize ) );
			lds.unlock();
		}
		break;
	case OAC_DDB_POGLOADREGION:
		{
			float x, y, w, h;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			this->ddbPOGLoadRegion( &uuid, x, y, w, h, lds.unpackString() );
			lds.unlock();
		}
		break;
	case OAC_DDB_ADDPARTICLEFILTER:
		{
			UUID owner;
			int numParticles, stateSize;
			_timeb tb;
			float *startState;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &owner );
			numParticles = lds.unpackInt32();
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			stateSize = lds.unpackInt32();
			startState = (float *)lds.unpackData( sizeof(float)*numParticles*stateSize );
			this->ddbAddParticleFilter( &uuid, &owner, numParticles, &tb, startState, stateSize );
			lds.unlock();
		}
		break;
	case OAC_DDB_REMPARTICLEFILTER:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemoveParticleFilter( &uuid );
		}
		break;
	case OAC_DDB_INSERTPFPREDICTION:
		{
			int offset;
			_timeb *tb;
			bool nochange;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tb = (_timeb *)lds.unpackData( sizeof(_timeb) );
			nochange = lds.unpackChar() ? 1 : 0;
			offset = sizeof(UUID)*2 + sizeof(_timeb) + 1;
			this->ddbInsertPFPrediction( &uuid, tb, (float *)(data + offset), nochange );
			lds.unlock();
		}
		break;
	case OAC_DDB_APPLYPFCORRECTION:
		{
			int regionAge, offset;
			_timeb tb;
			offset = sizeof(UUID)*2 + 4 + sizeof(_timeb);
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			regionAge = lds.unpackInt32();
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			this->ddbProcessPFCorrection( &uuid, regionAge, &tb, (float *)(data + offset) );
		}
		break;
	case OAC_DDB_APPLYPFRESAMPLE:
		lds.setData( data, len );
		this->ddbApplyPFResample( &lds );
		lds.unlock();
		break;
	case OAC_DDB_ADDAVATAR:
		{
			char type[64];
			int status;
			UUID agent, pf;
			float innerRadius, outerRadius;
			_timeb startTime;
			int capacity;
			int sensorTypes;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			strncpy_s( type, sizeof(type), lds.unpackString(), sizeof(type) );
			status = lds.unpackInt32();
			lds.unpackUUID( &agent );
			lds.unpackUUID( &pf );
			innerRadius = lds.unpackFloat32();
			outerRadius = lds.unpackFloat32();
			startTime = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			capacity = lds.unpackInt32();
			sensorTypes = lds.unpackInt32();
			lds.unlock();
			this->ddbAddAvatar( &uuid, type, status, &agent, &pf, innerRadius, outerRadius, &startTime, capacity, sensorTypes );
		}
		break;
	case OAC_DDB_REMAVATAR:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemoveAvatar( &uuid );
		}
		break;
	case OAC_DDB_AVATARSETINFO:
		{
			UUID sender;
			int infoFlags;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			this->ddbAvatarSetInfo( &uuid, infoFlags, &lds );
			lds.unlock();
		}
		break;
	case OAC_DDB_ADDSENSOR:
		{
			UUID avatar, pf;
			int type, offset;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			type = lds.unpackInt32();
			lds.unpackUUID( &avatar );
			lds.unpackUUID( &pf );
			lds.unlock();
			offset = sizeof(UUID)*2 + 4 + sizeof(UUID) + sizeof(UUID);
			this->ddbAddSensor( &uuid, type, &avatar, &pf, data + offset, len - offset );
		}
		break;
	case OAC_DDB_REMSENSOR:
		{ 
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->ddbRemoveSensor( &uuid );
		}
		break;
	case OAC_DDB_INSERTSENSORREADING:
		{
			_timeb tb;
			void *reading, *rdata;
			int readingSize, dataSize;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			readingSize = lds.unpackInt32();
			reading = lds.unpackData( readingSize );
			dataSize = lds.unpackInt32();
			if ( dataSize ) rdata = lds.unpackData( dataSize );
			else rdata = NULL;
			this->ddbInsertSensorReading( &uuid, &tb, reading, readingSize, rdata, dataSize );
			lds.unlock();
		}
		break;

	default:
		return 1; // unhandled message
	}

	return 0;
}


int AgentMirror::sendMessage( unsigned char message, char *data, unsigned int len ) {
	unsigned int msgSize;

	if ( message >= MSG_COMMON ) {
		Log.log( 0, "AgentMirror::sendMessage: message out of range (%d >= MSG_COMMON), perhaps you should be using sendMessageEx?", message );
		return 1;
	}

	msgSize = MSG_SIZE[message];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentMirror::sendMessage: message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	return this->conSendMessage( message, data, len, msgSize );
}

int AgentMirror::sendMessageEx( MSGEXargs, char *data, unsigned int len ) {
	unsigned int msgSize;
	
	if ( message >= msg_last ) {
		Log.log( 0, "AgentMirror::sendMessageEx: message out of range (%d >= msg_last)", message );
		return 1;
	} else if ( message < msg_first ) {
		Log.log( 0, "AgentMirror::sendMessageEx: message out of range (%d < msg_first)", message );
		return 1;
	}

	msgSize = msg_size[message-msg_first];

	if ( msgSize < (unsigned int)-3 && msgSize != len ) {
		Log.log( 0, "AgentMirror::sendMessageEx: message wrong length, [%d: %d != %d]", message, msgSize, len );
		return 1;
	}

	msgSize = -3; // all message ex are sent with size -3

	return conSendMessage( message, data, len, msgSize );
}

int AgentMirror::conSendMessage( unsigned char message, char *data, unsigned int len, unsigned int msgSize ) {
	char buf[256], *bufPtr, *bufStart, *tempBuf = NULL;
	unsigned int bufLen;
	int ret;
	
	if ( msgSize == -3 && len < 0xFF ) 
		msgSize = -1;
	
	bufPtr = bufStart = buf;
	bufLen = 1;
	if ( msgSize == -1 ) {
		bufLen += 1 + len; // message size contained in char
	} else if ( msgSize == -2 ) {
		bufLen += 4 + len; // message size contained in unsigned int
	} else if ( msgSize == -3 ) {
		bufLen += 1 + 4 + len; // message size contained in byte flag + unsigned int
	} else {
		bufLen += msgSize;
	}
	if ( bufLen > sizeof( buf ) ) {
		bufPtr = bufStart = tempBuf = (char *)malloc( bufLen );
		if ( !bufPtr ) {
			Log.log( 0, "AgentMirror::conSendMessage: tempBuf, malloc failed" );
			return 1;
		}
	}

	// write msg id
	*bufPtr = message;
	bufPtr++;

	if ( msgSize == -1 ) { // write msg size as unsigned char
		*bufPtr = (unsigned char)len;
		bufPtr++;
		msgSize = len;
	} else if ( msgSize == -2 ) { // write msg size as unsigned int
		memcpy( bufPtr, &len, 4 );
		bufPtr += 4;
		msgSize = len;
	} else if ( msgSize == -3 ) { // write msg size as byte flag + unsigned int
		*bufPtr = (unsigned char)0xFF;
		bufPtr++;
		memcpy( bufPtr, &len, 4 );
		bufPtr += 4;
		msgSize = len;
	}

	// write data
	if ( msgSize )
		memcpy( bufPtr, data, msgSize );

	// send buffer
	ret = this->hostSocket->Send( bufStart, bufLen );

	if ( tempBuf )
		free( tempBuf );

	if ( ret == SOCKET_ERROR ) {
		// we might need to do something?
	}

	return ret;
}


//-----------------------------------------------------------------------------
// Callbacks