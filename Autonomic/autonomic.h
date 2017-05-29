
#pragma once

#define AUTONOMIC_MAJOR_VERSION		0
#define AUTONOMIC_MINOR_VERSION		1
#define AUTONOMIC_SUB_VERSION		0

#define AgentBase_MAJOR 0
#define AgentBase_MINOR 1
#define AgentBase_BUILDNO 0
#define AgentBase_EXTEND 0
#define AgentBase_UUID "362ca30e-56fb-4fd8-b5f5-c19550e4b565"
#define AgentBase_PROCESS_COST 0.05, 0.05
#define AgentBase_TRANSFER_PENALTY 1
#define AgentBase_RESOURCE_REQUIREMENTS ""


//#define NO_LOGGING		//Shuts off logging for running machine learning experiments (would otherwise fill the disk)
#define NO_RANDOM_ERROR		//Shuts of random noise for sensor readings
//#define LOG_RESPONSES		//Log the recipients of all responses (for finding source of unknown conversation in AvatarSimulation)	//TODO - Fix this, causes a crash somewhere (reading uninitialized memory, most likely)
#define MAX_ITERATION_COUNT 500000	//Simulation ends after this many iterations, regardless of completion



#include <winsock2.h>
#include <ws2tcpip.h>

#include <sys/types.h>
#include <sys/timeb.h>

#include <rpc.h>
#include <list>
#include <map>

#include "dataStream.h"

#define _USE_MATH_DEFINES
#include "math.h"

#define fM_PI (float)M_PI
#define fM_PI_2 (float)M_PI_2
#define fM_PI_4 (float)M_PI_4

#define DEFAULT_PORT "50000"

#define MSGEX(namespc,msg) namespc::msg, (unsigned int *)namespc::MSG_SIZE, namespc::MSG_FIRST, namespc::MSG_LAST
#define MSGEXargs	unsigned char message, unsigned int *msg_size, unsigned int msg_first, unsigned int msg_last
#define MSGEXpassargs	message, msg_size, msg_first, msg_last


#define DEBUG_ATOMIC_MESSAGE 0
#define DEBUG_ATOMIC_MESSAGE_ID "A9024ED1-C31A-4290-AD6D-9F65D0D48A5C" //"8B2C3481-5734-4371-908B-322DD44E7E07"

#ifndef UUIDlessDefined
// UUIDless comparison function
class UUIDless {
public:
	bool operator()(const UUID& _Left, const UUID& _Right) const
		{	// apply operator< to operands
			RPC_STATUS Status;
			return UuidCompare( (UUID*)&_Left, (UUID*)&_Right, &Status ) < 0; 
		}
};
#define UUIDlessDefined
#endif

class _Callback {
public:
	virtual bool callback( void *data ) = 0;
	virtual bool callback( int evt, void *data ) = 0;

	bool staticCallback; // flags if this is a static callback or a member callback
};
class Callback : public _Callback {
	bool (*fA)( void *data );
	bool (*fB)( int evt, void *data );
public:
	Callback( bool (*fn)( void *data ) ) { fA = fn; fB = NULL; staticCallback = true; };
	Callback( bool (*fn)( int evt, void *data ) ) { fA = NULL; fB = fn; staticCallback = true; };
	bool callback( void *data = NULL ) { return (*fA)( data ); };
	bool callback( int evt, void *data = NULL ) { return (*fB)( evt, data ); };
};

#define DECLARE_CALLBACK_CLASS( ClassName ) \
class Callback : public _Callback { \
	ClassName *c; \
	bool (ClassName::*fA)( void *data ); \
	bool (ClassName::*fB)( int evt, void *data ); \
public: \
	Callback( ClassName *cls, bool (ClassName::*fn)( void *data ) ) { c = cls; fA = fn; fB = NULL; staticCallback = false; }; \
	Callback( ClassName *cls, bool (ClassName::*fn)( int evt, void *data ) ) { c = cls; fA = NULL; fB = fn; staticCallback = false; }; \
	bool callback( void *data = NULL ) { return (c->*fA)( data ); }; \
	bool callback( int evt, void *data = NULL ) { return (c->*fB)( evt, data ); }; \
};

#define NEW_STATIC_CB( Function )				new Callback( Function )
#define NEW_MEMBER_CB( ClassName, Function )	new ClassName::Callback( this, &ClassName::Function )

#define mapCallback		std::map<int, _Callback *>

#define CPU_USAGE_INTERVAL	5000

