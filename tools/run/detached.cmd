@echo off
rem Starts a long job detached (survives the shell tool's 10 minute background limit).
rem Usage: tools\run\detached.cmd <name> <command...>
rem Writes build\run\<name>.log, <name>.exit (when finished) and <name>.done (marker).
rem Check with tools\run\run_status.cmd <name>. Never poll in a loop.
setlocal
set "NAME=%~1"
if "%NAME%"=="" ( echo usage: detached.cmd ^<name^> ^<command...^> & exit /b 2 )
set "DIR=%~dp0..\..\build\run"
if not exist "%DIR%" mkdir "%DIR%"
del "%DIR%\%NAME%.exit" "%DIR%\%NAME%.done" >nul 2>&1
shift
set "CMDLINE="
:collect
if "%~1"=="" goto go
set "CMDLINE=%CMDLINE% %1"
shift
goto collect
:go
start "" /b cmd /v:on /c "(%CMDLINE%) > "%DIR%\%NAME%.log" 2>&1 & echo !ERRORLEVEL! > "%DIR%\%NAME%.exit" & echo done > "%DIR%\%NAME%.done""
echo started %NAME%
exit /b 0
