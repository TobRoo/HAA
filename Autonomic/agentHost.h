
#include <list>
using namespace std;

#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\DDBStore.h"

#include <vector>

//#define PLAYBACKMODE_DEFAULT PLAYBACKMODE_RECORD
#define PLAYBACKMODE_DEFAULT PLAYBACKMODE_OFF

#define OAC_GLOBAL					"6263b830-1de8-41a6-bc3d-1d22dfd750c3" // OAC queue for global state

#define HWRESOURCE_NO_CRASH			"2e82fead-0c05-45c7-9df5-d266c711c325" // host never crashes or exits
#define HWRESOURCE_GATHER_DATA		"ed4f8dbf-91aa-4f6d-af6b-ed16068b0f5b" // host is in charge of gathering data
#define HWRESOURCE_EXCLUSIVE		"67cf20af-a3c9-43eb-818d-96427a8ae6be" // host exclusively runs agents with this flag (e.g. ExecutiveSimualtion)
#define HWRESOURCE_AVATAR_PIONEER	"36ed6631-c465-433c-9a1c-9579b13676ae" // host is connected to the pioneer serial modem
#define HWRESOURCE_AVATAR_ER1_0		"0a89d5f7-3eea-47dc-abe9-ce25c7e93e21" // host is connected to the ER1-0 avatar
#define HWRESOURCE_AVATAR_ER1_1		"97446728-6fee-4d60-9af6-20bb03a2a902" // host is connected to the ER1-1 avatar

#define GM_FORMATION_TIMEOUT	20000 // ms

#define DATA_GATHER_PERIOD		10000 // ms

#define PFRESAMPLE_TIMEOUT		10000 // ms

// TEMP
#define CLUSTERSIZE 1

#define CLEAN_EXIT_INTERVAL  10 // ms

//#define TIMEOUT_REQUESTAGENTSPAWNPROPOSALS				 200000 // ms
//#define TIMEOUT_AGENTSPAWNPROPOSAL						1000000 // ms
//#define TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_GOOD			   50 // ms
//#define TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_ACCEPTABLE	  100 // ms
//#define TIMEOUT_ACCEPTAGENTSPAWNPROPOSAL				 200000 // ms, MUST BE BIGGER THAN TIMEOUT_SPAWNAGENT
#define	TIMEOUT_SPAWNAGENT								 100000 // ms, MUST BE SMALLER THAN TIMEOUT_ACCEPTAGENTSPAWNPROPOSAL

#define MAX_PORTS	199 // our local port + this amount should be reserved by the system

#define DDB_EFFECTIVEPARTICLENUM_THRESHOLD  0.3f // fraction of total particles

struct AgentSpawnProposal {
	spConnection con; // connection to the proposing host, NULL indicates local host
	// TEMP
	float favourable; // (0,1) weight whether the proposal is favourable
};
#define sAgentSpawnProposal struct AgentSpawnProposal
#define spAgentSpawnProposal struct AgentSpawnProposal *
typedef map<UUID, spAgentSpawnProposal, UUIDless> mapAgentSpawnProposal;

struct AgentSpawnRequest {
	spConnection con; // connection to the requesting host, NULL indicates local host
	UUID ticket; // spawn ticket
	AgentType type; // agent type
	unsigned int timeout; // expiry timeout
};
#define sAgentSpawnRequest struct AgentSpawnRequest
#define spAgentSpawnRequest struct AgentSpawnRequest *
typedef map<UUID, spAgentSpawnRequest, UUIDless> mapAgentSpawnRequest;

struct AgentSpawnProposalEvaluator {
	UUID ticket; // spawn ticket
	AgentType type; // type
	spConnection con; // connection to initiator, NULL indicates it was initiated by the host
	UUID thread; // conversation thread
	char openRFPs; // tracks the number of open RFPs
	list<spAgentSpawnProposal> *proposals; // list of proposals
	spConnection accepting; // currently accepting a proposal from this host
	unsigned int requestTimeout;
	unsigned int evaluateTimeout;
	unsigned int acceptTimeout;
};
#define sAgentSpawnProposalEvaluator struct AgentSpawnProposalEvaluator
#define spAgentSpawnProposalEvaluator struct AgentSpawnProposalEvaluator *
typedef map<UUID, spAgentSpawnProposalEvaluator, UUIDless> mapAgentSpawnProposalEvaluator;

enum SPAWN_TYPE {
	SPAWN_UNIQUE = 0,
	SPAWN_NORMAL,
	SPAWN_RESPAWN,
};
typedef map<UUID, char, UUIDless> mapSpawnType;

#define FD_HOST_TDu	  (10*1000) // ms (10 seconds)
#define FD_HOST_TMRL  (7*24*60*60*1000) // ms (once a week)
#define FD_HOST_TMU   (30*1000) // ms (30 seconds)

#define FD_AGENT_TDu   (10*1000) // ms (10 seconds)
#define FD_AGENT_TMRL  (7*24*60*60*1000) // ms (once a week)
#define FD_AGENT_TMU   (30*1000) // ms (30 seconds)

struct AgentAffinityBlock {
	UUID timeout;
	unsigned int size;
};

#define AGENTAFFINITY_CURBLOCK_TIMEOUT	5000	// ms

