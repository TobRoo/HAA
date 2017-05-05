
FOR /L %%A IN (17,1,300) DO (


start "hostLocal1" %~dp0\Autonomic.exe hostCfgs\hostLocal1.cfg %%A %%A data\missions\missionRCISLExperiment1.ini
start /wait "hostExclusive" %~dp0\Autonomic.exe hostCfgs\hostExclusive.cfg %%A %%A

ECHO Current run number is: %%A
)