// secrets.example.h - the template for secrets.h (used when there is no secrets.h).
// Without a secrets.h, NORA builds with these placeholders: she can't join RIFT
// (wrong password) and hosts her own "NORA" network instead.
#pragma once

#define FLEET_SSID      "RIFT"          // the fleet's router (RIFT's network)
#define FLEET_PASSWORD  "change-me"     // its WPA2 password (8+ characters)

#define NORA_AP_SSID     "NORA"          // her own network, when RIFT isn't around
#define NORA_AP_PASSWORD "change-me-too" // 8+ characters; MILA, WHIP and the KIDAs need the same one to fall back to her

#define MOTOR_PASSWORD  "change-me"     // unlocks her wheels (web /motorlockOff?pw=, Bluetooth "V...")
