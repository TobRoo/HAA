// SimSLAM.cpp
//

#include "stdafx.h"
#include "math.h"

#include "SimAgent.h"
#include "SimSLAM.h"

#include "SimSensorSonar.h"
#include "SimSensorLandmark.h"
#include "SimCooccupancy.h"

#include "..\\autonomic\\RandomGenerator.h"


//*****************************************************************************
// SimSLAM

//-----------------------------------------------------------------------------
// Constructor	
SimSLAM::SimSLAM( Simulation *sim, Visualize *vis, RandomGenerator *randGen ) : SimAgentBase( sim, vis, randGen ) {

	this->processingOrder = PO_LIFO;
	this->processingPriority = PP_LANDMARK;

	this->readingsProcessed = 0;

}

//-----------------------------------------------------------------------------
// Destructor
SimSLAM::~SimSLAM() {

	if ( this->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int SimSLAM::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		time( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		sprintf_s( logName, "log SimSLAM %s.txt", timeBuf );

		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "SimSLAM" );
	}

	return SimAgentBase::configure();
}

//-----------------------------------------------------------------------------
// Start

int SimSLAM::start() {
	int t;

	if ( !this->configured )
		return 1; // not configured

	if ( SimAgentBase::start() )
		return 1;

	this->started = false;

	for ( t=0; t<SimSLAM_MAX_THREADS; t++ ) 
		this->nextProcess[t] = this->simTime;

	this->sensorSonar = new SimSensorSonar( sim, vis, randGen );
	if ( !this->sensorSonar->configure() ) {
		this->sensorSonar->setMap( &this->mapId );
		this->sensorSonar->start();
	}
	this->sensorLandmark = new SimSensorLandmark( sim, vis, randGen );
	if ( !this->sensorLandmark->configure() ) {
		this->sensorLandmark->start();
	}
	this->cooccupancy = new SimCooccupancy( sim, vis, randGen );
	if ( !this->cooccupancy->configure() ) {
		this->cooccupancy->setMap( &this->mapId );
		this->cooccupancy->start();
	}

	this->activeThreads = SimSLAM_MAX_THREADS;
	
	this->started = true;

	return 0;
}

int SimSLAM::setMap( UUID *id ) {

	this->mapId = *id;

	return 0;
}

int SimSLAM::setProcessingOpts( int order, int priority ) {
	this->processingOrder = order;
	this->processingPriority = priority;

	return 0;
}

