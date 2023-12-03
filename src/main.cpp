

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
#define MAX_AP_PASSWORD_LENGTH 20 // Longitud máxima para la contraseña del AP
#define AP_PASSWORD "12345678"    // Contraseña del AP
#define WIFI_MODE 1               // 0 para AP, 1 para red WiFi
// EEPROM Addresses for storing configuration settings
const int ADDRESSES[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44};

// OLED display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 0 // Reset pin not used in this configuration
#define WIFI_AP_STA 1
#define LED_PIN D5
#define DEBUG 1                   // LED pin for Wi-Fi connection indication
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
unsigned long startTime;                              // Tracks the start time of Wi-Fi connection attempts
bool isFirstBoot = true;                              // Flag to check if it's the first boot
String ssid_sta = "enjoy";                            // WLAN name for station connection
String password_sta = "EmmO174..";                    // WLAN password for station connection
IPAddress ip1;                                        // IP address for SignalK server
unsigned int outPort = 4210;                          // UDP port for SignalK server communication
int WifiModeApSTa = 1;                                // 0 para AP, 1 para red WiFi
char apPassword[MAX_AP_PASSWORD_LENGTH] = "12345678"; // Contraseña del AP

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
  if (maxVoltage2 < 5 && maxVoltage1 < 5 && maxPressure1 > 0.1 && maxPressure1 < 150)
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
    EEPROM.put(ADDRESSES[8], WifiModeApSTa);
    EEPROM.put(ADDRESSES[9], apPassword);
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
  EEPROM.get(ADDRESSES[8], WifiModeApSTa);
  EEPROM.get(ADDRESSES[9], apPassword);

  // Check if the read values are valid, if not, set them to default values
  if (isnan(maxVoltage1) || isnan(maxPressure1) || maxVoltage1 <= 0 || maxVoltage1 > 5 || maxPressure1 > 150 || maxPressure1 <= 0 || WifiModeApSTa < 0 || WifiModeApSTa > 1 || isnan(WifiModeApSTa))
  {
    Serial.print("Invalid values. Using default settings");
    minVoltage1 = MIN_VOLTAGE1;
    maxVoltage1 = MAX_VOLTAGE1;
    minPressure1 = MIN_PRESSURE1;
    maxPressure1 = MAX_PRESSURE1;
    minVoltage2 = MIN_VOLTAGE2;
    maxVoltage2 = MAX_VOLTAGE2;
    minPressure2 = MIN_PRESSURE2;
    maxPressure2 = MAX_PRESSURE2;
    WifiModeApSTa = WIFI_MODE;
    strcpy(apPassword, AP_PASSWORD); // Copia el literal en apPassword
    writeEEPROM();
  }
}

// Event handler for the pressure endpoint
void Event_pressure()
{
  // Send pressure values as a JSON response
  String jsonResponse = "{\"pressure1\": " + String(pressure1) + ", \"pressure2\": " + String(pressure2) + "}";
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW); // Blink the LED
  server.send(200, "application/json", jsonResponse);
#if DEBUG == 1
  Serial.println(jsonResponse);
