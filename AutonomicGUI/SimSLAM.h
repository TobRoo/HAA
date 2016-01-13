
#ifdef PERFECT_RUN
	#define SimSLAM_PROCESSING_COST_SONAR		1 // sonar readings cost x ms to process
	#define SimSLAM_PROCESSING_COST_LANDMARK	1 // landmark readings cost x ms to process
	#define SimSLAM_PROCESSING_COST_COOCCUPANCY	1 // cooccupancy readings cost x ms to process
#else
	#define SimSLAM_PROCESSING_COST_SONAR		1000 // sonar readings cost x ms to process
	#define SimSLAM_PROCESSING_COST_LANDMARK	2000 // landmark readings cost x ms to process
	#define SimSLAM_PROCESSING_COST_COOCCUPANCY	300  // cooccupancy readings cost x ms to process
#endif

#define SimSLAM_MAX_THREADS	256 // number of simultaneous sensor processings that can happen
#define SimSLAM_QUEUE_COUNT	2 // number of processing queues

class SimSensorSonar;
class SimSensorLandmark;
class SimCooccupancy;

class SimSLAM : public SimAgentBase {
public:
	enum PROCESSING_ORDER {
		PO_FIFO = 0,
		PO_FIFO_DELAY, // delay movement until all processing is done
		PO_LIFO,
		PO_LIFO_PURGE, // discard any readings that are not immediately processed
	};

	enum PROCESSING_PRIORITY {
		PP_NONE = 0,
		PP_LANDMARK, // camera readings prioritized
	};

	struct READING_INFO {
		int type;
		UUID sensor;
		_timeb time;
		_timeb sortTime; // time used when inserting into reading queue
	};

public:
	SimSLAM( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimSLAM();

	virtual int configure();
	virtual int start();
	virtual int stop();
	virtual int preStep( int dt );
	virtual int step();

	int setMap( UUID *id );
	int setProcessingOpts( int order, int priority );
	int addThreadChange( _timeb *tb, int threads );

	int addReading( int type, UUID *sensor, _timeb *tb );

	int getReadingQueueSize();
	int getProcessingOrder() { return this->processingOrder; };

protected:
	
	UUID mapId;
	
	int readingsProcessed; // tracks the total number of readings processed

	int activeThreads; // number of active threads
	int processingOrder; // order to process readings
	int processingPriority; // priority mode
	_timeb nextProcess[SimSLAM_MAX_THREADS];
	std::list<READING_INFO> readingQueue[SimSLAM_QUEUE_COUNT]; // reading queues indexed by priority

	std::list<_timeb> threadTQueue; // list of times when the thread count changes
	std::list<int>    threadNQueue; // list of thread counts to change to

	SimSensorSonar *sensorSonar; // sonar processing agent
	SimSensorLandmark *sensorLandmark; // landmark processing agent
	SimCooccupancy *cooccupancy; // cooccupancy processing agent

public:
	
	DECLARE_CALLBACK_CLASS( SimSLAM )
	
};