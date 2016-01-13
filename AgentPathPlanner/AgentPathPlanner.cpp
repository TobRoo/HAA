// AgentPathPlanner.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"

#include "AgentPathPlanner.h"
#include "AgentPathPlannerVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//#define DEBUG_DUMPPATH
//#define DEBUG_DUMPMAP

#define round(val) floor((val) + 0.5f)

int CELLID( short u, short v ) { // convert coordinate into id (valid for coords -0x8FFF to 0x8FFF)
	int id;
	unsigned short a, b;
	a = u + 0x8FFF;
	b = v + 0x8FFF;
	memcpy( (void*)&id, (void*)&a, sizeof(short) );
	memcpy( ((byte*)&id) + sizeof(short), (void*)&b, sizeof(short) );
	return id;
}

//*****************************************************************************
// AgentPathPlanner

//-----------------------------------------------------------------------------
// Constructor	
AgentPathPlanner::AgentPathPlanner( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AgentPathPlanner, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentPathPlanner" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentPathPlanner_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentPathPlanner_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	//dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	dir[0][0] = 0; dir[0][1] = 1;
	dir[1][0] = 1; dir[1][1] = 0;
	dir[2][0] = 0; dir[2][1] = -1;
	dir[3][0] = -1; dir[3][1] = 0;

	//fromLookUp[4] = { 2, 3, 0, 1 };
	fromLookUp[0] = 2;
	fromLookUp[1] = 3;
	fromLookUp[2] = 0;
	fromLookUp[3] = 1;

	STATE(AgentPathPlanner)->initialSetup = 3;
	STATE(AgentPathPlanner)->parametersSet = false;
	STATE(AgentPathPlanner)->startDelayed = false;

	STATE(AgentPathPlanner)->updateId = -1;

	STATE(AgentPathPlanner)->visPathValid = false;

	STATE(AgentPathPlanner)->pfDirty = true;
	
	STATE(AgentPathPlanner)->waitingOnMap = 0;
	STATE(AgentPathPlanner)->waitingOnPF = 0;

	STATE(AgentPathPlanner)->haveTarget = false;
	STATE(AgentPathPlanner)->actionConv = nilUUID;

	STATE(AgentPathPlanner)->actionCompleteTime.time = 0; // unset

	STATE(AgentPathPlanner)->waitingOnAvatars = 0;

	this->DEBUGdirtyMapCount = 0;

	// Prepare callbacks
	this->callback[AgentPathPlanner_CBR_convMissionRegion] = NEW_MEMBER_CB(AgentPathPlanner,convMissionRegion);
	this->callback[AgentPathPlanner_CBR_convMapInfo] = NEW_MEMBER_CB(AgentPathPlanner,convMapInfo);
	this->callback[AgentPathPlanner_CBR_convMapRegion] = NEW_MEMBER_CB(AgentPathPlanner,convMapRegion);
	this->callback[AgentPathPlanner_CBR_convPFInfo] = NEW_MEMBER_CB(AgentPathPlanner,convPFInfo);
	this->callback[AgentPathPlanner_CBR_convAction] = NEW_MEMBER_CB(AgentPathPlanner,convAction);
	this->callback[AgentPathPlanner_CBR_convRequestAvatarLoc] = NEW_MEMBER_CB(AgentPathPlanner,convRequestAvatarLoc);
	this->callback[AgentPathPlanner_CBR_convGetAvatarList] = NEW_MEMBER_CB(AgentPathPlanner,convGetAvatarList);
	this->callback[AgentPathPlanner_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(AgentPathPlanner,convGetAvatarInfo);
}

//-----------------------------------------------------------------------------
// Destructor
AgentPathPlanner::~AgentPathPlanner() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	// free tiles
	while ( !this->tileRef.empty() ) {
		freeDynamicBuffer( this->tileRef.begin()->second );
		this->tileRef.erase( this->tileRef.begin() );
	}

}

//-----------------------------------------------------------------------------
// Configure

int AgentPathPlanner::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		RPC_WSTR rpc_wstr;
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		UuidToString( &STATE(AgentBase)->uuid, &rpc_wstr );
		sprintf_s( logName, "%s\\AgentPathPlanner %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentPathPlanner %.2d.%.2d.%.5d.%.2d", AgentPathPlanner_MAJOR, AgentPathPlanner_MINOR, AgentPathPlanner_BUILDNO, AgentPathPlanner_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentPathPlanner::start( char *missionFile ) {
	DataStream lds;

	if ( !STATE(AgentPathPlanner)->parametersSet ) { // delay start
		strcpy_s( STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), missionFile );
		STATE(AgentPathPlanner)->startDelayed = true; 
		return 0;
	}

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;
	
	STATE(AgentPathPlanner)->pfStateRef = newDynamicBuffer(sizeof(float)*3);

	// add target path
	float x[] = { -0.08f, 0.12f, -0.08f };
	float y[] = { 0.08f, 0.0f, -0.08f };
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( VIS_PATH_TARGET );
	lds.packInt32( 3 );
	lds.packData( x, 3*sizeof(float) );
	lds.packData( y, 3*sizeof(float) );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDPATH, lds.stream(), lds.length() );
	lds.unlock();

	// register as avatar watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_AVATAR );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request a list of avatars once DDBE_WATCH_TYPE notification is received

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentPathPlanner::stop() {

	freeDynamicBuffer( STATE(AgentPathPlanner)->mapDataRef );
	freeDynamicBuffer( STATE(AgentPathPlanner)->mapProcessedRef );
	freeDynamicBuffer( STATE(AgentPathPlanner)->mapBufRef );
	freeDynamicBuffer( STATE(AgentPathPlanner)->pfStateRef );

	if ( !this->frozen ) {
		// clear vis
		if ( STATE(AgentPathPlanner)->haveTarget ) {
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packInt32( VIS_OBJ_TARGET );
			this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_PATH_TARGET );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		if ( STATE(AgentPathPlanner)->visPathValid ) {
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packInt32( VIS_OBJ_CURRENT_PATH );
			this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packInt32( VIS_PATH_CURRENT_PATH );
			this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
			this->ds.unlock();

			STATE(AgentPathPlanner)->visPathValid = false;
		}
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentPathPlanner::step() {
	return AgentBase::step();
}

int AgentPathPlanner::configureParameters( DataStream *ds ) {
	ds->unpackUUID( &STATE(AgentPathPlanner)->ownerId );
	ds->unpackUUID( &STATE(AgentPathPlanner)->mapId );
	ds->unpackUUID( &STATE(AgentPathPlanner)->regionId );
	ds->unpackUUID( &STATE(AgentPathPlanner)->pfId );
	STATE(AgentPathPlanner)->maxLinear = ds->unpackFloat32();
	STATE(AgentPathPlanner)->maxRotation = ds->unpackFloat32();	
	STATE(AgentPathPlanner)->minLinear = ds->unpackFloat32();
	STATE(AgentPathPlanner)->minRotation = ds->unpackFloat32();

	Log.log( LOG_LEVEL_NORMAL, "AgentPathPlanner::configureParameters: ownerId %s mapId %s regionId %s pfId %s", 
		Log.formatUUID( LOG_LEVEL_NORMAL, &STATE(AgentPathPlanner)->ownerId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &STATE(AgentPathPlanner)->mapId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &STATE(AgentPathPlanner)->regionId ),
		Log.formatUUID( LOG_LEVEL_NORMAL, &STATE(AgentPathPlanner)->pfId ) );

	STATE(AgentPathPlanner)->initialSetup = 2; // need mission region info and map info

	// get mission region
	UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convMissionRegion, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentPathPlanner)->regionId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// get map info
	thread = this->conversationInitiate( AgentPathPlanner_CBR_convMapInfo, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentPathPlanner)->mapId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// register as watcher with map
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &STATE(AgentPathPlanner)->mapId );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// register as watcher for particle filters
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packInt32( DDB_PARTICLEFILTER );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->backup(); // initialSetup

	return 0;
}

int	AgentPathPlanner::finishConfigureParameters() {
	// allocate map data
	int mapDivisions = (int)floor(1/STATE(AgentPathPlanner)->mapResolution);

	STATE(AgentPathPlanner)->mapOffset[0] = -1;
	STATE(AgentPathPlanner)->mapOffset[1] = -1;
	STATE(AgentPathPlanner)->mapWidth = 2*MAX_CELL_PATH_LENGTH + 1 + 2;
	STATE(AgentPathPlanner)->mapHeight = 2*MAX_CELL_PATH_LENGTH + 1 + 2;
	STATE(AgentPathPlanner)->mapDataRef = newDynamicBuffer(sizeof(float)*STATE(AgentPathPlanner)->mapWidth*STATE(AgentPathPlanner)->mapHeight);
	STATE(AgentPathPlanner)->mapProcessedRef = newDynamicBuffer(sizeof(float)*STATE(AgentPathPlanner)->mapWidth*STATE(AgentPathPlanner)->mapHeight);
	STATE(AgentPathPlanner)->mapBufRef = newDynamicBuffer(sizeof(float)*STATE(AgentPathPlanner)->mapWidth*STATE(AgentPathPlanner)->mapHeight);

	STATE(AgentPathPlanner)->cellsRef = newDynamicBuffer(sizeof(TESTCELL)*STATE(AgentPathPlanner)->mapWidth*STATE(AgentPathPlanner)->mapHeight);

	STATE(AgentPathPlanner)->parametersSet = true;

	if ( STATE(AgentPathPlanner)->startDelayed ) {
		STATE(AgentPathPlanner)->startDelayed = false;
		this->start( STATE(AgentBase)->missionFile );
	}

	this->backup(); // initialSetup

	return 0;
}

int	AgentPathPlanner::setTarget( float x, float y, float r, char useRotation, UUID *thread ) {

	if ( !STATE(AgentBase)->started ) {

		// notify owner
		this->ds.reset();
		this->ds.packUUID( thread );
		this->ds.packChar( false );
		this->ds.packInt32( AvatarBase_Defs::TP_NOT_STARTED );
		this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
		this->ds.unlock();

		return 0;
	}

	Log.log( LOG_LEVEL_NORMAL, "AgentPathPlanner::setTarget: %f %f %f %d", x, y, r, useRotation ); 

	if ( STATE(AgentPathPlanner)->haveTarget ) {
		// clear vis target
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_OBJ_TARGET );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		// clear current actions
		this->ds.reset();
		this->ds.packInt32( 0 ); 
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_ACTION_CLEAR), this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
		this->ds.unlock();

		if ( STATE(AgentPathPlanner)->actionConv != nilUUID )
			this->conversationFinish( &STATE(AgentPathPlanner)->actionConv );

		// notify initiator
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentPathPlanner)->targetThread );
		this->ds.packChar( false );
		this->ds.packInt32( AvatarBase_Defs::TP_NEW_TARGET );
		this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );		
		this->ds.unlock();
	}

	STATE(AgentPathPlanner)->targetX = x;
	STATE(AgentPathPlanner)->targetY = y;
	STATE(AgentPathPlanner)->targetR = r;
	STATE(AgentPathPlanner)->useRotation = useRotation;
	STATE(AgentPathPlanner)->targetThread = *thread;

	STATE(AgentPathPlanner)->noPathCount = 0;

	STATE(AgentPathPlanner)->haveTarget = true;

	// set vis target
	this->ds.reset();
	this->ds.packUUID( this->getUUID() ); // agent
	this->ds.packInt32( VIS_OBJ_TARGET ); // objectId
	this->ds.packFloat32( x ); // x
	this->ds.packFloat32( y ); // y
	this->ds.packFloat32( r ); // r
	this->ds.packFloat32( 1 ); // s
	this->ds.packInt32( 1 ); // path count
	this->ds.packInt32( VIS_PATH_TARGET ); // path id
	this->ds.packFloat32( 0 ); // r
	this->ds.packFloat32( 1 ); // g
	this->ds.packFloat32( 1 ); // b
	this->ds.packFloat32( 1 ); // lineWidth
	this->ds.packBool( 0 ); // solid
	this->ds.packString( "Target" );

	this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDOBJECT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->goTarget();

	return 0;
}

