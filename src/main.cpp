

// Include necessary libraries
#include <Wire.h>
#include <Adafruit_ADS1x15.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <ESP8266MDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "config_html.h" // Script for the configuration page
#include <LittleFS.h>
#include <ArduinoOTA.h> // to update OVER THE AIR (OTA)
#include "ESP8266_Utils_OTA.hpp"

// Default values for voltage and pressure, used if EEPROM values are empty or not valid
#define MIN_VOLTAGE1 0.5
#define MAX_VOLTAGE1 4.5
#define MIN_PRESSURE1 0
#define MAX_PRESSURE1 15
#define MIN_VOLTAGE2 0.5
#define MAX_VOLTAGE2 4.5
#define MIN_PRESSURE2 0
#define MAX_PRESSURE2 15

// EEPROM Addresses for storing configuration settings
const int ADDRESSES[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36};

// OLED display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 0 // Reset pin not used in this configuration
#define WIFI_AP_STA 1
#define LED_PIN D5                // LED pin for Wi-Fi connection indication
unsigned long previousMillis = 0; // Stores the last time a UDP message was sent
const long interval = 3000;       // Interval in milliseconds for sending UDP messages

// Network and device configuration
const char *hostName = "watermaker"; // IMPORTANT hostName has to be char in order to work.
float minVoltage1 = 0.5;
float maxVoltage1 = 4.5;
float minPressure1 = 0;
float maxPressure1 = 15;
float pressure1;
float minVoltage2 = 0.5;
float maxVoltage2 = 4.5;
float minPressure2 = 0;
float maxPressure2 = 15;
float pressure2;
long unsigned int intervalForAP = 10000; // interval if wifi connection is not achieve to start AP
bool tryingToConnect = false;
unsigned long startTime;           // Tracks the start time of Wi-Fi connection attempts
bool isFirstBoot = true;           // Flag to check if it's the first boot
String ssid_sta = "enjoy";         // WLAN name for station connection
String password_sta = "EmmO174.."; // WLAN password for station connection
IPAddress ip1;                     // IP address for SignalK server
unsigned int outPort = 4210;       // UDP port for SignalK server communication

// Initialize ADS1115 for reading analog values
Adafruit_ADS1115 ads;

// Initialize OLED display
Adafruit_SSD1306 display(OLED_RESET);

// Text size and position constants for the OLED display
const int LargeTextSize = 2;
const int SmallTextSize = 1;
const int Pressure1YPosition = 0;
const int Pressure2YPosition = 30;
const int UnitXPosition = 54;
const int UnitYOffset = 7;

// Function to print text on the OLED display
void printText(int textSize, int x, int y, String text)
{
  display.setTextSize(textSize);
  display.setCursor(x, y);
  display.print(text);
}

// Function to draw the screen with pressure values
void drawScreen()
{
  display.clearDisplay();

  // Display pressure 1
  printText(LargeTextSize, 0, Pressure1YPosition, String(pressure1, 1));
  printText(SmallTextSize, UnitXPosition, Pressure1YPosition + UnitYOffset, "b");

  // Display pressure 2
  printText(LargeTextSize, 0, Pressure2YPosition, String(pressure2, 1));
  printText(SmallTextSize, UnitXPosition, Pressure2YPosition + UnitYOffset, "b");

  display.display();
}

// Error check for OLED display height
#if (SSD1306_LCDHEIGHT != 48)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Web server and network utilities initialization
ESP8266WebServer server(80); // Web Server on port 80 for HTTP requests
WiFiManager wifiManager;     // Handles Wi-Fi connection management
WiFiUDP Udp;                 // UDP communication object

