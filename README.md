
# Arduino IoT Pressure Monitor for MAB WaterMaker

**Table of Contents**
1. [Project Overview](#project-overview)
2. [Features](#features)
3. [Hardware Requirements](#hardware-requirements)
4. [Software](#software)
   - [Software Dependencies](#software-dependencies)
   - [Installation](#installation)
   - [WiFi Connectivity and Network Behavior](#wifi-connectivity-and-network-behavior)
   - [Web Interface for Pressure Monitoring](#web-interface-for-pressure-monitoring)
5. [Hardware Description](#hardware-description)
   - [Hardware Configuration](#hardware-configuration)
   - [PCB Design and Ordering](#pcb-design-and-ordering)
   - [Circuit Imagery](#circuit-imagery)
6. [Contributing](#contributing)
7. [License](#license)
8. [Contact](#contact)


# Arduino IoT Pressure Monitor for MAB WaterMaker

## Project Overview

This repository contains the source code for an Arduino-based IoT project designed for monitoring and reporting pressure readings in a MAB WaterMaker system. It features an ESP8266 module for versatile WiFi connectivity, allowing the device to operate in both Station and Access Point (AP) modes. The project is tailored for seamless integration with boat instrumentation systems, including automatic detection of SignalK servers when connected to a network in Station Mode. Data is displayed on an OLED screen and transmitted over the network using UDP, ensuring real-time monitoring and accessibility in various operational environments.

For more information about the MAB WaterMaker system, please visit [watermaker.se](https://watermaker.se).

## Features
- Pressure measurement with Adafruit ADS1115 ADC.
- Real-time display of pressure on an OLED screen.
- Flexible WiFi connectivity:
  - **Station Mode**: Connects to an existing WiFi network and automatically searches for a SignalK server.
  - **AP Mode**: Creates its own network for situations where no WiFi is available. Configurable AP password for secure access.
  - **Auto-fallback to AP Mode**: If the device cannot connect to a pre-configured network, it temporarily creates a network `watermaker_config` for direct access and configuration.
- Web server for configuration and real-time data display.
- EEPROM storage for configuration settings.
- UDP communication for data transmission.
- Automatic SignalK server detection in Station Mode, enhancing its integration with marine systems.


## Hardware Requirements
- ESP8266 WiFi module.
- Adafruit ADS1115 ADC.
- OLED display (128x64 pixels).
- Pressure sensors.
- General electronic components (resistors, capacitors, wires, breadboard, etc.).


#Software 
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

## Installation Steps

1. **Clone or Download the Repository**
   - Clone this repository to your local machine or download the source code.

2. **Open the Project in Visual Studio Code**
   - Install [Visual Studio Code (VS Code)](https://code.visualstudio.com/) if you haven't already.
   - Install the PlatformIO IDE extension for VS Code. This extension provides an integrated development environment for IoT development.
   - Open VS Code and navigate to the cloned or downloaded project directory.

3. **Install Required Libraries**
   - The PlatformIO IDE in VS Code automatically handles library dependencies as specified in the `platformio.ini` file. Ensure that all required libraries (listed in Software Dependencies) are included in this file.

4. **Configure WiFi Credentials and Other Settings**
   - Locate the configuration file or section within the `main.cpp` file.
   - Update the WiFi credentials and other settings according to your requirements.

5. **Connect Your Device**
   - Connect your ESP8266 or Arduino board to your computer using a USB cable.
   - VS Code with PlatformIO IDE will automatically detect the connected board. If not, you can manually select the correct board and port from the PlatformIO toolbar.

6. **Compile and Upload**
   - Use the PlatformIO IDE toolbar within VS Code to compile and upload the sketch to your board.
   - Monitor the output console in VS Code for any errors and to confirm a successful upload.

7. **Upload Additional Files**
   - After uploading the code, you must upload additional files such as `index.html` and `gauge.min.js` (JavaScript) to the ESP8266's Filesystem. This is necessary for your project.
   - To upload these files, click on the "Upload Filesystem Image" option in PlatformIO. **Before doing this, make sure to close any PlatformIO terminals to avoid port-related errors.**


### WiFi Connectivity and Network Behavior
- The device offers two WiFi modes: Station Mode and Access Point (AP) Mode.
- **Station Mode**: 
  - In this mode, the device connects to an existing WiFi network.
  - If the device cannot connect to the pre-configured network (e.g., network unavailable or incorrect credentials), it automatically creates a temporary Access Point named `watermaker_config`. This allows the user to connect directly to the device and select or reconfigure the WiFi network.
  - Access the device at `watermaker.local` or through the IP address assigned by the router.
  - In Station Mode, the software automatically searches for a SignalK server, simplifying integration with boat instrumentation systems.
- **AP Mode**: 
  - The device creates its own WiFi network named `watermaker`. This mode is useful when no existing WiFi network is available.
  - Users can configure the AP's password for secure access.
  - Access the device at `watermaker.local` or via the default IP `192.168.4.1`.
- **Mode Selection and Configuration**: 
  - The operational mode can be selected and configured through the web interface.
- **Reset Feature**: 
  - If AP mode is selected but the password is forgotten, you can reset the device using the ESP8266's physical reset button. This will reset the AP password to '12345678'.

Overall, the device is designed to be versatile, capable of operating connected to an existing WiFi network or independently in AP mode. Its adaptive network behavior ensures continuous accessibility and functionality in various operational scenarios.




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

- ![PCB Layout][def3]

These gauges are designed to provide clear and intuitive feedback on the system's status, with green zones specifically marking the ideal operating pressures. Users can conveniently monitor the watermaker's performance and ensure it operates within safe and efficient pressure ranges. 
Data is received in the SignalkServer, consequently it can be display with KIP or with third party sapps sucha as WillHelm SK.



# Hardware Description

## Hardware Configuration

The hardware for this pressure monitoring system is specifically designed with surface-mount device (SMD) components, selected for their precision and compact form factor. The following components are utilized in the circuit:

- **D1 Mini (ESP8266)**: The core microcontroller board that manages data processing and WiFi communication.
- **ADS1115 (Adafruit #1085)**: A high-precision 16-bit analog-to-digital converter, critical for converting analog pressure sensor signals into digital data.
- **DC-DC Converter (Adjustable to 5V)**: Supplies a regulated 5V power to the ESP8266 and ADS1115 to ensure stable operation.
- **SMD Resistors**: 
  - **R1, R2 (220Ω SMD Size 1206)**: Serve as pull-down resistors to establish a known state when the input is floating.
  - **R3, R4, R5 (100kΩ SMD Size 1206)**: Also configured as pull-down resistors to provide a default low signal when no input is detected.
- **SMD LEDs**: 
  - **L1 (Red SMD Size 1206)**: Acts as a status indicator, potentially signaling power or error conditions.
  - **L2 (Green SMD Size 1206)**: Indicates normal operation or successful processes.

The SMD resistors are not mere suggestions; the PCB layout is specifically designed for their use, enabling a cleaner design and facilitating automated assembly processes.

## PCB Design and Ordering

The custom PCB is tailored to fit the SMD components, ensuring an efficient and space-saving layout. For replication or utilization of this design, the PCB can be ordered directly through the provided link:

[Order PCB from Aisler](https://aisler.net/p/OSXMDHTM)

Aisler offers quality PCB manufacturing services, ensuring that the PCBs are produced to match the precise specifications of the design files.

## Circuit Imagery

For assembly and verification, high-resolution images of the circuit design are made available:


- ![Circuit Overview][def]
- ![PCB Layout][def2]

Please replace `url-to-circuit-overview` and `url-to-pcb-layout` with the actual links to the images where they are hosted.

## Contributing
Contributions to this project are welcome. Please fork the repository and submit a pull request with your changes.

## License
This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.

## Contact
- Juan Diaz   



[def]: jdiazrv/8266-pressure-sensor/images/Schematics.png
[def2]: jdiazrv/8266-pressure-sensor/images/pressure_pcb.png
[def3]: jdiazrv/8266-pressure-sensor/images/gauges.jpeg