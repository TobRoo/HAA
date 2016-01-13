
#include "StdAfx.h"

#include "SimAgent.h"
#include "SimAvatar.h"
#include "SimExplore.h"
#include "SimSLAM.h"

#include "..\\autonomic\\DDBStore.h"
#include "..\\autonomic\\agentPlayback.h"

#include "..\\include\\CxImage\\ximage.h"

#include <map>

#define _USE_MATH_DEFINES
#include "math.h"

#define round(val) floor((val) + 0.5f)

Simulation::Simulation( Visualize *visualizer ) {

	dumpNum = 0;

	this->visualizer = visualizer;

	this->ds = new DataStream();
	this->randGen = new RandomGenerator();
	this->apb = new AgentPlayback( PLAYBACKMODE_OFF, NULL );

	// set up basic paths
	float linePathX[2] = { 0.0f, 1.0f };
	float linePathY[2] = { 0.0f, 0.0f } ;
	this->pathIdLine = this->visualizer->newPath( 2, linePathX, linePathY );
	float xPathX[5] = { -0.5f, 0.5f, 0.0f, -0.5f, 0.5f };
	float xPathY[5] = { -0.5f, 0.5f, 0.0f, 0.5f, -0.5f };
	this->pathIdX = this->visualizer->newPath( 5, xPathX, xPathY );
	float robotPathX[4] = { -0.08f, 0.12f, -0.08f, -0.08f };
	float robotPathY[4] = { 0.08f, 0.0f, -0.08f, 0.08f } ;
	this->pathIdRobot = this->visualizer->newPath( 4, robotPathX, robotPathY );
	float particlePathX[3] = { 0.12f, 0.0f, 0.0f };
	float particlePathY[3] = { 0.0f, 0.0f, -0.04f } ;
	this->pathIdParticle = this->visualizer->newPath( 3, particlePathX, particlePathY );

	this->clean = true;

	this->skipCooccupancy = false;

	return;
}

Simulation::~Simulation() {

	if ( !this->clean ) {
		this->CleanUp();
	}

	delete this->ds;
	delete this->randGen;
	delete this->apb;

	// clean up basic paths
	this->visualizer->deletePath( this->pathIdLine );
	this->visualizer->deletePath( this->pathIdX );
	this->visualizer->deletePath( this->pathIdRobot );
	this->visualizer->deletePath( this->pathIdParticle );
	std::map<float,int>::iterator itSP;
	for ( itSP = this->pathIdSonar.begin(); itSP != this->pathIdSonar.end(); itSP++ ) {
		this->visualizer->deletePath( itSP->second );
	}
	this->pathIdSonar.clear();

}

int Simulation::getFreePort() {
	int i;
	for ( i=0; i<10; i++ ) {
		if ( usedPorts[i] == 0 ) {
			usedPorts[i] = 1;
			return BASE_PORT + i;
		}
	}
	return -1;
}

void Simulation::releasePort( int port ) {
	usedPorts[port - BASE_PORT] = 0;
}

int loadMap( FIMAGE *map, char *name ) {
	int r, c;
	float val;

	FILE *file;

	fopen_s( &file, name, "r" );
	if ( !file )
		return 1;

	for ( r=map->rows-1; r>=0; r-- ) {
		for ( c=0; c<map->cols; c++ ) {
			if ( 1 != fscanf_s( file, "%f", &val ) ) {
				fclose( file );
				return 1;
			}
			Px(map,r,c) = val;
		}
	}

	fclose( file );

	return 0;
}

int loadRegion( FIMAGE *map, char *name, int x, int y, int width, int height ) {
	int r, c;
	float val;

	FILE *file;

	fopen_s( &file, name, "r" );
	if ( !file )
		return 1;

	for ( r=height-1; r>=0; r-- ) {
		for ( c=0; c<width; c++ ) {
			if ( 1 != fscanf_s( file, "%f", &val ) ) {
				fclose( file );
				return 1;
			}
			Px(map,r+y,c+x) = val;
		}
	}

	fclose( file );

	return 0;
}

int loadPFMean( Visualize *visualizer, char *filename ) {
	FILE *fp;
	int infoFlags;
	int particleNum, stateSize;
	char regionFlag, stateFlag;
	_timeb tb;
	float pstate[1024];

	int count;
	float x[10240], y[10240];

	fopen_s( &fp, filename, "rb" );
	if ( !fp )
		return 1;

	//fread_s( &infoFlags, sizeof(int), 1, sizeof(int), fp ); // read infoFlags
	fread( &infoFlags,1, sizeof(int), fp ); // read infoFlags

	if ( infoFlags != (DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_MEAN ) ) 
		return 1; // doesn't have the right info

	//fread_s( &particleNum, sizeof(int), 1, sizeof(int), fp );; // read particleNum
	//fread_s( &stateSize, sizeof(int), 1, sizeof(int), fp );	// read stateSize
	fread( &particleNum, 1, sizeof(int), fp );; // read particleNum
	fread( &stateSize, 1, sizeof(int), fp );	// read stateSize
	if ( stateSize > 1024 )
		return 1; // need to increase pstate size

	count = 0;

	//fread_s( &regionFlag, sizeof(char), 1, sizeof(char), fp );	// read regionFlag
	fread( &regionFlag, 1, sizeof(char), fp );	// read regionFlag
	while ( regionFlag == 1 ) {

		//fread_s( &stateFlag, sizeof(char), 1, sizeof(char), fp ); // read stateFlag
		fread( &stateFlag, 1, sizeof(char), fp ); // read stateFlag
		while ( stateFlag == 1 ) {

			// read time
			//fread_s( &tb, sizeof(_timeb), 1, sizeof(_timeb), fp );
			fread( &tb, 1, sizeof(_timeb), fp );

			// read mean
			//fread_s( pstate, sizeof(pstate), 1, sizeof(float)*stateSize, fp );
			fread( pstate, 1, sizeof(float)*stateSize, fp );

			// extend path if necessary
			if ( count == 0 || pstate[0] != x[count-1] || pstate[1] != y[count-1] ) {
				x[count] = pstate[0];
				y[count] = pstate[1];
				count++;
			}

			//fread_s( &stateFlag, sizeof(char), 1, sizeof(char), fp ); // read stateFlag
			fread( &stateFlag, 1, sizeof(char), fp ); // read stateFlag
		}

		//fread_s( &regionFlag, sizeof(char), 1, sizeof(char), fp ); // read regionFlag
		fread( &regionFlag, 1, sizeof(char), fp ); // read regionFlag
	}
	
	// create path and return id
	if ( count )
		return visualizer->newPath( count, x, y );
	else
		return -1;
}

int Simulation::loadNextDump() {
	//char buf[512];
	//sprintf_s( buf, 512, "data\\dump\\ddbApplyPOGUpdate%d.txt", dumpNum );
	//loadRegion( this->map, buf, 0 + 70, 0 + 50, 50, 70 );
	//sprintf_s( buf, 512, "data\\dump\\ddbApplyPOGUpdate%d.txt.BH.txt", dump );
	//loadRegion( this->map, buf, 0 + 120, 0 + 50, 50, 70 );

	dumpNum++;

	return 0;
}

int Simulation::loadPrevDump() {
	char buf[512];
	
	dumpNum--;

	sprintf_s( buf, 512, "data\\dump\\ddbApplyPOGUpdate%d.txt", dumpNum );
	loadRegion( this->map, buf, 0 + 70, 0 + 50, 50, 70 );
	//sprintf_s( buf, 512, "data\\dump\\ddbApplyPOGUpdate%d.txt.BH.txt", dump );
	//loadRegion( this->map, buf, 0 + 120, 0 + 50, 50, 70 );

	return 0;
}

