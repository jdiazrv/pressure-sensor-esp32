#include <Wire.h>
#include <Adafruit_ADS1x15.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <ESP8266MDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "index_html.h"
#include "config_html.h" //script for configuration page
#include <LittleFS.h>
// this definitions are only used if EEPROM values are empty or not valid
#define MIN_VOLTAGE1 0.5
#define MAX_VOLTAGE1 4.5
#define MIN_PRESSURE1 0
#define MAX_PRESSURE1 15
#define MIN_VOLTAGE2 0.5
#define MAX_VOLTAGE2 4.5
#define MIN_PRESSURE2 0
#define MAX_PRESSURE2 15

// Direcciones EEPROM
const int ADDRESSES[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36};

// Oled display
//  OLED parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 0 // No se usa el pin de reset en esta configuración
#define WIFI_AP_STA 1
#define LED_PIN D5                // led para indicar si esta conectado a wifi (apagado)
unsigned long previousMillis = 0; // Almacena la última vez que se envió un mensaje UDP
const long interval = 3000;       // Intervalo en milisegundos (3000 ms = 3 segundos)

String hostName = "watermaker";
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
bool tryingToConnect = false;
unsigned long startTime;           // Variable to store the start time of the connection attempt
bool isFirstBoot = true;           // Variable to store if this is the first boot
String ssid_sta = "enjoy";         // Set WLAN name for station connection
String password_sta = "EmmO174.."; // Set password for station connection
IPAddress ip1;                     // IP Address to be used for SigalK so that udp knows where to send messages - amend if yours is different
unsigned int outPort = 4210;       // and which port to send to
Adafruit_ADS1115 ads;              // Use this for the 16-bit version (ADS1115)

Adafruit_SSD1306 display(OLED_RESET);
void drawScreen()
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(String(pressure1, 1));
  display.setTextSize(1);
  display.setCursor(54, 7);
  display.print("b");
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.print(String(pressure2, 1));
  display.setTextSize(1);
  display.setCursor(54, 37);
  display.print("b");
  display.display();
}

#if (SSD1306_LCDHEIGHT != 48)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

ESP8266WebServer server(80); // Define Web Server at port 80 for general connection to central boat wifi
WiFiManager wifiManager;
WiFiUDP Udp;
void Event_Index()
{

  // If "http://<ip address>/" requested

  File file = LittleFS.open("/index.html", "r");
  if (!file)
  {
    Serial.println("Error al abrir index.html");
    server.send(404, "text/plain", "File not found");

    Dir dir = LittleFS.openDir("/");
    Serial.println("Listado de archivos en LittleFS:");
    while (dir.next())
    {
      File file = dir.openFile("r");
      Serial.print("Archivo: ");
      Serial.print(dir.fileName());
      Serial.print(" Tamaño: ");
      Serial.print(file.size());
      Serial.println(" bytes");
      file.close();
    }
    return;
  }
  server.streamFile(file, "text/html");
 // server.send(200, "text/html", indexHTML); // Send Index Website
}

