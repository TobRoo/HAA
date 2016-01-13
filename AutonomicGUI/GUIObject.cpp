// GUIObject.cpp
// 

#include "stdafx.h"

#include "AgentMirrorGUI.h"

#define round(val) floor((val) + 0.5f)

//*****************************************************************************
// GUIObjectBase

GUIObjectBase::GUIObjectBase( UUID *id, AgentMirrorGUI *mirror ) {
	this->id = *id;
	this->mirror = mirror;
	this->dStore = mirror->getDataStore();
	this->visualizer = mirror->getVisualizer();

	this->classType = DDB_INVALID; // unset

	this->visible = true;
	this->highlighted = false;
	this->hasProperties = false;

	this->visObj = -1;
	this->visColour[0] = this->visColour[1] = this->visColour[2] = 0.0f;
	this->visHighlight[0] = this->visHighlight[1] = this->visHighlight[2] = 1.0f;

	this->ds = new DataStream();

	UuidCreateNil( &nilUUID );
}

GUIObjectBase::~GUIObjectBase() {
	delete this->ds;
}

int GUIObjectBase::getParent( UUID *id ) {
	return 1;
}

int GUIObjectBase::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"GUIObjectBase" );
	return 0;
}

int GUIObjectBase::getAbstract( WCHAR *buf, int len, int offset ) {
	return swprintf_s( buf + offset, len - offset, L"No abstract" );
}


int GUIObjectBase::prepareRClick( CMenu *menu ) {

	menu->AppendMenu( MF_STRING | MF_ENABLED | (this->visible ? MF_CHECKED : MF_UNCHECKED), MI_VISIBILITY, L"Visible" );
	if ( this->hasProperties )
		menu->AppendMenu( MF_STRING | MF_ENABLED, MI_PROPERTIES, L"Properties" );

	return 0;
}

int GUIObjectBase::rClick( UINT result ) {

	switch ( result ) {
	case MI_VISIBILITY:
		this->setVisibility( !this->visible );
		break;
	default:
		return 1; // unhandled
	};

	return 0;
}

int GUIObjectBase::setVisibility( bool vis ) {
	if ( this->visible == vis )
		return 1; // no change

	if ( this->visObj != -1 ) { // show/hide
		if ( vis ) this->visualizer->showObject( this->visObj );
		else this->visualizer->hideObject( this->visObj );
	}

	this->visible = vis;
	return 0;
}

int GUIObjectBase::setHighlight( bool highlight ) {
	if ( this->highlighted == highlight )
		return 1; // no change

	if ( this->visObj != -1 ) { 
		if ( highlight ) this->visualizer->setObjectColour( this->visObj, this->visHighlight );
		else this->visualizer->setObjectColour( this->visObj, this->visColour );
	}

	this->highlighted = highlight;
	return 0;
}

//*****************************************************************************
// GODDBAgent

GODDBAgent::GODDBAgent( UUID *id, AgentMirrorGUI *mirror ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	// initialize
	this->classType = DDB_AGENT;
	this->typeName[0] = 0; 
	UuidCreateNil( &this->agentTypeId );
	this->agentInstance = -1;

	dStore->AgentGetInfo( id, DDBAGENTINFO_RTYPE | DDBAGENTINFO_RPARENT, &lds, &nilUUID );

	lds.rewind();

	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackChar() == DDBR_OK ) { // success
		lds.unpackInt32(); // infoFlags
		strcpy_s( this->typeName, sizeof(this->typeName), lds.unpackString() );
		lds.unpackUUID( &this->agentTypeId );
		this->agentInstance = lds.unpackChar();

		lds.unpackUUID( &this->parentId );
	}

	lds.unlock();
}

GODDBAgent::~GODDBAgent() {
	std::map<int, int>::iterator iI;

	// clean up objects
	for ( iI = this->visObjects.begin(); iI != this->visObjects.end(); iI++ )
		this->visualizer->deleteObject( iI->second, true );
	this->visObjects.clear();

	// clean up paths
	for ( iI = this->visPaths.begin(); iI != this->visPaths.end(); iI++ )
		this->visualizer->deletePath( iI->second );
	this->visPaths.clear();

}

int GODDBAgent::getName( WCHAR *buf, int len ) {
	if ( strlen(this->typeName) == 0 )
		swprintf_s( buf, len, L"Unknown Agent" );
	else
		swprintf_s( buf, len, L"%hs/%d", this->typeName, this->agentInstance );
	return 0;
}