int Simulation::Step() {

	if ( paused )
		return 0;

	// TEMP dump map every 5 seconds
	if ( simTime <= 300000 && simTime % 5000 == 0 ) {
		char fname[512];
		sprintf_s( fname, 512, "data\\dump\\mapDump%04d.%03d.txt", simTime/1000, simTime%1000 );
		this->dStore->POGDumpRegion( &this->mapId, 0, 0, 3.4f, 4.9f, fname );	

		this->visualizePaths();
	}
	
	// TEMP
	if ( simTime == 110000 ) {
//		this->TogglePause();
	}

	simTime += simStep;

	// -- do prestep --
	this->supervisorExplore->preStep( simStep );
	this->supervisorSLAM->preStep( simStep );
	// avatar
	mapSimAvatar::iterator it = this->avatars.begin();
	while ( it != this->avatars.end() ) {
		it->second->preStep( simStep );
		it++;
	}

	// -- do step --
	this->supervisorExplore->step();
	this->supervisorSLAM->step();
	// avatars
	it = this->avatars.begin();
	while ( it != this->avatars.end() ) {
		it->second->step();
		it++;
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Configure


int Simulation::Initialize() {
	int r, c;

	this->clean = false;

	memset( usedPorts, 0, sizeof(usedPorts) );

	this->nextLandmarkCode = 0;

	this->dStore = new DDBStore( this->apb );

	simTime = 0;
	simStep = 10;

	paused = 0;

	// create fImages
	this->map = NewImage( MAP_SIZE*MAP_DIVISIONS, MAP_SIZE*MAP_DIVISIONS );

	// initialize mission region
	UuidCreate( &this->missionRegionId );
	this->ddbAddRegion( &this->missionRegionId, 0.0f, 0.0f, 3.3528f, 4.8768f );
	//this->ddbAddRegion( &this->missionRegionId, 0.0f, 0.0f, 2.0f, 3.0f );

	// initialize map
	for ( r=0; r<this->map->rows; r++ ) {
		for ( c=0; c<this->map->cols; c++ ) {
			Px(this->map,r,c) = 0.5f;
		}
	}

	UuidCreate( &this->mapId );
	this->ddbAddPOG( &this->mapId, MAP_TILE_SIZE, MAP_RESOLUTION );

	// load map from file
	//loadMap( this->map, "data\\maps\\layout1200x200.txt" );

	this->mapImage = this->visualizer->newfImage( MAP_X, MAP_Y, 1.0f/MAP_DIVISIONS, 0, 127, this->map );

	// create Explore supervisor
	this->supervisorExplore = new SimExplore( this, visualizer, randGen );
	if ( !this->supervisorExplore->configure() ) {
		this->supervisorExplore->addRegion( &this->missionRegionId );
		this->supervisorExplore->addMap( &this->mapId );
		this->supervisorExplore->start();
	}

	// create SLAM supervisor
	this->supervisorSLAM = new SimSLAM( this, visualizer, randGen );
	if ( !this->supervisorSLAM->configure() ) {
		this->supervisorSLAM->setMap( &this->mapId );
		this->supervisorSLAM->start();
	}

	// TEMP
	float bg[3] = { 0, 0, 0 };
	float fg[3] = { 1, 1, 1 };
	//this->visualizer->newfImage( -10, 0, 1.0f/MAP_DIVISIONS, 1, 50, this->densityT, bg, fg );
	//this->visualizer->newfImage( -10, 5, 1.0f/MAP_DIVISIONS, 0, 50, this->absoluteT, bg, fg );
	//this->visualizer->newfImage( 4, 0, 0.5f/MAP_DIVISIONS, 0.5, 50, this->cameraContours, bg, fg );
	//this->visualizer->newfImage( 4, 5, 1.0f/CAMERA_TEMPLATE_DIVISIONS, 0.5f, 50, this->cameraT, bg, fg );
	//this->mapUpdateImage = this->visualizer->newfImage( -10, 10, 1.0f/MAP_DIVISIONS, 0, 50, this->mapUpdate );

	return 0;
}

int Simulation::CleanUp() {

	this->ddbClearWatchers();

	// free avatars
	while ( this->avatars.size() ) {
		this->removeAvatar( this->avatars.begin()->first );
	}

	// remove landmarks
	while ( this->landmarks.size() ) {
		this->removeLandmark( this->landmarks.front() );
	}

	// clean up Explore supervisor
	delete this->supervisorExplore;

	// clean up SLAM supervisor
	delete this->supervisorSLAM;

	// delete images
	FreeImage( this->map );

	delete this->dStore;

	this->clean = true;

	return 0;
}

int Simulation::loadConfig( char *configFile ) {

	if ( !this->clean )
		this->CleanUp();

	this->Initialize();

	this->parseConfigFile( configFile );

	return 0;
}

int Simulation::parseConfigFile( char *configFile ) {
	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;

	fopen_s( &fp, configFile, "r" );
	if ( !fp )
		return 1; // couldn't open file

	i = 0;
	while ( 1 ) {
		ch = fgetc( fp );
		
		if ( ch == EOF ) {
			break;
		} else if ( ch == ']' ) {
			keyBuf[i] = ']';
			keyBuf[i+1] = '\0';

			// clear to end of line so that you can put in comments after the key
			do {
				ch = fgetc( fp );
			} while ( ch != '\n' && ch != EOF );

			if ( !strncmp( keyBuf, "[SLAM]", 64 ) ) {
				if ( this->parseSLAM( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[landmark]", 64 ) ) {
				if ( this->parseLandmark( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[avatar]", 64 ) ) {
				if ( this->parseAvatar( fp ) )
					return 1;
			} else if ( !strncmp( keyBuf, "[path]", 64 ) ) {
				if ( this->parsePath( fp ) )
					return 1;
			} else { // unknown key
				fclose( fp );
				return 1;
			}
			
			i = 0;
		} else {
			keyBuf[i] = ch;
			i++;
			if ( i == sizeof(keyBuf) - 1 )
				return 1;
		}
	}

	fclose( fp );

	return 0;
}

int Simulation::parseSLAM( FILE *fp ) {
	char buf[1024];
	int po, pp;
	int threadsChangeCount;
	int i;

	if ( 1 != fscanf_s( fp, "PROCESSORDER=%s\n", buf, 1024 ) ) {
		return 1;
	}
	if ( !strncmp( buf, "FIFO", sizeof(buf) ) ) {
		po = SimSLAM::PO_FIFO;
	} else if ( !strncmp( buf, "FIFO_DELAY", sizeof(buf) ) ) {
		po = SimSLAM::PO_FIFO_DELAY;
	} else if ( !strncmp( buf, "LIFO", sizeof(buf) ) ) {
		po = SimSLAM::PO_LIFO;
	} else { // LIFO_PURGE
		po = SimSLAM::PO_LIFO_PURGE;
	}

	if ( 1 != fscanf_s( fp, "PROCESSPRIORITY=%s\n", buf, 1024 ) ) {
		return 1;
	}
	if ( !strncmp( buf, "NONE", sizeof(buf) ) ) {
		pp = SimSLAM::PP_NONE;
	} else { // LANDMARK
		pp = SimSLAM::PP_LANDMARK;
	}

	if ( 1 != fscanf_s( fp, "DOEXPLORE=%s\n", buf, 1024 ) ) {
		return 1;
	}
	if ( !strncmp( buf, "YES", sizeof(buf) ) ) {
		// nothing to do
	} else { // NO
		this->supervisorExplore->stop();
	}

	if ( 1 != fscanf_s( fp, "DOCOOCCUPANCY=%s\n", buf, 1024 ) ) {
		return 1;
	}
	if ( !strncmp( buf, "YES", sizeof(buf) ) ) {
		this->skipCooccupancy = false;
	} else { // NO
		this->skipCooccupancy = true;
	}

	if ( 1 != fscanf_s( fp, "THREADSCHANGECOUNT=%d\n", &threadsChangeCount ) ) {
		return 1;
	}

	for ( i=0; i<threadsChangeCount; i++ ) {
		_timeb tb;
		float time;
		int threads;

		if ( 2 != fscanf_s( fp, "THREADS=%f %d\n", &time, &threads ) )
			return 1;
		
		tb.time = (int)time;
		tb.millitm = (int)(time * 1000) % 1000;

		this->supervisorSLAM->addThreadChange( &tb, threads );
	}

	this->supervisorSLAM->setProcessingOpts( po, pp );

	return 0;
}

int Simulation::parseLandmark( FILE *fp ) {
	SimLandmark *landmark;

	landmark = (SimLandmark *)malloc(sizeof(SimLandmark));
	if ( !landmark )
		return 1;

	// read data
	if ( 2 != fscanf_s( fp, "OFFSET=%f %f\n", &landmark->x, &landmark->y ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &landmark->color[0], &landmark->color[1], &landmark->color[2] ) ) {
		return 1;
	}

	UuidCreate( &landmark->id );
	UuidCreateNil( &landmark->owner );

	landmark->wx = landmark->x;
	landmark->wy = landmark->y;
	landmark->r = 0;
	landmark->s = 0.1f;

	// add to sim (who will add it to DDB)
	this->addLandmark( landmark );

	// add to visualizer
	landmark->objId = visualizer->newStaticObject( landmark->wx, landmark->wy, landmark->r, landmark->s, this->pathIdX, landmark->color, 2 );

	return 0;
}

int Simulation::parseAvatar( FILE *fp ) {
	char nameBuf[256];
	char buf[1024];
	float x, y, r;
	float color[3];
	int i, j;
	int actionCount;
	UUID id;
	float data;

	// read data
	if ( 1 != fscanf_s( fp, "NAME=%s\n", nameBuf, 256 ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "POSE=%f %f %f\n", &x, &y, &r ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "INI=%s\n", buf, 1024 ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &color[0], &color[1], &color[2] ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "ACTIONCOUNT=%d\n", &actionCount ) ) {
		return 1;
	}

	id = this->addAvatar( x, y, r, buf, nameBuf, color );

	RPC_STATUS Status;
	if ( !UuidIsNil( &id, &Status ) && actionCount ) {
		for ( i=0; i<actionCount; i++ ) {
			j = 0;
			while ( j < sizeof(buf) ) {
				buf[j] = fgetc( fp );
				if ( buf[j] == '=' || buf[j] == '\n' || buf[j] == EOF )
					break;
				j++;
			}
			if ( j == sizeof(buf) )
				return 1;

			buf[j] = '\0';

			if ( 1 != fscanf_s( fp, "%f\n", &data ) )
				return 1;
			
			if ( !strncmp( buf, "WAIT", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_WAIT, &data, sizeof(float) );
			} else if ( !strncmp( buf, "MOVE", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_MOVE, &data, sizeof(float) );
			} else if ( !strncmp( buf, "ROTATE", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_ROTATE, &data, sizeof(float) );
			} else if ( !strncmp( buf, "IMAGE", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_IMAGE, &data, sizeof(float) );
			} else if ( !strncmp( buf, "DELAY", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_DELAY );
			} else if ( !strncmp( buf, "MOVE_DELAY", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_MOVE, &data, sizeof(float) );
				this->avatars[id]->queueAction( &id, NULL, 0, AA_DELAY );
			} else if ( !strncmp( buf, "ROTATE_DELAY", sizeof(buf) ) ) {
				this->avatars[id]->queueAction( &id, NULL, 0, AA_ROTATE, &data, sizeof(float) );
				this->avatars[id]->queueAction( &id, NULL, 0, AA_DELAY );
			}
		}
	}

	return 0;
}

int Simulation::parsePath( FILE *fp ) {
	char buf[1024];

	// read data
	if ( 1 != fscanf_s( fp, "PATH=%s\n", buf, 1024 ) ) {
		return 1;
	}

	this->visualizer->loadPathFile( buf );

	return 0;
}


int Simulation::getSonarPathId( float alpha ) {
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

	pathId = visualizer->newPath( 7, pathX, pathY );
	
	this->pathIdSonar[alpha] = pathId;

	return pathId;
}

int Simulation::addLandmark( SimLandmark *landmark ) {
	// set the code
	landmark->code = this->nextLandmarkCode;
	this->nextLandmarkCode++;

	// add to our landmark list
	this->landmarks.push_back( landmark );

	// add to DDB
	this->ddbAddLandmark( &landmark->id, landmark->code, &landmark->owner, 0, 0, landmark->x, landmark->y );

	return 0;
}

int Simulation::removeLandmark( SimLandmark *landmark ) {
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

	this->ddbRemoveLandmark( &landmark->id );

	return 0;
}

int Simulation::getReadingQueueSize() { 
	return this->supervisorSLAM->getReadingQueueSize(); 
}

int Simulation::visualizePaths() {

	mapSimAvatar::iterator itAv = this->avatars.begin();
	while ( itAv != this->avatars.end() ) {
		itAv->second->visualizePath();
		itAv++;
	}

	return 0;
}

// 1 = on, 0 = off, -1 = no change, 2 = toggle
int Simulation::setVisibility( int avatarVis, int avatarPathVis, int avatarExtraVis, int landmarkVis, int particleFilterVis, int consoleVis ) {
	int i;

	mapSimAvatar::iterator itAv = this->avatars.begin();
	while ( itAv != this->avatars.end() ) {
		itAv->second->setVisibility( avatarVis, avatarPathVis, avatarExtraVis );
		itAv++;
	}

	std::list<SimLandmark*>::iterator itL;
	switch ( landmarkVis ) {
	case 1:
		for ( itL = this->landmarks.begin(); itL != this->landmarks.end(); itL++ ) {
			visualizer->showObject( (*itL)->objId );
		}
		break;
	case 0:
		for ( itL = this->landmarks.begin(); itL != this->landmarks.end(); itL++ ) {
			visualizer->hideObject( (*itL)->objId );
		}
		break;
	case 2:
		for ( itL = this->landmarks.begin(); itL != this->landmarks.end(); itL++ ) {
			visualizer->toggleObject( (*itL)->objId );
		}
		break;
	default:
		break;
	};

	mapVisParticleFilter::iterator itPF;
	switch ( particleFilterVis ) {
	case 1:
		for ( itPF = this->particleFilters.begin(); itPF != this->particleFilters.end(); itPF++ ) {
			for ( i=0; i<itPF->second.particleNum; i++ ) 
				visualizer->hideObject( itPF->second.objId[i] );
			visualizer->hideObject( itPF->second.avgObjId );
			visualizer->hideObject( itPF->second.trueObjId );
		}
		break;
	case 0:
		for ( itPF = this->particleFilters.begin(); itPF != this->particleFilters.end(); itPF++ ) {
			for ( i=0; i<itPF->second.particleNum; i++ ) 
				visualizer->showObject( itPF->second.objId[i] );
			visualizer->showObject( itPF->second.avgObjId );
			visualizer->showObject( itPF->second.trueObjId );
		}
		break;
	case 2:
		for ( itPF = this->particleFilters.begin(); itPF != this->particleFilters.end(); itPF++ ) {
			for ( i=0; i<itPF->second.particleNum; i++ ) 
				visualizer->toggleObject( itPF->second.objId[i] );
			visualizer->toggleObject( itPF->second.avgObjId );
			visualizer->toggleObject( itPF->second.trueObjId );
		}
		break;
	default:
		break;
	};

	switch ( consoleVis ) {
	case 1:
		visualizer->showConsole();
		break;
	case 0:
		visualizer->hideConsole();
		break;
	case 2:
		visualizer->toggleConsole();
		break;
	default:
		break;
	};
	

	return 0;
}

//-----------------------------------------------------------------------------
// DDB

int Simulation::ddbAddWatcher( UUID *id, int type ) {
	int i, flag;
	mapDDBWatchers::iterator watchers;
	std::list<UUID> *watcherList;

	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatchers.find( flag );
			if ( watchers == this->DDBWatchers.end() ) {
				watcherList = new std::list<UUID>;
				this->DDBWatchers[flag] = watcherList;
			} else {
				watcherList = watchers->second;
			}
			watcherList->push_back( *id );
		}
	}

	return 0;
}

int Simulation::ddbAddWatcher( UUID *id, UUID *item ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *item );
	std::list<UUID> *watcherList;

	if ( watchers == this->DDBItemWatchers.end() ) {
		watcherList = new std::list<UUID>;
		this->DDBItemWatchers[*item] = watcherList;
	} else {
		watcherList = watchers->second;
	}
	watcherList->push_back( *id );

	return 0;
}

int Simulation::ddbRemWatcher( UUID *id ) {
	int i, flag, type;
	mapDDBWatchers::iterator watchers;
	mapDDBItemWatchers::iterator itemWatchers;
	std::list<UUID>::iterator iter;
	
	type = 0;

	// check our lists by type
	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;

		watchers = this->DDBWatchers.find( flag );

		if ( watchers == this->DDBWatchers.end() ) {
			continue; // no watchers for this type!
		}

		iter = watchers->second->begin();
		while ( iter != watchers->second->end() ) {
			if ( (*iter) == *id ) {
				type |= flag;
				break;
			}
			iter++;
		}
	}

	if ( type ) // remove the watcher
		this->ddbRemWatcher( id, type );

	// check our lists by item
	itemWatchers = this->DDBItemWatchers.begin();
	while ( itemWatchers != this->DDBItemWatchers.end() ) {
		iter = itemWatchers->second->begin();
		while ( iter != itemWatchers->second->end() ) {
			if ( (*iter) == *id ) {
				this->ddbRemWatcher( id, (UUID *)&itemWatchers->first );
				break;
			}
			iter++;
		}
	}

	return 0;
}

int Simulation::ddbRemWatcher( UUID *id, int type ) {
	int i, flag;
	mapDDBWatchers::iterator watchers;
	std::list<UUID>::iterator iter;
	
	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatchers.find( flag );

			if ( watchers == this->DDBWatchers.end() ) {
				continue; // no watchers for this type!
			}

			iter = watchers->second->begin();
			while ( iter != watchers->second->end() ) {
				if ( (*iter) == *id ) {
					watchers->second->erase( iter );
					return 0;
				}
				iter++;
			}
		}
	}

	return 1; // watcher not found!
}

int Simulation::ddbRemWatcher( UUID *id, UUID *item ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *item );
	std::list<UUID>::iterator iter;

	if ( watchers == this->DDBItemWatchers.end() ) {
		return 1; // no watchers for this item!
	}

	iter = watchers->second->begin();
	while ( iter != watchers->second->end() ) {
		if ( (*iter) == *id ) {
			watchers->second->erase( iter );
			return 0;
		}
		iter++;
	}

	return 1; // watcher not found!
}

int Simulation::ddbClearWatchers( UUID *id ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *id );
	std::list<UUID>::iterator iter;

	if ( watchers == this->DDBItemWatchers.end() ) {
		return 0; // no watchers for this item
	}

	watchers->second->clear();

	this->DDBItemWatchers.erase( watchers );

	return 0;
}

int Simulation::ddbClearWatchers() {
	mapDDBWatchers::iterator watchers;
	mapDDBItemWatchers::iterator iwatchers;
	std::list<UUID>::iterator iter;

	while ( !this->DDBWatchers.empty() ) {
		watchers = this->DDBWatchers.begin();
		watchers->second->clear();
		delete watchers->second;
		this->DDBWatchers.erase( watchers );
	}

	while ( !this->DDBItemWatchers.empty() ) {
		iwatchers = this->DDBItemWatchers.begin();
		iwatchers->second->clear();
		delete iwatchers->second;
		this->DDBItemWatchers.erase( iwatchers );
	}

	return 0;
}

int Simulation::ddbNotifyWatchers( int type, char evt, UUID *id, void *data, int len ) {
	
	// Not connected for now

	/*
	std::list<UUID>::iterator iter;
	mapAgentInfo::iterator iterAI;
	
	
	// by type
	mapDDBWatchers::iterator watchers = this->DDBWatchers.find( type );
	if ( watchers != this->DDBWatchers.end() && !watchers->second->empty() ) {
		this->ds->reset();
		this->ds->packInt32( type );
		this->ds->packUUID( id );
		this->ds->packChar( evt );
		if ( len )
			this->ds->packData( data, len );
		iter = watchers->second->begin();
		while ( iter != watchers->second->end() ) {
			mapAgentInfo::iterator iterAI = this->agentInfo.find(*iter);
			if ( iterAI == this->agentInfo.end() ) { // unknown agent
				return 1;
			} else if ( iterAI->second.con != NULL ) { // this is one of our agents
				this->sendMessage( iterAI->second.con, MSG_DDB_NOTIFY, this->ds->stream(), this->ds->length() );
			} else {  // forward this message to the appropriate host
				mapSimulationState::iterator iterAHS = this->hostKnown.find( iterAI->second.host );
				if ( iterAHS == this->hostKnown.end() ) {
					return 1; // unknown host
				}

				this->sendMessage( iterAHS->second->connection, MSG_DDB_NOTIFY, this->ds->stream(), this->ds->length(), &(UUID)*iter );
			}
			iter++;
		}
	}

	// by item
	mapDDBItemWatchers::iterator iwatchers = this->DDBItemWatchers.find( *id );
	if ( iwatchers != this->DDBItemWatchers.end() && !iwatchers->second->empty() ) {
		if ( watchers == this->DDBWatchers.end() || watchers->second->empty() ) { // data hasn't been packed yet
			this->ds->reset();
			this->ds->packInt32( type );
			this->ds->packUUID( id );
			this->ds->packChar( evt );
			if ( len )
				this->ds->packData( data, len );
		}
		iter = iwatchers->second->begin();
		while ( iter != iwatchers->second->end() ) {
			mapAgentInfo::iterator iterAI = this->agentInfo.find(*iter);
			if ( iterAI == this->agentInfo.end() ) { // unknown agent
				return 1;
			} else if ( iterAI->second.con != NULL ) { // this is one of our agents
				this->sendMessage( iterAI->second.con, MSG_DDB_NOTIFY, this->ds->stream(), this->ds->length() );
			} else {  // forward this message to the appropriate host
				mapSimulationState::iterator iterAHS = this->hostKnown.find( iterAI->second.host );
				if ( iterAHS == this->hostKnown.end() ) {
					return 1; // unknown host
				}

				this->sendMessage( iterAHS->second->connection, MSG_DDB_NOTIFY, this->ds->stream(), this->ds->length(), &(UUID)*iter );
			}
			iter++;
		}
	}
	*/

	return 0;
}

int Simulation::ddbAddRegion( UUID *id, float x, float y, float w, float h ) {

	this->dStore->AddRegion( id, x, y, w, h ); // add locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_REGION, DDBE_ADD, id );

	return 0;
}

int Simulation::ddbRemoveRegion( UUID *id ) {

	this->dStore->RemoveRegion( id ); // remove locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_REGION, DDBE_REM, id );
	this->ddbClearWatchers( id );

	return 0;
}

int Simulation::ddbGetRegion( UUID *id, DataStream *stream, int thread ) {

	this->dStore->GetRegion( id, stream, thread );

	return 0;
}

int Simulation::ddbAddLandmark( UUID *id, char code, UUID *owner, float height, float elevation, float x, float y ) {

	this->dStore->AddLandmark( id, code, owner, height, elevation, x, y ); // add locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_LANDMARK, DDBE_ADD, id );

	return 0;
}

int Simulation::ddbRemoveLandmark( UUID *id ) {

	this->dStore->RemoveLandmark( id ); // remove locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_LANDMARK, DDBE_REM, id );
	this->ddbClearWatchers( id );

	return 0;
}

int Simulation::ddbGetLandmark( UUID *id, DataStream *stream, int thread ) {

	this->dStore->GetLandmark( id, stream, thread );
	
	return 0;
}

