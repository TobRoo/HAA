// AgentAdviceExchange.cpp : Defines the initialization routines for the DLL.

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"

#include "AgentAdviceExchange.h"
#include "AgentAdviceExchangeVersion.h"

#include "..\\AgentIndividualLearning\AgentIndividualLearningVersion.h"


#include <fstream>      // std::ifstream


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentAdviceExchange

//-----------------------------------------------------------------------------
// Constructor	

AgentAdviceExchange::AgentAdviceExchange(spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile) : AgentBase(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile) {
	// allocate state
	ALLOCATE_STATE(AgentAdviceExchange, AgentBase)

	// Assign the UUID
	sprintf_s(STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentAdviceExchange");
	UUID typeId;
	UuidFromString((RPC_WSTR)_T(AgentAdviceExchange_UUID), &typeId);
	STATE(AgentBase)->agentType.uuid = typeId;

	if (AgentAdviceExchange_TRANSFER_PENALTY < 1) STATE(AgentBase)->noCrash = false; // we're ok to crash

	// Initialize state members
	STATE(AgentAdviceExchange)->parametersSet = false;
	STATE(AgentAdviceExchange)->startDelayed = false;
	STATE(AgentAdviceExchange)->updateId = -1;
	STATE(AgentAdviceExchange)->setupComplete = false;
	STATE(AgentAdviceExchange)->epoch = 1;  // SET TO 1 FOR DEVELOPMENT, SHOULD BE 0

	//---------------------------------------------------------------
	// TODO Data that must be loaded in

	// Initialize advice parameters
	this->num_state_vrbls_ = 7;
	this->num_actions_ = 5;
	this->alpha = 0.80f;
	this->beta = 0.95f;
	this->delta = 0.0f;
	this->rho = 1.00f;

	// Initialize this agents performance metrics
	this->cq = 0.0f;
	this->bq = 0.0f;

	// Seed the random number generator
	srand(static_cast <unsigned> (time(0)));

	// Prepare callbacks
	this->callback[AgentAdviceExchange_CBR_convGetAgentList] = NEW_MEMBER_CB(AgentAdviceExchange, convGetAgentList);
	this->callback[AgentAdviceExchange_CBR_convGetAgentInfo] = NEW_MEMBER_CB(AgentAdviceExchange, convGetAgentInfo);
	this->callback[AgentAdviceExchange_CBR_convAdviceQuery] = NEW_MEMBER_CB(AgentAdviceExchange, convAdviceQuery);

}// end constructor

//-----------------------------------------------------------------------------
// Destructor

AgentAdviceExchange::~AgentAdviceExchange() {

	if (STATE(AgentBase)->started) {
		this->stop();
	}// end started if

}// end destructor

//-----------------------------------------------------------------------------
// Configure

int AgentAdviceExchange::configure() {
	// Create logger
	if (Log.getLogMode() == LOG_MODE_OFF) {
		// Init logging
		RPC_WSTR rpc_wstr;
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime(&t_t);
		localtime_s(&stm, &t_t);
		strftime(timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm);
		UuidToString(&STATE(AgentBase)->uuid, &rpc_wstr);
		sprintf_s(logName, "%s\\AgentAdviceExchange %s %ls.txt", logDirectory, timeBuf, rpc_wstr);

		Log.setLogMode(LOG_MODE_COUT);
		Log.setLogMode(LOG_MODE_FILE, logName);
		Log.setLogLevel(LOG_LEVEL_VERBOSE);
		Log.log(0, "AgentAdviceExchange %.2d.%.2d.%.5d.%.2d", AgentAdviceExchange_MAJOR, AgentAdviceExchange_MINOR, AgentAdviceExchange_BUILDNO, AgentAdviceExchange_EXTEND);
	}// end if

	if (AgentBase::configure()) {
		return 1;
	}// end if

	return 0;
}// end configure

int AgentAdviceExchange::configureParameters(DataStream *ds) {
	UUID uuid;

	DataStream lds;

	// Read in owner ID
	ds->unpackUUID(&STATE(AgentAdviceExchange)->ownerId);
	STATE(AgentAdviceExchange)->avatarCapacity = (ITEM_TYPES)ds->unpackInt32();
	ds->unpackUUID(&STATE(AgentAdviceExchange)->avatarId);

	Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::configureParameters: ownerId %s", Log.formatUUID(LOG_LEVEL_NORMAL, &STATE(AgentAdviceExchange)->ownerId));

	// register as agent watcher
	Log.log(0, "AgentAdviceExchange::start: registering as agent watcher");
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_AGENT);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();
	// NOTE: we request a list of agents once DDBE_WATCH_TYPE notification is received

	this->backup();

	return 0;
}// end configureParameters

