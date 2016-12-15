// AgentTeamLearning.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "AgentTeamLearning.h"
#include "AgentTeamLearningVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"

#include <ctime>

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
	STATE(AgentTeamLearning)->parametersSet = false;
	STATE(AgentTeamLearning)->updateId = -1;

	STATE(AgentTeamLearning)->isMyDataFound = false;

	// Prepare callbacks
	this->callback[AgentTeamLearning_CBR_convGetTaskInfo] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskInfo);
	this->callback[AgentTeamLearning_CBR_convGetTaskDataInfo] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskDataInfo);
	this->callback[AgentTeamLearning_CBR_convGetTaskList] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskList);
	this->callback[AgentTeamLearning_CBR_convGetTaskDataList] = NEW_MEMBER_CB(AgentTeamLearning, convGetTaskDataList);
	this->callback[AgentTeamLearning_CBR_convReqAcquiescence] = NEW_MEMBER_CB(AgentTeamLearning, convReqAcquiescence);
	this->callback[AgentTeamLearning_CBR_convReqMotReset] = NEW_MEMBER_CB(AgentTeamLearning, convReqMotReset);

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
	lAllianceObject.id = STATE(AgentTeamLearning)->ownerId;
	lAllianceObject.myData.agentId = *this->getUUID();
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::configureParameters: ownerId %s", Log.formatUUID(LOG_LEVEL_NORMAL, &STATE(AgentTeamLearning)->ownerId));
	STATE(AgentTeamLearning)->isSetupComplete = false; // need taskdata info and task info

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


	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::configureParameters: teammatesData is empty == %s", lAllianceObject.teammatesData.empty() ? "true" : "false");

	this->itNoTeammateChange = 0;
	this->isReadyToBroadcast = false;	//Default start value, will be adjusted as necessary in negotiateTasks()
	this->broadCastUnlocked = false;	//Default start value, will be adjusted as necessary in uploadTaskDataInfo()
	this->broadCastPending = false;		//Default start value, will be adjusted as necessary in uploadTaskDataInfo()
	this->avatarToListenFor = nilUUID;	
	this->timeoutPeriod = 0;
	this->hasStabilityOccurred = false;
	_ftime64_s(&lastTaskUpdateSent);	//Initialize
	//Request lists of tasks and task data when confirmation from the DDB is received

	this->backup(); // initialSetup
	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::configureParameters: done.");
	// finishConfigureParameters will be called once task, taskdata and agent info is received

	return 0;
}// end configureParameters

int	AgentTeamLearning::finishConfigureParameters() {

	if (STATE(AgentTeamLearning)->hasReceivedTaskList == true && STATE(AgentTeamLearning)->hasReceivedTaskDataList == true && STATE(AgentTeamLearning)->parametersSet == false) {		//Only proceed to start if we have tasks and taskdata, and have not yet started (prevents double start)
		STATE(AgentTeamLearning)->isSetupComplete = true;
		this->previousTaskId = nilUUID;
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::finishConfigureParameters");
		STATE(AgentTeamLearning)->parametersSet = true;

		if (STATE(AgentTeamLearning)->startDelayed) {
			STATE(AgentTeamLearning)->startDelayed = false;
			this->start(STATE(AgentBase)->missionFile);
		}// end if
		this->backup(); // initialSetup
	}
	return 0;
} // end finishConfigureParameters


int AgentTeamLearning::uploadTask(UUID *taskId, DDBTask *task) {
	UUID *myUUID = this->getUUID();
	Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::uploadTaskInfo: Uploading task: %s, Responsible avatar: %s, Responsible agent: %s.", Log.formatUUID(LOG_LEVEL_NORMAL, taskId), Log.formatUUID(LOG_LEVEL_NORMAL, &task->avatar), Log.formatUUID(LOG_LEVEL_NORMAL, &task->agentUUID));
	
	DataStream lds;
	lds.reset();
	lds.packUUID(taskId); // Task id
	lds.packUUID(&task->agentUUID); // Agent id
	lds.packUUID(&task->avatar); // Avatar id
	lds.packBool(task->completed);
	this->sendMessage(this->hostCon, MSG_DDB_TASKSETINFO, lds.stream(), lds.length());
	lds.unlock();
	return 0;
}

