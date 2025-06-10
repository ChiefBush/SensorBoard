// =============================================================================

// BufferLogic.cpp - Data Buffering and Caching Implementation
// =============================================================================
#include "../include/BufferLogic.h"

// Initialize static members
SensorReading BufferLogic::buffer[MAX_BUFFER_SIZE];
int BufferLogic::bufferIndex = 0;

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
  bufferIndex = 0; // Reset buffer index
  
  Serial.printf("-> Buffer initialized: max_size=%d, cache_duration=%lums\n", 
                MAX_BUFFER_SIZE, config.cacheDuration);
}

void BufferLogic::addReading(const SensorReading& reading) {
  if (reading.isValid) {
    if (bufferIndex < MAX_BUFFER_SIZE) {
      buffer[bufferIndex++] = reading;
      cachedReading = reading;
      transmissionAge = 0;
      
      maintainBufferSize();
      cleanupOldEntries();
      
      metadata.currentBufferSize = bufferIndex;
    } else {
      Serial.println("Buffer full, dropping oldest reading");
      // Shift buffer and add new reading
      for (int i = 0; i < MAX_BUFFER_SIZE - 1; i++) {
        buffer[i] = buffer[i + 1];
      }
      buffer[MAX_BUFFER_SIZE - 1] = reading;
      cachedReading = reading;
      transmissionAge = 0;
    }
  }
}

SensorReading BufferLogic::getDataForTransmission() {
  SensorReading dataToSend = cachedReading;
  
  if (dataToSend.isValid) {
    transmissionAge++;
    
    if (transmissionAge == 1) {
      metadata.cacheMisses++;
    } else {
      metadata.cacheHits++;
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
  int newIndex = 0;
  
  for (int i = 0; i < bufferIndex; i++) {
    if ((currentTime - buffer[i].timestamp * 1000) <= config.cacheDuration) {
      if (newIndex != i) {
        buffer[newIndex] = buffer[i];
      }
      newIndex++;
    }
  }
  
  bufferIndex = newIndex;
}

void BufferLogic::maintainBufferSize() {
  if (bufferIndex > config.maxSize) {
    int excess = bufferIndex - config.maxSize;
    for (int i = 0; i < bufferIndex - excess; i++) {
      buffer[i] = buffer[i + excess];
    }
    bufferIndex -= excess;
  }
}

void BufferLogic::flushBuffer() {
  bufferIndex = 0;
  metadata.currentBufferSize = 0;
}

void BufferLogic::printBufferStatus() {
  Serial.printf("Buffer Status: %d/%d entries\n", bufferIndex, MAX_BUFFER_SIZE);
  Serial.printf("Cache hits: %lu, misses: %lu\n", metadata.cacheHits, metadata.cacheMisses);
  Serial.printf("Success rate: %.1f%%\n", metadata.successRate);
}
