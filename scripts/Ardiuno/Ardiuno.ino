#include <SoftwareSerial.h>
SoftwareSerial mySerial(8, 9);

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

// =====================
// SETUP
// =====================

void setup() {
  mySerial.begin(9600);  // TX goes to ESP32 Serial2 RX

  pinMode(F_TRIG, OUTPUT);
  pinMode(F_ECHO, INPUT);
  pinMode(L_TRIG, OUTPUT);
  pinMode(L_ECHO, INPUT);
  pinMode(B_TRIG, OUTPUT);
  pinMode(B_ECHO, INPUT);
  pinMode(R_TRIG, OUTPUT);
  pinMode(R_ECHO, INPUT);
}

// =====================
// LOOP
// =====================

void loop() {
  float front = readDistance(F_TRIG, F_ECHO);
  delay(50);
  float left = readDistance(L_TRIG, L_ECHO);
  delay(50);
  float back = readDistance(B_TRIG, B_ECHO);
  delay(50);
  float right = readDistance(R_TRIG, R_ECHO);
  delay(50);

  // Send to ESP32 via Serial TX
  mySerial.print("F:");
  mySerial.print(front);
  mySerial.print(",L:");
  mySerial.print(left);
  mySerial.print(",B:");
  mySerial.print(back);
  mySerial.print(",R:");
  mySerial.println(right);

  delay(100);
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
