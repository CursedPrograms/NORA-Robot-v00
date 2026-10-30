<p align="center"><img src="images/avatar_round.png" width="160" alt="NORA"></p>

<h1 align="center">NORA</h1>
<h3 align="center">Nomadic Omnidirectional Reactive Automaton</h3>
<p align="center"><i>A DREAM Robotics Agent</i></p>

<div align="center">

[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

</div>

<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" width="180"/>
  </a>
</div>

<div align="center">
  <img alt="C++" src="https://img.shields.io/badge/c++%20-%23323330.svg?&style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
  <img alt="Arduino" src="https://img.shields.io/badge/-Arduino-323330?style=for-the-badge&logo=arduino&logoColor=white"/>
  <img alt="ESP32" src="https://img.shields.io/badge/ESP32-%23323330.svg?&style=for-the-badge&logo=espressif&logoColor=white"/>
  <img alt="Git" src="https://img.shields.io/badge/git%20-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
</div>

---

- Robot Type: Mecanum

<div align="center">
  <img src="images/nora_avatar.jpg" alt="NORA avatar: a human representation of the robot" width="320"/>
  <p><i>NORA</i></p>
</div>

---

### Software
- [Arduino IDE](https://docs.arduino.cc/software/ide/)

---

---

<div align="center">
  <img src="images/NORA1.jpg" alt="NORA Robot" width="400"/>
</div>

---

<div align="center">
  <img src="images/screenshots/controller.gif" alt="NORA's Python controller, driving a pretend NORA" width="420"/>
</div>

**Windows controller:** [`builds/NORA-Controller.exe`](builds/NORA-Controller.exe) shows NORA's own web page in a window, so it always matches the page. Source: `scripts/web_controller`.

---

**STAY CLOSE:** press STAY on [RITA](https://github.com/CursedPrograms/RITA), or STAY CLOSE on her page, and she keeps near RITA in your hand, stopping about a metre away. RITA's signal shows how far, not which way, so she tries a direction, keeps going if the signal gets stronger and turns if it doesn't. Her sensors still stop her for walls and the wheel lock still holds. She stops if RITA goes quiet. Tested in simulation only so far.

**Other languages:** `scripts/controllers` has this robot's controller in C#, F#, Rust, Go and Julia, all reading one `robot.json`: W A S D to drive, the mode buttons and the sensors. See its README for how to run each.

## Related Projects


<table>
  <tr>
    <td align="center"><a href="https://github.com/CursedPrograms/ARIA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/ARIA-Robot-v00/main/images/avatar_round.png" width="72" alt="ARIA"><br>ARIA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/KIDA-Robot-v01"><img src="https://raw.githubusercontent.com/CursedPrograms/KIDA-Robot-v01/main/images/avatar_round.png" width="72" alt="KIDA-01"><br>KIDA-01</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/KIDA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/KIDA-Robot-v00/main/images/avatar_round.png" width="72" alt="KIDA-00"><br>KIDA-00</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/WHIP-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/WHIP-Robot-v00/main/images/avatar_round.png" width="72" alt="WHIP"><br>WHIP</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/MILA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/MILA-Robot-v00/main/images/avatar_round.png" width="72" alt="MILA"><br>MILA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/NIKA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/NIKA-Robot-v00/main/images/avatar_round.png" width="72" alt="NIKA"><br>NIKA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/IDA-Robot-v00"><img src="https://raw.githubusercontent.com/CursedPrograms/IDA-Robot-v00/main/images/avatar_round.png" width="72" alt="IDA"><br>IDA</a></td>
  </tr>
  <tr>
    <td align="center"><a href="https://github.com/CursedPrograms/ARM-Robot-v01"><img src="https://raw.githubusercontent.com/CursedPrograms/ARM-Robot-v01/main/images/avatar_round.png" width="72" alt="ARM"><br>ARM</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/DREAM"><img src="https://raw.githubusercontent.com/CursedPrograms/DREAM/main/images/avatar_round.png" width="72" alt="DREAM"><br>DREAM</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/RIFT"><img src="https://raw.githubusercontent.com/CursedPrograms/RIFT/main/images/avatar_round.png" width="72" alt="RIFT"><br>RIFT</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/RITA"><img src="https://raw.githubusercontent.com/CursedPrograms/RITA/main/images/avatar_round.png" width="72" alt="RITA"><br>RITA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/NINA"><img src="https://raw.githubusercontent.com/CursedPrograms/NINA/main/images/avatar_round.png" width="72" alt="NINA"><br>NINA</a></td>
    <td align="center"><a href="https://github.com/CursedPrograms/TINA"><img src="https://raw.githubusercontent.com/CursedPrograms/TINA/main/images/avatar_round.png" width="72" alt="TINA"><br>TINA</a></td>
  </tr>
</table>

---

## Overview

<details>
<summary><b>Overview</b></summary>

NORA is built on the **ESP32**. She hosts the `NORA` WiFi access point the rest of the fleet joins, runs the fleet registry, and drives four mecanum wheels. An Arduino UNO with a SparkFun MP3 Player Shield reads her four ultrasonic sensors, light and sound sensors, plays music and drives the buzzer, and streams it all to the ESP32.

### Core Features
- [x] **Android app (`Android App/`, package `com.synthwomb`): FIND gets her address from the fleet list, or type it in. Hold the stick to drive, let go to stop, and her modes, HELLO, DANCE and the wheel lock are buttons. Build it with `gradlew assembleDebug`.**
- [x] **FOLLOW:** she keeps your hand 30 cm from her front sonar, inside her wall stop. On her web page, or **F** on the controller.
- [x] **HELLO and DANCE:** a wiggle and a spin on the spot, in Manual (`/act?do=hello`, `/act?do=dance`; **B** and **C** on the controller). Her wall stop still holds, and any stop ends them.
- [x] **Live view:** her controller draws her from above, wheels turning and walls on all four sides.
- [x] **`/status`:** the same JSON as `/sensors`, with `rev` (her settings in one number) and `moving`. Her address is `nora.local`.
- [x] **Omnidirectional movement:** drive, strafe and turn in place on mecanum wheels.
- [x] **Self-hosted AP:** no router needed; she is the fleet's network and registry (port `5000`).
- [x] **Reactive safety:** four HC-SR04 sensors (front, back, left, right) block drive commands that would hit something, with a buzzer warning.
- [x] **Four drive modes:** Manual (web / Bluetooth), Auto (obstacle avoidance), Line following, IR Remote.
- [x] **Fleet IR remote:** drives IDA, MILA, WHIP, KIDA-01 and NIKA through her IR transmitter, from the web page, the Python controller or Bluetooth. The panel shows the selected robot's avatar.
- [x] **MIND mode:** the LLM on her PC controller drives her, one short move at a time: she looks (her four distances, light, sound), thinks out loud on screen, says what she's doing in her own voice and personality (the fleet's boss), moves, and remembers. **N** on the Python controller, or MIND on her web page (shown only while that controller is connected). Only on an x86 PC with 8 GB+ RAM and a GTX 1050 or better. Her wheel lock and her wall stop hold in MIND too.
- [x] **IDA's mind:** **H** on her Python controller runs IDA's mind over the IR link. IDA can't report anything back, so her mind knows she's blind and moves one short step at a time.
- [x] **Gamepad rumble:** the pad buzzes as she nears a wall in the direction it drives her (from 40 cm, full at 15 cm).
- [x] **UV light:** off, on or blinking.
- [x] **Music:** an MP3 player with play, next, previous, stop, repeat and volume. A single clap starts the music.
- [x] **Voice:** she says her mode, motor lock, UV and "Something's in the way" out loud from MP3s on the SD card (falls back to beeps; never talks over music). Make them with `make_voice.bat`.
- [x] **Environment:** temperature and humidity (AHT10), and ambient light.
- [x] **Motor lock:** the web page needs a password (`1234` by default) to unlock the motors; on the IR remote, **5** locks and unlocks them (one button: the lock is a safety catch, not security).

</details>

---

## Prerequisites

<details>
<summary><b>Prerequisites</b></summary>

### Software
- `flash.bat` does it all for the ESP32: on a fresh PC it installs the Arduino command line tool, the ESP32 board package (3.3.12) and the libraries by itself, then finds the port, compiles and flashes, talking you through it (unplug the serial link to the UNO first). Or the [Arduino IDE](https://docs.arduino.cc/software/ide/) with the ESP32 board package
- ESP32 libraries: `IRremote` 4.x, `Adafruit AHTX0` (`WiFi`, `WebServer`, `Preferences`, `BluetoothSerial` and `Wire` come with the core)
- UNO libraries: [`SFEMP3Shield` and `SdFat`](https://github.com/madsci1016/Sparkfun-MP3-Player-Shield-Arduino-Library)
- For the ESP32, pick a bigger app partition under **Tools → Partition Scheme**: *Huge APP (3MB No OTA/1MB SPIFFS)* or *Minimal SPIFFS (1.9MB APP with OTA)*

### Hardware

| **Component** | **Details** |
|-----------|---------|
| Microcontroller 0 | ESP32 (ACEBOTT QA007 Max controller board) |
| Microcontroller 1 | Arduino UNO + VS1053 MP3 board with line in, microphone and headphone jack (microSD card) |
| Chassis | Omnidirectional (mecanum) robot chassis |
| Motor drivers | 2× L298N |
| Motors | 4× 5 V DC motors |
| Battery | 2S 18650 |
| Distance | 4× HC-SR04 (front, back, left, right) |
| Ground | 3-channel line tracking sensor |
| Environment | AHT10 temperature / humidity (I2C), photoresistor (LDR) |
| Sound | Sound sensor module (clap detection), buzzer, speaker on the MP3 board |
| FM radio (planned) | TEA5767 module, 76-108 MHz, 5 V: I2C at `0x60` on H13, H14 or H15, audio by aux cable into the VS1053 board's LINE IN |
| Light | UV LED |
| Remote | NEC IR receiver + remote, IR transmitter LED |
| Controllers | Web page, Python controller (WiFi or Bluetooth), IR remote |

</details>

---

<div align="center">
  <img src="images/NORA2.jpg" alt="NORA Robot" width="400"/>
</div>

---

## Pinouts

> [!CAUTION]
> **Ground Loop Warning:** All modules must share a common GND. Failure to bridge grounds will cause erratic motor behavior and sensor noise.

<details>
<summary><b>ESP32 wiring</b></summary>

#### L298N-0 (front drive)
| Motor | PWM Pin | Dir 1 | Dir 2 |
| :--- | :--- | :--- | :--- |
| **M0** | `GPIO 5` | `GPIO 16` | `GPIO 17` |
| **M1** | `GPIO 23`| `GPIO 18` | `GPIO 19` |

#### L298N-1 (rear drive)
| Motor | PWM Pin | Dir 1 | Dir 2 |
| :--- | :--- | :--- | :--- |
| **M2** | `GPIO 12` | `GPIO 13` | `GPIO 14` |
| **M3** | `GPIO 27` | `GPIO 26` | `GPIO 25` |

#### Sensors and outputs
| Component | Pin |
| :--- | :--- |
| **Line follower L / M / R** | `GPIO 34` / `GPIO 35` / `GPIO 39` |
| **UV LED** | `GPIO 4` |
| **IR receiver OUT** | `GPIO 32` (power it from **3.3 V**, not 5 V) |
| **IR transmitter LED** | `GPIO 33` (sends the fleet IR link) |
| **AHT10 SDA / SCL** | `GPIO 21` / `GPIO 22` (H13-H15 are this same I2C bus, with 5 V power) |
| **TEA5767 FM radio (planned)** | I2C, `0x60`: H13, H14 or H15 |
| **UNO link** | `RX0` / `TX0` (UART0, 9600 baud) |

</details>

<details>
<summary><b>UNO wiring</b></summary>

#### Ultrasonic sensors
All four TRIG pins are joined to one shared trigger.

| Signal | Pin |
| :--- | :--- |
| **TRIG (all four)** | `A4` |
| **Front ECHO** | `A0` |
| **Right ECHO** | `A1` |
| **Back ECHO** | `A2` |
| **Left ECHO** | `A3` |

#### Other
| Component | Pin |
| :--- | :--- |
| **Photoresistor (LDR)** | `A5` |
| **Sound sensor** | `D5` |
| **Buzzer** | `D10` |
| **ESP32 link** | `D0` (RX) / `D1` (TX), 9600 baud |

The MP3 shield reserves `D2`, `D3`, `D4`, `D6`, `D7`, `D8`, `D9`, `D11`, `D12` and `D13`.

</details>

<details>
<summary><b>ESP32 ↔ UNO serial link</b></summary>

UART at 9600 baud on the UNO's hardware serial (pins 0/1). Like WHIP, unplug the link before uploading to either board.

**UNO → ESP32** (one line per reading):
```
F:23.4,L:10.1,B:45.0,R:8.3,MT:101,MS:1,LT:62,SND:0
```
Distances in cm, `MT` current track, `MS` music state, `LT` light %, `SND` 1 if a sound was just heard.

**ESP32 → UNO:**
```
M:PLAY  M:NEXT  M:PREV  M:STOP  M:REPEAT     music
V:UP  V:DOWN  V:MUTE                          volume
BZ:SPD:<0-100>  BZ:MODE:<0-3>  BZ:UV:<0-2>    buzzer feedback
BZ:LOCK:<0|1>   BZ:DENIED
```

</details>

> [!TIP]
> **Pro-Tip:** Common GND is non-negotiable. If the motors behave erratically or the sensors give "0" readings, check your ground bridge first!

---

## Controls

<details>
<summary><b>Connectivity & Controls</b></summary>

### Network Configuration
| Parameter | Value |
| :--- | :--- |
| **SSID** | `NORA` |
| **Password** | `12345678` |
| **Control page** | `http://192.168.4.1:5002` |
| **Fleet registry** | `http://192.168.4.1:5000` (`/register`, `/robots`, `/ping`) |
| **Bluetooth** | Device name `NORA` (serial) |

### RIFT Integration
Connect to the `NORA` network, then reach NORA through [RIFT](https://github.com/CursedPrograms/RIFT) at `http://192.168.4.1:5002`. When a RIFT hub is running it takes over as the fleet authority, and NORA defers to it.

### Drive Modes
| Key | Mode | Description |
| :--- | :--- | :--- |
| `1` | **Manual** | D-pad control from the web page, the Python controller or Bluetooth |
| `2` | **Auto** | Omnidirectional obstacle avoidance using all four ultrasonic sensors |
| `3` | **Line** | Line following with the 3-channel sensor. Stops for anything closer than 15 cm in front |
| `4` | **IR Remote** | The remote's D-pad drives directly. `Enter` swaps left/right between strafing and turning |

Only NORA's own remote (NEC, address `0x00`) is obeyed: IR noise that decodes as some other protocol can't switch her mode any more. Every mode change records who made it (`web`, `bt` or `ir`) as `msrc` in `/sensors` and in the Bluetooth `?` reply.

The mode keys, music, volume and mute work in every mode. The D-pad and Enter only drive in IR Remote mode, so they never fight the web driver or the autonomous logic. There's no key-up over IR, so a 250 ms gap with no repeat counts as "released". The full button map is in [`ir_mapping.txt`](ir_mapping.txt).

### Controls
Every key, remote button and Bluetooth letter. The same list opens in its own window: **F1** in the Python controller, **KEYS** on her web page (`/keys`), or `python scripts/keymap.py`.

<!-- keymap:start -->
<!-- generated by keymap.py --readme; edit the list there -->

**Keyboard: controller and web page**

| Key | Does |
|---|---|
| W / Up | forward (hold) |
| S / Down | back (hold) |
| A / Left | strafe left (hold) |
| D / Right | strafe right (hold) |
| Q / E | turn left / right (hold) |
| Two at once | blend (controller): W+A forward and sliding left, W+Q a curve (her /drive) |
| | *driving with the wheels unlocked; in Auto / Line / IR the controller takes over (Manual)* |
| 1 / 2 / 3 / 4 | Manual / Auto / Line / IR-remote mode |
| F | FOLLOW mode: she keeps your hand 30 cm from her front sonar (controller; a button on her web page) |
| B / C | HELLO / DANCE, on the spot, in Manual (controller; buttons on her web page) |
| Space | play / pause music |
| M | next track |
| U | UV light: off, on, blink |
| X | speed preset: 25, 50, 75, 100 % |
| + / - | speed up / down 10 % (controller) |
| N | her MIND mode on / off: the LLM on this PC drives her (x86 PC, GTX 1050+; controller) |
| H | IDA's mind on / off: drives IDA over the IR link, blind and careful (controller) |
| I / J / K / L | drive the picked fleet robot through NORA's IR link (hold) |
| Tab / Shift+Tab | pick the next / previous fleet robot (controller) |
| O / P / Y | picked robot: auto / manual / speed (controller) |
| G / V | picked robot: unlock / lock its wheels (controller; UNLOCK / LOCK on the web page) |
| STOP | stops her in any mode (controller: the D-pad's middle, or the pad's A) |
| THEME | controller colours: default, light, custom (all from colour_scheme.xml) |
| Emulate | controller: a pretend NORA on this PC, no robot (connection screen, or --emulate) |
| F1 | this window (controller; KEYS on the web page) |
| Esc | quit (controller) |

**Gamepad or joystick (controller; ARM's flight stick works too)**

| Key | Does |
|---|---|
| Left stick / D-pad | drive: forward, back, strafe, blended (half left is both; takes over from Auto) |
| LB / RB, right stick, twist | turn left / right |
| A (button 0) | stop |
| B (1) | UV light: off, on, blink |
| X (2) | play / pause music |
| Y (3) | next track |
| Back (6) / Start (7) | Auto / Manual mode |
| | *plug it in any time; Xbox, PlayStation and Switch pads all have these buttons* |

**IR remote**

| Key | Does |
|---|---|
| Up / Down | forward / back (IR mode, hold) |
| Left / Right | strafe or turn (IR mode, hold) |
| Enter | switch Left / Right between strafing and turning |
| 1 / 2 / 3 / 4 | Manual / Auto / Line / IR-remote mode |
| 5 | the wheel lock: unlock / lock (one button, no PIN) |
| 6 / 7 | slower / faster: speed down / up 10 % |
| Play/Pause | play / pause music |
| Stop | stop music |
| Next / FFWD | next track |
| Prev / Rewind | previous track |
| Repeat | repeat the song |
| Vol+ / Vol- / Mute | volume |
| | *the wheel lock blocks driving here too: 5 frees the wheels* |

**Bluetooth serial (the controller's BT link)**

| Key | Does |
|---|---|
| F / B / L / R | forward / back / strafe left / right |
| Q / E | turn left / right |
| S | stop |
| 1 / 2 / 3 | Manual / Auto / Line mode |
| U | UV light: off, on, blink |
| K | lock the wheels |
| V1234 | unlock the wheels (V, then her PIN) |
| M / N / P / X | music: play-pause / next / previous / stop |
| + / - | speed up / down |
| ? | status |
| I or T, then a letter | drive IDA (I) or another robot (T) through the IR link |
| ... then U / K | unlock / lock that robot's wheels |
| H, then name:caps | register a fleet member (DREAM over Bluetooth) |

<!-- keymap:end -->

### Python controller
```bash
pip install -r requirements.txt
python scripts/controller.py                 # WiFi, NORA at 192.168.4.1:5002
python scripts/controller.py --bt COM7       # Bluetooth serial instead
python scripts/controller.py --emulate       # a pretend NORA on this PC (scripts/fake_nora.py), no robot
```
`run.bat` / `run.ps1` / `run.sh` offer the web page, the controller, or the controller on EMULATE.

- **Two keys blend**: W+A drives forward and slides left at once, W+Q curves, and the pad's stick is
  analog (her `/drive?f=&s=&t=`, mixed over the four mecanum wheels; she stops by herself 0.5 s after the
  last one). Firmware older than that gets the stronger of the two, one move at a time.
- **Driving takes over**: driving her while she's in AUTO, LINE or IR puts her in MANUAL, so she does what's
  pressed and stops when you let go. **STOP** works in any mode.
- **Settings stick**: mode, UV and speed are sent again until her `/sensors` says so.
- **Pads**: Xbox, PlayStation and Switch pads all have the same buttons (SDL's controller layer,
  `gamepad_drive.py`); a flight stick or an unknown pad is read raw.
- **THEME**: DEFAULT, LIGHT or CUSTOM, all from `colour_scheme.xml` (sampled from her avatar). The parts every
  robot's controller shares are in `scripts/controller_kit.py`.
- **EMULATE** (the connection screen, or `--emulate`): a pretend NORA in a pretend room: her four sonars
  change as she drives, she won't drive into a wall closer than 15 cm, and she boots with her wheels locked.
- Her MIND log: `~/.robot_minds/logs/NORA-<date>.log` (and `IDA-<date>.log`).

### Fleet IR link
NORA drives [IDA](https://github.com/CursedPrograms/IDA-Robot-v00), MILA, WHIP, KIDA-01 and NIKA through her IR transmitter (GPIO 33). The frames are Samsung-format IR, one address per robot, with the same commands `0x48`–`0x4F` for all of them. No remote in the fleet uses that protocol, so nothing else reacts, and NORA ignores her own frames on her receiver. Drive commands repeat every 150 ms while held, with `stop` on release; each robot stops by itself if the link goes quiet.

| Robot | Address | Receiver |
| :--- | :--- | :--- |
| [IDA](https://github.com/CursedPrograms/IDA-Robot-v00) | `0x0DA1` | `IDA.ino` |
| [MILA](https://github.com/CursedPrograms/MILA) | `0x0DA2` | `MILA.ino` |
| [WHIP](https://github.com/CursedPrograms/WHIP-Robot-v00) | `0x0DA3` | `esp32.ino` |
| [KIDA-01](https://github.com/CursedPrograms/KIDA-Robot-v01) | `0x0DA4` | `arduino01` sketch (prints the IR lines the Pi already reads) |
| [NIKA](https://github.com/CursedPrograms/NIKA-Robot-v00) | `0x0DA5` | `NIKA.ino` (her beacon from NORA is her synth brain's social drive) |

Pick the robot with the tabs on the web page's panel, or `<` `>` / `Tab` in the Python controller. The panel's avatar, name and colour follow the selection.

| | Forward / Back / Left / Right | Stop | Obstacle mode | Manual | Speed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Web page** (fleet panel) | `I` `K` `J` `L` or the buttons | buttons | button | button | button |
| **Python controller** (right column) | `I` `K` `J` `L` or the buttons | release | `O` | `P` | `Y` |
| **HTTP** | `/link?r=ida\|mila\|whip\|kida\|nika&c=fw` `bw` `left` `right` | `stop` | `auto` | `manual` | `speed` |
| **Bluetooth** | `T` + robot (`I` `M` `W` `K` `N`) + `F` `B` `L` `R` | `…S` | `…O` | `…W` | `…X` |

`/ida?c=...` and Bluetooth `I` + letter still drive IDA directly. WHIP has a single gait speed, so `speed` does nothing there.

### Voice
`make_voice.bat` turns her lines into MP3s with Windows text-to-speech (Zira) and ffmpeg: `voice\track200.mp3`–`track231.mp3`. Copy them to the root of the SD card next to `track000.mp3` (`make_voice.bat E:` copies them for you). The lines are listed at the top of `scripts/make_voice.ps1`:

| Track | Line | When |
| :--- | :--- | :--- |
| 200–203 | Manual / Auto / Line follow / Remote mode | mode changes |
| 204 / 205 | Motors locked / unlocked | motor lock |
| 206 | Something's in the way | a drive command blocked by an obstacle (at most every 3 s) |
| 207–209 | UV off / on / blinking | UV changes |
| 210 | Hello, I'm NORA | after the startup sound |

Missing tracks fall back to the old beeps, and she stays quiet while music is playing. The ESP32 can play any line with `SAY:<n>` on the UNO link.

### Talking with the fleet
Every 30–90 s, when she hasn't driven anyone over the link for 15 s (and isn't being driven by the IR remote), NORA says something to IDA: she beeps a phrase on her buzzer and, once she's finished, sends it over the link, and IDA answers with her reply. The robots talk in **Brainfuck**: every phrase is a Brainfuck program that prints its words (hello is `++++++++[>+++++++++++++<-]>.+.` → `hi`), and a robot says it by beeping the program on its buzzer, one tone per symbol, pitched to its own voice. The phrasebook (`talk_bf.h`) is generated by RIFT's `Fleet/brainfuck_talk.py`: phrases 0–6 (hello, how are you, happy, curious, sleepy, let's play, bye) and their replies 7–13. Between phrases a silent "I am here" beacon goes to IDA, MILA, WHIP and KIDA-01 in turn (each hears it every 8 s); IDA greets NORA when it comes back after a minute away. Her clap detector ignores sounds while she talks, so the beeps don't start the music. [RIFT](https://github.com/CursedPrograms/RIFT) also has her chat with the other robots over WiFi (`GET /chirp?u=<0-13>`) and merges her IR chats into its conversation log.

Phrases (link commands): `0x40` hello, `0x41` how are you, `0x42` happy, `0x43` curious, `0x44` sleepy, `0x45` let's play, `0x46` bye, `0x47` "I am here" beacon (silent).

- `GET /talk` returns the last 12 things she said, as JSON (`{"now":ms,"log":[{"ms":..,"from":"NORA","to":"IDA","said":"hello","text":"hi","bf":"++++++++[>+++++++++++++<-]>.+."}]}`) — the web panel shows the latest four.
- `/talk?r=ida&p=hello` says one now (`p` = `hello` `how` `happy` `curious` `sleepy` `play` `bye`); **SAY HI** on the fleet panel does this for the selected robot.

</details>

---

<div align="center">
  <img src="images/NORA4.jpg" alt="NORA Robot" width="400"/>
</div>

---

<details>
<summary><b>📂 Documentation & Assets</b></summary>

* [ACEBOTT ESP32 Max V1.0 Docs](https://acebottteam.github.io/acebott-docs-master/board/ESP32/QA007%20ESP32%20Max%20V1.0%20Controller%20Board.html)
* [ACEBOTT: Getting started with ESP32](https://acebott.com/docs/getting-started-with-esp32/)
* [CH340 Driver Download](https://acebottteam.github.io/acebott-docs-master/getting%20started/Arduino/Download%20CH340%20Driver%20on%20Windows%20System.html)

</details>

---

<div align="center">
  <img src="images/NORA5.jpg" alt="NORA Robot" width="400"/>
</div>

---

## Who's nearby (ESP-NOW + Bluetooth LE)

Every robot sends a small **"I'm here"** beacon twice a second and listens for the others'. From the **signal strength** it knows roughly how close each one is, and from how that changes over time whether it's **coming closer, steady or leaving**:

| Signal | Zone |
| :--- | :--- |
| above −45 dBm | very close |
| −45 to −60 dBm | near |
| −60 to −75 dBm | medium |
| below −75 dBm | far |

It's coarse (walls, bodies and antenna angle all change it): for "who's around", not distance. Precise collision avoidance stays with the ultrasonic and ToF sensors.

**They tell each other what they're doing**, because a rising signal looks the same from both sides even when only one robot moves. Over ESP-NOW they say it **in Brainfuck**, like the fleet's conversations: each beacon carries a program that prints `park`, `go`, `wait` or `hand` (a human is driving), and the receiver runs it. Bluetooth adverts are too small for a program, so BLE carries the same state as one byte.

**Who makes way**, in self-driving modes only:

| The other robot... | So this one... |
| :--- | :--- |
| is parked | is the one closing in: steers away |
| is yielding | carries on, carefully |
| is driven by a human | makes way (it's unpredictable) |
| drives itself | follows the alphabet: KIDA00, KIDA01, NORA, WHIP; everyone makes way for MILA, who can't hear the others |
| is leaving | carries on |

Making way = stop for 2 s, turn away, then drive on (and not yield again for 5 s, so two robots that stay close don't take turns forever). While another robot is near, or coming closer, it drives slower with wider margins.

NORA hears the others over **ESP-NOW** (WHIP, MILA) and **Bluetooth LE** (WHIP and the Pi robots KIDA-00 / KIDA-01), alongside her WiFi network and classic Bluetooth. In **Auto** mode she drives slower with wider margins while a robot is near, and gives way (stop, wait, turn away) when one with right of way is very close. In **Line** mode she waits on the line instead of turning. `GET /near` shows her list. Code: `scripts/esp32/fleet_near.h` and `fleet_near_ble.h`.

---

## Screenshots

<div align="center">
  <img src="images/screenshots/controller-python.png" alt="Python controller" width="260"/>
  <img src="images/screenshots/web-dashboard.png" alt="Web dashboard" width="640"/>
</div>

<p align="center"><i>Python controller (on EMULATE, driving W+Q), Web dashboard (captured without a robot connected).</i></p>

---

<div align="center">&copy; Cursed Entertainment 2026</div>

<div align="center">
  <a href="https://cursed-entertainment.itch.io/">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png" alt="Cursed Entertainment" width="250"/>
  </a>
</div>

<div align="center">
  <a href="https://github.com/SynthWomb">
    <img src="https://github.com/SynthWomb/synth.womb/blob/main/logos/synthwomb07.png?raw=true" alt="SynthWomb" width="200"/>
  </a>
</div>

---

<!-- DREAM-ECOSYSTEM:START -->
## The DREAM Robotics ecosystem

**Minds** — [DREAM](https://github.com/CursedPrograms/DREAM) companion · [TINA](https://github.com/CursedPrograms/TINA) engineer · [NINA](https://github.com/CursedPrograms/NINA) caretaker · [RIFT](https://github.com/CursedPrograms/RIFT) fleet hub · [LYCEA](https://github.com/CursedPrograms/LYCEA) training school  
**Robots** — [NORA](https://github.com/CursedPrograms/NORA-Robot-v00) · [MILA](https://github.com/CursedPrograms/MILA-Robot-v00) · [WHIP](https://github.com/CursedPrograms/WHIP-Robot-v00) · [KIDA-00](https://github.com/CursedPrograms/KIDA-Robot-v00) · [KIDA-01](https://github.com/CursedPrograms/KIDA-Robot-v01) · [IDA](https://github.com/CursedPrograms/IDA-Robot-v00) · [ARM](https://github.com/CursedPrograms/ARM-Robot-v01) · [NIKA](https://github.com/CursedPrograms/NIKA-Robot-v00) · [ARIA](https://github.com/CursedPrograms/ARIA-Robot-v00)  
**Remotes** — [RITA](https://github.com/CursedPrograms/RITA) handheld remote  
<!-- DREAM-ECOSYSTEM:END -->
