// AvatarER1.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\Autonomic\\autonomic.h"
#include "..\\AvatarBase\\AvatarBase.h"

#include "..\\Autonomic\\DDB.h"

#include "AvatarER1.h"
#include "AvatarER1Version.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

WCHAR DLL_Directory[512];

//*****************************************************************************
// AvatarER1

//-----------------------------------------------------------------------------
// Constructor	
AvatarER1::AvatarER1( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) : AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarER1" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarER1_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	this->avatarCon = NULL;

	this->listeningToEvents = false;
	this->avatarMessageTimeout = nilUUID;
	this->avatarMessageTimeouts = 0;
	this->messageQueueBegin = 0;
	this->messageQueueEnd = 0;
	this->avatarLatencyTicket = -1;

	// Prepare callbacks
	this->callback[AvatarER1_CBR_cbWatchAvatarConnection] = NEW_MEMBER_CB(AvatarER1,cbWatchAvatarConnection);
	this->callback[AvatarER1_CBR_cbActionStep] = NEW_MEMBER_CB(AvatarER1,cbActionStep);
	this->callback[AvatarER1_CBR_cbMessageTimeout] = NEW_MEMBER_CB(AvatarER1,cbMessageTimeout);
}

//-----------------------------------------------------------------------------
// Destructor
AvatarER1::~AvatarER1() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int AvatarER1::configure() {
	
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
		sprintf_s( logName, "%s\\AvatarER1 %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_ALL );
		Log.log( 0, "AvatarER1 %.2d.%.2d.%.5d.%.2d", AvatarER1_MAJOR, AvatarER1_MINOR, AvatarER1_BUILDNO, AvatarER1_EXTEND );
	}

	if ( AvatarBase::configure() ) 
		return 1;

	return 0;
}