#define AGENTAFFINITY_NORMALIZE		(0.01f/150)	// data rate of 150 bytes/second is worth a percent of capacity point 
#define AGENTAFFINITY_THRESHOLD		0.01f // affinities must be worth a percent of capacity to be worth considering

struct AgentInfo {
	// UUID host;
	AgentType type;
	int activationMode; 
	char priority;

	UUID spawnThread;

	// local
	spConnection con;
	int watcher;
	
	UUID spawnTimeout;

	map<UUID, AgentAffinityBlock, UUIDless> curAffinityBlock;

	int shellStatus; // status when shell is spawning
	spConnection shellCon; 
	int shellWatcher;

	list<UUID> expectingStatus; // we are expecting a status change
};

typedef map<UUID, AgentInfo, UUIDless> mapAgentInfo;

struct AgentAffinityCBData {
	UUID agent;
	UUID affinity;
};

enum ACTIVATIONMODE {
	AM_UNSET = 0,
	AM_NORMAL,	// regular agent 
	AM_EXTERNAL,	// flag for agents that aren't part of the system (i.e. AgentMirror)
};

struct PFLock {
	int waitingForLocks;
	std::list<DDBParticleFilter_Correction> *heldCorrection;
};
typedef std::map<UUID, PFLock *, UUIDless> mapPFLock;


struct GroupData {
	std::list<UUID>		  byJoin; // complete list of members in join order
	std::list<_timeb>	  joinTime; // list of join times
};
typedef std::map<UUID, GroupData, UUIDless> mapGroupMembers;

// TEMP
//#define DEBUG_USEFOW
//#define DEBUG_USEBLACKHOLE

#define GROUP_HOSTS	"154e9b1a-a5de-42c7-8287-b208deb00c4d"

enum PA_ABORT {
	PA_ABORT_AGENT_CHANGE = 0,
	PA_ABORT_HOST_CHANGE,
	PA_ABORT_STATUS_MISMATCH,
	PA_ABORT_INCOMPATIBLE_SESSION,
	PA_ABORT_REPLACED,
	PA_ABORT_ATOMIC_FAILED,
};

struct PA_ProcessInfo {
	float cost; // usage cost
	map<UUID, float, UUIDless> agentAffinity; // agent affinity list
	list<UUID> resourceRequirement; // resource requirement list
	int status;

	bool local; // local process
	float transferPenalty; // transfer penalty

	// bundle building data
	char priority;
	AgentType type;
	int agentId; // int id for the agent
	float ancestorAffinity; // affinity provided from agents already in the bundle
	float reward; // base reward
};

struct PA_Bid {
	UUID winner;
	float reward;
	float support;
	short round;

	// local
	int clusterHead; // if non zero indicates the offset from start of a cluster bid
};

#define CBBA_PA_MAX_AGENTS 256

struct CBBA_PA_SESSION { // stores all info related to a CBBA_PA (process allocation) session
	int id; // session id

	list<UUID> group; // session group
	int groupSize; // number of hosts participating in this session

	bool sessionReady; // ready to start bidding
	bool decided; // have we reached consensus yet?

	map<UUID, PA_ProcessInfo, UUIDless> p; // map of process info indexed by agent
	float usage; // current used capacity
	float capacity; // maximum capacity

	short round; // faux round value used in resolving conflicts
	short lastBuildRound; // record the last build round to make sure each build is in a new round

	list<UUID> b[DDBAGENT_PRIORITY_COUNT]; // bundle of agents in insert order
	list<UUID> ub; // list of agents not in our bundle

	map<UUID, map<UUID, PA_Bid, UUIDless>, UUIDless> allBids; // map of all bids, indexed by agent then host
	UUID consensusLowAgent; // id of the lowest agent id that is blocking consensus
	
	map<UUID, PA_Bid, UUIDless> outbox; // outgoing updates
	UUID buildQueued; // flag that there is a bundle build queued (also stores the timeout id)
	UUID distributeQueued; // flag that there is a distribute queued (also stores the timeout id)

	int nextClusterId;
	map<int, list<UUID>> cluster; // current affinity clusters during bundling
	int agentCluster[CBBA_PA_MAX_AGENTS]; // agent cluster id by agent int id
};

struct StateTransaction_MSG {
	unsigned char msg;
	char *data;
	unsigned int len;

	UUID ticket;

	bool bundleable; // can be bundled with other transactions
};


struct UUIDLock {
	UUID key;
	std::list<UUID> tumbler; // lock is locked once all tumblers are thrown (removed)
};
typedef std::map<UUID, UUIDLock, UUIDless> mapLock;

int UUIDLock_Throw( UUIDLock *lock, UUID *tumbler, UUID *key );

struct STATISTICS_MSGS {
	int msgCount; // counts only locally received messages, sum over all hosts to get total
	int dataSize; // counts only locally received data, sum over all hosts to get total
};

struct STATISTICS_ATOMIC_MSG {
	UUID initiator;
	int participantCount;
	int order; // final order
	int round; // final round number
	int msgsSent; // counts only locally sent messages, sum over all hosts to get total
	int dataSent; // counts only locally sent data, sum over all hosts to get total
	int orderChanges; // accepted a new order, potentially different on all hosts
	bool delivered;
	_timeb startT; // time message was initiated/first received
	_timeb decidedT; // time message was resolved
	_timeb deliveredT; // time message was delived (if applicable)
};