int GODDBAgent::getAbstract( WCHAR *buf, int len, int offset ) {
	RPC_STATUS Status;
	RPC_WSTR uuidBuf1, uuidBuf2, uuidBuf3, uuidBuf4;

	if ( UuidIsNil( &this->id, &Status ) ) { 
		uuidBuf1 = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &this->id, &uuidBuf1 );
	}
	if ( UuidIsNil( &this->agentTypeId, &Status ) ) { 
		uuidBuf2 = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &this->agentTypeId, &uuidBuf2 );
	}
	if ( UuidIsNil( &this->parentId, &Status ) ) { 
		uuidBuf3 = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &this->parentId, &uuidBuf3 );
	}

	UUID host = *dStore->AgentGetHost( &this->id );
	int status = dStore->AgentGetStatus( &this->id );

	if ( UuidIsNil( &host, &Status ) ) { 
		uuidBuf4 = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &host, &uuidBuf4 );
	}
	
	return swprintf_s( buf + offset, len - offset, 
		L"id: %s\r\ntype: %s\r\nparent: %s\r\ninstance: %d\r\nhost: %s\r\nstatus: %d", uuidBuf1, uuidBuf2, uuidBuf3, this->agentInstance, uuidBuf4, status );
}

int GODDBAgent::_VisAddPath( int id, int count, float *x, float *y ) {
	int localId;
	std::map<int, int>::iterator iI;

	// ensure id is valid (i.e. not already taken)
	iI = this->visPaths.find(id);
	if ( iI != this->visPaths.end() ) 
		return 1;
	
	// create path
	localId = this->visualizer->newPath( count, x, y );
	if ( localId == -1 )
		return 1; // path failed

	// save id
	this->visPaths[id] = localId;

	return 0;
}

int GODDBAgent::_VisRemovePath( int id ) {
	std::map<int, int>::iterator iI;

	// ensure id is valid
	iI = this->visPaths.find(id);
	if ( iI == this->visPaths.end() ) 
		return 1;

	// delete path
	this->visualizer->deletePath( iI->second );

	// remove from list
	this->visPaths.erase( iI );

	return 0;
}

int GODDBAgent::_VisExtendPath( int id, int count, float *x, float *y ) {
	return 0;
}

int GODDBAgent::_VisUpdatePath( int id, int count, int *nodes, float *x, float *y ) {
	return 0;
}

int GODDBAgent::_VisAddStaticObject( int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	int i;
	int localId;
	int *localPaths;
	std::map<int, int>::iterator iP;
	std::map<int, int>::iterator iI;
	
	// ensure id is valid (i.e. not already taken)
	iI = this->visObjects.find(id);
	if ( iI != this->visObjects.end() ) 
		return 1;	
	
	// ensure path ids are valid
	localPaths = (int *)malloc(sizeof(int)*count);
	if ( !localPaths )
		return 1;

	for ( i=0; i<count; i++ ) {
		iP = this->visPaths.find(paths[i]);
		if ( iP == this->visPaths.end() ) 
			return 1;
		localPaths[i] = iP->second;
	}
	
	// create object
	localId = this->visualizer->newStaticObject( x, y, r, s, count, localPaths, colours, lineWidths, solid, name );
	free( localPaths );

	if ( localId == -1 )
		return 1; // path failed

	// save id
	this->visObjects[id] = localId;

	return 0;
}

int GODDBAgent::_VisRemoveObject( int id ) {
	std::map<int, int>::iterator iI;

	// ensure id is valid
	iI = this->visObjects.find(id);
	if ( iI == this->visObjects.end() ) 
		return 1;

	// delete path
	this->visualizer->deleteObject( iI->second, true ); // always keep paths

	// remove from list
	this->visObjects.erase( iI );

	return 0;
}

int GODDBAgent::_VisUpdateObject( int id, float x, float y, float r, float s ) {
	std::map<int, int>::iterator iI;

	// ensure id is valid
	iI = this->visObjects.find(id);
	if ( iI == this->visObjects.end() ) 
		return 1;

	// update object
	this->visualizer->updateStaticObject( iI->second, x, y, r, s );

	return 0;
}

int GODDBAgent::_VisSetObjectVisible( int id, char visible ) {
	std::map<int, int>::iterator iI;

	// ensure id is valid
	iI = this->visObjects.find(id);
	if ( iI == this->visObjects.end() ) 
		return 1;

	if ( visible == -1 )
		this->visualizer->toggleObject( iI->second );
	else if ( visible == 1 )
		this->visualizer->showObject( iI->second );
	else
		this->visualizer->hideObject( iI->second );

	return 0;
}


//*****************************************************************************
// GODDBRegion

