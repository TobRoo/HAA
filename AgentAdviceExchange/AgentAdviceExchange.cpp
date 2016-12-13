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


	//---------------------------------------------------------------
	// TODO Data that must be loaded in

	// Initialize advice data
	this->num_state_vrbls_ = 7;
	this->num_actions_ = 5;

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
int AgentAdviceExchange::askAdvisers() {
	// Send state vector to all advisers, to receive their advice in return
	
	DataStream lds;
	std::map<UUID, adviserDataStruct, UUIDless>::iterator iter;
	for (iter = this->adviserData.begin(); iter != this->adviserData.end(); iter++) {

		iter->second.response = false;

        // Initiate conversation with adviser
		iter->second.queryConv = this->conversationInitiate(AgentAdviceExchange_CBR_convAdviceQuery, DDB_REQUEST_TIMEOUT);
		if (iter->second.queryConv == nilUUID) {
			return 1;
		}

		// Send a message with the state vector
		lds.reset();
		lds.packUUID(&iter->second.queryConv); // Add the thread
		lds.packUUID(&STATE(AgentBase)->uuid);  // Add sender Id
		
		// Pack the state vector
		for (std::vector<unsigned int>::iterator state_iter = this->state_vector_in.begin(); state_iter != this->state_vector_in.end(); ++state_iter) {
			lds.packUInt32(*state_iter);
		}

		this->sendMessageEx(this->hostCon, MSGEX(AgentAdviceExchange_MSGS, MSG_REQUEST_Q_VALUES), lds.stream(), lds.length(), &STATE(AgentAdviceExchange)->ownerId);
		lds.unlock();
	}

	return 0;
}

/* formAdvice
*
* Performs the AdviceExchange Algorithm
*/
int AgentAdviceExchange::formAdvice() {
	// TODO: Add the actual algorithm
	
	// Temporarily default to forwarding back the original q_vals
	std::vector<float> advice = this->q_vals_in;
	
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

	Log.log(0, "AgentAdviceExchange::formAdvice: Sent advice.");

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
	
		// Unpack Q values
		for (int i = 0; i < this->num_actions_; i++) {
			this->q_vals_in.push_back(lds.unpackFloat32());
		}
		// Unpack state vector
		for (int j = 0; j < this->num_state_vrbls_; j++) {
			this->state_vector_in.push_back(lds.unpackUInt32());
		}
		lds.unlock();

		// Proceed to ask advisers for advice
		this->askAdvisers();
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

	Log.log(0, "REMOVETHISWHENDONE - conv %s", Log.formatUUID(0, &thread));
	Log.log(0, "REMOVETHISWHENDONE - Sender %s", Log.formatUUID(0, &sender));

	int response_count = 0;
	
	std::map<UUID, adviserDataStruct, UUIDless>::iterator iter;
	for (iter = this->adviserData.begin(); iter != this->adviserData.end(); iter++) {
		Log.log(0, "REMOVETHISWHENDONE - Checking adviser ");
		if (iter->second.response == false) {

			Log.log(0, "REMOVETHISWHENDONE - Their response was false");
			Log.log(0, "REMOVETHISWHENDONE - parentId %s", Log.formatUUID(0, &iter->second.parentId));

			// Check for the right adviser
			if (iter->second.parentId == sender) {
				Log.log(0, "REMOVETHISWHENDONE - They were the sender");
				// Unpack Q values
				iter->second.advice.clear();
				for (int i = 0; i < this->num_actions_; i++) {
					iter->second.advice.push_back(lds.unpackFloat32());
				}
				iter->second.response = true;
				response_count++;
			}
		}
		else {
			response_count++;
		}
	}
	lds.unlock();

	Log.log(0, "REMOVETHISWHENDONE - Heard from %d advisers", response_count);
	
	// Only proceed to form advice once we have heard from all advisers
	if (response_count == this->adviserData.size()) {
		this->formAdvice();
	}

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