// =============================================================================
// Project Structure Summary
// =============================================================================
/*
ESP8266_DHT_Logger/
├── MainController.ino          # Main Arduino sketch
├── data/
│   ├── BootConfig.json        # Boot-time configuration
│   └── config.json            # Runtime configuration
├── ConfigManager.h            # Configuration management
├── ConfigManager.cpp
├── SensorModel.h              # Sensor handling
├── SensorModel.cpp
├── BufferLogic.h              # Data buffering/caching
├── BufferLogic.cpp
├── TransmitHandler.h          # Network transmission
├── TransmitHandler.cpp
├── SecurityLogic.h            # Security & authentication
├── SecurityLogic.cpp
├── JSONView.h                 # JSON formatting & display
├── JSONView.cpp
└── README.md                  # Documentation
