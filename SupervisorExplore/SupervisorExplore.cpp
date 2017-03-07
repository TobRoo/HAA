// SupervisorExplore.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "SupervisorExplore.h"
#include "SupervisorExploreVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
#include "..\\AvatarSurveyor\\AvatarSurveyorVersion.h"
#include "..\\AvatarPioneer\\AvatarPioneerVersion.h"
#include "..\\AvatarX80H\\AvatarX80HVersion.h"
#include "..\\AvatarSimulation\\AvatarSimulationVersion.h"

#include "..\\ExecutiveMission\\ExecutiveMissionVersion.h"

#define pfStateX( pr, i ) pr->pfState[i*3]
#define pfStateY( pr, i ) pr->pfState[i*3+1]
#define pfStateR( pr, i ) pr->pfState[i*3+2]

#define round(val) floor((val) + 0.5f)
#define intRound(val) ((int)((val) + 0.5f))

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//#define DO_VISUALIZE

//*****************************************************************************
// SupervisorExplore

//-----------------------------------------------------------------------------
// Constructor	
SupervisorExplore::SupervisorExplore( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	int i;

	// allocate state
	ALLOCATE_STATE( SupervisorExplore, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "SupervisorExplore" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(SupervisorExplore_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( SupervisorExplore_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	STATE(SupervisorExplore)->nextSectionId = 0;
	this->sectionIds = new std::list<int>;

	STATE(SupervisorExplore)->mapReady = false;

	STATE(SupervisorExplore)->totalCells = 0;

	STATE(SupervisorExplore)->puId = nilUUID;

	// Prepare callbacks
	this->callback[SupervisorExplore_CBR_cbPartitionUpdateTimeout] = NEW_MEMBER_CB(SupervisorExplore,cbPartitionUpdateTimeout);
	this->callback[SupervisorExplore_CBR_convGetRegion] = NEW_MEMBER_CB(SupervisorExplore,convGetRegion);
	this->callback[SupervisorExplore_CBR_convGetMapInfo] = NEW_MEMBER_CB(SupervisorExplore,convGetMapInfo);
	this->callback[SupervisorExplore_CBR_convGetMapData] = NEW_MEMBER_CB(SupervisorExplore,convGetMapData);
	this->callback[SupervisorExplore_CBR_convGetAvatarList] = NEW_MEMBER_CB(SupervisorExplore,convGetAvatarList);
	this->callback[SupervisorExplore_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(SupervisorExplore,convGetAvatarInfo);
	this->callback[SupervisorExplore_CBR_convGetAvatarPos] = NEW_MEMBER_CB(SupervisorExplore,convGetAvatarPos);
	this->callback[SupervisorExplore_CBR_convReachedPoint] = NEW_MEMBER_CB(SupervisorExplore,convReachedPoint);
	this->callback[SupervisorExplore_CBR_convAgentInfo] = NEW_MEMBER_CB(SupervisorExplore,convAgentInfo);

	STATE(SupervisorExplore)->partitionUpdateInProgress = 0;
	STATE(SupervisorExplore)->partitionUpdateTimer = nilUUID;

	this->sharedArrayGaussSize = 0;
	this->sharedArrayGauss = NULL;
	this->sharedArrayPixSize = 0;
	this->sharedArrayPix = NULL;

	for ( i=0; i<MAX_SECTIONS; i++ )
		STATE(SupervisorExplore)->sectionFree[i] = true;
}

//-----------------------------------------------------------------------------
// Destructor
SupervisorExplore::~SupervisorExplore() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}
		
	// free tiles
	while ( !this->tileRef.empty() ) {
		freeDynamicBuffer( this->tileRef.begin()->second );
		this->tileRef.erase( this->tileRef.begin() );
	}

	delete this->sectionIds;

}

//-----------------------------------------------------------------------------
// Configure

int SupervisorExplore::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\SupervisorExplore %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );



		Log.log( 0, "SupervisorExplore %.2d.%.2d.%.5d.%.2d", SupervisorExplore_MAJOR, SupervisorExplore_MINOR, SupervisorExplore_BUILDNO, SupervisorExplore_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
#ifdef	NO_LOGGING
	Log.setLogMode(LOG_MODE_OFF);
	Log.setLogLevel(LOG_LEVEL_NONE);
#endif	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int SupervisorExplore::start( char *missionFile ) {
	DataStream lds;

	// TEMP
	//Log.log( 0, "SupervisorExplore::start: SupervisorExplore disabled!" );
	//return 0;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// add cell path
	float x[] = { 0, 0, 0.5f };
	float y[] = { 0.5f, 0, 0 };
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( VIS_PATH_CELL );
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
	// NOTE: we request a list of avatars when DDBE_WATCH_TYPE notification is received

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SupervisorExplore::stop() {

	// clear dirty regions list
	this->dirtyMapRegions.clear();

	// free shared arrays
	if ( this->sharedArrayGaussSize )
		free( this->sharedArrayGauss );
	if ( this->sharedArrayPixSize )
		free( this->sharedArrayPix );

	// clean avatars
	mapAvatar::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		delete iA->second.cells;
	}
	this->avatars.clear();

	if ( !this->frozen ) {
		// clear vis
		this->visualizeClear();
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( VIS_PATH_CELL );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int SupervisorExplore::step() {
	//Log.log( 0, "SupervisorExplore::step: hello there" );
	return AgentBase::step();
}

int SupervisorExplore::addRegion( UUID *id, bool forbidden ) {

	// get the region
	RegionRequest rr;
	rr.id = *id;
	rr.forbidden = forbidden;
	UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetRegion, DDB_REQUEST_TIMEOUT, &rr, sizeof(RegionRequest) );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int SupervisorExplore::_addRegion( UUID *id, DDBRegion *region, bool forbidden ) {

	// add the region
	if ( !forbidden )
		this->regions[ *id ] = *region;
	else
		this->forbiddenRegions[ *id ] = *region;

	if ( STATE(SupervisorExplore)->mapReady ) {
		// initialize cells
		this->initializeRegion( id, forbidden );

		this->updatePartitions( true );
	}

	this->backup(); // regions

	return 0;
}

int SupervisorExplore::initializeRegion( UUID *id, bool forbidden ) {
	float x, y, w, h;
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int tileId, lastTileId;
	UUID ref;
	CELL *tile;
	mapRegions::iterator region;
	int ii, jj; // loop vars
	CELLCOORD cellCoord;

	mapTile::iterator iterTile;

	// get the region
	if ( !forbidden ) {
		region = this->regions.find( *id );

		if ( region == this->regions.end() )
			return 1; // region not found!
	} else {
		region = this->forbiddenRegions.find( *id );

		if ( region == this->forbiddenRegions.end() )
			return 1; // region not found!
	}

	// create new section
	int newId = this->getNewSectionId();
	STATE(SupervisorExplore)->sectionFree[newId] = false;
	this->sections[newId].clear();
	STATE(SupervisorExplore)->sectionDirty[newId] = true;

	// convert coords and tileSize into cell space
	x = floor(region->second.x/STATE(SupervisorExplore)->mapResolution);
	y = floor(region->second.y/STATE(SupervisorExplore)->mapResolution);
	w = ceil((region->second.x+region->second.w)/STATE(SupervisorExplore)->mapResolution) - x;
	h = ceil((region->second.y+region->second.h)/STATE(SupervisorExplore)->mapResolution) - y;
	tileSize = round(STATE(SupervisorExplore)->mapTileSize/STATE(SupervisorExplore)->mapResolution);

	// increment the total cell count
	if ( !forbidden ) {
		STATE(SupervisorExplore)->totalCells += (int)(w*h);
	} else {
		STATE(SupervisorExplore)->totalCells -= (int)(w*h);
	}

	lastTileId = -1;
	for ( c=0 ; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);
	
			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tileRef.find(tileId);
				if ( iterTile == this->tileRef.end() ) {
					// this tile doesn't exist yet, so make it!
					ref = newDynamicBuffer(sizeof(CELL)*STATE(SupervisorExplore)->mapStride*STATE(SupervisorExplore)->mapStride);
					tile = (CELL*)getDynamicBuffer( ref );
					if ( !tile ) {
						return 1; // malloc failed
					}
					
					// find neighbouring tiles
					CELL *tileUp, *tileRight, *tileDown, *tileLeft;
					int nId;
					mapTile::iterator iT;
					nId = DDB_TILE_ID(u,v+1);
					iT = this->tileRef.find(nId);
					if ( iT != this->tileRef.end() ) tileUp = (CELL *)getDynamicBuffer( iT->second );
					else tileUp = NULL;
					nId = DDB_TILE_ID(u+1,v);
					iT = this->tileRef.find(nId);
					if ( iT != this->tileRef.end() ) tileRight = (CELL *)getDynamicBuffer( iT->second );
					else tileRight = NULL;
					nId = DDB_TILE_ID(u,v-1);
					iT = this->tileRef.find(nId);
					if ( iT != this->tileRef.end() ) tileDown = (CELL *)getDynamicBuffer( iT->second );
					else tileDown = NULL;
					nId = DDB_TILE_ID(u-1,v);
					iT = this->tileRef.find(nId);
					if ( iT != this->tileRef.end() ) tileLeft = (CELL *)getDynamicBuffer( iT->second );
					else tileLeft = NULL;

					for ( ii=0; ii<STATE(SupervisorExplore)->mapStride; ii++ ) {
						for ( jj=0; jj<STATE(SupervisorExplore)->mapStride; jj++ ) {
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].x = u*STATE(SupervisorExplore)->mapTileSize+ii*STATE(SupervisorExplore)->mapResolution;
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].y = v*STATE(SupervisorExplore)->mapTileSize+jj*STATE(SupervisorExplore)->mapResolution;
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].type = CT_OUTOFBOUNDS;
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].section = -1;
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].partition = -1;
							tile[jj+ii*STATE(SupervisorExplore)->mapStride].dirty = false;

							// set neighbours
							// up
							if ( jj == STATE(SupervisorExplore)->mapStride - 1 ) {
								if ( tileUp == NULL ) {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[0] = NULL;
								} else  {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[0] = &tileUp[0+ii*STATE(SupervisorExplore)->mapStride];
									tileUp[0+ii*STATE(SupervisorExplore)->mapStride].neighbour[2] = &tile[jj+ii*STATE(SupervisorExplore)->mapStride];
								}
							} else {
								tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[0] = &tile[(jj+1)+ii*STATE(SupervisorExplore)->mapStride];
							}	

							// right
							if ( ii == STATE(SupervisorExplore)->mapStride - 1 ) {
								if ( tileRight == NULL ) {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[1] = NULL;
								} else {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[1] = &tileRight[jj+0*STATE(SupervisorExplore)->mapStride];
									tileRight[jj+0*STATE(SupervisorExplore)->mapStride].neighbour[3] = &tile[jj+ii*STATE(SupervisorExplore)->mapStride];
								}
							} else {
								tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[1] = &tile[jj+(ii+1)*STATE(SupervisorExplore)->mapStride];
							}

							// down
							if ( jj == 0 ) {
								if ( tileDown == NULL ) {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[2] = NULL;
								} else {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[2] = &tileDown[(STATE(SupervisorExplore)->mapStride-1)+ii*STATE(SupervisorExplore)->mapStride];
									tileDown[(STATE(SupervisorExplore)->mapStride-1)+ii*STATE(SupervisorExplore)->mapStride].neighbour[0] = &tile[jj+ii*STATE(SupervisorExplore)->mapStride];
								}
							} else {
								tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[2] = &tile[(jj-1)+ii*STATE(SupervisorExplore)->mapStride];
							}	

							// left
							if ( ii == 0 ) {
								if ( tileLeft == NULL ) {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[3] = NULL;
								} else {
									tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[3] = &tileLeft[jj+(STATE(SupervisorExplore)->mapStride-1)*STATE(SupervisorExplore)->mapStride];
									tileLeft[jj+(STATE(SupervisorExplore)->mapStride-1)*STATE(SupervisorExplore)->mapStride].neighbour[1] = &tile[jj+ii*STATE(SupervisorExplore)->mapStride];
								}
							} else {
								tile[jj+ii*STATE(SupervisorExplore)->mapStride].neighbour[3] = &tile[jj+(ii-1)*STATE(SupervisorExplore)->mapStride];
							}
						}
					}	

					this->tileRef[tileId] = ref;
				} else {
					tile = (CELL *)getDynamicBuffer( iterTile->second );
				}

				lastTileId = tileId;
			}

			if ( !forbidden && tile[j+i*STATE(SupervisorExplore)->mapStride].type == CT_OUTOFBOUNDS ) {
				tile[j+i*STATE(SupervisorExplore)->mapStride].type = CT_UNKNOWN;
				tile[j+i*STATE(SupervisorExplore)->mapStride].section = newId;

				// add to cell list
				cellCoord.x = tile[j+i*STATE(SupervisorExplore)->mapStride].x;
				cellCoord.y = tile[j+i*STATE(SupervisorExplore)->mapStride].y;
				this->sections[newId][cellCoord] = &tile[j+i*STATE(SupervisorExplore)->mapStride];
			} else if ( forbidden && tile[j+i*STATE(SupervisorExplore)->mapStride].type != CT_UNREACHABLE ) {
				tile[j+i*STATE(SupervisorExplore)->mapStride].type = CT_UNREACHABLE;
				
				if ( tile[j+i*STATE(SupervisorExplore)->mapStride].section != -1 ) { // remove from section
					this->sections[tile[j+i*STATE(SupervisorExplore)->mapStride].section].erase(cellCoord);
					tile[j+i*STATE(SupervisorExplore)->mapStride].section = -1;
				}
			}
		}
	}

	if ( !this->sections[newId].empty() ) {
		// dirty all sections so that they can merge if necessary
		std::list<int>::iterator iterS = this->sectionIds->begin();
		while ( iterS != this->sectionIds->end() ) {
			STATE(SupervisorExplore)->sectionDirty[ *iterS ] = true;
			iterS++;
		}
		
		this->sectionIds->push_back( newId );
	} else {
		this->sections[newId].clear();
		STATE(SupervisorExplore)->sectionFree[newId] = true;
	}

	// dirty sections 
	if ( this->dirtyRegion( &region->second ) ) { // we have some active sections in this region
		// dirty map
		this->dirtyMap( &region->second );
	}

	return 0;
}