int AvatarER1::setInstance( char instance ) {
	char paraN[512];
	FILE *paraF;

	// open parameter file
	sprintf_s( paraN, sizeof(paraN), "%ws\\instance%d.ini", DLL_Directory, instance );
	if ( fopen_s( &paraF, paraN, "r" ) ) {
		Log.log( 0, "AvatarER1::setInstance: failed to open %s", paraN );
		return 1;
	}

	if ( 1 != fscanf_s( paraF, "TYPE=%s\n", TYPE, sizeof(TYPE) ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected TYPE=<string>, check format" );
		goto returnerror;
	}	
	if ( 1 != fscanf_s( paraF, "ADDRESS=%s\n", ADDRESS, sizeof(ADDRESS) ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected ADDRESS=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "PORT=%s\n", PORT, sizeof(PORT) ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected PORT=<string>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "INNER_RADIUS=%f\n", &INNER_RADIUS ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected INNER_RADIUS=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "OUTER_RADIUS=%f\n", &OUTER_RADIUS ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected OUTER_RADIUS=<float>, check format" );
		goto returnerror;
	}

	if ( 1 != fscanf_s( paraF, "LINEAR_VELOCITY=%f\n", &LINEAR_VELOCITY ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected LINEAR_VELOCITY=<float>, check format" );
		goto returnerror;
	}
	// round to nearest 0.01 m
	LINEAR_VELOCITY = ((int)(LINEAR_VELOCITY*100))/100.0f;
	if ( 1 != fscanf_s( paraF, "LINEAR_ACCELERATION=%f\n", &LINEAR_ACCELERATION ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected LINEAR_ACCELERATION=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "LINEAR_DECELERATION=%f\n", &LINEAR_DECELERATION ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected LINEAR_DECELERATION=<float>, check format" );
		goto returnerror;
	}

	if ( 1 != fscanf_s( paraF, "ANGULAR_VELOCITY=%d\n", &ANGULAR_VELOCITY ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected ANGULAR_VELOCITY=<int>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "ANGULAR_ACCELERATION=%f\n", &ANGULAR_ACCELERATION ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected ANGULAR_ACCELERATION=<float>, check format" );
		goto returnerror;
	}
	if ( 1 != fscanf_s( paraF, "ANGULAR_DECELERATION=%f\n", &ANGULAR_DECELERATION ) ) {
		Log.log( 0, "AvatarER1::setInstance: expected ANGULAR_DECELERATION=<float>, check format" );
		goto returnerror;
	}

	fclose( paraF );

	this->registerAvatar( TYPE, INNER_RADIUS, OUTER_RADIUS, 0, 0 /* NO SENSORS SO FAR! */ );

	// set our maximum movements
	//STATE(AvatarBase)->maxLinear = 2.5f;
	//STATE(AvatarBase)->maxRotation = 2.5f;

	return AvatarBase::setInstance( instance );

returnerror:
	fclose( paraF );
	return 1;
}

//-----------------------------------------------------------------------------
// Start

int AvatarER1::start( char *missionFile ) {
	
	if ( STATE(AgentBase)->agentType.instance == -1 ) {
		Log.log( 0, "AvatarER1::start: instance must be set before we start" );
		return 1;
	}

	if ( AvatarBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	// initiate connection to avatar
	sprintf_s( this->avatarAP.address, sizeof(this->avatarAP.address), ADDRESS );
	sprintf_s( this->avatarAP.port, sizeof(this->avatarAP.port), PORT );

	this->avatarCon = this->openConnection( &this->avatarAP, NULL, -1 );
	if ( !this->avatarCon ) {
		Log.log( 0, "AvatarER1::start: failed to open connection to avatar" );
		return 1;
	}

	this->avatarCon->raw = true; // switch to raw mode
	this->watchConnection( this->avatarCon, AvatarER1_CBR_cbWatchAvatarConnection );

	STATE(AgentBase)->started = true;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarER1::stop() {

	if ( this->avatarCon )
		this->closeConnection( this->avatarCon );

	this->listeningToEvents = false;

	return AvatarBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarER1::step() {

	// TEMP
	static int startupactions = 1;
	if ( this->avatarReady && startupactions ) {
		float val = 1.0f;
		this->queueAction( this->getUUID(), 0, AA_MOVE, &val, 4 );
		val = -1.0f;
		this->queueAction( this->getUUID(), 0, AA_MOVE, &val, 4 );
		val = fM_PI;
		this->queueAction( this->getUUID(), 0, AA_ROTATE, &val, 4 );
		val = -fM_PI;
		this->queueAction( this->getUUID(), 0, AA_ROTATE, &val, 4 );
		startupactions = 0;
	}

	return AvatarBase::step();
}

int AvatarER1::setAvatarState( bool ready ) {

	int ret;
	if ( ret = AvatarBase::setAvatarState( ready ) )
		return ret;
	
	if ( ready ) {
		// set velocity limits
		char buf[256];
//		sprintf_s( buf, sizeof(buf), "move 10 cm" );
//		this->avatarQueueMessage( buf );
		sprintf_s( buf, sizeof(buf), "set v %d", (int)(LINEAR_VELOCITY*100) );
		this->avatarQueueMessage( buf );
		sprintf_s( buf, sizeof(buf), "set w %d", ANGULAR_VELOCITY );
		this->avatarQueueMessage( buf );

		this->firstAvatarPos = true;
		this->avatarQueueMessage( "position", RET_POSITION );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Position/Rotation State

int AvatarER1::updatePos( int action, unsigned short millis, _timeb *tb ) {
	float d, dx, dy, dr;

	if ( action == AA_MOVE ) {
		// in avatar frame
		dx = this->latestPosX - this->avatarPosX;
		dy = this->latestPosY - this->avatarPosY;
		dr = 0;
		
		// convert to agent frame
		d = (float)sqrt(dx*dx + dy*dy);
		if ( this->motionDirection == MOTION_BACKWARD )
			d = -d;

	} else if ( action == AA_ROTATE ) {
		if ( this->motionDirection == MOTION_CW ) {
			dr = -ANGULAR_VELOCITY*millis/1000.0f * fM_PI/180;
		} else {
			dr = ANGULAR_VELOCITY*millis/1000.0f * fM_PI/180;
		}
		d = 0;
	} else {
		Log.log( 0, "AvatarER1::updatePos: unhandled action %d", action );
		return 1;
	}

	AvatarBase::updatePos( d, 0, dr, tb );

	return 0;
}

//-----------------------------------------------------------------------------
// Actions

int AvatarER1::queueAction( UUID *director, UUID *thread, int action, void *data, int len ) {
/*	float delay = AvatarER1_IMAGE_DELAY;

	// slip in an image before every action (except image!)
	if ( action != AA_IMAGE )
		AvatarBase::queueAction( getUUID(), getUUID(), AA_IMAGE, &delay, sizeof(float) );
*/
	return AvatarBase::queueAction( director, thread, action, data, len );
}

int AvatarER1::nextAction() {
	ActionInfo *pAI;

	AvatarBase::nextAction(); // prepare for next action

	if ( !STATE(AvatarBase)->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::nextAction: AA_WAIT start %.3f", *pAD );
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarER1_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;
			char buf[256];

			float D = *pAD;
			float vel = 0.0f;
			float velMax = LINEAR_VELOCITY;
			float acc = LINEAR_ACCELERATION;
			float dec = LINEAR_DECELERATION;
			float accD, cruD, decD;
			float accT, cruT, decT;

			// make sure we're accelerating in the right direction
			if ( D < 0 ) {
				this->motionDirection = MOTION_BACKWARD;
				velMax *= -1;
				acc *= -1;
				dec *= -1;
			} else {
				this->motionDirection = MOTION_FORWARD;
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
			
			sprintf_s( buf, 256, "move %f meters", *pAD );
			this->motionMillis = millis;

			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::nextAction: AA_MOVE start %.3f millis=%d", *pAD, millis );
						
			this->avatarQueueMessage( buf );

			this->actionProgressStatus = 1;
			apb->apb_ftime_s( &this->actionProgressLast );
			this->actionProgressEnd.time = (this->actionProgressLast.time + millis/1000) + (this->actionProgressLast.millitm + (millis%1000))/1000;
			this->actionProgressEnd.millitm = (this->actionProgressLast.millitm + (millis%1000)) % 1000;

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarER1_ACTIONPROGRESSPERIOD, AvatarER1_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;
			char buf[256];

			float D = *pAD;
			float vel = 0.0f;
			float velMax = (float)(ANGULAR_VELOCITY*fM_PI/180);
			float acc = (float)(ANGULAR_ACCELERATION*fM_PI/180);
			float dec = (float)(ANGULAR_DECELERATION*fM_PI/180);
			float accD, cruD, decD;
			float accT, cruT, decT;

			// make sure we're accelerating in the right direction
			if ( D < 0 ) {
				this->motionDirection = MOTION_CW;			
				velMax *= -1;
				acc *= -1;
				dec *= -1;
			} else {
				this->motionDirection = MOTION_CCW;
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

			sprintf_s( buf, 256, "move %f degrees", *pAD*180/fM_PI );
			this->motionMillis = millis;
			
			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::nextAction: AA_ROTATE start %.3f millis=%d", *pAD, millis );
						
			this->avatarQueueMessage( buf );

			this->actionProgressStatus = 1;
			apb->apb_ftime_s( &this->actionProgressLast );
			this->actionProgressEnd.time = (this->actionProgressLast.time + millis/1000) + (this->actionProgressLast.millitm + (millis%1000))/1000;
			this->actionProgressEnd.millitm = (this->actionProgressLast.millitm + (millis%1000)) % 1000;

			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarER1_ACTIONPROGRESSPERIOD, AvatarER1_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
/*	case AA_IMAGE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned int millis = (unsigned int)((*pAD) * 1000);

			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::nextAction: AA_IMAGE, requesting image (delay %d ms)", millis );

			this->actionProgressStatus = 0; // delaying

			if ( !(STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, NEW_MEMBER_CB( AvatarER1, cbActionStep ) ) ) )
				return 1;
		}
		break;
		*/
	default:
		Log.log( 0, "AvatarER1::nextAction: unknown action type %d", pAI->action );
	}

	return 0;
}

bool AvatarER1::motorCommandUpdate( int action, _timeb *tb, bool finished ) {
	unsigned short millis;

	if ( finished
	  || tb->time > this->actionProgressEnd.time
	  || (tb->time == this->actionProgressEnd.time && tb->millitm >= this->actionProgressEnd.millitm) ) {
		// updatePos with remaining time
		millis = (unsigned short)((this->actionProgressEnd.time - this->actionProgressLast.time)*1000 + (this->actionProgressEnd.millitm - this->actionProgressLast.millitm));
		this->updatePos( action, millis, tb );
		if ( !finished && this->actionProgressStatus ) {
			this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
			STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarER1_MOVE_FINISH_DELAY, AvatarER1_CBR_cbActionStep );
		}
		this->actionProgressStatus = 0;
		return 0; 
	}

	// updatePos with elapsed time
	millis = (unsigned short)((tb->time - this->actionProgressLast.time)*1000 + (tb->millitm - this->actionProgressLast.millitm));
	this->updatePos( action, millis, tb );
	this->actionProgressLast.time = tb->time;
	this->actionProgressLast.millitm = tb->millitm;
	return 1; // repeat timeout
}

int AvatarER1::motorCommandFinished() {
	ActionInfo *pAI;
	
	if ( !STATE(AvatarBase)->actionInProgress ) {	// no action to in progress!
		Log.log( 0, "AvatarER1::motorCommandFinished: no action in progress!" );
		return 1;
	}

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_MOVE:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::motorCommandFinished: AA_MOVE complete" );
		//if ( this->actionProgressStatus != 0 ) // we ended earlier than expected
			//this->motorCommandUpdate( AA_MOVE, &this->avatarCon->recvTime, true );
		this->avatarQueueMessage( "position", RET_POSITION ); // update position
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();		
		break;
	case AA_ROTATE:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::motorCommandFinished: AA_ROTATE complete" );
		if ( this->actionProgressStatus != 0 ) // we ended earlier than expected
			this->motorCommandUpdate( AA_ROTATE, &this->avatarCon->recvTime, true );
		this->removeTimeout( &STATE(AvatarBase)->actionTimeout );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();		
		break;
	default:
		Log.log( 0, "AvatarER1::motorCommandFinished: unknown action type %d", pAI->action );
		return 1;
	}


	return 0;
}

/*
int AvatarER1::cameraCommandFinished() {

	Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::cameraCommandFinished: AA_IMAGE complete" );
		
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
	this->removeTimeout( STATE(AvatarBase)->actionTimeout );
	STATE(AvatarBase)->actionTimeout = NULL;
	this->nextAction();

	return 0;
}
*/


//-----------------------------------------------------------------------------
// Network

int AvatarER1::avatarResetConnection() {
	
	if ( this->avatarMessageTimeout != nilUUID ) {
		this->removeTimeout( &this->avatarMessageTimeout );
		this->avatarMessageTimeout = nilUUID;
	}
	
	if ( this->avatarReady ) {
		this->setAvatarState( false );
	}

	this->closeConnection( this->avatarCon );
	
	this->avatarCon = this->openConnection( &this->avatarAP, NULL, -1 );
	if ( !this->avatarCon ) {
		Log.log( 0, "AvatarER1::avatarResetConnection: failed to open connection to avatar" );
		return 1;
	}

	this->avatarCon->raw = true; // switch to raw mode
	this->watchConnection( this->avatarCon, AvatarER1_CBR_cbWatchAvatarConnection );

	return 0;
}

int AvatarER1::conProcessRaw( spConnection con ) {
	
	// look for end of line, using waitingForData to record how far we've looked
	while ( con->waitingForData < con->bufLen ) {
		if ( con->buf[con->waitingForData+con->bufStart] == '\n' )
			break;
		con->waitingForData++;
	}

	if ( con->waitingForData == con->bufLen ) {
		goto returnOK; // message not complete so keep waiting
	}

	// trim message
	con->buf[con->waitingForData+con->bufStart - 1 ] = '\0'; // -1 to remove the \r on the message

	// process message
	this->avatarProcessMessage( con, con->buf + con->bufStart );

	// update buf state
	con->bufStart += con->waitingForData + 1;
	con->bufLen -= con->waitingForData + 1;
	con->waitingForData = 0;

	AgentBase::conProcessRaw( con );
	return this->conProcessRaw( con );

returnOK:
	AgentBase::conProcessRaw( con );
	return 0;
}

int AvatarER1::avatarProcessMessage( spConnection con, char *message ) {

	if ( !strncmp( message, "OK", 2 ) || !strncmp( message, "Error:", 6 ) ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::avatarProcessMessage: message acknowledged %s", message );
	
		this->conFinishLatencyCheck( con, this->avatarLatencyTicket, &con->recvTime );	
		this->avatarMessageTimeouts = 0;

		if ( this->avatarMessageTimeout != nilUUID ) {
			this->removeTimeout( &this->avatarMessageTimeout );
			this->avatarMessageTimeout = nilUUID;
		}

		
		this->listeningToEvents = false; // assume we aren't listening to events anymore unless we set it specifically below

		if ( !strncmp( message, "OK", 2 ) ) { // success
			switch ( this->messageReturn[this->messageQueueBegin] ) {
			case RET_POSITION:
				{
					float x, y, r;				
					if ( 3 != sscanf_s( message, "OK %f %f %f", &x, &y, &r ) ) {  // note the returned heading value is always 0
						Log.log( 0, "AvatarER1::avatarProcessMessage: expected position in format OK x y r" );
						break;
					}
					if ( this->firstAvatarPos == true ) { // set the initial postion
						this->avatarPosX = x;
						this->avatarPosY = y;
					} else {
						this->latestPosX = x;
						this->latestPosY = y;
					}
					this->motorCommandUpdate( AA_MOVE, &con->recvTime );
				}
				break;
			case RET_EVENTS:
				this->listeningToEvents = true;
				break;
			case RET_OK:
			default:
				break;
			}
		}

		this->messageQueueBegin = (this->messageQueueBegin + 1) % AvatarER1_MESSAGE_QUEUE_SIZE;
		this->avatarNextMessage(); // try to send the next message

	} else if ( !strncmp( message, "move done", 9 ) ) {
		Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::avatarProcessMessage: move done" );
		this->motorCommandFinished();
	} else {
		Log.log( 0, "AvatarER1::avatarProcessMessage: unhandled message! %d", message );
		return 1; // unhandled message
	}

	return 0;
}

int AvatarER1::avatarQueueMessage( char *message, char expectedReturn ) {
	char *buf;
	int nextSlot;

	if ( strlen(message) >= AvatarER1_MAX_MESSAGE_LENGTH - 2 ) {
		Log.log( 0, "AvatarER1::avatarQueueMessage: message too long" );
		return 1;
	}

	nextSlot = (this->messageQueueEnd + 1) % AvatarER1_MESSAGE_QUEUE_SIZE;

	if ( nextSlot == this->messageQueueBegin ) {
		Log.log( 0, "AvatarER1::avatarQueueMessage: message queue full!" );
		return 1;
	}

	Log.log( 0, "AvatarER1::avatarQueueMessage: queueing message %s", message );

	buf = this->messageQueue[this->messageQueueEnd];
	sprintf_s( buf, AvatarER1_MAX_MESSAGE_LENGTH, "%s\r\n", message );

	this->messageReturn[this->messageQueueEnd] = expectedReturn;

	this->messageQueueEnd = nextSlot;

	return this->avatarNextMessage(); // try to send the next message
}


int AvatarER1::avatarNextMessage() {
	char *buf;

	if ( !this->avatarReady ) { // avatar not ready
		return 0;
	}

	if ( this->avatarMessageTimeout != nilUUID ) { // we're already waiting for ack of a previous message
		return 0;
	}

	if ( this->messageQueueBegin == this->messageQueueEnd ) { // no messages to send, so listen for events
		if ( this->listeningToEvents )
			return 0; // already listening
		return this->avatarQueueMessage( "events", RET_EVENTS );
	} else {
		buf = (char *)this->messageQueue[this->messageQueueBegin];
	}
	
	if ( strlen(buf) > 256 ) {
		Log.log( 0, "AvatarER1::avatarSendMessage: corrupted message" );
		return 1;
	}

	Log.log( 0, "AvatarER1::avatarSendMessage: sending message %s", buf );

	this->avatarMessageTimeout = this->addTimeout( AvatarER1_MESSAGE_TIMEOUT, AvatarER1_CBR_cbMessageTimeout );

	this->avatarLatencyTicket = this->conStartLatencyCheck( this->avatarCon );
	return this->sendRaw( this->avatarCon, buf, (unsigned int)strlen(buf) );
}

//-----------------------------------------------------------------------------
// Callbacks

bool AvatarER1::cbWatchAvatarConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	
	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { // connected 
			this->setAvatarState( true );
		} else { // not connected
			this->setAvatarState( false );
		}
	} else if ( evt == CON_EVT_CLOSED ) {
		this->setAvatarState( false );
		Log.log( 0, "AvatarER1::cbWatchAvatarConnection: avatarCon closed, how did this happen?" );
	}

	return 0;
}


bool AvatarER1::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;
	_timeb tb;
	apb->apb_ftime_s( &tb );

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::cbActionStep: AA_WAIT finished" );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();
		return 0;
	case AA_MOVE:
		if ( this->actionProgressStatus == 0 ) { // waiting to finish
			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::cbActionStep: AA_MOVE timed out!" );
			this->abortActions();
			return 0;
		} else { // action in progress
			// request latest position
			this->avatarQueueMessage( "position", RET_POSITION );
			this->avatarQueueMessage( "events", RET_EVENTS );
			return 1;
		}
	case AA_ROTATE:
		if ( this->actionProgressStatus == 0 ) { // waiting to finish
			Log.log( LOG_LEVEL_VERBOSE, "AvatarER1::cbActionStep: AA_ROTATE timed out!" );
			this->abortActions();
			return 0;
		} else { // action in progress
			return this->motorCommandUpdate( AA_ROTATE, &tb );
		}
	default:
		Log.log( 0, "AvatarER1::cbActionStep: unhandled action type %d", pAI->action );
	}

	return 0;
}

bool AvatarER1::cbMessageTimeout( void *NA ) {

	Log.log( 0, "AvatarER1::cbMessageTimeout: Message wasn't acknowledged!" );

	this->conFinishLatencyCheck( this->avatarCon, this->avatarLatencyTicket );

	this->avatarMessageTimeout = nilUUID;
	this->avatarMessageTimeouts++;
	
	//if ( this->avatarMessageTimeouts >= 3 ) {
		this->avatarResetConnection();
	//} else {
	//	this->avatarNextMessage();
	//}

	return 0;
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarER1 *agent = (AvatarER1 *)vpAgent;

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
	AvatarER1 *agent = new AvatarER1( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarER1 *agent = new AvatarER1( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CAvatarER1DLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAvatarER1DLL, CWinApp)
END_MESSAGE_MAP()

CAvatarER1DLL::CAvatarER1DLL() {
	GetCurrentDirectory( 512, DLL_Directory );
}

// The one and only CAvatarER1DLL object
CAvatarER1DLL theApp;

int CAvatarER1DLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAvatarER1DLL ---\n"));
  return CWinApp::ExitInstance();
}