GODDBRegion::GODDBRegion( UUID *id, AgentMirrorGUI *mirror ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	this->classType = DDB_REGION;

	dStore->GetRegion( id, &lds, &nilUUID );

	lds.rewind();

	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackChar() == DDBR_OK ) { // success
		this->x = lds.unpackFloat32();
		this->y = lds.unpackFloat32();
		this->w = lds.unpackFloat32();
		this->h = lds.unpackFloat32();
	}

	lds.unlock();

	this->visColour[0] = 1;
	this->visColour[1] = 0;
	this->visColour[2] = 0;
	this->visHighlight[0] = 0;
	this->visHighlight[1] = 1;
	this->visHighlight[2] = 0;

	float pathX[] = { 0, this->w, this->w, 0, 0 };
	float pathY[] = { 0, 0, this->h, this->h, 0 };
	
	int path = this->visualizer->newPath( 5, pathX, pathY );
	this->visObj = this->visualizer->newStaticObject( this->x, this->y, 0, 1, path, this->visColour, 2 );
	if ( !this->visible )
		this->visualizer->hideObject( this->visObj );
}

GODDBRegion::~GODDBRegion() {
	this->visualizer->deleteObject( this->visObj );
}

int GODDBRegion::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"Region" );
	return 0;
}

int GODDBRegion::getAbstract( WCHAR *buf, int len, int offset ) {
	return swprintf_s( buf + offset, len - offset, 
		L"x: %f\r\ny: %f\r\nw: %f\r\nh: %f", 
		this->x, this->y, this->w, this->h );
}


//*****************************************************************************
// GODDBLandmark

GODDBLandmark::GODDBLandmark( UUID *id, AgentMirrorGUI *mirror, int *pathLib ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	this->classType = DDB_LANDMARK;

	this->pathLib = pathLib;

	dStore->GetLandmark( id, &lds, &nilUUID );

	lds.rewind();

	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackChar() == DDBR_OK ) { // success
		this->landmark = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
	}

	lds.unlock();

	if ( this->landmark.owner == nilUUID ) { // dynamic landmarks need to be handled elsewhere
		float trueColour[] = { 0.2f, 0.8f, 0.2f };
		if ( this->landmark.estimatedPos ) {
			this->visColour[0] = 1;
			this->visColour[1] = 0.8f;
			this->visColour[2] = 0;
		} else {
			this->visColour[0] = 1;
			this->visColour[1] = 0;
			this->visColour[2] = 1;
		}
		this->visHighlight[0] = 0.3f;
		this->visHighlight[1] = 0.3f;
		this->visHighlight[2] = 1;

		if ( this->landmark.estimatedPos ) {
			float x = -1000, y = -1000;
			if ( this->landmark.posInitialized ) {
				x = this->landmark.x;
				y = this->landmark.y;
				this->visTrue = -1; // too late to get the true position
			} else {
				this->visTrue = this->visualizer->newStaticObject( this->landmark.x, this->landmark.y, 0, 0.1f, this->pathLib[PATHLIB_X], trueColour, 1 );
			}
			this->visObj = this->visualizer->newStaticObject( x, y, 0, 0.1f, this->pathLib[PATHLIB_X], this->visColour, 1 );
			this->visCov[0] = this->visualizer->newStaticObject( x - this->landmark.P, y, 0, max(0.00001f, this->landmark.P*2), this->pathLib[PATHLIB_LINE], this->visColour, 1 );
			this->visCov[1] = this->visualizer->newStaticObject( x, y - this->landmark.P, fM_PI_2, max(0.00001f, this->landmark.P*2), this->pathLib[PATHLIB_LINE], this->visColour, 1 );
		} else {
			this->visObj = this->visualizer->newStaticObject( this->landmark.x, this->landmark.y, 0, 0.1f, this->pathLib[PATHLIB_X], this->visColour, 1 );
		}
		if ( !this->visible ) {
			this->visualizer->hideObject( this->visObj );
			if ( this->landmark.estimatedPos ) {
				if ( this->visTrue != -1 ) this->visualizer->hideObject( this->visTrue );
				this->visualizer->hideObject( this->visCov[0] );
				this->visualizer->hideObject( this->visCov[1] );
			}
		}
	} else { // find owner
		GUIObjectBase *obj = mirror->getObject( &this->landmark.owner );
		if ( obj ) {
			if ( obj->getClass() == DDB_AVATAR ) {
				((GODDBAvatar *)obj)->addLandmark( id, this->landmark.x, this->landmark.y );
			}
		} else { // if you don't find the owner, the owner should check for landmarks when it is created
			// do nothing
		}
	}

}

GODDBLandmark::~GODDBLandmark() {
	if ( this->visObj != -1 )
		this->visualizer->deleteObject( this->visObj, true );
	if ( !this->landmark.collected && this->landmark.estimatedPos ) {
		if ( this->visTrue != -1 ) this->visualizer->deleteObject( this->visTrue, true );
		this->visualizer->deleteObject( this->visCov[0], true );
		this->visualizer->deleteObject( this->visCov[1], true );
	}
}