int SupervisorExplore::resetCellNeighbours() {
	CELL *tile;
	int i, j;
	short u, v;
	mapTile::iterator iT;
	mapTile::iterator iCur;

	CELL *tileUp, *tileRight, *tileDown, *tileLeft;
	int nId;

	int tileSize = STATE(SupervisorExplore)->mapStride;
	
	for ( iCur = this->tileRef.begin(); iCur != this->tileRef.end(); iCur++ ) {
		tile = (CELL *)getDynamicBuffer( iCur->second );

		DDB_REVERSE_TILE_ID( iCur->first, &u, &v );

		// find neighbouring tiles
		nId = DDB_TILE_ID(u,v+1);
		iT = this->tileRef.find(nId);
		if ( iT != this->tileRef.end() ) tileUp = (CELL *)getDynamicBuffer( iT->second );
		else tileUp = NULL;
		nId = DDB_TILE_ID(u+1,v);
		iT = this->tileRef.find(nId);
		if ( iT != this->tileRef.end() ) tileRight = (CELL *)getDynamicBuffer( iT->second );
		else tileRight = NULL;
		nId = DDB_TILE_ID(u,v-1);
		iT = this->tileRef.find(nId);
		if ( iT != this->tileRef.end() ) tileDown = (CELL *)getDynamicBuffer( iT->second );
		else tileDown = NULL;
		nId = DDB_TILE_ID(u-1,v);
		iT = this->tileRef.find(nId);
		if ( iT != this->tileRef.end() ) tileLeft = (CELL *)getDynamicBuffer( iT->second );
		else tileLeft = NULL;

		for ( i = 0; i < tileSize; i++ ) {
			for ( j = 0; j < tileSize; j++ ) {
				// set neighbours
				// up
				if ( j == tileSize - 1 ) {
					if ( tileUp == NULL ) {
						tile[j+i*tileSize].neighbour[0] = NULL;
					} else  {
						tile[j+i*tileSize].neighbour[0] = &tileUp[0+i*tileSize];
					}
				} else {
					tile[j+i*tileSize].neighbour[0] = &tile[(j+1)+i*tileSize];
				}	

				// right
				if ( i == tileSize - 1 ) {
					if ( tileRight == NULL ) {
						tile[j+i*tileSize].neighbour[1] = NULL;
					} else {
						tile[j+i*tileSize].neighbour[1] = &tileRight[j+0*tileSize];
					}
				} else {
					tile[j+i*tileSize].neighbour[1] = &tile[j+(i+1)*tileSize];
				}

				// down
				if ( j == 0 ) {
					if ( tileDown == NULL ) {
						tile[j+i*tileSize].neighbour[2] = NULL;
					} else {
						tile[j+i*tileSize].neighbour[2] = &tileDown[(tileSize-1)+i*tileSize];
					}
				} else {
					tile[j+i*tileSize].neighbour[2] = &tile[(j-1)+i*tileSize];
				}	

				// left
				if ( i == 0 ) {
					if ( tileLeft == NULL ) {
						tile[j+i*tileSize].neighbour[3] = NULL;
					} else {
						tile[j+i*tileSize].neighbour[3] = &tileLeft[j+(tileSize-1)*tileSize];
					}
				} else {
					tile[j+i*tileSize].neighbour[3] = &tile[j+(i-1)*tileSize];
				}
			}
		}
	}

	return 0;
}

CELL * SupervisorExplore::getCell( float x, float y, bool useRound ) {
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int tileId;
	
	if ( useRound ) {
		x = round(x/STATE(SupervisorExplore)->mapResolution);
		y = round(y/STATE(SupervisorExplore)->mapResolution);
	} else {
		x = floor(x/STATE(SupervisorExplore)->mapResolution);
		y = floor(y/STATE(SupervisorExplore)->mapResolution);
	}
	tileSize = round(STATE(SupervisorExplore)->mapTileSize/STATE(SupervisorExplore)->mapResolution);

	mapTile::iterator iterTile;

	u = (int)floor(x/tileSize);
	i = (int)(x - u*tileSize);
	
	v = (int)floor(y/tileSize);
	j = (int)(y - v*tileSize);

	tileId = DDB_TILE_ID(u,v);
	iterTile = this->tileRef.find(tileId);
	if ( iterTile == this->tileRef.end() ) { // tile doesn't exist
		return NULL;
	}

	CELL *tile = (CELL *)getDynamicBuffer( iterTile->second );

	return &tile[j+i*STATE(SupervisorExplore)->mapStride];
}

int SupervisorExplore::addMap( UUID *id ) {
	STATE(SupervisorExplore)->mapId = *id;

	// register as watcher with map
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &STATE(SupervisorExplore)->mapId );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->backup(); // mapId

	// get map info
	UUID thread = this->conversationInitiate(SupervisorExplore_CBR_convGetMapInfo, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(SupervisorExplore)->mapId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int SupervisorExplore::updateMap( float x, float y, float w, float h, float *data ) {
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int stride; // data stride
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	CELL *tile;

	mapTile::iterator iterTile;

	// convert coords and tileSize into cell space
	x = round(x/STATE(SupervisorExplore)->mapResolution);
	y = round(y/STATE(SupervisorExplore)->mapResolution);
	w = round(w/STATE(SupervisorExplore)->mapResolution);
	h = round(h/STATE(SupervisorExplore)->mapResolution);
	tileSize = round(STATE(SupervisorExplore)->mapTileSize/STATE(SupervisorExplore)->mapResolution);

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
				tile = (CELL *)getDynamicBuffer( iterTile->second );
				tile[j+i*STATE(SupervisorExplore)->mapStride].occupancy = data[r+c*stride] ;
			} else { // tile doesn't exist, therefor it isn't in a valid region probably this should never happen
				// nothing to do
			}
		}
	}

	return 0;
}

int SupervisorExplore::getNewSectionId() {
	int newId;
	for ( newId=STATE(SupervisorExplore)->nextSectionId; newId<STATE(SupervisorExplore)->nextSectionId + MAX_SECTIONS; newId++ ) {
		if ( STATE(SupervisorExplore)->sectionFree[newId<MAX_SECTIONS ? newId : newId-MAX_SECTIONS] == true )
			break;
	}

	if ( newId == STATE(SupervisorExplore)->nextSectionId + MAX_SECTIONS ) {
		Log.log( 0, "SupervisorExplore::getNewSectionId: ran out of section ids!" );	
		return 1;
	}
	STATE(SupervisorExplore)->nextSectionId = (newId + 1) % MAX_SECTIONS;

	return newId % MAX_SECTIONS;
}

// floods from the coordinate x,y and returns the new section id (-1 is invalid)
// unlisted - list of unlisted cells (either newly occupied or unoccupied and so were not added)
int SupervisorExplore::floodSection( CELL *seed, std::list<CELL*> *unlisted ) {
	int i;

	// create new section
	i = this->getNewSectionId();
	this->sections[i].clear();
	STATE(SupervisorExplore)->sectionFree[i] = false;
	STATE(SupervisorExplore)->sectionDirty[i] = true;


	// begin flood
	UUID floodId;
	std::list<CELL*> flood;
	apb->apbUuidCreate( &floodId );
	flood.push_back( seed );
	while ( flood.size() ) {
		this->_floodSectionRecurse( &flood, &floodId, i, &this->sections[i], unlisted );
	}

	return i;
}

