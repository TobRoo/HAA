// SimAvatar.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimAvatar.h"
#include "SimPathPlanner.h"

#include "..\\autonomic\\RandomGenerator.h"

//*****************************************************************************
// SimAvatar

//-----------------------------------------------------------------------------
// Constructor	
SimAvatar::SimAvatar( Simulation *sim, Visualize *vis, RandomGenerator *randGen, char *name, float *color ) : SimAgentBase( sim, vis, randGen ) {

	if ( name ) {
		strncpy( this->name, name, sizeof(this->name) );
	} else {
		sprintf_s( this->name, sizeof(this->name), "Avatar" );
	}

	if ( color ) {
		memcpy( this->color, color, 3*sizeof(float) );
	} else {
		color[0] = 0;
		color[1] = 0;
		color[2] = 1;
	}

	this->moveInProgress = false;
	this->moveTargetL = 0;
	this->moveTargetA = 0;

	this->visPath = -1;
	this->visPathPF = -1;
	this->pathVisible = true;
	this->extraVisible = true;

	this->actionInProgress = false;
	this->useDelayActionsFlag = false;

	this->targetCB = NULL;

}

//-----------------------------------------------------------------------------
// Destructor
SimAvatar::~SimAvatar() {
	int i;

	// clean up actions
	this->clearActions();

	if ( this->started ) {
		this->stop();
	}

	if ( this->targetCB )
		delete this->targetCB;

	// clean up vis object
	if ( this->visPath != -1 ) 
		vis->deleteObject( this->visPathObj );
	if ( this->visPathPF != -1 )
		vis->deleteObject( this->visPathPFObj );

	vis->deleteObject( this->objectId, true );
	vis->deleteObject( this->objectEstId, true );

	// clean up landmarks
	std::list<SimLandmark>::iterator itSL;
	for ( itSL = this->landmarks.begin(); itSL != this->landmarks.end(); itSL++ ) {
		vis->deleteObject( itSL->objId, true );
		sim->removeLandmark( &(*itSL) );
	}

	// clean up sensors
	std::list<SimSonar>::iterator itSS;
	for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
		vis->deleteObject( itSS->objId, true );
		sim->ddbRemoveSensor( &itSS->id );
	}
	std::list<SimCamera>::iterator itSC;
	for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
		for ( i=0; i<10; i++ ) {
			if ( itSC->objId[i] == -1 )
				break;
			vis->deleteObject( itSC->objId[i], true );
		}
		sim->ddbRemoveSensor( &itSC->id );
	}

	// clean up particle filter
	this->destroyParticleFilter();

}

//-----------------------------------------------------------------------------
// Configure

int SimAvatar::configure( SimAvatarState *state, char *avatarFile, UUID *mapId, UUID *missionRegionId ) {

	this->state = *state;
	this->stateEst = *state;
	this->lastStateEst = *state;

	this->mapId = *mapId;
	this->missionRegionId = *missionRegionId;

	// add avatar object
	int pathId = sim->getRobotPathId();
	this->objectId = vis->newDynamicObject( &this->state.x, &this->state.y, &this->state.r, pathId, this->color, 2, false, this->name );
	float colorEst[3] = { 1, 0, 0 };
	this->objectEstId = vis->newDynamicObject( &this->stateEst.x, &this->stateEst.y, &this->stateEst.r, pathId, colorEst, 2, false, this->name );

	// load avatar file
	this->parseConfigFile( avatarFile );

	this->maxLinear = 0.5f;
	this->maxRotation = fM_PI_2;

	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimAvatar %s %s.txt", this->name, timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimAvatar %s", this->name );
	}

	return SimAgentBase::configure();
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
	int numParticles;
	float sigmaInitial[3];
	float sigmaUpdate[3];

	// read data
	if ( 1 != fscanf_s( fp, "NUM=%d\n", &numParticles ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "SIGMA_INITIAL=%f %f %f\n", &sigmaInitial[0], &sigmaInitial[1], &sigmaInitial[2] ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "SIGMA_UPDATE=%f %f %f\n", &sigmaUpdate[0], &sigmaUpdate[1], &sigmaUpdate[2] ) ) {
		return 1;
	}

	// save sigmas
	memcpy( this->pfInitSigma, sigmaInitial, 3*sizeof(float) );
	memcpy( this->pfUpdateSigma, sigmaUpdate, 3*sizeof(float) );

	// create filter
	this->createParticleFilter( numParticles, sigmaInitial );

	return 0;
}

int SimAvatar::parseLandmark( FILE *fp ) {
	SimLandmark landmark;
	float sn, cs;

	// read data
	if ( 2 != fscanf_s( fp, "OFFSET=%f %f\n", &landmark.x, &landmark.y ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &landmark.color[0], &landmark.color[1], &landmark.color[2] ) ) {
		return 1;
	}

	UuidCreate( &landmark.id );
	landmark.owner = this->uuid;

	sn = sin( this->state.r );
	cs = cos( this->state.r );
	landmark.wx = this->state.x + landmark.x*cs - landmark.y*sn;
	landmark.wy = this->state.y + landmark.x*sn + landmark.y*cs;
	landmark.r = 0;
	landmark.s = 0.1f;

	int pathId = sim->getXPathId();
	if ( pathId == -1 )
		return 1; // failed to get path

	// add to list
	this->landmarks.push_back( landmark );

	// add to sim (who will add it to DDB)
	SimLandmark *ptSL = &this->landmarks.back();
	sim->addLandmark( ptSL );

	// add to visualizer
	ptSL->objId = vis->newDynamicObject( &ptSL->wx, &ptSL->wy, &ptSL->r, &ptSL->s, pathId, ptSL->color, 2 );

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

	UuidCreate( &sonar.id );
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

	// add to DDB
	sim->ddbAddSensor( &sonar.id, DDB_SENSOR_SONAR, &this->uuid, &sonar.pose, sizeof(SonarPose) );

	// add to visualizer
	SimSonar *ptSS = &this->sonars.back();
	ptSS->objId = vis->newDynamicObject( &ptSS->x, &ptSS->y, &ptSS->r, &ptSS->s, pathId, ptSS->color, 1 );
	vis->hideObject( ptSS->objId );

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

	UuidCreate( &camera.id );
	camera.nextT = this->state.t;
	
	// clear all vis objects
	int i;
	for ( i=0; i<10; i++ ) {
		camera.objId[i] = -1;
	}

	// add sonar to list
	this->cameras.push_back( camera );

	// add to DDB
	sim->ddbAddSensor( &camera.id, DDB_SENSOR_CAMERA, &this->uuid, &camera.pose, sizeof(CameraPose) );

	return 0;
}