int GODDBLandmark::getParent( UUID *id ) {
	RPC_STATUS Status;

	if ( UuidIsNil( &this->landmark.owner, &Status ) )
		return 1;

	*id = this->landmark.owner;

	return 0;
}

int GODDBLandmark::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"Landmark %2d", this->landmark.code );
	return 0;
}


int GODDBLandmark::getAbstract( WCHAR *buf, int len, int offset ) {
	RPC_STATUS Status;
	RPC_WSTR uuidBuf;

	if ( UuidIsNil( &this->landmark.owner, &Status ) ) { 
		uuidBuf = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &this->landmark.owner, &uuidBuf );
	}

	return swprintf_s( buf + offset, len - offset, 
		L"code: %d\r\nx: %f\r\ny: %f\r\nheight: %f\r\nelevation: %f\r\nowner: %s", 
		this->landmark.code, this->landmark.x, this->landmark.y, this->landmark.height, this->landmark.elevation, uuidBuf );
}

int GODDBLandmark::setVisibility( bool vis ) {

	if ( GUIObjectBase::setVisibility( vis ) )
		return 0; // no change

	if ( !this->landmark.collected && this->landmark.estimatedPos ) {
		if ( vis ) { // show
			if ( this->visTrue != -1 ) this->visualizer->showObject( this->visTrue );
			this->visualizer->showObject( this->visCov[0] );
			this->visualizer->showObject( this->visCov[1] );
		} else { // hide
			if ( this->visTrue != -1 ) this->visualizer->hideObject( this->visTrue );
			this->visualizer->hideObject( this->visCov[0] );
			this->visualizer->hideObject( this->visCov[1] );		
		}
	}

	return 0;
}

int GODDBLandmark::updatePosEstimation() {
	DataStream lds;

	dStore->GetLandmark( &this->id, &lds, &nilUUID );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread
	if ( lds.unpackChar() == DDBR_OK ) { // success
		this->landmark = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
	}

	if ( !this->landmark.collected ) {
		this->visualizer->updateStaticObject( this->visObj, this->landmark.x, this->landmark.y, 0, 0.1f );
		this->visualizer->updateStaticObject( this->visCov[0], this->landmark.x - this->landmark.P, this->landmark.y, 0, max(0.00001f, this->landmark.P*2) );
		this->visualizer->updateStaticObject( this->visCov[1], this->landmark.x, this->landmark.y - this->landmark.P, fM_PI_2, max(0.00001f, this->landmark.P*2) );
	}

	return 0;
}

int GODDBLandmark::collected() {
	DataStream lds;

	dStore->GetLandmark( &this->id, &lds, &nilUUID );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread
	if ( lds.unpackChar() == DDBR_OK ) { // success
		this->landmark = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
	}

	this->visualizer->deleteObject( this->visObj, true );
	this->visObj = -1;
	this->visualizer->deleteObject( this->visCov[0], true );
	this->visCov[0] = -1;
	this->visualizer->deleteObject( this->visCov[1], true );
	this->visCov[1] = -1;

	if ( this->visTrue != -1 ) {
		this->visualizer->deleteObject( this->visTrue, true );
		this->visTrue = -1;
	}

	return 0;
}

//*****************************************************************************
// GODDBMapPOG

GODDBMapPOG::GODDBMapPOG( UUID *id, AgentMirrorGUI *mirror ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	this->classType = DDB_MAP_PROBOCCGRID;

	float xlow, xhigh, ylow, yhigh;
	dStore->POGGetInfo( id, &lds, &nilUUID );

	lds.rewind();

	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackChar() == DDBR_OK ) { // success
		this->tileSize = lds.unpackFloat32();
		this->resolution = lds.unpackFloat32();
		this->stride = lds.unpackInt32();

		xlow = lds.unpackFloat32();
		xhigh = lds.unpackFloat32();
		ylow = lds.unpackFloat32();
		yhigh = lds.unpackFloat32();

		this->updateRegion( xlow, ylow, xhigh-xlow, yhigh-ylow );
	}

	lds.unlock();
}

GODDBMapPOG::~GODDBMapPOG() {

	while ( !this->tiles.empty() ) {
		if ( this->visible ) {
			this->hideTiles();
		}
		FreeImage( this->tiles.begin()->second );
		this->tiles.erase( this->tiles.begin() );
	}

}

int GODDBMapPOG::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"MapPOG" );
	return 0;
}

int GODDBMapPOG::getAbstract( WCHAR *buf, int len, int offset ) {
	return swprintf_s( buf + offset, len - offset, 
		L"tileSize: %f\r\nresolution: %f\r\nstride: %d\r\n# of tiles: %d", 
		this->tileSize, this->resolution, this->stride, this->tiles.size() );
}

