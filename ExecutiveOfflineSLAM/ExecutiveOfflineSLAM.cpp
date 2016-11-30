// ExecutiveOfflineSLAM.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "ExecutiveOfflineSLAM.h"
#include "ExecutiveOfflineSLAMVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\AgentSensorSonar\\AgentSensorSonarVersion.h"
#include "..\\AgentSensorLandmark\\AgentSensorLandmarkVersion.h"
#include "..\\AgentSensorFloorFinder\\AgentSensorFloorFinderVersion.h"
#include "..\\AgentSensorCooccupancy\\AgentSensorCooccupancyVersion.h"

#define CAMERA_READINGS_ONLY

#define NO_SLAM 0

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define SPEEDREAD 0 // don't actually process the readings, used to speed things up when trying to determine the # of slots

#define AVATAR_PFSTATE_SIZE 3
#define READING_DELAY_FUDGE 1000 // fudge a delay in processing readings so that we don't have to worry about pf updates catching up

//*****************************************************************************
// ExecutiveOfflineSLAM

//-----------------------------------------------------------------------------
// Constructor	
ExecutiveOfflineSLAM::ExecutiveOfflineSLAM( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	int i;

	// allocate state
	ALLOCATE_STATE( ExecutiveOfflineSLAM, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "ExecutiveOfflineSLAM" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(ExecutiveOfflineSLAM_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	this->missionDone = false;

	this->nextPathId = 0;
	for ( i=0; i<MAX_PATHS; i++ ) {
		this->paths[i].nodes = NULL;
	}

	this->nextObjectId = 0;
	this->highObjectId = 0;
	for ( i=0; i<MAX_OBJECTS; i++ ) {
		this->objects[i].path_refs = NULL;
	}

	this->readingInd = 0;

	this->simTime = -1;
	this->readingTimeHigh = 0;

	memset( this->readingsProcessedByType, 0, sizeof(this->readingsProcessedByType) );
	memset( this->readingsCountByType, 0, sizeof(this->readingsProcessedByType) );


	this->processingSlots = 10;

	this->waitingForAgents = 0;

	this->waitingForProcessing = 0;
	this->waitingForAck = 0;

	// Prepare callbacks
	this->callback[ExecutiveOfflineSLAM_CBR_convRequestAgentSpawn] = NEW_MEMBER_CB(ExecutiveOfflineSLAM,convRequestAgentSpawn);
	this->callback[ExecutiveOfflineSLAM_CBR_convAgentInfo] = NEW_MEMBER_CB(ExecutiveOfflineSLAM,convAgentInfo);
	this->callback[ExecutiveOfflineSLAM_CBR_convRequestPFInfo] = NEW_MEMBER_CB(ExecutiveOfflineSLAM,convRequestPFInfo);
	
}

//-----------------------------------------------------------------------------
// Destructor
ExecutiveOfflineSLAM::~ExecutiveOfflineSLAM() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int ExecutiveOfflineSLAM::configure() {

	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\ExecutiveOfflineSLAM %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "ExecutiveOfflineSLAM %.2d.%.2d.%.5d.%.2d", ExecutiveOfflineSLAM_MAJOR, ExecutiveOfflineSLAM_MINOR, ExecutiveOfflineSLAM_BUILDNO, ExecutiveOfflineSLAM_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}


int ExecutiveOfflineSLAM::parseMF_HandleMissionRegion( DDBRegion *region ) { 
	DataStream lds;
/*
	// add mission region
	apb->apbUuidCreate( &STATE(ExecutiveOfflineSLAM)->missionRegion );	

	lds.reset();
	lds.packUUID( &STATE(ExecutiveOfflineSLAM)->missionRegion );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();
*/
	return 0; 
}

int ExecutiveOfflineSLAM::parseMF_HandleForbiddenRegion( DDBRegion *region ) { 
	DataStream lds;
/*
	// add mission region
	UUID id;
	apb->apbUuidCreate( &id );	

	this->forbiddenRegions.push_back( id );

	lds.reset();
	lds.packUUID( &id );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();
*/
	return 0; 
}

int ExecutiveOfflineSLAM::parseMF_HandleCollectionRegion( DDBRegion *region ) { 
	DataStream lds;
	UUID id;
/*
	// add collection region
	apb->apbUuidCreate( &id );	

	this->collectionRegions[id] = *region;

	lds.reset();
	lds.packUUID( &id );
	lds.packFloat32( region->x );
	lds.packFloat32( region->y );
	lds.packFloat32( region->w );
	lds.packFloat32( region->h );
	this->sendMessage( this->hostCon, MSG_DDB_ADDREGION, lds.stream(), lds.length() );
	lds.unlock();
*/
	return 0; 
}


int ExecutiveOfflineSLAM::parseMF_HandleLandmarkFile( char *fileName ) { 

	this->parseLandmarkFile( fileName );

	return 0; 
}

int ExecutiveOfflineSLAM::parseMF_HandlePathFile( char *fileName ) { 

	this->loadPathFile( fileName );

	return 0; 
}

int ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM( int SLAMmode, int particleNum, float readingProcessingRate, int processingSlots, char *logPath ) { 

	this->SLAMmode = SLAMmode;
	this->particleNum = particleNum;
	this->readingProcessingRate = readingProcessingRate;
	this->processingSlots = processingSlots;

	// enumerate log directory
	WIN32_FIND_DATA ffd;
	TCHAR szDir[MAX_PATH];
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError = 0;

	swprintf_s( szDir, MAX_PATH, L"%hs\\*", logPath );
	hFind = FindFirstFile(szDir, &ffd);

	if (INVALID_HANDLE_VALUE == hFind) {
		Log.log( 0, "ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM: error FindFirstFile %s", logPath );
		return 1;
	} 

	// List all the files in the directory with some info about them.
	Log.log( 0, "ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM: enumerating directory %s", logPath );
	do {
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			Log.log( 0, "ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM: directory %ws", ffd.cFileName );
		} else {
			Log.log( 0, "ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM: file %ws", ffd.cFileName );
			char filename[MAX_PATH];
			char fullname[MAX_PATH];
			sprintf_s( filename, MAX_PATH, "%ws", ffd.cFileName );
			sprintf_s( fullname, MAX_PATH, "%s\\%ws", logPath, ffd.cFileName );
			this->parseAvatarLog( filename, fullname );
		}
	} while (FindNextFile(hFind, &ffd) != 0);

	dwError = GetLastError();
	if (dwError != ERROR_NO_MORE_FILES) {
		Log.log( 0, "ExecutiveOfflineSLAM::parseMF_HandleOfflineSLAM: enumeration error %d", dwError );
	}

	FindClose(hFind);

	return 0; 
}

//-----------------------------------------------------------------------------
// Start

int ExecutiveOfflineSLAM::start( char *missionFile ) {
	DataStream lds;
	UUID thread;

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	this->simStartTime = this->simTime; // record the start time

	// turn off failure detector
	this->setConnectionfailureDetectionQOS( this->hostCon, 2*60*60*1000 ); // 2 hours

	// register to watch particle filters, sensors, and host group
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packInt32( DDB_PARTICLEFILTER | DDB_SENSORS );
	this->sendMessage( this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length() );
	lds.unlock();

	// add map
	apb->apbUuidCreate( &this->pogUUID );

	lds.reset();
	lds.packUUID( &this->pogUUID );
	lds.packFloat32( 10.0f );
	lds.packFloat32( 0.1f );
	Log.log(0, "OfflineSLAM adding POG");
	this->sendMessage( this->hostCon, MSG_DDB_ADDPOG, lds.stream(), lds.length() );
	lds.unlock();

	// request sensor processing agents
	READING_TYPE type;
	type.sensor = DDB_PARTICLEFILTER;
	type.phase = 0;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;
	type.sensor = DDB_SENSOR_SONAR;
	type.phase = 0;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;
	type.sensor = DDB_SENSOR_CAMERA;
	type.phase = 0;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;
	type.sensor = DDB_SENSOR_CAMERA;
	type.phase = 1;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;
	type.sensor = DDB_SENSOR_SIM_CAMERA;
	type.phase = 0;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;
	type.sensor = DDB_SENSOR_SIM_CAMERA;
	type.phase = 1;
	this->requestAgentSpawn( &type, DDBAGENT_PRIORITY_CRITICAL );
	this->waitingForAgents++;

	STATE(AgentBase)->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int ExecutiveOfflineSLAM::stop() {

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int ExecutiveOfflineSLAM::step() {
	DataStream lds, sds;
	int lastSlots;

	if ( !STATE(AgentBase)->started )
		return AgentBase::step();

	if ( this->waitingForAgents ) {
		Log.log( 0, "ExecutiveOfflineSLAM::step: waiting for agents %d", this->waitingForAgents );
		return AgentBase::step();
	}

	Log.log( 0, "ExecutiveOfflineSLAM::step: step queue size %d waiting for acks %d waiting for processing %d", this->eventQueue.size(), this->waitingForAck, this->waitingForProcessing );

	while ( this->eventQueue.size() && !this->waitingForAck ) {
		// check our processing slots
		lastSlots = this->processingSlots;
		while ( this->processingSlotFinished.size() && this->processingSlotFinished.front() <= this->simTime ) {
			this->processingSlots++;
			this->processingSlotFinished.pop_front();
			
			Log.log( 0, "ExecutiveOfflineSLAM::step: processing slot free %u.%03u, %d slots available", (unsigned int)(this->simTime/1000), (unsigned int)(this->simTime % 1000), this->processingSlots );
		}
		if ( this->processingSlots > 0 ) { // some slots are available, see if we can fill them
			std::list<UUID> allagents;
			mapTypeAgents::iterator iTA;
			std::list<UUID>::iterator iA;
			int ind;
			for ( iTA = this->agents.begin(); iTA != this->agents.end(); iTA++ ) {
				for ( iA = iTA->second.begin(); iA != iTA->second.end(); iA++ ) {
					allagents.push_back( *iA );
				}
			}

			while ( this->processingSlots && allagents.size() ) {
				// pick agent randomly and see if they want the slot
				ind = (int)(apb->apbUniform01()*allagents.size());
				if ( ind == allagents.size() ) ind--; // just in case
				iA = allagents.begin();
				while ( ind ) {
					ind--;
					iA++;
				}

				this->nextSensorReading( &*iA );
				allagents.erase( iA );
			}
		}

		// see if we can process some events
		if ( this->eventQueue.front().time <= this->simTime ) {
			OS_EVENT *evt = &this->eventQueue.front();

			Log.log( 0, "ExecutiveOfflineSLAM::step: sim time %u.%03u processing event %s %d", (unsigned int)(evt->time/1000), (unsigned int)(evt->time % 1000), evt->name, evt->type );
			
			if ( evt->type == 0 ) { // msg
				if ( !evt->isReading || !NO_SLAM ) {
					this->sendMessage( this->hostCon, evt->msg, (char *)evt->data, evt->dataLen );

					if ( evt->expectAck )
						this->waitingForAck++;
					if ( evt->isReading ) {
						evt->readingInfo.ind = this->readingInd++;
						this->readingById[evt->readingInfo.sensorId].push_back( evt->readingInfo );
					}
				}
			} else if ( evt->type == 1 ) { // pf update
				UUID pfId;
				_timeb tm;
				int nochange;
				int dataLen;
				void *data;
				
				// unpack update data
				lds.setData( (char *)evt->data, evt->dataLen );
				lds.unpackUUID( &pfId );
				tm = *(_timeb *)lds.unpackData( sizeof(_timeb) );
				nochange = lds.unpackInt32();
				dataLen = lds.unpackInt32();
				data = lds.unpackData( dataLen );
				
				if ( nochange ) {
					// send no change to DDB
					sds.reset();
					sds.packUUID( &pfId );
					sds.packData( &tm, sizeof(_timeb) );
					sds.packChar( true );
					this->sendMessage( this->hostCon, MSG_DDB_INSERTPFPREDICTION, sds.stream(), sds.length() );
					sds.unlock();
				} else {
					int i;
					float *pfState;
					float *pState;
					float pforwardD, ptangentialD, protationalD;
					float dt, forwardD, tangentialD, rotationalD;
					float sigma[3];
					float sn, cs;
					
					sds.setData( (char *)data, dataLen );
					dt = sds.unpackFloat32();
					forwardD = sds.unpackFloat32();
					tangentialD = sds.unpackFloat32();
					rotationalD = sds.unpackFloat32();
					memcpy( sigma, sds.unpackData(sizeof(float)*3), sizeof(float)*3 );
					sds.unlock();

					if ( NO_SLAM ) // set sigma to 0
						memset( sigma, 0, sizeof(float)*3 );

					pfState = this->avatars[pfId].pfState;
					pState = pfState;

					for ( i=0; i<this->particleNum; i++ ) {
						pforwardD = forwardD + (float)apb->apbNormalDistribution(0,sigma[0])*dt;
						ptangentialD = tangentialD + (float)apb->apbNormalDistribution(0,sigma[1])*dt;
						protationalD = rotationalD + (float)apb->apbNormalDistribution(0,sigma[2])*dt;

						sn = sin( pState[2] );
						cs = cos( pState[2] );

						pState[0] = pState[0] + cs*pforwardD - sn*ptangentialD;
						pState[1] = pState[1] + sn*pforwardD + cs*ptangentialD;
						pState[2] = pState[2] + protationalD;

						pState += 3;
					}

					// send update to DDB
					sds.reset();
					sds.packUUID( &pfId );
					sds.packData( &tm, sizeof(_timeb) );
					sds.packChar( false );
					sds.packData( pfState, sizeof(float)*this->particleNum*3 );
					this->sendMessage( this->hostCon, MSG_DDB_INSERTPFPREDICTION, sds.stream(), sds.length() );
					sds.unlock();

					if ( evt->isReading ) {
						evt->readingInfo.ind = this->readingInd++;
						this->readingById[evt->readingInfo.sensorId].push_back( evt->readingInfo );
					}
				}

				lds.unlock();

				this->waitingForAck++;
			} else if ( evt->type == 2 ) { // true pose
				UUID avatarId;
				_timeb tb;
				
				// unpack data
				lds.setData( (char *)evt->data, evt->dataLen );
				lds.unpackUUID( &avatarId );
				tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
				lds.unlock();

				// request pf state
				UUID thread = this->conversationInitiate( ExecutiveOfflineSLAM_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, evt->data, evt->dataLen );
				if ( thread == nilUUID ) {
					return 0;
				}
				lds.reset();
				lds.packUUID( &this->pfByAvatar[avatarId] );
				lds.packInt32( DDBPFINFO_MEAN | DDBPFINFO_EFFECTIVE_NUM_PARTICLES );
				lds.packData( &tb, sizeof(_timeb) );
				lds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length() );
				lds.unlock();

			}
			
			// event handled
			if ( evt->dataLen )
				free( evt->data );
			this->eventQueue.pop_front();
		} else if ( !this->waitingForProcessing ) {
			// increment time
			this->simTime += 10; // 10 ms step		

			if ( (this->simTime - this->simStartTime) % 10000 == 0 ) { // request data dump every 10 seconds
				char label[256];
				sprintf_s( label, 256, "offline SLAM %u.%03u", (unsigned int)(this->simTime/1000), (unsigned int)(this->simTime % 1000) );
				lds.reset();
				lds.packBool( false ); // fulldump
				lds.packBool( false ); // getPose
				lds.packBool( true ); // hasLabel
				lds.packString( label );
				this->sendMessage( this->hostCon, MSG__DATADUMP_MANUAL, lds.stream(), lds.length() );
				lds.unlock();
			}
		} else {
			Log.log( 0, "ExecutiveOfflineSLAM::step: waiting for readings to process" );
			//apb->apbSleep( 100 );
			break;
		}
	}

	if ( !this->missionDone && !this->eventQueue.size() && !this->waitingForProcessing ) {
		Log.log( 0, "ExecutiveOfflineSLAM::step: mission complete!" );

		// dump reading info
		std::list<OS_READING_INFO>::iterator iR;
		int sumDelay = 0;
		int delay;
		for ( iR = this->readingByProcessOrder.begin(); iR != this->readingByProcessOrder.end(); iR++ ) {
			if ( iR->sensorType.sensor == DDB_PARTICLEFILTER )
				delay = (int)((iR->processT.time - iR->readingT.time)*1000 + iR->processT.millitm - iR->readingT.millitm);
			else // include the reading fudge
				delay = (int)((iR->processT.time - iR->readingT.time)*1000 + iR->processT.millitm - iR->readingT.millitm - READING_DELAY_FUDGE);
			sumDelay += delay;
			Log.log( 0, "ind %d reading time %d.%03d processing time %d.%03d delay %d sensor %s type %d-%d", 
				iR->ind, (int)iR->readingT.time, (int)iR->readingT.millitm, (int)iR->processT.time, (int)iR->processT.millitm,
				delay,
				Log.formatUUID( 0, &iR->sensorId ), iR->sensorType.sensor, iR->sensorType.phase );
		}
		Log.log( 0, "ExecutiveOfflineSLAM::step: Reading Info: processed %d total %d average delay %f", this->readingByProcessOrder.size(), this->readingInd, sumDelay/float(this->readingByProcessOrder.size()) );
		Log.log( 0, "ExecutiveOfflineSLAM::step: Reading Info: by type 16: %d/%d, 64: %d/%d, 256: %d/%d, all: %d/%d", 
			this->readingsProcessedByType[16], this->readingsCountByType[16], 
			this->readingsProcessedByType[64], this->readingsCountByType[64], 
			this->readingsProcessedByType[256], this->readingsCountByType[256],
			this->readingsProcessedByType[16] + this->readingsProcessedByType[64] + this->readingsProcessedByType[256],
			this->readingsCountByType[16] + this->readingsCountByType[64] + this->readingsCountByType[256] );

		
		// notify host
		lds.reset();
		lds.packChar( 1 ); // success
		this->sendMessage( this->hostCon, MSG_MISSION_DONE, lds.stream(), lds.length() );
		lds.unlock();

		this->missionDone = true;
	}

	while ( this->SLAMmode == SM_DISCARD && this->preReadingQueue.size() ) {
		std::list<PRE_READING_QUEUE> *l = &this->preReadingQueue.front();
		std::list<PRE_READING_QUEUE>::iterator iR;
		int ind;
		int i;

		while ( l->size() ) {
			for ( iR = l->begin(), i = 0, ind = (int)(apb->apbUniform01()*l->size()); i < ind; iR++, i++ ); // pick randomly
			this->assignSensorReading( &iR->agent, &iR->rq );
			l->erase( iR );
		}

		this->preReadingQueue.pop_front();
	}

	return AgentBase::step();
}

int ExecutiveOfflineSLAM::insertEvent( char *name, char type, char expectAck, unsigned _int64 time, void *data, int dataLen, unsigned char msg, bool isReading, OS_READING_INFO *readingInfo ) {

	OS_EVENT evt;

	strcpy_s( evt.name, 256, name );
	evt.name[255] = 0;

	evt.type = type;
	evt.expectAck = expectAck;
	evt.time = time;
	evt.msg = msg;
	if ( dataLen ) {
		evt.data = malloc(dataLen);
		memcpy( evt.data, data, dataLen );
	} else {
		evt.data = NULL;
	}
	evt.dataLen = dataLen;

	evt.isReading = isReading;
	if ( isReading )
		evt.readingInfo = *readingInfo;

	std::list<OS_EVENT>::iterator iE = this->eventQueue.begin();
	while ( iE != this->eventQueue.end() && iE->time < time ) 
		iE++;

	this->eventQueue.insert( iE, evt );

	if ( time < this->simTime )
		this->simTime = time; // this is our first event

	return 0;
}

int ExecutiveOfflineSLAM::readDataBlock( FILE *file, void **data, int *dataLen ) {
	int i;
	char ln[1024];

	if ( !fgets( ln, 1024, file ) ) 
		return 1; // couldn't read the first line

	if ( 1 != sscanf_s( ln, "DATABLOCK_START %d", dataLen ) )
		return 1; // bad open

	*data = malloc(*dataLen);

	size_t cnt = fread( *data, 1, *dataLen, file ); // don't use fread_s because it doesn't compile on Windows Server 2003
	if ( cnt != *dataLen ) {
		int err = ferror( file );
		int eof = feof( file );
		err = err;
	}

	fgetc( file ); // clear the new line char after the block

	if ( !fgets( ln, 1024, file ) ) 
		return 1; // couldn't read the last line

	if ( strncmp( ln, "DATABLOCK_END", 13 ) )
		return 1; // bad close

	return 0;
}


int ExecutiveOfflineSLAM::parseAvatarLog( char *filename, char *fullname ) {
	DataStream lds;
	FILE *file;
	char ln[1024];
	int type;
	int err = 0;

	if ( !strncmp( filename, "Avatar", 6 ) ) {
		type = 0;
	} else if ( !strncmp( filename, "ExecutiveSimulation", 19 ) ) {
		type = 1;
	} else {
		Log.log( 0, "ExecutiveOfflineSLAM::parseAvatarLog: wrong log type %s", fullname );
		return 1;
	}
	
	Log.log( LOG_LEVEL_NORMAL, "ExecutiveOfflineSLAM::parseAvatarLog: parsing file %s", fullname );

	// open file
	if ( fopen_s( &file, fullname, "rb" ) ) {
		Log.log( 0, "ExecutiveOfflineSLAM::parseAvatarLog: failed to open %s", fullname );
		return 1;
	}

	if ( type == 0 ) { // avatar log
		while ( fgets( ln, 1024, file ) ) {
			if ( !strncmp( ln + 15, "OFFLINE_SLAM", 12 ) ) {
				if ( !strncmp( ln + 28, "REGISTER_AVATAR ", 16 ) ) {
					WCHAR avatarIdBuf[64], agentIdBuf[64], pfIdBuf[64];
					UUID avatarId, agentId, pfId;
					float innerR, outerR;
					int capacity, sensorTypes;
					char type[64];
					unsigned int tsec, tmil;
					_timeb tm;
					
					sscanf_s( ln + 28, "REGISTER_AVATAR %36ws %36ws %36ws %f %f %d %d %u.%u %s", 
						avatarIdBuf, 64, agentIdBuf, 64, pfIdBuf, 64,
						&innerR, &outerR, &capacity, &sensorTypes,
						&tsec, &tmil, type, 64);
					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );
					UuidFromString( (RPC_WSTR)agentIdBuf, &agentId );
					UuidFromString( (RPC_WSTR)pfIdBuf, &pfId );

					tm.time = tsec;
					tm.millitm = tmil;

					Log.log( 0, "REGISTER_AVATAR %s event for %u.%03u", Log.formatUUID( 0, &avatarId ), tsec, tmil );


					// prepare DDB register message
					lds.reset();
					lds.packUUID( &avatarId );
					lds.packString( type );
					lds.packInt32( 1 );
					lds.packUUID( this->getUUID() ); // use our UUID instead of the agent since they don't exist right now
					lds.packUUID( &pfId );
					lds.packFloat32( innerR );
					lds.packFloat32( outerR );
					lds.packData( &tm, sizeof(_timeb) );
					lds.packInt32( capacity ); // capacity
					lds.packInt32( sensorTypes );
					this->insertEvent( "register avatar", 0, 0, ((unsigned _int64)tsec)*1000+tmil, lds.stream(), lds.length(), (char)MSG_DDB_ADDAVATAR );
					lds.unlock();

				} else if ( !strncmp( ln + 28, "REGISTER_AVATAR_LANDMARK ", 25 ) ) {
					WCHAR landmarkIdBuf[64], avatarIdBuf[64];
					UUID landmarkId, avatarId;
					int code;
					float x, y;
					
					sscanf_s( ln + 28, "REGISTER_AVATAR_LANDMARK %36ws %d %36ws %f %f", landmarkIdBuf, 64, &code, avatarIdBuf, 64, &x, &y );
					UuidFromString( (RPC_WSTR)landmarkIdBuf, &landmarkId );
					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );

					Log.log( 0, "REGISTER_AVATAR_LANDMARK %s", Log.formatUUID( 0, &landmarkId ) );

					// add to DDB
					lds.reset();
					lds.packUUID( &landmarkId );
					lds.packUChar( (unsigned char)code );
					lds.packUUID( &avatarId );
					lds.packFloat32( 0 ); 
					lds.packFloat32( 0 ); 
					lds.packFloat32( x ); 
					lds.packFloat32( y ); 
					lds.packChar( 0 );
					this->sendMessage( this->hostCon, MSG_DDB_ADDLANDMARK, lds.stream(), lds.length() );
					lds.unlock();

				} else if ( !strncmp( ln + 28, "RETIRE_AVATAR ", 14 ) ) {
					WCHAR avatarIdBuf[64];
					UUID avatarId;
					int retireMode;
					unsigned int tsec, tmil;
					_timeb tm;
					
					sscanf_s( ln + 28, "RETIRE_AVATAR %36ws %d %u.%u", 
						avatarIdBuf, 64,
						&retireMode, &tsec, &tmil);
					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );

					tm.time = tsec;
					tm.millitm = tmil;

					Log.log( 0, "RETIRE_AVATAR %s event for %u.%03u", Log.formatUUID( 0, &avatarId ), tsec, tmil );

					// prepare DDB retire message
					lds.reset();
					lds.packUUID( &avatarId );
					lds.packInt32( DDBAVATARINFO_RETIRE );
					lds.packChar( retireMode );
					lds.packData( &tm, sizeof(_timeb) );
					this->insertEvent( "retire avatar", 0, 0, ((unsigned _int64)tsec)*1000+tmil, lds.stream(), lds.length(), MSG_DDB_AVATARSETINFO );
					lds.unlock();

				} else if ( !strncmp( ln + 28, "REGISTER_PF ", 12 ) ) {
					WCHAR pfIdBuf[64], avatarIdBuf[64];
					UUID pfId, avatarId;
					unsigned int tsec, tmil;
					float x, y, r;
					float sigma[3];
					void *data;
					int dataLen;
					_timeb tm;

					sscanf_s( ln + 28, "REGISTER_PF %36ws %36ws %u.%u", pfIdBuf, 64, avatarIdBuf, 64, &tsec, &tmil );
					UuidFromString( (RPC_WSTR)pfIdBuf, &pfId );
					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );

					tm.time = tsec;
					tm.millitm = tmil;

					readDataBlock( file, &data, &dataLen ); 
					lds.setData( (char *)data, dataLen );
					x = lds.unpackFloat32();
					y = lds.unpackFloat32();
					r = lds.unpackFloat32();
					memcpy( sigma, lds.unpackData( sizeof(float)*3 ), sizeof(float)*3 );
					lds.unlock();
					free( data );

					this->pfByAvatar[avatarId] = pfId;
					this->initializePF( pfId, this->particleNum, x, y, r, sigma ); 

					Log.log( 0, "REGISTER_PF %s %u.%03u", Log.formatUUID( 0, &pfId ), tsec, tmil );
					
					// register with DDB
					lds.reset();
					lds.packUUID( &pfId );
					lds.packUUID( &avatarId );
					lds.packInt32( this->particleNum );
					lds.packData( &tm, sizeof(_timeb) );
					lds.packInt32( 3 );
					lds.packData( this->avatars[pfId].pfState, sizeof(float)*this->particleNum*3 );
					this->sendMessage( this->hostCon, MSG_DDB_ADDPARTICLEFILTER, lds.stream(), lds.length() );
					lds.unlock();

				} else if ( !strncmp( ln + 28, "REGISTER_SENSOR ", 16 ) ) {
					WCHAR sensorIdBuf[64], avatarIdBuf[64], pfIdBuf[64];
					UUID sensorId, avatarId, pfId;
					int type;
					void *data;
					int dataLen;

					sscanf_s( ln + 28, "REGISTER_SENSOR %36ws %d %36ws %36ws", sensorIdBuf, 64, &type, avatarIdBuf, 64, pfIdBuf, 64 );
					UuidFromString( (RPC_WSTR)sensorIdBuf, &sensorId );
					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );
					UuidFromString( (RPC_WSTR)pfIdBuf, &pfId );

					readDataBlock( file, &data, &dataLen ); 
					
					Log.log( 0, "REGISTER_SENSOR %s", Log.formatUUID( 0, &sensorId ) );

					// register with DDB
					lds.reset();
					lds.packUUID( &sensorId );
					lds.packInt32( type );
					lds.packUUID( &avatarId );
					lds.packUUID( &pfId );
					lds.packData( data, dataLen );
					this->sendMessage( this->hostCon, MSG_DDB_ADDSENSOR, lds.stream(), lds.length() );
					lds.unlock();

					free( data );

				} else if ( !strncmp( ln + 28, "SONAR_READING ", 14 ) ) {
					WCHAR sensorIdBuf[64];
					UUID sensorId;
					SonarReading reading;
					unsigned int tsec, tmil;
					_timeb tm;
					OS_READING_INFO readingInfo;
					
					sscanf_s( ln + 28, "SONAR_READING %36ws %u.%u %f", 
						sensorIdBuf, 64,
						&tsec, &tmil, &reading.value );
					UuidFromString( (RPC_WSTR)sensorIdBuf, &sensorId );

					tm.time = tsec;
					tm.millitm = tmil;

					readingInfo.sensorId = sensorId;
					readingInfo.readingT = tm;

					Log.log( 0, "SONAR_READING %s event for %u.%03u", Log.formatUUID( 0, &sensorId ), tsec, tmil );

					// prepare DDB update message
					lds.reset();
					lds.packUUID( &sensorId );
					lds.packData( &tm, sizeof(_timeb) );
					lds.packInt32( sizeof(SonarReading) );
					lds.packData( &reading, sizeof(SonarReading) );
					lds.packInt32( 0 );
					unsigned _int64 eventT = ((unsigned _int64)tsec)*1000+tmil + READING_DELAY_FUDGE; // fudge an extra second to ensure the pf update events are caught up
#ifndef CAMERA_READINGS_ONLY
					this->insertEvent( "insert sonar reading", 0, 1, eventT, lds.stream(), lds.length(), MSG_DDB_INSERTSENSORREADING, true, &readingInfo );
#else
					this->insertEvent( "insert sonar reading", 0, 1, eventT, lds.stream(), lds.length(), MSG_DDB_INSERTSENSORREADING, false, &readingInfo );
#endif
					lds.unlock();

				} else if ( !strncmp( ln + 28, "CAMERA_READING ", 15 ) ) {
					WCHAR sensorIdBuf[64];
					UUID sensorId;
					CameraReading reading;
					unsigned int tsec, tmil;
					_timeb tm;
					void *data;
					int dataLen;
					OS_READING_INFO readingInfo;
					
					sscanf_s( ln + 28, "CAMERA_READING %36ws %u.%u %d %d %s", 
						sensorIdBuf, 64,
						&tsec, &tmil, &reading.w, &reading.h, reading.format, IMAGEFORMAT_SIZE );
					UuidFromString( (RPC_WSTR)sensorIdBuf, &sensorId );

					tm.time = tsec;
					tm.millitm = tmil;

					readingInfo.sensorId = sensorId;
					readingInfo.readingT = tm;

					readDataBlock( file, &data, &dataLen ); 

					Log.log( 0, "CAMERA_READING %s event for %u.%03u", Log.formatUUID( 0, &sensorId ), tsec, tmil );

					// prepare DDB update message
					lds.reset();
					lds.packUUID( &sensorId );
					lds.packData( &tm, sizeof(_timeb) );
					lds.packInt32( sizeof(CameraReading) );
					lds.packData( &reading, sizeof(CameraReading) );
					lds.packInt32( dataLen );
					lds.packData( data, dataLen );
					unsigned _int64 eventT = ((unsigned _int64)tsec)*1000+tmil + READING_DELAY_FUDGE; // fudge an extra second to ensure the pf update events are caught up
					this->insertEvent( "insert camera reading", 0, 1, eventT, lds.stream(), lds.length(), MSG_DDB_INSERTSENSORREADING, true, &readingInfo );
					lds.unlock();

					free( data );

				} else if ( !strncmp( ln + 28, "PF_UPDATE ", 10 ) ) {
					WCHAR pfIdBuf[64];
					UUID pfId;
					unsigned int tsec, tmil;
					_timeb tm;
					int nochange;
					void *data;
					int dataLen;
					OS_READING_INFO readingInfo;

					sscanf_s( ln + 28, "PF_UPDATE %36ws %u.%u %d", 
						pfIdBuf, 64,
						&tsec, &tmil, &nochange );
					UuidFromString( (RPC_WSTR)pfIdBuf, &pfId );

					tm.time = tsec;
					tm.millitm = tmil;

					readingInfo.sensorId = pfId;
					readingInfo.readingT = tm;

					if ( !nochange )
						readDataBlock( file, &data, &dataLen ); 
					else
						dataLen = 0;

					Log.log( 0, "PF_UPDATE %s event for %u.%03u", Log.formatUUID( 0, &pfId ), tsec, tmil );

					// prepare update event
					lds.reset();
					lds.packUUID( &pfId );
					lds.packData( &tm, sizeof(_timeb) );
					lds.packInt32( nochange );
					lds.packInt32( dataLen );
					lds.packData( data, dataLen );
#ifndef CAMERA_READINGS_ONLY
					this->insertEvent( "pf update", 1, 1, ((unsigned _int64)tsec)*1000+tmil, lds.stream(), lds.length(), -1, true, &readingInfo );
#else
					this->insertEvent( "pf update", 1, 1, ((unsigned _int64)tsec)*1000+tmil, lds.stream(), lds.length(), -1, false, &readingInfo );
#endif
					lds.unlock();

					if ( !nochange )
						free( data );

				} else {
					Log.log( 0, "ExecutiveOfflineSLAM::parseAvatarLog: unhandled OFFLINE_SLAM event %s", ln );
				}
			}
			err = err;
		}
	
	} else if ( type == 1 ) { // simulation log
		int poseCount = 0;
		WCHAR avatarIdBuf[64];
		UUID avatarId;
		float x, y, r;
		unsigned int tsec, tmil;
		_timeb tb;
		while ( fgets( ln, 1024, file ) ) {
			if ( !strncmp( ln + 15, "ExecutiveSimulation::cbLogPose: log pose", 40 ) ) {
				sscanf_s( ln + 15, "ExecutiveSimulation::cbLogPose: log pose (%d):", &poseCount );
				while ( poseCount ) {
					fgets( ln, 1024, file );
					sscanf_s( ln + 15, "%36ws %u.%u %f %f %f",
						avatarIdBuf, 64, &tsec, &tmil, &x, &y, &r );

					UuidFromString( (RPC_WSTR)avatarIdBuf, &avatarId );

					tb.time = tsec;
					tb.millitm = tmil;
					
					// insert event
					lds.reset();
					lds.packUUID( &avatarId );
					lds.packData( &tb, sizeof(_timeb) );
					lds.packFloat32( x );
					lds.packFloat32( y );
					lds.packFloat32( r );
					this->insertEvent( "true pose", 2, 0, ((unsigned _int64)tsec)*1000+tmil + 1000, lds.stream(), lds.length() );
					lds.unlock();
	
					poseCount--;
				}
			}
		}
	}

	fclose( file );

	return err;
}

