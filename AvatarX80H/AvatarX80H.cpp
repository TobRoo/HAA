// AvatarX80H.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"
#include "windows.h"

#include "..\\Autonomic\\autonomic.h"
#include "..\\AvatarBase\\AvatarBase.h"

#include "..\\Autonomic\\DDB.h"

#include "AvatarX80H.h"
#include "AvatarX80HVersion.h"

#include "JpegDecode.h"

//#include "..\\include\\CxImage\\ximage.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AvatarX80H


#define DR_SHORT(ptr) ((unsigned char)*(ptr)+256*(unsigned char)*((ptr)+1))
#define DR_LSB(shrt) (unsigned char)(shrt & 0x00FF)
#define DR_MSB(shrt) (unsigned char)(shrt >> 8)
#define DR_SPLIT(ptr,shrt) *((unsigned char*)ptr) = (unsigned char)(shrt & 0x00FF); *(((unsigned char*)ptr+1)) = (unsigned char)(shrt >> 8);

BYTE checksumDrRobot( unsigned char *lpBuffer, int nSize ) {
	BYTE shift_reg, sr_lsb, data_bit, v;
	int i, j;
	BYTE fb_bit;

	shift_reg = 0; // initialize the shift register

	for ( i=0; i<nSize; i++ ) {
		v = (BYTE)(lpBuffer[i] & 0x0000FFFF);
		for ( j=0; j<8; j++ ) { // for each bit
			data_bit = v & 0x01; // isolate least sig bit
			sr_lsb = shift_reg & 0x01;
			fb_bit = (data_bit ^ sr_lsb) & 0x01; // calculate the feedback bit
			shift_reg = shift_reg >> 1;
			if ( fb_bit == 1 )
				shift_reg = shift_reg ^ 0x8C;
			v = v >> 1;
		}
	}
	return shift_reg;
}

//-----------------------------------------------------------------------------
// Constructor	
AvatarX80H::AvatarX80H( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) : AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AvatarX80H, AvatarBase )
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarX80H" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarX80H_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AvatarX80H_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	this->posEncodersInitialized = false;
	this->posEL = this->posER = 0;
	this->actionMonitorEncoders = false;

	this->avatarPingWatcher = nilUUID;
	this->avatarMessageTimeout = nilUUID;
	this->avatarMessageTimeouts = 0;
	this->messageQueueBegin = 0;
	this->messageQueueEnd = 0;
	this->avatarLatencyTicket = -1;

	this->imageBuf = (unsigned char *)malloc( 1024 );
	this->imageBufSize = 1024;
	this->imageDecoder = new CJpegDecode();
	this->_cameraCommandFinishedImgCount = 0;

	
	this->sonarSkip = 0;

	// set particle filter sigmas
	STATE(AvatarBase)->pfInitSigma[0] = AvatarX80H_INIT_LINEAR_SIG;
	STATE(AvatarBase)->pfInitSigma[1] = AvatarX80H_INIT_LINEAR_SIG;  
	STATE(AvatarBase)->pfInitSigma[2] = AvatarX80H_INIT_ROTATIONAL_SIG;
	STATE(AvatarBase)->pfUpdateSigma[0] = AvatarX80H_FORWARDV_SIG_EST;
	STATE(AvatarBase)->pfUpdateSigma[1] = AvatarX80H_TANGENTIALV_SIG_EST;
	STATE(AvatarBase)->pfUpdateSigma[2] = AvatarX80H_ROTATIONALV_SIG_EST;

	// Prepare callbacks
	this->callback[AvatarX80H_CBR_cbWatchAvatarConnection] = NEW_MEMBER_CB(AvatarX80H,cbWatchAvatarConnection);
	this->callback[AvatarX80H_CBR_cbMessageTimeout] = NEW_MEMBER_CB(AvatarX80H,cbMessageTimeout);
	this->callback[AvatarX80H_CBR_cbWaitForPing] = NEW_MEMBER_CB(AvatarX80H,cbWaitForPing);
	this->callback[AvatarX80H_CBR_cbActionStep] = NEW_MEMBER_CB(AvatarX80H,cbActionStep);
	this->callback[AvatarX80H_CBR_cbRerequestImage] = NEW_MEMBER_CB(AvatarX80H,cbRerequestImage);

}

//-----------------------------------------------------------------------------
// Destructor
AvatarX80H::~AvatarX80H() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	free( this->imageBuf );
	delete this->imageDecoder;

	// free state

}

//-----------------------------------------------------------------------------
// Configure

int AvatarX80H::configure() {
		
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\AvatarX80H %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AvatarX80H %.2d.%.2d.%.5d.%.2d", AvatarX80H_MAJOR, AvatarX80H_MINOR, AvatarX80H_BUILDNO, AvatarX80H_EXTEND );
	}

	if ( AvatarBase::configure() ) 
		return 1;

	return 0;
}

