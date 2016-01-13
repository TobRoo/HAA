// AvatarPioneer.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"
#include "windows.h"

#include "..\\Autonomic\\autonomic.h"
#include "..\\Autonomic\\DDB.h"
#include "..\\AvatarBase\\AvatarBase.h"

#include "AvatarPioneer.h"
#include "AvatarPioneerVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
using namespace AvatarBase_Defs;


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

WCHAR DLL_Directory[512];

//*****************************************************************************
// AvatarPioneer

#define AR_LOCK		this->robot->lock();
#define AR_UNLOCK	this->robot->unlock();

#define DegreesToRadians(a) (a*M_PI/180.0) 
#define RadiansToDegrees(a) (a*180.0/M_PI)

//-----------------------------------------------------------------------------
// Constructor	
AvatarPioneer::AvatarPioneer( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) : AvatarBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AvatarPioneer, AvatarBase )
	
	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AvatarPioneer" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AvatarPioneer_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AvatarPioneer_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	this->AriaWakeTimeout = nilUUID;
	
	this->ariaAbsoluteR = 0;
	memset( this->skipReading, 0, sizeof(this->skipReading) );

	// initialize Aria	
	Aria::init();

	this->ariaConnectedCB = new ArFunctorC<AvatarPioneer>( this, &AvatarPioneer::cbAriaThreadConnected );
	this->ariaConnFailCB = new ArFunctorC<AvatarPioneer>( this, &AvatarPioneer::cbAriaThreadConnectionFailed);
	this->ariaDisconnectedCB = new ArFunctorC<AvatarPioneer>( this, &AvatarPioneer::cbAriaThreadDisconnected );
	this->ariaSyncTaskCB = new ArFunctorC<AvatarPioneer>( this, &AvatarPioneer::cbAriaThreadSyncTask );
	this->ariaArgc = 0;
	this->ariaArgParser = new ArArgumentParser(&this->ariaArgc, NULL);
	this->ariaCon = new ArSimpleConnector(this->ariaArgParser);
	this->robot = new ArRobot();

	this->robot->addConnectCB( ariaConnectedCB, ArListPos::FIRST );
	this->robot->addFailedConnectCB( ariaConnFailCB, ArListPos::FIRST );
	this->robot->addDisconnectNormallyCB( ariaDisconnectedCB, ArListPos::FIRST );
	this->robot->addDisconnectOnErrorCB( ariaDisconnectedCB, ArListPos::FIRST );
	this->robot->addSensorInterpTask( "AvatarPioneer", 50, ariaSyncTaskCB ); 

	Aria::parseArgs();

	
	// Prepare callbacks
	this->callback[AvatarPioneer_CBR_cbActionStep] = NEW_MEMBER_CB(AvatarPioneer,cbActionStep);
	this->callback[AvatarPioneer_CBR_cbAriaWake] = NEW_MEMBER_CB(AvatarPioneer,cbAriaWake);

}

//-----------------------------------------------------------------------------
// Destructor
AvatarPioneer::~AvatarPioneer() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	// free state

	Aria::shutdown();
	delete this->ariaCon;
	delete this->ariaArgParser;
	delete this->robot;
	delete this->ariaConnectedCB;
	delete this->ariaConnFailCB;
	delete this->ariaDisconnectedCB;

}

//-----------------------------------------------------------------------------
// Configure

int AvatarPioneer::configure() {
		
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "%s\\AvatarPioneer %s.txt", logDirectory, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AvatarPioneer %.2d.%.2d.%.5d.%.2d", AvatarPioneer_MAJOR, AvatarPioneer_MINOR, AvatarPioneer_BUILDNO, AvatarPioneer_EXTEND );
	}

	if ( AvatarBase::configure() ) 
		return 1;

	return 0;
}

