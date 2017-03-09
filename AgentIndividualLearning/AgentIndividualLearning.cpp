// AgentIndividualLearning.cpp : Defines the initialization routines for the DLL.

/* The IndividualLearningAgent is responsible for all the learning
*  functionality for each robot. One instance of IndividualLearning will
*  exist for each robot.
*
*  The interaction with AvatarBase will be through the formAction
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

#include "..\\AgentAdviceExchange\\AgentAdviceExchangeVersion.h"
#include <fstream>      // std::ifstream

//#include <boost/filesystem/operations.hpp>
//#include <boost/filesystem/path.hpp>

#define COLLECTION_THRESHOLD 0.3f // m
#define DELIVERY_THRESHOLD 0.3f // m

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
    STATE(AgentIndividualLearning)->missionRegionReceived = false;
	STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned = false;
    STATE(AgentIndividualLearning)->action.action = AvatarBase_Defs::AA_WAIT;
    STATE(AgentIndividualLearning)->action.val = 0.0;
	STATE(AgentIndividualLearning)->runNumber = 0;

    // initialize avatar and target info
    this->task.landmarkUUID = nilUUID;
	this->target.landmarkType = NON_COLLECTABLE;
	this->target.owner = nilUUID;
	this->taskId = nilUUID;
    this->avatar.ready = false;	// Avatar's status will be set once the avatar info is recieved

    //---------------------------------------------------------------
    // TODO Data that must be loaded in

    // Learning parameters
    this->learning_frequency_ = 1;
    this->num_state_vrbls_ = 8;
    this->state_resolution_ = {3, 3, 5, 3, 5, 3, 5, 1};
    this->look_ahead_dist_ = 2.0;
    this->stateVector.resize(this->num_state_vrbls_, 0);
    this->prevStateVector.resize(this->num_state_vrbls_, 0);

    //Load in learning data from previous runs (if such data exist)
   // this->parseLearningData();

	//Advice exchange parameters
	STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned = false;

    // Policy parameters
    this->softmax_temp_ = 0.10;

    // Reward parameters
    this->item_closer_reward_ = 0.5f;
    this->item_further_reward_ = -0.3f;
    this->robot_closer_reward_ = 0.3f;
    this->robot_further_reward_ = -0.1f;
    this->return_reward_ = 10.0f;
    this->empty_reward_value_ = -0.01f;

    // Action parameters
    this->backupFractionalSpeed = -0.5f;
    this->num_actions_ = 5;
    //---------------------------------------------------------------

    // Initialize counters
    this->learning_iterations_ = 0;
    this->random_actions_ = 0;
    this->learned_actions_ = 0;

    this->totalActions = 0;
    this->usefulActions = 0;

	this->q_avg = 0.0f;

    // Seed the random number generator
    srand(static_cast <unsigned> (time(0)));

    //Set cargo flags
    this->hasCargo = false;
    this->hasDelivered = false;

	STATE(AgentIndividualLearning)->collectRequestSent = false;
	STATE(AgentIndividualLearning)->depositRequestSent = false;
	
    // Prepare callbacks
    this->callback[AgentIndividualLearning_CBR_convAction] = NEW_MEMBER_CB(AgentIndividualLearning, convAction);
    this->callback[AgentIndividualLearning_CBR_convRequestAvatarLoc] = NEW_MEMBER_CB(AgentIndividualLearning, convRequestAvatarLoc);
    this->callback[AgentIndividualLearning_CBR_convGetAvatarList] = NEW_MEMBER_CB(AgentIndividualLearning, convGetAvatarList);
    this->callback[AgentIndividualLearning_CBR_convGetAvatarInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convGetAvatarInfo);
	this->callback[AgentIndividualLearning_CBR_convGetLandmarkList] = NEW_MEMBER_CB(AgentIndividualLearning, convGetLandmarkList);
    this->callback[AgentIndividualLearning_CBR_convLandmarkInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convLandmarkInfo);
    this->callback[AgentIndividualLearning_CBR_convGetTargetInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convGetTargetInfo);
    this->callback[AgentIndividualLearning_CBR_convMissionRegion] = NEW_MEMBER_CB(AgentIndividualLearning, convMissionRegion);
    this->callback[AgentIndividualLearning_CBR_convGetTaskList] = NEW_MEMBER_CB(AgentIndividualLearning, convGetTaskList);
    this->callback[AgentIndividualLearning_CBR_convGetTaskInfo] = NEW_MEMBER_CB(AgentIndividualLearning, convGetTaskInfo);
    this->callback[AgentIndividualLearning_CBR_convCollectLandmark] = NEW_MEMBER_CB(AgentIndividualLearning, convCollectLandmark);
	this->callback[AgentIndividualLearning_CBR_convDepositLandmark] = NEW_MEMBER_CB(AgentIndividualLearning, convDepositLandmark);
	this->callback[AgentIndividualLearning_CBR_convRequestAgentAdviceExchange] = NEW_MEMBER_CB(AgentIndividualLearning, convRequestAgentAdviceExchange);
	this->callback[AgentIndividualLearning_CBR_convRequestAdvice] = NEW_MEMBER_CB(AgentIndividualLearning, convRequestAdvice);
	this->callback[AgentIndividualLearning_CBR_convGetRunNumber] = NEW_MEMBER_CB(AgentIndividualLearning, convGetRunNumber);

	

	tempCounter = 0;

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

//#ifdef	NO_LOGGING
//		Log.log(0, "Setting log mode to off.");
//		Log.setLogLevel(LOG_LEVEL_NONE);
//		Log.setLogMode(LOG_MODE_OFF);
//#endif

        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning %.2d.%.2d.%.5d.%.2d", AgentIndividualLearning_MAJOR, AgentIndividualLearning_MINOR, AgentIndividualLearning_BUILDNO, AgentIndividualLearning_EXTEND);
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
	this->avatar.capacity = (ITEM_TYPES)ds->unpackInt32();
	STATE(AgentIndividualLearning)->avatarInstance = ds->unpackInt32();

	// Set the reward activation distance
	// (must move at least at a 45 degree angle relative to target or goal)
	this->reward_activation_dist_ = 0.707f*STATE(AgentIndividualLearning)->maxLinear;


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

	// request run number info
	thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetRunNumber, DDB_REQUEST_TIMEOUT);
	if (thread == nilUUID) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID(&thread);
	//lds.packUUID(&STATE(AgentBase)->uuid);
	this->sendMessage(this->hostCon, MSG_RRUNNUMBER, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	//Request advice exchange agent
	this->spawnAgentAdviceExchange();

    this->backup();
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::configureParameters: done.");
    // finishConfigureParameters will be called once the mission region info and run number are received

    return 0;
}// end configureParameters

int	AgentIndividualLearning::finishConfigureParameters() {

	//Only proceed to start if we have the run number and have not yet started (prevents double start)
	if (STATE(AgentIndividualLearning)->parametersSet == false && STATE(AgentIndividualLearning)->hasReceivedRunNumber == true) {

		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::finishConfigureParameters");

		//Read in data from previous run (if any)
		this->parseLearningData();

		STATE(AgentIndividualLearning)->parametersSet = true;


		if (STATE(AgentIndividualLearning)->startDelayed) {
			STATE(AgentIndividualLearning)->startDelayed = false;
			this->start(STATE(AgentBase)->missionFile);
		}// end if

		this->backup();
	}
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
    Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::start: registering as avatar watcher");
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



    Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::start: Preparing for first action");
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
	//this->tempCounter++;
 // //  if (STATE(AgentBase)->stopFlag) {
 // //      uploadLearningData();	//Stores individual learningdata in DDB for next simulation run
 // //  }
 //   if (tempCounter == 500) {
	//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::step: REACHED UPLOAD TEST COUNT!");
 //       uploadLearningData();	//Stores individual learningdata in DDB for next simulation run
 //   }
    return AgentBase::step();
}// end step

/* updateStateData
*
* Initiates the conversation to request the avatar positions from the DDB
*/
int AgentIndividualLearning::updateStateData() {
	DataStream lds;

    // Only update if avatar is ready
    if (!this->avatar.ready) {
//        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::getStateData: Avatar not ready, sending wait action");
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
    lds.reset();
    lds.packUUID((UUID *)&this->avatarId);
    lds.packInt32(STATE(AgentIndividualLearning)->updateId);
    convoThread = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, lds.stream(), lds.length());
    lds.unlock();
    if (convoThread == nilUUID) {
        return 0;
    }
    lds.reset();
    lds.packUUID(&this->avatar.pf);
    lds.packInt32(DDBPFINFO_CURRENT);
    lds.packData(&tb, sizeof(_timeb)); // dummy
    lds.packUUID(&convoThread);
    this->sendMessage(this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length());
    lds.unlock();
    //Request teammates locations
    std::map<UUID, AVATAR_INFO, UUIDless>::iterator avatarIter;
    for (avatarIter = this->otherAvatars.begin(); avatarIter != this->otherAvatars.end(); avatarIter++) {
        lds.reset();
        lds.packUUID((UUID *)&avatarIter->first);
        lds.packInt32(STATE(AgentIndividualLearning)->updateId);
        convoThread = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAvatarLoc, DDB_REQUEST_TIMEOUT, lds.stream(), lds.length());
        lds.unlock();
        if (convoThread == nilUUID) {
            return 0;
        }
        lds.reset();
        lds.packUUID(&avatarIter->second.pf);
        lds.packInt32(DDBPFINFO_CURRENT);
        lds.packData(&tb, sizeof(_timeb)); // dummy
        lds.packUUID(&convoThread);
        this->sendMessage(this->hostCon, MSG_DDB_RPFINFO, lds.stream(), lds.length());
        lds.unlock();
    }

}

