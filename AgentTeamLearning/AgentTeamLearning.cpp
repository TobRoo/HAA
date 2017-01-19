// AgentTeamLearning.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "AgentTeamLearning.h"
#include "AgentTeamLearningVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"

#include <ctime>

#include <fstream>
#include <iostream>
#include <string>

using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentTeamLearning
 
//-----------------------------------------------------------------------------
// Constructor	
 
AgentTeamLearning::AgentTeamLearning(spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile) : AgentBase(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile), lAllianceObject(this) {
	// allocate state
	ALLOCATE_STATE(AgentTeamLearning, AgentBase)

		// Assign the UUID
		sprintf_s(STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentTeamLearning");
	UUID typeId;
	UuidFromString((RPC_WSTR)_T(AgentTeamLearning_UUID), &typeId);
	STATE(AgentBase)->agentType.uuid = typeId;

	if (AgentTeamLearning_TRANSFER_PENALTY < 1) STATE(AgentBase)->noCrash = false; // we're ok to crash

	STATE(AgentTeamLearning)->isSetupComplete = false;
	STATE(AgentTeamLearning)->hasReceivedTaskList = false;
	STATE(AgentTeamLearning)->hasReceivedTaskDataList = false;
	STATE(AgentTeamLearning)->receivedAllTeamLearningAgents = false;
	STATE(AgentTeamLearning)->parametersSet = false;
	STATE(AgentTeamLearning)->updateId = -1;

	STATE(AgentTeamLearning)->isMyDataFound = false;
	STATE(AgentTeamLearning)->hasReceivedRunNumber = false;
	STATE(AgentTeamLearning)->runNumber = 0;

	// V2 team learning stuff
	STATE(AgentTeamLearning)->round_number = 0;
	this->new_round_number = 0;
	this->lAllianceObject.myData.round_number = 0;
	this->round_info_set = true;
	this->delta_learn_time = 3000; // [ms]
	this->round_timout = 500;  // [ms]
	
	// Prepare callbacks
	this->callback[AgentTeamLearning_CBR_convGetAgentList] = NEW_MEMBER_CB(AgentTeamLearning, convGetAgentList);
	this->callback[AgentTeamLearning_CBR_convGetTaskList] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskList);
	this->callback[AgentTeamLearning_CBR_convGetTaskDataList] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskDataList);
	this->callback[AgentTeamLearning_CBR_convGetTaskInfo] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskInfo);
	this->callback[AgentTeamLearning_CBR_convGetTaskDataInfo] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskDataInfo);
	this->callback[AgentTeamLearning_CBR_convReqAcquiescence] = NEW_MEMBER_CB(AgentTeamLearning, convReqAcquiescence);
	this->callback[AgentTeamLearning_CBR_convReqMotReset] = NEW_MEMBER_CB(AgentTeamLearning, convReqMotReset);
	this->callback[AgentTeamLearning_CBR_convGetRunNumber] = NEW_MEMBER_CB(AgentTeamLearning, convGetRunNumber);
}// end constructor

//-----------------------------------------------------------------------------
// Destructor

AgentTeamLearning::~AgentTeamLearning() {

	if (STATE(AgentBase)->started) {
		this->stop();
	}// end started if

}// end destructor

//-----------------------------------------------------------------------------
// Configure

int AgentTeamLearning::configure() {
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
		sprintf_s(logName, "%s\\AgentTeamLearning %s %ls.txt", logDirectory, timeBuf, rpc_wstr);

		Log.setLogMode(LOG_MODE_COUT);
		Log.setLogMode(LOG_MODE_FILE, logName);
		Log.setLogLevel(LOG_LEVEL_VERBOSE);
		Log.log(0, "AgentTeamLearning %.2d.%.2d.%.5d.%.2d", AgentTeamLearning_MAJOR, AgentTeamLearning_MINOR, AgentTeamLearning_BUILDNO, AgentTeamLearning_EXTEND);
	}// end if

	if (AgentBase::configure()) {
		return 1;
	}// end if

	return 0;
}// end configure

int AgentTeamLearning::configureParameters(DataStream *ds) {
	DataStream lds;

	//Get owner id
	ds->unpackUUID(&STATE(AgentTeamLearning)->ownerId);
	STATE(AgentTeamLearning)->avatarInstance = ds->unpackInt32();
	this->lAllianceObject.id = STATE(AgentTeamLearning)->ownerId;
	this->lAllianceObject.myData.agentId = *this->getUUID();
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::configureParameters: ownerId %s", Log.formatUUID(LOG_LEVEL_NORMAL, &STATE(AgentTeamLearning)->ownerId));
	STATE(AgentTeamLearning)->isSetupComplete = false; // need taskdata info and task info

	// register as agent watcher
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::start: registering as agent watcher");
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_AGENT);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();
	// NOTE: we request a list of agents once DDBE_WATCH_TYPE notification is received

	// register as task watcher
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_TASK);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();

	// register as task data watcher
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_TASKDATA);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();

	// request run number info
	UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetRunNumber, DDB_REQUEST_TIMEOUT);
	if (thread == nilUUID) {
		return 1;
	}
	lds.reset();
	lds.packUUID(&thread);
	//lds.packUUID(&STATE(AgentBase)->uuid);
	this->sendMessage(this->hostCon, MSG_RRUNNUMBER, lds.stream(), lds.length());
	lds.unlock();
	
	this->backup(); // initialSetup
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::configureParameters: done.");
	// finishConfigureParameters will be called once task, taskdata, agent info and run number are received

	return 0;
}// end configureParameters

