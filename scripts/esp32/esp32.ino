#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "BluetoothSerial.h"
#include <IRremote.hpp>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

// =====================
// BLUETOOTH
// =====================
// Pair with "NORA" from your phone, then use any Bluetooth serial /
// RC controller app. Single-character commands:
//   F=forward  B=backward  L=strafe left  R=strafe right
//   Q=turn left  E=turn right  S=stop
//   1=manual mode  2=auto mode  3=line mode
//   U=cycle UV     M=music play/pause  N=next track  P=prev track  X=music stop
//   K=lock motors (always allowed)
//   V + 4 chars=unlock motors (password, e.g. "V1234")
//   +=speed up  -=speed down  ?=print sensor + music status
BluetoothSerial SerialBT;

// =====================
// WIFI
// =====================
const char* ap_ssid     = "NORA";
const char* ap_password = "12345678";
WebServer server(5002);

// =====================
// MOTOR PINS
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

// Forward declarations with default args (kept here only — the Arduino IDE
// auto-generates prototypes from the definitions below, and a default
// argument may only be specified once per translation unit).
void moveForward(int scale = 100);
void moveBackward(int scale = 100);
void strafeLeft(int scale = 100);
void strafeRight(int scale = 100);
void turnLeft(int scale = 100);
void turnRight(int scale = 100);
void stopMotors();
void setMotor(int ena, int pin1, int pin2, int s1, int s2, int speed, int scale = 100);

// =====================
// LINE FOLLOWER (on ESP32)
// =====================
#define LF_LEFT  34
#define LF_MID   35
#define LF_RIGHT 39

// =====================
// UV LIGHT (on ESP32)
// =====================
#define UV_PIN 4

// =====================
// LIGHT + SOUND SENSORS
// =====================
// Both live on the Arduino now (A5 / pin 5) -- it has to poll the sound
// sensor tightly between ultrasonic pings to catch a clap reliably, which
// its loop is naturally suited for. It forwards smoothed light% and a
// latched sound-event flag over serial (LT / SND fields), parsed below in
// parseSensorLine(). The clap-count/double-clap timing itself still runs
// here, just fed by that flag instead of a local digitalRead edge.
int  lightPct        = 0;      // 0 = dark, 100 = bright
bool soundRecent     = false;  // true if a sound was heard in the last 500ms
unsigned long lastSoundMs   = 0;
unsigned long firstClapMs   = 0;
int  clapCount       = 0;

// =====================
// TEMPERATURE / HUMIDITY (AHT10, I2C)
// =====================
// Wire.begin() with no args defaults to SDA=21 SCL=22 on the ESP32.
Adafruit_AHTX0 aht;
bool  aht10Ok  = false;   // if begin() failed, skip reads and leave -1 (matches ultrasonic's "no reading" convention -- NAN would serialize as invalid JSON and silently freeze the whole HUD)
float tempC    = -1;
float humidity = -1;

// =====================
// IR RECEIVER (on ESP32)
// =====================
// Cheap NEC remote (see ir_mapping.txt for the full button dump). Data pin
// only — GPIO32 was the next free, non-strapping pin left on the board;
// power the receiver from 3.3V, not 5V, so its output never exceeds the
// ESP32's logic level.
// IR transmitter LED lives on GPIO33 (freed up now that the sound sensor
// moved to the Arduino). Pin + library are initialized below; nothing
// sends on it yet, this only wires up the transmitter for later use.
#define IR_RECEIVE_PIN 32
#define IR_SEND_PIN    33

#define IR_UP         0x6
#define IR_LEFT       0x47
#define IR_DOWN       0x44
#define IR_RIGHT      0x40
#define IR_ENTER      0x7
#define IR_1          0x9
#define IR_2          0x1D
#define IR_3          0x1F
#define IR_4          0xD
#define IR_MUTE       0x56
#define IR_VOL_UP     0x3
#define IR_VOL_DOWN   0x2
#define IR_REPEAT     0x53
#define IR_PLAY_PAUSE 0xF
#define IR_STOP       0x42
#define IR_NEXT       0x1A
#define IR_PREV       0xE
#define IR_FFWD       0x1E
#define IR_REWIND     0xA

#define IR_DRIVE_TIMEOUT_MS 250   // stop if no drive repeat arrives within this long (no key-up event over IR)

bool          irStrafe     = true;  // Enter toggles Left/Right between strafe (true) and turn (false)
unsigned long irLastDriveMs = 0;

// =====================
// DRIVE MODE
// =====================
enum DriveMode { MODE_MANUAL, MODE_AUTO, MODE_LINE, MODE_IR };
DriveMode driveMode = MODE_MANUAL;

// =====================
// CALIBRATION & SPEED
// =====================
int cal[4]   = {255, 255, 255, 255};
int speedPct = 100;
Preferences prefs;

// =====================
// FLEET TEXT MESSAGES
// =====================
// NORA has no display/speaker, so incoming "/message" text just lands in a
// small ring buffer that other fleet nodes (RIFT, ComCentre) can poll via
// "/messages" — a lightweight message board rather than something acted on.
#define MESSAGE_LOG_SIZE 10
String messageLog[MESSAGE_LOG_SIZE];
int    messageLogHead  = 0;
int    messageLogCount = 0;

String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// =====================
// MOTOR LOCK
// =====================
// Starts locked on every boot so a static-feature test session can never
// accidentally drive off a counter (this is why it exists — see incident).
// Locking is always allowed with no password; unlocking requires one,
// checked here in firmware so every client (web, pygame, raw BT/HTTP)
// is held to the same gate instead of trusting each app to enforce it.
#define MOTOR_PASSWORD "1234"
bool   motorLocked   = true;
bool   btAwaitingPw  = false;
String btPwBuffer    = "";

// =====================
// AUTONOMOUS STATE
// =====================
#define DIR_FRONT  0
#define DIR_RIGHT  1
#define DIR_BACK   2
#define DIR_LEFT   3

enum AutoState { AUTO_COOLDOWN, AUTO_MOVING };
AutoState     autoState    = AUTO_COOLDOWN;
int           autoDir      = DIR_FRONT;
unsigned long autoCooldown = 0;

// boxed-in tracking for AutonomousMode() — global (not function-static) so
// entering Auto mode can reset it; otherwise a stale timestamp from a past
// stuck episode could fire an escape turn immediately on re-entry
unsigned long autoStuckSinceMs  = 0;
bool          autoEscaping      = false;
unsigned long autoEscapeUntilMs = 0;

// =====================
// LINE FOLLOWER STATE
// =====================
// global (not function-static) so entering Line mode can reset it —
// otherwise a stale "lost the line" timestamp from a past run could fire
// an immediate stop on re-entry instead of the intended grace period
int           lineLastTurn    = 1;
unsigned long lineLostSinceMs = 0;

// =====================
// FLEET REGISTRY (port 5000)
// =====================
// NORA is the always-on node at a fixed address, so she's the natural
// place for robots (and a fleet manager like RIFT, if one's running) to
// find each other, instead of every app brute-force-scanning the subnet.
//
//   GET  /ping      -> liveness check
//   POST /register  -> body: name, type, capabilities (comma-separated)
//   GET  /robots    -> {"authority": "...", "robots": [...]}
//
// If something registers with type "fleet_manager" (e.g. RIFT), NORA
// treats it as the fleet's source of truth: she keeps quietly bookkeeping
// whoever else registers, but stops *surfacing* her own roster in
// /robots — callers get pointed at the authority instead. The moment
// the authority's heartbeat lapses (it closed, crashed, or the network
// dropped), NORA reclaims the role automatically and starts serving her
// own roster again. No explicit handoff needed either direction.
#define FLEET_MAX    8
#define FLEET_TTL_MS 20000   // drop an entry (or the authority) after this long without a heartbeat

struct FleetEntry {
  bool          used = false;
  String        name;
  String        type;
  String        capabilities;   // as given, comma-separated
  IPAddress     ip;
  unsigned long lastSeenMs = 0;
};

