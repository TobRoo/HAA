 
#include "stdafx.h"

#include "autonomic.h"
#include "agentHost.h"

#define round(val) floor((val) + 0.5f)

//*****************************************************************************
// DDBStore

//-----------------------------------------------------------------------------
// Constructor	

DDBStore::DDBStore( AgentPlayback *apb, Logger *Log ) {
	this->apb = apb;
	this->Log = Log;
	UuidCreateNil( &nilUUID );
	this->DEBUGtotalPFUnpackTime = 0;

	this->pfForwardPropCount = 0;
}

//-----------------------------------------------------------------------------
// Destructor

DDBStore::~DDBStore() {

	Log->log( 0, "DDBStore::~DDBStore: pfForwardPropCount %d", this->pfForwardPropCount );

	this->Clean();

}

int DDBStore::Clean() {

	// clean up DDB
	while ( !this->DDBAgents.empty() ) {
		this->RemoveAgent( (UUID *)&this->DDBAgents.begin()->first );
	}
	while ( !this->DDBRegions.empty() ) {
		this->RemoveRegion( (UUID *)&this->DDBRegions.begin()->first );
	}
	while ( !this->DDBLandmarks.empty() ) {
		this->RemoveLandmark( (UUID *)&this->DDBLandmarks.begin()->first );
	}
	while ( !this->DDBPOGs.empty() ) {
		this->RemovePOG( (UUID *)&this->DDBPOGs.begin()->first );
	}
	while ( !this->DDBParticleFilters.empty() ) {
		this->RemoveParticleFilter( (UUID *)&this->DDBParticleFilters.begin()->first );
	}
	while ( !this->DDBAvatars.empty() ) {
		this->RemoveAvatar( (UUID *)&this->DDBAvatars.begin()->first );
	}
	while ( !this->DDBSensors.empty() ) {
		this->RemoveSensor( (UUID *)&this->DDBSensors.begin()->first );
	}

	return 0;
}


bool DDBStore::isValidId( UUID *id, int type ) {

	switch ( type ) {
	case DDB_AGENT:
		return this->DDBAgents.find( *id ) != this->DDBAgents.end();
	case DDB_REGION:
		return this->DDBRegions.find( *id ) != this->DDBRegions.end();
	case DDB_LANDMARK:
		return this->DDBLandmarks.find( *id ) != this->DDBLandmarks.end();
	case DDB_MAP_PROBOCCGRID:
		return this->DDBPOGs.find( *id ) != this->DDBPOGs.end();
	case DDB_PARTICLEFILTER:
		return this->DDBParticleFilters.find( *id ) != this->DDBParticleFilters.end();
	case DDB_AVATAR:
		return this->DDBAvatars.find( *id ) != this->DDBAvatars.end();
	case DDB_SENSORS:
	case DDB_SENSOR_SONAR:
	case DDB_SENSOR_CAMERA:
	case DDB_SENSOR_SIM_CAMERA:
		return this->DDBSensors.find( *id ) != this->DDBSensors.end();
	};

	return false;
}

//-----------------------------------------------------------------------------
// DDBAgent

int DDBStore::EnumerateAgents( DataStream *ds ) {
	int count;
	mapDDBAgent::iterator iter;

	count = 0;
	
	// pack all the objects
	iter = this->DDBAgents.begin();
	while ( iter != this->DDBAgents.end() ) {
		ds->packInt32( DDB_AGENT );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packString( iter->second->agentTypeName );
		ds->packUUID( &iter->second->agentTypeId );
		ds->packInt32( iter->second->agentInstance );

		ds->packUUID( &iter->second->host );
		ds->packUUID( &iter->second->hostTicket );
		ds->packInt32( iter->second->status );
		ds->packUUID( &iter->second->statusTicket );

		ds->packUUID( &iter->second->parent );
		ds->packUUID( &iter->second->spawnThread );
		ds->packFloat32( iter->second->spawnAffinity );
		ds->packChar( iter->second->priority );
		ds->packFloat32( iter->second->spawnProcessCost );
		ds->packInt32( iter->second->activationMode );

		ds->packData( &iter->second->spawnTime, sizeof(_timeb) );

		map<UUID, unsigned int, UUIDless>::iterator iAff;
		for ( iAff = iter->second->affinityData.begin(); iAff != iter->second->affinityData.end(); iAff++ ) {
			ds->packBool( 1 );
			ds->packUUID( (UUID *)&iAff->first );
			ds->packUInt32( iAff->second ); 
		}
		ds->packBool( 0 ); // done

		ds->packFloat32( iter->second->weightedUsage );

		ds->packData( &iter->second->stateTime, sizeof(_timeb) );
		ds->packUInt32( iter->second->stateSize );
		if ( iter->second->stateSize ) 
			ds->packData( iter->second->stateData, iter->second->stateSize );

		ds->packData( &iter->second->backupTime, sizeof(_timeb) );
		ds->packUInt32( iter->second->backupSize );
		if ( iter->second->backupSize ) 
			ds->packData( iter->second->backupData, iter->second->backupSize );

		list<DDBAgent_MSG>::iterator iM;
		for ( iM = iter->second->msgQueuePrimary.begin(); iM != iter->second->msgQueuePrimary.end(); iM++ ) {
			ds->packBool( 1 );
			ds->packUChar( iM->msg );
			ds->packUInt32( iM->len );
			if ( iM->len )
				ds->packData( iM->data, iM->len );
		}
		ds->packBool( 0 ); // done
		for ( iM = iter->second->msgQueueSecondary.begin(); iM != iter->second->msgQueueSecondary.end(); iM++ ) {
			ds->packBool( 1 );
			ds->packUChar( iM->msg );
			ds->packUInt32( iM->len );
			if ( iM->len )
				ds->packData( iM->data, iM->len );
		}
		ds->packBool( 0 ); // done

		// enumerate paths
		std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
		std::list<DDBVis_NODE>::iterator iN;
		for ( iP = iter->second->visPaths->begin(); iP != iter->second->visPaths->end(); iP++ ) {
			ds->packBool( 1 );
			ds->packInt32( iP->first ); // id
			// pack nodes
			for ( iN = iP->second->begin(); iN != iP->second->end(); iN++ ) {
				ds->packBool( 1 );
				ds->packFloat32( iN->x );
				ds->packFloat32( iN->y );
			}
			ds->packBool( 0 ); // finished nodes
		}
		ds->packBool( 0 ); // finished paths

		// enumerate objects
		std::map<int,DDBVis_OBJECT *>::iterator iO;
		std::list<DDBVis_PATH_REFERENCE>::iterator iPR;
		for ( iO = iter->second->visObjects->begin(); iO != iter->second->visObjects->end(); iO++ ) {
			ds->packBool( 1 );
			ds->packInt32( iO->first ); // id
			ds->packString( iO->second->name );
			ds->packBool( iO->second->solid );
			ds->packBool( iO->second->visible );
			ds->packFloat32( iO->second->x );
			ds->packFloat32( iO->second->y );
			ds->packFloat32( iO->second->r );
			ds->packFloat32( iO->second->s );
			// pack path refs
			for ( iPR = iO->second->path_refs.begin(); iPR != iO->second->path_refs.end(); iPR++ ) {
				ds->packBool( 1 );
				ds->packInt32( iPR->id );
				ds->packFloat32( iPR->r );
				ds->packFloat32( iPR->g );
				ds->packFloat32( iPR->b );
				ds->packFloat32( iPR->lineWidth );
				ds->packInt32( iPR->stipple );
			}
			ds->packBool( 0 ); // finished path refs
		}
		ds->packBool( 0 ); // finished objects

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseAgent( DataStream *ds, UUID *parsedId ) {
	UUID id, parent, affinity, spawnThread;
	UUID host, hostTicket, statusTicket;
	int status;
	AgentType agentType;
	int activationMode;
	float spawnAffinity, spawnProcessCost;
	char priority;
	mapDDBAgent::iterator iter;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBAgents.find( id );

	if ( iter == this->DDBAgents.end() ) { // new object
		// match data format to Enumerate<Type> function
		strcpy_s( agentType.name, sizeof(agentType.name), ds->unpackString() );
		ds->unpackUUID( &agentType.uuid );
		agentType.instance = ds->unpackInt32();

		ds->unpackUUID( &host );
		ds->unpackUUID( &hostTicket );
		status = ds->unpackInt32();
		ds->unpackUUID( &statusTicket );

		ds->unpackUUID( &parent );

		ds->unpackUUID( &spawnThread );
		spawnAffinity = ds->unpackFloat32();
		priority = ds->unpackChar();
		spawnProcessCost = ds->unpackFloat32();
		activationMode = ds->unpackInt32();

		if ( this->AddAgent( &id, &parent, agentType.name, &agentType.uuid, agentType.instance, &spawnThread, spawnAffinity, priority, spawnProcessCost, activationMode ) )
			return 1;

		this->DDBAgents[id]->host = host;
		this->DDBAgents[id]->hostTicket = hostTicket;
		this->DDBAgents[id]->status = status;
		this->DDBAgents[id]->statusTicket = statusTicket;
		
		this->DDBAgents[id]->spawnTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );

		while ( ds->unpackBool() ) {
			ds->unpackUUID( &affinity );
			this->DDBAgents[id]->affinityData[affinity] = ds->unpackUInt32();
		}

		this->DDBAgents[id]->weightedUsage = ds->unpackFloat32();
		
		this->DDBAgents[id]->stateTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		this->DDBAgents[id]->stateSize = ds->unpackUInt32();
		if ( this->DDBAgents[id]->stateSize ) {
			this->DDBAgents[id]->stateData = malloc( this->DDBAgents[id]->stateSize );
			if ( !this->DDBAgents[id]->stateData ) {
				return 1;
			}
			memcpy( this->DDBAgents[id]->stateData, ds->unpackData( this->DDBAgents[id]->stateSize ), this->DDBAgents[id]->stateSize );
		}

		this->DDBAgents[id]->backupTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		this->DDBAgents[id]->backupSize = ds->unpackUInt32();
		if ( this->DDBAgents[id]->backupSize ) {
			this->DDBAgents[id]->backupData = malloc( this->DDBAgents[id]->backupSize );
			if ( !this->DDBAgents[id]->backupData ) {
				return 1;
			}
			memcpy( this->DDBAgents[id]->backupData, ds->unpackData( this->DDBAgents[id]->backupSize ), this->DDBAgents[id]->backupSize );
		}

		DDBAgent_MSG M;
		while ( ds->unpackBool() ) {
			M.msg = ds->unpackUChar();
			M.len = ds->unpackUInt32();
			if ( M.len ) {
				M.data = (char *)malloc( M.len );
				if ( !M.data ) {
					return 1;
				}
				memcpy( M.data, ds->unpackData( M.len ), M.len );
			}
			this->DDBAgents[id]->msgQueuePrimary.push_back( M );
		}
		while ( ds->unpackBool() ) {
			M.msg = ds->unpackUChar();
			M.len = ds->unpackUInt32();
			if ( M.len ) {
				M.data = (char *)malloc( M.len );
				if ( !M.data ) {
					return 1;
				}
				memcpy( M.data, ds->unpackData( M.len ), M.len );
			}
			this->DDBAgents[id]->msgQueueSecondary.push_back( M );
		}

		// unpack paths
		int pathId;
		std::list<DDBVis_NODE> *path;
		DDBVis_NODE node;
		while ( ds->unpackBool() ) {
			pathId = ds->unpackInt32();
			path = new std::list<DDBVis_NODE>;
			if ( !path )
				return 1;
			// unpack nodes
			while ( ds->unpackBool() ) {
				node.x = ds->unpackFloat32();
				node.y = ds->unpackFloat32();
				path->push_back( node );
			}
			(*this->DDBAgents[id]->visPaths)[pathId] = path;
		}

		// unpack objects
		int objectId;
		DDBVis_OBJECT *object;
		DDBVis_PATH_REFERENCE pathRef;
		while ( ds->unpackBool() ) {
			objectId = ds->unpackInt32();
			object = new DDBVis_OBJECT;
			if ( !object )
				return 1;

			strncpy_s( object->name, sizeof(object->name), ds->unpackString(), sizeof(object->name) );
			object->solid = ds->unpackBool();
			object->visible = ds->unpackBool();
			object->x = ds->unpackFloat32();
			object->y = ds->unpackFloat32();
			object->r = ds->unpackFloat32();
			object->s = ds->unpackFloat32();

			// unpack path refs
			while ( ds->unpackBool() ) {
				pathRef.id = ds->unpackInt32();
				pathRef.r = ds->unpackFloat32();
				pathRef.g = ds->unpackFloat32();
				pathRef.b = ds->unpackFloat32();
				pathRef.lineWidth = ds->unpackFloat32();
				pathRef.stipple = ds->unpackInt32();
				object->path_refs.push_back( pathRef );
			}
			(*this->DDBAgents[id]->visObjects)[objectId] = object;
		}
		
	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}


int DDBStore::AddAgent( UUID *id, UUID *parentId, char *agentTypeName, UUID *agentTypeId, char agentInstance, UUID *spawnThread, float parentAffinity, char priority, float processCost, int activationMode ) {
	
	// create DDBAgent
	DDBAgent *agent = new DDBAgent;
	if ( !agent )
		return 1;

	// initialize
	agent->parent = *parentId;
	strcpy_s( agent->agentTypeName, sizeof(agent->agentTypeName), agentTypeName );
	agent->agentTypeId = *agentTypeId;
	agent->agentInstance = agentInstance;

	UuidCreateNil( &agent->host );
	UuidCreateNil( &agent->hostTicket );

	agent->status = DDBAGENT_STATUS_WAITING_SPAWN;
	UuidCreateNil( &agent->statusTicket );

	agent->spawnTime.time = 0; // unset
	agent->spawnThread = *spawnThread;
	agent->spawnAffinity = parentAffinity;
	agent->priority = priority;
	agent->spawnProcessCost = processCost;
	agent->activationMode = activationMode;

	agent->weightedUsage = 0;

	agent->stateSize = 0;
	agent->backupSize = 0;

	agent->visPaths = new std::map<int,std::list<DDBVis_NODE> *>;
	agent->visObjects = new std::map<int,DDBVis_OBJECT *>;

	// insert into DDBAgents
	this->DDBAgents[*id] = agent;

	return 0;
}

int DDBStore::RemoveAgent( UUID *id ) {
	mapDDBAgent::iterator iter = this->DDBAgents.find(*id);

	if ( iter == this->DDBAgents.end() )
		return 1;

	DDBAgent *agent = iter->second;


	// clean up
	if ( agent->stateSize )
		free( agent->stateData );
	if ( agent->backupSize )
		free( agent->backupData );
	
	DDBAgent_MSG *msg;
	while ( agent->msgQueuePrimary.size() ) {
		msg = &agent->msgQueuePrimary.front();
		if ( msg->len ) 
			free( msg->data );
		agent->msgQueuePrimary.pop_front();
	}
	while ( agent->msgQueueSecondary.size() ) {
		msg = &agent->msgQueueSecondary.front();
		if ( msg->len ) 
			free( msg->data );
		agent->msgQueueSecondary.pop_front();
	}

	while ( !agent->visObjects->empty() ) {
		this->VisRemoveObject( id, agent->visObjects->begin()->first );
	}
	delete agent->visObjects;
	while ( !agent->visPaths->empty() ) {
		this->VisRemovePath( id, agent->visPaths->begin()->first );
	}
	delete agent->visPaths;

	this->DDBAgents.erase( *id );

	delete agent;
	
	return 0;
}

int DDBStore::AgentGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	ds->reset();
	ds->packUUID( thread );


	if (infoFlags & DDBAGENTINFO_RLIST) {
		ds->packChar(DDBR_OK);
		ds->packInt32(infoFlags);
		ds->packInt32(this->DDBAgents.size());
		for (auto& iterA : this->DDBAgents) {
			ds->packUUID((UUID *)&iterA.first);
			ds->packString(iterA.second->agentTypeName);
			ds->packUUID(&iterA.second->agentTypeId);
			ds->packChar(iterA.second->agentInstance);
			ds->packUUID(&iterA.second->parent);
		}
		return 0;
	}
	else if ( iterA == this->DDBAgents.end() ) { // unknown agent
		ds->packChar( DDBR_NOTFOUND );
	} else { 
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );

		if ( infoFlags & DDBAGENTINFO_RTYPE ) {
			ds->packString( iterA->second->agentTypeName );
			ds->packUUID( &iterA->second->agentTypeId );
			ds->packChar( iterA->second->agentInstance );
		}

		if ( infoFlags & DDBAGENTINFO_RPARENT ) {
			ds->packUUID( &iterA->second->parent );
		}

		if ( infoFlags & DDBAGENTINFO_RSPAWNINFO ) {
			ds->packUUID( &iterA->second->spawnThread );
			ds->packInt32( iterA->second->activationMode );
			ds->packChar( iterA->second->priority );
		}

		if ( infoFlags & DDBAGENTINFO_RHOST ) {
			ds->packUUID( &iterA->second->host );
			ds->packUUID( &iterA->second->hostTicket );
		}

		if ( infoFlags & DDBAGENTINFO_RSTATUS ) {
			ds->packInt32( iterA->second->status );
			ds->packUUID( &iterA->second->statusTicket );
		}

		if ( infoFlags & DDBAGENTINFO_RAFFINITY ) {
			map<UUID, unsigned int, UUIDless>::iterator iAff;
			unsigned int elapsedMillis;
			
			if ( iterA->second->spawnTime.time == 0 ) { // unset
				elapsedMillis = 0;
			} else {
				_timeb tb;

				apb->apb_ftime_s( &tb );

				elapsedMillis = (unsigned int)(1000*(tb.time - iterA->second->spawnTime.time) + (tb.millitm - iterA->second->spawnTime.millitm));
			}

			// check parent first
			bool parentPacked = false;
			if ( iterA->second->spawnAffinity > 0 ) {
				if ( elapsedMillis < 3*AGENTAFFINITY_CURBLOCK_TIMEOUT ) {
					ds->packBool( 1 );
					ds->packUUID( &iterA->second->parent );
					ds->packFloat32( iterA->second->spawnAffinity );
					parentPacked = true;
				}
			}

			for ( iAff = iterA->second->affinityData.begin(); iAff != iterA->second->affinityData.end(); iAff++ ) {
				if ( iAff->first == iterA->second->parent && parentPacked )
					continue; // parent already packed
				ds->packBool( 1 );
				ds->packUUID( (UUID *)&iAff->first );
				ds->packFloat32( this->AgentGetAffinity( id, (UUID *)&iAff->first, elapsedMillis ) ); 
			}
			ds->packBool( 0 ); // done
		}

		if ( infoFlags & DDBAGENTINFO_RPROCESSCOST ) {
			ds->packFloat32( this->AgentGetProcessCost( id ) );
		}

		if ( infoFlags & DDBAGENTINFO_RSTATE ) {
			ds->packData( &iterA->second->stateTime, sizeof(_timeb) );
			ds->packUInt32( iterA->second->stateSize );
			if ( iterA->second->stateSize )
				ds->packData( iterA->second->stateData, iterA->second->stateSize );
		}

		if ( infoFlags & DDBAGENTINFO_RMSG_QUEUES ) {
			std::list<DDBAgent_MSG>::iterator iM;
			for ( iM = iterA->second->msgQueuePrimary.begin(); iM != iterA->second->msgQueuePrimary.end(); iM++ ) {
				ds->packBool( 1 );
				ds->packUChar( iM->msg );
				ds->packUInt32( iM->len );
				if ( iM->len )
					ds->packData( iM->data, iM->len );
			}
			for ( iM = iterA->second->msgQueueSecondary.begin(); iM != iterA->second->msgQueueSecondary.end(); iM++ ) {
				ds->packBool( 1 );
				ds->packUChar( iM->msg );
				ds->packUInt32( iM->len );
				if ( iM->len )
					ds->packData( iM->data, iM->len );
			}

			ds->packBool( 0 ); // done
		}

		if ( infoFlags & DDBAGENTINFO_RBACKUP ) {
			ds->packData( &iterA->second->backupTime, sizeof(_timeb) );
			ds->packUInt32( iterA->second->backupSize );
			if ( iterA->second->backupSize )
				ds->packData( iterA->second->backupData, iterA->second->backupSize );
		}
	}

	return 0;
}