int Simulation::ddbGetLandmark( char code, DataStream *stream, int thread ) {

	this->dStore->GetLandmark( code, stream, thread );
	
	return 0;
}

int Simulation::ddbAddPOG( UUID *id, float tileSize, float resolution ) {

	// verify that tileSize and resolution are valid
	if ( (float)floor(tileSize/resolution) != (float)(tileSize/resolution) )
		return 1; // tileSize must be an integer multiple of resolution

	this->dStore->AddPOG( id, tileSize, resolution ); // add locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_MAP_PROBOCCGRID, DDBE_ADD, id );

	return 0;
}

int Simulation::ddbRemovePOG( UUID *id ) {

	this->dStore->RemovePOG( id ); // remove locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_MAP_PROBOCCGRID, DDBE_REM, id );
	this->ddbClearWatchers( id );

	return 0;
}

int Simulation::ddbPOGGetInfo( UUID *id, DataStream *stream, int thread ) {

	this->dStore->POGGetInfo( id, stream, thread );

	return 0;
}

int Simulation::ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data ) {
	int updateSize;

	// verify that the cooridates are valid
	updateSize = this->dStore->POGVerifyRegion( id, x, y, w, h );
	if ( !updateSize ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	this->dStore->ApplyPOGUpdate( id, x, y, w, h, data ); // add locally

	// notify watchers (by type and POG uuid)
	this->ds->reset();
	this->ds->packFloat32( x );
	this->ds->packFloat32( y );
	this->ds->packFloat32( w );
	this->ds->packFloat32( h );
	this->ddbNotifyWatchers( DDB_MAP_PROBOCCGRID, DDBE_POG_UPDATE, id, this->ds->stream(), this->ds->length() );

	// TEMP dump the map
	/*{
		char filename[512];
		static int count = 0;
		sprintf_s( filename, 512, "data\\dump\\ddbApplyPOGUpdate%d.txt", count );
		count++;
		this->dStore->POGDumpRegion( id, -1, -1, 5, 7, filename );
	}*/

	// notify Explore supervisor
	this->supervisorExplore->notifyMap( id, DDBE_POG_UPDATE, this->ds->stream(), this->ds->length() );

	// visualize
	{
		float *newdata;
		float nx, ny, nw, nh, res;
		int r, c, rows, cols;
		int mapOffset[2];
		//this->dStore->POGGetRegion( id, x, y, w, h, this->ds, 0 );
		this->dStore->POGGetRegion( id, -4, -4, 12, 12, this->ds, 0 );
		//this->dStore->POGDumpRegion( id, -4, -4, 12, 12, "mapDump.txt" );
		this->ds->rewind();
		this->ds->unpackInt32(); // thread
		if ( this->ds->unpackChar() == DDBR_OK ) {
			nx = this->ds->unpackFloat32(); // x
			ny = this->ds->unpackFloat32(); // y
			nw = this->ds->unpackFloat32(); // w
			nh = this->ds->unpackFloat32(); // h
			res = this->ds->unpackFloat32(); // resolution
			newdata = (float *)this->ds->unpackData( sizeof(float)*(int)w*(int)h );

			mapOffset[0] = (int)((nx - MAP_X)*MAP_DIVISIONS + 0.5f);
			mapOffset[1] = (int)((ny - MAP_Y)*MAP_DIVISIONS + 0.5f);

			rows = (int)round(nh/res);
			cols = (int)round(nw/res);

			for ( r=0; r<rows; r++ ) {
				if ( r+mapOffset[1] < 0 )
					continue;
				if ( r+mapOffset[1] >= MAP_SIZE*MAP_DIVISIONS )
					break;
				for ( c=0; c<cols; c++ ) {
					if ( c+mapOffset[0] < 0 )
						continue;
					if ( c+mapOffset[0] >= MAP_SIZE*MAP_DIVISIONS )
						break;
					Px(map,r+mapOffset[1],c+mapOffset[0]) = newdata[r+c*rows];
					//Px(map,r+mapOffset[1],c+mapOffset[0]) *= 0.5f;
				}
			}
		}
	}

	return 0;
}

int Simulation::ddbPOGGetRegion( UUID *id, float x, float y, float w, float h, DataStream *stream, int thread ) {

	this->dStore->POGGetRegion( id, x, y, w, h, stream, thread );

	return 0;
}

int Simulation::ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename ) {
	FILE *fp;

	// verify that the cooridates are valid
	if ( !this->dStore->POGVerifyRegion( id, x, y, w, h ) ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	// make sure the file exists
	fopen_s( &fp, filename, "r" );
	if ( !fp ) {
		return 1; 
	}
	fclose( fp );

	this->dStore->POGLoadRegion( id, x, y, w, h, filename ); // add locally

	// notify watchers (by type and POG uuid)
	this->ds->reset();
	this->ds->packFloat32( x );
	this->ds->packFloat32( y );
	this->ds->packFloat32( w );
	this->ds->packFloat32( h );
	this->ddbNotifyWatchers( DDB_MAP_PROBOCCGRID, DDBE_POG_LOADREGION, id, this->ds->stream(), this->ds->length() );

	// notify Explore supervisor
	this->supervisorExplore->notifyMap( id, DDBE_POG_LOADREGION, this->ds->stream(), this->ds->length() );

	return 0;
}

int Simulation::ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize ) {
	int i;
	float colorP[3] = { 0.1f, 0.1f, 0.6f };
	float colorAvg[3] = { 0.0f, 0.0f, 1.0f };
	float colorTrue[3] = { 0.4f, 0.4f, 1.0f };
	float startWeight;
	float *pState;
	VisParticleFilter pf;

	// visualize
	pf.id = *id;
	pf.tb = *startTime;
	pf.particleNum = numParticles;
	pf.stateSize = stateSize;
	pf.objId = (int *)malloc( numParticles*sizeof(int) );
	if ( !pf.objId )
		return 1;
	pf.state = (float *)malloc( numParticles*stateSize*sizeof(float) );
	if ( !pf.state ) {
		free( pf.objId );
		return 1;
	}
	pf.scale = (float *)malloc( numParticles*sizeof(float) );
	if ( !pf.scale ) {
		free( pf.objId );
		free( pf.state );
		return 1;
	}
	memcpy( pf.state, startState, numParticles*stateSize*sizeof(float) );
	startWeight = 1.0f/numParticles;
	pState = pf.state;
	memset( pf.avgState, 0, sizeof(pf.avgState) );
	for ( i=0; i<numParticles; i++ ) {
		pf.avgState[0] += pState[0]*startWeight;
		pf.avgState[1] += pState[1]*startWeight;
		pf.avgState[2] += pState[2]*startWeight;

		pf.scale[i] = startWeight*numParticles;
		
		pf.objId[i] = this->visualizer->newDynamicObject( &pState[0], &pState[1], &pState[2], &pf.scale[i], this->pathIdParticle, colorP, 1 );

		pState += stateSize;
	}
	mapSimAvatar::iterator itAv = this->avatars.find( *owner );
	if ( itAv != this->avatars.end() ) {
		SimAvatarState *avState = itAv->second->getStatePtr();
		pf.trueState[0] = avState->x;
		pf.trueState[1] = avState->y;
		pf.trueState[2] = avState->r;
	} else {
		memset( pf.trueState, 0, sizeof(float)*3 );
	}
	
	this->particleFilters[*id] = pf;

	VisParticleFilter *pfPtr = &this->particleFilters[*id];
	pfPtr->avgObjId = this->visualizer->newDynamicObject( &pfPtr->avgState[0], &pfPtr->avgState[1], &pfPtr->avgState[2], this->pathIdRobot, colorAvg, 2 );
	pfPtr->trueObjId = this->visualizer->newDynamicObject( &pfPtr->trueState[0], &pfPtr->trueState[1], &pfPtr->trueState[2], this->pathIdRobot, colorTrue, 2 );
	

	this->dStore->AddParticleFilter( id, owner, numParticles, startTime, startState, stateSize ); // add locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_PARTICLEFILTER, DDBE_ADD, id );

	return 0;
}

int Simulation::ddbRemoveParticleFilter( UUID *id ) {
	int i;

	// remove from vis
	mapVisParticleFilter::iterator itPF = this->particleFilters.find( *id );
	if ( itPF != this->particleFilters.end() ) {
		for ( i=0; i<itPF->second.particleNum; i++ ) 
			this->visualizer->deleteObject( itPF->second.objId[i], true );

		this->visualizer->deleteObject( itPF->second.avgObjId, true );

		free( itPF->second.objId );
		free( itPF->second.state );
		free( itPF->second.scale );


		this->particleFilters.erase( itPF );
	}

	this->dStore->RemoveParticleFilter( id );  // remove locally

	// notify watchers
	this->ddbNotifyWatchers( DDB_PARTICLEFILTER, DDBE_REM, id );
	this->ddbClearWatchers( id );

	return 0;
}

int Simulation::ddbResampleParticleFilter( UUID *id ) {

	//-----------------------------------------------------------------------------------
	// Distributed algorithm goes something like this
	// initating host:
	// - request that leader does resample
	// leader:
	// - request any missing updates
	// - lock particle filter
	// - wait for all updates and lock acknowledgement
	// - do resample
	// - unlock particle filter and distribute results
	// other hosts (including initiator), on reciving lock:
	// - notify leader of all updates
	// - acknowledge lock
	// all hosts, while locked:
	// - hold any new corrections, but continue distributing predictions
	// all hosts, on unlock:
	// - add region
	// - apply held corrections
	// avatar, on receiving lock:
	// - acknowledge lock
	// - hold predictions
	// avatar, on unlock:
	// - submit predictions
	//
	// from now on if a new update comes in that is < latest resampling age it must be 
	// immediately propagated forward to the youngest resampling it is < than
	//-----------------------------------------------------------------------------------

	UUID uuid;
	_timeb *startTime;
	int particleNum, stateSize;
	int *parents;
	float *state;

	this->dStore->ResamplePF_Generate( id, this->ds ); // this function will pack the data stream that we send out below

	this->ds->rewind();
	this->ds->unpackUUID( &uuid );
	startTime = (_timeb *)this->ds->unpackData( sizeof(_timeb) );
	particleNum = this->ds->unpackInt32();
	stateSize = this->ds->unpackInt32();
	parents = (int *)this->ds->unpackData( sizeof(int)*particleNum );
	state = (float *)this->ds->unpackData( sizeof(float)*stateSize*particleNum );

	// apply the resample
	this->dStore->ResamplePF_Apply( id, startTime, parents, state );

	// distribute to owner
	UUID *ownerId = this->dStore->PFGetOwner( id );
	if ( !ownerId ) {
		return 1; // not found?
	}
	mapSimAvatar::iterator itAv = this->avatars.find( *ownerId );
	if ( itAv == this->avatars.end() ) {
		return 1; // not an avatar?!
	}
	itAv->second->pfResampled( startTime, parents, state );
	
	// notify watchers
	this->ddbNotifyWatchers( DDB_PARTICLEFILTER, DDBE_PF_RESAMPLE, id, (char *)startTime, sizeof(_timeb) );

	// visualize
	mapVisParticleFilter::iterator itPF = this->particleFilters.find( *id );
	if ( itPF != this->particleFilters.end() ) {
		int i;
		float *pState;
		VisParticleFilter *pf = &itPF->second;
		float weight = 1.0f/pf->particleNum;

		memcpy( pf->state, state, pf->particleNum*stateSize*sizeof(float) );
		memset( pf->avgState, 0, sizeof(pf->avgState) );
		pState = pf->state;
		for ( i=0; i<pf->particleNum; i++ ) {
			pf->avgState[0] += pState[0]*weight;
			pf->avgState[1] += pState[1]*weight;
			pf->avgState[2] += pState[2]*weight;

			pf->scale[i] = weight*pf->particleNum;

			pState += stateSize;
		}
	
		SimAvatarState *avState = itAv->second->getStatePtr();
		pf->trueState[0] = avState->x;
		pf->trueState[1] = avState->y;
		pf->trueState[2] = avState->r;
	}

	return 0;
}


int Simulation::ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange ) {
	int stateArraySize = this->dStore->PFGetStateArraySize( id );

	if ( !stateArraySize ) {
		return 1;
	}

	// TODO increment age

	this->dStore->InsertPFPrediction( id, tb, state, nochange ); // add locally

	// notify watchers (by type and pf uuid)
	this->ddbNotifyWatchers( DDB_PARTICLEFILTER, DDBE_PF_PREDICTION, id, (char *)tb, sizeof(_timeb) );

	// add for processing
	if ( !nochange && !this->skipCooccupancy )
		this->supervisorSLAM->addReading( DDB_PARTICLEFILTER, id, tb );

	// visualize
	mapVisParticleFilter::iterator itPF = this->particleFilters.find( *id );
	if ( itPF != this->particleFilters.end() && !nochange ) {
		int i;
		float *pState;
		float weight;
		VisParticleFilter *pf = &itPF->second;

		if ( (tb->time > pf->tb.time) || ( tb->time == pf->tb.time && tb->millitm > pf->tb.time ) ) {
			pf->tb = *tb;
			memcpy( pf->state, state, pf->particleNum*pf->stateSize*sizeof(float) );
			memset( pf->avgState, 0, sizeof(pf->avgState) );
			pState = pf->state;
			for ( i=0; i<pf->particleNum; i++ ) {
				weight = pf->scale[i]/pf->particleNum;
				pf->avgState[0] += pState[0]*weight;
				pf->avgState[1] += pState[1]*weight;
				pf->avgState[2] += pState[2]*weight;

				pState += pf->stateSize;
			}
		}

		UUID *ownerId = this->dStore->PFGetOwner( id );
		if ( ownerId ) {
			mapSimAvatar::iterator itAv = this->avatars.find( *ownerId );
			if ( itAv != this->avatars.end() ) {
				SimAvatarState *avState = itAv->second->getStatePtr();
				pf->trueState[0] = avState->x;
				pf->trueState[1] = avState->y;
				pf->trueState[2] = avState->r;
			}
		}
	}

	return 0;
}

int Simulation::ddbApplyPFCorrection( UUID *id, _timeb *tb, float *obsDensity ) {
	int particleNum = this->dStore->PFGetParticleNum( id );
	int regionAge = this->dStore->PFGetRegionCount( id );
	
	if ( !particleNum ) {
		return 1;
	}

	// TODO increment age

	this->dStore->ApplyPFCorrection( id, regionAge, tb, obsDensity ); // add locally

	// notify watchers (by type and pf uuid)
	this->ddbNotifyWatchers( DDB_PARTICLEFILTER, DDBE_PF_CORRECTION, id, (char *)tb, sizeof(_timeb) );

	// visualize
	mapVisParticleFilter::iterator itPF = this->particleFilters.find( *id );
	if ( itPF != this->particleFilters.end() ) {
		int i;
		float *pState;
		VisParticleFilter *pf = &itPF->second;

		// NOTE! This will force obsdensity propagation, so PF performance will be worse than in a real application
		this->ddbPFGetInfo( id, DDBPFINFO_WEIGHT, &pf->tb, this->ds, 0 );
		this->ds->rewind();
		this->ds->unpackInt32(); // thread
		if ( DDBR_OK == this->ds->unpackChar() ) {

			this->ds->unpackInt32(); // discard infoFlags

			memcpy( pf->scale, this->ds->unpackData( pf->particleNum*sizeof(float) ), pf->particleNum*sizeof(float) );
			memset( pf->avgState, 0, sizeof(pf->avgState) );
			pState = pf->state;
			for ( i=0; i<pf->particleNum; i++ ) {
				pf->avgState[0] += pState[0]*pf->scale[i];
				pf->avgState[1] += pState[1]*pf->scale[i];
				pf->avgState[2] += pState[2]*pf->scale[i];
				
				pf->scale[i] *= pf->particleNum;

				pState += pf->stateSize;
			}
		}
	}

	// TEMP
	//this->paused = true;

	return 0;
}