#define CBBA_ALLOCATION_INTERVAL	30000 // ms

struct STATISTICS_AGENT_ALLOCATION {
	UUID initiator;
	int participantCount;
	int agentCount;
	int round; // final round number
	int bundleBuilds; // number of local bundle builds
	int msgsSent; // counts only locally sent messages, sum over all hosts to get total
	int dataSent; // counts only locally sent data, sum over all hosts to get total
	int clustersFound; // counts how many clusters were found during all the builds, only really useful see if any are being found at all 
	int biggestCluster; // size of the largest cluster found
	bool decided;
	_timeb startT;	 // time session was initiated/first received
	_timeb decidedT; // time session was resolved
};

class AgentHost : public AgentBase {
public:

	struct State {
		AgentBase::State state; // inherit state from AgentBase

		// stub data
		sAddressPort serverAP;	// AP for server

		mapConnectionStatistics *hostStats; // map of host statistics by UUID
		
		//list<UUID> *activeAgents; // list of active agent UUIDs

		// state data
		mapAgentTemplateInstances *agentTemplateInstances; // map of template instances by UUID, -1 = unloaded, 0 = loaded, 1+ = active instances

		// local data
		float cpuUsage;		// current CPU usage
		float ramUsage;		// current RAM usage
		
		float cpuMax;		// maximum allowed CPU usage
		float cpuRating;	// approximate scale to normalize processing capabilities
		float ramMax;		// maximum allowed RAM usage

		// remote data
		char status;				// status of the host
		UUID statusTimeout; // id of the status timeout event
		unsigned int statusActivity; // enumerates network activity between STATUS timeouts

		spConnection connection;	// connection to host, if it exists

		int runNumber;			//Simulation run number in simulation series, increments from 1

	};
	
	typedef map<UUID, AgentHost::State *, UUIDless> mapAgentHostState;

	enum STATUS {
		STATUS_UNSET = 0,		// hasn't been initialized, or unused in the case of non local states
		STATUS_ACTIVE,			// host is connected and active
		STATUS_ALIVE,			// host is alive according to others
		STATUS_QUERY,			// host is being queried for status
		STATUS_GLOBAL_QUERY,	// network is being queried for host status
		STATUS_DEAD,			// host is dead
		STATUS_SHUTDOWN			// host issued a shutdown message
	};

	enum AGENT_LOST {
		AGENT_LOST_UNKNOWN = 0,
		AGENT_LOST_WILLING,		// left willingly
		AGENT_LOST_SUSPECTED,	// suspected of crashing
	};

public:
	AgentHost( char *libraryPath, int logLevel, char *logDirectory, char playbackMode, char *playbackFile, int runNumber );
	~AgentHost();

	virtual int configure( char *configPath = NULL ); // initial configuration
	int saveConfig(); // save configuration

	int keyboardInput( char ch ); // handle keyboard input

	virtual int start( char *missionFile, char *queueMission );
	virtual int stop();
	virtual int step();

	int queueMission( char *misFile );

private:
	DataStream ds; // shared DataStream object, for temporary, single thread, use only!
	DataStream ds2; // sometimes you need more than one..

	char keyboardBuf[256]; // new keyboard input

	bool missionDone; // mission done flag

	int offlineSLAMmode; // used to affect pf updates when in offline SLAM mode

	UUIDLock gracefulExitLock;
	int gracefulExit(); // stop gracefully
	int gracefulExitUpdate( UUID *agent = NULL );

