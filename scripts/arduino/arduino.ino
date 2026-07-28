/*
 * NORA — Arduino side (Uno + SparkFun MP3 Player Shield)
 * -------------------------------------------------------
 * Jobs:
 *   1. Play track001.mp3 ONCE at startup
 *   2. Music player: track100.mp3 and above are music tracks,
 *      controlled by the ESP32 over serial:
 *        M:PLAY M:NEXT M:PREV M:STOP M:REPEAT (toggle repeat-current-track)
 *        V:UP V:DOWN (volume nudge)  V:MUTE (toggle)
 *   3. Read 4 ultrasonic sensors (normal 2-wire hookup) and stream to ESP32
 *   4. Buzzer feedback (pin 10): an ambient "something's close" tone driven
 *      entirely by our own sensor readings (works in every drive mode), plus
 *      one-shot beeps for state changes the ESP32 tells us about over serial:
 *        BZ:SPD:<0-100>   speed changed, pitch tracks the new value
 *        BZ:MODE:<0-3>    drive mode changed (Manual/Auto/Line/Remote)
 *        BZ:UV:<0-2>      UV state changed (off/on/blink)
 *        BZ:LOCK:<0|1>    motor lock toggled
 *        BZ:DENIED        a Manual/Remote drive command got blocked by an
 *                         obstacle -- see esp32.ino, this never fires from
 *                         Auto/Line since they have their own avoidance
 *
 * Libraries (ZIP from github.com/madsci1016/Sparkfun-MP3-Player-Shield-Arduino-Library):
 *   - SFEMP3Shield
 *   - SdFat
 *
 * SHIELD RESERVES: 2 (DREQ), 3/4 (MIDI), 6 (XCS), 7 (XDCS), 8 (RESET),
 *                  9 (SD CS), 11/12/13 (SPI)
 *
 * PIN MAP:
 *   All 4 ultrasonic TRIGs joined together -> A4 (one shared trigger)
 *   Front ECHO: A0    Right ECHO: A1
 *   Back  ECHO: A2    Left  ECHO: A3
 *   Photoresistor (LDR): 5V -> LDR -> A5 -> 10k resistor -> GND
 *   Sound sensor DO:     pin 5 (VCC can go to 5V here, unlike the ESP32)
 *   Buzzer:              pin 10 (either polarity of a passive/piezo buzzer;
 *                        driven with tone()/noTone(), not a plain HIGH/LOW)
 *   ESP32 link:        hardware serial, pin 0 RX (<- ESP32 TX0) and
 *                      pin 1 TX (-> ESP32 RX0 through a voltage divider!)
 *
 *   The sound sensor has no free interrupt pin to land on (2/3 are both
 *   shield-reserved), so instead of one digitalRead per loop it's polled
 *   tightly during the delays we already take between ultrasonic pings --
 *   see pollDuring(). That's what actually makes clap detection reliable
 *   moved over here instead of on the ESP32's much faster loop.
 *
 * !!! UPLOADING: disconnect the two ESP32 wires from pins 0/1 before
 *     uploading a sketch, then reconnect. They share the USB lines. !!!
 *
 * SD card layout (FAT16/FAT32, 8.3 names, case doesn't matter):
 *   track000.mp3            -> startup sound
 *   track100.mp3, track101.mp3, track102.mp3 ...  -> music library
 *   (music tracks must be numbered consecutively from 100)
 */

#include <SPI.h>
#include <SdFat.h>
#include <SFEMP3Shield.h>

SdFat sd;
SFEMP3Shield MP3player;

// ---- Ultrasonic pins (shared trigger, one echo per sensor) ----
#define TRIG_ALL A4   // all four TRIG pins joined here
#define F_ECHO   A0
#define R_ECHO   A1
#define B_ECHO   A2
#define L_ECHO   A3

// ---- Light + sound (moved over from the ESP32) ----
#define LDR_PIN   A5
#define SOUND_PIN 5

int  lightPct        = 0;      // 0 = dark, 100 = bright, smoothed
bool soundEvent       = false; // latched: a debounced rising edge since the last report
bool lastSoundLevel   = false;
unsigned long lastSoundMs = 0;

// ---- Buzzer ----
#define BUZZER_PIN 10
// Ambient obstacle tone range: below OBSTACLE_STOP_CM it's pinned at its
// highest pitch, above OBSTACLE_WARN_CM it's silent, linear in between.
#define OBSTACLE_WARN_CM  25.0
#define OBSTACLE_STOP_CM  8.0
// While a one-shot beep (BZ:* or a music/volume blip) is still "owed" its
// duration, the ambient obstacle tone backs off instead of talking over it.
unsigned long buzzOverrideUntil = 0;

void oneShotBeep(unsigned int freq, unsigned int durMs) {
  tone(BUZZER_PIN, freq, durMs);
  buzzOverrideUntil = millis() + durMs;
}

