// =============================================================================
// SecurityLogic.h - Security and Authentication Header
// =============================================================================
#ifndef SECURITY_LOGIC_H
#define SECURITY_LOGIC_H

#include <Arduino.h>

class SecurityLogic {
private:
  String secretKey;
  bool encryptionEnabled;
  
  String generateHMAC(const String& data);
  String generateTimestamp();

public:
  SecurityLogic();
  
  bool initialize(const String& key);
  String signPayload(const String& payload);
  bool validateSignature(const String& payload, const String& signature);
  String encryptData(const String& data);
  String decryptData(const String& encryptedData);
};

#endif
