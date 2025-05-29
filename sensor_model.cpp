/*
 * sensor_model.cpp
 * DHT22 Sensor Model Implementation
 * Part of SensorNode_8266 project
 */

#include "sensor_model.h"
#include <time.h>

// Constructor
SensorModel::SensorModel() : dht(DHT_PIN, DHT_TYPE), lastReadingTime(0) {
  lastError = "";
}

// Initialize the sensor
bool SensorModel::begin() {
  Serial.println("Initializing DHT22 sensor...");
  
  dht.begin();
  delay(2000); // Give sensor time to stabilize
  
  // Test initial reading
  if (testSensor()) {
    Serial.println("DHT22 sensor initialized successfully");
    return true;
  } else {
    lastError = "Failed to initialize DHT22 sensor";
    Serial.println("ERROR: " + lastError);
    return false;
  }
}

// Take a sensor reading
SensorReading SensorModel::takeSensorReading() {
  SensorReading reading;
  
  if (!isReadyForReading()) {
    lastError = "Sensor not ready - too soon since last reading";
    return reading; // Returns invalid reading
  }
  
  Serial.println("Taking sensor reading...");
  
  // Read from DHT22
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // Celsius by default
  
  // Check if readings are valid
  if (isnan(humidity) || isnan(temperature)) {
    lastError = "Failed to read from DHT22 sensor - NaN values";
    Serial.println("ERROR: " + lastError);
    return reading; // Returns invalid reading
  }
  
  // Validate reading ranges
  if (!validateReading(temperature, humidity)) {
    lastError = "Sensor reading out of valid range";
    Serial.println("ERROR: " + lastError);
    return reading; // Returns invalid reading
  }
  
  // Create valid reading
  reading.temperature = temperature;
  reading.humidity = humidity;
  reading.timestamp = millis(); // You may want to use NTP time instead
  reading.isValid = true;
  lastReadingTime = millis();
  
  Serial.printf("Reading successful - Temp: %.2f°C, Humidity: %.2f%%\n", 
                temperature, humidity);
  
  lastError = "";
  return reading;
}

// Check if sensor is ready for another reading
bool SensorModel::isReadyForReading() {
  return (millis() - lastReadingTime) >= MIN_READING_INTERVAL;
}

// Set sensor metadata
void SensorModel::setMetadata(const SensorMetadata& meta) {
  metadata = meta;
  Serial.println("Sensor metadata updated:");
  Serial.println("  ID: " + metadata.sensor_id);
  Serial.println("  Location: " + metadata.location_name);
  Serial.printf("  Coordinates: %.6f, %.6f\n", metadata.latitude, metadata.longitude);
}

// Get sensor metadata
SensorMetadata SensorModel::getMetadata() const {
  return metadata;
}

// Get last error message
String SensorModel::getLastError() const {
  return lastError;
}

// Test if sensor is responding
bool SensorModel::testSensor() {
  float testTemp = dht.readTemperature();
  float testHumidity = dht.readHumidity();
  
  if (isnan(testTemp) || isnan(testHumidity)) {
    return false;
  }
  
  return validateReading(testTemp, testHumidity);
}

// Validate reading ranges (private method)
bool SensorModel::validateReading(float temp, float humidity) {
  return SensorUtils::isValidTemperature(temp) && SensorUtils::isValidHumidity(humidity);
}

// Utility functions implementation
namespace SensorUtils {
  
  String formatTimestamp(unsigned long timestamp) {
    return String(timestamp);
  }
  
  bool isValidTemperature(float temp) {
    // DHT22 range: -40 to 80°C
    return (temp >= -40.0 && temp <= 80.0);
  }
  
  bool isValidHumidity(float humidity) {
    // DHT22 range: 0 to 100% RH
    return (humidity >= 0.0 && humidity <= 100.0);
  }
}
