// ESP8266 AHT10 + Neo-6M GPS Data Logger - Simplified Version
// Install: "Adafruit AHTX0" by Adafruit in Library Manager

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// WiFi credentials 
const char* ssid = "KRC-101C";
const char* password = "krc101c@";

// Server configuration
const char* serverURL = "https://lostdevs.io/ctrl1/master.php";
const char* secretKey = "lostdev-sensor1-1008200303082003";

// AHT10 sensor configuration
Adafruit_AHTX0 aht;

// GPS configuration
#define GPS_RX_PIN 12    // D6 (GPIO12) -> GPS TX
#define GPS_TX_PIN 13    // D7 (GPIO13) -> GPS RX
#define GPS_BAUD 9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;

// NTP Client - using Google's time servers with IST offset
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 19800, 60000); // IST = UTC + 5:30 (19800 seconds)

// BootConfig.h
const int SENSOR_ID = 1;

// Default coordinates
float DEFAULT_LATITUDE = 28.637290;   
float DEFAULT_LONGITUDE = 77.170077;  

// Timing intervals
unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
unsigned long lastGPSReading = 0;
unsigned long lastStatusUpdate = 0;

const unsigned long sensorReadInterval = 8000;
const unsigned long gpsReadInterval = 15000;
const unsigned long transmissionInterval = 15000;
const unsigned long statusUpdateInterval = 120000;

// Cached sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
  unsigned long lastReadTime;
} cachedData = {-999.0, -999.0, false, 0};

// GPS data structure
struct GPSData {
  float latitude;
  float longitude;
  bool isValid;
  unsigned long lastReadTime;
  int satellites;
} gpsData = {DEFAULT_LATITUDE, DEFAULT_LONGITUDE, false, 0, 0};

// Error tracking
int wifiFailures = 0;
int sensorFailures = 0;
int httpFailures = 0;
int gpsFailures = 0;
unsigned long totalTransmissions = 0;
bool setupCompleted = false;
bool ntpSynced = false;

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  if (setupCompleted) {
    Serial.println("SETUP ALREADY COMPLETED - PREVENTING RESTART LOOP");
    return;
  }
  
  Serial.println("\n\n=== ESP8266 AHT10 + Neo-6M GPS Data Logger ===");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId(), HEX);
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("Target URL: https://lostdevs.io/ctrl1/master.php");
  Serial.println("GPS Wiring: D6(GPIO12)->TX, D7(GPIO13)->RX, 3.3V->VCC, GND->GND");
  Serial.println("AHT10 Wiring: D2(GPIO4)->SDA, D1(GPIO5)->SCL, 3.3V->VCC, GND->GND");
  Serial.println("===============================================\n");
  
  // Initialize AHT10 sensor
  Serial.println("Testing AHT10 sensor...");
  bool ahtWorking = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("AHT10 Attempt %d/3...", attempt);
    
    if (aht.begin()) {
      delay(2000);
      sensors_event_t humidity, temp;
      aht.getEvent(&humidity, &temp);
      
      float testTemp = temp.temperature;
      float testHumid = humidity.relative_humidity;
      
      if (!isnan(testTemp) && !isnan(testHumid) && testTemp > -40 && testTemp < 80 && testHumid >= 0 && testHumid <= 100) {
        Serial.printf(" ✅ SUCCESS - Temp: %.1f°C, Humidity: %.1f%%\n", testTemp, testHumid);
        ahtWorking = true;
        cachedData.temperature = testTemp;
        cachedData.humidity = testHumid;
        cachedData.isValid = true;
        cachedData.lastReadTime = millis();
        break;
      } else {
        Serial.printf(" ❌ FAILED - Invalid readings\n");
      }
    } else {
      Serial.println(" ❌ FAILED - Cannot initialize");
    }
    delay(2000);
  }
  
  if (!ahtWorking) {
    Serial.println("❌ AHT10 sensor failed! Check wiring.");
  }
  
  // Initialize GPS
  Serial.println("Initializing GPS...");
  gpsSerial.begin(GPS_BAUD);
  delay(1000);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize NTP
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Initializing NTP...");
    initializeNTP();
  }
  
  setupCompleted = true;
  Serial.println("🚀 Setup completed! Starting main loop...\n");
}