int AvatarPioneer::ariaConfigure() {
	int i, numSonar;
	float x[128], y[128], r[128];

	char paramFile[512];
	sprintf_s( paramFile, 512, "%ws\\AvatarPioneer.p", DLL_Directory );
	AR_LOCK
		this->robot->loadParamFile( paramFile );
		this->robot->comInt(ArCommands::SONAR, 1);			// Sonar ON
		this->robot->comInt(ArCommands::ENABLE, 1);			// Motors ON
		this->robot->comInt(ArCommands::SOUNDTOG, 0);		// Sound OFF

		ArRobotParams *params = (ArRobotParams *)this->robot->getRobotParams();
		numSonar = params->getNumSonar();
		for ( i=0; i<numSonar; i++ ) {
			x[i] = params->getSonarX(i)*0.001f;
			y[i] = params->getSonarY(i)*0.001f;
			r[i] = DegreesToRadians(params->getSonarTh(i));
		}
	AR_UNLOCK

	// register avatar
	this->registerAvatar( "Pioneer", 0.1f, 0.3f, 0, DDB_SENSOR_SONAR );

	// register sensors
	for ( i=0; i<numSonar; i++ ) {
		this->addSonar( i, x[i], y[i], r[i] );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AvatarPioneer::start( char *missionFile ) {
	
	if ( AvatarBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;



	// start robot thread
	this->robot->runAsync( false );

	// initiate connection to Pioneer
	AR_LOCK
		this->AriaEvent.clear();
		this->ariaFrameRotation = AvatarPioneer_UNSET;
		this->ariaLastR = AvatarPioneer_UNSET;
		this->lastUpdatePosX = AvatarPioneer_UNSET;
		this->lastUpdatePosY = AvatarPioneer_UNSET;
		this->lastUpdatePosR = AvatarPioneer_UNSET;
		this->ariaSonarReadings.clear();
		this->ariaCon->setupRobot( this->robot );
		this->robot->asyncConnect();
	AR_UNLOCK

	// add AriaWake timeout
	this->AriaWakeTimeout = this->addTimeout( AvatarPioneer_ARIAWAKE_PERIOD, AvatarPioneer_CBR_cbAriaWake );

	STATE(AgentBase)->started = true;

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AvatarPioneer::stop() {

	this->removeTimeout( &this->AriaWakeTimeout );

	// stop robot thread
	this->robot->stopRunning( true );

	return AvatarBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AvatarPioneer::step() {
	
	return AvatarBase::step();
}

int AvatarPioneer::setAvatarState( bool ready ) {

	if ( ready ) {
	} else {
	}

	return AvatarBase::setAvatarState( ready );
}


int AvatarPioneer::addSonar( int ind, float x, float y, float r ) {
	UUID uuid;
	SonarPose sp;

	mapSonarId::iterator iter = this->sonarId.find( ind );
	if ( iter != this->sonarId.end() ) {
		return 0; // we already have this sonar
	}

	// initialize pose
	sp.x = x;
	sp.y = y;
	sp.r = r;
	sp.max = AvatarPioneer_SONAR_RANGE_MAX;
	sp.sigma = AvatarPioneer_SONAR_SIG_EST;
	sp.alpha = AvatarPioneer_SONAR_A_EST;
	sp.beta = AvatarPioneer_SONAR_B_EST;

	// generate UUID for sensor
	apb->apbUuidCreate( &uuid );

	// register with DDB
	this->ds.reset();
	this->ds.packUUID( &uuid );
	this->ds.packInt32( DDB_SENSOR_SONAR );
	this->ds.packUUID( &STATE(AvatarBase)->avatarUUID );
	this->ds.packUUID( &STATE(AvatarBase)->pfId );
	this->ds.packData( &sp, sizeof(SonarPose) );
	this->sendMessage( this->hostCon, MSG_DDB_ADDSENSOR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	this->sonarId[ind] = uuid;

	return 0;
}

int AvatarPioneer::setPos( float x, float y, float r, _timeb *tb ) {

	AR_LOCK
		this->ariaAbsoluteR = r;
		if ( this->ariaFrameRotation != AvatarPioneer_UNSET ) {
			this->ariaFrameRotation = r - (this->robot->getPose()).getThRad();	
		}
	AR_UNLOCK

	return AvatarBase::setPos( x, y, r, tb );
}

int AvatarPioneer::updateAriaPos() {
	
	float ariadx, ariady;
	float dx, dy, dr;
	float sn, cs;
	float forwardD, tangentialD;
	_timeb tb;

	AR_LOCK
		ariadx = (this->robot->getX()*0.001f - this->lastUpdatePosX);
		ariady = (this->robot->getY()*0.001f - this->lastUpdatePosY);

		sn = sin( this->ariaFrameRotation );
		cs = cos( this->ariaFrameRotation );

		dx = cs*ariadx - sn*ariady;
		dy = sn*ariadx + cs*ariady;
		dr = (this->ariaAbsoluteR - this->lastUpdatePosR);

		tb.time = this->ariaPoseTb.time;
		tb.millitm = this->ariaPoseTb.millitm;

		Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::updateAriaPos: robot x %f y %f r %f last x %f y %f r %f", this->robot->getX()*0.001f, this->robot->getY()*0.001f, this->ariaAbsoluteR, this->lastUpdatePosX, this->lastUpdatePosY, this->lastUpdatePosR );

		sn = sin( this->lastUpdatePosR );
		cs = cos( this->lastUpdatePosR );
		forwardD = dx*cs + dy*sn;
		tangentialD = dx*-sn + dy*cs;

		this->lastUpdatePosX = this->robot->getX()*0.001f;
		this->lastUpdatePosY = this->robot->getY()*0.001f;
		this->lastUpdatePosR = this->ariaAbsoluteR;
	AR_UNLOCK

	return AvatarBase::updatePos( forwardD, tangentialD, dr, &tb );
}


//-----------------------------------------------------------------------------
// Actions

int AvatarPioneer::nextAction() {
	ActionInfo *pAI;

	AvatarBase::nextAction(); // prepare for next action

	if ( !STATE(AvatarBase)->actionInProgress ) // no action to start
		return 0;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::nextAction: AA_WAIT start %.3f", *pAD );
			unsigned int millis = (unsigned int)((*pAD) * 1000);
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( millis, AvatarPioneer_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_MOVE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;

			AR_LOCK
				float D = *pAD;
				float vel = (float)this->robot->getVel()/1000;
				float velMax = (float)this->robot->getTransVelMax()/1000;
				float acc = (float)this->robot->getTransAccel()/1000;
				float dec = (float)this->robot->getTransDecel()/1000;
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

				Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::nextAction: AA_MOVE start %.3f est millis=%d", D, millis );

				this->robot->move( (int)(D*1000) );
			AR_UNLOCK

			apb->apb_ftime_s( &this->actionGiveupTime );
			this->actionGiveupTime.time += (this->actionGiveupTime.millitm + millis + AvatarPioneer_MOVE_FINISH_DELAY) / 1000;
			this->actionGiveupTime.millitm = (this->actionGiveupTime.millitm + millis + AvatarPioneer_MOVE_FINISH_DELAY) % 1000;
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarPioneer_MOVECHECK_PERIOD, AvatarPioneer_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	case AA_ROTATE:
		{
			float *pAD = (float *)getDynamicBuffer(*this->actionData.begin());
			unsigned short millis;

			AR_LOCK
				float D = RadiansToDegrees(*pAD);
				float vel = (float)this->robot->getRotVel();
				float velMax = (float)this->robot->getRotVelMax();
				float acc = (float)this->robot->getRotAccel();
				float dec = (float)this->robot->getRotDecel();
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

				Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::nextAction: AA_ROTATE start %.3f est millis=%d", *pAD, millis );
			
				//printf( "AvatarPioneer:nextAction: rotate current %f target %f delta %f", this->robot->getTh(), this->robot->getTh()+D, D );

				this->robot->setHeading( this->robot->getTh() + D );
			AR_UNLOCK

			apb->apb_ftime_s( &this->actionGiveupTime );
			this->actionGiveupTime.time += (this->actionGiveupTime.millitm + millis + AvatarPioneer_MOVE_FINISH_DELAY) / 1000;
			this->actionGiveupTime.millitm = (this->actionGiveupTime.millitm + millis + AvatarPioneer_MOVE_FINISH_DELAY) % 1000;
			if ( nilUUID == (STATE(AvatarBase)->actionTimeout = this->addTimeout( AvatarPioneer_MOVECHECK_PERIOD, AvatarPioneer_CBR_cbActionStep ) ) )
				return 1;
		}
		break;
	default:
		Log.log( 0, "AvatarPioneer::nextAction: unknown action type %d", pAI->action );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool AvatarPioneer::cbActionStep( void *vpdata ) {
	ActionInfo *pAI;
	bool done;
	_timeb tb;

	pAI = &(*this->actionQueue.begin());
	switch ( pAI->action ) {
	case AA_WAIT:
		Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::cbActionStep: AA_WAIT finished" );
		STATE(AvatarBase)->actionTimeout = nilUUID;
		this->nextAction();
		return 0;
	case AA_MOVE:
		AR_LOCK
			done = this->robot->isMoveDone();
		AR_UNLOCK
		
		this->updateAriaPos();
		
		if ( done ) {
			Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::cbActionStep: AA_MOVE finished" );
			STATE(AvatarBase)->actionTimeout = nilUUID;
			this->nextAction();
		} else {
			apb->apb_ftime_s( &tb );
			if ( tb.time < this->actionGiveupTime.time
			  || tb.time == this->actionGiveupTime.time && tb.millitm < this->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::cbActionStep: AA_MOVE failed!" );
			this->abortActions();
		}
		return 0;
	case AA_ROTATE:
		AR_LOCK
			done = this->robot->isHeadingDone();
		AR_UNLOCK
		
		this->updateAriaPos();
		
		if ( done ) {
			Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::cbActionStep: AA_ROTATE finished" );
			STATE(AvatarBase)->actionTimeout = nilUUID;
			this->nextAction();
		} else {
			apb->apb_ftime_s( &tb );
			if ( tb.time < this->actionGiveupTime.time
			  || tb.time == this->actionGiveupTime.time && tb.millitm < this->actionGiveupTime.millitm ) 
				return 1; // keep waiting
			// give up
			Log.log( LOG_LEVEL_VERBOSE, "AvatarPioneer::cbActionStep: AA_ROTATE failed!" );
			this->abortActions();
		}
		return 0;
	default:
		Log.log( 0, "AvatarPioneer::cbActionStep: unhandled action type %d", pAI->action );
	}

	return 0;
}

bool AvatarPioneer::cbAriaWake( void *na ) {
	std::list<int> events;

	AR_LOCK
		while ( !this->AriaEvent.empty() ) {
			events.push_back( this->AriaEvent.front() );
			this->AriaEvent.pop_front();
		}
	AR_UNLOCK

	while ( !events.empty() ) {

		switch ( events.front() ) {
		case AE_NONE:
			return 1;
		case AE_CONNECTED:
			{
				this->ariaConfigure();
				if ( !this->avatarReady ) 
					this->setAvatarState( true );
			}
			break;
		case AE_CONNECTIONFAILED:
			if ( this->avatarReady ) 
				this->setAvatarState( false );
			AR_LOCK
				this->robot->asyncConnect();
			AR_UNLOCK
			break;
		case AE_DISCONNECTED:
			if ( this->avatarReady ) 
				this->setAvatarState( false );
			AR_LOCK
				this->robot->asyncConnect();
			AR_UNLOCK
			break;
		case AE_SYNC: // process sonar queue
			{
				std::list<SonarReadingPlus> srp;
				AR_LOCK
					while ( !this->ariaSonarReadings.empty() ) {
						srp.push_back( this->ariaSonarReadings.front() );
						this->ariaSonarReadings.pop_front();
					}
				AR_UNLOCK
				while ( !srp.empty() ) {
					this->ds.reset();
					this->ds.packUUID( &this->sonarId[ srp.front().ind ] );
					this->ds.packData( &srp.front().t, sizeof(_timeb) );
					this->ds.packInt32( sizeof(SonarReading) );
					this->ds.packData( &srp.front().reading, sizeof(SonarReading) );
					this->ds.packInt32( 0 );
					this->sendMessage( this->hostCon, MSG_DDB_INSERTSENSORREADING, this->ds.stream(), this->ds.length() );
					this->ds.unlock();
					srp.pop_front();
				}
			}
			break;
		default:
			Log.log( 0, "AvatarPioneer::cbAriaWake: unhandled AriaEvent type %d", this->AriaEvent );
		}

		events.pop_front();
	}

	return 1;
}

void AvatarPioneer::cbAriaThreadConnected() { 
	// called from the aria thread, no need to lock
	AR_LOCK
	this->AriaEvent.push_back( AE_CONNECTED ); 
	AR_UNLOCK
}

void AvatarPioneer::cbAriaThreadConnectionFailed() { 
	// called from the aria thread, no need to lock
	AR_LOCK
	this->AriaEvent.push_back( AE_CONNECTIONFAILED );
	AR_UNLOCK
}

void AvatarPioneer::cbAriaThreadDisconnected() {
	// called from the aria thread, no need to lock
	AR_LOCK	
	this->AriaEvent.push_back( AE_DISCONNECTED );
	AR_UNLOCK
}

void AvatarPioneer::cbAriaThreadSyncTask() {
	// called from the aria thread, no need to lock
	// BUT don't try to do anything related to the main thread in here! (no messages or logs)
	AR_LOCK
	int i;
	_timeb tb;

	apb->apb_ftime_s( &tb );

	ArPose pose = this->robot->getPose();
	
	this->ariaPoseTb.time = tb.time;
	this->ariaPoseTb.millitm = tb.millitm;

	if ( this->ariaLastR == AvatarPioneer_UNSET ) { // this is the first rotation we have
		this->ariaLastR = pose.getThRad();
		this->ariaFrameRotation = this->ariaAbsoluteR - pose.getThRad();
		this->lastUpdatePosX = pose.getX()*0.001f;
		this->lastUpdatePosY = pose.getY()*0.001f;
		this->lastUpdatePosR = this->ariaAbsoluteR;
	} else {
		float dr = pose.getThRad() - this->ariaLastR;
		while ( dr > fM_PI ) dr -= fM_PI*2;
		while ( dr < -fM_PI ) dr += fM_PI*2;
		this->ariaAbsoluteR += dr;
		this->ariaLastR = pose.getThRad();
	}

	if ( !STATE(AgentBase)->started ) {
		AR_UNLOCK
		return;
	}

	//printf( "AvatarPioneer::cbAriaThreadSyncTask: pose %f %f %f (%f)\n", pose.getX()*0.001f, pose.getY()*0.001f, pose.getThRad(), pose.getTh() );
	
	//if ( !this->waiting ) { // only log sensors if we're not waiting
	SonarReadingPlus srp;
	srp.t = this->ariaPoseTb;
	for ( i=0; i<8; i++ ) {
		if ( this->robot->isSonarNew( i ) ) {
			srp.reading.value = this->robot->getSonarRange( i )*0.001f;
			if ( skipReading[i] == 0 ) {
				if ( srp.reading.value > AvatarPioneer_SONAR_RANGE_MIN 
				  && srp.reading.value < AvatarPioneer_SONAR_RANGE_MAX ) {
					srp.ind = i;
					this->ariaSonarReadings.push_back( srp );
					skipReading[i] = AvatarPioneer_SKIP_SONAR_COUNT;
				}
			} else {
				skipReading[i]--;
			}
		}
	}
	//}

	this->AriaEvent.push_back( AE_SYNC );
	
	AR_UNLOCK
}

//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AvatarPioneer *agent = (AvatarPioneer *)vpAgent;

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
	AvatarPioneer *agent = new AvatarPioneer( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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
	AvatarPioneer *agent = new AvatarPioneer( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
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

// CAvatarPioneerDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAvatarPioneerDLL, CWinApp)
END_MESSAGE_MAP()

CAvatarPioneerDLL::CAvatarPioneerDLL() {
	GetCurrentDirectory( 512, DLL_Directory );
}

// The one and only CAvatarPioneerDLL object
CAvatarPioneerDLL theApp;

int CAvatarPioneerDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAvatarPioneerDLL ---\n"));
  return CWinApp::ExitInstance();
}