int ExecutiveOfflineSLAM::parseLandmarkFile( char *filename ) {
	FILE *landmarkF;
	char name[256];
	char *ch;
	int id;
	UUID uuid;
	float x, y, height, elevation;
	int estimatedPos;

	UUID nilId;
	UuidCreateNil( &nilId );

	int err = 0;

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveOfflineSLAM::parseLandmarkFile: parsing file %s", filename );

	// open file
	if ( fopen_s( &landmarkF, filename, "r" ) ) {
		Log.log( 0, "ExecutiveOfflineSLAM::parseLandmarkFile: failed to open %s", filename );
		return 1;
	}

	while (1) {
		ch = name;
		while ( EOF != (*ch = fgetc( landmarkF )) ) {
			if ( *ch == '\n' ) {
				*ch = 0;
				break;
			}
			ch++;
		}
		if ( *ch == EOF )
			break; // out of landmarks
		if ( 1 != fscanf_s( landmarkF, "id=%d\n", &id ) ) {
			Log.log( 0, "ExecutiveOfflineSLAM::parseLandmarkFile: expected id=<int>, check format (landmark %s)", name );
			err = 1;
			break;
		}
		if ( 5 != fscanf_s( landmarkF, "pose=%f %f %f %f %d\n", &x, &y, &height, &elevation, &estimatedPos ) ) {
			Log.log( 0, "ExecutiveOfflineSLAM::parseLandmarkFile: expected pose=<float> <float> <float> <float> <int>, check format (landmark %s)", name );
			err = 1;
			break;
		}

		// create uuid
		apb->apbUuidCreate( &uuid );

		// add to DDB
		this->ds.reset();
		this->ds.packUUID( &uuid );
		this->ds.packUChar( (unsigned char)id );
		this->ds.packUUID( &nilId );
		this->ds.packFloat32( height ); 
		this->ds.packFloat32( elevation ); 
		this->ds.packFloat32( x ); 
		this->ds.packFloat32( y ); 
		this->ds.packChar( estimatedPos );
		this->sendMessage( this->hostCon, MSG_DDB_ADDLANDMARK, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		Log.log( LOG_LEVEL_NORMAL, "ExecutiveOfflineSLAM::parseLandmarkFile: added landmark %d (%s): %f %f %f %f %d", id, name, x, y, height, elevation, estimatedPos );
	}

	fclose( landmarkF );

	return err;
}

int ExecutiveOfflineSLAM::newPath( int count, float *x, float *y ) {
	int i;
	NODE *node;

	while ( this->nextPathId < MAX_PATHS && this->paths[this->nextPathId].nodes != NULL )
		this->nextPathId++;

	if ( this->nextPathId == MAX_PATHS )
		return -1; // we ran out of ids!

	this->paths[this->nextPathId].references = 0;
	this->paths[this->nextPathId].nodes = new std::list<NODE *>;
	if ( this->paths[this->nextPathId].nodes == NULL )
		return -1;

	for ( i=0; i<count; i++ ) {
		node = (NODE *)malloc( sizeof(NODE) );
		if ( node == NULL )
			return -1;

		node->x = x[i];
		node->y = y[i];

		this->paths[this->nextPathId].nodes->push_back( node );
	}

	// vis
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packInt32( this->nextPathId );
	this->ds.packInt32( count );
	this->ds.packData( x, count*sizeof(float) );
	this->ds.packData( y, count*sizeof(float) );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDPATH, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return this->nextPathId++;
}

int ExecutiveOfflineSLAM::loadPathFile( char *fileN ) {
	FILE *file;
	char cBuf[1024];
	int scanned, points, id, obj;
	float x[65], y[65];
	float poseX, poseY, poseR, poseS;
	float width, colour[3];
	int solid;
	float *colourPtr[1] = {colour};

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveOfflineSLAM::loadPathFile: parsing file %s", fileN );

	
	if ( fopen_s( &file, fileN, "r" ) ) {
		Log.log( LOG_LEVEL_NORMAL, "ExecutiveOfflineSLAM::loadPathFile: couldn't open file" );
		return 1; // failed to open file
	}

	// expecting one or more paths with the format:
	// point=<x float>\t<y float>\n
	// point=<x float>\t<y float>\n
	// ... up to a maximum of 64 points
	// static=<name>\n
	// pose=<x float>\t<y float>\t<r float>\t<s float>\n
	// width=<line width float>\n
	// colour=<r float>\t<g float>\t<b float>\n
	while ( 1 ) {
		points = 0;
		while ( fscanf_s( file, "point=%f\t%f\n", &x[points], &y[points] ) == 2 ) {
			points++;
			if ( points == 65 ) {
				fclose( file );
				return 1; // more than 64 points
			}
		}
		if ( points == 0 ) {
			break; // expected at least 1 point, we must be finished
		}
		
		id = newPath( points, x, y );

		if ( id == -1 ) {
			fclose( file );
			return 1; // failed to create path
		}

		while ( 1 ) { // look for objects
			// read the static object name
			scanned = fscanf_s( file, "static=%s\n", cBuf, 1024 );
			if ( scanned != 1 ) {
				break; // expected name, we must be done objects for this path
			}
			// read pose
			scanned = fscanf_s( file, "pose=%f\t%f\t%f\t%f\n", &poseX, &poseY, &poseR, &poseS );
			if ( scanned != 4 ) {
				fclose( file );
				return 1; // expected pose
			}
			// read line width
			scanned = fscanf_s( file, "width=%f\n", &width );
			if ( scanned != 1 ) {
				fclose( file );
				return 1; // expected line width
			}
			// read colour
			scanned = fscanf_s( file, "colour=%f\t%f\t%f\n", &colour[0], &colour[1], &colour[2] );
			if ( scanned != 3 ) {
				fclose( file );
				return 1; // expected colour
			}
			// read solid
			scanned = fscanf_s( file, "solid=%d\n", &solid );
			if ( scanned != 1 ) {
				fclose( file );
				return 1; // expected solid
			}

			obj = newStaticObject( poseX, poseY, poseR, poseS, 1, &id, colourPtr, &width, (solid != 0), cBuf );
			if ( obj == -1 ) {
				fclose( file );
				return 1; // failed to create object
			}

		}
	}

	fclose( file );
	return 0;
}


int ExecutiveOfflineSLAM::newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	float *ptr = (float *)malloc( sizeof(float)*4 );
	if ( ptr == NULL )
		return -1;

	ptr[0] = x;
	ptr[1] = y;
	ptr[2] = r;
	ptr[3] = s;

	return createObject( ptr, ptr+1, ptr+2, ptr+3, count, paths, colours, lineWidths, false, solid, name );
}

