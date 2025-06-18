// ESP8266 AHT10 + Neo-6M GPS Data Logger with Buffer Logic
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
#include <EEPROM.h>

// WiFi credentials 
const char* ssid = "KRC-101C";
const char* password = "krc101c@";

// Server configuration
const char* serverURL = "https://lostdevs.io/ctrl1/master.php";
const char* secretKey = "lostdev-sensor1-1008200303082003";

// Buffer configuration
#define EEPROM_SIZE 4096              // Total EEPROM size
#define BUFFER_START_ADDR 100         // Start address for buffer data
#define MAX_BUFFER_SIZE 3500          // Use ~85% of EEPROM for safety
#define RECORD_SIZE 20                // Size of each buffered record (reduced)
#define MAX_RECORDS (MAX_BUFFER_SIZE / RECORD_SIZE)  // ~175 records
#define BUFFER_HEADER_SIZE 20         // Header info size

// Buffer memory usage thresholds
#define HEAP_SAFETY_THRESHOLD 8192    // Keep 8KB free heap minimum
#define BUFFER_USAGE_WARNING 85       // Warn at 85% buffer usage
#define BUFFER_CLEANUP_THRESHOLD 95   // Start cleanup at 95%

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
unsigned long lastBufferCleanup = 0;
unsigned long lastBufferWrite = 0;

const unsigned long sensorReadInterval = 8000;
const unsigned long gpsReadInterval = 15000;
const unsigned long transmissionInterval = 5000;
const unsigned long statusUpdateInterval = 120000;
const unsigned long bufferCleanupInterval = 300000; // 5 minutes
const unsigned long bufferInterval = 30000;        // Buffer data every 30 seconds

// Data structures
struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
  unsigned long lastReadTime;
} cachedData = {-999.0, -999.0, false, 0};

struct GPSData {
  float latitude;
  float longitude;
  bool isValid;
  unsigned long lastReadTime;
  int satellites;
} gpsData = {DEFAULT_LATITUDE, DEFAULT_LONGITUDE, false, 0, 0};

// Buffered record structure (20 bytes) - Only transmission fields
struct BufferedRecord {
  float temperature;      // 4 bytes - Temperature in Celsius (will convert to Kelvin)
  float humidity;         // 4 bytes - Humidity percentage (will round to int)
  float latitude;         // 4 bytes - GPS latitude
  float longitude;        // 4 bytes - GPS longitude
  uint32_t timestamp;     // 4 bytes - Unix timestamp
};

// Buffer management structure
struct BufferHeader {
  uint16_t totalRecords;
  uint16_t nextWriteIndex;
  uint16_t oldestRecordIndex;
  uint32_t nextRecordId;
  uint32_t totalBuffered;
  uint32_t totalTransmitted;
  uint16_t bufferVersion;
  uint16_t checksum;
};

BufferHeader bufferHeader;