int AvatarX80H::setInstance( char instance ) {
	char paraN[512];
	FILE *paraF;
	UUID *uuid;
	int i;

	// open parameter file
	sprintf_s( paraN, sizeof(paraN), "%ws\\instance%d.ini", DLL_Directory, instance );
	if ( fopen_s( &paraF, paraN, "r" ) ) {
		Log.log( 0, "AvatarX80H::setInstance: failed to open %s", paraN );
		return 1;
	}

	if ( 1 != fscanf_s( paraF, "TYPE=%s\n", TYPE, sizeof(TYPE) ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected TYPE=<string>, check format" );
		goto returnerror;
	}	
	if ( 1 != fscanf_s( paraF, "ADDRESS=%s\n", ADDRESS, sizeof(ADDRESS) ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected ADDRESS=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "PORT=%s\n", PORT, sizeof(PORT) ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected PORT=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "INNER_RADIUS=%f\n", &INNER_RADIUS ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected INNER_RADIUS=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "OUTER_RADIUS=%f\n", &OUTER_RADIUS ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected OUTER_RADIUS=<float>, check format" );
		goto returnerror;
	}

	if ( 1 != fscanf_s( paraF, "WHEEL_CIRCUMFERENCE=%f\n", &WHEEL_CIRCUMFERENCE ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected WHEEL_CIRCUMFERENCE=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "WHEEL_BASE=%f\n", &WHEEL_BASE ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected WHEEL_BASE=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "ENCODER_DIVS=%d\n", &ENCODER_DIVS ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected ENCODER_DIVS=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "COUNTS_PER_REV=%d\n", &COUNTS_PER_REV ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected COUNTS_PER_REV=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "ENCODER_THRESHOLD=%d\n", &ENCODER_THRESHOLD ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected ENCODER_THRESHOLD=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CALIBRATE_ROTATION=%f\n", &CALIBRATE_ROTATION ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected CALIBRATE_ROTATION=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "CALIBRATE_POSITION=%f\n", &CALIBRATE_POSITION ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected CALIBRATE_POSITION=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "MOVE_FINISH_DELAY=%d\n", &MOVE_FINISH_DELAY ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected MOVE_FINISH_DELAY=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "SAFE_VELOCITY=%f\n", &SAFE_VELOCITY ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected SAFE_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "SAFE_ROTATION=%f\n", &SAFE_ROTATION ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected SAFE_ROTATION=<float>, check format" );
		goto returnerror;
	}
	if ( 6 != fscanf_s( paraF, "SERVO_POSE=%hu %hu %hu %hu %hu %hu\n", &SERVO_POSE[0], &SERVO_POSE[1], &SERVO_POSE[2], &SERVO_POSE[3], &SERVO_POSE[4], &SERVO_POSE[5] ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected SERVO_POSE=<unsigned short> <unsigned short> <unsigned short> <unsigned short> <unsigned short> <unsigned short>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "PING_WAIT_PERIOD=%d\n", &PING_WAIT_PERIOD ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected PING_WAIT_PERIOD=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "MESSAGE_TIMEOUT=%d\n", &MESSAGE_TIMEOUT ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected MESSAGE_TIMEOUT=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "SKIP_SONAR_COUNT=%d\n", &SKIP_SONAR_COUNT ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected SKIP_SONAR_COUNT=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "NUM_SONAR=%d\n", &NUM_SONAR ) ) {
		Log.log( 0, "AvatarX80H::setInstance: expected NUM_SONAR=<int>, check format" );
		goto returnerror;
	}
	for ( i=0; i<NUM_SONAR; i++ ) {
		uuid = this->parseSensorSonar( paraF );
		if ( uuid == NULL )
			goto returnerror;
		this->sonarId[i] = *uuid;
	}
	uuid = this->parseSensorCamera( paraF );
	if ( uuid == NULL )
		goto returnerror;
	this->cameraId = *uuid;

	fclose( paraF );

	this->registerAvatar( TYPE, INNER_RADIUS, OUTER_RADIUS, 0, DDB_SENSOR_SONAR | DDB_SENSOR_CAMERA );

	STATE(AvatarBase)->maxLinear = 0.4f;
	STATE(AvatarBase)->maxRotation = 20.0f/180 * fM_PI;

	return AvatarBase::setInstance( instance );

returnerror:
	fclose( paraF );
	return 1;
}

//-----------------------------------------------------------------------------
// Start

int AvatarX80H::start( char *missionFile ) {
	
	if ( AvatarBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// initiate connection to X80H
	sprintf_s( this->avatarAP.address, ADDRESS );
	sprintf_s( this->avatarAP.port, PORT );

	this->avatarCon = this->openConnection( &this->avatarAP, NULL, -1, IPPROTO_UDP );
	if ( !this->avatarCon ) {
		Log.log( 0, "AvatarX80H::start: failed to open connection to avatar" );
		return 1;
	}

	this->avatarCon->raw = true; // switch to raw mode
	this->avatarConWatcher = this->watchConnection( this->avatarCon, AvatarX80H_CBR_cbWatchAvatarConnection );

	this->requestingImage = false;

	// TEMP
	this->sensorF = fopen( "sensors.txt", "w" );
	this->encoderF = fopen( "encoders.txt", "w" );

	STATE(AgentBase)->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarX80H::stop() {

	// TEMP
	fclose( this->sensorF );
	fclose( this->encoderF );

	// shut down some of the avatar functions to save power
	this->avatarQueueMessage( 30, "\0", 1 ); // disable DC motors and servos		
	this->avatarQueueMessage( 123, "\0", 1 ); // stop motor feedback
	this->avatarQueueMessage( 124, "\0", 1 ); // stop custom feedback
	this->avatarQueueMessage( 125, "\0", 1 ); // stop sensor feedback


	this->closeConnection( this->avatarCon );

	return AvatarBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarX80H::step() {
	
	// TEMP
	static int tempstuff = 0;
	if ( this->avatarReady && tempstuff ) {

		float val = 2.0f;
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );
		this->queueAction( this->getUUID(), 0, AA_IMAGE, &val, 4 );

		tempstuff = 0;
	}

	return AvatarBase::step();
}

int AvatarX80H::setAvatarState( bool ready ) {

	if ( ready ) {
		char buf[64];
		
		// Setup avatar
		this->avatarQueueMessage( 123, NULL, 0 ); // request motor feedback
		this->avatarQueueMessage( 125, NULL, 0 ); // request sensor feedback
		//this->avatarQueueMessage( 123, "\0", 1 ); // stop motor feedback
		this->avatarQueueMessage( 124, "\0", 1 ); // stop custom feedback
		//this->avatarQueueMessage( 125, "\0", 1 ); // stop sensor feedback

		sprintf_s( buf, 10, "%c%c%c", 13, 0, 2 );
		this->avatarQueueMessage( 7, buf, 3 ); // set DC 0 to encoder feedback
		sprintf_s( buf, 10, "%c%c%c", 14, 0, 1 );		
		this->avatarQueueMessage( 7, buf, 3 ); // set DC 0 to position control
		sprintf_s( buf, 10, "%c%c%c", 13, 1, 2 );
		this->avatarQueueMessage( 7, buf, 3 ); // set DC 1 to encoder feedback
		sprintf_s( buf, 10, "%c%c%c", 14, 1, 1 );
		this->avatarQueueMessage( 7, buf, 3 ); // set DC 1 to position control
		
		//sprintf_s( buf, 10, "%c%c%c%c%c", 7, 0, 1, 0, 0 );
		//this->avatarQueueMessage( 7, buf, 5 ); // set DC 0 Kp
		//sprintf_s( buf, 10, "%c%c%c%c%c", 7, 0, 2, 0, 0 );
		//this->avatarQueueMessage( 7, buf, 5 ); // set DC 0 Kd
		//sprintf_s( buf, 10, "%c%c%c%c%c", 7, 0, 3, 0, 1 );
		//this->avatarQueueMessage( 7, buf, 5 ); // set DC 0 Ki	

		this->avatarQueueMessage( 30, "\1", 1 ); // enable DC motors and servos
		buf[0] = DR_LSB(SERVO_POSE[0]); buf[1] = DR_MSB(SERVO_POSE[0]); // SERVO 1
		buf[2] = DR_LSB(SERVO_POSE[1]); buf[3] = DR_MSB(SERVO_POSE[1]); // SERVO 2
		buf[4] = DR_LSB(SERVO_POSE[2]); buf[5] = DR_MSB(SERVO_POSE[2]); // SERVO 3
		buf[6] = DR_LSB(SERVO_POSE[3]); buf[7] = DR_MSB(SERVO_POSE[3]); // SERVO 4
		buf[8] = DR_LSB(SERVO_POSE[4]); buf[9] = DR_MSB(SERVO_POSE[4]); // SERVO 5
		buf[10] = DR_LSB(SERVO_POSE[5]); buf[11] = DR_MSB(SERVO_POSE[5]); // SERVO 6
		buf[12] = DR_LSB(700); buf[13] = DR_MSB(700); // TIME (in control periods)
		this->avatarQueueMessage( 29, buf, 14 ); // set servo positions		

		this->avatarPingWatcher = this->addTimeout( PING_WAIT_PERIOD, AvatarX80H_CBR_cbWaitForPing, NULL, 0, false );
	} else {
		this->removeTimeout( &this->avatarPingWatcher );
		this->avatarPingWatcher = nilUUID;
	}

	return AvatarBase::setAvatarState( ready );
}

//-----------------------------------------------------------------------------
// Position/Rotation State

int AvatarX80H::updatePos( int el, int er, _timeb *tb ) {
	int del, der;
	float d, dr;

	//printf( "AvatarX80H::updatePos: el %d er %d\n", el, er );

	if ( !this->posEncodersInitialized ) {
		this->posEL = el;
		this->posER = er;
		this->posEncodersInitialized = true;

		return 0;
	}

	if ( !STATE(AvatarBase)->posInitialized ) {
		this->posEL = el;
		this->posER = er;
		return 1;
	}

	del = el - this->posEL;
	if ( del < -ENCODER_DIVS/2 ) del += ENCODER_DIVS;
	else if ( del > ENCODER_DIVS/2 ) del -= ENCODER_DIVS;
	der = er - this->posER;
	if ( der < -ENCODER_DIVS/2 ) der += ENCODER_DIVS;
	else if ( der > ENCODER_DIVS/2 ) der -= ENCODER_DIVS;

	this->posEL = el;
	this->posER = er;

	dr = (del + der)*((WHEEL_CIRCUMFERENCE/COUNTS_PER_REV)/(WHEEL_BASE*fM_PI)) * fM_PI / CALIBRATE_ROTATION;

	d = (-del + der)/2.0f * (WHEEL_CIRCUMFERENCE/COUNTS_PER_REV) / CALIBRATE_POSITION;

	AvatarBase::updatePos( d, 0, dr, tb );

	if ( this->actionMonitorEncoders
	  && abs(this->posEL - this->posELTarg) < ENCODER_THRESHOLD 
	  && abs(this->posER - this->posERTarg) < ENCODER_THRESHOLD ) { // action finished
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::updatePos: encoder action complete! (%5d vs %5d, %5d vs %5d)", this->posEL, this->posELTarg, this->posER, this->posERTarg );
		this->actionMonitorEncoders = false;
	
		int remainingT = this->readTimeout( &STATE(AvatarBase)->actionTimeout );
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = this->addTimeout( min( 500, remainingT ), AvatarX80H_CBR_cbActionStep );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Sensors

int AvatarX80H::processSensorData( char *data, _timeb *tb ) {
	int i;

	if ( this->sonarSkip == 0 ) {
		SonarReading sonarReading;
		this->sonarSkip = SKIP_SONAR_COUNT;
		for ( i=0; i<NUM_SONAR; i++ ) {
			// prepare reading
			sonarReading.value = ((unsigned char*)data)[i]/100.0f; // m

			// send to DDB
			this->ds.reset();
			this->ds.packUUID( &this->sonarId[i] );
			this->ds.packData( tb, sizeof(_timeb) );
			this->ds.packInt32( sizeof(SonarReading) );
			this->ds.packData( &sonarReading, sizeof(SonarReading) );
			this->ds.packInt32( 0 );
			this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}
	} else {
		this->sonarSkip--;
	}


	return 0;
}

int AvatarX80H::processImageChunk( unsigned char *data, int len, _timeb *tb ) {
	unsigned char chunkSeq;

	chunkSeq = data[0];

	// are we waiting for an image?
	if ( !this->requestingImage ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::processImageChunk: Received unexpected image chunk (%d), no image request in progress", chunkSeq );	
		return 0;
	}
	
	if ( !this->actionQueue.size() ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::processImageChunk: Received unexpected image chunk (%d), no action in progress", chunkSeq );	
		return 0;
	}

	ActionInfo *pAI;
	pAI = &(*this->actionQueue.begin());
	if ( pAI->action != AA_IMAGE || actionProgressStatus != 1 ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::processImageChunk: Received unexpected image chunk (%d), not on AA_IMAGE", chunkSeq );	
		return 0;
	}


	// is this the chunk we are expecting?
	if ( chunkSeq != this->imageSeq && ( this->imageSeq == 0 || (this->imageSeq > 0 && chunkSeq != 0xFF) ) ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::processImageChunk: Received out of order image chunk (expecting %d received %d)", this->imageSeq, chunkSeq );	
		// image failed
		this->cameraCommandFailed();
		return 0;
	} else {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::processImageChunk: Received image chunk %d", chunkSeq );	
	}

	// record time if this is the first chunk
	if ( chunkSeq == 0 ) {
		this->imageTime = *tb;
	}

	// ensure buffer is big enough
	if ( this->imageBufSize < this->imageSize + len ) {
		int newsize = this->imageBufSize * 2;
		while ( newsize < this->imageSize + len ) newsize *= 2;
		this->imageBuf = (unsigned char *)realloc( this->imageBuf, newsize );
		if ( !this->imageBuf ) {
			Log.log( 0, "AvatarX80H::processImageChunk: realloc failed" );	
			this->imageBufSize = 0;
			return 1;
		}
		this->imageBufSize = newsize;
	}

	this->imageSeq++; // expect next image chunk in sequence

	if ( chunkSeq != 0xFF ) {
		// append chunk
		memcpy( this->imageBuf + this->imageSize, data + 1, len - 1 );
		this->imageSize += len - 1;
	} else { // image finished
		// read bonus data (UNUSED)
		unsigned char FlagOfSize, Quality;
		FlagOfSize = data[1];
		Quality = data[2];

		// append chunk
		memcpy( this->imageBuf + this->imageSize, data + 3, len - 3 );
		this->imageSize += len - 3;

		this->cameraCommandFinished();
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Actions

int AvatarX80H::queueAction( UUID *director, UUID *thread, int action, void *data, int len ) {
	float delay = AvatarX80H_IMAGE_DELAY;

	// slip in an image before every action (except image!)
	if ( action != AA_IMAGE )
		AvatarBase::queueAction( getUUID(), getUUID(), AA_IMAGE, &delay, sizeof(float) );

	return AvatarBase::queueAction( director, thread, action, data, len );
}

int AvatarX80H::nextAction() {
	ActionInfo *pAI;

	AvatarBase::nextAction(); // prepare for next action

	if ( !STATE(AvatarBase)->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::nextAction: AA_WAIT start %.3f", *pAD );
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarX80H_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			int d;
			unsigned short millis;
			char buf[14];

			if ( !this->posEncodersInitialized ) {
				Log.log( 0, "AvatarX80H::nextAction: AA_MOVE request before encoders initialized!" );
				return 1;
			}
			
			d = (int)(*pAD/WHEEL_CIRCUMFERENCE * COUNTS_PER_REV * CALIBRATE_POSITION);
			if ( abs(d) > ENCODER_DIVS/2 - 1 ) {
				Log.log( 0, "AvatarX80H::nextAction: AA_MOVE requested distance too great!" );
				return 1;
			}
			millis = (unsigned short)(fabs(*pAD)/SAFE_VELOCITY * 1000);
			
			this->posELTarg = this->posEL - d;
			while ( this->posELTarg < 0 ) this->posELTarg +=ENCODER_DIVS;
			while ( this->posELTarg > ENCODER_DIVS ) this->posELTarg -= ENCODER_DIVS;
			this->posERTarg = this->posER + d;
			while ( this->posERTarg < 0 ) this->posERTarg += ENCODER_DIVS;
			while ( this->posERTarg > ENCODER_DIVS ) this->posERTarg -= ENCODER_DIVS;

			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::nextAction: AA_MOVE start %.3f (%5d->%5d %5d->%5d) est millis=%d", *pAD, this->posEL, this->posELTarg, this->posER, this->posERTarg, millis );
			
			buf[0] = DR_LSB(this->posELTarg); buf[1] = DR_MSB(this->posELTarg); // DC 0
			buf[2] = DR_LSB(this->posERTarg); buf[3] = DR_MSB(this->posERTarg); // DC 1
			buf[4] = buf[5] = buf[6] = buf[7] = buf[8] = buf[9] = buf[10] = buf[11] = 0; // DC 2-5
			buf[12] = DR_LSB(millis); buf[13] = DR_MSB(millis); // Time
			this->avatarQueueMessage( 4, buf, 14 );

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis + MOVE_FINISH_DELAY, AvatarX80H_CBR_cbActionStep ) ) )
				return 1;

			this->actionMonitorEncoders = true;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			int d;
			unsigned short millis;
			char buf[14];

			if ( !this->posEncodersInitialized ) {
				Log.log( 0, "AvatarX80H::nextAction: AA_ROTATE request before encoders initialized!" );
				return 1;
			}
			
			d = (int)((*pAD/(2*fM_PI)*(WHEEL_BASE*fM_PI))/WHEEL_CIRCUMFERENCE * COUNTS_PER_REV * CALIBRATE_ROTATION);
			if ( abs(d) > ENCODER_DIVS/2 - 1 ) {
				Log.log( 0, "AvatarX80H::nextAction: AA_ROTATE requested distance too great!" );
				return 1;
			}
			millis = (unsigned short)(fabs(*pAD)/SAFE_ROTATION * 1000);
			
			this->posELTarg = this->posEL + d;
			while ( this->posELTarg < 0 ) this->posELTarg += ENCODER_DIVS;
			while ( this->posELTarg > ENCODER_DIVS ) this->posELTarg -= ENCODER_DIVS;
			this->posERTarg = this->posER + d;
			while ( this->posERTarg < 0 ) this->posERTarg += ENCODER_DIVS;
			while ( this->posERTarg > ENCODER_DIVS ) this->posERTarg -= ENCODER_DIVS;

			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::nextAction: AA_ROTATE start %.3f (%5d->%5d %5d->%5d) est millis=%d", *pAD, this->posEL, this->posELTarg, this->posER, this->posERTarg, millis );
			
			buf[0] = DR_LSB(this->posELTarg); buf[1] = DR_MSB(this->posELTarg); // DC 0
			buf[2] = DR_LSB(this->posERTarg); buf[3] = DR_MSB(this->posERTarg); // DC 1
			buf[4] = buf[5] = buf[6] = buf[7] = buf[8] = buf[9] = buf[10] = buf[11] = 0; // DC 2-5
			buf[12] = DR_LSB(millis); buf[13] = DR_MSB(millis); // Time
			this->avatarQueueMessage( 4, buf, 14 );

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis + MOVE_FINISH_DELAY, AvatarX80H_CBR_cbActionStep ) ) )
				return 1;

			this->actionMonitorEncoders = true;
		}
		break;
	case AA_IMAGE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned int millis = (unsigned int)((*pAD) * 1000);

			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::nextAction: AA_IMAGE, requesting image (delay %d ms)", millis );

			// reset servos in case they got shaken off
			char buf[14];
			buf[0] = DR_LSB(SERVO_POSE[0]); buf[1] = DR_MSB(SERVO_POSE[0]); // SERVO 1
			buf[2] = DR_LSB(SERVO_POSE[1]); buf[3] = DR_MSB(SERVO_POSE[1]); // SERVO 2
			buf[4] = DR_LSB(SERVO_POSE[2]); buf[5] = DR_MSB(SERVO_POSE[2]); // SERVO 3
			buf[6] = DR_LSB(SERVO_POSE[3]); buf[7] = DR_MSB(SERVO_POSE[3]); // SERVO 4
			buf[8] = DR_LSB(SERVO_POSE[4]); buf[9] = DR_MSB(SERVO_POSE[4]); // SERVO 5
			buf[10] = DR_LSB(SERVO_POSE[5]); buf[11] = DR_MSB(SERVO_POSE[5]); // SERVO 6
			buf[12] = DR_LSB(700); buf[13] = DR_MSB(700); // TIME (in control periods)
			this->avatarQueueMessage( 29, buf, 14 ); // set servo positions		

			this->actionProgressStatus = 0; // delaying

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarX80H_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	default:
		Log.log( 0, "AvatarX80H::nextAction: unknown action type %d", pAI->action );
	}

	return 0;
}

int AvatarX80H::cameraCommandFailed() {

	Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cameraCommandFailed: image request failed" );

	if ( !this->requestingImage ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cameraCommandFailed: still waiting before requesting image again" );	
		return 0;
	}

	this->requestingImage = false; // flag us as done for now

	// this counts as acknowledgement for the TAKE_PHOTO message
	this->avatarMessageAcknowledged();

	// are we still expecting an image?
	if ( !this->actionQueue.size() ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cameraCommandFailed: no action in progress" );	
		return 0;
	}

	ActionInfo *pAI;
	pAI = &(*this->actionQueue.begin());
	if ( pAI->action != AA_IMAGE || actionProgressStatus != 1 ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cameraCommandFailed: not on AA_IMAGE" );	
		return 0;
	}

	// we are, try again soon
	this->addTimeout( 500, AvatarX80H_CBR_cbRerequestImage );

	return 0;
}

int AvatarX80H::cameraCommandFinished() {
	int i, j;

	Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cameraCommandFinished: AA_IMAGE complete" );

	this->requestingImage = false; // we're done
	
	// this counts as acknowledgement for the TAKE_PHOTO message
	this->avatarMessageAcknowledged();

	// decode image
	imageDecoder->DecodeInit(70);
	imageDecoder->Decode((const char *)this->imageBuf, this->imageSize, this->decodeRGBBuf, 144, 176 );
	imageDecoder->DecodeDel();

	// reorient rgbBuf
	for( i=0; i<144; i++ ) {
		for( j=0; j<176; j++ ) {
			this->properRGBBuf[i*176*3 + (j*3)+0] = this->decodeRGBBuf[144* 176 *3-(i+1)*176*3+j*3+2];
			this->properRGBBuf[i*176*3 + (j*3)+1] = this->decodeRGBBuf[144* 176 *3-(i+1)*176*3+j*3+1];
			this->properRGBBuf[i*176*3 + (j*3)+2] = this->decodeRGBBuf[144* 176 *3-(i+1)*176*3+j*3+0];
		}
	}

	// prepare headers
	BITMAPFILEHEADER bmFileHeader;
	bmFileHeader.bfType = 19778;
	bmFileHeader.bfSize = 144 * 176 * 3+54;
	bmFileHeader.bfReserved1 = 0;
	bmFileHeader.bfReserved2 = 0;
	bmFileHeader.bfOffBits = 54;

	BITMAPINFOHEADER bmInfo;
	bmInfo.biSize = 40;
	bmInfo.biWidth = 176;
	bmInfo.biHeight =144;
	bmInfo.biPlanes =1;
	bmInfo.biBitCount =24;
	bmInfo.biCompression =0;
	bmInfo.biSizeImage =144 * 176 * 3;
	bmInfo.biXPelsPerMeter =3780;
	bmInfo.biYPelsPerMeter =3780;
	bmInfo.biClrUsed =0;
	bmInfo.biClrImportant =0;

	// prepare camera reading
	CameraReading cr;
	sprintf_s( cr.format, IMAGEFORMAT_SIZE, "bmp" );
	cr.w = 176;
	cr.h = 144;

	// send reading
	this->ds.reset();
	this->ds.packUUID( &this->cameraId );
	this->ds.packData( &this->imageTime, sizeof(_timeb) );
	this->ds.packInt32( sizeof(CameraReading) );
	this->ds.packData( &cr, sizeof(CameraReading) );
	this->ds.packInt32( sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 144*176*3 );
	this->ds.packData( &bmFileHeader, sizeof(BITMAPFILEHEADER) );
	this->ds.packData( &bmInfo, sizeof(BITMAPINFOHEADER) );
	this->ds.packData( this->properRGBBuf, 144*176*3 );
	this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// TEMP
	char filename[1024];
	sprintf_s( filename, 1024, "data\\dump\\x80h%dimg%d.bmp", STATE(AgentBase)->agentType.instance, this->_cameraCommandFinishedImgCount++ );
	FILE *img = fopen( filename, "wb" );
	if ( img ) {
		fwrite( &bmFileHeader, 1, sizeof(BITMAPFILEHEADER), img );
		fwrite( &bmInfo, 1, sizeof(BITMAPINFOHEADER), img ); 
		fwrite( this->properRGBBuf, 1, 144*176*3, img );
		fclose( img );
	}

	// prepare next action
	this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
	STATE(AvatarBase)->actionTimeout = nilUUID;
	this->nextAction();

	return 0;
}

//-----------------------------------------------------------------------------
// Networking

int AvatarX80H::avatarResetConnection() {
	
	if ( this->avatarPingWatcher != nilUUID ) {
		this->removeTimeout( &this->avatarPingWatcher );
		this->avatarPingWatcher = nilUUID;
	}
	if ( this->avatarMessageTimeout != nilUUID ) {
		this->removeTimeout( &this->avatarMessageTimeout );
		this->avatarMessageTimeout = nilUUID;
	}
	
	if ( this->avatarReady ) {
		this->setAvatarState( false );
	}

	this->closeConnection( this->avatarCon );
	
	this->avatarCon = this->openConnection( &this->avatarAP, NULL, -1, IPPROTO_UDP );
	if ( !this->avatarCon ) {
		Log.log( 0, "AvatarX80H::avatarResetConnection: failed to open connection to avatar" );
		return 1;
	}

	this->avatarCon->raw = true; // switch to raw mode
	this->avatarConWatcher = this->watchConnection( this->avatarCon, AvatarX80H_CBR_cbWatchAvatarConnection );

	return 0;
}

int AvatarX80H::conProcessRaw( spConnection con ) {
	unsigned char dataLength;

	if ( !con->bufLen ) {
		goto returnOK;
	}

	if ( con->waitingForData == -1 ) { // waiting to complete STX
		if ( con->buf[con->bufStart] == 2 ) {
			con->bufStart++;
			con->bufLen--;
			con->message = (unsigned char)-1;
			if ( con->bufLen < 4 ) {
				con->waitingForData = 4;
				goto returnOK;
			} else {
				con->waitingForData = 0;
			}
		} else {
			con->waitingForData = 0; // bad STX!
		}
	} else if ( con->waitingForData > con->bufLen ) {
		goto returnOK;
	} else {
		con->waitingForData = 0;
	}		

	if ( con->message == (unsigned char)-1 ) { 
		dataLength = con->buf[con->bufStart+3];
		if ( con->bufLen < dataLength + 7 ) {
			con->waitingForData = dataLength + 7;
			goto returnOK;
		}

		// whole message has arrived
		// check ETX
		if ( !(con->buf[con->bufStart+5+dataLength] == 94 && con->buf[con->bufStart+6+dataLength] == 13) ) {
			// bad ETX, possibly false STX, search buffer for another STX
			Log.log( 3, "AvatarX80H::conProcessRaw: couldn't find ETX" );
			con->message = 0; 
			if ( this->requestingImage )
				this->cameraCommandFailed();
		}
		// check checksum
		else if ( (unsigned char)con->buf[con->bufStart+4+dataLength] != checksumDrRobot( (unsigned char *)(con->buf+con->bufStart), dataLength + 4 ) ) { // bad checksum
			// bad checksum, message is corrupt, discard message
			Log.log( 3, "AvatarX80H::conProcessRaw: bad checksum! %d %d", (unsigned char)con->buf[con->bufStart+4+dataLength], checksumDrRobot( (unsigned char *)(con->buf+con->bufStart), dataLength + 4 ) );
			con->message = 0;
			con->bufStart += dataLength + 7;
			con->bufLen -= dataLength + 7;
		}
		// check that we are the target
		else if ( con->buf[con->bufStart] != 0 ) {
			// wrong target, discard message
			Log.log( 3, "AvatarX80H::conProcessRaw: wrong target %d", con->buf[con->bufStart] );
			con->message = 0;
			con->bufStart += dataLength + 7;
			con->bufLen -= dataLength + 7;
		} 
		// everything is ok, process the message
		else {
			avatarProcessMessage( con, con->buf[con->bufStart+2], con->buf+con->bufStart+4, dataLength );
			//for ( int i = 0; i<dataLength+7; i++ ) {
			//	printf( "%d ", con->buf[con->bufStart+i] );
			//}
			//printf( "\n" );
			con->bufStart += dataLength + 7;
			con->bufLen -= dataLength + 7;
			con->message = 0;
		}
		
	} else { // looking for a STX to begin a new message
		while ( con->bufLen && con->buf[con->bufStart] != 94 ) { // clean until the beginning of the next message
			con->bufStart++;
			con->bufLen--;
		}
		if ( con->bufLen ) {
			con->waitingForData = -1;
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

int AvatarX80H::avatarMessageAcknowledged() {
	if ( this->avatarMessageTimeout != nilUUID ) {
		this->removeTimeout( &this->avatarMessageTimeout );
		this->avatarMessageTimeout = nilUUID;
	}

	this->messageQueueBegin = (this->messageQueueBegin + 1) % AvatarX80H_MESSAGE_QUEUE_SIZE;
	this->avatarNextMessage(); // try to send the next message

	return 0;
}

int AvatarX80H::avatarProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {

	if ( this->avatarPingWatcher != nilUUID )
		this->resetTimeout( &this->avatarPingWatcher );

	// see PMS5005_p.pdf and PMB5010_p.pdf for message types and format
	switch ( message ) {
	case 9: // COMTYPE_VIDEO (chunk of image frame)
		this->processImageChunk( (unsigned char *)data, len, &con->recvTime );
		break;
	case 123: // motor feedback
		//fprintf( encoderF, "%.3f	%d	%d	%d\n", con->recvTime.time+0.001f*con->recvTime.millitm, con->statistics.latency, DR_SHORT(data+24), DR_SHORT(data+28) );
		this->updatePos( DR_SHORT(data+24), DR_SHORT(data+28), &con->recvTime );
		break;
	case 125: // sensor feedback
		fprintf( sensorF, "%.3f	%d	%d	%d	%d\n", con->recvTime.time+0.001f*con->recvTime.millitm, con->statistics.latency, (unsigned char)data[0], (unsigned char)data[1], (unsigned char)data[2] );
		//Log.log(0, "sonar %d %d %d", (unsigned char)data[0], (unsigned char)data[1], (unsigned char)data[2] );
		this->processSensorData( data, &con->recvTime );
		break;
	case 255: // system communication
		Log.log( 0, "AvatarX80H::avatarProcessMessage: system message data = %d", data[0] );
		if ( data[0] == 1 ) { // ack
			this->conFinishLatencyCheck( con, this->avatarLatencyTicket, &con->recvTime );	
			this->avatarMessageTimeouts = 0;
			if ( !this->avatarReady ) {
				this->setAvatarState( true );
			}

			this->avatarMessageAcknowledged();
		}
		break;
	default:
		Log.log( 0, "AvatarX80H::avatarProcessMessage: unhandled message! %d", message );
		return 1; // unhandled message
	}

	return 0;
}

int AvatarX80H::avatarQueueMessage( unsigned char message, char *data, unsigned char len, unsigned char target ) {
	unsigned char *buf, *bufPtr;
	int i;
	int nextSlot;

	nextSlot = (this->messageQueueEnd + 1) % AvatarX80H_MESSAGE_QUEUE_SIZE;

	if ( nextSlot == this->messageQueueBegin ) {
		Log.log( 0, "AvatarX80H::avatarQueueMessage: message queue full!" );
		return 1;
	}

	Log.log( 0, "AvatarX80H::avatarQueueMessage: queueing message id = %d len = %d", message, len );

	buf = this->messageQueue[this->messageQueueEnd];
	this->messageQueueEnd = nextSlot;

	memset( buf, 0, sizeof(this->messageQueue[0]) );

	buf[0] = 94; buf[1] = 2;	// STX
	buf[2] = target;			// RID, 1 == PMS5005, 2 == PMB5010
	buf[3] = 0;					// Reserved
	buf[4] = message;			// DID
	buf[5] = len;				// Length

	// Data
	bufPtr = buf + 6;
	for ( i=0; i<len; i++ ) {
		*bufPtr = data[i];
		bufPtr++;
	}

	bufPtr[0] = checksumDrRobot( buf + 2, len + 4 );	// Checksum
	bufPtr[1] = 94; bufPtr[2] = 13;					// ETX

	return this->avatarNextMessage(); // try to send the next message
}


int AvatarX80H::avatarNextMessage() {
	unsigned char *buf;

	if ( this->avatarMessageTimeout != nilUUID ) { // we're already waiting for ack of a previous message
		return 0;
	}

	if ( this->messageQueueBegin == this->messageQueueEnd ) { // no messages to send
		return 0;
	}

	buf = this->messageQueue[this->messageQueueBegin];

	Log.log( 0, "AvatarX80H::avatarSendMessage: sending message id = %d len = %d", buf[4], buf[5] );
	if ( buf[4] == 0x20 ) { // image request
		this->requestingImage = true;
	}

	this->avatarMessageTimeout = this->addTimeout( MESSAGE_TIMEOUT, AvatarX80H_CBR_cbMessageTimeout );
	
	this->avatarLatencyTicket = this->conStartLatencyCheck( this->avatarCon );
	return this->sendRaw( this->avatarCon, (char *)buf, buf[5] + 9 );
}


int AvatarX80H::avatarPing() {
	return this->avatarQueueMessage( 255, "\0", 1 );
}

//-----------------------------------------------------------------------------
// Callbacks


bool AvatarX80H::cbWatchAvatarConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	
	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { // connected 
			if ( this->messageQueueBegin == this->messageQueueEnd ) {
				this->avatarPing(); // test connection
			} else {
				this->avatarNextMessage(); // try to send the next message
			}
		} else { // not connected
			if ( this->avatarReady ) 
				this->setAvatarState( false );
		}
	} else if ( evt == CON_EVT_CLOSED ) {
		if ( this->avatarReady ) 
			this->setAvatarState( false );

		if ( this->avatarMessageTimeout != nilUUID ) {
			this->removeTimeout( &this->avatarMessageTimeout );
			this->avatarMessageTimeout = nilUUID;
		}

		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbWatchAvatarConnection: avatarCon closed" );
	}

	return 0;
}

bool AvatarX80H::cbWaitForPing( void *NA ) {
	
	Log.log( 0, "AvatarX80H::cbWaitForPing: Wait for ping timed out!" );

	this->avatarPingWatcher = nilUUID;

	this->avatarResetConnection();

	return 0;
}

bool AvatarX80H::cbMessageTimeout( void *NA ) {

	Log.log( 0, "AvatarX80H::cbMessageTimeout: Message wasn't acknowledged!" );

	this->conFinishLatencyCheck( this->avatarCon, this->avatarLatencyTicket );

	this->avatarMessageTimeout = nilUUID;
	this->avatarMessageTimeouts++;
	
	if ( this->avatarMessageTimeouts >= 3 ) {
		this->avatarResetConnection();
	} else {
		this->avatarNextMessage();
	}

	return 0;
}

bool AvatarX80H::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbActionStep: AA_WAIT finished" );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();
		return 0;
	case AA_MOVE:
		this->actionMonitorEncoders = false;
		if ( abs(this->posEL - this->posELTarg) < ENCODER_THRESHOLD 
		  && abs(this->posER - this->posERTarg) < ENCODER_THRESHOLD ) { // action finished
			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbActionStep: AA_MOVE complete! (%5d vs %5d, %5d vs %5d)", this->posEL, this->posELTarg, this->posER, this->posERTarg );
			STATE(AvatarBase)->actionTimeout = nilUUID;
			this->nextAction(); 
		} else {
			Log.log( 0, "AvatarX80H::cbActionStep: AA_MOVE failed! (%5d vs %5d, %5d vs %5d)", this->posEL, this->posELTarg, this->posER, this->posERTarg );
			this->abortActions();
		}
		return 0;
	case AA_ROTATE:
		{
			int errL, errR;
			errL = abs(this->posEL - this->posELTarg);
			while ( errL > ENCODER_DIVS/2 ) errL -= ENCODER_DIVS;
			errR = abs(this->posER - this->posERTarg);
			while ( errR > ENCODER_DIVS/2 ) errR -= ENCODER_DIVS;
			this->actionMonitorEncoders = false;
			if ( abs(errL) < ENCODER_THRESHOLD 
			  && abs(errR) < ENCODER_THRESHOLD ) { // action finished
				Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbActionStep: AA_ROTATE complete! (%5d vs %5d, %5d vs %5d)", this->posEL, this->posELTarg, this->posER, this->posERTarg );
				STATE(AvatarBase)->actionTimeout = nilUUID;
				this->nextAction(); 
			} else {
				Log.log( 0, "AvatarX80H::cbActionStep: AA_ROTATE failed! (%5d vs %5d, %5d vs %5d)", this->posEL, this->posELTarg, this->posER, this->posERTarg );
				this->abortActions();
			}
		}
		return 0;
	case AA_IMAGE:
		if ( this->actionProgressStatus == 0 ) { // request the image
			this->actionProgressStatus = 1;
			

			// ask for the image		
			this->imageSize = 0;
			this->imageSeq = 0; // reset chunk sequence number
			this->avatarQueueMessage( 0x20, NULL, 0, 8 );

			STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarX80H_IMAGE_TIMEOUT, AvatarX80H_CBR_cbActionStep );
			return 0;
		} else { // timed out
			Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbActionStep: AA_IMAGE timed out!" );
			this->abortActions();
			this->cameraCommandFailed();
			return 0;
		}
	default:
		Log.log( 0, "AvatarX80H::cbActionStep: unhandled action type %d", pAI->action );
	}

	return 0;
}

bool AvatarX80H::cbRerequestImage( void *NA ) {

	Log.log( LOG_LEVEL_VERBOSE, "AvatarX80H::cbRerequestImage: requesting image" );

	this->imageSize = 0;
	this->imageSeq = 0; // reset chunk sequence number
	this->avatarQueueMessage( 0x20, NULL, 0, 8 );

	return 0;
}

//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarX80H *agent = (AvatarX80H *)vpAgent;

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
	AvatarX80H *agent = new AvatarX80H( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarX80H *agent = new AvatarX80H( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CAvatarX80HDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAvatarX80HDLL, CWinApp)
END_MESSAGE_MAP()

CAvatarX80HDLL::CAvatarX80HDLL() {
	GetCurrentDirectory( 512, DLL_Directory );
}

// The one and only CAvatarX80HDLL object
CAvatarX80HDLL theApp;

int CAvatarX80HDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAvatarX80HDLL ---\n"));
  return CWinApp::ExitInstance();
}