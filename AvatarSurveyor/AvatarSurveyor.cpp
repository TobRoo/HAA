// AvatarSurveyor.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\Autonomic\\autonomic.h"
#include "..\\AvatarBase\\AvatarBase.h"

#include "..\\Autonomic\\DDB.h"

#include "AvatarSurveyor.h"
#include "AvatarSurveyorVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

WCHAR DLL_Directory[512];

//*****************************************************************************
// AvatarSurveyor

//-----------------------------------------------------------------------------
// Constructor	
AvatarSurveyor::AvatarSurveyor( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) : AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarSurveyor" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarSurveyor_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	this->avatarCon = NULL;

	this->imageData = NULL;
	this->imageSize = 0;
	this->readingImage = false;

	this->_cameraCommandFinishedImgCount = 0;

	
	// Prepare callbacks
	this->callback[AvatarSurveyor_CBR_cbWatchAvatarConnection] = NEW_MEMBER_CB(AvatarSurveyor,cbWatchAvatarConnection);
	this->callback[AvatarSurveyor_CBR_cbActionStep] = NEW_MEMBER_CB(AvatarSurveyor,cbActionStep);
	this->callback[AvatarSurveyor_CBR_cbDebug] = NEW_MEMBER_CB(AvatarSurveyor,cbDebug);

}

//-----------------------------------------------------------------------------
// Destructor
AvatarSurveyor::~AvatarSurveyor() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	if ( this->imageData )
		free( this->imageData );

}

//-----------------------------------------------------------------------------
// Configure

int AvatarSurveyor::configure() {
	
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
		sprintf_s( logName, "%s\\AvatarSurveyor %s %ls.txt", logDirectory, timeBuf, rpc_wstr );
		
		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AvatarSurveyor %.2d.%.2d.%.5d.%.2d", AvatarSurveyor_MAJOR, AvatarSurveyor_MINOR, AvatarSurveyor_BUILDNO, AvatarSurveyor_EXTEND );
	}

	if ( AvatarBase::configure() ) 
		return 1;

	return 0;
}