int GODDBMapPOG::prepareRClick( CMenu *menu ) {


	menu->AppendMenu( MF_STRING | MF_ENABLED, MI_POG, L"POG Item" );

	GUIObjectBase::prepareRClick( menu );

	return 0;
}

int GODDBMapPOG::rClick( UINT result ) {
	
	if ( !GUIObjectBase::rClick( result ) )
		return 0;

	return 0;
}

int GODDBMapPOG::setVisibility( bool vis ) {

	if ( GUIObjectBase::setVisibility( vis ) )
		return 0; // no change

	if ( vis ) { // show
		std::map<int,FIMAGE *>::iterator it;
		for ( it = this->tiles.begin(); it != this->tiles.end(); it++ ) {
			this->visualizeTile( it->first, it->second );
		}		 
	} else { // hide
		this->hideTiles();
	}

	return 0;
}

int GODDBMapPOG::visualizeTile( int id, FIMAGE *img ) {
	short u, v; // tile coord
	int obj;

	// reverse tile id to get location
	DDB_REVERSE_TILE_ID( id, &u, &v );
	
	// insert image
	obj = this->visualizer->newfImage( u*this->tileSize, v*this->tileSize, this->resolution, 0, 127, img );
	if ( obj == -1 )
		return 1;
	this->tilesVis[id] = obj;

	return 0;
}

int GODDBMapPOG::hideTiles() {
	std::map<int,int>::iterator it;
	for ( it = this->tilesVis.begin(); it != this->tilesVis.end(); it++ ) {
		// remove image
		this->visualizer->deletefImage( it->second );
	}			
	this->tilesVis.clear();

	return 0;
}

int GODDBMapPOG::updateRegion( float x, float y, float w, float h ) {
	DataStream lds;

	// get data from dStore
	this->dStore->POGGetRegion( &this->id, x, y, w, h, &lds, &nilUUID );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if ( lds.unpackChar() != DDBR_OK ) {
		lds.unlock();
		return 1;
	}

	x = lds.unpackFloat32();
	y = lds.unpackFloat32();
	w = lds.unpackFloat32();
	h = lds.unpackFloat32();
	float res = lds.unpackFloat32();

	// update images
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int dataStride;
	float tileSize; // tileSize in cell space
	int tileId, lastTileId; // tile ids
	FIMAGE *tile;
	int ii, jj; // loop vars

	std::map<int,FIMAGE *>::iterator iterTile;

	// convert coords and tileSize into cell space
	x = round(x/res);
	y = round(y/res);
	w = round(w/res);
	h = round(h/res);
	tileSize = round(this->tileSize/res);

	dataStride = (int)h;

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
				iterTile = (this->tiles).find(tileId);
				if ( iterTile == (this->tiles).end() ) {			
					// this tile doesn't exist yet, so make it!
					tile = NewImage( this->stride, this->stride );
					if ( !tile ) {
						lds.unlock();
						return 1; // malloc failed
					}
					for ( ii=0; ii<this->stride; ii++ ) {
						for ( jj=0; jj<this->stride; jj++ ) {
							Px(tile,jj,ii) = 0.5f;
						}
					}
					if ( this->visible ) {
						this->visualizeTile( tileId, tile );
					}
					(this->tiles)[tileId] = tile;
				} else {
					tile = iterTile->second;
				}

				lastTileId = tileId;
			}

			// update cell
			Px(tile,j,i) = lds.unpackFloat32();
		}
	}

	lds.unlock();

	return 0;
}

//*****************************************************************************
// GODDBAvatar