int AgentTeamLearning::uploadTaskDataInfo()
{
	if (broadCastUnlocked == true) {

		DataStream lds;

	/*	Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::uploadTaskDataInfo: My avatar id is: %s", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId));
		Log.log(0, "AgentTeamLearning::uploadTaskDataInfo: My task id is: %s", Log.formatUUID(0, &lAllianceObject.myData.taskId));*/
		//Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::uploadTaskDataInfo: teammatesData are: ");
		std::map<UUID, DDBTaskData, UUIDless>::iterator tmIter;
		UUID teammate;
		if (lAllianceObject.teammatesData.empty() == false) {
			for (tmIter = lAllianceObject.teammatesData.begin(); tmIter != lAllianceObject.teammatesData.end(); tmIter++) {
				teammate = tmIter->first;
					/*Log.log(0, "AgentTeamLearning::uploadTaskDataInfo teammateId: %s", Log.formatUUID(0, &teammate));
					Log.log(0, "AgentTeamLearning::uploadTaskDataInfo teammate agentId: %s", Log.formatUUID(0, &tmIter->second.agentId));
					Log.log(0, "AgentTeamLearning::uploadTaskDataInfo taskId: %s", Log.formatUUID(0, &tmIter->second.taskId));*/
			}
		}

		_ftime64_s(&lAllianceObject.myData.updateTime);	//Update time
		lds.reset();
		lds.packUUID(&STATE(AgentTeamLearning)->ownerId);	//Avatar id
		lds.packTaskData(&lAllianceObject.myData);
		this->sendMessage(this->hostCon, MSG_DDB_TASKDATASETINFO, lds.stream(), lds.length());
		lds.unlock();
		broadCastUnlocked = false;	//Set to true at next step()
		broadCastPending = false;	//We have broadcast already, none pending right now
	}
	else {
		broadCastPending = true;
		Log.log(0, "AgentTeamLearning::uploadTaskDataInfo:: Setting broadcast pending...");
	}

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

		Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::start: uploading... my avatar id is: %s", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId));
		Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::start: uploading... my task id is: %s", Log.formatUUID(0, &lAllianceObject.myData.taskId));
		lds.reset();
		lds.packUUID(&STATE(AgentTeamLearning)->ownerId);
		_ftime64_s(&lAllianceObject.myData.updateTime);	//Update time
		lAllianceObject.myData.taskId = nilUUID;
		lAllianceObject.myData.agentId = *this->getUUID();
		lds.packTaskData(&lAllianceObject.myData);
		this->sendMessage(this->hostCon, MSG_DDB_ADDTASKDATA, lds.stream(), lds.length());	//... then upload
		lds.unlock();
	}

	Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::start: teammatesData is empty == %s", lAllianceObject.teammatesData.empty() ? "true" : "false");


	Log.log(0, "AgentTeamLearning::start completed.");

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
	this->broadCastUnlocked = true;		//Unlock broadcast - only broadcast once per step, maximum. Will lock again in uploadTaskData
	if (this->broadCastPending)
		this->uploadTaskDataInfo();

	if (STATE(AgentBase)->started){
//		Log.log(0, "AgentTeamLearning::step()");
	}

	return AgentBase::step();
}// end step


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

void AgentTeamLearning::logWrapper(int log_level, char* message) {
	Log.log(log_level, "AgentTeamLearning::L-ALLIANCE::%s", message);
}

