// =============================================================================
// MainController.ino - Main Arduino Sketch
// =============================================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "./include/ConfigManager.h"
#include "./include/SensorModel.h"
#include "./include/BufferLogic.h"
#include "./include/TransmitHandler.h"
#include "./include/SecurityLogic.h"
#include "./include/JSONView.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

// Global instances
ConfigManager configManager;
SensorModel sensorModel;
BufferLogic bufferLogic;
TransmitHandler transmitHandler;
SecurityLogic securityLogic;
JSONView jsonView;

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

// Timing variables
unsigned long lastSensorReading = 0;
unsigned long lastDataTransmission = 0;
unsigned long lastStatusReport = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== MODULAR ESP8266 DHT LOGGER STARTING ===");
  
  // Initialize configuration system
  if (!configManager.initialize()) {
    Serial.println("!-! Configuration initialization failed!");
    ESP.restart();
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  // Initialize security layer
  if (!securityLogic.initialize(configManager.getSecretKey())) {
    Serial.println("!-! Security initialization failed!");
    ESP.restart();
  }
  
  // Initialize sensor
  if (!sensorModel.initialize(configManager.getSensorConfig())) {
    Serial.println("!!!  Sensor initialization had issues, continuing...");
  }
  
  // Initialize buffer system
  bufferLogic.initialize(configManager.getBufferConfig());
  
  // Initialize transmission handler with WiFi config
  NetworkConfig networkConfig = configManager.getNetworkConfig();
  networkConfig.wifiConfig = configManager.getWiFiConfig();  // Set WiFi config
  if (!transmitHandler.initialize(networkConfig)) {
    Serial.println("!-! Network initialization failed!");
    ESP.restart();
  }
  
  // Initialize NTP client
  timeClient.begin();
  timeClient.setTimeOffset(19800); // IST offset
  
  Serial.println("-> All systems initialized successfully!");
  Serial.println("System Configuration:");
  jsonView.printSystemStatus(configManager, sensorModel, bufferLogic);
  Serial.println("==========================================\n");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Update NTP time periodically
  static unsigned long lastNtpUpdate = 0;
  if (currentTime - lastNtpUpdate > 60000) {
    timeClient.update();
    lastNtpUpdate = currentTime;
  }
  
  // Read sensor data
  if (currentTime - lastSensorReading >= configManager.getSensorReadInterval()) {
    SensorReading reading = sensorModel.readSensor();
    reading.timestamp = timeClient.getEpochTime();
    
    bufferLogic.addReading(reading);
    lastSensorReading = currentTime;
    
    if (reading.isValid) {
      Serial.printf("New reading: %.1f°C, %.1f%%\n", reading.temperature, reading.humidity);
    }
  }
  
  // Transmit data
  if (currentTime - lastDataTransmission >= configManager.getTransmissionInterval()) {
    SensorReading dataToSend = bufferLogic.getDataForTransmission();
    
    if (dataToSend.isValid) {
      String jsonPayload = jsonView.createTransmissionPayload(
        dataToSend, 
        configManager, 
        bufferLogic.getMetadata(),
        timeClient.getEpochTime()
      );
      
      String securePayload = securityLogic.signPayload(jsonPayload);
      bool success = transmitHandler.sendData(securePayload);
      
      bufferLogic.recordTransmissionResult(success);
      
      Serial.printf("📤 Transmission #%lu: %s\n", 
                    bufferLogic.getTotalTransmissions(), 
                    success ? "SUCCESS" : "FAILED");
    }
    
    lastDataTransmission = currentTime;
  }
  
  // Status report every 30 seconds
  if (currentTime - lastStatusReport >= 30000) {
    jsonView.printPerformanceMetrics(bufferLogic, transmitHandler);
    lastStatusReport = currentTime;
  }
  
  delay(50);
}

void initializeNTP() {
  Serial.println("🕐 Initializing NTP client...");
  timeClient.begin();
  timeClient.setTimeOffset(19800); // IST offset
  
  int attempts = 0;
  while (attempts < 20 && !timeClient.update()) {
    Serial.print(".");
    delay(1000);
    attempts++;
    
    if (attempts == 10) {
      timeClient.setPoolServerName("in.pool.ntp.org");
    }
  }
  
  if (timeClient.isTimeSet()) {
    Serial.println("\n->NTP synchronized to IST");
  } else {
    Serial.println("\n!!! NTP sync failed, using system time");
  }
}