int ExecutiveOfflineSLAM::createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name ) {
	int i;
	PATH_REFERENCE *path_ref;

	while ( this->nextObjectId < MAX_OBJECTS && this->objects[this->nextObjectId].path_refs != NULL )
		this->nextObjectId++;

	if ( this->nextObjectId == MAX_OBJECTS )
		return -1; // we ran out of ids!

	if ( this->nextObjectId > this->highObjectId )
		this->highObjectId = this->nextObjectId;

	if ( name != NULL )
		strcpy_s( this->objects[this->nextObjectId].name, 256, name );

	this->objects[this->nextObjectId].dynamic = dynamic;
	this->objects[this->nextObjectId].solid = solid;
	this->objects[this->nextObjectId].visible = true;

	this->objects[this->nextObjectId].x = x;
	this->objects[this->nextObjectId].y = y;
	this->objects[this->nextObjectId].r = r;
	this->objects[this->nextObjectId].s = s;

	this->objects[this->nextObjectId].path_refs = new std::list<PATH_REFERENCE *>;

	if ( this->objects[this->nextObjectId].path_refs == NULL )
		return -1;

	for ( i=0; i<count; i++ ) {
		path_ref = (PATH_REFERENCE *)malloc( sizeof(PATH_REFERENCE) );
		if ( path_ref == NULL )
			return -1;

		path_ref->id = paths[i];
		path_ref->r = colours[i][0];
		path_ref->g = colours[i][1];
		path_ref->b = colours[i][2];
		path_ref->lineWidth = lineWidths[i];
		path_ref->stipple = 0;

		this->paths[paths[i]].references++;

		this->objects[this->nextObjectId].path_refs->push_back( path_ref );
	}

	// vis
	this->ds.reset();
	this->ds.packUUID( this->getUUID() ); // agent
	this->ds.packInt32( this->nextObjectId ); // objectId
	this->ds.packFloat32( *x ); // x
	this->ds.packFloat32( *y ); // y
	this->ds.packFloat32( *r ); // r
	this->ds.packFloat32( *s ); // s
	this->ds.packInt32( count ); // path count
	for ( i=0; i<count; i++ ) {
		this->ds.packInt32( paths[i] ); // path id
		this->ds.packFloat32( colours[i][0] ); // r
		this->ds.packFloat32( colours[i][1] ); // g
		this->ds.packFloat32( colours[i][2] ); // b
		this->ds.packFloat32( lineWidths[i] ); // lineWidth
	}
	this->ds.packBool( solid ); // solid
	if ( name )
		this->ds.packString( name );
	else
		this->ds.packString( "" );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_ADDOBJECT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return this->nextObjectId;
}