int	AgentAdviceExchange::finishConfigureParameters() {

	// Perform preEpochUpdate
	this->preEpochUpdate();

	STATE(AgentAdviceExchange)->parametersSet = true;


	if (STATE(AgentAdviceExchange)->startDelayed) {
		STATE(AgentAdviceExchange)->startDelayed = false;
		this->start(STATE(AgentBase)->missionFile);
	}// end if

	this->backup();

	return 0;
}// end finishConfigureParameters

//-----------------------------------------------------------------------------
// Start

int AgentAdviceExchange::start(char *missionFile) {


	// Delay start if parameters not set
	if (!STATE(AgentAdviceExchange)->parametersSet) { // delay start
		strcpy_s(STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), missionFile);
		STATE(AgentAdviceExchange)->startDelayed = true;
		return 0;
	}// end if

	if (AgentBase::start(missionFile)) {
		return 1;
	}// end if

	STATE(AgentBase)->started = false;

	

	STATE(AgentBase)->started = true;
	return 0;
}// end start


//-----------------------------------------------------------------------------
// Stop

int AgentAdviceExchange::stop() {

	if (!this->frozen) {
		// TODO
	}// end if

	return AgentBase::stop();
}// end stop


//-----------------------------------------------------------------------------
// Step

int AgentAdviceExchange::step() {
	//Log.log(0, "AgentAdviceExchange::step()");

	return AgentBase::step();
}// end step

 /* askAdvisers
 *
 * Sends the state vector to all the known advisers
 */
int AgentAdviceExchange::askAdviser() {
	// Make sure we have an adviser
	if (this->adviser == nilUUID) {
		this->formAdvice();
		return 0;
	}

	// Send state vector to the adviser, to receive their advice in return
	this->adviserData[this->adviser].queryConv = this->conversationInitiate(AgentAdviceExchange_CBR_convAdviceQuery, DDB_REQUEST_TIMEOUT);
	if (this->adviserData[this->adviser].queryConv == nilUUID) {
		return 1;
	}

	// Send a message with the state vector
	DataStream lds;
	lds.reset();
	lds.packUUID(&this->adviserData[this->adviser].queryConv); // Add the thread
	lds.packUUID(&STATE(AgentBase)->uuid);  // Add sender Id

	// Pack the state vector
	for (std::vector<unsigned int>::iterator state_iter = this->state_vector_in.begin(); state_iter != this->state_vector_in.end(); ++state_iter) {
		lds.packUInt32(*state_iter);
	}

	this->sendMessageEx(this->hostCon, MSGEX(AgentAdviceExchange_MSGS, MSG_REQUEST_Q_VALUES), lds.stream(), lds.length(), &this->adviserData[this->adviser].parentId);
	lds.unlock();

	return 0;
}