int SupervisorExplore::_floodSectionRecurse( std::list<CELL*> *flood, UUID *floodId, int id, std::map<CELLCOORD,CELL*,CELLCOORDless> *section, std::list<CELL*> *unlisted ) {
	int i;
	CELL *cell;
	CELLCOORD cellCoord;

	cell = flood->front();
	flood->pop_front();
	
	if ( !cell 
	  || cell->section == id 
	  || cell->type == CT_OUTOFBOUNDS 
	  || cell->type == CT_OCCUPIED 
	  || cell->type == CT_UNREACHABLE ) {
		return 0;
	}

	cell->flooding = *floodId;

	if ( !STATE(SupervisorExplore)->sectionDirty[cell->section] ) { // we overran into a section that wasn't dirty! 
		STATE(SupervisorExplore)->sectionDirty[cell->section] = true;
		// move it to the end of the list to make sure we update it
		std::list<int>::iterator iterS = this->sectionIds->begin();
		while ( iterS != this->sectionIds->end() ) {
			if ( *iterS == cell->section )
				break;
			iterS++;
		}
		this->sectionIds->erase( iterS );
		this->sectionIds->push_back( cell->section );
	}

	if ( cell->type == CT_UNOCCUPIED ) {
		cell->section = id;
	} else { // CT_UNKNOWN
		if ( cell->occupancy > THRESHHOLD_UNOCCUPIED ) {
			cell->type = CT_UNOCCUPIED;
			cell->section = id;
			unlisted->push_back( cell );
			// include neighbours
			for ( i = 0; i < 4; i++ ) {
				if ( cell->neighbour[i] && cell->neighbour[i]->type == CT_UNKNOWN ) {
					cell->neighbour[i]->type = CT_UNOCCUPIED;
					if ( cell->neighbour[i]->section == id ) { // already been added to this section, remove it
						cellCoord.x = cell->neighbour[i]->x;
						cellCoord.y = cell->neighbour[i]->y;
						section->erase( cellCoord );
					}
					cell->section = id;
					unlisted->push_back( cell->neighbour[i] );
				}
			}
		} else if ( cell->occupancy < THRESHHOLD_OCCUPIED ) {
			cell->type = CT_OCCUPIED;
			cell->section = -1;
			unlisted->push_back( cell );
			// include neighbours
			for ( i = 0; i < 4; i++ ) {
				if ( cell->neighbour[i] && cell->neighbour[i]->type == CT_UNKNOWN ) {
					cell->neighbour[i]->type = CT_OCCUPIED;
					if ( cell->neighbour[i]->section == id ) { // already been added to this section, remove it
						cellCoord.x = cell->neighbour[i]->x;
						cellCoord.y = cell->neighbour[i]->y;
						section->erase( cellCoord );
					}
					cell->section = -1;
					unlisted->push_back( cell->neighbour[i] );
				}
			}
			return 0; // dead end
		} else if ( cell->activity > THRESHHOLD_ACTIVITY ) {
			// assume UNOCCUPIED?!
			cell->type = CT_UNOCCUPIED;
			cell->section = id;
			unlisted->push_back( cell );
	//	} else if ( this->checkCellNeighbours( x, y ) ) {
	//		// neighbours are known, don't worry about this cell
	//		cell->section = id;
	//		unlisted->push_back( cellCoord );
		} else {
			cell->section = id;
			cellCoord.x = cell->x;
			cellCoord.y = cell->y;
			(*section)[cellCoord] = cell;
		}
	}

	// flood to adjacent cells
	if ( cell->neighbour[0] 
	  && cell->neighbour[0]->flooding != *floodId
	  && cell->neighbour[0]->section != id 
	  && !(cell->neighbour[0]->type == CT_OUTOFBOUNDS 
	  || cell->neighbour[0]->type == CT_OCCUPIED 
	  || cell->neighbour[0]->type == CT_UNREACHABLE) ) {
		cell->neighbour[0]->flooding = *floodId;
		flood->push_back( cell->neighbour[0] ); // UP
	}
	  
	if ( cell->neighbour[1] 
	  && cell->neighbour[1]->flooding != *floodId
	  && cell->neighbour[1]->section != id 
	  && !(cell->neighbour[1]->type == CT_OUTOFBOUNDS 
	  || cell->neighbour[1]->type == CT_OCCUPIED 
	  || cell->neighbour[1]->type == CT_UNREACHABLE) ) {
		cell->neighbour[1]->flooding = *floodId;
		flood->push_back( cell->neighbour[1] ); // RIGHT
	}

	if ( cell->neighbour[2] 
	  && cell->neighbour[2]->flooding != *floodId
	  && cell->neighbour[2]->section != id 
	  && !(cell->neighbour[2]->type == CT_OUTOFBOUNDS 
	  || cell->neighbour[2]->type == CT_OCCUPIED 
	  || cell->neighbour[2]->type == CT_UNREACHABLE) ) {
		cell->neighbour[2]->flooding = *floodId;
		flood->push_back( cell->neighbour[2] ); // DOWN
	}

	if ( cell->neighbour[3] 
	  && cell->neighbour[3]->flooding != *floodId
	  && cell->neighbour[3]->section != id 
	  && !(cell->neighbour[3]->type == CT_OUTOFBOUNDS 
	  || cell->neighbour[3]->type == CT_OCCUPIED 
	  || cell->neighbour[3]->type == CT_UNREACHABLE) ) {
		cell->neighbour[3]->flooding = *floodId;
		flood->push_back( cell->neighbour[3] ); // LEFT
	}

	return 0;
}

int SupervisorExplore::checkCellNeighbours( float x, float y ) {
	CELL *up, *down, *left, *right;

	up = this->getCell( x, y + STATE(SupervisorExplore)->mapResolution );
	down = this->getCell( x, y - STATE(SupervisorExplore)->mapResolution );

	if ( up && down 
	  && (up->occupancy > THRESHHOLD_UNOCCUPIED || up->occupancy < THRESHHOLD_OCCUPIED) 
	  && (down->occupancy > THRESHHOLD_UNOCCUPIED || down->occupancy < THRESHHOLD_OCCUPIED) ) 
		return 1;
	
	left = this->getCell( x - STATE(SupervisorExplore)->mapResolution, y );
	right = this->getCell( x + STATE(SupervisorExplore)->mapResolution, y );
	
	if ( left && right 
	  && (left->occupancy > THRESHHOLD_UNOCCUPIED || left->occupancy < THRESHHOLD_OCCUPIED) 
	  && (right->occupancy > THRESHHOLD_UNOCCUPIED || right->occupancy < THRESHHOLD_OCCUPIED) ) 
	    return 1;

	return 0;
}

int SupervisorExplore::nextSearchCell( UUID *id ) {
	int i;
	CELLCOORD coord[4][3]; // store options for new cells
	int count[4][3];
	float dx, dy;
	int quad, range;
	int highQ, highR;
	float C, highC; // weighted count
	float cellsPer5SqM;
	UUID thread;

	float dist;
	AVATAR *avatar;
	mapAvatar::iterator iterAv = this->avatars.find( *id );
	std::list<CELLCOORD>::iterator iter;

	if ( iterAv == this->avatars.end() ) {
		Log.log( 0, "SimExplore::nextSearchCell: avatar not found!" ); // avatar on break
		return 1;
	}
	avatar = &iterAv->second;

	if ( this->avatarInfo.find( avatar->avatarId ) == this->avatarInfo.end() ) {
		Log.log( 0, "SimExplore::nextSearchCell: avatarInfo not found!" );
		return 1;
	}

	// make sure the avatar is ready
	std::map<UUID,int,UUIDless>::iterator iS = this->agentStatus.find( *id );
	if ( iS == this->agentStatus.end() 
	  || !( iS->second == DDBAGENT_STATUS_READY
	    || iS->second == DDBAGENT_STATUS_FREEZING
	    || iS->second == DDBAGENT_STATUS_FROZEN  
	    || iS->second == DDBAGENT_STATUS_THAWING) ) {

		return 0; // status not ok
	}

	memset( count, 0, sizeof(int)*4*3 );

	highC = 0;

	cellsPer5SqM = 5*(1/STATE(SupervisorExplore)->mapResolution)*(1/STATE(SupervisorExplore)->mapResolution);

	iter = avatar->cells->begin();
	while ( iter != avatar->cells->end() ) {
		dx = iter->x - avatar->search.x;
		dy = iter->y - avatar->search.y;
		if ( dx >= 0 ) {
			if ( dy >= 0 ) quad = 0;
			else quad = 1;
		} else {
			if ( dy >= 0 ) quad = 3;
			else quad = 2;
		}

		dist = max( fabs(dx), fabs(dy) );
		if ( dist == 0 ) {
			iter++;
			continue; // skip the old cell
		}

		if ( dist > 2 ) range = 2;
		else if ( dist > 1 ) range = 1;
		else range = 0;

		count[quad][range]++;
		if ( apb->apbUniform01() <= 1.0f/count[quad][range] ) // replace cell
				coord[quad][range] = *iter;

		// limit count for long range so that shorter picks are encouraged
		C =  min( cellsPer5SqM, count[quad][range] ) / pow(5.0f,range);
		if ( C > highC ) {
			highC = C;
			highQ = quad;
			highR = range;
		}
		
		iter++;
	}

	if ( highC == 0 ) { // no cells!
		// nothing to do, but don't clear searchDirty
		return 0;
	} else {
		avatar->search = coord[highQ][highR];
		avatar->search.x += 0.5f*STATE(SupervisorExplore)->mapResolution;
		avatar->search.y += 0.5f*STATE(SupervisorExplore)->mapResolution;
	}

	// try to find a cell looking at the target
	CELL *cell;
	float lx, ly; // look pos
	float sn, cs; 
	float weight;
	float highX, highY, highA;
	float highW = 0;
	float A, sA = atan2( avatar->search.y - avatar->pfState[1], avatar->search.x - avatar->pfState[0] );
	for ( i=0; i<18; i++ ) {
		A = sA + i*2*fM_PI/18.0f;

		sn = sin( A );
		cs = cos( A );

		lx = floor((avatar->search.x/STATE(SupervisorExplore)->mapResolution) + 0.5f - 6*cs) * STATE(SupervisorExplore)->mapResolution;
		ly = floor((avatar->search.y/STATE(SupervisorExplore)->mapResolution) + 0.5f - 6*sn) * STATE(SupervisorExplore)->mapResolution;

		// get cell
		cell = this->getCell( lx, ly, false );
		if ( !cell
		  || cell->type == CT_OUTOFBOUNDS 
		  || cell->type == CT_UNREACHABLE
		  || cell->type == CT_OCCUPIED
		  || cell->occupancy < 0.45f ) {
			continue;
		} else {
			weight = (2 - fabs( A - sA )/fM_PI) * cell->occupancy;
			if ( weight > highW ) {
				highW = weight;
				highX = lx + 0.5f*STATE(SupervisorExplore)->mapResolution;
				highY = ly + 0.5f*STATE(SupervisorExplore)->mapResolution;
				highA = A;
			}
		}
	}

	// tell the avatar about its new target
	if ( highW == 0 ) {
		// we couldn't find a good cell, have the avatar drive in that direction but don't clean the search
		
		thread = this->conversationInitiate( SupervisorExplore_CBR_convReachedPoint, -1, id, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packFloat32( avatar->search.x );
		this->ds.packFloat32( avatar->search.y );
		this->ds.packFloat32( 0 );
		this->ds.packChar( 0 );
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packInt32( this->avatarInfo[avatar->avatarId].controllerIndex );
		this->ds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), this->ds.stream(), this->ds.length(), id );
		this->ds.unlock();

		Log.log(0, "SupervisorExplore::nextSearchCell: avatar %s thread %s search %f %f, no safe look", Log.formatUUID(0,id), Log.formatUUID(0,&thread), avatar->search.x, avatar->search.y );
	} else {
	
		thread = this->conversationInitiate( SupervisorExplore_CBR_convReachedPoint, -1, id, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packFloat32( highX );
		this->ds.packFloat32( highY );
		this->ds.packFloat32( highA );
		this->ds.packChar( 1 );
		this->ds.packUUID( &STATE(AgentBase)->uuid );
		this->ds.packInt32( this->avatarInfo[avatar->avatarId].controllerIndex );
		this->ds.packUUID( &thread );
		this->sendMessageEx( this->hostCon, MSGEX(AvatarBase_MSGS,MSG_SET_TARGET), this->ds.stream(), this->ds.length(), id );
		this->ds.unlock();

		avatar->searchDirty = false;
		
		Log.log(0, "SupervisorExplore::nextSearchCell: avatar %s thread %s search %f %f, look from %f %f", Log.formatUUID(0,id), Log.formatUUID(0,&thread), avatar->search.x, avatar->search.y, highX, highY );
	}

	return 0;
}

