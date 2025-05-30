#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WiFi credentials 
const char* ssid = "KRC";
const char* password = "pinkzebra1";

// Server configuration - Try with trailing slash
const char* serverURL = "https://lostdevs.io/uploader.php/";
const char* secretKey = "lostdev-sensor1-1008200303082003";

// DHT sensor configuration 
#define DHT_PIN 2        // GPIO2 (D4 on NodeMCU)
#define DHT_TYPE DHT11   
// #define DHT_TYPE DHT22   

DHT dht(DHT_PIN, DHT_TYPE);

// Alternative pins 
// #define DHT_PIN 4     // GPIO4 (D2 on NodeMCU)  
// #define DHT_PIN 5     // GPIO5 (D1 on NodeMCU)
// #define DHT_PIN 14    // GPIO14 (D5 on NodeMCU)

// NTP Client for timestamp
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // UTC time, update every minute

// Sensor configuration
const int SENSOR_ID = 1001;  // Unique sensor identifier (4-digit integer)
const float LATITUDE = 28.6139;     //  latitude
const float LONGITUDE = 77.2090;    //  longitude

// Timing configuration
unsigned long lastReading = 0;
const unsigned long readingInterval = 500; // 0.5 seconds between readings

// Error tracking
int wifiFailures = 0;
int sensorFailures = 0;
int httpFailures = 0;
unsigned long lastSuccessfulReading = 0;

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(2000); // Increased delay for serial monitor
  
  Serial.println("\n\n=== ESP8266 DHT22 Data Logger Starting ===");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId(), HEX);
  Serial.print("Flash Chip ID: ");
  Serial.println(ESP.getFlashChipId(), HEX);
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Core Version: ");
  Serial.println(ESP.getCoreVersion());
  Serial.println("Configured for: https://lostdevs.io/uploader.php");
  Serial.println("==========================================\n");
  
  
  Serial.println("Initializing DHT sensor...");
  
  dht.begin();
  delay(3000); // DHT11 needs longer stabilization time
  
  // Test DHT sensor before WiFi connection with multiple attempts
  Serial.println("Testing DHT sensor...");
  bool sensorWorking = false;
  
  for (int i = 0; i < 5; i++) {
    #if DHT_TYPE == DHT11
      delay(2500); // DHT11 needs at least 2 seconds between readings
    #else  
      delay(2000); // DHT22 needs 2 seconds between readings
    #endif
    
    float testTemp = dht.readTemperature();
    float testHumid = dht.readHumidity();
    
    Serial.printf("Attempt %d: Temp=%.1f, Humidity=%.1f\n", i+1, testTemp, testHumid);
    
    if (!isnan(testTemp) && !isnan(testHumid)) {
      #if DHT_TYPE == DHT11
        Serial.printf("DHT11 working - Temp: %.0f°C, Humidity: %.0f%%\n", testTemp, testHumid);
      #else
        Serial.printf("DHT22 working - Temp: %.1f°C, Humidity: %.1f%%\n", testTemp, testHumid);
      #endif
      sensorWorking = true;
      break;
    }
  }
  
  if (!sensorWorking) {
    Serial.println("DHT sensor FAILED after 5 attempts!");
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize NTP client
  Serial.println("Initializing NTP client...");
  timeClient.begin();
  timeClient.setTimeOffset(0); // UTC time
  
  // Force first NTP update with better error handling
  Serial.println("Getting initial time from NTP server...");
  int ntpAttempts = 0;
  bool ntpSuccess = false;
  
  while (ntpAttempts < 20) { // Increased attempts
    if (timeClient.update()) {
      ntpSuccess = true;
      break;
    }
    Serial.print(".");
    delay(1000);
    ntpAttempts++;
    
    // Try different NTP servers if first fails
    if (ntpAttempts == 10) {
      Serial.println("\nTrying alternative NTP server...");
      timeClient.setPoolServerName("time.google.com");
    }
  }
  
  if (ntpSuccess) {
    Serial.println("NTP time synchronized");
    Serial.print("Current time: ");
    Serial.println(getISOTimestamp());
  } else {
    Serial.println("Warning: Could not sync with NTP server");
    Serial.println("Will use system time (may be inaccurate)");
  }
  
  Serial.println("\nSetup completed successfully!");
  Serial.println("Starting data collection...\n");
}