int	AgentTeamLearning::finishConfigureParameters() {

	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::finishConfigureParameters: hasReceivedTaskList: %d, hasReceivedTaskDataList: %d, parametersSet: %d, receivedAllTeamLearningAgents: %d, hasReceivedRunNumber: %d",
		STATE(AgentTeamLearning)->hasReceivedTaskList,
		STATE(AgentTeamLearning)->hasReceivedTaskDataList,
		STATE(AgentTeamLearning)->parametersSet,
		STATE(AgentTeamLearning)->receivedAllTeamLearningAgents,
		STATE(AgentTeamLearning)->hasReceivedRunNumber
	);

	//Only proceed to start if we have other agents, tasks, and taskdata, and have not yet started (prevents double start)
	if (STATE(AgentTeamLearning)->hasReceivedTaskList == true 
		&& STATE(AgentTeamLearning)->hasReceivedTaskDataList == true 
		&& STATE(AgentTeamLearning)->parametersSet == false
		&& STATE(AgentTeamLearning)->receivedAllTeamLearningAgents == true
		&& STATE(AgentTeamLearning)->hasReceivedRunNumber == true){

		STATE(AgentTeamLearning)->isSetupComplete = true;
		this->previousTaskId = nilUUID;
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::finishConfigureParameters");

		//Read in data from previous run (if any)
		this->parseLearningData();

		STATE(AgentTeamLearning)->parametersSet = true;

		if (STATE(AgentTeamLearning)->startDelayed) {
			STATE(AgentTeamLearning)->startDelayed = false;
			this->start(STATE(AgentBase)->missionFile);
		}
		this->backup(); // initialSetup
	}
	return 0;
} // end finishConfigureParameters

int AgentTeamLearning::parseLearningData()
{
	char learningDataFile[512];
	sprintf(learningDataFile, "learningData%d.tmp", STATE(AgentTeamLearning)->runNumber - 1);
	taskList tempTaskList = this->mTaskList;
	std::map<UUID, float, UUIDless> tauList = this->lAllianceObject.myData.tau;

	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;
	ITEM_TYPES landmark_type;
	float tauVal;

	if (fopen_s(&fp, learningDataFile, "r")) {
		Log.log(0, "AgentTeamLearning::parseLearningData: failed to open %s", learningDataFile);
		return 1; // couldn't open file
	}
	Log.log(0, "AgentTeamLearning::parseLearningData: parsing %s", learningDataFile);
	i = 0;
	while (1) {
		ch = fgetc(fp);

		if (ch == EOF) {
			break;
		}
		else if (ch == '\n') {
			keyBuf[i] = '\0';

			if (i == 0)
				continue; // blank line

			if (!strncmp(keyBuf, "[TLData]", 64)) {
				Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::parseLearningData: Found team learning data section.");
				int id;
				if (fscanf_s(fp, "id=%d\n", &id) != 1) { 
					Log.log(0, "AgentTeamLearning::parseLearningData: badly formatted id");
					break;
				}
				while (fscanf_s(fp, "landmark_type=%d\n", &landmark_type) == 1) {
					fscanf_s(fp, "tau=%f\n", &tauVal);
					Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::parseLearningData: type: %d, tau: %f", landmark_type, tauVal);
					if (id == STATE(AgentTeamLearning)->avatarInstance) {				//If the data belongs to this agent, store it
						for (auto& taskIter : tempTaskList) {							//Go through all tasks 
							if (taskIter.second->type == landmark_type) {				//Find one with the same landmark type
								lAllianceObject.myData.tau.at(taskIter.first) = tauVal;	//Set the stored tau value for the task
								tempTaskList.erase(taskIter.first);						//Remove the task from the temporary task list to avoid dual assignments
							}
						/*	if (taskIter == tempTaskList.end())
								Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::parseLearningData:reached end of task list, the task config has changed between runs.");*/
						}
					}
				}
				if (id == STATE(AgentTeamLearning)->avatarInstance) { //The data belongs to this agent, no need to parse further...
					Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::parseLearningData: parsing complete, stopping parsing process...");
					break;
				}
			}
			else if (!strncmp(keyBuf, "[QLData]", 64)) {
				;
			}
			else { // unknown key
				fclose(fp);
				Log.log(0, "AgentTeamLearning::parseLearningData: unknown key: %s", keyBuf);
				return 1;
			}
			i = 0;
		}
		else {
			keyBuf[i] = ch;
			i++;
		}
	}
	fclose(fp);
	return 0;
}


//-----------------------------------------------------------------------------
// Start

