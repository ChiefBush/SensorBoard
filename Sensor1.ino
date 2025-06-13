#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// WiFi credentials 
const char* ssid = "SidOmi";
const char* password = "28102003Omi";

// Server configuration
const char* serverURL = "https://lostdevs.io/ctrl1/master.php";
const char* secretKey = "lostdev-sensor2-1008200302082003";

// DHT sensor configuration
#define DHT_PIN 2        // GPIO2 (D4 on NodeMCU)
#define DHT_TYPE DHT11   

DHT dht(DHT_PIN, DHT_TYPE);

// GPS configuration - FIXED PINS
#define GPS_RX_PIN 4     // D2 (GPIO4) -> GPS TX
#define GPS_TX_PIN 5     // D1 (GPIO5) -> GPS RX
#define GPS_BAUD 9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;

// NTP Client - Using Google NTP for better reliability, IST timezone
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 19800, 60000); // IST = UTC + 5:30 = 19800 seconds

// BootConfig.h
const int SENSOR_ID = 1;

// Default coordinates (fallback if GPS fails)
float DEFAULT_LATITUDE = 0;   
float DEFAULT_LONGITUDE = 0;  

// Timing intervals
unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
unsigned long lastGPSReading = 0;
unsigned long lastStatusUpdate = 0;

const unsigned long sensorReadInterval = 10000;     // Read DHT11 every 10 seconds
const unsigned long gpsReadInterval = 15000;       // Read GPS every 15 seconds
const unsigned long transmissionInterval = 60000;  // Send data every 60 seconds
const unsigned long statusUpdateInterval = 120000; // Status update every 2 minutes

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
  float hdop;
} gpsData = {DEFAULT_LATITUDE, DEFAULT_LONGITUDE, false, 0, 0, 99.99};