/* preActionUpdate
*
* Performs the necessary work before an action can be selected and sent.
*
* The state vector is formed, the learn method is called (for updating the previous iteration),
* the quality values for the current state are retrieved, and advice from other agents is requested.
*/
int AgentIndividualLearning::preActionUpdate() {
    // Get the current state vector
    this->getStateVector();
    // Learn from the previous action
    this->learn();
    // Get quality from state vector
    std::vector<float> q_vals = this->q_learning_.getElements(this->stateVector);
	// Get advice
	this->requestAdvice(q_vals, this->stateVector);

    return 0;
}// end preActionUpdate


 /* formAction
 *
 * Calls the policy to select an action, forms the action (i.e. MOVE_FORWARD, MOVE_BACKWARD, etc.), then
 * makes the call to send the action.
 */
int AgentIndividualLearning::formAction() {
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: TOP");
	// Select action based quality
	int action = this->policy(this->q_vals);
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: TOP: action is %d", action);
	// Update average quality
	this->q_avg = (this->q_vals[action - 1] + this->totalActions*this->q_avg) / (this->totalActions + 1);

	// Form action
	if (action == MOVE_FORWARD) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Selected action MOVE_FORWARD");
		STATE(AgentIndividualLearning)->action.action = MOVE_FORWARD; //	AvatarBase_Defs::AA_MOVE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxLinear;
	}
	else if (action == MOVE_BACKWARD) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Selected action MOVE_BACKWARD");
		STATE(AgentIndividualLearning)->action.action = MOVE_BACKWARD; //AvatarBase_Defs::AA_MOVE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxLinear*this->backupFractionalSpeed;
	}
	else if (action == ROTATE_LEFT) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Selected action ROTATE_LEFT");
		STATE(AgentIndividualLearning)->action.action = ROTATE_LEFT; // AvatarBase_Defs::AA_ROTATE;
		STATE(AgentIndividualLearning)->action.val = STATE(AgentIndividualLearning)->maxRotation;
	}
	else if (action == ROTATE_RIGHT) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Selected action ROTATE_RIGHT");
		STATE(AgentIndividualLearning)->action.action = ROTATE_RIGHT;// AvatarBase_Defs::AA_ROTATE;
		STATE(AgentIndividualLearning)->action.val = -STATE(AgentIndividualLearning)->maxRotation;
	}
	else if (action == INTERACT) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Selected action INTERACT");
		// TODO: Add interact capabilities
		//If we have no cargo, pick up - first check if we can carry it (strength, capacity)
		//If we have cargo, drop it

		DataStream lds;
		float dx, dy;
		UUID avAgent = this->avatarAgentId;
		UUID avId = this->avatarId;
		UUID thread;


		if(!(STATE(AgentIndividualLearning)->collectRequestSent || STATE(AgentIndividualLearning)->depositRequestSent) ){		//Request is not yet answered
			if (!this->hasCargo ) {
	//			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: No cargo, trying to collect");
				if (target.landmarkType != NON_COLLECTABLE && this->avatar.capacity >= target.landmarkType && !this->task.completed) {	//Check that we can carry the item, and that the task is not completed (do not collect delivered targets)
					// see if we are within range of the landmark
					dx = STATE(AgentIndividualLearning)->prev_pos_x - this->target.x;
					dy = STATE(AgentIndividualLearning)->prev_pos_y - this->target.y;

					if (dx*dx + dy*dy < COLLECTION_THRESHOLD*COLLECTION_THRESHOLD) { // should be close enough
						Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: collecting landmark at %f %f", this->target.x, this->target.y);
						thread = this->conversationInitiate(AgentIndividualLearning_CBR_convCollectLandmark, -1, &avAgent, sizeof(UUID));
						if (thread == nilUUID) {
							return 1;
						}
						lds.reset();
						lds.packUChar(this->target.code);
						lds.packFloat32(this->target.x);
						lds.packFloat32(this->target.y);
						lds.packUUID(this->getUUID());
						lds.packUUID(&thread);
						this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_COLLECT_LANDMARK), lds.stream(), lds.length(), &avAgent);
						lds.unlock();

						STATE(AgentIndividualLearning)->collectRequestSent = true;
						this->backup();

					}

				}
				else {
	//				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: Cannot collect - booleans: %d %d %d", target.landmarkType != NON_COLLECTABLE, this->avatar.capacity >= target.landmarkType, !this->task.completed);
				}

			}
			else {

				for (auto& cRIter : this->collectionRegions) {				//See if we are inside a collection region when dropping cargo, otherwise no dropping cargo

	//for (cRIter = this->collectionRegions.begin(); cRIter != this->collectionRegions.end(); cRIter++) {				//See if we are inside a collection region when dropping cargo
					float cR_x_high = cRIter.second.x + cRIter.second.w;
					float cR_x_low = cRIter.second.x;
					float cR_y_high = cRIter.second.y + cRIter.second.h;
					float cR_y_low = cRIter.second.y;

					if (cR_x_low <= STATE(AgentIndividualLearning)->prev_pos_x && STATE(AgentIndividualLearning)->prev_pos_x <= cR_x_high && cR_y_low <= STATE(AgentIndividualLearning)->prev_pos_y && STATE(AgentIndividualLearning)->prev_pos_y <= cR_y_high) {


						Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: trying to drop off cargo...");
						thread = this->conversationInitiate(AgentIndividualLearning_CBR_convDepositLandmark, -1, &avAgent, sizeof(UUID));
						if (thread == nilUUID) {
							return 1;
						}
						lds.reset();
						lds.packUChar(this->target.code);
						lds.packFloat32(this->target.x);
						lds.packFloat32(this->target.y);
						lds.packUUID(this->getUUID());
						lds.packUUID(&thread);
						this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_DEPOSIT_LANDMARK), lds.stream(), lds.length(), &avAgent);
						lds.unlock();

						STATE(AgentIndividualLearning)->depositRequestSent = true;
						this->backup();

					}
				}


			}
		}
		STATE(AgentIndividualLearning)->action.action = INTERACT;//AvatarBase_Defs::AA_WAIT;
		STATE(AgentIndividualLearning)->action.val = 0.0;
	}
	else {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: No matching action, %d", action);
	}// end form action if

//	Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::formAction: Average action quality: %.3f", this->q_avg);

	 // When the action is not valid retain the type, but zero the movement value
	 // (so that it can still be used for learning)
	if (!this->validAction(STATE(AgentIndividualLearning)->action)) {
		STATE(AgentIndividualLearning)->action.val = 0.0;
	}
	// Send the action to AvatarBase
	this->sendAction(STATE(AgentIndividualLearning)->action);

	this->totalActions++;
	uploadQLearningData(true);	//Upload action counts to DDB
	return 0;
}// end formAction

/* learn
*
* Updates the quality and experience tables based on the reward
*/
int AgentIndividualLearning::learn() {
    // TODO: Adjust learning frequency based on experience
    if ((this->learning_iterations_ % this->learning_frequency_ == 0) && (STATE(AgentIndividualLearning)->action.action > 0)) {
        float reward = this->determineReward();
 //       Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::learn: Reward: %f", reward);

        this->q_learning_.learn(this->prevStateVector, this->stateVector, STATE(AgentIndividualLearning)->action.action, reward);
        this->learning_iterations_++;
    }
    return 0;
}

