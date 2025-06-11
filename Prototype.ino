#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WiFi credentials 
const char* ssid = "KRC-101C";
const char* password = "krc101c@";

// Server configuration
const char* serverURL = "https://lostdevs.io/ctrl1/master.php";
const char* secretKey = "lostdev-sensor1-1008200303082003";

// DHT sensor configuration a
#define DHT_PIN 2        // GPIO2 (D4 on NodeMCU)
#define DHT_TYPE DHT11   
// #define DHT_TYPE DHT22   

DHT dht(DHT_PIN, DHT_TYPE);

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // IST offset: 5.5 hours = 19800 seconds

// BootConfig.h
const int SENSOR_ID = 1;

const float LATITUDE = 28.637270;   
const float LONGITUDE = 77.170277;  

// sensor reading and data transmission
unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
const unsigned long sensorReadInterval = 3000;    // Read DHT11 every 3 seconds (respects 2.5s minimum)
const unsigned long transmissionInterval = 500;  // Send data every 0.5 seconds (120 packets/minute)


// Cached sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
  unsigned long lastReadTime;
  int readingAge; // How many transmissions have used this reading
} cachedData = {-999.0, -999.0, false, 0, 0};

// Error tracking
int wifiFailures = 0;
int sensorFailures = 0;
int httpFailures = 0;
unsigned long lastSuccessfulReading = 0;
unsigned long totalTransmissions = 0; // Track total packets sent

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n=== ESP8266 HIGH-FREQUENCY DHT Data Logger Starting ===");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId(), HEX);
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("Target URL: https://lostdevs.io/ctrl1/master.php");
  Serial.printf("Sensor read interval: %lu ms\n", sensorReadInterval);
  Serial.printf("Transmission interval: %lu ms\n", transmissionInterval);
  Serial.printf("Expected packets per minute: %.1f\n", 60000.0 / transmissionInterval);
  Serial.println("=============================================\n");
  
  Serial.println("Initializing DHT sensor...");
  dht.begin();
  delay(3000);
  
  // Test DHT sensor
  Serial.println("Testing DHT sensor...");
  bool sensorWorking = false;
  
  for (int i = 0; i < 5; i++) {
    delay(2500); // DHT11 needs at least 2 seconds between readings
    float testTemp = dht.readTemperature();
    float testHumid = dht.readHumidity();
    
    Serial.printf("Test %d: Temp=%.1f°C, Humidity=%.1f%%\n", i+1, testTemp, testHumid);
    
    if (!isnan(testTemp) && !isnan(testHumid)) {
      Serial.printf("DHT11 working - Temp: %.0f°C, Humidity: %.0f%%\n", testTemp, testHumid);
      sensorWorking = true;
      // ADDED: Initialize cached data with first valid reading
      cachedData.temperature = testTemp;
      cachedData.humidity = testHumid;
      cachedData.isValid = true;
      cachedData.lastReadTime = millis();
      cachedData.readingAge = 0;
      break;
    }
  }
  
  if (!sensorWorking) {
    Serial.println("DHT sensor FAILED after 5 attempts!");
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize NTP client for India Standard Time
  Serial.println("Initializing NTP client for India Standard Time (IST)...");
  timeClient.begin();
  timeClient.setTimeOffset(19800); // IST = UTC + 5:30 hours = 19800 seconds
  
  Serial.println("Getting initial time from NTP server...");
  int ntpAttempts = 0;
  bool ntpSuccess = false;
  
  while (ntpAttempts < 20) {
    if (timeClient.update()) {
      ntpSuccess = true;
      break;
    }
    Serial.print(".");
    delay(1000);
    ntpAttempts++;
    
    if (ntpAttempts == 10) {
      Serial.println("\nTrying Indian NTP server...");
      timeClient.setPoolServerName("in.pool.ntp.org"); // Use India NTP pool
    }
  }
  
  if (ntpSuccess) {
    Serial.println("NTP time synchronized to India Standard Time (IST)");
    Serial.print("Current IST time: ");
    Serial.println(getISOTimestamp());
  } else {
    Serial.println("Warning: Could not sync with NTP server");
  }
  
  Serial.println("\nSetup completed successfully!");
  Serial.println("Starting high-frequency data collection...\n");
}