//-----------------------------------------------------------------------------
// Start

int SimAvatar::start() {

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	this->haveTarget = false;
	this->pathPlanner = new SimPathPlanner( sim, vis, randGen );
	if ( !this->pathPlanner->configure() ) {
		this->ds.reset();
		this->ds.packUUID( &this->uuid );
		this->ds.packUUID( &this->mapId );
		this->ds.packUUID( &this->missionRegionId );
		this->ds.packUUID( &this->uuid );
		this->ds.packFloat32( this->maxLinear );
		this->ds.packFloat32( this->maxRotation );
		this->ds.rewind();
		this->pathPlanner->configureParameters( &this->ds, this );
		this->pathPlanner->start();
	}

	this->pfUpdateTimer = this->addTimeout( PFNOCHANGE_PERIOD, NEW_MEMBER_CB(SimAvatar,cbPFUpdateTimer) );
	if ( !this->pfUpdateTimer )
		return 1;

	this->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SimAvatar::stop() {

	delete this->pathPlanner;

	return SimAgentBase::stop();
}

int SimAvatar::visualizePath() {
#ifdef SimAvatar_SAVE_PATH
	float avgLocalizationErr;
	float curLocalizationErr;
	float avgRotationErr;
	float curRotationErr;
	float pr;

	int i;
	std::list<SimAvatarState>::iterator it;
	
	if ( this->visPath != -1 )
		vis->deleteObject( this->visPathObj );
	
	if ( this->visPathPF != -1 )
		vis->deleteObject( this->visPathPFObj );

	float *x, *y, *px, *py;
	int pts = (int)this->truePath.size();

	if ( !pts )
		return 0;

	x = (float *)malloc( 4*pts*sizeof(float) );
	if ( !x )
		return 1;
	y = x + pts;
	px = y + pts;
	py = px + pts;
	
	avgLocalizationErr = 0;
	avgRotationErr = 0;
	for ( i=0, it = this->truePath.begin(); it != this->truePath.end(); i++, it++ ) {
		x[i] = it->x;
		y[i] = it->y;

		sim->ddbPFGetInfo( &this->uuid, DDBPFINFO_MEAN, &it->t, &this->ds );
		this->ds.rewind();
		this->ds.unpackInt32(); // thread
		if ( this->ds.unpackChar() == DDBR_OK ) {
			this->ds.unpackInt32(); // flags
			px[i] = this->ds.unpackFloat32();
			py[i] = this->ds.unpackFloat32();
			pr = this->ds.unpackFloat32();
		} else {
			px[i] = px[i-1];
			py[i] = py[i-1];
		}

		curLocalizationErr = ( (px[i] - x[i])*(px[i] - x[i]) + (py[i] - y[i])*(py[i] - y[i]) );
		avgLocalizationErr += curLocalizationErr;

		curRotationErr = (pr - it->r)*(pr - it->r);
		avgRotationErr += curRotationErr;
	}
	avgLocalizationErr = sqrt( avgLocalizationErr / pts );
	avgRotationErr = sqrt( avgRotationErr / pts );
	curLocalizationErr = sqrt( curLocalizationErr );
	curRotationErr = sqrt( curRotationErr );

	Log.log( 0, "LOCALIZATION ERR: %04d.%03d %f %f", (int)this->simTime.time, (int)this->simTime.millitm, curLocalizationErr, avgLocalizationErr );
	Log.log( 0, "ROTATION ERR: %04d.%03d %f %f", (int)this->simTime.time, (int)this->simTime.millitm, curRotationErr, avgRotationErr );

	this->visPath = vis->newPath( pts, x, y );

	if ( this->visPath == -1 ) {
		free( x );
		return 1;
	}

	this->visPathObj = vis->newStaticObject( 0, 0, 0, 1, this->visPath, this->color, 1 );

	if ( this->visPathObj == -1 ) {
		vis->deletePath( this->visPath );
		this->visPath = -1;
		free( x );
		return 1;
	}
	if ( !this->pathVisible )
		vis->hideObject( this->visPathObj );
	
	vis->setObjectStipple( this->visPathObj, 1 );

	this->visPathPF = vis->newPath( pts, px, py );

	if ( this->visPathPF == -1 ) {
		free( x );
		return 1;
	}

	float color[3] = { 0, 0, 1 };
	this->visPathPFObj = vis->newStaticObject( 0, 0, 0, 1, this->visPathPF, color, 1 );

	if ( this->visPathPFObj == -1 ) {
		vis->deletePath( this->visPathPF );
		this->visPathPF = -1;
		free( x );
		return 1;
	}
	if ( !this->pathVisible )
		vis->hideObject( this->visPathPFObj );

	free( x );

#endif

	return 0;
}

int SimAvatar::setVisibility( int avatarVis, int pathVis, int extraVis ) {
	int i;

	switch ( avatarVis ) {
	case 1:
		vis->showObject( this->objectId );
		break;
	case 0:
		vis->hideObject( this->objectId );
		break;
	case 2:
		vis->toggleObject( this->objectId );
		break;
	default:
		break;
	};

	switch ( pathVis ) {
	case 1:
		this->pathVisible = true;
		if ( this->visPath != -1 )
			vis->showObject( this->visPathObj );
		if ( this->visPathPF != -1 )
			vis->showObject( this->visPathPFObj );
		break;
	case 0:
		this->pathVisible = false;
		if ( this->visPath != -1 )
			vis->hideObject( this->visPathObj );
		if ( this->visPathPF != -1 )
			vis->hideObject( this->visPathPFObj );
		break;
	case 2:
		this->pathVisible = !this->pathVisible;
		if ( this->visPath != -1 )
			vis->toggleObject( this->visPathObj );
		if ( this->visPathPF != -1 )
			vis->toggleObject( this->visPathPFObj );
		break;
	default:
		break;
	};

	std::list<SimSonar>::iterator itSS;
	std::list<SimCamera>::iterator itSC;
	switch ( extraVis ) {
	case 1:
		this->extraVisible = true;
		vis->showObject( this->objectEstId );
		for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
			if ( itSS->s > 0 )
				vis->showObject( itSS->objId );
		}
		for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
			for ( i=0; i<10; i++ ) {
				if ( itSC->objId[i] == -1 )
					break;
				if ( itSC->s[i] > 0 )
					vis->showObject( itSC->objId[i] );
			}
		}
		break;
	case 0:
		this->extraVisible = false;
		vis->hideObject( this->objectEstId );
		for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
			vis->hideObject( itSS->objId );
		}
		for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
			for ( i=0; i<10; i++ ) {
				if ( itSC->objId[i] == -1 )
					break;
				vis->hideObject( itSC->objId[i] );
			}
		}
		break;
	case 2:
		this->extraVisible = !this->extraVisible;
		vis->toggleObject( this->objectEstId );
		for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
			if ( this->extraVisible && itSS->s > 0 )
				vis->showObject( itSS->objId );
			else 
				vis->hideObject( itSS->objId );
		}
		for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
			for ( i=0; i<10; i++ ) {
				if ( itSC->objId[i] == -1 )
					break;
				if ( this->extraVisible && itSC->s[i] > 0 )
					vis->showObject( itSC->objId[i] );
				else
					vis->hideObject( itSC->objId[i] );
			}
		}
		break;
	default:
		break;
	};



	return 0;
}