/* getStateVector
*
* Converts the state data to an encoded state vector, with all
* state variables being represented by an integer value between 0 and
* one minus the state resolution.
*
* State vector consists of:
*     target_type
*     target_dist
*     target_angle
*     goal_dist
*     goal_angle
*     obst_dist
*     obst_angle
*
*/
int AgentIndividualLearning::getStateVector() {

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
    STATE(AgentIndividualLearning)->goal_x = bestR->second.x + 0.5f*bestR->second.w;
    STATE(AgentIndividualLearning)->goal_y = bestR->second.y + 0.5f*bestR->second.h;

    // Identify the closest obstacle, whether it is a physical obstacle or the world boundary.
    // Check distance to all borders (left, right, top, down), then to all obstacles, and find
    // which is the closest.

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

    // Initialize max distances (look for anything smaller)
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

    // Make position relative to zero, to make encoding easier
    float pos_x = this->avatar.x - STATE(AgentIndividualLearning)->missionRegion.x;
    float pos_y = this->avatar.y - STATE(AgentIndividualLearning)->missionRegion.y;
    float orient = this->avatar.r;

    // Calculate relative distances
    float rel_target_x = this->target.x - this->avatar.x;
    float rel_target_y = this->target.y - this->avatar.y;
    float rel_goal_x = STATE(AgentIndividualLearning)->goal_x - this->avatar.x;
    float rel_goal_y = STATE(AgentIndividualLearning)->goal_y - this->avatar.y;

    // State vector will consist of:
    //   target_type
    //   target_dist
    //   target_angle
    //   goal_dist
    //   goal_angle
    //   obst_dist
    //   obst_angle

    // Get target type
    unsigned int target_type = this->target.landmarkType;	// 0=None, 1=Light, 2=Heavy

    // Small factor to adjust distances by to make sure they are within boundaries
    float delta = 0.0001f;

    // Find euclidean distances from target/goal/obstacle, make sure it is within
    // the look ahead distance, then convert to the proper state resolution
    float target_dist_raw = (float)sqrt(pow(rel_target_x, 2) + pow(rel_target_y, 2));
	target_dist_raw = min(target_dist_raw, this->look_ahead_dist_ - delta);
    unsigned int target_dist = (unsigned int)floor((target_dist_raw /this->look_ahead_dist_)*this->state_resolution_[1]);

    float goal_dist_raw = (float)sqrt(pow(rel_goal_x, 2) + pow(rel_goal_y, 2));
	goal_dist_raw = min(goal_dist_raw, this->look_ahead_dist_ - delta);
    unsigned int goal_dist = (unsigned int)floor((goal_dist_raw /this->look_ahead_dist_)*this->state_resolution_[3]);

    float obst_dist_raw = (float)sqrt(pow(rel_obst_x, 2) + pow(rel_obst_y, 2));
	obst_dist_raw = min(obst_dist_raw, this->look_ahead_dist_ - delta);
    unsigned int obst_dist = (unsigned int)floor((obst_dist_raw /this->look_ahead_dist_)*this->state_resolution_[3]);

    // Get relative angles of targets/goal/obstacles, offset by half of resolution
    // (so that straight forward is the centre of a quadrant), then convert to the proper
    // state resolution
    float target_angle_raw = (float)atan2(rel_target_y, rel_target_x) - orient + (float)M_PI / this->state_resolution_[2];
	target_angle_raw = target_angle_raw - (float)(2 * M_PI*floor(target_angle_raw / (2 * M_PI)));
    unsigned int target_angle = (unsigned int)floor((target_angle_raw /(2 * M_PI))*this->state_resolution_[2]);

    float goal_angle_raw = (float)atan2(rel_goal_y, rel_goal_x) - orient + (float)M_PI / this->state_resolution_[4];
	goal_angle_raw = goal_angle_raw - (float)(2 * M_PI*floor(goal_angle_raw / (2 * M_PI)));
    unsigned int goal_angle = (unsigned int)((goal_angle_raw /(2 * M_PI))*this->state_resolution_[4]);

    float obst_angle_raw = (float)atan2(rel_obst_y, rel_obst_x) - orient + (float)M_PI / this->state_resolution_[6];
	obst_angle_raw = obst_angle_raw - (float)(2 * M_PI*floor(obst_angle_raw / (2 * M_PI)));
    unsigned int obst_angle = (unsigned int)floor((obst_angle_raw /(2 * M_PI))*this->state_resolution_[6]);

    // Form output vector
    std::vector<unsigned int> state_vector{target_type, target_dist, target_angle, goal_dist, goal_angle, obst_dist, obst_angle, (unsigned int)this->hasCargo};
//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::getStateVector: target type %d, target dist %d, target angle %d, goal dist %d, goal angle %d, obst dist %d, obst angle %d", target_type, target_dist, target_angle, goal_dist, goal_angle, obst_dist, obst_angle);
    // Check that state vector is valid
    for(int i = 0; i < this->num_state_vrbls_; i++) {
        if (state_vector[i] > this->state_resolution_[i]) {
            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::getStateVector: Error, invalid state vector. Reducing to max allowable value");
            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::getStateVector: Element: %d, Current value: %d, Max Allowable value: %d\n", i, state_vector[i], this->state_resolution_[i]);

            state_vector[i] = this->state_resolution_[i];
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
*
* OUTPUTS:
* action = The ID number of the selected action
*/
int AgentIndividualLearning::policy(std::vector<float> &quality) {
    int action = 1;     // Default to one
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: POLICY");
    // Maybe add error check for empty quality_vals?

    // Get the sum of all quality
    float quality_sum = 0.0f;
    for (int i = 0; i < quality.size(); ++i) {
        quality_sum += quality[i];
    }
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: POLICY 1");
    if (quality_sum == 0.0f) {
        random_actions_++;
        int action = (int)ceil(randomGenerator.Uniform01() * num_actions_);
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::policy: All zero quality, selecting a random action");
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

    // Softmax action selection [Girard, 2015]
    // Determine temp from experience (form of function is 1 minus a sigmoid function)
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: POLICY 2");
    // Find exponents and sums for softmax distribution
    std::vector<float> exponents(num_actions_);
    float exponents_sum = 0;
    std::vector<float> action_prob(num_actions_);

    for (int i = 0; i < num_actions_; ++i) {
        exponents[i] = (float)exp(quality[i] / softmax_temp_);
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

	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::formAction: POLICY 3");
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

    // First handle the case where no target is assigned, since the target
    // distance cannot be calculated if there is no target

    // Check if the robot has moved closer to the goal
    float goal_dist = (float)sqrt((float)pow(this->avatar.x - STATE(AgentIndividualLearning)->goal_x, 2)
                                  + (float)pow(this->avatar.y - STATE(AgentIndividualLearning)->goal_y, 2));
    float prev_goal_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_pos_x - STATE(AgentIndividualLearning)->goal_x, 2)
                                       + (float)pow(STATE(AgentIndividualLearning)->prev_pos_y - STATE(AgentIndividualLearning)->goal_y, 2));
    float delta_goal_dist = goal_dist - prev_goal_dist;

	// Reward moving closer to goal area, to encourage waiting there until a task is received
    if (this->task.landmarkUUID == nilUUID) {
        if (delta_goal_dist < -reward_activation_dist_) {
            return 1.0f;
        }
        else {
            return empty_reward_value_;
        }
    }

    // Now handle the cases where the robot has a target
    this->usefulActions++;

    // When the target is returned
    if (this->hasDelivered == true) {
        // Item has been returned
        this->hasDelivered = false;	//Set back to false in preparation for new task
        return return_reward_;
    }

    // Calculate change in distance from target item to goal
    float item_goal_dist = (float)sqrt((float)pow(this->target.x - STATE(AgentIndividualLearning)->goal_x, 2)
                                       + (float)pow(this->target.y - STATE(AgentIndividualLearning)->goal_y, 2));
    float prev_item_goal_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_target_x - STATE(AgentIndividualLearning)->goal_x, 2)
                                            + (float)pow(STATE(AgentIndividualLearning)->prev_target_y - STATE(AgentIndividualLearning)->goal_y, 2));
    float delta_item_goal_dist = item_goal_dist - prev_item_goal_dist;

    // Calculate change in distance from robot to target item
    float robot_item_dist = (float)sqrt((float)pow(this->target.x - this->avatar.x, 2) + (float)pow(this->target.y - this->avatar.y, 2));
    float prev_robot_item_dist = (float)sqrt((float)pow(STATE(AgentIndividualLearning)->prev_target_x - STATE(AgentIndividualLearning)->prev_pos_x, 2)
                                             + (float)pow(STATE(AgentIndividualLearning)->prev_target_y - STATE(AgentIndividualLearning)->prev_pos_y, 2));
    float delta_robot_item_dist = robot_item_dist - prev_robot_item_dist;

    // Rewards depend on if the robot is going to an item, or carrying one
    if (this->hasCargo && delta_item_goal_dist < -this->reward_activation_dist_) {
        // Item has moved closer
        return this->item_closer_reward_;
    }
    else if (this->hasCargo && delta_item_goal_dist > this->reward_activation_dist_) {
        // Item has moved further away
        return this->item_further_reward_;
    }
    else if (!this->hasCargo && delta_robot_item_dist < -this->reward_activation_dist_) {
        // Robot moved closer to item
        return this->robot_closer_reward_;
    }
    else if (!this->hasCargo && delta_robot_item_dist > this->reward_activation_dist_) {
        // Robot moved further away from item
        return this->robot_further_reward_;
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
//        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::validAction: Invalid action (world X boundary)");
        return false;
    }

    // Check Y world boundaries
    if ((new_pos_y - this->avatar.outerRadius) < STATE(AgentIndividualLearning)->missionRegion.y ||
        new_pos_y + this->avatar.outerRadius > (STATE(AgentIndividualLearning)->missionRegion.y + STATE(AgentIndividualLearning)->missionRegion.h)) {
 //       Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::validAction: Invalid action (world Y boundary)");
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
//            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::validAction: Invalid action (avatar collision)");
            return false;
        }
    }


    // Check against obstacles
//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::validAction: Checking obstacles (%d obstacles found)", this->obstacleList.size());
    for (auto& obstIter : this->obstacleList) {				//Get distance to closest obstacle

        float rel_obst_x_high = obstIter.second.x + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
        float rel_obst_x_low = obstIter.second.x;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)

        float rel_obst_y_high = obstIter.second.y + 0.5f;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)
        float rel_obst_y_low = obstIter.second.y;	//Magic number for now, TODO: Define obstacle width and height in a header file (autonomic.h?)

		// Check obstacle boundaries
		float obst_x_high_diff = (new_pos_x - this->avatar.outerRadius) - rel_obst_x_high;
		float obst_x_low_diff = rel_obst_x_low - (new_pos_x + this->avatar.outerRadius);
		float obst_y_high_diff = (new_pos_y - this->avatar.outerRadius) - rel_obst_y_high;
		float obst_y_low_diff = rel_obst_y_low - (new_pos_y + this->avatar.outerRadius);
        
		if ((obst_x_high_diff < 0) && (obst_x_low_diff < 0) && (obst_y_high_diff < 0) && (obst_y_low_diff < 0)) {
 //           Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::validAction: Invalid action (obstacle boundary)");
            return false;
        }
    }

    return true;
}

