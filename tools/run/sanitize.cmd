@echo off
rem Sanitizer runs go through here: timeout plus whole-process-tree kill (placeholder for Phase 0;
rem the ASan preset is added with the first code that needs it, recorded in the daily log).
echo sanitize: not configured yet ^(no ASan preset in Phase 0^)
exit /b 2