int SupervisorExplore::avatarUpdateController( UUID *avatarId, UUID *controller, int index ) {

	if ( this->avatarInfo.find( *avatarId ) == this->avatarInfo.end() ) { // don't know them yet
		this->avatarInfo[*avatarId].owner = nilUUID;
		this->avatarInfo[*avatarId].pfId = nilUUID;
		this->avatarInfo[*avatarId].controller = nilUUID;
		this->avatarInfo[*avatarId].controllerIndex = 0;
	}

	Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::avatarUpdateController: recieved avatar (%s) controller %s, old controller %s",
		Log.formatUUID(LOG_LEVEL_VERBOSE, avatarId), Log.formatUUID(LOG_LEVEL_VERBOSE, (controller ? controller : &nilUUID)), Log.formatUUID(LOG_LEVEL_VERBOSE, &this->avatarInfo[*avatarId].controller) );

	this->avatarInfo[*avatarId].controller = *controller;
	this->avatarInfo[*avatarId].controllerIndex = index;
	
	if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller == *this->getUUID() && this->avatars.find(this->avatarInfo[*avatarId].owner) == this->avatars.end() ) {
		// is now ours, add avatar
		this->addAvatar( &this->avatarInfo[*avatarId].owner, avatarId );
	} else if ( this->avatarInfo[*avatarId].owner != nilUUID && this->avatarInfo[*avatarId].controller != *this->getUUID() && this->avatars.find(this->avatarInfo[*avatarId].owner) != this->avatars.end() ) {
		// used to be ours, remove avatar
		this->remAvatar( &this->avatarInfo[*avatarId].owner );
	}

	return 0;
}

int SupervisorExplore::addAvatar( UUID *agentId, UUID *avatarId ) {
	DataStream lds;
	AVATAR avatar;

	Log.log( LOG_LEVEL_NORMAL, "SupervisorExplore::addAvatar: adding avatar %s (agent %s)", 
		Log.formatUUID(LOG_LEVEL_NORMAL,avatarId), Log.formatUUID(LOG_LEVEL_NORMAL,agentId) );

	avatar.avatarId = *avatarId;
	avatar.pfId = this->avatarInfo[*avatarId].pfId;
	avatar.pfKnown = true; 
	avatar.pfValid = false; // state isn't valid yet
	avatar.section = -1;
	avatar.searchDirty = true;
	avatar.cells = new std::list<CELLCOORD>;

	this->avatars[*agentId] = avatar;

	// register as agent watcher
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( agentId );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
	lds.unlock();
	// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

	this->updatePartitions( true );

	return 0;
}

int SupervisorExplore::remAvatar( UUID *agentId ) {
	DataStream lds;

	Log.log( LOG_LEVEL_NORMAL, "SupervisorExplore::remAvatar: removing avatar (agent %s)", 
		Log.formatUUID(LOG_LEVEL_NORMAL,agentId) );

	// stop watching agent
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( agentId );
	this->sendMessage( this->hostCon, MSG_DDB_STOP_WATCHING_ITEM, lds.stream(), lds.length() );
	lds.unlock();

	// delete avatar
	mapAvatar::iterator iA = this->avatars.find( *agentId );
	if ( iA != this->avatars.end() ) {
		delete iA->second.cells;	
		this->avatars.erase( *agentId );
	}

	// update search
	this->updatePartitions( true );

	return 0;
}

int SupervisorExplore::avatarUpdateSection( UUID *id, float x, float y ) {
	mapAvatar::iterator iterA;
	AVATAR *avatar;
	CELL *cell;
	int newSection;

	iterA = this->avatars.find(*id);
	if ( iterA == this->avatars.end() ) {
		return 1; // not found
	}

	avatar = &iterA->second;

	cell = this->getCell( x, y );
	if ( !cell 
	  || cell->type == CT_OUTOFBOUNDS 
	  || cell->type == CT_UNREACHABLE
	  || cell->type == CT_OCCUPIED ) {
		newSection = -1;
	} else {
		newSection = cell->section;
	}

	if ( avatar->section == -1 ) {
		avatar->search.x = x;
		avatar->search.y = y;
	}

	avatar->section = newSection;
	
	return 0;
}

int SupervisorExplore::updatePartitions( bool force ) {
	UUID thread;
	GETAVATARPOS_CONVDATA convData;


	if ( !STATE(AgentBase)->started )
		return 1;

	if ( !force && STATE(SupervisorExplore)->partitionUpdateTimer != nilUUID ) {
		_timeb tb;
		apb->apb_ftime_s( &tb );
		int elapsed = (int)((tb.time - STATE(SupervisorExplore)->lastPartitionUpdate.time)*1000 + tb.millitm - STATE(SupervisorExplore)->lastPartitionUpdate.millitm);
			
		// make sure no update is in progress
		// check to see if enough time has passed since our last update
		if ( STATE(SupervisorExplore)->partitionUpdateInProgress 
		  || elapsed < SupervisorExplore_UPDATE_RATE_MIN ) {
			this->removeTimeout( &STATE(SupervisorExplore)->partitionUpdateTimer );
			STATE(SupervisorExplore)->partitionUpdateTimer = this->addTimeout( max( 100, SupervisorExplore_UPDATE_RATE_MIN - elapsed ), SupervisorExplore_CBR_cbPartitionUpdateTimeout );
			if ( STATE(SupervisorExplore)->partitionUpdateTimer == nilUUID ) {
				return 1;
			}
			return 0;
		}
	}
	
	// make sure there is something to update
	if ( this->regions.size() == 0 
	  || this->avatars.size() == 0
	  || this->sectionIds->size() == 0 ) {

		return 0;
	}

	if ( STATE(SupervisorExplore)->partitionUpdateTimer != nilUUID ) {
		this->removeTimeout( &STATE(SupervisorExplore)->partitionUpdateTimer );
	}
	STATE(SupervisorExplore)->partitionUpdateTimer = this->addTimeout( SupervisorExplore_UPDATE_RATE, SupervisorExplore_CBR_cbPartitionUpdateTimeout );
	if ( STATE(SupervisorExplore)->partitionUpdateTimer == nilUUID ) {
		return 1;
	}
	apb->apb_ftime_s( &STATE(SupervisorExplore)->lastPartitionUpdate );

	// set the update id
	apb->apbUuidCreate( &STATE(SupervisorExplore)->puId );
	STATE(SupervisorExplore)->puWaiting = true;
	STATE(SupervisorExplore)->partitionUpdateInProgress = 1;

	// request map
	this->puWaitingOnMap[STATE(SupervisorExplore)->puId] = 0;
	if ( this->dirtyMapRegions.size() ) { // there are dirty regions
		thread = this->conversationInitiate( SupervisorExplore_CBR_convGetMapData, DDB_REQUEST_TIMEOUT, &STATE(SupervisorExplore)->puId, sizeof(UUID) );
		if ( thread == nilUUID ) {
			return 1;
		}
		
		std::list<DirtyRegion>::iterator iterR = this->dirtyMapRegions.begin();
		while ( iterR != this->dirtyMapRegions.end() ) {

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::updatePartitions: requesting map region %f %f %f %f", 
				iterR->x, iterR->y, iterR->w, iterR->h );

			this->ds.reset();
			this->ds.packUUID( &STATE(SupervisorExplore)->mapId );
			this->ds.packFloat32( iterR->x );
			this->ds.packFloat32( iterR->y );
			this->ds.packFloat32( iterR->w );
			this->ds.packFloat32( iterR->h );
			this->ds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGREGION, this->ds.stream(), this->ds.length() );
			this->ds.unlock();

			this->puWaitingOnMap[STATE(SupervisorExplore)->puId]++;
			iterR++;
		}
		this->dirtyMapRegions.clear();
	}
	
	// request avatar positions
	this->puWaitingOnPF[STATE(SupervisorExplore)->puId] = 0;

	convData.updateId = STATE(SupervisorExplore)->puId;

	mapAvatar::iterator iterA = this->avatars.begin();
	while ( iterA != this->avatars.end() ) {
		if ( !iterA->second.pfKnown ) { // skip for now
			iterA++;
			continue;
		}

		convData.avatarId = *(UUID *)&iterA->first;

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::updatePartitions: requesting avatar pos %s", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,(UUID *)&iterA->first) );

		thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT, &convData, sizeof(GETAVATARPOS_CONVDATA) );
		if ( thread == nilUUID ) {
			return 1;
		}
		
		this->ds.reset();
		this->ds.packUUID( (UUID *)&iterA->second.pfId ); 
		this->ds.packInt32( DDBPFINFO_CURRENT );
		this->ds.packData( &STATE(SupervisorExplore)->lastPartitionUpdate, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		this->puWaitingOnPF[STATE(SupervisorExplore)->puId]++;
		iterA++;
	}	

	return 0;
}