//-----------------------------------------------------------------------------
// Step

int SimAvatar::preStep( int dt ) {
	float fdt;
	float sn, cs;
	float dL, dA;

	if ( SimAgentBase::preStep( dt ) )
		return 1;

	fdt = dt*0.001f;

	// update state
	this->state.t = this->simTime;
	dL = fdt*(this->state.vL + this->state.vR)/2;
	dA = fdt*(this->state.vR - this->state.vL)/this->wheelBase;
	sn = sin( this->state.r );
	cs = cos( this->state.r );
	this->state.x += dL*cs;
	this->state.y += dL*sn;
	this->state.r += dA;

	
	// TEMP
	//this->state.r += dt*0.01f;
	//this->state.r = 1.34f*fM_PI_4;

	
	// update landmarks
	sn = sin( this->state.r );
	cs = cos( this->state.r );
	std::list<SimLandmark>::iterator itL;
	for ( itL = this->landmarks.begin(); itL != this->landmarks.end(); itL++ ) {
		(*itL).wx = this->state.x + (*itL).x*cs - (*itL).y*sn;
		(*itL).wy = this->state.y + (*itL).x*sn + (*itL).y*cs;
	}

	this->pathPlanner->preStep( dt );

	return 0;
}


int SimAvatar::step() {
	float sn, cs;
	float dt, dL, dA;
	float vel, acc;
	float accCmdL, accCmdR;

	// update state estimate
	dt = (this->simTime.time - this->stateEst.t.time) + (this->simTime.millitm - this->stateEst.t.millitm)*0.001f;
#ifdef PERFECT_RUN
	dL = sqrt( (this->state.x - this->stateEst.x)*(this->state.x - this->stateEst.x) + (this->state.y - this->stateEst.y)*(this->state.y - this->stateEst.y) );
	dA = this->state.r - this->stateEst.r;
	this->updatePos( this->state.x - this->stateEst.x, this->state.y - this->stateEst.y, this->state.r - this->stateEst.r, &this->simTime, false );
#else
	dL = dt*(this->stateEst.vL + this->stateEst.vR)/2;
	dA = dt*(this->stateEst.vR - this->stateEst.vL)/this->wheelBaseEst;
	sn = sin( this->stateEst.r );
	cs = cos( this->stateEst.r );
	this->updatePos( dL*cs, dL*sn, dA, &this->simTime, false );
#endif

	if ( SimAgentBase::step() ) 
		return 1;

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
						accCmdL = min( -(this->stateEst.vL/dt)/this->accelerationEst, 1 );
						accCmdR = min( -(this->stateEst.vR/dt)/this->accelerationEst, 1 );					
					} else {
						accCmdL = max( -(this->stateEst.vL/dt)/this->accelerationEst, -1 );
						accCmdR = max( -(this->stateEst.vR/dt)/this->accelerationEst, -1 );
					}						
				} else { // might as well keep accelerating
					if ( this->moveTargetL < 0 ) {
						accCmdL = -1;
						accCmdR = -1;					
					} else {
						accCmdL = 1;
						accCmdR = 1;
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
				// check if we need to start decelerating
				if ( fabs(vel/acc * vel/2) >= max( 0, (this->moveTargetA - this->moveCurrent)*(this->moveTargetA > 0 ? 1 : -1) ) ) { // slow down
					if ( this->moveTargetA < 0 ) {
						accCmdL = max( -(this->stateEst.vL/dt)/this->accelerationEst, -1 );
						accCmdR = min( -(this->stateEst.vR/dt)/this->accelerationEst, 1 );					
					} else {
						accCmdL = min( -(this->stateEst.vL/dt)/this->accelerationEst, 1 );
						accCmdR = max( -(this->stateEst.vR/dt)/this->accelerationEst, -1 );
					}						
				} else { // might as well keep accelerating
					if ( this->moveTargetA < 0 ) {
						accCmdL = 1;
						accCmdR = -1;					
					} else {
						accCmdL = -1;
						accCmdR = 1;
					}
				}
			}
		} else { // stop
			// check if we're done
			if ( this->stateEst.vL == 0 && this->stateEst.vR == 0 ) { // finished
				this->finishMove();
			} else {
				if ( this->stateEst.vL < 0 ) 
					accCmdL = min( -(this->stateEst.vL/dt)/this->accelerationEst, 1 );
				else if ( this->stateEst.vL > 0 )
					accCmdL = max( -(this->stateEst.vL/dt)/this->accelerationEst, -1 );

				if ( this->stateEst.vR < 0 ) 
					accCmdR = min( -(this->stateEst.vR/dt)/this->accelerationEst, 1 );
				else if ( this->stateEst.vR > 0 )
					accCmdR = max( -(this->stateEst.vR/dt)/this->accelerationEst, -1 );
			}
		}
	}
	
	if ( !this->moveInProgress ) { // make sure we're stopped
		if ( this->stateEst.vL < 0 ) 
			accCmdL = min( -(this->stateEst.vL/dt)/this->accelerationEst, 1 );
		else if ( this->stateEst.vL > 0 )
			accCmdL = max( -(this->stateEst.vL/dt)/this->accelerationEst, -1 );

		if ( this->stateEst.vR < 0 ) 
			accCmdR = min( -(this->stateEst.vR/dt)/this->accelerationEst, 1 );
		else if ( this->stateEst.vR > 0 )
			accCmdR = max( -(this->stateEst.vR/dt)/this->accelerationEst, -1 );
	}

	// update velocity
	this->state.vL += dt*this->accelerationL*accCmdL;
	if ( this->state.vL > this->maxVelocityL ) this->state.vL = this->maxVelocityL;
	if ( this->state.vL < -this->maxVelocityL ) this->state.vL = -this->maxVelocityL;
	this->state.vR += dt*this->accelerationR*accCmdR;
	if ( this->state.vR > this->maxVelocityR ) this->state.vR = this->maxVelocityR;
	if ( this->state.vR < -this->maxVelocityR ) this->state.vR = -this->maxVelocityR;

	if ( !_finite( this->state.vL ) || _isnan( this->state.vL ) ) {
		dt = dt;
	}

	// update velocity estimate
	this->stateEst.vL += dt*this->accelerationEst*accCmdL;
	if ( this->stateEst.vL > this->maxVelocityEst ) this->stateEst.vL = this->maxVelocityEst;
	if ( this->stateEst.vL < -this->maxVelocityEst ) this->stateEst.vL = -this->maxVelocityEst;
	this->stateEst.vR += dt*this->accelerationEst*accCmdR;
	if ( this->stateEst.vR > this->maxVelocityEst ) this->stateEst.vR = this->maxVelocityEst;
	if ( this->stateEst.vR < -this->maxVelocityEst ) this->stateEst.vR = -this->maxVelocityEst;

	// check sensors
	if ( this->actionInProgress ) { // don't submit readings if we're not doing anything
		if ( this->actionQueue.front().action != AA_DELAY ) { 
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
				if ( this->state.t.time > itSC->nextT.time || (this->state.t.time == itSC->nextT.time && this->state.t.millitm >= itSC->nextT.millitm) ) {
					// take reading
					this->doCamera( &(*itSC) );

					do { 
						itSC->nextT.time += (itSC->nextT.millitm + itSC->period)/1000;
						itSC->nextT.millitm = (itSC->nextT.millitm + itSC->period) % 1000;
					} while ( this->state.t.time > itSC->nextT.time || (this->state.t.time == itSC->nextT.time && this->state.t.millitm >= itSC->nextT.millitm) );
				}
			}
		} else { // we're delaying so increase next sensor times so we have a similar number of total readings
			std::list<SimSonar>::iterator itSS;
			for ( itSS = this->sonars.begin(); itSS != this->sonars.end(); itSS++ ) {
				itSS->nextT.time += (itSS->nextT.millitm + (int)(dt*1000))/1000;
				itSS->nextT.millitm = (itSS->nextT.millitm + (int)(dt*1000)) % 1000;
			}
			std::list<SimCamera>::iterator itSC;
			for ( itSC = this->cameras.begin(); itSC != this->cameras.end(); itSC++ ) {
				itSC->nextT.time += (itSC->nextT.millitm + (int)(dt*1000))/1000;
				itSC->nextT.millitm = (itSC->nextT.millitm + (int)(dt*1000)) % 1000;
			}
		}
	}

	this->pathPlanner->step();
	
	return 0;
}

