// AgentIndividualLearning.cpp : Defines the initialization routines for the DLL.

/* The IndividualLearningAgent is responsible for all the learning
*  functionality for each robot. One instance of IndividualLearning will
*  exist for each robot.
*
*  The interaction with AvatarBase will be through the getAction
*  and learn methods, which will return the "learned" action to be
*  performed, or perform learning from the previous action.
*
*  The actual learning updates and learning data is contained in a
*  seperate class, since IndividualLearningAgent is intended to be an
*  interface with AvatarBase, which can be filled out for different
*  learning mechanisms.
*
*  IndividualLearningAgent also contains the functionality for:
*      - The policy for selecting actions
*      - Extracting the state variables
*      - Compressing the state variables to a vector of integers
*/

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"

#include "AgentIndividualLearning.h"
#include "AgentIndividualLearningVersion.h"

#include "..\\AvatarBase\\AvatarBaseVersion.h"
#include "..\\AvatarSimulation\\AvatarSimulationVersion.h"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#define COLLECTION_THRESHOLD 0.15f // m
#define DELIVERY_THRESHOLD 0.15f // m

using namespace AvatarBase_Defs;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//*****************************************************************************
// AgentIndividualLearning

//-----------------------------------------------------------------------------
// Constructor	

AgentIndividualLearning::AgentIndividualLearning(spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile) : AgentBase(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile) {
	// allocate state
	ALLOCATE_STATE(AgentIndividualLearning, AgentBase)

		// Assign the UUID
		sprintf_s(STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentIndividualLearning");
	UUID typeId;
	UuidFromString((RPC_WSTR)_T(AgentIndividualLearning_UUID), &typeId);
	STATE(AgentBase)->agentType.uuid = typeId;

	if (AgentIndividualLearning_TRANSFER_PENALTY < 1) STATE(AgentBase)->noCrash = false; // we're ok to crash

	// Initialize state members
	STATE(AgentIndividualLearning)->parametersSet = false;
	STATE(AgentIndividualLearning)->startDelayed = false;
	STATE(AgentIndividualLearning)->updateId = -1;
	STATE(AgentIndividualLearning)->actionConv = nilUUID;
	STATE(AgentIndividualLearning)->setupComplete = false;
	STATE(AgentIndividualLearning)->action.action = AvatarBase_Defs::AA_WAIT;
	STATE(AgentIndividualLearning)->action.val = 0.0;

	// initialize avatar and landmark info
	this->landmarkId = nilUUID;
	this->avatar.ready = false;	// Avatar's status will be set once the avatar info is recieved

	//---------------------------------------------------------------
	// TODO Data that must be loaded in

	// Learning parameters
	this->learning_frequency_ = 1;
	this->state_bits_ = 4;
	this->num_state_vrbls_ = 5;
	this->look_ahead_dist_ = 3;
	this->stateVector.resize(this->num_state_vrbls_, 0);
	this->prevStateVector.resize(this->num_state_vrbls_, 0);

	//Load in learning data from previous runs (if such data exist)
	this->parseLearningData();

	//Request advice exchange agent




	// Policy parameters
	this->policy_ = "softmax";
	this->softmax_temp_min_ = 0.02;
	this->softmax_temp_max_ = 0.95;
	this->softmax_temp_transition_ = 2;
	this->softmax_temp_slope_ = 1.5;
	this->e_greedy_epsilon_ = 0.2;

	// Check for proper policy input, should be wherever the config file is read
	if (this->policy_ != "greedy" && this->policy_ != "e-greedy" && this->policy_ != "softmax") {
		// TODO log error
	}

	// Reward parameters
	this->item_closer_reward_ = 0.5f;
	this->item_further_reward_ = -0.3f;
	this->robot_closer_reward_ = 0.5f;
	this->robot_further_reward_ = -0.3f;
	this->return_reward_ = 10.0f;
	this->empty_reward_value_ = -0.01f;
	this->reward_activation_dist_ = 0.17f;

	// Action parameters
	this->backupFractionalSpeed = -0.3f;
	this->num_actions_ = 5;
	//---------------------------------------------------------------

	// Initialize counters
	this->learning_iterations_ = 0;
	this->random_actions_ = 0;
	this->learned_actions_ = 0;

	this->totalActions = 0;
	this->usefulActions = 0;

	// Seed the random number generator
	srand(static_cast <unsigned> (time(0)));

	//Set cargo flags
	this->hasCargo = false;
	this->hasDelivered = false;

	// Prepare callbacks
	this->callback[AgentIndividualLearning_CBR_convAction] = NEW_MEMBER_CB(AgentIndividualLearning, convAction);
	this->callback[AgentIndividualLearning_CBR_convRequestAvatarLoc] = NEW_MEMBER_CB(AgentIndividualLearning, convRequestAvatarLoc);
	this->callback[AgentIndividualLearning_CBR_convGetAvatarList] = NEW_MEMBER_CB(AgentIndividualLearning, convGetAvatarList);
	this->callback[AgentIndividualLearning_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convGetAvatarInfo);
	this->callback[AgentIndividualLearning_CBR_convLandmarkInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convLandmarkInfo);
	this->callback[AgentIndividualLearning_CBR_convOwnLandmarkInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convOwnLandmarkInfo);
	this->callback[AgentIndividualLearning_CBR_convMissionRegion] = NEW_MEMBER_CB(AgentIndividualLearning, convMissionRegion);
	this->callback[AgentIndividualLearning_CBR_convGetTaskList] = NEW_MEMBER_CB(AgentIndividualLearning, convGetTaskList);
	this->callback[AgentIndividualLearning_CBR_convGetTaskInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convGetTaskInfo);
	this->callback[AgentIndividualLearning_CBR_convCollectLandmark] = NEW_MEMBER_CB(AgentIndividualLearning, convCollectLandmark);
	

}// end constructor

//-----------------------------------------------------------------------------
// Destructor

AgentIndividualLearning::~AgentIndividualLearning() {

	if (STATE(AgentBase)->started) {
		this->stop();
	}// end started if

}// end destructor

//-----------------------------------------------------------------------------
// Configure

int AgentIndividualLearning::configure() {
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
		sprintf_s(logName, "%s\\AgentIndividualLearning %s %ls.txt", logDirectory, timeBuf, rpc_wstr);

		Log.setLogMode(LOG_MODE_COUT);
		Log.setLogMode(LOG_MODE_FILE, logName);
		Log.setLogLevel(LOG_LEVEL_VERBOSE);
		Log.log(0, "AgentIndividualLearning %.2d.%.2d.%.5d.%.2d", AgentIndividualLearning_MAJOR, AgentIndividualLearning_MINOR, AgentIndividualLearning_BUILDNO, AgentIndividualLearning_EXTEND);
	}// end if

	if (AgentBase::configure()) {
		return 1;
	}// end if

	return 0;
}// end configure

int AgentIndividualLearning::configureParameters(DataStream *ds) {
	UUID uuid;

	// Read in owner ID
	ds->unpackUUID(&STATE(AgentIndividualLearning)->ownerId);

	ds->unpackUUID(&STATE(AgentIndividualLearning)->regionId);

	// Read in collection region(s)
	while (ds->unpackBool()) {
		ds->unpackUUID(&uuid);
		this->collectionRegions[uuid] = *(DDBRegion *)ds->unpackData(sizeof(DDBRegion));
	}

	// Load avatar action parameters
	STATE(AgentIndividualLearning)->maxLinear = ds->unpackFloat32();
	STATE(AgentIndividualLearning)->maxRotation = ds->unpackFloat32();
	STATE(AgentIndividualLearning)->minLinear = ds->unpackFloat32();
	STATE(AgentIndividualLearning)->minRotation = ds->unpackFloat32();

	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::configureParameters: ownerId %s", Log.formatUUID(LOG_LEVEL_NORMAL, &STATE(AgentIndividualLearning)->ownerId));

	// get mission region
	UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convMissionRegion, DDB_REQUEST_TIMEOUT);
	if (thread == nilUUID) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID(&STATE(AgentIndividualLearning)->regionId);
	this->ds.packUUID(&thread);
	this->sendMessage(this->hostCon, MSG_DDB_RREGION, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	this->backup();

	// finishConfigureParameters will be called once the mission region info is received

	return 0;
}// end configureParameters