void writeEEPROM()
{
  if (maxVoltage1 > 1 && maxVoltage1 < 5 && maxPressure1 > 0.1 && maxPressure1 < 150)
  {
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

void readEEPROM()
{

  EEPROM.get(ADDRESSES[0], minVoltage1);
  EEPROM.get(ADDRESSES[1], maxVoltage1);
  EEPROM.get(ADDRESSES[2], minPressure1);
  EEPROM.get(ADDRESSES[3], maxPressure1);
  EEPROM.get(ADDRESSES[4], minVoltage2);
  EEPROM.get(ADDRESSES[5], maxVoltage2);
  EEPROM.get(ADDRESSES[6], minPressure2);
  EEPROM.get(ADDRESSES[7], maxPressure2);
  if (isnan(maxVoltage1) || isnan(maxPressure1) || maxVoltage1 <= 0 || maxVoltage1 > 5 || maxPressure1 > 150 || maxPressure1 <= 0)
  {
    // Código a ejecutar si maxVoltage1 o maxPressure1 es NaN,
    // o si cualquiera de las otras condiciones se cumple
    // asumimos que la EEPROM esta corrupta o no inicalizado y asignamos valores default
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

void Event_pressure()
{
  String jsonResponse = "{\"pressure1\": " + String(pressure1) + ", \"pressure2\": " + String(pressure2) + "}";
   server.send(200, "application/json", jsonResponse);
   Serial.println(jsonResponse);
}
void Event_js()
{
  File file = LittleFS.open("/gauge.min.js", "r");
  if (!file)
  {
    Serial.println("Error al abrir gauge.min.js");
    server.send(404, "text/plain", "File not found");

    Dir dir = LittleFS.openDir("/");
    Serial.println("Listado de archivos en LittleFS:");
    while (dir.next())
    {
      File file = dir.openFile("r");
      Serial.print("Archivo: ");
      Serial.print(dir.fileName());
      Serial.print(" Tamaño: ");
      Serial.print(file.size());
      Serial.println(" bytes");
      file.close();
    }
    return;
  }

  server.streamFile(file, "application/javascript");
  file.close();


  // If "http://<ip address>/" requested
//  server.send(200, "text/html", indexHTML); // Send Index Website
}

void Event_Submit()
{
  server.send(200, "text/html", "Configuración guardada");
  Serial.println("Configuración guardada");
  Serial.println(server.arg("maxPressure1"));
  Serial.println(server.arg("minPressure1"));
  Serial.println(server.arg("minVdc1"));
  Serial.println(server.arg("maxVdc1"));
  Serial.println(server.arg("maxPressure2"));
  Serial.println(server.arg("minPressure2"));
  Serial.println(server.arg("minVdc2"));
  Serial.println(server.arg("maxVdc2"));
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

/**
 * @brief This function is used to discover SignalK services.
 *
 * It attempts to find the service "signalk-ws" using the MDNS protocol.
 * The function makes multiple attempts and waits for a response before retrying.
 * If the service is found, the IP address of the service is returned.
 *
 * @return The IP address of the discovered SignalK service.
 */
IPAddress discoverSignalKServices()
{
  IPAddress serviceIP;
  if (isFirstBoot)
  {
    int maxAttempts = 5;
    bool serviceFound = false;

    for (int attempt = 1; attempt <= maxAttempts; attempt++)
    {
      Serial.print("Intento ");
      Serial.print(attempt);
      Serial.println(" de buscar servicios 'signalk-ws'...");

      unsigned long startDiscoveryTime = millis();

      // Buscar el servicio "signalk-ws" en el protocolo TCP
      int n = MDNS.queryService("signalk-ws", "tcp");

      unsigned long endDiscoveryTime = millis();
      Serial.print("Tiempo para descubrir servicios: ");
      Serial.print(endDiscoveryTime - startDiscoveryTime);
      Serial.println(" ms");

      if (n == 0)
      {
        Serial.println("No se encontraron servicios 'signalk-ws'!");
        if (attempt == maxAttempts)
        {
          Serial.println("Se han agotado los intentos para encontrar servicios 'signalk-ws'.");
          break;
        }
        Serial.println("Reintentando...");
        delay(1000); // Espera un segundo antes de reintentar
      }
      else
      {
        Serial.print("Se encontró servicio 'signalk-ws' en: ");
        Serial.print(MDNS.IP(0));
        Serial.print(":");
        Serial.println(MDNS.port(0));
        serviceIP = MDNS.IP(0);
        serviceFound = true;
        break;
      }
    }
    if (!serviceFound)
    {
      Serial.println("No se pudo encontrar el servicio 'signalk-ws' después de todos los intentos.");
    }
  }
  else // coming from reconecction after light sleep. ip1 already holds signalk IP
  {
    serviceIP = ip1;
  }
  Serial.println(serviceIP);
  return serviceIP; // Devuelve la dirección IP del servicio descubierto
}
void handleConnected()
{
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Host Name: ");
  Serial.println(WiFi.getHostname());
  if (!MDNS.begin("windlass"))
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
  char udpmessage1[1024]; // Next few lines send metadata to SignalK for chainlength - replace mmsi with your own "self" vessel details
                          // sprintf(udpmessage, "{\"context\": \"vessels.urn: mrn: imo: mmsi:247074820 \",\"updates\": [{\"meta\":[{\"path\": \"navigation.anchor.chainlength\",\"value\": {\"units\": \"m\",\"description\": \"deployed chain length\",\"displayName\": \"Chain Length\",\"shortName\": \"DCL\"}}]}]}");
  sprintf(udpmessage1, "{\"updates\":[{\"$source\":\"ESP32.watermakerl\",\"values\":[{\"path\":\"environment.watermaker.Pressure.high\",\"value\":%f}]}]}", pressure1);

  Udp.beginPacket(ip1, outPort);
  Udp.write((uint8_t *)udpmessage1, strlen(udpmessage1)); //
  Udp.printf("Seconds since boot: %lu", millis() / 1000);
  Udp.endPacket();
  Serial.print("Metadata sent to SignalK: ");
  Serial.println(udpmessage1);
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
  Serial.print("leo la EEPROM por primera vez...");
  Serial.println(maxPressure1);
  WiFi.hostname(hostName); // setting it via mdns (later on) only works for AP mode
  tryingToConnect = true;
  String chkPassword = WiFi.psk();
  if (chkPassword.length() == 0)
  {
    wifiManager.autoConnect("Windlass_AP", "12345678"); // password protected ap
  }
  else
  {
    WiFi.begin();
  }
  if (!LittleFS.begin())
  {
    Serial.println("Error al montar LittleFS");
    return;
  }
  else
  {
    Serial.println("LittleFS montado correctamente");
  }
  server.on("/", Event_Index);
  server.on("/gauge.min.js", Event_js); // when index_html calls for gauge.min.js we need to serve it.
  server.on("/config", HTTP_GET, Event_Config);
  server.on("/submit", HTTP_POST, Event_Submit);
  server.on("/pressure", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Event_pressure();});
  server.begin();
  Serial.println("HTTP Server started");
}



void loop()
{
  int16_t adc0 = ads.readADC_SingleEnded(0); // Reading from channel 0
  float voltage1 = adc0 * 0.1875 / 1000;     // Convert to voltage
  int16_t adc1 = ads.readADC_SingleEnded(1); // Reading from channel 0
  float voltage2 = adc1 * 0.1875 / 1000;     // Convert to voltage

  // Mapeo del voltaje a la presión
  // El voltaje de 0.5 a 4.5 V corresponde a 0 a 15 bar
  pressure1 = ((
                   voltage1 - minVoltage1) /
               (maxVoltage1 - minVoltage1)) *
              (maxPressure1 - minPressure1); // Ajustar el rango de voltaje al rango de presión
  if (pressure1 < 0)
  {
    pressure1 = 0; // no negative pressure
  }

  pressure2 = ((
                   voltage2 - minVoltage2) /
               (maxVoltage2 - minVoltage2)) *
              (maxPressure2 - minPressure2); // Ajustar el rango de voltaje al rango de presión
  if (pressure2 < 0)
  {
    pressure2 = 0; // no negative pressure
  }

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
    else if (millis() - startTime > 7000)
    {
      Serial.println("Attempting WiFi autoconnect");
      wifiManager.autoConnect("Windlass_AP", "12345678"); // password protected ap
      tryingToConnect = true;                             // Set true again for the next check
    }
  }
  else if (tryingToConnect) // we were trying to connect and now we are connected
  {
    Serial.println("Successfully reconnected to WiFi");
    handleConnected();
    digitalWrite(LED_PIN, LOW);
    tryingToConnect = false;
  }
  else // we are connected to wifi
  {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      previousMillis = currentMillis; // Guarda el tiempo actual
      char udpmessage1[1024];         // Next few lines send metadata to SignalK for chainlength - replace mmsi with your own "self" vessel details
                                      // sprintf(udpmessage, "{\"context\": \"vessels.urn: mrn: imo: mmsi:247074820 \",\"updates\": [{\"meta\":[{\"path\": \"navigation.anchor.chainlength\",\"value\": {\"units\": \"m\",\"description\": \"deployed chain length\",\"displayName\": \"Chain Length\",\"shortName\": \"DCL\"}}]}]}");
      sprintf(udpmessage1, "{\"updates\":[{\"$source\":\"ESP32.watermakerl\",\"values\":[{\"path\":\"environment.watermaker.Pressure.high\",\"value\":%f}]}]}", pressure1);
      Udp.beginPacket(ip1, outPort);
      Udp.write((uint8_t *)udpmessage1, strlen(udpmessage1)); //
      Udp.endPacket();
      char udpmessage2[1024]; // Next few lines send metadata to SignalK for chainlength - replace mmsi with your own "self" vessel details
                              // sprintf(udpmessage, "{\"context\": \"vessels.urn: mrn: imo: mmsi:247074820 \",\"updates\": [{\"meta\":[{\"path\": \"navigation.anchor.chainlength\",\"value\": {\"units\": \"m\",\"description\": \"deployed chain length\",\"displayName\": \"Chain Length\",\"shortName\": \"DCL\"}}]}]}");
      sprintf(udpmessage2, "{\"updates\":[{\"$source\":\"ESP32.watermakerl\",\"values\":[{\"path\":\"environment.watermaker.Pressure.low\",\"value\":%f}]}]}", pressure2);

      Udp.beginPacket(ip1, outPort);
      Udp.write((uint8_t *)udpmessage2, strlen(udpmessage2)); //
      Udp.endPacket();

      // Parpadea el LED
      digitalWrite(LED_PIN, HIGH); // Enciende el LED
      delay(100);                  // Espera 100 ms
      digitalWrite(LED_PIN, LOW);  // Apaga el LED
    }
  }
}
