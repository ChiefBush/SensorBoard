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
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "./include/ConfigManager.h"
#include "./include/SensorModel.h"
#include "./include/TransmitHandler.h"
#include "./include/JSONView.h"
#include "GPSHandler.h"

// WiFi credentials
const char* ssid = "SidOmi";
const char* password = "28102003Omi";

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
const unsigned long sensorReadInterval = 10000;    // Read DHT11 every 10 seconds
const unsigned long transmissionInterval = 60000;   // Send data every 60 seconds

WiFiClientSecure secureClient;
HTTPClient http;

ConfigManager config;
SensorModel sensor;
BufferLogic bufferLogic;
TransmitHandler transmitter;
JSONView jsonView;

GPSHandler gpsHandler;

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting up...");
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);  // Set WiFi to station mode
  WiFi.disconnect();    // Disconnect from any previous connection
  delay(1000);         // Give some time to disconnect
  
  Serial.print("Connecting to WiFi network: ");
  Serial.println(ssid);
  Serial.print("Signal strength: ");
  Serial.println(WiFi.RSSI());
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Print connection status
    switch(WiFi.status()) {
      case WL_IDLE_STATUS:
        Serial.print("\nStatus: Idle");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.print("\nStatus: SSID not found");
        break;
      case WL_CONNECT_FAILED:
        Serial.print("\nStatus: Connection failed");
        break;
      case WL_CONNECTION_LOST:
        Serial.print("\nStatus: Connection lost");
        break;
      case WL_DISCONNECTED:
        Serial.print("\nStatus: Disconnected");
        break;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("\nFailed to connect to WiFi!");
    Serial.println("Please check:");
    Serial.println("1. WiFi network is in range");
    Serial.println("2. SSID and password are correct");
    Serial.println("3. Network is 2.4GHz (not 5GHz)");
    Serial.println("4. Network allows new connections");
    // Continue anyway to see if we can get time sync
  }
  
  // Initialize time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for time sync...");
  while (time(nullptr) < 1000000000) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nTime synchronized!");
  
  // Initialize components with their configurations
  SensorConfig sensorConfig;
  sensorConfig.pin = DHT_PIN;
  sensorConfig.type = DHT_TYPE;
  sensor.initialize(sensorConfig);
  
  BufferConfig bufferConfig;
  bufferConfig.maxSize = 100;  // Maximum number of readings to store
  bufferLogic.initialize(bufferConfig);
  
  transmitter.initialize(config.getNetworkConfig());
  
  // Print initial status
  jsonView.printSystemStatus(config, sensor, bufferLogic);

  dht.begin();
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  Serial.println("\nSetup complete. Starting 60s interval transmission with buffer.");

  // Initialize GPS
  gpsHandler.begin();
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