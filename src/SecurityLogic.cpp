// =============================================================================
// SecurityLogic.cpp - Security and Authentication Implementation
// =============================================================================
#include "../include/SecurityLogic.h"
#include <Crypto.h>
#include <SHA256.h>
#include <AES.h>
#include <CBC.h>
#include <ArduinoJson.h>

SecurityLogic::SecurityLogic() : encryptionEnabled(false) {}

bool SecurityLogic::initialize(const String& key) {
  if (key.length() < 16) {
    Serial.println("!-! Security key too short!");
    return false;
  }
  
  // Initialize random number generator for crypto operations
  randomSeed(analogRead(0));
  
  secretKey = key;
  encryptionEnabled = true;
  
  // Test crypto operations
  String testData = "test";
  String encrypted = encryptData(testData);
  String decrypted = decryptData(encrypted);
  
  if (testData != decrypted) {
    Serial.println("!-! Crypto test failed!");
    encryptionEnabled = false;
    return false;
  }
  
  Serial.println("-> Security system initialized");
  return true;
}

String SecurityLogic::signPayload(const String& payload) {
  if (!encryptionEnabled) {
    Serial.println("!-! Security system not initialized!");
    return payload;
  }
  
  // Generate HMAC-SHA256 signature
  String signature = generateHMAC(payload);
  
  // Create signed payload
  DynamicJsonDocument doc(1024);
  doc["payload"] = payload;
  doc["signature"] = signature;
  doc["timestamp"] = generateTimestamp();
  
  String output;
  serializeJson(doc, output);
  return output;
}

bool SecurityLogic::validateSignature(const String& payload, const String& signature) {
  if (!encryptionEnabled) {
    Serial.println("!-! Security system not initialized!");
    return false;
  }
  
  String expectedSignature = generateHMAC(payload);
  return signature == expectedSignature;
}

String SecurityLogic::encryptData(const String& data) {
  if (!encryptionEnabled) {
    Serial.println("!-! Security system not initialized!");
    return data;
  }
  
  // Convert secret key to byte array
  byte key[32];
  for (int i = 0; i < 32; i++) {
    key[i] = secretKey[i % secretKey.length()];
  }
  
  // Generate IV
  byte iv[16];
  for (int i = 0; i < 16; i++) {
    iv[i] = random(256);
  }
  
  // Initialize AES
  AES128 aes128;
  CBC<AES128> cbc(aes128);
  
  // Pad data to 16-byte blocks
  int dataLen = data.length();
  int paddedLen = ((dataLen + 15) / 16) * 16;
  byte paddedData[paddedLen];
  memset(paddedData, 0, paddedLen);
  memcpy(paddedData, data.c_str(), dataLen);
  
  // Encrypt
  byte encrypted[paddedLen];
  cbc.setKey(key, 32);
  cbc.setIV(iv, 16);
  cbc.encrypt(encrypted, paddedData, paddedLen);
  
  // Combine IV and encrypted data
  String result;
  result.reserve(32 + paddedLen * 2); // Base64 encoding increases size by ~33%
  
  // Add IV
  for (int i = 0; i < 16; i++) {
    char hex[3];
    sprintf(hex, "%02x", iv[i]);
    result += hex;
  }
  
  // Add encrypted data
  for (int i = 0; i < paddedLen; i++) {
    char hex[3];
    sprintf(hex, "%02x", encrypted[i]);
    result += hex;
  }
  
  return result;
}

String SecurityLogic::decryptData(const String& encryptedData) {
  if (!encryptionEnabled) {
    Serial.println("!-! Security system not initialized!");
    return encryptedData;
  }
  
  // Extract IV and encrypted data
  if (encryptedData.length() < 32) {
    Serial.println("!-! Invalid encrypted data format!");
    return "";
  }
  
  byte iv[16];
  for (int i = 0; i < 16; i++) {
    char hex[3] = {encryptedData[i*2], encryptedData[i*2+1], 0};
    iv[i] = strtol(hex, NULL, 16);
  }
  
  int encryptedLen = (encryptedData.length() - 32) / 2;
  byte encrypted[encryptedLen];
  for (int i = 0; i < encryptedLen; i++) {
    char hex[3] = {encryptedData[32+i*2], encryptedData[32+i*2+1], 0};
    encrypted[i] = strtol(hex, NULL, 16);
  }
  
  // Convert secret key to byte array
  byte key[32];
  for (int i = 0; i < 32; i++) {
    key[i] = secretKey[i % secretKey.length()];
  }
  
  // Initialize AES
  AES128 aes128;
  CBC<AES128> cbc(aes128);
  
  // Decrypt
  byte decrypted[encryptedLen];
  cbc.setKey(key, 32);
  cbc.setIV(iv, 16);
  cbc.decrypt(decrypted, encrypted, encryptedLen);
  
  // Convert to string and remove padding
  String result;
  result.reserve(encryptedLen);
  for (int i = 0; i < encryptedLen; i++) {
    if (decrypted[i] != 0) {
      result += (char)decrypted[i];
    }
  }
  
  return result;
}

String SecurityLogic::generateHMAC(const String& data) {
  SHA256 sha256;
  byte hmacResult[32];
  
  // Convert secret key to byte array
  byte key[32];
  for (int i = 0; i < 32; i++) {
    key[i] = secretKey[i % secretKey.length()];
  }
  
  // Calculate HMAC
  sha256.resetHMAC(key, 32);
  sha256.update(data.c_str(), data.length());
  sha256.finalizeHMAC(key, 32, hmacResult, 32);
  
  // Convert to hex string
  String result;
  result.reserve(64);
  for (int i = 0; i < 32; i++) {
    char hex[3];
    sprintf(hex, "%02x", hmacResult[i]);
    result += hex;
  }
  
  return result;
}

String SecurityLogic::generateTimestamp() {
  unsigned long currentTime = millis();
  char timestamp[21];
  sprintf(timestamp, "%lu", currentTime);
  return String(timestamp);
} 