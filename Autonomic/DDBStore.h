
#include <rpc.h>

#define TASK_AVATAR_CHANGE_THRESHOLD	1000	//10 ms for now, should not need to be lower?


class Logger;

class DDBStore {

public:
	DDBStore( AgentPlayback *apb, Logger *Log );
	~DDBStore();

	int Clean(); // clean up the DDB

	unsigned int DEBUGtotalPFUnpackTime;

	// DDB
	mapDDBAgent		DDBAgents; // map of Agents
	mapDDBRegion	DDBRegions; // map of Regions
	mapDDBLandmark	DDBLandmarks; // map of Landmarks
	mapDDBPOG		DDBPOGs; // map of POGs
	mapDDBParticleFilter	DDBParticleFilters; // map of particles filters by UUID
	mapDDBAvatar	DDBAvatars;	// map of all avatars by UUID
	mapDDBSensor	DDBSensors;	// map of all sensors by UUID
	mapDDBTask		DDBTasks;	// map of all tasks by UUID
	mapDDBTaskData	DDBTaskDatas;	//map of all task performance data by UUID of the owning agent (better plural form?)
	mapDDBQLearningData DDBQLearningDatas;	//map of all Q-learning data stored at the end of a run, to be loaded in the next run
	mapDDBAdviceData	DDBAdviceDatas;		//map of all advice data stored at the end of a run, to be loaded in the next run

	unsigned long long DDBTotalSimSteps;		//Count of the total amount of simulation steps that have been taken

	RoundInfoStruct DDBTLRoundInfo;		//Struct for the team learning round info

	UUID nilUUID;

	// DEBUG
	int pfForwardPropCount; // count the number of times we actually forward propagated values

	bool isValidId( UUID *id, int type );

