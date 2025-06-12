#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// GPS configuration
#define GPS_RX_PIN 4     // D2 (GPIO4) -> GPS TX
#define GPS_TX_PIN 5     // D1 (GPIO5) -> GPS RX
#define GPS_BAUD 9600

class GPSHandler {
public:
    GPSHandler();
    void begin();
    void update();
    bool hasValidLocation();
    float getLatitude();
    float getLongitude();
    int getSatellites();
    float getHDOP();
    bool isDataValid();
    unsigned long getLastReadTime();
    int getFailureCount();

private:
    TinyGPSPlus gps;
    SoftwareSerial gpsSerial;
    bool dataValid;
    unsigned long lastReadTime;
    int failureCount;
    float latitude;
    float longitude;
    int satellites;
    float hdop;
};

#endif // GPS_HANDLER_H 