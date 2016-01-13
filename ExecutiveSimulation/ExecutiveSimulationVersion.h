#define ExecutiveSimulation_MAJOR 0
#define ExecutiveSimulation_MINOR 1
#define ExecutiveSimulation_BUILDNO 18
#define ExecutiveSimulation_EXTEND 0
#define ExecutiveSimulation_UUID "b2e39474-2c56-42ac-a40e-5c14f9a38ef7"
#define ExecutiveSimulation_PROCESS_COST 0.05, 0.05
#define ExecutiveSimulation_TRANSFER_PENALTY 1
#define ExecutiveSimulation_RESOURCE_REQUIREMENTS "2e82fead-0c05-45c7-9df5-d266c711c325,67cf20af-a3c9-43eb-818d-96427a8ae6be"

namespace ExecutiveSimulation_Defs {
	enum SimAvatar_Events {
		SAE_MOVE_FINISHED = 0,	// data format: char moveId
		SAE_POSE_UPDATE,		// data format: _timeb time, float x, float y, float r, float vLin, float vAng
		SAE_SENSOR_SONAR,		// data format: char index, _timeb time, float reading
		SAE_SENSOR_CAMERA,		// data format: char index, _timeb time, int dataSize, ... camera data
		SAE_COLLECT,			// data format: unsigned char code, char success, UUID thread
		SAE_STREAM_END,
	};
};

namespace ExecutiveSimulation_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_ADDAVATAR = MSG_FIRST, // add avatar [UUID avatarId, UUID ownerId, float x, float y, float r, string avatarFile, int landmarkCodeOffset]
		MSG_REMAVATAR,			   // remove avatar [UUID avatarId, char retireMode]

		MSG_AVATAR_MOVE_LINEAR,     // move linear [UUID avatarId, float move, char moveId]
		MSG_AVATAR_MOVE_ANGULAR,    // move angular [UUID avatarId, float rotation, char moveId]
		MSG_AVATAR_STOP,		    // stop [UUID avatarId, char moveId]

		MSG_AVATAR_IMAGE,	// request image [UUID avatarId, int cameraInd]

		MSG_AVATAR_COLLECT_LANDMARK, // attempt to collect a landmark [UUID avatarId, unsigned char landmarkCode, float x, float y, UUID thread]

		MSG_RAVATAR_OUTPUT, // request avatar output [UUID avatarId, UUID thread]
							// RESPONSE: output stream, [UUID thread, char response <, ... SimAvatar_Events for data format>]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		-2,				// MSG_ADDAVATAR
		sizeof(UUID) + 1, // MSG_REMAVATAR

		sizeof(UUID) + 4 + 1,  // MSG_AVATAR_MOVE_LINEAR
		sizeof(UUID) + 4 + 1,  // MSG_AVATAR_MOVE_ANGULAR
		sizeof(UUID) + 1,      // MSG_AVATAR_STOP

		sizeof(UUID) + 4, // MSG_AVATAR_IMAGE

		sizeof(UUID) + 1 + 4*2 + sizeof(UUID), // MSG_AVATAR_COLLECT_LANDMARK

		sizeof(UUID)*2,	  // MSG_RAVATAR_OUTPUT
	};

}