// AgentSensorCooccupancy.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\fImage.h"

#include "AgentSensorCooccupancy.h"
#include "AgentSensorCooccupancyVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\SupervisorSLAM\\SupervisorSLAMVersion.h"

#include <boost/math/special_functions/erf.hpp>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentSensorCooccupancy

//-----------------------------------------------------------------------------
// Constructor	
AgentSensorCooccupancy::AgentSensorCooccupancy( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AgentSensorCooccupancy, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentSensorCooccupancy" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentSensorCooccupancy_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentSensorCooccupancy_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(AgentSensorCooccupancy)->mapId );
	STATE(AgentSensorCooccupancy)->mapResolution = 0;

	STATE(AgentSensorCooccupancy)->readingNextId = 0;

	this->mapUpdate = NewImage( 64, 64 );

	this->sharedArraySize = 0;
	this->obsDensity = NULL;

	// Prepare callbacks
	this->callback[AgentSensorCooccupancy_CBR_convRequestMapInfo] = NEW_MEMBER_CB(AgentSensorCooccupancy,convRequestMapInfo);
	this->callback[AgentSensorCooccupancy_CBR_convRequestAvatarInfo] = NEW_MEMBER_CB(AgentSensorCooccupancy,convRequestAvatarInfo);
	this->callback[AgentSensorCooccupancy_CBR_convRequestPFInfo] = NEW_MEMBER_CB(AgentSensorCooccupancy,convRequestPFInfo);
	this->callback[AgentSensorCooccupancy_CBR_cbRepeatRPFInfo] = NEW_MEMBER_CB(AgentSensorCooccupancy,cbRepeatRPFInfo);
	this->callback[AgentSensorCooccupancy_CBR_convRequestMap] = NEW_MEMBER_CB(AgentSensorCooccupancy,convRequestMap);

}

//-----------------------------------------------------------------------------
// Destructor
AgentSensorCooccupancy::~AgentSensorCooccupancy() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	FreeImage( this->mapUpdate );

}

//-----------------------------------------------------------------------------
// Configure

int AgentSensorCooccupancy::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\AgentSensorCooccupancy %s.txt", logDirectory, timeBuf );

		//Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentSensorCooccupancy %.2d.%.2d.%.5d.%.2d", AgentSensorCooccupancy_MAJOR, AgentSensorCooccupancy_MINOR, AgentSensorCooccupancy_BUILDNO, AgentSensorCooccupancy_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentSensorCooccupancy::start( char *missionFile ) {

	if ( AgentBase::start( missionFile ) ) 
		return 1;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentSensorCooccupancy::stop() {

	// clean up readings.
	mapReadings::iterator iterR = this->readings.begin();
	while ( iterR != this->readings.end() ) {
		if ( !(iterR->second.waiting & WAITING_ON_PF) ) {
			freeDynamicBuffer( iterR->second.pfStateRef );
			freeDynamicBuffer( iterR->second.pfWeightRef );
		}
		if ( !(iterR->second.waiting & WAITING_ON_MAP) ) {
			freeDynamicBuffer( iterR->second.mapDataRef );
		}
		iterR++;
	}
	this->readings.clear();

	// clean up buffers

	if ( this->sharedArraySize ) {
		free( this->obsDensity );
		this->sharedArraySize = 0;
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentSensorCooccupancy::step() {
	return AgentBase::step();
}

int AgentSensorCooccupancy::configureParameters( DataStream *ds ) {
	UUID uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorCooccupancy)->ownerId = uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorCooccupancy)->mapId = uuid;

	// get map info
	UUID thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorCooccupancy)->mapId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->backup(); // mapId

	return 0;
}

int AgentSensorCooccupancy::addAvatarRadius( UUID *avatar, float radius ) {
	this->avatarRadius[ *avatar ] = radius;

	this->backup(); // avatarRadius

	return 0; // finished
}