int Simulation::ddbPFGetInfo( UUID *id, int infoFlags, _timeb *tb, DataStream *stream, int thread ) {
	float effectiveParticleNum;

	this->dStore->PFGetInfo( id, infoFlags, tb, stream, thread, &effectiveParticleNum );

	if ( effectiveParticleNum != -1 && effectiveParticleNum < 0.3f ) {
		this->ddbResampleParticleFilter( id );
	}

	return 0;
}

int Simulation::ddbAddSensor( UUID *id, int type, UUID *avatar, void *pose, int poseSize ) {

	//this->dStore->AddSensor( id, type, avatar, pose, poseSize ); // add locally

	// notify watchers
	this->ddbNotifyWatchers( type, DDBE_ADD, id );

	return 0;
}

int Simulation::ddbRemoveSensor( UUID *id ) {
	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;

	this->dStore->RemoveSensor( id ); // remove locally

	// notify watchers
	this->ddbNotifyWatchers( type, DDBE_REM, id );
	this->ddbClearWatchers( id );

	return 0;
}

int Simulation::ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data, int dataSize ) {
	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;	

	// TODO increment age

	this->dStore->InsertSensorReading( id, tb, reading, readingSize, data, dataSize ); // add locally

	// notify watchers (by type and sensor uuid)
	this->ddbNotifyWatchers( type, DDBE_SENSOR_UPDATE, id, (void *)tb, sizeof(_timeb) );

	// add for processing
	this->supervisorSLAM->addReading( type, id, tb );
	
	return 0;
}

int Simulation::ddbSensorGetInfo( UUID *id, int infoFlags, DataStream *stream, int thread ) {

	this->dStore->SensorGetInfo( id, infoFlags, stream, thread );
	
	return 0;
}

int Simulation::ddbSensorGetData( UUID *id, _timeb *tb, DataStream *stream, int thread ) {

	this->dStore->SensorGetData( id, tb, stream, thread );
	
	return 0;
}


//-----------------
// Old stuff

struct WAYPOINT {
	float x;
	float y;
};


static int CELLID( short u, short v) { // convert coordinate into id (valid for coords -0x8FFF to 0x8FFF)
	int id;
	unsigned short a, b;
	a = u + 0x8FFF;
	b = v + 0x8FFF;
	memcpy( (void*)&id, (void*)&a, sizeof(short) );
	memcpy( ((byte*)&id) + sizeof(short), (void*)&b, sizeof(short) );
	return id;
}

#define CELLWEIGHT_AMBIENT	0.04f // WARNING! if this is set too low then updateWeightedPathLength can take a loooong time (0.04f seems to be a lower bound)
#define CELLWEIGHT(v) (CELLWEIGHT_AMBIENT + (1-(v))*(1-(v)))

int Simulation::updateWeightedPathLength( int cellX, int cellY, char fromDir, float wPathLength, std::map<int,TESTCELL*> *cellListed ) {
	int i;
	static int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	static int fromLookUp[4] = { 2, 3, 0, 1 };
	static std::map<int,TESTCELL*>::iterator iterCL;
	float mapVal;
	
	static int mapOffset[2] = { (int)((-1)*MAP_DIVISIONS + 0.5f), (int)((-1)*MAP_DIVISIONS + 0.5f) };

	for ( i=0; i<4; i++ ) {
		if ( (iterCL = cellListed->find(CELLID(cellX+dir[i][0],cellY+dir[i][1]))) != cellListed->end() ) {
			mapVal = Px(blur, 1+cellY+dir[i][1]-mapOffset[1], 1+cellX+dir[i][0]-mapOffset[0]);
			if ( iterCL->second->wPathLength > wPathLength + CELLWEIGHT(mapVal) ) {
				iterCL->second->wPathLength = wPathLength + CELLWEIGHT(mapVal);
				iterCL->second->fromDir = fromLookUp[i];
				updateWeightedPathLength( cellX+dir[i][0], cellY+dir[i][1], fromLookUp[i], wPathLength + CELLWEIGHT(mapVal), cellListed );
			}
		}
	}

	return 0;
}

int Simulation::nextCell( int cellX, int cellY, char fromDir, float targetX, float targetY, int pathLength, float wPathLength, std::map<int,TESTCELL*> *cellListed, std::multimap<float,TESTCELL*> *frontier ) {
	static int i;
	static int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	static int fromLookUp[4] = { 2, 3, 0, 1 };
	static float weight[4];
	static float mapVal, distFactor;
	static float xdist, ydist;
	static float mapResolution = 1.0f / MAP_DIVISIONS;
	static int ret;
	
	static float colourOK[3] = { 0.0, 1.0, 0.0 };
	static float colourNO[3] = { 1.0, 0.0, 0.0 };
	static float colourQUIT[3] = { 1.0, 1.0, 0.0 };
	static std::map<int,TESTCELL*>::iterator iterCL;
	static int maxPathLength = 60;
	static int foundPathLength = 9999999; // large
	static float bestDistSq;
	static float curDistSq;
	static int passes = 0;
	static TESTCELL *testCell;
	static int cellId;
	int targetCellX, targetCellY;
	targetCellX = (int)floor(targetX / mapResolution);
	targetCellY = (int)floor(targetY / mapResolution);

	passes++;

	static int mapOffset[2] = { (int)((-1)*MAP_DIVISIONS + 0.5f), (int)((-1)*MAP_DIVISIONS + 0.5f) };

	// mark every square we visit
	this->visualizer->newStaticObject( cellX*mapResolution, cellY*mapResolution, 0, 1, cellPath, colourNO, 1 );


	xdist = targetX/mapResolution - (cellX + 0.5f);
	ydist = targetY/mapResolution - (cellY + 0.5f);
	if ( pathLength >= maxPathLength || pathLength + sqrt(xdist*xdist + ydist*ydist) >= foundPathLength ) {
		this->visualizer->newStaticObject( cellX*mapResolution, cellY*mapResolution, 0, 1, cellPath, colourQUIT, 1 );
		return -1;
	}

	if ( fromDir == -1 ) { // this is the first cell so we have to list ourselves
		testCell = new TESTCELL;
		testCell->x = cellX;
		testCell->y = cellY;
		testCell->pathLength = 0;
		testCell->wPathLength = 0;
		testCell->fromDir = -1;
		(*cellListed)[CELLID(cellX,cellY)] = testCell;

		xdist = targetX - (cellX + 0.5f)*mapResolution;
		ydist = targetY - (cellY + 0.5f)*mapResolution;
		bestDistSq = xdist*xdist + ydist*ydist + 1;
	}

	xdist = targetX - (cellX + 0.5f)*mapResolution;
	ydist = targetY - (cellY + 0.5f)*mapResolution;
	curDistSq = xdist*xdist + ydist*ydist;
	if ( cellX == targetCellX && cellY == targetCellY ) {
		//this->visualizer->newStaticObject( cellX*mapResolution, cellY*mapResolution, 0, 1, cellPath, colourOK, 1 );
		foundX = cellX;
		foundY = cellY;
		foundFrom = fromDir;
		bestDistSq = 0;

		foundPathLength = pathLength;
	
		return pathLength; // we made it!
	} else if ( curDistSq < bestDistSq ) {
		foundX = cellX;
		foundY = cellY;
		foundFrom = fromDir;
		bestDistSq = curDistSq;
	}

	// weigh each direction
	for ( i=0; i<4; i++ ) {
		if ( i == fromDir ) {
			weight[i] = 0;
		} else {
			mapVal = Px(blur, 1+cellY+dir[i][1]-mapOffset[1], 1+cellX+dir[i][0]-mapOffset[0]);
			if ( mapVal <= 0.5f ) {
				weight[i] = 0;
			} else if ( (iterCL = cellListed->find(CELLID(cellX+dir[i][0],cellY+dir[i][1]))) != cellListed->end() ) {
				weight[i] = 0;
				if ( iterCL->second->wPathLength > wPathLength + CELLWEIGHT(mapVal) ) {
					iterCL->second->wPathLength = wPathLength + CELLWEIGHT(mapVal);
					iterCL->second->fromDir = fromLookUp[i];
					updateWeightedPathLength( cellX+dir[i][0], cellY+dir[i][1], fromLookUp[i], wPathLength + CELLWEIGHT(mapVal), cellListed );
				}
			} else {
				xdist = targetX - (cellX + dir[i][0] + 0.5f)*mapResolution;
				ydist = targetY - (cellY + dir[i][1] + 0.5f)*mapResolution;
				distFactor = 1/max( mapResolution/2, sqrt(xdist*xdist + ydist*ydist));
				weight[i] = mapVal * distFactor;
				testCell = new TESTCELL;
				testCell->x = cellX + dir[i][0];
				testCell->y = cellY + dir[i][1];
				testCell->pathLength = pathLength + 1;
				testCell->wPathLength = wPathLength + CELLWEIGHT(mapVal);
				testCell->fromDir = fromLookUp[i];
				cellId = CELLID(cellX+dir[i][0],cellY+dir[i][1]);
				(*cellListed)[CELLID(cellX+dir[i][0],cellY+dir[i][1])] = testCell;
				frontier->insert( std::pair<float,TESTCELL*>(weight[i], testCell) );
			}
		}
	}

	return -1;
}

float Simulation::calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, std::map<int,TESTCELL*> *cellListed ) {
	static int mapOffset[2] = { (int)((-1)*MAP_DIVISIONS + 0.5f), (int)((-1)*MAP_DIVISIONS + 0.5f) };
	
	float fullLen;
	float curLen = 0, newLen, vertLen, horzLen;
	float wlen = 0;
	float dx, dy;
	float fullx, fully;
	float curX, curY;
	float mapVal;
	int vertEdge, vertStep, horzEdge, horzStep;

	
	//float mapResolution = 1.0f / MAP_DIVISIONS;
	//float colour[3] = { 0.0f, 1.0f, 0.0f };
	//float xarray[2], yarray[2];
	//int path;

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

	curX = startX;
	curY = startY;
	while ( curLen < fullLen ) {
		// make sure this is a valid cell
		if ( cellListed->find(CELLID(cellX,cellY)) == cellListed->end() )
			return -1; // this cell is no good

		mapVal = Px(blur, 1+cellY-mapOffset[1], 1+cellX-mapOffset[0]);

		//xarray[0] = curX*mapResolution;
		//yarray[0] = curY*mapResolution;
	
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
		curX = startX + fullx * newLen/fullLen;
		curY = startY + fully * newLen/fullLen;

		//xarray[1] = curX*mapResolution;
		//yarray[1] = curY*mapResolution;
		//colour[1] = min( 1, newLen/fullLen );
		//path = this->visualizer->newPath( 2, xarray, yarray );
		//this->visualizer->newStaticObject( 0, 0, 0, 1, path, colour, 2 );

		// make sure we don't pass the end point
		if ( newLen > fullLen )
			newLen = fullLen;

		// add the weight
		wlen += CELLWEIGHT(mapVal)*(newLen-curLen);

		curLen = newLen;
	}

	return wlen;
}