	virtual int parseMF_HandleStability( float timeMin, float timeMax ) { return 0; }; // ignore, because we read this from the host config file
	virtual int parseMF_HandleAgent( AgentType *agentType );
	virtual int parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode );
	virtual int	parseMF_HandleOfflineSLAM( int SLAMmode, int particleNum, float readingProcessingRate, int processingSlots, char *logPath );

	int runMission( char *misFile ); // manual mission start

	// -- internal data -------------------------------------------------------
	char configPath[64];		// config file path
	sAddressPort localAP;		// AP for local
	spAddressPort supervisorAP; // AP of cluster supervisors
	char clusterIDStr[64];		// identification key for cluster as string
	__int64 clusterID[2];		// identification key for cluster

	spConnection serverCon;     // server listener connection
	spConnection localCon;		// local listener connection
	list<spConnection> supervisorCon; // list of active supervisor connections

	int   processCores;			// number of cores available on this host
	float processCapacity;		// process capacity for each core
	list<UUID> hardwareResources; // list of local hardware resources 

	float timecardStart;		// delay before host starts
	float timecardEnd;			// time that the host retires

	UUID groupHostId; // id of the host group

	mapLock locks; // map of active locks by UUID

	virtual int	  atomicMessageEvaluate( UUID *id, unsigned char message, char *data = NULL, unsigned int len = 0 );
	virtual int	  atomicMessageDecided( char commit, UUID *id );
	virtual int	  _atomicMessageSendOrdered( UUID *queue ); // send next ordered message

	virtual int	  connectionStatusChanged( spConnection con, int status );

	// global state
	UUID oac_GLOBAL; // OAC queue for global state
	list<StateTransaction_MSG> globalStateQueue; // held transactions
	bool globalStateTransactionInProgress; // locally initiated transaction in progress
	int  globalStateChangeForward( unsigned char msg, char *data, unsigned int len ); // forward message to mirrors and sponsees
	int  globalStateTransaction( unsigned char msg, char *data, unsigned int len, bool retrying = false, bool bundleable = true ); // transaction on the global state
	int  globalStateAck( UUID *host, char *data, unsigned int len ); // make sure our status acknowledgement are in order 
	int  _globalStateTransactionSend(); // send held transactions

	// host membership
	spAddressPort gmApplyTo; // AP of hosts we are waiting to apply to
	spAddressPort gmIntroduceTo; // AP of hosts we are waiting to introduce ourselves to
	list<UUID>	  gmIntroducedTo; // list of hosts we have started the introduction process with
	int			  gmOACRemoveAndMembershipCount; // count of active OAC(remove,...) and OAC(membership,...)
	int			  gmMaxCoreFailures; // number of core failures that are allowed
	int			  gmCoreHead; // core head size
	list<UUID>	  gmActiveList;	// list of members we expect a response from for this update
	list<UUID>	  gmAcceptList; // list of applicants we are accepting this update
	UUID		  gmKey; // key we are using for this update

	bool		  gmWaitingUpdateMembership; // we are waiting to finish updating membership
	list<UUID>	  gmLockList; // list of members we are waiting for locks from
	list<UUID>    gmActiveOACList; // list of active OAC's when OAC(remove,...) was committed
	list<UUID>    gmLeaveActiveOACList; // list of active OAC's when we left the group
	bool		  gracefulExitWaitingOnOACs; // flag for waiting on OACs before exiting
		
	spAddressPort gmCoreHosts; // AP of core hosts
	list<UUID> gmJoinList; // list of applicants waiting to join
	list<UUID> gmMemberList; // list of members, ordered by insertion order
	list<UUID> gmRemoveList; // list of members who are suspected of failing
	list<UUID> gmLeaveList; // list of members who wish to leave
	UUID gmLocked; // flags whether the lock is set and stores the current key
	bool gmUpdatingMembers; // flags whether p is currently trying to update membership
	char gmGlobalStateSynced; // p has received the global state from its sponsor
	list<UUID> gmConnection; // list of processes p has connections with
	UUID gmSponsor; // id of p’s sponsor
	map<UUID,list<UUID>,UUIDless> gmSponsee; // map of lists of applicant connections p is sponsoring, indexed by id
	map<UUID,char,UUIDless> gmSponseeGlobalStateSync; // map of sponsees who have confirmed their global state sync, indexed by id
	bool gmGroupCorrect; // flags whether a correct group has been formed
	
	int hostGroupJoin(); // join host group
	int hostGroupLeave(); // leave host group

	int _hostGroupLeaveSuccessful(); // left the group
	int hostGroupLeaveRequest( UUID q ); // q wants to leave
	int hostGroupSuspect( UUID q ); // q is suspected
	int hostGroupPermanentlySuspect( UUID q ); // q is permanently suspected
	int hostGroupTrust( UUID q ); // q is trusted
	int hostGroupApply( UUID q, spAddressPort qAP, __int64 *key ); // q is applying to join
	int hostGroupIntroduce( UUID q, UUID a, spAddressPort aAP ); // q is introducing a
	int hostGroupSponsor( UUID q ); // q is our new sponsor
	int hostGroupGlobalStateEnd( UUID q ); // q says we've finished receiving the global state
	int hostGroupNewSponsee( UUID q ); // q is a new sponsee
	int hostGroupSponseeUpdate( DataStream *ds ); // connection update from sponsee
	int hostGroupUpdateMembership(); // update host membership
	int _hostGroupUpdateMembershipCheckWait();
	int _hostGroupUpdateMembership2();
	int hostGroupRemove( UUID q, UUID key, list<UUID> *removalList ); // commit remove transaction
	int hostGroupMembership( UUID q, UUID key, list<UUID> *newMemberList ); // commit membership transaction
	int hostGroupStartFallback(); // start the formation fallback process
	int hostGroupFormationFallback( UUID q, list<UUID> *newMemberList ); // commit formationfallback transaction
	int hostGroupAckLock( UUID q, UUID key ); // lock acknowledeged from q
	int hostGroupLock( UUID q, UUID key ); // lock group for q
	int hostGroupUnlock(); // release group lock
	int hostGroupChanged( list<UUID> *oldMembers ); // host group has changed

	int hostConCleanDuplicate( spConnection con ); // clean duplicate connection

	// hosts
	mapAgentHostState hostKnown; // map of known hosts by UUID

	int setHostKnownStatus( AgentHost::State *hState, int status ); // set the status of a known host

	int recvHostIntroduce( spConnection con, UUID *uuid );
	int recvHostShutdown( spConnection con );
	int sendHostLabelAll( spConnection con );
	int sendHostLabel( spConnection con, UUID *which );
	int recvHostLabel( spConnection con, DataStream *ds );
	int sendHostStub( spConnection con, UUID *which );
	int recvHostStub( spConnection con, DataStream *ds );

	// agent library
	char libraryPath[256]; // relative path to agent library
	mapAgentTemplate agentLibrary; // map of known agent templates by UUID
	//mapAgentSpawnProposalEvaluator agentSPE; // map of currently active agent spawn proposal evaluators by ticket
	//mapAgentSpawnRequest openASR; // map of currently open agent spawn requests by ticket

	int loadLibrary();  // scan the local agent templates
	int addAgentTemplate( spAgentTemplate AT ); // add a new template to the agentLibrary 
	
	// agent management
	list<AgentType> uniqueNeeded; // list of needed unique agents
	list<AgentType> uniqueSpawned; // list of spawned unique agents
	//mapSpawnType spawnTickets; // track type of spawn request by ticket 
	mapAgentInfo agentInfo; // map of agent info by UUID

	//int requestAgentSpawnProposals( spConnection con, UUID *thread, UUID *ticket, AgentType *type, DataStream *dsState = NULL ); // request that a new agent be spawned
	//int generateAgentSpawnProposal( spConnection con, UUID *ticket, /* requirements: */ AgentType *type ); // generate an agent spawn proposal based on the requirements and host state
	//int evaluateAgentSpawnProposal( UUID *ticket, spAgentSpawnProposal asp ); // evaluate agent spawn proposal
	//int acceptAgentSpawnProposal( spAgentSpawnProposalEvaluator aspe ); // accept current best agent spawn proposal
	//int finishAgentSpawnProposal( spAgentSpawnProposalEvaluator aspe, UUID *agentId ); // finish agent spawn proposal, spawn was either successful or timed out
	
	int recvRequestAgentSpawn( spConnection con, DataStream *ds );
	//int recvAgentSpawnProposal( spConnection con, DataStream *ds );
	//int recvAcceptAgentSpawnProposal( spConnection con, DataStream *ds );
	//int recvAgentSpawnSucceeded( spConnection con, DataStream *ds );
	//int recvAgentSpawnFailed( spConnection con, DataStream *ds );

	//int loadUniques(); // load a list of needed unique agents
	int startUniques();
	//int spawnUnique( AgentType *type ); // spawn a unique agent
	int newAgent( AgentType *type, UUID *parent, float affinity, char priority, UUID *spawnThread ); // prepare the new agent for the spawn process
	int spawnAgent( UUID *agentId, AgentType *type, DataStream *dsState, bool agentShell = false ); // spawn a new agent
	int spawnFinished( spConnection con, UUID *agentId, int spawnType ); // spawn finished
	int killAgent( UUID *agentId ); // kill agent
	int _killAgent( UUID *agentId );

	int agentLost( UUID *agent, int reason ); // all situations where an agent is lost (due to crash, exit, or other) are routed through here
	int _agentLost( UUID *agent, int reason );

	int _addAgent2( UUID *agent, AgentType *agentType, UUID *spawnThread, int activationMode, char priority, spConnection con = NULL, int watcher = -1 ); // add agent to host
	int _remAgent2( UUID *agent ); // remove agent from host
	int _addUnique( AgentType *type ); // add unique
	
	int recvAgentSpawned( spConnection con, DataStream *ds );
	int recvRequestUniqueId( spConnection con, DataStream *ds );
	
	int recvAgentMirrorRegister( spConnection con, DataStream *ds );

	int _distribute( unsigned char msg, char *data, unsigned int len ); // distribute message to hosts and mirrors
	
	// new hotness
	UUID cbbaQueued; // timeout for updating allocation
	CBBA_PA_SESSION paSession; // PA session data
	map<UUID,UUID,UUIDless> agentAllocation; // current agent allocation: winner indexed by agent
	int cbbaPAStart(); // start CBBA_PA session
	int _cbbaPAStart( DataStream *ds );
	int cbbaDecided( DataStream *ds ); // decided message received
	int cbbaAffinityClusterRecurse( UUID id, int clusterId, char *clusterSkip, int *agentCluster, list<UUID> *cluster );
	int cbbaBuildBundle(); // build a bundle with the current info
	int cbbaBuildBundleIterate( char priority );
	int cbbaTestClusterRecurse( list<UUID> *cluster, int activeCount, char *oldClusterSkip, list<UUID>::iterator start, map<int,list<list<UUID>>> *exploredClusters, PA_Bid *highBid, list<UUID> *highCluster, bool possibleClusterSplit = false );
	int cbbaCalculateClusterReward( list<UUID> *cluster, char *clusterSkip, float *clusterCost, float *clusterReward );
	int cbbaParseBidUpdate( DataStream *ds ); // parse a consensus update
	int cbbaTrimBundle( UUID *agent, char priority = DDBAGENT_PRIORITY_COUNT ); // trim bundle starting from this agent
	int cbbaQueueBuildBundle(); // queue build bundle for after we've processed all the current updates
	int cbbaCompareBid( PA_Bid *left, PA_Bid *right, bool breakTiesWithId = false, bool nilIdLoses = true ); // compare bids based on reward and support
	int cbbaUpdateConsensus( CBBA_PA_SESSION *paSession, UUID *agent, UUID *from, PA_Bid *bid );
	int cbbaCheckConsensus();

	// old slowness