FleetEntry    fleet[FLEET_MAX];
WebServer     fleetServer(5000);

String        authorityName      = "NORA";
unsigned long authorityExpiresMs = 0;   // 0 = NORA herself is the authority

// =====================
// SENSOR VALUES
// =====================
float front_cm = -1, left_cm = -1, back_cm = -1, right_cm = -1;
int   lf_l = 0, lf_m = 0, lf_r = 0;
String serialBuffer = "";

// =====================
// MUSIC STATE (reported by Arduino)
// =====================
int musicTrack = 100;   // current track number
int musicState = 0;     // 0=stopped 1=playing 2=paused

// =====================
// UV STATE
// =====================
int           uvMode       = 0;   // 0=off 1=on 2=blink
bool          uvBlinkState = false;
unsigned long lastUVBlink  = 0;

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(9600);   // link to Arduino (sensors in, music commands out)

  prefs.begin("cal", true);
  cal[0]   = prefs.getInt("m1",  255);
  cal[1]   = prefs.getInt("m2",  255);
  cal[2]   = prefs.getInt("m3",  255);
  cal[3]   = prefs.getInt("m4",  255);
  speedPct = prefs.getInt("spd", 100);
  prefs.end();

  WiFi.softAP(ap_ssid, ap_password);
  SerialBT.begin("NORA");   // Bluetooth device name shown when pairing
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
  // IrSender.begin(IR_SEND_PIN) is deliberately not called yet -- on the
  // ESP32, IRremote's send and receive sides can contend for the same
  // timer/PWM resource, and there's no transmit behavior wired up to need
  // it yet anyway. Uncomment once you actually build out the TX feature.

  Wire.begin();
  aht10Ok = aht.begin();   // if missing, tempC/humidity just stay -1

  pinMode(ENA1, OUTPUT); pinMode(M1_1, OUTPUT); pinMode(M1_2, OUTPUT);
  pinMode(ENA2, OUTPUT); pinMode(M2_1, OUTPUT); pinMode(M2_2, OUTPUT);
  pinMode(ENB1, OUTPUT); pinMode(M3_1, OUTPUT); pinMode(M3_2, OUTPUT);
  pinMode(ENB2, OUTPUT); pinMode(M4_1, OUTPUT); pinMode(M4_2, OUTPUT);

  pinMode(LF_LEFT,  INPUT);
  pinMode(LF_MID,   INPUT);
  pinMode(LF_RIGHT, INPUT);

  pinMode(UV_PIN, OUTPUT);
  digitalWrite(UV_PIN, LOW);

  // Manual movement
  server.on("/fw",    []() { if (driveMode == MODE_MANUAL) moveForward();  server.send(200, "text/plain", "OK"); });
  server.on("/bw",    []() { if (driveMode == MODE_MANUAL) moveBackward(); server.send(200, "text/plain", "OK"); });
  server.on("/left",  []() { if (driveMode == MODE_MANUAL) strafeLeft();   server.send(200, "text/plain", "OK"); });
  server.on("/right", []() { if (driveMode == MODE_MANUAL) strafeRight();  server.send(200, "text/plain", "OK"); });
  server.on("/turnL", []() { if (driveMode == MODE_MANUAL) turnLeft();     server.send(200, "text/plain", "OK"); });
  server.on("/turnR", []() { if (driveMode == MODE_MANUAL) turnRight();    server.send(200, "text/plain", "OK"); });
  server.on("/stop",  []() { if (driveMode == MODE_MANUAL) stopMotors();   server.send(200, "text/plain", "OK"); });

  // Mode switching
  server.on("/modeManual", []() { driveMode = MODE_MANUAL; stopMotors(); server.send(200, "text/plain", "OK"); });
  server.on("/modeAuto",   []() { driveMode = MODE_AUTO;   stopMotors(); autoState = AUTO_COOLDOWN; autoCooldown = 0; autoStuckSinceMs = 0; autoEscaping = false; server.send(200, "text/plain", "OK"); });
  server.on("/modeLine",   []() { driveMode = MODE_LINE;   stopMotors(); lineLostSinceMs = 0; server.send(200, "text/plain", "OK"); });
  server.on("/modeIR",     []() { driveMode = MODE_IR;     stopMotors(); irLastDriveMs = 0; server.send(200, "text/plain", "OK"); });

  // UV
  server.on("/uvOff",   []() { uvMode = 0; server.send(200, "text/plain", "OK"); });
  server.on("/uvOn",    []() { uvMode = 1; server.send(200, "text/plain", "OK"); });
  server.on("/uvBlink", []() { uvMode = 2; server.send(200, "text/plain", "OK"); });

  // Motor lock — lock is free, unlock needs the password
  server.on("/motorlockOn", []() {
    motorLocked = true;
    stopMotors();
    server.send(200, "text/plain", "OK");
  });
  server.on("/motorlockOff", []() {
    if (server.hasArg("pw") && server.arg("pw") == MOTOR_PASSWORD) {
      motorLocked = false;
      server.send(200, "text/plain", "OK");
    } else {
      server.send(403, "text/plain", "DENIED");
    }
  });

  // Music player -> forwarded to Arduino over serial
  server.on("/muPlay", []() { Serial.println("M:PLAY"); server.send(200, "text/plain", "OK"); });
  server.on("/muNext", []() { Serial.println("M:NEXT"); server.send(200, "text/plain", "OK"); });
  server.on("/muPrev", []() { Serial.println("M:PREV"); server.send(200, "text/plain", "OK"); });
  server.on("/muStop", []() { Serial.println("M:STOP"); server.send(200, "text/plain", "OK"); });

  // Generic serial passthrough — lets fleet controllers (RIFT, ComCentre)
  // send any command string straight to the onboard Arduino over UART,
  // beyond the hardcoded M:*/UV:* commands above.
  server.on("/serial", []() {
    if (!server.hasArg("cmd")) { server.send(400, "text/plain", "missing 'cmd'"); return; }
    Serial.println(server.arg("cmd"));
    server.send(200, "text/plain", "OK");
  });

  // Text message board (see MESSAGE_LOG_SIZE above).
  server.on("/message", HTTP_POST, []() {
    if (!server.hasArg("text")) { server.send(400, "text/plain", "missing 'text'"); return; }
    messageLog[messageLogHead] = server.arg("text");
    messageLogHead = (messageLogHead + 1) % MESSAGE_LOG_SIZE;
    if (messageLogCount < MESSAGE_LOG_SIZE) messageLogCount++;
    server.send(200, "text/plain", "OK");
  });
  server.on("/messages", HTTP_GET, []() {
    String json = "[";
    for (int i = 0; i < messageLogCount; i++) {
      int idx = (messageLogHead - messageLogCount + i + MESSAGE_LOG_SIZE) % MESSAGE_LOG_SIZE;
      if (i > 0) json += ",";
      json += "\"" + jsonEscape(messageLog[idx]) + "\"";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  // Calibration
  server.on("/setcal", []() {
    if (server.hasArg("m1")) cal[0] = constrain(server.arg("m1").toInt(), 0, 255);
    if (server.hasArg("m2")) cal[1] = constrain(server.arg("m2").toInt(), 0, 255);
    if (server.hasArg("m3")) cal[2] = constrain(server.arg("m3").toInt(), 0, 255);
    if (server.hasArg("m4")) cal[3] = constrain(server.arg("m4").toInt(), 0, 255);
    server.send(200, "text/plain", "OK");
  });
  server.on("/setspeed", []() {
    if (server.hasArg("v")) speedPct = constrain(server.arg("v").toInt(), 0, 100);
    server.send(200, "text/plain", "OK");
  });
  server.on("/savecal", []() {
    prefs.begin("cal", false);
    prefs.putInt("m1",  cal[0]); prefs.putInt("m2",  cal[1]);
    prefs.putInt("m3",  cal[2]); prefs.putInt("m4",  cal[3]);
    prefs.putInt("spd", speedPct);
    prefs.end();
    server.send(200, "text/plain", "OK");
  });
  server.on("/getcal", []() {
    String json = "{\"m1\":"  + String(cal[0])   +
                  ",\"m2\":"  + String(cal[1])   +
                  ",\"m3\":"  + String(cal[2])   +
                  ",\"m4\":"  + String(cal[3])   +
                  ",\"spd\":" + String(speedPct) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/sensors", []() {
    String json = "{\"F\":"    + String(front_cm, 1) +
                  ",\"L\":"    + String(left_cm,  1) +
                  ",\"B\":"    + String(back_cm,  1) +
                  ",\"R\":"    + String(right_cm, 1) +
                  ",\"lfl\":"  + String(lf_l)        +
                  ",\"lfm\":"  + String(lf_m)        +
                  ",\"lfr\":"  + String(lf_r)        +
                  ",\"mode\":" + String((int)driveMode) +
                  ",\"dir\":"  + String(autoDir)     +
                  ",\"uv\":"   + String(uvMode)      +
                  ",\"mt\":"   + String(musicTrack)  +
                  ",\"ms\":"   + String(musicState)  +
                  ",\"lt\":"   + String(lightPct)    +
                  ",\"snd\":"  + String(soundRecent ? 1 : 0) +
                  ",\"lock\":" + String(motorLocked ? 1 : 0) +
                  ",\"spd\":"  + String(speedPct)    +
                  ",\"temp\":" + String(tempC, 1)    +
                  ",\"hum\":"  + String(humidity, 1) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/", handleRoot);
  server.begin();

  setupFleetServer();
}

// =====================
// LOOP
// =====================
void loop() {
  server.handleClient();
  fleetServer.handleClient();
  handleBluetooth();
  handleIR();
  readSerialSensors();

  lf_l = digitalRead(LF_LEFT);
  lf_m = digitalRead(LF_MID);
  lf_r = digitalRead(LF_RIGHT);

  unsigned long now = millis();

  // IR has no key-up event — a held button just keeps resending, and
  // releasing it simply stops the resends. So if we're driving off the IR
  // remote and haven't seen a repeat in a while, treat that as "released".
  if (driveMode == MODE_IR && irLastDriveMs != 0 && now - irLastDriveMs > IR_DRIVE_TIMEOUT_MS) {
    stopMotors();
    irLastDriveMs = 0;
  }

  // ---- Sound recency decay (the event itself is latched in parseSensorLine) ----
  soundRecent = (now - lastSoundMs) < 500;

  // ---- Single clap (confirmed once the 0.8s double-clap window passes with
  // no second clap) -> play music. Can't fire this the instant a clap is
  // seen, or it'd also fire on the first half of every double clap.
  // Guarded on musicState so it only starts/resumes -- M:PLAY toggles to
  // pause if sent while already playing, which isn't what "1 clap = play"
  // should do. ----
  if (clapCount == 1 && now - firstClapMs > 800) {
    if (musicState != 1) Serial.println("M:PLAY");   // 1 = already playing
    clapCount = 0;
  }

  // ---- Temperature / humidity (AHT10) ----
  static unsigned long lastAhtMs = 0;
  if (aht10Ok && now - lastAhtMs >= 500) {
    sensors_event_t humEvt, tempEvt;
    aht.getEvent(&humEvt, &tempEvt);
    tempC     = tempEvt.temperature;
    humidity  = humEvt.relative_humidity;
    lastAhtMs = now;
  }

  if (uvMode == 0) {
    digitalWrite(UV_PIN, LOW);
  } else if (uvMode == 1) {
    digitalWrite(UV_PIN, HIGH);
  } else if (now - lastUVBlink >= 300) {
    uvBlinkState = !uvBlinkState;
    digitalWrite(UV_PIN, uvBlinkState ? HIGH : LOW);
    lastUVBlink = now;
  }

  if      (driveMode == MODE_AUTO) AutonomousMode(front_cm, left_cm, back_cm, right_cm);
  else if (driveMode == MODE_LINE) LineFollowerMode(lf_l, lf_m, lf_r);
}

// =====================
// BLUETOOTH COMMANDS
// =====================
void handleBluetooth() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;

    if (btAwaitingPw) {
      btPwBuffer += c;
      if (btPwBuffer.length() >= 4) {
        if (btPwBuffer == MOTOR_PASSWORD) {
          motorLocked = false;
          SerialBT.println("motorlock: off");
        } else {
          SerialBT.println("motorlock: denied");
        }
        btAwaitingPw = false;
        btPwBuffer   = "";
      }
      continue;
    }

    switch (c) {
      // ---- driving (manual mode only, same rule as the web pad) ----
      case 'F': case 'f': if (driveMode == MODE_MANUAL) moveForward();  break;
      case 'B': case 'b': if (driveMode == MODE_MANUAL) moveBackward(); break;
      case 'L': case 'l': if (driveMode == MODE_MANUAL) strafeLeft();   break;
      case 'R': case 'r': if (driveMode == MODE_MANUAL) strafeRight();  break;
      case 'Q': case 'q': if (driveMode == MODE_MANUAL) turnLeft();     break;
      case 'E': case 'e': if (driveMode == MODE_MANUAL) turnRight();    break;
      case 'S': case 's': if (driveMode == MODE_MANUAL) stopMotors();   break;

      // ---- modes ----
      case '1': driveMode = MODE_MANUAL; stopMotors(); SerialBT.println("mode: manual"); break;
      case '2': driveMode = MODE_AUTO;   stopMotors(); autoState = AUTO_COOLDOWN; autoCooldown = 0; autoStuckSinceMs = 0; autoEscaping = false; SerialBT.println("mode: auto"); break;
      case '3': driveMode = MODE_LINE;   stopMotors(); lineLostSinceMs = 0; SerialBT.println("mode: line"); break;

      // ---- UV ----
      case 'U': case 'u':
        uvMode = (uvMode + 1) % 3;
        SerialBT.println(uvMode == 0 ? "uv: off" : uvMode == 1 ? "uv: on" : "uv: blink");
        break;

      // ---- motor lock: 'K' locks (free), 'V' arms unlock (next 4 chars = password) ----
      case 'K': case 'k':
        motorLocked = true;
        stopMotors();
        SerialBT.println("motorlock: on");
        break;
      case 'V': case 'v':
        btAwaitingPw = true;
        btPwBuffer   = "";
        break;

      // ---- music (forwarded to the Arduino) ----
      case 'M': case 'm': Serial.println("M:PLAY"); break;
      case 'N': case 'n': Serial.println("M:NEXT"); break;
      case 'P': case 'p': Serial.println("M:PREV"); break;
      case 'X': case 'x': Serial.println("M:STOP"); break;

      // ---- status query ----
      case '?':
        SerialBT.print("F:");    SerialBT.print(front_cm);
        SerialBT.print(" L:");   SerialBT.print(left_cm);
        SerialBT.print(" B:");   SerialBT.print(back_cm);
        SerialBT.print(" R:");   SerialBT.print(right_cm);
        SerialBT.print(" mode:"); SerialBT.print((int)driveMode);
        SerialBT.print(" dir:");  SerialBT.print(autoDir);
        SerialBT.print(" lf:");   SerialBT.print(lf_l); SerialBT.print(lf_m); SerialBT.print(lf_r);
        SerialBT.print(" track:"); SerialBT.print(musicTrack);
        SerialBT.print(" state:"); SerialBT.print(musicState);
        SerialBT.print(" light:"); SerialBT.print(lightPct);
        SerialBT.print("% sound:"); SerialBT.print(soundRecent ? "yes" : "no");
        SerialBT.print(" temp:");   SerialBT.print(tempC, 1);
        SerialBT.print("C hum:");   SerialBT.print(humidity, 1);
        SerialBT.print("% lock:");  SerialBT.println(motorLocked ? 1 : 0);
        break;

      // ---- speed nudge ----
      case '+': speedPct = constrain(speedPct + 10, 10, 100); SerialBT.print("speed: "); SerialBT.println(speedPct); break;
      case '-': speedPct = constrain(speedPct - 10, 10, 100); SerialBT.print("speed: "); SerialBT.println(speedPct); break;
    }
  }
}

// =====================
// IR REMOTE COMMANDS
// =====================
// Driving (D-pad + Enter) only takes effect in MODE_IR — in every other
// mode those buttons are inert so they can't fight the website/BT driver
// or the autonomous/line logic. Mode switching, music, and volume/mute
// always work no matter which drive mode is active.
void handleIR() {
  if (!IrReceiver.decode()) return;

  if (IrReceiver.decodedIRData.protocol != UNKNOWN) {
    bool    isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
    uint8_t cmd      = IrReceiver.decodedIRData.command;

    switch (cmd) {
      // ---- driving: IR remote mode only. Repeats (button held) keep it
      // moving; the timeout watchdog in loop() stops it on release. ----
      case IR_UP:    if (driveMode == MODE_IR) { moveForward();  irLastDriveMs = millis(); } break;
      case IR_DOWN:  if (driveMode == MODE_IR) { moveBackward(); irLastDriveMs = millis(); } break;
      case IR_LEFT:  if (driveMode == MODE_IR) { irStrafe ? strafeLeft()  : turnLeft();  irLastDriveMs = millis(); } break;
      case IR_RIGHT: if (driveMode == MODE_IR) { irStrafe ? strafeRight() : turnRight(); irLastDriveMs = millis(); } break;

      default:
        if (isRepeat) break;   // everything below only fires on the initial press

        switch (cmd) {
          case IR_ENTER: if (driveMode == MODE_IR) irStrafe = !irStrafe; break;

          // ---- modes (always allowed) ----
          case IR_1: driveMode = MODE_MANUAL; stopMotors(); break;
          case IR_2: driveMode = MODE_AUTO;   stopMotors(); autoState = AUTO_COOLDOWN; autoCooldown = 0; autoStuckSinceMs = 0; autoEscaping = false; break;
          case IR_3: driveMode = MODE_LINE;   stopMotors(); lineLostSinceMs = 0; break;
          case IR_4: driveMode = MODE_IR;     stopMotors(); irLastDriveMs = 0; break;

          // ---- music, forwarded to the Arduino (always allowed) ----
          case IR_PLAY_PAUSE: Serial.println("M:PLAY");   break;
          case IR_STOP:       Serial.println("M:STOP");   break;
          case IR_NEXT:       Serial.println("M:NEXT");   break;
          case IR_PREV:       Serial.println("M:PREV");   break;
          case IR_FFWD:       Serial.println("M:NEXT");   break;   // no seek hardware — treat as skip
          case IR_REWIND:     Serial.println("M:PREV");   break;
          case IR_REPEAT:     Serial.println("M:REPEAT"); break;

          // ---- volume, forwarded to the Arduino (always allowed) ----
          case IR_VOL_UP:   Serial.println("V:UP");   break;
          case IR_VOL_DOWN: Serial.println("V:DOWN"); break;
          case IR_MUTE:     Serial.println("V:MUTE"); break;
        }
        break;
    }
  }

  IrReceiver.resume();
}

// =====================
// READ ARDUINO SERIAL
// =====================
void readSerialSensors() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseSensorLine(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }
}

void parseSensorLine(String line) {
  auto extractFloat = [&](String key) -> float {
    int idx = line.indexOf(key + ":");
    if (idx == -1) return -1;
    int start = idx + key.length() + 1;
    int end   = line.indexOf(',', start);
    String val = (end == -1) ? line.substring(start) : line.substring(start, end);
    return val.toFloat();
  };

  front_cm = extractFloat("F");
  left_cm  = extractFloat("L");
  back_cm  = extractFloat("B");
  right_cm = extractFloat("R");

  float mt = extractFloat("MT");
  float ms = extractFloat("MS");
  if (mt >= 0) musicTrack = (int)mt;
  if (ms >= 0) musicState = (int)ms;

  float lt = extractFloat("LT");
  if (lt >= 0) lightPct = (int)lt;

  // ---- Sound event: edge-detected + debounced on the Arduino already,
  // we just run the clap-count timing off of this latched flag.
  // 1 clap  -> play music  (fired from loop() once the window times out
  //            with no second clap -- see there for why)
  // 2 claps within 0.8s -> stop music (fires immediately, no need to wait:
  //            seeing the 2nd clap already confirms it wasn't a single) ----
  if (extractFloat("SND") == 1) {
    unsigned long now = millis();
    lastSoundMs = now;
    if (clapCount == 0 || now - firstClapMs > 800) {
      clapCount   = 1;
      firstClapMs = now;
    } else {
      clapCount++;
    }
    if (clapCount >= 2) {
      Serial.println("M:STOP");
      clapCount = 0;
    }
  }
}

// =====================
// LINE FOLLOWER MODE
// =====================
// Same shape of fix as AutonomousMode(): gentler turns instead of full-speed
// pivots (less zigzag along the line), and a fail-safe stop instead of
// spinning in place forever once the line is genuinely lost.
void LineFollowerMode(int l, int m, int r) {
  const int           TURN_SCALE = 65;    // gentler than a full-speed pivot
  const unsigned long LOST_MS    = 1000;  // give up and stop after this long off-line

  int pattern = (l << 2) | (m << 1) | r;
  switch (pattern) {
    case 0b010:
    case 0b111:
    case 0b101:   // both outer sensors on line, middle off — wide line/intersection, keep going
      lineLostSinceMs = 0;
      moveForward();
      break;
    case 0b110:
    case 0b100:
      lineLastTurn    = -1;
      lineLostSinceMs = 0;
      turnLeft(TURN_SCALE);
      break;
    case 0b011:
    case 0b001:
      lineLastTurn    = 1;
      lineLostSinceMs = 0;
      turnRight(TURN_SCALE);
      break;
    case 0b000:
      if (lineLostSinceMs == 0) lineLostSinceMs = millis();
      if (millis() - lineLostSinceMs > LOST_MS) stopMotors();
      else if (lineLastTurn < 0)                turnLeft(TURN_SCALE);
      else                                       turnRight(TURN_SCALE);
      break;
    default: moveForward(); break;
  }
}

// =====================
// AUTONOMOUS MODE
// =====================
void moveDir(int dir, int scale) {
  switch (dir) {
    case DIR_FRONT: moveForward(scale);  break;
    case DIR_RIGHT: strafeRight(scale);  break;
    case DIR_BACK:  moveBackward(scale); break;
    case DIR_LEFT:  strafeLeft(scale);   break;
  }
}

int clearestDir(float dist[4]) {
  int best = 0;
  for (int d = 1; d < 4; d++)
    if (dist[d] > dist[best]) best = d;
  return best;
}

// Reactive omnidirectional avoidance. Beyond the original bang-bang
// controller, this adds:
//   - EMA smoothing per sensor so single noisy pings don't flip decisions
//   - cautious (not "infinite") handling of dropped/out-of-range readings,
//     since a failed echo can mean "too close" just as easily as "clear"
//   - a switch hysteresis margin so it won't flap between two directions
//     that read almost the same distance
//   - a slow-down band instead of full speed all the way to CRITICAL
//   - an escape turn when genuinely boxed in, instead of freezing forever
void AutonomousMode(float front, float left, float back, float right) {
  const float DANGER        = 28.0;
  const float CRITICAL      = 15.0;
  const float SWITCH_MARGIN = 8.0;   // new dir must clear the old one by this much
  const float UNKNOWN_CM    = 40.0;  // cautious guess once a sensor goes stale
  const int   CREEP_SCALE   = 55;    // % speed while inside the DANGER band
  const unsigned long STALE_MS  = 800;
  const unsigned long STUCK_MS  = 1200;
  const unsigned long ESCAPE_MS = 400;

  unsigned long now = millis();

  // ---- per-sensor smoothing with cautious dropout handling ----
  static float filtered[4]           = {UNKNOWN_CM, UNKNOWN_CM, UNKNOWN_CM, UNKNOWN_CM};
  static unsigned long lastGoodMs[4] = {0, 0, 0, 0};
  float raw[4] = { front, right, back, left };   // matches DIR_FRONT/RIGHT/BACK/LEFT

  for (int d = 0; d < 4; d++) {
    if (raw[d] > 0) {
      filtered[d] = (lastGoodMs[d] == 0) ? raw[d] : (filtered[d] * 0.5 + raw[d] * 0.5);
      lastGoodMs[d] = now;
    } else if (now - lastGoodMs[d] > STALE_MS) {
      // no valid echo in a while: could be "clear" or could be "too close to
      // hear the echo" — assume neither, treat it as merely borderline
      filtered[d] = UNKNOWN_CM;
    }
    // else: a brief dropout — keep trusting the last smoothed value
  }
  float dist[4] = { filtered[0], filtered[1], filtered[2], filtered[3] };

  // ---- boxed-in escape: turn toward the more open side instead of freezing ----
  if (autoEscaping) {
    if (now < autoEscapeUntilMs) return;
    autoEscaping = false;
    stopMotors();
    autoState       = AUTO_COOLDOWN;
    autoCooldown    = now + 150;
    autoStuckSinceMs = 0;
    return;
  }

  if (autoState == AUTO_COOLDOWN) {
    if (now < autoCooldown) return;
    int best = clearestDir(dist);
    if (dist[best] < DANGER) {
      if (autoStuckSinceMs == 0) autoStuckSinceMs = now;
      if (now - autoStuckSinceMs > STUCK_MS) {
        if (dist[DIR_LEFT] >= dist[DIR_RIGHT]) turnLeft(CREEP_SCALE);
        else                                   turnRight(CREEP_SCALE);
        autoEscaping      = true;
        autoEscapeUntilMs = now + ESCAPE_MS;
        return;
      }
      autoCooldown = now + 300;
      return;
    }
    autoStuckSinceMs = 0;
    autoDir = best;
    moveDir(autoDir, 100);
    autoState = AUTO_MOVING;
    return;
  }

  // AUTO_MOVING
  float cur = dist[autoDir];
  if (cur < CRITICAL) {
    stopMotors();
    autoState    = AUTO_COOLDOWN;
    autoCooldown = now + 250;
  } else if (cur < DANGER) {
    int best = clearestDir(dist);
    if (best != autoDir && dist[best] > dist[autoDir] + SWITCH_MARGIN && dist[best] > DANGER) {
      stopMotors();
      autoDir = best;
      moveDir(autoDir, 100);
      autoState    = AUTO_COOLDOWN;
      autoCooldown = now + 150;
    } else {
      moveDir(autoDir, CREEP_SCALE);   // getting close — creep instead of full speed
    }
  } else {
    moveDir(autoDir, 100);             // clear again — resume full speed
  }
}

// =====================
// FLEET REGISTRY LOGIC
// =====================
void fleetPrune() {
  unsigned long now = millis();
  for (int i = 0; i < FLEET_MAX; i++) {
    if (fleet[i].used && now - fleet[i].lastSeenMs > FLEET_TTL_MS) fleet[i].used = false;
  }
  if (authorityExpiresMs != 0 && now > authorityExpiresMs) {
    authorityName      = "NORA";   // authority's heartbeat lapsed — reclaim the role
    authorityExpiresMs = 0;
  }
}

void fleetRegister(const String &name, const String &type, const String &caps, IPAddress ip) {
  unsigned long now = millis();
  int slot = -1;
  for (int i = 0; i < FLEET_MAX; i++) {
    if (fleet[i].used && fleet[i].name == name) { slot = i; break; }   // re-register = heartbeat
  }
  if (slot == -1) {
    for (int i = 0; i < FLEET_MAX; i++) if (!fleet[i].used) { slot = i; break; }
  }
  if (slot == -1) return;   // registry full — stale entries free up via TTL on their own

  fleet[slot].used         = true;
  fleet[slot].name         = name;
  fleet[slot].type         = type;
  fleet[slot].capabilities = caps;
  fleet[slot].ip           = ip;
  fleet[slot].lastSeenMs   = now;

  if (type == "fleet_manager") {
    authorityName      = name;
    authorityExpiresMs = now + FLEET_TTL_MS;
  }
}

// "a, b,c" -> ["a","b","c"]
String fleetCapsToJsonArray(const String &caps) {
  String out = "[";
  int  start = 0;
  bool first = true;
  while (start <= (int)caps.length()) {
    int comma = caps.indexOf(',', start);
    String item = (comma == -1) ? caps.substring(start) : caps.substring(start, comma);
    item.trim();
    if (item.length()) {
      if (!first) out += ",";
      out += "\"" + item + "\"";
      first = false;
    }
    if (comma == -1) break;
    start = comma + 1;
  }
  out += "]";
  return out;
}

void setupFleetServer() {
  fleetServer.on("/ping", []() {
    fleetServer.send(200, "text/plain", "NORA alive");
  });

  fleetServer.on("/register", HTTP_POST, []() {
    String name = fleetServer.hasArg("name") ? fleetServer.arg("name") : "";
    String type = fleetServer.hasArg("type") ? fleetServer.arg("type") : "unknown";
    String caps = fleetServer.hasArg("capabilities") ? fleetServer.arg("capabilities") : "";
    if (name.length() == 0) {
      fleetServer.send(400, "text/plain", "missing 'name'");
      return;
    }
    fleetRegister(name, type, caps, fleetServer.client().remoteIP());
    fleetServer.send(200, "text/plain", "OK");
  });

  fleetServer.on("/robots", HTTP_GET, []() {
    fleetPrune();

    String json = "{\"authority\":\"" + authorityName + "\",\"robots\":[";
    json += "{\"name\":\"NORA\",\"type\":\"omni\",\"ip\":\"" + WiFi.softAPIP().toString() +
            "\",\"capabilities\":[\"obstacle_avoidance\",\"line_follow\",\"uv_light\",\"music\"]}";

    // While someone else is the live authority, don't also author a
    // competing roster — just point callers at her. Entries registered
    // during that time are still tracked quietly so there's no gap the
    // instant NORA reclaims the role.
    if (authorityExpiresMs == 0) {
      for (int i = 0; i < FLEET_MAX; i++) {
        if (!fleet[i].used) continue;
        json += ",{\"name\":\"" + fleet[i].name + "\",\"type\":\"" + fleet[i].type +
                "\",\"ip\":\"" + fleet[i].ip.toString() +
                "\",\"capabilities\":" + fleetCapsToJsonArray(fleet[i].capabilities) + "}";
      }
    }

    json += "]}";
    fleetServer.send(200, "application/json", json);
  });

  fleetServer.begin();
}

// =====================
// WEB PAGE (stored in flash, not RAM)
// =====================
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>NORA Control</title>
  <style>
    :root {
      /* same palette as KIDA's HUD (styles.css) — one look across the fleet */
      --bg:          #07090f;
      --panel:       #0a0e1a;
      --border:      #1e3458;
      --accent:      #6496e6;
      --accent-rgb:  100,150,230;
      --text:        #a0b8e8;
      --text-dim:    #4a5f80;
      --text-val:    #d7e6ff;
      --green:       #46d764;
      --green-rgb:   70,215,100;
      --orange:      #ffbe50;
      --orange-rgb:  255,190,80;
      --red:         #c84040;
      --red-rgb:     200,64,64;
      --radius:      6px;
      --font:        'Courier New', Courier, monospace;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg); color: var(--text);
      font-family: var(--font);
      display: flex; flex-direction: column; align-items: center;
      min-height: 100vh; padding: 20px; gap: 16px;
    }
    h1 { font-size: 1.6rem; letter-spacing: 3px; color: var(--accent); }
    h2 { font-size: 0.8rem; color: var(--text-dim); letter-spacing: 1px; }

    #sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; width: 100%; max-width: 340px; }
    .sensor-box {
      background: var(--panel); border: 2px solid var(--border); border-radius: var(--radius);
      padding: 8px 12px; font-size: 0.85rem;
      display: flex; flex-direction: column; gap: 4px;
      transition: border-color 0.3s, background 0.3s;
    }
    .sensor-top { display: flex; justify-content: space-between; }
    .sensor-box span { color: var(--accent); font-weight: bold; }
    .sensor-box.warn  { border-color: var(--orange); }
    .sensor-box.crit  { border-color: var(--red); background: rgba(var(--red-rgb), 0.12); }
    .sensor-box.travel { border-color: var(--accent); background: rgba(var(--accent-rgb), 0.12); }
    .dist-bar-bg { background: rgba(var(--accent-rgb), 0.1); border-radius: 3px; height: 4px; overflow: hidden; }
    .dist-bar    { height: 4px; border-radius: 3px; background: var(--accent); transition: width 0.3s, background 0.3s; width: 0%; }

    #lfRow { display: flex; gap: 8px; align-items: center; font-size: 0.8rem; color: var(--text-dim); }
    .lf-dot {
      width: 20px; height: 20px; border-radius: 50%;
      background: var(--panel); border: 2px solid var(--border); transition: background 0.1s, border-color 0.1s;
    }
    .lf-dot.on { background: var(--accent); border-color: var(--accent); }

    #envRow { display: flex; gap: 6px; align-items: center; font-size: 0.8rem; color: var(--text-dim); }
    #envRow span { color: var(--accent); font-weight: bold; }

    #modeRow { display: flex; gap: 8px; flex-wrap: wrap; justify-content: center; }
    .mode-btn {
      padding: 10px 18px; border: 2px solid var(--border); border-radius: var(--radius);
      font-size: 0.85rem; font-weight: bold; cursor: pointer;
      background: var(--panel); color: var(--text-dim);
    }
    .mode-btn.active { border-color: var(--accent); color: var(--accent); background: rgba(var(--accent-rgb), 0.12); }

    #uvBtn {
      padding: 9px 20px; border: 2px solid var(--border); border-radius: var(--radius);
      font-size: 0.85rem; font-weight: bold; cursor: pointer;
      background: var(--panel); color: var(--text-dim); transition: all 0.2s;
    }
    #uvBtn.on    { border-color: var(--orange); color: var(--orange); background: rgba(var(--orange-rgb), 0.12); }
    #uvBtn.blink { border-color: var(--red);    color: var(--red);    background: rgba(var(--red-rgb), 0.12); }

    #lockBtn {
      padding: 9px 20px; border: 2px solid var(--border); border-radius: var(--radius);
      font-size: 0.85rem; font-weight: bold; cursor: pointer;
      background: var(--panel); color: var(--text-dim); transition: all 0.2s;
    }
    #lockBtn.locked   { border-color: var(--red);   color: var(--red);   background: rgba(var(--red-rgb), 0.12); }
    #lockBtn.unlocked { border-color: var(--green); color: var(--green); background: rgba(var(--green-rgb), 0.12); }

    /* Music player */
    #musicPanel {
      width: 100%; max-width: 340px;
      background: var(--panel); border: 1px solid var(--border); border-radius: var(--radius);
      padding: 12px 14px; display: flex; flex-direction: column; gap: 10px;
    }
    #musicTop { display: flex; justify-content: space-between; align-items: center; font-size: 0.8rem; color: var(--text-dim); }
    #musicTrack { color: var(--accent); font-weight: bold; }
    #musicState { color: var(--text-dim); }
    #musicState.playing { color: var(--green); }
    #musicState.paused  { color: var(--orange); }
    #musicBtns { display: flex; gap: 8px; justify-content: center; }
    .mu-btn {
      flex: 1; padding: 10px 0; border: 2px solid var(--border); border-radius: var(--radius);
      background: var(--panel); color: var(--text); font-size: 1rem; cursor: pointer;
      -webkit-tap-highlight-color: transparent;
    }
    .mu-btn:active { border-color: var(--accent); color: var(--accent); }
    .mu-btn.stopBtn { color: var(--red); }

    #speedRow {
      display: flex; align-items: center; gap: 10px;
      width: 100%; max-width: 340px; font-size: 0.85rem; color: var(--text-dim);
    }
    #speedRow input[type=range] { flex: 1; accent-color: var(--accent); }
    #speedVal { width: 36px; text-align: right; color: var(--accent); font-weight: bold; }

    .dpad {
      display: grid;
      grid-template-columns: repeat(3, 80px);
      grid-template-rows: repeat(3, 80px);
      gap: 8px;
    }
    .btn {
      background: var(--panel); border: 2px solid var(--border); border-radius: var(--radius);
      color: var(--text-val); font-size: 1.5rem; cursor: pointer; user-select: none;
      display: flex; align-items: center; justify-content: center;
      -webkit-tap-highlight-color: transparent; transition: opacity 0.2s;
      touch-action: none;
    }
    .btn.disabled { opacity: 0.25; pointer-events: none; }
    .btn.pressed  { background: rgba(var(--accent-rgb), 0.25); border-color: var(--accent); }
    .btn.stop-btn { background: rgba(var(--red-rgb), 0.15); border-color: var(--red); font-size: 0.8rem; font-weight: bold; color: var(--red); }
    .btn.stop-btn.pressed { background: rgba(var(--red-rgb), 0.3); }
    .fw    { grid-column: 2; grid-row: 1; }
    .turnL { grid-column: 1; grid-row: 2; }
    .left  { grid-column: 1; grid-row: 3; }
    .stop  { grid-column: 2; grid-row: 2; }
    .bw    { grid-column: 2; grid-row: 3; }
    .turnR { grid-column: 3; grid-row: 2; }
    .right { grid-column: 3; grid-row: 3; }
    #status { font-size: 0.8rem; color: var(--text-dim); }

    #calPanel {
      width: 100%; max-width: 340px;
      background: var(--panel); border: 1px solid var(--border); border-radius: var(--radius);
      padding: 14px; display: flex; flex-direction: column; gap: 10px;
    }
    #calPanel summary { font-size: 0.85rem; color: var(--accent); cursor: pointer; user-select: none; font-weight: bold; }
    .cal-row { display: flex; align-items: center; gap: 10px; font-size: 0.8rem; }
    .cal-row label { width: 24px; color: var(--text-dim); }
    .cal-row input[type=range] { flex: 1; accent-color: var(--accent); }
    .cal-row span { width: 30px; text-align: right; color: var(--accent); font-weight: bold; }
    #saveCalBtn {
      align-self: flex-end; padding: 6px 16px; background: var(--green); border: none;
      border-radius: var(--radius); color: var(--bg); font-size: 0.8rem; font-weight: bold; cursor: pointer;
    }
    #saveCalBtn.saved { background: var(--text-dim); }
  </style>
