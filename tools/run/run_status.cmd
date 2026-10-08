@echo off
rem Reports the state of a detached job. Usage: tools\run\run_status.cmd <name>
setlocal enabledelayedexpansion
set "DIR=%~dp0..\..\build\run"
set "NAME=%~1"
if not exist "%DIR%\%NAME%.log" echo no such job: %NAME%& exit /b 2
if exist "%DIR%\%NAME%.done" goto finished
echo %NAME%: running
goto tail
:finished
set /p CODE=<"%DIR%\%NAME%.exit"
echo %NAME%: finished, exit code !CODE!
:tail
echo log: %DIR%\%NAME%.log
exit /b 0
