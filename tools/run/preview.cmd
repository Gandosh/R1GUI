@echo off
rem Builds (dev preset) and launches the interactive preview. Usage: tools\run\preview.cmd
call "%~dp0..\build\build.cmd" dev || exit /b %ERRORLEVEL%
start "" "%~dp0..\..\build\dev\bin\r1gui-preview.exe"