struct AddressPort {
	char address[256];
	char port[16];
	struct AddressPort * next;
};
#define sAddressPort struct AddressPort
#define spAddressPort struct AddressPort *

int apCmp( spAddressPort A, spAddressPort B );

enum CON_STATES {
	CON_STATE_DISCONNECTED = 0,
	CON_STATE_CONNECTED,
	CON_STATE_LISTENING,
	CON_STATE_WAITING
};

enum CON_GROUPS {
	CON_GROUP_READ = 0,
	CON_GROUP_WRITE,
	CON_GROUP_EXCEPT,
	CON_GROUP_PASSIVE, // this connection was initiated by another party
	CON_GROUP_COMMON
};
#define CON_GROUPF_READ			0x1 << CON_GROUP_READ
#define CON_GROUPF_WRITE		0x1 << CON_GROUP_WRITE
#define CON_GROUPF_EXCEPT		0x1 << CON_GROUP_EXCEPT
#define CON_GROUPF_PASSIVE		0x1 << CON_GROUP_PASSIVE

#define CON_INIT_BUFSIZE	256 
#define CON_MAX_WATCHERS	8
#define CON_RETRY_DELAY		500
#define CON_MAX_RELIABILITY_CHECKS 120
#define CON_RELIABILITY_INTERVAL	5000
#define CON_MAX_LATENCY_CHECKS	10

enum CON_EVT {
	CON_EVT_STATE = 0, // state changed
	CON_EVT_CLOSED,    // connection closed
	CON_EVT_REMOVED    // watcher removed
};

#define FD_MESSAGE_SAMPLES	40
#define FD_INITIAL_PERIOD	250 // ms, small so that we get enough samples fairly quickly to estimate a proper period
#define FD_EVALUATE_QOS		10	// evaluate QOS every N messages

struct FDSubject {
	unsigned int count; // current message count
	unsigned int period; // current period in ms
	unsigned int nextPeriod; // next period to use

	UUID timeout; // timeout index
};

#define FD_DEFAULT_TDu	 (30*1000) // ms (30 seconds)
#define FD_DEFAULT_TMRL	 (7*24*60*60*1000) // ms (once a week)
#define FD_DEFAULT_TMU   (2*60*1000) // ms (2 minutes)

struct FDObserver {
	int status; // connection status

	unsigned int TDu; // upper bound on detection time relative to average message delay, ms
	unsigned int TMRL; // lower bound on average mistake recurrence time
	unsigned int TMU; // upper bound on average mistake duration

	unsigned int l; // count of last message received
	unsigned int d; // count of skipped messages
	_timeb Sl; // send time of last message received
	std::list<int> D; // up to FD_MESSAGE_SAMPLES most recent time differences (arrival time - send time) in ms, note: it doesn't matter if subject and observer clocks are not sync'd
	int sumD; // sum of D entries
	int r; // count until next QOS evaluation

	UUID timeout; // timeout index
};

struct ConnectionStatistics {
	bool  connected;	// currently connected
	short latency;		// average latency (one way!)
	char  reliability;	// reliability measure
};
#define sConnectionStatistics struct ConnectionStatistics
#define spConnectionStatistics struct ConnectionStatistics *

typedef std::map<UUID, spConnectionStatistics, UUIDless> mapConnectionStatistics;

struct Connection {
	bool markedForDeletion; // don't delete until we are sure all code is done with this connection
	SOCKET socket;
	sAddressPort ap; // address/port
	int	   protocol; // connection protocol
	UUID   uuid;     // the uuid of the agent at the other end, all zeros means it is unset
	bool   raw;		 // connection is in raw mode, NOTE: raw streams are responsible for making sure there is enough buffer space at all times
	sConnectionStatistics statistics; // connection statistics
	int	   maxRetries; // maximum retries
	int    retries; // current retries
	char   state; // Connection state
	char   reliabilityCheck[CON_MAX_RELIABILITY_CHECKS]; // results of the reliability check
	char   reliabilityInd; // current index for reliabilityCheck
	int    latencyCheck[CON_MAX_LATENCY_CHECKS]; // results of the latency check
	char   latencyInd; // current index for latencyCheck
	_timeb latencyBase; // used to shift latency check times
	char   groups; // Group membership flags
	_timeb recvTime; // Time that most recent data was added to the buffer
	char   *buf;
	int    bufStart; // Current buffer offset
	int    bufLen; // used length of buf
	int	   bufMax; // allocated length of buf
	unsigned char   message; // Current message
	int    waitingForData;
	char   *sendBuf; // Pointer to send buffer for large messages
	int    sendBufLen; // used length of the sendBuf
	int	   sendBufMax; // allocated length of the sendBuf
	int	   watchers[CON_MAX_WATCHERS]; // callback refs watchers
	UUID index; // connection index 