int ExecutiveOfflineSLAM::initializePF( UUID pfId, int particleNum, float x, float y, float r, float *sigma ) {
	int i;
	OS_AVATAR *av = &this->avatars[pfId];

	av->pfState = (float *)malloc(sizeof(float)*3*particleNum);
	float *pstate = av->pfState;

	for ( i=0; i<particleNum; i++ ) {
		pstate[0] = x + (float)apb->apbNormalDistribution( 0, sigma[0] );
		pstate[1] = y + (float)apb->apbNormalDistribution( 0, sigma[1] );
		pstate[2] = r + (float)apb->apbNormalDistribution( 0, sigma[2] );
		pstate += 3;
	}

	return 0;
}

int ExecutiveOfflineSLAM::_resampleParticleFilter( UUID *id, int pNum, float *weights ) {
	DataStream lds;
	int i, *parents, parentNum;
	float cdfOld, cdfNew;
	float particleNumInv;
	float *state, *pState, *parentPState;

	Log.log( 0, "ExecutiveOfflineSLAM::_resampleParticleFilter: resampling pf %s %u.%03u",
		Log.formatUUID( 0, id ), (unsigned int)(this->simTime/1000), (unsigned int)(this->simTime % 1000) );

	// create list of parents
	parents = (int *)malloc(sizeof(int)*pNum);
	if ( !parents )
		return 1;

	// create new state
	state = (float *)malloc(sizeof(float)*pNum*AVATAR_PFSTATE_SIZE);
	if ( !state )
		return 1;

	particleNumInv = 1.0f/pNum;
	
	parentNum = 0;
	parentPState = (float *)this->avatars[*id].pfState;
	pState = state;

	cdfOld = *weights;
	cdfNew = (float)apb->apbUniform01()*particleNumInv;

	// identify parents and copy states
	for ( i=0; i<pNum; i++ ) {
		
		while ( cdfOld < cdfNew ) {
			if ( parentNum == pNum-1 )
				break; // don't go passed the last parent
			weights++;
			parentNum++;
			parentPState += 3;
			cdfOld += *weights;
		}

		parents[i] = parentNum;
		memcpy( pState, parentPState, sizeof(float)*AVATAR_PFSTATE_SIZE );

		pState += AVATAR_PFSTATE_SIZE;
		cdfNew += particleNumInv;
	}

	// pack the data stream 
	lds.reset();
	lds.packUUID( id );
	lds.packInt32( pNum );
	lds.packInt32( AVATAR_PFSTATE_SIZE );
	lds.packData( parents, sizeof(int)*pNum );
	lds.packData( state, sizeof(float)*AVATAR_PFSTATE_SIZE*pNum );
	this->sendMessage( this->hostCon, MSG_DDB_SUBMITPFRESAMPLE, lds.stream(), lds.length() );
	lds.unlock();

	// apply locally
	memcpy( this->avatars[*id].pfState, state, sizeof(float)*AVATAR_PFSTATE_SIZE*pNum );

	free( parents );
	free( state );

	return 0;
}