GODDBAvatar::GODDBAvatar( UUID *id, AgentMirrorGUI *mirror, int *pathLib ) : GUIObjectBase( id, mirror ) {
	DataStream lds;
	
	this->classType = DDB_AVATAR;
	this->pathLib = pathLib;

	dStore->AvatarGetInfo( id, DDBAVATARINFO_RTYPE | DDBAVATARINFO_RSTATUS | DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII, &lds, &nilUUID );

	lds.rewind();

	lds.unpackData(sizeof(UUID)); // discard thread
	
	if ( lds.unpackChar() == DDBR_OK ) { // success
		lds.unpackInt32(); // infoFlags
		strcpy_s( this->info.type, sizeof(this->info.type), lds.unpackString() );
		this->info.status = lds.unpackInt32();
		lds.unpackUUID( &this->info.pf );
		this->info.innerRadius = lds.unpackFloat32();
		this->info.outerRadius = lds.unpackFloat32();
	}

	lds.unlock();

	// get pose
	this->x = this->y = this->r = 0;

	// claim landmarks
	UUID lmId;
	DDBLandmark lm;
	mirror->claimLandmarks( id, &lds );
	lds.rewind();
	while ( lds.unpackBool() ) {
		lds.unpackUUID( &lmId );
		lm = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
		this->addLandmark( &lmId, lm.x, lm.y );
	}
	lds.unlock();

	float effectiveParticleNum;
	dStore->PFGetInfo( &this->info.pf, DDBPFINFO_STATE_SIZE | DDBPFINFO_CURRENT, NULL, &lds, &nilUUID, &effectiveParticleNum );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if( lds.unpackChar() == DDBR_OK ) {
		lds.unpackInt32(); // infoFlags
		int stateSize = lds.unpackInt32();
		lds.unpackData( sizeof(_timeb) ); // time
		float *state = (float *)lds.unpackData( sizeof(float)*stateSize );
		this->x = state[0];
		this->y = state[1];
		this->r = state[2];
	}

	lds.unlock();

	// watch pf
	mirror->ddbAddWatcherCB( NEW_MEMBER_CB(GODDBAvatar,cbDDBWatcher), &this->info.pf );

	this->visColour[0] = 0;
	this->visColour[1] = 1;
	this->visColour[2] = 0;
	this->visHighlight[0] = 1;
	this->visHighlight[1] = 0;
	this->visHighlight[2] = 1;

	this->visObj = this->visualizer->newDynamicObject( &this->x, &this->y, &this->r, this->pathLib[PATHLIB_AVATAR], this->visColour, 2 );
	if ( !this->visible )
		this->visualizer->hideObject( this->visObj );
}

GODDBAvatar::~GODDBAvatar() {
	this->visualizer->deleteObject( this->visObj, true );

	// clean landmarks
	std::map<UUID,GODDBAvatar_LandmarkInfo,UUIDless>::iterator iL;
	for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
		this->visualizer->deleteObject( iL->second.visObj, true );
	}
}

int GODDBAvatar::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"Avatar %hs", this->info.type );
	return 0;
}

int GODDBAvatar::getAbstract( WCHAR *buf, int len, int offset ) {
	DataStream lds;

	dStore->AvatarGetInfo( &this->id, DDBAVATARINFO_RCARGO, &lds, &nilUUID );
	lds.rewind();
	lds.unpackData(sizeof(UUID)); // thread
	lds.unpackChar(); // result
	lds.unpackInt32(); // infoFlags

	int i;
	int cargoCount = lds.unpackInt32();
	char cargoStr[256], code;
	cargoStr[0] = 0;
	for ( i = 0; i < cargoCount; i++ ) {
		code = lds.unpackChar();
		sprintf_s( cargoStr + 5*i, 256-5*i, "%4d ", (int)code );
	}

	RPC_STATUS Status;
	RPC_WSTR uuidBuf1;

	if ( UuidIsNil( &this->id, &Status ) ) { 
		uuidBuf1 = (RPC_WSTR)L"N/A";
	} else {
		UuidToString( &this->id, &uuidBuf1 );
	}

	return swprintf_s( buf + offset, len - offset, 
		L"id: %s\r\ntype: %hs\r\nstatus: %d\r\npose: %.2f %.2f %.2f (%.2f %.2f ft)\r\ncargo (%d): %hs", 
		uuidBuf1, this->info.type, this->info.status, this->x, this->y, this->r*180/fM_PI, this->x / 0.3048f, this->y / 0.3048f, cargoCount, cargoStr );
}

int GODDBAvatar::setVisibility( bool vis ) {
	std::map<UUID,GODDBAvatar_LandmarkInfo,UUIDless>::iterator iL;

	if ( GUIObjectBase::setVisibility( vis ) )
		return 0; // no change

	if ( vis ) { // show
		for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
			this->visualizer->showObject( iL->second.visObj );
		}
	} else { // hide
		for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
			this->visualizer->hideObject( iL->second.visObj );
		}
	}

	return 0;
}

int GODDBAvatar::addLandmark( UUID *id, float ox, float oy ) {
	float sn, cs;

	this->landmarks[*id].ox = ox;
	this->landmarks[*id].oy = oy;

	// calc landmark pos
	sn = sin(this->r);
	cs = cos(this->r);
	this->landmarks[*id].x = this->x + this->landmarks[*id].x*cs - this->landmarks[*id].y*sn;
	this->landmarks[*id].y = this->y + this->landmarks[*id].x*sn + this->landmarks[*id].y*cs;
	this->landmarks[*id].s = 0.1f;

	float colour[] = { 1, 0, 1 };
	this->landmarks[*id].visObj = this->visualizer->newDynamicObject( &this->landmarks[*id].x, &this->landmarks[*id].y, &this->r, &this->landmarks[*id].s, this->pathLib[PATHLIB_X], colour, 1 );
	if ( !this->visible )
		this->visualizer->hideObject( this->landmarks[*id].visObj );
	
	return 0;
}