int	AgentIndividualLearning::finishConfigureParameters() {

	STATE(AgentIndividualLearning)->parametersSet = true;


	if (STATE(AgentIndividualLearning)->startDelayed) {
		STATE(AgentIndividualLearning)->startDelayed = false;
		this->start(STATE(AgentBase)->missionFile);
	}// end if

	this->backup();

	return 0;
}// end finishConfigureParameters

//-----------------------------------------------------------------------------
// Start

int AgentIndividualLearning::start(char *missionFile) {
	DataStream lds;

	// Delay start if parameters not set
	if (!STATE(AgentIndividualLearning)->parametersSet) { // delay start
		strcpy_s(STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), missionFile);
		STATE(AgentIndividualLearning)->startDelayed = true;
		return 0;
	}// end if

	if (AgentBase::start(missionFile)) {
		return 1;
	}// end if

	STATE(AgentBase)->started = false;

	// register as avatar watcher
	Log.log(0, "AgentIndividualLearning::start: registering as avatar watcher");
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_AVATAR);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();
	// NOTE: we request a list of avatars once DDBE_WATCH_TYPE notification is received

	// register as landmark watcher
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_LANDMARK);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();

	// register as task watcher
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packInt32(DDB_TASK);
	this->sendMessage(this->hostCon, MSG_DDB_WATCH_TYPE, lds.stream(), lds.length());
	lds.unlock();



	Log.log(0, "AgentIndividualLearning::start: Preparing for first action");
	this->updateStateData();

	STATE(AgentBase)->started = true;
	return 0;
}// end start


//-----------------------------------------------------------------------------
// Stop

int AgentIndividualLearning::stop() {

	if (!this->frozen) {
		// TODO
	}// end if

	return AgentBase::stop();
}// end stop


//-----------------------------------------------------------------------------
// Step

int AgentIndividualLearning::step() {
	//Log.log(0, "AgentIndividualLearning::step()");
	if (STATE(AgentBase)->stopFlag) {
		uploadLearningData();	//Stores individual learningdata in DDB for next simulation run
	}

	return AgentBase::step();
}// end step

/* updateStateData
*
* Initiates the conversation to request the avatar positions from the DDB
*/
int AgentIndividualLearning::updateStateData() {
	// Only update if avatar is ready
	if (!this->avatar.ready) {
		Log.log(0, "AgentIndividualLearning::getStateData: Avatar not ready, sending wait action");
		ActionPair action;
		action.action = AvatarBase_Defs::AA_WAIT;
		this->sendAction(action);
		return 0;
	}

	if (this->avatar.retired == 1)
		return 0; // properly retired, ignore
	UUID convoThread;
	_timeb tb; // just a dummy
	STATE(AgentIndividualLearning)->updateId++;
	this->avatar.locValid = false;

	//Request this avatar's location
	this->ds.reset();
	this->ds.packUUID((UUID *)&this->avatarId);
	this->ds.packInt32(STATE(AgentIndividualLearning)->updateId);
	convoThread = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length());
	this->ds.unlock();
	if (convoThread == nilUUID) {
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID(&this->avatar.pf);
	this->ds.packInt32(DDBPFINFO_CURRENT);
	this->ds.packData(&tb, sizeof(_timeb)); // dummy
	this->ds.packUUID(&convoThread);
	this->sendMessage(this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length());
	this->ds.unlock();
	//Request teammates locations
	std::map<UUID, AVATAR_INFO, UUIDless>::iterator avatarIter;
	for (avatarIter = this->otherAvatars.begin(); avatarIter != this->otherAvatars.end(); avatarIter++) {
		this->ds.reset();
		this->ds.packUUID((UUID *)&avatarIter->first);
		this->ds.packInt32(STATE(AgentIndividualLearning)->updateId);
		convoThread = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length());
		this->ds.unlock();
		if (convoThread == nilUUID) {
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID(&avatarIter->second.pf);
		this->ds.packInt32(DDBPFINFO_CURRENT);
		this->ds.packData(&tb, sizeof(_timeb)); // dummy
		this->ds.packUUID(&convoThread);
		this->sendMessage(this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length());
		this->ds.unlock();
	}

}

/* getAction
*
* Determines the action from the individual learning layer, by requesting the current
* state vector, getting the quality from Q-Learning, calling the policy to select an action,
* then requesting to send the action.
*
* Also responsible for calling the learn method.
*/
int AgentIndividualLearning::getAction() {
	// Get the current state vector
	this->getStateVector();
	// Learn from the previous action
	this->learn();
	// Get quality and experience from state vector
	this->q_learning_.getElements(this->stateVector);
	// Select action based quality
	int action = this->policy(q_learning_.q_vals_, q_learning_.exp_vals_);
	// Form action
	if (action == MOVE_FORWARD) {
		Log.log(0, "AgentIndividualLearning::getAction: Selected action MOVE_FORWARD");
		STATE(AgentIndividualLearning)->action.action = MOVE_FORWARD; //	AvatarBase_Defs::AA_MOVE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxLinear;
	}
	else if (action == MOVE_BACKWARD) {
		Log.log(0, "AgentIndividualLearning::getAction: Selected action MOVE_BACKWARD");
		STATE(AgentIndividualLearning)->action.action = MOVE_BACKWARD; //AvatarBase_Defs::AA_MOVE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxLinear*this->backupFractionalSpeed;
	}
	else if (action == ROTATE_LEFT) {
		Log.log(0, "AgentIndividualLearning::getAction: Selected action ROTATE_LEFT");
		STATE(AgentIndividualLearning)->action.action = ROTATE_LEFT; // AvatarBase_Defs::AA_ROTATE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxRotation;
	}
	else if (action == ROTATE_RIGHT) {
		Log.log(0, "AgentIndividualLearning::getAction: Selected action ROTATE_RIGHT");
		STATE(AgentIndividualLearning)->action.action = ROTATE_RIGHT;// AvatarBase_Defs::AA_ROTATE;
		STATE(AgentIndividualLearning)->action.val = -STATE(AgentIndividualLearning)->maxRotation;
	}
	else if (action == INTERACT) {
		Log.log(0, "AgentIndividualLearning::getAction: Selected action INTERACT");
		// TODO: Add interact capabilities
		//If we have no cargo, pick up - first check if we can carry it (strength, capacity)
		//If we have cargo, drop it


		DataStream lds;
		float dx, dy;

		UUID avAgent = this->avatarAgentId;
		UUID avId = this->avatarId;
		UUID thread;


		if (!this->hasCargo) {
			// see if we are within range of the landmark
			dx = STATE(AgentIndividualLearning)->prev_pos_x - this->landmark.x;
			dy = STATE(AgentIndividualLearning)->prev_pos_y - this->landmark.y;

			if (dx*dx + dy*dy < COLLECTION_THRESHOLD*COLLECTION_THRESHOLD) { // should be close enough
				Log.log(0, "AgentIndividualLearning:: collecting landmark at %f %f", this->landmark.x, this->landmark.y);
				thread = this->conversationInitiate(AgentIndividualLearning_CBR_convCollectLandmark, -1, &avAgent, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				lds.reset();
				lds.packUChar(this->landmark.code);
				lds.packFloat32(this->landmark.x);
				lds.packFloat32(this->landmark.y);
				lds.packUUID(this->getUUID());
				lds.packUUID(&thread);
				this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_COLLECT_LANDMARK), lds.stream(), lds.length(), &avAgent);
				lds.unlock();
			}
		}
		else {		//We have cargo, drop off
			this->backup(); // landmark delivered

			// drop off
			lds.reset();
			lds.packUChar(this->landmark.code);
			this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_DEPOSIT_LANDMARK), lds.stream(), lds.length(), &avAgent);
			lds.unlock();



			for (auto& cRIter : this->collectionRegions) {				//See if we are inside a collection region when dropping cargo

				float cR_x_high = cRIter.second.x + cRIter.second.w;
				float cR_x_low = cRIter.second.x;

				float cR_y_high = cRIter.second.y + cRIter.second.h;
				float cR_y_low = cRIter.second.y;



				if (cR_x_low <= STATE(AgentIndividualLearning)->prev_pos_x && STATE(AgentIndividualLearning)->prev_pos_x <= cR_x_high && cR_y_low <= STATE(AgentIndividualLearning)->prev_pos_y && STATE(AgentIndividualLearning)->prev_pos_y <= cR_y_high) {
					//We delivered the cargo!
					this->hasDelivered = true;
					this->task.completed = true;


					//Upload task completion info to DDB
					DataStream lds;

					UUID *myUUID = this->getUUID();
					Log.log(0, "AgentIndividualLearning::task %s completed, uploading to DDB...", Log.formatUUID(LOG_LEVEL_NORMAL, &this->taskId));
					lds.reset();
					lds.packUUID(&this->taskId);		//Task id
					lds.packUUID(&this->task.agentUUID);						//Agent id
					lds.packUUID(&this->task.avatar);	//Avatar id
					lds.packBool(&this->task.completed);
					this->sendMessage(this->hostCon, MSG_DDB_TASKSETINFO, lds.stream(), lds.length());
					lds.unlock();
					return 0;

				}
			}
		}
		STATE(AgentIndividualLearning)->action.action = INTERACT;//AvatarBase_Defs::AA_WAIT;
		STATE(AgentIndividualLearning)->action.val = 0.0;
	}
	else {
		Log.log(0, "AgentIndividualLearning::getAction: No matching action, %d", action);
	}

	// When the action is not valid retain the type, but zero the movement value
	// (so that it can still be used for learning)
	if (!this->validAction(STATE(AgentIndividualLearning)->action)) {
		STATE(AgentIndividualLearning)->action.val = 0.0;
	}
	// Send the action to AvatarBase
	this->sendAction(STATE(AgentIndividualLearning)->action);
	 
	return 0;
}// end getAction

