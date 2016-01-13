// AgentMirrorGUI.cpp
//

#include "stdafx.h"

#include "AgentMirrorGUI.h"
#include "AgentMirrorGUIVersion.h"

#include "..\\autonomic\\AgentHostVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentMirrorGUI

//-----------------------------------------------------------------------------
// Constructor	
AgentMirrorGUI::AgentMirrorGUI( ThinSocket *hostSocket, Visualize *visualizer, CTreeCtrl *objectTree, CEdit *absText ) : AgentMirror( hostSocket ) {
	
	sprintf_s( this->agentType.name, sizeof(this->agentType.name), "AgentMirrorGUI" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentMirrorGUI_UUID), &typeId );
	this->agentType.uuid = typeId;
	this->agentType.instance = -1;
	
	this->visualizer = visualizer;
	this->objectTree = objectTree;
	this->absText = absText;

	this->lastSelected = NULL;

	// get category handles
	catAgent = this->objectTree->GetNextItem( TVI_ROOT, TVGN_CHILD );
	catDDB = this->objectTree->GetNextItem( catAgent, TVGN_NEXT );
	catDDBLandmark = this->objectTree->GetNextItem( catDDB, TVGN_CHILD );

	// clear agentGraphTO
	this->agentGraphT0.time = -1;
	
	// watch all items
	this->ddbAddWatcherCB( NEW_MEMBER_CB(AgentMirrorGUI,cbDDBWatcher), 0xFFFF );

	// prepare path library
	float PATHLIB_AVATAR_x[] = { -0.08f, 0.12f, -0.08f, -0.08f };
	float PATHLIB_AVATAR_y[] = { 0.08f, 0.0f, -0.08f, 0.08f };
	this->pathLib[PATHLIB_AVATAR] = this->visualizer->newPath( 4, PATHLIB_AVATAR_x, PATHLIB_AVATAR_y );
	float PATHLIB_PARTICLE_x[] = { 0.03f, 0.0f, 0.0f };
	float PATHLIB_PARTICLE_y[] = { 0.0f, 0.0f, -0.01f };
	this->pathLib[PATHLIB_PARTICLE] = this->visualizer->newPath( 3, PATHLIB_PARTICLE_x, PATHLIB_PARTICLE_y );
	float PATHLIB_X_x[] = { -0.5f, 0.5f, 0.0f, -0.5f, 0.5f };
	float PATHLIB_X_y[] = { -0.5f, 0.5f, 0.0f, 0.5f, -0.5f };
	this->pathLib[PATHLIB_X] = this->visualizer->newPath( 5, PATHLIB_X_x, PATHLIB_X_y );
	float PATHLIB_LINE_x[] = { 0, 1 };
	float PATHLIB_LINE_y[] = { 0, 0 };
	this->pathLib[PATHLIB_LINE] = this->visualizer->newPath( 2, PATHLIB_LINE_x, PATHLIB_LINE_y );
	
}

//-----------------------------------------------------------------------------
// Destructor
AgentMirrorGUI::~AgentMirrorGUI() {
	int i;

	if ( this->started ) {
		this->stop();
	}

	// clean up tree control
	HTREEITEM h;
	h = this->objectTree->GetNextItem( catAgent, TVGN_CHILD );
	while ( h != NULL ) {
		this->objectTree->DeleteItem( h );
		h = this->objectTree->GetNextItem( catAgent, TVGN_CHILD );
	}
	h = this->objectTree->GetNextItem( catDDB, TVGN_CHILD );
	while ( h != NULL ) {
		this->objectTree->DeleteItem( h );
		h = this->objectTree->GetNextItem( catDDB, TVGN_CHILD );
	}
	catDDBLandmark = this->objectTree->InsertItem( L"Landmarks", catDDB );

	// clean up objects
	std::map<UUID,ObjectInfo,UUIDless>::iterator iterO = this->objects.begin();
	while ( !this->objects.empty() ) {
		delete this->objects.begin()->second.object;
		this->objects.erase( this->objects.begin() );
	}

	// clean up path library
	for ( i=0; i<PATHLIB_HIGH; i++ )
		this->visualizer->deletePath( this->pathLib[i] );

}

//-----------------------------------------------------------------------------
// Configure

