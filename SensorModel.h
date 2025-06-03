// =============================================================================
// SensorModel.h - Sensor Management Header
// =============================================================================
#ifndef SENSOR_MODEL_H
#define SENSOR_MODEL_H

#include <Arduino.h>
#include <DHT.h>
#include "ConfigManager.h"

struct SensorReading {
  float temperature;
  float humidity;
  unsigned long timestamp;
  unsigned long readDuration;
  bool isValid;
  int readingId;
  
  SensorReading() : temperature(-999), humidity(-999), timestamp(0), 
                   readDuration(0), isValid(false), readingId(0) {}
};

struct SensorMetrics {
  unsigned long totalReadings;
  unsigned long failedReadings;
  unsigned long lastSuccessfulRead;
  float avgReadTime;
  bool isOnline;
};

class SensorModel {
private:
  DHT* dht;
  SensorConfig config;
  SensorMetrics metrics;
  SensorReading lastValidReading;
  int nextReadingId;
  
  bool validateReading(float temp, float humidity);
  bool detectSpike(float temp, float humidity);

public:
  SensorModel();
  ~SensorModel();
  
  bool initialize(const SensorConfig& sensorConfig);
  SensorReading readSensor();
  SensorReading getLastValidReading() const { return lastValidReading; }
  SensorMetrics getMetrics() const { return metrics; }
  bool isHealthy() const;
  void calibrate();
};

#endif