/* learn
*
* Updates the quality and experience tables based on the reward
*/
int AgentIndividualLearning::learn() {
	// TODO: Adjust learning frequency based on experience
	if ((this->learning_iterations_ % this->learning_frequency_ == 0) && (STATE(AgentIndividualLearning)->action.action > 0)) {
		float reward = this->determineReward();
		//Log.log(0, "AgentIndividualLearning::learn():: Reward is: %f", reward);
		//Log.log(0, "AgentIndividualLearning::learn():: Action is: %d", STATE(AgentIndividualLearning)->action.action);

		this->q_learning_.learn(this->prevStateVector, this->stateVector, STATE(AgentIndividualLearning)->action.action, reward);
		this->learning_iterations_++;	//TODO: Check if incremented elsewhere - could not find anywhere else- should be incremented, right?
	}
	return 0;
}

/* getStateVector
*
* Converts the state data to an encoded state vector, with all
* state variables being represented by a set of n-bit integers,
* where n, the number of bits, is dictated in the configuration.
*/
int AgentIndividualLearning::getStateVector() {
	/* Example of representing two parameters, alpha and beta, using
	* 5 bits is shown below. Since there are 5 bits, the values
	* for alpha and beta will be divided into 4 equal ranges, with
	* the resulting code shown below being assigned.
	*
	*    Code     alpha     beta  |   Code     alpha     beta
	*      0        1        1    |     8        3        1
	*      1        1        2    |     9        3        2
	*      2        1        3    |     10       3        3
	*      3        1        4    |     11       3        4
	*      4        2        1    |     12       4        1
	*      5        2        2    |     13       4        2
	*      6        2        3    |     14       4        3
	*      7        2        4    |     15       4        4
	*/

	// Determine closest delivery location
	float dx, dy, DSq, bestDSq;
	bestDSq = 99999999.0f;
	std::map<UUID, DDBRegion, UUIDless>::iterator iR;
	std::map<UUID, DDBRegion, UUIDless>::iterator bestR;

	for (iR = this->collectionRegions.begin(); iR != this->collectionRegions.end(); iR++) {
		dx = this->avatar.x - (iR->second.x + iR->second.w*0.5f);
		dy = this->avatar.y - (iR->second.y + iR->second.h*0.5f);
		DSq = dx*dx + dy*dy;
		if (DSq < bestDSq) {
			bestDSq = DSq;
			bestR = iR;
		}
	}
	// Save the closest delivery location
	STATE(AgentIndividualLearning)->goal_x = bestR->second.x + 0.5*bestR->second.w;
	STATE(AgentIndividualLearning)->goal_y = bestR->second.y + 0.5*bestR->second.h;

	// Make position relative to zero, to make encoding easier
	float pos_x = this->avatar.x - STATE(AgentIndividualLearning)->missionRegion.x;
	float pos_y = this->avatar.y - STATE(AgentIndividualLearning)->missionRegion.y;
	float orient = this->avatar.r;

	// Calculate relative distances
	float rel_target_x = this->landmark.x - this->avatar.x;
	float rel_target_y = this->landmark.y - this->avatar.y;
	float rel_goal_x = STATE(AgentIndividualLearning)->goal_x - this->avatar.x;
	float rel_goal_y = STATE(AgentIndividualLearning)->goal_y - this->avatar.y;




	// Identify the closest obstacle, whether it is a physical obstacle 
	// or the world boundary

	unsigned int target_type = this->landmark.landmarkType;	// 0=None, 1=Light, 2=Heavy

	// --------------------------------------------




	float leftBorderDist =  STATE(AgentIndividualLearning)->missionRegion.x - STATE(AgentIndividualLearning)->prev_pos_x;
	float rightBorderDist = STATE(AgentIndividualLearning)->missionRegion.x + STATE(AgentIndividualLearning)->missionRegion.w - STATE(AgentIndividualLearning)->prev_pos_x; 
	float bottomBorderDist = STATE(AgentIndividualLearning)->missionRegion.y - STATE(AgentIndividualLearning)->prev_pos_x;
	float topBorderDist = STATE(AgentIndividualLearning)->missionRegion.y + STATE(AgentIndividualLearning)->missionRegion.h - STATE(AgentIndividualLearning)->prev_pos_x;

	float shortestDistToBorder = abs(leftBorderDist);
	float rel_border_x = leftBorderDist;
	float rel_border_y = 0;

	if (abs(rightBorderDist) < shortestDistToBorder) {
		shortestDistToBorder = rightBorderDist;
		float rel_border_x = rightBorderDist;
		float rel_border_y = 0;
	}
	if (abs(bottomBorderDist) < shortestDistToBorder) {
		shortestDistToBorder = bottomBorderDist;
		float rel_border_x = 0;
		float rel_border_y = bottomBorderDist;
	}
	if (abs(topBorderDist) < shortestDistToBorder) {
		shortestDistToBorder = topBorderDist;
		float rel_border_x = 0;
		float rel_border_y = topBorderDist;
	}



	float dist_rel_obst = 99999.0f;
	float dist_rel_obst_x = 99999.0f;
	float dist_rel_obst_y = 99999.0f;


	for (auto& obstIter : this->obstacleList) {				//Get distance to closest obstacle

		float rel_obst_x_high = obstIter.second.x + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
		float rel_obst_x_low = obstIter.second.x;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)

		float rel_obst_y_high = obstIter.second.y + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
		float rel_obst_y_low = obstIter.second.y;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)

		float test_dist_rel_obst_x;
		float test_dist_rel_obst_y;

		if (rel_obst_x_low <= STATE(AgentIndividualLearning)->prev_pos_x) {
			if (STATE(AgentIndividualLearning)->prev_pos_x <= rel_obst_x_high)
				test_dist_rel_obst_x = 0;							//We are in the obstacles x-span (between the edges) - set x-dist to zero
			else
				test_dist_rel_obst_x = rel_obst_x_high - STATE(AgentIndividualLearning)->prev_pos_x; //Our x coord is higher than the obstacle high x-coord
		}
		else
			test_dist_rel_obst_x = rel_obst_x_low - STATE(AgentIndividualLearning)->prev_pos_x; //Our x coord is lower than the obstacle low x-coord



		if (rel_obst_y_low <= STATE(AgentIndividualLearning)->prev_pos_y) {
			if (STATE(AgentIndividualLearning)->prev_pos_y <= rel_obst_y_high)
				test_dist_rel_obst_y = 0;							//We are in the obstacles x-span (between the edges) - set y-dist to zero
			else
				test_dist_rel_obst_y = rel_obst_y_high - STATE(AgentIndividualLearning)->prev_pos_y; //Our x coord is higher than the obstacle high x-coord
		}
		else
			test_dist_rel_obst_y = rel_obst_y_low - STATE(AgentIndividualLearning)->prev_pos_y; //Our x coord is lower than the obstacle low x-coord

		if (sqrt(test_dist_rel_obst_x*test_dist_rel_obst_x + test_dist_rel_obst_y*test_dist_rel_obst_y) < dist_rel_obst) { //Select the lowest distance
			dist_rel_obst = sqrt(test_dist_rel_obst_x*test_dist_rel_obst_x + test_dist_rel_obst_y*test_dist_rel_obst_y);
			dist_rel_obst_x = test_dist_rel_obst_x;
			dist_rel_obst_y = test_dist_rel_obst_y;
		}
	}

	float rel_obst_x;
	float rel_obst_y;

	if (dist_rel_obst < shortestDistToBorder) {		//Object is closest
		rel_obst_x = dist_rel_obst_x;
		rel_obst_y = dist_rel_obst_y;
	}
	else {
		rel_obst_x = rel_border_x;			//Border is closest
		rel_obst_y = rel_border_y;
	}





	// In case an values are outside the world bounds, they need to
	// be adjusted to be within the bounds by at least this much
	float delta = 0.0001;

	// Make sure orientation is within [0, 2pi) range
	orient = orient - (float)(2 * M_PI*floor(orient / (2 * M_PI)));

	float orient_range = (2 * (float)M_PI) / this->state_bits_;

	// Find size increments for robot position
	float x_range = STATE(AgentIndividualLearning)->missionRegion.w / (this->state_bits_ / 2);
	float y_range = STATE(AgentIndividualLearning)->missionRegion.h / (this->state_bits_ / 2);

	// Make sure position is within world limits (necessary because of noise)
	if (pos_x >= STATE(AgentIndividualLearning)->missionRegion.w) {
		pos_x = STATE(AgentIndividualLearning)->missionRegion.w - delta;
	}
	if (pos_y >= STATE(AgentIndividualLearning)->missionRegion.h) {
		pos_y = STATE(AgentIndividualLearning)->missionRegion.h - delta;
	}

	// Encode position and orientation
	//int pos_encoded = (int)pow(floor(orient/orient_range), 2) + (int)floor(pos_x/x_range)*2 + (int)floor(pos_y/y_range);
	unsigned int pos_encoded = (int)floor(orient / orient_range)*this->state_bits_ + (int)floor(pos_x / x_range)*(this->state_bits_ / 2) + (int)floor(pos_y / y_range);

	// Find angles from x and y coords of each state, and make relative to orientation
	// so that the qudarant is directly in front of the robot, and within [0, 2pi) range
	float rel_target_angle = (float)atan2(rel_target_y, rel_target_x) - orient + (float)M_PI / this->state_bits_;
	rel_target_angle = rel_target_angle - (float)(2 * M_PI*floor(rel_target_angle / (2 * M_PI)));

	float rel_goal_angle = (float)atan2(rel_goal_y, rel_goal_x) - orient + (float)M_PI / this->state_bits_;
	rel_goal_angle = rel_goal_angle - (float)(2 * M_PI*floor(rel_goal_angle / (2 * M_PI)));

	float rel_obst_angle = (float)atan2(rel_obst_y, rel_obst_x) - orient + (float)M_PI / this->state_bits_;
	rel_obst_angle = rel_obst_angle - (float)(2 * M_PI*floor(rel_obst_angle / (2 * M_PI)));

	// Find relative distances
	float rel_target_dist = (float)sqrt(pow(rel_target_x, 2) + pow(rel_target_y, 2));
	float rel_goal_dist = (float)sqrt(pow(rel_goal_x, 2) + pow(rel_goal_y, 2));
	float rel_obst_dist = (float)sqrt(pow(rel_obst_x, 2) + pow(rel_obst_y, 2));

	// Find ranges for targets, obstacles, and the goal
	float angle_range = 2 * (float)M_PI / this->state_bits_;
	float dist_range = look_ahead_dist_ / this->state_bits_;

	// Make sure distances are within range (necessary because of noise)
	if (rel_target_dist >= look_ahead_dist_) {
		rel_target_dist = look_ahead_dist_ - delta;
	}
	if (rel_goal_dist >= look_ahead_dist_) {
		rel_goal_dist = look_ahead_dist_ - delta;
	}
	if (rel_obst_dist >= look_ahead_dist_) {
		rel_obst_dist = look_ahead_dist_ - delta;
	}

	// Encode position and angle
	unsigned int rel_target_encoded = (int)floor(rel_target_angle / angle_range)*this->state_bits_ + (int)floor(rel_target_dist / dist_range);
	unsigned int rel_goal_encoded = (int)floor(rel_goal_angle / angle_range)*this->state_bits_ + (int)floor(rel_goal_dist / dist_range);
	unsigned int rel_obst_encoded = (int)floor(rel_obst_angle / angle_range)*this->state_bits_ + (int)floor(rel_obst_dist / dist_range);

	// Form output vector
	std::vector<unsigned int> state_vector{ pos_encoded, rel_target_encoded, rel_goal_encoded, rel_obst_encoded, target_type };

	// Check that state vector is valid
	for (std::vector<unsigned int>::iterator it = state_vector.begin(); it != state_vector.end(); ++it) {
		if (*it >= pow(this->state_bits_, 2)) {
			Log.log(0, "AgentIndividualLearning::getStateVector: Error, invalid state vector. Reducing to max allowable value");
			Log.log(0, "AgentIndividualLearning::getStateVector: Invalid state vector: %d %d %d %d %d \n", state_vector[0], state_vector[1],
				state_vector[2], state_vector[3], state_vector[4]);

			*it = (unsigned int)pow(this->state_bits_, 2) - 1;
		}
	}

	// Save the state vector
	this->prevStateVector = this->stateVector;
	this->stateVector = state_vector;

	return 0;
}

