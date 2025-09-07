/*
  Reactor Sensor Hub (Arduino)
  ---------------------------
  - Reads two TCS3200 color sensors (RGB1, RGB2)
  - RGB1 uses a fixed "Preset 1" mapping that matched wiring:
      RED=HL, GREEN=HH, BLUE=LH, CLEAR=LL  (S2/S3)
  - Reads temperature (A0), UV sensor (A1), photodiode (A2), turbidity2 (A4)
    (turbidity1 was removed; JSON keeps the key with 0.00 to avoid breaking Node-RED)
  - Drives Pump 1 (DC via GravityPump on D9) and Pump 2 (servo on A5)
  - Streams a single JSON object per loop for Node-RED

  Serial commands
  ---------------
  - "led on at intensity:<0..255>"   e.g. "led on at intensity:128"
  - "led off"
  - "pump:<mL>"                      e.g. "pump: 1.5"
  - "pump:<mL> at speed:<0..89>"     e.g. "pump: 1.5 at speed:30"
  - "pump2:<mL>"                     e.g. "pump2: 0.250"
  - "pump stop" / "pump2 stop"

  Notes
  -----
  - Keep white illumination on the sample for meaningful color readings.
  - A3 must be interrupt-capable on board for RGB1 OUT. 
*/

#include <Servo.h>
#include "GravityPump.h"
#include <EEPROM.h>
#include <math.h>

enum TCSChan { TCS_RED, TCS_GREEN, TCS_BLUE, TCS_CLEAR };

const int tempSensorPin = A0;
const int uvSensor1Pin  = A1;
const int photodiodePin = A2;
const int turbidityPin2 = A4;

const int uvLedPin1 = 6;
const int pumpPin   = 9;
const int pump2Pin  = A5;

const int s0_1 = 4, s1_1 = 5, s2_1 = 3, s3_1 = 7;
const int out_1 = A3;
const int s0_2 = 8, s1_2 = 10, s2_2 = 11, s3_2 = 12;
const int out_2 = 2;

const float alpha = 0.3f;
const unsigned GATE_MS = 100;
float vcc = 4.63;

const float pump2FlowRate   = 0.01f;
const int   pump2SpeedAngle = 10;

Servo pump2Servo;
GravityPump pump;

bool uvState = false;
bool pumpState = false;
bool pump2State = false;
int  uvLedIntensity = 0;

unsigned long pumpEndTime  = 0;
unsigned long pump2EndTime = 0;
int lastPumpSpeed = 0;

static String lastPump1Command = "";

volatile uint32_t counter1 = 0, counter2 = 0;
float r1=0,g1=0,b1=0,c1=0;
float r2=0,g2=0,b2=0,c2=0;

static inline uint8_t gamma22(float v) {
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  return (uint8_t)(255.0 * pow(v, 1.0/2.2));
}

int readSmoothedAnalog(int pin, int samples = 20) {
  analogRead(pin);
  delayMicroseconds(200);
  long total = 0;
  for (int i = 0; i < samples; i++) { total += analogRead(pin); delay(2); }
  return total / samples;
}

float getCalibratedFlowRate() {
  float value;
  byte *ptr = (byte*)&value;
  for (int i = 0; i < (int)sizeof(value); i++) ptr[i] = EEPROM.read(0x24 + i);
  return value;
}

void ISR_sensor1() { counter1++; }
void ISR_sensor2() { counter2++; }

void tcsSelect(int s2, int s3, TCSChan c) {
  switch (c) {
    case TCS_RED:   digitalWrite(s2, LOW);  digitalWrite(s3, LOW);  break;
    case TCS_GREEN: digitalWrite(s2, HIGH); digitalWrite(s3, HIGH); break;
    case TCS_BLUE:  digitalWrite(s2, LOW);  digitalWrite(s3, HIGH); break;
    case TCS_CLEAR: digitalWrite(s2, HIGH); digitalWrite(s3, LOW);  break;
  }
}

uint32_t tcsReadCounts(volatile uint32_t &ctr, unsigned gate_ms) {
  noInterrupts(); ctr = 0; interrupts();
  uint32_t t0 = millis();
  while (millis() - t0 < gate_ms) {}
  noInterrupts(); uint32_t counts = ctr; interrupts();
  return counts;
}

void readTCS3200(int s2, int s3, volatile uint32_t &ctr, float &R, float &G, float &B, float &C) {
  uint32_t rC, gC, bC, cC;
  tcsSelect(s2, s3, TCS_RED);   rC = tcsReadCounts(ctr, GATE_MS);
  tcsSelect(s2, s3, TCS_GREEN); gC = tcsReadCounts(ctr, GATE_MS);
  tcsSelect(s2, s3, TCS_BLUE);  bC = tcsReadCounts(ctr, GATE_MS);
  tcsSelect(s2, s3, TCS_CLEAR); cC = tcsReadCounts(ctr, GATE_MS);
  R = alpha * (float)rC + (1 - alpha) * R;
  G = alpha * (float)gC + (1 - alpha) * G;
  B = alpha * (float)bC + (1 - alpha) * B;
  C = alpha * (float)cC + (1 - alpha) * C;
}