#endif
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

  Serial.println("New Configuration");
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
  int newWifiModeApSTa = server.arg("wifiMode").toInt();
  String newApPassword = server.arg("apPassword"); // Asume que 'apPassword' es el nombre del campo
                                                   // Verificar si WifiModeApSTa ha cambiado
  bool shouldRestart = false;
  if (newWifiModeApSTa != WifiModeApSTa)
  {
    WifiModeApSTa = newWifiModeApSTa;
    shouldRestart = true;
  }
  if (newApPassword != apPassword)
  {
    strncpy(apPassword, newApPassword.c_str(), MAX_AP_PASSWORD_LENGTH);
    shouldRestart = true;
  }

  writeEEPROM();

  if (shouldRestart)
  {
    server.send(200, "text/html", "Configuration saved. Device will restart now.");
    delay(2000);
    ESP.restart(); // Reiniciar el dispositivo para aplicar cambios
  }
  drawScreen();

  // Enviar una página que muestra el mensaje y luego redirige
  String responseHTML = "<html><head><meta http-equiv='refresh' content='1;url=/'></head><body>Configuration saved. Redirecting...</body></html>";
  server.send(200, "text/html", responseHTML);
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
  htmlContent.replace("{wifiModeAP}", (WifiModeApSTa == 0) ? "selected" : "");
  htmlContent.replace("{wifiModeSTA}", (WifiModeApSTa == 1) ? "selected" : "");
  htmlContent.replace("{apPassword}", String(apPassword));
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
  readEEPROM();                                      // Read configurations from EEPROM, including WifiModeApSTa
  const rst_info *resetInfo = system_get_rst_info(); // Get information about the last reset
  if (resetInfo->reason == REASON_EXT_SYS_RST)
  {
    // Hardware reset detected through the reset button
    Serial.println("Hardware reset detected. Resetting AP password to default.");
    strcpy(apPassword, AP_PASSWORD); // Reset the AP password to default
    // Save the reset password to EEPROM
    writeEEPROM();
  }
  else
  {
    // Reset due to other reasons, load normal configurations
    readEEPROM();
  }
  // Component initialization
  ads.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  drawScreen();

  Serial.println("Start");

  // Network setup
  WiFi.hostname(hostName); // Set the device hostname

  if (WifiModeApSTa == 0)
  {
    // AP mode setup
    Serial.println("Setting AP mode. SSID is " + String(hostName) + ", password is " + String(apPassword));
    WiFi.softAP(hostName, apPassword);
    if (!MDNS.begin(hostName))
    { // Start the mDNS responder for hostName.local
      Serial.println("Error setting up MDNS responder!");
    }
    else
    {
      Serial.println("mDNS responder started");
    }
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    // Attempt to connect to an existing WiFi network
    String chkPassword = WiFi.psk();
    if (chkPassword.length() == 0)
    {
      wifiManager.autoConnect("watermaker_config", "12345678"); // Password-protected AP
    }
    else
    {
      WiFi.begin(ssid_sta.c_str(), password_sta.c_str());
    }
  }

  // Mount LittleFS
  if (!LittleFS.begin())
  {
    Serial.println("Error mounting LittleFS");
    return;
  }
  else
  {
    Serial.println("LittleFS mounted successfully");
  }

  // Web server setup
  server.on("/", Event_Index);
  server.on("/gauge.min.js", Event_js);
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
  float pressure = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * (maxPressure - minPressure); // Calculate the pressure based on the voltage
  return (pressure < 0) ? 0 : pressure;                                                                // Return the pressure, but never less than 0
}

void handleNetwork()
{

  // WiFi Mode
  if (WiFi.status() != WL_CONNECTED)
  {                              // If the device is not connected to WiFi
    digitalWrite(LED_PIN, HIGH); // Turn on the LED to indicate that it's not connected
    if (!tryingToConnect)
    {                                                               // If the device is not currently trying to connect
      Serial.println("WiFi not connected, trying to reconnect..."); // Print a message
      WiFi.begin(ssid_sta.c_str(), password_sta.c_str());           // Try to connect to the WiFi network
      startTime = millis();                                         // Record the start time
      tryingToConnect = true;                                       // Set the flag
    }
    else if (millis() - startTime > intervalForAP)
    {                                                       // If the device has been trying to connect for too long
      Serial.println("Retrying WiFi connection...");        // Print a message
      wifiManager.autoConnect("watermaker_AP", "12345678"); // Try to automatically connect
      tryingToConnect = true;                               // Set the flag
    }
  }
  else if (tryingToConnect)
  {                                                     // If the device is connected and was previously trying to connect
    Serial.println("Successfully reconnected to WiFi"); // Print a message
    handleConnected();                                  // Handle the connection
    digitalWrite(LED_PIN, LOW);                         // Turn off the LED
    tryingToConnect = false;                            // Reset the flag
    ArduinoOTA.setHostname(hostName);                   // Set the hostname for the OTA service
    InitOTA();                                          // Initialize the OTA service
  }
  else
  {                                         // If the device is connected and was not previously trying to connect
    unsigned long currentMillis = millis(); // Get the current time
    if (currentMillis - previousMillis >= interval)
    {                                                                                      // If it's time to send an update
      previousMillis = currentMillis;                                                      // Record the time of this update
      sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.high", pressure1); // Send an update for the high pressure
      sendPressureUpdate(ip1, outPort, "environment.watermaker.pressure.low", pressure2);  // Send an update for the low pressure
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW); // Blink the LED
    }
  }
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
  if (WifiModeApSTa == 1)
  {
    handleNetwork();
  }
}
