@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\launchers\launch.ps1" -Mode Game %*
if errorlevel 1 pause
