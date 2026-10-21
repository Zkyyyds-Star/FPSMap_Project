@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play_CFMBioPyramid.ps1" %*
if errorlevel 1 pause