int AgentTeamLearning::start(char *missionFile) {
	DataStream lds;

	// Delay start if parameters not set
	if (!STATE(AgentTeamLearning)->parametersSet) { // delay start
		strcpy_s(STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), missionFile);
		STATE(AgentTeamLearning)->startDelayed = true;
		return 0;
	}// end if

	if (AgentBase::start(missionFile)) {
		return 1;
	}// end if

	STATE(AgentBase)->started = false;

	if (STATE(AgentTeamLearning)->isMyDataFound == false) {		//We had no previous data in the DDB, upload first set

		//if (lAllianceObject.teammatesData.size() == 0) {	//We are first, start by selecting a task
	//	lAllianceObject.updateTaskProperties(this->mTaskList);	//First make sure that myData is populated
	//	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::start: teammatesData is empty == %s", lAllianceObject.teammatesData.empty() ? "true" : "false");
	//	lAllianceObject.chooseTask(this->mTaskList);			//And that we have chosen a task...
	//}


		DataStream lds;

		//Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::start: uploading... my avatar id is: %s", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId));
		//Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::start: uploading... my task id is: %s", Log.formatUUID(0, &lAllianceObject.myData.taskId));
		lds.reset();
		lds.packUUID(&STATE(AgentTeamLearning)->ownerId);
		//_ftime64_s(&this->lAllianceObject.myData.updateTime);	//Update time
		this->lAllianceObject.myData.taskId = nilUUID;
		this->lAllianceObject.myData.agentId = *this->getUUID();
		lds.packTaskData(&this->lAllianceObject.myData);
		this->sendMessage(this->hostCon, MSG_DDB_ADDTASKDATA, lds.stream(), lds.length());	//... then upload
		lds.unlock();
	}

	//Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::start: teammatesData is empty == %s", lAllianceObject.teammatesData.empty() ? "true" : "false");


	//Log.log(0, "AgentTeamLearning::start completed.");
	Log.log(LOG_LEVEL_NORMAL, "My instance is: %d", STATE(AgentTeamLearning)->avatarInstance);
	STATE(AgentBase)->started = true;
	return 0;
}// end start


//-----------------------------------------------------------------------------
// Stop

int AgentTeamLearning::stop() {

	if (!this->frozen) {
		// TODO
	}// end if

	return AgentBase::stop();
}// end stop


//-----------------------------------------------------------------------------
// Step

int AgentTeamLearning::step() {

	// Don't perform learning/task allocation every single step
	_timeb currentTime;
	_ftime64_s(&currentTime);
	bool round_started = (currentTime.time * 1000 + currentTime.millitm) > (this->round_start_time.time * 1000 + this->round_start_time.millitm);
	bool update_round_info = (currentTime.time * 1000 + currentTime.millitm) > (this->round_info_receive_time.time * 1000 + this->round_info_receive_time.millitm + this->round_timout);

	if (STATE(AgentTeamLearning)->round_number > 0) {
		// Round info is updated here after one timeout, to allow messages to come in, and cutting off any
		// that are too old (since they are rejected based on their round number)
		if (update_round_info && !this->round_info_set) {
			STATE(AgentTeamLearning)->round_number = this->new_round_number;
			this->lAllianceObject.myData.round_number = this->new_round_number;
			this->TLAgents = this->new_round_order;
			this->round_info_set = true;
		}

		if (round_started) {
			// See if any action is required this round
			this->checkRoundStatus();
		}
	}
	
	return AgentBase::step();
}// end step


/* checkRoundStatus
*
* Main method for determining when this agent should perform their task allocation. The ordered list of agents
* for this round is scanned to determine how many agents are ahead in line that still need to respond, which 
* which determines the maximum wait time. The response of each agent is marked in convGetTaskDataInfo.
*
* When this agent is the last in the list for the current round, they are responsible for initiating the next
* round (i.e. distributing a new randomized round order) by calling initiateNextRound.
*/
int AgentTeamLearning::checkRoundStatus() {

	// Only proceed if we have not participated in this round yet
	if (this->TLAgentData[STATE(AgentBase)->uuid].response)
		return 0;
	
	int n = 0;                 // Count of how many agents ahead of us left to participate
	bool last_agent = false;   // Flag for if we are the last agent in the round (and need to initiate the next round)
	
	// Find the number of agents ahead that still need to participate
	std::vector<UUID>::iterator iter;
	int count = 1;
	for (iter = this->TLAgents.begin(); iter != this->TLAgents.end(); ++iter) {
		// Stop counting once we've found ourself
		if (*iter == STATE(AgentBase)->uuid) {
			last_agent = (count == this->TLAgents.size());
			break;
		}

		// Increment counter (but reset when encountering an agent that has participated, because if they
		// defaulted to participating after a previous agent failed, we don't want to count that extra agent)
		n = !this->TLAgentData[*iter].response*(n + 1);

		count++;
	}

	// Perform task allocation, only when we are next in line or previous agents have timed out
	_timeb currentTime;
	_ftime64_s(&currentTime);
	if ((currentTime.time * 1000 + currentTime.millitm) > (this->last_response_time.time * 1000 + this->last_response_time.millitm + n*this->round_timout)) {
		
		if (n > 0)
			Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::checkRoundStatus: Round %d: Timed out waiting for response.", STATE(AgentTeamLearning)->round_number);
		
		// Record our previous task
		UUID prev_task_id = this->lAllianceObject.myData.taskId;

		// Perform the L-Alliance algorithm
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::checkRoundStatus: Round %d: Updating and performing task selection.", STATE(AgentTeamLearning)->round_number);
		this->lAllianceObject.updateTaskProperties(this->mTaskList);
		this->lAllianceObject.chooseTask(this->mTaskList);

		// Upload the new task data (DDBTask), and 
		UUID new_task_id = this->lAllianceObject.myData.taskId;
		if (new_task_id == nilUUID) {
			// When voluntarily leaving a task upload nil task data (DDBTask) to DDB to reset the task
			uploadTask(prev_task_id, nilUUID, nilUUID, false);
		}
		else {
			this->uploadTask(new_task_id, this->mTaskList[new_task_id]->agentUUID, this->mTaskList[new_task_id]->avatar, this->mTaskList[new_task_id]->completed);
		}

		// Upload the L-Alliance data (DDBTaskData)
		this->uploadTaskDataInfo();

		// Mark our participation
		this->TLAgentData[STATE(AgentBase)->uuid].response = true;

		// Do we need to initiate the next round?
		if (last_agent)
			this->initiateNextRound();
	}

	return 0;
}

