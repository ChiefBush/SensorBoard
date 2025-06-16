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
const char* secretKey = "lostdev-sensor1-1008200303082003";

// DHT sensor configuration
#define DHT_PIN 2        // GPIO2 (D4 on NodeMCU)
#define DHT_TYPE DHT11   

DHT dht(DHT_PIN, DHT_TYPE);

// GPS configuration - FIXED PINS
#define GPS_RX_PIN 4     // D2 (GPIO4) -> GPS TX (changed from 12)
#define GPS_TX_PIN 5     // D1 (GPIO5) -> GPS RX (changed from 13)
#define GPS_BAUD 9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;

// NTP Client - FIXED IST TIMEZONE
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // IST = UTC + 5:30 = 19800 seconds

// BootConfig.h
const int SENSOR_ID = 1;

// Default coordinates (fallback if GPS fails)
float DEFAULT_LATITUDE = 0;   
float DEFAULT_LONGITUDE = 0;  

// Timing intervals - INCREASED for stability
unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
unsigned long lastGPSReading = 0;
unsigned long lastStatusUpdate = 0;

const unsigned long sensorReadInterval = 10000;     // Read DHT11 every 10 seconds (increased)
const unsigned long gpsReadInterval = 15000;       // Read GPS every 15 seconds (increased)
const unsigned long transmissionInterval = 60000;  // Send data every 60 seconds (increased)
const unsigned long statusUpdateInterval = 120000; // Status update every 2 minutes (increased)