int DDBStore::AgentSetInfo( UUID *id, int infoFlags, DataStream *ds ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return 0; // unknown agent, no change
	}
	DDBAgent *agent = iterA->second;

	if ( infoFlags & DDBAGENTINFO_HOST ) {
		ds->unpackUUID( &agent->host );
		ds->unpackUUID( &agent->hostTicket );
	}

	if ( infoFlags & DDBAGENTINFO_STATUS ) {
		agent->status = ds->unpackInt32();
		ds->unpackUUID( &agent->statusTicket );
	}

	if ( infoFlags & DDBAGENTINFO_SPAWN_DATA ) {
		ds->unpackUUID( &agent->parent );
		agent->spawnAffinity = ds->unpackFloat32();
		agent->spawnProcessCost = ds->unpackFloat32();
	}

	if ( infoFlags & DDBAGENTINFO_SPAWN_TIME ) {
		agent->spawnTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
	}

	if ( infoFlags & DDBAGENTINFO_AFFINITY_UPDATE ) {
		UUID affinity;
		unsigned int size;
		ds->unpackUUID( &affinity );
		size = ds->unpackUInt32();

		std::map<UUID, unsigned int, UUIDless>::iterator iAff;
		iAff = agent->affinityData.find( affinity );
		if ( iAff == agent->affinityData.end() ) {
			agent->affinityData[affinity] = size;
		} else {
			agent->affinityData[affinity] += size;
		}
	}

	if ( infoFlags & DDBAGENTINFO_PROCESSCOST_UPDATE ) {
		agent->weightedUsage += ds->unpackFloat32();
	}

	if ( infoFlags & DDBAGENTINFO_STATE ) {
		_timeb newTime;
		unsigned int newSize;
		void *ptr;

		newTime = *(_timeb *)ds->unpackData(sizeof(_timeb));
		newSize = ds->unpackUInt32();
		ptr = malloc(newSize);
		if ( !ptr ) {
			infoFlags = infoFlags & ~DDBAGENTINFO_STATE; // state didn't change because of malloc error
			assert( ptr );
		} else {
			if ( agent->stateSize )
				free( agent->stateData );
			agent->stateTime = newTime;
			agent->stateSize = newSize;
			agent->stateData = ptr;
			memcpy_s( agent->stateData, agent->stateSize, ds->unpackData( agent->stateSize ), agent->stateSize );
		}
	}

	if ( infoFlags & DDBAGENTINFO_STATE_CLEAR ) {
		if ( agent->stateSize )
			free( agent->stateData );
		apb->apb_ftime_s( &agent->stateTime );
		agent->stateSize = 0;
		agent->stateData = NULL;
	}

	if ( infoFlags & DDBAGENTINFO_QUEUE_CLEAR ) {
		DDBAgent_MSG *msg;
		while ( agent->msgQueuePrimary.size() ) {
			msg = &agent->msgQueuePrimary.front();
			if ( msg->len ) 
				free( msg->data );
			agent->msgQueuePrimary.pop_front();
		}
		while ( agent->msgQueueSecondary.size() ) {
			msg = &agent->msgQueueSecondary.front();
			if ( msg->len ) 
				free( msg->data );
			agent->msgQueueSecondary.pop_front();
		}
	}

	if ( infoFlags & DDBAGENTINFO_QUEUE_MERGE ) {
		while ( agent->msgQueueSecondary.size() ) {
			agent->msgQueuePrimary.push_back( agent->msgQueueSecondary.front() );
			agent->msgQueueSecondary.pop_front();
		}
	}

	if ( infoFlags & DDBAGENTINFO_QUEUE_MSG ) { // NOTE that the message queue order is not necessary globally consistant but order from each source is still guaranteed
		char primary;
		DDBAgent_MSG msg;

		primary = ds->unpackChar();
		msg.msg = ds->unpackUChar();
		msg.len = ds->unpackUInt32();
		if ( msg.len ) {
			msg.data = (char *)malloc(msg.len);
			if ( !msg.data ) {
				infoFlags = infoFlags & ~DDBAGENTINFO_QUEUE_MSG; // message wasn't added because of malloc error
				assert( msg.data );
			} else {
				memcpy_s( msg.data, msg.len, ds->unpackData( msg.len ), msg.len );
			}
		}

		if ( primary )	agent->msgQueuePrimary.push_back( msg );
		else			agent->msgQueueSecondary.push_back( msg );
	}

	if ( infoFlags & DDBAGENTINFO_BACKUP ) {
		_timeb newTime;
		unsigned int newSize;
		void *ptr;

		newTime = *(_timeb *)ds->unpackData(sizeof(_timeb));
		newSize = ds->unpackUInt32();
		ptr = malloc(newSize);
		if ( !ptr ) {
			infoFlags = infoFlags & ~DDBAGENTINFO_BACKUP; // state didn't change because of malloc error
			assert( ptr );
		} else {
			if ( agent->backupSize )
				free( agent->backupData );
			agent->backupTime = newTime;
			agent->backupSize = newSize;
			agent->backupData = ptr;
			memcpy_s( agent->backupData, agent->backupSize, ds->unpackData( agent->backupSize ), agent->backupSize );
		}
	}

	return infoFlags;
}

UUID * DDBStore::AgentGetHost( UUID *id ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return &nilUUID; // unknown agent
	}

	return &iterA->second->host;
}

UUID * DDBStore::AgentGetParent( UUID *id ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return &nilUUID; // unknown agent
	}

	return &iterA->second->parent;
}
	
int DDBStore::AgentGetStatus( UUID *id ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return DDBAGENT_STATUS_ERROR; // unknown agent
	}

	return iterA->second->status;
}

float DDBStore::AgentGetAffinity( UUID *id, UUID *affinity, unsigned int elapsedMillis ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return -1; // unknown agent
	}

	if ( elapsedMillis == -1 ) {
		_timeb tb;
		if ( iterA->second->spawnTime.time == 0 ) { // unset
			return iterA->second->spawnAffinity;
		}

		apb->apb_ftime_s( &tb );

		elapsedMillis = (unsigned int)(1000*(tb.time - iterA->second->spawnTime.time) + (tb.millitm - iterA->second->spawnTime.millitm));
	}

	if ( elapsedMillis < 3*AGENTAFFINITY_CURBLOCK_TIMEOUT ) {
		if ( *affinity == iterA->second->parent )
			return iterA->second->spawnAffinity;
		else
			return 0;
	} else {
		map<UUID, unsigned int, UUIDless>::iterator iD = iterA->second->affinityData.find( *affinity );
		if ( iD == iterA->second->affinityData.end() )
			return 0;
		else
			return AGENTAFFINITY_NORMALIZE * iD->second*1000.0f/elapsedMillis; // normallized data rate
	}
}

float DDBStore::AgentGetProcessCost( UUID *id, unsigned int elapsedMillis ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*id);

	if ( iterA == this->DDBAgents.end() ) {
		return -1; // unknown agent
	}

	if ( elapsedMillis == -1 ) {
		_timeb tb;
		if ( iterA->second->spawnTime.time == 0 ) { // unset
			return iterA->second->spawnProcessCost;
		}

		apb->apb_ftime_s( &tb );

		elapsedMillis = (unsigned int)(1000*(tb.time - iterA->second->spawnTime.time) + (tb.millitm - iterA->second->spawnTime.millitm));
	}

	if ( elapsedMillis < 3*CPU_USAGE_INTERVAL ) {
		return iterA->second->spawnProcessCost;
	} else {
		return iterA->second->weightedUsage/elapsedMillis;
	}
}

int DDBStore::VisValidPathId( UUID *agentId, int id ) {
	mapDDBAgent::iterator iA;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 0;

	iP = iA->second->visPaths->find( id );

	if ( iP == iA->second->visPaths->end() )
		return 0;

	return 1;
}

int DDBStore::VisValidObjectId( UUID *agentId, int id ) {
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 0;

	iO = iA->second->visObjects->find( id );

	if ( iO == iA->second->visObjects->end() )
		return 0;

	return 1;
}

int DDBStore::VisAddPath( UUID *agentId, int id, int count, float *x, float *y ) {
	int i;
	mapDDBAgent::iterator iA;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
	std::list<DDBVis_NODE> *path;
	DDBVis_NODE node;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iP = iA->second->visPaths->find( id );

	if ( iP != iA->second->visPaths->end() )
		return 1; // id already exists

	// create path
	path = new std::list<DDBVis_NODE>;
	if ( !path )
		return 1;

	for ( i=0; i<count; i++ ) {
		node.x = x[i];
		node.y = y[i];
		path->push_back(node);
	}

	// insert path
	(*iA->second->visPaths)[id] = path;

	return 0;
}

int DDBStore::VisRemovePath( UUID *agentId, int id ) {
	mapDDBAgent::iterator iA;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iP = iA->second->visPaths->find( id );

	if ( iP == iA->second->visPaths->end() )
		return 1; // path doesn't exist

	// delete path
	delete iP->second;

	// remove from list
	iA->second->visPaths->erase( iP );

	return 0;
}

int DDBStore::VisExtendPath( UUID *agentId, int id, int count, float *x, float *y ) {

	return 0;
}

int DDBStore::VisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y ) {

	return 0;
}

int DDBStore::VisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	int i;
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
	DDBVis_OBJECT *object;
	DDBVis_PATH_REFERENCE pathRef;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iO = iA->second->visObjects->find( id );

	if ( iO != iA->second->visObjects->end() )
		return 1; // id already exists

	// check that paths exist
	for ( i=0; i<count; i++ ) {
		iP = iA->second->visPaths->find( paths[i] );	
		if ( iP == iA->second->visPaths->end() )
			return 1; // path not found
	}

	// create object
	object = new DDBVis_OBJECT;
	if ( !object )
		return 1;

	strncpy_s( object->name, sizeof(object->name), name, sizeof(object->name) );
	object->solid = solid;
	object->visible = true;
	object->x = x;
	object->y = y;
	object->r = r;
	object->s = s;

	for ( i=0; i<count; i++ ) {		
		pathRef.id = paths[i];
		pathRef.r = colours[i][0];
		pathRef.g = colours[i][1];
		pathRef.b = colours[i][2];
		pathRef.lineWidth = lineWidths[i];
		pathRef.stipple = 0;

		object->path_refs.push_back( pathRef );
	}

	// insert object
	(*iA->second->visObjects)[id] = object;

	return 0;
}

int DDBStore::VisRemoveObject( UUID *agentId, int id ) {
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;

	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iO = iA->second->visObjects->find( id );

	if ( iO == iA->second->visObjects->end() )
		return 1; // object doesn't exist

	// delete object
	delete iO->second;

	// remove from list
	iA->second->visObjects->erase( iO );

	return 0;
}


int DDBStore::VisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s ) {
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;

	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iO = iA->second->visObjects->find( id );

	if ( iO == iA->second->visObjects->end() )
		return 1; // object doesn't exist

	// update object
	iO->second->x = x;
	iO->second->y = y;
	iO->second->r = r;
	iO->second->s = s;

	return 0;
}

int DDBStore::VisSetObjectVisible( UUID *agentId, int id, char visible ) {
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;
	
	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	iO = iA->second->visObjects->find( id );

	if ( iO == iA->second->visObjects->end() )
		return 1; // object doesn't exist

	if ( visible == -1 )
		iO->second->visible = !iO->second->visible;
	else
		iO->second->visible = (visible == 1);

	return 0;
}

int DDBStore::VisGetPath( UUID *agentId, int id, DataStream *ds, UUID *thread ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*agentId);

	ds->reset();
	ds->packUUID( thread );
	
	if ( iterA == this->DDBAgents.end() ) { // unknown agent
		ds->packChar( DDBR_NOTFOUND );
		return 0;
	}

	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;
	std::list<DDBVis_NODE>::iterator iN;
	if ( id != -1 ) { // find the specified path
		iP = iterA->second->visPaths->find( id );
		if ( iP == iterA->second->visPaths->end() ) { // unknown path
			ds->packChar( DDBR_NOTFOUND );
		} else { 
			ds->packChar( DDBR_OK );

			ds->packInt32( (int)iP->second->size() );
			
			for ( iN = iP->second->begin(); iN != iP->second->end(); iN++ ) { 
				ds->packData( (DDBVis_NODE *)&(*iN), sizeof(DDBVis_NODE) );
			}
		}
	} else { // enumerate all paths
		ds->packChar( DDBR_OK );

		for ( iP = iterA->second->visPaths->begin(); iP != iterA->second->visPaths->end(); iP++ ) {
			ds->packBool( 1 );
			ds->packInt32( iP->first );
			ds->packInt32( (int)iP->second->size() );
			for ( iN = iP->second->begin(); iN != iP->second->end(); iN++ ) { 
				ds->packData( (DDBVis_NODE *)&(*iN), sizeof(DDBVis_NODE) );
			}
		}
		ds->packBool( 0 ); // done
	}

	return 0;
}

int DDBStore::VisObjectGetInfo( UUID *agentId, int id, int infoFlags, DataStream *ds, UUID *thread ) {
	mapDDBAgent::iterator iterA = this->DDBAgents.find(*agentId);

	ds->reset();
	ds->packUUID( thread );

	if ( iterA == this->DDBAgents.end() ) { // unknown agent
		ds->packChar( DDBR_NOTFOUND );
		return 0;
	}

	std::map<int,DDBVis_OBJECT *>::iterator iO;
	std::list<DDBVis_PATH_REFERENCE>::iterator iPR;
	if ( id != -1 ) { // find specified object
		iO = iterA->second->visObjects->find( id );
		if ( iO == iterA->second->visObjects->end() ) { // unknown object
			ds->packChar( DDBR_NOTFOUND );
		} else { 
			ds->packChar( DDBR_OK );
			ds->packInt32( infoFlags );

			if ( infoFlags & DDBVISOBJECTINFO_NAME ) {
				ds->packString( iO->second->name );
			}

			if ( infoFlags & DDBVISOBJECTINFO_POSE ) {
				ds->packFloat32( iO->second->x );
				ds->packFloat32( iO->second->y );
				ds->packFloat32( iO->second->r );
				ds->packFloat32( iO->second->s );
			}
			
			if ( infoFlags & DDBVISOBJECTINFO_EXTRA ) {
				ds->packBool( iO->second->solid );
				ds->packBool( iO->second->visible );
			}

			if ( infoFlags & DDBVISOBJECTINFO_PATHS ) {
				std::list<DDBVis_PATH_REFERENCE>::iterator iPR;
				ds->packInt32( (int)iO->second->path_refs.size() );
				for ( iPR = iO->second->path_refs.begin(); iPR != iO->second->path_refs.end(); iPR++ ) {
					ds->packData( (DDBVis_PATH_REFERENCE*)&(*iPR), sizeof(DDBVis_PATH_REFERENCE) );
				}
			}
		}
	} else { // enumerate all objects
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );

		for ( iO = iterA->second->visObjects->begin(); iO != iterA->second->visObjects->end(); iO++ ) {
			ds->packBool( 1 );

			ds->packInt32( iO->first );

			if ( infoFlags & DDBVISOBJECTINFO_NAME ) {
				ds->packString( iO->second->name );
			}

			if ( infoFlags & DDBVISOBJECTINFO_POSE ) {
				ds->packFloat32( iO->second->x );
				ds->packFloat32( iO->second->y );
				ds->packFloat32( iO->second->r );
				ds->packFloat32( iO->second->s );
			}
			
			if ( infoFlags & DDBVISOBJECTINFO_EXTRA ) {
				ds->packBool( iO->second->solid );
				ds->packBool( iO->second->visible );
			}

			if ( infoFlags & DDBVISOBJECTINFO_PATHS ) {
				ds->packInt32( (int)iO->second->path_refs.size() );
				for ( iPR = iO->second->path_refs.begin(); iPR != iO->second->path_refs.end(); iPR++ ) {
					ds->packData( (DDBVis_PATH_REFERENCE*)&(*iPR), sizeof(DDBVis_PATH_REFERENCE) );
				}
			}
		}
		ds->packBool( 0 ); // done 
	}

	return 0;
}

int DDBStore::VisClearAll( UUID *agentId, char clearPaths ) {
	mapDDBAgent::iterator iA;
	int id;
	std::map<int,DDBVis_OBJECT *>::iterator iO;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;

	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	for ( iO = iA->second->visObjects->begin(); iO != iA->second->visObjects->end(); ) {
		id = iO->first;
		iO++;
		this->VisRemoveObject( agentId, id );
	}

	if ( clearPaths ) {
		for ( iP = iA->second->visPaths->begin(); iP != iA->second->visPaths->end(); iP++ ) {
			id = iP->first;
			iP++;
			this->VisRemovePath( agentId, id );
		}
	}

	return 0;
}

int DDBStore::VisListObjects( UUID *agentId, list<int> *objects ) {
	mapDDBAgent::iterator iA;
	std::map<int,DDBVis_OBJECT *>::iterator iO;

	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	for ( iO = iA->second->visObjects->begin(); iO != iA->second->visObjects->end(); iO++ ) {
		objects->push_back( iO->first );
	}

	return 0;
}