// Error tracking
int wifiFailures = 0;
int sensorFailures = 0;
int httpFailures = 0;
int gpsFailures = 0;
unsigned long lastSuccessfulReading = 0;
unsigned long totalTransmissions = 0;
bool setupCompleted = false;

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  // Prevent setup from running multiple times
  if (setupCompleted) {
    Serial.println("SETUP ALREADY COMPLETED - PREVENTING RESTART LOOP");
    return;
  }
  
  Serial.println("\n\n=== ESP8266 DHT11 + Neo-6M GPS Data Logger ===");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId(), HEX);
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Reset Reason: ");
  Serial.println(ESP.getResetReason());
  Serial.println("Target URL: https://lostdevs.io/ctrl1/master.php");
  Serial.printf("Sensor read interval: %lu s\n", sensorReadInterval/1000);
  Serial.printf("GPS read interval: %lu s\n", gpsReadInterval/1000);
  Serial.printf("Transmission interval: %lu s\n", transmissionInterval/1000);
  Serial.println("GPS Wiring: D2(GPIO4)->TX, D1(GPIO5)->RX, 3.3V->VCC, GND->GND");
  Serial.println("DHT11 Wiring: D4(GPIO2)->DATA, 3.3V->VCC, GND->GND");
  Serial.println("===============================================\n");
  
  // Test basic functionality first
  Serial.println("=== HARDWARE DIAGNOSTIC ===");
  
  // 1. Test DHT sensor first
  Serial.println("1. Testing DHT11 sensor...");
  dht.begin();
  delay(3000);
  
  bool dhtWorking = false;
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("   DHT Attempt %d/5...", attempt);
    delay(2500);
    
    float testTemp = dht.readTemperature();
    float testHumid = dht.readHumidity();
    
    if (!isnan(testTemp) && !isnan(testHumid) && testTemp > -40 && testTemp < 80 && testHumid >= 0 && testHumid <= 100) {
      Serial.printf(" ✅ SUCCESS - Temp: %.1f°C, Humidity: %.1f%%\n", testTemp, testHumid);
      dhtWorking = true;
      cachedData.temperature = testTemp;
      cachedData.humidity = testHumid;
      cachedData.isValid = true;
      cachedData.lastReadTime = millis();
      break;
    } else {
      Serial.printf(" ❌ FAILED - Temp: %.1f, Humidity: %.1f\n", testTemp, testHumid);
    }
  }
  
  if (!dhtWorking) {
    Serial.println("❌ CRITICAL: DHT11 sensor completely failed!");
    Serial.println("   Check wiring: DATA->D4(GPIO2), VCC->3.3V, GND->GND");
    Serial.println("   Check if sensor is genuine DHT11");
  }
  
  // 2. Test GPS module
  Serial.println("\n2. Testing Neo-6M GPS module...");
  gpsSerial.begin(GPS_BAUD);
  delay(1000);
  
  // Clear any existing data
  while (gpsSerial.available()) {
    gpsSerial.read();
  }
  
  Serial.println("   Listening for GPS data for 10 seconds...");
  bool gpsDataReceived = false;
  bool gpsValidFix = false;
  unsigned long gpsTestStart = millis();
  int rawBytesReceived = 0;
  int validSentences = 0;
  
  while (millis() - gpsTestStart < 10000) {
    if (gpsSerial.available()) {
      char c = gpsSerial.read();
      rawBytesReceived++;
      gpsDataReceived = true;
      
      if (gps.encode(c)) {
        validSentences++;
        if (gps.location.isValid()) {
          gpsValidFix = true;
          gpsData.latitude = gps.location.lat();
          gpsData.longitude = gps.location.lng();
          gpsData.isValid = true;
          gpsData.satellites = gps.satellites.value();
          gpsData.hdop = gps.hdop.hdop();
        }
      }
    }
    delay(10);
  }
  
  Serial.printf("   Raw bytes received: %d\n", rawBytesReceived);
  Serial.printf("   Valid NMEA sentences: %d\n", validSentences);
  
  if (!gpsDataReceived) {
    Serial.println("   ❌ NO GPS DATA - Check wiring and power");
    Serial.println("   Wiring: GPS_TX->D2(GPIO4), GPS_RX->D1(GPIO5), VCC->3.3V, GND->GND");
  } else if (!gpsValidFix) {
    Serial.println("   ⚠️  GPS RESPONDING but NO FIX - Move to open sky area");
    Serial.printf("   Satellites visible: %d\n", gps.satellites.value());
  } else {
    Serial.printf("   ✅ GPS WORKING - Location: %.6f, %.6f\n", gpsData.latitude, gpsData.longitude);
    Serial.printf("   Satellites: %d, HDOP: %.2f\n", gpsData.satellites, gpsData.hdop);
  }
  
  // 3. Test WiFi connection
  Serial.println("\n3. Testing WiFi connection...");
  connectToWiFi();
  
  // 4. Test NTP with Google's NTP server
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n4. Testing NTP time sync with Google NTP...");
    improvedNTPSync();
  }
  
  Serial.println("\n=== DIAGNOSTIC COMPLETE ===");
  Serial.printf("DHT11: %s\n", dhtWorking ? "✅ Working" : "❌ Failed");
  Serial.printf("GPS: %s\n", gpsDataReceived ? (gpsValidFix ? "✅ Working with fix" : "⚠️  No fix") : "❌ No data");
  Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "✅ Connected" : "❌ Failed");
  Serial.println("================================\n");
  
  setupCompleted = true;
  Serial.println("🚀 Setup completed! Starting main loop...\n");
}

void loop() {
  if (!setupCompleted) {
    Serial.println("Setup not completed, waiting...");
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
    transmitCachedData();
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
    gpsData.hdop = gps.hdop.hdop();
    
    static float lastLat = 0, lastLng = 0;
    float latDiff = abs(gpsData.latitude - lastLat);
    float lngDiff = abs(gpsData.longitude - lastLng);
    
    if (latDiff > 0.0001 || lngDiff > 0.0001 || lastLat == 0) {
      Serial.printf("🛰️  GPS: %.6f, %.6f (Sats: %d, HDOP: %.2f)\n", 
                    gpsData.latitude, gpsData.longitude, gpsData.satellites, gpsData.hdop);
      lastLat = gpsData.latitude;
      lastLng = gpsData.longitude;
    }
  } else {
    if (!gpsData.isValid) {
      gpsFailures++;
      static unsigned long lastGPSError = 0;
      if (millis() - lastGPSError > 60000) {
        Serial.printf("⚠️  GPS: No valid fix (Failures: %d, using default coordinates)\n", gpsFailures);
        lastGPSError = millis();
      }
    }
  }
}

