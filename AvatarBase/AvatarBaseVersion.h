#define AvatarBase_MAJOR 0
#define AvatarBase_MINOR 1
#define AvatarBase_BUILDNO 243
#define AvatarBase_EXTEND 0
#define AvatarBase_UUID "e2e80015-91d2-42cf-9b3d-8bca98c91817"
#define AvatarBase_PROCESS_COST 0.05, 0.05
#define AvatarBase_TRANSFER_PENALTY 1
#define AvatarBase_RESOURCE_REQUIREMENTS ""

namespace AvatarBase_Defs {
	enum AvatarActions {
		AA_WAIT = 0,	// data = [float seconds]
		AA_MOVE,		// data = [float meters]
		AA_ROTATE,		// data = [float radians]
		AA_IMAGE,		// data = [float delay]
		AA_HIGH,
	};

	enum AvatarActionResults {
		AAR_SUCCESS = 0,	// data = [_timeb time]
		AAR_CANCELLED,		// data = [int reason, _timeb time]
		AAR_ABORTED,		// data = [int reason, _timeb time]
	};

	enum TARGETPOS_FAIL_REASONS {
		TP_NEW_TARGET = 0,
		TP_TARGET_OCCUPIED,
		TP_TARGET_UNREACHABLE,
		TP_MOVE_ERROR,
		TP_STUCK,
		TP_NOT_STARTED,
	};
}

namespace AvatarBase_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_ADD_MAP = MSG_FIRST, // add map to use [UUID uuid]
		MSG_ADD_REGION,		// add mission region [UUID uuid]

		MSG_SET_POS,		// set current pos [float x, float y, float r]
		MSG_SET_TARGET,		// set target pos [float x, float y, float r, char useRotation, UUID initiator, int controllerIndex, UUID thread]
							// RESPONSE: go target result (reason provide on failures) [UUID thread, char result <, int reason>]
		
		MSG_ACTION_QUEUE,	// queue action (see action definition for data format) [UUID director, UUID thread, int action <, ...data>]
							// RESPONSE: action result (see result definitions for data format) [UUID thread, char result, ... data]
		MSG_ACTION_CLEAR,	// clear all actions [int reason]
		MSG_ACTION_ABORT,	// abort all actions [int reason]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID),	// MSG_ADD_MAP
		-2,				// MSG_ADD_REGION

		4 + 4 + 4,		// MSG_SET_POSE
		4 + 4 + 4 + 1 + sizeof(UUID)*2 + 4, // MSG_SET_TARGET

		-3,				// MSG_ACTION_QUEUE
		4,				// MSG_ACTION_CLEAR
		4,				// MSG_ACTION_ABORT
	};

}