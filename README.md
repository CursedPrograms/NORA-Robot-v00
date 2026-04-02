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

![NORA](images/NORA1.jpg)

- ESP32
- Ardiuno Uno
- 2x l298n Motordrivers
- 4x 5v DC Motordriver
- 4x ultrasonic Sensors
- UV Light

#include <SoftwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>

ap_ssid     = "NORA";
ap_password = "12345678";

![NORA](images/NORA2.jpg)

// =====================
// MOTOR PINS - ESP32
// =====================
#define ENA1 5
#define M1_1 16
#define M1_2 17

#define ENA2 23
#define M2_1 18
#define M2_2 19

#define ENB1 12
#define M3_1 13
#define M3_2 14

#define ENB2 27
#define M4_1 26
#define M4_2 25

// =====================
// PIN SETUP
// =====================

// Front
#define F_TRIG A0
#define F_ECHO A1

// Left
#define L_TRIG 6
#define L_ECHO 7

// Back
#define B_TRIG A4
#define B_ECHO A5

// Right
#define R_TRIG A2
#define R_ECHO A3

![NORA](images/NORA4.jpg)


https://acebottteam.github.io/acebott-docs-master/board/ESP32/QA007%20ESP32%20Max%20V1.0%20Controller%20Board.html

https://acebottteam.github.io/acebott-docs-master/getting%20started/Arduino/Download%20CH340%20Driver%20on%20Windows%20System.html

![NORA](images/NORA5.jpg)

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