int AgentPathPlanner::goTarget() {
	UUID thread;
	_timeb tb; // just a dummy

	STATE(AgentPathPlanner)->updateId++;

	// DEBUG
	if ( STATE(AgentPathPlanner)->updateId == 26 )
		int i = 0;

	
	// make sure we have the necessary info
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		if ( iA->second.retired == 1 )
			continue; // properly retired
		
		if ( !iA->second.ready )
			break;
	}

	if ( this->avatars.size() == 0 ||  iA != this->avatars.end() ) {
		Log.log( 0, "AgentPathPlanner::goTarget: avatar info not available yet, delaying" );

		return 0;
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::goTarget: mapDirty %d pfDirty %d avatars %d updateId %d", this->dirtyMapRegions.size(), STATE(AgentPathPlanner)->pfDirty, this->avatars.size(), STATE(AgentPathPlanner)->updateId ); 

	// clear actions, get the avatar to stop for now
	this->ds.reset();
	this->ds.packInt32( 0 );
	this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_ACTION_CLEAR), this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
	this->ds.unlock();
	
	STATE(AgentPathPlanner)->waitingOnMap = 0;
	if ( this->dirtyMapRegions.size() ) { // there are dirty regions
		thread = this->conversationInitiate( AgentPathPlanner_CBR_convMapRegion, DDB_REQUEST_TIMEOUT, &STATE(AgentPathPlanner)->updateId, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
		}
		
		std::list<DirtyRegion>::iterator iterR = this->dirtyMapRegions.begin();
		while ( iterR != this->dirtyMapRegions.end() ) {

			Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::goTarget: requesting map region %f %f %f %f", 
				iterR->x, iterR->y, iterR->w, iterR->h );

			this->ds.reset();
			this->ds.packUUID( &STATE(AgentPathPlanner)->mapId );
			this->ds.packFloat32( iterR->x );
			this->ds.packFloat32( iterR->y );
			this->ds.packFloat32( iterR->w );
			this->ds.packFloat32( iterR->h );
			this->ds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGREGION, this->ds.stream(), this->ds.length() );
			this->ds.unlock();

			STATE(AgentPathPlanner)->waitingOnMap++;
			iterR++;
		}
		this->dirtyMapRegions.clear();
	}

	// get avatar position if necessary
	if ( STATE(AgentPathPlanner)->pfDirty ) {
		STATE(AgentPathPlanner)->waitingOnPF++;

		thread = this->conversationInitiate( AgentPathPlanner_CBR_convPFInfo, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentPathPlanner)->pfId );
		this->ds.packInt32( DDBPFINFO_CURRENT );
		this->ds.packData( &tb, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	if ( !STATE(AgentPathPlanner)->waitingOnPF && this->checkArrival() ) {
		return 0; // we're done!
	}

	// get avatar locations
	STATE(AgentPathPlanner)->waitingOnAvatars = 0;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		iA->second.locValid = false;

		if ( iA->second.retired == 1 )
			continue; // properly retired, ignore
		if ( iA->second.pf == STATE(AgentPathPlanner)->pfId ) 
			continue; // this is our owner, ignore

		// request PF
		this->ds.reset();
		this->ds.packUUID( (UUID *)&iA->first );
		this->ds.packInt32( STATE(AgentPathPlanner)->updateId );
		thread = this->conversationInitiate( AgentPathPlanner_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		if ( thread == nilUUID ) {
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID( &iA->second.pf );
		this->ds.packInt32( DDBPFINFO_CURRENT );
		this->ds.packData( &tb, sizeof(_timeb) ); // dummy
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		STATE(AgentPathPlanner)->waitingOnAvatars++;
	}

	if ( !STATE(AgentPathPlanner)->waitingOnMap && !STATE(AgentPathPlanner)->waitingOnPF && !STATE(AgentPathPlanner)->waitingOnAvatars )
		this->planPath();

	return 0;
}

int AgentPathPlanner::checkArrival() {
	int arrived = 1;
	float *pfState = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->pfStateRef);

	if ( fabs(pfState[0] - STATE(AgentPathPlanner)->targetX) > LINEAR_BOUND )
		arrived = 0;
	if ( fabs(pfState[1] - STATE(AgentPathPlanner)->targetY) > LINEAR_BOUND )
		arrived = 0;
	if ( STATE(AgentPathPlanner)->useRotation ) {
		float dR = pfState[2] - STATE(AgentPathPlanner)->targetR;
		while ( dR > fM_PI ) dR -= fM_PI*2;
		while ( dR < -fM_PI ) dR += fM_PI*2;
	    if ( fabs(dR) > ROTATIONAL_BOUND )
			arrived = 0;
	}

	if ( arrived ) {
		STATE(AgentPathPlanner)->haveTarget = false;

		Log.log( LOG_LEVEL_NORMAL, "AgentPathPlanner::checkArrival: target reached" ); 
		
		// clear vis
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_OBJ_TARGET );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		if ( STATE(AgentPathPlanner)->visPathValid ) {
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packInt32( VIS_OBJ_CURRENT_PATH );
			this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packInt32( VIS_PATH_CURRENT_PATH );
			this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
			this->ds.unlock();

			STATE(AgentPathPlanner)->visPathValid = false;
		}

		// notify owner
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentPathPlanner)->targetThread );
		this->ds.packChar( true );
		this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
		this->ds.unlock();
	}

	return arrived;
}

int AgentPathPlanner::giveUp( int reason ) {

	STATE(AgentPathPlanner)->haveTarget = false;

	// clear vis
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packInt32( VIS_OBJ_TARGET );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	if ( STATE(AgentPathPlanner)->visPathValid ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_OBJ_CURRENT_PATH );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_PATH_CURRENT_PATH );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		STATE(AgentPathPlanner)->visPathValid = false;
	}
	
	// notify owner
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentPathPlanner)->targetThread );
	this->ds.packChar( false );
	this->ds.packInt32( reason );
	this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
	this->ds.unlock();

	return 0;
}



float AgentPathPlanner::getCell( float x, float y, bool useRound ) {
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int tileId;
	
	if ( useRound ) {
		x = round(x/STATE(AgentPathPlanner)->mapResolution);
		y = round(y/STATE(AgentPathPlanner)->mapResolution);
	} else {
		x = floor(x/STATE(AgentPathPlanner)->mapResolution);
		y = floor(y/STATE(AgentPathPlanner)->mapResolution);
	}
	tileSize = round(STATE(AgentPathPlanner)->mapTileSize/STATE(AgentPathPlanner)->mapResolution);

	mapTile::iterator iterTile;

	u = (int)floor(x/tileSize);
	i = (int)(x - u*tileSize);
	
	v = (int)floor(y/tileSize);
	j = (int)(y - v*tileSize);

	tileId = DDB_TILE_ID(u,v);
	iterTile = this->tileRef.find(tileId);
	if ( iterTile == this->tileRef.end() ) { // tile doesn't exist
		return 0.5f;
	}

	float *tile = (float *)getDynamicBuffer( iterTile->second );

	return tile[j+i*STATE(AgentPathPlanner)->mapStride];
}