//-----------------------------------------------------------------------------
// Position/Rotation State

int SimAvatar::doLinearMove( float L ) {

	this->moveInProgress = true;
	this->moveTargetL = L;
	this->moveTargetA = 0;
	this->moveCurrent = 0;

	return 0;
}

int SimAvatar::doAngularMove( float A ) {

	this->moveInProgress = true;
	this->moveTargetL = 0;
	this->moveTargetA = A;
	this->moveCurrent = 0;

	return 0;
}

int SimAvatar::doStop() {

	this->moveInProgress = true;
	this->moveTargetL = 0;
	this->moveTargetA = 0;
	this->moveCurrent = 0;

	return 0;
}

int SimAvatar::finishMove() {

	this->moveInProgress = false;
	this->moveTargetL = 0;
	this->moveTargetA = 0;
	this->moveCurrent = 0;

	this->updatePFState();
	//this->updatePos( 0, 0, 0, &this->simTime );

	return 0;
}

int SimAvatar::updatePos( float dx, float dy, float dr, _timeb *tb, bool forcePF ) {

	this->stateEst.x += dx;
	this->stateEst.y += dy;
	this->stateEst.r += dr;

	this->stateEst.t.time = tb->time;
	this->stateEst.t.millitm = tb->millitm;
	
	if ( forcePF 
	  || (this->moveInProgress && ((this->stateEst.t.time - this->lastStateEst.t.time)*1000 + (this->stateEst.t.millitm - this->lastStateEst.t.millitm)) >= PFUPDATE_PERIOD) ) {
		this->updatePFState();
	}

	return 0;
}