	// failure detection
	FDSubject *subject; 
	FDObserver *observer; 

	// debugging
	unsigned char lastMessage;
	int lastMsgSize;
};
#define sConnection struct Connection
#define spConnection struct Connection *

typedef std::map<UUID, spConnection, UUIDless> mapConnection;

struct TimeoutEvent {
	UUID id;
	unsigned int period; // in ms   
	_timeb timeout;		
	int cbRef;
	struct TimeoutEvent * next;
	void *data;			
	int dataLen;

	bool stateSafe; // should the timeout be packed into the agent state
};
#define sTimeoutEvent struct TimeoutEvent
#define spTimeoutEvent struct TimeoutEvent *


struct Conversation {
	UUID thread;
	UUID timeout;
	int line;
	int cbRef;
	void *data;
	int dataLen;
	spConnection con; // last connection to respond in this conversation
	char *response;
	int responseLen;
	bool expired;
};
#define sConversation struct Conversation
#define spConversation struct Conversation *

typedef std::map<UUID, spConversation, UUIDless> mapConversation;

#define CONVERSATION_ERASE_DELAY	(1000*60*5) // 5 minutes

struct DelayedMessage {
	int type;
	UUID conId;
	UUID toId; // who is the connection to
	unsigned char message;
	UUID bufRef; // state buffer ref
	unsigned int len; 
	UUID forwardAddress; 
	UUID returnAddress;
	unsigned int *msg_size;
	unsigned int msg_first;
	unsigned int msg_last;
};
#define sDelayedMessage struct DelayedMessage
#define spDelayedMessage struct DelayedMessage *

enum DelayedMessageType {
	DMT_NORMAL = 0,
	DMT_GROUP,
	DMT_EX,
	DMT_EX_GROUP,
};

#define REQUESTAGENTSPAWN_TIMEOUT	(1000*60) // 1 minute

#define ALLOCATE_STATE( thisClass, parentClass ) \
	thisClass::State *newState = (thisClass::State*)malloc( sizeof(thisClass::State) ); \
	if ( newState ) memcpy( newState, this->state, sizeof(parentClass::State) ); \
	free( this->state ); \
	this->state = (AgentBase::State*)newState;

#define STATE(className) ((className::State*)this->state)

enum AGENTTYPE {
	AGENTTYPE_DLL = 0,
	AGENTTYPE_EXE
};

#define AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS 8

struct AgentTemplate {
	char name[32];
	unsigned char verMajor;
	unsigned char verMinor;
	unsigned short verBuildNo;
	unsigned char verExtend;
	UUID uuid;
	char type;
	char object[32];
	char debugmode[32];
	float processCost;
	float transferPenalty;
	UUID resourceRequirements[AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS];
	void *vp; // used to keep track of template data, e.g. for DLLs it is an HINSTANCE of the library 
};
#define sAgentTemplate struct AgentTemplate
#define spAgentTemplate struct AgentTemplate *

typedef std::map<UUID, spAgentTemplate, UUIDless> mapAgentTemplate;
typedef std::map<UUID, short, UUIDless> mapAgentTemplateInstances;


