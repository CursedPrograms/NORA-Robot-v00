@echo off
REM Make NORA's voice lines as MP3s for her SD card, talking you through it.
REM   make_voice.bat [E:] [-Voice "Microsoft Hazel Desktop"]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\make_voice.ps1" %*
if errorlevel 1 pause
