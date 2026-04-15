[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  
<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>
<div align="center">
  <img alt="C++" src="https://img.shields.io/badge/c++%20-%23323330.svg?&style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
</div>

<div align="center">
  <img alt="Arduino" src="https://img.shields.io/badge/-Arduino-323330?style=for-the-badge&logo=arduino&logoColor=white"/>
  <img alt="ESP32" src="https://img.shields.io/badge/ESP32-%23323330.svg?&style=for-the-badge&logo=espressif&logoColor=white"/>

</div>

<div align="center">
  <img alt="Git" src="https://img.shields.io/badge/git%20-%23323330.svg?&style=for-the-badge&logo=git&logoColor=white"/>
</div>

---

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

## 📖 Overview

<details>
<summary><b>View Overview</b></summary>

NORA is built on the **ESP32**, utilizing its dual-core processing to handle a custom WiFi Access Point for remote operation while simultaneously managing reactive obstacle avoidance via a 4-sensor ultrasonic array.

### Core Features
- [x] **Omnidirectional Movement:** Move in any direction without turning.
- [x] **Self-Hosted AP:** No router required for field operation.
- [x] **Reactive Safety:** 360° sensor coverage for auto-braking.
- [x] **Internal UV:** Specialized UV light disinfection capabilities.

</details>

---

## Prerequisites
<details>
<summary><b>View Prerequisites</b></summary>

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
| Ultrasonic Sensors | 4x HC-SR04 (Front, Back, Left, Right)|
| Line Follower | 3-Channel Line Tracking Sensor |


</details>

---

#### Libraries:

- SoftwareSerial.h
- WiFi.h
- WebServer.h

---

<div align="center">
  <img src="images/NORA2.jpg" alt="NORA Robot" width="400"/>
</div>

---

# Schematics
## ⚡ Technical Pinouts

> [!CAUTION]
> **Ground Loop Warning:** All modules must share a common GND. Failure to bridge grounds will cause erratic motor behavior and sensor noise.

<details>
<summary><b>View ESP32 Motor Controller Configuration</b></summary>

#### L298N-0 (Front Drive)
| Motor | PWM Pin | Dir 1 | Dir 2 |
| :--- | :--- | :--- | :--- |
| **M0** | `GPIO 5` | `GPIO 16` | `GPIO 17` |
| **M1** | `GPIO 23`| `GPIO 18` | `GPIO 19` |

#### L298N-1 (Rear Drive)
| Motor | PWM Pin | Dir 1 | Dir 2 |
| :--- | :--- | :--- | :--- |
| **M2** | `GPIO 12` | `GPIO 13` | `GPIO 14` |
| **M3** | `GPIO 27` | `GPIO 26` | `GPIO 25` |
</details>

<details>
<summary><b>View UNO Sensor Array Wiring</b></summary>

| Direction | Trigger Pin | Echo Pin |
| :--- | :--- | :--- |
| **FRONT** | `A0` | `A1` |
| **LEFT** | `D6` | `D7` |
| **BACK** | `A4` | `A5` |
| **RIGHT** | `A2` | `A3` |
</details>

> [!TIP]
> **Pro-Tip:** Common GND is non-negotiable. If the motors behave erratically or the sensors give "0" readings, check your ground bridge first!

---

## 🌐 Connectivity & Controls

<details>
<summary><b>Connectivity & Controls</b></summary>

### Network Configuration
| Parameter | Value |
| :--- | :--- |
| **SSID** | `NORA` |
| **Password** | `12345678` |

### RIFT Integration
To connect via [RIFT](https://github.com/CursedPrograms/RIFT), ensure NORA is active on:
* `localhost:5003`

</details>

---

<div align="center">
  <img src="images/NORA4.jpg" alt="NORA Robot" width="400"/>
</div>
---

## 📂 Documentation & Assets
* [ACEBOTT ESP32 Max V1.0 Docs](https://acebottteam.github.io/acebott-docs-master/board/ESP32/QA007%20ESP32%20Max%20V1.0%20Controller%20Board.html)
* [CH340 Driver Download](https://acebottteam.github.io/acebott-docs-master/getting%20started/Arduino/Download%20CH340%20Driver%20on%20Windows%20System.html)
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