// Event handler for the main page
void Event_Index()
{
  // Handle request for the main page ("/")

  File file = LittleFS.open("/index.html", "r");
  if (!file)
  {
    // If the file is not found, send an error message
    Serial.println("Error opening index.html");
    server.send(404, "text/plain", "File not found");

    // List all files in LittleFS (useful for debugging)
    Dir dir = LittleFS.openDir("/");
    Serial.println("Listing files in LittleFS:");
    while (dir.next())
    {
      File file = dir.openFile("r");
      Serial.print("File: ");
      Serial.print(dir.fileName());
      Serial.print(" Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
      file.close();
    }
    return;
  }
  server.streamFile(file, "text/html");
  // server.send(200, "text/html", indexHTML); // Alternative: Send index HTML directly
}

// Function to write configuration settings to EEPROM
void writeEEPROM()
{
  if (maxVoltage1 > 1 && maxVoltage1 < 5 && maxPressure1 > 0.1 && maxPressure1 < 150)
  {
    // Write values to EEPROM only if they are within valid ranges
    EEPROM.put(ADDRESSES[0], minVoltage1);
    EEPROM.put(ADDRESSES[1], maxVoltage1);
    EEPROM.put(ADDRESSES[2], minPressure1);
    EEPROM.put(ADDRESSES[3], maxPressure1);
    EEPROM.put(ADDRESSES[4], minVoltage2);
    EEPROM.put(ADDRESSES[5], maxVoltage2);
    EEPROM.put(ADDRESSES[6], minPressure2);
    EEPROM.put(ADDRESSES[7], maxPressure2);
    EEPROM.commit();
  }
}

// Function to read configuration settings from EEPROM
void readEEPROM()
{
  // Read values from EEPROM
  EEPROM.get(ADDRESSES[0], minVoltage1);
  EEPROM.get(ADDRESSES[1], maxVoltage1);
  EEPROM.get(ADDRESSES[2], minPressure1);
  EEPROM.get(ADDRESSES[3], maxPressure1);
  EEPROM.get(ADDRESSES[4], minVoltage2);
  EEPROM.get(ADDRESSES[5], maxVoltage2);
  EEPROM.get(ADDRESSES[6], minPressure2);
  EEPROM.get(ADDRESSES[7], maxPressure2);

  // Check if the read values are valid, if not, set them to default values
  if (isnan(maxVoltage1) || isnan(maxPressure1) || maxVoltage1 <= 0 || maxVoltage1 > 5 || maxPressure1 > 150 || maxPressure1 <= 0)
  {
    minVoltage1 = MIN_VOLTAGE1;
    maxVoltage1 = MAX_VOLTAGE1;
    minPressure1 = MIN_PRESSURE1;
    maxPressure1 = MAX_PRESSURE1;
    minVoltage2 = MIN_VOLTAGE2;
    maxVoltage2 = MAX_VOLTAGE2;
    minPressure2 = MIN_PRESSURE2;
    maxPressure2 = MAX_PRESSURE2;
    writeEEPROM();
  }
}

// Event handler for the pressure endpoint
void Event_pressure()
{
  // Send pressure values as a JSON response
  String jsonResponse = "{\"pressure1\": " + String(pressure1) + ", \"pressure2\": " + String(pressure2) + "}";
  server.send(200, "application/json", jsonResponse);
  Serial.println(jsonResponse);
}

// Event handler for serving JavaScript files
void Event_js()
{
  // Serve gauge.min.js file for the web interface

  File file = LittleFS.open("/gauge.min.js", "r");
  if (!file)
  {
    // If the file is not found, send an error message
    Serial.println("Error opening gauge.min.js");
    server.send(404, "text/plain", "File not found");

    // List all files in LittleFS (useful for debugging)
    Dir dir = LittleFS.openDir("/");
    Serial.println("Listing files in LittleFS:");
    while (dir.next())
    {
      File file = dir.openFile("r");
      Serial.print("File: ");
      Serial.print(dir.fileName());
      Serial.print(" Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
      file.close();
    }
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
  // server.send(200, "text/html", indexHTML); // Alternative: Send index HTML directly
}

// Event handler for saving configuration settings
void Event_Submit()
{
  // Handle configuration submission and save new settings

  server.send(200, "text/html", "Configuration saved");
  Serial.println("Configuration saved");
  // Print submitted values to the serial monitor (for debugging)
  Serial.println(server.arg("maxPressure1"));
  Serial.println(server.arg("minPressure1"));
  Serial.println(server.arg("minVdc1"));
  Serial.println(server.arg("maxVdc1"));
  Serial.println(server.arg("maxPressure2"));
  Serial.println(server.arg("minPressure2"));
  Serial.println(server.arg("minVdc2"));
  Serial.println(server.arg("maxVdc2"));
  // Update configuration settings with submitted
  maxPressure1 = server.arg("maxPressure1").toFloat();
  minPressure1 = server.arg("minPressure1").toFloat();
  minVoltage1 = server.arg("minVdc1").toFloat();
  maxVoltage1 = server.arg("maxVdc1").toFloat();
  maxPressure2 = server.arg("maxPressure2").toFloat();
  minPressure2 = server.arg("minPressure2").toFloat();
  minVoltage2 = server.arg("minVdc2").toFloat();
  maxVoltage2 = server.arg("maxVdc2").toFloat();
  writeEEPROM();
  drawScreen();
  delay(1000);
  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "");
}

void Event_Config()
{
  String htmlContent = configPageHTML;
  htmlContent.replace("{maxPressure1}", String(maxPressure1));
  htmlContent.replace("{minPressure1}", String(minPressure1));
  htmlContent.replace("{minVdc1}", String(minVoltage1));
  htmlContent.replace("{maxVdc1}", String(maxVoltage1));
  htmlContent.replace("{maxPressure2}", String(maxPressure2));
  htmlContent.replace("{minPressure2}", String(minPressure2));
  htmlContent.replace("{minVdc2}", String(minVoltage2));
  htmlContent.replace("{maxVdc2}", String(maxVoltage2));
  htmlContent.replace(',', '.');
  server.send(200, "text/html", htmlContent);
}
const int MaxDiscoveryAttempts = 5;
const int DiscoveryRetryDelay = 1000; // Delay time in milliseconds

IPAddress tryDiscoverSignalKService()
{
  for (int attempt = 1; attempt <= MaxDiscoveryAttempts; attempt++)
  {
    Serial.print("Attempt ");
    Serial.print(attempt);
    Serial.println(" to discover 'signalk-ws' services...");

    unsigned long startDiscoveryTime = millis();
    int n = MDNS.queryService("signalk-ws", "tcp");
    unsigned long endDiscoveryTime = millis();

    Serial.print("Time taken for service discovery: ");
    Serial.print(endDiscoveryTime - startDiscoveryTime);
    Serial.println(" ms");

    if (n > 0)
    {
      Serial.print("Found 'signalk-ws' service at: ");
      Serial.print(MDNS.IP(0));
      Serial.print(":");
      Serial.println(MDNS.port(0));
      return MDNS.IP(0);
    }

    Serial.println("No 'signalk-ws' services found!");
    if (attempt < MaxDiscoveryAttempts)
    {
      Serial.println("Retrying...");
      delay(DiscoveryRetryDelay);
    }
    else
    {
      Serial.println("Exhausted all attempts to find 'signalk-ws' services.");
    }
  }
  return IPAddress(); // Returns an empty IP if the service is not found
}

IPAddress discoverSignalKServices()
{
  Serial.println("Discovering SignalK services...");
  IPAddress serviceIP;
  if (isFirstBoot)
  {
    serviceIP = tryDiscoverSignalKService();
  }
  else
  {
    serviceIP = ip1; // Coming from reconnection after light sleep. ip1 already holds signalk IP
  }

  Serial.println(serviceIP);
  return serviceIP;
}

void sendPressureUpdate(IPAddress ip, int port, const char *path, float pressure)
{
  char udpMessage[1024];
  sprintf(udpMessage, "{\"updates\":[{\"$source\":\"ESP32.watermakerl\",\"values\":[{\"path\":\"%s\",\"value\":%f}]}]}", path, pressure);
  Udp.beginPacket(ip, port);
  Udp.write(reinterpret_cast<uint8_t *>(udpMessage), strlen(udpMessage));
  Udp.endPacket();
}

void handleConnected()
{
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Host Name: ");
  Serial.println(WiFi.getHostname());
  if (!MDNS.begin(hostName))
  {
    Serial.println("Error al iniciar mDNS");
    return;
  }

  Serial.println("mDNS iniciado");
  ssid_sta = WiFi.SSID();
  password_sta = WiFi.psk();
  drawScreen(); // we show the ip address on the screen
  ip1 = discoverSignalKServices();

  // Udp.begin(WiFi.localIP(), outPort);

  Serial.printf("Now listening at IP %s, UDP port %d\n", WiFi.localIP().toString().c_str(), outPort);
  sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.high", pressure1);
  sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.low", pressure2);
  drawScreen(); // we show the signalk server address on the screen
}

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(512);
  ads.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);
  Serial.println("Start");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  drawScreen();
  readEEPROM();
  Serial.print("Reading EEPROM for the first time...");
  Serial.println(maxPressure1);
  WiFi.hostname(hostName); // Set device hostname
  tryingToConnect = true;
  String chkPassword = WiFi.psk();
  if (chkPassword.length() == 0)
  {
    wifiManager.autoConnect("watermaker_AP", "12345678"); // password protected ap
  }
  else
  {
    WiFi.begin();
  }
  if (!LittleFS.begin())
  {
    Serial.println("Error mounting LittleFS");
    return;
  }
  else
  {
    Serial.println("LittleFS mounted successfully");
  }
  server.on("/", Event_Index);
  server.on("/gauge.min.js", Event_js); // when index_html calls for gauge.min.js we need to serve it.
  server.on("/config", HTTP_GET, Event_Config);
  server.on("/submit", HTTP_POST, Event_Submit);
  server.on("/pressure", HTTP_GET, []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Event_pressure(); });
  server.begin();
  Serial.println("HTTP Server started");
}

