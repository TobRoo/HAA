
#include "stdafx.h"

#include "SimAgent.h"


//*****************************************************************************
// SimAgentBase

//-----------------------------------------------------------------------------
// Constructor	

SimAgentBase::SimAgentBase( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) {
	
	this->sim = sim;
	this->vis = vis;
	this->randGen = randGen;

	this->configured = false;
	this->started = false;
	
	this->simTime.time = 0;
	this->simTime.millitm = 0;

	// uuid
	UuidCreate( &this->uuid );

	this->timeoutNextId = 1;
	this->firstTimeout = NULL;

}

//-----------------------------------------------------------------------------
// Destructor

SimAgentBase::~SimAgentBase() {

	if ( this->started ) {
		this->stop();
	}

	// clear timeouts
	spTimeoutEvent iterTE = this->firstTimeout;
	spTimeoutEvent nextTE;
	while ( iterTE ) {
		nextTE = iterTE->next;
		if ( iterTE->dataLen != 0 ) {
			free( iterTE->data );
		}
		delete iterTE->cb;
		free( iterTE );
		iterTE = nextTE;
	}

}
//-----------------------------------------------------------------------------
// Configure

int SimAgentBase::configure() {

	this->configured = true;

	return 0;
}

//-----------------------------------------------------------------------------
// Start

int SimAgentBase::start() {

	if ( !this->configured ) {
		return 1;
	}

	this->started = true;

	return 0;
}

//-----------------------------------------------------------------------------
// Stop

int SimAgentBase::stop() {
	this->started = false;

	return 0;
}

//-----------------------------------------------------------------------------
// Step

int SimAgentBase::preStep( int dt ) {

	if ( !this->started )
		return 1;

	this->simTime.time += (this->simTime.millitm + dt)/1000;
	this->simTime.millitm = (this->simTime.millitm + dt) % 1000;

	return 0;
}

int SimAgentBase::step() {

	if ( !this->started )
		return 1;

	this->checkTimeouts(); // process any outstanding timeouts

	return 0;
}

//-----------------------------------------------------------------------------
// Timeout functions

unsigned int SimAgentBase::addTimeout( unsigned int period, _Callback *cb, void *data, int dataLen ) {
	spTimeoutEvent iter;
	spTimeoutEvent prev;
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent = (spTimeoutEvent)malloc(sizeof(sTimeoutEvent));
	if ( !timeoutEvent ) {
		delete cb;
		return NULL;
	}

	// generate unique id
	int firstId = this->timeoutNextId;
	while (1) {
		timeoutEvent->id = this->timeoutNextId++;
		if ( timeoutEvent->id == NULL ) // 0 is not a valid id
			timeoutEvent->id = this->timeoutNextId++;
		iter = this->firstTimeout;
		while ( iter != NULL ) {
			if ( iter->id == timeoutEvent->id ) 
				break;
			iter = iter->next;
		}
		if ( iter == NULL )
			break;
		if ( this->timeoutNextId == firstId ) { // this should never happen
			delete cb;
			free( timeoutEvent );
			return NULL;
		}
	}

	if ( dataLen ) {
		timeoutEvent->data = malloc(dataLen);
		if ( !timeoutEvent->data ) {
			delete cb;
			free( timeoutEvent );
			return NULL;
		}
		memcpy( timeoutEvent->data, data, dataLen );
		timeoutEvent->dataLen = dataLen;
	} else {
		timeoutEvent->data = NULL;
		timeoutEvent->dataLen = 0;
	}

	timeoutEvent->period = period;
	timeoutEvent->timeout = this->simTime;
	secs = period / 1000;
	msecs = period % 1000;
	timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
	timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
	timeoutEvent->cb = cb;

	// insert
	iter = this->firstTimeout;
	prev = NULL;
	while ( iter != NULL ) {
		if ( iter->timeout.time > timeoutEvent->timeout.time )
			break;
		if ( iter->timeout.time == timeoutEvent->timeout.time 
		  && iter->timeout.millitm >= timeoutEvent->timeout.millitm )
			break;
		prev = iter;
		iter = iter->next;
	}
	if ( prev ) prev->next = timeoutEvent;
	else this->firstTimeout = timeoutEvent;
	timeoutEvent->next = iter;

	return timeoutEvent->id;
}

