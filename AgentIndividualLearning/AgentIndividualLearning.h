// AgentIndividualLearning.h : main header file for the AgentIndividualLearning DLL

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


/*
----------------- TODO -----------------
-Organize class members to be either in the STATE structure, or private,
 still unsure about the backup process and what data should be where
-Fill in the writeBackup and readBackup sections
-Get data about obstacles, and put it in validAction to check against obstacle
 collisions, and getStateVector to search for the closest obstacle
- Get target type, and add it to getStateVector
----------------------------------------
*/




#pragma once

#include "..\\autonomic\\DDB.h"
#include "QLearning.h"
#include <numeric>

#define ADVICE_REQUEST_TIMEOUT	50	//Wait 50 ms for advice (normally takes only ~1-2 ms), otherwise proceed with own values
//#define USE_ADVICE_EXCHANGE

struct ActionPair {
    int action;
    float val;
};

struct AVATAR_INFO {
	UUID avatarAgentId;
	char instance;
    bool ready; // avatar info is ready
    UUID pf;
    _timeb start;
    _timeb end;
    char retired;
    float innerRadius;
    float outerRadius;
    float x, y, r;
    bool locValid; // loc was successfully updated
	ITEM_TYPES capacity;	//1 = light items, 2 = heavy items (0 = NON_COLLECTABLE, cannot carry items)
};

#ifdef USE_ADVICE_EXCHANGE
typedef struct Adviser {
	UUID id;					// Id of adviser individual learning agent
	int avatarInstance;			// Instance of associated avatar
	std::vector<float> advice;  // vector of advised quality values
	float cq;                   // Current average quality
	float bq;                   // Best average quality
};
#endif


class AgentIndividualLearning : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables

public:
    struct State {
        AgentBase::State state; // inherit state from AvatarBase

        // Agent data --
        UUID ownerId;
        bool parametersSet;
        bool startDelayed; // start has been delayed because parameters are not set yet

        // State data
        float prev_pos_x;		// This avatar's position
        float prev_pos_y;		// This avatar's position
        float prev_orient;		// This avatar's orientation
        float prev_target_x;	// Avatar's target position
        float prev_target_y;	// Avatar's target position
        float goal_x;			// Foraging goal position
        float goal_y;			// Foraging goal position
        float obst_x;			// Closest obstacle position
        float obst_y;			// Closest obstacle position

        // Avatar data
        float maxLinear;   // maximum distance per move
        float maxRotation; // maximum distance per rotation
        float minLinear;   // minimum distance per move
        float minRotation; // minimum distance per rotation

        // Action data
        ActionPair action;
		
		ActionPair stuckAction;	//Action to take in the case the avatar gets stuck out of bounds or in an obstacle (reversing the previous action)


		// Advice data

        UUID regionId;
        DDBRegion missionRegion;

        bool missionRegionReceived;

		int avatarInstance;
		int runNumber;
		bool hasReceivedRunNumber;

		bool collectRequestSent;
		bool depositRequestSent;

		bool hasQLearningData;

		bool isStuck;

		float latestReward;	//Reward given for the latest action performed, used for upload and performance tracking


    };

//-----------------------------------------------------------------------------
// Non-state member variables