//	bool paSessionInitiator; // we are a PA session initiator
//	UUID paSessionQueued;
//	CBBA_PA_SESSION *paSession; // PA session data
	int cbbaPATestStart(); // start TEST CBBA_PA session
//	int cbbaAbortSession( int reason ); // abort current session
//	int _cbbaAbortSession( UUID sessionId, int reason );
//	int cbbaQueuePAStart(); // queue PA start for after we've processed all the current updates
	
	// Agent Spawn
	int AgentSpawnAbort( UUID *agent );

	// Agent Transfer
	map<UUID,list<UUID>,UUIDless> agentTransferActiveOACList; // map of agents who are waiting for active OAC's to finish
	map<UUID,list<DDBAgent_MSG>,UUIDless> agentLocalMessageQueue; // local message queues
	int recvAgentState( UUID *agent, DataStream *ds );
	int recvAgentResumeReady( UUID *agent, UUID *ticket );
	int AgentTransferInfoChanged( UUID *agent, int infoFlags );
	int AgentTransferAvailable( UUID *agent ); // agent is available for transfer
	int AgentTransferUpdate( UUID *from, UUID *agent, UUID *ticket );
	int AgentTransferStartFreeze( UUID *agent );
	int AgentTransferAbortThaw( UUID *agent );

	// Agent Recovery
	int recvAgentBackup( UUID *agent, DataStream *ds );
	int recvAgentRecovered( UUID *agent, UUID *ticket, int result );
	int AgentRecoveryAvailable( UUID *agent ); // agent is available for recovery
	int AgentRecoveryAbort( UUID *agent ); // abort the recovery

	// Group membership service
	mapGroupMembers groupMembers; // map of groups with members grouped by host
	int groupJoin( UUID group, UUID agent ); // join group
	int _groupJoin( UUID group, UUID agent, _timeb tb ); // join group
	int groupLeave( UUID group, UUID agent ); // leave group
	int _groupLeave( UUID group, UUID agent ); // leave group

	int groupSize( UUID *group ); // get group size
	int groupList( UUID *group, list<UUID> *list ); // get list of group members
	UUID * groupLeader( UUID *group ); // get group leader

	int groupMergeSend( spConnection con ); // send group merge request
	int groupMergeRequest( DataStream *ds ); // handle group merge request
	int _groupMerge( DataStream *ds );

	int sendGroupMessage( UUID *group, unsigned char message, char *data, unsigned int len, unsigned int msgSize );

	int sendAgentMessage( UUID *agent, unsigned char message, char *data = NULL, unsigned int len = 0 ) { return this->sendAgentMessage( agent, message, data, len, -4 ); }; // pass to overloaded function below
	int sendAgentMessage( UUID *agent, unsigned char message, char *data, unsigned int len, unsigned int msgSize );

	virtual int	  conFDSuspect( spConnection con );

	// DDB
	DDBStore	*dStore; // stores all the DDB data
	mapDDBWatchers	DDBWatchers; // map of watcher lists by data type
	mapDDBItemWatchers DDBItemWatchers; // map of watcher lists by item
	std::list<UUID> DDBMirrors; // list of all mirrors
	map<UUID,list<DDBAgent_MSG>,UUIDless> ddbNotificationQueue; // notificatoins for frozen agents

	int ddbAddWatcher( UUID *id, int type );
	int _ddbAddWatcher( UUID *id, int type );
	int ddbAddWatcher( UUID *id, UUID *item );
	int _ddbAddWatcher( UUID *id, UUID *item );
	int ddbRemWatcher( UUID *id, bool immediate ); // remove agent from all items/types
	int ddbRemWatcher( UUID *id, int type );
	int _ddbRemWatcher( UUID *id, int type );
	int ddbRemWatcher( UUID *id, UUID *item );
	int _ddbRemWatcher( UUID *id, UUID *item );
	//int ddbClearWatchers( UUID *id );
	int _ddbClearWatchers( UUID *id ); // remove everyone watching agent
	int _ddbClearWatchers();
	int _ddbNotifyWatchers( UUID *key, int type, char evt, UUID *id, void *data = NULL, int len = 0 );

	int ddbAddMirror( UUID *id );
	int _ddbAddMirror( UUID *id );
	int ddbRemMirror( UUID *id );
	int _ddbRemMirror( UUID *id );

	int ddbEnumerate( spConnection con, UUID *forward );
	int _ddbParseEnumerate( DataStream *ds );

	int ddbEnumerateAgents( spConnection con, UUID *forward );
	int ddbAddAgent( UUID *id, UUID *parentId, AgentType *agentType, UUID *spawnThread, float parentAffinity, char priority, float processCost, int activationMode );
	int ddbRemoveAgent( UUID *id );
	int ddbAgentGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread );
	int ddbAgentSetInfo( UUID *id, DataStream *ds );
	
	int ddbVisAddPath( UUID *agentId, int id, int count, float *x, float *y );
	int ddbVisRemovePath( UUID *agentId, int id );
	int ddbVisExtendPath( UUID *agentId, int id, int count, float *x, float *y );
	int ddbVisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y );
	int ddbVisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name );
	int ddbVisRemoveObject( UUID *agentId, int id );
	int ddbVisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s );
	int ddbVisSetObjectVisible( UUID *agentId, int id, char visible );
	int ddbVisClearAll( UUID *agentId, char clearPaths );

	int ddbEnumerateRegions( spConnection con, UUID *forward );
	int ddbAddRegion( UUID *id, float x, float y, float w, float h );
	int ddbRemoveRegion( UUID *id );
	int ddbGetRegion( UUID *id, spConnection con, UUID *thread );

	int ddbEnumerateLandmarks( spConnection con, UUID *forward );
	int ddbAddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, char estimatedPos, ITEM_TYPES landmarkType );
	int ddbRemoveLandmark( UUID *id );
	int ddbLandmarkSetInfo( char *data, unsigned int len );
	int ddbGetLandmark( UUID *id, spConnection con, UUID *thread, bool enumLandmarks );
	int ddbGetLandmark( unsigned char code, spConnection con, UUID *thread );

	int ddbEnumeratePOGs( spConnection con, UUID *forward );
	int ddbAddPOG( UUID *id, float tileSize, float resolution );
	int ddbRemovePOG( UUID *id );
	int ddbPOGGetInfo( UUID *id, spConnection con, UUID *thread );
	int ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data );
	int ddbPOGGetRegion( UUID *id, float x, float y, float w, float h, spConnection con, UUID *thread );
	int ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename );

	list<UUID> PFResampleInProgress; // list of particle filters we've asked for resamples

	map<UUID,list<DDBParticleFilter_Correction>,UUIDless> PFHeldCorrections; // map of held corrections by id
	int ddbEnumerateParticleFilters( spConnection con, UUID *forward );
	int ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize );
	int ddbRemoveParticleFilter( UUID *id );
	int _ddbRemoveParticleFilter( UUID *id );
	int ddbResampleParticleFilter( UUID *id );
	int ddbApplyPFResample( DataStream *ds );
	int _ddbApplyPFResample( DataStream *ds );
	//int _ddbResampleParticleFilter_Lock( UUID *id, spConnection con, UUID *key, UUID *thread, UUID *host );
	//int _ddbResampleParticleFilter_Do( UUID *id );
	//int _ddbResampleParticleFilter_Unlock( DataStream *ds );
	//int _ddbResampleParticleFilter_AbortLock( UUID *id, UUID *key );
	int ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange = false );
	int ddbApplyPFCorrection( UUID *id, _timeb *tb, float *obsDensity );
	int _ddbProcessPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity );
	int ddbPFGetInfo( UUID *id, int infoFlags, _timeb *tb, spConnection con, UUID *thread );
	
	int ddbEnumerateAvatars( spConnection con, UUID *forward );
	int ddbAddAvatar( UUID *id, char *type, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes );
	int ddbRemoveAvatar( UUID *id );
	int ddbAvatarGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread );
	int ddbAvatarSetInfo( char *data, unsigned int len );
	
	int ddbEnumerateSensors( spConnection con, UUID *forward );
	int ddbAddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize );
	int ddbRemoveSensor( UUID *id );
	int ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data = NULL, int dataSize = 0 );
	int ddbSensorGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread );
	int ddbSensorGetData( UUID *id, _timeb *tb, spConnection con, UUID *thread );

	int ddbAddTask(UUID * id, UUID * landmarkUUID, UUID * agent, UUID * avatar, bool completed, ITEM_TYPES TYPE);
	int ddbRemoveTask(UUID * id);
	int ddbTaskSetInfo(UUID * id, UUID * agent, UUID * avatar, bool completed);
	int ddbGetTask(UUID * id, spConnection con, UUID * thread, bool enumTasks);

	int ddbAddTaskData(UUID *avatarId, DDBTaskData *taskData);
	int ddbRemoveTaskData(UUID * avatarid);
	int ddbTaskDataSetInfo(UUID *avatarId, DDBTaskData *taskData);
	int ddbGetTaskData(UUID * id, spConnection con, UUID * thread, bool enumTaskData);
	int ddbTLRoundSetInfo(DataStream * ds);
	int ddbTLRoundGetInfo(spConnection con, UUID * thread);
	int ddbAddQLearningData(bool onlyActions, char typeId, unsigned long totalActions, unsigned long usefulActions, int tableSize, std::vector<float> qTable, std::vector<unsigned int> expTable);
	int ddbUpdateQLearningData(char instance, bool usefulAction, int key, float qVal, unsigned int expVal);
	int ddbGetQLearningData(spConnection con, UUID * thread, char instance);
	int ddbAddAdviceData(char instance, float cq, float bq);
	int ddbAddSimSteps(unsigned long long totalSimSteps);

	// Data gathering
	Logger Data; // data logger
	int DataDump( bool fulldump, bool getPose = true, char *label = NULL );
	int DataDump_AvatarPose( DataStream *ds );


	map<UUID,STATISTICS_MSGS,UUIDless> statisticsMsgs; // messages received from agent
	map<UUID,STATISTICS_ATOMIC_MSG,UUIDless> statisticsAtomicMsg; // atomic message stats
	map<int,STATISTICS_AGENT_ALLOCATION> statisticsAgentAllocation; // agent allocation stats 

	int DumpStatistics();

	// local ports
	char usedPorts[MAX_PORTS];
	int getFreePort( spConnection con, UUID *thread );
	int releasePort( int port );