/* getAdvice
*
* Initiates a conversation with AgentAdviceExchange, sending the current quality values
* and state vector, and waits for a response. The response will be the advised quality values,
* and once received the formAction method will be called. If the conversation times out, then 
* individual learning will proceed without advice.
*/
int AgentIndividualLearning::requestAdvice(std::vector<float> &q_vals, std::vector<unsigned int> &state_vector) {

//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::getAdvice: Sending request for advice");

	// Start the conversation
	STATE(AgentIndividualLearning)->adviceRequestConv = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAdvice, ADVICE_REQUEST_TIMEOUT);
	if (STATE(AgentIndividualLearning)->adviceRequestConv == nilUUID) {
		return 1;
	}

	// Send a message with the quality values and state vector
	DataStream lds;
	lds.reset();
	lds.packUUID(&STATE(AgentIndividualLearning)->adviceRequestConv);
	lds.packUUID(&STATE(AgentBase)->uuid);

	// Pack the average quality
	lds.packFloat32(this->q_avg);

	// Pack the Q values
	for (std::vector<float>::iterator q_iter = q_vals.begin(); q_iter != q_vals.end(); ++q_iter) {
		lds.packFloat32(*q_iter);
	}
	// Pack the state vector
	for (std::vector<unsigned int>::iterator state_iter = state_vector.begin(); state_iter != state_vector.end(); ++state_iter) {
		lds.packUInt32(*state_iter);
	}
	
	this->sendMessageEx(this->hostCon, MSGEX(AgentIndividualLearning_MSGS, MSG_REQUEST_ADVICE), lds.stream(), lds.length(), &STATE(AgentIndividualLearning)->agentAdviceExchange);
	lds.unlock();

	return 0;
}

/* spawnAgentAdviceExchange
*
* 
*/
int AgentIndividualLearning::spawnAgentAdviceExchange() {
	DataStream lds;
	UUID thread;
	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::spawnAgentAdviceExchange: requesting advice exchange agent...");
	if (!STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::spawnAgentAdviceExchange: agent not yet spawned, requesting...");
		UUID aAgentAdviceExchangeuuid;
		UuidFromString((RPC_WSTR)_T(AgentAdviceExchange_UUID), &aAgentAdviceExchangeuuid);
		thread = this->conversationInitiate(AgentIndividualLearning_CBR_convRequestAgentAdviceExchange, REQUESTAGENTSPAWN_TIMEOUT, &aAgentAdviceExchangeuuid, sizeof(UUID));
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(this->getUUID());
		lds.packUUID(&aAgentAdviceExchangeuuid);
		lds.packChar(-1); // no instance parameters
		lds.packFloat32(0); // affinity
		lds.packChar(DDBAGENT_PRIORITY_CRITICAL);
		lds.packUUID(&thread);
		this->sendMessage(this->hostCon, MSG_RAGENT_SPAWN, lds.stream(), lds.length());
		lds.unlock();

		STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned = false; // in progress
	}
	return 0;
}

int AgentIndividualLearning::uploadLearningData()
{
    //TODO Add additional learning algorithms
    return uploadQLearningData(false);
}

int AgentIndividualLearning::uploadQLearningData(bool onlyActions)
{
    DataStream lds;
	//Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::uploadQLearningData:Avatar id: %s, Instance: %d, Total actions: %d, Useful actions: %d, table size: %d", Log.formatUUID(0,&STATE(AgentIndividualLearning)->ownerId), STATE(AgentIndividualLearning)->avatarInstance, this->totalActions, usefulActions, this->q_learning_.table_size_);
    lds.reset();
    lds.packUUID(&STATE(AgentIndividualLearning)->ownerId);	//Avatar id
    lds.packChar(STATE(AgentIndividualLearning)->avatarInstance);		//Pack the agent type instance - remember to set as different for each avatar in the config! (different avatar types will have different learning data)
	lds.packBool(onlyActions);
	lds.packInt64(this->totalActions);
	lds.packInt64(this->usefulActions);
	if (!onlyActions) {			//Full upload at the end of a run
		lds.packInt32(this->q_learning_.table_size_);	//Size of value tables to store

		for (auto qIter : this->q_learning_.q_table_) {
			lds.packFloat32(qIter);						//Pack all values in q-table
	//		if (qIter > 0.0f)
	//			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::uploadQLearningData:Uploading qVal: %f", qIter);
		}
		for (auto expIter : this->q_learning_.exp_table_) {
			lds.packInt32(expIter);						//Pack all values in exp-table
	//		if (expIter > 0)
	//			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::uploadQLearningData:Uploading expVal: %d", expIter);
		}
	}
    this->sendMessage(this->hostCon, MSG_DDB_QLEARNINGDATA, lds.stream(), lds.length());
    lds.unlock();
    return 0;
}
int AgentIndividualLearning::parseLearningData()
{
	if (STATE(AgentIndividualLearning)->runNumber == 1)
		return 0;	//No data to parse on first run

	char learningDataFile[512];
	sprintf(learningDataFile, "learningData%d.tmp", STATE(AgentIndividualLearning)->runNumber - 1);


	FILE *fp;
	int i, id;
	char keyBuf[64];
	char ch;
	ITEM_TYPES landmark_type;
	float tauVal, cq, bq;

	if (fopen_s(&fp, learningDataFile, "r")) {
		Log.log(0, "AgentIndividualLearning::parseLearningData: failed to open %s", learningDataFile);
		return 1; // couldn't open file
	}
	Log.log(0, "AgentIndividualLearning::parseLearningData: parsing %s", learningDataFile);
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
				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::parseLearningData: Found [TLData] section.");
				if (fscanf_s(fp, "id=%d\n", &id) != 1) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted id");
					break;
				}
				while (fscanf_s(fp, "landmark_type=%d\n", &landmark_type) == 1) {
					fscanf_s(fp, "tau=%f\n", &tauVal);
				//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::parseLearningData: type: %d, tau: %f", landmark_type, tauVal);
				}
			}
			else if (!strncmp(keyBuf, "[AdviserData]", 64)) {
				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::parseLearningData: Found [AdviserData] section.");
				if (fscanf_s(fp, "id=%d\n", &id) != 1) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted id");
					break;
				}
				if (fscanf_s(fp, "cq=%f\n", &cq) != 1) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted cq");
					break;
				}
				if (fscanf_s(fp, "bq=%f\n", &bq) != 1) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted bq");
					break;
				}
			}
			else if (!strncmp(keyBuf, "[QLData]", 64)) {
				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::parseLearningData: Found [QLData] section.");
				int id;
				if (fscanf_s(fp, "id=%d\n", &id) != 1) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted id");
					break;
				}
				if (fscanf_s(fp, "qTable=\n") != 0) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted qTable");
					break;
				}
				float qVal;
				int count = 0;
				while (fscanf_s(fp, "%f\n", &qVal) == 1) {
					if (id == STATE(AgentIndividualLearning)->avatarInstance) {				//If the data belongs to this agent, store it
						this->q_learning_.q_table_[count] = qVal;
						count++;
					}
				}
				if (fscanf_s(fp, "expTable=\n") != 0) {
					Log.log(0, "AgentIndividualLearning::parseLearningData: badly formatted qTable");
					break;
				}
				int expVal;
				count = 0;
				while (fscanf_s(fp, "%d\n", &expVal) == 1) {
					if (id == STATE(AgentIndividualLearning)->avatarInstance) {				//If the data belongs to this agent, store it
						this->q_learning_.exp_table_[count] = expVal;
						count++;
					}
				}
				if (id == STATE(AgentIndividualLearning)->avatarInstance) { //The data belongs to this agent, no need to parse further...
					Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::parseLearningData: found this agent's id, parsing complete, stopping parsing process...");
					break;
				}
			}
			else { // unknown key
				fclose(fp);
				Log.log(0, "AgentIndividualLearning::parseLearningData: unknown key: %s", keyBuf);
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
            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::sendAction: Unknown action, cannot send.");
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
		if (type == DDB_LANDMARK) {
			// request list of landmarks
			UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetLandmarkList, DDB_REQUEST_TIMEOUT);
			if (thread == nilUUID) {
				return 1;
			}
			sds.reset();
			sds.packUUID(this->getUUID()); // dummy id, getting the full list of tasks anyway
			sds.packUUID(&thread);
			sds.packBool(true);			   //true == send list of tasks, otherwise only info about a specific task
			this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
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

        // Only update if it is for our assigned landmark
        if (uuid == this->task.landmarkUUID) {
            int infoFlags = lds.unpackInt32();
            // Get target position information
            UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetTargetInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
            sds.reset();
            sds.packUUID(&uuid);
            sds.packUUID(&thread);
			sds.packBool(false);
            this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
            sds.unlock();
        }
        else {
            int infoFlags = lds.unpackInt32();

            UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convLandmarkInfo, DDB_REQUEST_TIMEOUT, &uuid, sizeof(UUID));
            sds.reset();
            sds.packUUID(&uuid);
            sds.packUUID(&thread);
			sds.packBool(false);
            this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
            sds.unlock();
        }
    }
    else if (type == DDB_TASK) {
        if (evt == DDBE_ADD || evt == DDBE_UPDATE) {
			//Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::ddbNotification: Task update.");
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
		case AgentIndividualLearning_MSGS::MSG_REQUEST_Q_VALUES:
		{
		//	Log.log(0, " AgentIndividualLearning::conProcessMessage: MSG_REQUEST_Q_VALUES.");
			UUID conv;
			UUID sender;
			lds.setData(data, len);
			lds.unpackUUID(&conv);
			lds.unpackUUID(&sender);

			// Unpack state vector
			std::vector<unsigned int> state_vector;
			for (int j = 0; j < this->num_state_vrbls_; j++) {
				state_vector.push_back(lds.unpackUInt32());
			}
			lds.unlock();

			// Get quality values for this state vector
			std::vector<float> q_values = this->q_learning_.getElements(state_vector);

			// Send the quality values back
			DataStream lds_qvals;
			lds_qvals.reset();
			lds_qvals.packUUID(&conv);
			lds_qvals.packUUID(&STATE(AgentBase)->uuid);

			// Pack the Q values
			for (std::vector<float>::iterator q_iter = q_values.begin(); q_iter != q_values.end(); ++q_iter) {
				lds_qvals.packFloat32(*q_iter);
			}
			this->sendMessage(this->hostCon, MSG_RESPONSE, lds_qvals.stream(), lds_qvals.length(), &sender);
			lds_qvals.unlock();
			
		}
		break;
		case MSG_MISSION_DONE:
		{
			Log.log(0, " AgentIndividualLearning::conProcessMessage: mission done, uploading learning data for next run.");
			this->uploadLearningData();
		}
		break;
        default:
            return 1; // unhandled message
    }

    return 0;
}// end conProcessMessage