void readTCS3200_rgb1_fixed(int s2, int s3, volatile uint32_t &ctr, float &R, float &G, float &B, float &C) {
  uint32_t rC, gC, bC, cC;
  digitalWrite(s2, HIGH); digitalWrite(s3, LOW);   rC = tcsReadCounts(ctr, GATE_MS);
  digitalWrite(s2, HIGH); digitalWrite(s3, HIGH);  gC = tcsReadCounts(ctr, GATE_MS);
  digitalWrite(s2, LOW);  digitalWrite(s3, HIGH);  bC = tcsReadCounts(ctr, GATE_MS);
  digitalWrite(s2, LOW);  digitalWrite(s3, LOW);   cC = tcsReadCounts(ctr, GATE_MS);
  R = alpha * (float)rC + (1 - alpha) * R;
  G = alpha * (float)gC + (1 - alpha) * G;
  B = alpha * (float)bC + (1 - alpha) * B;
  C = alpha * (float)cC + (1 - alpha) * C;
}

void setup() {
  Serial.begin(9600);

  pinMode(tempSensorPin, INPUT);
  pinMode(uvSensor1Pin, INPUT);
  pinMode(photodiodePin, INPUT);
  pinMode(turbidityPin2, INPUT);
  pinMode(uvLedPin1, OUTPUT);

  pinMode(s0_1, OUTPUT); pinMode(s1_1, OUTPUT); pinMode(s2_1, OUTPUT); pinMode(s3_1, OUTPUT);
  pinMode(out_1, INPUT);
  pinMode(s0_2, OUTPUT); pinMode(s1_2, OUTPUT); pinMode(s2_2, OUTPUT); pinMode(s3_2, OUTPUT);
  pinMode(out_2, INPUT);

  digitalWrite(s0_1, HIGH); digitalWrite(s1_1, LOW);
  digitalWrite(s0_2, HIGH); digitalWrite(s1_2, LOW);

  attachInterrupt(digitalPinToInterrupt(out_1), ISR_sensor1, FALLING);
  attachInterrupt(digitalPinToInterrupt(out_2), ISR_sensor2, FALLING);

  pump.setPin(pumpPin);
  pump.getFlowRateAndSpeed();
}