void readSensorData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📶 WiFi reconnecting...");
    wifiFailures++;
    connectToWiFi();
  }
  
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate > 300000) {
    timeClient.update();
    lastNtpUpdate = millis();
  }
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity) || temperature < -40 || temperature > 80 || humidity < 0 || humidity > 100) {
    sensorFailures++;
    static unsigned long lastSensorError = 0;
    if (millis() - lastSensorError > 60000) {
      Serial.printf("⚠️  DHT sensor read failed (Failures: %d) - Temp: %.1f, Humidity: %.1f\n", 
                   sensorFailures, temperature, humidity);
      lastSensorError = millis();
    }
  } else {
    cachedData.temperature = temperature;
    cachedData.humidity = humidity;
    cachedData.isValid = true;
    cachedData.lastReadTime = millis();
    lastSuccessfulReading = millis();
    
    static float lastTemp = -999, lastHumid = -999;
    if (abs(temperature - lastTemp) > 0.5 || abs(humidity - lastHumid) > 2.0 || lastTemp == -999) {
      Serial.printf("🌡️  Sensor: %.1f°C, %.1f%% (Fresh reading)\n", temperature, humidity);
      lastTemp = temperature;
      lastHumid = humidity;
    }
  }
}

void transmitCachedData() {
  if (!cachedData.isValid) {
    Serial.println("❌ No valid sensor data to transmit!");
    return;
  }
  
  totalTransmissions++;
  
  String timestamp = getISTTimestamp();
  
  // Use GPS coordinates if available, otherwise use defaults
  float currentLat = gpsData.isValid ? gpsData.latitude : DEFAULT_LATITUDE;
  float currentLng = gpsData.isValid ? gpsData.longitude : DEFAULT_LONGITUDE;
  
  // Create payload with only required fields
  String formPayload = createSimplePayload(
    cachedData.temperature, 
    cachedData.humidity, 
    currentLat,
    currentLng,
    timestamp
  );
  
  // Send data to server
  bool httpSuccess = sendDataToServer(formPayload);
  if (!httpSuccess) {
    httpFailures++;
  }
  
  // Transmission summary
  Serial.println("📤 DATA TRANSMITTED");
  Serial.printf("   📊 Temp: %.1f°C, Humidity: %.1f%%\n", cachedData.temperature, cachedData.humidity);
  Serial.printf("   🛰️  Location: %.6f, %.6f %s\n", currentLat, currentLng, gpsData.isValid ? "(GPS)" : "(Default)");
  Serial.printf("   🕒 Timestamp: %s\n", timestamp.c_str());
  Serial.printf("   📈 Packet #%lu, Status: %s\n", totalTransmissions, httpSuccess ? "✅ SUCCESS" : "❌ FAILED");
  Serial.println();
}

void printSystemStatus() {
  Serial.println("=== SYSTEM STATUS ===");
  Serial.printf("⏱️  Uptime: %.1f minutes\n", millis() / 60000.0);
  Serial.printf("🕒 Current Time: %s\n", getISTTimestamp().c_str());
  Serial.printf("📶 WiFi: %s", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" (RSSI: %d dBm)", WiFi.RSSI());
  }
  Serial.println();
  Serial.printf("🛰️  GPS: %s", gpsData.isValid ? "Active" : "Inactive");
  if (gpsData.isValid) {
    Serial.printf(" (Sats: %d, HDOP: %.2f)", gpsData.satellites, gpsData.hdop);
  }
  Serial.println();
  Serial.printf("🌡️  Sensor: %s", cachedData.isValid ? "Active" : "Failed");
  if (cachedData.isValid) {
    Serial.printf(" (Last: %.1f°C, %.1f%%, Age: %ds)", 
                  cachedData.temperature, cachedData.humidity, 
                  (millis() - cachedData.lastReadTime) / 1000);
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
  delay(1000);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✅ Connected!");
    Serial.printf("   IP: %s, Signal: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println(" ❌ Failed after 30 attempts!");
    Serial.println("⚠️  Check WiFi credentials and signal strength");
  }
}

