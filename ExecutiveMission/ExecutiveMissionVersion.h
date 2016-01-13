#define ExecutiveMission_MAJOR 0
#define ExecutiveMission_MINOR 1
#define ExecutiveMission_BUILDNO 263
#define ExecutiveMission_EXTEND 0
#define ExecutiveMission_UUID "3ce2a3f1-5b63-4481-9fc8-544857faa381"
#define ExecutiveMission_PROCESS_COST 0.05, 0.05
#define ExecutiveMission_TRANSFER_PENALTY 0.1
#define ExecutiveMission_RESOURCE_REQUIREMENTS ""

namespace ExecutiveMission_Defs {

	enum RequestAvatar {
		RA_ONE = 0,
		RA_ALL,
		RA_AVAILABLE,
	};

}

namespace ExecutiveMission_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_REQUEST_AVATAR = MSG_FIRST,	// request avatar resources [UUID agent, char mode, int count, UUID avatar, int priority, ..., UUID thread]
										// RESPONSE [UUID thread, char success]
		MSG_RELEASE_AVATAR,				// release avatar resource [UUID agent, UUID avatar]

		MSG_TASK_COMPLETE,				// supervisor task is complete [UUID agent]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		-2,					// MSG_REQUEST_AVATAR
		sizeof(UUID)*2,		// MSG_RELEASE_AVATAR

		sizeof(UUID),		// MSG_TASK_COMPLETE
	};

}
