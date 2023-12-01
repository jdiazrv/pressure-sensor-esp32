
# Arduino IoT Pressure Monitor for MAB WaterMaker

## Project Overview
This repository contains the source code for an Arduino-based IoT project designed for monitoring and reporting pressure readings in a MAB WaterMaker system. It uses an ESP8266 module for WiFi connectivity and an Adafruit ADS1115 ADC for precise pressure measurements. The data is displayed on an OLED screen and transmitted over the network using UDP.

## Features
- Pressure measurement with Adafruit ADS1115 ADC.
- Real-time display of pressure on an OLED screen.
- WiFi connectivity with auto-reconnect functionality.
- Web server for configuration and real-time data display.
- EEPROM storage for configuration settings.
- UDP communication for data transmission.

## Hardware Requirements
- ESP8266 WiFi module.
- Adafruit ADS1115 ADC.
- OLED display (128x64 pixels).
- Pressure sensors.
- General electronic components (resistors, capacitors, wires, breadboard, etc.).

## Software Dependencies
- Arduino IDE.
- ESP8266 Board Package.
- Adafruit ADS1x15 Library.
- Adafruit GFX Library.
- Adafruit SSD1306 Library.
- ESP8266WiFi Library.
- WiFiManager Library.
- ESP8266mDNS Library.
- EEPROM Library.
- LittleFS Library.

## Installation
1. Clone this repository to your local machine or download the source code.
2. Open the `.ino` file with the Arduino IDE.
3. Install all required libraries (listed in Software Dependencies).
4. Configure your WiFi credentials and other settings as needed.
5. Connect your Arduino to your computer and select the correct board and port in the Arduino IDE.
6. Upload the sketch to your Arduino.

## WiFi Connectivity and Network Behavior
- If the device cannot connect to a pre-configured WiFi network, it automatically creates an Access Point (AP) with the SSID `Windlass_AP`. Users can connect to this AP to select the working WiFi network.
- The code is designed to automatically search for a SIGNALK server, simplifying the process of integrating with boat instrumentation systems.
- Currently, the program requires a WiFi connection to function. In the future, there are plans to allow operation in either AP mode or connected to a WiFi network, providing greater flexibility in various environments.

## Web Interface for Pressure Monitoring
This project includes a web interface for real-time monitoring of pressure readings, tailored for MAB WaterMaker systems. The interface features two radial gauges, each displaying a different aspect of the watermaker's operation:

### Low Pressure Gauge
- **Range**: 0 to 4 bar
- **Color-Coded Highlights**:
  - **Orange Zone (0-1 bar)**: Low-pressure warning.
  - **Green Zone (1-3 bar)**: Normal operating pressure range.
  - **Red Zone (3-4 bar)**: High-pressure warning.

### High Pressure Gauge
- **Range**: 0 to 70 bar
- **Color-Coded Highlights**:
  - **Orange Zone (0-50 bar)**: Preparatory pressure range.
  - **Green Zone (50-57 bar)**: Normal operating pressure range.
  - **Orange Zone (57-60 bar)**: Transition or caution area.
  - **Red Zone (60-70 bar)**: High-pressure warning.

These gauges are designed to provide clear and intuitive feedback on the system's status, with green zones specifically marking the ideal operating pressures. Users can conveniently monitor the watermaker's performance and ensure it operates within safe and efficient pressure ranges.

## Contributing
Contributions to this project are welcome. Please fork the repository and submit a pull request with your changes.

## License
[MIT License](LICENSE)

## Contact
- Your Name
- Your Contact Information (optional)
