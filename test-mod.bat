@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\test-mod.ps1" %*
set "testResult=%errorlevel%"
echo.
pause
exit /b %testResult%
