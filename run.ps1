# run.ps1 - NORA launcher: her web dashboard or the pygame controller. Buddies: run.bat, run.sh.
#   .\run.ps1          a menu
#   .\run.ps1 web      her web dashboard in the browser
#   .\run.ps1 app      the controller (scripts\controller.ps1, which owns its venv); extra args go to it
param([string]$Pick = "")
$url = "http://192.168.4.1:5002"
if (-not $Pick) {
    Write-Host "==============================="
    Write-Host "   NORA - pick a controller"
    Write-Host "==============================="
    Write-Host "  1) Website  (browser dashboard)"
    Write-Host "  2) App      (pygame controller)"
    Write-Host "  3) App, emulated (no robot)"
    Write-Host "  q) quit"
    $Pick = Read-Host ">"
}
switch ($Pick) {
    { $_ -in "1", "web" } { Write-Host "Opening $url (this PC must be on her WiFi)"; Start-Process $url }
    { $_ -in "2", "app" } { & "$PSScriptRoot\scripts\controller.ps1" @args }
    { $_ -in "3", "emulate" } { & "$PSScriptRoot\scripts\controller.ps1" --emulate }
    default { Write-Host "bye" }
}