void loop() {
  // Check if it's time to take a reading
  if (millis() - lastReading >= readingInterval) {
    
    unsigned long readStartTime = millis();
    
    // Ensure WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Reconnecting...");
      wifiFailures++;
      connectToWiFi();
    }
    
    // Update time from NTP server (only every 60 seconds to avoid overload)
    static unsigned long lastNtpUpdate = 0;
    if (millis() - lastNtpUpdate > 60000) {
      timeClient.update();
      lastNtpUpdate = millis();
    }
    
    // Read sensor data with appropriate timing for sensor type
    #if DHT_TYPE == DHT11
      static unsigned long lastDHTRead = 0;
      // DHT11 needs minimum 2 seconds between readings
      if (millis() - lastDHTRead < 2500) {
        lastReading = millis();
        return; // Skip this reading
      }
      lastDHTRead = millis();
    #endif
    
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    unsigned long readEndTime = millis();
    unsigned long readDuration = readEndTime - readStartTime;
    
    // Get current timestamp in ISO format
    String timestamp = getISOTimestamp();
    
    // Check if readings are valid
    bool sensorError = false;
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      sensorFailures++;
      sensorError = true;
      // Still send data with error flags
      temperature = -999.0; // Error value
      humidity = -999.0;    // Error value
    } else {
      lastSuccessfulReading = millis();
    }
    
    // Create FORM payload (FIXED - changed from JSON to form data)
    String formPayload = createFormPayload(temperature, humidity, timestamp, readDuration, sensorError);
    
    // Send data to server
    bool httpSuccess = sendDataToServer(formPayload);
    if (!httpSuccess) {
      httpFailures++;
    }
    
    // Update last reading time
    lastReading = millis();
    
    // Print data to serial for debugging
    Serial.println("Data sent:");
    Serial.println(formPayload);
    Serial.printf("Read time: %lu ms\n", readDuration);
    Serial.println("---");
  }
  
  delay(10); // Small delay to prevent excessive CPU usage
}

void connectToWiFi() {
  // Print WiFi debugging info
  Serial.println("\n=== WiFi Debug Info ===");
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  Serial.print("Password length: ");
  Serial.println(strlen(password));
  Serial.print("WiFi Mode: ");
  Serial.println(WiFi.getMode());
  
  // Disconnect any previous connection
  WiFi.disconnect();
  delay(1000);
  
  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  delay(1000);
  
  // Scan for available networks
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found!");
  } else {
    Serial.printf("Found %d networks:\n", n);
    for (int i = 0; i < n; ++i) {
      Serial.printf("%d: %s (%d dBm) %s\n", 
        i + 1, 
        WiFi.SSID(i).c_str(), 
        WiFi.RSSI(i),
        (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Open" : "Encrypted"
      );
      
      // Check if our target SSID is found
      if (WiFi.SSID(i) == ssid) {
        Serial.println(">>> Target network found! <<<");
      }
    }
  }
  Serial.println("========================\n");
  
  // Begin connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) { // Increased to 60 attempts
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Print status every 10 attempts
    if (attempts % 10 == 0) {
      Serial.println();
      Serial.print("Status: ");
      printWiFiStatus();
      Serial.print("Attempt ");
      Serial.print(attempts);
      Serial.print("/60: ");
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi Connected Successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println();
    Serial.println("Failed to connect to WiFi!");
    Serial.print("Final status: ");
    printWiFiStatus();
    Serial.println("Waiting 10 seconds before restart...");
    delay(10000);
    ESP.restart();
  }
}

void printWiFiStatus() {
  switch(WiFi.status()) {
    case WL_IDLE_STATUS:
      Serial.println("WL_IDLE_STATUS - WiFi is in process of changing between statuses");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("WL_NO_SSID_AVAIL - SSID cannot be reached");
      break;
    case WL_SCAN_COMPLETED:
      Serial.println("WL_SCAN_COMPLETED - Scan networks is completed");
      break;
    case WL_CONNECTED:
      Serial.println("WL_CONNECTED - Connected to WiFi");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("WL_CONNECT_FAILED - Connection failed");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("WL_CONNECTION_LOST - Connection lost");
      break;
    case WL_DISCONNECTED:
      Serial.println("WL_DISCONNECTED - Disconnected from network");
      break;
    case WL_NO_SHIELD:
      Serial.println("WL_NO_SHIELD - No WiFi shield is present");
      break;
    default:
      Serial.println("Unknown WiFi status");
      break;
  }
}

