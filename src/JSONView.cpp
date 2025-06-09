// =============================================================================
// JSONView.cpp - JSON Formatting and Display Implementation
// =============================================================================
#include "../include/JSONView.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

String JSONView::createTransmissionPayload(
  const SensorReading& reading,
  const ConfigManager& config,
  const BufferMetadata& bufferMeta,
  unsigned long currentTime
) {
  // Check available memory
  if (ESP.getFreeHeap() < 4096) {
    Serial.println("!-! Low memory warning!");
    ESP.wdtFeed(); // Feed the watchdog
  }
  
  DynamicJsonDocument doc(1024);
  
  // Basic sensor data
  doc["temperature"] = reading.temperature;
  doc["humidity"] = reading.humidity;
  doc["timestamp"] = reading.timestamp;
  doc["is_valid"] = reading.isValid;
  
  // Device information
  doc["device_id"] = config.getSystemConfig().deviceId;
  doc["firmware_version"] = config.getSystemConfig().firmwareVersion;
  
  // Location data
  doc["latitude"] = config.getLocationConfig().latitude;
  doc["longitude"] = config.getLocationConfig().longitude;
  
  // Buffer metrics
  doc["buffer_metrics"]["total_transmissions"] = bufferMeta.totalTransmissions;
  doc["buffer_metrics"]["successful_transmissions"] = bufferMeta.successfulTransmissions;
  doc["buffer_metrics"]["failed_transmissions"] = bufferMeta.failedTransmissions;
  doc["buffer_metrics"]["cache_hits"] = bufferMeta.cacheHits;
  doc["buffer_metrics"]["cache_misses"] = bufferMeta.cacheMisses;
  doc["buffer_metrics"]["success_rate"] = bufferMeta.successRate;
  doc["buffer_metrics"]["current_buffer_size"] = bufferMeta.currentBufferSize;
  doc["buffer_metrics"]["oldest_entry_age"] = bufferMeta.oldestEntryAge;
  
  // System metrics
  doc["system_metrics"]["heap_free"] = ESP.getFreeHeap();
  doc["system_metrics"]["uptime"] = millis();
  doc["system_metrics"]["rssi"] = WiFi.RSSI();
  
  String output;
  if (serializeJson(doc, output) == 0) {
    Serial.println("!-! JSON serialization failed!");
    return "{}";
  }
  
  return output;
}

void JSONView::printSystemStatus(
  const ConfigManager& config,
  const SensorModel& sensor,
  const BufferLogic& buffer
) {
  DynamicJsonDocument doc(1024);
  
  // System configuration
  doc["system"]["device_id"] = config.getSystemConfig().deviceId;
  doc["system"]["firmware_version"] = config.getSystemConfig().firmwareVersion;
  doc["system"]["debug_level"] = config.getSystemConfig().debugLevel;
  
  // WiFi status
  doc["wifi"]["ssid"] = config.getWiFiConfig().ssid;
  doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
  doc["wifi"]["rssi"] = WiFi.RSSI();
  
  // Sensor configuration
  doc["sensor"]["type"] = config.getSensorConfig().type;
  doc["sensor"]["pin"] = config.getSensorConfig().pin;
  doc["sensor"]["read_interval"] = config.getSensorConfig().readInterval;
  
  // Buffer status
  BufferMetadata meta = buffer.getMetadata();
  doc["buffer"]["total_transmissions"] = meta.totalTransmissions;
  doc["buffer"]["success_rate"] = meta.successRate;
  doc["buffer"]["current_size"] = meta.currentBufferSize;
  
  String output;
  serializeJsonPretty(doc, output);
  Serial.println("\n=== System Status ===");
  Serial.println(output);
  Serial.println("===================\n");
}

void JSONView::printPerformanceMetrics(
  const BufferLogic& buffer,
  const TransmitHandler& transmitter
) {
  DynamicJsonDocument doc(512);
  
  // Buffer metrics
  BufferMetadata bufferMeta = buffer.getMetadata();
  doc["buffer"]["total_transmissions"] = bufferMeta.totalTransmissions;
  doc["buffer"]["success_rate"] = bufferMeta.successRate;
  doc["buffer"]["cache_hits"] = bufferMeta.cacheHits;
  doc["buffer"]["cache_misses"] = bufferMeta.cacheMisses;
  
  // Network metrics
  NetworkMetrics networkMeta = transmitter.getMetrics();
  doc["network"]["total_requests"] = networkMeta.totalRequests;
  doc["network"]["successful_requests"] = networkMeta.successfulRequests;
  doc["network"]["failed_requests"] = networkMeta.failedRequests;
  doc["network"]["avg_response_time"] = networkMeta.avgResponseTime;
  doc["network"]["signal_strength"] = networkMeta.currentSignalStrength;
  
  String output;
  serializeJsonPretty(doc, output);
  Serial.println("\n=== Performance Metrics ===");
  Serial.println(output);
  Serial.println("=========================\n");
}

String JSONView::formatSensorReading(const SensorReading& reading) {
  DynamicJsonDocument doc(256);
  
  doc["temperature"] = reading.temperature;
  doc["humidity"] = reading.humidity;
  doc["timestamp"] = reading.timestamp;
  doc["is_valid"] = reading.isValid;
  
  String output;
  serializeJson(doc, output);
  return output;
}

String JSONView::createStatusReport() {
  DynamicJsonDocument doc(512);
  
  doc["heap_free"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["rssi"] = WiFi.RSSI();
  doc["chip_id"] = ESP.getChipId();
  
  String output;
  serializeJson(doc, output);
  return output;
} 