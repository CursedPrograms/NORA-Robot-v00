// secrets.h - NORA's WiFi names and passwords (the fleet's, 2026-10-14).
// RIFT's password is also set on the router; NORA's fallback password goes into
// every robot that falls back to her (MILA, WHIP, the KIDAs).
#pragma once

#define FLEET_SSID      "RIFT"          // the fleet's router (RIFT's network)
#define FLEET_PASSWORD  "mmvbkbak"     // its WPA2 password (8+ characters)

#define NORA_AP_SSID     "NORA"          // her own network, when RIFT isn't around
#define NORA_AP_PASSWORD "mmvbkbak" // 8+ characters; MILA, WHIP and the KIDAs need the same one to fall back to her

#define MOTOR_PASSWORD  "1234"     // unlocks her wheels (web /motorlockOff?pw=, Bluetooth "V...")