int SimAvatar::setTargetPos( float x, float y, float r, char useRotation, UUID *initiator, _Callback *cb, int thread ) {

	if ( this->haveTarget ) { // notify old initiator
		if ( this->targetCB != NULL ) {
			this->ds.reset();
			this->ds.packInt32( this->targetThread );
			this->ds.packBool( false );
			this->ds.packInt32( TP_NEW_TARGET );
			this->ds.rewind();
			_Callback *cb = this->targetCB;
			this->targetCB = NULL;
			cb->callback( &this->ds );
			delete cb;
		}

		// clear current actions
		this->clearActions();

		// queue a stop action so that we start fresh
		this->queueAction( &this->uuid, NEW_MEMBER_CB(SimAvatar,cbActionFinished), 0, AA_STOP );
	}

	this->haveTarget = true;
	this->targetX = x;
	this->targetY = y;
	this->targetR = r;
	this->targetUseRotation = useRotation;
	this->targetInitiator = *initiator;
	this->targetCB = cb;
	this->targetThread = thread;

	// set path planner target		
	this->pathPlanner->setTarget( this->targetX, this->targetY, this->targetR, this->targetUseRotation );

	return 0;
}

int SimAvatar::doneTargetPos( bool success, int reason ) {

	this->haveTarget = false;

	if ( !success && reason == TP_MOVE_ERROR ) { // make sure we stop first
		this->queueAction( &this->uuid, NEW_MEMBER_CB(SimAvatar,cbActionFinished), 0, AA_STOP );
	}

	// notify initiator
	if ( this->targetCB != NULL ) {
		this->ds.reset();
		this->ds.packInt32( this->targetThread );
		this->ds.packBool( success );
		if ( !success ) 
			this->ds.packInt32( reason );
		this->ds.rewind();
		_Callback *cb = this->targetCB;
		this->targetCB = NULL;
		cb->callback( &this->ds );
		delete cb;
	}
	
	//this->ds.reset();
	//this->ds.packInt32( this->targetThread );
	//this->sendMessage( this->hostCon, MSG_RESPONSE, this->ds.stream(), this->ds.length(), &this->targetInitiator );

	return 0;
}

//-----------------------------------------------------------------------------
// Actions

int SimAvatar::clearActions( int reason, bool aborted ) {

	this->_stopAction();

	std::list<ActionInfo>::iterator iterAI;
	while ( (iterAI = this->actionQueue.begin()) != this->actionQueue.end() ) {

		// notify director that the action was canceled/aborted
		RPC_STATUS Status;
		if ( !UuidCompare( &iterAI->director, &this->uuid, &Status ) ) { // we are the director
			this->ds.reset();
			this->ds.packInt32( iterAI->thread );
			if ( aborted )
				this->ds.packChar( AAR_ABORTED );
			else 
				this->ds.packChar( AAR_CANCELLED );
			this->ds.packInt32( reason );
			this->ds.rewind();
			iterAI->cb->callback( &this->ds );
		} else {
			this->ds.reset();
			this->ds.packInt32( iterAI->thread );
			if ( aborted )
				this->ds.packChar( AAR_ABORTED );
			else 
				this->ds.packChar( AAR_CANCELLED );
			this->ds.packInt32( reason );
			this->ds.rewind();
			iterAI->cb->callback( &this->ds );
		}

		delete iterAI->cb;
		this->actionQueue.pop_front();
	}
	std::list<void*>::iterator iter = this->actionData.begin();
	while ( iter != this->actionData.end() ) {
		free(*iter);
		iter++;
	}
	this->actionData.clear();

	return 0;
}

int SimAvatar::abortActions( int reason ) {
	return this->clearActions( reason, true );
}

int SimAvatar::queueAction( UUID *director, _Callback *cb, int thread, int action, void *data, int len ) {
	ActionInfo AI;
	AI.action = action;
	AI.director = *director;
	AI.cb = cb;
	AI.thread = thread;
	void *datacpy = NULL;

	Log.log( LOG_LEVEL_VERBOSE, "SimAvatar::queueAction: action %d, thread %d, director %s", action, thread, Log.formatUUID(LOG_LEVEL_VERBOSE,director) );

	if ( len ) {
		datacpy = malloc(len);
	
		if ( !datacpy )
			return 1;

		memcpy( datacpy, data, len );
	}

	this->actionQueue.push_back( AI );
	this->actionData.push_back( datacpy );

	if ( this->useDelayActionsFlag && action != AA_DELAY ) { // queue an extra delay
		this->queueAction( &this->uuid, NEW_MEMBER_CB(SimAvatar,cbActionFinished), 0, AA_DELAY );
	}

	if ( !this->actionInProgress ) {
		this->nextAction();
	}

	return 0;
}