int DDBStore::VisListPaths( UUID *agentId, list<int> *paths ) {
	mapDDBAgent::iterator iA;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iP;

	iA = this->DDBAgents.find(*agentId);

	if ( iA == this->DDBAgents.end() )
		return 1; // agent not found

	for ( iP = iA->second->visPaths->begin(); iP != iA->second->visPaths->end(); iP++ ) {
		paths->push_back( iP->first );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// DDBRegion

int DDBStore::EnumerateRegions( DataStream *ds ) {
	int count;
	mapDDBRegion::iterator iter;

	count = 0;
	
	// pack all the objects
	iter = this->DDBRegions.begin();
	while ( iter != this->DDBRegions.end() ) {
		ds->packInt32( DDB_REGION );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packFloat32( iter->second->x );
		ds->packFloat32( iter->second->y );
		ds->packFloat32( iter->second->w );
		ds->packFloat32( iter->second->h );

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseRegion( DataStream *ds, UUID *parsedId ) {
	UUID id;
	float x, y, w, h;
	mapDDBRegion::iterator iter;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBRegions.find( id );

	if ( iter == this->DDBRegions.end() ) { // new object
		// match data format to Enumerate<Type> function
		x = ds->unpackFloat32();
		y = ds->unpackFloat32();
		w = ds->unpackFloat32();
		h = ds->unpackFloat32();
		
		this->AddRegion( &id, x, y, w, h );
	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}

int DDBStore::AddRegion( UUID *id, float x, float y, float w, float h ) {
	
	// create DDBRegion
	DDBRegion *region = (DDBRegion *)malloc(sizeof(DDBRegion));
	if ( !region )
		return 1;

	// initialize
	region->x = x;
	region->y = y;
	region->w = w;
	region->h = h;

	// insert into DDBRegions
	this->DDBRegions[*id] = region;

	return 0;
}

int DDBStore::RemoveRegion( UUID *id ) {
	mapDDBRegion::iterator iter = this->DDBRegions.find(*id);

	if ( iter == this->DDBRegions.end() )
		return 1;

	DDBRegion *region = iter->second;

	this->DDBRegions.erase( *id );

	// free stuff
	free( region );

	return 0;
}

int DDBStore::GetRegion( UUID *id, DataStream *ds, UUID *thread ) {
	mapDDBRegion::iterator iter = this->DDBRegions.find(*id);
	
	ds->reset();
	ds->packUUID( thread );
	
	if ( iter == this->DDBRegions.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1; // not found
	}
	
	DDBRegion *region = iter->second;

	// send region
	ds->packChar( DDBR_OK );
	ds->packFloat32( region->x );
	ds->packFloat32( region->y );
	ds->packFloat32( region->w );
	ds->packFloat32( region->h );

	return 0;
}

//-----------------------------------------------------------------------------
// DDBLandmark

int DDBStore::EnumerateLandmarks( DataStream *ds ) {
	int count;
	mapDDBLandmark::iterator iter;

	count = 0;
	
	// pack all the objects
	iter = this->DDBLandmarks.begin();
	while ( iter != this->DDBLandmarks.end() ) {
		ds->packInt32( DDB_LANDMARK );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packData( iter->second, sizeof(DDBLandmark) );

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseLandmark( DataStream *ds, UUID *parsedId ) {
	UUID id;
	DDBLandmark landmark;
	mapDDBLandmark::iterator iter;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBLandmarks.find( id );

	if ( iter == this->DDBLandmarks.end() ) { // new object
		// match data format to Enumerate<Type> function
		landmark = *(DDBLandmark *)ds->unpackData( sizeof(DDBLandmark) );
		
		this->AddLandmark( &id, landmark.code, &landmark.owner, landmark.height, landmark.elevation, landmark.x, landmark.y, landmark.estimatedPos, landmark.landmarkType );
	
		this->DDBLandmarks[id]->posInitialized = landmark.posInitialized;
		this->DDBLandmarks[id]->P = landmark.P;
		this->DDBLandmarks[id]->collected = landmark.collected;
		this->DDBLandmarks[id]->trueX = landmark.trueX;
		this->DDBLandmarks[id]->trueY = landmark.trueY;

	} else { // we know this object already, see if we need to update
		
		return -1; // object known
	}

	return 0;
}

int DDBStore::AddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, bool estimatedPos, ITEM_TYPES landmarkType ) {
	
	// check to see if it already exists
	if ( this->DDBLandmarks.find( *id ) != this->DDBLandmarks.end() ) 
		return 1; // already exists

	// create DDBLandmark
	DDBLandmark *landmark = (DDBLandmark *)malloc(sizeof(DDBLandmark));
	if ( !landmark )
		return 1;

	// initialize
	landmark->code = code;
	landmark->owner = *owner;
	landmark->height = height;
	landmark->elevation = elevation;
	landmark->x = x;
	landmark->y = y;
	landmark->estimatedPos = estimatedPos;
	if ( estimatedPos )
		landmark->posInitialized = false;
	else
		landmark->posInitialized = true;
	landmark->collected = false;
	
	landmark->trueX = x;
	landmark->trueY = y;

	landmark->landmarkType = landmarkType;

	// insert into DDBLandmarks
	this->DDBLandmarks[*id] = landmark;

	return 0;
}

int DDBStore::RemoveLandmark( UUID *id ) {
	mapDDBLandmark::iterator iter = this->DDBLandmarks.find(*id);

	if ( iter == this->DDBLandmarks.end() )
		return 1;

	DDBLandmark *landmark = iter->second;

	this->DDBLandmarks.erase( *id );

	// free stuff
	free( landmark );

	return 0;
}

int DDBStore::LandmarkSetInfo( UUID *id, int infoFlags, DataStream *ds ) {
	mapDDBLandmark::iterator iter = this->DDBLandmarks.find( *id );

	if ( iter == this->DDBLandmarks.end() ) {
		return 0; // not found
	}

	DDBLandmark *lm = iter->second;

	if ( infoFlags & DDBLANDMARKINFO_POS ) {
		int i;
		int count = ds->unpackInt32();
		float R = ds->unpackFloat32(); // observation covariance
		
		float *X, *w;
		X = (float *)ds->unpackData( sizeof(float)*2*count ); // observation
		w = (float *)ds->unpackData( sizeof(float)*count ); // weight

		if ( lm->posInitialized ) {
			// update Kalman filter
			float Kw;
			for ( i=0; i<count; i++ ) {
				Kw = w[i] * (lm->P / (lm->P + R)); // calculate Kalman gain, multiplied by weight
				lm->x += Kw*(X[i*2] - lm->x); // update estimation
				lm->y += Kw*(X[i*2+1] - lm->y);

				lm->P = (1 - Kw)*lm->P; // update error covariance
			}
		} else {
			lm->posInitialized = true;

			// prepare initial estimate
			lm->x = 0;
			lm->y = 0;
			for ( i=0; i<count; i++ ) {
				lm->x += X[2*i]*w[i];
				lm->y += X[2*i+1]*w[i];
			}

			lm->P = R;
		}
	}

	if ( infoFlags & DDBLANDMARKINFO_COLLECTED ) {
		Log->log(0,"DDBStore: COLLECTED!");
		lm->collected = true;
		lm->x = ds->unpackFloat32();
		lm->y = ds->unpackFloat32();
		Log->log(0, "DDBStore: COLLECTED at %f %f", lm->x, lm->y);
	}
	if (infoFlags & DDBLANDMARKINFO_DEPOSITED) {
		lm->collected = false;
		lm->x = ds->unpackFloat32();
		lm->y = ds->unpackFloat32();
		Log->log(0, "DDBStore: DEPOSITED at %f %f", lm->x, lm->y);
	}
	return infoFlags;
}

int DDBStore::GetLandmark( UUID *id, DataStream *ds, UUID *thread, bool enumLandmarks ) {
	mapDDBLandmark::iterator iter = this->DDBLandmarks.find(*id);
	
	ds->reset();
	ds->packUUID( thread );
	
	if (enumLandmarks == true) {
		ds->packChar(DDBR_OK);
		ds->packBool(enumLandmarks);
		ds->packInt32((int)this->DDBLandmarks.size());
		for (iter = this->DDBLandmarks.begin(); iter != this->DDBLandmarks.end(); iter++) {
			ds->packUUID((UUID *)&iter->first);
			ds->packData(iter->second, sizeof(DDBLandmark));
		}
		return 0;
	}





	if ( iter == this->DDBLandmarks.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1; // not found
	}
	
	DDBLandmark *landmark = iter->second;

	// send landmark
	ds->packChar( DDBR_OK );
	ds->packData( landmark, sizeof(DDBLandmark) );
	
	return 0;
}

int DDBStore::GetLandmark( unsigned char code, DataStream *ds, UUID *thread ) {
	mapDDBLandmark::iterator iter = this->DDBLandmarks.begin();
	
	while ( iter != this->DDBLandmarks.end() ) {
		if ( iter->second->code == code )
			break;
		iter++;
	}

	ds->reset();
	ds->packUUID( thread );
	
	if ( iter == this->DDBLandmarks.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1; // not found
	}
	
	DDBLandmark *landmark = iter->second;

	// send landmark
	ds->packChar( DDBR_OK );
	ds->packData( landmark, sizeof(DDBLandmark) );
	
	return 0;
}

UUID DDBStore::GetLandmarkId( unsigned char code ) {
	mapDDBLandmark::iterator iter = this->DDBLandmarks.begin();
	
	while ( iter != this->DDBLandmarks.end() ) {
		if ( iter->second->code == code )
			break;
		iter++;
	}
	
	if ( iter == this->DDBLandmarks.end() ) {
		return nilUUID;
	} else {
		return iter->first;
	}
}

bool DDBStore::GetTaskId(UUID *id, UUID *foundId) {
	mapDDBTask::iterator iter = this->DDBTasks.begin();

	while (iter != this->DDBTasks.end()) {
		if (iter->first == *id)
			break;
		iter++;
	}

	if (iter == this->DDBTasks.end()) {
		*foundId = nilUUID;
		return 1;
	}
	else {
		*foundId = iter->first;
		return 0;
	}
}

UUID DDBStore::GetTaskDataId(UUID *id) {
	mapDDBTaskData::iterator iter = this->DDBTaskDatas.begin();

	while (iter != this->DDBTaskDatas.end()) {
		if (iter->first == *id)
			return iter->first;
		iter++;
	}
	return nilUUID;


	//while (iter != this->DDBTaskDatas.end()) {
	//	if (iter->first == *id)
	//		break;
	//	iter++;
	//}

//	if (iter == this->DDBTaskDatas.end()) {
//		return nilUUID;
//	}
//	else {
//		return iter->first;
//	}
}



//-----------------------------------------------------------------------------
// DDBProbabilisticOccupancyGrid

int DDBStore::EnumeratePOGs( DataStream *ds ) {
	int count;
	mapDDBPOG::iterator iter;
	mapDDBPOGTile::iterator iterT;

	count = 0;
	
	// pack all the objects
	iter = this->DDBPOGs.begin();
	while ( iter != this->DDBPOGs.end() ) {
		ds->packInt32( DDB_MAP_PROBOCCGRID );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packFloat32( iter->second->tileSize );
		ds->packFloat32( iter->second->resolution );
		ds->packFloat32( iter->second->xlow );
		ds->packFloat32( iter->second->xhigh );
		ds->packFloat32( iter->second->ylow );
		ds->packFloat32( iter->second->yhigh );

		// enumerate tiles
		iterT = iter->second->tiles->begin();
		while ( iterT != iter->second->tiles->end() ) {
			ds->packBool( 1 );
			ds->packInt32( iterT->first );
			ds->packData( iterT->second, sizeof(float)*iter->second->stride*iter->second->stride );
			iterT++;
		}
		ds->packBool( 0 ); // finished tiles

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParsePOG( DataStream *ds, UUID *parsedId ) {
	UUID id;
	float tileSize, resolution, xlow, xhigh, ylow, yhigh;
	int tileId;
	float *tile;
	mapDDBPOG::iterator iter;
	DDBProbabilisticOccupancyGrid *pog;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBPOGs.find( id );

	if ( iter == this->DDBPOGs.end() ) { // new object
		// match data format to Enumerate<Type> function
		tileSize = ds->unpackFloat32();
		resolution = ds->unpackFloat32();
		xlow = ds->unpackFloat32();
		xhigh = ds->unpackFloat32();
		ylow = ds->unpackFloat32();
		yhigh = ds->unpackFloat32();

		if ( this->AddPOG( &id, tileSize, resolution ) )
			return 1;

		pog = this->DDBPOGs[id];
		pog->xlow = xlow;
		pog->xhigh = xhigh;
		pog->ylow = ylow;
		pog->yhigh = yhigh;
		
		// unpack tiles
		while ( ds->unpackBool() ) {
			tileId = ds->unpackInt32();
			tile = (float *)malloc( sizeof(float)*pog->stride*pog->stride );
			if ( !tile )
				return 1;
			memcpy( tile, ds->unpackData( sizeof(float)*pog->stride*pog->stride ), sizeof(float)*pog->stride*pog->stride );
			(*pog->tiles)[tileId] = tile;
		}
		

	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}

int DDBStore::AddPOG( UUID *id, float tileSize, float resolution ) {
	DDBProbabilisticOccupancyGrid *pog = (DDBProbabilisticOccupancyGrid *)malloc(sizeof(DDBProbabilisticOccupancyGrid));
	if ( !pog )
		return 1;

	// initialize
	pog->age = new mapDataAge;
	pog->tileSize = tileSize;
	pog->resolution = resolution;
	pog->stride = (int)floor(tileSize/resolution);
	pog->tiles = new mapDDBPOGTile;
	pog->xlow = pog->xhigh = pog->ylow = pog->yhigh = 0;
	
#ifdef DEBUG_USEFOW
	pog->fow = new mapDDBPOGTile;
#endif
#ifdef DEBUG_USEBLACKHOLE
	pog->blackhole = new mapDDBPOGTile;
#endif

	// insert into DDBSensors
	this->DDBPOGs[*id] = pog;

	return 0;
}

int DDBStore::RemovePOG( UUID *id ) {
	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);

	if ( iterPOG == this->DDBPOGs.end() )
		return 1;

	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	this->DDBPOGs.erase( *id );

	// free stuff
	delete pog->age;
	while ( !pog->tiles->empty() ) {
		free( pog->tiles->begin()->second );
		pog->tiles->erase( pog->tiles->begin() );
	}
	delete pog->tiles;

#ifdef DEBUG_USEFOW
	while ( !pog->fow->empty() ) {
		free( pog->fow->begin()->second );
		pog->fow->erase( pog->fow->begin() );
	}
	delete pog->fow;
#endif
#ifdef DEBUG_USEBLACKHOLE
	while ( !pog->blackhole->empty() ) {
		free( pog->blackhole->begin()->second );
		pog->blackhole->erase( pog->blackhole->begin() );
	}
	delete pog->blackhole;
#endif

	free( pog );

	return 0;
}

int DDBStore::POGGetInfo( UUID *id, DataStream *ds, UUID *thread ) {
	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);

	ds->reset();
	ds->packUUID( thread );

	if ( iterPOG == this->DDBPOGs.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1;
	}

	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	ds->packChar( DDBR_OK );
	ds->packFloat32( pog->tileSize );
	ds->packFloat32( pog->resolution );
	ds->packInt32( pog->stride );
	ds->packFloat32( pog->xlow );
	ds->packFloat32( pog->xhigh );
	ds->packFloat32( pog->ylow );
	ds->packFloat32( pog->yhigh );
	
	return 0;	
}

int DDBStore::POGVerifyRegion( UUID *id, float x, float y, float w, float h ) {
	int size;
	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);

	if ( iterPOG == this->DDBPOGs.end() )
		return 1;

	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;
	
	// verify that the cooridates are valid
	if ( fabs((float)round(x/pog->resolution) - (float)(x/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(y/pog->resolution) - (float)(y/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(w/pog->resolution) - (float)(w/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(h/pog->resolution) - (float)(h/pog->resolution) ) > 0.1f*pog->resolution ) {
		return 0; // all coordinates must be integer multiples of resolution
	}

	size = (int)(round(w/pog->resolution)*round(h/pog->resolution))*sizeof(float);
	
	return size;
}

int DDBStore::ApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data ) {
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	int dataStride;
	float tileSize; // tileSize in cell space
	int tileId, lastTileId; // tile ids
	float *tile;
	float pC, pSC, pS;
	int ii, jj; // loop vars

#ifdef DEBUG_USEBLACKHOLE
	mapDDBPOGTile::iterator iterbhTile;
	float *bhTile;
#endif

	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);
	mapDDBPOGTile::iterator iterTile;

	if ( iterPOG == this->DDBPOGs.end() )
		return 1;

	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	// convert coords and tileSize into cell space
	x = round(x/pog->resolution);
	y = round(y/pog->resolution);
	w = round(w/pog->resolution);
	h = round(h/pog->resolution);
	tileSize = round(pog->tileSize/pog->resolution);

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
#ifdef DEBUG_USEFOW
				iterTile = (*pog->fow).find(tileId);
				if ( iterTile == (*pog->fow).end() ) {
#else
				iterTile = (*pog->tiles).find(tileId);
				if ( iterTile == (*pog->tiles).end() ) {
#endif				
					// this tile doesn't exist yet, so make it!
					tile = (float*)malloc(sizeof(float)*pog->stride*pog->stride);
					if ( !tile ) {
						return 1; // malloc failed
					}
					for ( ii=0; ii<pog->stride; ii++ ) {
						for ( jj=0; jj<pog->stride; jj++ ) {
							tile[jj+ii*pog->stride] = 0.5f;
						}
					}
#ifdef DEBUG_USEFOW
					(*pog->fow)[tileId] = tile;
#else
					(*pog->tiles)[tileId] = tile;
#endif
				} else {
					tile = iterTile->second;
				}

#ifdef DEBUG_USEBLACKHOLE
				iterbhTile = (*pog->blackhole).find(tileId);
				if ( iterbhTile == (*pog->blackhole).end() ) {		
					// this tile doesn't exist yet, so make it!
					bhTile = (float*)malloc(sizeof(float)*pog->stride*pog->stride);
					if ( !bhTile ) {
						return 1; // malloc failed
					}
					for ( ii=0; ii<pog->stride; ii++ ) {
						for ( jj=0; jj<pog->stride; jj++ ) {
							bhTile[jj+ii*pog->stride] = 0.5f;
						}
					}
					(*pog->blackhole)[tileId] = bhTile;
				} else {
					bhTile = iterbhTile->second;
				}
#endif
				lastTileId = tileId;
			}

			// update cell
#ifdef DEBUG_USEFOW
			pSC = min( 0.95f, max( 0.05f, fabs(data[r+c*dataStride]-0.5f)+0.5f ) );
#else
			pSC = min( 0.95f, max( 0.05f, data[r+c*dataStride] ) );
#endif
			if ( i >= pog->stride || j >= pog->stride || i < 0 || j < 0 ) // TEMP
				i=i;
			pC = tile[j+i*pog->stride];
			pS = pC*pSC + (1-pC)*(1-pSC);
			tile[j+i*pog->stride] = pC*pSC/pS;

#ifdef DEBUG_USEBLACKHOLE
			pSC = min( 0.95f, max( 0.05f, data[r+c*dataStride] ) );
			pC = bhTile[j+i*pog->stride];
			pS = pC*pSC + (1-pC)*(1-pSC);
			bhTile[j+i*pog->stride] = pC*pSC/pS;
#endif
		}
	}

	// update bounds
	if ( pog->xlow == pog->xhigh ) { // first region
		pog->xlow = x * pog->resolution;
		pog->xhigh = pog->xlow + w * pog->resolution;
		pog->ylow = y * pog->resolution;
		pog->yhigh = pog->ylow + h * pog->resolution;
	} else {
		if ( pog->xlow > x * pog->resolution ) pog->xlow = x * pog->resolution;
		if ( pog->xhigh < (x + w) * pog->resolution ) pog->xhigh = (x + w) * pog->resolution;
		if ( pog->ylow > y * pog->resolution ) pog->ylow = y * pog->resolution;
		if ( pog->yhigh < (y + h) * pog->resolution ) pog->yhigh = (y + h) * pog->resolution;
	}

	return 0;
}

int DDBStore::POGGetRegion( UUID *id, float x, float y, float w, float h, DataStream *ds, UUID *thread ) {
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // region coordinates
	int stride;
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	float *tile;
	float *region;

#ifdef DEBUG_USEFOW
	float *fow;
	mapDDBPOGTile::iterator iterFOW;
#endif
	
	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);
	mapDDBPOGTile::iterator iterTile;

	ds->reset();
	ds->packUUID( thread );
	
	if ( iterPOG == this->DDBPOGs.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1; // not found
	}
	
	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	// validate region coordinates
	if ( fabs((float)round(x/pog->resolution) - (float)(x/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(y/pog->resolution) - (float)(y/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(w/pog->resolution) - (float)(w/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(h/pog->resolution) - (float)(h/pog->resolution) ) > 0.1f*pog->resolution ) {
		ds->packChar( DDBR_INVALIDARGS );
		return 1; // all coordinates must be integer multiples of resolution
	}

	// convert coords and tileSize into cell space
	x = round(x/pog->resolution);
	y = round(y/pog->resolution);
	w = round(w/pog->resolution);
	h = round(h/pog->resolution);
	tileSize = round(pog->tileSize/pog->resolution);

	stride = (int)h;

	// allocate region
	region = (float *)malloc(sizeof(float)*(int)w*(int)h);
	if ( !region ) {
		ds->packChar( DDBR_INTERNALERROR );
		return 1; // malloc failed
	}

	// populate region
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
				iterTile = (*pog->tiles).find(tileId);

#ifdef DEBUG_USEFOW
				iterFOW = (*pog->fow).find(tileId);
#endif
				
				lastTileId = tileId;
			}

#ifdef DEBUG_USEFOW
			if ( iterTile != (*pog->tiles).end() && iterFOW != (*pog->fow).end() ) {
				tile = iterTile->second;
				fow = iterFOW->second;

				region[r+c*stride] = (tile[j+i*pog->stride]-0.5f)*(fow[j+i*pog->stride]-0.5f)*2 + 0.5f;
#else
			if ( iterTile != (*pog->tiles).end() ) {
				tile = iterTile->second;
				region[r+c*stride] = tile[j+i*pog->stride];
#endif
			} else { // tile doesn't exist, so fill in blank
				region[r+c*stride] = 0.5f;
			}
		}
	}

	// send region
	ds->packChar( DDBR_OK );
	ds->packFloat32( x*pog->resolution );
	ds->packFloat32( y*pog->resolution );
	ds->packFloat32( w*pog->resolution );
	ds->packFloat32( h*pog->resolution );
	ds->packFloat32( pog->resolution );
	ds->packData( region, sizeof(float)*(int)w*(int)h );
	
	free( region );

	return 0;
}

int DDBStore::POGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename ) {
	FILE *fp;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	float *tile;
	float val;
	int ii, jj; // loop vars

	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);
	mapDDBPOGTile::iterator iterTile;

	if ( iterPOG == this->DDBPOGs.end() )
		return 1;

	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	if ( fopen_s( &fp, filename, "r" ) ) {
		return 1; 
	}

	// convert coords and tileSize into cell space
	x = round(x/pog->resolution);
	y = round(y/pog->resolution);
	w = round(w/pog->resolution);
	h = round(h/pog->resolution);
	tileSize = round(pog->tileSize/pog->resolution);

	lastTileId = -1;
	for ( r=(int)h-1; r>=0; r-- ) {
		v = (int)floor((y+r)/tileSize);
		j = (int)(y+r - v*tileSize);
	
		for ( c=0; c<(int)w; c++ ) {
			u = (int)floor((x+c)/tileSize);
			i = (int)(x+c - u*tileSize);

			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = (*pog->tiles).find(tileId);
				if ( iterTile == (*pog->tiles).end() ) {
					// this tile doesn't exist yet, so make it!
					tile = (float*)malloc(sizeof(float)*pog->stride*pog->stride);
					if ( !tile ) {
						fclose( fp );
						return 1; // malloc failed
					}
					for ( ii=0; ii<pog->stride; ii++ ) {
						for ( jj=0; jj<pog->stride; jj++ ) {
							tile[jj+ii*pog->stride] = 0.5f;
						}
					}
					(*pog->tiles)[tileId] = tile;
				} else {
					tile = iterTile->second;
				}
				lastTileId = tileId;
			}

			// update cell
			if ( 1 != fscanf_s( fp, "%f", &val ) ) {
				fclose( fp );
				return 1;
			}
			tile[j+i*pog->stride] = val;
		}
	}

	fclose( fp );

	// update bounds
	if ( pog->xlow == pog->xhigh ) { // first region
		pog->xlow = x * pog->resolution;
		pog->xhigh = pog->xlow + w * pog->resolution;
		pog->ylow = y * pog->resolution;
		pog->yhigh = pog->ylow + h * pog->resolution;
	} else {
		if ( pog->xlow > x * pog->resolution ) pog->xlow = x * pog->resolution;
		if ( pog->xhigh < (x + w) * pog->resolution ) pog->xhigh = (x + w) * pog->resolution;
		if ( pog->ylow > y * pog->resolution ) pog->ylow = y * pog->resolution;
		if ( pog->yhigh < (y + h) * pog->resolution ) pog->yhigh = (y + h) * pog->resolution;
	}

	return 0;
}

int DDBStore::POGDumpRegion( UUID *id, float x, float y, float w, float h, char *filename ) {
	RPC_WSTR uuidStr;
	UuidToString(&*id, &uuidStr);
	FILE *fp;
	int u, v; // tile coordinates
	int i, j; // cell coordinates
	int c, r; // data coordinates
	float tileSize; // in cell space
	int tileId, lastTileId; // tile ids
	float *tile;
 
#ifdef DEBUG_USEFOW
	float *fow;
	mapDDBPOGTile::iterator iterFOW;
#endif
#ifdef DEBUG_USEBLACKHOLE
	FILE *bhfp;
	float *bhTile;
	mapDDBPOGTile::iterator iterbhTile;
#endif

	mapDDBPOG::iterator iterPOG = this->DDBPOGs.find(*id);
	mapDDBPOGTile::iterator iterTile;

	if (iterPOG == this->DDBPOGs.end()) {
		Log->log(0, "DDBStore::POGDUMP iterPOG == this->DDBPOGs.end()");
		
		return 1;
	}
	DDBProbabilisticOccupancyGrid *pog = iterPOG->second;

	// verify that the cooridates are valid
	if ( fabs((float)round(x/pog->resolution) - (float)(x/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(y/pog->resolution) - (float)(y/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(w/pog->resolution) - (float)(w/pog->resolution) ) > 0.1f*pog->resolution
	  || fabs((float)round(h/pog->resolution) - (float)(h/pog->resolution) ) > 0.1f*pog->resolution ) {
		Log->log(0, "DDBStore::POGDUMP all coordinates must be integer multiples of resolution");
		return 1; // all coordinates must be integer multiples of resolution
	}

	if ( fopen_s( &fp, filename, "w" ) ) {
		Log->log(0, "DDBStore::POGDUMP file op problem");
		return 1; 
	}

#ifdef DEBUG_USEBLACKHOLE
	char bhbuf[512];
	sprintf_s( bhbuf, 512, "%s.BH.txt", filename );
	if ( fopen_s( &bhfp, bhbuf, "w" ) ) {
		return 1;
	}
#endif

	// convert coords and tileSize into cell space
	x = round(x/pog->resolution);
	y = round(y/pog->resolution);
	w = round(w/pog->resolution);
	h = round(h/pog->resolution);
	tileSize = round(pog->tileSize/pog->resolution);

	lastTileId = -1;
	for ( r=(int)h-1; r>=0; r-- ) {
		v = (int)floor((y+r)/tileSize);
		j = (int)(y+r - v*tileSize);

		for ( c=0; c<(int)w; c++ ) {
			u = (int)floor((x+c)/tileSize);
			i = (int)(x+c - u*tileSize);

			// get the tile
			tileId = DDB_TILE_ID(u,v);
			if ( tileId != lastTileId ) {
				iterTile = (*pog->tiles).find(tileId);

#ifdef DEBUG_USEFOW
				iterFOW = (*pog->fow).find(tileId);
#endif
#ifdef DEBUG_USEBLACKHOLE
				iterbhTile = (*pog->blackhole).find(tileId);
#endif
				lastTileId = tileId;
			}

#ifdef DEBUG_USEFOW
			if ( iterTile != (*pog->tiles).end() && iterFOW != (*pog->fow).end() ) {
				tile = iterTile->second;
				fow = iterFOW->second;

				fprintf( fp, "%f\t", (tile[j+i*pog->stride]-0.5f)*(fow[j+i*pog->stride]-0.5f)*2 + 0.5f );
#else
			if ( iterTile != (*pog->tiles).end() ) {
				tile = iterTile->second;
				// dump cell
				fprintf( fp, "%f\t", tile[j+i*pog->stride] );
#endif
			} else {
				// tile doesn't exist so dump blank
				fprintf( fp, "%f\t", 0.5f );
			}

#ifdef DEBUG_USEBLACKHOLE
			if ( iterbhTile != (*pog->blackhole).end() ) {
				bhTile = iterbhTile->second;
				// dump cell
				fprintf( bhfp, "%f\t", bhTile[j+i*pog->stride] );
			} else {
				// tile doesn't exist so dump blank
				fprintf( bhfp, "%f\t", 0.5f );
			}
#endif
		}
		// end row
		fprintf( fp, "\n" );
	}

	fclose( fp );

	return 0;
}


//-----------------------------------------------------------------------------
// DDBParticleFilter

void FreeFilter( DDBParticleFilter *pf ) {
	DDBParticleRegion *pr;

	if ( pf != NULL ) {
		free( pf->stateCurrent );
		while ( pf->regions->begin() != pf->regions->end() ) {
			pr = *pf->regions->begin();
			pf->regions->pop_front();
			while ( pr->particles->begin() != pr->particles->end() ) {
				delete pr->particles->front();
				pr->particles->pop_front();
			}
			delete pr->particles;
			while ( pr->times->begin() != pr->times->end() ) {
				free( pr->times->front() );
				pr->times->pop_front();
			}
			delete pr->times;
			while ( pr->states->begin() != pr->states->end() ) {
				free( pr->states->front() );
				pr->states->pop_front();
			}
			delete pr->states;
			free( pr );
		}
		delete pf->age;
		delete pf->regions;

		free( pf );
	}
}

int DDBStore::EnumerateParticleFilters( DataStream *ds ) {
	int count;
	mapDDBParticleFilter::iterator iter;
	std::list<DDBParticleRegion*>::reverse_iterator iterR; // region
	std::list<DDBParticle*>::iterator iterP; // particle
	std::list<_timeb*>::iterator iterT; // time
	std::list<float*>::iterator iterS; // state

	count = 0;
	
	// pack all the objects
	iter = this->DDBParticleFilters.begin();
	while ( iter != this->DDBParticleFilters.end() ) {
		ds->packInt32( DDB_PARTICLEFILTER );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packUUID( &iter->second->owner );
		ds->packInt32( iter->second->regionCount );
		ds->packInt32( iter->second->depreciatedCount );
		ds->packUInt32( iter->second->particleNum );
		ds->packUInt32( iter->second->stateSize );
		ds->packData( iter->second->stateCurrent, sizeof(float)*iter->second->stateSize );
		ds->packBool( iter->second->currentDirty );
		
		ds->packInt32( iter->second->forwardMarker );

		ds->packInt32( iter->second->obsSinceLastWeightUpdate );
		ds->packInt32( iter->second->totalObs );

		// enumerate regions
		iterR = iter->second->regions->rbegin();
		while ( iterR != iter->second->regions->rend() ) {
			if ( (*iterR)->depreciated ) {
				iterR++;
				continue; // skip
			}

			ds->packBool( 1 );
			
			ds->packBool( (*iterR)->depreciated );
			ds->packData( &(*iterR)->startTime, sizeof(_timeb) );
			ds->packInt32( (*iterR)->index );
			ds->packBool( (*iterR)->weightsDirty );

			// enumerate particles
			iterP = (*iterR)->particles->begin();
			while ( iterP != (*iterR)->particles->end() ) {
				ds->packBool( 1 );

				// particle
				ds->packInt32( (*iterP)->index );
				ds->packFloat32( (*iterP)->weight );
				ds->packFloat32( (*iterP)->obsDensity );
				ds->packFloat32( (*iterP)->obsDensityForward );
				if ( (*iterR)->index != 0 ) { // we have parents
					ds->packInt32( (*(*iterP)->parent)->index );
				}

				iterP++;
			}
			ds->packBool( 0 ); // done particles

			// enumerate times and states
			iterT = (*iterR)->times->begin();
			iterS = (*iterR)->states->begin();
			while ( iterT != (*iterR)->times->end() ) {
				ds->packBool( 1 );

				// time
				ds->packData( (_timeb *)*iterT, sizeof(_timeb) );

				// state
				ds->packData( *iterS, sizeof(float)*iter->second->particleNum*iter->second->stateSize );

				iterT++;
				iterS++;
			}
			ds->packBool( 0 ); // done times and states

			iterR++;
		}
		ds->packBool( 0 ); // done regions

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseParticleFilter( DataStream *ds, UUID *parsedId ) {
	UUID id;
	mapDDBParticleFilter::iterator iter;
	
	_timeb *tb;
	float			  *ps;
	DDBParticle		  *p;
	DDBParticleRegion *pr;
	DDBParticleFilter *pf;

	std::list<DDBParticle*> *lastParticles;
	int parentIndex;
	std::list<DDBParticle*>::iterator parent;
	std::map<int,std::list<DDBParticle*>::iterator> parentMap;
	std::map<int,std::list<DDBParticle*>::iterator>::iterator iterParent;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBParticleFilters.find( id );

	if ( iter == this->DDBParticleFilters.end() ) { // new object
	
		_timeb unpackStart, unpackEnd;
		apb->apb_ftime_s( &unpackStart );

		// match data format to Enumerate<Type> function
		pf = (DDBParticleFilter *)malloc(sizeof(DDBParticleFilter));
		if ( !pf )
			return 1;

		bool firstRegion = true;

		// initialize
		ds->unpackUUID( &pf->owner );
		pf->age = new mapDataAge;
		pf->regionCount = ds->unpackInt32();
		pf->depreciatedCount = ds->unpackInt32();
		pf->particleNum = ds->unpackUInt32();
		pf->stateSize = ds->unpackUInt32();
		pf->stateCurrent = (float *)malloc(sizeof(float)*pf->stateSize);
		if ( pf->stateCurrent == NULL ) {
			FreeFilter( pf );
			return 1;
		}
		memcpy( pf->stateCurrent, ds->unpackData( sizeof(float)*pf->stateSize ), sizeof(float)*pf->stateSize );
		pf->currentDirty = ds->unpackBool();
		pf->forwardMarker = ds->unpackInt32();

		pf->obsSinceLastWeightUpdate = ds->unpackInt32();
		pf->totalObs = ds->unpackInt32();

		pf->regions = new list<DDBParticleRegion*>;

		// parse regions
		while ( ds->unpackBool() ) {
			// create region
			pr = (DDBParticleRegion *)malloc(sizeof(DDBParticleRegion));
			if ( pr == NULL ) {
				FreeFilter( pf );
				return 1;
			}

			pr->depreciated = ds->unpackBool();
			pr->startTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
			pr->index = ds->unpackInt32();
			pr->weightsDirty = ds->unpackBool();
			
			pr->particles = new std::list<DDBParticle*>();
			pr->times = new std::list<_timeb*>();
			pr->states = new std::list<float*>();

			pf->regions->push_front( pr );

			// parse particles
			while ( ds->unpackBool() ) {
				// particle
				p = new DDBParticle;
				if ( !p ) {
					FreeFilter( pf );
					return 1;
				}
				p->index = ds->unpackInt32();
				p->weight = ds->unpackFloat32();
				p->obsDensity = ds->unpackFloat32();
				if ( !_finite( p->obsDensity ) || _isnan( p->obsDensity ) )
					p=p;
				p->obsDensityForward = ds->unpackFloat32();
				if ( !_finite( p->obsDensityForward ) || _isnan( p->obsDensityForward ) )
					p=p;
				if ( !firstRegion ) { // we have parents
					parentIndex = ds->unpackInt32();
					iterParent = parentMap.find( parentIndex );
					p->parent = iterParent->second;
				} else { // we don't have parents, either because we were the first region or the previous region was depreciated
					if ( pr->index != 0 ) // unpack parent index
						parentIndex = ds->unpackInt32();
				}
				pr->particles->push_back( p );
			}
			lastParticles = pr->particles;
			// update parent map
			for ( parent = lastParticles->begin(); parent != lastParticles->end(); parent++ ) {
				parentMap[(*parent)->index] = parent;	
			}

			// parse times and states
			while ( ds->unpackBool() ) {
				// time
				tb = (_timeb *)malloc(sizeof(_timeb));
				if ( !tb ) {
					FreeFilter( pf );
					return 1;
				}
				memcpy( tb, ds->unpackData( sizeof(_timeb) ), sizeof(_timeb) );
				pr->times->push_back( tb );

				// state
				ps = (float *)malloc( sizeof(float)*pf->particleNum*pf->stateSize );
				if ( !ps ) {
					FreeFilter( pf );
					return 1;
				}
				memcpy( ps, ds->unpackData( sizeof(float)*pf->particleNum*pf->stateSize ), sizeof(float)*pf->particleNum*pf->stateSize );
				pr->states->push_back( ps );
			}

			firstRegion = false;
		}
			
		// insert into DDBParticleFilters
		this->DDBParticleFilters[id] = pf;

		apb->apb_ftime_s( &unpackEnd );
		unsigned int dt = (unsigned int)(1000*(unpackEnd.time - unpackStart.time) + unpackEnd.millitm - unpackStart.millitm);
		this->DEBUGtotalPFUnpackTime += dt;

	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}

int DDBStore::AddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize ) {
	int i;
	_timeb *tb;
	float			  *ps;
	DDBParticle		  *p;
	DDBParticleRegion *pr;
	DDBParticleFilter *pf = (DDBParticleFilter *)malloc(sizeof(DDBParticleFilter));
	if ( !pf )
		return 1;

	// initialize
	pf->owner = *owner;
	pf->age = new mapDataAge;
	pf->regionCount = 0;
	pf->depreciatedCount = 0;
	pf->particleNum = numParticles;
	pf->stateSize = stateSize;
	pf->stateCurrent = (float *)malloc(sizeof(float)*stateSize);
	if ( pf->stateCurrent == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	pf->currentDirty = true;
	pf->predictionHeld = false;
	pf->regions = new list<DDBParticleRegion*>;
	pf->forwardMarker = 0;
	
	pf->obsSinceLastWeightUpdate = 0;
	pf->totalObs = 0;

	// create first region
	pr = (DDBParticleRegion *)malloc(sizeof(DDBParticleRegion));
	if ( pr == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	pr->index = pf->regionCount;
	pr->depreciated = 0;
	pr->startTime.time = startTime->time;
	pr->startTime.millitm = startTime->millitm;
	pr->particles = new std::list<DDBParticle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<float*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	tb->time = startTime->time;
	tb->millitm = startTime->millitm;
	pr->times->push_front( tb );

	ps = (float *)malloc(sizeof(float)*numParticles*stateSize);
	if ( ps == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	memcpy( ps, startState, sizeof(float)*numParticles*stateSize );
	pr->states->push_front( ps );
	
	pf->regions->push_front( pr );
	pf->regionCount++;

	// create particles
	for ( i=0; i<numParticles; i++ ) {
		p = new DDBParticle;
		if ( p == NULL ) {
			FreeFilter( pf );
			return 1;
		}
		p->index = i;
		p->weight = 1.0f/numParticles;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		//p->parent = NULL;

		pr->particles->push_front( p );
	}

	// insert into DDBSensors
	this->DDBParticleFilters[*id] = pf;

	return 0;
}

int DDBStore::RemoveParticleFilter( UUID *id ) {
	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 1;

	DDBParticleFilter *pf = iterPF->second;

	this->DDBParticleFilters.erase( *id );

	// free stuff
	FreeFilter( pf );

	return 0;
}

int DDBStore::ResamplePF_Prepare( UUID *id, DataStream *ds, float *effectiveParticleNum ) {
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticle*>::iterator p;
	std::list<DDBParticle*>::iterator parentP;
	float weightedSumObsDensity;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 1;

	DDBParticleFilter *pf = iterPF->second;
	
	pr = pf->regions->begin();
	// propagate observations forward to last region
	this->_PFPropagateOD( pf, (*pr)->index );
	// update weights in last region
	if ( (*pr)->weightsDirty ) {
		weightedSumObsDensity = 0;
		p = (*pr)->particles->begin();
		while ( p != (*pr)->particles->end() ) {
			weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
			p++;
		}
		*effectiveParticleNum = 0;
		p = (*pr)->particles->begin();
		while ( p != (*pr)->particles->end() ) {
			(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
			*effectiveParticleNum += (*p)->weight * (*p)->weight;
			(*p)->obsDensity = 1;
			p++;
		}
		*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
		(*pr)->weightsDirty = false;

		Log->log( LOG_LEVEL_VERBOSE, "DDBStore::ResamplePF_Prepare: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
		pf->obsSinceLastWeightUpdate = 0;
	} else { // just calculate effectiveParticleNum
		*effectiveParticleNum = 0;
		p = (*pr)->particles->begin();
		while ( p != (*pr)->particles->end() ) {
			*effectiveParticleNum += (*p)->weight * (*p)->weight;
			p++;
		}
		*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
	}

	ds->reset();
	ds->packUUID( id );
	ds->packInt32( pf->particleNum );

	// pack weights
	p = (*pr)->particles->begin();
	while ( p != (*pr)->particles->end() ) {
		ds->packFloat32( (*p)->weight );
		p++;
	}

	return 0;
}

int DDBStore::ResamplePF_Apply( UUID *id, int *parents, float *state ) {
	int i;
	DDBParticleRegion *pr;
	DDBParticle		  *p;
	float			  *ps;
	_timeb			  *tb;
	std::list<DDBParticle*>::iterator parentP;
	int parentNum;
	_timeb			  startTime;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 1;

	DDBParticleFilter *pf = iterPF->second;

	pf->currentDirty = true;
	pf->predictionHeld = false;

	startTime = *pf->regions->front()->times->front();

	// create new region
	pr = (DDBParticleRegion *)malloc(sizeof(DDBParticleRegion));
	if ( pr == NULL ) {
		return 1;
	}
	pr->index = pf->regionCount;
	pr->depreciated = 0;
	pr->startTime.time = startTime.time;
	pr->startTime.millitm = startTime.millitm;
	pr->particles = new std::list<DDBParticle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<float*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		free( pr );
		return 1;
	}
	tb->time = startTime.time;
	tb->millitm = startTime.millitm;
	pr->times->push_front( tb );

	ps = (float *)malloc(sizeof(float)*pf->particleNum*pf->stateSize);
	if ( ps == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	memcpy( ps, state, sizeof(float)*pf->particleNum*pf->stateSize );
	pr->states->push_front( ps );
	
	parentP = pf->regions->front()->particles->begin();
	parentNum = 0;

	pf->regions->push_front( pr );
	pf->regionCount++;

	// create particles
	for ( i=0; i<(int)pf->particleNum; i++ ) {
		p = new DDBParticle;
		if ( p == NULL ) {
			FreeFilter( pf );
			return 1;
		}

		while ( parentNum < parents[i] ) {
			parentP++;
			parentNum++;
		}

		p->index = i;
		p->weight = 1.0f/pf->particleNum;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		p->parent = parentP;

		pr->particles->push_front( p );
	}


	// check if any regions should be depreciated
	int dt;
	_timeb rEndTime;
	std::list<DDBParticleRegion*>::iterator iR;
	iR = pf->regions->begin();
	while ( iR != pf->regions->end() ) {
		if ( (*iR)->depreciated )
			break; // everyone passed here must already be depreciated
		rEndTime = *(*iR)->times->front();
		dt = (int)((startTime.time - rEndTime.time)*1000 + (startTime.millitm - rEndTime.millitm));
		if ( dt > DDBPF_REGIONDEPRECIATIONTIME ) {
			(*iR)->depreciated = 1;
			pf->depreciatedCount++;
			Log->log( LOG_LEVEL_NORMAL, "DDBStore::ResamplePF_Apply: depreciating region %d (%s), dt %d", (*iR)->index, Log->formatUUID(LOG_LEVEL_NORMAL,id), dt );
		}
		iR++;
	}


	return 0;
}

int DDBStore::InsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange ) {
	DDBParticleRegion *pr;
	float			  *ps;
	_timeb *time;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 1;

	DDBParticleFilter *pf = iterPF->second;
	
	pf->currentDirty = true;

	if ( nochange ) {
		if ( pf->predictionHeld ) { // just need to update the last time
			// get top region
			pr = pf->regions->front(); 

			memcpy( pr->times->front(), tb, sizeof(_timeb) );
		} else { // copy the last state to a new state
			pf->predictionHeld = true;

			time = (_timeb *)malloc(sizeof(_timeb));
			if ( time == NULL ) {
				return 1;
			}
			memcpy( time, tb, sizeof(_timeb) );

			ps = (float *)malloc(sizeof(float)*pf->particleNum*pf->stateSize);
			if ( ps == NULL ) {
				free( time );
				return 1;
			}

			// get top region
			pr = pf->regions->front(); 

			memcpy( ps, pr->states->front(), sizeof(float)*pf->particleNum*pf->stateSize );

			pr->times->push_front( time );
			pr->states->push_front( ps );
		}
	} else { // insert prediction normally
		pf->predictionHeld = false;

		time = (_timeb *)malloc(sizeof(_timeb));
		if ( time == NULL ) {
			return 1;
		}
		memcpy( time, tb, sizeof(_timeb) );

		ps = (float *)malloc(sizeof(float)*pf->particleNum*pf->stateSize);
		if ( ps == NULL ) {
			free( time );
			return 1;
		}
		memcpy( ps, state, sizeof(float)*pf->particleNum*pf->stateSize );

		// get top region
		pr = pf->regions->front(); 

		pr->times->push_front( time );
		pr->states->push_front( ps );
	}

	// TODO see if we're locked, if so check if this prediction is one of the ones we're waiting for and see if we can resample now

	return 0;
}

int DDBStore::ApplyPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity, bool noForwardProp ) {
	unsigned int i;
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticle*>::iterator p;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 1;

	DDBParticleFilter *pf = iterPF->second;

	pf->currentDirty = true;
	
	//Log->log( 0, "DDBStore::ApplyPFCorrection: correction applied to %s", Log->formatUUID( 0, id ) );
	pf->obsSinceLastWeightUpdate++;
	pf->totalObs++;

	// find region
	pr = pf->regions->begin();
	while ( pr != pf->regions->end() ) {
		if ( (*pr)->startTime.time < tb->time 
	    || ( (*pr)->startTime.time == tb->time && (*pr)->startTime.millitm < tb->millitm ) )
			break;
		pr++;
	}

	bool lastRegion = false;
	if ( pr == pf->regions->begin() ) 
		lastRegion = true;

	if ( (*pr)->depreciated )
		return 0; // ignore

	// move forwardMarker if necessary
	if ( (*pr)->index < pf->forwardMarker )
		pf->forwardMarker = (*pr)->index;

	// update observation density and obsDensityForward
	// no longer applicable: if ( (*pf->regions->begin())->index == regionAge ) { // this is a recent correction
		float maxOD = 0, maxODF = 0;
		p = (*pr)->particles->begin();
		for ( i=0; i<pf->particleNum; i++ ) {
			(*p)->obsDensity *= obsDensity[i];
			if ( (*p)->obsDensity > maxOD )
				maxOD = (*p)->obsDensity;
			if ( !lastRegion && !noForwardProp ) { // lastRegion doesn't need obsDensityForward, or if we turned it off
				(*p)->obsDensityForward *= obsDensity[i];
				if ( (*p)->obsDensityForward > maxODF )
					maxODF = (*p)->obsDensityForward;
			}
			p++;
		}
		// normalize so we don't accumulate really large values
		maxOD = 1/maxOD;
		if ( !lastRegion && !noForwardProp )
			maxODF = 1/maxODF;
		p = (*pr)->particles->begin();
		for ( i=0; i<pf->particleNum; i++ ) {
			(*p)->obsDensity *= maxOD;
			//if ( !_finite( (*p)->obsDensity ) || _isnan( (*p)->obsDensity ) )
			//		i=i;
			if ( !lastRegion && !noForwardProp )
				(*p)->obsDensityForward *= maxODF;
			//if ( !_finite( (*p)->obsDensityForward ) || _isnan( (*p)->obsDensityForward ) )
			//		i=i;
			p++;
		}
		(*pr)->weightsDirty = true;
	/* this should never happen with the new resampling strategy
	} else { // this is an old correction so we need to push it through to region regionAge, because regions after that have already incorporated this correction
		int *parentMap = (int *)malloc(sizeof(int)*pf->particleNum*2);
		int *parentNew = parentMap;
		int *parentOld = parentMap + pf->particleNum;
		int *tempPtr;
		bool first = true;
		if ( !parentMap ) {
			return 1;
		}
		while ( (*pr)->index <= regionAge ) {
			p = (*pr)->particles->begin(); 
			for ( i=0; i<pf->particleNum; i++ ) {
				if ( first )
					parentNew[i] = i;	
				else
					parentNew[i] = parentOld[(*(*p)->parent)->index];
				(*p)->obsDensity *= obsDensity[parentNew[i]];
				if ( !_finite( (*p)->obsDensity ) || _isnan( (*p)->obsDensity ) )
					i=i;
				p++;
			}
			first = false;
			tempPtr = parentNew;
			parentNew = parentOld;
			parentOld = tempPtr;
			(*pr)->weightsDirty = true;
			if ( pr == pf->regions->begin() )
				break;
			pr--;
		}
		free( parentMap );
	}	
	*/

	return 0;
}

int DDBStore::_PFPropagateOD( DDBParticleFilter *pf, int toRegion ) {
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticleRegion*>::iterator	prForward;
	std::list<DDBParticle*>::iterator p;

	if ( pf->forwardMarker >= toRegion )
		return 0; // nothing to do

	// find first region with observations
	pr = pf->regions->begin();
	while ( (*pr)->index > pf->forwardMarker && !(*pr)->depreciated )
		pr++;

	if ( (*pr)->depreciated )
		pr--; // back up

	// propagate forward
	bool didforwardprop = false;
	float maxOD, maxODF;
	prForward = pr;
	while ( pf->forwardMarker < toRegion ) {
		maxOD = 0;
		maxODF = 0;
		prForward--;
		p = (*prForward)->particles->begin();
		while ( p != (*prForward)->particles->end() ) {
			if ( (*(*p)->parent)->obsDensityForward != 1 ) {
				didforwardprop = true;
			}
			(*p)->obsDensity *= (*(*p)->parent)->obsDensityForward;
			if ( (*p)->obsDensity > maxOD ) 
				maxOD = (*p)->obsDensity;
			if ( !_finite( (*p)->obsDensity ) || _isnan( (*p)->obsDensity ) )
					p=p;
			(*p)->obsDensityForward *= (*(*p)->parent)->obsDensityForward;
			if ( (*p)->obsDensityForward > maxODF ) 
				maxODF = (*p)->obsDensityForward;
			if ( !_finite( (*p)->obsDensityForward ) || _isnan( (*p)->obsDensityForward ) )
					p=p;
			p++;
		}
		// normalize so we don't accumulate really large values
		maxOD = 1/maxOD;
		maxODF = 1/maxODF;
		p = (*prForward)->particles->begin();
		while ( p != (*prForward)->particles->end() ) {
			(*p)->obsDensity *= maxOD;
			if ( !_finite( (*p)->obsDensity ) || _isnan( (*p)->obsDensity ) )
					p=p;
			(*p)->obsDensityForward *= maxODF;
			if ( !_finite( (*p)->obsDensityForward ) || _isnan( (*p)->obsDensityForward ) )
					p=p;
			p++;
		}
		(*prForward)->weightsDirty = true;
		// clear our parents obsDensityForward
		p = (*pr)->particles->begin();
		while ( p != (*pr)->particles->end() ) {
			(*p)->obsDensityForward = 1;
			p++;
		}
		pf->forwardMarker++;
		pr--;
	}

	if ( didforwardprop ) {
		Log->log( LOG_LEVEL_VERBOSE, "DDBStore::_PFPropagateOD: had values to forward propagate" );
		this->pfForwardPropCount++;
	}

	return 0;
}

int DDBStore::PFGetInfo( UUID *id, int infoFlags, _timeb *tb, DataStream *ds, UUID *thread, float *effectiveParticleNum ) {
	int i, j;
	DDBParticleFilter *pf;
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticleRegion*>::iterator prCurrent;
	std::list<DDBParticle*>::iterator p;
	std::list<float*>::iterator psBefore;
	std::list<float*>::iterator psAfter;
	std::list<_timeb*>::iterator tbBefore;
	std::list<_timeb*>::iterator tbAfter;
	float *stateBefore;
	float *stateAfter;
	float *state = NULL, *pstate;
	float *weight = NULL;
	float *mean = NULL;
	float interp;
	float weightedSumObsDensity;

	*effectiveParticleNum = -1; // calculates the effective particle num as a fraction of total particles, -1 means unset

	ds->reset();
	ds->packUUID( thread );
	
	if ( infoFlags == (DDBPFINFO_USECURRENTTIME | DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE | DDBPFINFO_MEAN) ) { // debug
		int i = 0;
	}

	if ( infoFlags == 0 ) {
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );
		return 0; // no info requested
	}

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() ) {
		ds->packChar( DDBR_NOTFOUND );
		return 1; // not found
	}

	pf = iterPF->second;

	if ( infoFlags == DDBPFINFO_OWNER ) { // only asked for the owner
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );
		ds->packUUID( &pf->owner );
		return 0;
	}
	

	if ( infoFlags & DDBPFINFO_USECURRENTTIME || tb == NULL ) { // get most recent
		pr = pf->regions->begin();
		tb = (*pr)->times->front();
	}

/*	if ( infoFlags & DDBPFINFO_DEBUG ) { // debug copy of the function

		// find region
		if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_WEIGHT | DDBPFINFO_MEAN ) ) {
			pr = pf->regions->begin();
			while ( pr != pf->regions->end() ) {
				if ( (*pr)->startTime.time < tb->time 
				|| ( (*pr)->startTime.time == tb->time && (*pr)->startTime.millitm < tb->millitm ) )
					break;
				pr++;
			}
			if ( pr == pf->regions->end() ) {
				ds->packChar( DDBR_TOOEARLY );
				return 1; // too early
			}
		}

		if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_MEAN) ) {
			// find bounding states
			tbBefore = (*pr)->times->begin();
			psBefore = (*pr)->states->begin();
			tbAfter = tbBefore;
			psAfter = psBefore;
			while ( tbBefore != (*pr)->times->end() ) {
				if ( (*tbBefore)->time < tb->time 
				|| ( (*tbBefore)->time == tb->time && (*tbBefore)->millitm < tb->millitm ) )
					break;
				tbAfter = tbBefore;
				psAfter = psBefore;
				tbBefore++;
				psBefore++;
			}

			if ( tbAfter == tbBefore ) { // we're at the top of the stack!?
				ds->packChar( DDBR_TOOLATE );
				return 1; // too late
			}

			interp = ( (tb->time - (*tbBefore)->time)*1000 + (tb->millitm - (*tbBefore)->millitm) ) / (float)( ((*tbAfter)->time - (*tbBefore)->time)*1000 + ((*tbAfter)->millitm - (*tbBefore)->millitm) );

			// interpolate state
			state = (float *)malloc(sizeof(float)*pf->particleNum*pf->stateSize);
			if ( !state ) {
				ds->packChar( DDBR_INTERNALERROR );
				return 1; // malloc failed
			}

			pstate = state;
			stateBefore = *psBefore;
			stateAfter = *psAfter;
			for ( i=0; i<(int)pf->particleNum; i++ ) {
				for ( j=0; j<(int)pf->stateSize; j++ ) {
					pstate[j] = stateBefore[j] + (stateAfter[j] - stateBefore[j])*interp;
				}
				pstate += pf->stateSize;
				stateBefore += pf->stateSize;
				stateAfter += pf->stateSize;
			}
		}

		if ( infoFlags & DDBPFINFO_CURRENT && pf->currentDirty ) {
			prCurrent = pf->regions->begin();

			// propagate obs density
			this->_PFPropagateOD( pf, (*prCurrent)->index );
			
			// update current
			if ( (*prCurrent)->weightsDirty ) {
				weightedSumObsDensity = 0;
				p = (*prCurrent)->particles->begin();
				while ( p != (*prCurrent)->particles->end() ) {
					weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
					p++;
				}
				p = (*prCurrent)->particles->begin();
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] = 0;
				pstate = (*prCurrent)->states->front();
				while ( p != (*prCurrent)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					for ( i=0; i<(int)pf->stateSize; i++ )
						pf->stateCurrent[i] += (*p)->weight * pstate[i];
					pstate += pf->stateSize;
					p++;
				}
				(*prCurrent)->weightsDirty = false;
			} else {
				p = (*prCurrent)->particles->begin();
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] = 0;
				pstate = (*prCurrent)->states->front();
				while ( p != (*prCurrent)->particles->end() ) {
					for ( i=0; i<(int)pf->stateSize; i++ )
						pf->stateCurrent[i] += (*p)->weight * pstate[i];
					pstate += pf->stateSize;
					p++;
				}
			}
			pf->currentDirty = false;
		}

		if ( infoFlags & (DDBPFINFO_WEIGHT | DDBPFINFO_MEAN) ) {
			// propagate obs density
			this->_PFPropagateOD( pf, (*pr)->index );			  

			// update weights
			weight = (float *)malloc(sizeof(float)*pf->particleNum);
			if ( !weight ) {
				if ( infoFlags & DDBPFINFO_STATE )
					free(state);
				ds->packChar( DDBR_INTERNALERROR );
				return 1; // malloc failed
			}
			if ( (*pr)->weightsDirty ) {
				weightedSumObsDensity = 0;
				p = (*pr)->particles->begin();
				while ( p != (*pr)->particles->end() ) {
					weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
					p++;
				}
				p = (*pr)->particles->begin();
				i = 0;
				while ( p != (*pr)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					weight[i] = (*p)->weight;
					p++;
					i++;
				}
				(*pr)->weightsDirty = false;
			} else {
				p = (*pr)->particles->begin();
				i = 0;
				while ( p != (*pr)->particles->end() ) {
					weight[i] = (*p)->weight;
					p++;
					i++;
				}
			}
		}

		if ( infoFlags & DDBPFINFO_MEAN ) {
			mean = (float *)malloc(sizeof(float)*pf->stateSize);
			if ( !mean ) {
				if ( infoFlags & DDBPFINFO_STATE )
					free(state);
				if ( infoFlags & DDBPFINFO_WEIGHT )
					free(weight);

				ds->packChar( DDBR_INTERNALERROR );
				return 1; // malloc failed
			}

			p = (*pr)->particles->begin();
			for ( i=0; i<(int)pf->stateSize; i++ )
				mean[i] = 0;
			pstate = (*pr)->states->front();
			while ( p != (*pr)->particles->end() ) {
				for ( i=0; i<(int)pf->stateSize; i++ )
					mean[i] += (*p)->weight * pstate[i];
				pstate += pf->stateSize;
				p++;
			}
		}

		// send info
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );
		if ( infoFlags & DDBPFINFO_NUM_PARTICLES ) {
			ds->packInt32( pf->particleNum );
		}
		if ( infoFlags & DDBPFINFO_STATE_SIZE ) {
			ds->packInt32( pf->stateSize );
		}
		if ( infoFlags & DDBPFINFO_STATE ) {
			ds->packData( state, sizeof(float)*pf->particleNum*pf->stateSize );
		}
		if ( infoFlags & DDBPFINFO_WEIGHT ) {
			ds->packData( weight, sizeof(float)*pf->particleNum );
		}
		if ( infoFlags & DDBPFINFO_CURRENT ) {
			ds->packData( &(*prCurrent)->times->front(), sizeof(_timeb) );
			ds->packData( pf->stateCurrent, sizeof(float)*pf->stateSize );
		}
		if ( infoFlags & DDBPFINFO_MEAN ) {
			ds->packData( mean, sizeof(float)*pf->stateSize );
		}
		
		if ( state ) free( state );
		if ( weight ) free( weight );
		if ( mean ) free( mean );

		return 0;
	} */

	// find region
	if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_WEIGHT | DDBPFINFO_MEAN ) ) {
		pr = pf->regions->begin();
		while ( pr != pf->regions->end() && !(*pr)->depreciated ) {
			if ( (*pr)->startTime.time < tb->time 
			|| ( (*pr)->startTime.time == tb->time && (*pr)->startTime.millitm < tb->millitm ) )
				break;
			pr++;
		}
		if ( pr == pf->regions->end() || (*pr)->depreciated ) {
			pr--;
			if ( !((*pr)->startTime.time == tb->time && (*pr)->startTime.millitm == tb->millitm ) ) { // make sure we're not right at the front
				ds->packChar( DDBR_TOOEARLY );
				return 1; // too early
			}
		}
	}

	if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_MEAN) ) {
		// find bounding states
		tbBefore = (*pr)->times->begin();
		psBefore = (*pr)->states->begin();
		tbAfter = tbBefore;
		psAfter = psBefore;
		while ( tbBefore != (*pr)->times->end() ) {
			if ( (*tbBefore)->time < tb->time 
			|| ( (*tbBefore)->time == tb->time && (*tbBefore)->millitm < tb->millitm ) )
				break;
			tbAfter = tbBefore;
			psAfter = psBefore;
			tbBefore++;
			psBefore++;
		}

		if ( tbBefore == (*pr)->times->end() ) { // region must start exactly at tb, implies we're in the earliest region
			psBefore--;
			psAfter = psBefore; // we'll interpolate with the same state
			interp = 0;
		} else if ( tbAfter == tbBefore ) { // we're at the top of the stack!?
			ds->packChar( DDBR_TOOLATE );
			return 1; // too late
		} else {
			interp = ( (tb->time - (*tbBefore)->time)*1000 + (tb->millitm - (*tbBefore)->millitm) ) / (float)( ((*tbAfter)->time - (*tbBefore)->time)*1000 + ((*tbAfter)->millitm - (*tbBefore)->millitm) );
		}

		// interpolate state
		state = (float *)malloc(sizeof(float)*pf->particleNum*pf->stateSize);
		if ( !state ) {
			ds->packChar( DDBR_INTERNALERROR );
			return 1; // malloc failed
		}

		pstate = state;
		stateBefore = *psBefore;
		stateAfter = *psAfter;
		for ( i=0; i<(int)pf->particleNum; i++ ) {
			for ( j=0; j<(int)pf->stateSize; j++ ) {
				pstate[j] = stateBefore[j] + (stateAfter[j] - stateBefore[j])*interp;
			}
			pstate += pf->stateSize;
			stateBefore += pf->stateSize;
			stateAfter += pf->stateSize;
		}
	}

	prCurrent = pf->regions->begin();
	if ( infoFlags & DDBPFINFO_CURRENT && pf->currentDirty ) {

		// propagate obs density
		this->_PFPropagateOD( pf, (*prCurrent)->index );
		
		// update current
		if ( (*prCurrent)->weightsDirty ) {
			weightedSumObsDensity = 0;
			p = (*prCurrent)->particles->begin();
			while ( p != (*prCurrent)->particles->end() ) {
				weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
				p++;
			}
			p = (*prCurrent)->particles->begin();
			for ( i=0; i<(int)pf->stateSize; i++ )
				pf->stateCurrent[i] = 0;
			pstate = (*prCurrent)->states->front();
			*effectiveParticleNum = 0;
			while ( p != (*prCurrent)->particles->end() ) {
				(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
				*effectiveParticleNum += (*p)->weight * (*p)->weight;
				(*p)->obsDensity = 1;
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] += (*p)->weight * pstate[i];
				pstate += pf->stateSize;
				p++;
			}
			*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
			(*prCurrent)->weightsDirty = false;

			Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFGetInfo: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
			pf->obsSinceLastWeightUpdate = 0;
		} else {
			p = (*prCurrent)->particles->begin();
			for ( i=0; i<(int)pf->stateSize; i++ )
				pf->stateCurrent[i] = 0;
			pstate = (*prCurrent)->states->front();
			while ( p != (*prCurrent)->particles->end() ) {
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] += (*p)->weight * pstate[i];
				pstate += pf->stateSize;
				p++;
			}
		}
		pf->currentDirty = false;
	}

	if ( infoFlags & (DDBPFINFO_WEIGHT | DDBPFINFO_MEAN) ) {
		// propagate obs density
		this->_PFPropagateOD( pf, (*pr)->index );			  

		// update weights
		weight = (float *)malloc(sizeof(float)*pf->particleNum);
		if ( !weight ) {
			if ( infoFlags & DDBPFINFO_STATE )
				free(state);
			ds->packChar( DDBR_INTERNALERROR );
			return 1; // malloc failed
		}
		if ( (*pr)->weightsDirty ) {
			weightedSumObsDensity = 0;
			p = (*pr)->particles->begin();
			while ( p != (*pr)->particles->end() ) {
				weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
				p++;
			}
			p = (*pr)->particles->begin();
			i = 0;
			if ( *effectiveParticleNum == -1 && pr == pf->regions->begin() ) { // calculate effectiveParticleNum
				*effectiveParticleNum = 0;
				while ( p != (*pr)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					*effectiveParticleNum += (*p)->weight * (*p)->weight;
					weight[i] = (*p)->weight;
					p++;
					i++;
				}
				*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
			} else {
				while ( p != (*pr)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					weight[i] = (*p)->weight;
					p++;
					i++;
				}
			}
			(*pr)->weightsDirty = false;

			Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFGetInfo: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
			pf->obsSinceLastWeightUpdate = 0;

		} else {
			p = (*pr)->particles->begin();
			i = 0;
			while ( p != (*pr)->particles->end() ) {
				weight[i] = (*p)->weight;
				p++;
				i++;
			}
		}
	}

	if ( infoFlags & DDBPFINFO_MEAN ) {
		mean = (float *)malloc(sizeof(float)*pf->stateSize);
		if ( !mean ) {
			if ( infoFlags & DDBPFINFO_STATE )
				free(state);
			if ( infoFlags & DDBPFINFO_WEIGHT )
				free(weight);

			ds->packChar( DDBR_INTERNALERROR );
			return 1; // malloc failed
		}

		p = (*pr)->particles->begin();
		for ( i=0; i<(int)pf->stateSize; i++ )
			mean[i] = 0;
		pstate = state;
		while ( p != (*pr)->particles->end() ) {
			for ( i=0; i<(int)pf->stateSize; i++ )
				mean[i] += (*p)->weight * pstate[i];
			pstate += pf->stateSize;
			p++;
		}
	}

	if ( infoFlags & DDBPFINFO_EFFECTIVE_NUM_PARTICLES ) {
		if ( *effectiveParticleNum == -1 ) { // not calculated yet
			std::list<DDBParticleRegion*>::iterator prf = pf->regions->begin(); // always use the newest region
			if ( (*prf)->weightsDirty ) {
				weightedSumObsDensity = 0;
				p = (*prf)->particles->begin();
				while ( p != (*prf)->particles->end() ) {
					weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
					p++;
				}

				p = (*prf)->particles->begin();
				i = 0;				
				*effectiveParticleNum = 0;
				while ( p != (*prf)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					*effectiveParticleNum += (*p)->weight * (*p)->weight;
					weight[i] = (*p)->weight;
					p++;
					i++;
				}
				*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
				
				(*prf)->weightsDirty = false;
		
				Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFGetInfo: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
				pf->obsSinceLastWeightUpdate = 0;

			} else {
				p = (*prf)->particles->begin();
				i = 0;				
				*effectiveParticleNum = 0;
				while ( p != (*prf)->particles->end() ) {
					*effectiveParticleNum += (*p)->weight * (*p)->weight;
					p++;
					i++;
				}
				*effectiveParticleNum = 1/((*effectiveParticleNum)*pf->particleNum);
			}
		}
	}

	// send info
	ds->packChar( DDBR_OK );
	ds->packInt32( infoFlags );
	if ( infoFlags & DDBPFINFO_USECURRENTTIME ) {
		ds->packData( tb, sizeof(_timeb) );
	}
	if ( infoFlags & DDBPFINFO_NUM_PARTICLES ) {
		ds->packInt32( pf->particleNum );
	}
	if ( infoFlags & DDBPFINFO_STATE_SIZE ) {
		ds->packInt32( pf->stateSize );
	}
	if ( infoFlags & DDBPFINFO_STATE ) {
		ds->packData( state, sizeof(float)*pf->particleNum*pf->stateSize );
	}
	if ( infoFlags & DDBPFINFO_WEIGHT ) {
		ds->packData( weight, sizeof(float)*pf->particleNum );
	}
	if ( infoFlags & DDBPFINFO_CURRENT ) {
		ds->packData( (*prCurrent)->times->front(), sizeof(_timeb) );
		ds->packData( pf->stateCurrent, sizeof(float)*pf->stateSize );
	}
	if ( infoFlags & DDBPFINFO_MEAN ) {
		ds->packData( mean, sizeof(float)*pf->stateSize );
	}
	if ( infoFlags & DDBPFINFO_OWNER ) {
		ds->packUUID( &pf->owner );
	}
	if ( infoFlags & DDBPFINFO_EFFECTIVE_NUM_PARTICLES ) {
		ds->packFloat32( *effectiveParticleNum );
	}
	
	if ( state ) free( state );
	if ( weight ) free( weight );
	if ( mean ) free( mean );

	return 0;
}

int DDBStore::PFDump( UUID *id, int infoFlags, _timeb *startT, _timeb *endT, char *filename ) {
	FILE *fp;
	int i, j;
	bool endFound;
	DDBParticleFilter *pf;
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticleRegion*>::iterator prEnd;
	std::list<DDBParticleRegion*>::iterator prCurrent;
	std::list<DDBParticle*>::iterator p;
	std::list<float*>::reverse_iterator psA;
	std::list<_timeb*>::reverse_iterator tbA;
	float weightedSumObsDensity;
	float *pstate;
	float mean[1024];

	int done = 0;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() ) {
		return 1; // not found
	}
	
	pf = iterPF->second;

	if ( pf->stateSize > 1024 )
		return 1; // mean needs to be bigger?!

	if ( fopen_s( &fp, filename, "wb" ) ) {
		return 1; 
	}

	// dump infoFlags
	fwrite( &infoFlags, 1, sizeof(int), fp );

	if ( infoFlags & DDBPFINFO_NUM_PARTICLES ) { // dump particleNum
		fwrite( &pf->particleNum, 1, sizeof(int), fp );
	}
	if ( infoFlags & DDBPFINFO_STATE_SIZE ) { // dump stateSize
		fwrite( &pf->stateSize, 1, sizeof(int), fp );
	}

	// find regions
	if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_WEIGHT | DDBPFINFO_MEAN ) ) {
		pr = pf->regions->begin();
		endFound = false;
		while ( pr != pf->regions->end() ) {
			if ( !endFound
			  && ( (*pr)->startTime.time < endT->time
			  || ( (*pr)->startTime.time == endT->time && (*pr)->startTime.millitm < endT->millitm ) ) ) {
				prEnd = pr;
				endFound = true;
			}
			if ( (*pr)->startTime.time < startT->time 
			  || ( (*pr)->startTime.time == startT->time && (*pr)->startTime.millitm < startT->millitm ) )
				break;
			pr++;
		}
		if ( pr == pf->regions->end() ) {
			pr--; // we start at the last region anyway
		}
	}

	// propagate obs density
	if ( infoFlags & (DDBPFINFO_WEIGHT | DDBPFINFO_CURRENT | DDBPFINFO_MEAN) ) {
		if ( infoFlags & DDBPFINFO_CURRENT ) {
			prCurrent = pf->regions->begin();
			this->_PFPropagateOD( pf, (*prCurrent)->index );
		} else {
			this->_PFPropagateOD( pf, (*prEnd)->index );
		}
	}

	if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_WEIGHT | DDBPFINFO_MEAN) ) { // dump state, weight, and/or mean
		while ( !done ) {
			psA = (*pr)->states->rbegin();
			tbA = (*pr)->times->rbegin();

			if ( (*pr)->weightsDirty ) { // update weights
				weightedSumObsDensity = 0;
				p = (*pr)->particles->begin();
				while ( p != (*pr)->particles->end() ) {
					weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
					p++;
				}
				p = (*pr)->particles->begin();
				while ( p != (*pr)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					p++;
				}
				(*pr)->weightsDirty = false;

				Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFDump: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
				pf->obsSinceLastWeightUpdate = 0;
			}

			// dump flag (there is info here)
			fwrite( "\1", 1, 1, fp );

			if ( infoFlags & DDBPFINFO_WEIGHT ) { // dump weight
				
				// dump time
				fwrite( (*tbA), 1, sizeof(_timeb), fp );

				p = (*pr)->particles->begin();
				while ( p != (*pr)->particles->end() ) {
					fwrite( &(*p)->weight, 1, sizeof(float), fp );
					p++;
				}
			}

			if ( infoFlags & (DDBPFINFO_STATE | DDBPFINFO_MEAN) ) {
				while ( psA != (*pr)->states->rend() ) {

					// check if we've started
					if ( (*tbA)->time < startT->time
					  || ( (*tbA)->time == startT->time && (*tbA)->millitm < startT->millitm ) ) {
						psA++;
						tbA++;
						continue;
					}

					// check if we're done
					if ( (*tbA)->time > endT->time
					  || ( (*tbA)->time == endT->time && (*tbA)->millitm > endT->millitm ) ) {
						done = 1;
						break;
					}

					// dump flag (there is info here)
					fwrite( "\1", 1, 1, fp );

					// dump time
					fwrite( (*tbA), 1, sizeof(_timeb), fp );

					if ( infoFlags & DDBPFINFO_STATE ) { // dump state
						fwrite( (*psA), 1, sizeof(float)*pf->stateSize*pf->particleNum, fp );
					}

					if ( infoFlags & DDBPFINFO_MEAN ) { // dump mean
						memset( mean, 0, sizeof(float)*pf->stateSize );
						p = (*pr)->particles->begin();
						for	( i=0; i<(int)pf->particleNum; i++ ) {
							for ( j=0; j<(int)pf->stateSize; j++ ) {
								mean[j] += (*psA)[i*pf->stateSize+j] * (*p)->weight;
							}
							p++;
						}

						fwrite( mean, 1, sizeof(float)*pf->stateSize, fp );
					}

					psA++;
					tbA++;
				}
				// dump flag state/mean is finished
				fwrite( "\0", 1, 1, fp );
			}

			// see if we just finished the last region
			if ( pr == pf->regions->begin() )
				break;

			pr--;
		}
	}
	// dump flag state/weight/mean is finished
	fwrite( "\0", 1, 1, fp );

	if ( infoFlags & DDBPFINFO_CURRENT ) { // dump current
		if ( pf->currentDirty ) {
			if ( (*prCurrent)->weightsDirty ) {
				weightedSumObsDensity = 0;
				p = (*prCurrent)->particles->begin();
				while ( p != (*prCurrent)->particles->end() ) {
					weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
					p++;
				}
				p = (*prCurrent)->particles->begin();
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] = 0;
				pstate = (*prCurrent)->states->front();
				while ( p != (*prCurrent)->particles->end() ) {
					(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
					(*p)->obsDensity = 1;
					for ( i=0; i<(int)pf->stateSize; i++ )
						pf->stateCurrent[i] += (*p)->weight * pstate[i];
					pstate += pf->stateSize;
					p++;
				}
				(*prCurrent)->weightsDirty = false;

				Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFDump: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
				pf->obsSinceLastWeightUpdate = 0;
			} else {
				p = (*prCurrent)->particles->begin();
				for ( i=0; i<(int)pf->stateSize; i++ )
					pf->stateCurrent[i] = 0;
				pstate = (*prCurrent)->states->front();
				while ( p != (*prCurrent)->particles->end() ) {
					for ( i=0; i<(int)pf->stateSize; i++ )
						pf->stateCurrent[i] += (*p)->weight * pstate[i];
					pstate += pf->stateSize;
					p++;
				}
			}
			pf->currentDirty = false;
		}

		fwrite( pf->stateCurrent, 1, sizeof(float)*pf->stateSize, fp );
	}

	fclose( fp );

	return 0;
}