int AgentMirrorGUI::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "GUI Logs\\AgentMirrorGUI %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentMirrorGUI %.2d.%.2d.%.5d.%.2d", AgentMirrorGUI_MAJOR, AgentMirrorGUI_MINOR, AgentMirrorGUI_BUILDNO, AgentMirrorGUI_EXTEND );
	}

	if ( AgentMirror::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentMirrorGUI::start() {

	if ( AgentMirror::start() ) 
		return 1;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentMirrorGUI::stop() {

	// clean up agentGraph
	while ( !this->agentGraphStrings.empty() ) {
		this->visualizer->deleteString( this->agentGraphStrings.front() );
		this->agentGraphStrings.pop_front();
	}
	while ( !this->agentGraphObjects.empty() ) {
		this->visualizer->deleteObject( this->agentGraphObjects.front(), true );
		this->agentGraphObjects.pop_front();
	}

	this->started = false;

	return AgentMirror::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentMirrorGUI::step() {
	
	// check agentGraph
	if ( this->agentGraphT0.time != -1 ) {
		_timeb tb;
		apb->apb_ftime_s( &tb );
		int dt = (int)((tb.time - this->agentGraphTU.time)*1000 + (tb.millitm - this->agentGraphTU.millitm));
		if ( dt > 2000 )
			this->agentGraphUpdateVis();
	}

	return AgentMirror::step();
}

//-----------------------------------------------------------------------------
// Tree management

int AgentMirrorGUI::treeSelectionChanged( HTREEITEM handle ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator it;

	if ( this->lastSelected != NULL )
		this->lastSelected->object->setHighlight( false );

	it = this->objects.begin();
	while ( it != this->objects.end() ) {
		if ( it->second.handle == handle ) {
			it->second.object->setHighlight( true );
			this->lastSelected = &it->second;
			WCHAR buf[1024];
			it->second.object->getAbstract( buf, 1024 );
			this->absText->SetWindowText( buf );
			return 0;
		} else {
			this->lastSelected = NULL;
		}
		it++;
	}

	this->absText->SetWindowText( L"" );

	return 0;
}

int AgentMirrorGUI::treeCheckChanged( HTREEITEM handle ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator it;

	it = this->objects.begin();
	while ( it != this->objects.end() ) {
		if ( it->second.handle == handle ) {

			it->second.object->setVisibility( this->objectTree->GetCheck( handle ) == 0 );
			
			return 0;
		}
		it++;
	}

	return 0;
}

int AgentMirrorGUI::treeRClick( HTREEITEM handle, CPoint *clickLoc ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator it;

	it = this->objects.begin();
	while ( it != this->objects.end() ) {
		if ( it->second.handle == handle ) {

			CMenu menu;
			menu.CreatePopupMenu();
			if ( it->second.object->prepareRClick( &menu ) )
				return 1; // error

			UINT result = (UINT)menu.TrackPopupMenu( TPM_LEFTALIGN | TPM_RETURNCMD, clickLoc->x, clickLoc->y, this->objectTree );

			it->second.object->rClick( result );

			// update checkmark in case visibility changed
			this->objectTree->SetCheck( it->second.handle, it->second.object->getVisibility() ? 1:0 );
			
			return 0;
		}
		it++;
	}

	return 0;
}

GUIObjectBase *AgentMirrorGUI::getObject( UUID *id ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator iO = this->objects.find( *id );

	if ( iO != this->objects.end() ) {
		return iO->second.object;
	} else {
		return NULL;
	}
}

int AgentMirrorGUI::claimLandmarks( UUID *id, DataStream *ds ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator iO;
	DDBLandmark lm;

	ds->reset();
	for ( iO = this->objects.begin(); iO != this->objects.end(); iO++ ) {
		if ( iO->second.object->getClass() == DDB_LANDMARK ) {
			lm = *((GODDBLandmark *)iO->second.object)->getLandmark();
			if ( lm.owner == *id ) {
				ds->packBool( 1 );
				ds->packUUID( (UUID *)&iO->first );
				ds->packData( &lm, sizeof(DDBLandmark) );
			}
		}
	}
	ds->packBool( 0 );

	return 0;
}

int AgentMirrorGUI::checkOrphanage( UUID *id ) {

	std::map<UUID,UUID,UUIDless>::iterator it;

	for ( it=this->orphans.begin(); it != this->orphans.end(); it++ ) {
		if ( it->second == nilUUID ) { // parent wasn't previously available, check again
			UUID parent;
			int ret;
			ret = this->objects[it->first].object->getParent( &parent );
			if ( ret == 0 ) { 
				it->second = parent; // got it
			}
		}
		if ( it->second == *id ) { // associate with parent
			WCHAR buf[256];
			this->objects[it->first].object->getName( buf, 256 );
			this->objectTree->DeleteItem( this->objects[it->first].handle );
			this->objects[it->first].handle = this->objectTree->InsertItem( buf, this->objects[*id].handle );
			this->objectTree->SetCheck( this->objects[it->first].handle, this->objects[it->first].object->getVisibility() ? 1:0 );
		}
	}

	return 0;
}

int AgentMirrorGUI::copyChildren( HTREEITEM child, HTREEITEM parent, HTREEITEM after ) {
	std::map<UUID,ObjectInfo,UUIDless>::iterator it;

	while ( child ) {
		// find the object
		for ( it = this->objects.begin(); it != this->objects.end(); it++ ) {
			if ( it->second.handle == child )
				break;
		}
		if ( it == this->objects.end() )
			return 1; // how did this happen?

		// copy object
		WCHAR buf[256];
		it->second.object->getName( buf, 256 );
		it->second.handle = this->objectTree->InsertItem( buf, parent, after );

		this->objectTree->SetCheck( it->second.handle, it->second.object->getVisibility() ? 1:0 );

		after = it->second.handle;

		// copy children
		this->copyChildren( this->objectTree->GetNextItem( child, TVGN_CHILD ), it->second.handle, TVI_LAST );

		// next child
		child = this->objectTree->GetNextItem( child, TVGN_NEXT );
	}


	return 0;
}
int AgentMirrorGUI::cleanRemoveItem( UUID *id ) {

	// copy children
	this->copyChildren( this->objectTree->GetNextItem( this->objects[*id].handle, TVGN_CHILD ), 
		this->objectTree->GetNextItem( this->objects[*id].handle, TVGN_PARENT ), 
		this->objects[*id].handle );

	// delete old branches
	this->objectTree->DeleteItem( this->objects[*id].handle );

	return 0;
}

int AgentMirrorGUI::agentGraphStart( UUID *id, char *name, UUID *host ) {
	AgentGraphInfo info;
	_timeb tb;
	
	if ( this->agentGraphT0.time == -1 ) {
		apb->apb_ftime_s( &this->agentGraphT0 );
	}
	
	apb->apb_ftime_s( &tb );
	int dt = (int)((tb.time - this->agentGraphT0.time)*1000 + (tb.millitm - this->agentGraphT0.millitm));
	info.id = *id;
	strcpy_s( info.name, sizeof(info.name), name );
	info.start = dt;
	info.end = -1;
	info.endStatus = DDBAGENT_STATUS_READY;
	this->agentGraph[*dStore->AgentGetHost(id)].push_back( info );

	std::list<AgentGraphConnection>::iterator iC;
	for ( iC = this->agentGraphConnectionOpen.begin(); iC != this->agentGraphConnectionOpen.end(); iC++ ) {
		if ( iC->id == *id ) {
			iC->end = dt + 100; // give a bit extra so we don't get vertical lines
			iC->iEnd = &this->agentGraph[*dStore->AgentGetHost(id)].back();
			this->agentGraphConnection.push_back( *iC );
			this->agentGraphConnectionOpen.erase( iC );
			break;
		}
	}

	this->agentGraphUpdateVis();

	return 0;
}

int AgentMirrorGUI::agentGraphEnd( UUID *id, int status ) {
	_timeb tb;
	apb->apb_ftime_s( &tb );
	int dt = (int)((tb.time - this->agentGraphT0.time)*1000 + (tb.millitm - this->agentGraphT0.millitm));

	std::map<UUID,std::list<AgentGraphInfo>,UUIDless>::iterator iH;
	std::list<AgentGraphInfo>::iterator iA;
	for ( iH = this->agentGraph.begin(); iH != this->agentGraph.end(); iH++ ) {
		for ( iA = iH->second.begin(); iA != iH->second.end(); iA++ ) {
			if ( iA->id == *id && iA->end == -1 ) {
				iA->end = dt;
				iA->endStatus = status;

				if ( status == DDBAGENT_STATUS_FROZEN || status == DDBAGENT_STATUS_FAILED ) {
					AgentGraphConnection c;
					c.id = iA->id;
					c.start = dt - 100; // take a bit off so we don't get vertical lines
					c.status = status;
					c.iStart = &*iA;
					this->agentGraphConnectionOpen.push_back( c ); 
				}

				break;
			}
		}
	}

	this->agentGraphUpdateVis();

	return 0;
}

int AgentMirrorGUI::agentGraphUpdateVis() {
	int i;
	float hx, hy, ax, ay, alength;
	int hostCount;
	int agentMax;
	RPC_WSTR rpc_wstr;
	char buf[256];
	float hcolour[3] = { 1, 0.5f, 0 };
	float acolour[3] = { 0, 0.1f, 1 };
	float abgcolourREADY[3] = { 0.3f, 0.8f, 0.3f };
	float abgcolourFROZEN[3] = { 0.3f, 0.3f, 0.6f };
	float abgcolourFAILED[3] = { 0.6f, 0.3f, 0.3f };
	float abgcolourEXIT[3] = { 0.1f, 0.3f, 0.1f };
	float abgcolourX[3] = { 1, 0, 0 };
	float ccolourFROZEN[3] = { 0.0f, 0.0f, 1.0f };
	float ccolourFAILED[3] = { 1.0f, 0.0f, 0.0f };
	float *clr;
	int agentSlot[64];

	float tScale = 1/10000.0f; // time scale (m/ms)

	apb->apb_ftime_s( &this->agentGraphTU );
	int dt = (int)((this->agentGraphTU.time - this->agentGraphT0.time)*1000 + (this->agentGraphTU.millitm - this->agentGraphT0.millitm));

	// clear last vis
	while ( !this->agentGraphStrings.empty() ) {
		this->visualizer->deleteString( this->agentGraphStrings.front() );
		this->agentGraphStrings.pop_front();
	}
	while ( !this->agentGraphObjects.empty() ) {
		this->visualizer->deleteObject( this->agentGraphObjects.front(), true );
		this->agentGraphObjects.pop_front();
	}

	// list by host and agent
	std::map<UUID,std::list<AgentGraphInfo>,UUIDless>::iterator iH;
	std::list<AgentGraphInfo>::iterator iA;
	hx = -dt*tScale;
	hy = -2;
	hostCount = 0;
	for ( iH = this->agentGraph.begin(); iH != this->agentGraph.end(); iH++ ) {
		agentMax = 0;

		// write host id
		UuidToString( (UUID *)&iH->first, &rpc_wstr );
		sprintf_s( buf, sizeof(buf), "Host (%ls)", rpc_wstr );
		this->agentGraphStrings.push_back( this->visualizer->newString( -3, hy, 1, buf, hcolour ) ); // align with x = 0

		memset( agentSlot, 0, sizeof(char)*64 );
		for ( iA = iH->second.begin(); iA != iH->second.end(); iA++ ) {
			// update slots
			for ( i=0; i<64; i++ )
				if ( agentSlot[i] != -1 && agentSlot[i] < iA->start )
					agentSlot[i] = 0; // old agent is done with the slot

			// get free slot
			for ( i=0; i<64; i++ ) 
				if ( agentSlot[i] == 0 )
					break;
			agentSlot[i] = iA->end; // reserved until end

			if ( i > agentMax )
				agentMax = i;

			ax = hx + iA->start*tScale;
			ay = hy - 0.20f - i*0.20f;
			if ( iA->end != -1 ) {
				alength = (iA->end-iA->start)*tScale;
			} else {
				alength = (dt-iA->start)*tScale;
			}

			iA->yOffset = ay; // save this for connections

			// write agent info
			UuidToString( (UUID *)&iA->id, &rpc_wstr );
			sprintf_s( buf, sizeof(buf), "(%ls)", rpc_wstr );
			this->agentGraphStrings.push_back( this->visualizer->newString( ax, ay + 0.05f, 0.6f, iA->name, acolour ) );
			this->agentGraphStrings.push_back( this->visualizer->newString( ax, ay, 0.3f, buf, acolour ) );

			// draw agent line
			if ( iA->end == -1 ) {
				clr = abgcolourREADY;
			} else if ( iA->endStatus == DDBAGENT_STATUS_FROZEN ) {
				clr = abgcolourFROZEN;
			} else if ( iA->endStatus == DDBAGENT_STATUS_FAILED ) {
				clr = abgcolourFAILED;
				// failed X
				this->agentGraphObjects.push_back( this->visualizer->newStaticObject( ax + alength - 1, ay, 0, 0.15f, this->pathLib[PATHLIB_X], abgcolourX, 3 ) );
			} else { // must have exited normally?
				clr = abgcolourEXIT;
			}
			this->agentGraphObjects.push_back( this->visualizer->newStaticObject( ax, ay, 0, alength, this->pathLib[PATHLIB_LINE], clr, 5 ) );
		}
		hostCount++;
		hy -= agentMax*0.20f + 0.50f; // agent max * agent offset + host offset
	}

	// draw connections
	float clength;
	float crot;
	float dx, dy;
	std::list<AgentGraphConnection>::iterator iC;
	for ( iC = this->agentGraphConnection.begin(); iC != this->agentGraphConnection.end(); iC++ ) {
		dx = (iC->end - iC->start) * tScale;
		dy = iC->iEnd->yOffset - iC->iStart->yOffset;
		// calculate angle
		crot = atan2( dy, dx );
		// calculate length
		clength = sqrt(dx*dx + dy*dy);

		if ( iC->status == DDBAGENT_STATUS_FROZEN ) {
			clr = ccolourFROZEN;
		} else if ( iC->status == DDBAGENT_STATUS_FAILED ) {
			clr = ccolourFAILED;
		}
		this->agentGraphObjects.push_back( this->visualizer->newStaticObject( hx + iC->start*tScale, iC->iStart->yOffset, crot, clength, this->pathLib[PATHLIB_LINE], clr, 2 ) );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// DDB Event Handlers

int AgentMirrorGUI::eventAgent( UUID id, char evt, DataStream *ds ) {
	DataStream lds;
	ObjectInfo obj;
	WCHAR buf[256];
	UUID parentId;
	HTREEITEM parentH;

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBAgent( &id, this );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		parentH = catAgent;
		if ( !obj.object->getParent( &parentId ) ) {
			std::map<UUID,ObjectInfo,UUIDless>::iterator it = this->objects.find( parentId );
			if ( it != this->objects.end() ) {
				parentH = it->second.handle;	
			} else {
				this->orphans[id] = parentId;
			}
		} 
		obj.handle = this->objectTree->InsertItem( buf, parentH );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();

		// handle paths now
		{
			int i;
			int pathId;
			int count;
			float *x, *y;
			DDBVis_NODE *n;
			// get all paths
			if ( this->dStore->VisGetPath( &id, -1, &lds, &nilUUID ) ) {
				lds.unlock();			
				return 1;
			}

			lds.rewind();
			lds.unpackData(sizeof(UUID)); // discard thread
			if ( lds.unpackChar() != DDBR_OK ) {
				lds.unlock();
				return 1;
			}

			while ( lds.unpackBool() ) { // loop through paths
				pathId = lds.unpackInt32();
				count = lds.unpackInt32();
				x = (float *)malloc(sizeof(int)*count*2); // alloc both blocks at once
				if ( !x ) {
					lds.unlock();				
					return 1;
				}
				y = x + count;
				if ( count ) {
					for ( i=0; i<count; i++ ) {
						n = (DDBVis_NODE *)lds.unpackData( sizeof(DDBVis_NODE) );
						x[i] = n->x;
						y[i] = n->y;
					}
				}
				// add the path to object
				((GODDBAgent *)this->objects[id].object)->_VisAddPath( pathId, count, x, y );
				free( x );
			}

			lds.unlock();
		}
		// handle objects now
		{
			int i;
			int objId;
			char *name;
			bool solid;
			bool visible;
			float x, y, r, s;
			int count;
			int *paths; 
			float **colours; 
			float *lineWidths;
			DDBVis_PATH_REFERENCE *pr;
			
			// get the state
			if ( this->dStore->VisObjectGetInfo( &id, -1, DDBVISOBJECTINFO_NAME | DDBVISOBJECTINFO_POSE | DDBVISOBJECTINFO_EXTRA | DDBVISOBJECTINFO_PATHS, &lds, &nilUUID ) ) {
				lds.unlock();
				return 1;
			}

			lds.rewind();
			lds.unpackData(sizeof(UUID)); // discard thread

			if ( lds.unpackChar() != DDBR_OK ) {
				lds.unlock();
				return 1;
			}

			lds.unpackInt32(); // infoFlags

			while ( lds.unpackBool() ) {
				objId = lds.unpackInt32();

				// DDBVISOBJECTINFO_NAME
				name = lds.unpackString();
				// DDBVISOBJECTINFO_POSE
				x = lds.unpackFloat32();
				y = lds.unpackFloat32();
				r = lds.unpackFloat32();
				s = lds.unpackFloat32();
				// DDBVISOBJECTINFO_EXTRA
				solid = lds.unpackBool();
				visible = lds.unpackBool();
				// DDBVISOBJECTINFO_PATHS
				count = lds.unpackInt32();
				if ( count ) {
					paths = (int *)malloc(sizeof(int)*count);
					if ( !paths ) {
						lds.unlock();
						return 1;
					}
					lineWidths = (float *)malloc(sizeof(float)*count);
					if ( !lineWidths ) {
						free( paths );
						lds.unlock();
						return 1;
					}
					colours = (float **)malloc(sizeof(float*)*count);
					if ( !colours ) {
						free( paths );
						free( lineWidths );		
						lds.unlock();
						return 1;
					}
					colours[0] = (float *)malloc(sizeof(float)*3*count); // allocate whole block at once
					if ( !colours[0] ) {
						free( paths );
						free( lineWidths );
						free( colours );
						lds.unlock();
						return 1;
					}

					for ( i=0; i<count; i++ ) {
						colours[i] = colours[0] + i*3; // assign chunk
						pr = (DDBVis_PATH_REFERENCE *)lds.unpackData( sizeof(DDBVis_PATH_REFERENCE) );
						paths[i] = pr->id;
						colours[i][0] = pr->r;
						colours[i][1] = pr->g;
						colours[i][2] = pr->b;
						lineWidths[i] = pr->lineWidth;
					}
				}
				
				// add the object to object
				if ( ((GODDBAgent *)this->objects[id].object)->_VisAddStaticObject( objId, x, y, r, s, count, paths, colours, lineWidths, solid, name ) ) {
					free( paths );
					free( lineWidths );
					free( colours[0] );
					free( colours );	

					lds.unlock();

					return 1;
				}

				free( paths );
				free( lineWidths );
				free( colours[0] );
				free( colours );

				lds.unlock();

				// update visiblity
				((GODDBAgent *)this->objects[id].object)->_VisSetObjectVisible( objId, visible );
			} 
		} 

		// update agentGraph
		if ( *dStore->AgentGetHost(&id) != nilUUID ) {
			char name[256];
			sprintf_s( name, sizeof(name), "%ls", buf );
			this->agentGraphStart( &id, name, dStore->AgentGetHost(&id) );
		}

		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );

		// update agentGraph
		this->agentGraphEnd( &id, dStore->AgentGetStatus(&id) );
		break;
	case DDBE_AGENT_UPDATE:
		{
			int infoFlags = ds->unpackInt32();
			if ( infoFlags & (DDBAGENTINFO_HOST) ) {
				if ( *dStore->AgentGetHost(&id) != nilUUID ) { // has a host, add to agentGraph
					WCHAR buf[256];
					char name[256];
					this->objects[id].object->getName( buf, 256 );
					sprintf_s( name, sizeof(name), "%ls", buf );
					this->agentGraphStart( &id, name, dStore->AgentGetHost(&id) );
				} else { // no host, remove from agentGraph
					int status = dStore->AgentGetStatus(&id);
					this->agentGraphEnd( &id, status );
				}
			}
		}
		break;
	case DDBE_VIS_ADDPATH:
		{
			int i;
			int pathId = ds->unpackInt32();
			int count;
			float *x, *y;
			DDBVis_NODE *n;
			// get the path
			if ( this->dStore->VisGetPath( &id, pathId, &lds, &nilUUID ) ) {
				lds.unlock();
				return 1;
			}

			lds.rewind();
			lds.unpackData(sizeof(UUID)); // discard thread
			if ( lds.unpackChar() != DDBR_OK ) {
				lds.unlock();
				return 1;
			}

			count = lds.unpackInt32();
			x = (float *)malloc(sizeof(int)*count*2); // alloc both blocks at once
			if ( !x ) {
				lds.unlock();			
				return 1;
			}
			y = x + count;
			if ( count ) {
				for ( i=0; i<count; i++ ) {
					n = (DDBVis_NODE *)lds.unpackData( sizeof(DDBVis_NODE) );
					x[i] = n->x;
					y[i] = n->y;
				}
			}
			// add the path to object
			((GODDBAgent *)this->objects[id].object)->_VisAddPath( pathId, count, x, y );
			free( x );
			lds.unlock();

		//	Log.log( LOG_LEVEL_VERBOSE, "AgentMirrorGUI::eventAgent: %s DDBE_VIS_ADDPATH pathId %d, %d nodes", Log.formatUUID(LOG_LEVEL_VERBOSE,&id), pathId, count );
			
		}
		break;
	case DDBE_VIS_REMPATH:
		{
			int pathId = ds->unpackInt32();
			((GODDBAgent *)this->objects[id].object)->_VisRemovePath( pathId );
		//	Log.log( LOG_LEVEL_VERBOSE, "AgentMirrorGUI::eventAgent: %s DDBE_VIS_REMPATH pathId %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&id), pathId );
		}
		break;
	case DDBE_VIS_UPDATEPATH:
		break;
	case DDBE_VIS_ADDOBJECT:
		{
			int i;
			int objId = ds->unpackInt32();
			char *name;
			bool solid;
			bool visible;
			float x, y, r, s;
			int count;
			int *paths; 
			float **colours; 
			float *lineWidths;
			DDBVis_PATH_REFERENCE *pr;
			
			// get the state
			if ( this->dStore->VisObjectGetInfo( &id, objId, DDBVISOBJECTINFO_NAME | DDBVISOBJECTINFO_POSE | DDBVISOBJECTINFO_EXTRA | DDBVISOBJECTINFO_PATHS, &lds, &nilUUID ) ) {
				lds.unlock();
				return 1;
			}

			lds.rewind();
			lds.unpackData(sizeof(UUID)); // discard thread

			if ( lds.unpackChar() != DDBR_OK ) {
				lds.unlock();
				return 1;
			}

			lds.unpackInt32(); // infoFlags

			// DDBVISOBJECTINFO_NAME
			name = lds.unpackString();
			// DDBVISOBJECTINFO_POSE
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			// DDBVISOBJECTINFO_EXTRA
			solid = lds.unpackBool();
			visible = lds.unpackBool();
			// DDBVISOBJECTINFO_PATHS
			count = lds.unpackInt32();
			if ( count ) {
				paths = (int *)malloc(sizeof(int)*count);
				if ( !paths ) {
					lds.unlock();
					return 1;
				}
				lineWidths = (float *)malloc(sizeof(float)*count);
				if ( !lineWidths ) {
					free( paths );
					lds.unlock();
					return 1;
				}
				colours = (float **)malloc(sizeof(float*)*count);
				if ( !colours ) {
					free( paths );
					free( lineWidths );	
					lds.unlock();
					return 1;
				}
				colours[0] = (float *)malloc(sizeof(float)*3*count); // allocate whole block at once
				if ( !colours[0] ) {
					free( paths );
					free( lineWidths );
					free( colours );
					lds.unlock();
					return 1;
				}

				for ( i=0; i<count; i++ ) {
					colours[i] = colours[0] + i*3; // assign chunk
					pr = (DDBVis_PATH_REFERENCE *)lds.unpackData( sizeof(DDBVis_PATH_REFERENCE) );
					paths[i] = pr->id;
					colours[i][0] = pr->r;
					colours[i][1] = pr->g;
					colours[i][2] = pr->b;
					lineWidths[i] = pr->lineWidth;
				}
			}

			lds.unlock();
			
			// add the object to object
			if ( ((GODDBAgent *)this->objects[id].object)->_VisAddStaticObject( objId, x, y, r, s, count, paths, colours, lineWidths, solid, name ) ) {
				free( paths );
				free( lineWidths );
				free( colours[0] );
				free( colours );	
				return 1;
			}

			free( paths );
			free( lineWidths );
			free( colours[0] );
			free( colours );

			// update visiblity
			((GODDBAgent *)this->objects[id].object)->_VisSetObjectVisible( objId, visible );
		
		//	Log.log( LOG_LEVEL_VERBOSE, "AgentMirrorGUI::eventAgent: %s DDBE_VIS_ADDOBJECT objectId %d, name %s x %f y %f r %f vis %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&id), objId, name, x, y, r, visible );		
		}
		break;
	case DDBE_VIS_REMOBJECT:
		{
			int objId = ds->unpackInt32();
			((GODDBAgent *)this->objects[id].object)->_VisRemoveObject( objId );
		//	Log.log( LOG_LEVEL_VERBOSE, "AgentMirrorGUI::eventAgent: %s DDBE_VIS_REMOBJECT objectId %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&id), objId );	
		}
		break;
	case DDBE_VIS_UPDATEOBJECT:
		{
			int objId = ds->unpackInt32();
			char msg = ds->unpackChar();
			if ( msg == MSG_DDB_VIS_SETOBJECTVISIBLE ) {
				char visible;
				// get the state
				if ( this->dStore->VisObjectGetInfo( &id, objId, DDBVISOBJECTINFO_EXTRA, &lds, &nilUUID ) ) {
					lds.unlock();
					return 1;
				}

				lds.rewind();
				lds.unpackData(sizeof(UUID)); // discard thread

				if ( lds.unpackChar() != DDBR_OK ) {
					lds.unlock();
					return 1;
				}

				lds.unpackInt32(); // infoFlags

				lds.unpackBool(); // solid
				visible = lds.unpackBool();
				((GODDBAgent *)this->objects[id].object)->_VisSetObjectVisible( objId, visible );

				lds.unlock();

			} else if ( msg == MSG_DDB_VIS_UPDATEOBJECT ) {
				float x, y, r, s;

				// get the state
				if ( this->dStore->VisObjectGetInfo( &id, objId, DDBVISOBJECTINFO_POSE, &lds, &nilUUID ) ) {
					lds.unlock();
					return 1;
				}

				lds.rewind();
				lds.unpackData(sizeof(UUID)); // discard thread

				if ( lds.unpackChar() != DDBR_OK ) {
					lds.unlock();
					return 1;
				}

				lds.unpackInt32(); // infoFlags

				x = lds.unpackFloat32();
				y = lds.unpackFloat32();
				r = lds.unpackFloat32();
				s = lds.unpackFloat32();

				lds.unlock();

				((GODDBAgent *)this->objects[id].object)->_VisUpdateObject( objId, x, y, r, s );
			}
		}
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}

int AgentMirrorGUI::eventRegion( UUID id, char evt, DataStream *ds ) {
	ObjectInfo obj;
	WCHAR buf[256];

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBRegion( &id, this );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		obj.handle = this->objectTree->InsertItem( buf, catDDB );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}

int AgentMirrorGUI::eventLandmark( UUID id, char evt, DataStream *ds ) {
	ObjectInfo obj;
	WCHAR buf[256];

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBLandmark( &id, this, this->pathLib );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		obj.handle = this->objectTree->InsertItem( buf, catDDBLandmark );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	case DDBE_UPDATE:
		{
			int infoFlags = ds->unpackInt32();
			if ( infoFlags & DDBLANDMARKINFO_POS ) {
				// position updated
				((GODDBLandmark *)this->objects[id].object)->updatePosEstimation();
			}
			if ( infoFlags & DDBLANDMARKINFO_COLLECTED ) {
				((GODDBLandmark *)this->objects[id].object)->collected();
			}
		}
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}

int AgentMirrorGUI::eventPOG( UUID id, char evt, DataStream *ds ) {
	ObjectInfo obj;
	WCHAR buf[256];

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBMapPOG( &id, this );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		obj.handle = this->objectTree->InsertItem( buf, catDDB );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	case DDBE_POG_UPDATE:
	case DDBE_POG_LOADREGION:
		{
			float x = ds->unpackFloat32();
			float y = ds->unpackFloat32();
			float w = ds->unpackFloat32();
			float h = ds->unpackFloat32();
			((GODDBMapPOG *)this->objects[id].object)->updateRegion( x, y, w, h );
		}
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}

int AgentMirrorGUI::eventParticleFilter( UUID id, char evt, DataStream *ds ) {
	ObjectInfo obj;
	WCHAR buf[256];
	UUID parentId;
	HTREEITEM parentH;
	int ret;

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBParticleFilter( &id, this, this->pathLib );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		parentH = catDDB;
		ret = obj.object->getParent( &parentId );
		if ( ret == 0 ) { // found
			std::map<UUID,ObjectInfo,UUIDless>::iterator it = this->objects.find( parentId );
			if ( it != this->objects.end() ) {
				parentH = it->second.handle;	
			} else {
				this->orphans[id] = parentId;
			}
		} else if ( ret == -1 ) { // we have one, but it's not currently available
			this->orphans[id] = nilUUID;
		}
		obj.handle = this->objectTree->InsertItem( buf, parentH );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	case DDBE_PF_RESAMPLE:
	case DDBE_PF_PREDICTION:
	case DDBE_PF_CORRECTION:
		((GODDBParticleFilter *)this->objects[id].object)->updateFilter( evt, ds );
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}

int AgentMirrorGUI::eventAvatar( UUID id, char evt, DataStream *ds ) {
	DataStream lds;
	ObjectInfo obj;
	WCHAR buf[256];

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBAvatar( &id, this, this->pathLib );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		obj.handle = this->objectTree->InsertItem( buf, catDDB );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	case DDBE_UPDATE:
		{
			int infoFlags = ds->unpackInt32();
			if ( infoFlags & DDBAVATARINFO_RETIRE ) {
				UUID pfId;
				dStore->AvatarGetInfo( &id, DDBAVATARINFO_RPF | DDBAVATARINFO_RTIMECARD, &lds, &nilUUID );
				lds.rewind();
				lds.unpackData( sizeof(UUID) ); // thread
				lds.unpackChar(); // OK
				lds.unpackInt32(); // infoFlags
				lds.unpackUUID( &pfId );
				lds.unpackData( sizeof(_timeb) ); // startTime
				char retireMode = lds.unpackChar();
				lds.unpackData( sizeof(_timeb) ); // endTime
				lds.unlock();

				this->objects[id].object->setVisibility( 0 ); 
				this->objectTree->SetCheck( this->objects[id].handle, 0 );

				if ( this->objects.find(pfId) != this->objects.end() ) { // found pf
					this->objects[pfId].object->setVisibility( 0 ); // hide in all cases
					this->objectTree->SetCheck( this->objects[pfId].handle, 0 );
				}
			}
		}
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}


int AgentMirrorGUI::eventSensor( UUID id, char evt, DataStream *ds ) {
	ObjectInfo obj;
	WCHAR buf[256];
	UUID parentId;
	HTREEITEM parentH;

	switch ( evt ) {
	case DDBE_ADD:
		// new object
		obj.object = new GODDBSensor( &id, this );
		if ( !obj.object )
			return 1;
		obj.object->getName( buf, 256 );
		parentH = catDDB;
		if ( !obj.object->getParent( &parentId ) ) {
			std::map<UUID,ObjectInfo,UUIDless>::iterator it = this->objects.find( parentId );
			if ( it != this->objects.end() ) {
				parentH = it->second.handle;	
			} else {
				this->orphans[id] = parentId;
			}
		} 
		obj.handle = this->objectTree->InsertItem( buf, parentH );

		this->objectTree->SetCheck( obj.handle, obj.object->getVisibility() ? 1:0 );

		this->objects[id] = obj;
		this->checkOrphanage( &id );

		this->objectTree->Invalidate();
		break;
	case DDBE_REM:
		// remove object
		this->cleanRemoveItem( &id );
		delete this->objects[id].object;
		this->objects.erase( id );
		break;
	case DDBE_SENSOR_UPDATE:
		break;
	default: // unhandled event
		return 1;
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int AgentMirrorGUI::conProcessMessage( unsigned char message, char *data, unsigned int len ) {
	
/*	// intercept DDB_VIS messages but still pass them on to AgentMirror::conProcessMessage
	switch ( message ) {
	case MSG_DDB_VIS_ADDPATH:
		{
			UUID agentId;
			int id, count;
			float *x, *y;
			this->ds.setData( data, len );
			this->ds.unpackUUID( &agentId );
			id = this->ds.unpackInt32();
			count = this->ds.unpackInt32();
			x = (float *)this->ds.unpackData(count*sizeof(float));
			y = (float *)this->ds.unpackData(count*sizeof(float));
			this->newPath( agentId, id, count, x, y );
			this->ds.unlock();
		}
		break;
	case MSG_DDB_VIS_REMPATH:
		{
			UUID agentId;
			int id;
			this->ds.setData( data, len );
			this->ds.unpackUUID( &agentId );
			id = this->ds.unpackInt32();
			this->ds.unlock();
			this->deletePath( agentId, id );
		}
		break;
	case MSG_DDB_VIS_EXTENDPATH:
	case MSG_DDB_VIS_UPDATEPATH:
		// TODO
		break;
	case MSG_DDB_VIS_ADDOBJECT:
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
			this->ds.setData( data, len );
			this->ds.unpackUUID( &agentId );
			id = this->ds.unpackInt32();
			x = this->ds.unpackFloat32();
			y = this->ds.unpackFloat32();
			r = this->ds.unpackFloat32();
			s = this->ds.unpackFloat32();
			count = this->ds.unpackInt32();
			paths = (int *)this->ds.unpackData(count*sizeof(int));
			colours = (float **)malloc(count*sizeof(float*));
			if ( !colours ) { // malloc failed!
				this->ds.unlock();
				return 0;
			}
			for ( i=0; i<count; i++ ) {
				colours[i] = (float *)this->ds.unpackData(sizeof(float)*3);
			}
			lineWidths = (float *)this->ds.unpackData(count*sizeof(float));
			solid = this->ds.unpackBool();
			name = this->ds.unpackString();
			this->newStaticObject( agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name );
			this->ds.unlock();
		}
		break;
	case MSG_DDB_VIS_REMOBJECT:
		{
			UUID agentId;
			int id;
			this->ds.setData( data, len );
			this->ds.unpackUUID( &agentId );
			id = this->ds.unpackInt32();
			this->ds.unlock();
			this->deleteObject( agentId, id );
		}
		break;
	case MSG_DDB_VIS_SETOBJECTVISIBLE:
		// TODO
		break;
	default:
		break; // not a vis message
	}*/

	if ( !AgentMirror::conProcessMessage( message, data, len ) ) // message handled
		return 0;

	return 1; // unhandled message
}

//-----------------------------------------------------------------------------
// Callbacks


bool AgentMirrorGUI::cbDDBWatcher( void *vpds ) {
	DataStream *ds = (DataStream *)vpds;

	int type;
	UUID id;
	char evt;

	type = ds->unpackInt32();
	ds->unpackUUID( &id );
	evt = ds->unpackChar();

	switch ( type ) {
	case DDB_AGENT:
		this->eventAgent( id, evt, ds );
		break;
	case DDB_REGION:
		this->eventRegion( id, evt, ds );
		break;
	case DDB_LANDMARK:
		this->eventLandmark( id, evt, ds );
		break;
	case DDB_MAP_PROBOCCGRID:
		this->eventPOG( id, evt, ds );
		break;
	case DDB_PARTICLEFILTER:
		this->eventParticleFilter( id, evt, ds );
		break;
	case DDB_AVATAR:
		this->eventAvatar( id, evt, ds );	
		break;
	case DDB_SENSOR_SONAR:
	case DDB_SENSOR_CAMERA:
	case DDB_SENSOR_SIM_CAMERA:
		this->eventSensor( id, evt, ds );	
		break;
	default:
		// Unhandled type?!
		return 0;
	}

	return 0;
}
