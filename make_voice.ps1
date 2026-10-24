# make_voice.ps1 - NORA's voice lines as MP3s for her SD card (the work is in scripts\make_voice.ps1).
# Buddies: make_voice.bat, make_voice.sh.
#   .\make_voice.ps1 [E:] [-Voice "Microsoft Hazel Desktop"]
& "$PSScriptRoot\scripts\make_voice.ps1" @args
exit $LASTEXITCODE
