#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

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

// =====================
// DRIVE MODE
// =====================
enum DriveMode { MODE_MANUAL, MODE_AUTO, MODE_LINE };
DriveMode driveMode = MODE_MANUAL;

// =====================
// CALIBRATION & SPEED
// =====================
int cal[4]   = {255, 255, 255, 255};  // per-motor PWM (0-255)
int speedPct = 100;                   // global speed (0-100%)
Preferences prefs;

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
// SENSOR VALUES
// =====================
float front_cm = -1, left_cm = -1, back_cm = -1, right_cm = -1;
int   lf_l = 0, lf_m = 0, lf_r = 0;
String serialBuffer = "";

// =====================
// UV
// =====================
// 0=off  1=on  2=blink
int           uvMode     = 0;
unsigned long lastUVSend = 0;

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(9600);

  prefs.begin("cal", true);
  cal[0]   = prefs.getInt("m1",  255);
  cal[1]   = prefs.getInt("m2",  255);
  cal[2]   = prefs.getInt("m3",  255);
  cal[3]   = prefs.getInt("m4",  255);
  speedPct = prefs.getInt("spd", 100);
  prefs.end();

  WiFi.softAP(ap_ssid, ap_password);

  pinMode(ENA1, OUTPUT); pinMode(M1_1, OUTPUT); pinMode(M1_2, OUTPUT);
  pinMode(ENA2, OUTPUT); pinMode(M2_1, OUTPUT); pinMode(M2_2, OUTPUT);
  pinMode(ENB1, OUTPUT); pinMode(M3_1, OUTPUT); pinMode(M3_2, OUTPUT);
  pinMode(ENB2, OUTPUT); pinMode(M4_1, OUTPUT); pinMode(M4_2, OUTPUT);

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
  server.on("/modeAuto",   []() { driveMode = MODE_AUTO;   stopMotors(); autoState = AUTO_COOLDOWN; autoCooldown = 0; server.send(200, "text/plain", "OK"); });
  server.on("/modeLine",   []() { driveMode = MODE_LINE;   stopMotors(); server.send(200, "text/plain", "OK"); });

  // UV — 0=off 1=on 2=blink
  server.on("/uvOff",   []() { uvMode = 0; server.send(200, "text/plain", "OK"); });
  server.on("/uvOn",    []() { uvMode = 1; server.send(200, "text/plain", "OK"); });
  server.on("/uvBlink", []() { uvMode = 2; server.send(200, "text/plain", "OK"); });

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
                  ",\"uv\":"   + String(uvMode)      + "}";
    server.send(200, "application/json", json);
  });

  server.on("/", handleRoot);
  server.begin();
}

// =====================
// LOOP
// =====================
void loop() {
  server.handleClient();
  readSerialSensors();

  // Broadcast UV state to Arduino every 500ms
  unsigned long now = millis();
  if (now - lastUVSend >= 500) {
    if      (uvMode == 0) Serial.println("UV:0");
    else if (uvMode == 1) Serial.println("UV:1");
    else                  Serial.println("UV:B");
    lastUVSend = now;
  }

  if      (driveMode == MODE_AUTO) AutonomousMode(front_cm, left_cm, back_cm, right_cm);
  else if (driveMode == MODE_LINE) LineFollowerMode(lf_l, lf_m, lf_r);
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

  int lfIdx = line.indexOf("LF:");
  if (lfIdx != -1 && (lfIdx + 5) < (int)line.length()) {
    lf_l = line[lfIdx + 3] - '0';
    lf_m = line[lfIdx + 4] - '0';
    lf_r = line[lfIdx + 5] - '0';
  }
}

// =====================
// LINE FOLLOWER MODE
// =====================
// Remembers last turn direction to spin-search when the line is lost.

void LineFollowerMode(int l, int m, int r) {
  static int lastTurn = 1;  // 1=right, -1=left
  int pattern = (l << 2) | (m << 1) | r;
  switch (pattern) {
    case 0b010:
    case 0b111: moveForward();              break;
    case 0b110: lastTurn = -1; turnLeft();  break;
    case 0b100: lastTurn = -1; turnLeft();  break;
    case 0b011: lastTurn =  1; turnRight(); break;
    case 0b001: lastTurn =  1; turnRight(); break;
    case 0b000:
      // Lost line — spin in last known direction to find it again
      if (lastTurn < 0) turnLeft(); else turnRight();
      break;
    default: moveForward(); break;
  }
}

