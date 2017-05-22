
FOR /L %%A IN (1,1,300) DO (


start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\Experiment3RCISLExtremeFail\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment3ExtremeFailure.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\Experiment3RCISLExtremeFail\hostLocal2.cfg %%A %%A
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\Experiment3RCISLExtremeFail\hostLocal3.cfg %%A %%A
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\Experiment3RCISLExtremeFail\hostLocal4.cfg %%A %%A
start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\Experiment3RCISLExtremeFail\hostExclusive.cfg %%A %%A

ECHO Current run number is: %%A
)