void AgentTeamLearning::negotiateTasks() {

	// Update teammates data
	if (lAllianceObject.teammatesData.size() == lastTeammateDataSize) { 
		// No new teammates discovered, taskdata update came from known teammate
		itNoTeammateChange++;	//increment counter for iterations with no teammate changes
	}
	else {
		itNoTeammateChange = 0;
		hasStabilityOccurred = false;
	}
	lastTeammateDataSize = lAllianceObject.teammatesData.size();

	if (hasStabilityOccurred) {
		_timeb currentTime;
		_ftime64_s(&currentTime);

		if (currentTime.time * 1000 + currentTime.millitm - (lastTaskUpdateSent.time * 1000 + lastTaskUpdateSent.millitm) > this->timeoutPeriod)
			this->isReadyToBroadcast = true;	//Others have timed out, send again
	}

	if (itNoTeammateChange >= TEAMMATE_NOCHANGE_COUNT) {
		if (!hasStabilityOccurred) {
			/*Log.log(0, "AgentTeamLearning::negotiateTasks: my teammateData size is: %d", lAllianceObject.teammatesData.size());
			Log.log(0, "AgentTeamLearning::negotiateTasks: STABILITY REACHED");*/

			std::list<UUID> avatarList;
			avatarList.push_back(STATE(AgentTeamLearning)->ownerId);

			std::map<UUID, DDBTaskData, UUIDless>::const_iterator tmIter;
			for (tmIter = lAllianceObject.teammatesData.begin(); tmIter != lAllianceObject.teammatesData.end(); tmIter++) {
				avatarList.push_back(tmIter->first);
			}
			avatarList.sort(UUIDless());

			UUID avatarListeningForUs;

			if (avatarList.front() == STATE(AgentTeamLearning)->ownerId) {
				//Log.log(0, "AgentTeamLearning::negotiateTasks: I'm in front!!!");
				this->isReadyToBroadcast = true;
				this->avatarToListenFor = avatarList.back();
				std::list<UUID>::const_iterator avatarItFront = avatarList.begin();
				avatarItFront++;
				avatarListeningForUs = *avatarItFront;
			}
			else {
				//Log.log(0, "AgentTeamLearning::negotiateTasks: I'm NOT in front!!!");
				this->isReadyToBroadcast = false;
				int count = 0;
				for (std::list<UUID>::const_iterator avatarIt = avatarList.begin(); avatarIt != avatarList.end(); avatarIt++) {
					count++;
					if (*avatarIt == STATE(AgentTeamLearning)->ownerId) {
						avatarIt--;	//Find the previous avatar in the list
						avatarToListenFor = *avatarIt;
						avatarIt++;
						avatarIt++;
						if (avatarIt == avatarList.end())
							avatarIt = avatarList.begin();
						avatarListeningForUs = *avatarIt;

						//Log.log(0, "AgentTeamLearning::negotiateTasks: Agent %s has avatarToListenFor %s and avatarListeningForUs %s", Log.formatUUID(0, this->getUUID()), Log.formatUUID(0, &avatarToListenFor), Log.formatUUID(0, &avatarListeningForUs));


						break;
					}
				}
				this->timeoutPeriod = count*TEAMMATE_CRASH_TIMEOUT;
			}

			for (auto& agentIter : lAllianceObject.teammatesData){
		//	std::map<UUID, DDBTaskData, UUIDless>::iterator agentIter;
		//	for (agentIter = lAllianceObject.teammatesData.begin(); agentIter != lAllianceObject.teammatesData.end(); agentIter++) {
				//Log.log(0, "AgentTeamLearning::negotiateTasks: Iterating through teammates to find agentListeningForUs, current avatar id is %s, target is %s", Log.formatUUID(0, &(UUID)agentIter.first), Log.formatUUID(0, &avatarListeningForUs));

				if ((UUID)agentIter.first == avatarListeningForUs) {
					//Log.log(0, "AgentTeamLearning::negotiateTasks: Iterating through teammates to find agentListeningForUs, found the target!");

					agentListeningForUs = agentIter.second.agentId;
					break;
				}
				
			}

			//Log.log(0, "AgentTeamLearning::negotiateTasks: Agent %s has avatarToListenFor %s and agentListeningForUs %s", Log.formatUUID(0, this->getUUID()), Log.formatUUID(0, &avatarToListenFor), Log.formatUUID(0, &agentListeningForUs));

			this->hasStabilityOccurred = true;
		}
	}
	else {
		uploadTaskDataInfo();		//Upload empty (or static) taskdata until stabilization (or re-stabilization)
	}

	if (this->isReadyToBroadcast) {
		//Log.log(0, "AgentTeamLearning::negotiateTasks: Avatar %s ready to broadcast.", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId));

		DDBTask previousTask;
		previousTaskId = lAllianceObject.myData.taskId;
		previousTask.avatar = nilUUID;

		if (this->mTaskList.find(previousTaskId) != this->mTaskList.end()) {
			previousTask = *mTaskList.find(previousTaskId)->second;
		}
		//Log.log(0, "AgentTeamLearning::negotiateTasks: Avatar %s has previous task %s with avatar id %s", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId), Log.formatUUID(0, &previousTaskId), Log.formatUUID(0, &previousTask.avatar));

		// Perform L-Alliance algorithm
		lAllianceObject.updateTaskProperties(mTaskList);
		lAllianceObject.chooseTask(mTaskList);

		// Upload previous task info
		if (previousTaskId != lAllianceObject.myData.taskId) {  
			if (previousTaskId != nilUUID) {
				// We have changed our task by own choice - upload info about our former task being unassigned
				Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::negotiateTasks: New task: %s", Log.formatUUID(LOG_LEVEL_NORMAL, &lAllianceObject.myData.taskId));

				mTaskList[previousTaskId]->avatar = nilUUID;
				mTaskList[previousTaskId]->agentUUID = nilUUID;
				previousTask.agentUUID = nilUUID,
				previousTask.avatar = nilUUID;
				uploadTask(&previousTaskId, &previousTask);
			}
		}

		// Upload new task info
		if (lAllianceObject.myData.taskId != nilUUID) {
			mTaskList[lAllianceObject.myData.taskId]->agentUUID = *this->getUUID();
			mTaskList[lAllianceObject.myData.taskId]->avatar = STATE(AgentTeamLearning)->ownerId;
			//Log.log(0, "AgentTeamLearning::negotiateTasks: Avatar %s broadcasting task %s ", Log.formatUUID(0, &STATE(AgentTeamLearning)->ownerId), Log.formatUUID(0, &lAllianceObject.myData.taskId));
			uploadTask(&lAllianceObject.myData.taskId, mTaskList[lAllianceObject.myData.taskId]);	//send our task to the DBB
		}
		else if (lAllianceObject.myData.taskId == nilUUID){		
			//Create nilTask to upload, just to have the succeeding avatar continue - we never read from this task
			DataStream lds;
			lds.reset();
			lds.packUUID(this->getUUID());	// Sender id
			this->sendMessageEx(this->hostCon, MSGEX(AgentTeamLearning_MSGS, MSG_SET_BROADCAST_READY), lds.stream(), lds.length(), &agentListeningForUs);
			lds.unlock();
		}

		uploadTaskDataInfo();
		_ftime64_s(&this->lastTaskUpdateSent);
		this->isReadyToBroadcast = false;		//Set as true again in convGetTaskInfo
		this->timeoutPeriod = (lAllianceObject.teammatesData.size() + 1)*TEAMMATE_CRASH_TIMEOUT;	//Wait for the whole list to time out (nearly), plus margin - worst case scenario
	}

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
		lAllianceObject.acquiesce(lAllianceObject.myData.taskId);
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
		lAllianceObject.motivationReset(taskId);
		lds.unlock();
	}
	break;
	case AgentTeamLearning_MSGS::MSG_SET_BROADCAST_READY:
	{
		lds.setData(data, len);
		UUID sender;
		lds.unpackUUID(&sender);
		Log.log(0, "AgentTeamLearning::conProcessMessage: Received MSG_SET_BROADCAST_READY from %s.", Log.formatUUID(0, &sender));
		this->isReadyToBroadcast = true;
	//	negotiateTasks();
		lds.unlock();
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks


bool AgentTeamLearning::convGetTaskInfo(void * vpConv)
{


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

//		Log.log(0, "AgentTeamLearning::convGetTaskInfo: 1 updated task with uuid %s, responsible avatar %s and responsible agent %s", Log.formatUUID(LOG_LEVEL_NORMAL, &taskId), Log.formatUUID(LOG_LEVEL_NORMAL, &mTaskList[taskId]->avatar), Log.formatUUID(LOG_LEVEL_NORMAL, &mTaskList[taskId]->agentUUID));
		//Log.log(0, "AgentTeamLearning::convGetTaskInfo: Got task with UUID %s", Log.formatUUID(0, &taskId));
		//Log.log(0, "AgentTeamLearning::convGetTaskInfo: Its assigned avatar is: %s", Log.formatUUID(0, &task->avatar));


			if (taskId == lAllianceObject.myData.taskId) {
				if (task->completed)
					lAllianceObject.finishTask();
			}


		if (task->avatar == this->avatarToListenFor) {
//			Log.log(0, "AgentTeamLearning::convGetTaskInfo: setting isReadyToBroadcast to true");

			this->isReadyToBroadcast = true;
		}
	}
	else {
		lds.unlock();
		// TODO try again?
	}
	//lAllianceObject.updateTaskProperties(mTaskList);



	return 0;
}
bool AgentTeamLearning::convGetTaskDataInfo(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		if (lds.unpackBool() != false) {	//True if we requested a list of taskdatas, false if we requested info about a single taskdata set
			Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: EnumTaskData is true, but shouldn't be");
			lds.unlock();
			return 0; // what happened here?
		}

		UUID avatarId;
		DDBTaskData taskData;

		lds.unpackUUID(&avatarId);
		lds.unpackTaskData(&taskData);
		if (avatarId == STATE(AgentTeamLearning)->ownerId) {	//Our own data - should never be passed here, sorted out in ddbNotification
			//lAllianceObject.myData = taskData;				//Do nothing - this avatar should be the one updating
			Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: own data, should not be able to see this...");
		}
		else {
			lAllianceObject.teammatesData[avatarId] = taskData;
		}
		lds.unlock();
	}
	else {
		lds.unlock();
		// TODO try again?
	}

	this->negotiateTasks();






	//Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: 1 My agent id is %s, my task id is now %s:", Log.formatUUID(0,&lAllianceObject.myData.agentId),Log.formatUUID(0,&lAllianceObject.myData.taskId));


	//lAllianceObject.updateTaskProperties(mTaskList);
	//Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: 2 My agent id is %s, my task id is now %s:", Log.formatUUID(0, &lAllianceObject.myData.agentId), Log.formatUUID(0, &lAllianceObject.myData.taskId));
	//lAllianceObject.chooseTask(mTaskList);
	//Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: 3 My agent id is %s, my task id is now %s:", Log.formatUUID(0, &lAllianceObject.myData.agentId), Log.formatUUID(0, &lAllianceObject.myData.taskId));

	//std::map<UUID, DDBTaskData, UUIDless>::iterator iter = lAllianceObject.teammatesData.find(lAllianceObject.myData.taskId);

	//if (previousTaskId != lAllianceObject.myData.taskId || iter != lAllianceObject.teammatesData.end()){		//If task assignment changed, or the task is found amongst or teammates
	//	mTaskList[lAllianceObject.myData.taskId]->agentUUID = *this->getUUID();
	//	mTaskList[lAllianceObject.myData.taskId]->avatar = STATE(AgentTeamLearning)->ownerId;
	//	uploadTask(&lAllianceObject.myData.taskId, mTaskList[lAllianceObject.myData.taskId]);
	////	uploadTaskDataInfo();
	//	previousTaskId = lAllianceObject.myData.taskId;
	//	Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: 4 My agent id is %s, my task id is now %s:", Log.formatUUID(0, &lAllianceObject.myData.agentId), Log.formatUUID(0, &lAllianceObject.myData.taskId));

	//}
	//else {
	//	Log.log(0, "AgentTeamLearning::convGetTaskDataInfo: 5 My agent id is %s, my task id is now %s:", Log.formatUUID(0, &lAllianceObject.myData.agentId), Log.formatUUID(0, &lAllianceObject.myData.taskId));

	////	uploadTaskDataInfo();
	//}



	return 0;
}
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
			if (taskId != nilUUID) {		//Guard against nil uploads from negotiateTasks()
				task = (DDBTask *)lds.unpackData(sizeof(DDBTask));
				free(mTaskList[taskId]);
				this->mTaskList[taskId] = (DDBTask *)malloc(sizeof(DDBTask));

				this->mTaskList[taskId]->landmarkUUID = task->landmarkUUID;
				this->mTaskList[taskId]->agentUUID = task->agentUUID;
				this->mTaskList[taskId]->avatar = task->avatar;
				this->mTaskList[taskId]->type = task->type;
				this->mTaskList[taskId]->completed = task->completed;

				lAllianceObject.addTask(taskId);	//Add new tasks to myData



	/*			Log.log(0, "AgentTeamLearning::convGetTaskList: added task with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &taskId));

				Log.log(0, "AgentTeamLearning::convGetTaskList: task contents: landmark UUID: %s,agent UUID: %s, avatar UUID: %s, completed: %s, ITEM_TYPES: %d", Log.formatUUID(LOG_LEVEL_NORMAL, &mTaskList[taskId]->landmarkUUID), Log.formatUUID(LOG_LEVEL_NORMAL, &mTaskList[taskId]->agentUUID), Log.formatUUID(LOG_LEVEL_NORMAL, &mTaskList[taskId]->avatar), mTaskList[taskId]->completed ? "true" : "false", mTaskList[taskId]->type);*/
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

bool AgentTeamLearning::convGetTaskDataList(void * vpConv)
{
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
				Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList: unpacking step 4a");
				lAllianceObject.myData = taskData;
				STATE(AgentTeamLearning)->isMyDataFound = true;		//We received our previously stored data (if there was any)
			}
			else {
	/*			Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList: unpacking step 4b");
				Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList: avatarId is: %s", Log.formatUUID(0, &avatarId));
				Log.log(0, "AgentTeamLearning::convGetTaskDataList taskId: %s", Log.formatUUID(0, &taskData.taskId));*/
				std::map<UUID, float, UUIDless>::const_iterator tauIter;
				for (tauIter = taskData.tau.begin(); tauIter != taskData.tau.end(); tauIter++) {
					//Log.log(0, "AgentTeamLearning::convGetTaskDataList tauIter: %f", tauIter->second);
				}
				std::map<UUID, float, UUIDless>::const_iterator motIter;
				for (motIter = taskData.motivation.begin(); motIter != taskData.motivation.end(); motIter++) {
					//Log.log(0, "AgentTeamLearning::convGetTaskDataList motIter: %f", motIter->second);
				}
				std::map<UUID, float, UUIDless>::const_iterator impIter;
				for (impIter = taskData.impatience.begin(); impIter != taskData.impatience.end(); impIter++) {
					//Log.log(0, "AgentTeamLearning::convGetTaskDataList impIter: %f", impIter->second);
				}
				std::map<UUID, int, UUIDless>::const_iterator attIter;
				for (attIter = taskData.attempts.begin(); attIter != taskData.attempts.end(); attIter++) {
					//Log.log(0, "AgentTeamLearning::convGetTaskDataList attIter: %d", attIter->second);
				}
				//Log.log(0, "AgentTeamLearning::convGetTaskDataList psi: %d", taskData.psi);
				//Log.log(0, "AgentTeamLearning::convGetTaskDataList tauStdDev: %f", taskData.tauStdDev);

				lastTeammateDataSize++;
				lAllianceObject.teammatesData[avatarId] = taskData;
			}
			//Log.log(LOG_LEVEL_VERBOSE, "AgentTeamLearning::convGetTaskDataList: unpacking step 5");
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
bool AgentTeamLearning::convReqAcquiescence(void * vpConv)
{
	return false;
}
bool AgentTeamLearning::convReqMotReset(void * vpConv)
{
	return false;
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
		if (type == DDB_TASK) {
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
			lAllianceObject.addTask(uuid);	//Add new tasks to myData
			Log.log(0, "AgentTeamLearning::ddbNotification: added task with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &uuid));
		}
		else if (evt == DDBE_UPDATE) {
			//Log.log(0, "AgentTeamLearning::ddbNotification: task update with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &uuid));

			std::map<UUID, DDBTaskData, UUIDless>::const_iterator tmIter = lAllianceObject.teammatesData.find(uuid);

			if (uuid == lAllianceObject.myData.taskId && tmIter != lAllianceObject.teammatesData.end()) {		//If it's our own update, and if it's not in the teammatesData, do nothing

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