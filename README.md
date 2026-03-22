# ESP32 Pressure Sensor for MAB WaterMaker

ESP32-based dual pressure monitor for a MAB WaterMaker system.
It reads two 0.5-4.5 V pressure transducers through an ADS1115, shows the values on a local dashboard and OLED, and publishes the data to SignalK over UDP.

This project is the ESP32 evolution of the original `8266-pressure-sensor`, adapted for the same hardware concept and marine workflow with a more complete web UI, better diagnostics, demo modes, OTA updates, and more robust SignalK discovery.

## Features

- Dual pressure acquisition with ADS1115
- OLED local display
- Responsive dashboard served from LittleFS
- SignalK UDP output with automatic discovery
- Manual SignalK IP override from the UI
- Configurable and persistent UDP port
- Real, Demo, and Demo + UDP modes
- Device diagnostics page
- SignalK monitor page with live scrolling log
- Firmware OTA update from the browser
- Filesystem OTA update from the browser
- AP mode or client mode with WiFiManager fallback
- Detection of:
  - ADS1115 missing
  - no signal from sensor
  - disconnected sensor
  - stale sensor data

## Hardware

- ESP32 dev board
- ADS1115 ADC
- 2 pressure sensors with 0.5-4.5 V output
- Small I2C OLED
- 5 V regulated supply

The original hardware concept and documentation come from the earlier 8266 project and are still relevant here.

## Pressure Channels

- Sensor 1: low pressure / inlet pressure
- Sensor 2: high pressure / main pressure

Default calibration:

- Sensor 1: `0.5-4.5 V` => `0-15 bar`
- Sensor 2: `0.5-4.5 V` => `0-80 bar`

Dashboard visual ranges:

- Low pressure gauge: `0-4 bar`
- High pressure gauge: `0-70 bar`

## WiFi Modes

`Client`

- Connects to an existing WiFi network
- Tries to discover a SignalK server with mDNS
- Publishes UDP to the configured or discovered target

`AP only`

- Creates a local access point for direct access
- Useful for standalone setup and service work

## Sensor Source Modes

`Real`

- Reads ADS1115 hardware
- Sends UDP only when ADS1115 is detected

`Demo`

- Generates realistic simulated pressures
- Does not send UDP

`Demo + UDP`

- Generates simulated pressures
- Sends them through the same UDP pipeline for testing

## Web Interface

Main dashboard:

- Large low and high pressure cards
- RSSI indicator
- SignalK status pill
- Clickable status details with firmware/UI version and current error

Settings:

- Calibration for both sensors
- WiFi mode
- sensor source mode
- AP password
- SignalK retry count
- SignalK IP
- UDP port

Maintenance:

- firmware update
- filesystem update
- tools
- factory reset

Tools:

- device diagnostics
- SignalK monitor

## OTA Updates

The device supports browser-based OTA:

- `/update` for firmware
- `/updatefs` for LittleFS filesystem image

It also exposes ArduinoOTA for LAN updates.

## SignalK Output

The device publishes:

- `environment.watermaker.pressure.low`
- `environment.watermaker.pressure.high`

Current mapping:

- low pressure comes from Sensor 1
- high pressure comes from Sensor 2

## Filesystem Content

The web UI is stored in `data/` and uploaded to LittleFS.

Important files:

- `data/index.html`
- `data/gauge.min.js`
- `data/manifest.webmanifest`
- `data/icon.svg`

## Build And Upload

This project uses PlatformIO.

### Build

```bash
platformio run -e esp32dev
```

### Upload Firmware

```bash
platformio run -e esp32dev -t upload
```

### Upload Filesystem

```bash
platformio run -e esp32dev -t uploadfs
```

### Serial Monitor

```bash
platformio device monitor -b 115200
```

## Project Structure

- [`src/main.cpp`](src/main.cpp): firmware logic
- [`src/config_html.h`](src/config_html.h): settings page HTML
- [`data/index.html`](data/index.html): dashboard UI
- [`platformio.ini`](platformio.ini): PlatformIO environment
- [`res/gauges.jpeg`](res/gauges.jpeg): legacy gauge reference
- [`res/Schematics.png`](res/Schematics.png): hardware reference
- [`res/pressure_pcb.png`](res/pressure_pcb.png): PCB reference
- [`res/pressure_bom.html`](res/pressure_bom.html): BOM reference

## Legacy Reference

This repository derives from the original 8266 project:

- https://github.com/jdiazrv/8266-pressure-sensor

Relevant hardware concepts, gauge ranges, PCB references, and documentation were carried forward and adapted for the ESP32 implementation.

## License

See [LICENSE.md](LICENSE.md).