/* initiateNextRound
*
* To be called when this agent is the last agent in the current round, after they have performed their task allocation.
* The list of agents is randomized, and sent to the other agents along with the new round number and the start time
* of the next round.
*
* When this method is called, it updates the round number for this agent. All other agents have their round numbers updated
* upon receiving the message sent by this method.
*/
int AgentTeamLearning::initiateNextRound() { 
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::initiateNextRound: Round %d: Sending info for the next round.", STATE(AgentTeamLearning)->round_number);

	// Increment our own round counters (others will increment when they receive the message)
	STATE(AgentTeamLearning)->round_number++;
	this->lAllianceObject.myData.round_number = STATE(AgentTeamLearning)->round_number;

	// Set the next round start time
	_ftime64_s(&this->round_start_time);
	this->round_start_time.millitm += this->delta_learn_time;

	// Randomize the agent list
	std::random_shuffle(this->TLAgents.begin(), this->TLAgents.end());

	// Send the info for the next round
	DataStream lds;
	std::vector<UUID>::iterator iter_outer;
	std::vector<UUID>::iterator iter_inner;
	int pos = 1;
	for (iter_outer = this->TLAgents.begin(); iter_outer != this->TLAgents.end(); ++iter_outer) {

		// Zero everyone's response
		this->TLAgentData[*iter_outer].response = false;

		// Don't send to ourselves
		if (*iter_outer == STATE(AgentBase)->uuid) {
			Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::initiateNextRound: Round %d: This round our position is %d.", STATE(AgentTeamLearning)->round_number, pos);
		}
		else {
			pos++;

			// Send the data
			lds.reset();
			lds.packInt32(STATE(AgentTeamLearning)->round_number);  // Next round number
			lds.packData(&this->round_start_time, sizeof(_timeb));  // Next round start time

			// Pack the new (randomized) list
			for (iter_inner = this->TLAgents.begin(); iter_inner != this->TLAgents.end(); ++iter_inner) {
				lds.packUUID(&*iter_inner);
			}

			this->sendMessageEx(this->hostCon, MSGEX(AgentTeamLearning_MSGS, MSG_ROUND_INFO), lds.stream(), lds.length(), &*iter_outer);
			lds.unlock();
		}
	}

	return 0;
}

/* uploadTask
*
* For the given task id, uploads the contents of a DDBTask strcut to the DDB
* (i.e. the assigned agent, avatar, and a completion flag).
*/
int AgentTeamLearning::uploadTask(UUID &task_id, UUID &agent_id, UUID &avatar_id, bool completed) {
	DataStream lds;
	lds.reset();
	lds.packUUID(&task_id);    // Task id
	lds.packUUID(&agent_id);   // Agent id
	lds.packUUID(&avatar_id);  // Avatar id
	lds.packBool(&completed);  // Completion flag
	this->sendMessage(this->hostCon, MSG_DDB_TASKSETINFO, lds.stream(), lds.length());
	lds.unlock();

	return 0;
}

/* uploadTaskDataInfo
*
* Uploads this agent's L-Alliance info (i.e. tau, motivation, impatience, etc.) in a DDBTaskData struct
* to the DDB.
*/
int AgentTeamLearning::uploadTaskDataInfo() {
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::uploadTaskDataInfo: Uploading task data info.");
	DataStream lds;
	lds.reset();
	_ftime64_s(&this->lAllianceObject.myData.updateTime);  // Update time
	lds.packUUID(&STATE(AgentTeamLearning)->ownerId);	   // Avatar id
	lds.packTaskData(&this->lAllianceObject.myData);       // DDBTaskData
	this->sendMessage(this->hostCon, MSG_DDB_TASKDATASETINFO, lds.stream(), lds.length());
	lds.unlock();

	return 0;
}