/* formAdvice
*
* Performs the AdviceExchange Algorithm
*/
int AgentAdviceExchange::formAdvice() {
	std::vector<float> advice;
	
	if (this->adviser == nilUUID) {
		// Return the original values
		advice = this->q_vals_in;
		Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::formAdvice: No adviser, returning original quality values.");
	}
	else {
		// Check Advice Exchange conditions
		bool condA = (this->q_avg_epoch < this->delta*this->adviserData[this->adviser].bq);
		bool condB = (this->bq < this->adviserData[this->adviser].bq);
		bool condC = (std::accumulate(this->q_vals_in.begin(), this->q_vals_in.end(), 0.0f) < this->rho*std::accumulate(this->adviserData[this->adviser].advice.begin(), this->adviserData[this->adviser].advice.end(), 0.0f));

		if (condA && condB && condC) {
			// Conditions are met, use this adviser's advice
			advice = this->adviserData[this->adviser].advice;
			Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::formAdvice: Adviser conditions met, using their advice.");
		}
		else {
			// Conditions not satisfied, do not use the advice
			advice = this->q_vals_in;
			Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::formAdvice: Adviser conditions not met, rejecting their advice.");
		}
	}

	// Send the advice back
	DataStream lds;
	lds.reset();
	lds.packUUID(&this->adviceRequestConv);
	lds.packUUID(&STATE(AgentBase)->uuid);

	// Pack the advised Q values
	for (std::vector<float>::iterator q_iter = advice.begin(); q_iter != advice.end(); ++q_iter) {
		lds.packFloat32(*q_iter);
	}
	this->sendMessage(this->hostCon, MSG_RESPONSE, lds.stream(), lds.length(), &STATE(AgentAdviceExchange)->ownerId);
	lds.unlock();

	return 0;
}


/* preEpochUpdate
*
* Retrieves the performance metrics for the parent agent, and each adviser,
* from the DDB and stores them.
*/
int AgentAdviceExchange::preEpochUpdate() {
	STATE(AgentAdviceExchange)->epoch++;
	Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::preEpochUpdate: Epoch number: %d.", STATE(AgentAdviceExchange)->epoch);

	// Default to no adviser
	this->adviser = nilUUID;

	if (STATE(AgentAdviceExchange)->epoch == 1) {
		// Nothing to update on the first epoch
		return 0;
	}

	// Get my cq and bq from the DDB
	// TODO (fake values for now)
	this->cq = (float)this->randomGenerator.Uniform01();
	this->bq = (float)this->randomGenerator.Uniform01();

	// Get the cq and bq values of each adviser from the DDB
	Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::preEpochUpdate: Requesting adviser performance metrics from the DDB.");
	std::map<UUID, adviserDataStruct, UUIDless>::iterator iter;
	for (iter = this->adviserData.begin(); iter != this->adviserData.end(); iter++) {
		// TODO (fake values for now)
		iter->second.cq = (float)this->randomGenerator.Uniform01();
		iter->second.bq = (float)this->randomGenerator.Uniform01();
	}
	Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::preEpochUpdate: Adviser performance metrics received.");

	// Select the adviser for this epoch
	float best_cq = 0.0f;
	for (iter = this->adviserData.begin(); iter != this->adviserData.end(); iter++) {
		// Their cq must be better 
		if ((iter->second.cq > this->cq) && (iter->second.cq > best_cq)) {
			// Select them as the adviser
			this->adviser = iter->first;
		}
	}

	if (this->adviser == nilUUID) {
		Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::preEpochUpdate: No superior agents exist, so no advice will be used.");
	}
	else {
		Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::preEpochUpdate: Adviser for this epoch is %s.", Log.formatUUID(LOG_LEVEL_NORMAL, &this->adviser));
	}

	return 0;
}


/* postEpochUpdate
*
* Updates this agents cq and bq metrics, then sends them to the DDB
*/
int AgentAdviceExchange::postEpochUpdate() {
	// Update cq and bq
	this->cq = this->alpha*this->cq + (1.0f - this->alpha)*this->q_avg_epoch;
	this->bq = max(this->q_avg_epoch, this->beta*this->bq);

	// Send cq and bq to the DDB
    Log.log(LOG_LEVEL_NORMAL, "AgentAdviceExchange::postEpochUpdateB: Sending cq and bq to DDB.");
	// TODO
	// SAVE(STATE(AgentBase)->uuid, this->cq, this->bq)

	return 0;
}


