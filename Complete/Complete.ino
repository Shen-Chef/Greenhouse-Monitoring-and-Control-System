/************ BLYNK CONFIG ************/
#define BLYNK_TEMPLATE_ID "TMPL33wSwIpvB"
#define BLYNK_TEMPLATE_NAME "Greenhouse Monitoring and Control System"
#define BLYNK_AUTH_TOKEN "nXZ1OWqfJpubOU9RmiajKTpk3iwnzZyF"

/************ LIBRARIES ************/
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include "DHT.h"
#include <MQUnifiedsensor.h>

/************ WIFI ************/
char ssid[] = "SHEN";
char pass[] = "123456789";

/************ PINS ************/
#define DHTPIN       15
#define DHTTYPE      DHT11
#define SOIL_PIN     32
#define RELAY_PUMP   26

#define fan    18
#define fan2   19
#define ENAFAN 21

#define MQ135_PIN    34

#define LDR_PIN      33
#define BULB_RELAY   23

/************ OBJECTS ************/
DHT dht(DHTPIN, DHTTYPE);

#define BOARD "ESP-32"
#define VOLTAGE 3.3
#define ADC_RES 12
#define RATIO_CLEAN_AIR 3.6

MQUnifiedsensor MQ135(BOARD, VOLTAGE, ADC_RES, MQ135_PIN, "MQ-135");

BlynkTimer timer;

/************ VARIABLES ************/
int moisturePercent = 0;
bool pumpRunning = false;
bool fanState = false;

float temperature = 0;
float humidity = 0;
float co2_percent = 0;

int ldrThreshold = 1500;

// 🔹 BLYNK FAN CONTROL VARIABLES
bool manualFanControl = false;
bool manualFanRequestedState = false;

/************ VIRTUAL PINS ************/
#define VP_TEMP V3
#define VP_HUM  V4
#define VP_SOIL V5
#define VP_PUMP V6
#define VP_FAN  V7
#define VP_CO2  V11

/************ SOIL MAP ************/
const int AirValue = 4095;
const int WaterValue = 1200;

/************ FUNCTIONS ************/
int getMoisturePercent(int raw) {
  raw = constrain(raw, WaterValue, AirValue);
  return map(raw, AirValue, WaterValue, 0, 100);
}

void sendToBlynk() {
  Blynk.virtualWrite(VP_TEMP, temperature);
  Blynk.virtualWrite(VP_HUM, humidity);
  Blynk.virtualWrite(VP_SOIL, moisturePercent);
  Blynk.virtualWrite(VP_PUMP, pumpRunning);
  Blynk.virtualWrite(VP_FAN, fanState ? 1 : 0);
  Blynk.virtualWrite(VP_CO2, co2_percent);
}

/************ BLYNK FAN CONTROL ************/
BLYNK_WRITE(VP_FAN) {
  int v = param.asInt();
  if (v == 1) {
    manualFanControl = true;
    manualFanRequestedState = true;
    digitalWrite(ENAFAN, HIGH);
    fanState = true;
    Serial.println("🌀 BLYNK: Manual Fan ON");
  } else {
    manualFanControl = false;
    manualFanRequestedState = false;
    Serial.println("🌀 BLYNK: Manual Fan OFF → Auto mode");
  }
  Blynk.virtualWrite(VP_FAN, fanState ? 1 : 0);
}
/************ SETUP ************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(fan, OUTPUT);
  pinMode(fan2, OUTPUT);
  pinMode(ENAFAN, OUTPUT);
  pinMode(BULB_RELAY, OUTPUT);

  digitalWrite(RELAY_PUMP, LOW);
  digitalWrite(BULB_RELAY, LOW);
  digitalWrite(fan, HIGH);
  digitalWrite(fan2, LOW);
  digitalWrite(ENAFAN, LOW);
  fanState = false;
  pumpRunning = false;

  dht.begin();

  MQ135.setRegressionMethod(1);
  MQ135.setA(110.47);
  MQ135.setB(-2.862);

  Serial.print("Calibrating MQ-135");
  float r0 = 0;
  for (int i = 0; i < 10; i++) {
    MQ135.update();
    r0 += MQ135.calibrate(RATIO_CLEAN_AIR);
    Serial.print(".");
    delay(500);
  }
  MQ135.setR0(r0 / 10);
  Serial.println(" ✅ DONE");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(5000L, sendToBlynk);

  Serial.println("🌱 GREENHOUSE SYSTEM STARTED 🌱");
}

/************ LOOP ************/
void loop() {
  Blynk.run();
  timer.run();

  /************ SOIL ************/
  int soilRaw = analogRead(SOIL_PIN);
  moisturePercent = getMoisturePercent(soilRaw);

  /************ DHT ************/
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity = h;
  }

  /************ PUMP CONTROL ************/
  if (moisturePercent <= 25 && !pumpRunning) {
    digitalWrite(RELAY_PUMP, HIGH);
    pumpRunning = true;
    Serial.println("💧 Soil Dry → Pump ON");
  }
  else if (moisturePercent >= 50 && pumpRunning) {
    digitalWrite(RELAY_PUMP, LOW);
    pumpRunning = false;
    Serial.println("💧 Soil Wet → Pump OFF");
  }

  /************ MQ135 ************/
  MQ135.update();
  float ppm = MQ135.readSensor();
  co2_percent = ppm / 10000.0;
  co2_percent = constrain(co2_percent, 0.03, 0.50);

  // Fan Control
  if (manualFanControl) {
    digitalWrite(ENAFAN, manualFanRequestedState ? HIGH : LOW);
    fanState = manualFanRequestedState;
    delay(200);
  } else {
    digitalWrite(ENAFAN, HIGH);
    fanState = true;
    Serial.println("AUTO FAN ON");
    delay(4000);
    digitalWrite(ENAFAN, LOW);
    fanState = false;
    Serial.println("AUTO FAN OFF");
    delay(4000);
  }

  /************ SERIAL OUTPUT (ALL PARAMETERS) ************/
  Serial.print("🌡 Temp: ");
  Serial.print(temperature);
  Serial.print(" °C | 💧 Humidity: ");
  Serial.print(humidity);
  Serial.print(" % | 🌱 Soil: ");
  Serial.print(moisturePercent);
  Serial.print("% | Pump: ");
  Serial.print(pumpRunning ? "ON" : "OFF");
  Serial.print(" | CO2: ");
  Serial.print(co2_percent * 100);
  Serial.println(" %");

  /************ 🌞🌑 LDR BULB CONTROL ************/
  int ldrValue = analogRead(LDR_PIN);
  Serial.print("☀️ LDR: ");
  Serial.print(ldrValue);

  if (ldrValue > ldrThreshold) {
    digitalWrite(BULB_RELAY, HIGH);
    Serial.println(" 🌑 DARK → Bulb ON");
  } else {
    digitalWrite(BULB_RELAY, LOW);
    Serial.println(" ☀️ LIGHT → Bulb OFF");
  }

  delay(1000);
}