int AgentPathPlanner::processMap() {
	//int kernalWidth = 7;
	//float kernalVals[] = { 0.05f, 0.1f, 0.2f, 0.3f, 0.2f, 0.1f, 0.05f };
	int kernalWidth = 5;
	float kernalVals[] = { 0.1f, 0.25f, 0.3f, 0.25f, 0.1f };
	float *kernal = kernalVals + kernalWidth/2;
	float *cell;
	int r, c, k;
	int startCX, startCY, endCX, endCY;

	float *mapData = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->mapDataRef);
	float *mapProcessed = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->mapProcessedRef);
	float *mapBuf = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->mapBufRef);

	// set map offset
	int aCX, aCY;
	float *pfState = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->pfStateRef);
	aCX = (int)floor(pfState[0] / STATE(AgentPathPlanner)->mapResolution);
	aCY = (int)floor(pfState[1] / STATE(AgentPathPlanner)->mapResolution);
	STATE(AgentPathPlanner)->mapOffset[0] = aCX - STATE(AgentPathPlanner)->mapWidth/2;
	STATE(AgentPathPlanner)->mapOffset[1] = aCY - STATE(AgentPathPlanner)->mapHeight/2;

	// copy map tile to mapData
	for ( c=0; c<STATE(AgentPathPlanner)->mapWidth; c++ ) {
		for ( r=0; r<STATE(AgentPathPlanner)->mapHeight; r++ ) {
			cell = mapData + r + c*STATE(AgentPathPlanner)->mapHeight;
			*cell = this->getCell( (c + STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapResolution, (r + STATE(AgentPathPlanner)->mapOffset[1])*STATE(AgentPathPlanner)->mapResolution, false );
		}
	}


	// map -> horizontal blur -> mapBuf
	// NOTE we are capping map values at 0.5f because we only want to blur the obstacles outwards
	for ( c=0; c<STATE(AgentPathPlanner)->mapWidth; c++ ) {
		for ( r=0; r<STATE(AgentPathPlanner)->mapHeight; r++ ) {
			cell = mapBuf + r + c*STATE(AgentPathPlanner)->mapHeight;
			*cell = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( c+k < 0 || c+k >= STATE(AgentPathPlanner)->mapWidth ) // we're outside the source
					*cell += kernal[k]*0.5f;
				else
					*cell += kernal[k]*min(0.5f,mapData[r + (c+k)*STATE(AgentPathPlanner)->mapHeight]);
			}
		}
	}


	// mapBuf -> vertical blur -> mapProcessed
	// NOTE we are also only taking the blurred value if it is lower than 0.5f and lower than the original map data, this should spead the obstacles while keeping the clear vacancies
	float blurVal;
	for ( c=0; c<STATE(AgentPathPlanner)->mapWidth; c++ ) {
		for ( r=0; r<STATE(AgentPathPlanner)->mapHeight; r++ ) {
			blurVal = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( r+k < 0 || r+k >= STATE(AgentPathPlanner)->mapHeight ) // we're outside the source
					blurVal += kernal[k]*0.5f;
				else
					blurVal += kernal[k]*mapBuf[r+k + c*STATE(AgentPathPlanner)->mapHeight];
			}
			if ( blurVal < 0.5f && blurVal < mapData[r + c*STATE(AgentPathPlanner)->mapHeight] )
				mapProcessed[r + c*STATE(AgentPathPlanner)->mapHeight] = blurVal;
			else 
				mapProcessed[r + c*STATE(AgentPathPlanner)->mapHeight] = mapData[r + c*STATE(AgentPathPlanner)->mapHeight];
		}
	}

	// draw on mission boundary (on both processed map and original map)
	startCX = (int)floor( STATE(AgentPathPlanner)->missionRegion.x / STATE(AgentPathPlanner)->mapResolution ) - STATE(AgentPathPlanner)->mapOffset[0];
	startCY = (int)floor( STATE(AgentPathPlanner)->missionRegion.y / STATE(AgentPathPlanner)->mapResolution ) - STATE(AgentPathPlanner)->mapOffset[1];
	endCX = (int)floor( (STATE(AgentPathPlanner)->missionRegion.x + STATE(AgentPathPlanner)->missionRegion.w) / STATE(AgentPathPlanner)->mapResolution ) - STATE(AgentPathPlanner)->mapOffset[0];
	endCY = (int)floor( (STATE(AgentPathPlanner)->missionRegion.y + STATE(AgentPathPlanner)->missionRegion.h) / STATE(AgentPathPlanner)->mapResolution ) - STATE(AgentPathPlanner)->mapOffset[1];
	c = startCX-1;
	k = endCX+1;
	for ( r=startCY-1; r<=endCY+1; r++ ) {
		if ( r < 0 )
			continue;
		if ( r >= STATE(AgentPathPlanner)->mapHeight )
			break;
		
		if ( c >= 0 && c < STATE(AgentPathPlanner)->mapWidth ) {
			mapData[r + c*STATE(AgentPathPlanner)->mapHeight] = 0;
			mapProcessed[r + c*STATE(AgentPathPlanner)->mapHeight] = 0;
		}
		if ( k >= 0 && k < STATE(AgentPathPlanner)->mapWidth ) {
			mapData[r + k*STATE(AgentPathPlanner)->mapHeight] = 0;
			mapProcessed[r + k*STATE(AgentPathPlanner)->mapHeight] = 0;
		}
	}
	r = startCY-1;
	k = endCY+1;
	for ( c=startCX; c<=endCX; c++ ) {
		if ( c < 0 )
			continue;
		if ( c >= STATE(AgentPathPlanner)->mapWidth )
			break;
		if ( r >= 0 && r < STATE(AgentPathPlanner)->mapHeight ) {
			mapData[r + c*STATE(AgentPathPlanner)->mapHeight] = 0;
			mapProcessed[r + c*STATE(AgentPathPlanner)->mapHeight] = 0;
		}
		if ( k >= 0 && k < STATE(AgentPathPlanner)->mapHeight ) {
			mapData[k + c*STATE(AgentPathPlanner)->mapHeight] = 0;
			mapProcessed[k + c*STATE(AgentPathPlanner)->mapHeight] = 0;
		}
	}

	// draw on other avatar positions
	float dx, dy;
	float avatarLoc[2];
	float avatarAvoidance, avatarAvoidanceSq;
	float ourRadius;
	std::map<UUID,AVATAR_INFO,UUIDless>::iterator iL;
	for ( iL = this->avatars.begin(); iL != this->avatars.end(); iL++ ) {
		if ( iL->second.pf == STATE(AgentPathPlanner)->pfId ) { // this is us
			ourRadius = iL->second.outerRadius;
			break;
		}
	}
	for ( iL = this->avatars.begin(); iL != this->avatars.end(); iL++ ) {
		if ( iL->second.pf == STATE(AgentPathPlanner)->pfId ) // this is us
			continue; // skip		
		if ( !iL->second.locValid )
			continue; // skip
		avatarLoc[0] = iL->second.x/STATE(AgentPathPlanner)->mapResolution;
		avatarLoc[1] = iL->second.y/STATE(AgentPathPlanner)->mapResolution;
		avatarAvoidance = (iL->second.outerRadius + ourRadius + AVATAR_AVOIDANCE_THRESHOLD)/STATE(AgentPathPlanner)->mapResolution;
		avatarAvoidanceSq = avatarAvoidance*avatarAvoidance;
		
		for ( c = (int)floor(avatarLoc[0] - avatarAvoidance); c <= (int)ceil(avatarLoc[0] + avatarAvoidance); c++ ) {
			if ( c < STATE(AgentPathPlanner)->mapOffset[0] )
				continue;
			if ( c >= STATE(AgentPathPlanner)->mapOffset[0] + STATE(AgentPathPlanner)->mapWidth )
				continue;
			for ( r = (int)floor(avatarLoc[1] - avatarAvoidance); r <= (int)ceil(avatarLoc[1] + avatarAvoidance); r++ ) {
				if ( r < STATE(AgentPathPlanner)->mapOffset[1] )
					continue;
				if ( r >= STATE(AgentPathPlanner)->mapOffset[1] + STATE(AgentPathPlanner)->mapHeight )
					continue;

				dx = c - avatarLoc[0];
				dy = r - avatarLoc[1];
				if ( dx*dx + dy*dy < avatarAvoidanceSq ) {
					mapData[(r-STATE(AgentPathPlanner)->mapOffset[1]) + (c-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] = 0.4f; // discourage, but don't forbid
					mapProcessed[(r-STATE(AgentPathPlanner)->mapOffset[1]) + (c-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] = 0;
				}
			}
		}
	}


#ifdef DEBUG_DUMPMAP
	{
		static int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "data\\dump\\mapdump%d.txt", count );
		float temp = mapData[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight];
		mapData[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight] = 0;
		

		if ( !fopen_s( &fp, namebuf, "w" ) ) {
			int r, c;
			for ( r=STATE(AgentPathPlanner)->mapHeight-1; r>=0; r-- ) {
				for ( c=0; c<STATE(AgentPathPlanner)->mapWidth; c++ ) {
					fprintf( fp, "%f\t", mapData[r + c*STATE(AgentPathPlanner)->mapHeight] );
				}
				fprintf( fp, "\n" );
			}
			fclose( fp );
		}
		count++;

		mapData[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight] = temp;
	}
	{
		static int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "data\\dump\\mapprocessed%d.txt", count );
		
		float temp = mapProcessed[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight];
		mapProcessed[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight] = 0;
		
		if ( !fopen_s( &fp, namebuf, "w" ) ) {
			int r, c;
			for ( r=STATE(AgentPathPlanner)->mapHeight-1; r>=0; r-- ) {
				for ( c=0; c<STATE(AgentPathPlanner)->mapWidth; c++ ) {
					fprintf( fp, "%f\t", mapProcessed[r + c*STATE(AgentPathPlanner)->mapHeight] );
				}
				fprintf( fp, "\n" );
			}
			fclose( fp );
		}
		count++;

		mapProcessed[(-STATE(AgentPathPlanner)->mapOffset[1] + aCY) + (-STATE(AgentPathPlanner)->mapOffset[0] + aCX)*STATE(AgentPathPlanner)->mapHeight] = temp;
	}
#endif

	return 0;
}

