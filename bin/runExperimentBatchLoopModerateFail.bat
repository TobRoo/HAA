@echo off
@set /a "loopCount = 200"
@set /a "A = 1"
:startLoop
@ECHO Current run number is: %A%
@start "hostLocal1" %~dp0\Autonomic3.exe hostCfgs\Experiment3RCISLModerateFail\hostLocal1.cfg %A% %A% data\missions\missionRCISLExperiment3ModerateFailure.ini
@start "hostLocal2" %~dp0\Autonomic3.exe hostCfgs\Experiment3RCISLModerateFail\hostLocal2.cfg %A% %A%
@start "hostLocal3" %~dp0\Autonomic3.exe hostCfgs\Experiment3RCISLModerateFail\hostLocal3.cfg %A% %A%
@start "hostLocal4" %~dp0\Autonomic3.exe hostCfgs\Experiment3RCISLModerateFail\hostLocal4.cfg %A% %A%
@start "hostExclusive" %~dp0\Autonomic3.exe hostCfgs\Experiment3RCISLModerateFail\hostExclusive.cfg %A% %A%
timeout /t 10 /nobreak > nul
goto RUNNING
:WATCHDOG
set number=0
for /f "skip=3" %%x in ('tasklist /FI "IMAGENAME eq Autonomic3.exe"') do set /a number=number+1
echo Total Autonomic3.exe tasks running: %number%
if %number% geq 3 (
goto RUNNING
)
echo Not running...
if %number% == 0 (
echo No hosts running, starting a new run...
goto NEWRUN
)

echo 1 or 2 hosts running, sleeping and checking again...
timeout /t 20 /nobreak > nul
set number=0
for /f "skip=3" %%x in ('tasklist /FI "IMAGENAME eq Autonomic3.exe"') do set /a number=number+1
echo Total Autonomic3.exe tasks running: %number%

if %number% == 0 (
echo No hosts running, starting a new run...
goto NEWRUN
)
goto CRASHED


:RUNNING
echo At least one host is running, now searching for crashed ones...

tasklist /nh /fi "imagename eq Autonomic3.exe" /fi "STATUS eq UNKNOWN" | find /i "Autonomic3.exe" >nul && (
goto CRASHED
)
tasklist /nh /fi "imagename eq Autonomic3.exe" /fi "STATUS eq NOT RESPONDING" | find /i "Autonomic3.exe" >nul && (
goto CRASHED
)
echo No crashed hosts, sleeping for 10 seconds...
timeout /t 10 /nobreak > nul
goto WATCHDOG
:CRASHED
echo At least one host has crashed unexpectedly, restarting run...
echo Killing all hosts...
taskkill /F /IM Autonomic3.exe
goto REPEATRUN
:NEWRUN
echo Newrun
@if exist "failedRun.tmp" (
@	rem file exists, this run failed -> delete failedRun.tmp and decrement loop variable, redoing the run
@	del "failedRun.tmp"
@	ECHO Failed run, restarting...
	timeout /t 10 /nobreak > nul
	goto startLoop
) 


@	set /a "A = A + 1"
    timeout /t 10 /nobreak > nul
@	if %A% leq %loopCount% (
	GOTO startLoop
)

goto FIN
:REPEATRUN
echo Repeatrun
timeout /t 10 /nobreak > nul
GOTO startLoop
goto FIN
:FIN
echo Simulation complete, %loopCount% runs performed
pause
rem tasklist /nh /fi "imagename eq Autonomic3.exe" | find /i "Autonomic3.exe" >nul && (