</head>
<body>
  <h1>NORA</h1>
  <h2>Nomadic Omnidirectional Reactive Automaton</h2>

  <div id="sensors">
    <div class="sensor-box" id="boxF">
      <div class="sensor-top">Front <span id="sF">--</span> cm</div>
      <div class="dist-bar-bg"><div class="dist-bar" id="barF"></div></div>
    </div>
    <div class="sensor-box" id="boxB">
      <div class="sensor-top">Back <span id="sB">--</span> cm</div>
      <div class="dist-bar-bg"><div class="dist-bar" id="barB"></div></div>
    </div>
    <div class="sensor-box" id="boxL">
      <div class="sensor-top">Left <span id="sL">--</span> cm</div>
      <div class="dist-bar-bg"><div class="dist-bar" id="barL"></div></div>
    </div>
    <div class="sensor-box" id="boxR">
      <div class="sensor-top">Right <span id="sR">--</span> cm</div>
      <div class="dist-bar-bg"><div class="dist-bar" id="barR"></div></div>
    </div>
  </div>

  <div id="lfRow">
    Line:
    <div class="lf-dot" id="lfL"></div>
    <div class="lf-dot" id="lfM"></div>
    <div class="lf-dot" id="lfR"></div>
    &nbsp;&nbsp;Light: <span id="lightVal" style="color:var(--accent);font-weight:bold">--%</span>
    &nbsp;&nbsp;<div class="lf-dot" id="sndDot" title="sound"></div>
  </div>

  <div id="envRow">
    Temp: <span id="tempVal">--</span>&deg;C
    &nbsp;&nbsp;Humidity: <span id="humVal">--</span>%
  </div>

  <div id="modeRow">
    <button class="mode-btn active" id="btnManual" onclick="setMode('Manual')">MANUAL</button>
    <button class="mode-btn"        id="btnAuto"   onclick="setMode('Auto')">AUTO</button>
    <button class="mode-btn"        id="btnLine"   onclick="setMode('Line')">LINE</button>
  </div>

  <button id="uvBtn" onclick="cycleUV()">UV OFF</button>
  <button id="lockBtn" class="locked" onclick="toggleLock()">MOTOR LOCK: ON</button>

  <div id="musicPanel">
    <div id="musicTop">
      <span>MUSIC</span>
      <span><span id="musicTrack">track---</span> &nbsp; <span id="musicState">stopped</span></span>
    </div>
    <div id="musicBtns">
      <button class="mu-btn" onclick="cmd('/muPrev')">&#9198;</button>
      <button class="mu-btn" onclick="cmd('/muPlay')">&#9199;</button>
      <button class="mu-btn" onclick="cmd('/muNext')">&#9197;</button>
      <button class="mu-btn stopBtn" onclick="cmd('/muStop')">&#9209;</button>
    </div>
  </div>

  <div id="speedRow">
    Speed
    <input id="speedSlider" type="range" min="0" max="100" value="100" oninput="setSpeed(this)">
    <span id="speedVal">100%</span>
  </div>

  <div class="dpad">
    <div class="btn fw"    data-cmd="/fw">&#9650;</div>
    <div class="btn turnL" data-cmd="/turnL">&#8634;</div>
    <div class="btn stop-btn stop" data-cmd="/stop">STOP</div>
    <div class="btn bw"    data-cmd="/bw">&#9660;</div>
    <div class="btn left"  data-cmd="/left">&#9668;</div>
    <div class="btn turnR" data-cmd="/turnR">&#8635;</div>
    <div class="btn right" data-cmd="/right">&#9658;</div>
  </div>
  <div id="status">Idle</div>

  <details id="calPanel">
    <summary>Wheel Calibration</summary>
    <div class="cal-row"><label>M1</label><input id="sl1" type="range" min="0" max="255" value="255" oninput="setCal(this,0,'v1')"><span id="v1">255</span></div>
    <div class="cal-row"><label>M2</label><input id="sl2" type="range" min="0" max="255" value="255" oninput="setCal(this,1,'v2')"><span id="v2">255</span></div>
    <div class="cal-row"><label>M3</label><input id="sl3" type="range" min="0" max="255" value="255" oninput="setCal(this,2,'v3')"><span id="v3">255</span></div>
    <div class="cal-row"><label>M4</label><input id="sl4" type="range" min="0" max="255" value="255" oninput="setCal(this,3,'v4')"><span id="v4">255</span></div>
    <button id="saveCalBtn" onclick="saveCal()">Save</button>
  </details>