int AgentPathPlanner::updateWeightedPathLength( TESTCELL *cell, TESTCELL *cells, float *mapPtr ) {
	int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	int fromLookUp[4] = { 2, 3, 0, 1 };
	
	int i;
	float mapVal;

	TESTCELL *nCell;
	
	for ( i=0; i<4; i++ ) {
		nCell = &cells[(cell->y+dir[i][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (cell->x+dir[i][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
		if ( nCell->listed ) {
			mapVal = mapPtr[(cell->y+dir[i][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (cell->x+dir[i][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
			if ( nCell->wPathLength > cell->wPathLength + CELLWEIGHT(mapVal) ) {
				nCell->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
				nCell->fromDir = fromLookUp[i];
				updateWeightedPathLength( nCell, cells, mapPtr );
			}
		}
	}

	return 0;
}

int AgentPathPlanner::nextCell( TESTCELL *cell, float targetCX, float targetCY, TESTCELL *cells, std::multimap<float,TESTCELL*> *frontier, TESTCELL *bestCell, float *bestDistSq, float vacantThreshold, float *mapPtr ) {
	int i;
	float weight;
	float mapVal, distFactor;
	float xdist, ydist;
	
	TESTCELL *nCell;

	float curDistSq;

	if ( cell->fromDir == -1 ) { // this is the first cell so we have to list ourselves
		cell->listed = 1;

		xdist = targetCX - (cell->x + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
		ydist = targetCY - (cell->y + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
		*bestDistSq = xdist*xdist + ydist*ydist + 1; // add one because we want this to be immediately replaced
	}
	
	xdist = targetCX/STATE(AgentPathPlanner)->mapResolution - (cell->x + 0.5f);
	ydist = targetCY/STATE(AgentPathPlanner)->mapResolution - (cell->y + 0.5f);
	if ( cell->pathLength >= MAX_CELL_PATH_LENGTH 
	  || (*bestDistSq == 0 && cell->pathLength + sqrt(xdist*xdist + ydist*ydist) >= bestCell->pathLength) ) {
		return -1; // give up on this cell
	}

	xdist = targetCX - (cell->x + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
	ydist = targetCY - (cell->y + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
	curDistSq = xdist*xdist + ydist*ydist;
	if ( cell->x == (int)floor( targetCX / STATE(AgentPathPlanner)->mapResolution ) && cell->y == (int)floor( targetCY / STATE(AgentPathPlanner)->mapResolution ) ) {
		*bestCell = *cell;
		*bestDistSq = 0;
	
		return 0; // we made it!
	} else if ( curDistSq < *bestDistSq ) {
		*bestCell = *cell;
		*bestDistSq = curDistSq;
	}

	// check each direction
	for ( i=0; i<4; i++ ) {
		if ( i == cell->fromDir ) {
			continue;
		} else {
			mapVal = mapPtr[(cell->y+dir[i][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (cell->x+dir[i][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
			nCell = &cells[(cell->y+dir[i][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (cell->x+dir[i][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
			if ( mapVal <= vacantThreshold ) { // drive carefully
				continue;
			} else if ( nCell->listed ) { // already listed
				if ( nCell->wPathLength > cell->wPathLength + CELLWEIGHT(mapVal) ) {
					nCell->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
					nCell->fromDir = fromLookUp[i];
					updateWeightedPathLength( nCell, cells, mapPtr );
				}
			} else {
				xdist = targetCX - (cell->x + dir[i][0] + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
				ydist = targetCY - (cell->y + dir[i][1] + 0.5f)*STATE(AgentPathPlanner)->mapResolution;
				distFactor = 1/max( STATE(AgentPathPlanner)->mapResolution/2, sqrt(xdist*xdist + ydist*ydist));
				weight = mapVal * distFactor;
				nCell->x = cell->x + dir[i][0];
				nCell->y = cell->y + dir[i][1];
				nCell->pathLength = cell->pathLength + 1;
				nCell->wPathLength = cell->wPathLength + CELLWEIGHT(mapVal);
				nCell->fromDir = fromLookUp[i];
				nCell->listed = 1;
				frontier->insert( std::pair<float,TESTCELL*>(weight, nCell) );
			}
		}
	}

	return -1;
}

float AgentPathPlanner::calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, TESTCELL *cells, float vacantThreshold, float *mapPtr ) {
	float fullLen;
	float curLen = 0, newLen, vertLen, horzLen;
	float wlen = 0;
	float dx, dy;
	float fullx, fully;
	float mapVal;
	int vertEdge, vertStep, horzEdge, horzStep;

	TESTCELL *cell;

	fullx = endX - startX;
	fully = endY - startY;
	fullLen = sqrt( fullx*fullx + fully*fully );

	if ( fullx >= 0 ) {
		horzEdge = 1;
		horzStep = 1;
	} else {
		horzEdge = 0;
		horzStep = -1;
	}
	if ( fully >= 0 ) {
		vertEdge = 1;
		vertStep = 1;
	} else {
		vertEdge = 0;
		vertStep = -1;
	}

	while ( curLen < fullLen ) {
		cell = &cells[(cellY-STATE(AgentPathPlanner)->mapOffset[1]) + (cellX-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];

		// make sure this is a valid cell
		if ( !cell->listed )
			return -1; // this cell is no good

		mapVal = mapPtr[(cellY-STATE(AgentPathPlanner)->mapOffset[1]) + (cellX-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
			
		// figure out where we exit this cell
		// first assume we exit vertically
		if ( fully != 0 ) {
			dy = cellY + vertEdge - startY;
			dx = fullx * dy/fully;
			vertLen = dx*dx + dy*dy;
		} else {
			vertLen = 99999999.0f; // very big
		}
		// the assume we exit horizontally
		if ( fullx != 0 ) {
			dx = cellX + horzEdge - startX;
			dy = fully * dx/fullx;
			horzLen = dx*dx + dy*dy;
		} else {
			horzLen = 99999999.0f; // very big
		}

		// pick the smaller length
		if ( horzLen < vertLen ) {
			cellX += horzStep;
			newLen = sqrt( horzLen );
		} else {
			cellY += vertStep;
			newLen = sqrt( vertLen );
		}

		// make sure we don't pass the end point
		if ( newLen > fullLen )
			newLen = fullLen;

		// add the weight
		wlen += CELLWEIGHT(mapVal)*(newLen-curLen);
		if ( mapVal <= vacantThreshold )
			wlen += 10*STATE(AgentPathPlanner)->mapResolution; // big penalty

		curLen = newLen;
	}

	return wlen;
}

int AgentPathPlanner::shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, TESTCELL *cells, float vacantThreshold, float *mapPtr ) {
	TESTCELL *baseCell;
	TESTCELL *endCell;
	TESTCELL *prevCell;
	TESTCELL *bestCell = NULL;
	float lastWLength;
	float wLength;
	float interLength;
	int badCells;
	
	baseCell = &cells[(*baseCellY-STATE(AgentPathPlanner)->mapOffset[1]) + (*baseCellX-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
	endCell = &cells[(*baseCellY+dir[baseCell->fromDir][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (*baseCellX+dir[baseCell->fromDir][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
	
	if ( endCell->x == destCellX && endCell->y == destCellY ) { // done already
		*baseCellX = destCellX;
		*baseCellY = destCellY;
		*baseX = destX;
		*baseY = destY;
		return 0;
	}

	lastWLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cells, vacantThreshold, mapPtr );
	if ( lastWLength == -1 )
		return 0; // this should never happen

	interLength = 0;
	bestCell = endCell;
	badCells = 0;

	prevCell = endCell;
	while ( endCell->x != destCellX || endCell->y != destCellY ) {
		
		endCell = &cells[(endCell->y+dir[endCell->fromDir][1]-STATE(AgentPathPlanner)->mapOffset[1]) + (endCell->x+dir[endCell->fromDir][0]-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];

		if ( endCell->x == destCellX && endCell->y == destCellY ) { 
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, destX, destY, cells, vacantThreshold, mapPtr );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, destX, destY, cells, vacantThreshold, mapPtr );
		} else {
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cells, vacantThreshold, mapPtr );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, endCell->x + 0.5f, endCell->y + 0.5f, cells, vacantThreshold, mapPtr );
		}

		if ( wLength == -1 || (wLength - (lastWLength + interLength)) > 0.00000001f ) {
			badCells++;
			if ( badCells >= 10 ) // give up
				break;	
		} else {
			lastWLength = wLength;
			interLength = 0;
			bestCell = endCell;
			badCells = 0;
		}
		prevCell = endCell;
	}

	if ( badCells == 0 && endCell->x == destCellX && endCell->y == destCellY ) {
		*baseCellX = destCellX;
		*baseCellY = destCellY;
		*baseX = destX;
		*baseY = destY;
	} else {
		*baseCellX = bestCell->x;
		*baseCellY = bestCell->y;
		*baseX = bestCell->x + 0.5f;
		*baseY = bestCell->y + 0.5f;
	}

	return 0;
}

int AgentPathPlanner::nearbyVacantCell( int startX, int startY, int *endX, int *endY, float vacantThreshold, float *mapPtr ) {
	int radius;
	int maxRadius = (int)ceil(1/STATE(AgentPathPlanner)->mapResolution); // one meter
	int x, y;
	int r, c;
	float foundVal;
	int foundCount;

	for ( radius=2; radius<maxRadius; radius++ ) {
		foundVal = vacantThreshold;
		foundCount = 0; // randomize cell selection to prevent multiple stuck avatars from always picking the same spot
		
		// check top
		y = radius;
		for ( x=-radius; x<radius; x++ ) {
			c = startX + x - STATE(AgentPathPlanner)->mapOffset[0];
			r = startY + y - STATE(AgentPathPlanner)->mapOffset[1];
			if ( r < 0 || r >= STATE(AgentPathPlanner)->mapHeight || c < 0 || c >= STATE(AgentPathPlanner)->mapWidth )
				continue; // out of bounds
			if ( mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight] > foundVal && (apb->apbUniform01() <= 1.0f/(foundCount+1)) ) {
				foundCount++;
				foundVal = mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight];
				*endX = startX + x;
				*endY = startY + y;
			}
		}

		// check right
		x = radius;
		for ( y=radius; y>-radius; y-- ) {
			c = startX + x - STATE(AgentPathPlanner)->mapOffset[0];
			r = startY + y - STATE(AgentPathPlanner)->mapOffset[1];
			if ( r < 0 || r >= STATE(AgentPathPlanner)->mapHeight || c < 0 || c >= STATE(AgentPathPlanner)->mapWidth )
				continue; // out of bounds
			if ( mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight] > foundVal && (apb->apbUniform01() <= 1.0f/(foundCount+1)) ) {
				foundCount++;
				foundVal = mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight];
				*endX = startX + x;
				*endY = startY + y;
			}
		}

		// check bottom
		y = -radius;
		for ( x=radius; x>-radius; x-- ) {
			c = startX + x - STATE(AgentPathPlanner)->mapOffset[0];
			r = startY + y - STATE(AgentPathPlanner)->mapOffset[1];
			if ( r < 0 || r >= STATE(AgentPathPlanner)->mapHeight || c < 0 || c >= STATE(AgentPathPlanner)->mapWidth )
				continue; // out of bounds
			if ( mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight] > foundVal && (apb->apbUniform01() <= 1.0f/(foundCount+1)) ) {
				foundCount++;
				foundVal = mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight];
				*endX = startX + x;
				*endY = startY + y;
			}
		}

		// check left
		x = -radius;
		for ( y=-radius; y<radius; y++ ) {
			c = startX + x - STATE(AgentPathPlanner)->mapOffset[0];
			r = startY + y - STATE(AgentPathPlanner)->mapOffset[1];
			if ( r < 0 || r >= STATE(AgentPathPlanner)->mapHeight || c < 0 || c >= STATE(AgentPathPlanner)->mapWidth )
				continue; // out of bounds
			if ( mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight] > foundVal && (apb->apbUniform01() <= 1.0f/(foundCount+1)) ) {
				foundCount++;
				foundVal = mapPtr[r + c*STATE(AgentPathPlanner)->mapHeight];
				*endX = startX + x;
				*endY = startY + y;
			}
		}

		if ( foundVal != vacantThreshold ) { // found something
			return 0;
		}
	}
	
	return 1; // hopelessly stuck
}

int AgentPathPlanner::planPath() {
	float startX, startY;
	float targX, targY;
	int startCX, startCY, endCX, endCY;
	bool nopath = false;
	bool stuck = false;
	float vacantThreshold; // never enter these cells
	float *mapPtr; // pointer to map data for planning path

	std::multimap<float,TESTCELL*>::iterator iter;
	std::map<int,TESTCELL*>::iterator iterCL;
	TESTCELL *testCell;
	TESTCELL bestCell;
	WAYPOINT wp;
	std::list<WAYPOINT> waypoints;
	std::list<WAYPOINT>::iterator iterWP;

	// process map
	this->processMap();

	float *mapProcessed = (float *)getDynamicBuffer( STATE(AgentPathPlanner)->mapProcessedRef );
	float *pfState = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->pfStateRef);

	Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: startPose %f %f %f endPose %f %f %f %d", pfState[0], pfState[1], pfState[2], STATE(AgentPathPlanner)->targetX, STATE(AgentPathPlanner)->targetY, STATE(AgentPathPlanner)->targetR, STATE(AgentPathPlanner)->useRotation ); 

	// plan path
	startX = pfState[0];
	startY = pfState[1];

	startCX = (int)floor(startX / STATE(AgentPathPlanner)->mapResolution);
	startCY = (int)floor(startY / STATE(AgentPathPlanner)->mapResolution);

	// check to see if we're stuck (in an off limits cell)
	vacantThreshold = VACANT_THRESHOLD;

	if ( mapProcessed[(startCY+0-STATE(AgentPathPlanner)->mapOffset[1]) + (startCX+1-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] <= vacantThreshold
	  && mapProcessed[(startCY+0-STATE(AgentPathPlanner)->mapOffset[1]) + (startCX-1-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] <= vacantThreshold
	  && mapProcessed[(startCY+1-STATE(AgentPathPlanner)->mapOffset[1]) + (startCX+0-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] <= vacantThreshold
	  && mapProcessed[(startCY-1-STATE(AgentPathPlanner)->mapOffset[1]) + (startCX+0-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight] <= vacantThreshold ) {
		// we're stuck!
		stuck = true;
		Log.log( 0, "AgentPathPlanner::planPath: we are stuck!" ); 

		// find a nearby vacant cell according to processed map
		if ( this->nearbyVacantCell( startCX, startCY, &endCX, &endCY, vacantThreshold, mapProcessed ) ) { // no cell!
			Log.log( 0, "AgentPathPlanner::planPath: no nearby vacant cell found!" ); 
			nopath = true;
		} else { // found cell
			targX = (endCX + 0.5f) * STATE(AgentPathPlanner)->mapResolution;
			targY = (endCY + 0.5f) * STATE(AgentPathPlanner)->mapResolution;

			Log.log( 0, "AgentPathPlanner::planPath: vacant cell at %f %f", targX, targY ); 
		}

		// set things up to plan a path to vacant cell
		vacantThreshold = 0.0f; // don't be deterred while trying to escape!
		mapPtr = (float *)getDynamicBuffer( STATE(AgentPathPlanner)->mapDataRef ); // use unprocessed map to plan path
	} else { // we're not stuck
		mapPtr = mapProcessed;

		targX = STATE(AgentPathPlanner)->targetX;
		targY = STATE(AgentPathPlanner)->targetY;
	}

	TESTCELL *cells = (TESTCELL *)getDynamicBuffer( STATE(AgentPathPlanner)->cellsRef );
	memset( cells, 0, sizeof(TESTCELL)*STATE(AgentPathPlanner)->mapWidth*STATE(AgentPathPlanner)->mapHeight );
	
	if ( !nopath ) { // we're not completely stuck
		endCX = (int)floor(targX / STATE(AgentPathPlanner)->mapResolution);
		endCY = (int)floor(targY / STATE(AgentPathPlanner)->mapResolution);

		testCell = &cells[(startCY-STATE(AgentPathPlanner)->mapOffset[1]) + (startCX-STATE(AgentPathPlanner)->mapOffset[0])*STATE(AgentPathPlanner)->mapHeight];
		testCell->x = startCX;
		testCell->y = startCY;
		testCell->fromDir = -1;
		testCell->pathLength = 0;
		testCell->wPathLength = 0;

		float bestDistSq = -1;
	
		this->nextCell( testCell, targX, targY, cells, &this->frontier, &bestCell, &bestDistSq, vacantThreshold, mapPtr );
		// try the next best cell
		while ( !this->frontier.empty() ) {
			iter = --this->frontier.end();
			testCell = iter->second;
			this->frontier.erase( iter );
			this->nextCell( testCell, targX, targY, cells, &this->frontier, &bestCell, &bestDistSq, vacantThreshold, mapPtr );
		}

		if ( bestCell.fromDir != -1 ) {  // have a path
			float cbaseX, cbaseY; // in cell coordinates
			float cstartX, cstartY; // in cell coordinates
			int baseCX, baseCY;
			cstartX = startX / STATE(AgentPathPlanner)->mapResolution;
			cstartY = startY / STATE(AgentPathPlanner)->mapResolution;
			baseCX = bestCell.x;
			baseCY = bestCell.y;
			if ( bestCell.x == endCX && bestCell.y == endCY ) {
				cbaseX = targX / STATE(AgentPathPlanner)->mapResolution;
				cbaseY = targY / STATE(AgentPathPlanner)->mapResolution;
			} else {
				cbaseX = baseCX + 0.5f;
				cbaseY = baseCY + 0.5f;
			}
			while ( baseCX != startCX || baseCY != startCY ) {
				wp.x = cbaseX*STATE(AgentPathPlanner)->mapResolution;
				wp.y = cbaseY*STATE(AgentPathPlanner)->mapResolution;
				waypoints.push_front( wp );
				shortestPath( &baseCX, &baseCY, &cbaseX, &cbaseY, startCX, startCY, cstartX, cstartY, cells, vacantThreshold, mapPtr );
			}
			wp.x = cbaseX*STATE(AgentPathPlanner)->mapResolution;
			wp.y = cbaseY*STATE(AgentPathPlanner)->mapResolution;
			waypoints.push_front( wp );
		} else { // we start and end in the same cell
			if ( startCX == endCX && startCY == endCY ) { // our destination is here
				wp.x = targX;
				wp.y = targY;
				waypoints.push_front( wp );
			} else { // no path
				nopath = true;
				/*float len;
				wp.x = STATE(AgentPathPlanner)->targetX - startX;
				wp.y = STATE(AgentPathPlanner)->targetY - startY;
				len = sqrt( wp.x*wp.x + wp.y*wp.y );

				wp.x = startX + wp.x/len * min( 0.15f, len ); // 15 cm
				wp.y = startY + wp.y/len * min( 0.15f, len ); // 15 cm
				waypoints.push_front( wp );*/
			}
			
			wp.x = startX;
			wp.y = startY;
			waypoints.push_front( wp );
		}
	} else { // we're completely stuck
			wp.x = startX;
			wp.y = startY;
			waypoints.push_front( wp );
	}

#ifdef DEBUG_DUMPPATH
	{
		int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
		int cellX, cellY, fromDir;
		int count = 0;
		char namebuf[256];
		FILE *fp;
		sprintf_s( namebuf, 256, "pathdump%d.txt", count );
		if ( !fopen_s( &fp, namebuf, "w" ) ) {
			cellX = bestCell.x;
			cellY = bestCell.y;
			fromDir = bestCell.fromDir;
			fprintf( fp, "%d %d\n", cellX, cellY );
			while ( fromDir != -1 ) {
		
				cellX += dir[fromDir][0];
				cellY += dir[fromDir][1];

				iterCL = cellListed.find(CELLID(cellX,cellY));
				fromDir = iterCL->second->fromDir;
				fprintf( fp, "%d %d\n", cellX, cellY );
			}
			std::list<WAYPOINT>::iterator iter = waypoints.begin();
			while ( iter != waypoints.end() ) {
				fprintf( fp, "%f %f\n", iter->x, iter->y );
				iter++;
			}
			fclose( fp );
		}
		count++;
	}
#endif

	// formulate actions
	std::list<ActionPair> actions;
	ActionPair action;
	float newR, curR, dR;
	float dx, dy, dD;

	// TODO if the end point is just a short way behind us consider backing up

	curR = pfState[2];
	iterWP = waypoints.begin();
	wp = *iterWP;
	iterWP++;
	if ( !nopath
	  && (fabs(pfState[0] - STATE(AgentPathPlanner)->targetX) > LINEAR_BOUND || fabs(pfState[1] - STATE(AgentPathPlanner)->targetY) > LINEAR_BOUND) ) { // move only if we need to
		do {
			Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: waypoint %f %f %f to %f %f", wp.x, wp.y, curR, iterWP->x, iterWP->y ); 

			// do rotation
			newR = atan2( iterWP->y - wp.y, iterWP->x - wp.x );
			dR = newR - curR;
			while ( dR > fM_PI ) dR -= 2*fM_PI;
			while ( dR < -fM_PI ) dR += 2*fM_PI;
			while ( fabs(dR) > STATE(AgentPathPlanner)->maxRotation ) {
				action.action = AvatarBase_Defs::AA_ROTATE;
				if ( dR > 0 ) {
					action.val = STATE(AgentPathPlanner)->maxRotation;
					dR -= STATE(AgentPathPlanner)->maxRotation;
				} else {
					action.val = -STATE(AgentPathPlanner)->maxRotation;
					dR += STATE(AgentPathPlanner)->maxRotation;
				}
				if ( fabs(action.val) > STATE(AgentPathPlanner)->minRotation/2 ) { // we're going to rotate
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minRotation ) { // approximate by minRotation
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minRotation : -STATE(AgentPathPlanner)->minRotation );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_ROTATE %f", action.val ); 
				} // else don't bother rotating
			}
			if ( fabs(dR) > 0.01f ) {
				action.action = AvatarBase_Defs::AA_ROTATE;
				action.val = dR;
				if ( fabs(action.val) > STATE(AgentPathPlanner)->minRotation/2 ) { // we're going to rotate
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minRotation ) { // approximate by minRotation
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minRotation : -STATE(AgentPathPlanner)->minRotation );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_ROTATE %f", action.val ); 
				} // else don't bother rotating	
			}
			curR = newR;

			// do move(s)
			dx = iterWP->x - wp.x;
			dy = iterWP->y - wp.y;
			dD = sqrt( dx*dx + dy*dy );
			while ( fabs(dD) > STATE(AgentPathPlanner)->maxLinear ) {
				action.action = AvatarBase_Defs::AA_MOVE;
				if ( dD > 0 ) {
					action.val = STATE(AgentPathPlanner)->maxLinear;
					dD -= STATE(AgentPathPlanner)->maxLinear;
				} else {
					action.val = -STATE(AgentPathPlanner)->maxLinear;
					dD += STATE(AgentPathPlanner)->maxLinear;
				}
				
				if ( fabs(action.val) > STATE(AgentPathPlanner)->minLinear/2 ) { // we're going to move
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minLinear ) { // approximate by minLinear
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minLinear : -STATE(AgentPathPlanner)->minLinear );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_MOVE %f", action.val ); 
				} // else don't bother moving
			}
			if ( fabs(dD) > 0.005f ) {
				action.action = AvatarBase_Defs::AA_MOVE;
				action.val = dD;
				if ( fabs(action.val) > STATE(AgentPathPlanner)->minLinear/2 ) { // we're going to move
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minLinear ) { // approximate by minLinear
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minLinear : -STATE(AgentPathPlanner)->minLinear );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_MOVE %f", action.val ); 
				} // else don't bother moving
			}
			wp = *iterWP;

			iterWP++;
		} while ( iterWP != waypoints.end() );
	}

	if ( !nopath
	  && STATE(AgentPathPlanner)->useRotation
	  && fabs(wp.x - STATE(AgentPathPlanner)->targetX) <= LINEAR_BOUND 
	  && fabs(wp.y - STATE(AgentPathPlanner)->targetY) <= LINEAR_BOUND ) { // do the final rotation if we're at our end point
		newR = STATE(AgentPathPlanner)->targetR;
		dR = newR - curR;
		while ( dR > fM_PI ) dR -= 2*fM_PI;
		while ( dR < -fM_PI ) dR += 2*fM_PI;
		if ( fabs(dR) > fM_PI ) // DEBUG
			dR = dR; 
		while ( fabs(dR) > STATE(AgentPathPlanner)->maxRotation ) {
			action.action = AvatarBase_Defs::AA_ROTATE;
			if ( dR > 0 ) {
				action.val = STATE(AgentPathPlanner)->maxRotation;
				dR -= STATE(AgentPathPlanner)->maxRotation;
			} else {
				action.val = -STATE(AgentPathPlanner)->maxRotation;
				dR += STATE(AgentPathPlanner)->maxRotation;
			}
			if ( fabs(action.val) > STATE(AgentPathPlanner)->minRotation/2 ) { // we're going to rotate
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minRotation ) { // approximate by minRotation
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minRotation : -STATE(AgentPathPlanner)->minRotation );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_ROTATE %f", action.val ); 
				} // else don't bother rotating
		}
		if ( fabs(dR) > 0.01f ) {
			action.action = AvatarBase_Defs::AA_ROTATE;
			action.val = dR;
			if ( fabs(action.val) > STATE(AgentPathPlanner)->minRotation/2 ) { // we're going to rotate
					if ( fabs(action.val) < STATE(AgentPathPlanner)->minRotation ) { // approximate by minRotation
						action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minRotation : -STATE(AgentPathPlanner)->minRotation );
					}
					actions.push_back( action );
					Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_ROTATE %f", action.val ); 
				} // else don't bother rotating
		}
		curR = newR;
	}

	// clear old path vis
	if ( STATE(AgentPathPlanner)->visPathValid ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_OBJ_CURRENT_PATH );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_PATH_CURRENT_PATH );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		STATE(AgentPathPlanner)->visPathValid = false;
	}

	if ( nopath ) { // we couldn't find a path, rotate randomly and wait a bit and hopefully more sensor readings will be processed
		Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: no path %d", STATE(AgentPathPlanner)->noPathCount ); 
		if ( STATE(AgentPathPlanner)->noPathCount > NOPATH_GIVEUP_COUNT ) {
			this->giveUp( AvatarBase_Defs::TP_TARGET_UNREACHABLE );
			return 0;
		}
		
		action.action = AA_ROTATE;
		action.val = (float)apb->apbUniform01() - 0.5f;
		if ( fabs(action.val) < STATE(AgentPathPlanner)->minRotation )
			action.val = (action.val > 0 ? STATE(AgentPathPlanner)->minRotation : -STATE(AgentPathPlanner)->minRotation );
		actions.push_back( action );
		Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_ROTATE %f", action.val ); 
		Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::planPath: AA_WAIT %f", NOPATH_DELAY ); 
		action.action = AA_WAIT;
		action.val = NOPATH_DELAY;
		actions.push_back( action );
		STATE(AgentPathPlanner)->noPathCount++;

	} else {
		STATE(AgentPathPlanner)->noPathCount = 0;
		
		// visualize path
		STATE(AgentPathPlanner)->visPathValid = true;
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packInt32( VIS_PATH_CURRENT_PATH );
		this->ds.packInt32( (int)waypoints.size() );
		for ( iterWP = waypoints.begin(); iterWP != waypoints.end(); iterWP++ ) // x's
			this->ds.packFloat32( iterWP->x );
		for ( iterWP = waypoints.begin(); iterWP != waypoints.end(); iterWP++ ) // y's
			this->ds.packFloat32( iterWP->y );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		this->ds.reset();
		this->ds.packUUID( this->getUUID() ); // agent
		this->ds.packInt32( VIS_OBJ_CURRENT_PATH ); // objectId
		this->ds.packFloat32( 0 ); // x
		this->ds.packFloat32( 0 ); // y
		this->ds.packFloat32( 0 ); // r
		this->ds.packFloat32( 1 ); // s
		this->ds.packInt32( 1 ); // path count
		this->ds.packInt32( VIS_PATH_CURRENT_PATH ); // path id
		if ( !stuck ) {
			this->ds.packFloat32( 1 ); // r
			this->ds.packFloat32( 0 ); // g
			this->ds.packFloat32( 0 ); // b
		} else {
			this->ds.packFloat32( 1 ); // r
			this->ds.packFloat32( 1 ); // g
			this->ds.packFloat32( 0 ); // b
		}
		this->ds.packFloat32( 1 ); // lineWidth
		this->ds.packBool( 0 ); // solid
		this->ds.packString( "Path" );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	// send actions
	std::list<ActionPair>::iterator iterA = actions.begin();
	int lastAction = -1;
	STATE(AgentPathPlanner)->actionsSent = 0;
	STATE(AgentPathPlanner)->actionConv = this->conversationInitiate( AgentPathPlanner_CBR_convAction, 60000 );
	if ( STATE(AgentPathPlanner)->actionConv == nilUUID ) {
		return 1;
	}
	int maxActions = ACTION_HORIZON;
	if ( stuck ) maxActions = ACTION_HORIZON_STUCK; // give a few extra actions to help us get unstuck
	while ( iterA != actions.end() && STATE(AgentPathPlanner)->actionsSent < maxActions ) {
		if ( STATE(AgentPathPlanner)->actionsSent > 1 && lastAction == AA_ROTATE && iterA->action == AA_MOVE )
			iterA->val = min( 0.05f, iterA->val ); // only move a small amount immediatly after a rotate to give sensors a chance to catch up

		// send action
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packUUID( &STATE(AgentPathPlanner)->actionConv );
		this->ds.packInt32( iterA->action );
		this->ds.packFloat32( iterA->val );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_ACTION_QUEUE), this->ds.stream(), this->ds.length(), &STATE(AgentPathPlanner)->ownerId );
		this->ds.unlock();

		STATE(AgentPathPlanner)->actionsSent++;

		if ( !stuck && STATE(AgentPathPlanner)->actionsSent > 1 && lastAction == AA_ROTATE && iterA->action == AA_MOVE )
			break; 
		lastAction = iterA->action;

		iterA++;
	}

	return 0;
}

int AgentPathPlanner::updateMap( float x, float y, float w, float h, float *data ) {
	int u, v; // tile coordinates
	int i, j, ii, jj; // cell coordinates
	int c, r; // data coordinates
	int stride; // data stride
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	float *tile;

	mapTile::iterator iterTile;

	// convert coords and tileSize into cell space
	x = round(x/STATE(AgentPathPlanner)->mapResolution);
	y = round(y/STATE(AgentPathPlanner)->mapResolution);
	w = round(w/STATE(AgentPathPlanner)->mapResolution);
	h = round(h/STATE(AgentPathPlanner)->mapResolution);
	tileSize = round(STATE(AgentPathPlanner)->mapTileSize/STATE(AgentPathPlanner)->mapResolution);

	stride = (int)h;

	lastTileId = -1;
	for ( c=0; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);

			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tileRef.find(tileId);				
				lastTileId = tileId;
			}

			if ( iterTile != this->tileRef.end() ) {
				tile = (float *)getDynamicBuffer( iterTile->second );
			} else { // this tile doesn't exist yet, so make it!
				UUID ref = newDynamicBuffer(sizeof(float)*STATE(AgentPathPlanner)->mapStride*STATE(AgentPathPlanner)->mapStride);
				tile = (float *)getDynamicBuffer( ref );
				if ( !tile ) {
					return 1; // malloc failed
				}
				for ( ii=0; ii<STATE(AgentPathPlanner)->mapStride; ii++ ) {
					for ( jj=0; jj<STATE(AgentPathPlanner)->mapStride; jj++ ) {
						tile[jj+ii*STATE(AgentPathPlanner)->mapStride] = 0.5f;
					}
				}

				this->tileRef[tileId] = ref;
				iterTile = this->tileRef.find(tileId);
			}
			tile[j+i*STATE(AgentPathPlanner)->mapStride] = data[r+c*stride];
		}
	}

	return 0;
}

int AgentPathPlanner::dirtyMap( DDBRegion *region ) {
	DirtyRegion reg;

	this->DEBUGdirtyMapCount++;

	if ( this->DEBUGdirtyMapCount == 369 )
		int debug = 0;

	reg.x = floor(region->x/STATE(AgentPathPlanner)->mapResolution)*STATE(AgentPathPlanner)->mapResolution;
	reg.y = floor(region->y/STATE(AgentPathPlanner)->mapResolution)*STATE(AgentPathPlanner)->mapResolution;
	reg.w = ceil((region->x + region->w)/STATE(AgentPathPlanner)->mapResolution)*STATE(AgentPathPlanner)->mapResolution - reg.x;
	reg.h = ceil((region->y + region->h)/STATE(AgentPathPlanner)->mapResolution)*STATE(AgentPathPlanner)->mapResolution - reg.y;
	
	apb->apbUuidCreate( &reg.key );

	this->_dirtyMap( &reg, this->dirtyMapRegions.begin(), this->dirtyMapRegions.end() );
	
	// delete any regions that are flagged
	std::list<DirtyRegion>::iterator iterLast;
	std::list<DirtyRegion>::iterator iter = this->dirtyMapRegions.begin();
	while ( iter != this->dirtyMapRegions.end() ) {
		iterLast = iter;
		iter++;
		if ( iterLast->w == 0 )
			this->dirtyMapRegions.erase( iterLast );
	}

	return 0;
}
int AgentPathPlanner::_dirtyMap( DirtyRegion *region, std::list<DirtyRegion>::iterator iter, std::list<DirtyRegion>::iterator insertAt ) {
	DirtyRegion cutL, cutT, cutB, cutR, grow;
	float Asx, Aex, Asy, Aey;
	float Bsx, Bex, Bsy, Bey;

	Bsx = region->x;
	Bex = region->x + region->w;
	Bsy = region->y;
	Bey = region->y + region->h;

	// see if we overlap with a current region
	while ( iter != this->dirtyMapRegions.end() ) {
		if ( iter->w == 0 || iter->key == region->key ) { // flagged for deletion || part of the same original region (we never merge with parts of the original region to prevent infinit loops)
			iter++;
			continue; 
		}

		Asx = iter->x;
		Aex = iter->x + iter->w;
		Asy = iter->y;
		Aey = iter->y + iter->h;

		// check if corner of A is inside B
		if ( Asx >= Bsx && Asx < Bex
		  && Asy >= Bsy && Asy < Bey )
			break;
		// check if corner of B is inside A
		if ( Bsx >= Asx && Bsx < Aex
		  && Bsy >= Asy && Bsy < Aey )
			break;
		// check if horizontal edges intersect vertical edges
		if ( Asy >= Bsy && Asy < Bey 
		  && ( (Bsx >= Asx && Bsx < Aex) 
		    || (Bex >= Asx && Bex < Aex) ) )
			break;
		if ( Aey >= Bsy && Aey < Bey 
		  && ( (Bsx >= Asx && Bsx < Aex) 
		    || (Bex >= Asx && Bex < Aex) ) )
			break;
		// check if vertical edges intersect horizontal edges
		if ( Asx >= Bsx && Asx < Bex 
		  && ( (Bsy >= Asy && Bsy < Aey) 
		    || (Bey >= Asy && Bey < Aey) ) )
			break;
		if ( Aex >= Bsx && Aex < Bex 
		  && ( (Bsy >= Asy && Bsy < Aey) 
		    || (Bey >= Asy && Bey < Aey) ) )
			break;

		iter++;
	}

	if ( iter == this->dirtyMapRegions.end() ) { // no overlap, add the region
		if ( insertAt != this->dirtyMapRegions.end() )
			this->dirtyMapRegions.insert( insertAt, *region );
		else
			this->dirtyMapRegions.push_back( *region );
		return 0;
	}

	// we overlap, figure out grows and cuts
	grow.x = min( Asx, Bsx );
	grow.w = max( Aex, Bex ) - grow.x;
	grow.y = min( Asy, Bsy );
	grow.h = max( Aey, Bey ) - grow.y;
	grow.key = region->key;

	if ( grow.x == iter->x && grow.y == iter->y && grow.w == iter->w && grow.h == iter->h ) {
		return 0; // we're entirely contained, nothing to do
	}

	if ( Bsx < Asx ) {	
		cutL.x = Bsx;
		cutL.w = Asx - Bsx;
		cutL.y = region->y;
		cutL.h = region->h;
		cutL.key = region->key;
	} else {
		cutL.w = cutL.h = 0;
	}
	if ( Bsy < Asy ) {
		cutB.x = max( Asx, Bsx );
		cutB.w = min( Aex, Bex ) - cutB.x;
		cutB.y = Bsy;
		cutB.h = Asy - Bsy;
		cutB.key = region->key;
	} else {
		cutB.w = cutB.h = 0;
	}
	if ( Aey < Bey ) {
		cutT.x = max( Asx, Bsx );
		cutT.w = min( Aex, Bex ) - cutT.x;
		cutT.y = Aey;
		cutT.h = Bey - Aey;
		cutT.key = region->key;
	} else {
		cutT.w = cutT.h = 0;
	}
	if ( Aex < Bex ) {
		cutR.x = Aex;
		cutR.w = Bex - Aex;
		cutR.y = region->y;
		cutR.h = region->h;
		cutR.key = region->key;
	} else {
		cutR.w = cutR.h = 0;
	}

	// decide to grow or cut
	// NOTE: make sure cuts are inserted before the next iter so that they don't try to overlap each other
	// unless they grow, in which case they should be inserted at the end
	float cutSize = cutL.w*cutL.h + cutB.w*cutB.h + cutT.w*cutT.h + cutR.w*cutR.h;
	if ( grow.w*grow.h - cutSize - iter->w*iter->h <= cutSize ) { // empty space < cut sections, so just grow
		iter->w = 0; // flag for deletion
		this->_dirtyMap( &grow, this->dirtyMapRegions.begin(), this->dirtyMapRegions.end() );
	} else { // add all cuts individually
		iter++; // start at the next region
		if ( cutL.w && cutL.h )
			this->_dirtyMap( &cutL, iter, iter );
		if ( cutB.w && cutB.h )
			this->_dirtyMap( &cutB, iter, iter );
		if ( cutT.w && cutT.h )
			this->_dirtyMap( &cutT, iter, iter );
		if ( cutR.w && cutR.h )
			this->_dirtyMap( &cutR, iter, iter );
	}

	return 0;
}

int AgentPathPlanner::notifyMap( UUID *item, char evt, char *data, int len ) {
	DDBRegion newRegion;
	DDBRegion activeR;

	if ( evt != DDBE_POG_UPDATE && evt != DDBE_POG_LOADREGION ) {
		return 0; // nothing to do
	}

	newRegion.x = ((float *)data)[0];
	newRegion.y = ((float *)data)[1];
	newRegion.w = ((float *)data)[2];
	newRegion.h = ((float *)data)[3];

	// trim area to active regions
	activeR.x = max( newRegion.x, STATE(AgentPathPlanner)->missionRegion.x );
	activeR.w = min( newRegion.x + newRegion.w, STATE(AgentPathPlanner)->missionRegion.x + STATE(AgentPathPlanner)->missionRegion.w ) - activeR.x;
	activeR.y = max( newRegion.y, STATE(AgentPathPlanner)->missionRegion.y );
	activeR.h = min( newRegion.y + newRegion.h, STATE(AgentPathPlanner)->missionRegion.y + STATE(AgentPathPlanner)->missionRegion.h ) - activeR.y;

	if ( activeR.w > 0 && activeR.h > 0 ) {
		// dirty map
		this->dirtyMap( &activeR );
	}

	return 0;
}

int AgentPathPlanner::notifyPF( UUID *item, char evt, char *data, int len ) {

	if ( STATE(AgentPathPlanner)->pfId == *item ) {
		STATE(AgentPathPlanner)->pfDirty = true;
	}

	return 0;
}

int AgentPathPlanner::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

	int offset;
	int type;
	char evt;
	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	offset = 4 + sizeof(UUID)*2 + 1;
	if ( evt == DDBE_WATCH_TYPE ) {
		if ( type == DDB_AVATAR ) {
			// request list of avatars
			UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}	
			sds.reset();
			sds.packUUID( this->getUUID() ); // dummy id 
			sds.packInt32( DDBAVATARINFO_ENUM );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_AVATAR ) {
		if ( evt == DDBE_ADD ) {
			// add avatar
			this->avatars[uuid].ready = false;
			this->avatars[uuid].start.time = 0;
			this->avatars[uuid].retired = 0;

			// request avatar info
			UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &uuid ); 
			sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAVATARINFO_RETIRE ) {
				// request avatar info
				UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				if ( thread == nilUUID ) {
					return 1;
				}
				sds.reset();
				sds.packUUID( &uuid ); 
				sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		} else if ( evt == DDBE_REM ) {
			// TODO
		}
	} else if ( type == DDB_MAP_PROBOCCGRID ) {
		this->notifyMap( &uuid, evt, data + offset, len - offset );
	} else {
		this->notifyPF( &uuid, evt, data + offset, len - offset );
	}
	lds.unlock();
	
	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentPathPlanner::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case AgentPathPlanner_MSGS::MSG_CONFIGURE:
		lds.setData( data, len );
		this->configureParameters( &lds );
		lds.unlock();
		break;
	case AgentPathPlanner_MSGS::MSG_SET_TARGET:
		{
			float x, y, r;
			char useRotation;
			UUID thread;
			lds.setData( data, len );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			useRotation = lds.unpackChar();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->setTarget( x, y, r, useRotation, &thread );
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool AgentPathPlanner::convMissionRegion( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convMissionRegion: request timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		this->ds.unlock();
		STATE(AgentPathPlanner)->missionRegion.x = this->ds.unpackFloat32();
		STATE(AgentPathPlanner)->missionRegion.y = this->ds.unpackFloat32();
		STATE(AgentPathPlanner)->missionRegion.w = this->ds.unpackFloat32();
		STATE(AgentPathPlanner)->missionRegion.h = this->ds.unpackFloat32();

		this->dirtyMap( &STATE(AgentPathPlanner)->missionRegion );

		STATE(AgentPathPlanner)->initialSetup--;
		if ( !STATE(AgentPathPlanner)->initialSetup ) {
			this->finishConfigureParameters();
		}
	} else {
		this->ds.unlock();
		// TODO try again?
	}

	return 0;
}


bool AgentPathPlanner::convMapInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convMapInfo: request timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		STATE(AgentPathPlanner)->mapTileSize = this->ds.unpackFloat32(); // tilesize
		STATE(AgentPathPlanner)->mapResolution = this->ds.unpackFloat32();
		STATE(AgentPathPlanner)->mapStride = this->ds.unpackInt32(); // stride
		this->ds.unlock();

		STATE(AgentPathPlanner)->initialSetup--;
		if ( !STATE(AgentPathPlanner)->initialSetup ) {
			this->finishConfigureParameters();
		}
	} else {
		this->ds.unlock();
		// TODO try again?
	}

	return 0;
}

bool AgentPathPlanner::convMapRegion( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	UUID thread;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convMapRegion: request timed out" );
		return 0; // end conversation
	}

	int convUpdateid = *(int *)conv->data;
	if ( convUpdateid != STATE(AgentPathPlanner)->updateId )
		return 0; // ignore

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackUUID( &thread );
	
	char res = this->ds.unpackChar();
	if ( res == DDBR_OK ) { // succeeded
		float x, y, w, h;
		int cw, ch;
		x = this->ds.unpackFloat32();
		y = this->ds.unpackFloat32();
		w = this->ds.unpackFloat32();
		h = this->ds.unpackFloat32();
		this->ds.unpackFloat32(); // resolution

		cw = (int)floor( w / STATE(AgentPathPlanner)->mapResolution );
		ch = (int)floor( h / STATE(AgentPathPlanner)->mapResolution );

		this->updateMap( x, y, w, h, (float *)this->ds.unpackData( cw*ch*4 ) );

	} else {

	}
	
	this->ds.unlock();

	STATE(AgentPathPlanner)->waitingOnMap--;

	if ( !STATE(AgentPathPlanner)->waitingOnMap && !STATE(AgentPathPlanner)->waitingOnPF && !STATE(AgentPathPlanner)->waitingOnAvatars ) {
		this->planPath();
	} else if ( !STATE(AgentPathPlanner)->waitingOnMap ) { // we have all the map updates
		return 0;
	} else { // we're still waiting on Map data, keep the conversation open
		return 1; 
	}

	return 0;
}

bool AgentPathPlanner::convPFInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convPFInfo: request timed out" );
		
		STATE(AgentPathPlanner)->waitingOnPF--;
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
		_timeb tb;
		float *pfState = (float*)getDynamicBuffer(STATE(AgentPathPlanner)->pfStateRef);

		if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		tb = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
		memcpy( pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );
		this->ds.unlock();

		if ( tb.time > STATE(AgentPathPlanner)->actionCompleteTime.time 
			|| (tb.time == STATE(AgentPathPlanner)->actionCompleteTime.time 
			&& tb.millitm >= STATE(AgentPathPlanner)->actionCompleteTime.millitm) ) { // data is recent enough
		
		
			STATE(AgentPathPlanner)->pfDirty = false;
			STATE(AgentPathPlanner)->waitingOnPF--;
			
			{
				_timeb curtb;
				apb->apb_ftime_s( &curtb );
				Log.log( 0, "AgentPathPlanner::convPFInfo: state x %f y %f r %f (tb=%d.%d, cur=%d.%d)", pfState[0], pfState[1], pfState[2], (int)tb.time, (int)tb.millitm, (int)curtb.time, (int)curtb.millitm );
			}
			
			if ( this->checkArrival() ) {
				return 0; // we made it!
			}

			if ( !STATE(AgentPathPlanner)->waitingOnMap && !STATE(AgentPathPlanner)->waitingOnPF && !STATE(AgentPathPlanner)->waitingOnAvatars )
				this->planPath();
		} else { // we need more recent data
			Log.log( LOG_LEVEL_NORMAL, "AgentPathPlanner::convPFInfo: requesting more recent data (in 250 ms)" );
			UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convPFInfo, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 0;
			}
			this->ds.reset();
			this->ds.packUUID( &STATE(AgentPathPlanner)->pfId );
			this->ds.packInt32( DDBPFINFO_CURRENT );
			this->ds.packData( &tb, sizeof(_timeb) );
			this->ds.packUUID( &thread );
			this->delayMessage( 250, this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}
	} else {
		this->ds.unlock();
		
		STATE(AgentPathPlanner)->waitingOnPF--;
		// TODO try again?
	}

	return 0;
}


bool AgentPathPlanner::convAction( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	UUID thread;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convAction: request timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackUUID( &thread ); // thread
	char result = this->ds.unpackChar();
	_timeb tb;
	if ( result == AAR_SUCCESS ) {
		tb = *(_timeb *)this->ds.unpackData(sizeof(_timeb));
	} else {
		this->ds.unpackInt32(); // reason
		tb = *(_timeb *)this->ds.unpackData(sizeof(_timeb));
	}
	this->ds.unlock();

	if ( thread != STATE(AgentPathPlanner)->actionConv )
		return 0; // ignore

	STATE(AgentPathPlanner)->actionCompleteTime = tb;

	STATE(AgentPathPlanner)->actionsSent--;
	
	Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::convAction: action complete, waiting for %d more actions (thread %s)", STATE(AgentPathPlanner)->actionsSent, Log.formatUUID( LOG_LEVEL_VERBOSE, &thread ) );

	if ( STATE(AgentPathPlanner)->actionsSent ) {
		return 1; // waiting for more
	} else {
		STATE(AgentPathPlanner)->actionConv = nilUUID;
		this->goTarget(); // plan the next step
		return 0;
	}
}

bool AgentPathPlanner::convRequestAvatarLoc( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	int infoFlags;
	char response;
	UUID aId;
	int updateId;

	this->ds.setData( (char *)conv->data, conv->dataLen );
	this->ds.unpackUUID( &aId );
	updateId = this->ds.unpackInt32();
	this->ds.unlock();

	if ( updateId != STATE(AgentPathPlanner)->updateId )
		return 0; // ignore

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentPathPlanner::convRequestAvatarLoc: request timed out" );
		
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != DDBPFINFO_CURRENT ) {
			this->ds.unlock();
			return 0; // what happened?
		}

		this->ds.unpackData( sizeof(_timeb) ); // discard time

		// unpack state
		float *state = (float *)this->ds.unpackData( sizeof(float)*3 );
		this->avatars[aId].x = state[0];
		this->avatars[aId].y = state[1];
		this->avatars[aId].r = state[2];

		this->avatars[aId].locValid = true;

		this->ds.unlock();
	} else {
		this->ds.unlock();
		Log.log( 0, "AgentPathPlanner::convRequestAvatarLoc: request failed %d", response );
	}

	STATE(AgentPathPlanner)->waitingOnAvatars--;

	if ( !STATE(AgentPathPlanner)->waitingOnMap && !STATE(AgentPathPlanner)->waitingOnPF && !STATE(AgentPathPlanner)->waitingOnAvatars )
		this->planPath();

	return 0;
}

bool AgentPathPlanner::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentPathPlanner::convGetAvatarList: timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID thread;
		UUID avatarId;
		UUID agentId;
		AgentType agentType;
		int i, count;

		if ( lds.unpackInt32() != DDBAVATARINFO_ENUM ) {
			lds.unlock();
			return 0; // what happened here?
		}
		
		count = lds.unpackInt32();
		Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::convGetAvatarList: recieved %d avatars", count );

		for ( i=0; i<count; i++ ) {
			lds.unpackUUID( &avatarId );
			lds.unpackString(); // avatar type
			lds.unpackUUID( &agentId );
			lds.unpackUUID( &agentType.uuid );
			agentType.instance = lds.unpackInt32();

			// add avatar
			this->avatars[avatarId].ready = false;
			this->avatars[avatarId].start.time = 0;
			this->avatars[avatarId].retired = 0;

			// request avatar info
			thread = this->conversationInitiate( AgentPathPlanner_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( (UUID *)&avatarId ); 
			sds.packInt32( DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		}

		lds.unlock();

	} else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool AgentPathPlanner::convGetAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentPathPlanner::convGetAvatarInfo: timed out" );
		return 0; // end conversation
	}

	UUID avId = *(UUID *)conv->data;
	
	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID pfId;
		char retired;
		float innerR, outerR;
		_timeb startTime, endTime;

		if ( this->ds.unpackInt32() != (DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD) ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		this->ds.unpackUUID( &pfId );
		innerR = this->ds.unpackFloat32();
		outerR = this->ds.unpackFloat32();
		startTime = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
		retired = this->ds.unpackChar();
		if ( retired )
			endTime = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "AgentPathPlanner::convGetAvatarInfo: recieved avatar (%s) pf id: %s", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&pfId) );

		this->avatars[avId].pf = pfId;
		this->avatars[avId].start = startTime;
		if ( retired )
			this->avatars[avId].end = endTime;
		this->avatars[avId].retired = retired;
		this->avatars[avId].innerRadius = innerR;
		this->avatars[avId].outerRadius = outerR;

		this->avatars[avId].ready = true;

		if ( STATE(AgentPathPlanner)->haveTarget )
			this->goTarget(); // try to go now

	} else {
		this->ds.unlock();

		// TODO try again?
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AgentPathPlanner::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AgentPathPlanner::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	AgentPathPlanner::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentPathPlanner);

	_WRITE_STATE_MAP( int, UUID, &this->tileRef );
	_WRITE_STATE_LIST( DirtyRegion, &this->dirtyMapRegions );
	
	_WRITE_STATE_MAP_LESS( UUID, AVATAR_INFO, UUIDless, &this->avatars );

	return AgentBase::writeState( ds, false );;
}

int	AgentPathPlanner::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentPathPlanner);

	_READ_STATE_MAP( int, UUID, &this->tileRef );
	_READ_STATE_LIST( DirtyRegion, &this->dirtyMapRegions );
	
	_READ_STATE_MAP( UUID, AVATAR_INFO, &this->avatars );

	return AgentBase::readState( ds, false );
}

