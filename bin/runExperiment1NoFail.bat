start "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostExclusive.cfg 300
start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal1.cfg 300 data\missions\missionRCISLExperiment1.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal2.cfg 300 
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal3.cfg 300 
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal4.cfg 300 