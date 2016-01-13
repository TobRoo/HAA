// AgentMirrorGUI.h : main header file for the AgentMirrorGUI
//

#include "AgentMirror.h"

#include "Visualize.h"
#include "GUIObject.h"

#include <map>

struct ObjectInfo {
	GUIObjectBase *object;
	HTREEITEM handle;
};

struct AgentGraphInfo {
	UUID id;
	char name[256];
	int start; // start time (ms)
	int end; // end time (ms)
	int endStatus; // status when ended
	float yOffset; // last yOffset when drawn
};

struct AgentGraphConnection {
	UUID id;
	int start; // start time (ms)
	int end; // end time (ms)
	int status; // type of connection
	AgentGraphInfo *iStart; // pointer to the start agent
	AgentGraphInfo *iEnd; // pointer to the end agent
};

class AgentMirrorGUI : public AgentMirror {

public:
	AgentMirrorGUI( ThinSocket *hostSocket, Visualize *visualizer, CTreeCtrl *objectTree, CEdit *absText  );
	~AgentMirrorGUI();

	virtual int configure();	// initial configuration
	virtual int start();		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	DDBStore *getDataStore() { return this->dStore; };
	Visualize *getVisualizer() { return this->visualizer; };

	int treeSelectionChanged( HTREEITEM handle );
	int treeCheckChanged( HTREEITEM handle );
	int treeRClick( HTREEITEM handle, CPoint *clickLoc );

	GUIObjectBase *getObject( UUID *id );
	int claimLandmarks( UUID *id, DataStream *ds );

protected:
	virtual int conProcessMessage( unsigned char message, char *data, unsigned int len );
	
private:
	Visualize *visualizer;
	int pathLib[PATHLIB_HIGH]; // library of common paths

	CTreeCtrl *objectTree;
	CEdit *absText;

	HTREEITEM catAgent;
	HTREEITEM catDDB;
	HTREEITEM catDDBLandmark; // special category for landmarks because there are a bunch of them

	std::map<UUID,ObjectInfo,UUIDless> objects;
	ObjectInfo	*lastSelected;
	std::map<UUID,UUID,UUIDless> orphans; // map of missing parents by orphan

	int checkOrphanage( UUID *id );
	int copyChildren( HTREEITEM child, HTREEITEM parent, HTREEITEM after );
	int cleanRemoveItem( UUID *id );

	// agent graph
	_timeb agentGraphT0; // baseline time for agentGraph
	_timeb agentGraphTU; // last update time
	std::map<UUID,std::list<AgentGraphInfo>,UUIDless> agentGraph; // helps visualize agent allocation
	std::list<AgentGraphConnection> agentGraphConnectionOpen; // list of open agent connections
	std::list<AgentGraphConnection> agentGraphConnection; // list of agent connections
	std::list<int> agentGraphStrings; // list of strings being visualized
	std::list<int> agentGraphObjects; // list of objects being visualized

	int agentGraphStart( UUID *id, char *name, UUID *host );
	int agentGraphEnd( UUID *id, int status );
	int agentGraphUpdateVis();

	int eventAgent( UUID id, char evt, DataStream *ds );
	int eventRegion( UUID id, char evt, DataStream *ds );
	int eventLandmark( UUID id, char evt, DataStream *ds );
	int eventParticleFilter( UUID id, char evt, DataStream *ds );
	int eventPOG( UUID id, char evt, DataStream *ds );
	int eventAvatar( UUID id, char evt, DataStream *ds );
	int eventSensor( UUID id, char evt, DataStream *ds );

public:
	DECLARE_CALLBACK_CLASS( AgentMirrorGUI )

	bool cbDDBWatcher( void *vpds );

};