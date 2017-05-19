
FOR /L %%A IN (2,1,300) DO (


start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment1.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal2.cfg %%A %%A
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal3.cfg %%A %%A
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal4.cfg %%A %%A
::start "hostLocal5" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal5.cfg %%A %%A
::start "hostLocal6" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal6.cfg %%A %%A
::start "hostLocal7" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal7.cfg %%A %%A
::start "hostLocal8" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostLocal8.cfg %%A %%A
start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment1RCISLNoFail\hostExclusive.cfg %%A %%A

ECHO Current run number is: %%A
)