bool GODDBAvatar::cbDDBWatcher( void *vpds ) {
	DataStream lds;
	DataStream *ds = (DataStream *)vpds;

	int type;
	UUID id;
	char evt;

	type = ds->unpackInt32();
	ds->unpackUUID( &id );
	evt = ds->unpackChar();

	switch ( type ) {
	case DDB_PARTICLEFILTER:
		if ( evt == DDBE_PF_RESAMPLE 
  		  || evt == DDBE_PF_PREDICTION
		  || evt == DDBE_PF_CORRECTION ) {
		    if ( evt == DDBE_PF_PREDICTION ) {
				ds->unpackData( sizeof(_timeb) ); // time
				if ( ds->unpackChar() ) 
					break; // no change
		    }
			// update pose
			float effectiveParticleNum;
			dStore->PFGetInfo( &this->info.pf, DDBPFINFO_STATE_SIZE | DDBPFINFO_CURRENT, NULL, &lds, &nilUUID, &effectiveParticleNum );

			lds.rewind();
			lds.unpackData(sizeof(UUID)); // discard thread

			if( lds.unpackChar() == DDBR_OK ) {
				lds.unpackInt32(); // infoFlags
				int stateSize = lds.unpackInt32();
				lds.unpackData( sizeof(_timeb) ); // time
				float *state = (float *)lds.unpackData( sizeof(float)*stateSize );
				this->x = state[0];
				this->y = state[1];
				this->r = state[2];
			}

			lds.unlock();

			// update landmarks
			float sn, cs;
			std::map<UUID,GODDBAvatar_LandmarkInfo,UUIDless>::iterator iL;
			for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
				sn = sin(this->r);
				cs = cos(this->r);
				iL->second.x = this->x + iL->second.ox*cs - iL->second.oy*sn;
				iL->second.y = this->y + iL->second.ox*sn + iL->second.oy*cs;
			}

		}
		break;
	case DDB_AVATAR:
		
		break;
	default:
		// Unhandled type?!
		return 0;
	}

	return 0;
}

//*****************************************************************************
// GODDBParticleFilter

GODDBParticleFilter::GODDBParticleFilter( UUID *id, AgentMirrorGUI *mirror, int *pathLib ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	this->classType = DDB_PARTICLEFILTER;

	int i;
	this->numParticles = this->stateSize = 0;
	this->state = this->weight = 0;

	dStore->PFGetInfo( id, DDBPFINFO_USECURRENTTIME | DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT, NULL, &lds, &nilUUID, &this->effectiveParticleNum );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if( lds.unpackChar() == DDBR_OK ) {
		lds.unpackInt32(); // infoFlags
		lds.unpackData( sizeof(_timeb) ); // time
		this->numParticles = lds.unpackInt32();
		this->stateSize = lds.unpackInt32();

		this->state = (float *)malloc(sizeof(float)*this->numParticles*this->stateSize);
		this->weight = (float *)malloc(sizeof(float)*this->numParticles);

		if ( !this->state || !this->weight ) 
			throw "GODDBParticleFilter::GODDBParticleFilter: malloc failed (state/weight)";

		memcpy( this->state, lds.unpackData( sizeof(float)*this->numParticles*this->stateSize ), sizeof(float)*this->numParticles*this->stateSize );
		memcpy( this->weight, lds.unpackData( sizeof(float)*this->numParticles ), sizeof(float)*this->numParticles );
	}

	lds.unlock();

	this->pathLib = pathLib;

	float color[3] = { 0.1f, 0.1f, 0.6f };
	
	this->pObj = (int *)malloc( this->numParticles*sizeof(int) );
	if ( !this->pObj )
		throw "GODDBParticleFilter::GODDBParticleFilter: malloc failed (pObj)";	
	this->visScale = (float *)malloc( this->numParticles*sizeof(float) );
	if ( !this->visScale )
		throw "GODDBParticleFilter::GODDBParticleFilter: malloc failed (visScale)";
	
	float *pState = this->state;
	for ( i=0; i<this->numParticles; i++ ) {
		this->visScale[i] = this->weight[i]*numParticles;
		this->pObj[i] = this->visualizer->newDynamicObject( &pState[0], &pState[1], &pState[2], &this->visScale[i], this->pathLib[PATHLIB_PARTICLE], color, 1 );

		if ( !this->visible ) 
			this->visualizer->hideObject( this->pObj[i] );

		pState += stateSize;
	}
}


GODDBParticleFilter::~GODDBParticleFilter() {
	int i;

	if ( this->state )
		free( this->state );
	if ( this->weight )
		free( this->weight );

	for ( i=0; i<this->numParticles; i++ ) {
		this->visualizer->deleteObject( this->pObj[i], true );
	}

	free( this->pObj );
	free( this->visScale );
}