String getISOTimestamp() {
  unsigned long epochTime = timeClient.getEpochTime();
  
  // Convert epoch time to ISO 8601 format
  int year, month, day, hour, minute, second;
  
  second = epochTime % 60;
  epochTime /= 60;
  minute = epochTime % 60;
  epochTime /= 60;
  hour = epochTime % 24;
  epochTime /= 24;
  
  // Calculate date from days since epoch (1970-01-01)
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
  
  // Month calculation
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
  
  // Format as ISO 8601 string
  char isoTime[25];
  sprintf(isoTime, "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour, minute, second);
  
  return String(isoTime);
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// FIXED FUNCTION - Changed from JSON to form data to match server expectations
String createFormPayload(float temp, float humid, String timestamp, unsigned long readTime, bool sensorError) {
  String payload = "";
  
  // Add secret key for authentication (FIXED: parameter name is "secret", not "secret_key")
  payload += "secret=" + String(secretKey);
  
  // FIXED: Use correct parameter name "sensor_unique_id" instead of "sensor_id"
  payload += "&sensor_unique_id=" + String(SENSOR_ID);
  
  // Temperature and humidity (server expects these exact names)
  if (sensorError) {
    payload += "&temperature=-999";
    payload += "&humidity=-999";
  } else {
    #if DHT_TYPE == DHT11
      payload += "&temperature=" + String((int)round(temp));
      payload += "&humidity=" + String((int)round(humid));
    #else
      payload += "&temperature=" + String(temp, 1);
      payload += "&humidity=" + String(humid, 1);
    #endif
  }
  
  // FIXED: Location data parameter names are "sensor_latitude" and "sensor_longitude"
  payload += "&sensor_latitude=" + String(LATITUDE, 6);
  payload += "&sensor_longitude=" + String(LONGITUDE, 6);
  
  // FIXED: Timestamp parameter name is "receiving_date", not "timestamp"
  payload += "&receiving_date=" + timestamp;
  
  // Optional: Add diagnostic data as additional parameters
  payload += "&read_time_ms=" + String(readTime);
  payload += "&wifi_rssi=" + String(WiFi.RSSI());
  payload += "&free_heap=" + String(ESP.getFreeHeap());
  payload += "&uptime_ms=" + String(millis());
  payload += "&sensor_error=" + String(sensorError ? "1" : "0");
  payload += "&wifi_failures=" + String(wifiFailures);
  payload += "&sensor_failures=" + String(sensorFailures);
  payload += "&http_failures=" + String(httpFailures);
  
  return payload;
}

// FIXED FUNCTION - Changed to send form data instead of JSON
bool sendDataToServer(String payload) {
  if (WiFi.status() == WL_CONNECTED) {
    http.begin(wifiClient, serverURL);
    
    // FIXED: Change Content-Type to form data instead of JSON
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", "ESP8266-DHT22-Logger");
    
    // REMOVED: Authorization header since we're using form parameter for secret
    http.setTimeout(10000); // Increased timeout to 10 seconds
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Handle 301 redirects
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      
      // Handle different response codes
      if (httpResponseCode == 301 || httpResponseCode == 302) {
        Serial.println("⚠️  SERVER REDIRECT DETECTED!");
        Serial.println("Fix your serverURL - check for:");
        Serial.println("1. Missing trailing slash");
        Serial.println("2. HTTP vs HTTPS");
        Serial.println("3. Correct path/domain");
        
        // Print all headers for debugging
        Serial.println("All response headers:");
        for (int i = 0; i < http.headers(); i++) {
          Serial.printf("Header %d: %s = %s\n", i, http.headerName(i).c_str(), http.header(i).c_str());
        }
        
        String location = http.header("Location");
        if (location.length() > 0) {
          Serial.print("Redirect to: ");
          Serial.println(location);
        } else {
          Serial.println("No Location header found in redirect response");
        }
        
        // Try alternative URLs
        Serial.println("\nSuggested URLs to try:");
        Serial.println("1. https://lostdevs.io/uploader.php/");
        Serial.println("2. https://www.lostdevs.io/uploader.php");
        Serial.println("3. https://www.lostdevs.io/uploader.php/");
        Serial.println("4. http://lostdevs.io/uploader.php");
        Serial.println("5. http://lostdevs.io/uploader.php/");
      }
      
      Serial.print("Server Response: ");
      Serial.println(response);
      
      http.end();
      return (httpResponseCode >= 200 && httpResponseCode < 300); // Success codes 2xx
    } else {
      Serial.print("HTTP Error: ");
      Serial.println(httpResponseCode);
      Serial.println("Connection failed - check server URL and network");
      http.end();
      return false;
    }
  } else {
    Serial.println("WiFi not connected. Cannot send data.");
    return false;
  }
}