/* policy
*
* Contains the policy for action selection. Can be multiple of
* these, with the desired policy being listed in the configuration
*
* INPUTS:
* quality = Vector of quality values for the next actions
* experience = Vector of experience values for the next actions
*
* OUTPUTS:
* action = The ID number of the selected action
*/
int AgentIndividualLearning::policy(std::vector<float> &quality, std::vector<unsigned int> &experience) {
	int action = 1;     // Default to one

	// Maybe add error check for empty quality_vals?

	// Get the sum of all quality
	float quality_sum = 0.0f;
	for (int i = 0; i < quality.size(); ++i) {
		Log.log(0, "AgentIndividualLearning::quality[i]: %f", quality[i]);
		quality_sum += quality[i];
	}
	Log.log(0, "AgentIndividualLearning::quality_sum: %f", quality_sum);
	if (quality_sum == 0.0f) {
		random_actions_++;
		int action = (int)ceil(randomGenerator.Uniform01() * num_actions_);
		Log.log(0, "AgentIndividualLearning::policy: All zero quality, selecting a random action");
		return action;
	}
	else {
		learned_actions_++;
	}// end if

	 // Make all actions with zero quality equal to 0.005*sum(Total Quality),
	 // giving it 0.5% probability to help discover new actions
	for (int i = 0; i < quality.size(); ++i) {
		if (quality[i] == 0) {
			quality[i] = 0.005f*quality_sum;
		}
	}

	// Use the policy indicated in the configuration
	if (policy_ == "greedy") {
		float max_val = 0;
		for (int i = 0; i < quality.size(); ++i) {
			if (quality[i] > max_val) {
				max_val = quality[i];
				action = i + 1;
			}
		}
	}
	else if (policy_ == "e-greedy") {
		float rand_val = (float)rand() / RAND_MAX;

		if (rand_val < e_greedy_epsilon_) {
			action = (int)ceil(randomGenerator.Uniform01() * num_actions_);
		}
		else {
			float max_val = 0;
			for (int i = 0; i < quality.size(); ++i) {
				if (quality[i] > max_val) {
					max_val = quality[i];
					action = i + 1;
				}
			}
		}
	}
	else if (policy_ == "softmax") {
		// Softmax action selection [Girard, 2015] with variable temperature addition
		// Determine temp from experience (form of function is 1 minus a sigmoid function)

		// Find lowest experience
		int min_exp = experience[0];
		for (int i = 1; i < experience.size(); ++i) {
			if (experience[i] < min_exp) {
				min_exp = experience[i];
			}
		}

		// Calculate softmax temperature
		float temp = (softmax_temp_max_ - softmax_temp_min_)*(1 - 1 / (1 + (float)exp(softmax_temp_slope_*
			(softmax_temp_transition_ - min_exp)))) + softmax_temp_min_;

		// Find exponents and sums for softmax distribution
		std::vector<float> exponents(num_actions_);
		float exponents_sum = 0;
		std::vector<float> action_prob(num_actions_);

		for (int i = 0; i < num_actions_; ++i) {
			exponents[i] = (float)exp(quality[i] / temp);
			exponents_sum += exponents[i];
		}

		for (int i = 0; i < num_actions_; ++i) {
			action_prob[i] = exponents[i] / exponents_sum;
		}

		float rand_val = randomGenerator.Uniform01();
		float action_prob_sum = action_prob[0];

		// Loop through actions to select one
		for (int i = 0; i < num_actions_; ++i) {
			if (rand_val < action_prob_sum) {
				action = i + 1;
				break;
			}
			else if (i == (num_actions_ - 1)) {
				action = i + 1;
			}
			else {
				action_prob_sum += action_prob[i + 1];
			}
		}
	}
	else {
		Log.log(0, "AgentIndividualLearning::policy: No matching policy");
		return -1;
	}// end policy if

	return action;
}