// Closer = higher pitch, like a parking sensor. `d` must already be a
// valid (>0) reading -- callers check that before calling this.
int obstacleFreq(float d) {
  if (d <= OBSTACLE_STOP_CM) return 1800;
  if (d >= OBSTACLE_WARN_CM) return 500;
  return map((long)(d * 10), (long)(OBSTACLE_STOP_CM * 10), (long)(OBSTACLE_WARN_CM * 10),
             1800, 500);
}

// ---- Music player state ----
#define FIRST_MUSIC_TRACK 100
int  maxTrack    = 0;      // highest music track found on the SD card
int  curTrack    = FIRST_MUSIC_TRACK;
bool inMusicMode = false;  // true while the jukebox is active (not startup sound)
bool repeatMode  = false;  // true: replay curTrack on end instead of advancing

// ---- Volume ----
// VS1053 setVolume(l,r) takes attenuation in ~0.5dB steps: 0 = loudest,
// 254 = essentially silent (255 cuts the analog output entirely, so we
// stop one step short of that for mute).
#define VOLUME_MIN  0
#define VOLUME_MAX  254
#define VOLUME_STEP 20
int  volume = VOLUME_MIN;   // current volume (0 = max, matches old hardcoded startup value)
bool muted  = false;        // true: hardware register forced quiet, `volume` itself untouched

String cmdBuffer = "";

void setup() {
  Serial.begin(9600);   // link to ESP32 on pins 0/1

  pinMode(TRIG_ALL, OUTPUT);
  digitalWrite(TRIG_ALL, LOW);
  pinMode(F_ECHO, INPUT);
  pinMode(R_ECHO, INPUT);
  pinMode(B_ECHO, INPUT);
  pinMode(L_ECHO, INPUT);

  pinMode(SOUND_PIN, INPUT);
  // LDR_PIN (A5) is analog input; no pinMode needed

  pinMode(BUZZER_PIN, OUTPUT);

 if (sd.begin(SD_SEL, SPI_HALF_SPEED) && sd.chdir("/")) {
    uint8_t r = MP3player.begin();
    if (r == 0 || r == 6) {          // 6 = no patch file, still plays fine
      MP3player.setVolume(volume, volume);

      char name[13];
      for (int i = FIRST_MUSIC_TRACK; i <= 254; i++) {
        sprintf(name, "track%03d.mp3", i);
        if (!sd.exists(name)) break;
        maxTrack = i;
      }

      MP3player.playTrack(0);        // startup sound
    }
  }
}

void loop() {
  // ---- Commands from ESP32 ----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(cmdBuffer);
      cmdBuffer = "";
    } else if (c != '\r') {
      cmdBuffer += c;
    }
  }

  // ---- Auto-advance: when a music track finishes, repeat or play next ----
  if (inMusicMode && MP3player.getState() == ready) {
    if (repeatMode) MP3player.playTrack(curTrack);
    else            nextTrack();
  }

  // ---- Light level (smoothed) ----
  int rawLdr = analogRead(LDR_PIN);        // 0-1023 on the Uno
  int pct    = map(rawLdr, 0, 1023, 0, 100);
  lightPct   = (lightPct * 3 + pct) / 4;   // simple smoothing

  // ---- Ultrasonics ----
  // One shared trigger fires all four sensors at once; we time one
  // echo per ping. The 60ms gaps let stray echoes die down between reads --
  // pollDuring() spends that time watching for sound edges instead of
  // just sitting in delay().
  float front = readDistance(F_ECHO); pollDuring(60);
  float right = readDistance(R_ECHO); pollDuring(60);
  float back  = readDistance(B_ECHO); pollDuring(60);
  float left  = readDistance(L_ECHO); pollDuring(60);

  // ---- Ambient obstacle-proximity tone ----
  // Independent of drive mode -- this is just "something is close on some
  // side", not the Manual/Remote crash-prevention block (that lives on the
  // ESP32 and announces itself with the sharper one-shot BZ:DENIED instead).
  // Skipped while a one-shot beep is still sounding so it doesn't get cut
  // off or talked over.
  if (millis() >= buzzOverrideUntil) {
    float dists[4] = {front, right, back, left};
    float minD = -1;
    for (int i = 0; i < 4; i++) {
      if (dists[i] > 0 && (minD < 0 || dists[i] < minD)) minD = dists[i];
    }
    if (minD > 0 && minD < OBSTACLE_WARN_CM) {
      tone(BUZZER_PIN, obstacleFreq(minD), 90);   // re-triggered every loop while close
    } else {
      noTone(BUZZER_PIN);
    }
  }

  // ---- Status line to ESP32 ----
  // MS: 0 = stopped, 1 = playing, 2 = paused
  // LT: light 0-100%   SND: 1 if a debounced sound edge fired since the last line
  int ms = 0;
  state_m st = MP3player.getState();
  if (inMusicMode) {
    if      (st == playback)         ms = 1;
    else if (st == paused_playback)  ms = 2;
  }

  bool sndOut = soundEvent;
  soundEvent = false;   // consumed: latch is only valid for one report

  Serial.print("F:");   Serial.print(front);
  Serial.print(",L:");  Serial.print(left);
  Serial.print(",B:");  Serial.print(back);
  Serial.print(",R:");  Serial.print(right);
  Serial.print(",MT:"); Serial.print(curTrack);
  Serial.print(",MS:"); Serial.print(ms);
  Serial.print(",LT:"); Serial.print(lightPct);
  Serial.print(",SND:"); Serial.println(sndOut ? 1 : 0);

  pollDuring(100);
}