protected:
    UUID AgentIndividualLearning_recoveryLock1;
	UUID AgentIndividualLearning_recoveryLock2;
	UUID AgentIndividualLearning_recoveryLock3;
	UUID AgentIndividualLearning_recoveryLock5;

    DataStream ds; // shared DataStream object, for temporary, single thread, use only!

    std::map<UUID, DDBRegion, UUIDless> collectionRegions;
    AVATAR_INFO avatar;
    UUID avatarId;
    UUID avatarAgentId;
    std::map<UUID, AVATAR_INFO, UUIDless> otherAvatars;
    
    bool hasCargo;
    bool hasDelivered;

    QLearning q_learning_;				   	    // QLearning object
	std::vector<float> q_vals;                  // Vector of quality values (for the current state vector)
	std::vector<unsigned int> exp_vals;                // Vector of experience values (for the current state vector)
    std::vector<unsigned int> stateVector;		// Current state vector
    std::vector<unsigned int> prevStateVector;	// Previous state vector
	float q_avg;                                // Cumulative average quality for selected action

    // Learning parameters
    unsigned int learning_iterations_;          // Counter for how many times learning is performed
    unsigned int learning_frequency_;			// How often learning is performed
    unsigned int random_actions_;               // Counter for number of random actions
    unsigned int learned_actions_;              // Counter for number of learned actions
    unsigned int num_state_vrbls_;              // Number of variables in state vector
    std::vector<unsigned int> state_resolution_;// Bits required to express state values
    float look_ahead_dist_;						// Distance robot looks ahead for obstacle state info

    // Policy parameters
    float softmax_temp_;					    // Coefficient for softmax policy temp

    // Reward parameters
    float item_closer_reward_;					// For moving target item closer to goal
    float item_further_reward_;					// For moving target item further from goal
    float robot_closer_reward_;					// For moving closer to the target item
    float robot_further_reward_;				// For moving further from the target item
    float return_reward_;						// For returning the item to the goal area
    float empty_reward_value_;					// Default reward, when no other rewards are given
    float reward_activation_dist_;				// Minimum distance to move for item/robot closer/further reward

    // Action parameters
    float backupFractionalSpeed;				// Percent of forward speed to use for reverse motion
    unsigned int num_actions_;                  // Number of possible actions
    enum ActionId {
        MOVE_FORWARD = 1,
        MOVE_BACKWARD,
        ROTATE_LEFT,
        ROTATE_RIGHT,
        INTERACT
    };

    // Task data
    UUID taskId;
    DDBTask task;
	DDBLandmark target;
    std::map<unsigned char, DDBLandmark> targetList;
    std::map<unsigned char, DDBLandmark> obstacleList;

    //Metrics
    unsigned long totalActions;
    unsigned long usefulActions;

	// Random number generator
	RandomGenerator *randomGenerator;

#ifdef USE_ADVICE_EXCHANGE

	//Advice exchange parameters

	float alpha;                                // Coefficient for current average quality metric cq
	float beta;                                 // Coefficient for best avrage quality metric bq
	float delta;                                // Coefficient for bq comparison
	float rho;                                  // Coefficient for quality sum comparison


	// Personal data and metrics
	std::vector<float> q_vals_in;               // Incoming Q values received during advice request
	std::vector<unsigned int> state_vector_in;  // Incoming state vector received during advice request
	float q_avg_epoch_in;						 // Incoming average q-value received during advice request
	float cq;                                   // Current average quality
	float bq;                                   // Best average quality


	std::map<UUID, Adviser, UUIDless> adviserData;   // All advice data for each adviser
	Adviser adviser;							// This agent's adviser 

	// Data monitoring
	int condA_count;
	int condB_count;
	int condC_count;
	int ask_count;

	bool hasAgentList;

#endif

	UUID cbGoAfterResumeId; //Id for resume callback, does not need to be saved

//-----------------------------------------------------------------------------
// Functions