void loop() {
  if (!setupCompleted) {
    delay(5000);
    return;
  }
  
  unsigned long currentTime = millis();
  
  // Read GPS data
  if (currentTime - lastGPSReading >= gpsReadInterval) {
    readGPSData();
    lastGPSReading = currentTime;
  }
  
  // Read sensor data
  if (currentTime - lastSensorReading >= sensorReadInterval) {
    readSensorData();
    lastSensorReading = currentTime;
  }
  
  // Transmit data
  if (currentTime - lastDataTransmission >= transmissionInterval) {
    transmitData();
    lastDataTransmission = currentTime;
  }
  
  // Status update
  if (currentTime - lastStatusUpdate >= statusUpdateInterval) {
    printSystemStatus();
    lastStatusUpdate = currentTime;
  }
  
  // Process GPS data continuously
  if (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  
  delay(500);
}

void readGPSData() {
  bool newData = false;
  unsigned long gpsStart = millis();
  
  while (millis() - gpsStart < 2000) {
    if (gpsSerial.available()) {
      if (gps.encode(gpsSerial.read())) {
        newData = true;
      }
    }
    delay(10);
  }
  
  if (newData && gps.location.isValid()) {
    gpsData.latitude = gps.location.lat();
    gpsData.longitude = gps.location.lng();
    gpsData.isValid = true;
    gpsData.lastReadTime = millis();
    gpsData.satellites = gps.satellites.value();
    
    Serial.printf("🛰️  GPS: %.6f, %.6f (Sats: %d)\n", 
                  gpsData.latitude, gpsData.longitude, gpsData.satellites);
  } else {
    if (!gpsData.isValid) {
      gpsFailures++;
      Serial.println("⚠️  GPS: No valid fix, using default coordinates");
    }
  }
}

void readSensorData() {
  // Ensure WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📶 WiFi reconnecting...");
    wifiFailures++;
    connectToWiFi();
  }
  
  // Update NTP time periodically
  if (!ntpSynced || (millis() % 300000 < 1000)) {
    timeClient.update();
    if (timeClient.getEpochTime() > 1640995200) {
      ntpSynced = true;
    }
  }
  
  // Read AHT10 sensor
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  
  float temperature = temp.temperature;
  float humidityValue = humidity.relative_humidity;
  
  if (isnan(temperature) || isnan(humidityValue) || temperature < -40 || temperature > 80 || humidityValue < 0 || humidityValue > 100) {
    sensorFailures++;
    Serial.printf("⚠️  AHT10 read failed (Failures: %d)\n", sensorFailures);
  } else {
    cachedData.temperature = temperature;
    cachedData.humidity = humidityValue;
    cachedData.isValid = true;
    cachedData.lastReadTime = millis();
    
    Serial.printf("🌡️  Sensor: %.1f°C, %.1f%%\n", temperature, humidityValue);
  }
}

void transmitData() {
  if (!cachedData.isValid) {
    Serial.println("❌ No valid sensor data to transmit!");
    return;
  }
  
  totalTransmissions++;
  
  String timestamp = getTimestamp();
  float currentLat = gpsData.isValid ? gpsData.latitude : DEFAULT_LATITUDE;
  float currentLng = gpsData.isValid ? gpsData.longitude : DEFAULT_LONGITUDE;
  
  String payload = createPayload(
    cachedData.temperature, 
    cachedData.humidity, 
    currentLat,
    currentLng,
    timestamp
  );
  
  bool success = sendDataToServer(payload);
  if (!success) {
    httpFailures++;
  }
  
  Serial.println("📤 DATA TRANSMITTED");
  Serial.printf("   📊 Temp: %.1f°C, Humidity: %.1f%%\n", cachedData.temperature, cachedData.humidity);
  Serial.printf("   🛰️  Location: %.6f, %.6f %s\n", currentLat, currentLng, gpsData.isValid ? "(GPS)" : "(Default)");
  Serial.printf("   🕒 Timestamp: %s\n", timestamp.c_str());
  Serial.printf("   📈 Status: %s\n", success ? "✅ SUCCESS" : "❌ FAILED");
  Serial.println();
}

