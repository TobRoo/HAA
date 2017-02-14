// AvatarSimulation.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\Autonomic\\autonomic.h"
#include "..\\Autonomic\\DDB.h"
#include "..\\AvatarBase\\AvatarBase.h"


#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;

#include "AvatarSimulation.h"
#include "AvatarSimulationVersion.h"

#include "..\\ExecutiveSimulation\\ExecutiveSimulationVersion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

WCHAR DLL_Directory[512];

//*****************************************************************************
// AvatarSimulation

#define DegreesToRadians(a) (a*M_PI/180.0) 
#define RadiansToDegrees(a) (a*180.0/M_PI)

//-----------------------------------------------------------------------------
// Constructor	
AvatarSimulation::AvatarSimulation( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) : AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AvatarSimulation, AvatarBase )
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarSimulation" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarSimulation_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AvatarSimulation_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	UuidCreateNil( &STATE(AvatarSimulation)->execSimulationId );

	STATE(AvatarSimulation)->TYPE[0] = 0;
	STATE(AvatarSimulation)->AVATAR_FILE[0] = 0;

	STATE(AvatarSimulation)->simConfigured = false;
	STATE(AvatarSimulation)->simRegistrationConfirmed = false;
	STATE(AvatarSimulation)->simWakeTimeout = nilUUID;

	STATE(AvatarSimulation)->simPoseInitialized = false;

	STATE(AvatarSimulation)->moveId = 0;
	
	memset( STATE(AvatarSimulation)->skipReading, 0, sizeof(STATE(AvatarSimulation)->skipReading) );
	STATE(AvatarSimulation)->requestedImages = 0;

	STATE(AvatarSimulation)->capacity = 0;

	// Prepare callbacks
	this->callback[AvatarSimulation_CBR_cbActionStep] = NEW_MEMBER_CB(AvatarSimulation,cbActionStep);
	this->callback[AvatarSimulation_CBR_cbSimWake] = NEW_MEMBER_CB(AvatarSimulation,cbSimWake);
	this->callback[AvatarSimulation_CBR_convRequestExecutiveSimulationId] = NEW_MEMBER_CB(AvatarSimulation,convRequestExecutiveSimulationId);
	this->callback[AvatarSimulation_CBR_cbRequestExecutiveSimulationId] = NEW_MEMBER_CB(AvatarSimulation,cbRequestExecutiveSimulationId);
	this->callback[AvatarSimulation_CBR_convRequestAvatarOutput] = NEW_MEMBER_CB(AvatarSimulation,convRequestAvatarOutput);
	
}

//-----------------------------------------------------------------------------
// Destructor
AvatarSimulation::~AvatarSimulation() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	// free state

}

//-----------------------------------------------------------------------------
// Configure

int AvatarSimulation::configure() {
		
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
		sprintf_s( logName, "%s\\AvatarSimulation %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AvatarSimulation %.2d.%.2d.%.5d.%.2d", AvatarSimulation_MAJOR, AvatarSimulation_MINOR, AvatarSimulation_BUILDNO, AvatarSimulation_EXTEND );
	}

	if ( AvatarBase::configure() ) 
		return 1;

	return 0;
}