int ExecutiveOfflineSLAM::getProcessingPhases( int sensor ) {
	switch ( sensor ) {
	case DDB_INVALID:
	case DDB_PARTICLEFILTER:
	case DDB_SENSOR_SONAR:
		return 1;
#ifndef CAMERA_READINGS_ONLY
	case DDB_SENSOR_CAMERA:
		return 2;
	case DDB_SENSOR_SIM_CAMERA:
		return 2;
#else // pure localization
	case DDB_SENSOR_CAMERA:
		return 1;
	case DDB_SENSOR_SIM_CAMERA:
		return 1;
#endif
	default:
		Log.log( 0, "ExecutiveOfflineSLAM::getProcessingPhases: unknown sensor type!" );
		return 0;
	}
}

int ExecutiveOfflineSLAM::requestAgentSpawn( READING_TYPE *type, char priority ) {

	this->agentsSpawning[*type]++; // we're spawning this type of agent

	UUID sAgentuuid;
	UUID thread;
	switch ( type->sensor ) {
	case DDB_PARTICLEFILTER:
		UuidFromString( (RPC_WSTR)_T(AgentSensorCooccupancy_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_SONAR:
		UuidFromString( (RPC_WSTR)_T(AgentSensorSonar_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_CAMERA:
		if ( type->phase == 0 )		UuidFromString( (RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid );
		else						UuidFromString( (RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid );
		break;
	case DDB_SENSOR_SIM_CAMERA:
		if ( type->phase == 0 )		UuidFromString( (RPC_WSTR)_T(AgentSensorLandmark_UUID), &sAgentuuid );
		else						UuidFromString( (RPC_WSTR)_T(AgentSensorFloorFinder_UUID), &sAgentuuid );
		break;
	default:
		Log.log( 0, "ExecutiveOfflineSLAM::requestAgentSpawn: unknown sensor type!" );
		return NULL;
	}
	thread = this->conversationInitiate( ExecutiveOfflineSLAM_CBR_convRequestAgentSpawn, REQUESTAGENTSPAWN_TIMEOUT, type, sizeof(READING_TYPE) );
	if ( thread == nilUUID ) {
		return NULL;
	}

	Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::requestAgentSpawn: requesting agent %s %d-%d (thread %s)", Log.formatUUID(LOG_LEVEL_VERBOSE,&sAgentuuid), type->sensor, type->phase, Log.formatUUID(LOG_LEVEL_VERBOSE,&thread) );

	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( &sAgentuuid );
	this->ds.packChar( -1 ); // no instance parameters
	this->ds.packFloat32( 0.0f ); // affinity
	this->ds.packChar( priority );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

UUID * ExecutiveOfflineSLAM::getProcessingAgent( READING_TYPE *type ) {
	int i;

	// see if we already have agents of this type
	mapTypeAgents::iterator iterTA;
	iterTA = this->agents.find( *type );

	if ( iterTA == this->agents.end() || iterTA->second.empty() ) { // we need to spawn an agent of this type
		mapAgentSpawning::iterator iterSpawning;
		iterSpawning = this->agentsSpawning.find( *type );
		if ( iterSpawning != this->agentsSpawning.end() )
			return NULL; // already spawning
	
		Log.log( 0, "ExecutiveOfflineSLAM::getProcessingAgent: error! we don't have a processing agent for this type (%d-%d)!", type->sensor, type->phase );

		return NULL;
	} else { // we only ever have one agent
		UUID *bestAgent;
		int bestReadings;

		std::list<UUID>::iterator iter;
		iter = iterTA->second.begin();
		bestAgent = (UUID *)&(*iter);
		bestReadings = (int)this->agentQueue[*iter].size();

		return bestAgent;
	}
}

int ExecutiveOfflineSLAM::preAssignSensorReading( UUID *agent, READING_QUEUE *rq ) {
	PRE_READING_QUEUE prq;
	std::list<std::list<PRE_READING_QUEUE>>::iterator iT; // iterator by time
	std::list<PRE_READING_QUEUE>::iterator iR; // iterator by reading

	prq.agent = *agent;
	prq.rq = *rq;

	for ( iT = this->preReadingQueue.begin(); iT != this->preReadingQueue.end(); iT++ ) {
		if ( iT->front().rq.tb.time >= rq->tb.time )
			break;
	}

	if ( iT == this->preReadingQueue.end() ) { // after all current readings
		std::list<PRE_READING_QUEUE> l;
		l.push_back( prq );
		this->preReadingQueue.push_back( l );
	} else if ( iT->front().rq.tb.time == rq->tb.time ) { // within a second of this set
		iT->push_back( prq );
	} else { // before the first set
		std::list<PRE_READING_QUEUE> l;
		l.push_back( prq );
		this->preReadingQueue.push_front( l );
	}
	
	return 0;
}

int ExecutiveOfflineSLAM::nextSensorReading( UUID *agent ) {
	bool agentReady = false;
	std::map<UUID,int,UUIDless>::iterator iS = this->agentsStatus.find( *agent );

	if ( iS != this->agentsStatus.end() 
	  && ( iS->second == DDBAGENT_STATUS_READY
	  || iS->second == DDBAGENT_STATUS_FREEZING
	  || iS->second == DDBAGENT_STATUS_FROZEN
	  || iS->second == DDBAGENT_STATUS_THAWING) ) {
		agentReady = true;
	}

	if ( ( this->SLAMmode == SM_IDEAL || this->SLAMmode == SM_DISCARD || this->processingSlots > 0 ) // if SM_DISCARD and the reading has gotten this far then it has already taken a slot, otherwise make sure a slot is available
		&& !this->agentsProcessing[*agent] && !this->agentQueue[*agent].empty() && agentReady ) { // assign next reading
		READING_QUEUE rqNext;
		
		switch ( this->SLAMmode ) {
		case SM_IDEAL:
		case SM_DISCARD:
		case SM_JCSLAM_FIFO:
			rqNext = this->agentQueue[*agent].front(); // FIFO
			break;
		case SM_JCSLAM_RANDOM:
			{
				std::list<READING_QUEUE>::iterator iR;
				int ind = (int)(apb->apbUniform01()*this->agentQueue[*agent].size());
				if ( ind == this->agentQueue[*agent].size() ) ind--; // just in case
				iR = this->agentQueue[*agent].begin();
				while ( ind ) {
					ind--;
					iR++;
				}
				rqNext = *iR; // random
			}
			break;
		//case SM_JCSLAM:
		default:
			rqNext = this->agentQueue[*agent].back(); // LIFO
		};
		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::nextSensorReading: assign next reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rqNext.uuid) );

		// assign to agent
		if ( this->SLAMmode != SM_DISCARD ) { // in discard mode this reading has already been assigned	
			this->readingAssigned( &rqNext );
		}

		this->agentsProcessing[*agent] = true;
		this->ds.reset();
		this->ds.packUUID( &rqNext.uuid );
		this->ds.packData( (void *)&rqNext.tb, sizeof(_timeb) );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_ADD_READING), this->ds.stream(), this->ds.length(), agent );
		this->ds.unlock();
	}

	return 0;
}

int ExecutiveOfflineSLAM::assignSensorReading( UUID *agent, READING_QUEUE *rq ) {
	
	bool agentReady = false;
	std::map<UUID,int,UUIDless>::iterator iS = this->agentsStatus.find( *agent );

	if ( iS != this->agentsStatus.end() 
	  && ( iS->second == DDBAGENT_STATUS_READY
	  || iS->second == DDBAGENT_STATUS_FREEZING
	  || iS->second == DDBAGENT_STATUS_FROZEN  
	  || iS->second == DDBAGENT_STATUS_THAWING) ) {
		agentReady = true;
	}

	if ( this->SLAMmode == SM_DISCARD && this->processingSlots == 0 ) {
		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::assignSensorReading: all processing slots are full, discarding reading" );
		return 0; // discard!
	}

	if ( ( this->SLAMmode == SM_IDEAL || this->processingSlots > 0 )
		&& this->agentQueue[*agent].empty() && agentReady ) {
		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::assignSensorReading: agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
		// add to agent queue
		this->agentQueue[*agent].push_back( *rq );

		// assign to agent
		this->readingAssigned( rq );
#if SPEEDREAD
		this->doneSensorReading( agent, &rq->uuid, &rq->tb, 1 ); // wow, we're done already!
#else
		this->agentsProcessing[*agent] = true;
		this->ds.reset();
		this->ds.packUUID( &rq->uuid );
		this->ds.packData( (void *)&rq->tb, sizeof(_timeb) );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_ADD_READING), this->ds.stream(), this->ds.length(), agent );
		this->ds.unlock();
#endif
	} else { // agent is already processing

		Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::assignSensorReading: queue reading, agent %s sensor %s", Log.formatUUID(LOG_LEVEL_VERBOSE,agent), Log.formatUUID(LOG_LEVEL_VERBOSE,&rq->uuid) );
	
		// add to agent queue
		this->agentQueue[*agent].push_back( *rq );

		if ( this->SLAMmode == SM_DISCARD ) {
			this->readingAssigned( rq ); // in discard mode the reading takes up a slot as soon as it is accepted for processing
		}
	}

	return 0;
}

int ExecutiveOfflineSLAM::doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success ) {
	READING_QUEUE rq;
	RPC_STATUS Status;

	Log.log( 0, "ExecutiveOfflineSLAM::doneSensorReading: agent %s sensor %s success %d", Log.formatUUID(0,uuid), Log.formatUUID(0,sensor), success );
	
	this->waitingForProcessing--; // done reading for better or worse

	if ( this->agentQueue[*uuid].empty() )
		return 1; // what happened?

	std::list<READING_QUEUE>::iterator iter = this->agentQueue[*uuid].begin();
	while ( iter != this->agentQueue[*uuid].end() ) { // find the reading
		if ( !UuidCompare( &(*iter).uuid, sensor, &Status ) && (*iter).tb.time == tb->time && (*iter).tb.millitm == tb->millitm )
			break;
		iter++;
	}

	if ( iter == this->agentQueue[*uuid].end() )
		return 1; // couldn't find the reading?

	rq = *iter;
	this->agentQueue[*uuid].erase( iter );

	this->agentsProcessing[*uuid] = false;

	// tell agent to process next assigned reading
	this->nextSensorReading( uuid );

	if ( success == 1 ) { 
		rq.type.phase++;
		
		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
		
	} else { // give up after any kind of failure, because in this case failure probably means we haven't advanced the pfs far enough, and it's easier just to skip the reading
		rq.type.phase++;
		
		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
	}
	/*} else if ( success == -1 ) { // permanent failure
		rq.type.phase++;
		
		// next reading/phase
		if ( rq.type.phase < this->getProcessingPhases( rq.type.sensor ) ) { // start next phase
			rq.attempt = 0;
			this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
		}
		
	} else { // failure
		// assign reading again
		this->newSensorReading( &rq.type, &rq.uuid, &rq.tb, rq.attempt );
	} */

	return 0;
}