void loop() {
  unsigned long currentTime = millis();
  
  // STEP 1: Check if it's time to read the sensor (every 3 seconds)
  if (currentTime - lastSensorReading >= sensorReadInterval) {
    readSensorData();
    lastSensorReading = currentTime;
  }
  
  // STEP 2: Check if it's time to transmit data (every 5 seconds, or whatever you set)
  if (currentTime - lastDataTransmission >= transmissionInterval) {
    transmitCachedData();
    lastDataTransmission = currentTime;
  }
  
  delay(50); // Small delay to prevent excessive CPU usage
}

// ADDED: Function to read sensor data and update cache
void readSensorData() {
  unsigned long readStartTime = millis();
  
  // Ensure WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    wifiFailures++;
    connectToWiFi();
  }
  
  // Update time from NTP server periodically
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate > 60000) {
    timeClient.update();
    lastNtpUpdate = millis();
  }
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  unsigned long readEndTime = millis();
  unsigned long readDuration = readEndTime - readStartTime;
  
  // Check if readings are valid
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("⚠️  Failed to read from DHT sensor! Using cached data.");
    sensorFailures++;
    // Don't update cache - keep using last valid reading
  } else {
    // Update cache with new valid reading
    cachedData.temperature = temperature;
    cachedData.humidity = humidity;
    cachedData.isValid = true;
    cachedData.lastReadTime = millis();
    cachedData.readingAge = 0; // Reset age counter
    lastSuccessfulReading = millis();
    
    Serial.printf("📊 NEW SENSOR DATA: Temp=%.1f°C, Humidity=%.1f%% (read in %lums)\n", 
                  temperature, humidity, readDuration);
  }
}

// ADDED: Function to transmit cached data
void transmitCachedData() {
  if (!cachedData.isValid) {
    Serial.println("❌ No valid cached data to transmit!");
    return;
  }
  
  totalTransmissions++;
  cachedData.readingAge++;
  
  unsigned long dataAge = millis() - cachedData.lastReadTime;
  String timestamp = getISOTimestamp();
  
  // Create payload with cached data
  String formPayload = createFormPayload(
    cachedData.temperature, 
    cachedData.humidity, 
    timestamp, 
    dataAge, 
    false, // Not a sensor error since we have valid cached data
    cachedData.readingAge
  );
  
  // Send data to server
  bool httpSuccess = sendDataToServer(formPayload);
  if (!httpSuccess) {
    httpFailures++;
  }
  
  // Enhanced debug output
  Serial.println("=== DATA TRANSMISSION ===");
  Serial.printf("📤 Packet #%lu sent\n", totalTransmissions);
  Serial.printf("📊 Data: Temp=%.1f°C, Humidity=%.1f%%\n", cachedData.temperature, cachedData.humidity);
  Serial.printf("⏱️  Data age: %lu ms (reading #%d)\n", dataAge, cachedData.readingAge);
  Serial.printf("📈 Transmission rate: %.1f packets/min\n", totalTransmissions * 60000.0 / millis());
  Serial.printf("✅ Status: %s\n", httpSuccess ? "SUCCESS" : "FAILED");
  Serial.println("========================\n");
}