int Simulation::shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, std::map<int,TESTCELL*> *cellListed ) {
	TESTCELL *baseCell;
	TESTCELL *endCell;
	TESTCELL *prevCell;
	TESTCELL *bestCell;
	float lastWLength;
	float wLength;
	float interLength;
	int badCells;

	static int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	static int mapOffset[2] = { (int)((-1)*MAP_DIVISIONS + 0.5f), (int)((-1)*MAP_DIVISIONS + 0.5f) };
	
	static std::map<int,TESTCELL*>::iterator iterCL;
	
	iterCL = cellListed->find(CELLID(*baseCellX,*baseCellY));
	baseCell = iterCL->second;
	iterCL = cellListed->find(CELLID(*baseCellX+dir[baseCell->fromDir][0],*baseCellY+dir[baseCell->fromDir][1]));
	endCell = iterCL->second;

	if ( endCell->x == destCellX && endCell->y == destCellY ) { // done already
		*baseCellX = destCellX;
		*baseCellY = destCellY;
		*baseX = destX;
		*baseY = destY;
		return 0;
	}

	lastWLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
	if ( lastWLength == -1 )
		return 0;

	interLength = 0;

	badCells = 0;
	prevCell = endCell;
	while ( endCell->x != destCellX || endCell->y != destCellY ) {
		

		iterCL = cellListed->find(CELLID(endCell->x+dir[endCell->fromDir][0],endCell->y+dir[endCell->fromDir][1]));
		endCell = iterCL->second;

		
		if ( endCell->x == destCellX && endCell->y == destCellY ) { 
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, destX, destY, cellListed );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, destX, destY, cellListed );
		} else {
			wLength = calcWeightedPathLength( *baseCellX, *baseCellY, *baseX, *baseY, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
			interLength += calcWeightedPathLength( prevCell->x, prevCell->y, prevCell->x + 0.5f, prevCell->y + 0.5f, endCell->x + 0.5f, endCell->y + 0.5f, cellListed );
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

int Simulation::planPath( float startX, float startY, float endX, float endY  ) {
	
	static std::map<int,TESTCELL*> cellListed;
	static std::map<int,TESTCELL*>::iterator iterCL;
	static std::multimap<float,TESTCELL*> frontier;
	static int dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	static int fromLookUp[4] = { 2, 3, 0, 1 };
	int ret;
	
	static float colourOK[3] = { 0.0, 1.0, 0.0 };
	static float colourNO[3] = { 1.0, 0.0, 0.0 };
	static float colourQUIT[3] = { 1.0, 1.0, 0.0 };
	static TESTCELL testCell;

	std::list<WAYPOINT> waypoints;
	WAYPOINT wp;
	int cellX, cellY, endCellX, endCellY;
	int startCellX, startCellY;
	char fromDir;

	int count;
	int path;
	float colour[3] = { 1.0, 0.0, 1.0 };
	float mapResolution = 1.0f / MAP_DIVISIONS;

	static float xcell[] = { 0, 0, mapResolution, mapResolution, 0 };
	static float ycell[] = { 0, mapResolution, mapResolution, 0, 0 };
	cellPath = this->visualizer->newPath( 5, xcell, ycell );

	int kernalWidth = 7;
	float kernalVals[] = { 0.05f, 0.1f, 0.2f, 0.3f, 0.2f, 0.1f, 0.05f };
	float *kernal = kernalVals + kernalWidth/2;
	float *px;
	static int blurOffset[] = { 7*MAP_DIVISIONS, 5*MAP_DIVISIONS };
	static int mapOffset[2] = { (int)((-1)*MAP_DIVISIONS + 0.5f), (int)((-1)*MAP_DIVISIONS + 0.5f) };
	int r, c, k;

	// initialize blur and blurBuf
	blurBuf = NewImage( 70, 50 );
	blur = NewImage( 70, 50 );

	// create horizontal blur in blurBuf
	for ( c=0; c<50; c++ ) {
		for ( r=0; r<70; r++ ) {
			px = &Px(blurBuf,r,c);
			*px = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( c+k < 0 || c+k >= 50 ) // we're outside the source
					*px += kernal[k]*0.5f;
				else
					*px += kernal[k]*Px(this->map,r+blurOffset[1],c+k+blurOffset[0]);
			}
		}
	}

	// create vertical blur in blur
	for ( c=0; c<50; c++ ) {
		for ( r=0; r<70; r++ ) {
			px = &Px(blur,r,c);
			*px = 0;
			for ( k=-kernalWidth/2; k<=kernalWidth/2; k++ ) {
				if ( r+k < 0 || r+k >= 70 ) // we're outside the source
					*px += kernal[k]*0.5f;
				else
					*px += kernal[k]*Px(blurBuf,r+k,c);
			}
		}
	}

	// draw on mission boundary
	cellX = (int)floor( 0 / mapResolution );
	cellY = (int)floor( 0 / mapResolution);
	endCellX = (int)floor( 3.3528f / mapResolution);
	endCellY = (int)floor( 4.8768f / mapResolution);
	c = cellX-1;
	for ( r=cellY-1; r<=endCellY+1; r++ ) {
		Px(blur,1+r-mapOffset[1],1+c-mapOffset[0]) = 0;
	}
	c = endCellX+1;
	for ( r=cellY-1; r<=endCellY+1; r++ ) {
		Px(blur,1+r-mapOffset[1],1+c-mapOffset[0]) = 0;
	}
	r=cellY-1;
	for ( c=cellX; c<=endCellX; c++ ) {
		Px(blur,1+r-mapOffset[1],1+c-mapOffset[0]) = 0;
	}
	r=endCellY+1;
	for ( c=cellX; c<=endCellX; c++ ) {
		Px(blur,1+r-mapOffset[1],1+c-mapOffset[0]) = 0;
	}
	

	
	this->visualizer->newfImage( -1, -1, 1.0f/MAP_DIVISIONS, 0, 12, blur );

	startCellX = (int)floor(startX / mapResolution);
	startCellY = (int)floor(startY / mapResolution);
	endCellX = (int)floor(endX / mapResolution);
	endCellY = (int)floor(endY / mapResolution);
	
	cellX = startCellX;
	cellY = startCellY;

	this->nextCell( cellX, cellY, -1, endX, endY, 0, 0, &cellListed, &frontier );
	// try the next best cell
	while ( !frontier.empty() ) {
		std::multimap<float,TESTCELL*>::iterator iter = --frontier.end();
		testCell = *iter->second;
		frontier.erase( iter );
		ret = nextCell( testCell.x, testCell.y, testCell.fromDir, endX, endY, testCell.pathLength, testCell.wPathLength, &cellListed, &frontier );
		//if ( ret > 0 ) maxPathLength = ret;
	}

	cellX = foundX;
	cellY = foundY;
	fromDir = foundFrom;
	this->visualizer->newStaticObject( cellX*mapResolution, cellY*mapResolution, 0, 1, cellPath, colourOK, 1 );
	int pathLength = 0;

	FILE *fp;
	char namebuf[256];
	sprintf_s( namebuf, 256, "simpath%d.txt", 0 );
	fopen_s( &fp, namebuf, "w" );
	fprintf( fp, "%d %d\n", cellX, cellY );
	while ( fromDir != -1 ) {
		
		cellX += dir[fromDir][0];
		cellY += dir[fromDir][1];

		iterCL = cellListed.find(CELLID(cellX,cellY));
		fromDir = iterCL->second->fromDir;
		fprintf( fp, "%d %d\n", cellX, cellY );
		this->visualizer->newStaticObject( cellX*mapResolution, cellY*mapResolution, 0, 1, cellPath, colourOK, 1 );
		pathLength++;
	}
	fclose( fp );

	if ( foundFrom != -1 ) {
		float baseX, baseY; // in cell coordinates
		int baseCellX, baseCellY;
		baseCellX = foundX;
		baseCellY = foundY;
		if ( foundX == endCellX && foundY == endCellY ) {
			baseX = endX / mapResolution;
			baseY = endY / mapResolution;
		} else {
			baseX = baseCellX + 0.5f;
			baseY = baseCellY + 0.5f;
		}
		while ( baseCellX != startCellX || baseCellY != startCellY ) {
			wp.x = baseX*mapResolution;
			wp.y = baseY*mapResolution;
			waypoints.push_front( wp );
			shortestPath( &baseCellX, &baseCellY, &baseX, &baseY, startCellX, startCellY, startX/mapResolution, startY/mapResolution, &cellListed );
		}
		wp.x = baseX*mapResolution;
		wp.y = baseY*mapResolution;
		waypoints.push_front( wp );
	} else { // we start and end in the same cell
		if ( startCellX == endCellX && startCellY == endCellY ) { // our destination is here
			wp.x = endX;
			wp.y = endY;
			waypoints.push_front( wp );
		} else { // try to drive a bit towards our destination
			float len;
			wp.x = endX - startX;
			wp.y = endY - startY;
			len = sqrt( wp.x*wp.x + wp.y*wp.y );
			wp.x = startX + wp.x/len * 0.15f; // 15 cm
			wp.y = startY + wp.y/len * 0.15f; // 15 cm
			waypoints.push_front( wp );
		}
		
		wp.x = startX;
		wp.y = startY;
		waypoints.push_front( wp );
	}

	// cleanup TESTCELL objects
	while ( !cellListed.empty() ) {
		iterCL = cellListed.begin();
		free( iterCL->second );
		cellListed.erase( iterCL );
	}

	float xarray[500], yarray[500];
	std::list<WAYPOINT>::iterator iter = waypoints.begin();
	count = 0;
	while ( iter != waypoints.end() ) {
		xarray[count] = iter->x;
		yarray[count] = iter->y;
		
		count++;
		iter++;
	}
	path = this->visualizer->newPath( count, xarray, yarray );
	this->visualizer->newStaticObject( 0, 0, 0, 1, path, colour, 1 );

	return 0;
}

UUID Simulation::addAvatar( float x, float y, float r, char *avatarFile, char *name, float *color ) {
	SimAvatarState state;
	UUID id;

	state.t.time = 0;
	state.t.millitm = 0;
	state.x = x;
	state.y = y;
	state.r = r;
	state.vL = 0;
	state.vR = 0;

	SimAvatar *simAvatar = new SimAvatar( this, this->visualizer, this->randGen, name, color );

	id = *simAvatar->getUUID();

	if ( simAvatar->configure( &state, avatarFile, &this->mapId, &this->missionRegionId ) ) {
		UuidCreateNil( &id );
		return id; // error
	}

	if ( this->supervisorSLAM->getProcessingOrder() == SimSLAM::PO_FIFO_DELAY )
		simAvatar->useDelayActions( true );

	this->avatars[id] = simAvatar;

	simAvatar->start();

	// add to explore supervisor
	this->supervisorExplore->addAvatar( simAvatar->getUUID(), avatarFile, simAvatar );

	return id;
}

int Simulation::removeAvatar( UUID id, bool ignoreVis ) {
	mapSimAvatar::iterator it = this->avatars.find( id );

	if ( it == this->avatars.end() )
		return 1; // not found

	delete it->second;

	this->avatars.erase( it );

	return 0;
}

int Simulation::_setAvatarPos( float x, float y, float r ) {

	SimAvatarState *aState = this->avatars.begin()->second->getStatePtr();
	
	aState->x = x;
	aState->y = y;

	if ( r != 99.9f ) {
		aState->r = r;
	}

	return 0;
}

/*
Avatar * Simulation::addAvatar( char *poseFileN ) {
	int i;
	ParticleRegion *pr;
	std::list<Particle*>::iterator p;
	std::list<void*>::iterator stateIter;
	ParticleState_Avatar *pState;
		
	Avatar *avatar = (Avatar *)malloc(sizeof(Avatar));

	if ( avatar == NULL )
		return NULL;

	// open pose file
	fopen_s( &avatar->poseF, poseFileN, "r" );
	if ( avatar->poseF == NULL ) {
		free( avatar );
		return NULL;
	}

	// read initial position
	if ( 4 != fscanf_s( avatar->poseF, "%f\t%f\t%f\t%f\n", &avatar->nextT, &avatar->nextX, &avatar->nextY, &avatar->nextR ) ) {
		fclose( avatar->poseF );
		free( avatar );
		return NULL; // bad pose info
	}
	avatar->t = 0;
	avatar->x = avatar->nextX;
	avatar->y = avatar->nextY;
	avatar->r = avatar->nextR;
	avatar->s = 1.0f;

	avatar->nextPrediction = PREDICTION_PERIOD;
	avatar->lastT = 0;
	avatar->lastX = avatar->x;
	avatar->lastY = avatar->y;
	avatar->lastR = avatar->r;

	float colour[3] = { 0.0, 1.0, 0.0 };
	avatar->object = this->visualizer->newDynamicObject( &avatar->x, &avatar->y, &avatar->r, &avatar->s, this->pathIdRobot, colour, 2 );

	if ( avatar->object == -1 ) {
		fclose( avatar->poseF );
		free( avatar );
		return NULL; // failed to create object
	}

	// create particle filter
	_timeb tb;
	tb.time = 0;
	tb.millitm = 0;
	avatar->filter = CreateFilter( NUM_PARTICLES, &tb, generateParticleState_Avatar, avatar );

	// create particle objects
	colour[0] = 1;
	colour[1] = 0;
	colour[2] = 1;
	i = 0;
	pr = avatar->filter->regions->front();
	p = pr->particles->begin();
	stateIter = pr->states->front()->begin();
	while ( p != pr->particles->end() ) {
		pState = (ParticleState_Avatar *)*stateIter;
		avatar->particleX[i] = pState->x;
		avatar->particleY[i] = pState->y;
		avatar->particleR[i] = pState->r;
		avatar->particleS[i] = (*p)->weight*NUM_PARTICLES;

		avatar->particleObj[i] = this->visualizer->newDynamicObject( &avatar->particleX[i], &avatar->particleY[i], &avatar->particleR[i], &avatar->particleS[i], this->pathIdParticle, colour, 1 );

		i++;
		p++;
		stateIter++;
	}

	avatar->cameraCount = 0;
	for ( i=0; i<AVATAR_MAX_CAMERAS; i++ )
		avatar->cameraData[i] = NULL;

	avatar->sonarCount = 0;
	for ( i=0; i<AVATAR_MAX_SONAR; i++ )
		avatar->sonarData[i] = NULL;

	this->avatars.push_back( avatar );

	return avatar;
}

int	Simulation::removeAvatar( Avatar *avatar, bool ignoreVis ) {
	int i;
	std::list<CameraReading *>::iterator iterCR;
	std::list<SonarReading *>::iterator iterSR;

	this->avatars.remove( avatar );

	fclose( avatar->poseF );
	FreeFilter( avatar->filter );

	if ( !ignoreVis ) {
		this->visualizer->deleteObject( avatar->object, true );
		for ( i=0; i<NUM_PARTICLES; i++ ) {
			this->visualizer->deleteObject( avatar->particleObj[i], true );
		}
	}

	for ( i=0; i<avatar->cameraCount; i++ ) {
		avatar->cameraPose[i].rr_FloorFinder->close();
		delete avatar->cameraPose[i].rr_FloorFinder;
		this->releasePort( avatar->cameraPose[i].rr_FloorFinderPort );
		
		if ( avatar->cameraData[i] != NULL ) {
			iterCR = avatar->cameraData[i]->begin();
			while ( iterCR != avatar->cameraData[i]->end() ) {
				free( (*iterCR)->data );
				free( *iterCR );
				iterCR++;
			}
			delete avatar->cameraData[i];
		}
	}

	for ( i=0; i<avatar->sonarCount; i++ ) {
		if ( avatar->sonarData[i] != NULL ) {
			iterSR = avatar->sonarData[i]->begin();
			while ( iterSR != avatar->sonarData[i]->end() ) {
				free( *iterSR );
				iterSR++;
			}
			delete avatar->sonarData[i];
		}
	}

	free( avatar );

	return 0;
}
*/

// -- Camera Sensor --
/*
int Simulation::avatarConfigureCameras( Avatar *avatar, int cameraCount, char *dataFileN, char *poseFileN, char *imagePath ) {
	FILE *dataF, *poseF, *imageF;
	int i;
	int width, height, size;
	float time;
	char imageName[1024], imageFullName[1024];
	char imageFormat[IMAGEFORMAT_SIZE];
	char buf[256];
	CameraReading *cameraReading;
	
	int useCount = min( cameraCount, AVATAR_MAX_CAMERAS );

	// configure pose
	fopen_s( &poseF, poseFileN, "r" );
	if ( poseF == NULL ) {
		return 1;
	}
	i = 0;
	while ( i < useCount ) {
		if ( 24 != fscanf_s( poseF, "%f %f %f %f %f %d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", 
			  &avatar->cameraPose[i].x, &avatar->cameraPose[i].y, &avatar->cameraPose[i].r,
			  &avatar->cameraPose[i].sigma, &avatar->cameraPose[i].horizon, &avatar->cameraPose[i].frameH,
			  &avatar->cameraPose[i].H[0], &avatar->cameraPose[i].H[1], &avatar->cameraPose[i].H[2],
			  &avatar->cameraPose[i].H[3], &avatar->cameraPose[i].H[4], &avatar->cameraPose[i].H[5],
			  &avatar->cameraPose[i].H[6], &avatar->cameraPose[i].H[7], &avatar->cameraPose[i].H[8],
			  &avatar->cameraPose[i].G[0], &avatar->cameraPose[i].G[1], &avatar->cameraPose[i].G[2],
			  &avatar->cameraPose[i].G[3], &avatar->cameraPose[i].G[4], &avatar->cameraPose[i].G[5],
			  &avatar->cameraPose[i].G[6], &avatar->cameraPose[i].G[7], &avatar->cameraPose[i].G[8] ) ) {
			fclose( poseF );
			return 1;
		}
		avatar->cameraPose[i].r = (float)DegreesToRadians( avatar->cameraPose[i].r );

		// launch roboRealm for FloorFinding
		avatar->cameraPose[i].rr_FloorFinderPort = this->getFreePort();
		if ( avatar->cameraPose[i].rr_FloorFinderPort < 0 ) {
			fclose( poseF );
			return 1;
		}

		avatar->cameraPose[i].rr_FloorFinder = new RR_API();
		if ( avatar->cameraPose[i].rr_FloorFinder == NULL ) {
			fclose( poseF );
			this->releasePort( avatar->cameraPose[i].rr_FloorFinderPort );
			return 1;
		}

		sprintf_s( buf, 256, "-api_port %d", avatar->cameraPose[i].rr_FloorFinderPort );
		avatar->cameraPose[i].rr_FloorFinder->open( _T("RoboRealm\\RoboRealm.exe"), buf, avatar->cameraPose[i].rr_FloorFinderPort );
		avatar->cameraPose[i].rr_FloorFinder->connect( "localhost", avatar->cameraPose[i].rr_FloorFinderPort);
		
		avatar->cameraPose[i].rr_FloorFinder->loadProgram( "RRscripts\\FLOORFINDER256BMP.robo" );

		avatar->cameraPose[i].rr_FloorFinderHorizon = (int)(256*avatar->cameraPose[i].horizon);
		sprintf_s( buf, 256, "%d", avatar->cameraPose[i].rr_FloorFinderHorizon );
		avatar->cameraPose[i].rr_FloorFinder->setVariable( "CROPHORIZON", buf );

		i++;
	}
	fclose( poseF );

	fopen_s( &dataF, dataFileN, "r" );
	if ( dataF == NULL ) {
		return 1;
	}

	// allocate cameraData
	for ( i=0; i<useCount; i++ ) {
		avatar->cameraData[i] = new std::list<CameraReading *>;
		if ( avatar->cameraData[i] == NULL ) {
			goto cleanfail;
		}
	}

	// read dataF
	while ( 1 ) {
		if ( 1 != fscanf_s( dataF, "%f", &time ) ) {
			break; // expected time, we must be finished
		}
		if ( 5 != fscanf_s( dataF, "%d %d %d %s %s", &i, &width, &height, imageFormat, sizeof(imageFormat), imageName, sizeof(imageName) ) ) {
			goto cleanfail; // expect camera index, width, height, format, image name
		}

		if ( i < AVATAR_MAX_CAMERAS ) {
			cameraReading = (CameraReading *)malloc(sizeof(CameraReading));
			if ( cameraReading == NULL ) {
				goto cleanfail;
			}
			cameraReading->tb.time = ((int)(time*1000)) / 1000;
			cameraReading->tb.millitm = ((int)(time*1000)) % 1000;
			
			cameraReading->w = width;
			cameraReading->h = height;
			strcpy_s( cameraReading->format, sizeof(cameraReading->format), imageFormat );

			sprintf_s( imageFullName, sizeof(imageFullName), "%s\\%s", imagePath, imageName );
			fopen_s( &imageF, imageFullName, "rb" );
			if( imageF == NULL ) {
				free( cameraReading );
				goto cleanfail;
			}

			/*
			if ( !strcmp( imageFormat, "bmp" ) ) { // http://en.wikipedia.org/wiki/BMP_file_format
				int offset;
				fread( buf, 1, 0x000A, imageF );
				fread( &offset, 1, 4, imageF );
				fread( buf, 1, offset-0x000A-4, imageF );
				size = 3*width*height;
			} else if ( !strcmp( imageFormat, "jpg" ) ) {
				fseek( imageF, 0, SEEK_END );
				size = ftell( imageF );
				fseek( imageF, 0, SEEK_SET );
			} else {
				// unsupported format
				free( cameraReading );
				fclose( imageF );
				goto cleanfail;
			}***

			fseek( imageF, 0, SEEK_END );
			size = ftell( imageF );
			fseek( imageF, 0, SEEK_SET );

			cameraReading->data = (char *)malloc(size);
			if ( cameraReading->data == NULL ) {
				fclose( imageF );
				free( cameraReading );
				goto cleanfail;
			}
			if ( size != fread( cameraReading->data, 1, size, imageF ) ) {
				free( cameraReading->data );
				fclose( imageF );
				free( cameraReading );
				goto cleanfail;
			}
			fclose( imageF );

			cameraReading->size = size;

			// TEMP
			//fopen_s( &imageF, "test.jpg", "wb" );
			//fwrite( cameraReading->data, 1, filelength, imageF );
			//fclose( imageF );

			avatar->cameraData[i]->push_back( cameraReading );
		}

	}
	fclose( dataF );

	avatar->cameraCount = useCount;

	for ( i=0; i<useCount; i++ ) {
		if ( this->nextSensorTime > 
			 avatar->cameraData[i]->front()->tb.time*1000 + avatar->cameraData[i]->front()->tb.millitm + SENSOR_DELAY )
			this->nextSensorTime = (int)avatar->cameraData[i]->front()->tb.time*1000 + (int)avatar->cameraData[i]->front()->tb.millitm + SENSOR_DELAY;
	}

	return 0;

cleanfail:
	fclose( dataF );

	std::list<CameraReading *>::iterator iter;
	for ( i=0; i<useCount; i++ ) {
		if ( avatar->cameraData[i] != NULL ) {
			iter = avatar->cameraData[i]->begin();
			while ( iter != avatar->cameraData[i]->end() ) {
				free( (*iter)->data );
				free( *iter );
				iter++;
			}
			delete avatar->cameraData[i];
		}
	}

	return 1;
}


int Simulation::correctPF_AvatarCamera( ParticleFilter *pf, CameraReading *reading, CameraPose *pose ) {
	std::list<ParticleRegion*>::iterator pr;
	std::list<Particle*>::iterator p;
	std::list<std::list<void*>*>::iterator psBefore;
	std::list<std::list<void*>*>::iterator psAfter;
	std::list<_timeb*>::iterator tbBefore;
	std::list<_timeb*>::iterator tbAfter;
	std::list<void*>::iterator stateBefore;
	std::list<void*>::iterator stateAfter;
	ParticleState_Avatar state;
	float interp;
	int border;
	int i, j;
	int r, c;
	int div;
	float sn, cs;

	float PFbound[4];
	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	bool  rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date

	float penalty;
	float maxAbsoluteOd;
	float absoluteNorm;
	float sumWeightedObs;
	float particleNumEff;

	generateCameraTemplate( reading, pose, this->cameraT, this->cameraContours );
	
	// find region and bounding particles
	pr = pf->regions->begin();
	while ( pr != pf->regions->end() ) {
		if ( (*pr)->startTime.time < reading->tb.time 
	    || ( (*pr)->startTime.time == reading->tb.time && (*pr)->startTime.millitm < reading->tb.millitm ) )
			break;
		pr++;
	}
	if ( pr == pf->regions->end() )
		return 1;

	tbBefore = (*pr)->times->begin();
	psBefore = (*pr)->states->begin();
	tbAfter = tbBefore;
	psAfter = psBefore;
	while ( tbBefore != (*pr)->times->end() ) {
		if ( (*tbBefore)->time < reading->tb.time 
	    || ( (*tbBefore)->time == reading->tb.time && (*tbBefore)->millitm < reading->tb.millitm ) )
			break;
		tbAfter = tbBefore;
		psAfter = psBefore;
		tbBefore++;
		psBefore++;
	}

	if ( tbAfter == tbBefore ) { // we're at the top of the stack!?
		return 1;
	}

	interp = ( (reading->tb.time - (*tbBefore)->time)*1000 + (reading->tb.millitm - (*tbBefore)->millitm) ) / (float)( ((*tbAfter)->time - (*tbBefore)->time)*1000 + ((*tbAfter)->millitm - (*tbBefore)->millitm) );

	// update PFbound
	// TODO ignore particles with very little weight?
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	PFbound[0] = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
	PFbound[1] = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
	PFbound[2] = 0;
	PFbound[3] = 0;
	stateBefore++;
	stateAfter++;
	while ( stateBefore != (*psBefore)->end() ) {
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		if ( PFbound[0] > state.x ) {
			PFbound[2] = PFbound[0] + PFbound[2] - state.x;
			PFbound[0] = state.x;
		} else if ( PFbound[0] + PFbound[2] < state.x ) {
			PFbound[2] = state.x - PFbound[0];
		}
		if ( PFbound[1] > state.y ) {
			PFbound[3] = PFbound[1] + PFbound[3] - state.y;
			PFbound[1] = state.y;
		} else if ( PFbound[1] + PFbound[3] < state.y ) {
			PFbound[3] = state.y - PFbound[1];
		}
		stateBefore++;
		stateAfter++;
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[0] = floor(PFbound[0]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[1] = floor(PFbound[1]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[3] -= PFbound[1];
	
	// prepare map update
	border = (int)ceil(5 + MAP_DIVISIONS*CAMERA_TEMPLATE_LENGTH);
	mapUpdateLoc[0] = PFbound[0] - border/(float)MAP_DIVISIONS;
	mapUpdateLoc[1] = PFbound[1] - (ceil(PFbound[3]*MAP_DIVISIONS) + border)/(float)MAP_DIVISIONS;
	if ( mapUpdate->bufSize < (int)sizeof(float)*((int)(PFbound[2]*MAP_DIVISIONS)+border*2) * ((int)(PFbound[3]*MAP_DIVISIONS)+border*2) ) {
		// allocate more space for the map update
		ReallocImage( mapUpdate, (int)(PFbound[3]*MAP_DIVISIONS)+border*2, (int)(PFbound[2]*MAP_DIVISIONS)+border*2 );
	} else {
		mapUpdate->rows = ((int)(PFbound[3]*MAP_DIVISIONS)+border*2);
		mapUpdate->cols = ((int)(PFbound[2]*MAP_DIVISIONS)+border*2);
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	// clear the rotationDivision flags
	memset( rotationDivision, 0, sizeof(rotationDivision) );
	
	// for each particle
	maxAbsoluteOd = 0;
	p = (*pr)->particles->begin();
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	while ( p != (*pr)->particles->end() ) {
		// interpolate state
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		state.r = ((ParticleState_Avatar*)*stateBefore)->r + (((ParticleState_Avatar*)*stateAfter)->r - ((ParticleState_Avatar*)*stateBefore)->r)*interp;
		// adjust for sensor pose
		sn = (float)sin(state.r);
		cs = (float)cos(state.r);
		state.x += cs*pose->x - sn*pose->y;
		state.y += sn*pose->x + cs*pose->y;
		state.r += pose->r;
		while ( state.r < 0 ) state.r += 2*fM_PI;
		while ( state.r > 2*fM_PI ) state.r -= 2*fM_PI;
		div = (int)(ROTATION_DIVISIONS*(state.r + ROTATION_RESOLUTION*0.5)/(2*fM_PI)) % ROTATION_DIVISIONS;
		// rotate
		if ( rotationDivision[div] == false ) {
			RotateImageEx( cameraT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), cameraR[div], integralTarget, 0 );
			rotationDivision[div] = true;
		}
		origin[0] = cameraR[div]->cols/2.0f - cameraT->cols/2.0f*(float)cos(div*ROTATION_RESOLUTION);
		origin[1] = cameraR[div]->rows/2.0f - cameraT->cols/2.0f*(float)sin(div*ROTATION_RESOLUTION);
		
		// compare against map
		offset[0] = (state.x - MAP_X)*MAP_DIVISIONS - origin[0]/CAMERA_TEMPLATE_DIVISIONS*MAP_DIVISIONS;
		offset[1] = (state.y - MAP_Y)*MAP_DIVISIONS - origin[1]/CAMERA_TEMPLATE_DIVISIONS*MAP_DIVISIONS;
		(*p)->absoluteOd = 0;
		for ( j=0; j<cameraR[div]->cols; j++ ) {
			c = (int)(j*MAP_DIVISIONS/(float)CAMERA_TEMPLATE_DIVISIONS + offset[0]);
			for ( i=0; i<cameraR[div]->rows; i++ ) {
				r = (int)(i*MAP_DIVISIONS/(float)CAMERA_TEMPLATE_DIVISIONS + offset[1]);
				// update absoluteOd, total penalty
				if ( Px(cameraR[div],i,j) < 0 ) {
					penalty = max( 0, Px(map,r,c) - 0.5f ) * -Px(cameraR[div],i,j);
				} else {
					penalty = max( 0, 0.5f - Px(map,r,c) ) * Px(cameraR[div],i,j);
				}
				(*p)->absoluteOd += penalty;
			}
		}
		maxAbsoluteOd = max( maxAbsoluteOd, (*p)->absoluteOd );

		// TEMP
		//offset[0] = (state.x - mapLoc[0])*MAP_DIVISIONS;
		//offset[1] = (state.y - mapLoc[1])*MAP_DIVISIONS;
		//map[(int)offset[0]][(int)offset[1]] = 0;

		// stamp on update
		float absoluteScale = 1.0f;
		offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS - origin[0]/CAMERA_TEMPLATE_DIVISIONS*MAP_DIVISIONS;
		offset[1] = (state.y - mapUpdateLoc[1])*MAP_DIVISIONS - origin[1]/CAMERA_TEMPLATE_DIVISIONS*MAP_DIVISIONS;
		ImageAdd( cameraR[div], mapUpdate, offset[0], offset[1], MAP_DIVISIONS/(float)CAMERA_TEMPLATE_DIVISIONS, (*p)->weight*absoluteScale, 0 );
		
		// TEMP
		//this->visualizer->deletefImage( this->mapUpdateImage );
		//this->mapUpdateImage = this->visualizer->newfImage( mapUpdateLoc[0], mapUpdateLoc[1], 1.0f/MAP_DIVISIONS, 0, 50, this->mapUpdate );
		//offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS;
		//offset[1] = (state.y - mapUpdateLoc[1])*MAP_DIVISIONS;
		//Px(mapUpdate,(int)offset[1],(int)offset[0]) = 1;

		p++;
		stateBefore++;
		stateAfter++;
	}
	// apply observations
	if ( maxAbsoluteOd > 0 ) absoluteNorm = 1/maxAbsoluteOd;
	else absoluteNorm = -1.0f;
	sumWeightedObs = 0;
	p = (*pr)->particles->begin();
	while ( p != (*pr)->particles->end() ) {
		if ( absoluteNorm != -1.0f ) 
			(*p)->obsDensity *= (*p)->absoluteOd*absoluteNorm;
		sumWeightedObs += (*p)->weight * (*p)->obsDensity;
		p++;
	}
	p = (*pr)->particles->begin();
	particleNumEff = 0;
	while ( p != (*pr)->particles->end() ) {
		(*p)->weight = (*p)->weight * (*p)->obsDensity / sumWeightedObs;
		(*p)->obsDensity = 1;
		particleNumEff += (*p)->weight * (*p)->weight;
		p++;
	}
	particleNumEff = 1/particleNumEff;
	if ( particleNumEff < MIN_EFF_PARTICLES && pr ==  pf->regions->begin() )
		resampleParticleFilter( pf, copyParticleState_Avatar );

	// apply update to map
	float pC, pSC, pS;
	int mapOffset[2];
	mapOffset[0] = (int)((mapUpdateLoc[0] - MAP_X)*MAP_DIVISIONS + 0.5f);
	mapOffset[1] = (int)((mapUpdateLoc[1] - MAP_Y)*MAP_DIVISIONS + 0.5f);
	for ( r=0; r<mapUpdate->rows; r++ ) {
		if ( r+mapOffset[1] < 0 )
			continue;
		if ( r+mapOffset[1] >= MAP_SIZE*MAP_DIVISIONS )
			break;
		for ( c=0; c<mapUpdate->cols; c++ ) {
			if ( c+mapOffset[0] < 0 )
				continue;
			if ( c+mapOffset[0] >= MAP_SIZE*MAP_DIVISIONS )
				break;
			pSC = min( 0.95f, max( 0.05f, Px(mapUpdate,r,c) ) );
			pC = Px(map,r+mapOffset[1],c+mapOffset[0]);
			pS = pC*pSC + (1-pC)*(1-pSC);
			Px(map,r+mapOffset[1],c+mapOffset[0]) = pC*pSC/pS;
		}
	}

	return 0;
}

int charHexToInt( char *hex ) {
	char high, low;
	if ( hex[0] >= 'a' )		high = hex[0] - 'a' + 10;
	else if ( hex[0] >= 'A' )	high = hex[0] - 'A' + 10;
	else						high = hex[0] - '0';
	if ( hex[1] >= 'a' )		low = hex[1] - 'a' + 10;
	else if ( hex[1] >= 'A' )	low = hex[1] - 'A' + 10;
	else						low = hex[1] - '0';
	return high*16 + low;
}

void applyHomography( float *H, float px, float py, float *qx, float *qy ) {
	float norm = 1.0f/(H[6]*px + H[7]*py + H[8]);
	*qx = (H[0]*px + H[1]*py + H[2])*norm;
	*qy = (H[3]*px + H[4]*py + H[5])*norm;
}

int generateCameraTemplate( CameraReading *reading, CameraPose *pose, FIMAGE *cameraT, FIMAGE *cameraContours ) {
	unsigned char pixels[256000];
	char colourStr[256];
	unsigned char high; // highest pixel value we expect in the processed image
	int width, height, pstride;
	int r, c;
	float var = pose->sigma*pose->sigma; // variances (in floor space)
	float expon;
	float norm;
	int bmpoffset;
	
	// fake the normal distribution norm so that anything below 0.016f has a peak at 1, 
	// and everything larger than that is smaller
	if ( pose->sigma < 0.016f ) {
		norm = 1.0f;
	} else {
		norm = 0.016f/pose->sigma;
	}	

	// make sure horizon is set correctly
	if ( pose->rr_FloorFinderHorizon != (int)(reading->h*pose->horizon) ) {
		char buf[256];	
		pose->rr_FloorFinderHorizon = (int)(256*pose->horizon);
		sprintf_s( buf, 256, "%d", pose->rr_FloorFinderHorizon );
		pose->rr_FloorFinder->setVariable( "CROPHORIZON", buf );
	}

	// setImage in RR
	if ( !strcmp( reading->format, "bmp" ) ) {
		bmpoffset = *(int *)(reading->data+0x000A);
		pose->rr_FloorFinder->setImage( (unsigned char *)reading->data + bmpoffset, reading->w, reading->h,  true );
	} else if ( !strcmp( reading->format, "jpg" ) ) {
		CxImage image((BYTE*)reading->data,reading->size,CXIMAGE_FORMAT_JPG);
		CxMemFile memfile;
		memfile.Open();
		image.Encode(&memfile,CXIMAGE_FORMAT_BMP);
		BYTE *encbuffer = memfile.GetBuffer();
		
		bmpoffset = *(int *)(encbuffer+0x000A);
		pose->rr_FloorFinder->setImage( (unsigned char *)encbuffer + bmpoffset, reading->w, reading->h,  true );

		image.FreeMemory(encbuffer);
		memfile.Close();
	} else {
		return 1; // unsupported format
	}
	//pose->rr_FloorFinder->setCompressedImage( (unsigned char *)reading->data, reading->size,  true );

	// getImage from RR
	pose->rr_FloorFinder->getImage( "processed", pixels, &width, &height, sizeof(pixels) );
	pose->rr_FloorFinder->getVariable( "COLOR_SAMPLE", colourStr, sizeof(colourStr) );

	high = (charHexToInt(colourStr) + charHexToInt(colourStr+2) + charHexToInt(colourStr+4))/3;

	// create vertical line contours
	// Notes:
	// 1. pixel format is RGB horizontal from top left corner
	// 2. the R value of the first pixel gets overwritten with \0 by RR due to a bug
	// 3. the FLOORFINDER script scales the image horizontally to 10% and rotates it 270 degrees
	//    (i.e. for vertical camera lines just read along the pixels)
	if ( width*height > cameraContours->bufSize/(int)sizeof(float) ) { // we need more space
		ReallocImage( cameraContours, height, width );
	} else {
		cameraContours->rows = height;
		cameraContours->cols = width;
		cameraContours->stride = cameraContours->rows;
	}
	memset( cameraContours->data, 0, width*height*sizeof(float) );

	float val;
	float cutoffC, cutoffX, cutoffY, cutoffU, cutoffV, cutoffD;	// cutoff pixel coordinates (top of the hill)
	float startC, startX, startY, startU, startV, startD;		// start of the hill and gaussian
	float endC, endX, endY, endU, endV, endD;					// end of the gaussian (hill ends at cutoff)
	float X, Y, U, V, D;										// current pixel
	float gauss, hill;
	bool foundhit; // did we find a hit?
	for ( r=0; r<cameraContours->rows; r++ ) {
		pstride = (height-r-1)*width;
		// search for the cut off
		foundhit = false;
		for ( c=0; c<cameraContours->cols; c++ ) {
			val = pixels[3*(c+pstride)+1]/(float)high;
			if ( val < 0.1f ) { // cut off
				foundhit = true;
				break;
			}
			Px(cameraContours,r,c) = 0.45f * val;
		}

		// figure out start and end cols for the gaussian and hill
		cutoffC = (float)c;
		cutoffU = (height/2.0f - r)/FLOORFINDER_SCALE*(float)pose->frameH/reading->h;  // cut off pixel in homography image frame
		cutoffV = (c - reading->h/2.0f)*(float)pose->frameH/reading->h;
		applyHomography( pose->G, cutoffU, cutoffV, &cutoffX, &cutoffY ); // cutoff pixel in floor frame
		cutoffD = sqrt( cutoffX*cutoffX + cutoffY*cutoffY ); // distance from camera to cutoff pixel
		startD = max( cutoffD - 3*pose->sigma, 0.01f );
		startX = cutoffX/cutoffD * startD; // start pixel in floor frame
		startY = cutoffY/cutoffD * startD;
		applyHomography( pose->H, startX, startY, &startU, &startV ); // start pixel in homography image frame
		//startR = height/2.0f - (startU*FLOORFINDER_SCALE*(float)reading->h/pose->frameH); 
		startC = reading->h/2.0f + (startV*(float)reading->h/pose->frameH);  // start pixel in contour frame
		if ( foundhit ) {
			endD = cutoffD + 3*pose->sigma;
			endX = cutoffX/cutoffD * endD; // start pixel in floor frame
			endY = cutoffY/cutoffD * endD;
			applyHomography( pose->H, endX, endY, &endU, &endV ); // start pixel in homography image frame
			//endR = height/2.0f - (endU*FLOORFINDER_SCALE*(float)reading->h/pose->frameH); 
			endC = reading->h/2.0f + (endV*(float)reading->h/pose->frameH); // end pixel in contour frame
		} else {
			endC = (float)(cameraContours->cols - 1); // stop on the last pixel
		}

		// scale by hill, apply gaussian
		for ( c=(int)ceil(startC); c<=endC; c++ ) {
			U = (height/2.0f - r)/FLOORFINDER_SCALE*(float)pose->frameH/reading->h;  // current pixel in homography image frame
			V = (c - reading->h/2.0f)*(float)pose->frameH/reading->h;
			applyHomography( pose->G, U, V, &X, &Y ); // current pixel in floor frame
			D = sqrt( X*X + Y*Y );
			
			if ( c < cutoffC ) {
				hill = (cutoffD - D)/(cutoffD - startD);
				Px(cameraContours,r,c) *= hill;
			}

			if ( foundhit ) {
				expon = exp( -(D-cutoffD)*(D-cutoffD)/(2*var) );
				gauss = expon*norm;
				
				Px(cameraContours,r,c) += -(1-val)*gauss*0.45f;
			}
		}
	}

	// scan through pixels of cameraT
	int Utemp, Vtemp;
	float interpA, interpB, interpC, interpD, interpH, interpV;
	for ( r=0; r<cameraT->rows; r++ ) {
		for ( c=0; c<cameraT->cols; c++ ) {
			X = CAMERA_TEMPLATE_WIDTH/2.0f - r/(float)CAMERA_TEMPLATE_DIVISIONS;	// pixel in floor frame
			Y = c/(float)CAMERA_TEMPLATE_DIVISIONS;
			applyHomography( pose->H, X, Y, &U, &V );			// pixel in homography image frame
			U = height/2.0f - (U*FLOORFINDER_SCALE*(float)reading->h/pose->frameH);  // pixel in cameraContours frame
			V = reading->h/2.0f + (V*(float)reading->h/pose->frameH);
			D = sqrt( U*U + V*V );

			// four point interpolate value
			interpA = interpB = interpC = interpD = 0;
			Utemp = (int)floor( U );
			Vtemp = (int)floor( V );
			interpH = V - Vtemp; // distance from bl corner to U,V
			interpV = U - Utemp; 
			if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
				interpA = Px(cameraContours,Utemp,Vtemp);
			Vtemp++;
			if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
				interpB = Px(cameraContours,Utemp,Vtemp);
			Utemp++;
			if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
				interpC = Px(cameraContours,Utemp,Vtemp);
			Vtemp--;
			if ( !(Utemp < 0 || Vtemp < 0 || Utemp >= cameraContours->rows || Vtemp >= cameraContours->cols) )
				interpD = Px(cameraContours,Utemp,Vtemp);

			Px(cameraT,r,c) = (interpA*interpH + interpD*(1-interpH))*interpV + (interpB*interpH + interpC*(1-interpH))*(1-interpV);
		}
	}

	return 0;
}

// -- Sonar Sensor --
int Simulation::avatarConfigureSonar( Avatar *avatar, int sonarCount, char *sonarFileN, char *poseFileN ) {
	FILE *sonarF, *poseF;
	int i;
	float time;
	float val;
	SonarReading *sonarReading;
	
	int useCount = min( sonarCount, AVATAR_MAX_SONAR );

	// configure pose
	fopen_s( &poseF, poseFileN, "r" );
	if ( poseF == NULL ) {
		return 1;
	}
	i = 0;
	while ( i < useCount ) {
		if ( 3 != fscanf_s( poseF, "%f %f %f", &avatar->sonarPose[i].x, &avatar->sonarPose[i].y, &avatar->sonarPose[i].r ) ) {
			fclose( poseF );
			return 1;
		}
		avatar->sonarPose[i].r = (float)DegreesToRadians( avatar->sonarPose[i].r );
		i++;
	}
	fclose( poseF );

	fopen_s( &sonarF, sonarFileN, "r" );
	if ( sonarF == NULL ) {
		return 1;
	}

	// allocate sonarData
	for ( i=0; i<useCount; i++ ) {
		avatar->sonarData[i] = new std::list<SonarReading *>;
		if ( avatar->sonarData[i] == NULL ) {
			goto cleanfail;
		}
	}

	// read sonarF
	while ( 1 ) {
		if ( 1 != fscanf_s( sonarF, "%f", &time ) ) {
			break; // expected time, we must be finished
		}
		for ( i=0; i<sonarCount; i++ ) {
			if ( 1 != fscanf_s( sonarF, "%f", &val ) ) {
				goto cleanfail; // expected val
			}

			if ( i < AVATAR_MAX_SONAR && val > 0 && val < 4.0 ) {
				sonarReading = (SonarReading *)malloc(sizeof(SonarReading));
				if ( sonarReading == NULL ) {
					goto cleanfail;
				}
				sonarReading->tb.time = ((int)(time*1000)) / 1000;
				sonarReading->tb.millitm = ((int)(time*1000)) % 1000;
				sonarReading->value = val;
				avatar->sonarData[i]->push_back( sonarReading );
			}
		}

	}
	fclose( sonarF );

	avatar->sonarCount = useCount;

	for ( i=0; i<useCount; i++ ) {
		if ( this->nextSensorTime > 
			 avatar->sonarData[i]->front()->tb.time*1000 + avatar->sonarData[i]->front()->tb.millitm + SENSOR_DELAY )
			this->nextSensorTime = (int)avatar->sonarData[i]->front()->tb.time*1000 + (int)avatar->sonarData[i]->front()->tb.millitm + SENSOR_DELAY;
	}

	return 0;

cleanfail:
	fclose( sonarF );

	std::list<SonarReading *>::iterator iter;
	for ( i=0; i<useCount; i++ ) {
		if ( avatar->sonarData[i] != NULL ) {
			iter = avatar->sonarData[i]->begin();
			while ( iter != avatar->sonarData[i]->end() ) {
				free( *iter );
				iter++;
			}
			delete avatar->sonarData[i];
		}
	}

	return 1;
}

int Simulation::correctPF_AvatarConicSensor( ParticleFilter *pf, SonarReading *reading, SonarPose *pose ) {
	std::list<ParticleRegion*>::iterator pr;
	std::list<Particle*>::iterator p;
	std::list<std::list<void*>*>::iterator psBefore;
	std::list<std::list<void*>*>::iterator psAfter;
	std::list<_timeb*>::iterator tbBefore;
	std::list<_timeb*>::iterator tbAfter;
	std::list<void*>::iterator stateBefore;
	std::list<void*>::iterator stateAfter;
	ParticleState_Avatar state;
	float scale;
	float interp;
	int border;
	int i, j;
	int r, c;
	int div;
	float sn, cs;

	float PFbound[4];
	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	bool  rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date

	float penalty, support;
	float maxAbsoluteOd, maxDensityOd;
	float absoluteNorm, densityNorm;
	float sumWeightedObs;
	float particleNumEff;

	generateConicTemplates( reading->value, SONAR_RANGE_MAX, SONAR_SIG_EST, SONAR_A_EST, SONAR_B_EST, densityT, absoluteT, &scale );

	// find region and bounding particles
	pr = pf->regions->begin();
	while ( pr != pf->regions->end() ) {
		if ( (*pr)->startTime.time < reading->tb.time 
	    || ( (*pr)->startTime.time == reading->tb.time && (*pr)->startTime.millitm < reading->tb.millitm ) )
			break;
		pr++;
	}
	if ( pr == pf->regions->end() )
		return 1;

	tbBefore = (*pr)->times->begin();
	psBefore = (*pr)->states->begin();
	tbAfter = tbBefore;
	psAfter = psBefore;
	while ( tbBefore != (*pr)->times->end() ) {
		if ( (*tbBefore)->time < reading->tb.time 
	    || ( (*tbBefore)->time == reading->tb.time && (*tbBefore)->millitm < reading->tb.millitm ) )
			break;
		tbAfter = tbBefore;
		psAfter = psBefore;
		tbBefore++;
		psBefore++;
	}

	if ( tbAfter == tbBefore ) { // we're at the top of the stack!?
		return 1;
	}

	interp = ( (reading->tb.time - (*tbBefore)->time)*1000 + (reading->tb.millitm - (*tbBefore)->millitm) ) / (float)( ((*tbAfter)->time - (*tbBefore)->time)*1000 + ((*tbAfter)->millitm - (*tbBefore)->millitm) );

	// update PFbound
	// TODO ignore particles with very little weight?
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	PFbound[0] = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
	PFbound[1] = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
	PFbound[2] = 0;
	PFbound[3] = 0;
	stateBefore++;
	stateAfter++;
	while ( stateBefore != (*psBefore)->end() ) {
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		if ( PFbound[0] > state.x ) {
			PFbound[2] = PFbound[0] + PFbound[2] - state.x;
			PFbound[0] = state.x;
		} else if ( PFbound[0] + PFbound[2] < state.x ) {
			PFbound[2] = state.x - PFbound[0];
		}
		if ( PFbound[1] > state.y ) {
			PFbound[3] = PFbound[1] + PFbound[3] - state.y;
			PFbound[1] = state.y;
		} else if ( PFbound[1] + PFbound[3] < state.y ) {
			PFbound[3] = state.y - PFbound[1];
		}
		stateBefore++;
		stateAfter++;
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[0] = floor(PFbound[0]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[1] = floor(PFbound[1]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[3] -= PFbound[1];
	
	// prepare map update
	border = (int)ceil(5 + scale*MAP_DIVISIONS*TEMPLATE_SIZE);
	mapUpdateLoc[0] = PFbound[0] - border/(float)MAP_DIVISIONS;
	mapUpdateLoc[1] = PFbound[1] - (ceil(PFbound[3]*MAP_DIVISIONS) + border)/(float)MAP_DIVISIONS;
	if ( mapUpdate->bufSize < (int)sizeof(float)*((int)(PFbound[2]*MAP_DIVISIONS)+border*2) * ((int)(PFbound[3]*MAP_DIVISIONS)+border*2) ) {
		// allocate more space for the map update
		ReallocImage( mapUpdate, (int)(PFbound[3]*MAP_DIVISIONS)+border*2, (int)(PFbound[2]*MAP_DIVISIONS)+border*2 );
	} else {
		mapUpdate->rows = ((int)(PFbound[3]*MAP_DIVISIONS)+border*2);
		mapUpdate->cols = ((int)(PFbound[2]*MAP_DIVISIONS)+border*2);
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	// clear the rotationDivision flags
	memset( rotationDivision, 0, sizeof(rotationDivision) );
	
	// for each particle
	maxAbsoluteOd = 0;
	maxDensityOd = 0;
	p = (*pr)->particles->begin();
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	while ( p != (*pr)->particles->end() ) {
		// interpolate state
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		state.r = ((ParticleState_Avatar*)*stateBefore)->r + (((ParticleState_Avatar*)*stateAfter)->r - ((ParticleState_Avatar*)*stateBefore)->r)*interp;
		// adjust for sensor pose
		sn = (float)sin(state.r);
		cs = (float)cos(state.r);
		state.x += cs*pose->x - sn*pose->y;
		state.y += sn*pose->x + cs*pose->y;
		state.r += pose->r;
		while ( state.r < 0 ) state.r += 2*fM_PI;
		while ( state.r > 2*fM_PI ) state.r -= 2*fM_PI;
		div = (int)(ROTATION_DIVISIONS*(state.r + ROTATION_RESOLUTION*0.5)/(2*fM_PI)) % ROTATION_DIVISIONS;
		// rotate
		if ( rotationDivision[div] == false ) {
			if ( reading->value < SONAR_RANGE_MAX )
				RotateImageEx( densityT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), densityR[div], integralTarget, 0 );
			RotateImageEx( absoluteT, -(float)RadiansToDegrees( div*ROTATION_RESOLUTION ), absoluteR[div], integralTarget, 0 );
			rotationDivision[div] = true;
		}
		origin[0] = absoluteR[div]->cols/2.0f - absoluteT->cols/2.0f*(float)cos(div*ROTATION_RESOLUTION);
		origin[1] = absoluteR[div]->rows/2.0f - absoluteT->cols/2.0f*(float)sin(div*ROTATION_RESOLUTION);
		
		// compare against map
		offset[0] = (state.x - MAP_X)*MAP_DIVISIONS - origin[0]*scale*MAP_DIVISIONS;
		offset[1] = (state.y - MAP_Y)*MAP_DIVISIONS - origin[1]*scale*MAP_DIVISIONS;
		(*p)->absoluteOd = 0;
		(*p)->densityOd = 0;
		for ( j=0; j<absoluteR[div]->cols; j++ ) {
			c = (int)(j*scale*MAP_DIVISIONS + offset[0]);
			for ( i=0; i<absoluteR[div]->rows; i++ ) {
				r = (int)(i*scale*MAP_DIVISIONS + offset[1]);
				// update absoluteOd, total penalty
				if ( Px(absoluteR[div],i,j) < 0 ) {
					penalty = max( 0, Px(map,r,c) - 0.5f ) * -Px(absoluteR[div],i,j);
				} else {
					penalty = max( 0, 0.5f - Px(map,r,c) ) * Px(absoluteR[div],i,j);
				}
				(*p)->absoluteOd += penalty;
				// update densityOd, max support
				if ( reading->value < SONAR_RANGE_MAX ) {
					if ( Px(densityR[div],i,j) < 0 ) {
						support = max( 0, 0.5f - Px(map,r,c) ) * -Px(densityR[div],i,j);
					} else {
						support = max( 0, Px(map,r,c) - 0.5f ) * Px(densityR[div],i,j);
					}
					(*p)->densityOd = max( (*p)->densityOd, support );
				}
			}
		}
		maxAbsoluteOd = max( maxAbsoluteOd, (*p)->absoluteOd );
		maxDensityOd = max( maxDensityOd, (*p)->densityOd );

		// TEMP
		//offset[0] = (state.x - mapLoc[0])*MAP_DIVISIONS;
		//offset[1] = (state.y - mapLoc[1])*MAP_DIVISIONS;
		//map[(int)offset[0]][(int)offset[1]] = 0;

		// stamp on update
		float densityScale = 0.01f;
		float absoluteScale = 0.01f;
		offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS - origin[0]*scale*MAP_DIVISIONS;
		offset[1] = (state.y - mapUpdateLoc[1])*MAP_DIVISIONS - origin[1]*scale*MAP_DIVISIONS;
		if ( reading->value < SONAR_RANGE_MAX )
			ImageAdd( densityR[div], mapUpdate, offset[0], offset[1], scale*MAP_DIVISIONS, (*p)->weight*densityScale, 1 );
		ImageAdd( absoluteR[div], mapUpdate, offset[0], offset[1], scale*MAP_DIVISIONS, (*p)->weight*absoluteScale, 0 );
		
		// TEMP
		//this->visualizer->deletefImage( this->mapUpdateImage );
		//this->mapUpdateImage = this->visualizer->newfImage( mapUpdateLoc[0], mapUpdateLoc[1], 1.0f/MAP_DIVISIONS, 0, 50, this->mapUpdate );
		//offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS;
		//offset[1] = (state.y - mapUpdateLoc[1])*MAP_DIVISIONS;
		//Px(mapUpdate,(int)offset[1],(int)offset[0]) = 1;

		p++;
		stateBefore++;
		stateAfter++;
	}
	// apply observations
	if ( maxAbsoluteOd > 0 ) absoluteNorm = 1/maxAbsoluteOd;
	else absoluteNorm = 1;
	if ( maxDensityOd > 0 ) densityNorm = 1/maxDensityOd;
	else densityNorm = 1;
	sumWeightedObs = 0;
	p = (*pr)->particles->begin();
	while ( p != (*pr)->particles->end() ) {
		(*p)->obsDensity *= OBS_DENSITY_RATIO*(*p)->absoluteOd*absoluteNorm 
			             + (1-OBS_DENSITY_RATIO)*(*p)->densityOd*densityNorm
						 + OBS_DENSITY_AMBIENT;
		sumWeightedObs += (*p)->weight * (*p)->obsDensity;
		p++;
	}
	p = (*pr)->particles->begin();
	particleNumEff = 0;
	while ( p != (*pr)->particles->end() ) {
		(*p)->weight = (*p)->weight * (*p)->obsDensity / sumWeightedObs;
		(*p)->obsDensity = 1;
		particleNumEff += (*p)->weight * (*p)->weight;
		p++;
	}
	particleNumEff = 1/particleNumEff;
	if ( particleNumEff < MIN_EFF_PARTICLES && pr ==  pf->regions->begin() )
		resampleParticleFilter( pf, copyParticleState_Avatar );

	// apply update to map
	float pC, pSC, pS;
	int mapOffset[2];
	mapOffset[0] = (int)((mapUpdateLoc[0] - MAP_X)*MAP_DIVISIONS + 0.5f);
	mapOffset[1] = (int)((mapUpdateLoc[1] - MAP_Y)*MAP_DIVISIONS + 0.5f);
	for ( r=0; r<mapUpdate->rows; r++ ) {
		if ( r+mapOffset[1] < 0 )
			continue;
		if ( r+mapOffset[1] >= MAP_SIZE*MAP_DIVISIONS )
			break;
		for ( c=0; c<mapUpdate->cols; c++ ) {
			if ( c+mapOffset[0] < 0 )
				continue;
			if ( c+mapOffset[0] >= MAP_SIZE*MAP_DIVISIONS )
				break;
			pSC = min( 0.95f, max( 0.05f, Px(mapUpdate,r,c) ) );
			pC = Px(map,r+mapOffset[1],c+mapOffset[0]);
			pS = pC*pSC + (1-pC)*(1-pSC);
			// TEMP don't apply update
			Px(map,r+mapOffset[1],c+mapOffset[0]) = pC*pSC/pS;
		}
	}

	return 0;
}

int generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale ) {
	int i, j;
	float r, t;
	float coverage;
	float var = sig*sig;
	float norm = ( 1/(sig*sqrt(2*fM_PI)) );
	float expon;
	bool skipDensity;

	if ( d > max ) 
		d = max;

	*scale = 0.7071f * (d + 3*var)/TEMPLATE_SIZE_DIV2;
	
	int cols = (int)ceil(0.7071f * TEMPLATE_SIZE);
	densityT->cols = cols;
	absoluteT->cols = cols;

	if ( d < max ) {
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			skipDensity = false;
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );
					
					if ( skipDensity ) {
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
					} else {
						expon = exp( -(d-r)*(d-r)/(2*var) );
						if ( expon < 1E-15 ) {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
							Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
							if ( r < d ) // early exit
								skipDensity = true;
						} else {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) 
								= Px(densityT,TEMPLATE_SIZE_DIV2+i,j) 
								= - *scale * 0.45f 
								* ( 1/(2*r*a) ) 
								* norm * expon;
						}
					}

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	} else { // no density
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*sig);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	}

	return 0;
}

void * generateParticleState_Avatar( int index, void *genParams ) {
	Avatar *r = (Avatar *)genParams;

	ParticleState_Avatar *ps_a = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
	if ( ps_a == NULL ) {
		return NULL;
	}

	ps_a->x = r->x + (float)randNormalDistribution( 0, AVATAR_INIT_LINEAR_SIG );
	ps_a->y = r->y + (float)randNormalDistribution( 0, AVATAR_INIT_LINEAR_SIG );
	ps_a->r = r->r + (float)randNormalDistribution( 0, AVATAR_INIT_ROTATIONAL_SIG );
	
	//ps_a->r = 0; // TEMP

	return (void *)ps_a;
}

void * copyParticleState_Avatar( void *vpstate ) {
	ParticleState_Avatar *state = (ParticleState_Avatar *)vpstate;
	ParticleState_Avatar *copy = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
	if ( copy == NULL ) {
		return NULL;
	}

	copy->x = state->x;
	copy->y = state->y;
	copy->r = state->r;

	return (void *)copy;
}

int predictPF_Avatar( ParticleFilter *pf, unsigned int dtime, float forwardV, float tangentialV, float rotationalV ) {
	ParticleRegion *pr;
	std::list<Particle*>::iterator p;
	std::list<void*>::iterator stateIter;
	std::list<void*> *ps;
	ParticleState_Avatar *oldState, *newState;
	_timeb *tb;
	float forwardD, tangentialD, rotationalD;
	float s, c;

	if ( pf == NULL )
		return 1;

	ps = new std::list<void*>();
	if ( ps == NULL )
		return 1;

	pr = pf->regions->front();
	p = pr->particles->begin();
	stateIter = pr->states->front()->begin();
	while ( p != pr->particles->end() ) {
		oldState = (ParticleState_Avatar *)*stateIter;
		
		newState = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
		if ( newState == NULL )
			return 1;

		forwardD = (forwardV + (float)randNormalDistribution(0,AVATAR_LINEARV_SIG_EST))*dtime;
		tangentialD = (tangentialV + (float)randNormalDistribution(0,AVATAR_LINEARV_SIG_EST))*dtime;
		rotationalD = (rotationalV + (float)randNormalDistribution(0,AVATAR_ROTATIONALV_SIG_EST))*dtime;

		// forwardD = tangentialD = rotationalD = 0; // TEMP

		s = sin( oldState->r );
		c = cos( oldState->r );
		newState->x = oldState->x + c*forwardD - s*tangentialD;
		newState->y = oldState->y + s*forwardD + c*tangentialD;
		newState->r = oldState->r + rotationalD;

		ps->push_back( (void*)newState );

		p++;
		stateIter++;
	}
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		return 1;
	}
	tb->millitm = pr->times->front()->millitm + dtime;
	tb->time = pr->times->front()->time + tb->millitm/1000;
	tb->millitm %= 1000;
	pr->times->push_front( tb );
	pr->states->push_front( ps );

	return 0;	
}

int resampleParticleFilter( ParticleFilter *pf, void * (*copyState)( void *vpstate ) ) {
	int i;
	ParticleRegion *pr;
	Particle	   *p;
	std::list<void*> *ps;
	void *state;
	_timeb *tb;
	_timeb *startTime = pf->regions->front()->times->front(); // time of latest state
	float cdfOld, cdfNew;
	std::list<Particle*>::iterator parentP;
	std::list<void*>::iterator parentS;

	float particleNumInv = 1.0f/pf->particleNum;

	parentP = pf->regions->front()->particles->begin();
	parentS = pf->regions->front()->states->front()->begin();

	// create new region
	pr = (ParticleRegion *)malloc(sizeof(ParticleRegion));
	if ( pr == NULL ) {
		return 1;
	}
	pr->index = pf->regionCount;
	pr->startTime.time = startTime->time;
	pr->startTime.millitm = startTime->millitm;
	pr->particles = new std::list<Particle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<std::list<void*>*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		free( pr );
		return 1;
	}
	tb->time = startTime->time;
	tb->millitm = startTime->millitm;
	pr->times->push_front( tb );

	ps = new std::list<void*>();
	if ( ps == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	pr->states->push_front( ps );
	
	pf->regions->push_front( pr );
	pf->regionCount++;

	cdfOld = (*parentP)->weight;
	cdfNew = (float)randUniform01()*particleNumInv;

	// create particles
	for ( i=0; i<(int)pf->particleNum; i++ ) {
		p = (Particle *)malloc(sizeof(Particle));
		if ( p == NULL ) {
			FreeFilter( pf );
			return 1;
		}
		p->weight = particleNumInv;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		
		while ( cdfOld < cdfNew ) {
			parentP++;
			parentS++;
			cdfOld += (*parentP)->weight;
		}

		// copy state		
		state = (*copyState)( (void *) *parentS );
		if ( state == NULL ) {
			free( p );
			FreeFilter( pf );
			return 1;
		}

		ps->push_front( state );

		pr->particles->push_front( p );

		cdfNew += particleNumInv;
	}

	return 0;
}

ParticleFilter * CreateFilter( int particleNum, _timeb *startTime, void * (*genState)(int index, void *genParams), void *genParams ) {
	ParticleFilter *pf;
	ParticleRegion *pr;
	Particle	   *p;
	_timeb *tb;
	std::list<void*> *ps;
	void *state;
	int i;

	pf = (ParticleFilter *)malloc(sizeof(ParticleFilter));
	if ( pf == NULL ) {
		return NULL;
	}

	pf->regionCount = 0;
	pf->particleNum = particleNum;
	pf->forwardMarker = -1;
	pf->regions = new std::list<ParticleRegion*>();

	// create first region
	pr = (ParticleRegion *)malloc(sizeof(ParticleRegion));
	if ( pr == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	pr->index = pf->regionCount;
	pr->startTime.time = startTime->time;
	pr->startTime.millitm = startTime->millitm;
	pr->particles = new std::list<Particle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<std::list<void*>*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	tb->time = startTime->time;
	tb->millitm = startTime->millitm;
	pr->times->push_front( tb );

	ps = new std::list<void*>();
	if ( ps == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	pr->states->push_front( ps );
	
	pf->regions->push_front( pr );
	pf->regionCount++;
	
	// create particles
	for ( i=0; i<particleNum; i++ ) {
		p = (Particle *)malloc(sizeof(Particle));
		if ( p == NULL ) {
			FreeFilter( pf );
			return NULL;
		}
		p->weight = 1.0f/particleNum;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		
		// create first state		
		state = (*genState)( i, genParams );
		if ( state == NULL ) {
			free( p );
			FreeFilter( pf );
			return NULL;
		}

		ps->push_front( state );

		pr->particles->push_front( p );
	}

	return pf;
}

void FreeFilter( ParticleFilter *pf ) {
	ParticleRegion *pr;
	Particle	   *p;
	std::list<void*> *ps;

	if ( pf != NULL ) {
		while ( pf->regions->begin() != pf->regions->end() ) {
			pr = *pf->regions->begin();
			pf->regions->pop_front();
			while ( pr->particles->begin() != pr->particles->end() ) {
				p = *pr->particles->begin();
				pr->particles->pop_front();
				free( p );
			}
			delete pr->particles;
			while ( pr->times->begin() != pr->times->end() ) {
				free( pr->times->front() );
				pr->times->pop_front();
			}
			delete pr->times;
			while ( pr->states->begin() != pr->states->end() ) {
				ps = pr->states->front();
				pr->states->pop_front();
				while ( ps->begin() != ps->end() ) {
					free( ps->front() );
					ps->pop_front();
				}
				delete ps;
			}
			delete pr->states;
			free( pr );
		}
		delete pf->regions;
		free( pf );
	}
}
*/