//-----------------------------------------------------------------------------
// Callbacks

/* convAction
*
* Callback for response from AvatarSimulation with the result of the action. When the
* action fails, it will be resent. When the action is a success, it proceeds to update
* for the next action. This callback is solely responsible for initiating the next action.
*/
bool AgentIndividualLearning::convAction(void *vpConv) {
    spConversation conv = (spConversation)vpConv;
    UUID thread;
	DataStream lds;

    if (conv->response == NULL) {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convAction: Request timed out");
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convAction: Resending previous action");
		// Resend the action
		this->sendAction(STATE(AgentIndividualLearning)->action);
        return 0; // end conversation
    }

	lds.setData(conv->response, conv->responseLen);
	lds.unpackUUID(&thread); // thread
    char result = lds.unpackChar();
    _timeb tb;
    tb = *(_timeb *)lds.unpackData(sizeof(_timeb));
    int reason = lds.unpackInt32();
	lds.unlock();

    if (result == AAR_SUCCESS) {
//        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convAction: Action successful, getting new action");

        // Begin getting new action
        this->updateStateData();
    }
    else {
        if (result == AAR_CANCELLED) {
            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convAction: Action canceled, reason %d. Resending previous action", reason);
        }
        else if (result == AAR_ABORTED) {
            Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convAction: Action aborted, reason %d. Resending previous action", reason);
        }

        // Resend the action
        this->sendAction(STATE(AgentIndividualLearning)->action);
    }

    STATE(AgentIndividualLearning)->actionCompleteTime = tb;

    return 0;

}// end convAction

/* convGetAvatarList
*
* Callback for being an avatar watcher. Called from ddbNotification when the event is 
* DDBE_WATCH_TYPE and the type DDB_AVATAR.
*
* Loops through all known avatars. When this agent belongs to the avatar their info is
* add to the class property avatar. All other avatars are added to otherAvatars. For both
* types, their position information is requested via convGetAvatarInfo.
*/
bool AgentIndividualLearning::convGetAvatarList(void *vpConv) {
    DataStream lds, sds;
    spConversation conv = (spConversation)vpConv;

    if (conv->response == NULL) { // timed out
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetAvatarList: Timed out");
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
        Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetAvatarList: Notified of %d avatars", count);

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
	recoveryCheck(&this->AgentIndividualLearning_recoveryLock1);
    return 0;
}

/* convGetAvatarInfo
*
* Callback for the request to the DDB for avatar info. When the avatar is the owner of this
* agent their info is added to avatar, otherwise it is added to otherAvatars.
*/
bool AgentIndividualLearning::convGetAvatarInfo(void *vpConv) {
    DataStream lds;
    spConversation conv = (spConversation)vpConv;

    if (conv->response == NULL) { // timed out
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetAvatarInfo: Timed out");
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

        Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetAvatarInfo: Recieved avatar (%s) pf id: %s",
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

/* convGetLandmarkList
*
* Callback for being a landmark watcher. Called from ddbNotification when the event is 
* DDBE_WATCH_TYPE and the type DDB_LANDMARK.
*
* Loops through all known landmarks, and adds the landmark to either obstacleList or 
* targetList, depending if it is collectable or not.
*/
bool AgentIndividualLearning::convGetLandmarkList(void * vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetLandmarkList: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	char response = lds.unpackChar();
	if (response == DDBR_OK) { // succeeded
		int i, count;

		if (lds.unpackBool() != true) {	//True if we requested a list of landmarks as opposed to just one
			lds.unlock();
			return 0; // what happened here?
		}

		count = lds.unpackInt32();
		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetLandmarkList: Notified of %d landmarks", count);

		UUID landmarkId;
		DDBLandmark landmark;

		for (i = 0; i < count; i++) {
			lds.unpackUUID(&landmarkId);
			landmark = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark)); // avatar type

			if (landmark.landmarkType == NON_COLLECTABLE) {
				// This is an obstacle
				this->obstacleList[landmark.code] = landmark;
			}
			else if (landmark.landmarkType == HEAVY_ITEM || landmark.landmarkType == LIGHT_ITEM || landmark.landmarkType == TYPE_PENDING){
				// This is a target
				this->targetList[landmark.code] = landmark;
			}
		}
		Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetLandmarkList: %d Non-Collectable, %d Collectable.", this->obstacleList.size(), this->targetList.size());
	}
	lds.unlock();

	return 0;
}

/* convRequestAvatarLoc
*
* Callback for updateing the avatars position. For this avatar it updates the class 
* property avatar, and the previous positions in STATE(AgentIndividualLearning). For
* all other avaters their info is saved in otherAvatars.
*/
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
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAvatarLoc: request timed out");
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

			//Update target location, actual landmark will be updated when cargo is dropped off

			if (this->hasCargo) {	
//				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAvatarLoc: UPDATING CARGO POS");
				this->target.x = this->avatar.x;
				this->target.y = this->avatar.y;
			}

            this->preActionUpdate();
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
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAvatarLoc: request failed %d", response);
    }

    return 0;
}

/* convLandmarkInfo
*
* Callback for landmark info update from the DDB. Simply adds the landmark to either
* obstacleList or targetList, depending on its type.
*/
bool AgentIndividualLearning::convLandmarkInfo(void *vpConv) {

    DataStream lds;
    spConversation conv = (spConversation)vpConv;
    char response;

    if (conv->response == NULL) {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convLandmarkInfo: request timed out");
        return 0; // end conversation
    }

    lds.setData(conv->response, conv->responseLen);
    lds.unpackData(sizeof(UUID)); // discard thread

    response = lds.unpackChar();
    if (response == DDBR_OK) { // succeeded
        DDBLandmark newLandmark = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark));
        lds.unlock();

		if (newLandmark.landmarkType == NON_COLLECTABLE) {			
			// Obstacle, cannot be collected
			this->obstacleList[newLandmark.code] = newLandmark;
//			Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convLandmarkInfo: Updating obstacle");
		} else if (newLandmark.landmarkType == HEAVY_ITEM || newLandmark.landmarkType == LIGHT_ITEM || newLandmark.landmarkType == TYPE_PENDING) {
			// Target
			this->targetList[newLandmark.code] = newLandmark;
//			Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convLandmarkInfo: Updating target");
		}
    }
    else {
        lds.unlock();
    }

    return 0;
}

/* convGetTargetInfo
*
* Callback for landmark info ONLY when the landmark is the assigned target. The new target data
* is set, and the previous target position is updated.
*/
bool AgentIndividualLearning::convGetTargetInfo(void *vpConv) {
    DataStream lds;
    spConversation conv = (spConversation)vpConv;
    char response;

    if (conv->response == NULL) {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTargetInfo: request timed out");
        return 0; // end conversation
    }

    lds.setData(conv->response, conv->responseLen);
    lds.unpackData(sizeof(UUID)); // discard thread

    response = lds.unpackChar();
    if (response == DDBR_OK) { // succeeded

		// Save the new target, and update the previous one
		if (this->target.owner == nilUUID) {
			// There was no previous target, so set it to the new one
			this->target = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark));
			STATE(AgentIndividualLearning)->prev_target_x = this->target.x;
			STATE(AgentIndividualLearning)->prev_target_y = this->target.y;
		}
		else {
			// There is a previous target to update
			STATE(AgentIndividualLearning)->prev_target_x = this->target.x;
			STATE(AgentIndividualLearning)->prev_target_y = this->target.y;
			this->target = *(DDBLandmark *)lds.unpackData(sizeof(DDBLandmark));
		}
		lds.unlock();
		
        this->targetList[target.code] = target;
    }
    else {
        lds.unlock();
    }

    return 0;
}

/* convMissionRegion
*
* Callback for getting mission region info at the start of a new run. Data is
* saved in STATE(AgentIndividualLearning). Responsible for calling finishConfigureParameters,
* but only when AgentAdviceExchange has already been spawned.
*/
bool AgentIndividualLearning::convMissionRegion(void *vpConv) {
	DataStream lds;
    spConversation conv = (spConversation)vpConv;

    if (conv->response == NULL) {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convMissionRegion: request timed out");
        return 0; // end conversation
    }

    lds.setData(conv->response, conv->responseLen);
    lds.unpackData(sizeof(UUID)); // discard thread

    if (lds.unpackChar() == DDBR_OK) { // succeeded
        lds.unlock();
        STATE(AgentIndividualLearning)->missionRegion.x = lds.unpackFloat32();
        STATE(AgentIndividualLearning)->missionRegion.y = lds.unpackFloat32();
        STATE(AgentIndividualLearning)->missionRegion.w = lds.unpackFloat32();
        STATE(AgentIndividualLearning)->missionRegion.h = lds.unpackFloat32();

        STATE(AgentIndividualLearning)->missionRegionReceived = true;

		if (STATE(AgentIndividualLearning)->missionRegionReceived && STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned)
			this->finishConfigureParameters();
    }
    else {
        lds.unlock();
        // TODO try again?
    }
	recoveryCheck(&this->AgentIndividualLearning_recoveryLock3);
    return 0;
}

