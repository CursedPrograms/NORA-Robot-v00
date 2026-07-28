@echo off
REM =============================================================
REM NORA pygame controller launcher (Windows cmd.exe)
REM
REM Creates a dedicated virtualenv next to this script on first run,
REM installs dependencies from ..\requirements.txt, then launches
REM controller.py inside it. Re-running just reuses the existing venv.
REM
REM Usage:
REM   controller.bat              connection picker
REM   controller.bat --wifi       skip picker, connect over WiFi
REM   controller.bat --bt COM5
REM =============================================================

setlocal
set "SCRIPT_DIR=%~dp0"
set "VENV=%SCRIPT_DIR%.venv"
set "REQS=%SCRIPT_DIR%..\requirements.txt"
set "CONTROLLER=%SCRIPT_DIR%controller.py"
set "VENV_PY=%VENV%\Scripts\python.exe"

if not exist "%VENV_PY%" (
    echo No venv found -- creating one and installing dependencies...
    py -3 -m venv "%VENV%"
    if errorlevel 1 (
        echo venv creation failed.
        exit /b 1
    )
    "%VENV_PY%" -m pip install --quiet --upgrade pip
    "%VENV_PY%" -m pip install -r "%REQS%"
    if errorlevel 1 (
        echo dependency install failed.
        exit /b 1
    )
    echo venv ready.
)

"%VENV_PY%" "%CONTROLLER%" %*
