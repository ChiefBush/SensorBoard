// =============================================================================
// BufferLogic.h - Data Buffering and Caching Header
// =============================================================================
#ifndef BUFFER_LOGIC_H
#define BUFFER_LOGIC_H

#include <Arduino.h>
#include <vector>
#include "SensorModel.h"
#include "ConfigManager.h"

// Define maximum buffer size to prevent excessive memory usage
#define MAX_BUFFER_SIZE 50

struct BufferMetadata {
  unsigned long totalTransmissions;
  unsigned long successfulTransmissions;
  unsigned long failedTransmissions;
  unsigned long cacheHits;
  unsigned long cacheMisses;
  float successRate;
  int currentBufferSize;
  unsigned long oldestEntryAge;
};

class BufferLogic {
private:
  // Use static array instead of vector to avoid dynamic memory allocation
  static SensorReading buffer[MAX_BUFFER_SIZE];
  static int bufferIndex;
  BufferConfig config;
  BufferMetadata metadata;
  SensorReading cachedReading;
  int transmissionAge; // How many times current cached reading has been sent
  
  void cleanupOldEntries();
  void maintainBufferSize();

public:
  BufferLogic();
  
  void initialize(const BufferConfig& bufferConfig);
  void addReading(const SensorReading& reading);
  SensorReading getDataForTransmission();
  void recordTransmissionResult(bool success);
  
  BufferMetadata getMetadata() const { return metadata; }
  unsigned long getTotalTransmissions() const { return metadata.totalTransmissions; }
  int getBufferSize() const { return bufferIndex; }
  
  void flushBuffer();
  void printBufferStatus();
};

#endif
