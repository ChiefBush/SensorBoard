// =============================================================================
// ConfigManager.h - Configuration Management Header
// =============================================================================
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

struct SystemConfig {
  String deviceId;
  String firmwareVersion;
  int debugLevel;
  unsigned long watchdogTimeout;
  bool restartOnFailure;
};

struct WiFiConfig {
  String ssid;
  String password;
  unsigned long connectionTimeout;
  int retryAttempts;
  bool powerSaveMode;
};

struct SensorConfig {
  String type;
  int pin;
  unsigned long readInterval;
  int validationAttempts;
  float spikeThresholdTemp;
  float spikeThresholdHumidity;
};

struct LocationConfig {
  float latitude;
  float longitude;
  int timezoneOffset;
  String locationName;
};

struct NetworkConfig {
  String serverUrl;
  unsigned long transmissionInterval;
  unsigned long timeout;
  int retryAttempts;
  int batchSize;
  WiFiConfig wifiConfig;
};

struct BufferConfig {
  int maxSize;
  unsigned long cacheDuration;
  unsigned long cleanupInterval;
  bool emergencyFlush;
};

class ConfigManager {
private:
  SystemConfig systemConfig;
  WiFiConfig wifiConfig;
  SensorConfig sensorConfig;
  LocationConfig locationConfig;
  NetworkConfig networkConfig;
  BufferConfig bufferConfig;
  String secretKey;
  
  bool loadBootConfig();
  bool loadRuntimeConfig();
  bool parseBootConfig(const String& json);
  bool parseRuntimeConfig(const String& json);

public:
  bool initialize();
  
  // Getters
  SystemConfig getSystemConfig() const { return systemConfig; }
  WiFiConfig getWiFiConfig() const { return wifiConfig; }
  SensorConfig getSensorConfig() const { return sensorConfig; }
  LocationConfig getLocationConfig() const { return locationConfig; }
  NetworkConfig getNetworkConfig() const { return networkConfig; }
  BufferConfig getBufferConfig() const { return bufferConfig; }
  String getSecretKey() const { return secretKey; }
  
  unsigned long getSensorReadInterval() const { return sensorConfig.readInterval; }
  unsigned long getTransmissionInterval() const { return networkConfig.transmissionInterval; }
  
  void printConfiguration();
};

#endif