public:

	// Data gathering
	bool gatherData; // are we gathering data?

	// TESTING
	int testAgentBidding();

	//Learning data gathering
	int LearningDataDump();
	int WriteLearningData(DataStream * taskDataDS, DataStream * taskDS, mapDDBQLearningData *QLData, mapDDBAdviceData *adviceData);
	int WritePerformanceData(mapDDBQLearningData *QLData);

protected:

	int updateAgentInfo( UUID *agent, AgentType *agentType, int activationMode, UUID *spawnThread ); // update agent info

	int processCostUpdate( UUID *agent, unsigned int usage );

	int affinityUpdate( UUID *agent, AgentInfo *agentInfo, UUID *affinity, unsigned int size );
	int _affinityUpdate( UUID *agent, UUID *affinity, unsigned int size );

	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentHost )

	// Enumerate callback references
	enum CallbackRef {
		AgentHost_CBR_cbCleanExitCheck = AgentBase_CBR_HIGH,
		AgentHost_CBR_cbDelayedAgentSpawn,
	//	AgentHost_CBR_cbSupervisorWatcher,
		AgentHost_CBR_cbWatchHostConnection,
		AgentHost_CBR_cbHostFormationTimeout,
		AgentHost_CBR_cbHostConCleanDuplicate,
		AgentHost_CBR_cbHostStatusTimeout,
		AgentHost_CBR_cbDelayGlobalQuery,
		AgentHost_CBR_cbWatchAgentConnection,
		AgentHost_CBR_cbGlobalStateTransaction,
		AgentHost_CBR_cbSpawnAgentExpired,
		AgentHost_CBR_cbCBBABuildQueued,
		AgentHost_CBR_cbCBBADistributeQueued,
		AgentHost_CBR_cbCBBAStartQueued,
		AgentHost_CBR_cbAffinityCurBlock,
		AgentHost_CBR_convRequestUniqueSpawn,
//		AgentHost_CBR_convDDBResamplePF_Lock,
		AgentHost_CBR_cbRetire,
		AgentHost_CBR_cbDataDump,
		AgentHost_CBR_cbQueueMission,
		AgentHost_CBR_cbPFResampleTimeout,
		AgentHost_CBR_cbCBBAQueued,
		AgentHost_CBR_cbQueueCloseConnection,
		AgentHost_CBR_cbMissionDone,
		AgentHost_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbCleanExitCheck( void *NA ) { return 1; }; // wake thead up once in a while to check for stopFlags

	bool cbDelayedAgentSpawn( void *vpAgentType );

//	bool cbSupervisorWatcher( int evt, void *vpcon );
	bool cbWatchHostConnection( int evt, void *vpcon );

	bool cbHostFormationTimeout( void *vpCoreMember );
	bool cbHostConCleanDuplicate( void *vpuuid );

	bool cbHostStatusTimeout( void *vpuuid );
	bool cbDelayGlobalQuery( void *vpuuid );
	bool cbWatchAgentConnection( int evt, void *vpcon );
	
	bool cbGlobalStateTransaction( int commit, void *vpuuid );

	bool cbSpawnAgentExpired( void *vpAgentId );

	bool cbCBBABuildQueued( void *vpSessionId );
	bool cbCBBADistributeQueued( void *vpSessionId );
	bool cbCBBAStartQueued( void *NA );

	bool cbAffinityCurBlock( void *vpAgentAffinityCBData );

	bool convRequestUniqueSpawn( void *vpConv );
	//bool convDDBResamplePF_Lock( void *vpConv );

	bool cbRetire( void *NA );
	bool cbDataDump( void *NA ) { this->DataDump( false ); return 1; };

	bool cbQueueMission( void *vpMisFile ) { this->runMission( (char *)vpMisFile ); return 0; };

	bool cbPFResampleTimeout( void *NA );
	bool cbCBBAQueued( void *NA );

	bool cbQueueCloseConnection( void *vpConId );
	bool cbMissionDone(void *vpConId);
};

// state functions
int AgentHost_writeStub( AgentHost::State *state, DataStream * ds, bool climb = true );
int AgentHost_readStub( AgentHost::State *state, DataStream * ds, unsigned int age, bool climb = true );
int AgentHost_writeState( AgentHost::State *state, DataStream * ds );
int AgentHost_readState( AgentHost::State *state, DataStream * ds, unsigned int age );

// note: these functions are used to create state object for other agents, not for yourself
AgentBase::State * AgentHost_CreateState( UUID *uuid, int size = sizeof(AgentHost::State) );
void			   AgentHost_DeleteState( AgentBase::State *state, AgentHost *owner = NULL );

spAgentSpawnProposal NewAgentSpawnProposal( spConnection con, float favourable ); // malloc sAgentSpawnProposal and initialize
spAgentSpawnRequest NewAgentSpawnRequest( spConnection con, UUID *ticket, AgentType *type ); // malloc sAgentSpawnRequest and initialize