typedef int (*SpawnFuncPtr)( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

struct AgentType {
	char name[256];
	UUID uuid;
	char instance;
};

struct StateBuf {
	unsigned int size;
	void *buf;
};

typedef std::map<UUID, StateBuf, UUIDless> mapStateBuf;

enum AtomicMessage_PHASE {
	AM_PHASE_NULL = 0,
	AM_PHASE_P1,
	AM_PHASE_C1,
	AM_PHASE_P2,
	AM_PHASE_C2,
};

struct AtomicMessage_MSG {
	UUID q;
	int order;
	int round;
	char estCommit;
	int timestamp;
	char ack;
};

struct AtomicMessage {
	UUID id;
	bool placeholder; // is this a placeholder or a fully defined message?
	bool orderedMsg; // is this message ordered or not?
	UUID queue; // what queue is this message part of?
//	bool targetsSuspect; // are any of the targets suspected

	std::list<UUID> targets;
	int f; // number of supported failures

	// message to deliver if decision is commit
	unsigned char msg;
	unsigned int len;
	UUID dataRef;

	// callback on decide evt = 1/0 (commit/abort), data = msgid
	int cbRef;

	int retries; // number of retries on this transaction

/*	// message to deliver if decision is abort (only valid for unordered commits)
	unsigned char msgAbort;
	unsigned int lenAbort;
	UUID dataAbortRef;*/

	// local
	UUID timeout;

	char phase;
	char decided; // state
	char delivered; // decision has been handled, waiting for cleanup
	char estCommit; // estimate
	int order; // current order
	int round;
	int timestamp;

	UUID coord; // current coord
	std::list<UUID> suspects;
	std::list<UUID> permanentSuspects;	
	std::list<UUID> decisions;
	std::map<int,std::list<AtomicMessage_MSG>> msgs;
	std::map<int, AtomicMessage_MSG> proposals;
	std::map<int,std::list<AtomicMessage_MSG>> acks;

	// statistics
	UUID initiator;
	int msgsSent; // counts only locally sent messages, sum over all hosts to get total
	int dataSent; // counts only locally sent data, sum over all hosts to get total
	int orderChanges; // accepted a new order, potentially different on all hosts
	_timeb startT; // time message was initiated/first received
	_timeb decidedT; // time message was resolved
	_timeb deliveredT; // time message was delived (if applicable)
};

typedef std::map<UUID, AtomicMessage, UUIDless> mapAtomicMessage;

struct LocalMessage {
	unsigned char message;
	UUID dataRef;
	unsigned int len;
};

#define DDB_NOTIFICATION_FILTER_EXPIRY 60000 // ms

enum CONNECTION_STATUS {
	CS_NO_CONNECTION = -1,
	CS_TRUSTED,
	CS_SUSPECT,
};

enum SLAM_MODE {
	SLAM_MODE_JCSLAM = 0,
	SLAM_MODE_DISCARD,
	SLAM_MODE_DELAY,
};

#include "DDB.h"
#include "agentPlayback.h"
#include "Logger.h"

#include "autonomicVersion.h"


class AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		_timeb lastStub;  // last time the stub was updated
		_timeb lastState; // last time the state was updated

		// stub data
		UUID uuid;
		UUID parentId;

		// state data
		AgentType agentType; // agent type

		char missionFile[256]; // mission file name

		bool configured;	// configuration has been run
		bool started;		// agent has been started
		bool stopFlag;		// stop if true
		bool gracefulExit;  // flag for graceful exit
		bool simulateCrash; // flag to simulate crash

		bool noCrash;
		float stabilityTimeMin;
		float stabilityTimeMax;

		char haveReturn;    // is the return address valid?
		UUID returnAddress; // return address of the last forwarded message (if available)

		int conversationLast;

		UUID localMessageTimeout;
	};

protected:
	State *state;		// stub and state data
	
	// dynamic state buffers (these can be used to store blocks of state data that might need to be referenced by multiple pointers)
	mapStateBuf stateBuffers;

	// local messages
	std::list<LocalMessage> localMessageQueue;

	// atomic messages
	mapAtomicMessage atomicMsgs;
	std::map<UUID, int, UUIDless> atomicMessageHighestOrder; // highest order of an atomic message we are participating in, indexed by queue
	std::map<UUID, std::list<AtomicMessage>, UUIDless> atomicMsgsOrderedQueue; // maintain local order by queuing ordered messages and only sending them once the previous is finished

	// timeouts
	spTimeoutEvent firstTimeout;

	// conversations
	mapConversation conversations;

	// delayed messages
	std::map<UUID, char, UUIDless> delayedMessageBufRefs; // map of buf refs by ref id

	// duplicate notification filter
	std::map<UUID, char, UUIDless> notificationFilter; // DEPRECIATED filters out duplicate DDB notifications (since the distributed algorithm doesn't guarantee no duplicates)

//-----------------------------------------------------------------------------
// Non-state member variables