	int EnumerateAgents( DataStream *ds );
	int ParseAgent( DataStream *ds, UUID *parsedId );
	int AddAgent( UUID *id, UUID *parentId, char *agentTypeName, UUID *agentTypeId, char agentInstance, UUID *spawnThread, float parentAffinity, char priority, float processCost, int activationMode );
	int RemoveAgent( UUID *id );
	int AgentGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread );
	int AgentSetInfo( UUID *id, int infoFlags, DataStream *ds );
	UUID * AgentGetHost( UUID *id );
	UUID * AgentGetParent( UUID *id );
	int AgentGetStatus( UUID *id );
	float AgentGetAffinity( UUID *id, UUID *affinity, unsigned int elapsedMillis = -1 );
	float AgentGetProcessCost( UUID *id, unsigned int elapsedMillis = -1 );
	
	int VisValidPathId( UUID *agentId, int id );
	int VisValidObjectId( UUID *agentId, int id );
	int VisAddPath( UUID *agentId, int id, int count, float *x, float *y );
	int VisRemovePath( UUID *agentId, int id );
	int VisExtendPath( UUID *agentId, int id, int count, float *x, float *y );
	int VisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y );
	int VisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name );
	int VisRemoveObject( UUID *agentId, int id );
	int VisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s );
	int VisSetObjectVisible( UUID *agentId, int id, char visible );
	int VisGetPath( UUID *agentId, int id, DataStream *ds, UUID *thread );
	int VisObjectGetInfo( UUID *agentId, int id, int infoFlags, DataStream *ds, UUID *thread );
	int VisClearAll( UUID *agentId, char clearPaths );
	int VisListObjects( UUID *agentId, std::list<int> *objects );
	int VisListPaths( UUID *agentId, std::list<int> *paths );

	int EnumerateRegions( DataStream *ds );
	int ParseRegion( DataStream *ds, UUID *parsedId );
	int AddRegion( UUID *id, float x, float y, float w, float h );
	int RemoveRegion( UUID *id );
	int GetRegion( UUID *id, DataStream *ds, UUID *thread );

	int EnumerateLandmarks( DataStream *ds );
	int ParseLandmark( DataStream *ds, UUID *parsedId );
	int AddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, bool estimatedPos, ITEM_TYPES landmarkType );
	int RemoveLandmark( UUID *id );
	int LandmarkSetInfo( UUID *id, int infoFlags, DataStream *ds );
	int GetLandmark( UUID *id, DataStream *ds, UUID *thread, bool enumLandmarks );
	int GetLandmark( unsigned char code, DataStream *ds, UUID *thread );
	UUID GetLandmarkId( unsigned char code );



	int EnumeratePOGs( DataStream *ds );
	int ParsePOG( DataStream *ds, UUID *parsedId );
	int AddPOG( UUID *id, float tileSize, float resolution );
	int RemovePOG( UUID *id );
	int POGGetInfo( UUID *id, DataStream *ds, UUID *thread );
	int POGVerifyRegion( UUID *id, float x, float y, float w, float h );
	int ApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data );
	int POGGetRegion( UUID *id, float x, float y, float w, float h, DataStream *ds, UUID *thread );
	int POGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename );
	int POGDumpRegion( UUID *id, float x, float y, float w, float h, char *filename );

	int EnumerateParticleFilters( DataStream *ds );
	int ParseParticleFilter( DataStream *ds, UUID *parsedId );
	int AddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize );
	int RemoveParticleFilter( UUID *id );
	int ResamplePF_Prepare( UUID *id, DataStream *ds, float *effectiveParticleNum );
	int ResamplePF_Apply( UUID *id, int *parents, float *state );
	int InsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange = false );
	int ApplyPFCorrection( UUID *id, _timeb *tb, float *obsDensity );
	int _ProcessPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity );
	int ApplyPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity, bool noForwardProp = false );
	int _PFPropagateOD( DDBParticleFilter *pf, int toRegion );
	int PFGetInfo( UUID *id, int infoFlags, _timeb *tb, DataStream *ds, UUID *thread, float *effectiveParticleNum );
	int PFDump( UUID *id, int infoFlags, _timeb *startT, _timeb *endT, char *filename );
	int PFDumpPath( UUID *id, char *filename );
	int PFDumpStatistics();
	UUID *PFGetOwner( UUID *id );
	int PFGetParticleNum( UUID *id );
	int PFGetRegionCount( UUID *id );
	int PFGetStateArraySize( UUID *id );

	int EnumerateAvatars( DataStream *ds );
	int ParseAvatar( DataStream *ds, UUID *parsedId );
	int AddAvatar( UUID *id, char *type, AgentType *agentType, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes );
	int RemoveAvatar( UUID *id );
	int AvatarGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread );
	int AvatarSetInfo( UUID *id, int infoFlags, DataStream *ds );
	
	int EnumerateSensors( DataStream *ds );
	int ParseSensor( DataStream *ds, UUID *parsedId );
	int AddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize );
	int RemoveSensor( UUID *id );
	int InsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data = NULL, int dataSize = 0 );
	int SensorGetInfo( UUID *id, int infoFlags, DataStream *ds, UUID *thread );
	int SensorGetData( UUID *id, _timeb *tb, DataStream *ds, UUID *thread );
	int GetSensorType( UUID *id );

	int AddTask(UUID *id, UUID *landmark, UUID *agent, UUID *avatar, bool completed, ITEM_TYPES type );
	int RemoveTask(UUID *id);
	int TaskSetInfo(UUID *id, UUID *agent, UUID *avatar, bool completed);
	int GetTask(UUID *id, DataStream *ds, UUID *thread, bool enumTasks);

	int AddTaskData(UUID * id, DDBTaskData * data);
	int RemoveTaskData(UUID * id);
	int TaskDataSetInfo(UUID * id, DDBTaskData * data);
	int GetTaskData(UUID * id, DataStream * ds, UUID * thread, bool enumTaskData);

	int SetTLRoundInfo(RoundInfoStruct *newRoundInfo);

	int GetTLRoundInfo(DataStream * ds, UUID * thread);

	bool GetTaskId(UUID * id, UUID * foundId);
	UUID GetTaskDataId(UUID * id);

	bool AddQLearningData(bool onlyActions, char instance, long long totalActions, long long usefulActions, int tableSize, std::vector<float> qTable, std::vector<unsigned int> expTable);
	bool UpdateQLearningData(char instance, bool usefulAction, float reward, int key, float qVal, unsigned int expVal);
	bool AddAdviceData(char instance, float cq, float bq);
	bool AddSimSteps(unsigned long long totalSimSteps);
	unsigned long long GetSimSteps();
	mapDDBAdviceData GetAdviceData();
	mapDDBQLearningData GetQLearningData();

	int GetQLearningData(DataStream * ds, UUID * thread, char instance);

	int DataDump( Logger *Data, bool fulldump, char *logDirectory ); // dump data and statistics

private:
	AgentPlayback *apb;

	Logger *Log;

};