int DDBStore::PFDumpPath( UUID *id, char *filename ) {

	FILE *fp;
	int i, j;
	DDBParticleFilter *pf;
	std::list<DDBParticleRegion*>::iterator pr;
	std::list<DDBParticleRegion*>::iterator prEnd;
	std::list<DDBParticleRegion*>::iterator prCurrent;
	std::list<DDBParticle*>::iterator p;
	std::list<float*>::reverse_iterator psA;
	std::list<_timeb*>::reverse_iterator tbA;
	float weightedSumObsDensity;
	float mean[1024];

	int done = 0;

	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() ) {
		return 1; // not found
	}
	
	pf = iterPF->second;

	if ( pf->stateSize > 1024 )
		return 1; // mean needs to be bigger?!

	if ( fopen_s( &fp, filename, "w" ) ) {
		return 1; 
	}

	// find regions
	pr = pf->regions->end();
	pr--; // last region

	// propagate obs density
	prCurrent = pf->regions->begin();
	this->_PFPropagateOD( pf, (*prCurrent)->index );
	
	while ( !done ) {
		psA = (*pr)->states->rbegin();
		tbA = (*pr)->times->rbegin();

		if ( (*pr)->weightsDirty ) { // update weights
			weightedSumObsDensity = 0;
			p = (*pr)->particles->begin();
			while ( p != (*pr)->particles->end() ) {
				weightedSumObsDensity += (*p)->weight * (*p)->obsDensity;
				p++;
			}
			p = (*pr)->particles->begin();
			while ( p != (*pr)->particles->end() ) {
				(*p)->weight *= (*p)->obsDensity / weightedSumObsDensity;
				(*p)->obsDensity = 1;
				p++;
			}
			(*pr)->weightsDirty = false;

			Log->log( LOG_LEVEL_VERBOSE, "DDBStore::PFDumpPath: obsSinceLastWeightUpdate %d (%s)", pf->obsSinceLastWeightUpdate, Log->formatUUID( LOG_LEVEL_VERBOSE, id ) );
			pf->obsSinceLastWeightUpdate = 0;
		}

		while ( psA != (*pr)->states->rend() ) {

			fprintf_s( fp, "%d.%d", (int)(*tbA)->time, (int)(*tbA)->millitm ); 

			memset( mean, 0, sizeof(float)*pf->stateSize );
			p = (*pr)->particles->begin();
			for	( i=0; i<(int)pf->particleNum; i++ ) {
				for ( j=0; j<(int)pf->stateSize; j++ ) {
					mean[j] += (*psA)[i*pf->stateSize+j] * (*p)->weight;
				}
				p++;
			}

			for ( i=0; i<(int)pf->stateSize; i++ ) {
				fprintf_s( fp, "\t%f", mean[i] );
			}
			fprintf_s( fp, "\n" );

			psA++;
			tbA++;
		}

		// see if we just finished the last region
		if ( pr == pf->regions->begin() )
			break;

		pr--;
	}
	
	fclose( fp );

	return 0;
}

