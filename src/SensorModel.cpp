// =============================================================================
// SensorModel.cpp - Sensor Management Implementation
// =============================================================================
#include "../include/SensorModel.h"

SensorModel::SensorModel() : dht(nullptr), nextReadingId(1) {
  metrics.totalReadings = 0;
  metrics.failedReadings = 0;
  metrics.lastSuccessfulRead = 0;
  metrics.avgReadTime = 0;
  metrics.isOnline = false;
}

SensorModel::~SensorModel() {
  if (dht) {
    delete dht;
  }
}

bool SensorModel::initialize(const SensorConfig& sensorConfig) {
  config = sensorConfig;
  
  Serial.printf("-> Initializing %s sensor on pin %d...\n", 
                config.type.c_str(), config.pin);
  
  // Initialize DHT sensor
  int dhtType = (config.type == "DHT22") ? DHT22 : DHT11;
  dht = new DHT(config.pin, dhtType);
  dht->begin();
  
  delay(3000); // Allow sensor to stabilize
  
  // Test sensor with multiple attempts
  bool sensorWorking = false;
  for (int i = 0; i < config.validationAttempts; i++) {
    delay(2500);
    
    SensorReading testReading = readSensor();
    if (testReading.isValid) {
      Serial.printf("-> Sensor test %d: %.1f°C, %.1f%%\n", 
                    i+1, testReading.temperature, testReading.humidity);
      sensorWorking = true;
      metrics.isOnline = true;
      break;
    } else {
      Serial.printf("!-! Sensor test %d failed\n", i+1);
    }
  }
  
  if (!sensorWorking) {
    Serial.println("!!!  Sensor validation failed, but continuing...");
  }
  
  return sensorWorking;
}

SensorReading SensorModel::readSensor() {
  SensorReading reading;
  reading.readingId = nextReadingId++;
  
  unsigned long startTime = millis();
  
  float temp = dht->readTemperature();
  float humid = dht->readHumidity();
  
  unsigned long endTime = millis();
  reading.readDuration = endTime - startTime;
  
  metrics.totalReadings++;
  
  if (validateReading(temp, humid)) {
    reading.temperature = temp;
    reading.humidity = humid;
    reading.isValid = true;
    
    // Update metrics
    metrics.lastSuccessfulRead = millis();
    metrics.avgReadTime = (metrics.avgReadTime + reading.readDuration) / 2;
    metrics.isOnline = true;
    
    // Check for spikes
    if (detectSpike(temp, humid)) {
      Serial.printf("-----!!!!-------- Spike detected: %.1f°C, %.1f%%\n", temp, humid);
    }
    
    lastValidReading = reading;
  } else {
    reading.isValid = false;
    metrics.failedReadings++;
    
    if (metrics.failedReadings > 10) {
      metrics.isOnline = false;
    }
  }
  
  return reading;
}

bool SensorModel::validateReading(float temp, float humidity) {
  if (isnan(temp) || isnan(humidity)) {
    return false;
  }
  
  // Basic range validation for DHT11/DHT22
  if (temp < -40 || temp > 80) return false;
  if (humidity < 0 || humidity > 100) return false;
  
  return true;
}

bool SensorModel::detectSpike(float temp, float humidity) {
  if (!lastValidReading.isValid) {
    return false;
  }
  
  float tempDiff = abs(temp - lastValidReading.temperature);
  float humidDiff = abs(humidity - lastValidReading.humidity);
  
  return (tempDiff > config.spikeThresholdTemp || 
          humidDiff > config.spikeThresholdHumidity);
}

bool SensorModel::isHealthy() const {
  unsigned long timeSinceLastRead = millis() - metrics.lastSuccessfulRead;
  float failureRate = (float)metrics.failedReadings / metrics.totalReadings;
  
  return (metrics.isOnline && 
          timeSinceLastRead < 30000 && 
          failureRate < 0.5);
}
