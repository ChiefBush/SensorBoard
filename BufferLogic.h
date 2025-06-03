// =============================================================================
// BufferLogic.h - Data Buffering and Caching Header
// =============================================================================
#ifndef BUFFER_LOGIC_H
#define BUFFER_LOGIC_H

#include <Arduino.h>
#include <vector>
#include "SensorModel.h"
#include "ConfigManager.h"

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
  std::vector<SensorReading> buffer;
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
  int getBufferSize() const { return buffer.size(); }
  
  void flushBuffer();
  void printBufferStatus();
};

#endif