// Watches the sound sensor for a debounced rising edge for `ms` milliseconds,
// instead of just blocking in delay() -- this is what we spend the
// inter-sensor gaps on so a clap isn't missed between the coarse ~340ms
// loop iterations. Sets soundEvent; never clears it (the caller consumes
// and resets that after building the status line).
void pollDuring(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    bool level = (digitalRead(SOUND_PIN) == HIGH);
    if (level && !lastSoundLevel && millis() - lastSoundMs > 120) {
      lastSoundMs = millis();
      soundEvent  = true;
    }
    lastSoundLevel = level;
  }
}

// =====================
// MUSIC COMMANDS
// =====================
void handleCommand(String cmd) {
  // ---- Buzzer notifications from the ESP32 (mode/UV/lock/speed all live
  // there, but the buzzer's out here, so they have to travel over serial) ----
  if (cmd.startsWith("BZ:SPD:")) {
    int pct  = constrain(cmd.substring(7).toInt(), 0, 100);
    oneShotBeep(map(pct, 0, 100, 400, 1400), 60);
    return;
  }
  if (cmd.startsWith("BZ:MODE:")) {
    int m = constrain(cmd.substring(8).toInt(), 0, 3);
    oneShotBeep(700 + m * 200, 90);
    return;
  }
  if (cmd.startsWith("BZ:UV:")) {
    int s = constrain(cmd.substring(6).toInt(), 0, 2);
    oneShotBeep(500 + s * 250, 70);
    return;
  }
  if (cmd.startsWith("BZ:LOCK:")) {
    bool locked = cmd.substring(8).toInt() == 1;
    oneShotBeep(locked ? 300 : 950, locked ? 180 : 90);
    return;
  }
  if (cmd == "BZ:DENIED") {
    oneShotBeep(220, 150);   // low "no" -- a blocked Manual/Remote drive attempt
    return;
  }

  // Volume/mute act on the VS1053 hardware register directly, so they work
  // even before any track100+ file exists on the card — unlike playback,
  // there's nothing here that depends on a music track being present.
  if (cmd == "V:UP") {
    muted  = false;
    volume = max(VOLUME_MIN, volume - VOLUME_STEP);   // lower value = louder
    MP3player.setVolume(volume, volume);
    oneShotBeep(900, 40);
    return;
  }
  if (cmd == "V:DOWN") {
    muted  = false;
    volume = min(VOLUME_MAX, volume + VOLUME_STEP);
    MP3player.setVolume(volume, volume);
    oneShotBeep(600, 40);
    return;
  }
  if (cmd == "V:MUTE") {
    if (muted) {
      muted = false;
      MP3player.setVolume(volume, volume);
    } else {
      muted = true;
      MP3player.setVolume(VOLUME_MAX, VOLUME_MAX);
    }
    oneShotBeep(muted ? 400 : 900, 60);
    return;
  }

  if (maxTrack == 0) return;   // no music tracks on the card

  if (cmd == "M:PLAY") {
    state_m st = MP3player.getState();
    if (inMusicMode && st == playback) {
      MP3player.pauseMusic();
      oneShotBeep(500, 50);          // falling blip = paused
    } else if (inMusicMode && st == paused_playback) {
      MP3player.resumeMusic();
      oneShotBeep(800, 50);          // rising blip = resumed
    } else {
      // Not in music mode: stop whatever is playing and start the jukebox
      MP3player.stopTrack();
      inMusicMode = true;
      MP3player.playTrack(curTrack);
      oneShotBeep(800, 50);
    }
  }
  else if (cmd == "M:NEXT") {
    if (inMusicMode) { nextTrack(); oneShotBeep(1000, 30); }
  }
  else if (cmd == "M:PREV") {
    if (inMusicMode) { prevTrack(); oneShotBeep(700, 30); }
  }
  else if (cmd == "M:STOP") {
    MP3player.stopTrack();
    inMusicMode = false;
    oneShotBeep(400, 80);
  }
  else if (cmd == "M:REPEAT") {
    repeatMode = !repeatMode;
    oneShotBeep(repeatMode ? 1100 : 700, 40);
  }
}

void nextTrack() {
  curTrack++;
  if (curTrack > maxTrack) curTrack = FIRST_MUSIC_TRACK;
  MP3player.stopTrack();
  MP3player.playTrack(curTrack);
}

void prevTrack() {
  curTrack--;
  if (curTrack < FIRST_MUSIC_TRACK) curTrack = maxTrack;
  MP3player.stopTrack();
  MP3player.playTrack(curTrack);
}

// =====================
// ULTRASONIC
// =====================
float readDistance(int echoPin) {
  digitalWrite(TRIG_ALL, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_ALL, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_ALL, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  float distance = duration * 0.034 / 2;
  if (distance == 0 || distance > 400) return -1;
  return distance;
}
