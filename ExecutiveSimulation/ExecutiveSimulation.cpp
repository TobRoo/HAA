// ExecutiveSimulation.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "ExecutiveSimulation.h"
#include "ExecutiveSimulationVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define LOG_TRUE_POSITIONS 1000 // every second

//*****************************************************************************
// ExecutiveSimulation

//-----------------------------------------------------------------------------
// Constructor	
ExecutiveSimulation::ExecutiveSimulation( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	int i;

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "ExecutiveSimulation" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(ExecutiveSimulation_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	this->nextPathId = 0;
	for ( i=0; i<MAX_PATHS; i++ ) {
		this->paths[i].nodes = NULL;
	}

	this->nextObjectId = 0;
	this->highObjectId = 0;
	for ( i=0; i<MAX_OBJECTS; i++ ) {
		this->objects[i].path_refs = NULL;
	}

	this->totalSimSteps = 0;

	// Prepare callbacks
	this->callback[ExecutiveSimulation_CBR_cbSimStep] = NEW_MEMBER_CB(ExecutiveSimulation,cbSimStep);
	this->callback[ExecutiveSimulation_CBR_cbLogPose] = NEW_MEMBER_CB(ExecutiveSimulation,cbLogPose);
}

//-----------------------------------------------------------------------------
// Destructor
ExecutiveSimulation::~ExecutiveSimulation() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}


}

//-----------------------------------------------------------------------------
// Configure

