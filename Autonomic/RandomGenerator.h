

#include <boost/random/uniform_int.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/lagged_fibonacci.hpp>
#include <boost/random/variate_generator.hpp>

class RandomGenerator {

public:
	RandomGenerator();
	~RandomGenerator();

	double NormalDistribution( double mean, double sigma );
	double Uniform01();

protected: 
	// boost
	int boost_firstRun;
	boost::lagged_fibonacci607 *boost_engine;
	boost::normal_distribution<double> *boost_nd;
	boost::variate_generator<boost::lagged_fibonacci607&,boost::normal_distribution<double>> *boost_gen_nd;
	boost::uniform_int<int> *boost_uni_int;
	boost::variate_generator<boost::lagged_fibonacci607&,boost::uniform_int<int>> *boost_gen_ui;
	boost::lagged_fibonacci607 * randEngine_lf607();

};