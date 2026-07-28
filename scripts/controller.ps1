# =============================================================
# NORA pygame controller launcher (Windows PowerShell)
#
# Creates a dedicated virtualenv next to this script on first run,
# installs dependencies from ..\requirements.txt, then launches
# controller.py inside it. Re-running just reuses the existing venv.
#
# Usage:
#   .\controller.ps1              connection picker
#   .\controller.ps1 --wifi       skip picker, connect over WiFi
#   .\controller.ps1 --bt COM5
# =============================================================

$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Venv       = Join-Path $ScriptDir ".venv"
$Reqs       = Join-Path $ScriptDir "..\requirements.txt"
$Controller = Join-Path $ScriptDir "controller.py"
$VenvPython = Join-Path $Venv "Scripts\python.exe"

if (-not (Test-Path $VenvPython)) {
    Write-Host "No venv found -- creating one and installing dependencies..."

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher) {
        & py -3 -m venv $Venv
    } else {
        & python -m venv $Venv
    }
    if (-not (Test-Path $VenvPython)) {
        Write-Error "venv creation failed."
        exit 1
    }

    & $VenvPython -m pip install --quiet --upgrade pip
    & $VenvPython -m pip install -r $Reqs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "dependency install failed"
        exit 1
    }
    Write-Host "venv ready."
}

& $VenvPython $Controller @args