int SimAvatar::nextAction() {
	
	ActionInfo *pAI;

	if ( this->actionInProgress ) { // finish the currect action
		pAI = &(*this->actionQueue.begin());

		Log.log( LOG_LEVEL_VERBOSE, "SimAvatar::nextAction: finished action %d, thread %d, director %s", pAI->action, pAI->thread, Log.formatUUID(LOG_LEVEL_VERBOSE,&pAI->director) );

		if ( pAI->action == AA_MOVE
		  || pAI->action == AA_ROTATE ) {
			this->updatePFState(); // update our state
		}
		
		// notify director that the action is complete
		RPC_STATUS Status;
		if ( pAI->cb != NULL ) {
			if ( !UuidCompare( &pAI->director, &this->uuid, &Status ) ) { // we are the director
				this->ds.reset();
				this->ds.packInt32( pAI->thread );
				this->ds.packChar( AAR_SUCCESS );
				this->ds.rewind();
				pAI->cb->callback( &this->ds );
			} else {
				this->ds.reset();
				this->ds.packInt32( pAI->thread );
				this->ds.packChar( AAR_SUCCESS );
				this->ds.rewind();
				pAI->cb->callback( &this->ds );
			}

			delete pAI->cb;
		}

		this->actionQueue.pop_front();
		free( this->actionData.front() );
		this->actionData.pop_front();

		this->actionInProgress = false;
	} 
	
	if ( this->actionQueue.begin() != this->actionQueue.end() ) {
		pAI = &(*this->actionQueue.begin());

		Log.log( LOG_LEVEL_VERBOSE, "SimAvatar::nextAction: starting action %d, thread %d, director %s", pAI->action, pAI->thread, Log.formatUUID(LOG_LEVEL_VERBOSE,&pAI->director) );

		if ( pAI->action == AA_MOVE
		  || pAI->action == AA_ROTATE ) {
			this->updatePFState(); // refresh our pos to begin move
			//this->updatePos( 0, 0, 0, &this->simTime ); // refresh our pos to begin move
		}

		this->actionInProgress = true;
	}

	if ( !this->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)*this->actionData.begin();
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( !(this->actionTimeout = this->addTimeout( millis, NEW_MEMBER_CB( SimAvatar, cbActionStep ) ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)*this->actionData.begin();
			unsigned short millis;
			
			float D = *pAD;
			float vel = (this->stateEst.vL + this->stateEst.vR)/2;
			float velMax = this->maxVelocityEst;
			float acc = this->accelerationEst;
			float dec = this->accelerationEst;
			float accD, cruD, decD;
			float accT, cruT, decT;

			// make sure we're accelerating in the right direction
			if ( D < 0 ) {
				velMax *= -1;
				acc *= -1;
				dec *= -1;
			}

			// see if we will reach crusing speed
			accT = (velMax - vel)/acc;
			accD = vel*accT + 0.5f*acc*accT*accT;
			decT = velMax/dec;
			decD = 0.5f*dec*decT*decT;
			if ( fabs(D) >= fabs(accD + decD) ) { // we will cruse
				cruD = D - accD - decD;
				cruT = cruD/velMax;
				millis = (unsigned short)(1000 * (accT + cruT + decT));
			} else { // we never reach crusing speed
				decT = sqrt( (2*D + vel*vel/acc)/(dec*dec/acc + dec) );
				accT = (dec*decT - vel)/acc;
				millis = (unsigned short)(1000 * (accT + decT));
			}

			this->doLinearMove( D );

			this->actionGiveupTime.time = this->simTime.time;
			this->actionGiveupTime.millitm = this->simTime.millitm;
			this->actionGiveupTime.time += (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) / 1000;
			this->actionGiveupTime.millitm = (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) % 1000;
			if ( !(this->actionTimeout = this->addTimeout( SimAvatar_MOVECHECK_PERIOD, NEW_MEMBER_CB( SimAvatar, cbActionStep ) ) ) )
				return 1;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)*this->actionData.begin();
			unsigned short millis;

			float D = *pAD;
			float vel = (this->stateEst.vR - this->stateEst.vL)/this->wheelBaseEst;
			float velMax = 2*this->maxVelocityEst/this->wheelBaseEst;
			float acc = 2*this->accelerationEst/this->wheelBaseEst;
			float dec = acc;
			float accD, cruD, decD;
			float accT, cruT, decT;

			// make sure we're accelerating in the right direction
			if ( D < 0 ) {
				velMax *= -1;
				acc *= -1;
				dec *= -1;
			}

			// see if we will reach crusing speed
			accT = (velMax - vel)/acc;
			accD = vel*accT + 0.5f*acc*accT*accT;
			decT = velMax/dec;
			decD = 0.5f*dec*decT*decT;
			if ( fabs(D) >= fabs(accD + decD) ) { // we will cruse
				cruD = D - accD - decD;
				cruT = cruD/velMax;
				millis = (unsigned short)(1000 * (accT + cruT + decT));
			} else { // we never reach crusing speed
				decT = sqrt( (2*D + vel*vel/acc)/(dec*dec/acc + dec) );
				accT = (dec*decT - vel)/acc;
				millis = (unsigned short)(1000 * (accT + decT));
			}

			this->doAngularMove( D );
			
			this->actionGiveupTime.time = this->simTime.time;
			this->actionGiveupTime.millitm = this->simTime.millitm;
			this->actionGiveupTime.time += (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) / 1000;
			this->actionGiveupTime.millitm = (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) % 1000;
			if ( !(this->actionTimeout = this->addTimeout( SimAvatar_MOVECHECK_PERIOD, NEW_MEMBER_CB( SimAvatar, cbActionStep ) ) ) )
				return 1;
		}
		break;
	case AA_STOP:
		{
			unsigned short millis;

			float vel = max(fabs(this->stateEst.vR), fabs(this->stateEst.vL));
			float dec = this->accelerationEst;
			float decT;

			// see if we will reach crusing speed
			decT = vel/dec;
			millis = (unsigned short)(1000 * decT);
			
			this->doStop();
			
			this->actionGiveupTime.time = this->simTime.time;
			this->actionGiveupTime.millitm = this->simTime.millitm;
			this->actionGiveupTime.time += (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) / 1000;
			this->actionGiveupTime.millitm = (this->actionGiveupTime.millitm + millis + SimAvatar_MOVE_FINISH_DELAY) % 1000;
			if ( !(this->actionTimeout = this->addTimeout( SimAvatar_MOVECHECK_PERIOD, NEW_MEMBER_CB( SimAvatar, cbActionStep ) ) ) )
				return 1;
		}
		break;
	case AA_IMAGE:
		
		break;
	case AA_DELAY:
		if ( !(this->actionTimeout = this->addTimeout( 100, NEW_MEMBER_CB( SimAvatar, cbActionStep ) ) ) )
			return 1;
		break;
	default:
		//Log.log( 0, "AvatarSurveyor::nextAction: unknown action type %d", pAI->action );
		break;
	}

	return 0;
}

int SimAvatar::_stopAction() {

	if ( this->actionTimeout ) {
		this->removeTimeout( this->actionTimeout );
		this->actionTimeout = NULL;
	}
	this->actionInProgress = false;

	return 0;
}

//-----------------------------------------------------------------------------
// Particle Filter

int SimAvatar::createParticleFilter( int numParticles, float *sigma ) {
	_timeb startTime;

	startTime.time = this->simTime.time;
	startTime.millitm = this->simTime.millitm;

	if ( 0 ) { // debug
		int i;
		this->pfNumParticles = 7;
	
		// generate initial state
		this->pfState = (float *)malloc(sizeof(float)*this->pfNumParticles*AVATAR_PFSTATE_SIZE);
		if ( !this->pfState )
			return 1; // malloc failed

		for ( i=0; i<this->pfNumParticles; i++ ) {
			this->pfState[i*3+0] = this->state.x;
			this->pfState[i*3+1] = this->state.y + (i-(this->pfNumParticles/2))*0.1f;
			this->pfState[i*3+2] = this->state.r;
		}
	} else { // real code
		// generate initial state
		this->pfState = (float *)malloc(sizeof(float)*numParticles*AVATAR_PFSTATE_SIZE);
		if ( !this->pfState )
			return 1; // malloc failed

		this->pfNumParticles = numParticles;

		this->generatePFState( sigma );
	}

	// register with DDB
	sim->ddbAddParticleFilter( &this->uuid, &this->uuid, this->pfNumParticles, &startTime, this->pfState, AVATAR_PFSTATE_SIZE );

	return 0;
}