int AgentSensorCooccupancy::getAvatarRadius( PROCESS_READING *pr ) {
	UUID thread;

	// get avatar radius
	mapAvatarRadius::iterator iterAR = this->avatarRadius.find( pr->avatar );
	if ( iterAR == this->avatarRadius.end() ) {
		pr->waiting |= WAITING_ON_RADIUS;
		// request avatar radius
		thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestAvatarInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( &pr->avatar );
		this->ds.packInt32( DDBAVATARINFO_RRADII );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::getAvatarRadius: requesting avatar radius for reading %d", pr->id );
	}

	return 0;
}

int AgentSensorCooccupancy::addReading( UUID *pf, _timeb *time ) {
	PROCESS_READING pr;
	UUID thread;

	if ( STATE(AgentSensorCooccupancy)->mapResolution == 0 ) {
		Log.log( 0, "AgentSensorCooccupancy::addReading: map info not available yet, fail reading" );

		// notify owner
		this->ds.reset();
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packUUID( pf );
		this->ds.packData( time, sizeof(_timeb) );
		this->ds.packChar( 0 );
		this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorCooccupancy)->ownerId );
		this->ds.unlock();

		return 1;
	}


	pr.id = STATE(AgentSensorCooccupancy)->readingNextId++;
	pr.waiting = 0;
	pr.pf = *pf;
	pr.time = *time;
	pr.retries = 0;

	// TEMP
	//Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::addReading: adding reading %d", pr.id );
	//pr.waiting = 0xffffffff;
	//this->readings[pr.id] = pr;
	//this->finishReading( &this->readings[pr.id], 1 );
	//return 0;

	// get owner of this pf
	mapPFAvatar::iterator itPA = this->pfAvatars.find( pr.pf );
	if ( itPA == this->pfAvatars.end() ) {
		pr.waiting |= WAITING_ON_AVATAR;
	} else {
		pr.avatar = itPA->second;
		this->getAvatarRadius( &pr );
	}

	// request PF
	pr.waiting |= WAITING_ON_PF;

	thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &pr.pf );
	int infoFlags = DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT;
	if ( pr.waiting & WAITING_ON_AVATAR )
		infoFlags |= DDBPFINFO_OWNER;
	this->ds.packInt32( infoFlags );
	this->ds.packData( &pr.time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// flag map
	pr.waiting |= WAITING_ON_MAP;

	// add reading
	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::addReading: adding reading %d", pr.id );
	this->readings[pr.id] = pr;

	return 0;
}

int AgentSensorCooccupancy::finishReading( PROCESS_READING *pr, char success ) {

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::finishReading: reading %d success == %d", pr->id, success );

	// notify owner
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &pr->pf );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packChar( success );
	this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorCooccupancy)->ownerId );
	this->ds.unlock();

	// clean up reading
	if ( !(pr->waiting & WAITING_ON_PF) ) {
		freeDynamicBuffer( pr->pfStateRef );
		freeDynamicBuffer( pr->pfWeightRef );
	}
	if ( !(pr->waiting & WAITING_ON_MAP) ) {
		freeDynamicBuffer( pr->mapDataRef );
	}

	this->readings.erase( pr->id );

	return 0;
}