int AgentAdviceExchange::ddbNotification(char *data, int len) {
	DataStream lds, sds;
	UUID uuid;

	int offset;
	int type;
	char evt;
	lds.setData(data, len);
	lds.unpackData(sizeof(UUID)); // key
	type = lds.unpackInt32();
	lds.unpackUUID(&uuid);
	evt = lds.unpackChar();
	offset = 4 + sizeof(UUID) * 2 + 1;
	if (evt == DDBE_WATCH_TYPE) {
		if (type == DDB_AGENT) {
			// request list of agents
			UUID thread = this->conversationInitiate(AgentAdviceExchange_CBR_convGetAgentList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(&nilUUID); // dummy id 
			sds.packInt32(DDBAGENTINFO_RLIST);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length());
			sds.unlock();
		}
	}
	if (evt == DDBE_UPDATE) {
		if (type == DDB_AGENT) {
			// request agent info
			UUID thread = this->conversationInitiate(AgentAdviceExchange_CBR_convGetAgentInfo, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(&uuid); // Agent id 
			sds.packInt32(DDBAGENTINFO_RTYPE| DDBAGENTINFO_RPARENT);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_RAGENTINFO, sds.stream(), sds.length());
			sds.unlock();


		}
	}


	lds.unlock();

	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentAdviceExchange::conProcessMessage(spConnection con, unsigned char message, char *data, unsigned int len) {
	DataStream lds;

	if (!AgentBase::conProcessMessage(con, message, data, len)) // message handled
		return 0;

	switch (message) {
	case AgentAdviceExchange_MSGS::MSG_CONFIGURE:
	{
		lds.setData(data, len);
		this->configureParameters(&lds);
		lds.unlock();
		break;
	}
	break;
	case AgentAdviceExchange_MSGS::MSG_REQUEST_ADVICE:
	{
		// Clear the old data
		this->q_vals_in.clear();
		this->state_vector_in.clear();

		UUID sender;
		lds.setData(data, len);

		lds.unpackUUID(&this->adviceRequestConv);
		lds.unpackUUID(&sender);

		// Unpack the epoch average quality
		this->q_avg_epoch = lds.unpackFloat32();
	
		// Unpack Q values
		for (int i = 0; i < this->num_actions_; i++) {
			this->q_vals_in.push_back(lds.unpackFloat32());
		}
		// Unpack state vector
		for (int j = 0; j < this->num_state_vrbls_; j++) {
			this->state_vector_in.push_back(lds.unpackUInt32());
		}
		lds.unlock();

		// Proceed to ask adviser for advice
	    this->askAdviser();
	}
	break;
	case AgentAdviceExchange_MSGS::MSG_REQUEST_CAPACITY:
	{
		DataStream sds;

		//int infoFlags;
		//UUID thread;
		//lds.setData(data, len);
		//lds.unpackUUID(&uuid);
		//infoFlags = lds.unpackInt32();
		//lds.unpackUUID(&thread);
		//lds.unlock();
		//this->ddbAgentGetInfo(&uuid, infoFlags, con, &thread);

	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks


bool AgentAdviceExchange::convGetAgentList(void *vpConv) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentAdviceExchange::convGetAgentList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		
		if (lds.unpackInt32() != DDBAGENTINFO_RLIST) {
			lds.unlock();
			return 0; // what happened here?
		}

		// Get number of agents
		int count;
		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentAdviceExchange::convGetAgentList: recieved %d agents", count);

		UUID thread;
		UUID agentId;
		AgentType agentType;
		UUID parent;

		UUID adviceExchangeAgentId;
		UuidFromString((RPC_WSTR)_T(AgentAdviceExchange_UUID), &adviceExchangeAgentId);
		UUID individualLearningAgentId;
		UuidFromString((RPC_WSTR)_T(AgentIndividualLearning_UUID), &individualLearningAgentId);

		// Scan agents of type AgentAdviceExchange
		adviserDataStruct empty_data;
		for (int i = 0; i < count; i++) {
			lds.unpackUUID(&agentId);
			lds.unpackString(); // agent type
			lds.unpackUUID(&agentType.uuid);
			agentType.instance = lds.unpackChar();
			lds.unpackUUID(&parent);

			if (agentType.uuid == adviceExchangeAgentId && agentId != STATE(AgentBase)->uuid) {			//The agent is an advice exchange agent
				// Add agent to the list if it has the same capacity as own capacity (Light or heavy)
				//TODO: Exclude by type
				this->adviserData[agentId] = empty_data;
				this->adviserData[agentId].parentId = parent;
				this->adviserData[agentId].bq = 0.0f;
				this->adviserData[agentId].cq = 0.0f;
			}
		}
		Log.log(LOG_LEVEL_VERBOSE, "AgentAdviceExchange::convGetAgentList: Found %d advisers.", this->adviserData.size());

		lds.unlock();
	}
	this->finishConfigureParameters();
	return 0;
}

bool AgentAdviceExchange::convGetAgentInfo(void *vpConv) {

	//DataStream lds;
	//spConversation conv = (spConversation)vpConv;

	//if (conv->response == NULL) { // timed out
	//	Log.log(0, "AgentAdviceExchange::convGetAgentInfo: timed out");
	//	return 0; // end conversation
	//}

	//UUID avatarId = *(UUID *)conv->data;

	//lds.setData(conv->response, conv->responseLen);
	//lds.unpackData(sizeof(UUID)); // discard thread

	//char response = lds.unpackChar();
	//if (response == DDBR_OK) { // succeeded
	//	UUID pfId, avatarAgent;
	//	char retired;
	//	float innerR, outerR;
	//	_timeb startTime, endTime;

	//	if (lds.unpackInt32() != (DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD)) {
	//		lds.unlock();
	//		return 0; // what happened here?
	//	}

	//	lds.unpackUUID(&avatarAgent);
	//	lds.unpackUUID(&pfId);
	//	innerR = lds.unpackFloat32();
	//	outerR = lds.unpackFloat32();
	//	startTime = *(_timeb *)lds.unpackData(sizeof(_timeb));
	//	retired = lds.unpackChar();
	//	if (retired)
	//		endTime = *(_timeb *)lds.unpackData(sizeof(_timeb));
	//	lds.unlock();

	//	Log.log(LOG_LEVEL_VERBOSE, "AgentAdviceExchange::convGetAgentInfo: recieved agent (%s) ",
	//		Log.formatUUID(LOG_LEVEL_VERBOSE, &agentId));

	//	if (avatarId == this->avatarId) {
	//		// This is our avatar
	//		this->avatarAgentId = avatarAgent;
	//		this->avatar.pf = pfId;
	//		this->avatar.start = startTime;
	//		if (retired)
	//			this->avatar.end = endTime;
	//		this->avatar.retired = retired;
	//		this->avatar.innerRadius = innerR;
	//		this->avatar.outerRadius = outerR;
	//		this->avatar.ready = true;
	//	}
	//	else {
	//		// This is a teammate
	//		this->otherAvatars[avatarId].pf = pfId;
	//		this->otherAvatars[avatarId].start = startTime;
	//		if (retired)
	//			this->otherAvatars[avatarId].end = endTime;
	//		this->otherAvatars[avatarId].retired = retired;
	//		this->otherAvatars[avatarId].innerRadius = innerR;
	//		this->otherAvatars[avatarId].outerRadius = outerR;
	//		this->otherAvatars[avatarId].ready = true;
	//	}
	//}
	//else {
	//	lds.unlock();

	//	// TODO try again?
	//}

	return 0;
}

bool AgentAdviceExchange::convAdviceQuery(void *vpConv) {
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) {
		Log.log(0, "AgentAdviceExchange::convAdviceQuery: request timed out");
		return 0; // end conversation
	}

	// Start unpacking
	DataStream lds;
	UUID thread;
	UUID sender;
	
	lds.setData(conv->response, conv->responseLen);
	lds.unpackUUID(&thread);
	lds.unpackUUID(&sender);

	// Unpack Q values
	this->adviserData[this->adviser].advice.clear();
	for (int i = 0; i < this->num_actions_; i++) {
		this->adviserData[this->adviser].advice.push_back(lds.unpackFloat32());
	}

	// Proceed to form advice
	this->formAdvice();

	return 0;
}

//-----------------------------------------------------------------------------
// State functions

int AgentAdviceExchange::freeze(UUID *ticket) {
	return AgentBase::freeze(ticket);
}// end freeze

int AgentAdviceExchange::thaw(DataStream *ds, bool resumeReady) {
	return AgentBase::thaw(ds, resumeReady);
}// end thaw

int	AgentAdviceExchange::writeState(DataStream *ds, bool top) {
	if (top) _WRITE_STATE(AgentAdviceExchange);

	return AgentBase::writeState(ds, false);
}// end writeState

int	AgentAdviceExchange::readState(DataStream *ds, bool top) {
	if (top) _READ_STATE(AgentAdviceExchange);

	return AgentBase::readState(ds, false);
}// end readState

int AgentAdviceExchange::recoveryFinish() {
	if (AgentBase::recoveryFinish())
		return 1;

	return 0;
}// end recoveryFinish

int AgentAdviceExchange::writeBackup(DataStream *ds) {

	// Agent Data
	ds->packUUID(&STATE(AgentAdviceExchange)->ownerId);
	ds->packBool(STATE(AgentAdviceExchange)->parametersSet);
	ds->packBool(STATE(AgentAdviceExchange)->startDelayed);
	ds->packInt32(STATE(AgentAdviceExchange)->updateId);
	ds->packBool(STATE(AgentAdviceExchange)->setupComplete);
	ds->packInt32(STATE(AgentAdviceExchange)->avatarCapacity);
	ds->packInt32(STATE(AgentAdviceExchange)->epoch);

	return AgentBase::writeBackup(ds);
}// end writeBackup

int AgentAdviceExchange::readBackup(DataStream *ds) {
	DataStream lds;

	// configuration
	ds->unpackUUID(&STATE(AgentAdviceExchange)->ownerId);
	STATE(AgentAdviceExchange)->parametersSet = ds->unpackBool();
	STATE(AgentAdviceExchange)->startDelayed = ds->unpackBool();
	STATE(AgentAdviceExchange)->updateId = ds->unpackInt32();
	STATE(AgentAdviceExchange)->setupComplete = ds->unpackBool();
	STATE(AgentAdviceExchange)->avatarCapacity = (ITEM_TYPES)ds->unpackInt32();
	STATE(AgentAdviceExchange)->epoch = ds->unpackInt32();

	if (STATE(AgentAdviceExchange)->setupComplete) {
		this->finishConfigureParameters();
	}
	else {
		//TODO
	}

	return AgentBase::readBackup(ds);
}// end readBackup


//*****************************************************************************
// Threading

DWORD WINAPI RunThread(LPVOID vpAgent) {
	AgentAdviceExchange *agent = (AgentAdviceExchange *)vpAgent;

	if (agent->configure()) {
		return 1;
	}

	while (!agent->step());

	if (agent->isStarted())
		agent->stop();
	delete agent;

	return 0;
}

int Spawn(spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile) {
	AgentAdviceExchange *agent = new AgentAdviceExchange(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

	HANDLE hThread;
	DWORD  dwThreadId;

	hThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		RunThread,				// thread function name
		agent,					// argument to thread function 
		0,                      // use default creation flags 
		&dwThreadId);			// returns the thread identifier 

	if (hThread == NULL) {
		delete agent;
		return 1;
	}

	return 0;
}// end Spawn

int Playback(spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile) {
	AgentAdviceExchange *agent = new AgentAdviceExchange(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

	if (agent->configure()) {
		delete agent;
		return 1;
	}

	while (!agent->step());

	if (agent->isStarted())
		agent->stop();

	printf_s("Playback: agent finished\n");

	delete agent;

	return 0;
}// end Playback


// CAgentPathPlannerDLL: Override ExitInstance for debugging memory leaks
CAgentAdviceExchangeDLL::CAgentAdviceExchangeDLL() {}

// The one and only CAgentPathPlannerDLL object
CAgentAdviceExchangeDLL theApp;

int CAgentAdviceExchangeDLL::ExitInstance() {
	TRACE(_T("--- ExitInstance() for regular DLL: CAgentAdviceExchange ---\n"));
	return CWinApp::ExitInstance();
}