int GODDBParticleFilter::getParent( UUID *id ) {
	DataStream lds;

	UUID *ownerId;

	ownerId = this->dStore->PFGetOwner( &this->id ); 

	*id = *ownerId;

/*	// get avatar agent
	this->dStore->AvatarGetInfo( ownerId, DDBAVATARINFO_RAGENT, &lds, &nilUUID );
	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread
	if ( lds.unpackChar() != DDBR_OK ) {
		lds.unlock();
		return -1; // not currently available
	}
	lds.unpackInt32(); // info flags
	lds.unpackUUID( id );
	lds.unlock();
*/
	return 0; 
}

int GODDBParticleFilter::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"Particle Filter" );
	return 0;
}

int GODDBParticleFilter::getAbstract( WCHAR *buf, int len, int offset ) {
	return swprintf_s( buf + offset, len - offset, 
		L"numParticles: %d\r\nstateSize: %d\r\neffeciveParticleNum: %f\r\n", 
		this->numParticles, this->stateSize, this->effectiveParticleNum );
}

int GODDBParticleFilter::setVisibility( bool vis ) {
	int i;

	if ( GUIObjectBase::setVisibility( vis ) )
		return 0; // no change

	if ( vis ) { // show
		for ( i=0; i<this->numParticles; i++ ) {
			this->visualizer->showObject( this->pObj[i] );
		}
	} else { // hide
		for ( i=0; i<this->numParticles; i++ ) {
			this->visualizer->hideObject( this->pObj[i] );
		}
	}

	return 0;
}

int GODDBParticleFilter::updateFilter( int evt, DataStream *ds ) {
	DataStream lds;

	int i;

	if ( evt == DDBE_PF_PREDICTION ) {
		ds->unpackData( sizeof(_timeb) ); // time
		if ( ds->unpackChar() ) 
			return 0; // no change
	}

	// update state
	dStore->PFGetInfo( &this->id, DDBPFINFO_USECURRENTTIME | DDBPFINFO_STATE | DDBPFINFO_WEIGHT, NULL, &lds, &nilUUID, &this->effectiveParticleNum );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if( lds.unpackChar() == DDBR_OK ) {
		lds.unpackInt32(); // infoFlags
		lds.unpackData( sizeof(_timeb) ); // time
		memcpy( this->state, lds.unpackData( sizeof(float)*this->numParticles*this->stateSize ), sizeof(float)*this->numParticles*this->stateSize );
		memcpy( this->weight, lds.unpackData( sizeof(float)*this->numParticles ), sizeof(float)*this->numParticles );
	
		// update visScale
		for ( i=0; i<this->numParticles; i++ ) {
			this->visScale[i] = this->weight[i]*this->numParticles;
		}
	
	}

	lds.unlock();

	return 0;
}

//*****************************************************************************
// GODDBSensor

GODDBSensor::GODDBSensor( UUID *id, AgentMirrorGUI *mirror ) : GUIObjectBase( id, mirror ) {
	DataStream lds;

	this->classType = DDB_SENSORS;

	this->id = *id;
	
	this->dStore->SensorGetInfo( &this->id, DDBSENSORINFO_TYPE | DDBSENSORINFO_AVATAR | DDBSENSORINFO_PF, &lds, &nilUUID );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if( lds.unpackChar() == DDBR_OK ) {
		lds.unpackInt32(); // infoFlags
		this->info.type = lds.unpackInt32();
		lds.unpackUUID( &this->info.avatar );
		lds.unpackUUID( &this->info.pf );
	}

	lds.unlock();
}

GODDBSensor::~GODDBSensor() {
	
}

int GODDBSensor::getName( WCHAR *buf, int len ) {
	swprintf_s( buf, len, L"Sensor (Type %d)", this->info.type );
	return 0;
}

int GODDBSensor::getAbstract( WCHAR *buf, int len, int offset ) {
	DataStream lds;

	RPC_WSTR savatar, spf;
	UuidToString( &this->info.avatar, &savatar );
	UuidToString( &this->info.pf, &spf );

	int numReadings = 0;

	this->dStore->SensorGetInfo( &this->id, DDBSENSORINFO_READINGCOUNT, &lds, &nilUUID );

	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread

	if( lds.unpackChar() == DDBR_OK ) {
		lds.unpackInt32(); // infoFlags
		numReadings = lds.unpackInt32();
	}

	lds.unlock();
	
	return swprintf_s( buf + offset, len - offset, 
		L"type: %d\r\navatar: %s\r\npf: %s\r\n# readings: %d", 
		this->info.type, savatar, spf, numReadings );
}