int DDBStore::PFDumpStatistics() { 
	mapDDBParticleFilter::iterator iter;
	
	// pack all the objects
	for ( iter = this->DDBParticleFilters.begin(); iter != this->DDBParticleFilters.end(); iter++ ) {
		Log->log( 0, "DDBStore::PFDumpStatistics: %s obsSinceLastWeightUpdate %d totalObs %d",
			Log->formatUUID( 0, (UUID *)&iter->first ), iter->second->obsSinceLastWeightUpdate, iter->second->totalObs );
	}

	return 0;
}

UUID *DDBStore::PFGetOwner( UUID *id ) {
	DDBParticleFilter *pf;
	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() ) {
		return NULL; // not found
	}
	
	pf = iterPF->second;

	return &pf->owner;
}

int DDBStore::PFGetParticleNum( UUID *id ) {
	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 0;

	DDBParticleFilter *pf = iterPF->second;

	return pf->particleNum;
}

int DDBStore::PFGetRegionCount( UUID *id ) {
	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 0;

	DDBParticleFilter *pf = iterPF->second;

	return pf->regionCount;
}

int DDBStore::PFGetStateArraySize( UUID *id ) {
	mapDDBParticleFilter::iterator iterPF = this->DDBParticleFilters.find(*id);

	if ( iterPF == this->DDBParticleFilters.end() )
		return 0;

	DDBParticleFilter *pf = iterPF->second;
	
	return pf->particleNum*pf->stateSize*sizeof(float);
}

