/*
 * sensor_model.h
 * DHT22 Sensor Model - Data structures and sensor interface
 * Part of SensorNode_8266 project
 */

#ifndef SENSOR_MODEL_H
#define SENSOR_MODEL_H

#include <Arduino.h>
#include <DHT.h>

// DHT22 Configuration
#define DHT_PIN 4        // GPIO4 (D2 on NodeMCU)
#define DHT_TYPE DHT22   // DHT22 sensor type

// Data structure for sensor readings
struct SensorReading {
  float temperature;      // Temperature in Celsius
  float humidity;         // Humidity percentage
  unsigned long timestamp; // Unix timestamp when reading was taken
  bool isValid;          // Flag to indicate if reading is valid
  
  // Constructor
  SensorReading() : temperature(0.0), humidity(0.0), timestamp(0), isValid(false) {}
};

// Data structure for sensor metadata
struct SensorMetadata {
  String sensor_id;      // Unique sensor identifier
  float latitude;        // GPS latitude
  float longitude;       // GPS longitude
  String location_name;  // Human readable location
  
  // Constructor
  SensorMetadata() : sensor_id(""), latitude(0.0), longitude(0.0), location_name("") {}
};

// Main sensor model class
class SensorModel {
private:
  DHT dht;
  SensorMetadata metadata;
  unsigned long lastReadingTime;
  static const unsigned long MIN_READING_INTERVAL = 2000; // 2 seconds minimum between readings
  
public:
  // Constructor
  SensorModel();
  
  // Initialization
  bool begin();
  
  // Sensor reading methods
  SensorReading takeSensorReading();
  bool isReadyForReading();
  
  // Metadata management
  void setMetadata(const SensorMetadata& meta);
  SensorMetadata getMetadata() const;
  
  // Utility methods
  String getLastError() const;
  bool testSensor(); // Test if DHT22 is responding
  
private:
  String lastError;
  bool validateReading(float temp, float humidity);
};

// Utility functions
namespace SensorUtils {
  String formatTimestamp(unsigned long timestamp);
  bool isValidTemperature(float temp);
  bool isValidHumidity(float humidity);
}

#endif // SENSOR_MODEL_H
