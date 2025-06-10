// =============================================================================
// ConfigManager.cpp - Configuration Management Implementation
// =============================================================================
#include "../include/ConfigManager.h"

bool ConfigManager::initialize() {
  Serial.println("🔧 Initializing Configuration Manager...");
  
  if (!SPIFFS.begin()) {
    Serial.println("!-! SPIFFS initialization failed!");
    return false;
  }
  
  bool bootConfigOk = loadBootConfig();
  bool runtimeConfigOk = loadRuntimeConfig();
  
  if (!bootConfigOk || !runtimeConfigOk) {
    Serial.println("!!!  Using default configuration values");
    // Set reasonable defaults
    systemConfig.deviceId = "ESP8266_DEFAULT";
    systemConfig.firmwareVersion = "2.0.0";
    systemConfig.debugLevel = 1;
    
    wifiConfig.ssid = "KRC-101C";
    wifiConfig.password = "krc101c@";
    wifiConfig.connectionTimeout = 30000;
    
    sensorConfig.type = "DHT11";
    sensorConfig.pin = 2;
    sensorConfig.readInterval = 3000;
    
    networkConfig.serverUrl = "https://lostdevs.io/ctrl1/master.php";
    networkConfig.transmissionInterval = 5000;
    networkConfig.timeout = 15000;
    
    secretKey = "lostdev-sensor1-1008200303082003";
  }
  
  Serial.println("-> Configuration Manager initialized");
  return true;
}

bool ConfigManager::loadBootConfig() {
  File file = SPIFFS.open("/BootConfig.json", "r");
  if (!file) {
    Serial.println("!!! BootConfig.json not found");
    return false;
  }
  
  String content = file.readString();
  file.close();
  
  return parseBootConfig(content);
}

bool ConfigManager::loadRuntimeConfig() {
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("!!! config.json not found");
    return false;
  }
  
  String content = file.readString();
  file.close();
  
  return parseRuntimeConfig(content);
}

bool ConfigManager::parseBootConfig(const String& json) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.printf("!-! Boot config parse error: %s\n", error.c_str());
    return false;
  }
  
  // Parse system config
  systemConfig.deviceId = doc["system"]["device_id"] | "ESP8266_DEFAULT";
  systemConfig.firmwareVersion = doc["system"]["firmware_version"] | "2.0.0";
  systemConfig.debugLevel = doc["system"]["debug_level"] | 1;
  
  // Parse WiFi config
  wifiConfig.ssid = doc["wifi"]["ssid"] | "DefaultSSID";
  wifiConfig.password = doc["wifi"]["password"] | "DefaultPassword";
  wifiConfig.connectionTimeout = doc["wifi"]["connection_timeout"] | 30000;
  
  // Parse sensor config
  sensorConfig.type = doc["sensor"]["type"] | "DHT11";
  sensorConfig.pin = doc["sensor"]["pin"] | 2;
  sensorConfig.readInterval = doc["sensor"]["read_interval"] | 3000;
  
  // Parse location config
  locationConfig.latitude = doc["location"]["latitude"] | 0.0;
  locationConfig.longitude = doc["location"]["longitude"] | 0.0;
  locationConfig.timezoneOffset = doc["location"]["timezone_offset"] | 0;
  
  return true;
}

bool ConfigManager::parseRuntimeConfig(const String& json) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.printf("!-! Runtime config parse error: %s\n", error.c_str());
    return false;
  }
  
  // Parse network config
  networkConfig.serverUrl = doc["transmission"]["server_url"] | "https://default.com";
  networkConfig.transmissionInterval = doc["transmission"]["interval"] | 5000;
  networkConfig.timeout = doc["transmission"]["timeout"] | 15000;
  
  // Parse buffer config
  bufferConfig.maxSize = doc["buffer"]["max_size"] | 50;
  bufferConfig.cacheDuration = doc["buffer"]["cache_duration"] | 300000;
  
  // Parse security config
  secretKey = doc["security"]["secret_key"] | "default_secret";
  
  return true;
}
