#define SupervisorExplore_MAJOR 0
#define SupervisorExplore_MINOR 1
#define SupervisorExplore_BUILDNO 40
#define SupervisorExplore_EXTEND 0
#define SupervisorExplore_UUID "646b27b1-9078-4b8e-9284-6324a7e53f4d"
#define SupervisorExplore_PROCESS_COST 0.05, 0.05
#define SupervisorExplore_TRANSFER_PENALTY 0.1
#define SupervisorExplore_RESOURCE_REQUIREMENTS ""

namespace SupervisorExplore_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_ADD_REGION = MSG_FIRST, // add region to explore [UUID uuid, char forbidden]
		MSG_ADD_MAP,				// add map [UUID uuid]
		MSG_AVATAR_EXEC,			// set avatar exec id [UUID avatarExec]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID) + 1,		// MSG_ADD_REGION
		sizeof(UUID),			// MSG_ADD_MAP
		sizeof(UUID),			// MSG_AVATAR_EXEC
	};

}