int AgentPathPlanner::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	// clean up visualization
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packChar( 0 ); // keep paths
	this->sendMessage( this->hostCon, MSG_DDB_VIS_CLEAR_ALL, lds.stream(), lds.length() );
	lds.unlock();
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( VIS_PATH_CURRENT_PATH );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items	
		STATE(AgentPathPlanner)->pfStateRef = newDynamicBuffer(sizeof(float)*3);
		// request list of avatars
		UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();

		// dirty entire mission region
		this->dirtyMap( &STATE(AgentPathPlanner)->missionRegion );
	}

	return 0;
}

int AgentPathPlanner::writeBackup( DataStream *ds ) {

	// configuration
	ds->packUUID( &STATE(AgentPathPlanner)->ownerId );

	ds->packBool( STATE(AgentPathPlanner)->parametersSet );
	ds->packBool( STATE(AgentPathPlanner)->startDelayed );
	ds->packInt32( STATE(AgentPathPlanner)->initialSetup );

	// region
	ds->packUUID( &STATE(AgentPathPlanner)->regionId );
	ds->packData( &STATE(AgentPathPlanner)->missionRegion, sizeof(DDBRegion) );

	// map data
	ds->packUUID( &STATE(AgentPathPlanner)->mapId );
	ds->packFloat32( STATE(AgentPathPlanner)->mapTileSize );
	ds->packFloat32( STATE(AgentPathPlanner)->mapResolution );
	ds->packInt32( STATE(AgentPathPlanner)->mapStride );

	// particle filter
	ds->packUUID( &STATE(AgentPathPlanner)->pfId );

	// avatar config
	ds->packFloat32( STATE(AgentPathPlanner)->maxLinear );
	ds->packFloat32( STATE(AgentPathPlanner)->maxRotation );
	ds->packFloat32( STATE(AgentPathPlanner)->minLinear );
	ds->packFloat32( STATE(AgentPathPlanner)->minRotation );

	return AgentBase::writeBackup( ds );
}

