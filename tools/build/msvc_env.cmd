@echo off
rem Runs the given command inside the MSVC x64 environment (and the Vulkan SDK env if present).
rem Usage: tools\build\msvc_env.cmd <command...>
rem Owns: locating Visual Studio via vswhere so no path is hard-coded. Applies the optional,
rem machine-local tools\build\env.local.cmd (gitignored) for VULKAN_SDK or GPU selection.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo msvc_env: vswhere not found& exit /b 2
rem Short (8.3) path avoids the parentheses in "Program Files (x86)" breaking the for loop.
for %%I in ("%VSWHERE%") do set "VSWHERE=%%~sI"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT echo msvc_env: no Visual Studio with C++ tools found& exit /b 2
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 2
if exist "%~dp0env.local.cmd" call "%~dp0env.local.cmd"
%*
exit /b %ERRORLEVEL%
