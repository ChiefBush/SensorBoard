// =============================================================================
// JSONView.h - JSON Formatting and Display Header
// =============================================================================
#ifndef JSON_VIEW_H
#define JSON_VIEW_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "SensorModel.h"
#include "ConfigManager.h"
#include "BufferLogic.h"
#include "TransmitHandler.h"

class JSONView {
public:
  String createTransmissionPayload(
    const SensorReading& reading,
    const ConfigManager& config,
    const BufferMetadata& bufferMeta,
    unsigned long currentTime
  );
  
  void printSystemStatus(
    const ConfigManager& config,
    const SensorModel& sensor,
    const BufferLogic& buffer
  );
  
  void printPerformanceMetrics(
    const BufferLogic& buffer,
    const TransmitHandler& transmitter
  );
  
  String formatSensorReading(const SensorReading& reading);
  String createStatusReport();
};

#endif
