#include "../include/TransmitHandler.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

TransmitHandler::TransmitHandler() : transmissionCount(0), lastTransmissionTime(0) {
    // Initialize transmission handler
}

void TransmitHandler::initialize(const NetworkConfig& config) {
    this->config = config;
}

bool TransmitHandler::sendData(const String& payload) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("!-! WiFi not connected!");
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    
    String url = config.serverUrl + "/data";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED);
    
    if (success) {
        transmissionCount++;
        lastTransmissionTime = millis();
        Serial.printf("📤 Transmission #%lu: SUCCESS\n", transmissionCount);
    } else {
        Serial.print("!-! HTTP request failed: ");
        Serial.println(httpCode);
    }
    
    http.end();
    return success;
}

unsigned long TransmitHandler::getTransmissionCount() const {
    return transmissionCount;
}

unsigned long TransmitHandler::getLastTransmissionTime() const {
    return lastTransmissionTime;
} 