/* determineReward
*
* Returns the reward from the specified action by comparing the current
* and previous states
*
* OUTPUT:
* reward = Value of the reward for the previous action
*/
float AgentIndividualLearning::determineReward() {

	this->totalActions++;

	//----------------------------------------------------
	// Create fake data, to be loaded later
	//bool has_target = false;		//Replaced by check for nilUUID
	//bool target_state = false;		//Replaced by this->hasDelivered
	//bool prev_target_state = false;	//Replaced by this->hasDelivered
	//bool carrying_item = false;	//Replaced by this->hasCargo
	//----------------------------------------------------

	// First handle the case where no target is assigned, since the target
	// distance cannot be calculated if there is no target

	// Check if the robot has moved closer to the goal
	float goal_dist = (float)sqrt((float)pow(this->avatar.x - STATE(AgentIndividualLearning)->goal_x, 2)
		+ (float)pow(this->avatar.y - STATE(AgentIndividualLearning)->goal_y, 2));
	float prev_goal_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_pos_x - STATE(AgentIndividualLearning)->goal_x, 2)
		+ (float)pow(STATE(AgentIndividualLearning)->prev_pos_y - STATE(AgentIndividualLearning)->goal_y, 2));
	float delta_goal_dist = goal_dist - prev_goal_dist;

	if (this->landmarkId == nilUUID) {
		if (delta_goal_dist < -reward_activation_dist_) {
			//Log.log(0, "AgentIndividualLearning::determineReward():: DEBUG 1");
			return 1;
		}
		else {
			return empty_reward_value_;
		}
	}

	// Now handle the cases where the robot has a target

	this->usefulActions++;

	//// When the target is returned
	//if (target_state && !prev_target_state) {
	//	// Item has been returned
	//	return return_reward_;
	//}

	// When the target is returned
	if (this->hasDelivered == true) {
		// Item has been returned
		this->hasDelivered = false;	//Set back to false in perparation for new task
		return return_reward_;

	}
	


	// Calculate change in distance from target item to goal
	float item_goal_dist = (float)sqrt((float)pow(this->landmark.x - STATE(AgentIndividualLearning)->goal_x, 2)
		+ (float)pow(this->landmark.y - STATE(AgentIndividualLearning)->goal_y, 2));
	float prev_item_goal_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_target_x - STATE(AgentIndividualLearning)->goal_x, 2)
		+ (float)pow(STATE(AgentIndividualLearning)->prev_target_y - STATE(AgentIndividualLearning)->goal_y, 2));
	float delta_item_goal_dist = item_goal_dist - prev_item_goal_dist;

	// Calculate change in distance from robot to target item
	float robot_item_dist = (float)sqrt((float)pow(this->landmark.x - this->avatar.x, 2) + (float)pow(this->landmark.y - this->avatar.y, 2));
	float prev_robot_item_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_target_x - STATE(AgentIndividualLearning)->prev_pos_x, 2)
		+ (float)pow(STATE(AgentIndividualLearning)->prev_target_y - STATE(AgentIndividualLearning)->prev_pos_y, 2));
	float delta_robot_item_dist = robot_item_dist - prev_robot_item_dist;

	// Rewards depend on if the robot is going to an item, or carrying one
	if (this->hasCargo && delta_item_goal_dist < -reward_activation_dist_) {
		// Item has moved closer
		return item_closer_reward_;
	}
	else if (this->hasCargo && delta_item_goal_dist > reward_activation_dist_) {
		// Item has moved further away
		return item_further_reward_;
	}
	else if (!this->hasCargo && delta_robot_item_dist < -reward_activation_dist_) {
		// Robot moved closer to item
		return robot_closer_reward_;
	}
	else if (!this->hasCargo && delta_robot_item_dist > reward_activation_dist_) {
		// Robot moved further away from item
		return robot_further_reward_;
	}
	else {
		// Penalize for not getting any reward
		return empty_reward_value_;
	}// end reward if
}

/* validAction
*
* Checks if the selected action is valid by checking for collisions between the
* new position and world boundaries, obstacles, or other avatars.
*/
bool AgentIndividualLearning::validAction(ActionPair &action) {

	Log.log(0, "AgentIndividualLearning::validAction: action.action == %d", action.action);

	// Only need to consider movement actions
	if (!(action.action == MOVE_FORWARD ||action.action == MOVE_BACKWARD )){
		return true;
	}

	// Determine new position
	float new_pos_x = this->avatar.x + cos(this->avatar.r) * action.val;
	float new_pos_y = this->avatar.y + sin(this->avatar.r) * action.val;

	// Check X world boundaries
	if ((new_pos_x - this->avatar.outerRadius) < STATE(AgentIndividualLearning)->missionRegion.x ||
		new_pos_x + this->avatar.outerRadius > (STATE(AgentIndividualLearning)->missionRegion.x + STATE(AgentIndividualLearning)->missionRegion.w)) {
		Log.log(0, "AgentIndividualLearning::validAction: Invalid action (world X boundary)");
		return false;
	}

	// Check Y world boundaries
	if ((new_pos_y - this->avatar.outerRadius) < STATE(AgentIndividualLearning)->missionRegion.y ||
		new_pos_y + this->avatar.outerRadius > (STATE(AgentIndividualLearning)->missionRegion.y + STATE(AgentIndividualLearning)->missionRegion.h)) {
		Log.log(0, "AgentIndividualLearning::validAction: Invalid action (world Y boundary)");
		return false;
	}

	// Check against other avatars
	std::map<UUID, AVATAR_INFO>::iterator avatarIter;
	for (avatarIter = this->otherAvatars.begin(); avatarIter != this->otherAvatars.end(); avatarIter++) {
		// Must ignore robots in the goal area
		bool inGoalArea = false;
		std::map<UUID, DDBRegion, UUIDless>::iterator goalIter;
		for (goalIter = this->collectionRegions.begin(); goalIter != this->collectionRegions.end(); goalIter++) {
			if (avatarIter->second.x >= goalIter->second.x && avatarIter->second.x <= (goalIter->second.x + goalIter->second.w) &&
				avatarIter->second.y >= goalIter->second.y && avatarIter->second.y <= (goalIter->second.y + goalIter->second.h)) {
				inGoalArea = true;
				break;
			}
		}

		if (inGoalArea) break;

		if (abs(new_pos_y - avatarIter->second.y) < (this->avatar.outerRadius + avatarIter->second.outerRadius) &&
			abs(new_pos_x - avatarIter->second.x) < (this->avatar.outerRadius + avatarIter->second.outerRadius)) {
			Log.log(0, "AgentIndividualLearning::validAction: Invalid action (avatar collision)");
			return false;
		}
	}


	// Check against obstacles
	for (auto& obstIter : this->obstacleList) {				//Get distance to closest obstacle

		float rel_obst_x_high = obstIter.second.x + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
		float rel_obst_x_low = obstIter.second.x;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)

		float rel_obst_y_high = obstIter.second.y + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
		float rel_obst_y_low = obstIter.second.y;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)



		// Check obstacle boundaries
		if ( ( (new_pos_x - this->avatar.outerRadius) < rel_obst_x_high && (new_pos_x + this->avatar.outerRadius) > rel_obst_x_low )  && ((new_pos_y - this->avatar.outerRadius) < rel_obst_y_high && (new_pos_y + this->avatar.outerRadius) > rel_obst_y_low) ){
			Log.log(0, "AgentIndividualLearning::validAction: Invalid action (obstacle boundary)");
			return false;
		}
	}



	// Check against landmarks?
	// Is this necessary???

	return true;
}