int AvatarSimulation::setInstance( char instance ) {
	char paraN[512];
	FILE *paraF;

	// open parameter file
	sprintf_s( paraN, sizeof(paraN), "%ws\\instance%d.ini", DLL_Directory, instance );
	if ( fopen_s( &paraF, paraN, "r" ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: failed to open %s", paraN );
		return 1;
	}

/*	if ( 1 != fscanf_s( paraF, "TYPE=%s\n", STATE(AvatarSimulation)->TYPE, sizeof(STATE(AvatarSimulation)->TYPE) ) ) {
		Log.log( 0, "AvatarSimulation::setInstance: expected TYPE=<string>, check format" );
		goto returnerror;
	}	
	if ( 1 != fscanf_s( paraF, "AVATAR_FILE=%s\n", STATE(AvatarSimulation)->AVATAR_FILE, sizeof(STATE(AvatarSimulation)->AVATAR_FILE) ) ) {
		Log.log( 0, "AvatarSimulation::setInstance: expected AVATAR_FILE=<string>, check format" );
		goto returnerror;
	}	
*/	if ( 1 != fscanf_s( paraF, "LANDMARK_CODE_OFFSET=%d\n", &STATE(AvatarSimulation)->LANDMARK_CODE_OFFSET ) ) {
		Log.log( 0, "AvatarSimulation::setInstance: expected LANDMARK_CODE_OFFSET=<int>, check format" );
	//	goto returnerror;
	}

	fclose( paraF );

/*	this->parseAvatarFile( STATE(AvatarSimulation)->AVATAR_FILE );

	STATE(AvatarBase)->maxLinear = 0.2f;
	STATE(AvatarBase)->maxRotation = 20.0f/180 * fM_PI;

	STATE(AvatarSimulation)->capacity = 1;

	// register with DDB
	this->registerAvatar( STATE(AvatarSimulation)->TYPE, STATE(AvatarSimulation)->wheelBaseEst, STATE(AvatarSimulation)->wheelBaseEst*1.5f, STATE(AvatarSimulation)->capacity );
*/	
	return AvatarBase::setInstance( instance );
/*
returnerror:
	fclose( paraF );
	return 1; */
}

int AvatarSimulation::parseAvatarFile( char *avatarFile ) {
	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;

	if ( fopen_s( &fp, avatarFile, "r" ) ) {
		Log.log( 0, "AvatarSurveyor::parseAvatarFile: failed to open %s", avatarFile );
		return 1; // couldn't open file
	}

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

int AvatarSimulation::parseBase( FILE *fp ) {
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
	STATE(AvatarSimulation)->wheelBaseEst = wheelBaseEst;
	STATE(AvatarSimulation)->accelerationEst = accEst;
	STATE(AvatarSimulation)->maxVelocityEst = maxVEst;
	STATE(AvatarSimulation)->capacity = capacity;
	
	return 0;
}


int AvatarSimulation::parseParticleFilter( FILE *fp ) {
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

	// save data
	STATE(AvatarBase)->pfNumParticles = num;
	STATE(AvatarBase)->pfInitSigma[0] = initSigma[0];
	STATE(AvatarBase)->pfInitSigma[1] = initSigma[1];
	STATE(AvatarBase)->pfInitSigma[2] = initSigma[2];
	STATE(AvatarBase)->pfUpdateSigma[0] = updateSigma[0];
	STATE(AvatarBase)->pfUpdateSigma[1] = updateSigma[1];
	STATE(AvatarBase)->pfUpdateSigma[2] = updateSigma[2];

	return 0;
}

int AvatarSimulation::parseLandmark( FILE *fp ) {
	DataStream lds;
	UUID uuid;
	float x, y;
	int code;

	// read data
	if ( 2 != fscanf_s( fp, "OFFSET=%f %f\n", &x, &y ) ) {
		return 1;
	}
	if ( 1 != fscanf_s( fp, "CODE=%d\n", &code ) ) {
		return 1;
	}

	apb->apbUuidCreate( &uuid );
	code += STATE(AvatarSimulation)->LANDMARK_CODE_OFFSET;

	this->registerLandmark( &uuid, (unsigned char)code, &STATE(AvatarBase)->avatarUUID, x, y );
	
	return 0;
}


int AvatarSimulation::parseSonar( FILE *fp ) {
	UUID uuid;
	int period;
	float color[3];

	SonarPose sp;

	// read data
	if ( 1 != fscanf_s( fp, "PERIOD=%d\n", &period ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &color[0], &color[1], &color[2] ) ) {
		return 1;
	}
	if ( 7 != fscanf_s( fp, "SENSOR_SONAR=%f %f %f %f %f %f %f %f\n", 
			  &sp.x, &sp.y, &sp.r,
			  &sp.max,
			  &sp.sigma,
			  &sp.alpha, &sp.beta ) ) {
		return 1;
	}

	// generate UUID for sensor
	apb->apbUuidCreate( &uuid );
	this->sonarId[(int)this->sonarId.size()] = uuid;
	
	this->registerSensor( &uuid, DDB_SENSOR_SONAR, &STATE(AvatarBase)->avatarUUID, &STATE(AvatarBase)->pfId, &sp, sizeof(SonarPose) );

	STATE(AvatarBase)->sensorTypes |= DDB_SENSOR_SONAR;

	return 0;
}

int AvatarSimulation::parseCamera( FILE *fp ) {
	UUID uuid;
	int period;
	float color[3];
	float max, alpha;

	CameraPose cp;
	memset( &cp, 0, sizeof(CameraPose) );
	
	// read data
	if ( 1 != fscanf_s( fp, "PERIOD=%d\n", &period ) ) {
		return 1;
	}
	if ( 3 != fscanf_s( fp, "COLOR=%f %f %f\n", &color[0], &color[1], &color[2] ) ) {
		return 1;
	}
	if ( 6 != fscanf_s( fp, "SENSOR_CAMERA=%f %f %f %f %f %f %f\n", 
			  &cp.x, &cp.y, &cp.r,
			  &max,
			  &cp.sigma,
			  &alpha ) ) {
		return 1;
	}

	// generate UUID for sensor
	apb->apbUuidCreate( &uuid );
	this->cameraId[(int)this->cameraId.size()] = uuid;
	
	this->registerSensor( &uuid, DDB_SENSOR_SIM_CAMERA, &STATE(AvatarBase)->avatarUUID, &STATE(AvatarBase)->pfId, &cp, sizeof(CameraPose) );
	
	STATE(AvatarBase)->sensorTypes |= DDB_SENSOR_SIM_CAMERA;

	return 0;
}

int AvatarSimulation::simConfigure() {

	if ( STATE(AvatarSimulation)->simConfigured || !STATE(AvatarBase)->posInitialized || STATE(AvatarSimulation)->execSimulationId == nilUUID ) 
		return 0; // wait until we have a start pos

	// add SimWake timeout
	STATE(AvatarSimulation)->simWakeTimeout = this->addTimeout( AvatarSimulation_SIMWAKE_PERIOD, AvatarSimulation_CBR_cbSimWake );

	STATE(AvatarSimulation)->simConfigured = true;
	STATE(AvatarSimulation)->simRegistrationConfirmed = false;

	this->backup(); // simConfigured

	return 0;
}

int AvatarSimulation::parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode ) {
		
	if ( agentType->uuid == STATE(AgentBase)->agentType.uuid 
		&& agentType->instance == STATE(AgentBase)->agentType.instance ) {

		strcpy_s( STATE(AvatarSimulation)->AVATAR_FILE, sizeof(STATE(AvatarSimulation)->AVATAR_FILE), fileName );
		this->parseAvatarFile( STATE(AvatarSimulation)->AVATAR_FILE );

		STATE(AvatarBase)->maxLinear = 0.2f;
		STATE(AvatarBase)->maxRotation = 20.0f/180 * fM_PI;

		// register with DDB
		this->registerAvatar( STATE(AvatarSimulation)->AVATAR_FILE, STATE(AvatarSimulation)->wheelBaseEst, STATE(AvatarSimulation)->wheelBaseEst*1.5f, STATE(AvatarSimulation)->capacity, STATE(AvatarBase)->sensorTypes );
	
	}

	AvatarBase::parseMF_HandleAvatar( agentType, fileName, x, y, r, startTime, duration, retireMode );

	return 0;
}


//-----------------------------------------------------------------------------
// Start

int AvatarSimulation::start( char *missionFile ) {
	UUID thread;

	if ( AvatarBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// find the Simulation Executive so we can register
	UUID simExecType;
	UuidFromString( (RPC_WSTR)_T(ExecutiveSimulation_UUID), &simExecType );
	thread = this->conversationInitiate( AvatarSimulation_CBR_convRequestExecutiveSimulationId, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &simExecType );
	this->ds.packChar( -1 );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RUNIQUEID, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	STATE(AgentBase)->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarSimulation::stop() {

	this->removeTimeout( &STATE(AvatarSimulation)->simWakeTimeout );

	return AvatarBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarSimulation::step() {

	static int debugStuff = 0;
	if ( debugStuff && this->avatarReady ) {
		debugStuff = 0;

		float val;
		val = 1;

		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );

	}

	return AvatarBase::step();
}

int AvatarSimulation::setAvatarState( bool ready ) {

	if ( ready ) {
	} else {
	}

	return AvatarBase::setAvatarState( ready );
}

int AvatarSimulation::retireAvatar() {

	// rem sim avatar
	this->ds.reset();
	this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
	this->ds.packChar( (char)STATE(AvatarBase)->retireMode );
	this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_REMAVATAR), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
	this->ds.unlock();

	return AvatarBase::retireAvatar();
}

int AvatarSimulation::setPos( float x, float y, float r, _timeb *tb ) {

	if ( AvatarBase::setPos( x, y, r, tb ) )
		return 1;

	this->simConfigure();

	return 0;
}

int AvatarSimulation::updateSimPos( float x, float y, float r, _timeb *tb ) {
	float dx, dy, dr;
	
	if ( !STATE(AvatarSimulation)->simPoseInitialized ) {
		STATE(AvatarSimulation)->simPoseInitialized = true;
		STATE(AvatarSimulation)->lastUpdatePosX = x;
		STATE(AvatarSimulation)->lastUpdatePosY = y;
		STATE(AvatarSimulation)->lastUpdatePosR = r;

		this->setAvatarState( true ); // we're ready

		return 0;
	}

	dx = (x - STATE(AvatarSimulation)->lastUpdatePosX);
	dy = (y - STATE(AvatarSimulation)->lastUpdatePosY);
	dr = (r - STATE(AvatarSimulation)->lastUpdatePosR);

	Log.log( LOG_LEVEL_ALL, "AvatarSimulation::updateSimPos: robot x %f y %f r %f last x %f y %f r %f delta x %f y %f r %f", x, y, r, STATE(AvatarSimulation)->lastUpdatePosX, STATE(AvatarSimulation)->lastUpdatePosY, STATE(AvatarSimulation)->lastUpdatePosR, dx, dy, dr );

	float sn, cs;
	float forwardD, tangentialD;
	sn = sin( STATE(AvatarSimulation)->lastUpdatePosR );
	cs = cos( STATE(AvatarSimulation)->lastUpdatePosR );
	forwardD = dx*cs + dy*sn;
	tangentialD = dx*-sn + dy*cs;

	STATE(AvatarSimulation)->lastUpdatePosX = x;
	STATE(AvatarSimulation)->lastUpdatePosY = y;
	STATE(AvatarSimulation)->lastUpdatePosR = r;

	return AvatarBase::updatePos( forwardD, tangentialD, dr, tb );
}

int AvatarSimulation::parseAvatarOutput( DataStream *ds ) {
	DataStream lds;
	char evt;

	while ( (evt = ds->unpackChar()) != ExecutiveSimulation_Defs::SAE_STREAM_END ) {
		switch (evt) {
		case ExecutiveSimulation_Defs::SAE_MOVE_FINISHED:
			{
				char moveId;
				moveId = ds->unpackChar();
				if ( moveId == STATE(AvatarSimulation)->moveId ) // make sure this is the current move
					STATE(AvatarSimulation)->moveDone = true;
			}
			break;
		case ExecutiveSimulation_Defs::SAE_POSE_UPDATE:
			{
				_timeb *t;
				float x, y, r;

				t = (_timeb *)ds->unpackData(sizeof(_timeb));
				x = ds->unpackFloat32();
				y = ds->unpackFloat32();
				r = ds->unpackFloat32();
				
				STATE(AvatarSimulation)->lastUpdateVelLin = ds->unpackFloat32();
				STATE(AvatarSimulation)->lastUpdateVelAng = ds->unpackFloat32();

				this->updateSimPos( x, y, r, t );
			}
			break;
		case ExecutiveSimulation_Defs::SAE_SENSOR_SONAR:
			{
				char index;
				_timeb *t;
				SonarReading sr;

				index = ds->unpackChar();
				t = (_timeb *)ds->unpackData(sizeof(_timeb));
				sr.value = ds->unpackFloat32();

				if ( STATE(AvatarBase)->haveTarget ) { // only submit if we have a target
					this->submitSensorSonar( &this->sonarId[index], t, &sr );
				}
			}
			break;
		case ExecutiveSimulation_Defs::SAE_SENSOR_CAMERA:
			{
				char index;
				_timeb *t;
				int dataSize;
				void *data;
				CameraReading cr;
				cr.w = cr.h = 0;
				sprintf_s( cr.format, sizeof(cr.format), "stream" );

				index = ds->unpackChar();
				t = (_timeb *)ds->unpackData(sizeof(_timeb));
				dataSize = ds->unpackInt32();
				data = ds->unpackData( dataSize );

				if ( STATE(AvatarBase)->haveTarget ) { // only submit if we have a target
					this->submitSensorCamera( &this->cameraId[index], t, &cr, data, dataSize );
				}

				if ( STATE(AvatarSimulation)->requestedImages > 0 ) { // make sure we were expecting something
					STATE(AvatarSimulation)->requestedImages--;
					if ( STATE(AvatarSimulation)->requestedImages == 0 ) {
						this->cameraCommandFinished();
					}
				}
			}
			break;
		case ExecutiveSimulation_Defs::SAE_COLLECT:
			{
				unsigned char code, success;
				UUID thread;
				code = ds->unpackUChar();
				success = ds->unpackChar();
				ds->unpackUUID( &thread );

				if ( success ) {
					// update avatar capacity
					lds.reset();
					lds.packUUID( &STATE(AvatarBase)->avatarUUID );
					lds.packInt32( DDBAVATARINFO_CARGO );
					lds.packInt32( 1 );
					lds.packBool( 1 ); // load
					lds.packUChar( code );
					this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
					lds.unlock();

					// update landmark status
					lds.reset();
					lds.packUChar( code );
					lds.packInt32( DDBLANDMARKINFO_COLLECTED );
					this->sendMessage( this->hostCon, MSG_DDB_LANDMARKSETINFO, lds.stream(), lds.length() );
					lds.unlock();
				}

				// notify initiator
				if ( this->collectionTask.front().thread == thread ) {
					Log.log( 0, "AvatarSimulation::parseAvatarOutput: collection attempt finished, %d", success );
					lds.reset();
					lds.packUUID( &thread );
					lds.packChar( success );
					this->sendMessage( this->hostCon, MSG_RESPONSE, lds.stream(), lds.length(), &this->collectionTask.front().initiator );
					lds.unlock();
					this->collectionTask.pop_front();
				} else {
					Log.log( 0, "AvatarSimulation::parseAvatarOutput: out of order collection!" );
					// what happened?
				}
			}
			break;
		default:
			Log.log( 0, "AvatarSimulation::parseAvatarOutput: unknown event %d!", evt );
			return 1;
		}
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Actions

int AvatarSimulation::queueAction( UUID *director, UUID *thread, int action, void *data, int len ) {
	float delay = AvatarSimulation_IMAGE_DELAY;

	// if we have a camera, slip in an image before every action (except image!)
	if ( action != AA_IMAGE && !this->cameraId.empty() )
		AvatarBase::queueAction( getUUID(), getUUID(), AA_IMAGE, &delay, sizeof(float) );

	return AvatarBase::queueAction( director, thread, action, data, len );
}

int AvatarSimulation::nextAction() {
	ActionInfo *pAI;

	AvatarBase::nextAction(); // prepare for next action

	if ( !STATE(AvatarBase)->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::nextAction: AA_WAIT start %.3f", *pAD );
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarSimulation_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;

			STATE(AvatarSimulation)->moveStartX = STATE(AvatarSimulation)->lastUpdatePosX;
			STATE(AvatarSimulation)->moveStartY = STATE(AvatarSimulation)->lastUpdatePosY;
			STATE(AvatarSimulation)->moveStartR = STATE(AvatarSimulation)->lastUpdatePosR;

			float D = *pAD;
			float vel = STATE(AvatarSimulation)->lastUpdateVelLin;
			float velMax = STATE(AvatarSimulation)->maxVelocityEst;
			float acc = STATE(AvatarSimulation)->accelerationEst;
			float dec = STATE(AvatarSimulation)->accelerationEst;
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

			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::nextAction: AA_MOVE start %.3f est millis=%d", D, millis );

			STATE(AvatarSimulation)->moveDone = false;
			STATE(AvatarSimulation)->moveId++;

			this->ds.reset();
			this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
			this->ds.packFloat32( D );
			this->ds.packChar( STATE(AvatarSimulation)->moveId );
			this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_AVATAR_MOVE_LINEAR), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
			this->ds.unlock();

			apb->apb_ftime_s( &STATE(AvatarSimulation)->actionGiveupTime );
			STATE(AvatarSimulation)->actionGiveupTime.time += (STATE(AvatarSimulation)->actionGiveupTime.millitm + millis + AvatarSimulation_MOVE_FINISH_DELAY) / 1000;
			STATE(AvatarSimulation)->actionGiveupTime.millitm = (STATE(AvatarSimulation)->actionGiveupTime.millitm + millis + AvatarSimulation_MOVE_FINISH_DELAY) % 1000;
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSimulation_MOVECHECK_PERIOD, AvatarSimulation_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;

			STATE(AvatarSimulation)->moveStartX = STATE(AvatarSimulation)->lastUpdatePosX;
			STATE(AvatarSimulation)->moveStartY = STATE(AvatarSimulation)->lastUpdatePosY;
			STATE(AvatarSimulation)->moveStartR = STATE(AvatarSimulation)->lastUpdatePosR;

			float D = *pAD;
			float vel = STATE(AvatarSimulation)->lastUpdateVelAng;
			float velMax = 2*STATE(AvatarSimulation)->maxVelocityEst/STATE(AvatarSimulation)->wheelBaseEst;
			float acc = 2*STATE(AvatarSimulation)->accelerationEst/STATE(AvatarSimulation)->wheelBaseEst;
			float dec = 2*STATE(AvatarSimulation)->accelerationEst/STATE(AvatarSimulation)->wheelBaseEst;
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

			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::nextAction: AA_ROTATE start %.3f est millis=%d", *pAD, millis );
		
			STATE(AvatarSimulation)->moveDone = false;
			STATE(AvatarSimulation)->moveId++;

			this->ds.reset();
			this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
			this->ds.packFloat32( D );
			this->ds.packChar( STATE(AvatarSimulation)->moveId );
			this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_AVATAR_MOVE_ANGULAR), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
			this->ds.unlock();

			apb->apb_ftime_s( &STATE(AvatarSimulation)->actionGiveupTime );
			STATE(AvatarSimulation)->actionGiveupTime.time += (STATE(AvatarSimulation)->actionGiveupTime.millitm + millis + AvatarSimulation_MOVE_FINISH_DELAY) / 1000;
			STATE(AvatarSimulation)->actionGiveupTime.millitm = (STATE(AvatarSimulation)->actionGiveupTime.millitm + millis + AvatarSimulation_MOVE_FINISH_DELAY) % 1000;
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSimulation_MOVECHECK_PERIOD, AvatarSimulation_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_IMAGE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned int millis = (unsigned int)((*pAD) * 1000);

			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::nextAction: AA_IMAGE, requesting image (delay %d ms)", millis );

			STATE(AvatarSimulation)->actionProgressStatus = 0; // delaying

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarSimulation_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	default:
		Log.log( 0, "AvatarSimulation::nextAction: unknown action type %d", pAI->action );
	}

	return 0;
}

int AvatarSimulation::cameraCommandFinished() {

	Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cameraCommandFinished: AA_IMAGE complete" );

	// prepare next action
	this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
	STATE(AvatarBase)->actionTimeout = nilUUID;
	this->nextAction();

	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AvatarSimulation::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	if ( !AvatarBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case AvatarSimulation_MSGS::MSG_COLLECT_LANDMARK:
		{
			unsigned char code;
			float x, y;
			UUID initiator;
			UUID thread;
			lds.setData( data, len );
			code = lds.unpackUChar();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			lds.unpackUUID( &initiator );
			lds.unpackUUID( &thread );
			lds.unlock();

			// ask the simulation to collect
			lds.reset();
			lds.packUUID( &STATE(AvatarBase)->avatarUUID );
			lds.packUChar( code );
			lds.packFloat32( x );
			lds.packFloat32( y );
			lds.packUUID( &thread );
			this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_AVATAR_COLLECT_LANDMARK), lds.stream(), lds.length(), &STATE(AvatarSimulation)->execSimulationId );
			lds.unlock();

			// record collection
			COLLECTION_TASK_INFO ct;
			ct.initiator = initiator;
			ct.thread = thread;
			this->collectionTask.push_back( ct );
		}
		break;

	case AvatarSimulation_MSGS::MSG_DEPOSIT_LANDMARK:
		{

			unsigned char code;
			lds.setData( data, len );
			code = lds.unpackUChar();
			lds.unlock();
			Log.log(LOG_LEVEL_VERBOSE, "AvatarSimulation::conProcessMessage: MSG_DEPOSIT_LANDMARK received concerning landmark %d", code );
			// update avatar capacity
			lds.reset();
			lds.packUUID( &STATE(AvatarBase)->avatarUUID );
			lds.packInt32( DDBAVATARINFO_CARGO );
			lds.packInt32( 1 );
			lds.packBool( 0 ); // unload
			lds.packUChar( code );
			this->sendMessage( this->hostCon, MSG_DDB_AVATARSETINFO, lds.stream(), lds.length() );
			lds.unlock();

			// ask the simulation to deposit
			lds.reset();
			lds.packUUID(&STATE(AvatarBase)->avatarUUID);
			lds.packUChar(code);
			this->sendMessageEx(this->hostCon, MSGEX(ExecutiveSimulation_MSGS, MSG_AVATAR_DEPOSIT_LANDMARK), lds.stream(), lds.length(), &STATE(AvatarSimulation)->execSimulationId);
			lds.unlock();
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool AvatarSimulation::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;
	_timeb tb;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_WAIT finished" );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();
		return 0;
	case AA_MOVE:
		if ( STATE(AvatarSimulation)->moveDone ) {
			float dX = STATE(AvatarSimulation)->lastUpdatePosX - STATE(AvatarSimulation)->moveStartX;
			float dY = STATE(AvatarSimulation)->lastUpdatePosY - STATE(AvatarSimulation)->moveStartY;
			float dD = sqrt(dX*dX + dY*dY);
			
			apb->apb_ftime_s( &tb );
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_MOVE finished (dD=%f, tb=%d.%d)", dD, tb.time, (int)tb.millitm );
			STATE(AvatarBase)->actionTimeout = nilUUID;
			this->nextAction();
		} else {
			apb->apb_ftime_s( &tb );
			if ( tb.time < STATE(AvatarSimulation)->actionGiveupTime.time
			  || tb.time == STATE(AvatarSimulation)->actionGiveupTime.time && tb.millitm < STATE(AvatarSimulation)->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_MOVE failed!" );
			this->abortActions();
		}
		return 0;
	case AA_ROTATE:	
		if ( STATE(AvatarSimulation)->moveDone ) {
			float dR = STATE(AvatarSimulation)->lastUpdatePosR - STATE(AvatarSimulation)->moveStartR;

			apb->apb_ftime_s( &tb );
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_ROTATE finished (dR=%f, tb=%d.%d)", dR, tb.time, (int)tb.millitm );
			STATE(AvatarBase)->actionTimeout = nilUUID;
			this->nextAction();
		} else {
			apb->apb_ftime_s( &tb );
			if ( tb.time < STATE(AvatarSimulation)->actionGiveupTime.time
			  || tb.time == STATE(AvatarSimulation)->actionGiveupTime.time && tb.millitm < STATE(AvatarSimulation)->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_ROTATE failed!" );
			this->abortActions();
		}
		return 0;
	case AA_IMAGE:
		if ( STATE(AvatarSimulation)->actionProgressStatus == 0 ) { // request the image
			STATE(AvatarSimulation)->actionProgressStatus = 1;

			// request image
			mapSensorId::iterator iC;
			for ( iC = this->cameraId.begin(); iC != this->cameraId.end(); iC++ ) {
				this->ds.reset();
				this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
				this->ds.packInt32( iC->first );
				this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_AVATAR_IMAGE), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
				this->ds.unlock();

				STATE(AvatarSimulation)->requestedImages++;
			}

			STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSimulation_IMAGECHECKPERIOD, AvatarSimulation_CBR_cbActionStep );
		} else {
			STATE(AvatarSimulation)->requestedImages = 0;
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSimulation::cbActionStep: AA_IMAGE timed out!" );
			this->abortActions();
		}
		return 0;
	default:
		Log.log( 0, "AvatarSimulation::cbActionStep: unhandled action type %d", pAI->action );
	}

	return 0;
}

bool AvatarSimulation::cbSimWake( void *na ) {
	UUID thread;

	if ( !STATE(AvatarSimulation)->simRegistrationConfirmed ) { // repeat until we have confirmed registration
		// register sim avatar
		this->ds.reset();
		this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
		this->ds.packUUID( this->getUUID() );
		this->ds.packFloat32( STATE(AvatarBase)->posX );
		this->ds.packFloat32( STATE(AvatarBase)->posY );
		this->ds.packFloat32( STATE(AvatarBase)->posR );
		this->ds.packString( STATE(AvatarSimulation)->AVATAR_FILE );
		this->ds.packChar( (char)STATE(AvatarSimulation)->LANDMARK_CODE_OFFSET );
		this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_ADDAVATAR), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
		this->ds.unlock();
	}

	// request avatar output
	thread = this->conversationInitiate( AvatarSimulation_CBR_convRequestAvatarOutput, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
	this->ds.packUUID( &thread );
	this->sendMessageEx( this->hostCon, MSGEX(ExecutiveSimulation_MSGS,MSG_RAVATAR_OUTPUT), this->ds.stream(), this->ds.length(), &STATE(AvatarSimulation)->execSimulationId );
	this->ds.unlock();

	return 1;
}

bool AvatarSimulation::convRequestExecutiveSimulationId( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	UUID thread;
	char response;

	if ( conv->response == NULL ) { // request timed out
		Log.log( 0, "AvatarSimulation::convRequestExecutiveSimulationId: request timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackUUID( &thread ); // thread
	
	response = this->ds.unpackChar();

	if ( response == 1 ) { // spawned
		this->ds.unpackUUID( &STATE(AvatarSimulation)->execSimulationId );
		this->ds.unlock();
		
		// initialize sim avatar
		this->simConfigure();		

		this->backup(); // execSimulationId

	} else if ( response == 0 ) { // not spawned
		this->ds.unlock();

		// ask again in a few seconds
		this->addTimeout( 2000, AvatarSimulation_CBR_cbRequestExecutiveSimulationId );
	} else { // unknown
		this->ds.unlock();

		// no Simulation Executive? nothing to do
	}

	return 0;
}

bool AvatarSimulation::cbRequestExecutiveSimulationId( void *NA ) {
	// find the Simulation Executive so we can register
	UUID simExecType;
	UuidFromString( (RPC_WSTR)_T(ExecutiveSimulation_UUID), &simExecType );
	UUID thread = this->conversationInitiate( AvatarSimulation_CBR_convRequestExecutiveSimulationId, DDB_REQUEST_TIMEOUT );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( &simExecType );
	this->ds.packChar( -1 );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_RUNIQUEID, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

bool AvatarSimulation::convRequestAvatarOutput( void *vpConv ) {
	DataStream ds;
	spConversation conv = (spConversation)vpConv;
	UUID thread;
	char response;

	if ( conv->response == NULL ) { // request timed out
		Log.log( 0, "AvatarSimulation::convRequestAvatarOutput: request timed out" );
		return 0; // end conversation
	}

	ds.setData( conv->response, conv->responseLen );
	ds.unpackUUID( &thread ); // thread
	
	response = ds.unpackChar();
	this->ds.unlock();

	if ( response == 0 ) { // success
		STATE(AvatarSimulation)->simRegistrationConfirmed = true;

		this->parseAvatarOutput( &ds );
	} else { // failure
		// avatar doesn't exist? nothing to do
	}

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AvatarSimulation::freeze( UUID *ticket ) {

	return AvatarBase::freeze( ticket );
}

int AvatarSimulation::thaw( DataStream *ds, bool resumeReady ) {
	int ret = AvatarBase::thaw( ds, resumeReady );

	if ( STATE(AvatarSimulation)->simPoseInitialized ) {
		this->setAvatarState( true ); // we're ready
	}

	return ret;
}

int	AvatarSimulation::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AvatarSimulation);

	// pack id maps
	_WRITE_STATE_MAP( int, UUID, &this->sonarId );
	_WRITE_STATE_MAP( int, UUID, &this->cameraId );

	_WRITE_STATE_LIST( COLLECTION_TASK_INFO, &this->collectionTask );

	return AvatarBase::writeState( ds, false );;
}

int	AvatarSimulation::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AvatarSimulation);

	// unpack id maps
	_READ_STATE_MAP( int, UUID, &this->sonarId );
	_READ_STATE_MAP( int, UUID, &this->cameraId );

	_READ_STATE_LIST( COLLECTION_TASK_INFO, &this->collectionTask );

	return AvatarBase::readState( ds, false );
}


int AvatarSimulation::recoveryFinish() {
	DataStream lds;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
		if ( STATE(AvatarSimulation)->execSimulationId == nilUUID ) { // don't know the simulation executive yet
			UUID simExecType;
			UuidFromString( (RPC_WSTR)_T(ExecutiveSimulation_UUID), &simExecType );
			UUID thread = this->conversationInitiate( AvatarSimulation_CBR_convRequestExecutiveSimulationId, DDB_REQUEST_TIMEOUT );
			if ( thread == nilUUID ) {
				return 1;
			}
			lds.reset();
			lds.packUUID( &simExecType );
			lds.packChar( -1 );
			lds.packUUID( &thread );
			this->sendMessage( this->hostCon, MSG_RUNIQUEID, lds.stream(), lds.length() );
			lds.unlock();
		}

		if ( STATE(AvatarSimulation)->simConfigured ) {
			// add SimWake timeout
			STATE(AvatarSimulation)->simWakeTimeout = this->addTimeout( AvatarSimulation_SIMWAKE_PERIOD, AvatarSimulation_CBR_cbSimWake );
		}
	}

	if ( AvatarBase::recoveryFinish() ) 
		return 1;

	return 0;
}