//-----------------------------------------------------------------------------
// DDBAvatar

int DDBStore::EnumerateAvatars( DataStream *ds ) {
	int count;
	mapDDBAvatar::iterator iter;
	
	count = 0;
	
	// pack all the objects
	iter = this->DDBAvatars.begin();
	while ( iter != this->DDBAvatars.end() ) {
		ds->packInt32( DDB_AVATAR );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packData( iter->second, sizeof(DDBAvatar) );

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseAvatar( DataStream *ds, UUID *parsedId ) {
	UUID id;
	mapDDBAvatar::iterator iter;

	DDBAvatar aTemp;
	AgentType agentType;
	DDBAvatar *avatar;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBAvatars.find( id );

	if ( iter == this->DDBAvatars.end() ) { // new object
		// match data format to Enumerate<Type> function
		aTemp = *(DDBAvatar *)ds->unpackData(sizeof(DDBAvatar));
		agentType.uuid = aTemp.agentTypeId;
		agentType.instance = aTemp.agentTypeInstance;
		agentType.name[0] = 0;

		if ( this->AddAvatar( &id, aTemp.type, &agentType, aTemp.status, &aTemp.agent, &aTemp.pf, aTemp.innerRadius, aTemp.outerRadius, &aTemp.startTime, aTemp.capacity, aTemp.sensorTypes ) )
			return 1;
		
		avatar = this->DDBAvatars[id];
		avatar->age = aTemp.age;
		avatar->controller = aTemp.controller;
		avatar->controllerIndex = aTemp.controllerIndex;
		avatar->controllerPriority = aTemp.controllerPriority;
		memcpy( avatar->cargo, aTemp.cargo, sizeof(char)*DDBAVATAR_MAXCARGO );
		avatar->cargoCount = aTemp.cargoCount;
		avatar->retired = aTemp.retired;
		avatar->endTime = aTemp.endTime;

	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}

int DDBStore::AddAvatar( UUID *id, char *type, AgentType *agentType, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes ) {
	int i;

	if ( this->DDBAvatars.find( *id ) != this->DDBAvatars.end() )
		return 1; // already got one!

	// create DDBAvatar
	DDBAvatar *avatar = (DDBAvatar *)malloc(sizeof(DDBAvatar));
	if ( !avatar )
		return 1;

	// initialize
	avatar->age = 0;
	strncpy_s( avatar->type, sizeof(avatar->type), type, sizeof(avatar->type) );
	avatar->status = status;
	avatar->agent = *agent;
	avatar->agentTypeId = agentType->uuid;
	avatar->agentTypeInstance = agentType->instance;
	avatar->pf = *pf;
	avatar->innerRadius = innerRadius;
	avatar->outerRadius = outerRadius;
	avatar->sensorTypes = sensorTypes;

	avatar->controller = nilUUID;
	avatar->controllerIndex = 0;
	avatar->controllerPriority = DDBAVATAR_CP_UNSET;

	avatar->capacity = capacity;
	for ( i = 0; i < DDBAVATAR_MAXCARGO; i++ ) 
		avatar->cargo[i] = -1;
	avatar->cargoCount = 0;

	avatar->startTime = *startTime;
	avatar->retired = 0;

	// insert into DDBAvatars
	this->DDBAvatars[*id] = avatar;

	return 0;
}

int DDBStore::RemoveAvatar( UUID *id ) {
	mapDDBAvatar::iterator iterS = this->DDBAvatars.find(*id);

	if ( iterS == this->DDBAvatars.end() )
		return 1;

	DDBAvatar *avatar = iterS->second;

	this->DDBAvatars.erase( *id );

	// free stuff
	free( avatar );

	return 0;
}

int DDBStore::AvatarGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread ) {
	
	mapDDBAvatar::iterator iterA = this->DDBAvatars.find(*id);

	ds->reset();
	ds->packUUID( thread );

	if ( infoFlags == DDBAVATARINFO_ENUM ) {
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );
		ds->packInt32( (int)this->DDBAvatars.size() );
		for ( iterA = this->DDBAvatars.begin(); iterA != this->DDBAvatars.end(); iterA++ ) {
			ds->packUUID( (UUID *)&iterA->first );
			ds->packString( iterA->second->type );
			ds->packUUID( &iterA->second->agent );
			ds->packUUID( &iterA->second->agentTypeId );
			ds->packInt32( iterA->second->agentTypeInstance );
		}
		return 0;
	}

	if ( iterA == this->DDBAvatars.end() ) { // unknown avatar
		ds->packChar( DDBR_NOTFOUND );
	} else { 
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );

		if ( infoFlags & DDBAVATARINFO_RTYPE ) {
			ds->packString( iterA->second->type );
		}

		if ( infoFlags & DDBAVATARINFO_RSTATUS ) {
			ds->packInt32( iterA->second->status );
		}
		
		if ( infoFlags & DDBAVATARINFO_RAGENT ) {
			ds->packUUID( &iterA->second->agent );
		}

		if ( infoFlags & DDBAVATARINFO_RPF ) {
			ds->packUUID( &iterA->second->pf );
		}

		if ( infoFlags & DDBAVATARINFO_RRADII ) {
			ds->packFloat32( iterA->second->innerRadius );
			ds->packFloat32( iterA->second->outerRadius );
		}

		if ( infoFlags & DDBAVATARINFO_RCONTROLLER ) {
			ds->packUUID( &iterA->second->controller );
			ds->packInt32( iterA->second->controllerIndex );
			ds->packInt32( iterA->second->controllerPriority );
		}

		if ( infoFlags & DDBAVATARINFO_RSENSORTYPES ) {
			ds->packInt32( iterA->second->sensorTypes );
		}

		if ( infoFlags & DDBAVATARINFO_RCAPACITY ) {
			ds->packInt32( iterA->second->capacity );
		}

		if ( infoFlags & DDBAVATARINFO_RCARGO ) {
			int i;
			ds->packInt32( iterA->second->cargoCount );
			for ( i = 0; i < DDBAVATAR_MAXCARGO; i++ ) 
				if ( iterA->second->cargo[i] != -1 ) ds->packChar( iterA->second->cargo[i] );
		}
		
		if ( infoFlags & DDBAVATARINFO_RTIMECARD ) {
			ds->packData( &iterA->second->startTime, sizeof(_timeb) );
			ds->packChar( iterA->second->retired );
			if ( iterA->second->retired )
				ds->packData( &iterA->second->endTime, sizeof(_timeb) );
		}
	}

	return 0;
}

