[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  
<br>
<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>
<br>
<div align="center">
  <img alt="Python" src="https://img.shields.io/badge/python%20-%23323330.svg?&style=for-the-badge&logo=python&logoColor=white"/>
</div>
<div align="center">
    <img alt="Git" src="https://img.shields.io/badge/git%20-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
  <img alt="PowerShell" src="https://img.shields.io/badge/PowerShell-%23323330.svg?&style=for-the-badge&logo=powershell&logoColor=white"/>
  <img alt="Shell" src="https://img.shields.io/badge/Shell-%23323330.svg?&style=for-the-badge&logo=gnu-bash&logoColor=white"/>
  <img alt="Batch" src="https://img.shields.io/badge/Batch-%23323330.svg?&style=for-the-badge&logo=windows&logoColor=white"/>
  </div>
<br>

# NORA: Nomadic Omnidirectional Reactive Automaton

## Related Projects

- [KIDA-Robot-v01](https://github.com/CursedPrograms/KIDA-Robot-v01)
- [KIDA-Robot-v00](https://github.com/CursedPrograms/KIDA-Robot-v00)
- [WHIP-Robot-v00](https://github.com/CursedPrograms/WHIP-Robot-v00)
- [DREAM](https://github.com/CursedPrograms/DREAM)
- [RIFT](https://github.com/CursedPrograms/RIFT)

---

<div align="center">
  <img src="images/NORA1.jpg" alt="NORA Robot" width="400"/>
</div>

---

## Overview

NORA is an omnidirectional mobile robot platform built around the ESP32, featuring 4-wheel independent drive, ultrasonic obstacle detection in all directions, and UV light capabilities. The robot creates its own WiFi access point for remote control and monitoring.

---

## Prerequisites

### Software
- [Arduino IDE](https://docs.arduino.cc/software/ide/)

### Hardware

### Microcontrollers
| **Component** | **Details** |
|-----------|---------|
| Microcontroller 0 | ESP32 (ACEBOTT QA007 Max Controller Board) | Dev0 |
| Microcontroller 1 | Arduino UNO | Dev1 |

### Chassis & Motion
| **Component** | **Details** |
|-----------|---------|
| Chassis | Omnidirectional Robot Chassis |
| Motor Driver | 2x L298N |
| Motors | 4x 5V DC Motors |

### User Controllers
| **Component** | **Details** |
|-----------|---------|
| Interface | PC, Android, iPhone |

### Power System
| **Component** | **Details** |
|-----------|---------|
| Battery | 2s 18650|

### Sensors
| **Component** | **Details** |
|-----------|---------|
| Ultrasonic Sensors | HC-SR04 × 4|
| Line Follower | 3-Channel Line Tracking Sensor |

---

#### Libraries:

- SoftwareSerial.h
- WiFi.h
- WebServer.h

---
### Network Setup:
#### Broadcast Network:
- ap_ssid     = "NORA";
- ap_password = "12345678";

#### Connect to [RIFT](https://github.com/CursedPrograms/RIFT):
- autoconnect on rift: localhost:5000, dream: localhost:5001 or nora: localhost:5003
---

<div align="center">
  <img src="images/NORA2.jpg" alt="NORA Robot" width="400"/>
</div>

---
### Pin Configuration
#### MOTOR PINS - ESP32
**L298N-0**
MOTOR 0
```
ENA1 → GPIO 5   (PWM)
M1_1 → GPIO 16
M1_2 → GPIO 17
```

MOTOR 1
```
ENA2 → GPIO 23  (PWM)
M2_1 → GPIO 18
M2_2 → GPIO 19
```

**L298N-1**
MOTOR 2
```
ENB1 → GPIO 12  (PWM)
M3_1 → GPIO 13
M3_2 → GPIO 14
```

MOTOR 3
```
ENB2 → GPIO 27  (PWM)
M4_1 → GPIO 26
M4_2 → GPIO 25
```

### ⚙️ ESP32 NET SUMMARY

#### PWM
```
GPIO5  → ENA1
GPIO23 → ENA2
GPIO12 → ENB1
GPIO27 → ENB2
```

#### Direction
```
GPIO16,17 → Motor0 direction
GPIO18,19 → Motor1 direction
GPIO13,14 → Motor2 direction
GPIO26,25 → Motor3 direction
```

---

#### 📡 ARDUINO SENSOR SYSTEM
##### FRONT ULTRASONIC
```
F_TRIG → A0
F_ECHO → A1
```
##### LEFT ULTRASONIC
```
L_TRIG → D6
L_ECHO → D7
```
##### BACK ULTRASONIC
```
B_TRIG → A4
B_ECHO → A5
```
##### RIGHT ULTRASONIC
```
R_TRIG → A2
R_ECHO → A3
```
---

<div align="center">
  <img src="images/NORA4.jpg" alt="NORA Robot" width="400"/>
</div>
---

### ACEBOTT ESP32 Documentation

https://acebottteam.github.io/acebott-docs-master/board/ESP32/QA007%20ESP32%20Max%20V1.0%20Controller%20Board.html

https://acebottteam.github.io/acebott-docs-master/getting%20started/Arduino/Download%20CH340%20Driver%20on%20Windows%20System.html

---

<div align="center">
  <img src="images/NORA5.jpg" alt="NORA Robot" width="400"/>
</div>

<br>
<div align="center">
© Cursed Entertainment 2026
</div>
<br>
<div align="center">
<a href="https://cursed-entertainment.itch.io/" target="_blank">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png"
        alt="CursedEntertainment Logo" style="width:250px;">
</a>
</div>