int SupervisorExplore::_updatePartitions() {
	DataStream lds;
	int newSection, searchSection;
	CELL *cell;
	mapAvatar::iterator iterA;
	AVATAR *avatar;
	std::list<int>::iterator iterS, iterSTemp;
	int sId, newId; // section ids
	std::list<int> *newSections = new std::list<int>;
	std::list<CELL*> unlisted;
	std::map<CELLCOORD,CELL*,CELLCOORDless>::iterator iterC;
	std::list<CELL*>::iterator iterCOld;

	Log.log( 0, "SupervisorExplore::_updatePartitions: starting update" );

	// update sections
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		sId = *iterS;
		if ( STATE(SupervisorExplore)->sectionDirty[sId] ) {
			while ( !this->sections[sId].empty() ) {
				cell = this->sections[sId].begin()->second;
				
				// make sure the seed cell hasn't been claimed by another section!
				if ( cell->section != sId ) {
					this->sections[sId].erase( this->sections[sId].begin() );
					continue;
				}

				unlisted.clear();
				newId = this->floodSection( cell, &unlisted );
				if ( newId == -1 ) {
					return 1; // ran out of ids!?
				}
				
				if ( this->sections[newId].empty() ) { // seed cell didn't grow, try again
					this->sections[newId].clear();
					STATE(SupervisorExplore)->sectionFree[newId] = true;

					this->sections[sId].erase( this->sections[sId].begin() );
				} else if ( this->sections[newId].size() + unlisted.size() != this->sections[sId].size() ) { // there are still unaccounted for cells in the old section
					newSections->push_back( newId );

					std::map<CELLCOORD,CELL*,CELLCOORDless> remainder;

					iterC = this->sections[sId].begin();
					while ( iterC != this->sections[sId].end() ) {
						cell = iterC->second;
						if ( cell->section == sId )
							remainder[iterC->first] = iterC->second;
						iterC++;
					}

					this->sections[sId].clear();
					this->sections[sId] = remainder;
				} else { // all cells are accounted for
					newSections->push_back( newId );
					this->sections[sId].clear();
				}
			}
			this->sections[sId].clear();
			STATE(SupervisorExplore)->sectionFree[sId] = true;
			iterSTemp = iterS;
			iterS++;
			this->sectionIds->erase( iterSTemp );
		} else {
			iterS++;
		}
	}
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) { // we need to loop through again to collect clean sections instead of doing it in the first loop in case any became dirty and got moved to the end of the list for reprocessing
		newSections->push_back( *iterS );
		iterS++;
	}
	delete this->sectionIds;
	this->sectionIds = newSections;

	Log.log( 0, "SupervisorExplore::_updatePartitions: sections updated" );

	// remove cells with well defined neighbours to speed things up
/*	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		sId = *iterS;
		
		iterC = this->sections[sId].begin();
		while ( iterC != this->sections[sId].end() ) {
			// check neighbours
			if ( this->checkCellNeighbours( (*iterC)->x, (*iterC)->y ) ) {
				iterCOld = iterC;
				iterC++;
				this->sections[sId].erase(iterCOld);
			} else {
				iterC++;
			}
		}

		iterS++;
	}

	Log.log( 0, "SupervisorExplore::_updatePartitions: neighbours removed" );
*/	
	// update avatar sections
	iterA = this->avatars.begin();
	while ( iterA != this->avatars.end() ) {
		avatar = &iterA->second;

		if ( !avatar->pfValid ) { // skip
			iterA++;
			continue;
		}

		// get pos cell
		cell = this->getCell( avatar->pfState[0], avatar->pfState[1], false );
		if ( !cell 
		  || cell->type == CT_OUTOFBOUNDS 
		  || cell->type == CT_OCCUPIED ) {
			newSection = -1;
			Log.log( 0, "SupervisorExplore::_updatePartitions: avatar in invalid cell" );
		} else {
			newSection = cell->section;
		}

		if ( avatar->section != -1 ) { // we already had a section
			// get search cell
			cell = this->getCell( avatar->search.x, avatar->search.y, false );
			if ( !cell 
			  || cell->type == CT_OUTOFBOUNDS 
			  || cell->type == CT_UNREACHABLE
			  || cell->type == CT_OCCUPIED ) {
				searchSection = -1;
			} else {
				searchSection = cell->section;
			}

			if ( searchSection != newSection ) {
				STATE(SupervisorExplore)->sectionDirty[newSection] = true; // make sure we repartition this section
				avatar->searchDirty = true;
				avatar->search.x = avatar->pfState[0];
				avatar->search.y = avatar->pfState[1];
			}
		} else if ( newSection != -1 ) { // we didn't have a section but now we do
			STATE(SupervisorExplore)->sectionDirty[newSection] = true; // make sure we repartition this section
			avatar->searchDirty = true;
			avatar->search.x = avatar->pfState[0];
			avatar->search.y = avatar->pfState[1];
		} else {
			// we don't have a section, move to a clear area
			// TODO
		}

		avatar->section = newSection;

		iterA++;
	}

	// clear vis
	this->visualizeClear();

	Log.log( 0, "SupervisorExplore::_updatePartitions: updating partitions" );

	// update partitions
	int numAvatars;
	int cellsRemaining = 0;
	iterS = this->sectionIds->begin();
	while ( iterS != this->sectionIds->end() ) {
		sId = *iterS;

		// get the number of avatars in this section
		iterA = this->avatars.begin();
		numAvatars = 0;
		while ( iterA != this->avatars.end() ) {
			if ( iterA->second.section == sId ) {	
				numAvatars++;
			}
			iterA++;
		}

		if ( numAvatars > 0 ) {
			cellsRemaining += (int)this->sections[sId].size();
		}

		if ( STATE(SupervisorExplore)->sectionDirty[sId] ) {
			this->calculatePartitions( sId );
			STATE(SupervisorExplore)->sectionDirty[sId] = false;
		}

		// visualize section
		this->visualizeSection( &this->sections[sId] );

		iterS++;
	}

	Log.log( 0, "SupervisorExplore::_updatePartitions: partitions finished" );

	// check our search threshold
	if ( (this->avatars.size() >= 1 && this->avatars.begin()->second.pfValid && this->avatars.begin()->second.section != -1) && cellsRemaining / (float)STATE(SupervisorExplore)->totalCells < SEARCH_THRESHOLD ) {
		// we're done!		
		Log.log( 0, "SupervisorExplore::_updatePartitions: reached search threshold!!! we're done!" );
		
		// release all avatars
		for ( iterA = this->avatars.begin(); iterA != this->avatars.end(); iterA++ ) {
			lds.reset();
			lds.packUUID( this->getUUID() );
			lds.packUUID( &iterA->second.avatarId );
			this->sendMessageEx( this->hostCon, MSGEX(ExecutiveMission_MSGS,MSG_RELEASE_AVATAR), lds.stream(), lds.length(), &STATE(SupervisorExplore)->avatarExecId );
			lds.unlock();
		}
	} else {

		Log.log( 0, "SupervisorExplore::_updatePartitions: search threshold not reached %f >= %f (or avatars are not valid)", cellsRemaining / (float)STATE(SupervisorExplore)->totalCells, SEARCH_THRESHOLD );
		
		// update search cells
		iterA = this->avatars.begin();
		while ( iterA != this->avatars.end() ) {
			avatar = &iterA->second;

			if ( avatar->section == -1 ) { // skip
				iterA++;			
				continue;
			}

			// get search cell
			cell = this->getCell( avatar->search.x, avatar->search.y, false );

			if ( avatar->searchDirty ) {
				this->nextSearchCell( (UUID *)&iterA->first );
			} else if ( !cell 
			  || cell->type != CT_UNKNOWN ) {
				// cell is not what it should be, get new cell
				this->nextSearchCell( (UUID *)&iterA->first );
			} else if ( cell->section != avatar->section || cell->partition != avatar->partition ) {
				// cell no longer belongs to us, get new cell
				this->nextSearchCell( (UUID *)&iterA->first );
			}

			iterA++;
		}
	}

	Log.log( 0, "SupervisorExplore::_updatePartitions: finished update" );

	return 0;
}

//#define DEBUG_N	7

