
#include "..\\autonomic\\DataStream.h"
#include "..\\autonomic\\Logger.h"
#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\fImage.h"
#include "Simulation.h"
#include "Visualize.h"

class _Callback {
public:
	virtual bool callback( void *data ) = 0;
	virtual bool callback( int evt, void *data ) = 0;
};
class Callback : public _Callback {
	bool (*fA)( void *data );
	bool (*fB)( int evt, void *data );
public:
	Callback( bool (*fn)( void *data ) ) { fA = fn; };
	Callback( bool (*fn)( int evt, void *data ) ) { fB = fn; };
	bool callback( void *data = NULL ) { return (*fA)( data ); };
	bool callback( int evt, void *data = NULL ) { return (*fB)( evt, data ); };
};

#define DECLARE_CALLBACK_CLASS( ClassName ) \
class Callback : public _Callback { \
	ClassName *c; \
	bool (ClassName::*fA)( void *data ); \
	bool (ClassName::*fB)( int evt, void *data ); \
public: \
	Callback( ClassName *cls, bool (ClassName::*fn)( void *data ) ) { c = cls; fA = fn; fB = NULL; }; \
	Callback( ClassName *cls, bool (ClassName::*fn)( int evt, void *data ) ) { c = cls; fA = NULL; fB = fn; }; \
	bool callback( void *data = NULL ) { return (c->*fA)( data ); }; \
	bool callback( int evt, void *data = NULL ) { return (c->*fB)( evt, data ); }; \
};

#define NEW_STATIC_CB( Function )		new Callback( Function )
#define NEW_MEMBER_CB( ClassName, Function )	new ClassName::Callback( this, &ClassName::Function )

struct TimeoutEvent {
	unsigned int id;
	unsigned int period; // in ms   
	_timeb timeout;		
	_Callback *cb;
	struct TimeoutEvent * next;
	void *object;
	void *data;			
	int dataLen;
};
#define sTimeoutEvent struct TimeoutEvent
#define spTimeoutEvent struct TimeoutEvent *

class SimAgentBase {

public:
	SimAgentBase( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimAgentBase();

	virtual int configure();	// initial configuration
	virtual int start();		// start agent
	virtual int stop();			// stop agent
	virtual int preStep( int dt ); // move simulation forward
	virtual int step();			// run one step

	unsigned int  addTimeout( unsigned int period, _Callback *cb, void *data = NULL, int dataLen = 0 );
	int			  removeTimeout( unsigned int id );
	int			  readTimeout( unsigned int id );
	void		  resetTimeout( unsigned int id );

protected:
	_timeb simTime; // current sim time

	Simulation *sim; // simualtion pointer
	Visualize *vis; // visualization pointer
	RandomGenerator *randGen; // random generator

	Logger Log;

protected:
	UUID uuid;

	bool configured;	// configuration has been run
	bool started;		// agent has been started

public:
	bool isStarted() { return started; }; // public access to check if the agent has started

protected:
	DataStream ds; // shared DataStream object, for temporary, single thread, use only!

private:
	struct timeval timeVal;
	unsigned int   timeoutNextId;
	spTimeoutEvent firstTimeout;
	void		   checkTimeouts();
	
public:
	
	DECLARE_CALLBACK_CLASS( SimAgentBase )
	
public:
	UUID * getUUID() { return &this->uuid; };

};