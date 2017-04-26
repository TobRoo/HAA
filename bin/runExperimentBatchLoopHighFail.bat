
FOR /L %%A IN (4,1,300) DO (


start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment2HighFailure.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal2.cfg %%A %%A
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal3.cfg %%A %%A
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal4.cfg %%A %%A
start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostExclusive.cfg %%A %%A

ECHO Current run number is: %%A
)