int AvatarSurveyor::setInstance( char instance ) {
	char paraN[512];
	FILE *paraF;
	UUID *uuid;

	// open parameter file
	sprintf_s( paraN, sizeof(paraN), "%ws\\instance%d.ini", DLL_Directory, instance );
	if ( fopen_s( &paraF, paraN, "r" ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: failed to open %s", paraN );
		return 1;
	}

	if ( 1 != fscanf_s( paraF, "TYPE=%s\n", TYPE, sizeof(TYPE) ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected TYPE=<string>, check format" );
		goto returnerror;
	}	
	if ( 1 != fscanf_s( paraF, "ADDRESS=%s\n", ADDRESS, sizeof(ADDRESS) ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected ADDRESS=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "PORT=%s\n", PORT, sizeof(PORT) ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected PORT=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "INNER_RADIUS=%f\n", &INNER_RADIUS ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected INNER_RADIUS=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "OUTER_RADIUS=%f\n", &OUTER_RADIUS ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected OUTER_RADIUS=<float>, check format" );
		goto returnerror;
	}

	if ( 1 != fscanf_s( paraF, "FORWARD_VELOCITY=%f\n", &FORWARD_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected FORWARD_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "FORWARD_COAST=%f\n", &FORWARD_COAST ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected FORWARD_COAST=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "BACKWARD_VELOCITY=%f\n", &BACKWARD_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected BACKWARD_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "BACKWARD_COAST=%f\n", &BACKWARD_COAST ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected BACKWARD_COAST=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "LEFT_FORWARD_PWM=%d\n", &LEFT_FORWARD_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected LEFT_FORWARD_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "RIGHT_FORWARD_PWM=%d\n", &RIGHT_FORWARD_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected RIGHT_FORWARD_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "LEFT_BACKWARD_PWM=%d\n", &LEFT_BACKWARD_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected LEFT_BACKWARD_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "RIGHT_BACKWARD_PWM=%d\n", &RIGHT_BACKWARD_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected RIGHT_BACKWARD_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CW_VELOCITY=%f\n", &CW_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CW_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CW_DRAG=%f\n", &CW_DRAG ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CW_DRAG=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CW_SMALL_ANGLE=%f\n", &CW_SMALL_ANGLE ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CW_SMALL_ANGLE=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CW_SMALL_ANGLE_VELOCITY=%f\n", &CW_SMALL_ANGLE_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CW_SMALL_ANGLE_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CCW_VELOCITY=%f\n", &CCW_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CCW_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CCW_DRAG=%f\n", &CCW_DRAG ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CCW_DRAG=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CCW_SMALL_ANGLE=%f\n", &CCW_SMALL_ANGLE ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CCW_SMALL_ANGLE=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CCW_SMALL_ANGLE_VELOCITY=%f\n", &CCW_SMALL_ANGLE_VELOCITY ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected CCW_SMALL_ANGLE_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "LEFT_CW_PWM=%d\n", &LEFT_CW_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected LEFT_CW_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "RIGHT_CW_PWM=%d\n", &RIGHT_CW_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected RIGHT_CW_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "LEFT_CCW_PWM=%d\n", &LEFT_CCW_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected LEFT_CCW_PWM=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "RIGHT_CCW_PWM=%d\n", &RIGHT_CCW_PWM ) ) {
		Log.log( 0, "AvatarSurveyor::setInstance: expected RIGHT_CCW_PWM=<int>, check format" );
		goto returnerror;
	}
	uuid = this->parseSensorCamera( paraF );
	if ( uuid == NULL )
		goto returnerror;
	this->cameraId = *uuid;

	fclose( paraF );
	
	this->registerAvatar( TYPE, INNER_RADIUS, OUTER_RADIUS, 0, DDB_SENSOR_CAMERA );

	// set our maximum movements
	//STATE(AvatarBase)->maxLinear = 2.5f * min( FORWARD_VELOCITY, BACKWARD_VELOCITY );
	//STATE(AvatarBase)->maxRotation = 2.5f * min( CW_VELOCITY, CCW_VELOCITY );
	// set them lower than necessary so we take more pictures along the way
	STATE(AvatarBase)->maxLinear = 0.2f;
	STATE(AvatarBase)->maxRotation = 20.0f/180 * fM_PI;
	STATE(AvatarBase)->minLinear = max( FORWARD_VELOCITY*10/1000.0f + FORWARD_COAST, BACKWARD_VELOCITY*10/1000.0f + BACKWARD_COAST ); // limited by minimum time we can command
	STATE(AvatarBase)->minRotation = max( CW_SMALL_ANGLE_VELOCITY*10/1000.0f, CCW_SMALL_ANGLE_VELOCITY*10/1000.0f );

	return AvatarBase::setInstance( instance );

returnerror:
	fclose( paraF );
	return 1;
}

//-----------------------------------------------------------------------------
// Start

int AvatarSurveyor::start( char *missionFile ) {
	
	if ( STATE(AgentBase)->agentType.instance == -1 ) {
		Log.log( 0, "AvatarSurveyor::start: instance must be set before we start" );
		return 1;
	}

	if ( AvatarBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// initialize camera reading
	sprintf_s( this->cameraReading.format, IMAGEFORMAT_SIZE, "jpg" );
	this->cameraReading.w = 320;
	this->cameraReading.h = 240;

	// initiate connection to surveyor
	sprintf_s( this->avatarAP.address, sizeof(this->avatarAP.address), ADDRESS );
	sprintf_s( this->avatarAP.port, sizeof(this->avatarAP.port), PORT );

	this->avatarCon = this->openConnection( &this->avatarAP, NULL, -1 );
	if ( !this->avatarCon ) {
		Log.log( 0, "AvatarSurveyor::start: failed to open connection to avatar" );
		return 1;
	}

	this->avatarCon->raw = true; // switch to raw mode
	this->watchConnection( this->avatarCon, AvatarSurveyor_CBR_cbWatchAvatarConnection );

	STATE(AgentBase)->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarSurveyor::stop() {

	if ( this->avatarCon )
		this->closeConnection( this->avatarCon );

	return AvatarBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarSurveyor::step() {

	// TEMP
	static int takePhotos = 0;
	if ( takePhotos && this->avatarReady ) {
		takePhotos = 0;

		//this->addTimeout( AvatarSurveyor_ACTIONPROGRESSPERIOD, NEW_MEMBER_CB( AvatarSurveyor, cbDebug ) );
		
		//this->sendRaw( this->avatarCon, "b", 1 ); // set capture size
		//this->sendRaw( this->avatarCon, "q1", 2 ); // set jpeg quality

		//float delay = 5;
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		//this->queueAction( this->getUUID(), this->getUUID(), AA_IMAGE, &delay, sizeof(float) );
		
		float val;
		int i;
		val = 1.0f;

		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );

		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
/*		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		val = 0.3048f / 2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = fM_PI_4;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f / 2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = fM_PI_4;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f / 2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = -fM_PI_4;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f / 2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = -fM_PI_4/2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f / 2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = 1;
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
	/*	val = -fM_PI_2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = fM_PI_2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 0.3048f;
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
		val = -fM_PI_2;
		this->queueAction( this->getUUID(), this->getUUID(), AA_ROTATE, &val, sizeof(float) );
		val = 1;
		this->queueAction( this->getUUID(), this->getUUID(), AA_WAIT, &val, sizeof(float) );
	*/
	//	val = -STATE(AvatarBase)->maxLinear;
	//	this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
	//	val = -STATE(AvatarBase)->maxLinear;
	//	this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
	//	val = -STATE(AvatarBase)->maxLinear;
	//	this->queueAction( this->getUUID(), this->getUUID(), AA_MOVE, &val, sizeof(float) );
	}


	return AvatarBase::step();
}

int AvatarSurveyor::setAvatarState( bool ready ) {

	// TODO stuff

	return AvatarBase::setAvatarState( ready );
}

//-----------------------------------------------------------------------------
// Position/Rotation State

int AvatarSurveyor::updatePos( int action, unsigned short millis, _timeb *tb, bool finished_move ) {
	float d, dr;

	if ( action == AA_MOVE ) {
		dr = 0;
		if ( this->motionDirection == MOTION_FORWARD ) {
			d = FORWARD_VELOCITY*millis/1000.0f;
			if ( finished_move ) d += FORWARD_COAST; // add a bit extra because we coast at the end
		} else {
			d = -BACKWARD_VELOCITY*millis/1000.0f;
			if ( finished_move ) d -= BACKWARD_COAST; // add a bit extra because we coast at the end
		}
	} else if ( action == AA_ROTATE ) {
		if ( this->motionDirection == MOTION_CW ) {
			if ( this->motionSmallAngle )
				dr = -(CW_SMALL_ANGLE_VELOCITY*millis/1000.0f);
			else
				dr = -(CW_VELOCITY*millis/1000.0f + CW_DRAG*millis/(float)this->motionMillis);
		} else {
			if ( this->motionSmallAngle )
				dr = CCW_SMALL_ANGLE_VELOCITY*millis/1000.0f;
			else
				dr = CCW_VELOCITY*millis/1000.0f + CCW_DRAG*millis/(float)this->motionMillis;
		}
		d = 0;
	} else {
		Log.log( 0, "AvatarSurveyor::cbActionStep: unhandled action %d", action );
		return 1;
	}

	AvatarBase::updatePos( d, 0, dr, tb );

	return 0;
}

//-----------------------------------------------------------------------------
// Actions

int AvatarSurveyor::queueAction( UUID *director, UUID *thread, int action, void *data, int len ) {
	float delay = AvatarSurveyor_IMAGE_DELAY;

	// slip in an image before every action (except image!)
	if ( action != AA_IMAGE )
		AvatarBase::queueAction( getUUID(), getUUID(), AA_IMAGE, &delay, sizeof(float) );

	return AvatarBase::queueAction( director, thread, action, data, len );
}

int AvatarSurveyor::nextAction() {
	ActionInfo *pAI;

	AvatarBase::nextAction(); // prepare for next action

	if ( !STATE(AvatarBase)->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_WAIT start %.3f", *pAD );
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarSurveyor_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;
			char buf[4];

			if ( *pAD > 0 ) {
				this->motionDirection = MOTION_FORWARD;
				millis = (unsigned)(((*pAD-FORWARD_COAST)/FORWARD_VELOCITY)*1000);
				millis = ((millis+5)/10)*10; // round to tenths of a second
				buf[0] = 'M';
				buf[1] = (char)(LEFT_FORWARD_PWM);
				buf[2] = (char)(RIGHT_FORWARD_PWM);
				buf[3] = (unsigned char)(millis/10);
			} else {
				this->motionDirection = MOTION_BACKWARD;
				millis = (unsigned)(((-*pAD-BACKWARD_COAST)/BACKWARD_VELOCITY)*1000);
				millis = ((millis+5)/10)*10; // round to tenths of a second
				buf[0] = 'M';
				buf[1] = (char)(LEFT_BACKWARD_PWM);
				buf[2] = (char)(RIGHT_BACKWARD_PWM);
				buf[3] = (unsigned char)(millis/10);
			}
			this->motionMillis = millis;

			if ( millis/10 > 0xFF ) {
				Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_MOVE requested distance too great!" );
				return 1;
			} else if ( millis/10 == 0 ) {
				Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_MOVE requested distance too short!" );
				return 1;
			}
			
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_MOVE start %.3f millis=%d", *pAD, millis );
						
			this->sendRaw( this->avatarCon, buf, 4 );

			this->actionProgressStatus = 1;
			apb->apb_ftime_s( &this->actionProgressLast );
			this->actionProgressEnd.time = (this->actionProgressLast.time + millis/1000) + (this->actionProgressLast.millitm + (millis%1000))/1000;
			this->actionProgressEnd.millitm = (this->actionProgressLast.millitm + (millis%1000)) % 1000;

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSurveyor_ACTIONPROGRESSPERIOD, AvatarSurveyor_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;
			char buf[4];

			if ( *pAD > 0 ) {
				this->motionDirection = MOTION_CCW;
				if ( *pAD < CCW_SMALL_ANGLE ) {
					millis = (unsigned)((*pAD/CCW_SMALL_ANGLE_VELOCITY)*1000);
					this->motionSmallAngle = true;
				} else {
					millis = (unsigned)(((*pAD-CCW_DRAG)/CCW_VELOCITY)*1000);
					this->motionSmallAngle = false;
				}
				millis = ((millis+5)/10)*10; // round to tenths of a second
				buf[0] = 'M';
				buf[1] = (char)(LEFT_CCW_PWM);
				buf[2] = (char)(RIGHT_CCW_PWM);
				buf[3] = (unsigned char)(millis/10);
			} else {
				this->motionDirection = MOTION_CW;
				if ( -*pAD < CW_SMALL_ANGLE ) {
					millis = (unsigned)((-*pAD/CW_SMALL_ANGLE_VELOCITY)*1000);
					this->motionSmallAngle = true;
				} else {
					millis = (unsigned)(((-*pAD-CW_DRAG)/CW_VELOCITY)*1000);
					this->motionSmallAngle = false;
				}
				millis = ((millis+5)/10)*10; // round to tenths of a second
				buf[0] = 'M';
				buf[1] = (char)(LEFT_CW_PWM);
				buf[2] = (char)(RIGHT_CW_PWM);
				buf[3] = (unsigned char)(millis/10);
			}
			this->motionMillis = millis;
			
			if ( millis/10 > 0xFF ) {
				Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_ROTATE requested distance too great!" );
				return 1;
			} else if ( millis/10 == 0 ) {
				Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_ROTATE requested distance too short!" );
				return 1;
			}
			
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_ROTATE start %.3f millis=%d", *pAD, millis );
						
			this->sendRaw( this->avatarCon, buf, 4 );

			this->actionProgressStatus = 1;
			apb->apb_ftime_s( &this->actionProgressLast );
			this->actionProgressEnd.time = (this->actionProgressLast.time + millis/1000) + (this->actionProgressLast.millitm + (millis%1000))/1000;
			this->actionProgressEnd.millitm = (this->actionProgressLast.millitm + (millis%1000)) % 1000;

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSurveyor_ACTIONPROGRESSPERIOD, AvatarSurveyor_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_IMAGE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned int millis = (unsigned int)((*pAD) * 1000);

			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::nextAction: AA_IMAGE, requesting image (delay %d ms)", millis );

			this->actionProgressStatus = 0; // delaying

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarSurveyor_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	default:
		Log.log( 0, "AvatarSurveyor::nextAction: unknown action type %d", pAI->action );
	}

	return 0;
}

bool AvatarSurveyor::motorCommandUpdate( int action, _timeb *tb, bool finished ) {
	unsigned short millis;

	if ( finished
	  || tb->time > this->actionProgressEnd.time
	  || (tb->time == this->actionProgressEnd.time && tb->millitm >= this->actionProgressEnd.millitm) ) {
		// updatePos with remaining time
		millis = (unsigned short)((this->actionProgressEnd.time - this->actionProgressLast.time)*1000 + (this->actionProgressEnd.millitm - this->actionProgressLast.millitm));
		this->updatePos( action, millis, tb, true );
		this->actionProgressStatus = 0;
		if ( !finished ) {
			STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSurveyor_MOVE_FINISH_DELAY, AvatarSurveyor_CBR_cbActionStep );
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::motorCommandUpdate: action time is up, waiting for acknowledgement" );
		}
		return 0; 
	}

	// updatePos with elapsed time
	millis = (unsigned short)((tb->time - this->actionProgressLast.time)*1000 + (tb->millitm - this->actionProgressLast.millitm));
	this->updatePos( action, millis, tb );
	this->actionProgressLast.time = tb->time;
	this->actionProgressLast.millitm = tb->millitm;
	return 1; // repeat timeout
}

int AvatarSurveyor::motorCommandFinished() {
	ActionInfo *pAI;
	
	if ( !STATE(AvatarBase)->actionInProgress ) {	// no action to in progress!
		Log.log( 0, "AvatarSurveyor::motorCommandFinished: no action in progress!" );
		return 1;
	}

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_MOVE:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::motorCommandFinished: AA_MOVE complete" );
		if ( this->actionProgressStatus != 0 ) // we ended earlier than expected
			this->motorCommandUpdate( AA_MOVE, &this->avatarCon->recvTime, true );
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();		
		break;
	case AA_ROTATE:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::motorCommandFinished: AA_ROTATE complete" );
		if ( this->actionProgressStatus != 0 ) // we ended earlier than expected
			this->motorCommandUpdate( AA_ROTATE, &this->avatarCon->recvTime, true );
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();		
		break;
	default:
		Log.log( 0, "AvatarSurveyor::motorCommandFinished: unknown action type %d", pAI->action );
		return 1;
	}


	return 0;
}

int AvatarSurveyor::cameraCommandFinished() {

	Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cameraCommandFinished: AA_IMAGE complete" );
		
	this->ds.reset();
	this->ds.packUUID( &this->cameraId );
	this->ds.packData( &this->avatarCon->recvTime, sizeof(_timeb) );
	this->ds.packInt32( sizeof(CameraReading) );
	this->ds.packData( &this->cameraReading, sizeof(CameraReading) );
	this->ds.packInt32( this->imageSize );
	this->ds.packData( this->imageData, this->imageSize );
	this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// TEMP
	char filename[1024];
	sprintf_s( filename, 1024, "data\\dump\\surveyor%dimg%d.jpg", STATE(AgentBase)->agentType.instance, this->_cameraCommandFinishedImgCount++ );
	FILE *img = fopen( filename, "wb" );
	if ( img ) {
		fwrite( this->imageData, 1, this->imageSize, img );
		fclose( img );
	}

	// prepare next action
	this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
	STATE(AvatarBase)->actionTimeout = nilUUID;
	this->nextAction();

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool AvatarSurveyor::cbWatchAvatarConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	
	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { // connected 
			this->setAvatarState( true );
		} else { // not connected
			this->setAvatarState( false );
		}
	} else if ( evt == CON_EVT_CLOSED ) {
		this->setAvatarState( false );
		Log.log( 0, "AvatarSurveyor::cbWatchAvatarConnection: avatarCon closed, how did this happen?" );
	}

	return 0;
}


bool AvatarSurveyor::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;
	_timeb tb;
	apb->apb_ftime_s( &tb );

	Log.log( 0, "AvatarSurveyor::cbActionStep: DEBUG cbActionStep" );

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cbActionStep: AA_WAIT finished" );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();
		return 0;
	case AA_MOVE:
		if ( this->actionProgressStatus == 0 ) { // waiting to finish
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cbActionStep: AA_MOVE timed out!" );
			this->abortActions();
			return 0;
		} else { // action in progress
			return this->motorCommandUpdate( AA_MOVE, &tb );
		}
	case AA_ROTATE:
		if ( this->actionProgressStatus == 0 ) { // waiting to finish
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cbActionStep: AA_ROTATE timed out!" );
			this->abortActions();
			return 0;
		} else { // action in progress
			return this->motorCommandUpdate( AA_ROTATE, &tb );
		}
	case AA_IMAGE:
		if ( this->actionProgressStatus == 0 ) { // request the image
			this->actionProgressStatus = 1;

			
			this->sendRaw( this->avatarCon, "q1", 2 ); // set jpeg quality
			this->sendRaw( this->avatarCon, "I", 1 );

			STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarSurveyor_IMAGECHECKPERIOD, AvatarSurveyor_CBR_cbActionStep );

			// set give up
			apb->apb_ftime_s( &this->actionProgressLast );
			this->actionProgressEnd.time = (this->actionProgressLast.time + AvatarSurveyor_IMAGE_TIMEOUT/1000) + (this->actionProgressLast.millitm + (AvatarSurveyor_IMAGE_TIMEOUT%1000))/1000;
			this->actionProgressEnd.millitm = (this->actionProgressLast.millitm + (AvatarSurveyor_IMAGE_TIMEOUT%1000)) % 1000;
			return 0;
		}
		if ( tb.time > this->actionProgressEnd.time
		 || (tb.time == this->actionProgressEnd.time && tb.millitm >= this->actionProgressEnd.millitm) ) {
			Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cbActionStep: AA_IMAGE timed out!" );
			this->abortActions();
			return 0;
		}
		Log.log( LOG_LEVEL_VERBOSE, "AvatarSurveyor::cbActionStep: AA_IMAGE, requesting image" );
		this->sendRaw( this->avatarCon, "I", 1 );
		return 1; // repeat timeout
	default:
		Log.log( 0, "AvatarSurveyor::cbActionStep: unhandled action type %d", pAI->action );
	}

	return 0;
}