/* convGetTaskInfo
*
* Callback for when a DDB_TASK event occurs in ddbNotification.
*
* Loops through all known landmarks checking for the one assigned to this avatar.
* Once found the AgentIndividualLearning properties task, and taskId are 
* filled. Then, request the position information for the target.
*/
bool AgentIndividualLearning::convGetTaskInfo(void * vpConv) {
    DataStream lds;
    spConversation conv = (spConversation)vpConv;
	//Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: Task info received.");
    if (conv->response == NULL) { // timed out
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: timed out");
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

        UUID taskIdIn;
        lds.unpackUUID(&taskIdIn);
        DDBTask newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));

//		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: Received info about task %s assigned to %s, my current task is: %s.", Log.formatUUID(0, &taskIdIn), Log.formatUUID(0, &newTask.agentUUID), Log.formatUUID(0, &this->taskId));

		// Make sure the task is assigned to this avatar
        if (newTask.avatar == STATE(AgentIndividualLearning)->ownerId) {	
			//Only need new info if the task has changed
            if (taskIdIn != this->taskId) {	

				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: Received new task assignment: %s.", Log.formatUUID(0, &taskIdIn));

				// Drop the cargo if we get a new task assigned
                if (this->hasCargo && !STATE(AgentIndividualLearning)->depositRequestSent) {
					Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: Dropping our current cargo");
					DataStream sds;
					UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convDepositLandmark, -1, &this->avatarAgentId, sizeof(UUID));
					if (thread == nilUUID) {
						return 1;
					}
					sds.reset();
					sds.packUChar(this->target.code);
					sds.packFloat32(this->target.x);
					sds.packFloat32(this->target.y);
					sds.packUUID(this->getUUID());
					sds.packUUID(&thread);
					this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_DEPOSIT_LANDMARK), sds.stream(), sds.length(), &this->avatarAgentId);
					sds.unlock();

					STATE(AgentIndividualLearning)->depositRequestSent = true;
					this->backup();

				}
				else {
					lds.unlock();
				}

				// Save the new task data
                this->task = newTask;
                this->taskId = taskIdIn;

				// Get position information for this target
                UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetTargetInfo, DDB_REQUEST_TIMEOUT, &this->task.landmarkUUID, sizeof(UUID));
				DataStream sds;
				sds.reset();
                sds.packUUID(&this->task.landmarkUUID);
                sds.packUUID(&thread);
				sds.packBool(false);
                this->sendMessage(this->hostCon, MSG_DDB_RLANDMARK, sds.stream(), sds.length());
                sds.unlock();
            }
        }
		else if (newTask.avatar != STATE(AgentIndividualLearning)->ownerId && newTask.avatar != nilUUID && !newTask.completed) {
			if (taskIdIn == this->taskId && taskIdIn != nilUUID) {
				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: My task %s has a different avatar than me: %s", Log.formatUUID(0, &taskIdIn), Log.formatUUID(0, &newTask.avatar));
				Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: I should acquiesce...");
				if (this->hasCargo && !STATE(AgentIndividualLearning)->depositRequestSent) {
					Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskInfo: Dropping our current cargo");
					DataStream sds;
					UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convDepositLandmark, -1, &this->avatarAgentId, sizeof(UUID));
					if (thread == nilUUID) {
						return 1;
					}
					sds.reset();
					sds.packUChar(this->target.code);
					sds.packFloat32(this->target.x);
					sds.packFloat32(this->target.y);
					sds.packUUID(this->getUUID());
					sds.packUUID(&thread);
					this->sendMessageEx(this->hostCon, MSGEX(AvatarSimulation_MSGS, MSG_DEPOSIT_LANDMARK), sds.stream(), sds.length(), &this->avatarAgentId);
					sds.unlock();

					STATE(AgentIndividualLearning)->depositRequestSent = true;
					this->backup();

				}


				this->target.code = 0;
				this->target.collected = false;
				this->target.elevation = 0.0;
				this->target.estimatedPos = this->target.estimatedPos;
				this->target.height = 0.0;
				this->target.landmarkType = NON_COLLECTABLE;
				this->target.owner = nilUUID;
				this->target.P = 0.0;
				this->target.posInitialized = this->target.posInitialized;
				this->target.trueX = 0.0;
				this->target.trueY = 0.0;
				this->target.x = 0.0;
				this->target.y = 0.0;

				this->taskId = nilUUID;
				this->task.agentUUID = nilUUID;
				this->task.avatar = nilUUID;
				this->task.completed = this->task.completed;
				this->task.landmarkUUID = nilUUID;
				this->task.type = NON_COLLECTABLE;



			}
			lds.unlock();
		}




    }
    else {
        lds.unlock();
        // TODO try again?
    }
    return 0;
}

