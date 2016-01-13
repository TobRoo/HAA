
#include "stdafx.h"

#include <sys/types.h>
#include <sys/timeb.h>

#include "RandomGenerator.h"


//*****************************************************************************
// RandomGenerator

//-----------------------------------------------------------------------------
// Constructor	

RandomGenerator::RandomGenerator() {

	// boost
	boost_firstRun = 1;
	boost_engine = new boost::lagged_fibonacci607;
	boost_nd = new boost::normal_distribution<double>(0,1);
	boost_gen_nd = new boost::variate_generator<boost::lagged_fibonacci607&,boost::normal_distribution<double>>(*randEngine_lf607(),*boost_nd);
	boost_uni_int = new boost::uniform_int<int>(0,999999);
	boost_gen_ui = new boost::variate_generator<boost::lagged_fibonacci607&,boost::uniform_int<int>>(*randEngine_lf607(),*boost_uni_int);

}

//-----------------------------------------------------------------------------
// Destructor

RandomGenerator::~RandomGenerator() {

	// boost
	delete boost_engine;
	delete boost_nd;
	delete boost_gen_nd;
	delete boost_uni_int;
	delete boost_gen_ui;

}

//-----------------------------------------------------------------------------
// Random functions

boost::lagged_fibonacci607 * RandomGenerator::randEngine_lf607() {
	
	if ( boost_firstRun ) {
		_timeb seedt;
		_ftime_s( &seedt );
		boost_engine->seed( (unsigned int) (seedt.time*1000 + seedt.millitm) );
	
		boost_firstRun = 0;
	}

	return boost_engine;
}

double RandomGenerator::NormalDistribution( double mean, double sigma ) {
	return mean + sigma * (*boost_gen_nd)();
}

double RandomGenerator::Uniform01() {
	return (*boost_gen_ui)()*0.000001;
}