int AgentIndividualLearning::uploadLearningData()
{
	//TODO Add additional learning algorithms
	return uploadQLearningData();
}

int AgentIndividualLearning::uploadQLearningData()
{
	DataStream lds;

	lds.reset();
	lds.packUUID(&STATE(AgentIndividualLearning)->ownerId);	//Avatar id
	lds.packChar(STATE(AgentBase)->agentType.instance);		//Pack the agent type instance - remember to set as different for each avatar in the config! (different avatar types will have different learning data)
	lds.packInt64(this->totalActions);
	lds.packInt64(this->usefulActions);
	lds.packInt32(this->q_learning_.table_size_);	//Size of value tables to store

	for (auto qIter : this->q_learning_.q_table_) {
		lds.packFloat32(qIter);						//Pack all values in q-table
	}
	for (auto expIter : this->q_learning_.exp_table_) {
		lds.packInt32(expIter);						//Pack all values in exp-table
	}

	this->sendMessage(this->hostCon, MSG_DDB_QLEARNINGDATA, lds.stream(), lds.length());
	lds.unlock();



	return 0;
}

int AgentIndividualLearning::parseLearningData()
{
	if(!boost::filesystem::exists("learningData.tmp"));
		return 0;	//No previous data, first run
	



	return 0;
}

/* sendAction
*
* Sends a message to AvatarBase to add the selected action to the action queue
*/
int AgentIndividualLearning::sendAction(ActionPair action) {
	// Initiate conversation
	STATE(AgentIndividualLearning)->actionConv = this->conversationInitiate(AgentIndividualLearning_CBR_convAction, DDB_REQUEST_TIMEOUT);
	if (STATE(AgentIndividualLearning)->actionConv == nilUUID) {
		return 1;
	}
	// Send message with action
	DataStream lds;
	lds.reset();
	lds.packUUID(&STATE(AgentBase)->uuid);
	lds.packUUID(&STATE(AgentIndividualLearning)->actionConv);
	switch (action.action) {		//If INTERACT is selected, send WAIT to the simulation, but keep the value internally for reward
		case MOVE_FORWARD:
			lds.packInt32(AvatarBase_Defs::AA_MOVE);
		break;
		case MOVE_BACKWARD:
			lds.packInt32(AvatarBase_Defs::AA_MOVE);
			break;
		case ROTATE_LEFT:
			lds.packInt32(AvatarBase_Defs::AA_ROTATE);
			break;
		case ROTATE_RIGHT:
			lds.packInt32(AvatarBase_Defs::AA_ROTATE);
			break;
		case INTERACT:
			lds.packInt32(AvatarBase_Defs::AA_WAIT);
			break;
		case AvatarBase_Defs::AA_WAIT:
			lds.packInt32(AvatarBase_Defs::AA_WAIT);
			break;

		default:
			Log.log(0, "AgentIndividualLearning::sendAction: Unknown action, cannot send.");
			break;
	}
	lds.packFloat32(action.val);
	this->sendMessageEx(this->hostCon, MSGEX(AvatarBase_MSGS, MSG_ACTION_QUEUE), lds.stream(), lds.length(), &STATE(AgentIndividualLearning)->ownerId);
	lds.unlock();

	return 0;

}// end sendAction

int AgentIndividualLearning::ddbNotification(char *data, int len) {
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
		if (type == DDB_AVATAR) {
			// request list of avatars
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetAvatarList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(this->getUUID()); // dummy id 
			sds.packInt32(DDBAVATARINFO_ENUM);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}
		if (type == DDB_TASK) {
			// request list of tasks
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetTaskList, DDB_REQUEST_TIMEOUT);
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
	}
	if (type == DDB_AVATAR) {
		if (evt == DDBE_ADD) {
			// add avatar
			this->avatar.ready = false;
			this->avatar.start.time = 0;
			this->avatar.retired = 0;

			// request avatar info
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(&uuid);
			sds.packInt32(DDBAVATARINFO_RAGENT| DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}
		else if (evt == DDBE_UPDATE) {
			int infoFlags = lds.unpackInt32();
			if (infoFlags & DDBAVATARINFO_RETIRE) {
				// request avatar info
				UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
				if (thread == nilUUID) {
					return 1;
				}
				sds.reset();
				sds.packUUID(&uuid);
				sds.packInt32(DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD);
				sds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length());
				sds.unlock();
			}
		}
		else if (evt == DDBE_REM) {
			// TODO
		}
	}
	else if (type == DDB_LANDMARK) {

		DataStream derpStream;
		UUID derpUUID;

		derpStream.setData(data, len);

		derpStream.unpackData(sizeof(UUID)); // key
		derpStream.unpackInt32();
		derpStream.unpackUUID(&derpUUID);

	//	Log.log(0, "AgentIndividualLearning::ddbNotification:Received update about landmark with UUID %s", Log.formatUUID(0, &derpUUID));



		//// **********************************************************************
		//// Temporarily assignng the landmark ID to the first landmark found
		//if (this->landmarkId == nilUUID) {
		//	this->landmarkId = uuid;
		//}
		//// **********************************************************************

		// Only update if it is for our assigned landmark
		if (uuid == this->landmarkId) {
			//	if (evt == DDBE_UPDATE || evt == DDBE_ADD ) {
			int infoFlags = lds.unpackInt32();
			//	if (infoFlags & DDBLANDMARKINFO_POS) {
					// get landmark
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convOwnLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
			sds.reset();
			sds.packUUID(&uuid);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
			sds.unlock();
			//	}
		//	}
		}
		else {
			//	if (evt == DDBE_UPDATE || evt == DDBE_ADD ) {
			int infoFlags = lds.unpackInt32();
			//	if (infoFlags & DDBLANDMARKINFO_POS) {
			// get landmark
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
			sds.reset();
			sds.packUUID(&uuid);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
			sds.unlock();
			//	}
			//	}



		}
	}
	else if (type == DDB_TASK) {
		if (evt == DDBE_ADD || evt == DDBE_UPDATE) {

			// request task info
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetTaskInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(&uuid);			// Task id
			sds.packUUID(&thread);
			sds.packBool(false);			   //true == send list of tasks, otherwise only info about a specific task
			this->sendMessage(this->hostCon, MSG_DDB_TASKGETINFO, sds.stream(), sds.length());
			sds.unlock();
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

int AgentIndividualLearning::conProcessMessage(spConnection con, unsigned char message, char *data, unsigned int len) {
	DataStream lds;

	if (!AgentBase::conProcessMessage(con, message, data, len)) // message handled
		return 0;

	switch (message) {
	case AgentIndividualLearning_MSGS::MSG_CONFIGURE:
	{
		lds.setData(data, len);
		this->configureParameters(&lds);
		lds.unlock();
		break;
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks

bool AgentIndividualLearning::convAction(void *vpConv) {
	spConversation conv = (spConversation)vpConv;
	UUID thread;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convAction: request timed out");
		return 0; // end conversation
	}

	this->ds.setData(conv->response, conv->responseLen);
	this->ds.unpackUUID(&thread); // thread
	char result = this->ds.unpackChar();
	_timeb tb;
	tb = *(_timeb *)this->ds.unpackData(sizeof(_timeb));
	int reason = this->ds.unpackInt32();
	this->ds.unlock();

	if (result == AAR_SUCCESS) {
		Log.log(0, "AgentIndividualLearning::convAction: Action successful, getting new action");

		// Begin getting new action
		this->updateStateData();
	}
	else {
		if (result == AAR_CANCELLED) {
			Log.log(0, "AgentIndividualLearning::convAction: Action canceled, reason %d. Resending previous action", reason);
		}
		else if (result == AAR_ABORTED) {
			Log.log(0, "AgentIndividualLearning::convAction: Action aborted, reason %d. Resending previous action", reason);
		}

		// Resend the action
		this->sendAction(STATE(AgentIndividualLearning)->action);
	}

	STATE(AgentIndividualLearning)->actionCompleteTime = tb;

	return 0;

}// end convAction

bool AgentIndividualLearning::convGetAvatarList(void *vpConv) {
	DataStream lds, sds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentIndividualLearning::convGetAvatarList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		int i, count;

		if (lds.unpackInt32() != DDBAVATARINFO_ENUM) {
			lds.unlock();
			return 0; // what happened here?
		}

		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetAvatarList: recieved %d avatars", count);

		UUID thread;
		UUID avatarId;
		UUID agentId;
		AgentType agentType;

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&avatarId);
			lds.unpackString(); // avatar type
			lds.unpackUUID(&agentId);
			lds.unpackUUID(&agentType.uuid);
			agentType.instance = lds.unpackInt32();

			if (agentId == STATE(AgentIndividualLearning)->ownerId) {
				// This is our avatar
				this->avatar.ready = false;
				this->avatar.start.time = 0;
				this->avatar.retired = 0;
				this->avatarId = avatarId;
			}
			else {
				this->otherAvatars[avatarId].ready = false;
				this->otherAvatars[avatarId].start.time = 0;
				this->otherAvatars[avatarId].retired = 0;
				//this->otherAvatarsLocUpdateId[avatarId] = -1;
			}

			// request avatar info
			thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetAvatarInfo, DDB_REQUEST_TIMEOUT, &avatarId, sizeof(UUID));
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID((UUID *)&avatarId);
			sds.packInt32(DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD);
			sds.packUUID(&thread);
			this->sendMessage(this->hostCon, MSG_DDB_AVATARGETINFO, sds.stream(), sds.length());
			sds.unlock();
		}
		lds.unlock();

	}
	else {
		lds.unlock();
		// TODO try again?
	}

	return 0;
}