int DDBStore::AvatarSetInfo( UUID *id, int infoFlags, DataStream *ds ) {
	int i, c;
	mapDDBAvatar::iterator iterA = this->DDBAvatars.find(*id);

	if ( iterA == this->DDBAvatars.end() )
		return 0; // not found

	if ( infoFlags & DDBAVATARINFO_STATUS ) {
		iterA->second->status = ds->unpackInt32();
	}

	if ( infoFlags & DDBAVATARINFO_CONTROLLER ) {
		ds->unpackUUID( &iterA->second->controller );
		iterA->second->controllerIndex++;
		iterA->second->controllerPriority = ds->unpackInt32();
	}

	if ( infoFlags & DDBAVATARINFO_CONTROLLER_RELEASE ) {
		UUID agent;
		ds->unpackUUID( &agent );
		if ( agent == iterA->second->controller ) {
			iterA->second->controller = nilUUID;
			iterA->second->controllerIndex++;
			iterA->second->controllerPriority = DDBAVATAR_CP_UNSET;
			infoFlags |= DDBAVATARINFO_CONTROLLER; // controller changed
		}
	}

	if ( infoFlags & DDBAVATARINFO_CARGO ) {
		bool load;
		unsigned char code;
		int count = ds->unpackInt32();
		for ( c = 0; c < count; c++ ) {
			load = ds->unpackBool();
			code = ds->unpackUChar();
			if ( load ) { // loading
				for ( i = 0; i < DDBAVATAR_MAXCARGO; i++ )
					if ( iterA->second->cargo[i] == -1 ) break;

				if ( i < DDBAVATAR_MAXCARGO ) {
					iterA->second->cargo[i] = code;
					iterA->second->cargoCount++;
				}
			} else { // unloading
				for ( i = 0; i < DDBAVATAR_MAXCARGO; i++ ) {
					if ( iterA->second->cargo[i] == code ) {
						iterA->second->cargo[i] = -1;
						iterA->second->cargoCount--;
						break;
					}
				}
			}
		}
	}

	if ( infoFlags & DDBAVATARINFO_RETIRE ) {
		iterA->second->retired = ds->unpackChar();
		iterA->second->endTime = *(_timeb *)ds->unpackData(sizeof(_timeb));
	}

	return infoFlags;
}


//-----------------------------------------------------------------------------
// DDBSensor

int DDBStore::EnumerateSensors( DataStream *ds ) {
	int count;
	mapDDBSensor::iterator iter;
	std::list<DDBSensorData *>::iterator iterD;

	count = 0;
	
	// pack all the objects
	iter = this->DDBSensors.begin();
	while ( iter != this->DDBSensors.end() ) {
		ds->packInt32( iter->second->type );
		ds->packUUID( (UUID *)&iter->first );

		// TEMP enumerate full data
		ds->packInt32( iter->second->age );
		ds->packInt32( iter->second->type );
		ds->packUUID( &iter->second->avatar );
		ds->packUUID( &iter->second->pf );
		ds->packInt32( iter->second->poseSize );
		ds->packData( iter->second->pose, iter->second->poseSize );

		// enumerate data
		iterD = iter->second->data->begin();
		while ( iterD != iter->second->data->end() ) {
			ds->packBool( 1 );
			
			ds->packData( &(*iterD)->t, sizeof(_timeb) );
			ds->packInt32( (*iterD)->readingSize );
			if ( (*iterD)->readingSize ) 
				ds->packData( (*iterD)->reading, (*iterD)->readingSize );
			ds->packInt32( (*iterD)->dataSize );
			if ( (*iterD)->dataSize ) 
				ds->packData( (*iterD)->data, (*iterD)->dataSize );

			iterD++;
		}
		ds->packBool( 0 ); // done data

		count++;
		iter++;
	}

	return count;
}

int DDBStore::ParseSensor( DataStream *ds, UUID *parsedId ) {
	UUID id;
	mapDDBSensor::iterator iter;

	int age;
	int type;
	UUID avatar;
	UUID pf;
	void *pose;
	int poseSize;
	DDBSensor *sensor;
	DDBSensorData *sd;

	ds->unpackUUID( &id );
	*parsedId = id;

	iter = this->DDBSensors.find( id );

	if ( iter == this->DDBSensors.end() ) { // new object
		// match data format to Enumerate<Type> function
		age = ds->unpackInt32();
		type = ds->unpackInt32();
		ds->unpackUUID( &avatar );
		ds->unpackUUID( &pf );
		poseSize = ds->unpackInt32();
		pose = ds->unpackData( poseSize );

		if ( this->AddSensor( &id, type, &avatar, &pf, pose, poseSize ) )
			return 1;
		
		sensor = this->DDBSensors[id];
		sensor->age = age;

		// parse data
		while ( ds->unpackBool() ) {
			sd = new DDBSensorData;

			sd->t = *(_timeb *)ds->unpackData( sizeof(_timeb) );
			sd->readingSize = ds->unpackInt32();
			if ( sd->readingSize ) {
				sd->reading = malloc( sd->readingSize );
				if ( !sd->reading ) {
					delete sd;
					return 1;
				}
				memcpy( sd->reading, ds->unpackData( sd->readingSize ), sd->readingSize );
			}

			sd->dataSize = ds->unpackInt32();
			if ( sd->dataSize ) {
				sd->data = malloc( sd->dataSize );
				if ( !sd->data ) {
					free( sd->reading );
					delete sd;
					return 1;
				}
				memcpy( sd->data, ds->unpackData( sd->dataSize ), sd->dataSize );
			}

			sensor->data->push_back( sd );
		}

	} else { // we know this object already, see if we need to update

		return -1; // object known
	}

	return 0;
}

int DDBStore::AddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize ) {
	
	// create DDBSensor
	DDBSensor *sensor = (DDBSensor *)malloc(sizeof(DDBSensor));
	if ( !sensor )
		return 1;

	// initialize
	sensor->age = 0;
	sensor->type = type;
	sensor->avatar = *avatar;
	sensor->pf = *pf;
	sensor->pose = malloc( poseSize );
	if ( !sensor->pose ) {
		free( sensor );	
		return 1;
	}
	memcpy( sensor->pose, pose, poseSize );
	sensor->poseSize = poseSize;
	sensor->data = new list<DDBSensorData *>;

	// insert into DDBSensors
	this->DDBSensors[*id] = sensor;

	return 0;
}

int DDBStore::RemoveSensor( UUID *id ) {
	mapDDBSensor::iterator iterS = this->DDBSensors.find(*id);

	if ( iterS == this->DDBSensors.end() )
		return 1;

	DDBSensor *sensor = iterS->second;

	this->DDBSensors.erase( *id );

	// free stuff
	free( sensor->pose );

	list<DDBSensorData *>::iterator iter = sensor->data->begin();
	while ( iter != sensor->data->end() ) {
		if ( (*iter)->readingSize )
			free( (*iter)->reading );
		if ( (*iter)->dataSize )
			free( (*iter)->data );
		free( *iter );
		iter++;
	}

	delete sensor->data;

	free( sensor );

	return 0;
}

int DDBStore::InsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data, int dataSize ) {
	DDBSensorData *sData;

	mapDDBSensor::iterator iterS = this->DDBSensors.find(*id);

	if ( iterS == this->DDBSensors.end() )
		return 1;

	DDBSensor *sensor = iterS->second;

	sData = (DDBSensorData *)malloc(sizeof(DDBSensorData));
	if ( !sData )
		return 1; 

	sData->readingSize = readingSize;
	sData->reading = NULL;
	sData->dataSize = dataSize;
	sData->data = NULL;

	sData->t = *tb;

	sData->reading = malloc(readingSize);
	if ( !sData->reading ) {
		free( sData );
		return 1;
	}
	memcpy( sData->reading, reading, readingSize );
	
	if ( dataSize ) {
		sData->data = malloc(dataSize);
		if ( !sData->data ) {
			free( sData->reading );
			free( sData );
			return 1;
		}
		memcpy( sData->data, data, dataSize );
	}

	sensor->data->push_back( sData );

	return 0;
}

int DDBStore::SensorGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread ) {
	
	mapDDBSensor::iterator iterS = this->DDBSensors.find(*id);

	ds->reset();
	ds->packUUID( thread );

	if ( iterS == this->DDBSensors.end() ) { // unknown sensor
		ds->packChar( DDBR_NOTFOUND );
	} else { 
		ds->packChar( DDBR_OK );
		ds->packInt32( infoFlags );
		
		if ( infoFlags & DDBSENSORINFO_TYPE ) {
			ds->packInt32( iterS->second->type );
		}

		if ( infoFlags & DDBSENSORINFO_AVATAR ) {
			ds->packUUID( &iterS->second->avatar );
		}
		
		if ( infoFlags & DDBSENSORINFO_PF ) {
			ds->packUUID( &iterS->second->pf );
		}

		if ( infoFlags & DDBSENSORINFO_POSE ) {
			ds->packInt32( iterS->second->poseSize );
			ds->packData( iterS->second->pose, iterS->second->poseSize );
		}

		if ( infoFlags & DDBSENSORINFO_READINGCOUNT ) {
			ds->packInt32( (int)iterS->second->data->size() );
		}

	}

	return 0;
}

int DDBStore::SensorGetData( UUID *id, _timeb *tb, DataStream *ds, UUID *thread ) {
	std::list<DDBSensorData *>::iterator iterD;
	mapDDBSensor::iterator iterS = this->DDBSensors.find(*id);

	ds->reset();
	ds->packUUID( thread );

	if ( iterS == this->DDBSensors.end() ) { // unknown sensor
		ds->packChar( DDBR_NOTFOUND );
	} else { 

		// find the reading
		iterD = iterS->second->data->begin();
		while ( iterD != iterS->second->data->end() ) {
			if ( (*iterD)->t.time > tb->time
			  || ( (*iterD)->t.time == tb->time && (*iterD)->t.millitm >= tb->millitm ) ) 
				break;
			iterD++;
		}

		if ( iterD == iterS->second->data->end() ) { // too late
			ds->packChar( DDBR_TOOLATE );
		} else if ( (*iterD)->t.time == tb->time && (*iterD)->t.millitm >= tb->millitm ) { // found it
			ds->packChar( DDBR_OK );
			ds->packInt32( (*iterD)->readingSize );
			ds->packData( (*iterD)->reading, (*iterD)->readingSize );
			ds->packInt32( (*iterD)->dataSize );
			if ( (*iterD)->dataSize )
				ds->packData( (*iterD)->data, (*iterD)->dataSize );
		} else if ( iterD == iterS->second->data->begin() ) { // too early
			ds->packChar( DDBR_TOOEARLY );
		} else { // bad range
			ds->packChar( DDBR_BADRANGE );
		}
	}

	return 0;
}

int DDBStore::GetSensorType( UUID *id ) {
	mapDDBSensor::iterator iterS = this->DDBSensors.find(*id);

	if ( iterS == this->DDBSensors.end() )
		return 0; // 0 is not a valid sensor type (I hope)

	return iterS->second->type;
}