int AgentSensorCooccupancy::requestMapRegion( PROCESS_READING *pr ) {

	float radius;
	int border;
	int p;

	float PFbound[4];
	int	  mapDivisions = (int)floor(1/STATE(AgentSensorCooccupancy)->mapResolution);
	
	float *pfState = (float *)getDynamicBuffer( pr->pfStateRef );

	// update PFbound
	// TODO ignore particles with very little weight?
	PFbound[0] = pfStateX(pfState,0);
	PFbound[1] = pfStateY(pfState,0);
	PFbound[2] = 0;
	PFbound[3] = 0;
	for ( p=1; p<pr->pfNum; p++ ) {
		if ( PFbound[0] > pfStateX(pfState,p) ) {
			PFbound[2] = PFbound[0] + PFbound[2] - pfStateX(pfState,p);
			PFbound[0] = pfStateX(pfState,p);
		} else if ( PFbound[0] + PFbound[2] < pfStateX(pfState,p) ) {
			PFbound[2] = pfStateX(pfState,p) - PFbound[0];
		}
		if ( PFbound[1] > pfStateY(pfState,p) ) {
			PFbound[3] = PFbound[1] + PFbound[3] - pfStateY(pfState,p);
			PFbound[1] = pfStateY(pfState,p);
		} else if ( PFbound[1] + PFbound[3] < pfStateY(pfState,p) ) {
			PFbound[3] = pfStateY(pfState,p) - PFbound[1];
		}
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*mapDivisions)/mapDivisions;
	PFbound[0] = floor(PFbound[0]*mapDivisions)/mapDivisions;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*mapDivisions)/mapDivisions;
	PFbound[1] = floor(PFbound[1]*mapDivisions)/mapDivisions;
	PFbound[3] -= PFbound[1];
	
	// prepare map update
	radius = this->avatarRadius[pr->avatar];
	border = (int)ceil(radius/STATE(AgentSensorCooccupancy)->mapResolution);
	pr->mapUpdateLoc[0] = PFbound[0] - border/(float)mapDivisions;
	pr->mapUpdateLoc[1] = PFbound[1] - border/(float)mapDivisions;
	pr->mapUpdateSize[0] = (int)(PFbound[2]*mapDivisions)+border*2;
	pr->mapUpdateSize[1] = (int)(PFbound[3]*mapDivisions)+border*2;

	// request map
	UUID thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestMap, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorCooccupancy)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorCooccupancy)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorCooccupancy)->mapResolution );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::requestMapRegion: request region for reading %d", pr->id );

	return 0;
}

