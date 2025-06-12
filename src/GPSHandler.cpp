#include "GPSHandler.h"

GPSHandler::GPSHandler() 
    : gpsSerial(GPS_RX_PIN, GPS_TX_PIN),
      dataValid(false),
      lastReadTime(0),
      failureCount(0),
      latitude(0),
      longitude(0),
      satellites(0),
      hdop(99.99) {
}

void GPSHandler::begin() {
    gpsSerial.begin(GPS_BAUD);
    delay(1000); // Give GPS module time to initialize
}

void GPSHandler::update() {
    bool newData = false;
    unsigned long startTime = millis();
    
    // Read GPS for up to 2 seconds
    while (millis() - startTime < 2000) {
        if (gpsSerial.available()) {
            if (gps.encode(gpsSerial.read())) {
                newData = true;
            }
        }
        delay(10);
    }
    
    if (newData && gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        satellites = gps.satellites.value();
        hdop = gps.hdop.hdop();
        dataValid = true;
        lastReadTime = millis();
    } else {
        if (!dataValid) {
            failureCount++;
        }
    }
}

bool GPSHandler::hasValidLocation() {
    return dataValid && gps.location.isValid();
}

float GPSHandler::getLatitude() {
    return latitude;
}

float GPSHandler::getLongitude() {
    return longitude;
}

int GPSHandler::getSatellites() {
    return satellites;
}

float GPSHandler::getHDOP() {
    return hdop;
}

bool GPSHandler::isDataValid() {
    return dataValid;
}

unsigned long GPSHandler::getLastReadTime() {
    return lastReadTime;
}

int GPSHandler::getFailureCount() {
    return failureCount;
} 