int ExecutiveOfflineSLAM::readingAssigned( READING_QUEUE *rq ) {
	
	this->waitingForProcessing++; // wait until this reading is finished before processing more events
	
	this->processingSlots--;
	this->processingSlotFinished.push_back( (unsigned _int64)(this->simTime + 1000/this->readingProcessingRate) );
	
	Log.log( 0, "ExecutiveOfflineSLAM::readingAssigned: slot taken %u.%03u, reserved for %d, %d slots remaining", 
		(unsigned int)(this->simTime/1000), (unsigned int)(this->simTime % 1000), 
		(int)(1000/this->readingProcessingRate), this->processingSlots );

	// find the reading for our records
	if ( rq->type.phase == 0 && rq->attempt == 1 ) { // only the first time
		std::list<OS_READING_INFO>::iterator iR;
		for ( iR = this->readingById[rq->uuid].begin(); iR != this->readingById[rq->uuid].end(); iR++ ) {
			if ( iR->readingT.millitm == rq->tb.millitm && iR->readingT.time == rq->tb.time ) {
				iR->processT.time = this->simTime / 1000;
				iR->processT.millitm = this->simTime % 1000;
				iR->sensorType = rq->type;
				this->readingByProcessOrder.push_back( *iR );
				this->readingsProcessedByType[iR->sensorType.sensor]++;
				this->readingById[rq->uuid].erase( iR );
				break;
			}
		}
		if ( iR == this->readingById[rq->uuid].end() ) {
			Log.log( 0, "ExecutiveOfflineSLAM::readingAssigned: reading not found!" );
		}
	}
	return 0;
}

