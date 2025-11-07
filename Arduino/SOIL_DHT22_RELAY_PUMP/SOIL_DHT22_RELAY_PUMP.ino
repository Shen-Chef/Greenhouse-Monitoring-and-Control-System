#define BLYNK_TEMPLATE_ID "TMPL33wSwIpvB"
#define BLYNK_TEMPLATE_NAME "Greenhouse Monitoring and Control System"
#define BLYNK_AUTH_TOKEN "nXZ1OWqfJpubOU9RmiajKTpk3iwnzZyF"

// --- Libraries ---
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <time.h>

// --- WiFi Credentials ---
char ssid[] = "SHEN";
char pass[] = "123456789";

// --- API Endpoint ---
const char* serverName = "http://172.20.10.2:5000/api/logs";  // Flask server IP

// --- Pin Configuration ---
#define DHTPIN 13
#define DHTTYPE DHT11
#define SOIL_PIN 32
#define RELAY_PIN 26

// --- Blynk Virtual Pins ---
#define VP_TEMP V3
#define VP_HUM  V4
#define VP_SOIL V5
#define VP_PUMP V6

// --- Sensor & Timer Objects ---
DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

// --- Calibration Values ---
const int AIR_VALUE = 4095;   // Dry
const int WATER_VALUE = 1200; // Wet
const int DRY_THRESHOLD = 40; // Below this = dry

// --- Global Variables ---
int soilValue = 0;
int moisturePercent = 0;
bool pumpState = false;

// --- Time Function ---
String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

// --- Function: Send Data to Blynk ---
void sendToBlynk(float temperature, float humidity) {
  Blynk.virtualWrite(VP_TEMP, temperature);
  Blynk.virtualWrite(VP_HUM, humidity);
  Blynk.virtualWrite(VP_SOIL, moisturePercent);
  Blynk.virtualWrite(VP_PUMP, pumpState);
}

// --- Function: Send Data to Flask API ---
void sendToAPI(float temperature, float humidity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    String timestamp = getFormattedTime();
    String jsonPayload = "{";
    jsonPayload += "\"temperature\":" + String(temperature) + ",";
    jsonPayload += "\"humidity\":" + String(humidity) + ",";
    jsonPayload += "\"moisture\":" + String(moisturePercent) + ",";
    jsonPayload += "\"timestamp\":\"" + timestamp + "\"";
    jsonPayload += "}";

    int code = http.POST(jsonPayload);
    if (code > 0)
      Serial.println("✅ Data sent to API");
    else
      Serial.println("⚠️ Failed to send to API");
    http.end();
  } else {
    Serial.println("⚠️ WiFi not connected!");
  }
}

// --- Function: Read Sensors and Control Pump ---
void readSensorsAndControl() {
  // Read Soil Moisture
  soilValue = analogRead(SOIL_PIN);
  moisturePercent = map(soilValue, AIR_VALUE, WATER_VALUE, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // Read DHT Sensor
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Display Sensor Data
  Serial.print("🌱 Soil: ");
  Serial.print(moisturePercent);
  Serial.print("% | 🌡️ Temp: ");
  Serial.print(temperature);
  Serial.print("°C | 💧 Humidity: ");
  Serial.print(humidity);
  Serial.println("%");

  // --- Pump Control Logic ---
  bool shouldPumpBeOn = (moisturePercent < DRY_THRESHOLD);

  if (shouldPumpBeOn != pumpState) {
    pumpState = shouldPumpBeOn;
    if (pumpState) {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("💧 Soil dry → Pump ON");
    } else {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("✅ Soil moist → Pump OFF");
    }
  }

  // Send Data to Blynk and API
  sendToBlynk(temperature, humidity);
  sendToAPI(temperature, humidity);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();
  delay(1000);

  Serial.println("\n🌿 ESP32 Smart Irrigation System Starting...");

  // WiFi + Blynk Connection
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Wait for WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Connected!");
  Serial.println(WiFi.localIP());

  // Setup NTP Time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("⏰ Syncing Time...");
  while (time(nullptr) < 3000) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\n✅ Time Synced!");

  // Timer: Read sensors every 5 seconds
  timer.setInterval(5000L, readSensorsAndControl);

  Serial.println("🌿 Greenhouse Monitoring & Control System Ready!");
}

// --- Loop ---
void loop() {
  Blynk.run();
  timer.run();
}
