@echo off
rem Runs one named CTest tier; no hand-quoted label regexes.
rem Usage: tools\run\ctest_tier.cmd <fast|phaseclose> [preset=dev]
setlocal
set "TIER=%~1"
set "PRESET=%~2"
if "%PRESET%"=="" set "PRESET=dev"
if /i "%TIER%"=="fast" goto ok
if /i "%TIER%"=="phaseclose" goto ok
echo ctest_tier: unknown tier "%TIER%" ^(fast, phaseclose^) & exit /b 2
:ok
call "%~dp0..\build\msvc_env.cmd" ctest --test-dir "%~dp0..\..\build\%PRESET%" -L %TIER% --output-on-failure --timeout 300
exit /b %ERRORLEVEL%