void connectToWiFi() {
  Serial.println("\n=== WiFi Connection ===");
  Serial.print("Connecting to: ");
  Serial.println(ssid);
  
  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(1000);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println();
    Serial.println("WiFi connection FAILED!");
    Serial.println("Restarting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }
  Serial.println("=====================\n");
}

String getISOTimestamp() {
  unsigned long epochTime = timeClient.getEpochTime();
  
  int year, month, day, hour, minute, second;
  
  second = epochTime % 60;
  epochTime /= 60;
  minute = epochTime % 60;
  epochTime /= 60;
  hour = epochTime % 24;
  epochTime /= 24;
  
  long days = epochTime;
  year = 1970;
  
  while (days >= 365) {
    if (isLeapYear(year)) {
      if (days >= 366) {
        days -= 366;
        year++;
      } else {
        break;
      }
    } else {
      days -= 365;
      year++;
    }
  }
  
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(year)) {
    daysInMonth[1] = 29;
  }
  
  month = 1;
  while (days >= daysInMonth[month - 1]) {
    days -= daysInMonth[month - 1];
    month++;
  }
  day = days + 1;
  
  char isoTime[25];
  sprintf(isoTime, "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour, minute, second);
  
  return String(isoTime);
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// MODIFIED: Enhanced payload creation with caching metadata
String createFormPayload(float temp, float humid, String timestamp, unsigned long dataAge, bool sensorError, int readingAge) {
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
  payload += "&Temperature(K)=" + String(tempKelvin, 2); // 2 decimal places 
  
  // Humidity 
  if (sensorError) {
    payload += "&humidity(%)=-999";
  } else {
    payload += "&humidity(%)=" + String((int)round(humid)); // Integer 
  }
  
  // Coordinates 
  payload += "&sensor_longitude=" + String(LONGITUDE, 6);
  payload += "&sensor_latitude=" + String(LATITUDE, 6);
  
  // Timestamp 
  payload += "&receiving_date=" + timestamp;
  
  // RDF metadata 
  payload += "&rdf_metadata=sensor_type:DHT11,location:indoor,purpose:environmental_monitoring,transmission_mode:cached_high_frequency";
  
  // ENHANCED: Download metadata with caching info
  payload += "&download_metadata=chip_id:" + String(ESP.getChipId(), HEX) + 
             ",data_age_ms:" + String(dataAge) + 
             ",reading_age:" + String(readingAge) + 
             ",total_transmissions:" + String(totalTransmissions) +
             ",wifi_failures:" + String(wifiFailures) + 
             ",sensor_failures:" + String(sensorFailures) + 
             ",http_failures:" + String(httpFailures);
  
  // Expected noise - lower for cached data since it's the same reading
  if (sensorError) {
    payload += "&expected_noise=high";
  } else if (readingAge > 1) {
    payload += "&expected_noise=low"; // Cached data has no sensor noise
  } else {
    payload += "&expected_noise=medium"; // Fresh DHT11 reading
  }
  
  // Spike detection - only relevant for fresh readings
  static float lastTemp = -999;
  static float lastHumid = -999;
  String spikeStatus = "none";
  
  if (!sensorError && readingAge == 1) { // Only check spikes on fresh readings
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
    spikeStatus = "cached"; // Indicate this is cached data
  }
  
  payload += "&spike=" + spikeStatus;
  
  return payload;
}

// HTTP client with better error handling
bool sendDataToServer(String payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Cannot send data.");
    return false;
  }
  
  // WiFiClientSecure 
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // For testing - in production will use proper certificates
  
  http.begin(secureClient, serverURL);
  
  // headers 
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("User-Agent", "ESP8266-DHT-Logger/2.0-HighFreq");
  
  http.setTimeout(15000); // 15 second timeout
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    
    // Only show detailed response for errors or first few transmissions
    if (httpResponseCode != 200 || totalTransmissions <= 5) {
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      Serial.print("Response: ");
      Serial.println(response);
    }
    
    // Handle different response codes
    if (httpResponseCode == 301 || httpResponseCode == 302) {
      Serial.println("⚠️  REDIRECT DETECTED!");
      String location = http.header("Location");
      if (location.length() > 0) {
        Serial.print("Redirect to: ");
        Serial.println(location);
      }
    }
    
    http.end();
    return (httpResponseCode >= 200 && httpResponseCode < 300);
    
  } else {
    Serial.print("HTTP Error Code: ");
    Serial.println(httpResponseCode);
    
    // Detailed error reporting
    switch(httpResponseCode) {
      case HTTPC_ERROR_CONNECTION_REFUSED:
        Serial.println("Connection refused");
        break;
      case HTTPC_ERROR_SEND_HEADER_FAILED:
        Serial.println("Send header failed");
        break;
      case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
        Serial.println("Send payload failed");
        break;
      case HTTPC_ERROR_NOT_CONNECTED:
        Serial.println("Not connected");
        break;
      case HTTPC_ERROR_CONNECTION_LOST:
        Serial.println("Connection lost");
        break;
      case HTTPC_ERROR_NO_STREAM:
        Serial.println("No stream");
        break;
      case HTTPC_ERROR_NO_HTTP_SERVER:
        Serial.println("No HTTP server");
        break;
      case HTTPC_ERROR_TOO_LESS_RAM:
        Serial.println("Too less RAM");
        break;
      case HTTPC_ERROR_ENCODING:
        Serial.println("Encoding error");
        break;
      case HTTPC_ERROR_STREAM_WRITE:
        Serial.println("Stream write error");
        break;
      case HTTPC_ERROR_READ_TIMEOUT:
        Serial.println("Read timeout");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
    
    http.end();
    return false;
  }
}