int SimSLAM::addThreadChange( _timeb *tb, int threads ) {

	this->threadTQueue.push_back( *tb );
	this->threadNQueue.push_back( threads );

	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int SimSLAM::stop() {

	delete this->sensorSonar;
	delete this->sensorLandmark;
	delete this->cooccupancy;

	return SimAgentBase::stop();
}

//-----------------------------------------------------------------------------
// Step

int SimSLAM::preStep( int dt ) {

	// update activeThreads if necessary
	_timeb *threadT;
	while ( this->threadTQueue.size() ) {
		threadT = &this->threadTQueue.front();
		
		if ( threadT->time < this->simTime.time
		  || (threadT->time == this->simTime.time && threadT->millitm <= this->simTime.millitm ) ) {
			this->activeThreads = min( this->threadNQueue.front(), SimSLAM_MAX_THREADS );
			this->threadTQueue.pop_front();
			this->threadNQueue.pop_front();
		} else {
			break;
		}
	}

	if ( SimAgentBase::preStep( dt ) )
		return 1;

	this->sensorLandmark->preStep( dt );
	this->sensorSonar->preStep( dt );
	this->cooccupancy->preStep( dt );

	return 0;
}


int SimSLAM::step() {
	int q;
	_timeb minTime;
	int t, tMin;
	READING_INFO reading;
	int processingCost;

	if ( SimAgentBase::step() ) 
		return 1;

	// TEMP
	vis->consolePrintLn( NULL, "Reading Queues: %d %d", this->readingQueue[0].size(), this->readingQueue[1].size() );

	// process
	while ( 1 ) {
		minTime.time = this->simTime.time + 9999; // set high
		for ( t=0; t<this->activeThreads; t++ ) {
			if ( this->nextProcess[t].time < minTime.time 
			  || ( this->nextProcess[t].time == minTime.time && this->nextProcess[t].millitm <= minTime.millitm ) ) {
				minTime = this->nextProcess[t];
				tMin = t;
			}
		}

		if ( minTime.time > this->simTime.time 
		  || ( minTime.time == this->simTime.time && minTime.millitm > this->simTime.millitm ) )
			break;
		
		for ( q=0; q<SimSLAM_QUEUE_COUNT; q++ ) {
			if ( this->readingQueue[q].size() )
				break;
		}
		if ( q != SimSLAM_QUEUE_COUNT ) {
			if ( this->processingOrder == PO_LIFO 
			  || this->processingOrder == PO_LIFO_PURGE ) {
				reading = this->readingQueue[q].front();
				this->readingQueue[q].pop_front();
			} else { // FIFO or FIFO_DELAY
				reading = this->readingQueue[q].back();
				this->readingQueue[q].pop_back();
			}
			
			switch ( reading.type ) {
			case DDB_SENSOR_SONAR:
				this->sensorSonar->addReading( &reading.sensor, &reading.time );
				processingCost = SimSLAM_PROCESSING_COST_SONAR;
				break;
			case DDB_SENSOR_CAMERA:
				this->sensorLandmark->addReading( &reading.sensor, &reading.time );
				processingCost = SimSLAM_PROCESSING_COST_LANDMARK;
				break;
			case DDB_PARTICLEFILTER:
				this->cooccupancy->addReading( &reading.sensor, &reading.time );
				processingCost = SimSLAM_PROCESSING_COST_COOCCUPANCY;
				break;
			default:
				processingCost = 0;
				break;
			};

			this->readingsProcessed++;

			this->nextProcess[tMin] = this->simTime;
			this->nextProcess[tMin].time += (this->nextProcess[tMin].millitm + processingCost) / 1000;
			this->nextProcess[tMin].millitm = (this->nextProcess[tMin].millitm + processingCost) % 1000;
			
		} else {
			break;
		}
	}

	if ( this->processingOrder == PO_LIFO_PURGE ) { // throw out any remaining readings
		for ( q=0; q<SimSLAM_QUEUE_COUNT; q++ ) {
			this->readingQueue[q].clear();
		}
	}
	
	// log thread usage
	if ( this->simTime.millitm == 0 || this->simTime.millitm == 500 ) {
		int usedThreads = 0;
		for ( t=0; t<this->activeThreads; t++ ) {
			if ( this->nextProcess[t].time > this->simTime.time 
			  || ( this->nextProcess[t].time == this->simTime.time && this->nextProcess[t].millitm > this->simTime.millitm ) ) {
				usedThreads++;
			}
		}

		Log.log(LOG_LEVEL_NORMAL, "threads: %04d.%03d used %d active %d (total processed %d)", (int)this->simTime.time, (int)this->simTime.millitm, usedThreads, activeThreads, readingsProcessed );
	}

	this->sensorLandmark->step();
	this->sensorSonar->step();	
	this->cooccupancy->step();
	
	return 0;
}


//-----------------------------------------------------------------------------
// Processing

int SimSLAM::addReading( int type, UUID *sensor, _timeb *tb ) {
	READING_INFO reading;
	std::list<READING_INFO>::iterator it;
	int q;
	
	// prepare reading
	reading.type = type;
	reading.sensor = *sensor;
	reading.time = *tb;
	
	// scramble sort time slightly to get a random order on adjacent readings
	int dt = (int)(50 * (0.5 - randGen->Uniform01()));
	if ( dt < 0 ) {
		reading.sortTime.time = tb->time + (tb->millitm + dt >= 0 ? 0 : -1 - (tb->millitm + dt)/1000);
		reading.sortTime.millitm = tb->millitm - ((-dt) % 1000); 
	} else {
		reading.sortTime.time = tb->time + (tb->millitm + dt)/1000;
		reading.sortTime.millitm = (tb->millitm + dt) % 1000;
	}

	if ( this->processingPriority == PP_NONE ) {
		  q = 0;
	} else if ( this->processingPriority == PP_LANDMARK ) {
		if ( type == DDB_SENSOR_CAMERA ) // prioritize camera readings
			q = 0;
		else 
			q = 1;
	}

	// insert into processing queue q
	for ( it = this->readingQueue[q].begin(); it != this->readingQueue[q].end(); it++ ) {
		if ( it->sortTime.time < reading.sortTime.time 
		  || ( it->sortTime.time == reading.sortTime.time && it->sortTime.millitm < reading.sortTime.millitm ) )
			break;
	}
	if ( it == this->readingQueue[q].end() ) {
		this->readingQueue[q].push_back( reading );
	} else {
		this->readingQueue[q].insert( it, reading );
	}

	return 0;
}

int SimSLAM::getReadingQueueSize() {
	int count = 0;
	int q;

	for ( q=0; q<SimSLAM_QUEUE_COUNT; q++ ) {
		count += (int)this->readingQueue[q].size();
	}

	return count;
}

//-----------------------------------------------------------------------------
// Callbacks