<script>
  let activeCmd = null, cmdInterval = null;
  const calVals  = [255,255,255,255];
  let calTimer   = null;
  let speedTimer = null;
  let uvState    = 0;
  let currentMode = 'Manual';
  let motorLocked = true;   // matches firmware's boot-time default

  const dirBox = ['boxF', 'boxR', 'boxB', 'boxL'];

  function cmd(c) { fetch(c).catch(() => {}); }

  const modeNames = ['Manual', 'Auto', 'Line', 'IR'];

  // Applies a mode to the UI only -- used both when we initiate a change
  // (setMode) and when we're just reflecting one that happened elsewhere
  // (IR remote, pygame controller) via the /sensors poll.
  function applyModeUI(mode) {
    currentMode = mode;
    ['Manual','Auto','Line'].forEach(m => {
      const btn = document.getElementById('btn' + m);
      if (btn) btn.classList.toggle('active', m === mode);
    });
    updateDpadEnabled();
  }

  function setMode(mode) {
    cmd('/mode' + mode);
    applyModeUI(mode);
  }

  function updateDpadEnabled() {
    const enabled = currentMode === 'Manual' && !motorLocked;
    document.querySelectorAll('.btn').forEach(b => b.classList.toggle('disabled', !enabled));
  }

  function applyLock() {
    const btn = document.getElementById('lockBtn');
    btn.textContent = motorLocked ? 'MOTOR LOCK: ON' : 'MOTOR LOCK: OFF';
    btn.className   = motorLocked ? 'locked' : 'unlocked';
    updateDpadEnabled();
  }

  function toggleLock() {
    if (motorLocked) {
      const pw = prompt('Enter password to unlock motors:');
      if (pw === null) return;
      fetch('/motorlockOff?pw=' + encodeURIComponent(pw)).then(r => {
        if (r.ok) { motorLocked = false; applyLock(); }
        else { alert('Wrong password — motors stay locked.'); }
      }).catch(() => {});
    } else {
      fetch('/motorlockOn').then(() => { motorLocked = true; applyLock(); }).catch(() => {});
    }
  }

  const uvLabels   = ['UV OFF', 'UV ON', 'UV BLINK'];
  const uvClasses  = ['', 'on', 'blink'];
  const uvRoutes   = ['/uvOff', '/uvOn', '/uvBlink'];

  function cycleUV() {
    uvState = (uvState + 1) % 3;
    applyUV();
    cmd(uvRoutes[uvState]);
  }

  function applyUV() {
    const btn = document.getElementById('uvBtn');
    btn.textContent = uvLabels[uvState];
    btn.className   = uvClasses[uvState];
  }

  function setSpeed(slider) {
    document.getElementById('speedVal').textContent = slider.value + '%';
    clearTimeout(speedTimer);
    speedTimer = setTimeout(() => cmd('/setspeed?v=' + slider.value), 200);
  }

  const speedPresets = [25, 50, 75, 100];
  let speedIndex = 3;   // matches the slider's hardcoded default of 100%

  function cycleSpeed() {
    speedIndex = (speedIndex + 1) % speedPresets.length;
    const v = speedPresets[speedIndex];
    document.getElementById('speedSlider').value = v;
    document.getElementById('speedVal').textContent = v + '%';
    cmd('/setspeed?v=' + v);
  }

  function setCal(slider, idx, labelId) {
    calVals[idx] = parseInt(slider.value);
    document.getElementById(labelId).textContent = calVals[idx];
    clearTimeout(calTimer);
    calTimer = setTimeout(() =>
      fetch(`/setcal?m1=${calVals[0]}&m2=${calVals[1]}&m3=${calVals[2]}&m4=${calVals[3]}`).catch(() => {})
    , 200);
  }

  function saveCal() {
    fetch('/savecal').then(() => {
      const btn = document.getElementById('saveCalBtn');
      btn.textContent = 'Saved!'; btn.classList.add('saved');
      setTimeout(() => { btn.textContent = 'Save'; btn.classList.remove('saved'); }, 1500);
    }).catch(() => {});
  }

  fetch('/getcal').then(r => r.json()).then(d => {
    [d.m1, d.m2, d.m3, d.m4].forEach((v, i) => {
      calVals[i] = v;
      document.getElementById('sl' + (i+1)).value = v;
      document.getElementById('v'  + (i+1)).textContent = v;
    });
    document.getElementById('speedSlider').value = d.spd;
    document.getElementById('speedVal').textContent = d.spd + '%';
  }).catch(() => {});

  function startCmd(c) {
    if (activeCmd === c) return;
    stopCmd();
    activeCmd = c;
    cmd(c);
    document.getElementById('status').textContent = c.replace('/', '').toUpperCase();
    cmdInterval = setInterval(() => cmd(c), 150);
  }

  function stopCmd() {
    if (activeCmd) {
      clearInterval(cmdInterval);
      activeCmd = null;
      cmd('/stop');
      document.getElementById('status').textContent = 'Idle';
    }
  }

  document.querySelectorAll('.btn').forEach(btn => {
    const c = btn.dataset.cmd;
    btn.addEventListener('mousedown',  e => { e.preventDefault(); c === '/stop' ? cmd('/stop') : startCmd(c); btn.classList.add('pressed'); });
    btn.addEventListener('mouseup',    e => { e.preventDefault(); if (c !== '/stop') stopCmd(); btn.classList.remove('pressed'); });
    btn.addEventListener('mouseleave', e => { if (activeCmd === c && c !== '/stop') stopCmd(); btn.classList.remove('pressed'); });
    btn.addEventListener('touchstart', e => { e.preventDefault(); c === '/stop' ? cmd('/stop') : startCmd(c); btn.classList.add('pressed'); }, { passive: false });
    btn.addEventListener('touchend',   e => { e.preventDefault(); if (c !== '/stop') stopCmd(); btn.classList.remove('pressed'); });
  });

  document.addEventListener('mouseup',  () => stopCmd());
  document.addEventListener('touchend', () => stopCmd());

  const keyMap = {
    'arrowup':'/fw', 'arrowdown':'/bw', 'arrowleft':'/left', 'arrowright':'/right',
    'w':'/fw', 's':'/bw', 'a':'/left', 'd':'/right',
    'q':'/turnL', 'e':'/turnR'
  };
  document.addEventListener('keydown', e => {
    const k = e.key.toLowerCase();

    // ---- always-allowed controls: mirror the physical remote, where
    // music/UV/mode switching work no matter which drive mode is active ----
    if (k === ' ') { e.preventDefault(); cmd('/muPlay'); return; }
    if (k === 'm') { e.preventDefault(); cmd('/muNext'); return; }
    if (k === 'u') { e.preventDefault(); cycleUV();      return; }
    if (k === 'x') { e.preventDefault(); cycleSpeed();   return; }
    if (k === '1') { e.preventDefault(); setMode('Manual'); return; }
    if (k === '2') { e.preventDefault(); setMode('Auto');   return; }
    if (k === '3') { e.preventDefault(); setMode('Line');   return; }
    if (k === '4') { e.preventDefault(); setMode('IR');     return; }

    // ---- driving: Manual mode only, same rule as the D-pad ----
    const c = keyMap[k];
    if (!c || currentMode !== 'Manual') return;
    e.preventDefault();
    startCmd(c);
  });
  document.addEventListener('keyup', e => { if (keyMap[e.key.toLowerCase()]) stopCmd(); });

  function updateSensor(boxId, barId, spanId, value) {
    const box = document.getElementById(boxId);
    const bar = document.getElementById(barId);
    document.getElementById(spanId).textContent = value > 0 ? value : '--';

    box.classList.remove('warn', 'crit');
    if (value > 0 && value < 15)  box.classList.add('crit');
    else if (value > 0 && value < 28) box.classList.add('warn');

    const pct = value > 0 ? Math.min(value / 100 * 100, 100) : 0;
    bar.style.width = pct + '%';
    bar.style.background = value > 0 && value < 15 ? '#c84040' : value > 0 && value < 28 ? '#ffbe50' : '#6496e6';
  }

  const msLabels  = ['stopped', 'playing', 'paused'];
  const msClasses = ['', 'playing', 'paused'];

  function updateSensors() {
    fetch('/sensors').then(r => r.json()).then(d => {
      updateSensor('boxF', 'barF', 'sF', d.F);
      updateSensor('boxB', 'barB', 'sB', d.B);
      updateSensor('boxL', 'barL', 'sL', d.L);
      updateSensor('boxR', 'barR', 'sR', d.R);

      document.getElementById('lfL').classList.toggle('on', d.lfl === 1);
      document.getElementById('lfM').classList.toggle('on', d.lfm === 1);
      document.getElementById('lfR').classList.toggle('on', d.lfr === 1);

      document.getElementById('lightVal').textContent = d.lt + '%';
      document.getElementById('sndDot').classList.toggle('on', d.snd === 1);

      document.getElementById('tempVal').textContent = d.temp >= 0 ? d.temp : '--';
      document.getElementById('humVal').textContent  = d.hum  >= 0 ? d.hum  : '--';

      dirBox.forEach(id => document.getElementById(id).classList.remove('travel'));
      if (d.mode === 1) document.getElementById(dirBox[d.dir]).classList.add('travel');

      // Reflect state that may have changed from elsewhere -- the IR remote,
      // the pygame controller -- so this page never shows a stale mode/UV/
      // speed after someone else drove the change. Only *apply* the UI here,
      // never re-send the command (that would just echo it back out).
      const srvMode = modeNames[d.mode];
      if (srvMode && srvMode !== currentMode) applyModeUI(srvMode);

      if (d.uv !== uvState) { uvState = d.uv; applyUV(); }

      if (typeof d.spd === 'number' && d.spd !== speedPresets[speedIndex]) {
        const nearest = speedPresets.reduce((best, v, i) =>
          Math.abs(v - d.spd) < Math.abs(speedPresets[best] - d.spd) ? i : best, 0);
        speedIndex = nearest;
        document.getElementById('speedSlider').value = d.spd;
        document.getElementById('speedVal').textContent = d.spd + '%';
      }

      const lockState = d.lock === 1;
      if (lockState !== motorLocked) { motorLocked = lockState; applyLock(); }

      document.getElementById('musicTrack').textContent = 'track' + String(d.mt).padStart(3, '0');
      const st = document.getElementById('musicState');
      st.textContent = msLabels[d.ms]  || 'stopped';
      st.className   = msClasses[d.ms] || '';
    }).catch(() => {});
  }
  setInterval(updateSensors, 500);
  updateSensors();
