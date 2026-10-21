@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play_YunSummerMegatron.ps1" %*
if errorlevel 1 pause
