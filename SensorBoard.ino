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
  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  bufferLogic.initialize();
  Serial.println("\nSetup complete. Starting 30s interval transmission with buffer.");
}

void loop() {
  unsigned long currentTime = millis();

  // Read sensor data every 3 seconds
  if (currentTime - lastSensorReading >= sensorReadInterval) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      SensorReading reading = {t, h, true, timeClient.getEpochTime()};
      bufferLogic.addReading(reading);
      Serial.printf("New reading: %.1f°C, %.1f%%\n", t, h);
    } else {
      Serial.println("Sensor read failed. Skipping buffer update.");
    }
    lastSensorReading = currentTime;
  }

  // Transmit data every 30 seconds
  if (currentTime - lastDataTransmission >= transmissionInterval) {
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
      Serial.println("No valid data in buffer. Skipping transmission.");
    }
    lastDataTransmission = currentTime;
  }
  delay(50);
} 