</script>
</body>
</html>
)rawhtml";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// =====================
// MOVEMENT FUNCTIONS
// =====================
void moveForward(int scale)  { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2],scale); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3],scale); }
void moveBackward(int scale) { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2],scale); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3],scale); }
void strafeLeft(int scale)   { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2],scale); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3],scale); }
void strafeRight(int scale)  { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2],scale); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3],scale); }
void turnLeft(int scale)     { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2],scale); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3],scale); }
void turnRight(int scale)    { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0],scale);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1],scale);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2],scale); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3],scale); }

void stopMotors() {
  analogWrite(ENA1, 0); digitalWrite(M1_1, LOW); digitalWrite(M1_2, LOW);
  analogWrite(ENA2, 0); digitalWrite(M2_1, LOW); digitalWrite(M2_2, LOW);
  analogWrite(ENB1, 0); digitalWrite(M3_1, LOW); digitalWrite(M3_2, LOW);
  analogWrite(ENB2, 0); digitalWrite(M4_1, LOW); digitalWrite(M4_2, LOW);
}

void setMotor(int ena, int pin1, int pin2, int s1, int s2, int speed, int scale) {
  if (motorLocked) {
    analogWrite(ena, 0);
    digitalWrite(pin1, LOW);
    digitalWrite(pin2, LOW);
    return;
  }
  analogWrite(ena, (speed * speedPct * scale) / 10000);
  digitalWrite(pin1, s1);
  digitalWrite(pin2, s2);
}