/* sendRequest
*
* Handles sending acquiescence and motivation reset requests to other other team learning agents.
*/
int AgentTeamLearning::sendRequest(UUID *agentId, int message, UUID *id) {
	DataStream lds;

	switch (message) {
	case AgentTeamLearning_MSGS::MSG_REQUEST_ACQUIESCENCE:
	{
		Log.log(0, "AgentTeamLearning::sendRequest: Sending acquiescence request to agent %s.", Log.formatUUID(0, agentId));
		UUID thread = this->conversationInitiate(AgentTeamLearning::AgentTeamLearning_CBR_convReqAcquiescence, DDB_REQUEST_TIMEOUT, &agentId, sizeof(UUID));
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(this->getUUID()); // Sender id
		lds.packUUID(&thread);
		lds.packUUID(&previousTaskId);
		this->sendMessageEx(this->hostCon, MSGEX(AgentTeamLearning_MSGS, MSG_REQUEST_ACQUIESCENCE), lds.stream(), lds.length(), agentId);
	}
	break;
	case AgentTeamLearning_MSGS::MSG_REQUEST_MOTRESET:
	{
		Log.log(0, "AgentTeamLearning::sendRequest: Sending motivation reset request to agent %s.", Log.formatUUID(0, agentId));
		UUID thread = this->conversationInitiate(AgentTeamLearning::AgentTeamLearning_CBR_convReqMotReset, DDB_REQUEST_TIMEOUT, &agentId, sizeof(UUID));
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(this->getUUID()); // Sender id
		lds.packUUID(&thread);
		lds.packUUID(id);
		this->sendMessageEx(this->hostCon, MSGEX(AgentTeamLearning_MSGS, MSG_REQUEST_MOTRESET), lds.stream(), lds.length(), agentId);
	}
	break;

	}
	lds.unlock();

	return 0;
}

/* logWrapper
*
* Enables the L-Alliance object to write log messages by passing in the desired log level
* along with the message.
*/
void AgentTeamLearning::logWrapper(int log_level, char* message) {
	Log.log(log_level, "AgentTeamLearning::L-ALLIANCE::%s", message);
}