int AgentPathPlanner::readBackup( DataStream *ds ) {
	DataStream lds;

	// configuration
	ds->unpackUUID( &STATE(AgentPathPlanner)->ownerId );

	STATE(AgentPathPlanner)->parametersSet = ds->unpackBool();
	STATE(AgentPathPlanner)->startDelayed = ds->unpackBool();
	STATE(AgentPathPlanner)->initialSetup = ds->unpackInt32();

	// region
	ds->unpackUUID( &STATE(AgentPathPlanner)->regionId );
	STATE(AgentPathPlanner)->missionRegion = *(DDBRegion *)ds->unpackData( sizeof(DDBRegion) );

	// map data
	ds->unpackUUID( &STATE(AgentPathPlanner)->mapId );
	STATE(AgentPathPlanner)->mapTileSize = ds->unpackFloat32();
	STATE(AgentPathPlanner)->mapResolution = ds->unpackFloat32();
	STATE(AgentPathPlanner)->mapStride = ds->unpackInt32();

	// particle filter
	ds->unpackUUID( &STATE(AgentPathPlanner)->pfId );

	// avatar config
	STATE(AgentPathPlanner)->maxLinear = ds->unpackFloat32();
	STATE(AgentPathPlanner)->maxRotation = ds->unpackFloat32();
	STATE(AgentPathPlanner)->minLinear = ds->unpackFloat32();
	STATE(AgentPathPlanner)->minRotation = ds->unpackFloat32();

	if ( STATE(AgentPathPlanner)->initialSetup == 3 ) {
		return 1; // not enough info to recovery
	} else if ( STATE(AgentPathPlanner)->initialSetup == 2
		     || STATE(AgentPathPlanner)->initialSetup == 1 ) {
		
		// we have tasks to take care of before we can resume
		apb->apbUuidCreate( &this->AgentPathPlanner_recoveryLock );
		this->recoveryLocks.push_back( this->AgentPathPlanner_recoveryLock );

		STATE(AgentPathPlanner)->initialSetup = 2; // need mission region info and map info

		// get mission region
		UUID thread = this->conversationInitiate( AgentPathPlanner_CBR_convMissionRegion, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( &STATE(AgentPathPlanner)->regionId );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RREGION, lds.stream(), lds.length() );
		lds.unlock();

		// get map info
		thread = this->conversationInitiate( AgentPathPlanner_CBR_convMapInfo, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}
		lds.reset();
		lds.packUUID( &STATE(AgentPathPlanner)->mapId );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
		lds.unlock();
	} else {
		this->finishConfigureParameters();
	}

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AgentPathPlanner *agent = (AgentPathPlanner *)vpAgent;

	if ( agent->configure() ) {
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();
	delete agent;

	return 0;
}

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentPathPlanner *agent = new AgentPathPlanner( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	HANDLE hThread;
	DWORD  dwThreadId;

	hThread = CreateThread( 
        NULL,                   // default security attributes
        0,                      // use default stack size  
        RunThread,				// thread function name
        agent,					// argument to thread function 
        0,                      // use default creation flags 
        &dwThreadId);			// returns the thread identifier 

	if ( hThread == NULL ) {
		delete agent;
		return 1;
	}

	return 0;
}

int Playback( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentPathPlanner *agent = new AgentPathPlanner( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	if ( agent->configure() ) {
		delete agent;
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();

	printf_s( "Playback: agent finished\n" );

	delete agent;

	return 0;
}


// CAgentPathPlannerDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAgentPathPlannerDLL, CWinApp)
END_MESSAGE_MAP()

CAgentPathPlannerDLL::CAgentPathPlannerDLL() {}

// The one and only CAgentPathPlannerDLL object
CAgentPathPlannerDLL theApp;

int CAgentPathPlannerDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentPathPlannerDLL ---\n"));
  return CWinApp::ExitInstance();
}