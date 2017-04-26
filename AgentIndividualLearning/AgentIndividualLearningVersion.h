#define AgentIndividualLearning_MAJOR 0
#define AgentIndividualLearning_MINOR 1
#define AgentIndividualLearning_BUILDNO 19
#define AgentIndividualLearning_EXTEND 0
#define AgentIndividualLearning_UUID "1eacd02e-c926-480a-98da-272e025101df" // Need to generate new one
#define AgentIndividualLearning_PROCESS_COST 0.05, 0.05
#define AgentIndividualLearning_TRANSFER_PENALTY 0.1
#define AgentIndividualLearning_RESOURCE_REQUIREMENTS ""

namespace AgentIndividualLearning_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST, // configure [UUID owner, UUID map, UUID mission region, UUID pf, float maxLinear, float maxRotation, float minLinear, flaot minRotation, int capacity, int instance]
		MSG_REQUEST_ADVICE,        // Requesting advice from AgentAdviceExchange [UUID owner, UUID convo, float q[0], float q[1], ..., unsigned int state[0], unsigned int state[1], ...]
		MSG_REQUEST_Q_VALUES,      // Requesting Q-values from Q-learning [UUID owner, UUID convo, float q[0], float q[1], ..., unsigned int state[0], unsigned int state[1], ...]
		MSG_LAST			       // last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		-2,					// MSG_CONFIGURE
		2*sizeof(UUID)+sizeof(float)+5*sizeof(float)+7*sizeof(unsigned int),                 // MSG_REQUEST_ADVICE
		2 * sizeof(UUID) + 5 * sizeof(float),                 // MSG_REQUEST_Q_VALUES

	};

}