int SupervisorExplore::calculatePartitions( unsigned char sectionId ) {
	int i, j, partition, step;
	int n; // number of avatars/patitions
	int m; // number of cells
	CELLCOORD cellCoord;

//	_timeb startT;
//	_timeb curT;

//	apb->apb_ftime_s( &startT );
	
	// get the section
	std::map<CELLCOORD,CELL*,CELLCOORDless> *section = &this->sections[sectionId];
	std::map<CELLCOORD,CELL*,CELLCOORDless>::iterator iterC;
	m = (int)section->size();

	// get the number of avatars in this section
	mapAvatar::iterator iterAv = this->avatars.begin();
	n = 0;
	while ( iterAv != this->avatars.end() ) {
		if ( iterAv->second.section == sectionId ) {
			iterAv->second.partition = n;		
			n++;
		}
		iterAv++;
	}

	if ( n == 0 ) { // no avatars!
		return 0;
	}

	if ( n == 1 ) { // one avatar, our job is easy
		// find the avatar
		iterAv = this->avatars.begin();
		while ( iterAv != this->avatars.end() ) {
			if ( iterAv->second.section == sectionId )
				break;
			iterAv++;
		}

		iterAv->second.cells->clear();

		// iterate through cells and assign partitions
		iterC = section->begin();
		for ( j=0; j<m; j++, iterC++ ) {
			partition = 0;

			iterC->second->partition = partition;

			// add to avatar's list
			cellCoord.x = iterC->second->x;
			cellCoord.y = iterC->second->y;
			iterAv->second.cells->push_back( cellCoord );
		}
		return 0;
	}

	// prepare arrays
	int requiredSize;
	
	double *mu, *var, *mix, *nmu, *nvar;
	double *norm, *invVarVar2;
	double *x, *y;
	requiredSize = (2*n + 2*n + n + 2*n + 2*n + 2*n + 2*n + 2*m) * sizeof(double);
	if ( this->sharedArrayGaussSize < requiredSize ) {
		if ( this->sharedArrayGauss )
			free( this->sharedArrayGauss );
		this->sharedArrayGaussSize = 0;

		this->sharedArrayGauss = (double *)malloc( requiredSize );
		if ( !this->sharedArrayGauss ) {
			return 1;
		}
		this->sharedArrayGaussSize = requiredSize;
	}
	mu = this->sharedArrayGauss;
	var = mu + 2*n;
	mix = var + 2*n;
	nmu = mix + n;
	nvar = nmu + 2*n;
	norm = nvar + 2*n;
	invVarVar2 = norm + 2*n;
	x = invVarVar2 + 2*n;
	y = x + m;

	double px;
	double *pxi, *pix, *tpix, *sumPix, *split, *nsplit;
	requiredSize = (n + n*m + n*m + n + n + n) * sizeof(double);
	if ( this->sharedArrayPixSize < requiredSize ) {
		if ( this->sharedArrayPix )
			free( this->sharedArrayPix );
		this->sharedArrayPixSize = 0;

		this->sharedArrayPix = (double *)malloc( requiredSize );
		if ( !this->sharedArrayPix ) {
			return 1;
		}
		this->sharedArrayPixSize = requiredSize;
	}
	pxi = this->sharedArrayPix;
	pix = pxi + n;
	tpix = pix + n*m;
	sumPix = tpix + n*m;
	split = sumPix + n;
	nsplit = split + n;

	std::list<CELLCOORD> **cells = (std::list<CELLCOORD> **)malloc( n * sizeof(std::list<CELLCOORD> *) );
	if ( !cells ) {
		return 1;
	}

	double change, sizeDif, lastSizeDif, tarSize, val;
	bool settled = false;
	tarSize = m/(double)n; // target partition size

	// initialize distributions
	iterAv = this->avatars.begin();
	i = 0;
	while ( iterAv != this->avatars.end() ) {
		if ( iterAv->second.section == sectionId ) {
			mu[2*i] = iterAv->second.pfState[0];
			mu[2*i+1] = iterAv->second.pfState[1];
			var[2*i] = 0.5f;
			var[2*i+1] = 0.5f;
			mix[i] = 0.5f;

			iterAv->second.cells->clear();
			cells[i] = iterAv->second.cells;
			i++;
		}
		iterAv++;
	}

	memset( split, 0, sizeof(double)*n );

	// initialize cell locs
	for ( i = 0, iterC = section->begin(); iterC != section->end(); i++, iterC++ ) {
		x[i] = iterC->second->x;
		y[i] = iterC->second->y;
	}

	// DEBUG	
	//double _pxi[DEBUG_N], _mix[DEBUG_N], _invVarVar2[2*DEBUG_N], _norm[2*DEBUG_N], _mu[2*DEBUG_N];

	for ( step = 0; step<6; step++ ) {
		// -- E Step --
		// prepare distribution constants
		for ( i=0; i<2*n; i++ ) {
			norm[i] = 1/(sqrt(2*fM_PI)*var[i]);
			invVarVar2[i] = 1/(2*var[i]*var[i]);
		}

		memset( nsplit, 0, sizeof(double)*n );
		memset( nmu, 0, sizeof(double)*2*n );
		memset( nvar, 0, sizeof(double)*2*n );

		// iterate through cells
		for ( j=0; j<m; j++ ) {
			// calculate pxi
			px = 0;
			for ( i=0; i<n; i++ ) {
				pxi[i] = norm[2*i] * exp( -((mu[2*i] - x[j])*(mu[2*i] - x[j])) * invVarVar2[2*i] )
				       * norm[2*i+1] * exp( -((mu[2*i+1] - y[j])*(mu[2*i+1] - y[j])) * invVarVar2[2*i+1] );
				px += pxi[i]*mix[i];
			}

			if ( px == 0 ) { // all avatars are too far away, just give it to the first one
				px = 1;
				pxi[0] = 1;
			}

			// calculate pix
			for ( i=0; i<n; i++ ) {
				tpix[i+j*n] = mix[i] * pxi[i] / px;
				nsplit[i] += tpix[i+j*n];
				nmu[2*i] += tpix[i+j*n] * x[j];		// sum up the numerator first
				nmu[2*i+1] += tpix[i+j*n] * y[j];
			}
		}

		// check improvement
		change = 0;
		sizeDif = 0;
		for ( i=0; i<n; i++ ) {
			change += (split[i] - nsplit[i])*(split[i] - nsplit[i]);
			sizeDif += fabs(nsplit[i] - tarSize);
		}
		if ( settled && sizeDif - lastSizeDif > 1 ) { // we got signifcantly worse, keep last values and give up
			break;
		}
		if ( change < 1 ) {
			settled = true;
		}
		if ( change < 0.001f ) { // wasn't much improvement, accept new values and give up
			memcpy( pix, tpix, sizeof(double)*n*m );
			break; 
		}
		sizeDif /= n;
		if ( sizeDif < max( 0.5f, m*0.01f ) ) { // we're close enough, accept new values and give up
			memcpy( pix, tpix, sizeof(double)*n*m );
			break; 
		}
		memcpy( pix, tpix, sizeof(double)*n*m );
		lastSizeDif = sizeDif;
		memcpy( split, nsplit, sizeof(double)*n );

		// -- M Step --
		for ( i=0; i<n; i++ ) {
			// a) new mix
			mix[i] = mix[i]*(split[i]>tarSize ? 0.9f : 1.5f );

			// b) new mu
			nmu[2*i] /= split[i];
			nmu[2*i+1] /= split[i];
			mu[2*i] = (mu[2*i] + nmu[2*i]) * 0.5f;
			mu[2*i+1] = (mu[2*i+1] + nmu[2*i+1]) * 0.5f;
		}

		// c) new var
		// iterate through cells
		for ( j=0; j<m; j++ ) {
			// iterate through partition
			for ( i=0; i<n; i++ ) {
				val = (x[j] - mu[2*i]);
				nvar[2*i] += pix[i+j*n] * val*val;		// sum up the numerator first
				
				val = (y[j] - mu[2*i+1]);
				nvar[2*i+1] += pix[i+j*n] * val*val;
			}
		}
		
		for ( i=0; i<n; i++ ) {
			nvar[2*i] = sqrt(nvar[2*i] / split[i]);
			nvar[2*i+1] = sqrt(nvar[2*i+1] / split[i]);
			var[2*i] = (var[2*i] + nvar[2*i]) * 0.5f;
			var[2*i+1] = (var[2*i+1] + nvar[2*i+1]) * 0.5f;
		}
	}

	// iterate through cells and assign partitions
	iterC = section->begin();
	for ( j=0; j<m; j++, iterC++ ) {
		// iterate through partitions to find the max
		val = 0;
		partition = 0;
		for ( i=0; i<n; i++ ) {
			if ( val < pix[i+j*n] ) {
				val = pix[i+j*n];
				partition = i;
			}
		}

		iterC->second->partition = partition;

		// add to avatar's list
		cellCoord.x = (float)x[j];
		cellCoord.y = (float)y[j];
		cells[partition]->push_back( cellCoord );
	}

	Log.log( 0, "SupervisorExplore::calculatePartitions: done, n %d, m %d, tarSize %f, steps %d, change %f, stepDif %f", n, m, tarSize, step, change, sizeDif );
	for ( i = 0; i < min(10,n); i++ ){
		Log.log( 0, "SupervisorExplore::calculatePartitions: nsplit[%d] %f cells %d", i, nsplit[i], (int)cells[i]->size() );
	}

	// free arrays
	free( cells );

	return 0;
}

int SupervisorExplore::notifyMap( UUID *item, char evt, char *data, int len ) {
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
	mapRegions::iterator iter = this->regions.begin();
	while ( iter != this->regions.end() ) {
		activeR.x = max( newRegion.x, iter->second.x );
		activeR.w = min( newRegion.x + newRegion.w, iter->second.x + iter->second.w ) - activeR.x;
		activeR.y = max( newRegion.y, iter->second.y );
		activeR.h = min( newRegion.y + newRegion.h, iter->second.y + iter->second.h ) - activeR.y;

		if ( activeR.w > 0 && activeR.h > 0 ) {
			// dirty sections
			if ( this->dirtyRegion( &activeR ) ) { // we have some active sections in this region
				// dirty map
				this->dirtyMap( &activeR );
			}
		}

		iter++;
	}
	
	// see if we should update sections now
	// TODO

	return 0;
}

int SupervisorExplore::dirtyRegion( DDBRegion *region ) {
	float x, y, w, h;
	float tileSize;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int tileId, lastTileId;
	CELL *tile;
	mapTile::iterator iterTile;

	// log if we actually dirtied any sections
	int sectionDirtied = 0;

	// convert coords and tileSize into cell space
	x = floor(region->x/STATE(SupervisorExplore)->mapResolution);
	y = floor(region->y/STATE(SupervisorExplore)->mapResolution);
	w = ceil((region->x+region->w)/STATE(SupervisorExplore)->mapResolution) - x;
	h = ceil((region->y+region->h)/STATE(SupervisorExplore)->mapResolution) - y;
	tileSize = round(STATE(SupervisorExplore)->mapTileSize/STATE(SupervisorExplore)->mapResolution);

	lastTileId = -1;
	for ( c=0 ; c<(int)w; c++ ) {
		u = (int)floor((x+c)/tileSize);
		i = (int)(x+c - u*tileSize);

		for ( r=0; r<(int)h; r++ ) {
			v = (int)floor((y+r)/tileSize);
			j = (int)(y+r - v*tileSize);
	
			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = this->tileRef.find(tileId);
				if ( iterTile == this->tileRef.end() ) {
					tile = NULL; // this tile doesn't exist
				} else {
					tile = (CELL *)getDynamicBuffer( iterTile->second );
				}

				lastTileId = tileId;
			}

			if ( tile && tile[j+i*STATE(SupervisorExplore)->mapStride].type == CT_UNKNOWN && tile[j+i*STATE(SupervisorExplore)->mapStride].section != -1 ) {
				STATE(SupervisorExplore)->sectionDirty[ tile[j+i*STATE(SupervisorExplore)->mapStride].section ] = true;
				sectionDirtied = 1;
			}
		}
	}

	return sectionDirtied;
}

