#define AgentPathPlanner_MAJOR 0
#define AgentPathPlanner_MINOR 1
#define AgentPathPlanner_BUILDNO 19
#define AgentPathPlanner_EXTEND 0
#define AgentPathPlanner_UUID "5eacd02e-c926-480a-98da-272e025101df"
#define AgentPathPlanner_PROCESS_COST 0.05, 0.05
#define AgentPathPlanner_TRANSFER_PENALTY 0.1
#define AgentPathPlanner_RESOURCE_REQUIREMENTS ""

namespace AgentPathPlanner_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST, // configure [UUID owner, UUID map, UUID mission region, UUID pf, float maxLinear, float maxRotation, float minLinear, flaot minRotation]
		MSG_SET_TARGET,		// set target pos [float x, float y, float r, char useRotation, UUID thread]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID)*4 + 4*4,	// MSG_CONFIGURE
		4 + 4 + 4 + 1 + sizeof(UUID),	// MSG_SET_TARGET
	};

}