int ExecutiveSimulation::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\ExecutiveSimulation %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );



		Log.log( 0, "ExecutiveSimulation %.2d.%.2d.%.5d.%.2d", ExecutiveSimulation_MAJOR, ExecutiveSimulation_MINOR, ExecutiveSimulation_BUILDNO, ExecutiveSimulation_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

int ExecutiveSimulation::parseMF_HandleLandmarkFile( char *fileName ) { 
	Log.log(LOG_LEVEL_NORMAL, "ExecutiveSimulation::parseMF_HandleLandmarkFile:DEBUG start");
	this->parseLandmarkFile( fileName );

	return 0; 
}

int ExecutiveSimulation::parseMF_HandlePathFile( char *fileName ) { 

	this->loadPathFile( fileName );

	return 0; 
}

//-----------------------------------------------------------------------------
// Start

int ExecutiveSimulation::start( char *missionFile ) {
#ifdef	NO_LOGGING
	Log.setLogMode(LOG_MODE_OFF);
	Log.setLogLevel(LOG_LEVEL_NONE);
#endif
	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	UUID id = this->addTimeout( SIMULATION_STEP, ExecutiveSimulation_CBR_cbSimStep );
	if ( id == nilUUID ) {
		Log.log( 0, "ExecutiveSimulation::start: addTimeout failed" );
		return 1;
	}

#ifdef LOG_TRUE_POSITIONS
	UUID ltp = this->addTimeout( LOG_TRUE_POSITIONS, ExecutiveSimulation_CBR_cbLogPose );
	if ( ltp == nilUUID ) {
		Log.log( 0, "ExecutiveSimulation::start: addTimeout failed" );
		return 1;
	}
#endif

	apb->apb_ftime_s( &this->simTime );

	// set up basic paths
	float linePathX[2] = { 0.0f, 1.0f };
	float linePathY[2] = { 0.0f, 0.0f } ;
	this->pathIdLine = this->newPath( 2, linePathX, linePathY );
	float xPathX[5] = { -0.5f, 0.5f, 0.0f, -0.5f, 0.5f };
	float xPathY[5] = { -0.5f, 0.5f, 0.0f, 0.5f, -0.5f };
	this->pathIdX = this->newPath( 5, xPathX, xPathY );
	float robotPathX[4] = { -0.08f, 0.12f, -0.08f, -0.08f };
	float robotPathY[4] = { 0.08f, 0.0f, -0.08f, 0.08f } ;
	this->pathIdRobot = this->newPath( 4, robotPathX, robotPathY );

	STATE(AgentBase)->started = true;

	std::list<AVATAR_HOLD>::iterator iA;
	for ( iA = this->heldAvatars.begin(); iA != this->heldAvatars.end(); iA++ ) {
		this->addAvatar( &iA->uuid, &iA->owner, iA->x, iA->y, iA->r, iA->avatarFile, iA->landmarkCodeOffset );
	}
	this->heldAvatars.clear();

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int ExecutiveSimulation::stop() {
	int i;

	// remove landmarks
	while ( this->landmarks.size() ) {
		this->removeLandmark( this->landmarks.front() );
	}

	// free avatars
	while ( this->avatars.size() ) {
		this->removeAvatar( (UUID *)&this->avatars.begin()->first, 1 );
	}

	for ( i=0; i<MAX_OBJECTS; i++ ) {
		if ( this->objects[i].path_refs != NULL )
			this->deleteObject( i );
	}

	for ( i=0; i<MAX_PATHS; i++ ) {
		if ( this->paths[i].nodes != NULL )
			this->deletePath( i );
	}

	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int ExecutiveSimulation::step() {
	return AgentBase::step();
}

int ExecutiveSimulation::parseLandmarkFile( char *filename ) {
	FILE *landmarkF;
	char name[256];
	char *ch;
	int id;
	UUID uuid;
	float x, y, height, elevation;
	int estimatedPos;
	ITEM_TYPES landmarkType;

	UUID nilId;
	UuidCreateNil( &nilId );

	int err = 0;

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveSimulation::parseLandmarkFile: parsing file %s", filename );

	// open file
	if ( fopen_s( &landmarkF, filename, "r" ) ) {
		Log.log( 0, "ExecutiveSimulation::parseLandmarkFile: failed to open %s", filename );
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
			Log.log( 0, "ExecutiveSimulation::parseLandmarkFile: expected id=<int>, check format (landmark %s)", name );
			err = 1;
			break;
		}
		if ( 5 != fscanf_s( landmarkF, "pose=%f %f %f %f %d\n", &x, &y, &height, &elevation, &estimatedPos ) ) {
			Log.log( 0, "ExecutiveSimulation::parseLandmarkFile: expected pose=<float> <float> <float> <float> <int>, check format (landmark %s)", name );
			err = 1;
			break;
		}
		if (1 != fscanf_s(landmarkF, "landmark_type=%d\n", &landmarkType)) {
			Log.log(0, "ExecutiveMission::parseLandmarkFile: expected landmark_type=<int>, check format (landmark %s)", name);
			err = 1;
			break;
		}
		// create uuid
		apb->apbUuidCreate( &uuid );

		// add locally
		SimLandmark *landmark = (SimLandmark *)malloc(sizeof(SimLandmark));
		if ( !landmark ) {
			err = 1;
			break;
		}

		landmark->id = uuid;
		UuidCreateNil( &landmark->owner );
		landmark->code = id;
		landmark->wx = landmark->x = x;
		landmark->wy = landmark->y = y;
		landmark->height = height;
		landmark->elevation = elevation;
		landmark->collected = false;

		this->addLandmark( landmark );

		Log.log( LOG_LEVEL_NORMAL, "ExecutiveSimulation::parseLandmarkFile: added landmark %d (%s): %f %f %f %f", id, name, x, y, height, elevation );
	}

	fclose( landmarkF );

	return err;
}

int ExecutiveSimulation::uploadSimSteps()
{
	DataStream lds;

	// notify host
	lds.reset();
	lds.packUInt64(this->totalSimSteps); 
	this->sendMessage(this->hostCon, MSG_DDB_SIMSTEPS, lds.stream(), lds.length());
	lds.unlock();

	return 0;
}

int ExecutiveSimulation::newPath( int count, float *x, float *y ) {
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

int ExecutiveSimulation::deletePath( int id ) {

	if ( id < this->nextPathId )
		this->nextPathId = id;

	std::list<NODE *>::iterator iter = this->paths[id].nodes->begin();
	while ( iter != this->paths[id].nodes->end() ) {
		free ( *iter );
		iter++;
	}
	delete this->paths[id].nodes;

	this->paths[id].references = 0;
	this->paths[id].nodes = NULL;

	// vis
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packInt32( this->nextPathId );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_REMPATH, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int ExecutiveSimulation::loadPathFile( char *fileN ) {
	FILE *file;
	char cBuf[1024];
	int scanned, points, id, obj;
	float x[65], y[65];
	float poseX, poseY, poseR, poseS;
	float width, colour[3];
	int solid;
	float *colourPtr[1] = {colour};

	Log.log( LOG_LEVEL_NORMAL, "ExecutiveSimulation::loadPathFile: parsing file %s", fileN );

	
	if ( fopen_s( &file, fileN, "r" ) ) {
		Log.log( LOG_LEVEL_NORMAL, "ExecutiveSimulation::loadPathFile: couldn't open file" );
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

int ExecutiveSimulation::newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	float *ptr = (float *)malloc( sizeof(float)*4 );
	if ( ptr == NULL )
		return -1;

	ptr[0] = x;
	ptr[1] = y;
	ptr[2] = r;
	ptr[3] = s;

	return createObject( ptr, ptr+1, ptr+2, ptr+3, count, paths, colours, lineWidths, false, solid, name );
}

int ExecutiveSimulation::createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name ) {
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

int ExecutiveSimulation::deleteObject( int id ) {

	if ( id < this->nextObjectId )
		this->nextObjectId = id;

	if ( id == this->highObjectId ) {
		do {
			this->highObjectId--;
		} while ( this->highObjectId > 0 && this->objects[this->highObjectId].path_refs == NULL );
	}

	if ( !this->objects[id].dynamic ) {
		free( this->objects[id].x );
	}
	
	std::list<PATH_REFERENCE *>::iterator iter = this->objects[id].path_refs->begin();
	while ( iter != this->objects[id].path_refs->end() ) {
		this->paths[(*iter)->id].references--;

		//if ( !keepPath && this->paths[(*iter)->id].references == 0 )
		//	deletePath( (*iter)->id );

		free( *iter );

		iter++;
	}
	delete this->objects[id].path_refs;

	this->objects[id].path_refs = NULL;

	// vis
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packInt32( id );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_REMOBJECT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}


int ExecutiveSimulation::updateStaticObject( int id, float x, float y, float r, float s ) {

	*this->objects[id].x = x;
	*this->objects[id].y = y;
	*this->objects[id].r = r;
	*this->objects[id].s = s;

	// vis
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packInt32( id );
	this->ds.packFloat32( x );
	this->ds.packFloat32( y );
	this->ds.packFloat32( r );
	this->ds.packFloat32( s );
	this->sendMessage( this->hostCon, MSG_DDB_VIS_UPDATEOBJECT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int ExecutiveSimulation::showObject( int id ) { 
	if ( id >= MAX_OBJECTS )
		return 1;

	if ( !this->objects[id].visible ) {
		this->objects[id].visible = true;

		// vis
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( id );
		this->ds.packChar( 1 );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_SETOBJECTVISIBLE, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 0;
}

int ExecutiveSimulation::hideObject( int id ) { 
	if ( id >= MAX_OBJECTS )
		return 1;

	if ( this->objects[id].visible ) {
		this->objects[id].visible = false;

		// vis
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packInt32( id );
		this->ds.packChar( 0 );
		this->sendMessage( this->hostCon, MSG_DDB_VIS_SETOBJECTVISIBLE, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}

	return 0;
}

int ExecutiveSimulation::addAvatar( UUID *uuid, UUID *owner, float x, float y, float r, char *avatarFile, int landmarkCodeOffset ) {
	SimAvatar *avatar;

	this->avatarOwners[*uuid] = *owner;

	if ( !STATE(AgentBase)->started ) {
		AVATAR_HOLD hold;
		hold.uuid = *uuid;
		hold.owner = *owner;
		hold.x = x;
		hold.y = y;
		hold.r = r;
		strcpy_s( hold.avatarFile, MAX_PATH, avatarFile );
		hold.landmarkCodeOffset = landmarkCodeOffset;

		this->heldAvatars.push_back( hold );

		return 0;
	}

	mapSimAvatar::iterator iA = this->avatars.find( *uuid );
	if ( iA != this->avatars.end() ) {
		Log.log( 0, "ExecutiveSimulation::addAvatar: avatar %s already exists", Log.formatUUID( 0, uuid ) );
		return 1;
	}

	avatar = new SimAvatar( uuid, owner, x, y, r, &this->simTime, avatarFile, landmarkCodeOffset, this, &this->Log, this->apb );

	this->avatars[*uuid] = avatar;

	return 0;
}

int ExecutiveSimulation::removeAvatar( UUID *uuid, char retireMode ) {

	mapSimAvatar::iterator iSA;
	iSA = this->avatars.find( *uuid );
	if ( iSA == this->avatars.end() ) 
		return 1;

	if ( retireMode == 1 ) { // clean retire
		delete iSA->second;

		this->avatars.erase( *uuid );
	} else { // crash
		iSA->second->crash();
	}

	return 0;
}

int ExecutiveSimulation::getSonarPathId( float alpha ) {
	int i;
	float a, da;
	int pathId;
	float pathX[7], pathY[7];

	// see if we have a path already
	std::map<float,int>::iterator it = this->pathIdSonar.find( alpha );
	if ( it != this->pathIdSonar.end() ) {
		return it->second; // found it
	}

	// create a new path
	pathX[0] = 0;
	pathY[0] = 0;
	a = alpha;
	da = -2*alpha/4;
	for ( i=1; i<6; i++ ) {
		pathX[i] = cos(a);
		pathY[i] = sin(a);
		a += da;
	}
	pathX[6] = 0;
	pathY[6] = 0;

	pathId = this->newPath( 7, pathX, pathY );
	
	this->pathIdSonar[alpha] = pathId;

	return pathId;
}

int ExecutiveSimulation::addLandmark( SimLandmark *landmark ) {
	// set the code
	//landmark->code = this->nextLandmarkCode;
	//this->nextLandmarkCode++;

	// add to our landmark list
	this->landmarks.push_back( landmark );

	// add to DDB
//	this->ddbAddLandmark( &landmark->id, landmark->code, &landmark->owner, 0, 0, landmark->x, landmark->y );

	// add to DDB
/*	this->ds.reset();
	this->ds.packUUID( &landmark->id );
	this->ds.packChar( (char)landmark->code );
	this->ds.packUUID( &landmark->owner );
	this->ds.packFloat32( landmark->height ); 
	this->ds.packFloat32( landmark->elevation ); 
	this->ds.packFloat32( landmark->x ); 
	this->ds.packFloat32( landmark->y ); 
	this->ds.packChar( 0 );
	this->sendMessage( this->hostCon, MSG_DDB_ADDLANDMARK, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
*/
	return 0;
}

int ExecutiveSimulation::removeLandmark( SimLandmark *landmark ) {
	RPC_STATUS Status;

	std::list<SimLandmark*>::iterator itSL;
	for ( itSL = this->landmarks.begin(); itSL != this->landmarks.end(); itSL++ ) {
		if ( (*itSL)->code == landmark->code ) {
			if ( UuidIsNil( &(*itSL)->owner, &Status ) ) {
				free( *itSL );
			}
			this->landmarks.erase( itSL );
			break;
		}
	}

//	this->ddbRemoveLandmark( &landmark->id );

	return 0;
}

// pass in id -1 to get the first object
int ExecutiveSimulation::getNextObject( OBJECT **object, int after ) {
	int id = after + 1;

	while ( id <= this->highObjectId && this->objects[id].path_refs == NULL )
		id++;

	if ( id > this->highObjectId )
		return -1; // no objects?

	*object = &this->objects[id];

	return id;
}

int ExecutiveSimulation::getPath( int id, PATH **path ) {
	
	if ( id > MAX_PATHS || this->paths[id].nodes == NULL )
		return 1; // bad id

	*path = &this->paths[id];

	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int ExecutiveSimulation::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case MSG__DATADUMP_RAVATARPOSES:
		{
			UUID sender = *(UUID *)data;
			// send back all avatar poses
			mapSimAvatar::iterator iA;
			SimAvatarState *state;
			lds.reset();
			for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
				state = iA->second->getState();
				lds.packBool( true );
				lds.packUUID( (UUID *)&iA->first );
				lds.packData( &state->t, sizeof(_timeb) );
				lds.packFloat32( state->x );
				lds.packFloat32( state->y );
				lds.packFloat32( state->r );
			}
			lds.packBool( false );
			this->delayMessage( 5000, this->hostCon, MSG__DATADUMP_AVATARPOSES, lds.stream(), lds.length(), &sender ); // delay message to give particle filters a chance to catch up
			lds.unlock();
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_ADDAVATAR:
		{
			UUID owner;
			float x, y, r;
			char *avatarFile;
			int landmarkCodeOffset;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &owner );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			avatarFile = lds.unpackString();
			landmarkCodeOffset = lds.unpackChar();
			this->addAvatar( &uuid, &owner, x, y, r, avatarFile, landmarkCodeOffset );
			lds.unlock();
		}
		break;	
	case ExecutiveSimulation_MSGS::MSG_REMAVATAR:
		{
			char retireMode;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			retireMode = lds.unpackChar();
			this->removeAvatar( &uuid, retireMode );
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_MOVE_LINEAR:
		{
			float move;
			char moveId;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			move = lds.unpackFloat32();
			moveId = lds.unpackChar();
			lds.unlock();

			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA != this->avatars.end() ) {
				iA->second->doLinearMove( move, moveId ); // move
			}
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_MOVE_ANGULAR:
		{
			float rotation;
			char moveId;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			rotation = lds.unpackFloat32();
			moveId = lds.unpackChar();
			lds.unlock();

			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA != this->avatars.end() ) {
				iA->second->doAngularMove( rotation, moveId ); // rotate
			}
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_STOP:
		{
			char moveId;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			moveId = lds.unpackChar();
			lds.unlock();
			
			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA != this->avatars.end() ) {
				iA->second->doStop( moveId ); // stop
			}
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_IMAGE:
		{
			int cameraInd;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			cameraInd = lds.unpackInt32();
			lds.unlock();
			
			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA != this->avatars.end() ) {
				iA->second->doCamera( cameraInd ); // take image
			}
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_COLLECT_LANDMARK:
		{
			unsigned char code;
			float x, y;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			code = lds.unpackUChar();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			lds.unpackUUID( &thread );
			lds.unlock();
			
			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA != this->avatars.end() ) {
				iA->second->doCollectLandmark( code, x, y, &thread ); // collect landmark
			}
		}
		break;
	case ExecutiveSimulation_MSGS::MSG_AVATAR_DEPOSIT_LANDMARK:
	{
		unsigned char code;
		float x, y;
		UUID thread, initiator;
		lds.setData(data, len);
		lds.unpackUUID(&uuid);
		code = lds.unpackUChar();
		x = lds.unpackFloat32();
		y = lds.unpackFloat32();
		lds.unpackUUID(&thread);
		lds.unpackUUID(&initiator);
		lds.unlock();

		// find avatar
		mapSimAvatar::iterator iA;
		iA = this->avatars.find(uuid);
		if (iA != this->avatars.end()) {
			iA->second->doDepositLandmark(code, x, y, &thread, &initiator); // deposit landmark
		}
	}
	break;

	case ExecutiveSimulation_MSGS::MSG_RAVATAR_OUTPUT:
		{
			UUID thread, owner;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &thread );
			lds.unlock();

			lds.reset();
			lds.packUUID( &thread );
			
			// find avatar
			mapSimAvatar::iterator iA;
			iA = this->avatars.find( uuid );
			if ( iA == this->avatars.end() ) {
				lds.packChar( 1 ); // failure
				std::map<UUID,UUID,UUIDless>::iterator iO = this->avatarOwners.find( uuid );
				if ( iO == this->avatarOwners.end() ) 
					break; // don't know anything!

				owner = iO->second;
			} else {
				lds.packChar( 0 ); // success
				iA->second->appendOutput( &lds );

				owner = iA->second->ownerId;
			}

			// reply
		//	Log.log(0, "ExecutiveSimulation_MSGS::MSG_RAVATAR_OUTPUT:: conversation thread id: %s", Log.formatUUID(0, &thread));
			this->sendMessage( this->hostCon, MSG_RESPONSE, lds.stream(), lds.length(), &owner );
#ifdef LOG_RESPONSES
			Log.log(LOG_LEVEL_NORMAL, "RESPONSE: Sending message from agent %s to agent %s in conversation %s", Log.formatUUID(0, this->getUUID()), Log.formatUUID(0, &owner), Log.formatUUID(0, &thread));
#endif
			lds.unlock();
		}
		break;
	case MSG_MISSION_DONE:
	{
		Log.log(0, " ExecutiveSimulation::conProcessMessage: mission done, uploading total sim step count.");
		this->uploadSimSteps();
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool ExecutiveSimulation::cbSimStep( void *NA ) {
	int dt;
	_timeb lastTime;
	this->totalSimSteps++;
	//Log.log( 0, "ExecutiveSimulation::cbSimStep: step!" );

	if (this->totalSimSteps == MAX_ITERATION_COUNT)	//Maximum number of iterations allowed
	{
		DataStream lds;

		Log.log(0, "ExecutiveSimulation::missionDone: mission ran out of time!");
		// notify host
		lds.reset();
		lds.packChar(0); // failure, did not complete in time
		this->sendMessage(this->hostCon, MSG_MISSION_DONE, lds.stream(), lds.length());
		lds.unlock();
	}


	lastTime = this->simTime;
	apb->apb_ftime_s( &this->simTime );

	dt = (int)((this->simTime.time - lastTime.time)*1000 + this->simTime.millitm - lastTime.millitm);
	if ( dt == 0 )
		return 1; // repeat

	if ( dt > 100 )
		Log.log( 0, "ExecutiveSimulation::cbSimStep: dt is too large %d", dt );

	mapSimAvatar::iterator iA;
	// PreStep
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		iA->second->SimPreStep( &this->simTime, dt );
	}

	// Step
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		iA->second->SimStep( &this->simTime, dt );
	}

	return 1; // repeat
}


bool ExecutiveSimulation::cbLogPose( void *NA ) {
	Log.log( 0, "ExecutiveSimulation::cbLogPose: log pose (%d):", this->avatars.size() );

	mapSimAvatar::iterator iA;
	for ( iA = this->avatars.begin(); iA != this->avatars.end(); iA++ ) {
		SimAvatarState *state = iA->second->getState();
		Log.log( 0, "%s %d.%03d %f %f %f %f %f", 
			Log.formatUUID( 0, (UUID *)&iA->first ),
			(int)state->t.time, (int)state->t.millitm, state->x, state->y, state->r, state->vL, state->vR );
	}

	return 1; // repeat
}

//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	ExecutiveSimulation *agent = (ExecutiveSimulation *)vpAgent;

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
	ExecutiveSimulation *agent = new ExecutiveSimulation( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	ExecutiveSimulation *agent = new ExecutiveSimulation( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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


// CExecutiveSimulationDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CExecutiveSimulationDLL, CWinApp)
END_MESSAGE_MAP()

CExecutiveSimulationDLL::CExecutiveSimulationDLL() {}

// The one and only CExecutiveSimulationDLL object
CExecutiveSimulationDLL theApp;

int CExecutiveSimulationDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CExecutiveSimulationDLL ---\n"));
  return CWinApp::ExitInstance();
}

//*****************************************************************************
// SimAvatar

//-----------------------------------------------------------------------------
// Constructor

SimAvatar::SimAvatar( UUID *uuid, UUID *owner, float x, float y, float r, _timeb *t, char *avatarFile, int landmarkCodeOffset, ExecutiveSimulation *sim, Logger *logger, AgentPlayback *apb ) {

	this->uuid = *uuid;
	this->ownerId = *owner;

	this->sim = sim;
	this->Log = logger;
	this->apb = apb;

	this->crashed = false;

	this->state.t = *t;
	this->state.x = x;
	this->state.y = y;
	this->state.r = r;
	this->state.vL = 0;
	this->state.vR = 0;

	this->stateEst = this->lastReportedStateEst = this->state;

	this->accCmdL = this->accCmdR = 0;

	this->moveInProgress = false;
	this->moveTargetL = 0;
	this->moveTargetA = 0;
	this->moveCurrent = 0;

	this->landmarkCodeOffset = landmarkCodeOffset;

	this->parseConfigFile( avatarFile );

	this->output.reset();

	// vis
	this->sinceLastVisUpdate = 0;

	this->extraVisible = true;

	float lineWidths = 1, colour[3];
	float *colourPtr[1] = {colour};
	int pathId = sim->getRobotPathId();
	colour[0] = 0;
	colour[1] = 0.6f;
	colour[2] = 0;
	this->objIdTrue = sim->newStaticObject( this->state.x, this->state.y, this->state.r, 1, 1, &pathId, colourPtr, &lineWidths, true, "SimAvatar" );
	
	colour[0] = 0.6f;
	colour[1] = 0;
	colour[2] = 0;
	this->objIdEst = sim->newStaticObject( this->state.x, this->state.y, this->state.r, 1, 1, &pathId, colourPtr, &lineWidths, false, "SimAvatar" );

}

//-----------------------------------------------------------------------------
// Destructor

SimAvatar::~SimAvatar() {

	// vis
	sim->deleteObject( this->objIdTrue );
	sim->deleteObject( this->objIdEst );

	// clean up landmarks
	std::list<SimLandmark>::iterator iL;
	for ( iL = this->landmarks.begin(); iL != this->landmarks.end(); iL++ ) {
		sim->removeLandmark( &*iL );
	}

	// clean up sonar
	std::list<SimSonar>::iterator iS;
	for ( iS = this->sonars.begin(); iS != this->sonars.end(); iS++ ) {
		sim->deleteObject( iS->objId );
	}

	// clean up cameras
	int i;		
	std::list<SimCamera>::iterator iC;
	for ( iC = this->cameras.begin(); iC != this->cameras.end(); iC++ ) {
		for ( i=0; i<10; i++ ) {
			if ( iC->objId[i] != -1 ) sim->deleteObject( iC->objId[i] );
			if ( iC->ffobjId[i] != -1 ) sim->deleteObject( iC->ffobjId[i] );
		}
	}

}

int SimAvatar::SimPreStep( _timeb *simTime, int dt ) {

	if ( this->crashed )
		return 0; // don't do anything

	float fdt;
	float sn, cs;
	float dL, dA;

	fdt = dt*0.001f;

	// update state
	dL = fdt*(this->state.vL + this->state.vR)/2;
	dA = fdt*(this->state.vR - this->state.vL)/this->wheelBase;

	dL = this->moveTargetL;
	dA = this->moveTargetA;

	sn = sin( this->state.r );
	cs = cos( this->state.r );
	this->state.t = *simTime;

	//Quick fix - lock the avatars to mission region

	if (this->state.x + dL*cs < 0.0f || this->state.x + dL*cs > 10.0f || this->state.y + dL*sn < 0.0f || this->state.y + dL*sn > 10.0f)
		;
		//dL = 0.0f;
	else
	{
		this->state.x += dL*cs;
		this->state.y += dL*sn;
	}
	this->state.r += dA;

	//Quick fix - lock the avatars to mission region

	//if (this->state.x < 0.0f)
	//	this->state.x = 0.05f;
	//if (this->state.x > 10.0f)
	//	this->state.x = 9.95f;
	//if (this->state.y < 0.0f)
	//	this->state.y = 0.05f;
	//if (this->state.y < 10.0f)
	//	this->state.y = 9.95f;


	// update landmarks
	sn = sin( this->state.r );
	cs = cos( this->state.r );
	std::list<SimLandmark>::iterator itL;
	for ( itL = this->landmarks.begin(); itL != this->landmarks.end(); itL++ ) {
		(*itL).wx = this->state.x + (*itL).x*cs - (*itL).y*sn;
		(*itL).wy = this->state.y + (*itL).x*sn + (*itL).y*cs;
	}

	return 0;
}

int SimAvatar::SimStep( _timeb *simTime, int dt ) {
	float sn, cs;
	float fdt, dL, dA;
	float vel, acc;
	float accCmdL, accCmdR;

	if ( this->crashed )
		return 0; // don't do anything

	fdt = dt*0.001f;

	// update state estimate
	dL = fdt*(this->stateEst.vL + this->stateEst.vR)/2;
	dA = fdt*(this->stateEst.vR - this->stateEst.vL)/this->wheelBaseEst;

	dL = this->moveTargetL;
	dA = this->moveTargetA;

	sn = sin( this->stateEst.r );
	cs = cos( this->stateEst.r );
	this->stateEst.t = *simTime;

	//Quick fix - lock the avatars to mission region
	if (this->stateEst.x + dL*cs < 0.0f || this->stateEst.x + dL*cs > 10.0f || this->stateEst.y + dL*sn < 0.0f || this->stateEst.y + dL*sn > 10.0f)
		this->finishMove();
		//dL = 0.0f;
	else
	{
		this->stateEst.x += dL*cs;
		this->stateEst.y += dL*sn;
	}
	this->stateEst.r += dA;

	// handle motion command
	accCmdL = accCmdR = 0;
	if ( this->moveInProgress ) { // do move
		if ( this->moveTargetL ) { // linear move
			this->moveCurrent += dL;

			// check if we're done
			if ( (this->moveTargetL - this->moveCurrent)/this->moveTargetL <= 0 
			  && this->stateEst.vL == 0 && this->stateEst.vR == 0 ) { // finished
				this->finishMove();
			} else {
				vel = (this->stateEst.vL + this->stateEst.vR)/2;
				acc = this->accelerationEst;
				// check if we need to start decelerating
				if ( fabs(vel/acc * vel/2) >= max( 0, (this->moveTargetL - this->moveCurrent)*(this->moveTargetL > 0 ? 1 : -1) ) ) { // slow down
					if ( this->moveTargetL < 0 ) {
						this->accCmdL = min( -(this->stateEst.vL/fdt)/this->accelerationEst, 1 );
						this->accCmdR = min( -(this->stateEst.vR/fdt)/this->accelerationEst, 1 );					
					} else {
						this->accCmdL = max( -(this->stateEst.vL/fdt)/this->accelerationEst, -1 );
						this->accCmdR = max( -(this->stateEst.vR/fdt)/this->accelerationEst, -1 );
					}						
				} else { // might as well keep accelerating
					if ( this->moveTargetL < 0 ) {
						this->accCmdL = -1;
						this->accCmdR = -1;					
					} else {
						this->accCmdL = 1;
						this->accCmdR = 1;
					}
				}
			}
		} else if ( this->moveTargetA ) { // angular move

			this->moveCurrent += dA;

			// check if we're done
			if ( (this->moveTargetA - this->moveCurrent)/this->moveTargetA <= 0
			  && this->stateEst.vL == 0 && this->stateEst.vR == 0 ) { // finished
				this->finishMove();
			} else {
				vel = (this->stateEst.vR - this->stateEst.vL)/this->wheelBaseEst;
				acc = 2*this->accelerationEst/this->wheelBaseEst;
				// check if we need to start decelerating (put in a fudge factor of 0.5, otherwise we tend to overshoot a lot?)
				if ( fabs(vel/acc * vel/2) >= 0.5f*max( 0, (this->moveTargetA - this->moveCurrent)*(this->moveTargetA > 0 ? 1 : -1) ) ) { // slow down
					if ( this->moveTargetA < 0 ) {
						this->accCmdL = max( -(this->stateEst.vL/fdt)/this->accelerationEst, -1 );
						this->accCmdR = min( -(this->stateEst.vR/fdt)/this->accelerationEst, 1 );					
					} else {
						this->accCmdL = min( -(this->stateEst.vL/fdt)/this->accelerationEst, 1 );
						this->accCmdR = max( -(this->stateEst.vR/fdt)/this->accelerationEst, -1 );
					}						
				} else { // might as well keep accelerating
					if ( this->moveTargetA < 0 ) {
						this->accCmdL = 1;
						this->accCmdR = -1;					
					} else {
						this->accCmdL = -1;
						this->accCmdR = 1;
					}
				}
			}
		} else { // stop
			// check if we're done
			if ( this->stateEst.vL == 0 && this->stateEst.vR == 0 ) { // finished
				this->finishMove();
			} else {
				if ( this->stateEst.vL < 0 ) 
					this->accCmdL = min( -(this->stateEst.vL/fdt)/this->accelerationEst, 1 );
				else if ( this->stateEst.vL > 0 )
					this->accCmdL = max( -(this->stateEst.vL/fdt)/this->accelerationEst, -1 );

				if ( this->stateEst.vR < 0 ) 
					this->accCmdR = min( -(this->stateEst.vR/fdt)/this->accelerationEst, 1 );
				else if ( this->stateEst.vR > 0 )
					this->accCmdR = max( -(this->stateEst.vR/fdt)/this->accelerationEst, -1 );
			}
		}
	}
	
	if ( !this->moveInProgress ) { // make sure we're stopped
		if ( this->stateEst.vL < 0 ) 
			this->accCmdL = min( -(this->stateEst.vL/fdt)/this->accelerationEst, 1 );
		else if ( this->stateEst.vL > 0 )
			this->accCmdL = max( -(this->stateEst.vL/fdt)/this->accelerationEst, -1 );

		if ( this->stateEst.vR < 0 ) 
			this->accCmdR = min( -(this->stateEst.vR/fdt)/this->accelerationEst, 1 );
		else if ( this->stateEst.vR > 0 )
			this->accCmdR = max( -(this->stateEst.vR/fdt)/this->accelerationEst, -1 );
	}
	
	// update velocity
	this->state.vL += fdt*this->accelerationL*this->accCmdL;
	if ( this->state.vL > this->maxVelocityL ) this->state.vL = this->maxVelocityL;
	if ( this->state.vL < -this->maxVelocityL ) this->state.vL = -this->maxVelocityL;
	this->state.vR += fdt*this->accelerationR*this->accCmdR;
	if ( this->state.vR > this->maxVelocityR ) this->state.vR = this->maxVelocityR;
	if ( this->state.vR < -this->maxVelocityR ) this->state.vR = -this->maxVelocityR;

	// update velocity estimate
	this->stateEst.vL += fdt*this->accelerationEst*this->accCmdL;
	if ( this->stateEst.vL > this->maxVelocityEst ) this->stateEst.vL = this->maxVelocityEst;
	if ( this->stateEst.vL < -this->maxVelocityEst ) this->stateEst.vL = -this->maxVelocityEst;
	this->stateEst.vR += fdt*this->accelerationEst*this->accCmdR;
	if ( this->stateEst.vR > this->maxVelocityEst ) this->stateEst.vR = this->maxVelocityEst;
	if ( this->stateEst.vR < -this->maxVelocityEst ) this->stateEst.vR = -this->maxVelocityEst;



	if (dt > 100) { //Likely server crash or similar, set estimate state to true state to avoid extreme divergence (quick fix)
		stateEst = state;
	}

	#ifdef NO_RANDOM_ERROR
		stateEst = state;
	#endif


	//verify that we are within bounds (quick fix)

	if (this->stateEst.x < 0.0f)
		this->stateEst.x = 0.1f;
	if (this->stateEst.x > 10.0f)
		this->stateEst.x = 9.9f;
	if (this->stateEst.y < 0.0f)
		this->stateEst.y = 0.1f;
	if (this->stateEst.y > 10.0f)
		this->stateEst.y = 9.9f;
	if (this->state.x < 0.0f)
		this->state.x = 0.1f;
	if (this->state.x > 10.0f)
		this->state.x = 9.9f;
	if (this->state.y < 0.0f)
		this->state.y = 0.1f;
	if (this->state.y > 10.0f)
		this->state.y = 9.9f;


	// check sensors
	std::list<SimSonar>::iterator itSS;
	for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
		if ( this->state.t.time > itSS->nextT.time || (this->state.t.time == itSS->nextT.time && this->state.t.millitm >= itSS->nextT.millitm) ) {
			// take reading
			this->doSonar( &(*itSS) );

			do { 
				itSS->nextT.time += (itSS->nextT.millitm + itSS->period)/1000;
				itSS->nextT.millitm = (itSS->nextT.millitm + itSS->period) % 1000;
			} while ( this->state.t.time > itSS->nextT.time || (this->state.t.time == itSS->nextT.time && this->state.t.millitm >= itSS->nextT.millitm) );
		}
	}
	std::list<SimCamera>::iterator itSC;
	for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
		if ( itSC->period == 0 )
			continue; // manual only
		
		if ( this->state.t.time > itSC->nextT.time || (this->state.t.time == itSC->nextT.time && this->state.t.millitm >= itSC->nextT.millitm) ) {
			// take reading
			this->doCamera( &(*itSC) );

			do { 
				itSC->nextT.time += (itSC->nextT.millitm + itSC->period)/1000;
				itSC->nextT.millitm = (itSC->nextT.millitm + itSC->period) % 1000;
			} while ( this->state.t.time > itSC->nextT.time || (this->state.t.time == itSC->nextT.time && this->state.t.millitm >= itSC->nextT.millitm) );
		}
	}

	// update vis
	this->sinceLastVisUpdate += dt;
	if ( this->sinceLastVisUpdate >= 100 ) {
		this->sinceLastVisUpdate = 0;
		sim->updateStaticObject( this->objIdTrue, this->state.x, this->state.y, this->state.r, 1 );
		sim->updateStaticObject( this->objIdEst, this->stateEst.x, this->stateEst.y, this->stateEst.r, 1 );
	}

	return 0;
}


int SimAvatar::appendOutput( DataStream *ds ) {

	// pack in pose update
	this->output.packChar( ExecutiveSimulation_Defs::SAE_POSE_UPDATE );
	this->output.packData( &state.t, sizeof(_timeb) );
	this->output.packFloat32( stateEst.x );
	this->output.packFloat32( stateEst.y );
	this->output.packFloat32( stateEst.r );
	this->output.packFloat32( (stateEst.vL + stateEst.vR)/2 ); // vLin
	this->output.packFloat32( (stateEst.vL - stateEst.vR)/this->wheelBaseEst ); // vAng

	// DEBUG
/*	static int count = 0;
	float sn, cs;
	float forwardD, tangentialD, rotationalD;
	sn = sin( this->lastReportedStateEst.r );
	cs = cos( this->lastReportedStateEst.r );
	forwardD = (this->stateEst.x - this->lastReportedStateEst.x)*cs + (this->stateEst.y - this->lastReportedStateEst.y)*sn;
	tangentialD = (this->stateEst.x - this->lastReportedStateEst.x)*-sn + (this->stateEst.y - this->lastReportedStateEst.y)*cs;
	rotationalD = this->stateEst.r - this->lastReportedStateEst.r;
	this->lastReportedStateEst = this->stateEst;
	UUID did;
	UuidFromString( (RPC_WSTR)_T("f97f01bf-9080-4678-8808-5d5ca2d47015"), &did );
	if ( this->uuid == did )
		printf( "SimAvatar::appendOutput: D %f %f %f\n", forwardD, tangentialD, rotationalD );
*/
	// end stream
	this->output.packChar( ExecutiveSimulation_Defs::SAE_STREAM_END );

	// append data
	ds->packData( this->output.stream(), this->output.length() );
	this->output.unlock();
	
	// reset for next update
	this->output.reset();

	return 0;
}

int SimAvatar::crash() {
	
	this->crashed = true;

	// clean up sonar
	std::list<SimSonar>::iterator iS;
	for ( iS = this->sonars.begin(); iS != this->sonars.end(); iS++ ) {
		sim->hideObject( iS->objId );
	}

	// clean up cameras
	int i;		
	std::list<SimCamera>::iterator iC;
	for ( iC = this->cameras.begin(); iC != this->cameras.end(); iC++ ) {
		for ( i=0; i<10; i++ ) {
			if ( iC->objId[i] != -1 ) sim->hideObject( iC->objId[i] );
			if ( iC->ffobjId[i] != -1 ) sim->hideObject( iC->ffobjId[i] );
		}
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Position/Rotation State

int SimAvatar::doLinearMove( float L, char moveId ) {			

	this->moveInProgress = true;
	this->moveTargetL = L;
	this->moveTargetA = 0;
	this->moveCurrent = 0;
	this->moveId = moveId;

	return 0;
}

int SimAvatar::doAngularMove( float A, char moveId ) {

	this->moveInProgress = true;
	this->moveTargetL = 0;
	this->moveTargetA = A;
	this->moveCurrent = 0;
	this->moveId = moveId;

	return 0;
}

int SimAvatar::doStop( char moveId ) {

	this->moveInProgress = true;
	this->moveTargetL = 0;
	this->moveTargetA = 0;
	this->moveCurrent = 0;
	this->moveId = moveId;

	return 0;
}

int SimAvatar::finishMove() {

	this->moveInProgress = false;
	this->moveTargetL = 0;
	this->moveTargetA = 0;
	this->moveCurrent = 0;

	// output
	this->output.packChar( ExecutiveSimulation_Defs::SAE_MOVE_FINISHED );
	this->output.packChar( this->moveId );

	return 0;
}

int SimAvatar::parseConfigFile( char *avatarFile ) {
	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;

	if ( fopen_s( &fp, avatarFile, "r" ) )
		return 1; // couldn't open file

	i = 0;
	while ( 1 ) {
		ch = fgetc( fp );
		
		if ( ch == EOF ) {
			break;
		} else if ( ch == '\n' ) {
			keyBuf[i] = '\0';

			if ( !strncmp( keyBuf, "[base]", 64 ) ) {
				if ( this->parseBase( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[particle filter]", 64 ) ) {
				if ( this->parseParticleFilter( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[landmark]", 64 ) ) {
				if ( this->parseLandmark( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[sonar]", 64 ) ) {
				if ( this->parseSonar( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[camera]", 64 ) ) {
				if ( this->parseCamera( fp ) )
					return 1;
			} else { // unknown key
				fclose( fp );
				return 1;
			}
			
			i = 0;
		} else {
			keyBuf[i] = ch;
			i++;
		}
	}

	fclose( fp );

	return 0;
}

int SimAvatar::parseBase( FILE *fp ) {
	float wheelBase, wheelBaseEst;
	float errL, errR, accEst;
	float maxVEst;
	int capacity;

	// read data
	if ( 1 != fscanf_s( fp, "WHEEL_BASE=%f\n", &wheelBase ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "WHEEL_BASE_EST=%f\n", &wheelBaseEst ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "ACCELERATION_EST=%f\n", &accEst ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "MAX_VELOCITY_EST=%f\n", &maxVEst ) ) {
		return 1;
	}
	if ( 2 != fscanf_s( fp, "WHEEL_ERR=%f %f\n", &errL, &errR ) ) { // simulates acceleration and max velocity error as a percentage of estimated acceleration and max velocity
		return 1;
	}
	if ( 1 != fscanf_s( fp, "CAPACITY=%d\n", &capacity ) ) {
		return 1;
	}

	// save data
	this->wheelBase = wheelBase;
	this->wheelBaseEst = wheelBaseEst;
	
	this->accelerationEst = accEst;
	this->accelerationL = accEst*(1+errL);
	this->accelerationR = accEst*(1+errR);
	
	this->maxVelocityEst = maxVEst;
	this->maxVelocityL = maxVEst*(1+errL);
	this->maxVelocityR = maxVEst*(1+errR);
	
	return 0;
}

int SimAvatar::parseParticleFilter( FILE *fp ) {
	int num;
	float initSigma[3], updateSigma[3];

	// read data
	if ( 1 != fscanf_s( fp, "NUM=%d\n", &num ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "SIGMA_INITIAL=%f %f %f\n", &initSigma[0], &initSigma[1], &initSigma[2] ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "SIGMA_UPDATE=%f %f %f\n", &updateSigma[0], &updateSigma[1], &updateSigma[2] ) ) {
		return 1;
	}

	// ignore data

	return 0;
}

int SimAvatar::parseLandmark( FILE *fp ) {
	SimLandmark landmark;
	float sn, cs;
	int code;

	// read data
	if ( 2 != fscanf_s( fp, "OFFSET=%f %f\n", &landmark.x, &landmark.y ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "CODE=%d\n", &code ) ) {
		return 1;
	}

	apb->apbUuidCreate( &landmark.id );
	landmark.owner = this->uuid;
	landmark.code = code + this->landmarkCodeOffset;

	sn = sin( this->state.r );
	cs = cos( this->state.r );
	landmark.wx = this->state.x + landmark.x*cs - landmark.y*sn;
	landmark.wy = this->state.y + landmark.x*sn + landmark.y*cs;
	landmark.height = landmark.elevation = 0;
	//landmark.r = 0;
	//landmark.s = 0.1f;
	landmark.collected = false;

	int pathId = sim->getXPathId();
	if ( pathId == -1 )
		return 1; // failed to get path

	// add to list
	this->landmarks.push_back( landmark );

	// add to sim (who will add it to DDB)
	SimLandmark *ptSL = &this->landmarks.back();
	sim->addLandmark( ptSL );

	// add to visualizer
	//ptSL->objId = vis->newDynamicObject( &ptSL->wx, &ptSL->wy, &ptSL->r, &ptSL->s, pathId, ptSL->color, 2 );

	return 0;
}


int SimAvatar::parseSonar( FILE *fp ) {
	SimSonar sonar;

	// read data
	if ( 1 != fscanf_s( fp, "PERIOD=%d\n", &sonar.period ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &sonar.color[0], &sonar.color[1], &sonar.color[2] ) ) {
		return 1;
	}
	if ( 7 != fscanf_s( fp, "SENSOR_SONAR=%f %f %f %f %f %f %f %f\n", 
			  &sonar.pose.x, &sonar.pose.y, &sonar.pose.r,
			  &sonar.pose.max,
			  &sonar.pose.sigma,
			  &sonar.pose.alpha, &sonar.pose.beta ) ) {
		return 1;
	}

	sonar.index = (int)this->sonars.size();
	sonar.nextT = this->state.t;

	sonar.x = 0;
	sonar.y = 0;
	sonar.r = 0;
	sonar.s = 1;

	int pathId = sim->getSonarPathId( sonar.pose.alpha );
	if ( pathId == -1 )
		return 1; // failed to get path

	// add sonar to list
	this->sonars.push_back( sonar );

	// add to visualizer
	SimSonar *ptSS = &this->sonars.back();
	ptSS->objId = sim->newStaticObject( ptSS->x, ptSS->y, ptSS->r, ptSS->s, pathId, ptSS->color, 1 );
	sim->hideObject( ptSS->objId );

	return 0;
}

int SimAvatar::parseCamera( FILE *fp ) {
	SimCamera camera;

	// read data
	if ( 1 != fscanf_s( fp, "PERIOD=%d\n", &camera.period ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &camera.color[0], &camera.color[1], &camera.color[2] ) ) {
		return 1;
	}
	if ( 6 != fscanf_s( fp, "SENSOR_CAMERA=%f %f %f %f %f %f %f\n", 
			  &camera.pose.x, &camera.pose.y, &camera.pose.r,
			  &camera.max,
			  &camera.pose.sigma,
			  &camera.alpha ) ) {
		return 1;
	}

	camera.ffcolor[0] = 1 - camera.color[0];
	camera.ffcolor[1] = 1 - camera.color[1];
	camera.ffcolor[2] = 1 - camera.color[2];

	camera.index = (int)this->cameras.size();
	camera.nextT = this->state.t;
	
	// clear all vis objects
	int i;
	for ( i=0; i<10; i++ ) {
		camera.objId[i] = -1;
		camera.ffobjId[i] = sim->newStaticObject( 0, 0, 0, 1, sim->getLinePathId(), camera.ffcolor, 1 );
		sim->hideObject( camera.ffobjId[i] );
	}

	// add camera to list
	this->cameras.push_back( camera );

	return 0;
}

//-----------------------------------------------------------------------------
// Sensors

int SimAvatar::doSonar( SimSonar *sonar ) {
	SonarReading reading;
	ExecutiveSimulation::OBJECT *object;
	ExecutiveSimulation::PATH *path;
	ExecutiveSimulation::NODE *nodeA, *nodeB;
	float boundary[3][2]; // sonar boundary
	float boundaryO[3][2]; // boundary in object space
	float distSq[5]; // distances squared of points A, B, C, D, and E
	float pt[5][2];
	float tmp, tmpA, tmpB, tmpC;
	float minValSq;
	int i, mini;
	int intersections;
	float sn, cs;
	
	std::list<ExecutiveSimulation::PATH_REFERENCE *>::iterator itPR;
	std::list<ExecutiveSimulation::NODE *>::iterator itN;

	float hitValSq = sonar->pose.max*sonar->pose.max;
	bool hitValid = false;

	// initialize sonar boundary
	boundary[0][0] = sonar->pose.x;
	boundary[0][1] = sonar->pose.y;
	sn = sin( sonar->pose.r + sonar->pose.alpha );
	cs = cos( sonar->pose.r + sonar->pose.alpha );
	boundary[1][0] = sonar->pose.x + 100*sonar->pose.max*cs;
	boundary[1][1] = sonar->pose.y + 100*sonar->pose.max*sn;
	sn = sin( sonar->pose.r - sonar->pose.alpha );
	cs = cos( sonar->pose.r - sonar->pose.alpha );
	boundary[2][0] = sonar->pose.x + 100*sonar->pose.max*cs;
	boundary[2][1] = sonar->pose.y + 100*sonar->pose.max*sn;

	// transform boundary into world space
	sn = sin( state.r );
	cs = cos( state.r );
	for ( i=0; i<3; i++ ) {
		tmp = state.x + boundary[i][0]*cs - boundary[i][1]*sn;
		boundary[i][1] = state.y + boundary[i][0]*sn + boundary[i][1]*cs;
		boundary[i][0] = tmp;
	}

	// iterate through objects
	int objId = -1; // get the first object
	while ( (objId = sim->getNextObject( &object, objId )) != -1 ) {
		if ( !object->solid || !object->visible )
			continue;

		// transform boundary into object space
		sn = sin( *object->r );
		cs = cos( *object->r );
		for ( i=0; i<3; i++ ) {
			boundaryO[i][0] = boundary[i][0] - *object->x;
			boundaryO[i][1] = boundary[i][1] - *object->y;
			tmp = ( boundaryO[i][0]*cs + boundaryO[i][1]*sn ) / *object->s;
			boundaryO[i][1] = ( boundaryO[i][0]*-sn + boundaryO[i][1]*cs ) / *object->s;
			boundaryO[i][0] = tmp;
		}

		// iterate through paths
		for ( itPR = object->path_refs->begin(); itPR != object->path_refs->end(); itPR++ ) {
			if ( sim->getPath( (*itPR)->id, &path ) )
				continue; // how did this happen?
			
			// iterate through nodes
			itN = path->nodes->begin();
			if ( itN == path->nodes->end() )
				continue; // no nodes?
			nodeA = *itN;
			itN++;
			for ( ; itN != path->nodes->end(); itN++, nodeA = nodeB ) {
				nodeB = *itN;

				// check normal to see if we need to consider this line segment
				if ( 0 <= -(nodeB->y-nodeA->y)*(nodeA->x-boundaryO[0][0]) + (nodeB->x-nodeA->x)*(nodeA->y-boundaryO[0][1]) ) // facing the wrong way
					continue;

				// step 1: check distances
				// closest point C
				tmp = fabs((nodeB->x-nodeA->x)*(nodeA->y-boundaryO[0][1]) - (nodeA->x-boundaryO[0][0])*(nodeB->y-nodeA->y));
				distSq[2] = tmp*tmp/((nodeB->x-nodeA->x)*(nodeB->x-nodeA->x) + (nodeB->y-nodeA->y)*(nodeB->y-nodeA->y)) ;
				if ( distSq[2] >= hitValSq )
					continue; // no point in checking this line further
				tmpA = nodeA->x - boundaryO[0][0];
				tmpB = nodeA->y - boundaryO[0][1];
				tmp = sqrt( tmpA*tmpA + tmpB*tmpB - distSq[2] ) / sqrt((nodeB->x - nodeA->x)*(nodeB->x - nodeA->x) + (nodeB->y - nodeA->y)*(nodeB->y - nodeA->y));
				pt[2][0] = nodeA->x + tmp*(nodeB->x - nodeA->x);
				pt[2][1] = nodeA->y + tmp*(nodeB->y - nodeA->y);
				if ( distSq[2] != (pt[2][0]-boundaryO[0][0])*(pt[2][0]-boundaryO[0][0]) + (pt[2][1]-boundaryO[0][1])*(pt[2][1]-boundaryO[0][1]) ) { // we went the wrong direction
					pt[2][0] = nodeA->x - tmp*(nodeB->x - nodeA->x);
					pt[2][1] = nodeA->y - tmp*(nodeB->y - nodeA->y);	
				}
				if ( fabs(nodeA->x - nodeB->x) > fabs(nodeA->y - nodeB->y) ) { // check to see if the point is off the line segment
					if ( ( pt[2][0] < min(nodeA->x, nodeB->x) ) || ( pt[2][0] > max(nodeA->x, nodeB->x) ) ) 
						distSq[2] = hitValSq; // off
				} else {
					if ( ( pt[2][1] < min(nodeA->y, nodeB->y) ) || ( pt[2][1] > max(nodeA->y, nodeB->y) ) )
						distSq[2] = hitValSq; // off
				}
				// end point A
				pt[0][0] = nodeA->x;
				pt[0][1] = nodeA->y;
				distSq[0] = (nodeA->x-boundaryO[0][0])*(nodeA->x-boundaryO[0][0]) + (nodeA->y-boundaryO[0][1])*(nodeA->y-boundaryO[0][1]);
				// end point B
				pt[1][0] = nodeB->x;
				pt[1][1] = nodeB->y;
				distSq[1] = (nodeB->x-boundaryO[0][0])*(nodeB->x-boundaryO[0][0]) + (nodeB->y-boundaryO[0][1])*(nodeB->y-boundaryO[0][1]); 
				// intersection D
				tmpA = boundaryO[0][0]*boundaryO[1][1] - boundaryO[0][1]*boundaryO[1][0];
				tmpB = nodeA->x*nodeB->y - nodeA->y*nodeB->x;
				tmpC = (boundaryO[0][0]-boundaryO[1][0])*(nodeA->y-nodeB->y) - (boundaryO[0][1]-boundaryO[1][1])*(nodeA->x-nodeB->x);
				pt[3][0] = (tmpA*(nodeA->x - nodeB->x) - (boundaryO[0][0] - boundaryO[1][0])*tmpB) / tmpC;
				pt[3][1] = (tmpA*(nodeA->y - nodeB->y) - (boundaryO[0][1] - boundaryO[1][1])*tmpB) / tmpC;
				distSq[3] = (pt[3][0]-boundaryO[0][0])*(pt[3][0]-boundaryO[0][0]) + (pt[3][1]-boundaryO[0][1])*(pt[3][1]-boundaryO[0][1]);
				tmp = (pt[3][0]-boundaryO[0][0])*(boundaryO[1][0]-boundaryO[0][0]) + (pt[3][1]-boundaryO[0][1])*(boundaryO[1][1]-boundaryO[0][1]);
				if ( tmp < 0 ) { // point is behind us
					distSq[3] = hitValSq; // invalidate
				} else {
					if ( fabs(nodeA->x - nodeB->x) > fabs(nodeA->y - nodeB->y) ) { // check to see if the point is off the line segment
						if ( ( pt[3][0] < min(nodeA->x, nodeB->x) ) || ( pt[3][0] > max(nodeA->x, nodeB->x) ) ) 
							distSq[3] = hitValSq; // off
					} else {
						if ( ( pt[3][1] < min(nodeA->y, nodeB->y) ) || ( pt[3][1] > max(nodeA->y, nodeB->y) ) )
							distSq[3] = hitValSq; // off
					}
				}
				// intersection E
				tmpA = boundaryO[0][0]*boundaryO[2][1] - boundaryO[0][1]*boundaryO[2][0];
				tmpB = nodeA->x*nodeB->y - nodeA->y*nodeB->x;
				tmpC = (boundaryO[0][0]-boundaryO[2][0])*(nodeA->y-nodeB->y) - (boundaryO[0][1]-boundaryO[2][1])*(nodeA->x-nodeB->x);
				pt[4][0] = (tmpA*(nodeA->x - nodeB->x) - (boundaryO[0][0] - boundaryO[2][0])*tmpB) / tmpC;
				pt[4][1] = (tmpA*(nodeA->y - nodeB->y) - (boundaryO[0][1] - boundaryO[2][1])*tmpB) / tmpC;
				distSq[4] = (pt[4][0]-boundaryO[0][0])*(pt[4][0]-boundaryO[0][0]) + (pt[4][1]-boundaryO[0][1])*(pt[4][1]-boundaryO[0][1]);
				tmp = (pt[4][0]-boundaryO[0][0])*(boundaryO[2][0]-boundaryO[0][0]) + (pt[4][1]-boundaryO[0][1])*(boundaryO[2][1]-boundaryO[0][1]);
				if ( tmp < 0 ) { // point is behind us
					distSq[4] = hitValSq; // invalidate
				} else {
					if ( fabs(nodeA->x - nodeB->x) > fabs(nodeA->y - nodeB->y) ) { // check to see if the point is off the line segment
						if ( ( pt[4][0] < min(nodeA->x, nodeB->x) ) || ( pt[4][0] > max(nodeA->x, nodeB->x) ) ) 
							distSq[4] = hitValSq; // off
					} else {
						if ( ( pt[4][1] < min(nodeA->y, nodeB->y) ) || ( pt[4][1] > max(nodeA->y, nodeB->y) ) )
							distSq[4] = hitValSq; // off
					}
				}

				// step 2: check containment
				while (1) {
					mini = -1;
					minValSq = hitValSq;
					for ( i=0; i<5; i++ ) {
						if ( distSq[i] < minValSq ) {
							mini = i;
							minValSq = distSq[i];
						}
					}

					if ( mini == -1 )
						break; // no more points to check

					// check containment
					if ( mini >= 3 ) // intersection points D and E are contained by definition
						break;

					intersections = 0;
					// intersection 01
					if ( ( pt[mini][1] <= max(boundaryO[0][1],boundaryO[1][1]) && pt[mini][1] >= min(boundaryO[0][1],boundaryO[1][1]) ) ) { // must be between y's
						tmpA = boundaryO[0][0]*boundaryO[1][1] - boundaryO[0][1]*boundaryO[1][0];
						tmpB = -pt[mini][1];
						tmpC = boundaryO[0][1]-boundaryO[1][1];
						if ( pt[mini][0] <= (-tmpA - (boundaryO[0][0] - boundaryO[1][0])*tmpB) / tmpC )  // must be past start x
							intersections++;
					}
					// intersection 12
					if ( ( pt[mini][1] <= max(boundaryO[1][1],boundaryO[2][1]) && pt[mini][1] >= min(boundaryO[1][1],boundaryO[2][1]) ) ) { // must be between y's
						tmpA = boundaryO[1][0]*boundaryO[2][1] - boundaryO[1][1]*boundaryO[2][0];
						tmpB = -pt[mini][1];
						tmpC = boundaryO[1][1]-boundaryO[2][1];
						if ( pt[mini][0] <= (-tmpA - (boundaryO[1][0] - boundaryO[2][0])*tmpB) / tmpC )  // must be past start x
							intersections++;
					}
					// intersection 02
					if ( ( pt[mini][1] <= max(boundaryO[0][1],boundaryO[2][1]) && pt[mini][1] >= min(boundaryO[0][1],boundaryO[2][1]) ) ) { // must be between y's
						tmpA = boundaryO[0][0]*boundaryO[2][1] - boundaryO[0][1]*boundaryO[2][0];
						tmpB = -pt[mini][1];
						tmpC = boundaryO[0][1]-boundaryO[2][1];
						if ( pt[mini][0] <= (-tmpA - (boundaryO[0][0] - boundaryO[2][0])*tmpB) / tmpC )  // must be past start x
							intersections++;
					}

					if ( intersections == 1 ) 
						break; // we're contained
					
					distSq[mini] = hitValSq; // invalidate this value and try again
				}

				if ( mini == -1 )
					continue; // no hit point, continue to next line

				// step 3: check angle
				tmpA = atan2f( nodeB->y - nodeA->y, nodeB->x - nodeA->x );
				tmpB = atan2f( pt[mini][1] - boundaryO[0][1], pt[mini][0] - boundaryO[0][0] );
				tmp = tmpA - tmpB;
				while ( tmp > fM_PI ) tmp -= 2*fM_PI;
				while ( tmp < -fM_PI ) tmp += 2*fM_PI;
				tmp = fabs(tmp);
				if ( tmp >= fM_PI_4 && tmp <= 3*fM_PI_4  ) // must be greater than 45 deg
					hitValid = true; // we have a valid hit!
				else
					hitValid = false;
				hitValSq = distSq[mini]; // hit might not be valid but it's still closest
			}
		}
	}

	if ( hitValid && hitValSq < sonar->pose.max*sonar->pose.max ) {
		reading.value = sqrt( hitValSq );

		// add error
		#ifndef NO_RANDOM_ERROR
		reading.value += (float)apb->apbNormalDistribution( 0, SimAvatar_SONAR_SIGMA ); // m error
		#endif

		// add reading to output stream
		this->output.packChar( ExecutiveSimulation_Defs::SAE_SENSOR_SONAR );
		this->output.packChar( sonar->index );
		this->output.packData( &state.t, sizeof(_timeb) );
		this->output.packFloat32( reading.value );

	} else {
		reading.value = -1;
	}

	// visualize
	if ( reading.value > 0 ) {
		sonar->x = boundary[0][0];
		sonar->y = boundary[0][1];
		sonar->r = state.r + sonar->pose.r;
		sonar->s = reading.value;

		if ( this->extraVisible ) {
			sim->updateStaticObject( sonar->objId, sonar->x, sonar->y, sonar->r, sonar->s );		
			sim->showObject( sonar->objId );
		}

	} else {
		sim->hideObject( sonar->objId );

		sonar->s = reading.value;
	}

	return 0;
}

int SimAvatar::doCamera( int cameraInd ) {
	std::list<SimCamera>::iterator iC;

	for ( iC = this->cameras.begin(); iC != this->cameras.end(); iC++ ) {
		if ( iC->index == cameraInd )
			break;
	}

	if ( iC == this->cameras.end() )
		return 1;

	return this->doCamera( (SimCamera *)&(*iC) );
}

int SimAvatar::doCamera( SimCamera *camera ) {
	RPC_STATUS Status;
	int i, col;
	float sn, cs;
	float readingVal, readingAngle;
	SimLandmark *lm;
	float tmp, tmpA, tmpB, tmpC;
	float maxSq = camera->max*camera->max;
	float distSq;
	float pt[2];
	int hitCount = 0;

	ExecutiveSimulation::OBJECT *object;
	ExecutiveSimulation::PATH *path;
	ExecutiveSimulation::NODE *nodeA, *nodeB;
	std::list<ExecutiveSimulation::PATH_REFERENCE *>::iterator itPR;
	std::list<ExecutiveSimulation::NODE *>::iterator itN;
	float line[2][2]; // camera to landmark in world space
	float lineO[2][2]; // camera to landmark in object space
	bool blocked;
	float ffline[10][2][2]; // floorfinder line in world space
	float fflineO[10][2][2]; // floorfinder in object space
	float ffHit[10]; // floorfinder closest hit
	int objId;
	
	// transform boundary into world space
	sn = sin( state.r );
	cs = cos( state.r );
	camera->x = state.x + camera->pose.x*cs - camera->pose.y*sn;
	camera->y = state.y + camera->pose.x*sn + camera->pose.y*cs;

	line[0][0] = camera->x;
	line[0][1] = camera->y;

	this->ds.reset();

	// -- landmarks --
	// iterate through landmarks
	std::list<SimLandmark*> *landmarks = sim->getLandmarkList();
	std::list<SimLandmark*>::iterator itSL;
	for ( itSL = landmarks->begin(); itSL != landmarks->end(); itSL++ ) {
		lm = *itSL;

		if ( lm->collected )
			continue; // skip

		// skip our landmarks
		if ( !UuidCompare( &lm->owner, &this->uuid, &Status ) )
			continue;

		// check if it is in our fov
		tmpA = atan2f( lm->wy - camera->y, lm->wx - camera->x );
		tmpB = state.r + camera->pose.r;
		readingAngle = tmpA - tmpB;
		while ( readingAngle > fM_PI ) readingAngle -= 2*fM_PI;
		while ( readingAngle < -fM_PI ) readingAngle += 2*fM_PI;
		tmp = fabs(readingAngle);
		if ( tmp < camera->alpha ) { // it is within our fov

			readingVal = (lm->wx-camera->x)*(lm->wx-camera->x) + (lm->wy-camera->y)*(lm->wy-camera->y); // squared val for now
			if ( readingVal < maxSq ) {

				line[1][0] = lm->wx;
				line[1][1] = lm->wy;

				// iterate through objects to see if we have a clear view
				blocked = false;
				objId = -1; // get the first object
				while ( (objId = sim->getNextObject( &object, objId )) != -1 ) {
					if ( !object->solid || !object->visible )
						continue;

					// transform into object space
					sn = sin( *object->r );
					cs = cos( *object->r );
					for ( i=0; i<2; i++ ) {
						lineO[i][0] = line[i][0] - *object->x;
						lineO[i][1] = line[i][1] - *object->y;
						tmp = ( lineO[i][0]*cs + lineO[i][1]*sn ) / *object->s;
						lineO[i][1] = ( lineO[i][0]*-sn + lineO[i][1]*cs ) / *object->s;
						lineO[i][0] = tmp;
					}

					// iterate through paths
					for ( itPR = object->path_refs->begin(); itPR != object->path_refs->end(); itPR++ ) {
						if ( sim->getPath( (*itPR)->id, &path ) )
							continue; // how did this happen?
						
						// iterate through nodes
						itN = path->nodes->begin();
						if ( itN == path->nodes->end() )
							continue; // no nodes?
						nodeA = *itN;
						itN++;
						for ( ; itN != path->nodes->end(); itN++, nodeA = nodeB ) {
							nodeB = *itN;

							// check normal to see if we need to consider this line segment
							if ( 0 <= -(nodeB->y-nodeA->y)*(lineO[1][0]-lineO[0][0]) + (nodeB->x-nodeA->x)*(lineO[1][1]-lineO[0][1]) ) // facing the wrong way
								continue;

							// check intersection
							tmpA = lineO[0][0]*lineO[1][1] - lineO[0][1]*lineO[1][0];
							tmpB = nodeA->x*nodeB->y - nodeA->y*nodeB->x;
							tmpC = (lineO[0][0]-lineO[1][0])*(nodeA->y-nodeB->y) - (lineO[0][1]-lineO[1][1])*(nodeA->x-nodeB->x);
							pt[0] = (tmpA*(nodeA->x - nodeB->x) - (lineO[0][0] - lineO[1][0])*tmpB) / tmpC;
							pt[1] = (tmpA*(nodeA->y - nodeB->y) - (lineO[0][1] - lineO[1][1])*tmpB) / tmpC;
							distSq = (pt[0]-lineO[0][0])*(pt[0]-lineO[0][0]) + (pt[1]-lineO[0][1])*(pt[1]-lineO[0][1]);
							if ( distSq < readingVal ) { // must be closer than the landmark
								tmp = (pt[0]-lineO[0][0])*(lineO[1][0]-lineO[0][0]) + (pt[1]-lineO[0][1])*(lineO[1][1]-lineO[0][1]);
								if ( tmp < 0 ) { // point is behind us
									continue; // not blocked
								} else {
									if ( fabs(nodeA->x - nodeB->x) > fabs(nodeA->y - nodeB->y) ) { // check to see if the point is off the line segment
										if ( ( pt[0] >= min(nodeA->x, nodeB->x) ) && ( pt[0] <= max(nodeA->x, nodeB->x) ) ) {
											blocked = true;
											break;
										}
									} else {
										if ( ( pt[1] >= min(nodeA->y, nodeB->y) ) && ( pt[1] <= max(nodeA->y, nodeB->y) ) ) {
											blocked = true;
											break;
										}
									}
								}
							}
						}

						if ( blocked )
							break;
					}

					if ( blocked )
						break;
				}

				if ( !blocked ) { // hit
					// take the square root to get the real value
					readingVal = sqrt( readingVal );

					// add error
					#ifndef NO_RANDOM_ERROR
						readingVal *= 1 + (float)apb->apbNormalDistribution( 0, SimAvatar_CAMERA_R_SIGMA ); // percentage error
						readingAngle += (float)apb->apbNormalDistribution( 0, SimAvatar_CAMERA_A_SIGMA ); // radian error
					#endif

					// pack reading
					this->ds.packChar( SimCamera_DATA_LANDMARK );
					this->ds.packUChar( lm->code );
					this->ds.packFloat32( readingVal );
					this->ds.packFloat32( readingAngle );

					// visualize
					if ( hitCount < 10 ) { // we only vis the first 10 hits
						if ( camera->objId[hitCount] == -1 ) // object hasn't be created yet
							camera->objId[hitCount] = sim->newStaticObject( camera->x, camera->y, camera->r[hitCount], camera->s[hitCount], sim->getLinePathId(), camera->color, 1 );
						
						camera->r[hitCount] = state.r + camera->pose.r + readingAngle;
						camera->s[hitCount] = readingVal;
						if ( this->extraVisible ) {
							sim->updateStaticObject( camera->objId[hitCount], camera->x, camera->y, camera->r[hitCount], camera->s[hitCount] );
							sim->showObject( camera->objId[hitCount] );
						} else
							sim->hideObject( camera->objId[hitCount] );
					}

					hitCount++;
				}
			}
		}

	}

	// -- floorfinder --
	for ( col=0; col<10; col++ ) { // prepare floorfinder lines
		ffline[col][0][0] = camera->x; // start
		ffline[col][0][1] = camera->y;
		sn = sin( state.r + camera->pose.r + (col-4.5f)*camera->alpha/5 );
		cs = cos( state.r + camera->pose.r + (col-4.5f)*camera->alpha/5 );
		ffline[col][1][0] = camera->x + SimCamera_FLOORFINDERMAX*cs; // end
		ffline[col][1][1] = camera->y + SimCamera_FLOORFINDERMAX*sn;

		ffHit[col] = SimCamera_FLOORFINDERMAX*SimCamera_FLOORFINDERMAX;
	}

	objId = -1; // get the first object
	while ( (objId = sim->getNextObject( &object, objId )) != -1 ) {
		if ( !object->solid || !object->visible )
			continue;

		// transform into object space
		sn = sin( *object->r );
		cs = cos( *object->r );

		for ( col=0; col<10; col++ ) {
			for ( i=0; i<2; i++ ) {
				fflineO[col][i][0] = ffline[col][i][0] - *object->x;
				fflineO[col][i][1] = ffline[col][i][1] - *object->y;
				tmp = ( fflineO[col][i][0]*cs + fflineO[col][i][1]*sn ) / *object->s;
				fflineO[col][i][1] = ( fflineO[col][i][0]*-sn + fflineO[col][i][1]*cs ) / *object->s;
				fflineO[col][i][0] = tmp;
			}
		}

		// iterate through paths
		for ( itPR = object->path_refs->begin(); itPR != object->path_refs->end(); itPR++ ) {
			if ( sim->getPath( (*itPR)->id, &path ) )
				continue; // how did this happen?
			
			// iterate through nodes
			itN = path->nodes->begin();
			if ( itN == path->nodes->end() )
				continue; // no nodes?
			nodeA = *itN;
			itN++;
			for ( ; itN != path->nodes->end(); itN++, nodeA = nodeB ) {
				nodeB = *itN;

				for ( col=0; col<10; col++ ) {
					// check normal to see if we need to consider this line segment
					if ( 0 <= -(nodeB->y-nodeA->y)*(fflineO[col][1][0]-fflineO[col][0][0]) + (nodeB->x-nodeA->x)*(fflineO[col][1][1]-fflineO[col][0][1]) ) // facing the wrong way
						continue;

					// check intersection
					tmpA = fflineO[col][0][0]*fflineO[col][1][1] - fflineO[col][0][1]*fflineO[col][1][0];
					tmpB = nodeA->x*nodeB->y - nodeA->y*nodeB->x;
					tmpC = (fflineO[col][0][0]-fflineO[col][1][0])*(nodeA->y-nodeB->y) - (fflineO[col][0][1]-fflineO[col][1][1])*(nodeA->x-nodeB->x);
					pt[0] = (tmpA*(nodeA->x - nodeB->x) - (fflineO[col][0][0] - fflineO[col][1][0])*tmpB) / tmpC;
					pt[1] = (tmpA*(nodeA->y - nodeB->y) - (fflineO[col][0][1] - fflineO[col][1][1])*tmpB) / tmpC;
					distSq = (pt[0]-fflineO[col][0][0])*(pt[0]-fflineO[col][0][0]) + (pt[1]-fflineO[col][0][1])*(pt[1]-fflineO[col][0][1]);
					if ( distSq < ffHit[col] ) { // must be closer that current hit
						tmp = (pt[0]-fflineO[col][0][0])*(fflineO[col][1][0]-fflineO[col][0][0]) + (pt[1]-fflineO[col][0][1])*(fflineO[col][1][1]-fflineO[col][0][1]);
						if ( tmp < 0 ) { // point is behind us
							continue; // not blocked
						} else {
							if ( fabs(nodeA->x - nodeB->x) > fabs(nodeA->y - nodeB->y) ) { // check to see if the point is off the line segment
								if ( ( pt[0] >= min(nodeA->x, nodeB->x) ) && ( pt[0] <= max(nodeA->x, nodeB->x) ) ) {
									ffHit[col] = distSq;
								}
							} else {
								if ( ( pt[1] >= min(nodeA->y, nodeB->y) ) && ( pt[1] <= max(nodeA->y, nodeB->y) ) ) {
									ffHit[col] = distSq;
								}
							}
						}
					}
				}
			}
		}
	}

	// record floorfinder hits
	this->ds.packChar( SimCamera_DATA_FLOORFINDER );
	this->ds.packFloat32( camera->alpha ); 
	for ( col=0; col<10; col++ ) {
		if ( ffHit[col] == SimCamera_FLOORFINDERMAX*SimCamera_FLOORFINDERMAX ) { // miss
			this->ds.packFloat32( -1 ); 
			// vis
			sim->hideObject( camera->ffobjId[col] );	
		} else { // hit
			ffHit[col] = sqrt( ffHit[col] );
			this->ds.packFloat32( ffHit[col] ); 
			sim->updateStaticObject( camera->ffobjId[col], camera->x, camera->y, state.r + camera->pose.r + (col-4.5f)*camera->alpha/5, ffHit[col] );
			sim->showObject( camera->ffobjId[col] );
		}
	}
	
	// add to output stream
	this->ds.packChar( SimCamera_DATA_END );
	
	// pack data
	this->output.packChar( ExecutiveSimulation_Defs::SAE_SENSOR_CAMERA );
	this->output.packChar( camera->index );
	this->output.packData( &state.t, sizeof(_timeb) );
	this->output.packInt32( this->ds.length() );
	this->output.packData( this->ds.stream(), this->ds.length() );

	this->ds.unlock();

	// hide extra objects
	for ( i=hitCount; i<10; i++ ) {
		if ( camera->objId[i] == -1 )
			break;
		sim->hideObject( camera->objId[i] );
		camera->s[i] = -1;
	}

	return 0;
}

int SimAvatar::doCollectLandmark( unsigned char code, float x, float y, UUID *thread ) {

	SimLandmark *lm;
	float dx, dy;
	char successCode;

	// find the landmark
	std::list<SimLandmark*> *landmarks = sim->getLandmarkList();
	std::list<SimLandmark*>::iterator itSL;
	for ( itSL = landmarks->begin(); itSL != landmarks->end(); itSL++ ) {
		lm = *itSL;

		if ( lm->code == code )
			break;
	}

	if ( itSL == landmarks->end() || lm->collected ) {

		if (itSL == landmarks->end())
		{
			Log->log(0, "SimAvatar::doCollectLandmark: landmark not found (%d)", code);
			successCode = -2;
		}
		else
		{
			Log->log(0, "SimAvatar::doCollectLandmark: landmark already collected (%d)", code);
			successCode = -1;
		}
		// pack data
		this->output.packChar( ExecutiveSimulation_Defs::SAE_COLLECT );
		this->output.packUChar( code );
		this->output.packChar( successCode ); // fail
		this->output.packUUID( thread );
		this->output.packFloat32(-1);
		this->output.packFloat32(-1);
		return 1; // not found
	}

	// see if we can pick it up
	dx = state.x - lm->wx;
	dy = state.y - lm->wy;
	if ( dx*dx + dy*dy < 0.6f*0.6f ) { // close enough
//	if (dx*dx + dy*dy < 9.0f*9.0f) { // close enough		
		Log->log( 0, "SimAvatar::doCollectLandmark: landmark collected (%d)", code );

		// pack data
		this->output.packChar( ExecutiveSimulation_Defs::SAE_COLLECT );
		this->output.packUChar( code );
		this->output.packChar( 1 ); // success
		this->output.packUUID( thread );
		this->output.packFloat32(x);
		this->output.packFloat32(y);

		// flag as collected
		lm->collected = true;
		lm->wx = state.x;
		lm->wy = state.y;

	} else { // failed
		Log->log( 0, "SimAvatar::doCollectLandmark: landmark out of reach (%d)", code );
		
		// pack data
		this->output.packChar( ExecutiveSimulation_Defs::SAE_COLLECT );
		this->output.packUChar( code );
		this->output.packChar( 0 ); // fail
		this->output.packUUID( thread );
		this->output.packFloat32(-1);
		this->output.packFloat32(-1);
	}

	return 0;
}

int SimAvatar::doDepositLandmark(unsigned char code, float x, float y, UUID *thread, UUID *initiator)
{

	SimLandmark *lm;
	char successCode;


	// find the landmark
	std::list<SimLandmark*> *landmarks = sim->getLandmarkList();
	std::list<SimLandmark*>::iterator itSL;
	for (itSL = landmarks->begin(); itSL != landmarks->end(); itSL++) {
		lm = *itSL;

		if (lm->code == code)
			break;
	}

	if (itSL == landmarks->end() || !lm->collected) {

		if (itSL == landmarks->end()) 
			{
				Log->log(0, "SimAvatar::doDepositLandmark: landmark not found (%d)", code);
				successCode = 0;
			}
		else
			{
				Log->log(0, "SimAvatar::doDepositLandmark: landmark not yet collected (%d)", code);
				successCode = -1;
			}
		// pack data
		this->output.packChar(ExecutiveSimulation_Defs::SAE_DEPOSIT);
		this->output.packUChar(code);
		this->output.packChar(successCode); // fail
		this->output.packUUID(thread); 
		this->output.packFloat32(x);
		this->output.packFloat32(y);
		this->output.packUUID(initiator);

		return 1; // not found
	}

	// deposit		
		Log->log(0, "SimAvatar::doDepositLandmark: landmark deposited (%d)", code);
		successCode = 1;
		// pack data
		this->output.packChar(ExecutiveSimulation_Defs::SAE_DEPOSIT);
		this->output.packUChar(code);
		this->output.packChar(successCode); // success
		this->output.packUUID(thread);
		this->output.packFloat32(x);
		this->output.packFloat32(y);
		this->output.packUUID(initiator);

		// flag as deposited
		lm->collected = false;


	return 0;
}