void improvedNTPSync() {
  Serial.println("🕒 Syncing with Google NTP server...");
  timeClient.begin();
  
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("   NTP attempt %d/5...", attempt);
    
    if (timeClient.update()) {
      unsigned long epochTime = timeClient.getEpochTime();
      if (epochTime > 1640995200) { // Sanity check: after 2022
        Serial.printf(" ✅ SUCCESS - IST Time: %s\n", timeClient.getFormattedTime().c_str());
        Serial.printf("   Epoch: %lu, IST: %s\n", epochTime, getISTTimestamp().c_str());
        return;
      }
    }
    
    Serial.println(" ❌ Failed");
    delay(2000);
  }
  
  Serial.println("⚠️  NTP sync failed - will use estimated time");
}

String getISTTimestamp() {
  unsigned long epochTime = timeClient.getEpochTime();
  
  // If NTP failed, try to update once more
  if (epochTime == 0 || epochTime < 1640995200) {
    Serial.println("⚠️  NTP time invalid, attempting sync...");
    timeClient.forceUpdate();
    delay(2000);
    epochTime = timeClient.getEpochTime();
    
    // If still failed, use estimated current time
    if (epochTime == 0 || epochTime < 1640995200) {
      epochTime = 1749705600 + (millis() / 1000); // June 12, 2025 00:00:00 UTC + uptime
      Serial.println("⚠️  Using estimated IST time");
    }
  }
  
  // Manual conversion to IST time
  unsigned long days = epochTime / 86400;
  unsigned long secondsInDay = epochTime % 86400;
  
  int hour = secondsInDay / 3600;
  int minute = (secondsInDay % 3600) / 60;
  int second = secondsInDay % 60;
  
  // Calculate date from days since Unix epoch
  int year = 1970;
  int month = 1;
  int day = 1;
  
  unsigned long remainingDays = days;
  
  // Calculate year
  while (remainingDays >= 365) {
    int daysInYear = isLeapYear(year) ? 366 : 365;
    if (remainingDays >= daysInYear) {
      remainingDays -= daysInYear;
      year++;
    } else {
      break;
    }
  }
  
  // Calculate month and day
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(year)) {
    daysInMonth[1] = 29;
  }
  
  month = 1;
  while (remainingDays >= daysInMonth[month - 1] && month <= 12) {
    remainingDays -= daysInMonth[month - 1];
    month++;
  }
  day = remainingDays + 1;
  
  // Sanity check the date
  if (year < 2024 || year > 2030 || month < 1 || month > 12 || day < 1 || day > 31) {
    Serial.println("⚠️  Date calculation failed, using fallback");
    return "2025-06-12 12:00:00";
  }
  
  // Format as simple timestamp without timezone suffix
  char istTime[20];
  snprintf(istTime, sizeof(istTime), "%04d-%02d-%02d %02d:%02d:%02d", 
           year, month, day, hour, minute, second);
  
  return String(istTime);
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

String createSimplePayload(float temp, float humid, float lat, float lng, String timestamp) {
  String payload = "";
  
  // Only the required fields
  payload += "secret=" + String(secretKey);
  payload += "&sensor_unique_id=" + String(SENSOR_ID);
  
  // Temperature in Kelvin
  float tempKelvin = temp + 273.15;
  payload += "&Temperature(K)=" + String(tempKelvin, 2);
  
  // Humidity as integer percentage
  payload += "&humidity(%)=" + String((int)round(humid));
  
  // GPS Coordinates
  payload += "&sensor_longitude=" + String(lng, 6);
  payload += "&sensor_latitude=" + String(lat, 6);
  
  // Timestamp (IST without timezone suffix)
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
  http.addHeader("User-Agent", "ESP8266-DHT-GPS-Logger/1.0");
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    if (httpResponseCode != 200 || totalTransmissions <= 3) {
      String response = http.getString();
      Serial.printf("🌐 HTTP %d: %s\n", httpResponseCode, response.c_str());
    }
    
    http.end();
    return (httpResponseCode >= 200 && httpResponseCode < 300);
  } else {
    static unsigned long lastHttpError = 0;
    if (millis() - lastHttpError > 120000) {
      Serial.printf("❌ HTTP Error: %d (%s)\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
      lastHttpError = millis();
    }
    
    http.end();
    return false;
  }
}
