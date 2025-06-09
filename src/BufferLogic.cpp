// =============================================================================
// BufferLogic.cpp - Data Buffering and Caching Implementation
// =============================================================================
#include "BufferLogic.h"

BufferLogic::BufferLogic() : transmissionAge(0) {
  metadata.totalTransmissions = 0;
  metadata.successfulTransmissions = 0;
  metadata.failedTransmissions = 0;
  metadata.cacheHits = 0;
  metadata.cacheMisses = 0;
  metadata.successRate = 0.0;
  metadata.currentBufferSize = 0;
  metadata.oldestEntryAge = 0;
}

void BufferLogic::initialize(const BufferConfig& bufferConfig) {
  config = bufferConfig;
  buffer.reserve(config.maxSize);
  
  Serial.printf("-> Buffer initialized: max_size=%d, cache_duration=%lums\n", 
                config.maxSize, config.cacheDuration);
}

void BufferLogic::addReading(const SensorReading& reading) {
  if (reading.isValid) {
    buffer.push_back(reading);
    cachedReading = reading;
    transmissionAge = 0; // Reset age counter for new reading
    
    maintainBufferSize();
    cleanupOldEntries();
    
    metadata.currentBufferSize = buffer.size();
  }
}

SensorReading BufferLogic::getDataForTransmission() {
  SensorReading dataToSend = cachedReading;
  
  if (dataToSend.isValid) {
    transmissionAge++;
    
    if (transmissionAge == 1) {
      metadata.cacheMisses++; // Fresh data
    } else {
      metadata.cacheHits++; // Cached data
    }
  }
  
  return dataToSend;
}

void BufferLogic::recordTransmissionResult(bool success) {
  metadata.totalTransmissions++;
  
  if (success) {
    metadata.successfulTransmissions++;
  } else {
    metadata.failedTransmissions++;
  }
  
  metadata.successRate = (float)metadata.successfulTransmissions / metadata.totalTransmissions * 100.0;
}

void BufferLogic::cleanupOldEntries() {
  unsigned long currentTime = millis();
  
  buffer.erase(
    std::remove_if(buffer.begin(), buffer.end(),
      [this, currentTime](const SensorReading& reading) {
        return (currentTime - reading.timestamp * 1000) > config.cacheDuration;
      }),
    buffer.end()
  );
}

void BufferLogic::maintainBufferSize() {
  if (buffer.size() > config.maxSize) {
    int excess = buffer.size() - config.maxSize;
    buffer.erase(buffer.begin(), buffer.begin() + excess);
  }
}