int SimAvatar::destroyParticleFilter() {

	// remove from DDB
	sim->ddbRemoveParticleFilter( &this->uuid );

	free( this->pfState );

	return 0;
}

int SimAvatar::generatePFState( float *sigma ) {
	int i;
	float *pstate = this->pfState;

	for ( i=0; i<this->pfNumParticles; i++ ) {
		pstate[AVATAR_PFSTATE_X] = this->stateEst.x + (float)this->randGen->NormalDistribution( 0, sigma[0] );
		pstate[AVATAR_PFSTATE_Y] = this->stateEst.y + (float)this->randGen->NormalDistribution( 0, sigma[1] );
		pstate[AVATAR_PFSTATE_R] = this->stateEst.r + (float)this->randGen->NormalDistribution( 0, sigma[2] );
		pstate += AVATAR_PFSTATE_SIZE;
	}

	return 0;
}

int SimAvatar::updatePFState() {
	int i;
	float forwardD, tangentialD, rotationalD;
	float pforwardD, ptangentialD, protationalD;
	float dt;
	float sn, cs;
	float *pState;

	this->resetTimeout( this->pfUpdateTimer );

	_timeb tb;

	tb.time = stateEst.t.time;
	tb.millitm = stateEst.t.millitm;

	dt = (this->lastStateEst.t.time - this->stateEst.t.millitm) + (this->lastStateEst.t.millitm - this->stateEst.t.millitm)*0.001f;
	
	sn = sin( this->lastStateEst.r );
	cs = cos( this->lastStateEst.r );
	forwardD = (this->stateEst.x - this->lastStateEst.x)*cs + (this->stateEst.y - this->lastStateEst.y)*sn;
	tangentialD = (this->stateEst.x - this->lastStateEst.x)*-sn + (this->stateEst.y - this->lastStateEst.y)*cs;
	rotationalD = this->stateEst.r - this->lastStateEst.r;

	if ( forwardD == 0 && tangentialD == 0 && rotationalD == 0 ) { // no change
		// send no change to DDB
		sim->ddbInsertPFPrediction( &this->uuid, &tb, NULL, true );
	} else {
		pState = this->pfState;
		for ( i=0; i<this->pfNumParticles; i++ ) {
			pforwardD = forwardD + (float)this->randGen->NormalDistribution(0,this->pfUpdateSigma[0])*dt;
			ptangentialD = tangentialD + (float)this->randGen->NormalDistribution(0,this->pfUpdateSigma[1])*dt;
			protationalD = rotationalD + (float)this->randGen->NormalDistribution(0,this->pfUpdateSigma[2])*dt;

			sn = sin( pState[AVATAR_PFSTATE_R] );
			cs = cos( pState[AVATAR_PFSTATE_R] );

			pState[AVATAR_PFSTATE_X] = pState[AVATAR_PFSTATE_X] + cs*pforwardD - sn*ptangentialD;
			pState[AVATAR_PFSTATE_Y] = pState[AVATAR_PFSTATE_Y] + sn*pforwardD + cs*ptangentialD;
			pState[AVATAR_PFSTATE_R] = pState[AVATAR_PFSTATE_R] + protationalD;
			pState += AVATAR_PFSTATE_SIZE;
		}

		// send update to DDB
		sim->ddbInsertPFPrediction( &this->uuid, &tb, this->pfState );

#ifdef SimAvatar_SAVE_PATH
		this->truePath.push_back( this->state );
		//Log.log( 0, "SAVEPATH: %f %f", this->state.x, this->state.y );
#endif
	}

	this->lastStateEst = this->stateEst;
	
	return 0;
}

int	SimAvatar::pfResampled( _timeb *startTime, int *parents, float *state ) {
	
	memcpy( this->pfState, state, sizeof(float)*AVATAR_PFSTATE_SIZE*this->pfNumParticles );

	return 0;
}

//-----------------------------------------------------------------------------
// Sensors

int SimAvatar::doSonar( SimSonar *sonar ) {
	_timeb tb;
	SonarReading reading;
	Visualize::OBJECT *object;
	Visualize::PATH *path;
	Visualize::NODE *nodeA, *nodeB;
	float boundary[3][2]; // sonar boundary
	float boundaryO[3][2]; // boundary in object space
	float distSq[5]; // distances squared of points A, B, C, D, and E
	float pt[5][2];
	float tmp, tmpA, tmpB, tmpC;
	float minValSq;
	int i, mini;
	int intersections;
	float sn, cs;
	
	std::list<Visualize::PATH_REFERENCE *>::iterator itPR;
	std::list<Visualize::NODE *>::iterator itN;

	float hitValSq = sonar->pose.max*sonar->pose.max;
	bool hitValid = false;

	tb.time = this->simTime.time;
	tb.millitm = this->simTime.millitm;

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
	while ( (objId = vis->getNextObject( &object, objId )) != -1 ) {
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
			if ( vis->getPath( (*itPR)->id, &path ) )
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
		reading.value += (float)randGen->NormalDistribution( 0, SimAvatar_SONAR_SIGMA ); // m error

		sim->ddbInsertSensorReading( &sonar->id, &tb, &reading, sizeof(SonarReading) );

	} else {
		reading.value = -1;
	}

	// visualize
	if ( reading.value > 0 ) {
		if ( this->extraVisible )
			vis->showObject( sonar->objId );

		sonar->x = boundary[0][0];
		sonar->y = boundary[0][1];
		sonar->r = state.r + sonar->pose.r;
		sonar->s = reading.value;
	} else {
		vis->hideObject( sonar->objId );

		sonar->s = reading.value;
	}

	return 0;
}