protected:
	Logger Log;	// log class

	int logLevel;
	char logDirectory[512];  // current log directory

	UUID agentplaybackUUID; // id for the playback file
	AgentPlayback *apb; // agent playback handler
	
	DataStream ds; // shared DataStream object, for temporary, single thread, use only!

	struct fd_set group[8];   // groups

	struct addrinfo *addr_result,
		            *addr_ptr,
					addr_hints;
	
	struct timeval timeVal;

	UUID nilUUID; // used to check if uuids are nil

	int	CON_GROUP_HIGH;

	bool frozen;	    // are we frozen? used to determine clean up behaviour
	UUID thawTicket;

	bool isHost;		// are we a host?

	sAddressPort hostAP;
	spConnection hostCon;
	
	mapConnection connection; // connections

	mapCallback	callback; // map of callback pointers by callbackRef

	UUID _atomicMessageDeliveryId; // id of the atomic message currently being delivered

	_timeb			cpuUsageLastUpdate;
	FILETIME		cpuUsageKernalStart;
	FILETIME		cpuUsageUserStart;
	unsigned int	cpuUsageTotalUsed;
	unsigned int	cpuUsageTotalElapsed;

	bool			recoveryInProgress; 
	UUID			recoveryTicket;
	std::list<UUID> recoveryLocks; // list of locks we must clear before recovery is complete
	UUID			AgentBase_recoveryLock; // id for the AgentBase recovery lock
	

//-----------------------------------------------------------------------------
// Functions