int ExecutiveOfflineSLAM::newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt ) {
	READING_QUEUE rq;
	UUID *agent;

	Log.log( LOG_LEVEL_VERBOSE, "ExecutiveOfflineSLAM::newSensorReading: type %d-%d id %s attempt %d", type->sensor, type->phase, Log.formatUUID(LOG_LEVEL_VERBOSE,uuid), attempt );

	if ( attempt > 5 ) // give up on this reading
		return 0;

	rq.type = *type;
	rq.uuid = *uuid;
	rq.tb = *tb;
	rq.attempt = attempt + 1;

	// get appropriate processing agent
	agent = this->getProcessingAgent( type );

	if ( !agent ) {
		// assign reading to wait queue
		this->typeQueue[*type].push_front( rq );
	} else {
		// assign reading to agent
	// Don't do this	if ( this->SLAMmode == SM_DISCARD ) 
	//		this->preAssignSensorReading( agent, &rq );
	//	else
			this->assignSensorReading( agent, &rq );
	}

	return 0;
}

int ExecutiveOfflineSLAM::ddbNotification( char *data, int len ) {
	DataStream lds, sds;
	UUID uuid;

	int type;
	char evt;
	lds.setData( data, len );
	lds.unpackData( sizeof(UUID) ); // key
	type = lds.unpackInt32();
	lds.unpackUUID( &uuid );
	evt = lds.unpackChar();
	
	if ( evt == DDBE_WATCH_ITEM ) {
		if ( this->agentsProcessing.find(uuid) != this->agentsProcessing.end() ) { // one of our processing agents
			// get status
			UUID thread = this->conversationInitiate( ExecutiveOfflineSLAM_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
			sds.reset();
			sds.packUUID( &uuid );
			sds.packInt32( DDBAGENTINFO_RSTATUS );
			sds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
			sds.unlock();
		}
	}
	if ( type == DDB_PARTICLEFILTER ) { 
		if ( evt == DDBE_PF_PREDICTION ) {
			this->waitingForAck--; // prediction was added

			_timeb tb;
			READING_TYPE reading;

			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			char nochange = lds.unpackChar();
			
			if ( !nochange ) {
#ifndef CAMERA_READINGS_ONLY
				reading.sensor = type;
				reading.phase = 0;
				this->newSensorReading( &reading, &uuid, &tb );

				this->readingsCountByType[DDB_PARTICLEFILTER]++;
#endif
			}
		}
	} else if ( type & DDB_SENSORS ) {
		if ( evt == DDBE_SENSOR_UPDATE ) {
			this->waitingForAck--; // update was added

			_timeb tb;
			READING_TYPE reading;

			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
#ifndef CAMERA_READINGS_ONLY
			reading.sensor = type;
			reading.phase = 0;
			this->newSensorReading( &reading, &uuid, &tb );
			this->readingsCountByType[reading.sensor]++;
#else
			if ( type == DDB_SENSOR_SIM_CAMERA || type == DDB_SENSOR_CAMERA ) {
				reading.sensor = type;
				reading.phase = 0;
				this->newSensorReading( &reading, &uuid, &tb );
				this->readingsCountByType[reading.sensor]++;
			}
#endif
		}
	} else if ( type == DDB_AGENT && this->agentsStatus.find(uuid) != this->agentsStatus.end() ) { // our agent
		if ( evt == DDBE_AGENT_UPDATE ) {
			int infoFlags = lds.unpackInt32();
			if ( infoFlags & DDBAGENTINFO_STATUS ) { // status changed
				
				// get status
				UUID thread = this->conversationInitiate( ExecutiveOfflineSLAM_CBR_convAgentInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID) );
				sds.reset();
				sds.packUUID( &uuid );
				sds.packInt32( DDBAGENTINFO_RSTATUS );
				sds.packUUID( &thread );
				this->sendMessage( this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length() );
				sds.unlock();
			}
		}
	}
	lds.unlock();
	
	return 0;
}


