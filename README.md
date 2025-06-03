# ESP8266 DHT Logger

## Overview

This project is a modular, configurable temperature and humidity logger using the ESP8266MOD (NodeMCU v3) and a DHT11 sensor. It transmits sensor data over Wi-Fi to a remote PHP-based server endpoint using HTTP POST requests. The system follows the MVC2 architectural pattern and includes configuration management, buffer logic, secure payload signing, and real-time performance monitoring.

## Features

- Modular architecture with separation of concerns
- JSON-based configuration files stored in SPIFFS
- NTP-synchronized timestamp generation in ISO 8601 format
- Smart buffering and caching system with retry logic
- Configurable sensor read intervals and transmission frequencies
- Real-time status and diagnostic logs via serial monitor
- Payload authentication using HMAC-SHA256
- Network resilience with auto-reconnection
- Configurable buffer limits and performance metrics
- Secure transmission via HTTPS


## Configuration Files

### `BootConfig.json`

Contains static configuration used at startup.

```json
{
  "system": {
    "device_id": "ESP8266_DHT_001",
    "firmware_version": "2.0.0",
    "debug_level": 2
  },
  "wifi": {
    "ssid": "YourSSID",
    "password": "YourPassword"
  },
  "sensor": {
    "type": "DHT11",
    "pin": 2,
    "read_interval": 3000
  },
  "location": {
    "latitude": 28.637270,
    "longitude": 77.170277,
    "timezone_offset": 19800
  }
}
```
config.json
Contains runtime-configurable parameters.
```json
{
  "transmission": {
    "interval": 5000,
    "server_url": "https://yourdomain.com/controller.php",
    "timeout": 15000,
    "retry_attempts": 3
  },
  "buffer": {
    "max_size": 50,
    "cache_duration": 300000
  },
  "security": {
    "secret_key": "your-secret-key",
    "enable_encryption": false,
    "signature_algorithm": "HMAC-SHA256"
  }
}
```

## How It Works
- DHT11 sensor collects temperature and humidity data.
- Timestamp is synchronized via NTP (UTC +5:30 IST offset).
- Data is pushed to a buffer with time-based aging and size constraints.
- Valid sensor packets are converted to signed JSON and sent to a PHP server.
- Failed transmissions are retried based on configuration.
- Buffer management handles overflows and cache expiry.

### Setup Requirements
- Arduino IDE with ESP8266 board support
### Required Libraries:
- ESP8266WiFi
- ESP8266HTTPClient
- WiFiClientSecure
- ArduinoJson
- DHT
- NTPClient
- SPIFFS

- An HTTPS-capable PHP endpoint to receive POST requests

# Notes
- Sensor reads every read_interval ms; transmission every transmission.interval ms.
- Cached readings are retried with exponential backoff and aging-based eviction.
- JSON payload includes metrics like heap size, RSSI, and uptime.

# License
This project is intended for academic and prototyping use. Commercial usage requires explicit permission.