public:
	AgentBase( spAddressPort ap, UUID *agentId, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentBase();
	virtual int	  simulateCrash();

	int		      getPlaybackMode() { return this->apb->getPlaybackMode(); };

	virtual int   configure();	// initial configuration
	int			  setParentId( UUID *id ); // set parent id
	virtual int   setInstance( char instance ); // instance specific parameters
	int			  calcLifeExpectancy(); // set our crash time


	virtual int   parseMF_HandleOptions( int SLAMmode ) { return 0; };			// override these functions to handle mission information that is relevant to each agent
	virtual int   parseMF_HandleStability( float timeMin, float timeMax );
	virtual int   parseMF_HandleAgent( AgentType *agentType ) { return 0; };			
	virtual int   parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode ) { return 0; };
	virtual int   parseMF_HandleMissionRegion( DDBRegion *region ) { return 0; };
	virtual int   parseMF_HandleForbiddenRegion( DDBRegion *region ) { return 0; };
	virtual int   parseMF_HandleCollectionRegion( DDBRegion *region ) { return 0; };
	virtual int   parseMF_HandleTravelTarget( float x, float y, float r, bool useRotation ) { return 0; };
	virtual int   parseMF_HandleLandmarkFile( char *fileName ) { return 0; };
	virtual int   parseMF_HandlePathFile( char *fileName ) { return 0; };
	virtual int   parseMF_HandleOfflineSLAM( int SLAMmode, int particleNum, float readingProcessingRate, int processingSlots, char *logPath ) { return 0; };
	virtual int   parseMF_HandleLearning(bool individualLearning, bool teamLearning) { return 0; };
	virtual int   parseMF_HandleTeamLearning(bool teamLearning) { return 0; };

	virtual int	  parseMF_HandleMapOptions(bool mapReveal, bool mapRandom) { return 0; }
	virtual int	  doMapReveal() { return 0; }

	int			  parseMF_Agent( FILE *fp, AgentType *agentType );
	int			  parseMissionFile( char *misFile );


	virtual int   start( char *missionFile );	// start agent
	virtual int   stop();			// stop agent
	int			  prepareStop();			// stop agent at next step
	virtual int   step();			// run one step

	bool		  isStarted() { return this->state->started; }; // public access to check if the agent has started
	UUID *		  getUUID() { return &STATE(AgentBase)->uuid; };
	UUID *		  getParentId() { return &STATE(AgentBase)->parentId; };

	UUID		  newDynamicBuffer( unsigned int size ); // create a dynamic state buffer, used to facilitate state transfer for dynamic buffers
	int			  freeDynamicBuffer( UUID ref );
	void *		  getDynamicBuffer( UUID ref );

	spConnection  openConnection( spAddressPort ap, unsigned int groups = NULL, int retries = 0, int protocol = IPPROTO_TCP );
	int           closeConnection( spConnection con, bool force = true );
	spConnection  getConnection( spAddressPort ap );
	int			  connectionStatus( UUID uuid ); // get the connection status for an agent

	void		  setConnectionMaxRetries( spConnection con, int retries );
	void		  resetConnectionRetries( spConnection con );
	int			  watchConnection( spConnection con, int cbRef );
	int			  stopWatchingConnection( spConnection con, int watcher );

	int			  initializeConnectionFailureDetection( spConnection con ); // call from the subject to start detection
	int			  setConnectionfailureDetectionQOS( spConnection con, unsigned int TDu = FD_DEFAULT_TDu, unsigned int TMRL = FD_DEFAULT_TMRL, unsigned int TMU = FD_DEFAULT_TMU ); // call from observer to set QOS parameters
	virtual int	  connectionStatusChanged( spConnection con, int status );

	spConnection  openListener( spAddressPort ap );
	int           closeListener( spConnection con );

	int			  sendRaw( spConnection con, char *data, unsigned int len );
	int			  sendMessage( spConnection con, unsigned char message, UUID *forwardAddress, UUID *returnAddress = NULL );
	int			  sendMessage( spConnection con, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	int			  sendMessage( UUID *group, unsigned char message, char *data = NULL, unsigned int len = 0 );
	int			  sendMessageEx( spConnection con, MSGEXargs, UUID *forwardAddress, UUID *returnAddress = NULL );
	int			  sendMessageEx( spConnection con, MSGEXargs, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	int			  sendMessageEx( UUID *group, MSGEXargs, char *data = NULL, unsigned int len = 0 );
	int			  replyLastMessage( spConnection con, unsigned char message, UUID *returnAddress );
	int			  replyLastMessage( spConnection con, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *returnAddress = NULL );
	int			  replyLastMessageEx( spConnection con, MSGEXargs, UUID *returnAddress );
	int			  replyLastMessageEx( spConnection con, MSGEXargs, char *data = NULL, unsigned int len = 0, UUID *returnAddress = NULL );
	UUID		  delayMessage( unsigned int delay, spConnection con, unsigned char message, UUID *forwardAddress, UUID *returnAddress = NULL );
	UUID		  delayMessage( unsigned int delay, spConnection con, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	UUID		  delayMessage( unsigned int delay, UUID *group, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	UUID		  delayMessageEx( unsigned int delay, spConnection con, MSGEXargs, UUID *forwardAddress, UUID *returnAddress = NULL );
	UUID		  delayMessageEx( unsigned int delay, spConnection con, MSGEXargs, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	UUID		  delayMessageEx( unsigned int delay, UUID *group, MSGEXargs, char *data = NULL, unsigned int len = 0, UUID *forwardAddress = NULL, UUID *returnAddress = NULL );
	int			  delayMessageAbort( UUID *messageId );
	UUID		  _delayMessage( int type, unsigned int delay, UUID *conId, unsigned char message, char *data, unsigned int len, UUID *forwardAddress, UUID *returnAddress, unsigned int *msg_size, unsigned int msg_first, unsigned int msg_last );
	int			  _queueLocalMessage( unsigned char message, char *data, unsigned int len ); // deliver message to ourselves after the message processing phase

	virtual int	  sendAgentMessage( UUID *agent, unsigned char message, char *data = NULL, unsigned int len = 0 );

	// atomic commit algo adapted from "Non Blocking Atomic Commitment with an Unreliable Failure Detector" Guerraoui et al.
	// NOTE: only hosts can participate in atomic messages!
	void		  atomicMessageInit( AtomicMessage *am ); // called to initialize the struct only, relevant data is set manually
	UUID		  atomicMessage( std::list<UUID> *targets, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *ticket = NULL, int cbRef = 0, unsigned char msgAbort = -1, char *dataRejected = NULL, unsigned int lenAbort = 0 );
	UUID		  atomicMessageOrdered( UUID *queue, std::list<UUID> *targets, unsigned char message, char *data = NULL, unsigned int len = 0, UUID *ticket = NULL, int cbRef = 0 );
	UUID		  atomicMessageRetry( UUID *id, std::list<UUID> *targets = NULL ); // retry transaction
	virtual int	  atomicMessageEvaluate( UUID *id, unsigned char message, char *data = NULL, unsigned int len = 0 );
	virtual int	  atomicMessageDecided( char commit, UUID *id );
	UUID		  atomicMessageDeliveryId() { return this->_atomicMessageDeliveryId; }; // return the id of the message being delivered
	virtual int	  _atomicMessageSendOrdered( UUID *queue ); // send next ordered message
	int			  _receiveAtomicMessage( DataStream *ds );
	int			  _atomicMessageCheckOrder( UUID *id, UUID *queue, int order );
	int			  _atomicMessageTask2( AtomicMessage *am );
	int			  _atomicMessageStepC1( UUID *id, UUID *q = NULL, int orderq = 0, int roundq = 0, char estCommitq = false, int timestampq = 0 );
	int			  _atomicMessageStepP2( UUID *id, UUID *coord = NULL, int orderc = 0, int roundc = 0, char estCommitc = false );
	int			  _atomicMessageStepC2( UUID *id, UUID *q, int orderq, int roundq, char ackq );
	int			  _atomicMessageDecide( UUID *id, UUID *q, int orderq, int roundq, char commitq );
	int			  _atomicMessageAttemptCommit( UUID queue );
	int			  _atomicMessageTrust( UUID *id, UUID *suspect );
	int			  _atomicMessageSuspect( UUID *id, UUID *suspect );
	int			  _atomicMessagePermanentlySuspect( UUID *id, UUID *suspect );
	int			  _atomicMessageCheckCleanup( AtomicMessage *am, bool force = false );
	//int			  _removeAtomicMessage( UUID *id, bool expired = false );

	virtual int   ddbNotification( char *data, int len ) { return 0; };

	int			  agentLostNotification( UUID *agent );

	UUID		  addTimeout( unsigned int period, int cbRef, void *data = NULL, int dataLen = 0, bool stateSafe = true );
	int			  removeTimeout( UUID *id );
	int			  readTimeout( UUID *id );
	void		  resetTimeout( UUID *id );

	UUID		  conversationInitiate( int cbRef, unsigned int period, void *data = NULL, int len = 0 );
	int			  conversationResponse( UUID *thread, spConnection con, char *response, int len );
	int			  conversationFinish( UUID *thread ); // we don't want to get any more responses in this conversation

protected:
	int			  _conversationEnd( spConversation conv, bool immediate = false );

	float		  getCpuUsage(); // get average cpu usage

	int			  retryConnection( spConnection con ); // retry connection after CON_RETRY_DELAY ms
	int			  finishConnection( spConnection con ); // waiting connection was connected
	spConnection  acceptConnection( spConnection listener, unsigned int groups ); // accept connection from listener

	int 		  conInit( spConnection con, spAddressPort ap, int protocol = IPPROTO_TCP, int maxRetries = 0 );
	int			  conConnect( spConnection con, int ai_protocol = IPPROTO_TCP );
	int			  conSetState( spConnection con, unsigned int state );
	int			  conGroupAddTest( spConnection, unsigned int group ); // test adding to group without performing transaction
	int			  conGroupAdd( spConnection, unsigned int group );
	int			  conGroupRemove( spConnection, unsigned int group );
	int			  conAdd( spConnection con );
	spConnection  conRemove( spConnection con );
	spConnection  conRemove( UUID *conIndex );
	int			  conDelete( spConnection con );

	int			  conCheckBufSize( spConnection con );
	int			  conReceiveData( spConnection con );
	virtual int	  conProcessRaw( spConnection con ); // process raw data from stream, subclasses are responsible for making sure data gets processed and buffer is cleared
	int			  conProcessStream( spConnection con );
	virtual int	  conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );
	int			  conIncreaseSendBuffer( spConnection con, unsigned int minSize );
	int			  conSendMessage( spConnection con, unsigned char message, char *data, unsigned int len, unsigned int msgSize, char *header = NULL, unsigned int headerLen = 0 );
	int			  conSendBuffer( spConnection con );
	int			  conSendBuffer( spConnection con, char *buf, unsigned int bufLen );

	int			  conStartLatencyCheck( spConnection con, _timeb *tb = NULL );
	int			  conFinishLatencyCheck( spConnection con, int ticket, _timeb *tb = NULL );

	int			  conFDAlive( spConnection con, unsigned int count, unsigned int period, _timeb *sendTime );
	int			  conFDQOS( spConnection con, unsigned int newPeriod );
	int			  conFDEvaluateQOS( spConnection con );
	virtual int	  conFDSuspect( spConnection con );

private:
	void		  checkTimeouts();
	
public:
	
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentBase )

	// Enumerate callback references
	enum CallbackRef {
		CBR_INVALID = 0,
		AgentBase_CBR_cbSimulateCrash,
		AgentBase_CBR_cbUpdateCpuUsage,
		AgentBase_CBR_cbUpdateConnectionReliability,
		AgentBase_CBR_cbDoRetry,
		AgentBase_CBR_cbConversationTimeout,
		AgentBase_CBR_cbConversationErase,
		AgentBase_CBR_cbDelayedMessage,
		AgentBase_CBR_cbFDSubject,
		AgentBase_CBR_cbFDObserver,
		AgentBase_CBR_cbWatchHostConnection,
		AgentBase_CBR_cbAtomicMessageTimeout,
		AgentBase_CBR_cbLocalMessage,
		AgentBase_CBR_cbNotificationFilterExpiry,
		AgentBase_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool	cbSimulateCrash( void *NA ) { this->simulateCrash(); return 0; };
	bool	cbUpdateCpuUsage( void *NA );
	
	bool	cbUpdateConnectionReliability( void *NA );
	bool	cbDoRetry( void *vpindex );

	bool	cbConversationTimeout( void *vpthread );
	bool	cbConversationErase( void *vpthread );

	bool	cbDelayedMessage( void *vpDelayedMessage );
	
	bool	cbFDSubject( void *vpindex );
	bool	cbFDObserver( void *vpindex );

	bool	cbWatchHostConnection( int evt, void *vpcon );

	bool	cbAtomicMessageTimeout( void *vpid );

	bool	cbLocalMessage( void *NA );

	bool	cbNotificationFilterExpiry( void *vpkey );

protected:

	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );
	virtual int	  resume( DataStream *ds );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	virtual int   backup();
	virtual int   recover( DataStream *ds );
	virtual int	  recoveryFinish();
	int			  recoveryFailed();
	int			  recoveryCheck( UUID *key ); // release lock and check if recovery is ready to finish

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream

};

#define _WRITE_STATE(classname)	ds->packData( this->state, sizeof(classname::State) );
#define _READ_STATE(classname)	memcpy_s( this->state, sizeof(classname::State), ds->unpackData( sizeof(classname::State) ), sizeof(classname::State) );

#define _WRITE_STATE_LIST( type, listPtr ) \
{ \
	std::list<type>::iterator it; \
	for ( it = (listPtr)->begin(); it != (listPtr)->end(); it++ ) { \
		ds->packBool( 1 ); \
		ds->packData( (void *)&*it, sizeof(type) ); \
	} \
	ds->packBool( 0 ); \
}

#define _READ_STATE_VECTOR( type, vectorPtr ) \
{ \
	type var; \
	while ( ds->unpackBool() ) { \
		var = *(type *)ds->unpackData(sizeof(type)); \
		(vectorPtr)->push_back( var ); \
	} \
}

#define _WRITE_STATE_VECTOR( type, vectorPtr ) \
{ \
	std::vector<type>::iterator it; \
	for ( it = (vectorPtr)->begin(); it != (vectorPtr)->end(); it++ ) { \
		ds->packBool( 1 ); \
		ds->packData( (void *)&*it, sizeof(type) ); \
	} \
	ds->packBool( 0 ); \
}

#define _READ_STATE_LIST( type, listPtr ) \
{ \
	type var; \
	while ( ds->unpackBool() ) { \
		var = *(type *)ds->unpackData(sizeof(type)); \
		(listPtr)->push_back( var ); \
	} \
}

#define _WRITE_STATE_MAP( keyType, varType, mapPtr ) \
{ \
	std::map<keyType,varType>::iterator it; \
	for ( it = (mapPtr)->begin(); it != (mapPtr)->end(); it++ ) { \
		ds->packBool( 1 ); \
		ds->packData( (void *)&it->first, sizeof(keyType) ); \
		ds->packData( (void *)&it->second, sizeof(varType) ); \
	} \
	ds->packBool( 0 ); \
}

#define _WRITE_STATE_MAP_LESS( keyType, varType, less, mapPtr ) \
{ \
	std::map<keyType,varType,less>::iterator it; \
	for ( it = (mapPtr)->begin(); it != (mapPtr)->end(); it++ ) { \
		ds->packBool( 1 ); \
		ds->packData( (void *)&it->first, sizeof(keyType) ); \
		ds->packData( (void *)&it->second, sizeof(varType) ); \
	} \
	ds->packBool( 0 ); \
}

#define _READ_STATE_MAP( keyType, varType, mapPtr ) \
{ \
	keyType key; \
	varType var; \
	while ( ds->unpackBool() ) { \
		key = *(keyType *)ds->unpackData( sizeof(keyType) ); \
		var = *(varType *)ds->unpackData( sizeof(varType) ); \
		(*mapPtr)[key] = var; \
	} \
}


// old unused state functions
int AgentBase_writeStub( AgentBase::State * state, DataStream * ds, bool climb = true );
int AgentBase_readStub( AgentBase::State * state, DataStream * ds, unsigned int age, bool climb = true );
int AgentBase_writeState( AgentBase::State * state, DataStream * ds );
int AgentBase_readState( AgentBase::State * state, DataStream * ds, unsigned int age );

// note: these functions are used to create state objects for other agents, not for yourself
AgentBase::State * AgentBase_CreateState( UUID *uuid, int size = sizeof(AgentBase::State) );
void			   AgentBase_DeleteState( AgentBase::State *state, AgentBase *owner = NULL );