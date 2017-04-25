
FOR /L %%A IN (6,1,300) DO (


start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment1.ini
start "hostLocal2" %~dp0\Autonomic.exe hostCfgs\hostLocal2.cfg %%A %%A
start "hostLocal3" %~dp0\Autonomic.exe hostCfgs\hostLocal3.cfg %%A %%A
start "hostLocal4" %~dp0\Autonomic.exe hostCfgs\hostLocal4.cfg %%A %%A
start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\hostExclusive.cfg %%A %%A

ECHO Current run number is: %%A
)