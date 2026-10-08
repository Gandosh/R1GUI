@echo off
rem Local replacement for hosted CI: configures a fresh tree, builds, and runs the fast test tier.
rem Usage: tools\run\clean_check.cmd
rem Runs the MSVC build from scratch in build\clean (deleted first), then the same with clang-cl
rem in build\clean-clang when clang-cl is installed; otherwise reports the clang pass as SKIPPED.
rem Exit code 0 only if every pass that ran succeeded. Run before pushing and at phase close.
setlocal
pushd "%~dp0..\.." || exit /b 2
set "CHECK_STATUS=0"

call :pass clean dev || set "CHECK_STATUS=1"

call "%~dp0..\build\msvc_env.cmd" where clang-cl >nul 2>nul
if errorlevel 1 (
  echo clean_check: clang-cl pass SKIPPED ^(clang-cl not installed^)
) else (
  call :pass clean-clang clang || set "CHECK_STATUS=1"
)

if "%CHECK_STATUS%"=="0" ( echo clean_check: PASS ) else ( echo clean_check: FAIL )
popd
exit /b %CHECK_STATUS%

:pass
rem %1 = build dir name, %2 = base preset (its settings are reused with a fresh binary dir)
echo ==== clean_check: %2 in build\%1 ====
if exist "build\%1" rmdir /s /q "build\%1"
call "%~dp0..\build\msvc_env.cmd" cmake --preset %2 -B "build\%1" || exit /b 1
call "%~dp0..\build\msvc_env.cmd" cmake --build "build\%1" || exit /b 1
call "%~dp0..\build\msvc_env.cmd" ctest --test-dir "build\%1" -L fast --output-on-failure --timeout 300 || exit /b 1
exit /b 0
