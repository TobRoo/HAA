start "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostExclusive.cfg 300 1
start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal1.cfg 300 1 data\missions\missionRCISLExperiment2HighFailure.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal2.cfg 300 1
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal3.cfg 300 1
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal4.cfg 300 1