// =====================
// AUTONOMOUS MODE
// =====================
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
    if (dist[best] < DANGER) { autoCooldown = now + 300; return; }
    autoDir = best;
    moveDir(autoDir);
    autoState = AUTO_MOVING;
    return;
  }

  float cur = dist[autoDir];
  if (cur < CRITICAL) {
    stopMotors();
    autoState = AUTO_COOLDOWN;
    autoCooldown = now + 250;
  } else if (cur < DANGER) {
    int best = clearestDir(dist);
    if (best != autoDir && dist[best] > DANGER) {
      stopMotors();
      autoDir = best;
      moveDir(autoDir);
      autoState = AUTO_COOLDOWN;
      autoCooldown = now + 150;
    }
  }
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
  <title>NORA Control</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #111; color: #eee;
      font-family: 'Segoe UI', sans-serif;
      display: flex; flex-direction: column; align-items: center;
      min-height: 100vh; padding: 20px; gap: 16px;
    }
    h1 { font-size: 1.6rem; letter-spacing: 3px; color: #0af; }
    h2 { font-size: 0.8rem; color: #555; letter-spacing: 1px; }

    /* Sensors */
    #sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; width: 100%; max-width: 340px; }
    .sensor-box {
      background: #1e1e1e; border: 2px solid #333; border-radius: 8px;
      padding: 8px 12px; font-size: 0.85rem;
      display: flex; flex-direction: column; gap: 4px;
      transition: border-color 0.3s, background 0.3s;
    }
    .sensor-top { display: flex; justify-content: space-between; }
    .sensor-box span { color: #0af; font-weight: bold; }
    .sensor-box.warn  { border-color: #f90; }
    .sensor-box.crit  { border-color: #f44; background: #1a0a0a; }
    .sensor-box.travel { border-color: #0af; background: #001a2a; }
    .dist-bar-bg { background: #2a2a2a; border-radius: 3px; height: 4px; overflow: hidden; }
    .dist-bar    { height: 4px; border-radius: 3px; background: #0af; transition: width 0.3s, background 0.3s; width: 0%; }

    /* Line follower dots */
    #lfRow { display: flex; gap: 8px; align-items: center; font-size: 0.8rem; color: #666; }
    .lf-dot {
      width: 20px; height: 20px; border-radius: 50%;
      background: #222; border: 2px solid #444; transition: background 0.1s, border-color 0.1s;
    }
    .lf-dot.on { background: #0af; border-color: #0af; }

    /* Mode buttons */
    #modeRow { display: flex; gap: 8px; flex-wrap: wrap; justify-content: center; }
    .mode-btn {
      padding: 10px 18px; border: 2px solid #333; border-radius: 8px;
      font-size: 0.85rem; font-weight: bold; cursor: pointer;
      background: #1e1e1e; color: #aaa;
    }
    .mode-btn.active { border-color: #0af; color: #0af; background: #001a2a; }

    /* UV button */
    #uvBtn {
      padding: 9px 20px; border: 2px solid #444; border-radius: 8px;
      font-size: 0.85rem; font-weight: bold; cursor: pointer;
      background: #1e1e1e; color: #aaa; transition: all 0.2s;
    }
    #uvBtn.on    { border-color: #bb0; color: #ff0; background: #1a1900; }
    #uvBtn.blink { border-color: #f80; color: #f80; background: #1a1000; }

    /* Speed row */
    #speedRow {
      display: flex; align-items: center; gap: 10px;
      width: 100%; max-width: 340px; font-size: 0.85rem; color: #aaa;
    }
    #speedRow input[type=range] { flex: 1; accent-color: #0af; }
    #speedVal { width: 36px; text-align: right; color: #0af; font-weight: bold; }

    /* D-pad */
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
      -webkit-tap-highlight-color: transparent; transition: opacity 0.2s;
      touch-action: none;
    }
    .btn.disabled { opacity: 0.25; pointer-events: none; }
    .btn.pressed  { background: #004466; border-color: #0af; }
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

    /* Calibration panel */
    #calPanel {
      width: 100%; max-width: 340px;
      background: #1a1a1a; border: 1px solid #333; border-radius: 10px;
      padding: 14px; display: flex; flex-direction: column; gap: 10px;
    }
    #calPanel summary { font-size: 0.85rem; color: #0af; cursor: pointer; user-select: none; font-weight: bold; }
    .cal-row { display: flex; align-items: center; gap: 10px; font-size: 0.8rem; }
    .cal-row label { width: 24px; color: #aaa; }
    .cal-row input[type=range] { flex: 1; accent-color: #0af; }
    .cal-row span { width: 30px; text-align: right; color: #0af; font-weight: bold; }
    #saveCalBtn {
      align-self: flex-end; padding: 6px 16px; background: #0a5; border: none;
      border-radius: 6px; color: #fff; font-size: 0.8rem; font-weight: bold; cursor: pointer;
    }
    #saveCalBtn.saved { background: #555; }
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
  </div>

  <div id="modeRow">
    <button class="mode-btn active" id="btnManual" onclick="setMode('Manual')">MANUAL</button>
    <button class="mode-btn"        id="btnAuto"   onclick="setMode('Auto')">AUTO</button>
    <button class="mode-btn"        id="btnLine"   onclick="setMode('Line')">LINE</button>
  </div>

  <button id="uvBtn" onclick="cycleUV()">UV OFF</button>

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
  let uvState    = 0;       // 0=off 1=on 2=blink
  let currentMode = 'Manual';

  // Direction index → sensor box id
  const dirBox = ['boxF', 'boxR', 'boxB', 'boxL'];

  function cmd(c) { fetch(c).catch(() => {}); }

  // ── Mode ──────────────────────────────
  function setMode(mode) {
    currentMode = mode;
    cmd('/mode' + mode);
    ['Manual','Auto','Line'].forEach(m =>
      document.getElementById('btn' + m).classList.toggle('active', m === mode)
    );
    const isManual = mode === 'Manual';
    document.querySelectorAll('.btn').forEach(b => b.classList.toggle('disabled', !isManual));
  }

  // ── UV ────────────────────────────────
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

  // ── Speed ─────────────────────────────
  function setSpeed(slider) {
    document.getElementById('speedVal').textContent = slider.value + '%';
    clearTimeout(speedTimer);
    speedTimer = setTimeout(() => cmd('/setspeed?v=' + slider.value), 200);
  }

  // ── Calibration ───────────────────────
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

  // Load saved cal + speed on page open
  fetch('/getcal').then(r => r.json()).then(d => {
    [d.m1, d.m2, d.m3, d.m4].forEach((v, i) => {
      calVals[i] = v;
      document.getElementById('sl' + (i+1)).value = v;
      document.getElementById('v'  + (i+1)).textContent = v;
    });
    document.getElementById('speedSlider').value = d.spd;
    document.getElementById('speedVal').textContent = d.spd + '%';
  }).catch(() => {});

  // ── D-pad ─────────────────────────────
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
  document.addEventListener('keydown', e => {
    const c = keyMap[e.key];
    if (!c || currentMode !== 'Manual') return;
    e.preventDefault();
    c === '/stop' ? cmd('/stop') : startCmd(c);
  });
  document.addEventListener('keyup', e => { if (keyMap[e.key] && keyMap[e.key] !== '/stop') stopCmd(); });

  // ── Sensor polling ────────────────────
  function updateSensor(boxId, barId, spanId, value) {
    const box = document.getElementById(boxId);
    const bar = document.getElementById(barId);
    document.getElementById(spanId).textContent = value > 0 ? value : '--';

    // Proximity colour
    box.classList.remove('warn', 'crit');
    if (value > 0 && value < 15)  box.classList.add('crit');
    else if (value > 0 && value < 28) box.classList.add('warn');

    // Distance bar (max range 100 cm shown as 100%)
    const pct = value > 0 ? Math.min(value / 100 * 100, 100) : 0;
    bar.style.width = pct + '%';
    bar.style.background = value > 0 && value < 15 ? '#f44' : value > 0 && value < 28 ? '#f90' : '#0af';
  }

  function updateSensors() {
    fetch('/sensors').then(r => r.json()).then(d => {
      updateSensor('boxF', 'barF', 'sF', d.F);
      updateSensor('boxB', 'barB', 'sB', d.B);
      updateSensor('boxL', 'barL', 'sL', d.L);
      updateSensor('boxR', 'barR', 'sR', d.R);

      // Line follower dots
      document.getElementById('lfL').classList.toggle('on', d.lfl === 1);
      document.getElementById('lfM').classList.toggle('on', d.lfm === 1);
      document.getElementById('lfR').classList.toggle('on', d.lfr === 1);

      // Auto travel direction highlight
      dirBox.forEach(id => document.getElementById(id).classList.remove('travel'));
      if (d.mode === 1) document.getElementById(dirBox[d.dir]).classList.add('travel');

      // Sync UV button if needed
      if (d.uv !== uvState) { uvState = d.uv; applyUV(); }
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
void moveForward()  { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0]);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1]);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2]); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3]); }
void moveBackward() { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0]);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1]);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2]); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3]); }
void strafeLeft()   { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0]);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1]);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2]); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3]); }
void strafeRight()  { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0]);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1]);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2]); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3]); }
void turnLeft()     { setMotor(ENA1,M1_1,M1_2,HIGH,LOW,cal[0]);  setMotor(ENA2,M2_1,M2_2,LOW,HIGH,cal[1]);  setMotor(ENB1,M3_1,M3_2,LOW,HIGH,cal[2]); setMotor(ENB2,M4_1,M4_2,HIGH,LOW,cal[3]); }
void turnRight()    { setMotor(ENA1,M1_1,M1_2,LOW,HIGH,cal[0]);  setMotor(ENA2,M2_1,M2_2,HIGH,LOW,cal[1]);  setMotor(ENB1,M3_1,M3_2,HIGH,LOW,cal[2]); setMotor(ENB2,M4_1,M4_2,LOW,HIGH,cal[3]); }

void stopMotors() {
  analogWrite(ENA1, 0); digitalWrite(M1_1, LOW); digitalWrite(M1_2, LOW);
  analogWrite(ENA2, 0); digitalWrite(M2_1, LOW); digitalWrite(M2_2, LOW);
  analogWrite(ENB1, 0); digitalWrite(M3_1, LOW); digitalWrite(M3_2, LOW);
  analogWrite(ENB2, 0); digitalWrite(M4_1, LOW); digitalWrite(M4_2, LOW);
}

void setMotor(int ena, int pin1, int pin2, int s1, int s2, int speed) {
  analogWrite(ena, (speed * speedPct) / 100);
  digitalWrite(pin1, s1);
  digitalWrite(pin2, s2);
}