int SupervisorExplore::dirtyMap( DDBRegion *region ) {
	DirtyRegion reg;

	reg.x = floor(region->x/STATE(SupervisorExplore)->mapResolution)*STATE(SupervisorExplore)->mapResolution;
	reg.y = floor(region->y/STATE(SupervisorExplore)->mapResolution)*STATE(SupervisorExplore)->mapResolution;
	reg.w = ceil((region->x + region->w)/STATE(SupervisorExplore)->mapResolution)*STATE(SupervisorExplore)->mapResolution - reg.x;
	reg.h = ceil((region->y + region->h)/STATE(SupervisorExplore)->mapResolution)*STATE(SupervisorExplore)->mapResolution - reg.y;
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
int SupervisorExplore::_dirtyMap( DirtyRegion *region, std::list<DirtyRegion>::iterator iter, std::list<DirtyRegion>::iterator insertAt ) {
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

int SupervisorExplore::ddbNotification( char *data, int len ) {
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
			UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
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
	} else if ( evt == DDBE_WATCH_ITEM ) {
		if ( this->avatars.find(uuid) != this->avatars.end() ) { // one of our avatar agents
			// get status
			UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_MAP_PROBOCCGRID ) {
		this->notifyMap( &uuid, evt, data + offset, len - offset );
	} else if ( type == DDB_AGENT ) {
		if ( this->avatars.find(uuid) != this->avatars.end() ) { // our agent
			if ( evt == DDBE_AGENT_UPDATE ) {
				int infoFlags = lds.unpackInt32();
				if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
					
					// get status
					UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
					sds.reset();
					sds.packUUID( &uuid );
					sds.packInt32( DDBAGENTINFO_RSTATUS );
					sds.packUUID( &thread );
					this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
					sds.unlock();
				}
			}
		} else { // must be holdout from an earlier crash
			// stop watching agent
			sds.reset();
			sds.packUUID( &STATE(AgentBase)->uuid );
			sds.packUUID( &uuid );
			this->sendMessage( this->hostCon, MSG_DDB_STOP_WATCHING_ITEM, sds.stream(), sds.length() );
			sds.unlock();
		}
	} else if ( type == DDB_AVATAR ) {
		if ( evt == DDBE_ADD ) {
			// get info
			UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &uuid ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
			sds.unlock();
		} else if ( evt == DDBE_REM ) {
			this->avatarUpdateController( &uuid, &nilUUID, 0 );
		} else if ( evt == DDBE_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAVATARINFO_CONTROLLER ) {
				// get info
				UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				if ( thread == nilUUID ) {
					return 1;
				}
				sds.reset();
				sds.packUUID( &uuid ); 
				sds.packInt32( DDBAVATARINFO_RCONTROLLER );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	}
	lds.unlock();
	
	return 0;
}



//-----------------------------------------------------------------------------
// Visualize

int SupervisorExplore::visualizeClear() {
#ifdef DO_VISUALIZE
	std::list<int>::iterator it;

	for ( it = this->visObjs.begin(); it != this->visObjs.end(); it++ ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( *it );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}
	this->visObjs.clear();
#endif
	return 0;
}

int SupervisorExplore::visualizeSection( std::map<CELLCOORD,CELL*,CELLCOORDless> *section ) {
#ifdef DO_VISUALIZE 
	std::map<CELLCOORD,CELL*,CELLCOORDless>::iterator it;
	int objId;
	float colours[][3] = {  { 1, 0, 0 }, // red
							{ 0, 1, 0 }, // green
							{ 0, 0, 1 }, // blue
							{ 1, 1, 0 }, // yellow
							{ 0, 1, 1 }, // cyan
							{ 1, 0, 1 }, // magenta
							{ 1, 0.5f, 0 }, // orange
							{ 0, 1, 0.5f }, // turquoise
							{ 0.5f, 0, 1 }, // purple
						 };

	objId = 100;
	for ( it = section->begin(); it != section->end(); it++ ) {
		this->ds.reset();
		this->ds.packUUID( this->getUUID() ); // agent
		this->ds.packInt32( objId ); // objectId
		this->ds.packFloat32( it->second->x ); // x
		this->ds.packFloat32( it->second->y ); // y
		this->ds.packFloat32( 0 ); // r
		this->ds.packFloat32( STATE(SupervisorExplore)->mapResolution ); // s
		this->ds.packInt32( 1 ); // path count
		this->ds.packInt32( VIS_PATH_CELL ); // path id
		
		if ( it->second->partition < 9 ) {
			this->ds.packFloat32( colours[it->second->partition][0] );
			this->ds.packFloat32( colours[it->second->partition][1] );
			this->ds.packFloat32( colours[it->second->partition][2] );
		} else {
			this->ds.packFloat32( 1 ); // r
			this->ds.packFloat32( 0 ); // g
			this->ds.packFloat32( 0 ); // b
		}
		
		this->ds.packFloat32( 1 ); // lineWidth
		this->ds.packBool( 0 ); // solid
		this->ds.packString( "Cell" );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDOBJECT, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		this->visObjs.push_back(objId);
		objId++;
	}
#endif
	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int SupervisorExplore::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case SupervisorExplore_MSGS::MSG_ADD_REGION:
		{
			UUID id;
			bool forbidden;
			lds.setData( data, len );
			lds.unpackUUID( &id );
			forbidden = lds.unpackChar() > 0;
			this->addRegion( &id, forbidden );
		}
		break;
	case SupervisorExplore_MSGS::MSG_ADD_MAP:
		this->addMap( (UUID *)data );
		break;
	case SupervisorExplore_MSGS::MSG_AVATAR_EXEC:
		STATE(SupervisorExplore)->avatarExecId = *(UUID *)data;
		this->backup(); // avatarExecId
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool SupervisorExplore::cbPartitionUpdateTimeout( void *NA ) {
	this->updatePartitions();
	return 0;
}

bool SupervisorExplore::convGetRegion( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetRegion: timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	if ( DDBR_OK != this->ds.unpackChar() ) {
		this->ds.unlock();
		Log.log( 0, "SupervisorExplore::convGetRegion: region not found!" );
		return 0;
	}

	RegionRequest rr;
	rr = *(RegionRequest *)conv->data;

	DDBRegion region;

	region.x = this->ds.unpackFloat32();
	region.y = this->ds.unpackFloat32();
	region.w = this->ds.unpackFloat32();
	region.h = this->ds.unpackFloat32();

	this->ds.unlock();

	this->_addRegion( &rr.id, &region, rr.forbidden );

	return 0;
}

bool SupervisorExplore::convGetMapInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetMapInfo: timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	if ( DDBR_OK != this->ds.unpackChar() ) {
		this->ds.unlock();
		Log.log( 0, "SupervisorExplore::convGetMapInfo: map not found!" );
		return 0;
	}


	STATE(SupervisorExplore)->mapTileSize = this->ds.unpackFloat32();
	STATE(SupervisorExplore)->mapResolution = this->ds.unpackFloat32();
	STATE(SupervisorExplore)->mapStride = this->ds.unpackInt32();

	this->ds.unlock();

	STATE(SupervisorExplore)->mapReady = true;

	this->backup(); // mapReady

	// initialize regions if we have any waiting
	mapRegions::iterator iR;
	for ( iR = this->regions.begin(); iR != this->regions.end(); iR++ ) {
		this->initializeRegion( (UUID *)&iR->first, false );
	}
	for ( iR = this->forbiddenRegions.begin(); iR != this->forbiddenRegions.end(); iR++ ) {
		this->initializeRegion( (UUID *)&iR->first, true );
	}

	this->updatePartitions( true );

	return 0;
}

bool SupervisorExplore::convGetMapData( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetMapData: timed out" );
		return 0; // end conversation
	}

	UUID id = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	if ( lds.unpackChar() == DDBR_OK ) { // succeeded
		float x, y, w, h, res;
		int cw, ch;

		x = lds.unpackFloat32();
		y = lds.unpackFloat32();
		w = lds.unpackFloat32();
		h = lds.unpackFloat32();
		res = lds.unpackFloat32();
		
		cw = (int)floor( w / res );
		ch = (int)floor( h / res );

		this->updateMap( x, y, w, h, (float *)lds.unpackData( cw*ch*4 ) );
	}

	lds.unlock();


	this->puWaitingOnMap[id]--;

	if ( id == STATE(SupervisorExplore)->puId ) {
		if ( !this->puWaitingOnMap[id] && !this->puWaitingOnPF[id] ) { // ready to do update
			STATE(SupervisorExplore)->puWaiting = 0;
			this->puWaitingOnMap.erase(id);
			this->puWaitingOnPF.erase(id);
			this->_updatePartitions();
			return 0;
		} else if ( !this->puWaitingOnMap[id] ) { // we have all the map updates
			return 0;
		} else { // we're still waiting on Map data, keep the conversation open
			return 1; 
		}
	} else { // old
		if ( !this->puWaitingOnMap[id] ) { // we have all the map updates
			this->puWaitingOnMap.erase( id );
			return 0;
		} else { // we're still waiting on Map data, keep the conversation open
			return 1;
		}
	}

	return 0;
}

bool SupervisorExplore::convGetAvatarList( void *vpConv ) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetAvatarList: timed out" );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID avatarId;
		UUID agentId;
		AgentType agentType;
		int i, count;

		if ( lds.unpackInt32() != DDBAVATARINFO_ENUM ) {
			lds.unlock();
			return 0; // what happened here?
		}
		
		count = lds.unpackInt32();
		Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::convGetAvatarList: recieved %d avatars", count );

		for ( i=0; i<count; i++ ) {
			lds.unpackUUID( &avatarId );
			lds.unpackString(); // avatar type
			lds.unpackUUID( &agentId );
			lds.unpackUUID( &agentType.uuid );
			agentType.instance = lds.unpackInt32();

			// request further info
			UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID) );
			if ( thread == nilUUID ) {
				return 1;
			}
			sds.reset();
			sds.packUUID( &avatarId ); 
			sds.packInt32( DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RCONTROLLER );
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

bool SupervisorExplore::convGetAvatarInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetAvatarInfo: timed out" );
		return 0; // end conversation
	}

	UUID avId = *(UUID *)conv->data;

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		UUID agent, pfId, controller;
		int index, priority;
		int infoFlags = this->ds.unpackInt32();

		if ( infoFlags == DDBAVATARINFO_RCONTROLLER ) {
			this->ds.unpackUUID( &controller );
			index = this->ds.unpackInt32();
			priority = this->ds.unpackInt32();
			this->ds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::convGetAvatarInfo: recieved avatar (%s) controller: %s, index %d, priority %d", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&controller), index, priority );

			this->avatarUpdateController( &avId, &controller, index );

		} else if ( infoFlags == (DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF  | DDBAVATARINFO_RCONTROLLER) ) {
			this->ds.unpackUUID( &agent );
			this->ds.unpackUUID( &pfId );
			this->ds.unpackUUID( &controller );
			index = this->ds.unpackInt32();
			priority = this->ds.unpackInt32();
			this->ds.unlock();

			Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::convGetAvatarInfo: recieved avatar (%s) agent: %s, pf id: %s", 
				Log.formatUUID(LOG_LEVEL_VERBOSE,&avId), Log.formatUUID(LOG_LEVEL_VERBOSE,&agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&pfId) );

			this->avatarInfo[avId].owner = agent;
			this->avatarInfo[avId].pfId = pfId;

			this->avatarUpdateController( &avId, &controller, index );
		} else {
			this->ds.unlock();
			return 0; // what happened here?
		}

	} else {
		this->ds.unlock();

		// TODO try again?
	}

	return 0;
}