// Error tracking
int wifiFailures = 0;
int sensorFailures = 0;
int httpFailures = 0;
int gpsFailures = 0;
int bufferErrors = 0;
unsigned long totalTransmissions = 0;
unsigned long bufferedReadings = 0;
bool setupCompleted = false;
bool ntpSynced = false;
bool bufferInitialized = false;

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  if (setupCompleted) {
    Serial.println("SETUP ALREADY COMPLETED - PREVENTING RESTART LOOP");
    return;
  }
  
  Serial.println("\n\n=== ESP8266 AHT10 + Neo-6M GPS Data Logger with Buffer ===");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId(), HEX);
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.printf("Buffer Config: Max %d records (%d bytes), 30s intervals, Safety threshold: %d%%\n", 
                MAX_RECORDS, MAX_BUFFER_SIZE, BUFFER_CLEANUP_THRESHOLD);
  Serial.println("Target URL: https://lostdevs.io/ctrl1/master.php");
  Serial.println("GPS Wiring: D6(GPIO12)->TX, D7(GPIO13)->RX, 3.3V->VCC, GND->GND");
  Serial.println("AHT10 Wiring: D2(GPIO4)->SDA, D1(GPIO5)->SCL, 3.3V->VCC, GND->GND");
  Serial.println("============================================================\n");
  
  // Initialize EEPROM and buffer
  initializeBuffer();
  
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
  
  // Try to send any buffered data
  if (bufferHeader.totalRecords > 0) {
    Serial.printf("📦 Found %d buffered records, attempting to transmit...\n", bufferHeader.totalRecords);
    transmitBufferedData();
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
  
  // Monitor memory usage
  if (ESP.getFreeHeap() < HEAP_SAFETY_THRESHOLD) {
    Serial.printf("⚠️  Low memory warning: %d bytes free\n", ESP.getFreeHeap());
  }
  
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
  
  // Transmit data (online) or buffer data (offline)
  if (currentTime - lastDataTransmission >= transmissionInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      transmitData();
      // Also try to send buffered data if any exists
      if (bufferHeader.totalRecords > 0) {
        transmitBufferedData();
      }
    }
    lastDataTransmission = currentTime;
  }
  
  // Buffer data every 30 seconds when WiFi is down
  if (WiFi.status() != WL_CONNECTED && currentTime - lastBufferWrite >= bufferInterval) {
    bufferData();
    lastBufferWrite = currentTime;
  }
  
  // Periodic buffer cleanup
  if (currentTime - lastBufferCleanup >= bufferCleanupInterval) {
    performBufferMaintenance();
    lastBufferCleanup = currentTime;
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

void initializeBuffer() {
  Serial.println("🗂️  Initializing buffer system...");
  EEPROM.begin(EEPROM_SIZE);
  
  // Read existing buffer header
  EEPROM.get(BUFFER_START_ADDR, bufferHeader);
  
  // Check if buffer is valid (version check and reasonable values)
  bool bufferValid = (bufferHeader.bufferVersion == 1001 && 
                     bufferHeader.totalRecords <= MAX_RECORDS &&
                     bufferHeader.nextWriteIndex < MAX_RECORDS &&
                     bufferHeader.oldestRecordIndex < MAX_RECORDS);
  
  if (!bufferValid) {
    Serial.println("   Initializing new buffer...");
    // Initialize new buffer
    bufferHeader.totalRecords = 0;
    bufferHeader.nextWriteIndex = 0;
    bufferHeader.oldestRecordIndex = 0;
    bufferHeader.nextRecordId = 1;
    bufferHeader.totalBuffered = 0;
    bufferHeader.totalTransmitted = 0;
    bufferHeader.bufferVersion = 1001;
    bufferHeader.checksum = calculateBufferChecksum();
    
    saveBufferHeader();
    Serial.println("   ✅ New buffer initialized");
  } else {
    Serial.printf("   ✅ Existing buffer loaded: %d records, next ID: %lu\n", 
                  bufferHeader.totalRecords, bufferHeader.nextRecordId);
  }
  
  bufferInitialized = true;
}

void bufferData() {
  if (!bufferInitialized || !cachedData.isValid) {
    return;
  }
  
  // Check if buffer is getting full
  int bufferUsagePercent = (bufferHeader.totalRecords * 100) / MAX_RECORDS;
  if (bufferUsagePercent >= BUFFER_CLEANUP_THRESHOLD) {
    Serial.printf("🗂️  Buffer %d%% full, cleaning up...\n", bufferUsagePercent);
    cleanupOldRecords();
  }
  
  // Create new record with only transmission fields
  BufferedRecord record;
  record.temperature = cachedData.temperature;
  record.humidity = cachedData.humidity;
  record.latitude = gpsData.isValid ? gpsData.latitude : DEFAULT_LATITUDE;
  record.longitude = gpsData.isValid ? gpsData.longitude : DEFAULT_LONGITUDE;
  
  // Get current timestamp (Unix epoch)
  uint32_t currentTimestamp;
  if (ntpSynced) {
    currentTimestamp = timeClient.getEpochTime();
  } else {
    currentTimestamp = 1749705600 + (millis() / 1000); // Fallback timestamp
  }
  record.timestamp = currentTimestamp;
  
  // Calculate EEPROM address for this record
  int recordAddr = BUFFER_START_ADDR + BUFFER_HEADER_SIZE + (bufferHeader.nextWriteIndex * RECORD_SIZE);
  
  // Write record to EEPROM
  EEPROM.put(recordAddr, record);
  EEPROM.commit();
  
  // Update buffer management
  if (bufferHeader.totalRecords < MAX_RECORDS) {
    bufferHeader.totalRecords++;
  } else {
    // Buffer is full, move oldest record pointer
    bufferHeader.oldestRecordIndex = (bufferHeader.oldestRecordIndex + 1) % MAX_RECORDS;
  }
  
  bufferHeader.nextWriteIndex = (bufferHeader.nextWriteIndex + 1) % MAX_RECORDS;
  bufferHeader.totalBuffered++;
  
  saveBufferHeader();
  bufferedReadings++;
  
  Serial.printf("📦 BUFFERED: %.1f°C, %.1f%% at %s - Buffer: %d/%d records\n",
                record.temperature, record.humidity, formatTimestamp(record.timestamp).c_str(),
                bufferHeader.totalRecords, MAX_RECORDS);
}

void transmitBufferedData() {
  if (!bufferInitialized || bufferHeader.totalRecords == 0 || WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  Serial.printf("📤 Transmitting %d buffered records...\n", bufferHeader.totalRecords);
  
  int transmitted = 0;
  int maxBatchSize = 5; // Transmit in small batches to avoid memory issues
  
  // Fix: Cast both arguments to the same type
  uint16_t recordsToProcess = min((uint16_t)bufferHeader.totalRecords, (uint16_t)maxBatchSize);
  
  for (int i = 0; i < recordsToProcess; i++) {
    BufferedRecord record;
    int recordAddr = BUFFER_START_ADDR + BUFFER_HEADER_SIZE + (bufferHeader.oldestRecordIndex * RECORD_SIZE);
    
    EEPROM.get(recordAddr, record);
    
    // Create timestamp string from stored timestamp
    String timestamp = formatTimestamp(record.timestamp);
    
    String payload = createPayload(record.temperature, record.humidity, 
                                 record.latitude, record.longitude, timestamp);
    
    if (sendDataToServer(payload)) {
      transmitted++;
      bufferHeader.totalTransmitted++;
      Serial.printf("✅ Transmitted buffered data: %.1f°C, %.1f%% at %s\n", 
                    record.temperature, record.humidity, timestamp.c_str());
      removeOldestRecord();
    } else {
      Serial.printf("❌ Failed to transmit buffered data: %.1f°C, %.1f%%\n", 
                    record.temperature, record.humidity);
      break; // Stop if transmission fails
    }
    
    delay(1000); // Small delay between transmissions
  }
  
  if (transmitted > 0) {
    saveBufferHeader();
    Serial.printf("📤 Transmitted %d buffered records, %d remaining\n", 
                  transmitted, bufferHeader.totalRecords);
  }
}

void removeOldestRecord() {
  if (bufferHeader.totalRecords == 0) return;
  
  bufferHeader.oldestRecordIndex = (bufferHeader.oldestRecordIndex + 1) % MAX_RECORDS;
  bufferHeader.totalRecords--;
}

void cleanupOldRecords() {
  int recordsToRemove = MAX_RECORDS / 4; // Remove 25% of oldest records
  
  Serial.printf("🧹 Cleaning up %d oldest records...\n", recordsToRemove);
  
  for (int i = 0; i < recordsToRemove && bufferHeader.totalRecords > 0; i++) {
    removeOldestRecord();
  }
  
  saveBufferHeader();
  Serial.printf("🧹 Cleanup complete, %d records remaining\n", bufferHeader.totalRecords);
}

void performBufferMaintenance() {
  if (!bufferInitialized) return;
  
  int bufferUsagePercent = (bufferHeader.totalRecords * 100) / MAX_RECORDS;
  
  if (bufferUsagePercent >= BUFFER_USAGE_WARNING) {
    Serial.printf("⚠️  Buffer usage warning: %d%% full (%d/%d records)\n", 
                  bufferUsagePercent, bufferHeader.totalRecords, MAX_RECORDS);
  }
  
  // Verify buffer integrity occasionally
  if (calculateBufferChecksum() != bufferHeader.checksum) {
    Serial.println("❌ Buffer header corruption detected, reinitializing...");
    bufferErrors++;
    initializeBuffer();
  }
}

uint8_t calculateRecordChecksum(BufferedRecord* record) {
  uint8_t checksum = 0;
  uint8_t* data = (uint8_t*)record;
  
  // Calculate checksum for all bytes in the record
  for (int i = 0; i < RECORD_SIZE; i++) {
    checksum ^= data[i];
  }
  
  return checksum;
}

uint16_t calculateBufferChecksum() {
  uint16_t checksum = bufferHeader.totalRecords ^ bufferHeader.nextWriteIndex ^ 
                     bufferHeader.oldestRecordIndex ^ (bufferHeader.nextRecordId & 0xFFFF);
  return checksum;
}

void saveBufferHeader() {
  bufferHeader.checksum = calculateBufferChecksum();
  EEPROM.put(BUFFER_START_ADDR, bufferHeader);
  EEPROM.commit();
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
  
  // Buffer status
  int bufferUsagePercent = bufferInitialized ? (bufferHeader.totalRecords * 100) / MAX_RECORDS : 0;
  Serial.printf("📦 Buffer: %d/%d records (%d%%), %lu total buffered, %lu transmitted\n", 
                bufferHeader.totalRecords, MAX_RECORDS, bufferUsagePercent,
                bufferHeader.totalBuffered, bufferHeader.totalTransmitted);
  
  Serial.printf("📊 Stats: %lu sent, %d WiFi fails, %d sensor fails, %d GPS fails, %d HTTP fails, %d buffer errors\n", 
                totalTransmissions, wifiFailures, sensorFailures, gpsFailures, httpFailures, bufferErrors);
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