int DDBStore::DataDump( Logger *Data, bool fulldump, char *logDirectory ) {

	// -- database size -- 
	size_t grandTotal, totalSize, fullSize; // note: sizes are only approximate

	grandTotal = 0;
	
	// Agents
	std::list<DDBAgent_MSG>::iterator iAMSG;
	std::map<int,std::list<DDBVis_NODE> *>::iterator iVisPaths;
	std::map<int,DDBVis_OBJECT *>::iterator iVisObj;
	mapDDBAgent::iterator iAgent;
	totalSize = 0;
	for ( iAgent = this->DDBAgents.begin(); iAgent != this->DDBAgents.end(); iAgent++ ) {
		totalSize += sizeof(DDBAgent) + iAgent->second->affinityData.size()*sizeof(unsigned int) + iAgent->second->stateSize + iAgent->second->backupSize;
		for ( iAMSG = iAgent->second->msgQueuePrimary.begin(); iAMSG != iAgent->second->msgQueuePrimary.end(); iAMSG++ ) {
			totalSize += sizeof(DDBAgent_MSG) + iAMSG->len;
		}
		for ( iAMSG = iAgent->second->msgQueueSecondary.begin(); iAMSG != iAgent->second->msgQueueSecondary.end(); iAMSG++ ) {
			totalSize += sizeof(DDBAgent_MSG) + iAMSG->len;
		}
		for ( iVisPaths = iAgent->second->visPaths->begin(); iVisPaths != iAgent->second->visPaths->end(); iVisPaths++ ) {
			totalSize += sizeof(DDBVis_NODE)*iVisPaths->second->size();
		}
		for ( iVisObj = iAgent->second->visObjects->begin(); iVisObj != iAgent->second->visObjects->end(); iVisObj++ ) {
			totalSize += sizeof(DDBVis_OBJECT) + sizeof(DDBVis_PATH_REFERENCE)*iVisObj->second->path_refs.size();
		}
	}
	grandTotal += totalSize;
	Data->log( 0, "DDB_AGENTS count %d totalsize %d", this->DDBAgents.size(), totalSize );

	// Regions
	totalSize = this->DDBRegions.size()*sizeof(DDBRegion);
	grandTotal += totalSize;
	Data->log( 0, "DDB_REGIONS count %d totalsize %d", this->DDBRegions.size(), totalSize );

	// Landmarks
	totalSize = this->DDBLandmarks.size()*sizeof(DDBLandmark);
	grandTotal += totalSize;
	Data->log( 0, "DDB_LANDMARKS count %d totalsize %d", this->DDBLandmarks.size(), totalSize );

	// POGs
	mapDDBPOG::iterator iPOG;
	totalSize = 0;
	for ( iPOG = this->DDBPOGs.begin(); iPOG != this->DDBPOGs.end(); iPOG++ ) {
		totalSize += sizeof(DDBProbabilisticOccupancyGrid) + iPOG->second->tiles->size()*iPOG->second->stride*iPOG->second->stride*sizeof(float);
	}
	grandTotal += totalSize;
	Data->log( 0, "DDB_POGS count %d totalsize %d", this->DDBPOGs.size(), totalSize );

	// particle filters
	std::list<DDBParticleRegion*>::iterator iPFRegion;
	mapDDBParticleFilter::iterator iPF;
	totalSize = 0;
	fullSize = 0;
	for ( iPF = this->DDBParticleFilters.begin(); iPF != this->DDBParticleFilters.end(); iPF++ ) {
		totalSize += sizeof(DDBParticleFilter) + iPF->second->stateSize;
		fullSize += sizeof(DDBParticleFilter) + iPF->second->stateSize;
		for ( iPFRegion = iPF->second->regions->begin(); iPFRegion != iPF->second->regions->end(); iPFRegion++ ) {
			if ( !(*iPFRegion)->depreciated )
				totalSize += sizeof(DDBParticleRegion) + (*iPFRegion)->particles->size()*sizeof(DDBParticle) + (*iPFRegion)->times->size()*sizeof(_timeb) + (*iPFRegion)->states->size()*iPF->second->particleNum*iPF->second->stateSize*sizeof(float);
			fullSize += sizeof(DDBParticleRegion) + (*iPFRegion)->particles->size()*sizeof(DDBParticle) + (*iPFRegion)->times->size()*sizeof(_timeb) + (*iPFRegion)->states->size()*iPF->second->particleNum*iPF->second->stateSize*sizeof(float);
		}
	}
	grandTotal += totalSize;
	Data->log( 0, "DDB_PARTICLE_FILTERS count %d totalsize %d fullsize %d", this->DDBParticleFilters.size(), totalSize, fullSize );

	// avatars
	totalSize = this->DDBAvatars.size()*sizeof(DDBAvatar);
	grandTotal += totalSize;
	Data->log( 0, "DDB_AVATARS count %d totalsize %d", this->DDBAvatars.size(), totalSize );

	// sensors
	std::list<DDBSensorData *>::iterator iSensorData;
	mapDDBSensor::iterator iSensor;
	totalSize = 0;
	for ( iSensor = this->DDBSensors.begin(); iSensor != this->DDBSensors.end(); iSensor++ ) {
		totalSize += sizeof(DDBSensor) + iSensor->second->poseSize;
		for ( iSensorData = iSensor->second->data->begin(); iSensorData != iSensor->second->data->end(); iSensorData++ ) {
			totalSize += sizeof(DDBSensorData) + (*iSensorData)->readingSize + (*iSensorData)->dataSize;
		}
	}
	grandTotal += totalSize;
	Data->log( 0, "DDB_SENSORS count %d totalsize %d", this->DDBSensors.size(), totalSize );

	// grand total
	Data->log( 0, "DDB_TOTAL_SIZE %d", grandTotal );

	// -- landmark localization accuracy --
	float errX, errY, errD, errAvg;
	int errCount;
	mapDDBLandmark::iterator iLandmark;
	errAvg = 0;
	errCount = 0;
	for ( iLandmark = this->DDBLandmarks.begin(); iLandmark != this->DDBLandmarks.end(); iLandmark++ ) {
		if ( iLandmark->second->estimatedPos && iLandmark->second->posInitialized ) {
			errX = iLandmark->second->x - iLandmark->second->trueX;
			errY = iLandmark->second->y - iLandmark->second->trueY;
			errD = sqrt( errX*errX + errY*errY );
			errAvg += errD;
			errCount++;
			Data->log( 0, "LANDMARK_POSITION %s x %f y %f P %f trueX %f trueY %f errX %f errY %f errD %f", 
				Data->formatUUID(0,(UUID *)&iLandmark->first),
				iLandmark->second->x, iLandmark->second->y, iLandmark->second->P, 
				iLandmark->second->trueX, iLandmark->second->trueY, errX, errY, errD );	
		}
	}
	if ( errCount ) {
		Data->log( 0, "LANDMARK_ESTIMATION count %d errAvg %f", errCount, errAvg/errCount );
	}

	// -- maps --
	{
		char fname[256];
		char formatBuf[64];
		char timeBuf[64];
		time_t t_t;
		_timeb tb;
		struct tm stm;
		apb->apbtime( &t_t );
		apb->apb_ftime_s( &tb );
		localtime_s( &stm, &t_t );
		strftime( formatBuf, 64, "%H.%M.%S", &stm );
		sprintf_s( timeBuf, 64, "[%s.%3d]", formatBuf, tb.millitm );

		for ( iPOG = this->DDBPOGs.begin(); iPOG != this->DDBPOGs.end(); iPOG++ ) {
			sprintf_s( fname, 256, "%s\\dump\\POGDUMP %s %s.txt", logDirectory, Data->formatUUID(0,(UUID *)&iPOG->first), timeBuf );
			this->POGDumpRegion( (UUID *)&iPOG->first, iPOG->second->xlow, iPOG->second->ylow, iPOG->second->xhigh - iPOG->second->xlow, iPOG->second->yhigh - iPOG->second->ylow, fname );
			Data->log( 0, "POG_DUMP %s x %f y %f w %f h %f %s", Data->formatUUID(0,(UUID *)&iPOG->first), 
				iPOG->second->xlow, iPOG->second->ylow, iPOG->second->xhigh - iPOG->second->xlow, iPOG->second->yhigh - iPOG->second->ylow, fname );
		}
	}
	
	// -- particle filters --
	if ( fulldump ) {
		char fname[256];
		char formatBuf[64];
		char timeBuf[64];
		time_t t_t;
		_timeb tb;
		struct tm stm;
		apb->apbtime( &t_t );
		apb->apb_ftime_s( &tb );
		localtime_s( &stm, &t_t );
		strftime( formatBuf, 64, "%H.%M.%S", &stm );
		sprintf_s( timeBuf, 64, "[%s.%3d]", formatBuf, tb.millitm );

		for ( iPF = this->DDBParticleFilters.begin(); iPF != this->DDBParticleFilters.end(); iPF++ ) {
			sprintf_s( fname, 256, "%s\\dump\\PFDUMP %s %s.txt", logDirectory, Data->formatUUID(0,(UUID *)&iPF->first), timeBuf );
			this->PFDumpPath( (UUID *)&iPF->first, fname );
			Data->log( 0, "PARTICLE_FILTER_DUMP %s %s", Data->formatUUID(0,(UUID *)&iPF->first), fname );
		}
	}

	return 0;
}



//-------------------------------------------------------------------------
//DDBTasks	Task storage for team and individual learning agents
int DDBStore::AddTask(UUID *id, UUID *landmark, UUID *agent, UUID *avatar, bool completed, ITEM_TYPES type) {

	// check to see if it already exists
	if (this->DDBTasks.find(*id) != this->DDBTasks.end())
		return 1; // already exists

				  // create DDBTask
	DDBTask *task = (DDBTask *)malloc(sizeof(DDBTask));
	if (!task)
		return 1;

	// initialize

	task->landmarkUUID = *landmark;
	task->agentUUID = *agent;
	task->avatar = *avatar;
	task->type = type;
	task->completed = completed;


	//_timeb insertionTime;

	// insert into DDBTasks
	this->DDBTasks[*id].first = task;
	
	_ftime64_s(&this->DDBTasks[*id].second);	//Log time of insertion
	//this->DDBTasks[*id] = task;


	return 0;
}

int DDBStore::RemoveTask(UUID *id) {
	mapDDBTask::iterator iter = this->DDBTasks.find(*id);

	if (iter == this->DDBTasks.end())
		return 1;

	DDBTask *task = iter->second.first;
	//DDBTask *task = iter->second;

	this->DDBTasks.erase(*id);

	// free stuff
	free(&task);

	return 0;
}

int DDBStore::TaskSetInfo(UUID *id, UUID *agent, UUID *avatar,  bool completed) {
	mapDDBTask::iterator iter = this->DDBTasks.find(*id);

	if (*id != nilUUID) {	//Nil upload from negotiateTasks()
		if (iter == this->DDBTasks.end()) {
			return 1; // not found
		}


	DDBTask *task = iter->second.first;
	//DDBTask *task = iter->second;
	_timeb lastAvatarChange = iter->second.second;

	//if (task->completed != completed) {	//Completion status has changed, treat as normal
		task->agentUUID = *agent;
		task->avatar = *avatar;
		task->completed = task->completed || completed;	//If task is already completed, or is set to completed, it is updated to be completed (prevents rare case of already completed task to be marked as incomplete)
		_ftime64_s(&iter->second.second);	//Set the update time as now
	//}
	//else {	//Only avatar and/or agent changed - make sure that task ownership is not rapidly changing by enforcing a wait period 
	//		//This enables any OAC messages to "catch up"
	//	_timeb timeNow;
	//	_ftime64_s(&timeNow);
	//	if (timeNow.millitm - lastAvatarChange.millitm < TASK_AVATAR_CHANGE_THRESHOLD) {
	//		return 2;
	//	}
	//	else {	//Proceed as usual, enough time has passed since the last avatar change

	//		task->agentUUID = *agent;
	//		task->avatar = *avatar;
	//		task->completed = completed;
	//	}	
	//}
	}

	return 0;
}

int DDBStore::GetTask(UUID *id, DataStream *ds, UUID *thread, bool enumTasks) {
	mapDDBTask::iterator iter = this->DDBTasks.find(*id);

	ds->reset();
	ds->packUUID(thread);


	if (enumTasks == true) {
		ds->packChar(DDBR_OK);
		ds->packBool(enumTasks);
		ds->packInt32((int)this->DDBTasks.size());
		for (iter = this->DDBTasks.begin(); iter != this->DDBTasks.end(); iter++) {
			ds->packUUID((UUID *)&iter->first);
			ds->packData(iter->second.first, sizeof(DDBTask));
			//ds->packUUID((UUID *)&iter->first);
			//ds->packData(iter->second, sizeof(DDBTask));
		}
		return 0;
	}


	if (iter == this->DDBTasks.end()) {
		ds->packChar(DDBR_NOTFOUND);
		return 1; // not found
	}

	//DDBTask *task = iter->second;
	DDBTask *task = iter->second.first;


	// send task
	ds->packChar(DDBR_OK);
	ds->packBool(enumTasks);
	ds->packUUID(id);
	ds->packData(task, sizeof(DDBTask));

	return 0;

}

//-------------------------------------------------------------------------
//DDBTaskData	Task performance metrics storage for team learning agents
int DDBStore::AddTaskData(UUID *id, DDBTaskData *data) {

	// check to see if it already exists
	if (this->DDBTaskDatas.find(*id) != this->DDBTaskDatas.end())
		return 1; // already exists

	DataStream lds;

	lds.reset();
	lds.packTaskData(data);		//Try this "hackaround" for now, marshals taskData for storage
	lds.unlock();

	char *storage = (char *)malloc(lds.length());
	memcpy(storage, lds.stream(), lds.length());
	
	//*storage = *lds.stream();


	//*sds = lds;

	//// create DDBTaskData
	//DDBTaskData *taskData = (DDBTaskData *)malloc(sizeof(*data));
	//if (!taskData)
	//	return 1;
	//// initialize

	//memcpy(taskData, data, sizeof(data));

	storagePair sP;
	sP.first = storage;
	sP.second = lds.length();
	// insert into DDBTasks
	this->DDBTaskDatas[*id] = sP;

	return 0;
}

int DDBStore::RemoveTaskData(UUID *id) {
	mapDDBTaskData::iterator iter = this->DDBTaskDatas.find(*id);

	if (iter == this->DDBTaskDatas.end())
		return 1;

	storagePair storage = iter->second;

	this->DDBTaskDatas.erase(*id);

	// free stuff
	free(&storage);

	return 0;
}

int DDBStore::TaskDataSetInfo(UUID *id, DDBTaskData *data) {
	mapDDBTaskData::iterator iter = this->DDBTaskDatas.find(*id);

	if (iter == this->DDBTaskDatas.end()) {
		return 1; // not found
	}

	DataStream lds;

	lds.reset();
	lds.packTaskData(data);		//Try this "hackaround" for now, marshals taskData for storage
	lds.unlock();

	char *storage = (char *)malloc(lds.length());
	memcpy(storage, lds.stream(), lds.length());


	//*sds = lds;
	storagePair sP;
	sP.first = storage;
	sP.second = lds.length();
	// insert into DDBTasks
	this->DDBTaskDatas[*id] = sP;


	//*iter->second = *data;
	/*DDBTaskData *taskData = iter->second;

	taskData = data;*/

	return 0;
}

int DDBStore::GetTaskData(UUID *id, DataStream *ds, UUID *thread, bool enumTaskData) {
	mapDDBTaskData::iterator iter = this->DDBTaskDatas.find(*id);

	DDBTaskData taskData;
	DataStream lds;
//	Log->log(0," DDBStore::GetTaskData 1");



	ds->reset();
	ds->packUUID(thread);
//	Log->log(0, " DDBStore::GetTaskData:: enumTaskData is %d", enumTaskData);
	if (enumTaskData == true) {
//		Log->log(0, " DDBStore::GetTaskData:: packing char %d", DDBR_OK);
		ds->packChar(DDBR_OK);
//		Log->log(0, " DDBStore::GetTaskData:: enumTaskData is %d", enumTaskData);
		ds->packBool(enumTaskData);
//		Log->log(0, " DDBStore::GetTaskData:: DDBTaskDatas.size() is %d", (int)this->DDBTaskDatas.size());
		ds->packInt32((int)this->DDBTaskDatas.size());
//		Log->log(0, " DDBStore::GetTaskData:: DDBTaskDatas.empty() is %d", DDBTaskDatas.empty());
		if (DDBTaskDatas.empty() == false) {
			for (iter = this->DDBTaskDatas.begin(); iter != this->DDBTaskDatas.end(); iter++) {
				ds->packUUID((UUID *)&iter->first);
				lds.setData(iter->second.first, iter->second.second);
				lds.unpackTaskData(&taskData);
				lds.unlock();
				ds->packTaskData(&taskData);
			}
		}
		return 0;
	}

	if (iter == this->DDBTaskDatas.end()) {
		ds->packChar(DDBR_NOTFOUND);
		return 1; // not found
	}
	lds.setData(iter->second.first, iter->second.second);
	lds.unpackTaskData(&taskData);
	lds.unlock();

	// send task data
	ds->packChar(DDBR_OK);
	ds->packBool(enumTaskData);
	ds->packUUID(id);
	ds->packTaskData(&taskData);


	//ds->reset();
	//ds->packUUID(thread);

	//if (enumTaskData == true) {
	//	ds->packChar(DDBR_OK);
	//	ds->packBool(enumTaskData);
	//	ds->packInt32((int)this->DDBTaskDatas.size());

	//	if (DDBTaskDatas.empty() == false) {
	//		for (iter = this->DDBTaskDatas.begin(); iter != this->DDBTaskDatas.end(); iter++) {
	//			ds->packUUID((UUID *)&iter->first);
	//			ds->packTaskData(iter->second);
	//		}
	//	}
	//	return 0;
	//}

	//if (iter == this->DDBTaskDatas.end()) {
	//	ds->packChar(DDBR_NOTFOUND);
	//	return 1; // not found
	//}

	//DDBTaskData *taskData = iter->second;

	//// send task data
	//ds->packChar(DDBR_OK);
	//ds->packBool(enumTaskData);
	//ds->packUUID(id);
	//ds->packTaskData(taskData);

	return 0;
}


int DDBStore::SetTLRoundInfo(RoundInfoStruct *newRoundInfo) {


	this->DDBTLRoundInfo.roundNumber = newRoundInfo->roundNumber;
	this->DDBTLRoundInfo.newRoundNumber = newRoundInfo->newRoundNumber;
	this->DDBTLRoundInfo.startTime = newRoundInfo->startTime;
	this->DDBTLRoundInfo.TLAgents = newRoundInfo->TLAgents;

	return 0;
}


int DDBStore::GetTLRoundInfo(DataStream *ds, UUID *thread) {

	ds->reset();
	ds->packUUID(thread);
	
	// send task data
	ds->packChar(DDBR_OK);
	ds->packInt32(this->DDBTLRoundInfo.roundNumber);
	ds->packInt32(this->DDBTLRoundInfo.newRoundNumber);
	ds->packData(&this->DDBTLRoundInfo.startTime, sizeof(_timeb));
	ds->packInt32(this->DDBTLRoundInfo.TLAgents.size());
	for (auto tlAIter : this->DDBTLRoundInfo.TLAgents) {
		ds->packUUID(&tlAIter);
	}

	return 0;
}

//Individual Q-learning value storage, for storing data between simulation runs

bool DDBStore::AddQLearningData(bool onlyActions, char instance, long long totalActions, long long usefulActions, int tableSize, std::vector<float> qTable, std::vector<unsigned int> expTable)
{

	if (onlyActions) {
		if (this->DDBQLearningDatas.find(instance) != this->DDBQLearningDatas.end()) { //Already exists
			this->DDBQLearningDatas[instance].totalActions = totalActions;
			this->DDBQLearningDatas[instance].usefulActions = usefulActions;
		}
		else {	//Create new
			QLStorage newQLStorage;
			newQLStorage.totalActions = totalActions;
			newQLStorage.usefulActions = usefulActions;
		}
	}
	else {		//Full upload, end of run

		if (totalActions > this->DDBQLearningDatas[instance].totalActions)
			this->DDBQLearningDatas[instance].totalActions = totalActions;	//Only update if our number is greater, otherwise we have missed updates while crashed
		if (usefulActions > this->DDBQLearningDatas[instance].usefulActions)
			this->DDBQLearningDatas[instance].usefulActions = usefulActions;	//Only update if our number is greater, otherwise we have missed updates while crashed
		this->DDBQLearningDatas[instance].qTable = qTable;
		this->DDBQLearningDatas[instance].expTable = expTable;
	}

	

	return false;
}

bool DDBStore::UpdateQLearningData(char instance, bool usefulAction, int key, float qVal, unsigned int expVal)
{


		if (this->DDBQLearningDatas.find(instance) != this->DDBQLearningDatas.end()) { //Already exists

			this->DDBQLearningDatas[instance].qTable.at(key) = qVal;
			this->DDBQLearningDatas[instance].expTable.at(key) = expVal;
			this->DDBQLearningDatas[instance].totalActions++;
			if (usefulAction)
				this->DDBQLearningDatas[instance].usefulActions++;
		}
		else {
			Log->log(0, "DDBStore::UpdateQLearningData: Avatar instance %d not found.", instance);
			for(auto instIter: this->DDBQLearningDatas)
				Log->log(0, "DDBStore::UpdateQLearningData: Avatar instance in store: %d", instIter.first);
		}
	return false;
}

mapDDBQLearningData DDBStore::GetQLearningData()
{
	return this->DDBQLearningDatas;
}

int DDBStore::GetQLearningData(DataStream *ds, UUID *thread, char instance)
{
	mapDDBQLearningData::iterator iter = this->DDBQLearningDatas.find(instance);

	ds->reset();
	ds->packUUID(thread);
	if (iter == this->DDBQLearningDatas.end()) {
		ds->packChar(DDBR_NOTFOUND);
		return 1; // not found
	}

	// send task data
	ds->packChar(DDBR_OK);
	ds->packInt64(iter->second.totalActions);
	ds->packInt64(iter->second.usefulActions);
	_WRITE_STATE_VECTOR(float, &iter->second.qTable);
	_WRITE_STATE_VECTOR(unsigned int, &iter->second.expTable);

	return 0;
}


//Adviser data storage, for storing data between simulation runs


bool DDBStore::AddAdviceData(char instance, float cq, float bq)
{
	// check to see if it already exists
	if (this->DDBAdviceDatas.find(instance) != this->DDBAdviceDatas.end()) {
		return true; // already exists
	}

	AdviceStorage newAdviceStorage;
	newAdviceStorage.cq = cq;
	newAdviceStorage.bq = bq;

	// insert into DDBQLearningDatas
	this->DDBAdviceDatas[instance] = newAdviceStorage;

	return false;
}

bool DDBStore::AddSimSteps(unsigned long long totalSimSteps)
{
	// insert into DDBTotalSimSteps
	this->DDBTotalSimSteps = totalSimSteps;

	return false;
}

unsigned long long DDBStore::GetSimSteps()
{

	return this->DDBTotalSimSteps;
}


mapDDBAdviceData DDBStore::GetAdviceData()
{
	return this->DDBAdviceDatas;
}