bool SupervisorExplore::convGetAvatarPos( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convGetAvatarPos: timed out" );
		return 0; // end conversation
	}

	GETAVATARPOS_CONVDATA convData = *(GETAVATARPOS_CONVDATA *)conv->data;
	UUID id = convData.updateId;

	if ( id != STATE(SupervisorExplore)->puId ) {
		this->puWaitingOnPF[id]--;
		if ( !this->puWaitingOnPF[id] ) 
			this->puWaitingOnPF.erase(id);
		return 0; // old, ignore
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread

	UUID uuid;
	uuid = convData.avatarId;

	if ( this->avatars.find( uuid ) == this->avatars.end() ) {
		this->ds.unlock();
		return 0; // no longer our avatar
	}

	char response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		_timeb *tb;
		CELL *cell;

		if ( this->ds.unpackInt32() != DDBPFINFO_CURRENT ) {
			this->ds.unlock();
			return 0; // what happened here?
		}
		
		tb = (_timeb *)this->ds.unpackData( sizeof(_timeb) );
		memcpy( this->avatars[uuid].pfState, this->ds.unpackData(sizeof(float)*3), sizeof(float)*3 );

		this->ds.unlock();

		this->avatars[uuid].pfValid = true; // we have received the state at least once

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::convGetAvatarPos: recieved avatar pos %s (%f %f)", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&uuid), this->avatars[uuid].pfState[0], this->avatars[uuid].pfState[1] );

		// dirty avatar section
		cell = this->getCell( this->avatars[uuid].pfState[0], this->avatars[uuid].pfState[1], false );
		if ( !cell 
		  || cell->type == CT_OUTOFBOUNDS 
		  || cell->type == CT_OCCUPIED ) {
			// nothing to do
		} else {
			STATE(SupervisorExplore)->sectionDirty[cell->section] = true;
		}

		this->puWaitingOnPF[id]--;

		if ( !this->puWaitingOnMap[id] && !this->puWaitingOnPF[id] ) { // ready to do update
			STATE(SupervisorExplore)->puWaiting = 0;
			this->puWaitingOnMap.erase(id);
			this->puWaitingOnPF.erase(id);
			this->_updatePartitions();
		}

		//Log.log( 0, "SupervisorExplore::convGetAvatarPos: state x %f y %f r %f", this->avatars[id].pfState[0], this->avatars[id].pfState[1], this->avatars[id].pfState[2] );
			
	} else { // failed
		this->ds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "SupervisorExplore::convGetAvatarPos: failed to get avatar pos %s, retrying in %d", 
			Log.formatUUID(LOG_LEVEL_VERBOSE,&uuid), SupervisorExplore_AVATAR_POS_RETRY );

		UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarPos, DDB_REQUEST_TIMEOUT + SupervisorExplore_AVATAR_POS_RETRY, &convData, sizeof(GETAVATARPOS_CONVDATA) );
		if ( thread == nilUUID ) {
			return 1;
		}
		
		this->ds.reset();
		this->ds.packUUID( (UUID *)&this->avatars[uuid].pfId ); 
		this->ds.packInt32( DDBPFINFO_CURRENT );
		this->ds.packData( &STATE(SupervisorExplore)->lastPartitionUpdate, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->delayMessage( SupervisorExplore_AVATAR_POS_RETRY, this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 0;
}

bool SupervisorExplore::convReachedPoint( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "SupervisorExplore::convReachedPoint: timed out" );
		return 0; // end conversation
	}

	UUID id;
	id = *(UUID *)conv->data;

	if ( this->avatars.find( id ) == this->avatars.end() ) {
		return 0; // no longer our avatar
	}

	this->ds.setData( conv->response, conv->responseLen );
	
	UUID thread;
	this->ds.unpackUUID( &thread );
	char success = this->ds.unpackChar();

	int reason;
	if ( !success ) {
		reason = this->ds.unpackInt32();
		this->ds.unlock();
		Log.log(0, "SupervisorExplore::convReachedPoint: avatar thread %s target fail %d", Log.formatUUID( 0, &thread), reason );
		if ( reason == AvatarBase_Defs::TP_NEW_TARGET ) // we gave them a new target, no reason to do anything
			return 0;
	} else {
		this->ds.unlock();
		Log.log(0, "SupervisorExplore::convReachedPoint: avatar thread %s target success", Log.formatUUID( 0, &thread) );
	}

	// dirty search
	this->avatars[id].searchDirty = true;

	this->updatePartitions( true );

	return 0;
}


bool SupervisorExplore::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "SupervisorExplore::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID agent = *(UUID *)conv->data;

	if ( this->avatars.find( agent ) == this->avatars.end() ) {
		return 0; // no longer our avatar
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		if ( lds.unpackInt32() != DDBAGENTINFO_RSTATUS ) {
			lds.unlock();
			return 0; // what happened here?
		}

		this->agentStatus[agent] = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "SupervisorExplore::convAgentInfo: status %d", this->agentStatus[agent] );

		if ( this->avatars[agent].section != -1 )
			this->nextSearchCell( &agent );

	} else {
		lds.unlock();

		Log.log( 0, "SupervisorExplore::convAgentInfo: request failed %d", result );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int SupervisorExplore::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int SupervisorExplore::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	SupervisorExplore::writeState( DataStream *ds, bool top ) {
	int i;

	if ( top ) _WRITE_STATE(SupervisorExplore);

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->regions );
	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->forbiddenRegions );
	_WRITE_STATE_MAP( int, UUID, &this->tileRef );

	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->puWaitingOnMap );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->puWaitingOnPF );

	// sections are handled specially, see below

	// sectionIds
	if ( this->sectionIds != NULL ) {
		ds->packBool( 1 );
		_WRITE_STATE_LIST( int, this->sectionIds );
	} else {
		ds->packBool( 0 );
	}

	_WRITE_STATE_LIST( DirtyRegion, &this->dirtyMapRegions );

	// avatars
	_WRITE_STATE_MAP_LESS( UUID, AVATAR_INFO, UUIDless, &this->avatarInfo );
	mapAvatar::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		ds->packBool( 1 );
		ds->packUUID( (UUID *)&iA->first );
		ds->packData( (void *)&iA->second, sizeof(AVATAR) );
		_WRITE_STATE_LIST( CELLCOORD, iA->second.cells ); // pack cells
	}
	ds->packBool( 0 );
	_WRITE_STATE_MAP_LESS( UUID, int, UUIDless, &this->agentStatus );

	_WRITE_STATE_LIST( int, &this->visObjs );

	int ret = AgentBase::writeState( ds, false );
	if ( ret ) return ret;

	// sections, must happen after state buffers are written
	for ( i=0; i<MAX_SECTIONS; i++ ) {
		std::map<CELLCOORD,CELL *,CELLCOORDless>::iterator iC; 
		for ( iC = this->sections[i].begin(); iC != this->sections[i].end(); iC++ ) { 
			ds->packBool( 1 ); 
			ds->packFloat32( iC->second->x );
			ds->packFloat32( iC->second->y );
		} 
		ds->packBool( 0 );
	}

	return 0;
}

int	SupervisorExplore::readState( DataStream *ds, bool top ) {
	int i;

	if ( top ) _READ_STATE(SupervisorExplore);

	_READ_STATE_MAP( UUID, DDBRegion, &this->regions );
	_READ_STATE_MAP( UUID, DDBRegion, &this->forbiddenRegions );
	_READ_STATE_MAP( int, UUID, &this->tileRef );
	
	_READ_STATE_MAP( UUID, int, &this->puWaitingOnMap );
	_READ_STATE_MAP( UUID, int, &this->puWaitingOnPF );
	
	// sections are handled specially, see below

	// sectionIds
	if ( ds->unpackBool() ) {
		if ( this->sectionIds )
			delete this->sectionIds;
		this->sectionIds = new std::list<int>;
		_READ_STATE_LIST( int, this->sectionIds );
	}

	_READ_STATE_LIST( DirtyRegion, &this->dirtyMapRegions );

	// avatars
	_READ_STATE_MAP( UUID, AVATAR_INFO, &this->avatarInfo );
	UUID keyA;
	AVATAR varA;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &keyA );
		varA = *(AVATAR *)ds->unpackData( sizeof(AVATAR) );
		// unpack cells
		varA.cells = new std::list<CELLCOORD>;
		_READ_STATE_LIST( CELLCOORD, varA.cells );		
		this->avatars[keyA] = varA;
	}
	_READ_STATE_MAP( UUID, int, &this->agentStatus );

	_READ_STATE_LIST( int, &this->visObjs );

	int ret = AgentBase::readState( ds, false );
	if ( ret ) return ret;

	// sections, must happen after state buffers are read!
	CELLCOORD cellCoord;
	CELL *cell;
	for ( i=0; i<MAX_SECTIONS; i++ ) {
		while ( ds->unpackBool() ) {
			cellCoord.x = ds->unpackFloat32();
			cellCoord.y = ds->unpackFloat32();

			cell = getCell( cellCoord.x, cellCoord.y );
			this->sections[i][cellCoord] = cell;
		}
	}

	// reset neighbour pointers for tiles
	this->resetCellNeighbours();

	return 0;
}

int SupervisorExplore::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	// clean up visualization
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packChar( 0 ); // keep paths
	this->sendMessage( this->hostCon, MSG_DDB_VIS_CLEAR_ALL, lds.stream(), lds.length() );
	lds.unlock();

	if ( STATE(SupervisorExplore)->mapId != nilUUID ) {
		if ( !STATE(SupervisorExplore)->mapReady ) { 
			// get map info
			UUID thread = this->conversationInitiate(SupervisorExplore_CBR_convGetMapInfo, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &STATE(SupervisorExplore)->mapId );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RPOGINFO, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	// re add all regions
	std::map<UUID,DDBRegion,UUIDless>::iterator iR;
	for ( iR = this->regions.begin(); iR != this->regions.end(); iR++ ) {
		this->_addRegion( (UUID *)&iR->first, &iR->second, false );
	}
	for ( iR = this->forbiddenRegions.begin(); iR != this->forbiddenRegions.end(); iR++ ) {
		this->_addRegion( (UUID *)&iR->first, &iR->second, true );
	}

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		// request list of avatars
		UUID thread = this->conversationInitiate( SupervisorExplore_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT );
		if ( thread == nilUUID ) {
			return 1;
		}	
		lds.reset();
		lds.packUUID( this->getUUID() ); // dummy id 
		lds.packInt32( DDBAVATARINFO_ENUM );
		lds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, lds.stream(), lds.length() );
		lds.unlock();
	}

	return 0;
}

int SupervisorExplore::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(SupervisorExplore)->avatarExecId );
	ds->packBool( STATE(SupervisorExplore)->mapReady );
	ds->packUUID( &STATE(SupervisorExplore)->mapId );
	ds->packFloat32( STATE(SupervisorExplore)->mapTileSize );
	ds->packFloat32( STATE(SupervisorExplore)->mapResolution );
	ds->packInt32( STATE(SupervisorExplore)->mapStride );

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->regions );
	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &this->forbiddenRegions );

	return AgentBase::writeBackup( ds );
}

int SupervisorExplore::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(SupervisorExplore)->avatarExecId );
	STATE(SupervisorExplore)->mapReady = ds->unpackBool();
	ds->unpackUUID( &STATE(SupervisorExplore)->mapId );
	STATE(SupervisorExplore)->mapTileSize = ds->unpackFloat32();
	STATE(SupervisorExplore)->mapResolution = ds->unpackFloat32();
	STATE(SupervisorExplore)->mapStride = ds->unpackInt32();

	_READ_STATE_MAP( UUID, DDBRegion, &this->regions );
	_READ_STATE_MAP( UUID, DDBRegion, &this->forbiddenRegions );

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	SupervisorExplore *agent = (SupervisorExplore *)vpAgent;

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
	SupervisorExplore *agent = new SupervisorExplore( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	SupervisorExplore *agent = new SupervisorExplore( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CSupervisorExploreDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CSupervisorExploreDLL, CWinApp)
END_MESSAGE_MAP()

CSupervisorExploreDLL::CSupervisorExploreDLL() {}

// The one and only CSupervisorExploreDLL object
CSupervisorExploreDLL theApp;

int CSupervisorExploreDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CSupervisorExploreDLL ---\n"));
  return CWinApp::ExitInstance();
}