void printSystemStatus() {
  Serial.println("=== SYSTEM STATUS ===");
  Serial.printf("⏱️  Uptime: %.1f minutes\n", millis() / 60000.0);
  Serial.printf("📶 WiFi: %s", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" (RSSI: %d dBm)", WiFi.RSSI());
  }
  Serial.println();
  Serial.printf("🛰️  GPS: %s", gpsData.isValid ? "Active" : "Inactive");
  if (gpsData.isValid) {
    Serial.printf(" (Sats: %d)", gpsData.satellites);
  }
  Serial.println();
  Serial.printf("🌡️  Sensor: %s", cachedData.isValid ? "Active" : "Failed");
  if (cachedData.isValid) {
    Serial.printf(" (%.1f°C, %.1f%%)", cachedData.temperature, cachedData.humidity);
  }
  Serial.println();
  Serial.printf("📊 Stats: %lu sent, %d WiFi fails, %d sensor fails, %d GPS fails, %d HTTP fails\n", 
                totalTransmissions, wifiFailures, sensorFailures, gpsFailures, httpFailures);
  Serial.printf("💾 Memory: %d bytes free\n", ESP.getFreeHeap());
  Serial.println("====================\n");
}

void connectToWiFi() {
  Serial.print("📶 Connecting to WiFi");
  
  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✅ Connected!");
    Serial.printf("   IP: %s, Signal: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println(" ❌ Failed!");
  }
}

void initializeNTP() {
  Serial.println("🕒 Initializing NTP...");
  timeClient.begin();
  
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("   NTP attempt %d/5...", attempt);
    
    if (timeClient.forceUpdate()) {
      unsigned long epochTime = timeClient.getEpochTime();
      if (epochTime > 1640995200) {
        Serial.printf(" ✅ SUCCESS - IST Time: %s\n", timeClient.getFormattedTime().c_str());
        ntpSynced = true;
        return;
      }
    }
    
    Serial.println(" ❌ Failed");
    delay(2000);
  }
  
  Serial.println("⚠️  NTP sync failed - will use system time");
  ntpSynced = false;
}

String getTimestamp() {
  if (ntpSynced) {
    unsigned long epochTime = timeClient.getEpochTime();
    if (epochTime > 1640995200) {
      return formatTimestamp(epochTime);
    }
  }
  
  // Fallback: use estimated IST time based on system uptime
  unsigned long estimatedEpoch = 1749705600 + (millis() / 1000) + 19800; // Add IST offset
  return formatTimestamp(estimatedEpoch);
}

String formatTimestamp(unsigned long epochTime) {
  // Format IST timestamp (no timezone suffix as requested)
  unsigned long days = epochTime / 86400;
  unsigned long secondsInDay = epochTime % 86400;
  
  int hour = secondsInDay / 3600;
  int minute = (secondsInDay % 3600) / 60;
  int second = secondsInDay % 60;
  
  // Simple date calculation (approximate)
  int year = 1970;
  int daysRemaining = days;
  
  while (daysRemaining >= 365) {
    int daysInYear = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
    if (daysRemaining >= daysInYear) {
      daysRemaining -= daysInYear;
      year++;
    } else {
      break;
    }
  }
  
  int month = 1;
  int day = 1;
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    daysInMonth[1] = 29;
  }
  
  while (daysRemaining >= daysInMonth[month - 1] && month <= 12) {
    daysRemaining -= daysInMonth[month - 1];
    month++;
  }
  day = daysRemaining + 1;
  
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d", 
           year, month, day, hour, minute, second);
  
  return String(timestamp);
}

String createPayload(float temp, float humid, float lat, float lng, String timestamp) {
  String payload = "";
  
  payload += "secret=" + String(secretKey);
  payload += "&sensor_unique_id=" + String(SENSOR_ID);
  payload += "&Temperature(K)=" + String(temp + 273.15, 2);
  payload += "&humidity(%)=" + String((int)round(humid));
  payload += "&sensor_longitude=" + String(lng, 6);
  payload += "&sensor_latitude=" + String(lat, 6);
  payload += "&receiving_date=" + timestamp;
  
  return payload;
}

bool sendDataToServer(String payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Cannot send data - WiFi not connected");
    return false;
  }
  
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  
  http.begin(secureClient, serverURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("User-Agent", "ESP8266-Sensor/1.0");
  http.setTimeout(15000);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    if (httpResponseCode != 200 || totalTransmissions <= 5) {
      Serial.printf("🌐 HTTP %d: %s\n", httpResponseCode, response.c_str());
    }
    http.end();
    return (httpResponseCode >= 200 && httpResponseCode < 300);
  } else {
    Serial.printf("❌ HTTP Error: %d\n", httpResponseCode);
    http.end();
    return false;
  }
}
