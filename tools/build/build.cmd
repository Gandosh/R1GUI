@echo off
rem Configures and builds one preset under a per-tree lock.
rem Usage: tools\build\build.cmd [preset=dev]
rem Owns: the per-tree build lock. The block runs with build\<preset>.lock open as handle 9;
rem a second build in the same tree cannot open it and stops with a sharing error instead of
rem corrupting the tree.
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=dev"
pushd "%~dp0..\.." || exit /b 2
if not exist build mkdir build
( call :run ) 9>"build\%PRESET%.lock"
set "BUILD_STATUS=%ERRORLEVEL%"
del "build\%PRESET%.lock" >nul 2>&1
popd
exit /b %BUILD_STATUS%

:run
call "%~dp0msvc_env.cmd" cmake --preset %PRESET% || exit /b 1
call "%~dp0msvc_env.cmd" cmake --build --preset %PRESET%
exit /b %ERRORLEVEL%
