// =============================================================================
// SecurityLogic.h - Security and Authentication Header
// =============================================================================
#ifndef SECURITY_LOGIC_H
#define SECURITY_LOGIC_H

#include <Arduino.h>

class SecurityLogic {
private:
    String deviceId;

public:
    SecurityLogic();
    void initialize(const String& deviceId);
    String signPayload(const String& payload);
};

#endif