bool AvatarSurveyor::cbDebug( void *vpdata ) {
	Log.log( 0, "AvatarSurveyor::cbDebug: callback recieved" );
	
	return 1;
}

int AvatarSurveyor::conProcessRaw( spConnection con ) {
	
	if ( !con->bufLen ) {
		goto returnOK;
	}

	if ( this->readingImage ) {
		int read = min( con->bufLen, this->imageSize - this->imageRead );
		memcpy( this->imageData + this->imageRead, con->buf + con->bufStart, read );
		this->imageRead += read;
		con->bufStart += read;
		con->bufLen -= read;
		
		if ( this->imageRead == this->imageSize ) {
			this->cameraCommandFinished();
			this->readingImage = false;
			con->message = 0; // message handled
		}

		if ( !con->bufLen ) {
			goto returnOK;
		}
	}

	// TEMP
	//con->buf[con->bufStart+con->bufLen] = '\0';
	//printf( "--------------\n%d raw: %s\n---------------\n", con->message, con->buf+con->bufStart );

	if ( con->waitingForData > con->bufLen ) {
		goto returnOK;
	} else {
		con->waitingForData = 0;
	}		

	if ( con->message == (unsigned char)-1 ) { 
		con->message = con->buf[con->bufStart]; 
		con->bufStart++;
		con->bufLen--;
		switch ( con->message ) {
		case '#':
			con->waitingForData = 2; // we need at least 2 bytes to identify this message
			break;
		case '8': // move forward
		case '5': // robot stop
		case 'a': // set capture resolution to 160x128 
		case 'b': // set capture resolution to 320x256 
		case 'c': // set capture resolution to 640x512 
		case 'A': // set capture resolution to 1280x1024 
			Log.log( 0, "AvatarSurveyor::conProcessRaw: handled message %c (%d)", con->message, con->message );
			con->message = 0; // message handled
			break;
		case 'M': // motor command
			this->motorCommandFinished();
			con->message = 0; // message handled
			break;
		default:
			Log.log( 0, "AvatarSurveyor::conProcessRaw: unhandled message! %c (%d)", con->message, con->message );
			con->message = 0; // give up and try for the next message
		};
	} else if ( con->message == '#' ) {
		if (		!strncmp( "g0", con->buf + con->bufStart, 2 ) ) { // grab reference frame and enable frame differencing 
			con->bufStart += 2;
			con->bufLen -= 2;
			con->message = 0; // message handled
		} else if ( !strncmp( "g1", con->buf + con->bufStart, 2 ) ) { // enable color segmentation 
			con->bufStart += 2;
			con->bufLen -= 2;
			con->message = 0; // message handled
		} else if ( !strncmp( "IM", con->buf + con->bufStart, 2 ) ) { // grab JPEG compressed video frame  
			if ( con->bufLen < 8 ) {
				con->waitingForData = 8;
			} else {
				this->imageSize = (unsigned char)con->buf[con->bufStart+4]             // s0
								+ (unsigned char)con->buf[con->bufStart+5] * 256	   // + s1 * 256
								+ (unsigned char)con->buf[con->bufStart+6] * 65536	   // + s2 * 256^2
							    + (unsigned char)con->buf[con->bufStart+7] * 16777216; // + s3 * 256^3
				
				if ( !this->imageData ) { // hasn't been allocated yet
					this->imageData = (unsigned char *)malloc( this->imageSize*2 ); // *2 because images should be pretty small and this way we probably won't have to realloc
					if ( !this->imageData ) {
						Log.log( 0, "AvatarSurveyor::conProcessRaw: malloc failed (imageData)" );
						return 1;
					}
				} else if ( sizeof(this->imageData) < this->imageSize ) { // needs to be resized
					this->imageData = (unsigned char *)realloc( this->imageData, this->imageSize*2 ); // *2 because images should be pretty small and this way we probably won't have to realloc (again)
					if ( !this->imageData ) {
						Log.log( 0, "AvatarSurveyor::conProcessRaw: realloc failed (imageData)" );
						return 1;
					}
				}

				if ( con->bufLen - 8 < this->imageSize ) { // this will very likely always be the case
					memcpy( this->imageData, con->buf + con->bufStart + 8, con->bufLen - 8 );
					this->imageRead = con->bufLen - 8;
					this->readingImage = true;
					con->bufStart = 0;
					con->bufLen = 0;
				} else {
					memcpy( this->imageData, con->buf + con->bufStart + 8, this->imageSize );
					con->bufStart += this->imageSize + 8;
					con->bufLen -= this->imageSize + 8;
					this->cameraCommandFinished();
					this->readingImage = false;
					con->message = 0; // message handled
				}
			}
		} else if ( !strncmp( "ir", con->buf + con->bufStart, 2 ) ) { // I2C register read  (8 bit return)
			if ( con->bufLen < 4 ) {
				con->waitingForData = 4;
			} else {
				// TODO do something with the data
				con->bufStart += 4;
				con->bufLen -= 4;
				con->message = 0; // message handled
			}
		} else if ( !strncmp( "qu", con->buf + con->bufStart, 2 ) ) { // jpeg quality set
			con->message = 0; // message handled
		} else {
			Log.log( 0, "AvatarSurveyor::conProcessRaw: unhandled extended message!" );
			con->message = 0; // give up and try for the next message
		}
	} else { // looking for a # to begin a new message
		while ( con->bufLen && con->buf[con->bufStart] != '#' ) { // clean until the beginning of the next message
			con->bufStart++;
			con->bufLen--;
		}
		if ( con->bufLen ) {
			con->message = -1;
			con->bufStart++;
			con->bufLen--;
		}
	}
	
	AgentBase::conProcessRaw( con );
	return this->conProcessRaw( con );

	returnOK:
	AgentBase::conProcessRaw( con );
	return 0;
}

//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarSurveyor *agent = (AvatarSurveyor *)vpAgent;

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
	AvatarSurveyor *agent = new AvatarSurveyor( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarSurveyor *agent = new AvatarSurveyor( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CAvatarSurveyorDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAvatarSurveyorDLL, CWinApp)
END_MESSAGE_MAP()

CAvatarSurveyorDLL::CAvatarSurveyorDLL() {
	GetCurrentDirectory( 512, DLL_Directory );
}

// The one and only CAvatarSurveyorDLL object
CAvatarSurveyorDLL theApp;

int CAvatarSurveyorDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAvatarSurveyorDLL ---\n"));
  return CWinApp::ExitInstance();
}