int AgentSensorCooccupancy::processReading( PROCESS_READING *pr ) {
	int mapDivisions = (int)floor(1/STATE(AgentSensorCooccupancy)->mapResolution);

	int p;
	int r, c;
	float stateX, stateY;

	float minObsDensity; 

	float radius; // avatar radius
	
	radius = this->avatarRadius[pr->avatar];

	float *pfState = (float *)getDynamicBuffer( pr->pfStateRef );
	float *pfWeight = (float *)getDynamicBuffer( pr->pfWeightRef );
	float *mapData = (float *)getDynamicBuffer( pr->mapDataRef );
	
	// generate particle filter and map updates
	
	// prepare map update
	if ( mapUpdate->bufSize < (int)sizeof(float)*(pr->mapUpdateSize[0] * pr->mapUpdateSize[1]) ) {
		// allocate more space for the map update
		ReallocImage( mapUpdate, pr->mapUpdateSize[1], pr->mapUpdateSize[0] );
	} else {
		mapUpdate->rows = pr->mapUpdateSize[1];
		mapUpdate->cols = pr->mapUpdateSize[0];
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	float mapX = pr->mapUpdateLoc[0];
	float mapY = pr->mapUpdateLoc[1];


	// make sure arrays are big enough
	if ( this->sharedArraySize < pr->pfNum ) {
		if ( this->sharedArraySize ) {
			free( this->obsDensity );
		}
		this->sharedArraySize = pr->pfNum;
		this->obsDensity = (float *)malloc( sizeof(float)*this->sharedArraySize );
		if ( !this->obsDensity ) {
			this->sharedArraySize = 0;
			return 1; // malloc failed
		}
	}
	
	float sigma = radius/3;
	float invSqrt2SigSq = 1 / sqrt(2*sigma*sigma);
	float CcdfA, CcdfB;
	float RcdfA, RcdfB;
	float vacancy;

	// for each particle
	minObsDensity = 0;
	for ( p=0; p<pr->pfNum; p++ ) {
		stateX = pfStateX(pfState,p);
		stateY = pfStateY(pfState,p);

		// iterate through cells, calculate vacancy and compare against map compare against map
		obsDensity[p] = 0;
		CcdfB = 0.5f * ( 1 + boost::math::erf( (mapX - stateX) * invSqrt2SigSq ));
		for ( c=0; c<mapUpdate->cols; c++ ) {
			CcdfA = CcdfB;
			CcdfB = 0.5f * ( 1 + boost::math::erf( (mapX + (c+1)*STATE(AgentSensorCooccupancy)->mapResolution - stateX) * invSqrt2SigSq ));
			
			RcdfB = 0.5f * ( 1 + boost::math::erf( (mapY - stateY) * invSqrt2SigSq ));
			for ( r=0; r<mapUpdate->rows; r++ ) {
				RcdfA = RcdfB;
				RcdfB = 0.5f * ( 1 + boost::math::erf( (mapY + (r+1)*STATE(AgentSensorCooccupancy)->mapResolution - stateY) * invSqrt2SigSq ));

				vacancy = (CcdfB - CcdfA)/STATE(AgentSensorCooccupancy)->mapResolution * (RcdfB - RcdfA)/STATE(AgentSensorCooccupancy)->mapResolution;

				Px(mapUpdate,r,c) += pfWeight[p] * vacancy * 0.49f * MAP_UPDATE_STRENGTH;

				// total penalty
				if ( mapData[r+c*pr->mapHeight] < 0.5f ) {
					obsDensity[p] -= vacancy * (0.5f - mapData[r+c*pr->mapHeight]);
				}
			}
		}

		minObsDensity = min( minObsDensity, obsDensity[p] );
	}

	// compute observation density
	if ( minObsDensity < 0 ) {
		for ( p=0; p<pr->pfNum; p++ ) { // shift observation densities positive and add ambient
			obsDensity[p] += -minObsDensity + OBS_DENSITY_AMBIENT;
		}


		// submit particle filter update
		this->ds.reset();
		this->ds.packUUID( &pr->avatar );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packData( obsDensity, sizeof(float)*pr->pfNum );
		this->sendMessage( this->hostCon, MSG_DDB_APPLYPFCORRECTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	// submit map update
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentSensorCooccupancy)->mapId );
	this->ds.packFloat32( pr->mapUpdateLoc[0] );
	this->ds.packFloat32( pr->mapUpdateLoc[1] );
	this->ds.packFloat32( pr->mapUpdateSize[0]*STATE(AgentSensorCooccupancy)->mapResolution );
	this->ds.packFloat32( pr->mapUpdateSize[1]*STATE(AgentSensorCooccupancy)->mapResolution );
	this->ds.packData( this->mapUpdate->data, sizeof(float)*this->mapUpdate->rows*this->mapUpdate->cols );
	this->sendMessage( this->hostCon, MSG_DDB_APPLYPOGUPDATE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
		
	// finish reading
	this->finishReading( pr, 1 );
	
	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int AgentSensorCooccupancy::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case AgentSensorProcessing_MSGS::MSG_CONFIGURE:
		lds.setData( data, len );
		this->configureParameters( &lds );
		lds.unlock();
		break;	
	case AgentSensorProcessing_MSGS::MSG_ADD_READING:
		{
			_timeb tb;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			this->addReading( &uuid, &tb );
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool AgentSensorCooccupancy::convRequestMapInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestMapInfo: request timed out" );
		
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) {
		this->ds.unpackFloat32(); // tilesize
		STATE(AgentSensorCooccupancy)->mapResolution = this->ds.unpackFloat32();
		this->ds.unpackInt32(); // stride
		this->ds.unlock();

		this->backup(); // mapResolution
	} else {
		this->ds.unlock();
		Log.log( 0, "AgentSensorCooccupancy::convRequestMapInfo: request failed!" );
		UuidCreateNil( &STATE(AgentSensorCooccupancy)->mapId );
		return 0;
	}

	return 0;
}

bool AgentSensorCooccupancy::convRequestAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	mapReadings::iterator it;
	PROCESS_READING *pr;
	int infoFlags;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestAvatarInfo: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestAvatarInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::convRequestAvatarInfo: received info for reading %d", pr->id );

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
  
		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != DDBAVATARINFO_RRADII ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		float innerRadius = this->ds.unpackFloat32();
		this->ds.unpackFloat32();
		this->ds.unlock();

		this->addAvatarRadius( &pr->avatar, innerRadius );
		
		pr->waiting &= ~WAITING_ON_RADIUS;

		if ( !(pr->waiting & WAITING_ON_PF) ) { // ready to get the map now
			// request map data
			this->requestMapRegion( pr );
		}

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorCooccupancy::convRequestPFInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	mapReadings::iterator it;
	PROCESS_READING *pr;
	int infoFlags;
	int wantedFlags;
	char response;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestPFInfo: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestPFInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::convRequestPFInfo: received info for reading %d", pr->id );

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// handle info
		wantedFlags = DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT;
		if ( pr->waiting & WAITING_ON_AVATAR )
			wantedFlags |= DDBPFINFO_OWNER;

		infoFlags = lds.unpackInt32();
		if ( infoFlags != wantedFlags ) {
			lds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		pr->pfNum = lds.unpackInt32();
		pr->pfStateSize = lds.unpackInt32();
		pr->pfStateRef = newDynamicBuffer( pr->pfNum*pr->pfStateSize*sizeof(float) );
		if ( pr->pfStateRef == nilUUID ) {
			lds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		pr->pfWeightRef = newDynamicBuffer( pr->pfNum*sizeof(float) );
		if ( pr->pfWeightRef == nilUUID ) {
			lds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( pr->pfStateRef ), lds.unpackData( pr->pfNum*pr->pfStateSize*sizeof(float) ), pr->pfNum*pr->pfStateSize*sizeof(float) );
		memcpy( getDynamicBuffer( pr->pfWeightRef ), lds.unpackData( pr->pfNum*sizeof(float) ), pr->pfNum*sizeof(float) );

		pr->waiting &= ~WAITING_ON_PF;

		if ( pr->waiting & WAITING_ON_AVATAR ) { // get radius
			pr->waiting &= ~WAITING_ON_AVATAR;

			lds.unpackUUID( &pr->avatar );
			this->pfAvatars[ pr->pf ] = pr->avatar;

			this->getAvatarRadius( pr );

			this->backup(); // pfAvatars
		}

		lds.unlock();
		
		if ( !(pr->waiting & WAITING_ON_RADIUS) ) { // ready to get the map now
			// request map data
			this->requestMapRegion( pr );
		}

	} else if ( response == DDBR_TOOLATE )  {
		lds.unlock();

		// wait 250 ms and try again
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::convRequestPFInfo: response DDBR_TOOLATE (reading %d)", pr->id );
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorCooccupancy_CBR_cbRepeatRPFInfo, &pr->id, sizeof(int) );
	} else if ( response == DDBR_TOOEARLY ) {
		this->ds.unlock();
		this->finishReading( pr, -1 ); // permenant failure
	} else {
		lds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorCooccupancy::cbRepeatRPFInfo( void *vpid ) {
	UUID thread;
	mapReadings::iterator it;
	PROCESS_READING *pr;
	
	it = this->readings.find( *(int*)vpid );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorCooccupancy::cbRepeatRPFInfo: reading does not exist %d", *(int*)vpid );
		
		return 0; // end conversation
	}

	pr = &it->second;

	if ( pr->retries > 6 ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	pr->retries++;

	// request PF
	thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &pr->avatar );
	int infoFlags = DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT;
	if ( pr->waiting & WAITING_ON_AVATAR )
		infoFlags |= DDBPFINFO_OWNER;
	this->ds.packInt32( infoFlags );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

bool AgentSensorCooccupancy::convRequestMap( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	mapReadings::iterator it;
	PROCESS_READING *pr;
	char response;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestMap: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorCooccupancy::convRequestMap: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorCooccupancy::convRequestMap: recieved data for reading %d", pr->id );

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		float x, y, w, h;
		int cw, ch;
		x = this->ds.unpackFloat32();
		y = this->ds.unpackFloat32();
		w = this->ds.unpackFloat32();
		h = this->ds.unpackFloat32();
		STATE(AgentSensorCooccupancy)->mapResolution = this->ds.unpackFloat32();

		cw = (int)floor( w / STATE(AgentSensorCooccupancy)->mapResolution );
		ch = (int)floor( h / STATE(AgentSensorCooccupancy)->mapResolution );

		pr->waiting &= ~WAITING_ON_MAP;

		pr->mapDataRef = newDynamicBuffer(cw*ch*4);
		if ( pr->mapDataRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( pr->mapDataRef ), this->ds.unpackData( cw*ch*4 ), cw*ch*4 );
		this->ds.unlock();

		pr->mapHeight = ch;

		// see if we're ready to process
		if ( !pr->waiting )
			this->processReading( pr );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AgentSensorCooccupancy::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AgentSensorCooccupancy::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	AgentSensorCooccupancy::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentSensorCooccupancy);
	
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->pfAvatars );
	_WRITE_STATE_MAP_LESS( UUID, float, UUIDless, &this->avatarRadius );
	
	_WRITE_STATE_MAP( int, PROCESS_READING, &this->readings );

	return AgentBase::writeState( ds, false );;
}

int	AgentSensorCooccupancy::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentSensorCooccupancy);

	_READ_STATE_MAP( UUID, UUID, &this->pfAvatars );
	_READ_STATE_MAP( UUID, float, &this->avatarRadius );
	
	_READ_STATE_MAP( int, PROCESS_READING, &this->readings );

	return AgentBase::readState( ds, false );
}


