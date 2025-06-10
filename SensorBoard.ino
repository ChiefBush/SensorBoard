// =============================================================================
// SensorBoard.ino - Main Arduino Sketch (No Crypto, With Buffer, 30s Interval)
// =============================================================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "./include/BufferLogic.h"
#include <time.h>

// WiFi credentials
const char* ssid = "KRC-101C";
const char* password = "krc101c@";

// Server configuration
const char* serverURL = "https://lostdevs.io/ctrl1/master.php";
const char* secretKey = "lostdev-sensor1-1008200303082003";

// DHT sensor configuration
#define DHT_PIN 2
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

const int SENSOR_ID = 1;
const float LATITUDE = 28.637270;
const float LONGITUDE = 77.170277;

unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
const unsigned long sensorReadInterval = 3000;    // Read DHT11 every 3 seconds
const unsigned long transmissionInterval = 30000; // Send data every 30 seconds

BufferLogic bufferLogic;

WiFiClientSecure secureClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");
  Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  BufferConfig config;
  bufferLogic.initialize(config);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" done!");
  Serial.println("\nSetup complete. Starting 30s interval transmission with buffer.");
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) { // Print every 5 seconds
    Serial.print("Free Heap: ");
    Serial.println(ESP.getFreeHeap());
    lastPrint = millis();
  }
  Serial.println("Starting loop...");
  timeClient.update();
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReading >= sensorReadInterval) {
    Serial.println("Reading sensor...");
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      SensorReading reading;
      reading.temperature = t;
      reading.humidity = h;
      reading.isValid = true;
      reading.timestamp = timeClient.getEpochTime();
      bufferLogic.addReading(reading);
      Serial.printf("New reading: %.1f°C, %.1f%%\n", t, h);
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
    lastSensorReading = currentMillis;
  }
  if (currentMillis - lastDataTransmission >= transmissionInterval) {
    Serial.println("Transmitting data...");
    SensorReading dataToSend = bufferLogic.getDataForTransmission();
    if (dataToSend.isValid) {
      String payload = "";
      payload += "secret=" + String(secretKey);
      payload += "&sensor_unique_id=" + String(SENSOR_ID);
      payload += "&Temperature(K)=" + String(dataToSend.temperature + 273.15, 2);
      payload += "&humidity(%)=" + String((int)round(dataToSend.humidity));
      payload += "&sensor_longitude=" + String(LONGITUDE, 6);
      payload += "&sensor_latitude=" + String(LATITUDE, 6);
      payload += "&receiving_date=" + String(dataToSend.timestamp);
      payload += "&rdf_metadata=sensor_type:DHT11,location:indoor,purpose:environmental_monitoring,transmission_mode:buffered";
      payload += "&download_metadata=chip_id:" + String(ESP.getChipId(), HEX);

      secureClient.setInsecure();
      http.begin(secureClient, serverURL);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int httpResponseCode = http.POST(payload);
      Serial.printf("HTTP POST code: %d\n", httpResponseCode);
      http.end();
    } else {
      Serial.println("No valid data to transmit.");
    }
    lastDataTransmission = currentMillis;
  }
  time_t now = time(nullptr);
  Serial.print("Current timestamp: ");
  Serial.println(now);
  Serial.println("End of loop.");
} 