/* convGetTaskList
*
* Callback for being a task watcher. Called from ddbNotification when the event is
* DDBE_WATCH_TYPE and the type DDB_TASK.
*
* Loops through all known tasks, and if the task is assigned to this avatar, then
* the position information for that target is requested.
*/
bool AgentIndividualLearning::convGetTaskList(void * vpConv)
{
    DataStream lds;
    spConversation conv = (spConversation)vpConv;

    if (conv->response == NULL) { // timed out
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskList: timed out");
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
        Log.log(LOG_LEVEL_VERBOSE, "AgentIndividualLearning::convGetTaskList: Notified of %d tasks", count);

        UUID taskIdIn;
        DDBTask newTask;

        for (i = 0; i < count; i++) {
            lds.unpackUUID(&taskIdIn);
            newTask = *(DDBTask *)lds.unpackData(sizeof(DDBTask));

            if (newTask.avatar == this->avatarId){
                Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convGetTaskList: task %s is assigned to this avatar.", Log.formatUUID(LOG_LEVEL_NORMAL, &taskIdIn));
				
				// Save the task data
                this->taskId == taskIdIn;
                this->task = newTask;

				// Get position information for this target
                UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetTargetInfo, DDB_REQUEST_TIMEOUT, &this->task.landmarkUUID, sizeof(UUID));
				DataStream sds;
				sds.reset();
                sds.packUUID(&this->task.landmarkUUID);
                sds.packUUID(&thread);
				sds.packBool(false);
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

/* convCollectLandmark
*
* Callback for attempting to collect a landmark. Called from fromAction when the action 
* is INTERACT. Responsible for setting the hasCargo flag (only when the collection is a success).
*/
bool AgentIndividualLearning::convCollectLandmark(void * vpConv) {

    DataStream lds;
    spConversation conv = (spConversation)vpConv;
    char success;

    if (conv->response == NULL) {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convCollectLandmark: request timed out");
        return 0; // end conversation
    }

    lds.setData(conv->response, conv->responseLen);
    lds.unpackData(sizeof(UUID)); // discard thread

    success = lds.unpackChar();
    lds.unlock();

    if (success == 1) { // succeeded
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convCollectLandmark: success");
        this->hasCargo = true;
		STATE(AgentIndividualLearning)->collectRequestSent = false;
        this->backup(); // landmarkCollected
    }
	else if (success == -1) {
		if (STATE(AgentIndividualLearning)->collectRequestSent) { // We have sent a collectRequest - the landmark was collected, and we missed the confirmation - proceed as if success
			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convCollectLandmark: already collected, but we have sent a collectRequest - proceeding as success");
			this->hasCargo = true;
			STATE(AgentIndividualLearning)->collectRequestSent = false;
			this->backup(); // landmarkCollected
		}
	}
    else {
        Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convCollectLandmark: failed");
    }
	STATE(AgentIndividualLearning)->collectRequestSent = false;
	return 0;
}

bool AgentIndividualLearning::convDepositLandmark(void * vpConv) {

	DataStream lds;
	spConversation conv = (spConversation)vpConv;
	char success;

	if (conv->response == NULL) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: request timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	success = lds.unpackChar();
	lds.unlock();

	if (success == 1) { // succeeded
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: success");

		//We have cargo, drop off	
		this->hasCargo = false;
		// drop off
		std::map<UUID, DDBRegion, UUIDless>::iterator cRIter;

		for ( auto& cRIter : this->collectionRegions) {				//See if we are inside a collection region when dropping cargo

		//for (cRIter = this->collectionRegions.begin(); cRIter != this->collectionRegions.end(); cRIter++) {				//See if we are inside a collection region when dropping cargo
			float cR_x_high = cRIter.second.x + cRIter.second.w;
			float cR_x_low = cRIter.second.x;
			float cR_y_high = cRIter.second.y + cRIter.second.h;
			float cR_y_low = cRIter.second.y;

				if (cR_x_low <= STATE(AgentIndividualLearning)->prev_pos_x && STATE(AgentIndividualLearning)->prev_pos_x <= cR_x_high && cR_y_low <= STATE(AgentIndividualLearning)->prev_pos_y && STATE(AgentIndividualLearning)->prev_pos_y <= cR_y_high) {
		//We delivered the cargo!
		this->hasDelivered = true;
		this->task.completed = true;

		//Upload task completion info to DDB
		DataStream sds;
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: task %s completed, uploading to DDB...", Log.formatUUID(LOG_LEVEL_NORMAL, &this->taskId));
		sds.reset();
		sds.packUUID(&this->taskId);		//Task id
											//lds.packUUID(&this->task.agentUUID);						//Agent id
											//lds.packUUID(&this->task.avatar);	//Avatar id
		sds.packUUID(&nilUUID);						//Agent id
		sds.packUUID(&nilUUID);						//Avatar id

		sds.packBool(&this->task.completed);
		this->sendMessage(this->hostCon, MSG_DDB_TASKSETINFO, sds.stream(), sds.length());

		this->target.code = 0;
		this->target.collected = false;
		this->target.elevation = 0.0;
		this->target.estimatedPos = this->target.estimatedPos;
		this->target.height = 0.0;
		this->target.landmarkType = NON_COLLECTABLE;
		this->target.owner = nilUUID;
		this->target.P = 0.0;
		this->target.posInitialized = this->target.posInitialized;
		this->target.trueX = 0.0;
		this->target.trueY = 0.0;
		this->target.x = 0.0;
		this->target.y = 0.0;

		this->taskId = nilUUID;
		this->task.agentUUID = nilUUID;
		this->task.avatar = nilUUID;
		this->task.completed = this->task.completed;
		this->task.landmarkUUID = nilUUID;
		this->task.type = NON_COLLECTABLE;



		sds.unlock();
		break;
				}

		}
		this->backup(); // landmarkDeposited
	}
	else if (success == -1) {	//Not yet collected / already dropped off, but missed the confirmation message
		if (STATE(AgentIndividualLearning)->depositRequestSent) { // We have sent a depositRequest - the landmark was dropped off, and we missed the confirmation - proceed as if success
			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: not yet collected, but we have sent a depositRequest - proceeding as success");

			//We have cargo, drop off	
			this->hasCargo = false;
			// drop off
			std::map<UUID, DDBRegion, UUIDless>::iterator cRIter;

				for ( auto& cRIter : this->collectionRegions) {				//See if we are inside a collection region when dropping cargo

			//for (cRIter = this->collectionRegions.begin(); cRIter != this->collectionRegions.end(); cRIter++) {				//See if we are inside a collection region when dropping cargo
				float cR_x_high = cRIter.second.x + cRIter.second.w;
				float cR_x_low = cRIter.second.x;
				float cR_y_high = cRIter.second.y + cRIter.second.h;
				float cR_y_low = cRIter.second.y;

					if (cR_x_low <= STATE(AgentIndividualLearning)->prev_pos_x && STATE(AgentIndividualLearning)->prev_pos_x <= cR_x_high && cR_y_low <= STATE(AgentIndividualLearning)->prev_pos_y && STATE(AgentIndividualLearning)->prev_pos_y <= cR_y_high) {
			//We delivered the cargo!
			this->hasDelivered = true;
			this->task.completed = true;

			//Upload task completion info to DDB
			DataStream sds;
			Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: task %s completed, uploading to DDB...", Log.formatUUID(LOG_LEVEL_NORMAL, &this->taskId));
			sds.reset();
			sds.packUUID(&this->taskId);		//Task id
												//lds.packUUID(&this->task.agentUUID);						//Agent id
												//lds.packUUID(&this->task.avatar);	//Avatar id
			sds.packUUID(&nilUUID);						//Agent id
			sds.packUUID(&nilUUID);						//Avatar id

			sds.packBool(&this->task.completed);
			this->sendMessage(this->hostCon, MSG_DDB_TASKSETINFO, sds.stream(), sds.length());

			this->target.code = 0;
			this->target.collected = false;
			this->target.elevation = 0.0;
			this->target.estimatedPos = this->target.estimatedPos;
			this->target.height = 0.0;
			this->target.landmarkType = NON_COLLECTABLE;
			this->target.owner = nilUUID;
			this->target.P = 0.0;
			this->target.posInitialized = this->target.posInitialized;
			this->target.trueX = 0.0;
			this->target.trueY = 0.0;
			this->target.x = 0.0;
			this->target.y = 0.0;

			this->taskId = nilUUID;
			this->task.agentUUID = nilUUID;
			this->task.avatar = nilUUID;
			this->task.completed = this->task.completed;
			this->task.landmarkUUID = nilUUID;
			this->task.type = NON_COLLECTABLE;



			sds.unlock();
			break;
					}

			}
			STATE(AgentIndividualLearning)->depositRequestSent = false;
			this->backup(); // landmarkDeposited



		}


	}
	else {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convDepositLandmark: failed");
	}

	STATE(AgentIndividualLearning)->depositRequestSent = false;
	return 0;
}



/* convRequestAgentAdviceExchange
*
* Callback for the request to spawn AgentAdviceExchange. Responsible for calling
* finishConfigureParameters, but only when the mission region info has already been
* received.
*/
bool AgentIndividualLearning::convRequestAgentAdviceExchange(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // spawn timed out
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAgentAdviceExchange: request spawn timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread

	if (lds.unpackBool()) { // succeeded
		lds.unpackUUID(&STATE(AgentIndividualLearning)->agentAdviceExchange);
		lds.unlock();
		STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned = 1; // ready

		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAgentAdviceExchange: Advice exchange agent %s", Log.formatUUID(0, &STATE(AgentIndividualLearning)->agentAdviceExchange));

		// register as agent watcher
		lds.reset();
		lds.packUUID(&STATE(AgentBase)->uuid);
		lds.packUUID(&STATE(AgentIndividualLearning)->agentAdviceExchange);
		this->sendMessage(this->hostCon, MSG_DDB_WATCH_ITEM, lds.stream(), lds.length());
		lds.unlock();
		// NOTE: we request the status once DDBE_WATCH_ITEM notification is received

		// set parent
		this->sendMessage(this->hostCon, MSG_AGENT_SETPARENT, (char *)this->getUUID(), sizeof(UUID), &STATE(AgentIndividualLearning)->agentAdviceExchange);

		lds.reset();
		lds.packUUID(&STATE(AgentBase)->uuid);
		lds.packInt32(this->avatar.capacity);
		lds.packUUID(&STATE(AgentIndividualLearning)->ownerId);
		lds.packInt32(STATE(AgentIndividualLearning)->avatarInstance);
		lds.packInt32(STATE(AgentIndividualLearning)->runNumber);
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAgentAdviceExchange: sending run number: %d", STATE(AgentIndividualLearning)->runNumber);

		this->sendMessageEx(this->hostCon, MSGEX(AgentAdviceExchange_MSGS, MSG_CONFIGURE), lds.stream(), lds.length(), &STATE(AgentIndividualLearning)->agentAdviceExchange);
		lds.unlock();

		lds.reset();
		lds.packString(STATE(AgentBase)->missionFile);
		this->sendMessage(this->hostCon, MSG_AGENT_START, lds.stream(), lds.length(), &STATE(AgentIndividualLearning)->agentAdviceExchange);
		lds.unlock();

		if (STATE(AgentIndividualLearning)->missionRegionReceived && STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned)
			this->finishConfigureParameters();
	}
	else {
		lds.unlock();
		// TODO try again?
	}
	recoveryCheck(&this->AgentIndividualLearning_recoveryLock4);
	return 0;
}

/* convRequestAdvice
*
* Callback for request to AgentAdviceExchange for advice.
*
* Unpacks the advised quality values, then calls the formAction method.
* When the request times out it proceeds to formAction without the quality
* values being overwritten by the advice.
*/
bool AgentIndividualLearning::convRequestAdvice(void *vpConv) {
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) {
		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAdvice: request timed out");
		this->formAction(); // Continue without advice
		return 0; // end conversation
	}

	// Start unpacking
	UUID thread;
    UUID sender;
	lds.setData(conv->response, conv->responseLen);
	lds.unpackUUID(&thread);
	lds.unpackUUID(&sender);

	// Unpack Q values
	this->q_vals.clear();
	for (int i = 0; i < this->num_actions_; i++) {
		this->q_vals.push_back(lds.unpackFloat32());
//		Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAdvice: received q_val: %f", this->q_vals.back());
	}
	lds.unlock();

//	Log.log(LOG_LEVEL_NORMAL, "AgentIndividualLearning::convRequestAdvice: Received advice.");

	// Proceed to form the next action
	this->formAction();

	return 0;
}

bool AgentIndividualLearning::convGetRunNumber(void * vpConv)
{
	DataStream lds;
	spConversation conv = (spConversation)vpConv;

	if (conv->response == NULL) { // timed out
		Log.log(LOG_LEVEL_NORMAL, "AgentTeamLearning::convGetRunNumber: timed out");
		return 0; // end conversation
	}

	lds.setData(conv->response, conv->responseLen);
	lds.unpackData(sizeof(UUID)); // discard thread
	STATE(AgentIndividualLearning)->runNumber = lds.unpackInt32();
	Log.log(0, "My run number is: %d", STATE(AgentIndividualLearning)->runNumber);
	STATE(AgentIndividualLearning)->hasReceivedRunNumber = true;
	recoveryCheck(&this->AgentIndividualLearning_recoveryLock2);
	return false;
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

	_WRITE_STATE_MAP_LESS( UUID, DDBRegion, UUIDless, &collectionRegions);
	ds->packData(&avatar, sizeof(avatar));
	ds->packUUID(&avatarId);
	ds->packUUID(&avatarAgentId);
	_WRITE_STATE_MAP_LESS(UUID, AVATAR_INFO, UUIDless, &otherAvatars);
	ds->packBool(hasCargo);
	ds->packBool(hasDelivered);

	_WRITE_STATE_VECTOR(float, &this->q_learning_.q_table_);
	_WRITE_STATE_VECTOR(unsigned int, &this->q_learning_.exp_table_);

	_WRITE_STATE_VECTOR(float, &this->q_learning_.q_vals_);
	_WRITE_STATE_VECTOR(unsigned int, &this->q_learning_.exp_vals_);

	_WRITE_STATE_VECTOR(float, &this->q_vals);
	_WRITE_STATE_VECTOR(unsigned int, &this->stateVector);
	_WRITE_STATE_VECTOR(unsigned int, &this->prevStateVector);
	ds->packFloat32(q_avg);

												// Learning parameters
	ds->packUInt32(learning_iterations_);          // Counter for how many times learning is performed
	ds->packUInt32(random_actions_);               // Counter for number of random actions
	ds->packUInt32(learned_actions_);              // Counter for number of learned actions

	// Task data
	ds->packUUID(&taskId);
	ds->packData(&task, sizeof(task));
	ds->packData(&target, sizeof(target));

	_WRITE_STATE_MAP(unsigned char, DDBLandmark, &targetList);
	_WRITE_STATE_MAP(unsigned char, DDBLandmark, &obstacleList);

	ds->packData(&totalActions, sizeof(totalActions));
	ds->packData(&usefulActions, sizeof(usefulActions));

    return AgentBase::writeState(ds, false);
}// end writeState

int	AgentIndividualLearning::readState(DataStream *ds, bool top) {
    if (top) _READ_STATE(AgentIndividualLearning);

	_READ_STATE_MAP(UUID, DDBRegion, &collectionRegions);
	this->avatar = *(AVATAR_INFO*)ds->unpackData(sizeof(AVATAR_INFO));
	ds->unpackUUID(&avatarId);
	ds->unpackUUID(&avatarAgentId);
	_READ_STATE_MAP(UUID, AVATAR_INFO, &otherAvatars);
	hasCargo = ds->unpackBool();
	hasDelivered = ds->unpackBool();

	_READ_STATE_VECTOR(float, &this->q_learning_.q_table_);
	_READ_STATE_VECTOR(unsigned int, &this->q_learning_.exp_table_);

	_READ_STATE_VECTOR(float, &this->q_learning_.q_vals_);
	_READ_STATE_VECTOR(unsigned int, &this->q_learning_.exp_vals_);

	_READ_STATE_VECTOR(float, &this->q_vals);
	_READ_STATE_VECTOR(unsigned int, &this->stateVector);
	_READ_STATE_VECTOR(unsigned int, &this->prevStateVector);
	q_avg = ds->unpackFloat32();

	// Learning parameters
	learning_iterations_ = ds->unpackUInt32();          // Counter for how many times learning is performed
	random_actions_ = ds->unpackUInt32();               // Counter for number of random actions
	learned_actions_ = ds->unpackUInt32();              // Counter for number of learned actions

												   // Task data
	ds->unpackUUID(&taskId);
	this->task = *(DDBTask*)ds->unpackData(sizeof(DDBTask));
	this->target = *(DDBLandmark*)ds->unpackData(sizeof(DDBLandmark));

	_READ_STATE_MAP(unsigned char, DDBLandmark, &targetList);
	_READ_STATE_MAP(unsigned char, DDBLandmark, &obstacleList);

	this->totalActions = *(unsigned long*)ds->unpackData(sizeof(unsigned long));
	this->usefulActions = *(unsigned long*)ds->unpackData(sizeof(unsigned long));

    return AgentBase::readState(ds, false);
}// end readState

int AgentIndividualLearning::recoveryFinish() {
    if (AgentBase::recoveryFinish())
        return 1;

	int action = (int)ceil(randomGenerator.Uniform01() * num_actions_);
	Log.log(0, "RecoveryFinish actionTest: num_actions_ %d", num_actions_);
	Log.log(0, "RecoveryFinish actionTest: %d", action);
	this->updateStateData();

    return 0;
}// end recoveryFinish

int AgentIndividualLearning::writeBackup(DataStream *ds) {

	_WRITE_STATE(AgentIndividualLearning);

	_WRITE_STATE_MAP_LESS(UUID, DDBRegion, UUIDless, &collectionRegions);
	ds->packData(&avatar, sizeof(avatar));
	ds->packUUID(&avatarId);
	ds->packUUID(&avatarAgentId);
	_WRITE_STATE_MAP_LESS(UUID, AVATAR_INFO, UUIDless, &otherAvatars);
	ds->packBool(hasCargo);
	ds->packBool(hasDelivered);

	_WRITE_STATE_VECTOR(float, &this->q_learning_.q_table_);
	_WRITE_STATE_VECTOR(unsigned int, &this->q_learning_.exp_table_);

	_WRITE_STATE_VECTOR(float, &this->q_learning_.q_vals_);
	_WRITE_STATE_VECTOR(unsigned int, &this->q_learning_.exp_vals_);

	_WRITE_STATE_VECTOR(float, &this->q_vals);
	_WRITE_STATE_VECTOR(unsigned int, &this->stateVector);
	_WRITE_STATE_VECTOR(unsigned int, &this->prevStateVector);
	ds->packFloat32(q_avg);

	// Learning parameters
	ds->packUInt32(learning_iterations_);          // Counter for how many times learning is performed
	ds->packUInt32(random_actions_);               // Counter for number of random actions
	ds->packUInt32(learned_actions_);              // Counter for number of learned actions

												   // Task data
	ds->packUUID(&taskId);
	ds->packData(&task, sizeof(task));
	ds->packData(&target, sizeof(target));

	_WRITE_STATE_MAP(unsigned char, DDBLandmark, &targetList);
	_WRITE_STATE_MAP(unsigned char, DDBLandmark, &obstacleList);

	ds->packData(&totalActions, sizeof(totalActions));
	ds->packData(&usefulActions, sizeof(usefulActions));

    return AgentBase::writeBackup(ds);
}// end writeBackup

int AgentIndividualLearning::readBackup(DataStream *ds) {
    DataStream lds;

	_READ_STATE(AgentIndividualLearning);

	_READ_STATE_MAP(UUID, DDBRegion, &collectionRegions);
	this->avatar = *(AVATAR_INFO*)ds->unpackData(sizeof(AVATAR_INFO));
	ds->unpackUUID(&avatarId);
	ds->unpackUUID(&avatarAgentId);
	_READ_STATE_MAP(UUID, AVATAR_INFO, &otherAvatars);
	hasCargo = ds->unpackBool();
	hasDelivered = ds->unpackBool();

	_READ_STATE_VECTOR(float, &this->q_learning_.q_table_);
	_READ_STATE_VECTOR(unsigned int, &this->q_learning_.exp_table_);

	_READ_STATE_VECTOR(float, &this->q_learning_.q_vals_);
	_READ_STATE_VECTOR(unsigned int, &this->q_learning_.exp_vals_);

	_READ_STATE_VECTOR(float, &this->q_vals);
	_READ_STATE_VECTOR(unsigned int, &this->stateVector);
	_READ_STATE_VECTOR(unsigned int, &this->prevStateVector);
	q_avg = ds->unpackFloat32();

	// Learning parameters
	learning_iterations_ = ds->unpackUInt32();          // Counter for how many times learning is performed
	random_actions_ = ds->unpackUInt32();               // Counter for number of random actions
	learned_actions_ = ds->unpackUInt32();              // Counter for number of learned actions

														// Task data
	ds->unpackUUID(&taskId);
	this->task = *(DDBTask*)ds->unpackData(sizeof(DDBTask));
	this->target = *(DDBLandmark*)ds->unpackData(sizeof(DDBLandmark));

	_READ_STATE_MAP(unsigned char, DDBLandmark, &targetList);
	_READ_STATE_MAP(unsigned char, DDBLandmark, &obstacleList);

	this->totalActions = *(unsigned long*)ds->unpackData(sizeof(unsigned long));
	this->usefulActions = *(unsigned long*)ds->unpackData(sizeof(unsigned long));

	this->randomGenerator = *new RandomGenerator();


	if (!STATE(AgentIndividualLearning)->hasReceivedRunNumber) {
		// request run number info
		UUID thread = this->conversationInitiate(AgentIndividualLearning_CBR_convGetRunNumber, DDB_REQUEST_TIMEOUT);
		if (thread == nilUUID) {
			return 1;
		}
		lds.reset();
		lds.packUUID(&thread);
		//lds.packUUID(&STATE(AgentBase)->uuid);
		this->sendMessage(this->hostCon, MSG_RRUNNUMBER, lds.stream(), lds.length());
		lds.unlock();

		// we have tasks to take care of before we can resume
		apb->apbUuidCreate(&this->AgentIndividualLearning_recoveryLock2);
		this->recoveryLocks.push_back(this->AgentIndividualLearning_recoveryLock2);
	}

    if (STATE(AgentIndividualLearning)->missionRegionReceived && STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned) {
		if (!STATE(AgentIndividualLearning)->parametersSet)
			this->finishConfigureParameters();
    }
    else if (STATE(AgentIndividualLearning)->agentAdviceExchangeSpawned) {		//Only need mission region
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

		// we have tasks to take care of before we can resume
		apb->apbUuidCreate(&this->AgentIndividualLearning_recoveryLock3);
		this->recoveryLocks.push_back(this->AgentIndividualLearning_recoveryLock3);

    }
	else {		//Need both mission region and agentAdviceExchange
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

		// we have tasks to take care of before we can resume
		apb->apbUuidCreate(&this->AgentIndividualLearning_recoveryLock3);
		this->recoveryLocks.push_back(this->AgentIndividualLearning_recoveryLock3);

		this->spawnAgentAdviceExchange();

		// we have tasks to take care of before we can resume
		apb->apbUuidCreate(&this->AgentIndividualLearning_recoveryLock4);
		this->recoveryLocks.push_back(this->AgentIndividualLearning_recoveryLock4);

	}

	DataStream sds;

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

	// we have tasks to take care of before we can resume
	apb->apbUuidCreate(&this->AgentIndividualLearning_recoveryLock1);
	this->recoveryLocks.push_back(this->AgentIndividualLearning_recoveryLock1);

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