int AvatarSimulation::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(AvatarSimulation)->execSimulationId );

	ds->packString( STATE(AvatarSimulation)->TYPE );
	ds->packString( STATE(AvatarSimulation)->AVATAR_FILE );
	ds->packInt32( STATE(AvatarSimulation)->LANDMARK_CODE_OFFSET );

	ds->packFloat32( STATE(AvatarSimulation)->wheelBaseEst );
	ds->packFloat32( STATE(AvatarSimulation)->accelerationEst );
	ds->packFloat32( STATE(AvatarSimulation)->maxVelocityEst );

	ds->packInt32( STATE(AvatarSimulation)->capacity );
	
	_WRITE_STATE_MAP( int, UUID, &this->sonarId );
	_WRITE_STATE_MAP( int, UUID, &this->cameraId );
	
	ds->packBool( STATE(AvatarSimulation)->simConfigured );
	ds->packBool( STATE(AvatarSimulation)->simRegistrationConfirmed );

	return AvatarBase::writeBackup( ds );
}

int AvatarSimulation::readBackup( DataStream *ds ) {
	DataStream lds;

	ds->unpackUUID( &STATE(AvatarSimulation)->execSimulationId );

	strcpy_s( STATE(AvatarSimulation)->TYPE, sizeof(STATE(AvatarSimulation)->TYPE), ds->unpackString() );
	strcpy_s( STATE(AvatarSimulation)->AVATAR_FILE, sizeof(STATE(AvatarSimulation)->AVATAR_FILE), ds->unpackString() );
	STATE(AvatarSimulation)->LANDMARK_CODE_OFFSET = ds->unpackInt32();

	STATE(AvatarSimulation)->wheelBaseEst = ds->unpackFloat32();
	STATE(AvatarSimulation)->accelerationEst = ds->unpackFloat32();
	STATE(AvatarSimulation)->maxVelocityEst = ds->unpackFloat32();

	STATE(AvatarSimulation)->capacity = ds->unpackInt32();
	
	_READ_STATE_MAP( int, UUID, &this->sonarId );
	_READ_STATE_MAP( int, UUID, &this->cameraId );
	
	STATE(AvatarSimulation)->simConfigured = ds->unpackBool();
	STATE(AvatarSimulation)->simRegistrationConfirmed = ds->unpackBool();
	
	return AvatarBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarSimulation *agent = (AvatarSimulation *)vpAgent;

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
	AvatarSimulation *agent = new AvatarSimulation( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarSimulation *agent = new AvatarSimulation( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CAvatarSimulationDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAvatarSimulationDLL, CWinApp)
END_MESSAGE_MAP()

CAvatarSimulationDLL::CAvatarSimulationDLL() {
	GetCurrentDirectory( 512, DLL_Directory );
}

// The one and only CAvatarSimulationDLL object
CAvatarSimulationDLL theApp;

int CAvatarSimulationDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAvatarSimulationDLL ---\n"));
  return CWinApp::ExitInstance();
}