bool AgentIndividualLearning::convGetAvatarInfo(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentIndividualLearning::convGetAvatarInfo: timed out");
		return 0; // end conversation
	}

	UUID avatarId = *(UUID *)conv->data;

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		UUID pfId, avatarAgent;
		char retired;
		float innerR, outerR;
		_timeb startTime, endTime;

		if (lds.unpackInt32() != (DDBAVATARINFO_RAGENT | DDBAVATARINFO_RPF | DDBAVATARINFO_RRADII | DDBAVATARINFO_RTIMECARD)) {
			lds.unlock();
			return 0; // what happened here?
		}

		lds.unpackUUID(&avatarAgent);
		lds.unpackUUID(&pfId);
		innerR = lds.unpackFloat32();
		outerR = lds.unpackFloat32();
		startTime = *(_timeb *)lds.unpackData(sizeof(_timeb));
		retired = lds.unpackChar();
		if (retired)
			endTime = *(_timeb *)lds.unpackData(sizeof(_timeb));
		lds.unlock();

		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetAvatarInfo: recieved avatar (%s) pf id: %s",
			Log.formatUUID(LOG_LEVEL_VERBOSE, &avatarId), Log.formatUUID(LOG_LEVEL_VERBOSE, &pfId));

		if (avatarId == this->avatarId) {
			// This is our avatar
			this->avatarAgentId = avatarAgent;
			this->avatar.pf = pfId;
			this->avatar.start = startTime;
			if (retired)
				this->avatar.end = endTime;
			this->avatar.retired = retired;
			this->avatar.innerRadius = innerR;
			this->avatar.outerRadius = outerR;
			this->avatar.ready = true;
		}
		else {
			// This is a teammate
			this->otherAvatars[avatarId].pf = pfId;
			this->otherAvatars[avatarId].start = startTime;
			if (retired)
				this->otherAvatars[avatarId].end = endTime;
			this->otherAvatars[avatarId].retired = retired;
			this->otherAvatars[avatarId].innerRadius = innerR;
			this->otherAvatars[avatarId].outerRadius = outerR;
			this->otherAvatars[avatarId].ready = true;
		}
	}
	else {
		lds.unlock();

		// TODO try again?
	}

	return 0;
}

bool AgentIndividualLearning::convRequestAvatarLoc(void *vpConv) {
	spConversation conv = (spConversation)vpConv;
	DataStream lds;
	int infoFlags;
	char response;
	UUID avatarId;
	int updateId;

	lds.setData((char *)conv->data, conv->dataLen);
	lds.unpackUUID(&avatarId);
	updateId = lds.unpackInt32();
	lds.unlock();

	// Update Id must match to continue
	if (updateId != STATE(AgentIndividualLearning)->updateId)
		return 0; // ignore

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convRequestAvatarLoc: request timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread
	response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded

		// handle info
		infoFlags = lds.unpackInt32();
		if (infoFlags != DDBPFINFO_CURRENT) {
			lds.unlock();
			return 0; // what happened?
		}

		lds.unpackData(sizeof(_timeb)); // discard time

		// unpack particle filter state
		float *state = (float *)lds.unpackData(sizeof(float) * 3);
		lds.unlock();

		if (avatarId == this->avatarId) {
			// This is our avatar
			// Save the previous position
			STATE(AgentIndividualLearning)->prev_pos_x = this->avatar.x;
			STATE(AgentIndividualLearning)->prev_pos_y = this->avatar.y;
			STATE(AgentIndividualLearning)->prev_orient = this->avatar.r;

			// Update the new position
			this->avatar.x = state[0];
			this->avatar.y = state[1];
			this->avatar.r = state[2];
			this->avatar.locValid = true;

			this->getAction();
		}
		else if (this->otherAvatars.count(avatarId)) {
			// This is a teammate
			// Update the new position
			this->otherAvatars[avatarId].x = state[0];
			this->otherAvatars[avatarId].y = state[1];
			this->otherAvatars[avatarId].r = state[2];
			this->otherAvatars[avatarId].locValid = true;
		}
	}
	else {
		lds.unlock();
		Log.log(0, "AgentIndividualLearning::convRequestAvatarLoc: request failed %d", response);
	}

	return 0;
}


bool AgentIndividualLearning::convLandmarkInfo(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char response;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convLandmarkInfo: request timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded

		DDBLandmark newLandmark = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark));


//		Log.log(0, "AgentIndividualLearning::convLandmarkInfo: landmark code %d, owner %s, x: %f, y: %f, type %d", newLandmark.code, Log.formatUUID(0, &newLandmark.owner), newLandmark.x, newLandmark.y, newLandmark.landmarkType);

		lds.unlock();

		//Add or update to landmark list


		if (newLandmark.landmarkType == NON_COLLECTABLE)			//Obstacle, cannot be collected
			this->obstacleList[newLandmark.code] = newLandmark;
		else
			this->targetList[newLandmark.code] = newLandmark;


		//this->backup();
	}
	else {
		lds.unlock();
	}

	return 0;
}

bool AgentIndividualLearning::convOwnLandmarkInfo(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char response;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convOwnLandmarkInfo: request timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded

		// Save the old position
		STATE(AgentIndividualLearning)->prev_target_x = this->landmark.x;
		STATE(AgentIndividualLearning)->prev_target_y = this->landmark.y;

		this->landmark = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark));
//		Log.log(0, "AgentIndividualLearning::convOwnLandmarkInfo: own landmark updated");
//		Log.log(0, "AgentIndividualLearning::convOwnLandmarkInfo: landmark code %d, owner %s, x: %f, y: %f, type %d", landmark.code, Log.formatUUID(0, &landmark.owner), landmark.x, landmark.y, landmark.landmarkType);

		lds.unlock();

		this->targetList[landmark.code] = landmark;

//		this->backup();
	}
	else {
		lds.unlock();
	}

	return 0;
}

