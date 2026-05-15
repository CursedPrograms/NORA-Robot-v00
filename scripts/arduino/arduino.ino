#include <SoftwareSerial.h>
SoftwareSerial mySerial(8, 9);  // RX=8 (← ESP32 TX), TX=9 (→ ESP32 RX)

// =====================
// PIN SETUP
// =====================

// Ultrasonic — Front
#define F_TRIG A0
#define F_ECHO A1

// Ultrasonic — Left
#define L_TRIG 6
#define L_ECHO 7

// Ultrasonic — Back
#define B_TRIG A4
#define B_ECHO A5

// Ultrasonic — Right
#define R_TRIG A2
#define R_ECHO A3

// Line follower (left → right)
#define LF_LEFT  4
#define LF_MID   5
#define LF_RIGHT 6

// UV light
#define UV_PIN 10

// =====================
// SETUP
// =====================

String cmdBuffer = "";

void setup() {
  mySerial.begin(9600);

  pinMode(F_TRIG, OUTPUT); pinMode(F_ECHO, INPUT);
  pinMode(L_TRIG, OUTPUT); pinMode(L_ECHO, INPUT);
  pinMode(B_TRIG, OUTPUT); pinMode(B_ECHO, INPUT);
  pinMode(R_TRIG, OUTPUT); pinMode(R_ECHO, INPUT);

  pinMode(LF_LEFT,  INPUT);
  pinMode(LF_MID,   INPUT);
  pinMode(LF_RIGHT, INPUT);

  pinMode(UV_PIN, OUTPUT);
  digitalWrite(UV_PIN, LOW);
}

// =====================
// LOOP
// =====================

void loop() {
  // Read commands from ESP32 (UV control)
  while (mySerial.available()) {
    char c = mySerial.read();
    if (c == '\n') {
      handleCommand(cmdBuffer);
      cmdBuffer = "";
    } else if (c != '\r') {
      cmdBuffer += c;
    }
  }

  float front = readDistance(F_TRIG, F_ECHO); delay(50);
  float left  = readDistance(L_TRIG, L_ECHO); delay(50);
  float back  = readDistance(B_TRIG, B_ECHO); delay(50);
  float right = readDistance(R_TRIG, R_ECHO); delay(50);

  int lfl = digitalRead(LF_LEFT);
  int lfm = digitalRead(LF_MID);
  int lfr = digitalRead(LF_RIGHT);

  mySerial.print("F:");  mySerial.print(front);
  mySerial.print(",L:"); mySerial.print(left);
  mySerial.print(",B:"); mySerial.print(back);
  mySerial.print(",R:"); mySerial.print(right);
  mySerial.print(",LF:");
  mySerial.print(lfl); mySerial.print(lfm); mySerial.println(lfr);

  delay(100);
}

// =====================
// COMMAND HANDLER
// =====================

void handleCommand(String cmd) {
  if      (cmd == "UV:1") digitalWrite(UV_PIN, HIGH);
  else if (cmd == "UV:0") digitalWrite(UV_PIN, LOW);
}

// =====================
// ULTRASONIC FUNCTION
// =====================

float readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  float distance = duration * 0.034 / 2;

  if (distance == 0 || distance > 400) return -1;
  return distance;
}