float adcToVoltage(int16_t adcValue)
{
  return static_cast<float>(adcValue) * 0.1875 / 1000.0;
}
float mapToPressure(float voltage, float minVoltage, float maxVoltage, float minPressure, float maxPressure)
{
  float pressure = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * (maxPressure - minPressure);
  return (pressure < 0) ? 0 : pressure;
}

void loop()
{
  ArduinoOTA.handle();                       // por si actualizamos el codigo Over The Air
  MDNS.update();                             // Check to see if browser is looking for IP address
  int16_t adc0 = ads.readADC_SingleEnded(0); // Reading from channel 0
  int16_t adc1 = ads.readADC_SingleEnded(1); // Reading from channel 1

  float voltage1 = adcToVoltage(adc0);
  float voltage2 = adcToVoltage(adc1);

  pressure1 = mapToPressure(voltage1, minVoltage1, maxVoltage1, minPressure1, maxPressure1);
  pressure2 = mapToPressure(voltage2, minVoltage2, maxVoltage2, minPressure2, maxPressure2);

  drawScreen();
  server.handleClient(); // Handle HTTP requests
  if (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_PIN, HIGH);
    if (!tryingToConnect)
    {
      Serial.println("WiFi not connected, trying to reconnect");
      WiFi.begin(ssid_sta, password_sta);
      startTime = millis();
      tryingToConnect = true;
    }
    else if (millis() - startTime > intervalForAP) // si no hay red en intervalForAP pasa al modo AP
    {
      Serial.println("Attempting WiFi autoconnect");
      wifiManager.autoConnect("watermaker_AP", "12345678"); // password-protected AP
      tryingToConnect = true;                               // Set true again for the next check
    }
  }
  else if (tryingToConnect) // we were trying to connect and now we are connected
  {
    Serial.println("Successfully reconnected to WiFi");
    handleConnected();
    digitalWrite(LED_PIN, LOW);
    tryingToConnect = false;
    ArduinoOTA.setHostname(hostName);
    InitOTA();
  }
  else // we are connected to WiFi
  {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      previousMillis = currentMillis; // Save the current time
      sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.high", pressure1);
      sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.low", pressure2);

      // Blink the LED
      digitalWrite(LED_PIN, HIGH); // Turn on the LED
      delay(100);                  // Wait 100 ms
      digitalWrite(LED_PIN, LOW);  // Turn off the LED
    }
  }
}
