#include <WiFi.h>
#include <WebServer.h>

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

bool autoMode = false;

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

// =====================
// SENSOR VALUES (from Arduino via hardware Serial RX pin)
// =====================
float front_cm = -1, left_cm = -1, back_cm = -1, right_cm = -1;
String serialBuffer = "";

// =====================
// SETUP
// =====================
void setup() {
  // Single Serial port — receives from Arduino, no debug prints
  Serial.begin(9600);

  WiFi.softAP(ap_ssid, ap_password);

  // Motor pins
  pinMode(ENA1, OUTPUT); pinMode(M1_1, OUTPUT); pinMode(M1_2, OUTPUT);
  pinMode(ENA2, OUTPUT); pinMode(M2_1, OUTPUT); pinMode(M2_2, OUTPUT);
  pinMode(ENB1, OUTPUT); pinMode(M3_1, OUTPUT); pinMode(M3_2, OUTPUT);
  pinMode(ENB2, OUTPUT); pinMode(M4_1, OUTPUT); pinMode(M4_2, OUTPUT);

  digitalWrite(ENA1, HIGH);
  digitalWrite(ENA2, HIGH);
  digitalWrite(ENB1, HIGH);
  digitalWrite(ENB2, HIGH);

  // Routes
  server.on("/",        handleRoot);
  server.on("/fw",      []() { if (!autoMode) moveForward();   server.send(200, "text/plain", "OK"); });
  server.on("/bw",      []() { if (!autoMode) moveBackward();  server.send(200, "text/plain", "OK"); });
  server.on("/left",    []() { if (!autoMode) strafeLeft();    server.send(200, "text/plain", "OK"); });
  server.on("/right",   []() { if (!autoMode) strafeRight();   server.send(200, "text/plain", "OK"); });
  server.on("/turnL",   []() { if (!autoMode) turnLeft();      server.send(200, "text/plain", "OK"); });
  server.on("/turnR",   []() { if (!autoMode) turnRight();     server.send(200, "text/plain", "OK"); });
  server.on("/stop",    []() { if (!autoMode) stopMotors();    server.send(200, "text/plain", "OK"); });
  server.on("/autoOn",  []() { autoMode = true;                server.send(200, "text/plain", "OK"); });
  server.on("/autoOff", []() { autoMode = false; stopMotors(); autoState = AUTO_COOLDOWN; autoCooldown = 0; server.send(200, "text/plain", "OK"); });

  server.on("/sensors", []() {
    String json = "{\"F\":" + String(front_cm, 1) +
                  ",\"L\":" + String(left_cm,  1) +
                  ",\"B\":" + String(back_cm,  1) +
                  ",\"R\":" + String(right_cm, 1) + "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  autoMode = false;
}

// =====================
// LOOP
// =====================
void loop() {
  server.handleClient();
  readSerialSensors();

  if (autoMode) {
    AutonomousMode(front_cm, left_cm, back_cm, right_cm);
  }
}

// =====================
// READ ARDUINO SERIAL
// Arduino SoftwareSerial pin 9 (TX) → ESP32 RX pin, both 9600 baud
// Expects lines like: F:23.4,L:10.1,B:45.0,R:8.3
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
  auto extractVal = [&](String key) -> float {
    int idx = line.indexOf(key + ":");
    if (idx == -1) return -1;
    int start = idx + key.length() + 1;
    int end = line.indexOf(',', start);
    String val = (end == -1) ? line.substring(start) : line.substring(start, end);
    return val.toFloat();
  };
  front_cm = extractVal("F");
  left_cm  = extractVal("L");
  back_cm  = extractVal("B");
  right_cm = extractVal("R");
}

// =====================
// AUTONOMOUS MODE
// =====================
// NORA is omnidirectional — no preferred forward direction.
// All 4 sides are equal candidates. The robot picks the clearest
// side and drives that way, re-evaluating only when blocked.
// Non-blocking: uses millis() instead of delay() so the web server
// stays responsive and movement is smooth.

void moveDir(int dir) {
  switch (dir) {
    case DIR_FRONT: moveForward();  break;
    case DIR_RIGHT: strafeRight();  break;
    case DIR_BACK:  moveBackward(); break;
    case DIR_LEFT:  strafeLeft();   break;
  }
}

int clearestDir(float dist[4]) {
  int best = 0;
  for (int d = 1; d < 4; d++)
    if (dist[d] > dist[best]) best = d;
  return best;
}

void AutonomousMode(float front, float left, float back, float right) {
  const float DANGER   = 28.0;
  const float CRITICAL = 15.0;

  auto safe = [](float v) -> float { return (v < 0) ? 999.0 : v; };
  float dist[4] = { safe(front), safe(right), safe(back), safe(left) };
  unsigned long now = millis();

  if (autoState == AUTO_COOLDOWN) {
    if (now < autoCooldown) return;
    int best = clearestDir(dist);
    if (dist[best] < DANGER) { autoCooldown = now + 300; return; }  // boxed in, wait
    autoDir = best;
    moveDir(autoDir);
    autoState = AUTO_MOVING;
    return;
  }

  // AUTO_MOVING: watch the current travel direction only
  float cur = dist[autoDir];
  if (cur < CRITICAL) {
    // Hard stop — too close
    stopMotors();
    autoState = AUTO_COOLDOWN;
    autoCooldown = now + 250;
  } else if (cur < DANGER) {
    // Getting close — switch to clearest available direction
    int best = clearestDir(dist);
    if (best != autoDir && dist[best] > DANGER) {
      stopMotors();
      autoDir = best;
      moveDir(autoDir);
      autoState = AUTO_COOLDOWN;
      autoCooldown = now + 150;
    }
  }
  // else: path clear, motors already running — nothing to do
}

// =====================
// WEB PAGE
// =====================
void handleRoot() {
  String page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>NORA: Nomadic Omnidirectional Reactive Automaton - Control</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #111; color: #eee;
      font-family: 'Segoe UI', sans-serif;
      display: flex; flex-direction: column; align-items: center;
      min-height: 100vh; padding: 20px; gap: 20px; touch-action: none;
    }
    h1 { font-size: 1.6rem; letter-spacing: 3px; color: #0af; }
    #sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; width: 100%; max-width: 340px; }
    .sensor-box {
      background: #1e1e1e; border: 1px solid #333; border-radius: 8px;
      padding: 8px 12px; font-size: 0.85rem; display: flex; justify-content: space-between;
    }
    .sensor-box span { color: #0af; font-weight: bold; }
    #modeRow { display: flex; gap: 10px; }
    .mode-btn { padding: 10px 22px; border: none; border-radius: 8px; font-size: 0.9rem; font-weight: bold; cursor: pointer; }
    #btnAutoOn  { background: #0a5; color: #fff; }
    #btnAutoOff { background: #a00; color: #fff; }
    .dpad {
      display: grid;
      grid-template-columns: repeat(3, 80px);
      grid-template-rows: repeat(3, 80px);
      gap: 8px;
    }
    .btn {
      background: #1e1e1e; border: 2px solid #333; border-radius: 12px;
      color: #eee; font-size: 1.5rem; cursor: pointer; user-select: none;
      display: flex; align-items: center; justify-content: center;
      -webkit-tap-highlight-color: transparent;
    }
    .btn.pressed { background: #004466; border-color: #0af; }
    .btn.stop-btn { background: #2a0000; border-color: #a00; font-size: 0.8rem; font-weight: bold; color: #f55; }
    .btn.stop-btn.pressed { background: #500; }
    .fw    { grid-column: 2; grid-row: 1; }
    .turnL { grid-column: 1; grid-row: 2; }
    .left  { grid-column: 1; grid-row: 3; }
    .stop  { grid-column: 2; grid-row: 2; }
    .bw    { grid-column: 2; grid-row: 3; }
    .turnR { grid-column: 3; grid-row: 2; }
    .right { grid-column: 3; grid-row: 3; }
    #status { font-size: 0.8rem; color: #555; }
  </style>
</head>
<body>
  <h1>NORA</h1>
  <h2>Nomadic Omnidirectional Reactive Automaton</h2>
  <div id="sensors">
    <div class="sensor-box">Front <span id="sF">--</span> cm</div>
    <div class="sensor-box">Back  <span id="sB">--</span> cm</div>
    <div class="sensor-box">Left  <span id="sL">--</span> cm</div>
    <div class="sensor-box">Right <span id="sR">--</span> cm</div>
  </div>
  <div id="modeRow">
    <button class="mode-btn" id="btnAutoOn"  onclick="cmd('/autoOn')">AUTO ON</button>
    <button class="mode-btn" id="btnAutoOff" onclick="cmd('/autoOff')">AUTO OFF</button>
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
<script>
  let activeCmd = null, cmdInterval = null;

  function cmd(c) { fetch(c).catch(() => {}); }

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

  const keyMap = { 'ArrowUp':'/fw','ArrowDown':'/bw','ArrowLeft':'/left','ArrowRight':'/right','a':'/turnL','d':'/turnR',' ':'/stop' };
  document.addEventListener('keydown', e => { const c = keyMap[e.key]; if (!c) return; e.preventDefault(); c === '/stop' ? cmd('/stop') : startCmd(c); });
  document.addEventListener('keyup',   e => { if (keyMap[e.key] && keyMap[e.key] !== '/stop') stopCmd(); });

  function updateSensors() {
    fetch('/sensors').then(r => r.json()).then(d => {
      document.getElementById('sF').textContent = d.F > 0 ? d.F : '--';
      document.getElementById('sL').textContent = d.L > 0 ? d.L : '--';
      document.getElementById('sB').textContent = d.B > 0 ? d.B : '--';
      document.getElementById('sR').textContent = d.R > 0 ? d.R : '--';
    }).catch(() => {});
  }
  setInterval(updateSensors, 500);
  updateSensors();
</script>
</body>
</html>
)rawhtml";
  server.send(200, "text/html", page);
}

// =====================
// MOVEMENT FUNCTIONS
// =====================
void moveForward()  { setMotor(M1_1,M1_2,LOW,HIGH);  setMotor(M2_1,M2_2,LOW,HIGH);  setMotor(M3_1,M3_2,HIGH,LOW); setMotor(M4_1,M4_2,HIGH,LOW); }
void moveBackward() { setMotor(M1_1,M1_2,HIGH,LOW);  setMotor(M2_1,M2_2,HIGH,LOW);  setMotor(M3_1,M3_2,LOW,HIGH); setMotor(M4_1,M4_2,LOW,HIGH); }
void strafeLeft()   { setMotor(M1_1,M1_2,HIGH,LOW);  setMotor(M2_1,M2_2,LOW,HIGH);  setMotor(M3_1,M3_2,HIGH,LOW); setMotor(M4_1,M4_2,LOW,HIGH); }
void strafeRight()  { setMotor(M1_1,M1_2,LOW,HIGH);  setMotor(M2_1,M2_2,HIGH,LOW);  setMotor(M3_1,M3_2,LOW,HIGH); setMotor(M4_1,M4_2,HIGH,LOW); }
void turnLeft()     { setMotor(M1_1,M1_2,HIGH,LOW);  setMotor(M2_1,M2_2,LOW,HIGH);  setMotor(M3_1,M3_2,LOW,HIGH); setMotor(M4_1,M4_2,HIGH,LOW); }
void turnRight()    { setMotor(M1_1,M1_2,LOW,HIGH);  setMotor(M2_1,M2_2,HIGH,LOW);  setMotor(M3_1,M3_2,HIGH,LOW); setMotor(M4_1,M4_2,LOW,HIGH); }
void stopMotors()   { setMotor(M1_1,M1_2,LOW,LOW);   setMotor(M2_1,M2_2,LOW,LOW);   setMotor(M3_1,M3_2,LOW,LOW);  setMotor(M4_1,M4_2,LOW,LOW);  }

void setMotor(int pin1, int pin2, int s1, int s2) {
  digitalWrite(pin1, s1);
  digitalWrite(pin2, s2);
}