//-----------------------------------------------------------------------------
// Process message

int ExecutiveOfflineSLAM::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case MSG_DDB_PFRESAMPLEREQUEST:
		{
			UUID id;
			int pNum;
			float *weights;
			lds.setData( data, len );
			lds.unpackUUID( &id );
			pNum = lds.unpackInt32();
			weights = (float *)lds.unpackData( sizeof(float)*pNum );
			this->_resampleParticleFilter( &id, pNum, weights );
		}
		break;
	case MSG_DONE_SENSOR_READING:
		{
			UUID sensor;
			_timeb tb;
			char success;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &sensor );
			tb = *(_timeb*)lds.unpackData( sizeof(_timeb) );
			success = lds.unpackChar();
			lds.unlock();
			this->doneSensorReading( &uuid, &sensor, &tb, success );
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool ExecutiveOfflineSLAM::convRequestAgentSpawn( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	READING_TYPE type = *(READING_TYPE *)conv->data;
	
	this->agentsSpawning[type]--; // spawn is over for good or ill

	this->waitingForAgents--;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "ExecutiveOfflineSLAM::convRequestAgentSpawn: request spawn timed out (thread %s)", Log.formatUUID(0,&conv->thread) );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread

	if ( lds.unpackBool() ) { // succeeded
		UUID uuid;

		lds.unpackUUID( &uuid );
		lds.unlock();

		Log.log( 0, "ExecutiveOfflineSLAM::convRequestAgentSpawn: spawn succeeded %d-%d (%s) (thread %s)", type.sensor, type.phase, Log.formatUUID(0, &uuid), Log.formatUUID(0,&conv->thread) );

		// add this agent to our list
		this->agents[type].push_back( uuid );
		this->agentsProcessing[uuid] = false;

		// register as agent watcher
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &uuid );
		this->sendMessage( this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length() );
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		// set parent
		this->sendMessage( this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &uuid );

		// configure agent
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packUUID( &this->pogUUID );
		this->sendMessageEx( this->hostCon, MSGEX(AgentSensorProcessing_MSGS,MSG_CONFIGURE), lds.stream(), lds.length(), &uuid );
		lds.unlock();

		// start the agent
		lds.reset();
		lds.packString( STATE(AgentBase)->missionFile );
		this->sendMessage( this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &uuid );
		lds.unlock();

		// if we have readings waiting then assign them
		mapTypeQueue::iterator iterTQ = this->typeQueue.find( type );
		if ( iterTQ != this->typeQueue.end() ) {
			while ( !iterTQ->second.empty() ) {
				this->newSensorReading( &iterTQ->second.front().type, &iterTQ->second.front().uuid, &iterTQ->second.front().tb );
				//this->assignSensorReading( &uuid, &iterTQ->second.front() );
				iterTQ->second.pop_front();
			}			
		}

	} else {
		lds.unlock();
	
		Log.log( 0, "ExecutiveOfflineSLAM::convRequestAgentSpawn: spawn failed %d-%d (thread %s)", type.sensor, type.phase, Log.formatUUID(0,&conv->thread) );
	}

	return 0;
}

bool ExecutiveOfflineSLAM::convAgentInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) {
		Log.log( 0, "ExecutiveOfflineSLAM::convAgentInfo: request timed out" );
		return 0; // end conversation
	}

	UUID agent = *(UUID *)conv->data;

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	char result = lds.unpackChar(); 
	if ( result == DDBR_OK ) { // succeeded
		if ( lds.unpackInt32() != DDBAGENTINFO_RSTATUS ) {
			lds.unlock();
			return 0; // what happened here?
		}

		this->agentsStatus[agent] = lds.unpackInt32();
		lds.unlock();

		Log.log( 0, "ExecutiveOfflineSLAM::convAgentInfo: status %d", this->agentsStatus[agent] );

		if ( this->agentsStatus[agent] != DDBAGENT_STATUS_READY
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_FREEZING
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_FROZEN
		  && this->agentsStatus[agent] != DDBAGENT_STATUS_THAWING ) {
			this->agentsProcessing[agent] = false; // give up
		}

		// try do process a reading
		this->nextSensorReading( &agent );

	} else {
		lds.unlock();

		Log.log( 0, "ExecutiveOfflineSLAM::convAgentInfo: request failed %d", result );
	}

	return 0;
}

bool ExecutiveOfflineSLAM::convRequestPFInfo( void *vpConv ) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	int infoFlags;
	char response;
	
	if ( conv->response == NULL ) {
		Log.log( 0, "ExecutiveOfflineSLAM::convRequestPFInfo: request timed out" );
		
		return 0; // end conversation
	}

	// unpack conv data
	UUID avatarId;
	_timeb tb;
	float x, y, r;
	lds.setData( (char *)conv->data, conv->dataLen );
	lds.unpackUUID( &avatarId );
	tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
	x = lds.unpackFloat32();
	y = lds.unpackFloat32();
	r = lds.unpackFloat32();
	lds.unlock();

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		float *pfmean;
		float effectiveP;

		// handle info
		infoFlags = lds.unpackInt32();
		if ( infoFlags != (DDBPFINFO_MEAN | DDBPFINFO_EFFECTIVE_NUM_PARTICLES) ) {
			lds.unlock();
			return 0; // what happened?
		}

		pfmean = (float *)lds.unpackData( sizeof(float)*AVATAR_PFSTATE_SIZE);
		effectiveP = lds.unpackFloat32(); // effective num particles

		Log.log( 0, "ExecutiveOfflineSLAM::convRequestPFInfo: avatar %s %d.%03d true pose %f %f %f pf mean %f %f %f effective pNum %f",
			Log.formatUUID( 0, &avatarId ), (int)tb.time, (int)tb.millitm,
			x, y, r,
			pfmean[0], pfmean[1], pfmean[2],
			effectiveP );
	}

	lds.unlock();

	return 0;
}



//-----------------------------------------------------------------------------
// State functions

int ExecutiveOfflineSLAM::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int ExecutiveOfflineSLAM::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	ExecutiveOfflineSLAM::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(ExecutiveOfflineSLAM);

	return AgentBase::writeState( ds, false );;
}

int	ExecutiveOfflineSLAM::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(ExecutiveOfflineSLAM);

	return AgentBase::readState( ds, false );
}

int ExecutiveOfflineSLAM::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	return 0;
}

int ExecutiveOfflineSLAM::writeBackup( DataStream *ds ) {

	return AgentBase::writeBackup( ds );
}

int ExecutiveOfflineSLAM::readBackup( DataStream *ds ) {

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	ExecutiveOfflineSLAM *agent = (ExecutiveOfflineSLAM *)vpAgent;

	if ( agent->configure() ) {
		delete agent;
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();
	delete agent;

	return 0;
}

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	ExecutiveOfflineSLAM *agent = new ExecutiveOfflineSLAM( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	ExecutiveOfflineSLAM *agent = new ExecutiveOfflineSLAM( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CExecutiveOfflineSLAMDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CExecutiveOfflineSLAMDLL, CWinApp)
END_MESSAGE_MAP()

CExecutiveOfflineSLAMDLL::CExecutiveOfflineSLAMDLL() {}

// The one and only CExecutiveOfflineSLAMDLL object
CExecutiveOfflineSLAMDLL theApp;

int CExecutiveOfflineSLAMDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CExecutiveOfflineSLAMDLL ---\n"));
  return CWinApp::ExitInstance();
}