# make_voice.ps1 - turn NORA's lines into MP3s for her SD card (Windows
# text-to-speech + ffmpeg), talking you through it. Run it with make_voice.bat.
#
#   make_voice.bat            make voice\track200.mp3 ... in the repo
#   make_voice.bat E:         same, then copy them onto the SD card in E:
#   make_voice.bat -Voice "Microsoft Hazel Desktop"   pick another installed voice
#
# The UNO (scripts/arduino/arduino.ino) plays track(200+n).mp3 for line n.
# Keep the numbers below in step with the VOICE_* defines there; new lines
# can go on the end (up to 31) and be played with SAY:<n> from the ESP32.
param(
    [string]$Drive = "",
    [string]$Voice = "Microsoft Zira Desktop",
    [int]$Rate = 0
)

$Lines = [ordered]@{
    0  = "Manual mode."
    1  = "Auto mode."
    2  = "Line follow mode."
    3  = "Remote mode."
    4  = "Motors locked."
    5  = "Motors unlocked."
    6  = "Something's in the way."
    7  = "U V off."
    8  = "U V on."
    9  = "U V blinking."
    10 = "Hello, I'm NORA."
}

$ErrorActionPreference = "Stop"
$Repo = Split-Path $PSScriptRoot -Parent
$Out  = Join-Path $Repo "voice"

Add-Type -AssemblyName System.Speech
$guide = New-Object System.Speech.Synthesis.SpeechSynthesizer
function Say([string]$text) { Write-Host ">> $text" -ForegroundColor Cyan; $guide.Speak($text) }

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) {
    $ffmpeg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\*FFmpeg*\*\bin\ffmpeg.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ffmpeg) { Say "I can't find f f m peg. Install it with winget install ffmpeg."; exit 1 }

$tts = New-Object System.Speech.Synthesis.SpeechSynthesizer
try { $tts.SelectVoice($Voice) } catch {
    Say "The voice $Voice isn't installed. Using the default one."
    Write-Host "Installed: $(($tts.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name }) -join ', ')"
}
$tts.Rate = $Rate

New-Item -ItemType Directory -Force $Out | Out-Null
Say "Making $($Lines.Count) voice lines for NORA."
$wav = Join-Path $env:TEMP "nora_voice.wav"
foreach ($n in $Lines.Keys) {
    $mp3 = Join-Path $Out ("track{0:D3}.mp3" -f (200 + $n))
    $tts.SetOutputToWaveFile($wav)
    $tts.Speak($Lines[$n])
    $tts.SetOutputToNull()
    # mono 44.1 kHz, the VS1053 on the MP3 shield plays it without trouble
    & $ffmpeg -y -loglevel error -i $wav -ac 1 -ar 44100 -b:a 96k $mp3
    if ($LASTEXITCODE -ne 0) { Say "f f m peg failed on line $n."; exit 1 }
    Write-Host ("  {0}  {1}" -f (Split-Path $mp3 -Leaf), $Lines[$n])
}
Remove-Item $wav -ErrorAction SilentlyContinue
Say "Done. They're in the voice folder."

if ($Drive) {
    $dest = $Drive.TrimEnd('\', ':') + ":\"
    if (-not (Test-Path $dest)) { Say "I can't see a card in drive $Drive."; exit 1 }
    Copy-Item (Join-Path $Out "track2*.mp3") $dest -Force
    Say "Copied them to the S D card. Put it back in NORA's M P 3 shield."
} else {
    Write-Host "Copy voice\track2*.mp3 to the root of NORA's SD card (next to track000.mp3)."
}
