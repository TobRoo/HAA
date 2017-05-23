
@FOR /L %%A IN (1,1,300) DO (
@ECHO Current run number is: %%A
@start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment2HighFailure.ini
@start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal2.cfg %%A %%A
@start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal3.cfg %%A %%A
@start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostLocal4.cfg %%A %%A
@start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment2RCISLHighFail\hostExclusive.cfg %%A %%A
@if exist "failedRun.tmp" (
@	rem file exists, this run failed -> delete failedRun.tmp and decrement loop variable, redoing the run
@	del "failedRun.tmp"
@	set /a A-=1
@	ECHO Failed run, restarting...
)
)