// Cached sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
  unsigned long lastReadTime;
  int readingAge;
} cachedData = {-999.0, -999.0, false, 0, 0};

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
  delay(3000); // Increased delay for stability
  
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
  
  // 1. Test DHT sensor first (most critical)
  Serial.println("1. Testing DHT11 sensor...");
  dht.begin();
  delay(3000); // Wait for sensor to stabilize
  
  bool dhtWorking = false;
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("   DHT Attempt %d/5...", attempt);
    delay(2500); // DHT11 needs 2s between readings
    
    float testTemp = dht.readTemperature();
    float testHumid = dht.readHumidity();
    
    if (!isnan(testTemp) && !isnan(testHumid) && testTemp > -40 && testTemp < 80 && testHumid >= 0 && testHumid <= 100) {
      Serial.printf(" ✅ SUCCESS - Temp: %.1f°C, Humidity: %.1f%%\n", testTemp, testHumid);
      dhtWorking = true;
      cachedData.temperature = testTemp;
      cachedData.humidity = testHumid;
      cachedData.isValid = true;
      cachedData.lastReadTime = millis();
      cachedData.readingAge = 0;
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
  
  while (millis() - gpsTestStart < 10000) { // 10 second test
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
  
  // 4. Test NTP with improved sync
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n4. Testing NTP time sync...");
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
  // Prevent infinite restart loops
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
  
  // Process GPS data continuously but don't print
  if (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  
  delay(500); // Increased delay to prevent overwhelming the system
}

void readGPSData() {
  bool newData = false;
  unsigned long gpsStart = millis();
  
  // Read GPS for up to 2 seconds
  while (millis() - gpsStart < 2000) {
    if (gpsSerial.available()) {
      if (gps.encode(gpsSerial.read())) {
        newData = true;
      }
    }
    delay(10); // Small delay to prevent overwhelming
  }
  
  if (newData && gps.location.isValid()) {
    // Always update GPS coordinates on every valid reading
    gpsData.latitude = gps.location.lat();
    gpsData.longitude = gps.location.lng();
    gpsData.isValid = true;
    gpsData.lastReadTime = millis();
    gpsData.satellites = gps.satellites.value();
    gpsData.hdop = gps.hdop.hdop();
    
    // Always print GPS updates to show real-time coordinates
    Serial.printf("🛰️  GPS: %.6f, %.6f (Sats: %d, HDOP: %.2f)\n", 
                  gpsData.latitude, gpsData.longitude, gpsData.satellites, gpsData.hdop);
    
  } else {
    if (!gpsData.isValid) {
      gpsFailures++;
      // Only print GPS errors occasionally to avoid flooding
      static unsigned long lastGPSError = 0;
      if (millis() - lastGPSError > 60000) { // Every minute
        Serial.printf("⚠️  GPS: No valid fix (Failures: %d, using default coordinates)\n", gpsFailures);
        lastGPSError = millis();
      }
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
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate > 300000) { // Every 5 minutes
    timeClient.update();
    lastNtpUpdate = millis();
  }
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  // Check if readings are valid with proper bounds checking
  if (isnan(temperature) || isnan(humidity) || temperature < -40 || temperature > 80 || humidity < 0 || humidity > 100) {
    sensorFailures++;
    // Only print sensor errors occasionally
    static unsigned long lastSensorError = 0;
    if (millis() - lastSensorError > 60000) { // Every minute
      Serial.printf("⚠️  DHT sensor read failed (Failures: %d) - Temp: %.1f, Humidity: %.1f\n", 
                   sensorFailures, temperature, humidity);
      lastSensorError = millis();
    }
  } else {
    // Update cache with new valid reading
    cachedData.temperature = temperature;
    cachedData.humidity = humidity;
    cachedData.isValid = true;
    cachedData.lastReadTime = millis();
    cachedData.readingAge = 0;
    lastSuccessfulReading = millis();
    
    // Only print new sensor data if it changed significantly
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
  cachedData.readingAge++;
  
  unsigned long dataAge = millis() - cachedData.lastReadTime;
  String timestamp = getISOTimestamp();
  
  // Use GPS coordinates if available, otherwise use defaults
  float currentLat = gpsData.isValid ? gpsData.latitude : DEFAULT_LATITUDE;
  float currentLng = gpsData.isValid ? gpsData.longitude : DEFAULT_LONGITUDE;
  
  // Create payload with cached data
  String formPayload = createFormPayload(
    cachedData.temperature, 
    cachedData.humidity, 
    currentLat,
    currentLng,
    timestamp, 
    dataAge, 
    false,
    cachedData.readingAge
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
  Serial.printf("🕒 Current Time: %s\n", getISOTimestamp().c_str());
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
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Increased attempts
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
    // Don't restart immediately, continue with offline operation
  }
}

void improvedNTPSync() {
  Serial.println("🕒 Syncing with NTP server...");
  timeClient.begin();
  
  // Try multiple sync attempts
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.printf("   NTP attempt %d/5...", attempt);
    
    if (timeClient.update()) {
      unsigned long epochTime = timeClient.getEpochTime();
      if (epochTime > 1640995200) { // Sanity check: after 2022
        Serial.printf(" ✅ SUCCESS - IST Time: %s\n", timeClient.getFormattedTime().c_str());
        Serial.printf("   Epoch: %lu, ISO: %s\n", epochTime, getISOTimestamp().c_str());
        return;
      }
    }
    
    Serial.println(" ❌ Failed");
    delay(2000);
  }
  
  Serial.println("⚠️  NTP sync failed - will use estimated time");
}

String getISOTimestamp() {
  unsigned long epochTime = timeClient.getEpochTime();
  
  // If NTP failed, try to update once more
  if (epochTime == 0 || epochTime < 1640995200) {
    Serial.println("⚠️  NTP time invalid, attempting sync...");
    timeClient.forceUpdate();
    delay(2000);
    epochTime = timeClient.getEpochTime();
    
    // If still failed, use estimated current time (June 2025)
    if (epochTime == 0 || epochTime < 1640995200) {
      // Use current date base (June 12, 2025) + device uptime  
      epochTime = 1749705600 + (millis() / 1000); // June 12, 2025 00:00:00 UTC + uptime
      Serial.println("⚠️  Using estimated IST time");
    }
  }
  
  // Manual conversion since ESP8266 doesn't have reliable gmtime
  // epochTime is already IST-adjusted due to NTP offset
  unsigned long days = epochTime / 86400;
  unsigned long secondsInDay = epochTime % 86400;
  
  int hour = secondsInDay / 3600;
  int minute = (secondsInDay % 3600) / 60;
  int second = secondsInDay % 60;
  
  // Calculate date from days since Unix epoch (Jan 1, 1970)
  int year = 1970;
  int month = 1;
  int day = 1;
  
  // Add days to get current date
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
    return "2025-06-12T12:00:00+05:30";
  }
  
  // Format as ISO timestamp with IST timezone
  char isoTime[30];
  snprintf(isoTime, sizeof(isoTime), "%04d-%02d-%02dT%02d:%02d:%02d+05:30", 
           year, month, day, hour, minute, second);
  
  return String(isoTime);
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

String createFormPayload(float temp, float humid, float lat, float lng, String timestamp, unsigned long dataAge, bool sensorError, int readingAge) {
  String payload = "";
  
  // Authentication 
  payload += "secret=" + String(secretKey);
  
  // Sensor ID 
  payload += "&sensor_unique_id=" + String(SENSOR_ID);
  
  // Temperature in Kelvin 
  float tempKelvin;
  if (sensorError) {
    tempKelvin = -999.0;
  } else {
    tempKelvin = temp + 273.15;
  }
  payload += "&Temperature(K)=" + String(tempKelvin, 2);
  
  // Humidity 
  if (sensorError) {
    payload += "&humidity(%)=-999";
  } else {
    payload += "&humidity(%)=" + String((int)round(humid));
  }
  
  // GPS Coordinates 
  payload += "&sensor_longitude=" + String(lng, 6);
  payload += "&sensor_latitude=" + String(lat, 6);
  
  // Timestamp 
  payload += "&receiving_date=" + timestamp;
  
  // RDF metadata 
  payload += "&rdf_metadata=sensor_type:DHT11,gps_module:Neo-6M,location:mobile,purpose:environmental_monitoring,transmission_mode:gps_enabled,timezone:IST";
  
  // Download metadata with GPS info
  payload += "&download_metadata=chip_id:" + String(ESP.getChipId(), HEX) + 
             ",data_age_ms:" + String(dataAge) + 
             ",reading_age:" + String(readingAge) + 
             ",total_transmissions:" + String(totalTransmissions) +
             ",wifi_failures:" + String(wifiFailures) + 
             ",sensor_failures:" + String(sensorFailures) + 
             ",gps_failures:" + String(gpsFailures) +
             ",http_failures:" + String(httpFailures) +
             ",gps_valid:" + String(gpsData.isValid ? "true" : "false") +
             ",gps_satellites:" + String(gpsData.satellites) +
             ",gps_hdop:" + String(gpsData.hdop, 2) +
             ",timestamp_source:" + String((timeClient.getEpochTime() > 1640995200) ? "ntp" : "estimated");
  
  // Expected noise
  if (sensorError) {
    payload += "&expected_noise=high";
  } else if (readingAge > 1) {
    payload += "&expected_noise=low";
  } else {
    payload += "&expected_noise=medium";
  }
  
  // Spike detection
  static float lastTemp = -999;
  static float lastHumid = -999;
  String spikeStatus = "none";
  
  if (!sensorError && readingAge == 1) {
    if (lastTemp != -999 && lastHumid != -999) {
      float tempDiff = abs(temp - lastTemp);
      float humidDiff = abs(humid - lastHumid);
      
      if (tempDiff > 5.0 || humidDiff > 10.0) {
        spikeStatus = "detected";
      }
    }
    lastTemp = temp;
    lastHumid = humid;
  } else if (readingAge > 1) {
    spikeStatus = "cached";
  }
  
  payload += "&spike=" + spikeStatus;
  
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
  http.setTimeout(20000); // Increased timeout
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    // Only show response details for errors or first few transmissions
    if (httpResponseCode != 200 || totalTransmissions <= 3) {
      String response = http.getString();
      Serial.printf("🌐 HTTP %d: %s\n", httpResponseCode, response.c_str());
    }
    
    http.end();
    return (httpResponseCode >= 200 && httpResponseCode < 300);
  } else {
    // Only print HTTP errors occasionally to avoid flooding
    static unsigned long lastHttpError = 0;
    if (millis() - lastHttpError > 120000) { // Every 2 minutes
      Serial.printf("❌ HTTP Error: %d (%s)\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
      lastHttpError = millis();
    }
    
    http.end();
    return false;
  }
}