bool AgentIndividualLearning::convMissionRegion(void *vpConv) {
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convMissionRegion: request timed out");
		return 0; // end conversation
	}

	this->ds.setData(conv->response, conv->responseLen);
	this->ds.unpackData(sizeof(UUID)); // discard thread

	if (this->ds.unpackChar() == DDBR_OK) { // succeeded
		this->ds.unlock();
		STATE(AgentIndividualLearning)->missionRegion.x = this->ds.unpackFloat32();
		STATE(AgentIndividualLearning)->missionRegion.y = this->ds.unpackFloat32();
		STATE(AgentIndividualLearning)->missionRegion.w = this->ds.unpackFloat32();
		STATE(AgentIndividualLearning)->missionRegion.h = this->ds.unpackFloat32();

		STATE(AgentIndividualLearning)->setupComplete = true;
		this->finishConfigureParameters();
	}
	else {
		this->ds.unlock();
		// TODO try again?
	}

	return 0;
}

bool AgentIndividualLearning::convGetTaskInfo(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentIndividualLearning::convGetTaskInfo: timed out");
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
		lds.unpackUUID(&taskId);

		DDBTask newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));

		if (newTask.avatar == STATE(AgentIndividualLearning)->ownerId) {	//The task is assigned to this avatar
			if (taskId != this->taskId) {	//Only need new info if the task has changed

				DataStream sds;
				if (this->hasCargo) { //Drop the cargo if we get a new task assigned			  
				lds.reset();
				lds.packUChar(this->landmark.code);
				this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_DEPOSIT_LANDMARK), lds.stream(), lds.length(), &this->avatarAgentId); // drop off
				lds.unlock();

				}

				//Log.log(0, "AgentIndividualLearning::convGetTaskInfo: Old task: %s, new task %s", Log.formatUUID(0,&this->taskId), Log.formatUUID(0, &taskId) );
				//Log.log(0, "AgentIndividualLearning::convGetTaskInfo: New task assigned, getting info...");
				//Log.log(0, "AgentIndividualLearning::convGetTaskInfo: task assigned DERP!");
				this->task = newTask;
				this->taskId = taskId;
				this->landmarkId = newTask.landmarkUUID;


				UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convOwnLandmarkInfo, DDB_REQUEST_TIMEOUT, &landmarkId, sizeof(UUID));
				sds.reset();
				sds.packUUID(&landmarkId);
				sds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
				sds.unlock();

			}

		}

		lds.unlock();
//		Log.log(0, "AgentIndividualLearning::convGetTaskInfo: 1 updated task with uuid %s, responsible avatar %s and responsible agent %s", Log.formatUUID(LOG_LEVEL_NORMAL, &taskId), Log.formatUUID(LOG_LEVEL_NORMAL, &newTask.avatar), Log.formatUUID(LOG_LEVEL_NORMAL, &newTask.agentUUID));

	}
	else {
		lds.unlock();
		// TODO try again?
	}
	return 0;
}

bool AgentIndividualLearning::convGetTaskList(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(0, "AgentIndividualLearning::convGetTaskList: timed out");
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
		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning: received %d tasks", count);

		UUID taskId;
		DDBTask newTask;

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&taskId);
			newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));
			Log.log(0, "AgentIndividualLearning::convGetTaskList: got task with uuid %s", Log.formatUUID(LOG_LEVEL_NORMAL, &taskId));
			Log.log(0, "AgentIndividualLearning::convGetTaskList: task contents: landmark UUID: %s,agent UUID: %s, avatar UUID: %s, completed: %s, ITEM_TYPES: %d", Log.formatUUID(LOG_LEVEL_NORMAL, &newTask.landmarkUUID), Log.formatUUID(LOG_LEVEL_NORMAL, &newTask.agentUUID), Log.formatUUID(LOG_LEVEL_NORMAL, &newTask.avatar), newTask.completed ? "true" : "false", newTask.type);
			
			if (newTask.avatar == this->avatarId){//   this->taskId) {
				//Log.log(0, "AgentIndividualLearning::convGetTaskList: task %s assigned. DERP!", Log.formatUUID(LOG_LEVEL_NORMAL, &taskId));

				this->taskId == taskId;
			//if (taskId == this->taskId) {
				this->task = newTask;
				this->landmarkId = newTask.landmarkUUID;
				DataStream sds;

				UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convOwnLandmarkInfo, DDB_REQUEST_TIMEOUT, &landmarkId, sizeof(UUID));
				sds.reset();
				sds.packUUID(&landmarkId);
				sds.packUUID(&thread);
				this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
				sds.unlock();

			}
		}
		lds.unlock();
	}
	else {
		lds.unlock();
		// TODO try again?
	}



	return 0;
}

bool AgentIndividualLearning::convCollectLandmark(void * vpConv)
{

	//TODO: Do we need to add any logic to handle -actually- collecting the landmark, or is that handled in the simulation?


	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char success;

	if (conv->response == NULL) {
		Log.log(0, "AgentIndividualLearning::convCollectLandmark: request timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	success = lds.unpackChar();
	lds.unlock();

	if (success) { // succeeded
		Log.log(0, "AgentIndividualLearning::convCollectLandmark: success");
		this->hasCargo = true;
		this->backup(); // landmarkCollected
	}
	else {
		Log.log(0, "SupervisorForage::convCollectLandmark: failed");
	}

}

//-----------------------------------------------------------------------------
// State functions

int AgentIndividualLearning::freeze(UUID *ticket) {
	return AgentBase::freeze(ticket);
}// end freeze

int AgentIndividualLearning::thaw(DataStream *ds, bool resumeReady) {
	return AgentBase::thaw(ds, resumeReady);
}// end thaw

int	AgentIndividualLearning::writeState(DataStream *ds, bool top) {
	if (top) _WRITE_STATE(AgentIndividualLearning);

	return AgentBase::writeState(ds, false);
}// end writeState

int	AgentIndividualLearning::readState(DataStream *ds, bool top) {
	if (top) _READ_STATE(AgentIndividualLearning);

	return AgentBase::readState(ds, false);
}// end readState

int AgentIndividualLearning::recoveryFinish() {
	if (AgentBase::recoveryFinish())
		return 1;

	return 0;
}// end recoveryFinish

int AgentIndividualLearning::writeBackup(DataStream *ds) {

	// Agent Data
	ds->packUUID(&STATE(AgentIndividualLearning)->ownerId);
	ds->packBool(STATE(AgentIndividualLearning)->parametersSet);
	ds->packBool(STATE(AgentIndividualLearning)->startDelayed);
	ds->packInt32(STATE(AgentIndividualLearning)->updateId);
	ds->packBool(STATE(AgentIndividualLearning)->setupComplete);

	return AgentBase::writeBackup(ds);
}// end writeBackup

int AgentIndividualLearning::readBackup(DataStream *ds) {
	DataStream lds;

	// configuration
	ds->unpackUUID(&STATE(AgentIndividualLearning)->ownerId);
	STATE(AgentIndividualLearning)->parametersSet = ds->unpackBool();
	STATE(AgentIndividualLearning)->startDelayed = ds->unpackBool();
	STATE(AgentIndividualLearning)->updateId = ds->unpackInt32();
	STATE(AgentIndividualLearning)->setupComplete = ds->unpackBool();

	if (STATE(AgentIndividualLearning)->setupComplete) {
		this->finishConfigureParameters();
	}
	else {
		// get mission region
		UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convMissionRegion, DDB_REQUEST_TIMEOUT);
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(&STATE(AgentIndividualLearning)->regionId);
		lds.packUUID(&thread);
		this->sendMessage(this->hostCon, MSG_DDB_RREGION, lds.stream(), lds.length());
		lds.unlock();
	}

	return AgentBase::readBackup(ds);
}// end readBackup


//*****************************************************************************
// Threading

DWORD WINAPI RunThread(LPVOID vpAgent) {
	AgentIndividualLearning *agent = (AgentIndividualLearning *)vpAgent;

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
	AgentIndividualLearning *agent = new AgentIndividualLearning(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

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
	AgentIndividualLearning *agent = new AgentIndividualLearning(ap, ticket, logLevel, logDirectory, playbackMode, playbackFile);

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
CAgentIndividualLearningDLL::CAgentIndividualLearningDLL() {}

// The one and only CAgentPathPlannerDLL object
CAgentIndividualLearningDLL theApp;

int CAgentIndividualLearningDLL::ExitInstance() {
	TRACE(_T("--- ExitInstance() for regular DLL: CAgentIndividualLearning ---\n"));
	return CWinApp::ExitInstance();
}