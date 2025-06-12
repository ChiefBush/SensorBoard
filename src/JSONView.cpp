// =============================================================================
// JSONView.cpp - JSON Formatting and Display Implementation
// =============================================================================
#include "../include/JSONView.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <time.h>

// Helper function to convert Unix timestamp to ISO 8601 format
String formatISOTimestamp(unsigned long timestamp) {
    time_t now = time(nullptr);
    if (now < 1000000000) { // If time is not synchronized (before year 2001)
        // Return a placeholder format with the millis value
        char buffer[30];
        sprintf(buffer, "1970-01-01T00:00:%luZ", timestamp / 1000);
        return String(buffer);
    }
    
    struct tm* timeinfo = gmtime(&now);
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
    return String(buffer);
}

String JSONView::createTransmissionPayload(
  const SensorReading& reading,
  const ConfigManager& config,
  const BufferMetadata& bufferMeta,
  unsigned long timestamp
) {
  // Check available memory
  if (ESP.getFreeHeap() < 4096) {
    Serial.println("!-! Low memory warning!");
    ESP.wdtFeed(); // Feed the watchdog
  }
  
  StaticJsonDocument<512> doc;
  
  doc["device_id"] = config.getSystemConfig().deviceId;
  doc["timestamp"] = formatISOTimestamp(timestamp);
  doc["temperature"] = reading.temperature;
  doc["humidity"] = reading.humidity;
  doc["buffer_size"] = bufferMeta.currentBufferSize;
  doc["buffer_capacity"] = MAX_BUFFER_SIZE;
  
  String output;
  serializeJson(doc, output);
  return output;
}

void JSONView::printSystemStatus(
  const ConfigManager& config,
  const SensorModel& sensor,
  const BufferLogic& buffer
) {
  StaticJsonDocument<256> doc;
  
  doc["status"] = "system_status";
  doc["device_id"] = config.getSystemConfig().deviceId;
  doc["buffer_size"] = buffer.getMetadata().currentBufferSize;
  doc["buffer_capacity"] = MAX_BUFFER_SIZE;
  doc["sensor_status"] = sensor.isHealthy() ? "connected" : "disconnected";
  doc["timestamp"] = formatISOTimestamp(millis());
  
  String output;
  serializeJson(doc, output);
  Serial.println(output);
}

void JSONView::printPerformanceMetrics(
  const BufferLogic& buffer,
  const TransmitHandler& transmitter
) {
  StaticJsonDocument<256> doc;
  
  doc["status"] = "performance_metrics";
  doc["buffer_size"] = buffer.getMetadata().currentBufferSize;
  doc["buffer_capacity"] = MAX_BUFFER_SIZE;
  doc["transmission_count"] = transmitter.getTransmissionCount();
  doc["last_transmission_time"] = formatISOTimestamp(transmitter.getLastTransmissionTime());
  
  String output;
  serializeJson(doc, output);
  Serial.println(output);
}

String JSONView::formatSensorReading(const SensorReading& reading) {
  StaticJsonDocument<128> doc;
  
  doc["temperature"] = reading.temperature;
  doc["humidity"] = reading.humidity;
  doc["timestamp"] = formatISOTimestamp(reading.timestamp);
  
  String output;
  serializeJson(doc, output);
  return output;
}

String JSONView::createStatusReport() {
  StaticJsonDocument<128> doc;
  
  doc["status"] = "ok";
  doc["heap_free"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["timestamp"] = formatISOTimestamp(millis());
  
  String output;
  serializeJson(doc, output);
  return output;
} 