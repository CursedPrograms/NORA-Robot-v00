@echo off
REM run.bat - NORA launcher: her web dashboard or the pygame controller (run.ps1 does the work).
REM   run.bat [web^|app^|emulate]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
if errorlevel 1 pause
