// =============================================================================
// TransmitHandler.h - Network Transmission Header
// =============================================================================
#ifndef TRANSMIT_HANDLER_H
#define TRANSMIT_HANDLER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "ConfigManager.h"

class TransmitHandler {
private:
    NetworkConfig config;
    unsigned long transmissionCount;
    unsigned long lastTransmissionTime;

public:
    TransmitHandler();
    void initialize(const NetworkConfig& config);
    bool sendData(const String& payload);
    unsigned long getTransmissionCount() const;
    unsigned long getLastTransmissionTime() const;
};

#endif
