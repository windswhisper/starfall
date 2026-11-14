@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Case "%~1"
exit /b %errorlevel%