void loop() {
  readTCS3200_rgb1_fixed(s2_1, s3_1, counter1, r1, g1, b1, c1);
  readTCS3200(s2_2, s3_2, counter2, r2, g2, b2, c2);

  pump.update();
  if (pumpState && millis() > pumpEndTime) pumpState = false;
  if (pump2State && millis() > pump2EndTime) {
    pump2Servo.write(90); pump2Servo.detach(); pump2State = false;
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("led on at intensity:")) {
      int value = command.substring(command.indexOf(':') + 1).toInt();
      if (value >= 0 && value <= 255) {
        uvState = true;
        uvLedIntensity = value;
        Serial.print("UV LED ON at intensity = "); Serial.println(uvLedIntensity);
      } else {
        Serial.println("Invalid intensity value (0–255)");
      }

    } else if (command.equalsIgnoreCase("led off")) {
      uvState = false;
      Serial.println("UV LED OFF");

    } else if (command.startsWith("pump:")) {
      if (command != lastPump1Command) {
        lastPump1Command = command;
        float ml = 0; int speed = -1; bool valid = false;
        int atIndex = command.indexOf("at");
        if (atIndex > 0) {
          String volStr = command.substring(5, atIndex); volStr.trim();
          ml = volStr.toFloat();
          int speedIndex = command.indexOf("speed:", atIndex);
          if (speedIndex > -1) { speed = command.substring(speedIndex + 6).toInt(); if (speed >= 0 && speed < 90) valid = true; }
        } else { ml = command.substring(5).toFloat(); valid = true; }

        if (valid && ml > 0) {
          lastPumpSpeed = speed;
          Serial.print("Pump1 dosing "); Serial.print(ml, 2); Serial.print(" mL at "); Serial.println(speed);
          pumpState = true; pump.pumpDriver(speed, 10);
          float dur = pump.flowPump(ml); if (dur > 0) pumpEndTime = millis() + dur;
        }
      } else Serial.println("Duplicate pump command ignored");

    } else if (command.startsWith("pump2:")) {
      float ml = command.substring(6).toFloat();
      if (ml > 0) {
        unsigned long duration2 = (ml / pump2FlowRate) * 1000UL;
        Serial.print("Pump2 dosing "); Serial.print(ml, 3); Serial.println(" mL");
        pump2Servo.attach(pump2Pin);
        pump2Servo.write(pump2SpeedAngle);
        pump2State = true; pump2EndTime = millis() + duration2;
      }

    } else if (command.equalsIgnoreCase("pump stop")) {
      pumpState = false; pumpEndTime = 0; analogWrite(pumpPin, 0); Serial.println("Pump1 STOP");
    } else if (command.equalsIgnoreCase("pump2 stop")) {
      pump2Servo.write(90); pump2Servo.detach(); pump2State = false; pump2EndTime = 0; Serial.println("Pump2 STOP");
    }
  }

  analogWrite(uvLedPin1, uvState ? uvLedIntensity : 0);

  int tempRaw = readSmoothedAnalog(tempSensorPin);
  float temperature = (tempRaw / 1023.0f) * 3.3f * 100.0f;
  int uvRaw1 = readSmoothedAnalog(uvSensor1Pin);
  float uvIndex1 = (uvRaw1 * (vcc / 1023.0f)) / 0.1118f;
  int diodeRaw = readSmoothedAnalog(photodiodePin);
  float photoVoltage = diodeRaw * (vcc / 1023.0f);
  int turbidityRaw2 = readSmoothedAnalog(turbidityPin2, 20);
  float turbidityVoltage2 = turbidityRaw2 * (vcc / 1023.0f);

  float sum1 = r1 + g1 + b1;
  uint8_t R1s = gamma22(sum1 > 0 ? (r1 / sum1) : 0);
  uint8_t G1s = gamma22(sum1 > 0 ? (g1 / sum1) : 0);
  uint8_t B1s = gamma22(sum1 > 0 ? (b1 / sum1) : 0);
  float sum2 = r2 + g2 + b2;
  uint8_t R2s = gamma22(sum2 > 0 ? (r2 / sum2) : 0);
  uint8_t G2s = gamma22(sum2 > 0 ? (g2 / sum2) : 0);
  uint8_t B2s = gamma22(sum2 > 0 ? (b2 / sum2) : 0);

  Serial.print("{");
  Serial.print("\"temp\":");        Serial.print(temperature, 1); Serial.print(",");
  Serial.print("\"uvLed\":");       Serial.print(uvState ? 1 : 0); Serial.print(",");
  Serial.print("\"uvIntensity\":"); Serial.print(uvLedIntensity);  Serial.print(",");
  Serial.print("\"pump\":");        Serial.print(pumpState ? 1 : 0); Serial.print(",");
  Serial.print("\"pump2\":");       Serial.print(pump2State ? 1 : 0); Serial.print(",");
  Serial.print("\"pumpSpeed\":");   Serial.print(pumpState ? lastPumpSpeed : 0); Serial.print(",");
  Serial.print("\"flowRate\":");    Serial.print(pumpState ? getCalibratedFlowRate() : 0.0, 4); Serial.print(",");
  Serial.print("\"uv1\":");         Serial.print(uvIndex1, 1);     Serial.print(",");
  Serial.print("\"photodiode\":");  Serial.print(photoVoltage, 2); Serial.print(",");
  Serial.print("\"turbidity\":");   Serial.print(0.00, 2);         Serial.print(",");
  Serial.print("\"turbidity2\":");  Serial.print(turbidityVoltage2, 2); Serial.print(",");
  Serial.print("\"rgb1_r\":"); Serial.print((int)R1s); Serial.print(",");
  Serial.print("\"rgb1_g\":"); Serial.print((int)G1s); Serial.print(",");
  Serial.print("\"rgb1_b\":"); Serial.print((int)B1s); Serial.print(",");
  Serial.print("\"rgb2_r\":"); Serial.print((int)R2s); Serial.print(",");
  Serial.print("\"rgb2_g\":"); Serial.print((int)G2s); Serial.print(",");
  Serial.print("\"rgb2_b\":"); Serial.print((int)B2s); Serial.print(",");
  Serial.print("\"rgb1_r_raw\":"); Serial.print((int)r1); Serial.print(",");
  Serial.print("\"rgb1_g_raw\":"); Serial.print((int)g1); Serial.print(",");
  Serial.print("\"rgb1_b_raw\":"); Serial.print((int)b1); Serial.print(",");
  Serial.print("\"rgb1_c_raw\":"); Serial.print((int)c1); Serial.print(",");
  Serial.print("\"rgb2_r_raw\":"); Serial.print((int)r2); Serial.print(",");
  Serial.print("\"rgb2_g_raw\":"); Serial.print((int)g2); Serial.print(",");
  Serial.print("\"rgb2_b_raw\":"); Serial.print((int)b2); Serial.print(",");
  Serial.print("\"rgb2_c_raw\":"); Serial.print((int)c2);
  Serial.println("}");

  delay(50);
}