int AgentTeamLearning::ddbNotification(char *data, int len) {
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
	if (evt == DDBE_WATCH_TYPE) {
		if (type == DDB_AGENT) {
			// request list of agents
			UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetAgentList, DDB_REQUEST_TIMEOUT);
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
		else if (type == DDB_TASK) {
			// request list of tasks
			UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetTaskList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(this->getUUID()); // dummy id, getting the full list of tasks anyway
			sds.packUUID(&thread);
			sds.packBool(true);			   //true == send list of tasks, otherwise only info about a specific task
			this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}
		else if (type == DDB_TASKDATA) {

			//	uploadTaskDataInfo();
			//apb->apbSleep(1000);

			// request list of taskdata
			UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetTaskDataList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(this->getUUID()); // dummy id, getting the full list of task datas anyway
			sds.packUUID(&thread);
			sds.packBool(true);			   //true == send list of taskdatas, otherwise only info about a specific taskdata set
			this->sendMessage(this->hostCon, MSG_DDB_TASKDATAGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}
	}
	if (type == DDB_TASK) {
		if (evt == DDBE_ADD) {

			// request task info
			UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetTaskInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(&uuid);			// Task id
			sds.packUUID(&thread);
			sds.packBool(false);			   //true == send list of taskdatas, otherwise only info about a specific task
			this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
			sds.unlock();
			this->lAllianceObject.addTask(uuid);	//Add new tasks to myData
			Log.log(0, "AgentTeamLearning::ddbNotification: added task with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &uuid));
		}
		else if (evt == DDBE_UPDATE) {
			//Log.log(0, "AgentTeamLearning::ddbNotification: task update with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &uuid));

			std::map<UUID, DDBTaskData, UUIDless>::const_iterator tmIter = this->lAllianceObject.teammatesData.find(uuid);

			if (uuid == this->lAllianceObject.myData.taskId && tmIter != this->lAllianceObject.teammatesData.end()) {		//If it's our own update, and if it's not in the teammatesData, do nothing

																												//Log.log(0, "AgentTeamLearning::ddbNotification: task is our own, discarding.");
			}
			else {
				// request task info
				UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetTaskInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				sds.reset();
				sds.packUUID(&uuid);			// Task id
				sds.packUUID(&thread);
				sds.packBool(false);			   //true == send list of tasks, otherwise only info about a specific task
				this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
				sds.unlock();
				// Updates are handled in updateTaskProperties, no need to interact with LALLIANCE here
				//Log.log(0, "AgentTeamLearning::ddbNotification: task update handled, request for more info sent.");


			}
		}
		else if (evt == DDBE_REM) {
			// TODO - remember to free the memory assigned by malloc!
		}
	}

	if (type == DDB_TASKDATA) {
		if (evt == DDBE_ADD || evt == DDBE_UPDATE) {
	    //if (evt == DDBE_UPDATE) {

			if (uuid != STATE(AgentTeamLearning)->ownerId) {		//If it's our own update, do nothing

																	// request taskdata info
				UUID thread = this->conversationInitiate(AgentTeamLearning_CBR_convGetTaskDataInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				sds.reset();
				sds.packUUID(&uuid);			// Taskdata (avatar) id
				sds.packUUID(&thread);
				sds.packBool(false);			   //true == send list of taskdatas, otherwise only info about a specific taskdata set
				this->sendMessage(this->hostCon, MSG_DDB_TASKDATAGETINFO, sds.stream(), sds.length());
				sds.unlock();
				//Log.log(0, "AgentTeamLearning::ddbNotification: taskdata update handled, request for more info sent.");
			}
			else
				;
			//Log.log(0, "AgentTeamLearning::ddbNotification: taskdata is our own, discarding.");
		}
		else if (evt == DDBE_REM) {
			// TODO
		}
	}
	lds.unlock();

	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentTeamLearning::conProcessMessage(spConnection con, unsigned char message, char *data, unsigned int len) {
	DataStream lds;


	if (!AgentBase::conProcessMessage(con, message, data, len)) // message handled
		return 0;

	switch (message) {
	case AgentTeamLearning_MSGS::MSG_CONFIGURE:
	{
		lds.setData(data, len);
		this->configureParameters(&lds);
		lds.unlock();
	}
	break;
	case AgentTeamLearning_MSGS::MSG_ROUND_INFO:
	{
		// This message contains the information about the next round
		//   -New round number
		//   -Round start time
		//   -Agent order

		// The round number and agent order will be updated in the step method, once the timeout time has passed
		this->round_info_set = false;

		// Record when we received this message
		_ftime64_s(&this->round_info_receive_time);

		lds.setData(data, len);

		int round = lds.unpackInt32();  // Next round number
		this->new_round_number = round;
		this->round_start_time = *(_timeb *)lds.unpackData(sizeof(_timeb)); // Next round start time

		// Unpack the new randomized list of agents
		this->TLAgents.clear();
		int pos;
		UUID new_id;
		std::vector<UUID>::iterator iter;
		for (int i = 0; i < this->TLAgentData.size(); i++) {
			lds.unpackUUID(&new_id);
			this->TLAgents.push_back(new_id);

			// Initialize to no response for the next round
			this->TLAgentData[new_id].response = false;

			// For logging
			if (new_id == STATE(AgentBase)->uuid) 
				pos = i + 1;
			
		}
		lds.unlock();
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::conProcessMessage: Round %d: Received info for new round, our position is %d.", round, pos);
	}
	break;
	case AgentTeamLearning_MSGS::MSG_REQUEST_ACQUIESCENCE:
	{
		UUID sender;
		UUID nilTask;
		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackData(sizeof(UUID));	//Discard thread (for now - do we need a conversation?)
		lds.unpackUUID(&nilTask);

		if (this->mTaskList.find(nilTask) != this->mTaskList.end()) {
			mTaskList[nilTask]->avatar = nilUUID;
			mTaskList[nilTask]->agentUUID = nilUUID;
		}

		Log.log(0, "AgentTeamLearning::conProcessMessage: Received acquiescence request from %s.", Log.formatUUID(0, &sender));
		this->lAllianceObject.acquiesce(this->lAllianceObject.myData.taskId);
		lds.unlock();
	}
	break;
	case AgentTeamLearning_MSGS::MSG_REQUEST_MOTRESET:
	{
		lds.setData(data, len);
		UUID sender;
		UUID taskId;
		lds.unpackUUID(&sender);
		lds.unpackData(sizeof(UUID));	//Discard thread (for now - do we need a conversation?)
		lds.unpackUUID(&taskId);
		Log.log(0, "AgentTeamLearning::conProcessMessage: Received motivation reset request from %s.", Log.formatUUID(0, &sender));
		this->lAllianceObject.motivationReset(taskId);
		lds.unlock();
	}
	break;
	case MSG_MISSION_DONE:
	{
		Log.log(0, " AgentTeamLearning::conProcessMessage: mission done, uploading learning data.");
		this->uploadTaskDataInfo();
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks

/* convGetAgentList
*
* Loops through all available agents, scanning for other team learning agents and
* adds them to the member property TLAgentData.
*
* This agent is set as a DDB_AGENT watcher, which results in this callback beign visited when
* a new agwnt is added to the DDB. Visitation of this callback is necessary for start, and is 
* checked in finishConfigureParameters().
*/
bool AgentTeamLearning::convGetAgentList(void *vpConv) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentTeamLearning::convGetAgentList: timed out");
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

		// The type UUID we're looking for
		UUID teamLearningAgentId;
		UuidFromString((RPC_WSTR)_T(AgentTeamLearning_UUID), &teamLearningAgentId);

		// Get number of agents
		int count;
		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetAgentList: recieved %d total agents.", count);

		std::list<UUID> tempTLAgentList;

		// Scan agents
		UUID thread;
		UUID agentId;
		AgentType agentType;
		UUID parent;
		for (int i = 0; i < count; i++) {
			lds.unpackUUID(&agentId);              // Id
			lds.unpackString();                    // Type
			lds.unpackUUID(&agentType.uuid);       // Id of their type
			agentType.instance = lds.unpackChar(); // Instance
			lds.unpackUUID(&parent);               // Parent agent Id

			// Need team learning agents (including myself)
			if (agentType.uuid == teamLearningAgentId) {
				// Add them to the list
				tempTLAgentList.push_back(agentId);

				// Save their data
				this->TLAgentData[agentId].id = agentId;
				this->TLAgentData[agentId].parentId = parent;
				this->TLAgentData[agentId].response = false;

				Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetAgentList: Found team learning agent %s.", Log.formatUUID(LOG_LEVEL_VERBOSE, &agentId));
			}
		}
		lds.unlock();
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetAgentList: Found %d other team learning agents.", this->TLAgentData.size() - 1);

		// Make the actual agent list a sorted list (so every agent starts with the same order),
		tempTLAgentList.sort(UUIDless());
		this->TLAgents.clear();
		std::list<UUID>::iterator iter;
		for (iter = tempTLAgentList.begin(); iter != tempTLAgentList.end(); ++iter) {
			this->TLAgents.push_back(*iter);
		}

		// Set the next round start time
		_ftime64_s(&this->round_start_time);
		this->round_start_time.millitm += this->delta_learn_time;
		STATE(AgentTeamLearning)->round_number++;
		this->new_round_number++;
		this->lAllianceObject.myData.round_number++;

		STATE(AgentTeamLearning)->receivedAllTeamLearningAgents = true;
		this->finishConfigureParameters();

	}// end response Ok

	return 0;
}

/* convGetTaskList
*
* Handles notifications from the DDB about new tasks. The task will be added to the mTaskLis
* map, and the task will be added to the L-Alliance object as well.
*
* This agent is set as a DDB_TASK watcher, which results in this callback beign visited when
* a new task is added to the DDB.
*/
bool AgentTeamLearning::convGetTaskList(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentTeamLearning::convGetTaskList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		int i, count;

		if (lds.unpackBool() != true) {	//True if we requested a list of tasks as opposed to just one
			lds.unlock();
			return 0; // what happened here?
		}

		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskList: received %d tasks", count);

		UUID taskId;
		TASK task = (DDBTask *)malloc(sizeof(DDBTask));

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&taskId);
			// Guard against nil uploads
			if (taskId != nilUUID) {
				// Add the new task to our data
				task = (DDBTask *)lds.unpackData(sizeof(DDBTask));
				free(mTaskList[taskId]);
				this->mTaskList[taskId] = (DDBTask *)malloc(sizeof(DDBTask));

				this->mTaskList[taskId]->landmarkUUID = task->landmarkUUID;
				this->mTaskList[taskId]->agentUUID = task->agentUUID;
				this->mTaskList[taskId]->avatar = task->avatar;
				this->mTaskList[taskId]->type = task->type;
				this->mTaskList[taskId]->completed = task->completed;

				// Add the task to L-Alliance
				this->lAllianceObject.addTask(taskId);
			}
		}
		lds.unlock();
		STATE(AgentTeamLearning)->hasReceivedTaskList = true;
		this->finishConfigureParameters();
	}
	else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

/* convGetTaskDataList
*
* Handles notifications from the DDB about new L-Alliance task data entries (i.e. tau, motivation,
* impatience, etc.).
*
* This agent is set as a DDB_TASKDATA watcher, which results in this callback beign visited when
* new (L-Alliance) task data is added to the DDB.
*/
bool AgentTeamLearning::convGetTaskDataList(void * vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentTeamLearning::convGetTaskDataList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread	

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		int i, count;

		if (lds.unpackBool() != true) {	//True if we requested a list of taskdatas as opposed to just one
			lds.unlock();
			return 0; // what happened here?
		}

		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList: received %d taskdatas", count);

		UUID avatarId;
		DDBTaskData taskData;

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&avatarId);
			Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList:  avatarId is: %s", Log.formatUUID(0, &avatarId));
			lds.unpackTaskData(&taskData);
			if (avatarId == STATE(AgentTeamLearning)->ownerId) {	//Our own data (perhaps recovered from a crash?)
				this->lAllianceObject.myData = taskData;
				STATE(AgentTeamLearning)->isMyDataFound = true;		//We received our previously stored data (if there was any)
			}
			else {
				this->lAllianceObject.teammatesData[avatarId] = taskData;
			}
		}
		lds.unlock();
		STATE(AgentTeamLearning)->hasReceivedTaskDataList = true;
		this->finishConfigureParameters();
	}
	else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

/* convGetTaskInfo
*
* For receiving updates about tasks (assigned agent, assigned avatar, etc.).
* Receives an updated DDBTask, and puts it in the corresponding entry of mTaskList.
* 
* Used when there is a DDB notification for DDB_TASK, and it is a DDBE_ADD or DDBE_UPDATE.
*/
bool AgentTeamLearning::convGetTaskInfo(void * vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentTeamLearning::convGetTaskInfo: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread
	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		if (lds.unpackBool() != false) {	//True if we requested a list of tasks, false if we requested info about a single task
			lds.unlock();
			return 0; // what happened here?
		}

		UUID taskId;
		TASK task;
		lds.unpackUUID(&taskId);

		task = (DDBTask *)lds.unpackData(sizeof(DDBTask));
		free(mTaskList[taskId]);
		this->mTaskList[taskId] = (DDBTask *)malloc(sizeof(DDBTask));

		this->mTaskList[taskId]->landmarkUUID = task->landmarkUUID;
		this->mTaskList[taskId]->agentUUID = task->agentUUID;
		this->mTaskList[taskId]->avatar = task->avatar;
		this->mTaskList[taskId]->type = task->type;
		this->mTaskList[taskId]->completed = task->completed;

		lds.unlock();

		// Has our task been completed?
		if (taskId == this->lAllianceObject.myData.taskId && task->completed) {
			this->lAllianceObject.finishTask();
		}

	}
	else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

/* convGetTaskDataInfo
*
* For receiving updates about L-Alliance tasks data (i.e. tau, motivation,
* impatience, etc.) for other agents' tasks.
*
* Used when there is a DDB notification for DDB_TASKDATA, and it is a DDBE_ADD or DDBE_UPDATE.
*/
bool AgentTeamLearning::convGetTaskDataInfo(void * vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetTaskDataInfo: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		if (lds.unpackBool() != false) {	// True if we requested a list of taskdatas, false if we requested info about a single taskdata set
			Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetTaskDataInfo: EnumTaskData is true, but shouldn't be.");
			lds.unlock();
			return 0; // what happened here?
		}

		UUID avatarId;
		DDBTaskData taskData;
		lds.unpackUUID(&avatarId);
		lds.unpackTaskData(&taskData);
		// Only add data about other agent's tasks, ours is handled in ddbNotification
		if (avatarId != STATE(AgentTeamLearning)->ownerId) {

			if (taskData.round_number == STATE(AgentTeamLearning)->round_number) {
				// Valid data
				lAllianceObject.teammatesData[avatarId] = taskData;

				// Mark that we have received a response from this agent
				this->TLAgentData[taskData.agentId].response = true;
				_ftime64_s(&this->last_response_time);
			}
			else {
				// Invalid data, corrupt or from an old round
				Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetTaskDataInfo: Round %d: Received invalid round number (%d != %d) from %s", STATE(AgentTeamLearning)->round_number, taskData.round_number, STATE(AgentTeamLearning)->round_number, Log.formatUUID(0, &taskData.agentId));
			}
			
		}
		lds.unlock();
	} else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

// TODO - probably remove these
bool AgentTeamLearning::convReqAcquiescence(void * vpConv) {
	return false;
}
bool AgentTeamLearning::convReqMotReset(void * vpConv) {
	return false;
}

bool AgentTeamLearning::convGetRunNumber(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetRunNumber: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread
	STATE(AgentTeamLearning)->runNumber = lds.unpackInt32();
	Log.log(0, "My run number is: %d", STATE(AgentTeamLearning)->runNumber);
	STATE(AgentTeamLearning)->hasReceivedRunNumber = true;
	return false;
}

//-----------------------------------------------------------------------------
// State functions

int AgentTeamLearning::freeze(UUID *ticket) {
	return AgentBase::freeze(ticket);
}// end freeze

int AgentTeamLearning::thaw(DataStream *ds, bool resumeReady) {
	return AgentBase::thaw(ds, resumeReady);
}// end thaw

int	AgentTeamLearning::writeState(DataStream *ds, bool top) {
	if (top) _WRITE_STATE(AgentTeamLearning);

	return AgentBase::writeState(ds, false);;
}// end writeState

int	AgentTeamLearning::readState(DataStream *ds, bool top) {
	if (top) _READ_STATE(AgentTeamLearning);

	return AgentBase::readState(ds, false);
}// end readState

int AgentTeamLearning::recoveryFinish() {
	if (AgentBase::recoveryFinish())
		return 1;

	return 0;
}// end recoveryFinish

int AgentTeamLearning::writeBackup(DataStream *ds) {

	// configuration
	ds->packUUID(&STATE(AgentTeamLearning)->ownerId);

	ds->packBool(STATE(AgentTeamLearning)->parametersSet);
	ds->packBool(STATE(AgentTeamLearning)->startDelayed);
	ds->packInt32(STATE(AgentTeamLearning)->updateId);
	ds->packBool(STATE(AgentTeamLearning)->isSetupComplete);


	return AgentBase::writeBackup(ds);
}// end writeBackup

int AgentTeamLearning::readBackup(DataStream *ds) {
	Log.log(0, "AgentTeamLearning::readBackup: failures likely now...");
	DataStream lds;

	// configuration
	ds->unpackUUID(&STATE(AgentTeamLearning)->ownerId);

	STATE(AgentTeamLearning)->parametersSet = ds->unpackBool();
	STATE(AgentTeamLearning)->startDelayed = ds->unpackBool();
	STATE(AgentTeamLearning)->updateId = ds->unpackInt32();
	STATE(AgentTeamLearning)->isSetupComplete = ds->unpackBool();


	if (STATE(AgentTeamLearning)->isSetupComplete) {
		this->finishConfigureParameters();
	}
	else {

	}

	return AgentBase::readBackup(ds);
}// end readBackup


//*****************************************************************************
// Threading

DWORD WINAPI RunThread(LPVOID vpAgent) {
	AgentTeamLearning *agent = (AgentTeamLearning *)vpAgent;

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
	AgentTeamLearning *agent = new AgentTeamLearning(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

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
	AgentTeamLearning *agent = new AgentTeamLearning(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

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


// CAgentTeamLearningDLL: Override ExitInstance for debugging memory leaks
CAgentTeamLearningDLL::CAgentTeamLearningDLL() {}

// The one and only CAgentTeamLearningDLL object
CAgentTeamLearningDLL theApp;

int CAgentTeamLearningDLL::ExitInstance() {
	TRACE(_T("--- ExitInstance() for regular DLL: CAgentTeamLearning ---\n"));
	return CWinApp::ExitInstance();
}