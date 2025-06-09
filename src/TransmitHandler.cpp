// =============================================================================
// TransmitHandler.cpp - Network Transmission Implementation
// =============================================================================
#include "../include/TransmitHandler.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Commented out security/crypto includes and logic
// #include "../include/SecurityLogic.h"
// #include <Crypto.h>
// #include <SHA256.h>
// #include <AES.h>
// #include <CBC.h>

TransmitHandler::TransmitHandler() {
  metrics.totalRequests = 0;
  metrics.successfulRequests = 0;
  metrics.failedRequests = 0;
  metrics.avgResponseTime = 0;
  metrics.currentSignalStrength = 0;
  metrics.isConnected = false;
  metrics.lastConnectionTime = 0;
}

bool TransmitHandler::initialize(const NetworkConfig& networkConfig) {
  config = networkConfig;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  secureClient.setInsecure();
  Serial.println("-> Network transmission system initialized");
  return true;
}

// Helper to build form-encoded payload (fields must match the user's script)
String TransmitHandler::buildFormPayload(const SensorReading& reading, const ConfigManager& config, unsigned long dataAge, int readingAge) {
  String payload = "";
  payload += "secret=" + config.getSecretKey();
  payload += "&sensor_unique_id=" + config.getSystemConfig().deviceId;
  payload += "&Temperature(K)=" + String(reading.temperature + 273.15, 2);
  payload += "&humidity(%)=" + String((int)round(reading.humidity));
  payload += "&sensor_longitude=" + String(config.getLocationConfig().longitude, 6);
  payload += "&sensor_latitude=" + String(config.getLocationConfig().latitude, 6);
  payload += "&receiving_date=" + String(reading.timestamp);
  payload += "&rdf_metadata=sensor_type:DHT11,location:indoor,purpose:environmental_monitoring,transmission_mode:cached_high_frequency";
  payload += "&download_metadata=chip_id:" + String(ESP.getChipId(), HEX) + ",data_age_ms:" + String(dataAge) + ",reading_age:" + String(readingAge);
  return payload;
}

// Send form-encoded data (no security, no JSON)
bool TransmitHandler::sendData(const String& payload) {
  if (!metrics.isConnected) {
    Serial.println("!-! Not connected to WiFi!");
    return false;
  }
  
  unsigned long startTime = millis();
  metrics.totalRequests++;
  
  http.begin(secureClient, config.serverUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("User-Agent", "ESP8266-DHT-Logger/2.0-HighFreq");
  http.setTimeout(config.timeout);
  
  int httpCode = http.POST(payload);
  unsigned long responseTime = millis() - startTime;
  metrics.avgResponseTime = (metrics.avgResponseTime * (metrics.totalRequests - 1) + responseTime) / metrics.totalRequests;
  metrics.currentSignalStrength = WiFi.RSSI();
  bool success = (httpCode >= 200 && httpCode < 300);
  
  if (success) {
    metrics.successfulRequests++;
    String response = http.getString();
    Serial.printf("📤 Transmission #%lu: SUCCESS (%.1fms)\n", metrics.totalRequests, (float)responseTime);
    Serial.printf("Response: %s\n", response.c_str());
  } else {
    metrics.failedRequests++;
    Serial.printf("❌ Transmission #%lu: FAILED (HTTP %d)\n", metrics.totalRequests, httpCode);
  }
  http.end();
  return success;
}

bool TransmitHandler::isHealthy() const {
  return metrics.isConnected && 
         (metrics.successfulRequests > 0 || metrics.totalRequests == 0) &&
         metrics.currentSignalStrength > -80;
}

void TransmitHandler::reconnect() {
  Serial.println("Attempting to reconnect to WiFi...");
  
  WiFi.disconnect();
  delay(1000);
  
  WiFi.begin(config.wifiConfig.ssid.c_str(), config.wifiConfig.password.c_str());
  
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttempt < config.wifiConfig.connectionTimeout) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    metrics.isConnected = true;
    metrics.lastConnectionTime = millis();
    Serial.println("\n-> WiFi reconnected successfully!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    metrics.isConnected = false;
    Serial.println("\n!-! WiFi reconnection failed!");
  }
}

bool TransmitHandler::connectToWiFi(const WiFiConfig& wifiConfig) {
  Serial.printf("Connecting to %s...\n", wifiConfig.ssid.c_str());
  
  WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
  
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttempt < wifiConfig.connectionTimeout) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    metrics.isConnected = true;
    metrics.lastConnectionTime = millis();
    metrics.currentSignalStrength = WiFi.RSSI();
    
    Serial.println("\n-> WiFi connected successfully!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  } else {
    metrics.isConnected = false;
    Serial.println("\n!-! WiFi connection failed!");
    return false;
  }
}

bool TransmitHandler::ensureConnection(const WiFiConfig& wifiConfig) {
  if (WiFi.status() != WL_CONNECTED) {
    return connectToWiFi(wifiConfig);
  }
  return true;
} 