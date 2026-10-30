@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture-explorer-ui-stacks.ps1" %*
pause