int SimAgentBase::removeTimeout( unsigned int id ) {
	spTimeoutEvent iter = this->firstTimeout;
	spTimeoutEvent prev = NULL;
	while ( iter != NULL && iter->id != id ) {
		prev = iter;
		iter = iter->next;
	}

	if ( iter == NULL ) {
		return NULL; // not found
	}

	// remove
	if ( prev ) prev->next = iter->next;
	else this->firstTimeout = iter->next;
	if ( iter->dataLen )
		free( iter->data );
	delete iter->cb;
	free( iter );

	return 0;
}

int SimAgentBase::readTimeout( unsigned int id ) {

	spTimeoutEvent iter = this->firstTimeout;
	while ( iter != NULL && iter->id != id ) {
		iter = iter->next;
	}

	if ( iter == NULL )
		return -1; // didn't find the timeout

	return max( 0, (int)(iter->timeout.time - this->simTime.time)*1000 + iter->timeout.millitm - this->simTime.millitm );
}

void SimAgentBase::resetTimeout( unsigned int id ) {
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent;

	spTimeoutEvent iter = this->firstTimeout;
	spTimeoutEvent prev = NULL;
	while ( iter != NULL && iter->id != id ) {
		prev = iter;
		iter = iter->next;
	}

	if ( iter == NULL )
		return; // didn't find the timeout
	
	// remove from list
	if ( prev ) prev->next = iter->next;
	else this->firstTimeout = iter->next;

	// reset timeout
	timeoutEvent = iter;
	timeoutEvent->timeout = this->simTime;
	secs = timeoutEvent->period / 1000;
	msecs = timeoutEvent->period % 1000;
	timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
	timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
	
	// reinsert
	iter = this->firstTimeout;
	prev = NULL;
	while ( iter != NULL ) {
		if ( iter->timeout.time > timeoutEvent->timeout.time )
			break;
		if ( iter->timeout.time == timeoutEvent->timeout.time 
		  && iter->timeout.millitm >= timeoutEvent->timeout.millitm )
			break;
		prev = iter;
		iter = iter->next;
	}
	if ( prev ) prev->next = timeoutEvent;
	else this->firstTimeout = timeoutEvent;
	timeoutEvent->next = iter;
}

void SimAgentBase::checkTimeouts() {
	unsigned int secs, msecs;
	spTimeoutEvent timeoutEvent;
	spTimeoutEvent iter;
	spTimeoutEvent prev;

	while ( this->firstTimeout ) {
		if ( this->firstTimeout->timeout.time > this->simTime.time )
			break;
		if ( this->firstTimeout->timeout.time == this->simTime.time 
		  && this->firstTimeout->timeout.millitm >= this->simTime.millitm )
			break;
	
		timeoutEvent = this->firstTimeout;
		this->firstTimeout = timeoutEvent->next;
		if ( timeoutEvent->cb->callback( timeoutEvent->data ) ) { // repeat timeout event
			secs = timeoutEvent->period / 1000;
			msecs = timeoutEvent->period % 1000;
			timeoutEvent->timeout.time += secs + (timeoutEvent->timeout.millitm + msecs) / 1000;
			timeoutEvent->timeout.millitm = (timeoutEvent->timeout.millitm + msecs) % 1000;
			iter = this->firstTimeout;
			prev = NULL;
			while ( iter != NULL ) {
				if ( iter->timeout.time > timeoutEvent->timeout.time )
					break;
				if ( iter->timeout.time == timeoutEvent->timeout.time 
				  && iter->timeout.millitm >= timeoutEvent->timeout.millitm )
					break;
				prev = iter;
				iter = iter->next;
			}
			if ( prev ) prev->next = timeoutEvent;
			else this->firstTimeout = timeoutEvent;
			timeoutEvent->next = iter;
		} else { // remove timeout event
			delete timeoutEvent->cb;
			if ( timeoutEvent->dataLen )
				free( timeoutEvent->data );
			free( timeoutEvent );
		}
	}
}