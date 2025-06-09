// =============================================================================
// TransmitHandler.h - Network Transmission Header
// =============================================================================
#ifndef TRANSMIT_HANDLER_H
#define TRANSMIT_HANDLER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "ConfigManager.h"

struct NetworkMetrics {
  unsigned long totalRequests;
  unsigned long successfulRequests;
  unsigned long failedRequests;
  unsigned long avgResponseTime;
  int currentSignalStrength;
  bool isConnected;
  unsigned long lastConnectionTime;
};

class TransmitHandler {
private:
  NetworkConfig config;
  NetworkMetrics metrics;
  WiFiClientSecure secureClient;
  HTTPClient http;
  
  bool connectToWiFi(const WiFiConfig& wifiConfig);
  bool ensureConnection(const WiFiConfig& wifiConfig);

public:
  TransmitHandler();
  
  bool initialize(const NetworkConfig& networkConfig);
  // Build form-encoded payload (no security/crypto)
  String buildFormPayload(const SensorReading& reading, const ConfigManager& config, unsigned long dataAge, int readingAge);
  bool sendData(const String& payload);
  NetworkMetrics getMetrics() const { return metrics; }
  bool isHealthy() const;
  void reconnect();
};

#endif
