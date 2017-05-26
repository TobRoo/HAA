@echo off
@set /a "loopCount = 200"
@set /a "A = 1"
:startLoop
@ECHO Current run number is: %A%
@start "hostLocal1" %~dp0\Autonomic1.exe hostCfgs\Experiment1RCISLNoFail\hostLocal1.cfg %A% %A% data\missions\missionRCISLExperiment1.ini
@start "hostLocal2" %~dp0\Autonomic1.exe hostCfgs\Experiment1RCISLNoFail\hostLocal2.cfg %A% %A%
@start "hostLocal3" %~dp0\Autonomic1.exe hostCfgs\Experiment1RCISLNoFail\hostLocal3.cfg %A% %A%
@start "hostLocal4" %~dp0\Autonomic1.exe hostCfgs\Experiment1RCISLNoFail\hostLocal4.cfg %A% %A%
@start "hostExclusive" %~dp0\Autonomic1.exe hostCfgs\Experiment1RCISLNoFail\hostExclusive.cfg %A% %A%
goto RUNNING
:WATCHDOG

tasklist /nh /fi "imagename eq autonomic1.exe" | find /i "autonomic1.exe" >nul && (
goto RUNNING
)
echo No hosts running, starting a new run...
goto NEWRUN
:RUNNING
echo At least one host is running, now searching for crashed ones...

tasklist /nh /fi "imagename eq autonomic1.exe" /fi "STATUS eq UNKNOWN" | find /i "autonomic1.exe" >nul && (
goto CRASHED
)
tasklist /nh /fi "imagename eq autonomic1.exe" /fi "STATUS eq NOT RESPONDING" | find /i "autonomic1.exe" >nul && (
goto CRASHED
)
echo No crashed hosts, sleeping for 10 seconds...
timeout /t 10 /nobreak > nul
goto WATCHDOG
:CRASHED
echo At least one host has crashed unexpectedly, restarting run...
echo Killing all hosts...
taskkill /F /IM autonomic1.exe
goto REPEATRUN
:NEWRUN
echo Newrun
@if exist "failedRun.tmp" (
@	rem file exists, this run failed -> delete failedRun.tmp and decrement loop variable, redoing the run
@	del "failedRun.tmp"
@	ECHO Failed run, restarting...
) ELSE (
@	set /a "A = A + 1"
)
@	TIMEOUT /t 10 /nobreak > nul
@if %A% leq %loopCount% (
	GOTO :startLoop
)

goto FIN
:REPEATRUN
echo Repeatrun
timeout /t 10 /nobreak > nul
GOTO :startLoop
goto FIN
:FIN
echo Simulation complete, %loopCount% runs performed