int AgentSensorCooccupancy::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
	

		// make sure we have map resolution
		if ( STATE(AgentSensorCooccupancy)->mapResolution == 0 ) {
			// get map info
			UUID thread = this->conversationInitiate( AgentSensorCooccupancy_CBR_convRequestMapInfo, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &STATE(AgentSensorCooccupancy)->mapId );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	return 0;
}

int AgentSensorCooccupancy::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(AgentSensorCooccupancy)->ownerId );

	ds->packUUID( &STATE(AgentSensorCooccupancy)->mapId );
	ds->packFloat32( STATE(AgentSensorCooccupancy)->mapResolution );
	
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->pfAvatars );
	_WRITE_STATE_MAP_LESS( UUID, float, UUIDless, &this->avatarRadius );

	return AgentBase::writeBackup( ds );
}

int AgentSensorCooccupancy::readBackup( DataStream *ds ) {
	
	ds->unpackUUID( &STATE(AgentSensorCooccupancy)->ownerId );

	ds->unpackUUID( &STATE(AgentSensorCooccupancy)->mapId );
	STATE(AgentSensorCooccupancy)->mapResolution = ds->unpackFloat32();
	
	_READ_STATE_MAP( UUID, UUID, &this->pfAvatars );
	_READ_STATE_MAP( UUID, float, &this->avatarRadius );

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AgentSensorCooccupancy *agent = (AgentSensorCooccupancy *)vpAgent;

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
	AgentSensorCooccupancy *agent = new AgentSensorCooccupancy( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AgentSensorCooccupancy *agent = new AgentSensorCooccupancy( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CAgentSensorCooccupancyDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAgentSensorCooccupancyDLL, CWinApp)
END_MESSAGE_MAP()

CAgentSensorCooccupancyDLL::CAgentSensorCooccupancyDLL() {}

// The one and only CAgentSensorCooccupancyDLL object
CAgentSensorCooccupancyDLL theApp;

int CAgentSensorCooccupancyDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentSensorCooccupancyDLL ---\n"));
  return CWinApp::ExitInstance();
}