public:
    AgentIndividualLearning( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
    ~AgentIndividualLearning();

    virtual int configure();	// initial configuration
    virtual int start( char *missionFile );		// start agent
    virtual int stop();			// stop agent
    virtual int step();			// run one step

#ifdef USE_ADVICE_EXCHANGE
	int formAdvice(UUID *conv, UUID *advisee, std::vector<float> advice);         // Performs the Advice Exchange algorithm and sends the advice back

	int preRunUpdate();     // Performs the necessary updates before a new epoch begins
	int postRunUpdate();   // Performs the necessary updates after an epoch is finished

	int parseAdviserData();
	int uploadAdviceData();
#endif

private:

    int configureParameters( DataStream *ds );
    int finishConfigureParameters();
    int updateStateData();
    int preActionUpdate();
	int formAction();
    int getStateVector();
    int policy(std::vector<float> &quality, std::vector<unsigned int> &experience);
	int requestAdvice(std::vector<float> &quality, std::vector<unsigned int> &state_vector);
    int sendAction(ActionPair action);
    int learn();
    float determineReward();
    bool validAction(ActionPair &action);

	int spawnAgentAdviceExchange();		//Deprecated, use built-in

    //Learning data storage for multiple simulation runs - so far, only Q-Learning implemented

    int uploadLearningData();		//General function, selects method depending on policy - so far, only Q-Learning implemented
    int uploadQLearningData(bool onlyActions);		//Uploads Q-Learning data to the DDB
	int updateQLearningData();		//Updates the data stored in the DDB with qvals of the latest action, and increments action counts 
    int parseLearningData();		//Loads data from previous runs when constructing the agent

    virtual int ddbNotification(char *data, int len);

protected:
    virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
    // -- Callbacks -----------------------------------------
    DECLARE_CALLBACK_CLASS( AgentIndividualLearning )

    // Enumerate callback references
    enum CallbackRef {
        AgentIndividualLearning_CBR_convAction = AgentBase_CBR_HIGH,
        AgentIndividualLearning_CBR_convRequestAvatarLoc,
        AgentIndividualLearning_CBR_convGetAvatarList,
        AgentIndividualLearning_CBR_convGetAvatarInfo,
		AgentIndividualLearning_CBR_convGetLandmarkList,
        AgentIndividualLearning_CBR_convLandmarkInfo,
        AgentIndividualLearning_CBR_convGetTargetInfo,
        AgentIndividualLearning_CBR_convMissionRegion,
        AgentIndividualLearning_CBR_convGetTaskList,
        AgentIndividualLearning_CBR_convGetTaskInfo,
        AgentIndividualLearning_CBR_convCollectLandmark,
		AgentIndividualLearning_CBR_convRequestAdvice,
		AgentIndividualLearning_CBR_convGetRunNumber,
		AgentIndividualLearning_CBR_convDepositLandmark,
		AgentIndividualLearning_CBR_convGetQLearningData,
#ifdef USE_ADVICE_EXCHANGE
		AgentIndividualLearning_CBR_convGetAgentList,
#endif
		AgentIndividualLearning_CBR_cbGoAfterResume,
    };

    // Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
    bool convAction(void *vpConv);
    bool convRequestAvatarLoc(void *vpConv);
    bool convGetAvatarList(void *vpConv);
    bool convGetAvatarInfo(void *vpConv);
	bool convGetLandmarkList(void * vpConv);
    bool convLandmarkInfo(void *vpConv);
    bool convGetTargetInfo(void * vpConv);
    bool convMissionRegion(void *vpConv);

    bool convGetTaskInfo(void * vpConv);
    bool convGetTaskList(void * vpConv);

    bool convCollectLandmark(void * vpConv);
	bool convDepositLandmark(void * vpConv);
	bool convRequestAdvice(void *vpConv);

	bool convGetRunNumber(void * vpConv);

	bool convGetQLearningData(void * vpConv);

#ifdef USE_ADVICE_EXCHANGE
	bool convGetAgentList(void *vpConv);
#endif
	bool cbGoAfterResume(void *vpConv);
protected:
    virtual int	  freeze( UUID *ticket );
    virtual int   thaw( DataStream *ds, bool resumeReady = true );

    virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
    virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

    virtual int	  recoveryFinish();

    virtual int   writeBackup( DataStream *ds ); // write backup data to stream
    virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentIndividualLearningDLL: Override ExitInstance for debugging memory leaks
class CAgentIndividualLearningDLL : public CWinApp {
public:
    CAgentIndividualLearningDLL();
    virtual BOOL ExitInstance();
};