int SimAvatar::doCamera( SimCamera *camera ) {
	RPC_STATUS Status;
	_timeb tb;
	int i;
	float sn, cs;
	float readingVal, readingAngle;
	SimLandmark *lm;
	float tmp, tmpA, tmpB, tmpC;
	CameraReading cameraReading;
	float maxSq = camera->max*camera->max;
	float distSq;
	float pt[2];
	int hitCount = 0;

	Visualize::OBJECT *object;
	Visualize::PATH *path;
	Visualize::NODE *nodeA, *nodeB;
	std::list<Visualize::PATH_REFERENCE *>::iterator itPR;
	std::list<Visualize::NODE *>::iterator itN;
	float line[2][2]; // camera to landmark in world space
	float lineO[2][2]; // camera to landmark in object space
	bool blocked;
	int objId;

	tb.time = this->simTime.time;
	tb.millitm = this->simTime.millitm;
	
	// transform boundary into world space
	sn = sin( state.r );
	cs = cos( state.r );
	camera->x = state.x + camera->pose.x*cs - camera->pose.y*sn;
	camera->y = state.y + camera->pose.x*sn + camera->pose.y*cs;

	line[0][0] = camera->x;
	line[0][1] = camera->y;

	this->ds.reset();

	// iterate through landmarks
	std::list<SimLandmark*> *landmarks = sim->getLandmarkList();
	std::list<SimLandmark*>::iterator itSL;
	for ( itSL = landmarks->begin(); itSL != landmarks->end(); itSL++ ) {
		lm = *itSL;

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
				while ( (objId = vis->getNextObject( &object, objId )) != -1 ) {
					if ( !object->solid || !object->visible )
						continue;

					// transform into object space
					sn = sin( *object->r );
					cs = cos( *object->r );
					for ( i=0; i<3; i++ ) {
						lineO[i][0] = line[i][0] - *object->x;
						lineO[i][1] = line[i][1] - *object->y;
						tmp = ( lineO[i][0]*cs + lineO[i][1]*sn ) / *object->s;
						lineO[i][1] = ( lineO[i][0]*-sn + lineO[i][1]*cs ) / *object->s;
						lineO[i][0] = tmp;
					}

					// iterate through paths
					for ( itPR = object->path_refs->begin(); itPR != object->path_refs->end(); itPR++ ) {
						if ( vis->getPath( (*itPR)->id, &path ) )
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
					readingVal *= 1 + (float)randGen->NormalDistribution( 0, SimAvatar_CAMERA_R_SIGMA ); // percentage error
					readingAngle += (float)randGen->NormalDistribution( 0, SimAvatar_CAMERA_A_SIGMA ); // radian error

					// pack reading
					this->ds.packChar( SimCamera_DATA_LANDMARK );
					this->ds.packChar( lm->code );
					this->ds.packFloat32( readingVal );
					this->ds.packFloat32( readingAngle );

					// visualize
					if ( hitCount < 10 ) { // we only vis the first 10 hits
						if ( camera->objId[hitCount] == -1 ) // object hasn't be created yet
							camera->objId[hitCount] = vis->newDynamicObject( &camera->x, &camera->y, &camera->r[hitCount], &camera->s[hitCount], sim->getLinePathId(), camera->color, 1 );
						
						camera->r[hitCount] = state.r + camera->pose.r + readingAngle;
						camera->s[hitCount] = readingVal;
						if ( this->extraVisible )
							vis->showObject( camera->objId[hitCount] );
						else
							vis->hideObject( camera->objId[hitCount] );
					}

					hitCount++;
				}
			}
		}

	}

	// submit to DDB
	if ( hitCount ) {
		this->ds.packChar( SimCamera_DATA_END );

		sprintf_s( cameraReading.format, sizeof(cameraReading.format), "stream" );
		sim->ddbInsertSensorReading( &camera->id, &tb, &cameraReading, sizeof(CameraReading), this->ds.stream(), this->ds.length() );
	}

	// hide extra objects
	for ( i=hitCount; i<10; i++ ) {
		if ( camera->objId[i] == -1 )
			break;
		vis->hideObject( camera->objId[i] );
		camera->s[i] = -1;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool SimAvatar::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		this->actionTimeout = NULL;
		this->nextAction();
		return 0;
	case AA_MOVE:
		if ( !this->moveInProgress ) {
			this->actionTimeout = NULL;
			this->nextAction();
		} else {
			if ( this->simTime.time < this->actionGiveupTime.time
			  || this->simTime.time == this->actionGiveupTime.time && this->simTime.millitm < this->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			this->abortActions();
			if ( this->haveTarget )
				this->doneTargetPos( false, TP_MOVE_ERROR );
		}
		return 0;
	case AA_ROTATE:
		if ( !this->moveInProgress ) {
			this->actionTimeout = NULL;
			this->nextAction();
		} else {
			if ( this->simTime.time < this->actionGiveupTime.time
			  || this->simTime.time == this->actionGiveupTime.time && this->simTime.millitm < this->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			this->abortActions();
			if ( this->haveTarget )
				this->doneTargetPos( false, TP_MOVE_ERROR );
		}
		return 0;
	case AA_STOP:
		if ( !this->moveInProgress ) {
			this->actionTimeout = NULL;
			this->nextAction();
		} else {
			if ( this->simTime.time < this->actionGiveupTime.time
			  || this->simTime.time == this->actionGiveupTime.time && this->simTime.millitm < this->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			this->abortActions();
			if ( this->haveTarget )
				this->doneTargetPos( false, TP_MOVE_ERROR );
		}
		return 0;
	case AA_DELAY:
		if ( !sim->getReadingQueueSize() ) {
			this->actionTimeout = NULL;
			this->nextAction();
		} else {
			return 1; // keep waiting
		}
	default:
		// nothing
		break;
	}

	return 0;
}

bool SimAvatar::cbActionFinished( void *vpdata ) {

	return 0;
}

bool SimAvatar::cbPFUpdateTimer( void *NA ) {

	this->updatePFState();
	
	return 1; // repeat
}