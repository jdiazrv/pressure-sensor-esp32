// Pressure Sensor ESP32 - SignalK Watermaker
// Based on Chain Counter ESP32 mejorado v3.7
// Dual pressure sensor with SignalK integration
// Changelog:
//   v1.2
//     Add: password-only auth on config, OTA and factory reset routes
//     Add: dedicated admin credentials separated from AP password
//     Add: lightweight admin session cookie for protected routes
//     Add: shared-state mutex for sensor/network/runtime snapshots
//     Add: I2C bus hardening with 100 kHz clock and timeout
//     Add: password confirmation and reveal toggles on device settings
//     Change: /config now opens a public settings menu and /device-settings stays protected
//     Change: device settings page focuses on settings only, without maintenance shortcuts
//     Change: dashboard and admin UI text unified in English
//     Change: runtime NVS checkpoint relaxed to 15 min to reduce flash wear
//   v1.1
//     Fix: Sensor 2 factory max pressure corrected to 80 bar
//     Fix: SignalK UDP mapping corrected to low/high semantic paths
//     Add: SignalK HTTP health probe via /signalk every 15 s
//     Add: automatic rediscovery when a previously reachable SignalK server disappears
//     Fix: dashboard SignalK pill now reflects real server availability
//     Fix: dashboard uses backend data validity instead of guessing from pressure values
//     Fix: UDP monitor distinguishes local send success from local send failure
//   v1.0
//     Add: SignalK discovery with robust .local hostname resolution
//     Fix: do not overwrite configured UDP port with SignalK websocket service port
//     Add: persistent SignalK IP and UDP port configuration in NVS
//     Add: sensor source modes Real / Demo / Demo + UDP
//     Add: HTTP OTA for firmware and filesystem update
//     Add: UI status details with FW/UI version and device error reporting
//     Add: ADS1115 missing, no-signal, and disconnected sensor detection
//     Add: SignalK IP edit from UI and LittleFS-served dashboard updates

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config_html.h"

// ── Versión del software ───────────────────────────────────────────────────────
#define SW_VERSION "v1.2"

// ── Nivel de debug ──────────────────────────────────────────────────────────────
// 0 = sin logs
// 1 = errores
// 2 = info
// 3 = verbose
#define DEBUG_LEVEL_ERROR 1
#define DEBUG_LEVEL_INFO 2
#define DEBUG_LEVEL_VERBOSE 3
#define DEBUG DEBUG_LEVEL_INFO

// Macros de logging
#define LOG_ERR(msg)                      \
    do                                    \
    {                                     \
        if (DEBUG >= DEBUG_LEVEL_ERROR)   \
            Serial.println("[ERR] " msg); \
    } while (0)
#define LOG_ERRF(fmt, ...)                                   \
    do                                                       \
    {                                                        \
        if (DEBUG >= DEBUG_LEVEL_ERROR)                      \
            Serial.printf("[ERR] " fmt "\n", ##__VA_ARGS__); \
    } while (0)
#define LOG_INF(msg)                      \
    do                                    \
    {                                     \
        if (DEBUG >= DEBUG_LEVEL_INFO)    \
            Serial.println("[INF] " msg); \
    } while (0)
#define LOG_INFF(fmt, ...)                                   \
    do                                                       \
    {                                                        \
        if (DEBUG >= DEBUG_LEVEL_INFO)                       \
            Serial.printf("[INF] " fmt "\n", ##__VA_ARGS__); \
    } while (0)
#define LOG_VRB(msg)                      \
    do                                    \
    {                                     \
        if (DEBUG >= DEBUG_LEVEL_VERBOSE) \
            Serial.println("[VRB] " msg); \
    } while (0)
#define LOG_VRBF(fmt, ...)                                   \
    do                                                       \
    {                                                        \
        if (DEBUG >= DEBUG_LEVEL_VERBOSE)                    \
            Serial.printf("[VRB] " fmt "\n", ##__VA_ARGS__); \
    } while (0)

// ── Valores por defecto / fábrica ───────────────────────────────────────────────
#define MIN_VOLTAGE1 0.5
#define MAX_VOLTAGE1 4.5
#define MIN_PRESSURE1 0
#define MAX_PRESSURE1 15
#define MIN_VOLTAGE2 0.5
#define MAX_VOLTAGE2 4.5
#define MIN_PRESSURE2 0
#define MAX_PRESSURE2 80
#define MAX_AP_PASSWORD_LENGTH 20
#define AP_PASSWORD "12345678"
#define ADMIN_USERNAME "admin"
#define ADMIN_PASSWORD "12345678"
#define WIFI_MODE 1
#define SENSOR_MODE_REAL 0
#define SENSOR_MODE_DEMO 1
#define SENSOR_MODE_DEMO_UDP 2
#define DEFAULT_SIGNALK_MAX_ATTEMPTS 12
#define DEFAULT_UDP_PORT 4210
#define SIGNALK_RETRY_MS 60000
#define SIGNALK_PROBE_MS 60000
#define SIGNALK_TCP_PROBE_TIMEOUT_MS 500
#define RUNTIME_START_LOW_PRESSURE_BAR 1.0f
#define RUNTIME_STOP_HIGH_PRESSURE_BAR 10.0f
#define RUNTIME_CHECKPOINT_MS 900000UL
#define SENSOR_DISCONNECT_TOLERANCE_V 0.2f
#define SENSOR_NO_SIGNAL_V 0.05f
#define MONITOR_BUFFER_SIZE 160
#define SENSOR_REFRESH_MS 250
#define DISPLAY_REFRESH_MS 1000
#define DISPLAY_PAGE_MS 5000
#define NETWORK_TASK_DELAY_MS 25
#define I2C_CLOCK_HZ 100000UL
#define I2C_TIMEOUT_MS 50
#define AUTH_SESSION_TTL_MS 1800000UL

// ── OLED ────────────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 48
#define OLED_ADDR 0x3C
#define OLED_SDA 16
#define OLED_SCL 17

Adafruit_SSD1306 display(-1);  // RST = -1 (no reset pin)
bool displayAvailable = false;

// ── Variables globales ──────────────────────────────────────────────────────────
Preferences prefs;
SemaphoreHandle_t stateMutex = nullptr;

// Sensor configuration
float minVoltage1 = MIN_VOLTAGE1;
float maxVoltage1 = MAX_VOLTAGE1;
float minPressure1 = MIN_PRESSURE1;
float maxPressure1 = MAX_PRESSURE1;
float pressure1 = 0.0f;
float voltage1 = 0.0f;

float minVoltage2 = MIN_VOLTAGE2;
float maxVoltage2 = MAX_VOLTAGE2;
float minPressure2 = MIN_PRESSURE2;
float maxPressure2 = MAX_PRESSURE2;
float pressure2 = 0.0f;
float voltage2 = 0.0f;

// Network configuration
int wifiModeApSta = WIFI_MODE;
String APpassword = AP_PASSWORD;
String ssid_sta = "";
String password_sta = "";
IPAddress ip1 = IPAddress(0, 0, 0, 0);
unsigned int outPort = DEFAULT_UDP_PORT;
unsigned int signalkServicePort = 3000;
int signalkMaxAttempts = DEFAULT_SIGNALK_MAX_ATTEMPTS;
int signalkDiscoveryAttempts = 0;
bool signalkDiscoveryPending = false;
unsigned long lastDiscoveryAttempt = 0;
bool signalkServerReachable = false;
bool signalkHadSuccessfulProbe = false;
bool signalkDiscoveryContinuous = false;
unsigned long lastSignalkProbeMs = 0;
int sensorMode = SENSOR_MODE_REAL;

// Device configuration
const char *hostName = "watermaker";
unsigned long intervalForAP = 10000;
bool tryingToConnect = false;
bool Ads1115Found = false;
unsigned long startTime = 0;
bool isFirstBoot = true;

// LED pins
#define LED_PIN 2
#define LED_PIN2 4

// Timing
unsigned long previousMillis = 0;
const long interval = 3000;
unsigned long ledBlinkStart = 0;
bool ledBlinkActive = false;
unsigned long lastSensorUpdateMs = 0;
String lastDeviceError = "";
String lastLoggedDeviceError = "";
bool otaInProgress = false;
bool otaWasRunning = false;
unsigned long otaStartMs = 0;
size_t otaProgressLogBytes = 0;
String otaError = "";
int otaCommand = U_FLASH;
String otaLabel = "firmware";
String monitorLines[MONITOR_BUFFER_SIZE];
unsigned long monitorSeqStart = 0;
unsigned long monitorSeqNext = 0;
uint64_t totalRuntimeMs = 0;
uint64_t runtimePersistedMs = 0;
unsigned long runtimeStartMs = 0;
bool runtimeRunning = false;
bool runtimeActivePersisted = false;
String adminPassword = ADMIN_PASSWORD;
String adminSessionToken = "";
unsigned long adminSessionExpiresMs = 0;

// ADS1115
Adafruit_ADS1115 ads;

// Web server and network utilities
WebServer server(80);
WiFiManager wifiManager;
WiFiUDP Udp;
TaskHandle_t networkTaskHandle = nullptr;

// ── Utilities ───────────────────────────────────────────────────────────────────

float adcToVoltage(int16_t adcValue);
float mapToPressure(float voltage, float minVoltage, float maxVoltage, float minPressure, float maxPressure);
void addMonitorEvent(const char *tag, const String &message);
String jsonEscape(const String &input);
String formatMillisBrief(unsigned long ms);
void probeSignalKServer(bool force = false);
void taskNetwork(void *parameter);
void handleConnected();

struct DeviceSettingsPayload
{
    float maxPressure1;
    float minPressure1;
    float minVoltage1;
    float maxVoltage1;
    float maxPressure2;
    float minPressure2;
    float minVoltage2;
    float maxVoltage2;
    int wifiModeApSta;
    int sensorMode;
    int signalkMaxAttempts;
    unsigned int outPort;
    String signalkIp;
    String apPassword;
    String adminPassword;
    uint64_t totalRuntimeMs;
};

void appendSettingsJson(String &json);
void sendSettingsJson();
bool validateSettingsPayload(const DeviceSettingsPayload &payload, String &errorMessage, IPAddress &parsedIp);
bool applySettingsPayload(const DeviceSettingsPayload &payload, bool jsonResponse);
bool readSettingsFromJson(DeviceSettingsPayload &payload, String &errorMessage);
void Event_ApiSettingsGet();
void Event_ApiSettingsPost();
void writePreferences();
void writeRuntimePreferences();
uint64_t currentPartialRuntimeMs();
uint64_t currentTotalRuntimeMs();
void updateRuntimeTracking();
bool ensureAuthenticated();
void handleAuthLogin();
bool hasAdminSession();
String makeSessionToken();
void sendPasswordPrompt();
void lockState();
void unlockState();

struct SharedStateSnapshot
{
    float pressure1;
    float pressure2;
    float voltage1;
    float voltage2;
    IPAddress signalkIp;
    unsigned int udpPort;
    unsigned int signalkServicePort;
    bool adsFound;
    bool signalkAlive;
    bool runtimeRunning;
    bool runtimeActivePersisted;
    bool tryingToConnect;
    int wifiModeApSta;
    int sensorMode;
    int signalkMaxAttempts;
    String lastDeviceError;
};

void captureSharedState(SharedStateSnapshot &snapshot);

bool isValidIP(const IPAddress &ip)
{
    return ip != IPAddress((uint32_t)0);
}

void lockState()
{
    if (stateMutex)
        xSemaphoreTake(stateMutex, portMAX_DELAY);
}

void unlockState()
{
    if (stateMutex)
        xSemaphoreGive(stateMutex);
}

void captureSharedState(SharedStateSnapshot &snapshot)
{
    lockState();
    snapshot.pressure1 = pressure1;
    snapshot.pressure2 = pressure2;
    snapshot.voltage1 = voltage1;
    snapshot.voltage2 = voltage2;
    snapshot.signalkIp = ip1;
    snapshot.udpPort = outPort;
    snapshot.signalkServicePort = signalkServicePort;
    snapshot.adsFound = Ads1115Found;
    snapshot.signalkAlive = signalkServerReachable;
    snapshot.runtimeRunning = runtimeRunning;
    snapshot.runtimeActivePersisted = runtimeActivePersisted;
    snapshot.tryingToConnect = tryingToConnect;
    snapshot.wifiModeApSta = wifiModeApSta;
    snapshot.sensorMode = sensorMode;
    snapshot.signalkMaxAttempts = signalkMaxAttempts;
    snapshot.lastDeviceError = lastDeviceError;
    unlockState();
}

bool ensureAuthenticated()
{
    if (hasAdminSession())
        return true;
    sendPasswordPrompt();
    return false;
}

bool hasAdminSession()
{
    if (adminSessionToken.length() == 0)
        return false;

    unsigned long now = millis();
    if ((long)(now - adminSessionExpiresMs) >= 0)
    {
        adminSessionToken = "";
        adminSessionExpiresMs = 0;
        return false;
    }

    if (!server.hasHeader("Cookie"))
        return false;

    String cookie = server.header("Cookie");
    String expected = "wm_admin_session=" + adminSessionToken;
    return cookie.indexOf(expected) >= 0;
}

String makeSessionToken()
{
    uint32_t a = esp_random();
    uint32_t b = esp_random();
    char buf[24];
    snprintf(buf, sizeof(buf), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
    return String(buf);
}

void sendPasswordPrompt()
{
    String nextPath = server.uri();
    if (nextPath.length() == 0)
        nextPath = "/";

    String html;
    html.reserve(1200);
    html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Admin Access</title>"
           "<style>"
           "body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(180deg,#0d1a2e 0%,#08111d 100%);color:#fff;"
           "display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px;box-sizing:border-box;}"
           ".card{width:min(100%,360px);background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);"
           "border-radius:18px;padding:24px;box-shadow:0 8px 24px rgba(0,0,0,0.25);}"
           "h2{margin:0 0 10px 0;}p{color:#b9c2d0;line-height:1.4;margin:0 0 16px 0;}"
           "input{width:100%;box-sizing:border-box;border:none;border-radius:12px;min-height:48px;padding:0 14px;font-size:16px;margin-bottom:14px;}"
           "button{appearance:none;border:none;border-radius:12px;min-height:48px;padding:0 16px;width:100%;"
           "background:linear-gradient(180deg,#2f7df6,#1459c7);color:#fff;font-size:16px;font-weight:bold;cursor:pointer;}"
           ".cancel{display:block;margin-top:12px;text-align:center;color:#b9c2d0;text-decoration:none;font-size:15px;}"
           ".hint{margin-top:12px;font-size:13px;color:#8fa3bb;}"
           "</style></head><body><div class='card'>"
           "<h2>Admin access</h2>"
           "<p>Enter the admin password to continue.</p>"
           "<form method='POST' action='/auth/login'>"
           "<input type='hidden' name='next' value='" + jsonEscape(nextPath) + "'>"
           "<input type='password' name='password' placeholder='Password' autocomplete='current-password' autofocus>"
           "<button type='submit'>Continue</button>"
           "</form>"
           "<a class='cancel' href='/'>Cancel and return to telemetry</a>"
           "<div class='hint'>Admin user: " ADMIN_USERNAME "</div>"
           "</div></body></html>";

    server.sendHeader("Cache-Control", "no-store");
    server.send(401, "text/html", html);
}

void handleAuthLogin()
{
    String password = server.arg("password");
    String nextPath = server.arg("next");
    if (nextPath.length() == 0)
        nextPath = "/device-settings";

    if (password != adminPassword)
    {
        server.sendHeader("Cache-Control", "no-store");
        server.send(403, "text/html",
                    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<style>"
                    "body{margin:0;font-family:Arial,sans-serif;background:#0d1a2e;color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px;box-sizing:border-box;}"
                    ".card{width:min(100%,360px);background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);border-radius:18px;padding:24px;box-shadow:0 8px 24px rgba(0,0,0,0.25);}"
                    "h2{margin:0 0 10px 0;}p{color:#ffb4b4;line-height:1.4;margin:0 0 16px 0;}"
                    "input{width:100%;box-sizing:border-box;border:none;border-radius:12px;min-height:48px;padding:0 14px;font-size:16px;margin-bottom:14px;}"
                    "button{appearance:none;border:none;border-radius:12px;min-height:48px;padding:0 16px;width:100%;background:linear-gradient(180deg,#2f7df6,#1459c7);color:#fff;font-size:16px;font-weight:bold;cursor:pointer;}"
                    ".cancel{display:block;margin-top:12px;text-align:center;color:#b9c2d0;text-decoration:none;font-size:15px;}"
                    "</style></head><body><div class='card'>"
                    "<h2>Invalid password</h2>"
                    "<p>The admin password you entered is not correct.</p>"
                    "<form method='POST' action='/auth/login'>"
                    "<input type='hidden' name='next' value='" + jsonEscape(nextPath) + "'>"
                    "<input type='password' name='password' placeholder='Password' autocomplete='current-password' autofocus>"
                    "<button type='submit'>Try again</button>"
                    "</form>"
                    "<a class='cancel' href='/'>Cancel and return to telemetry</a>"
                    "</div></body></html>");
        return;
    }

    adminSessionToken = makeSessionToken();
    adminSessionExpiresMs = millis() + AUTH_SESSION_TTL_MS;
    server.sendHeader("Set-Cookie",
                      "wm_admin_session=" + adminSessionToken + "; Max-Age=1800; Path=/; HttpOnly; SameSite=Lax");
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", nextPath, true);
    server.send(302, "text/plain", "");
}

String jsonEscape(const String &input)
{
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); i++)
    {
        char c = input[i];
        if (c == '\\' || c == '"')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out;
}

String formatMillisBrief(unsigned long ms)
{
    unsigned long totalSec = ms / 1000UL;
    unsigned long h = totalSec / 3600UL;
    unsigned long m = (totalSec % 3600UL) / 60UL;
    unsigned long s = totalSec % 60UL;
    char buf[24];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
    return String(buf);
}

void addMonitorEvent(const char *tag, const String &message)
{
    String line = "[" + formatMillisBrief(millis()) + "]";
    if (tag && strlen(tag) > 0)
        line += " [" + String(tag) + "]";
    line += " " + message;

    monitorLines[monitorSeqNext % MONITOR_BUFFER_SIZE] = line;
    monitorSeqNext++;
    if (monitorSeqNext - monitorSeqStart > MONITOR_BUFFER_SIZE)
        monitorSeqStart = monitorSeqNext - MONITOR_BUFFER_SIZE;
}

const char *sensorModeName(int mode)
{
    switch (mode)
    {
        case SENSOR_MODE_DEMO:
            return "demo";
        case SENSOR_MODE_DEMO_UDP:
            return "demo_udp";
        default:
            return "real";
    }
}

bool isDemoMode()
{
    return sensorMode == SENSOR_MODE_DEMO || sensorMode == SENSOR_MODE_DEMO_UDP;
}

bool canProduceReadings()
{
    return isDemoMode() || Ads1115Found;
}

bool shouldSendUdpData()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    if (WiFi.status() != WL_CONNECTED || snapshot.wifiModeApSta != 1) return false;
    if (!isValidIP(snapshot.signalkIp)) return false;
    if (!snapshot.signalkAlive) return false;
    if (snapshot.sensorMode == SENSOR_MODE_DEMO_UDP) return true;
    return snapshot.sensorMode == SENSOR_MODE_REAL && snapshot.adsFound;
}

bool isSystemRunning()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    bool dataValid = canProduceReadings() && snapshot.lastDeviceError.length() == 0;
    if (!dataValid) return false;
    if (snapshot.runtimeRunning) return snapshot.pressure2 > RUNTIME_STOP_HIGH_PRESSURE_BAR;
    if (snapshot.runtimeActivePersisted) return snapshot.pressure2 > RUNTIME_STOP_HIGH_PRESSURE_BAR;
    return snapshot.pressure1 >= RUNTIME_START_LOW_PRESSURE_BAR;
}

uint64_t currentPartialRuntimeMs()
{
    if (!runtimeRunning) return 0;
    return (uint64_t)(millis() - runtimeStartMs);
}

uint64_t currentTotalRuntimeMs()
{
    return totalRuntimeMs + currentPartialRuntimeMs();
}

void updateRuntimeTracking()
{
    bool nowRunning = isSystemRunning();
    unsigned long nowMs = millis();

    if (nowRunning && !runtimeRunning)
    {
        lockState();
        runtimeRunning = true;
        runtimeStartMs = nowMs;
        runtimeActivePersisted = true;
        unlockState();
        writeRuntimePreferences();
    }
    else if (!nowRunning && !runtimeRunning && runtimeActivePersisted)
    {
        lockState();
        runtimeActivePersisted = false;
        unlockState();
        writeRuntimePreferences();
    }
    else if (!nowRunning && runtimeRunning)
    {
        lockState();
        totalRuntimeMs += (uint64_t)(nowMs - runtimeStartMs);
        runtimeRunning = false;
        runtimeStartMs = 0;
        runtimeActivePersisted = false;
        unlockState();
        writeRuntimePreferences();
        runtimePersistedMs = totalRuntimeMs;
    }

    uint64_t currentTotal = currentTotalRuntimeMs();
    if (runtimeRunning && currentTotal >= runtimePersistedMs + (uint64_t)RUNTIME_CHECKPOINT_MS)
    {
        lockState();
        totalRuntimeMs = currentTotal;
        runtimeStartMs = nowMs;
        runtimeActivePersisted = true;
        unlockState();
        writeRuntimePreferences();
        runtimePersistedMs = totalRuntimeMs;
    }
}

void probeSignalKServer(bool force)
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    if (wifiModeApSta != 1 || WiFi.status() != WL_CONNECTED)
    {
        signalkServerReachable = false;
        return;
    }

    if (!isValidIP(snapshot.signalkIp))
    {
        signalkServerReachable = false;
        return;
    }

    unsigned long now = millis();
    if (!force && lastSignalkProbeMs != 0 && now - lastSignalkProbeMs < SIGNALK_PROBE_MS)
        return;

    lastSignalkProbeMs = now;

    bool previousState = snapshot.signalkAlive;
    String target = snapshot.signalkIp.toString() + ":" + String(snapshot.signalkServicePort);
    WiFiClient client;
    client.setTimeout(1);
    bool connected = client.connect(snapshot.signalkIp, snapshot.signalkServicePort, SIGNALK_TCP_PROBE_TIMEOUT_MS);
    lockState();
    signalkServerReachable = connected;
    if (connected)
    {
        signalkHadSuccessfulProbe = true;
        client.stop();
    }
    unlockState();

    if (signalkServerReachable != previousState)
    {
        if (signalkServerReachable)
        {
            LOG_INFF("SignalK TCP OK: %s", target.c_str());
            addMonitorEvent("SK", "TCP probe OK " + target);
        }
        else
        {
            LOG_ERRF("SignalK TCP probe failed: %s", target.c_str());
            addMonitorEvent("ERR", "SignalK TCP probe failed " + target);
        }
    }

    if (!signalkServerReachable && !signalkDiscoveryPending)
    {
        if (previousState || signalkHadSuccessfulProbe)
            signalkDiscoveryContinuous = true;
        signalkDiscoveryPending = true;
        signalkDiscoveryAttempts = 0;
        lastDiscoveryAttempt = 0;
        LOG_INF("SignalK unreachable - restarting discovery");
        addMonitorEvent("SK", "Probe failed, rediscovery started");
    }
}

void updateDeviceError()
{
    if (sensorMode == SENSOR_MODE_REAL && !Ads1115Found)
    {
        lastDeviceError = "ADS1115 not found";
        return;
    }

    if (isDemoMode())
    {
        lastDeviceError = "";
        return;
    }

    if (voltage1 <= SENSOR_NO_SIGNAL_V && voltage2 <= SENSOR_NO_SIGNAL_V)
    {
        lastDeviceError = "No signal from pressure sensors";
        return;
    }

    if (voltage1 <= SENSOR_NO_SIGNAL_V)
    {
        lastDeviceError = "No signal from pressure sensor 1";
        return;
    }

    if (voltage2 <= SENSOR_NO_SIGNAL_V)
    {
        lastDeviceError = "No signal from pressure sensor 2";
        return;
    }

    if (voltage1 < (minVoltage1 - SENSOR_DISCONNECT_TOLERANCE_V) &&
        voltage2 < (minVoltage2 - SENSOR_DISCONNECT_TOLERANCE_V))
    {
        lastDeviceError = "Pressure sensors disconnected";
        return;
    }

    if (voltage1 < (minVoltage1 - SENSOR_DISCONNECT_TOLERANCE_V))
    {
        lastDeviceError = "Pressure sensor 1 disconnected";
        return;
    }

    if (voltage2 < (minVoltage2 - SENSOR_DISCONNECT_TOLERANCE_V))
    {
        lastDeviceError = "Pressure sensor 2 disconnected";
        return;
    }

    if (lastSensorUpdateMs == 0 || millis() - lastSensorUpdateMs > 5000)
    {
        lastDeviceError = "No fresh sensor data";
        return;
    }

    lastDeviceError = "";
}

bool configIsValid()
{
    bool voltagesOk =
        minVoltage1 >= 0.0f && maxVoltage1 <= 5.0f && minVoltage1 < maxVoltage1 &&
        minVoltage2 >= 0.0f && maxVoltage2 <= 5.0f && minVoltage2 < maxVoltage2;

    bool pressuresOk =
        minPressure1 >= 0.0f && maxPressure1 <= 150.0f && minPressure1 < maxPressure1 &&
        minPressure2 >= 0.0f && maxPressure2 <= 150.0f && minPressure2 < maxPressure2;

    bool wifiModeOk = (wifiModeApSta == 0 || wifiModeApSta == 1);
    bool sensorModeOk = (sensorMode >= SENSOR_MODE_REAL && sensorMode <= SENSOR_MODE_DEMO_UDP);
    bool apPasswordOk = APpassword.length() >= 8 && APpassword.length() < MAX_AP_PASSWORD_LENGTH;
    bool adminPasswordOk = adminPassword.length() >= 8 && adminPassword.length() < MAX_AP_PASSWORD_LENGTH;

    return voltagesOk && pressuresOk && wifiModeOk && sensorModeOk && apPasswordOk && adminPasswordOk;
}

// ── OLED helpers ────────────────────────────────────────────────────────────────

void drawScreen()
{
    if (!displayAvailable) return;

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(SW_VERSION);
    display.setCursor(30, 0);
    if (sensorMode == SENSOR_MODE_DEMO)
        display.print("DEMO");
    else if (sensorMode == SENSOR_MODE_DEMO_UDP)
        display.print("DMO+U");
    else
        display.print(Ads1115Found ? "ADS OK" : "NO ADS");
    display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, WHITE);

    bool showNetworkPage = ((millis() / DISPLAY_PAGE_MS) % 2UL) == 1UL;

    if (showNetworkPage)
    {
        display.setTextSize(1);
        display.setCursor(0, 14);
        display.print("NET");

        if (WiFi.status() == WL_CONNECTED)
        {
            String localIp = WiFi.localIP().toString();
            int splitAt = localIp.lastIndexOf('.', 9);
            if (splitAt < 0) splitAt = min((int)localIp.length(), 10);

            display.setCursor(0, 24);
            display.print(localIp.substring(0, splitAt));
            display.setCursor(0, 36);
            if (splitAt < (int)localIp.length())
                display.print(localIp.substring(splitAt + 1));
            else
                display.print("OK");
        }
        else
        {
            display.setCursor(0, 24);
            display.print("Connecting");
            display.setCursor(0, 36);
            display.print("AP active");
        }
    }
    else
    {
        display.setTextSize(2);
        display.setCursor(0, 12);
        display.print(String(pressure1, 1));
        display.setTextSize(1);
        display.setCursor(52, 16);
        display.print("b1");

        display.setTextSize(2);
        display.setCursor(0, 32);
        display.print(String(pressure2, 1));
        display.setTextSize(1);
        display.setCursor(52, 36);
        display.print("b2");
    }

    display.display();
    LOG_VRBF("OLED: %s", showNetworkPage ? "NET" : "PRESSURE");
}

void drawErrorScreen(const char *errorMessage)
{
    if (!displayAvailable) return;

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("ERROR:");
    display.setCursor(0, 10);
    display.print(errorMessage);

    if (WiFi.status() == WL_CONNECTED)
    {
        display.setCursor(0, 24);
        display.print(WiFi.localIP().toString());
    }
    else
    {
        display.setCursor(0, 24);
        display.print("Connecting");
        display.setCursor(0, 36);
        display.print("AP: watermaker");
    }
    display.display();
}

// ── NVS Functions ───────────────────────────────────────────────────────────────

void writePreferences()
{
    if (!configIsValid())
    {
        LOG_ERR("Invalid config. Preferences not saved.");
        return;
    }

    prefs.begin("config", false);
    prefs.putFloat("minV1", minVoltage1);
    prefs.putFloat("maxV1", maxVoltage1);
    prefs.putFloat("minP1", minPressure1);
    prefs.putFloat("maxP1", maxPressure1);
    prefs.putFloat("minV2", minVoltage2);
    prefs.putFloat("maxV2", maxVoltage2);
    prefs.putFloat("minP2", minPressure2);
    prefs.putFloat("maxP2", maxPressure2);
    prefs.putInt("wifiMode", wifiModeApSta);
    prefs.putInt("sensorMode", sensorMode);
    prefs.putString("apPassword", APpassword);
    prefs.putString("adminPassword", adminPassword);
    prefs.putUInt("signalkIp", (uint32_t)ip1);
    prefs.putUInt("outPort", outPort);
    prefs.putInt("signalkMaxAttempts", signalkMaxAttempts);
    prefs.end();
    LOG_INF("NVS saved");
}

void writeRuntimePreferences()
{
    prefs.begin("config", false);
    prefs.putULong64("totalRuntimeMs", totalRuntimeMs);
    prefs.putBool("runtimeActive", runtimeActivePersisted);
    prefs.end();
    LOG_INF("Runtime NVS saved");
}

void readPreferences()
{
    prefs.begin("config", true);
    
    // Check if first run
    bool tpInit = prefs.isKey("nvsInit");
    if (!tpInit)
    {
        LOG_INF("NVS first run - writing defaults");
        prefs.end();
        prefs.begin("config", false);
        prefs.putBool("nvsInit", true);
        prefs.putFloat("minV1", MIN_VOLTAGE1);
        prefs.putFloat("maxV1", MAX_VOLTAGE1);
        prefs.putFloat("minP1", MIN_PRESSURE1);
        prefs.putFloat("maxP1", MAX_PRESSURE1);
        prefs.putFloat("minV2", MIN_VOLTAGE2);
        prefs.putFloat("maxV2", MAX_VOLTAGE2);
        prefs.putFloat("minP2", MIN_PRESSURE2);
        prefs.putFloat("maxP2", MAX_PRESSURE2);
        prefs.putInt("wifiMode", WIFI_MODE);
        prefs.putInt("sensorMode", SENSOR_MODE_REAL);
        prefs.putString("apPassword", AP_PASSWORD);
        prefs.putString("adminPassword", ADMIN_PASSWORD);
        prefs.putUInt("signalkIp", 0);
        prefs.putUInt("outPort", DEFAULT_UDP_PORT);
        prefs.putInt("signalkMaxAttempts", DEFAULT_SIGNALK_MAX_ATTEMPTS);
        prefs.putULong64("totalRuntimeMs", 0);
        prefs.putBool("runtimeActive", false);
    }
    
    minVoltage1 = prefs.getFloat("minV1", MIN_VOLTAGE1);
    maxVoltage1 = prefs.getFloat("maxV1", MAX_VOLTAGE1);
    minPressure1 = prefs.getFloat("minP1", MIN_PRESSURE1);
    maxPressure1 = prefs.getFloat("maxP1", MAX_PRESSURE1);
    minVoltage2 = prefs.getFloat("minV2", MIN_VOLTAGE2);
    maxVoltage2 = prefs.getFloat("maxV2", MAX_VOLTAGE2);
    minPressure2 = prefs.getFloat("minP2", MIN_PRESSURE2);
    maxPressure2 = prefs.getFloat("maxP2", MAX_PRESSURE2);
    wifiModeApSta = prefs.getInt("wifiMode", WIFI_MODE);
    sensorMode = prefs.getInt("sensorMode", SENSOR_MODE_REAL);
    APpassword = prefs.getString("apPassword", AP_PASSWORD);
    adminPassword = prefs.getString("adminPassword", ADMIN_PASSWORD);
    outPort = prefs.getUInt("outPort", DEFAULT_UDP_PORT);
    signalkMaxAttempts = prefs.getInt("signalkMaxAttempts", DEFAULT_SIGNALK_MAX_ATTEMPTS);
    totalRuntimeMs = prefs.getULong64("totalRuntimeMs", 0);
    runtimePersistedMs = totalRuntimeMs;
    runtimeActivePersisted = prefs.getBool("runtimeActive", false);
    
    {
        uint32_t savedIp = prefs.getUInt("signalkIp", 0);
        if (savedIp != 0)
        {
            ip1 = IPAddress(savedIp);
            LOG_INFF("SignalK IP from NVS: %s", ip1.toString().c_str());
        }
    }
    prefs.end();

    if (!configIsValid())
    {
        LOG_ERR("Invalid values in NVS. Using default settings.");
        minVoltage1 = MIN_VOLTAGE1;
        maxVoltage1 = MAX_VOLTAGE1;
        minPressure1 = MIN_PRESSURE1;
        maxPressure1 = MAX_PRESSURE1;
        minVoltage2 = MIN_VOLTAGE2;
        maxVoltage2 = MAX_VOLTAGE2;
        minPressure2 = MIN_PRESSURE2;
        maxPressure2 = MAX_PRESSURE2;
        wifiModeApSta = WIFI_MODE;
        sensorMode = SENSOR_MODE_REAL;
        APpassword = AP_PASSWORD;
        adminPassword = ADMIN_PASSWORD;
        totalRuntimeMs = 0;
        runtimePersistedMs = 0;
        runtimeActivePersisted = false;
        writePreferences();
        writeRuntimePreferences();
    }
}

// ── SignalK discovery ───────────────────────────────────────────────────────────

void trySignalKDiscovery()
{
    if (!signalkDiscoveryPending) return;
    if (wifiModeApSta != 1 || WiFi.status() != WL_CONNECTED) return;
    if (millis() - lastDiscoveryAttempt < SIGNALK_RETRY_MS) return;

    if (!signalkDiscoveryContinuous && signalkMaxAttempts > 0 && signalkDiscoveryAttempts >= signalkMaxAttempts)
    {
        signalkDiscoveryPending = false;
        LOG_INFF("SignalK discovery: stopped after %d attempts", signalkDiscoveryAttempts);
        addMonitorEvent("SK", "Discovery stopped after " + String(signalkDiscoveryAttempts) + " attempts");
        return;
    }

    lastDiscoveryAttempt = millis();
    signalkDiscoveryAttempts++;
    LOG_INFF("SignalK discovery attempt %d/%d...", signalkDiscoveryAttempts, signalkMaxAttempts);

    int n = MDNS.queryService("signalk-ws", "tcp");
    if (n > 0)
    {
        String signalkHost = MDNS.hostname(0);
        int signalkPort = MDNS.port(0);
        IPAddress resolved = MDNS.address(0);

        LOG_INFF("mDNS found: host=%s port=%d addr=%s",
                 signalkHost.c_str(),
                 signalkPort,
                 resolved.toString().c_str());

        // On ESP32, MDNS.address(0) may come back as 0.0.0.0 even when the
        // service exists, so only trust it if it is a real IPv4 address.
        if (!isValidIP(resolved) && signalkHost.length() > 0)
        {
            LOG_INFF("Resolving hostname: %s", signalkHost.c_str());
            if (!WiFi.hostByName(signalkHost.c_str(), resolved) || !isValidIP(resolved))
            {
                String hostLocal = signalkHost.endsWith(".local") ? signalkHost : signalkHost + ".local";
                LOG_INFF("Resolving with .local: %s", hostLocal.c_str());
                if (!WiFi.hostByName(hostLocal.c_str(), resolved) || !isValidIP(resolved))
                {
                    LOG_ERR("SignalK: could not resolve hostname or obtain a valid IP");
                    addMonitorEvent("ERR", "SignalK hostname resolve failed for " + signalkHost);
                    return;
                }
            }
        }

        if (!isValidIP(resolved))
        {
            LOG_ERR("SignalK: mDNS returned 0.0.0.0");
            addMonitorEvent("ERR", "SignalK mDNS returned 0.0.0.0");
            return;
        }

        ip1 = resolved;
        signalkServicePort = (signalkPort > 0) ? (unsigned int)signalkPort : 3000;
        signalkDiscoveryPending = false;
        signalkDiscoveryContinuous = false;
        signalkDiscoveryAttempts = 0;
        signalkServerReachable = false;
        lastSignalkProbeMs = 0;
        prefs.begin("config", false);
        prefs.putUInt("signalkIp", (uint32_t)ip1);
        prefs.end();

        LOG_INFF("SignalK found: %s (service port %d, UDP port %d)",
                 ip1.toString().c_str(), signalkPort, outPort);
        addMonitorEvent("SK", "Discovered " + ip1.toString() + " service=" + String(signalkPort) +
                              " udp=" + String(outPort));

        probeSignalKServer(true);

        if (shouldSendUdpData())
        {
            char udpmessage[256];
            sprintf(udpmessage,
                    "{\"updates\":[{\"$source\":\"ESP32.watermaker\","
                    "\"values\":[{\"path\":\"environment.watermaker.pressure.high\","
                    "\"value\":%.3f},{\"path\":\"environment.watermaker.pressure.low\","
                    "\"value\":%.3f}]}]}",
                    pressure2, pressure1);
            Udp.beginPacket(ip1, outPort);
            Udp.write((const uint8_t *)udpmessage, strlen(udpmessage));
            int udpResult = Udp.endPacket();
            if (udpResult == 1)
            {
                LOG_VRBF("SignalK UDP sent: HP=%.2f LP=%.2f", pressure2, pressure1);
                addMonitorEvent("UDP", "Initial send OK " + ip1.toString() + ":" + String(outPort) +
                                       " hp=" + String(pressure2, 2) + " lp=" + String(pressure1, 2));
            }
            else
            {
                LOG_ERRF("SignalK initial UDP send failed: result=%d", udpResult);
                addMonitorEvent("ERR", "Initial UDP send failed " + ip1.toString() + ":" + String(outPort));
            }
        }
        return;
    }
    else
    {
        LOG_INFF("SignalK not found (%d/%d)", signalkDiscoveryAttempts, signalkMaxAttempts);
        addMonitorEvent("SK", "Discovery attempt " + String(signalkDiscoveryAttempts) + " no match");
    }
}

void sendUDP()
{
    if (!shouldSendUdpData()) return;
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);

    char udpmessage[256];
    sprintf(udpmessage,
            "{\"updates\":[{\"$source\":\"ESP32.watermaker\","
            "\"values\":[{\"path\":\"environment.watermaker.pressure.high\","
            "\"value\":%.3f},{\"path\":\"environment.watermaker.pressure.low\","
            "\"value\":%.3f}]}]}",
            snapshot.pressure2, snapshot.pressure1);
    Udp.beginPacket(snapshot.signalkIp, snapshot.udpPort);
    Udp.write((const uint8_t *)udpmessage, strlen(udpmessage));
    int udpResult = Udp.endPacket();
    if (udpResult == 1)
    {
        LOG_VRBF("UDP sent: HP=%.2f LP=%.2f", snapshot.pressure2, snapshot.pressure1);
        addMonitorEvent("UDP", "Send OK " + snapshot.signalkIp.toString() + ":" + String(snapshot.udpPort) +
                               " hp=" + String(snapshot.pressure2, 2) + " lp=" + String(snapshot.pressure1, 2));
    }
    else
    {
        LOG_ERRF("UDP send failed: result=%d", udpResult);
        addMonitorEvent("ERR", "UDP send failed " + snapshot.signalkIp.toString() + ":" + String(snapshot.udpPort));
    }
}

void taskNetwork(void *parameter)
{
    unsigned long lastSendTime = 0;

    for (;;)
    {
        if (otaInProgress || Update.isRunning())
        {
            vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_DELAY_MS));
            continue;
        }

        trySignalKDiscovery();
        probeSignalKServer();

        if (wifiModeApSta == 1)
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                digitalWrite(LED_PIN, LOW);

                if (!tryingToConnect)
                {
                    LOG_INF("WiFi lost - reconnecting");
                    WiFi.begin(ssid_sta.c_str(), password_sta.c_str());
                    startTime = millis();
                    tryingToConnect = true;
                }
                else if (millis() - startTime > 7000)
                {
                    LOG_INF("WiFi autoConnect attempt");
                    wifiManager.setConfigPortalTimeout(60);
                    wifiManager.setConnectTimeout(15);
                    wifiManager.autoConnect(hostName, APpassword.c_str());
                    WiFi.mode(WIFI_AP_STA);
                    delay(200);
                    WiFi.softAP(hostName, APpassword.c_str());
                    tryingToConnect = true;
                }
            }
            else if (tryingToConnect)
            {
                LOG_INF("WiFi reconnected");
                handleConnected();
                tryingToConnect = false;
            }
            else if (millis() - lastSendTime >= interval)
            {
                lastSendTime = millis();
                if (shouldSendUdpData())
                    sendUDP();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_DELAY_MS));
    }
}

void updateDemoPressures()
{
    float t = millis() / 1000.0f;
    float lowBase = 1.8f + 0.25f * sinf(t * 0.31f) + 0.12f * sinf(t * 1.07f);
    float highBase = 55.0f + 1.1f * sinf(t * 0.22f) + 0.4f * sinf(t * 1.17f);
    const float demoHighMin = 0.0f;
    const float demoHighMax = 70.0f;

    pressure1 = constrain(lowBase, minPressure1, min(maxPressure1, 4.0f));
    pressure2 = constrain(highBase, demoHighMin, demoHighMax);
    lastSensorUpdateMs = millis();
}

void updatePressureReadings()
{
    if (sensorMode == SENSOR_MODE_REAL)
    {
        if (!Ads1115Found)
        {
            updateDeviceError();
            return;
        }

        int16_t adc0 = ads.readADC_SingleEnded(0);
        int16_t adc1 = ads.readADC_SingleEnded(1);

        voltage1 = adcToVoltage(adc0);
        voltage2 = adcToVoltage(adc1);

        pressure1 = mapToPressure(voltage1, minVoltage1, maxVoltage1, minPressure1, maxPressure1);
        pressure2 = mapToPressure(voltage2, minVoltage2, maxVoltage2, minPressure2, maxPressure2);
        lockState();
        lastSensorUpdateMs = millis();
        unlockState();
    }
    else
    {
        voltage1 = minVoltage1 + 0.15f;
        voltage2 = minVoltage2 + 0.15f;
        updateDemoPressures();
    }

    updateDeviceError();

    static unsigned long lastVoltageLogMs = 0;
    if (DEBUG >= 2 && millis() - lastVoltageLogMs >= 2000)
    {
        lastVoltageLogMs = millis();
        LOG_INFF("Sensor voltages: V1=%.3fV V2=%.3fV", voltage1, voltage2);
    }

    if (DEBUG >= DEBUG_LEVEL_INFO && lastDeviceError != lastLoggedDeviceError)
    {
        lastLoggedDeviceError = lastDeviceError;
        if (lastDeviceError.length() > 0)
        {
            LOG_INFF("Device error: %s (V1=%.3fV V2=%.3fV)", lastDeviceError.c_str(), voltage1, voltage2);
            addMonitorEvent("ERR", "Device: " + lastDeviceError);
        }
        else
        {
            LOG_INFF("Device error cleared (V1=%.3fV V2=%.3fV)", voltage1, voltage2);
            addMonitorEvent("INF", "Device error cleared");
        }
    }
}

static void handleOtaUpload()
{
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        otaError = "";
        otaProgressLogBytes = 0;
        otaInProgress = true;

        uint32_t slotMax = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        Serial.printf("[INF] OTA %s upload start: %s  slot_max=%u bytes  stack_free=%u\n",
                      otaLabel.c_str(), upload.filename.c_str(), slotMax,
                      uxTaskGetStackHighWaterMark(NULL));
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, otaCommand))
        {
            otaError = Update.errorString();
            Serial.printf("[ERR] OTA begin failed: %s\n", otaError.c_str());
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE && otaError.length() == 0)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            otaError = Update.errorString();
            Serial.printf("[ERR] OTA write failed at %u: %s\n",
                          Update.progress(), otaError.c_str());
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        Serial.printf("[INF] OTA upload END: received=%u  written=%u\n",
                      upload.totalSize, Update.progress());
        if (otaError.length() == 0)
        {
            if (!Update.end(true))
            {
                otaError = Update.errorString();
                Serial.printf("[ERR] OTA end failed: %s\n", otaError.c_str());
            }
            else
            {
                Serial.println("[INF] OTA end OK - rebooting");
            }
        }
        otaInProgress = false;
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        Update.end();
        otaInProgress = false;
        Serial.printf("[ERR] OTA aborted at %u bytes\n", Update.progress());
    }
}

// ── HTTP handlers ───────────────────────────────────────────────────────────────

void Event_pressure()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    String jsonResponse;
    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    bool dataValid = canProduceReadings() && snapshot.lastDeviceError.length() == 0;
    bool systemRunning = isSystemRunning();
    bool udpAllowed = shouldSendUdpData();
    jsonResponse.reserve(256);
    jsonResponse = "{";
    jsonResponse += "\"pressure1\":" + String(snapshot.pressure1, 3) + ",";
    jsonResponse += "\"pressure2\":" + String(snapshot.pressure2, 3) + ",";
    jsonResponse += "\"rssi\":" + String(rssi) + ",";
    jsonResponse += "\"adsFound\":" + String(snapshot.adsFound ? "true" : "false") + ",";
    jsonResponse += "\"dataValid\":" + String(dataValid ? "true" : "false") + ",";
    jsonResponse += "\"systemRunning\":" + String(systemRunning ? "true" : "false") + ",";
    jsonResponse += "\"partialRuntimeMs\":" + String((unsigned long long)currentPartialRuntimeMs()) + ",";
    jsonResponse += "\"totalRuntimeMs\":" + String((unsigned long long)currentTotalRuntimeMs()) + ",";
    jsonResponse += "\"udpEnabled\":" + String(udpAllowed ? "true" : "false") + ",";
    jsonResponse += "\"sensorMode\":\"" + String(sensorModeName(sensorMode)) + "\",";
    jsonResponse += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    jsonResponse += "\"signalkIp\":\"" + (isValidIP(snapshot.signalkIp) ? snapshot.signalkIp.toString() : String("0.0.0.0")) + "\",";
    jsonResponse += "\"signalkAlive\":" + String(snapshot.signalkAlive ? "true" : "false") + ",";
    jsonResponse += "\"error\":\"" + snapshot.lastDeviceError + "\"";
    jsonResponse += "}";
    ledBlinkStart = millis();
    ledBlinkActive = true;
    digitalWrite(LED_PIN, LOW);

#if DEBUG >= 1
    Serial.println(jsonResponse);
#endif

    server.send(200, "application/json", jsonResponse);
}

void Event_State()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    String json;
    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    bool dataValid = canProduceReadings() && snapshot.lastDeviceError.length() == 0;
    bool systemRunning = isSystemRunning();
    bool udpAllowed = shouldSendUdpData();

    json.reserve(512);
    json = "{";
    json += "\"device\":\"pressure-sensor-esp32\",";
    json += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    json += "\"hostname\":\"" + String(hostName) + "\",";
    json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"localIp\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"sensorMode\":\"" + String(sensorModeName(sensorMode)) + "\",";
    json += "\"adsFound\":" + String(snapshot.adsFound ? "true" : "false") + ",";
    json += "\"dataValid\":" + String(dataValid ? "true" : "false") + ",";
    json += "\"systemRunning\":" + String(systemRunning ? "true" : "false") + ",";
    json += "\"partialRuntimeMs\":" + String((unsigned long long)currentPartialRuntimeMs()) + ",";
    json += "\"totalRuntimeMs\":" + String((unsigned long long)currentTotalRuntimeMs()) + ",";
    json += "\"deviceError\":\"" + jsonEscape(snapshot.lastDeviceError) + "\",";
    json += "\"pressure1\":" + String(snapshot.pressure1, 3) + ",";
    json += "\"pressure2\":" + String(snapshot.pressure2, 3) + ",";
    json += "\"voltage1\":" + String(snapshot.voltage1, 3) + ",";
    json += "\"voltage2\":" + String(snapshot.voltage2, 3) + ",";
    json += "\"signalkIp\":\"" + (isValidIP(snapshot.signalkIp) ? snapshot.signalkIp.toString() : String("0.0.0.0")) + "\",";
    json += "\"signalkAlive\":" + String(snapshot.signalkAlive ? "true" : "false") + ",";
    json += "\"signalkServicePort\":" + String(snapshot.signalkServicePort) + ",";
    json += "\"udpPort\":" + String(snapshot.udpPort) + ",";
    json += "\"udpEnabled\":" + String(udpAllowed ? "true" : "false") + ",";
    json += "\"uptimeMs\":" + String(millis());
    json += "}";

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

void Event_manifest()
{
    File file = LittleFS.open("/manifest.webmanifest", "r");
    if (!file)
    {
        server.send(404, "text/plain", "File not found");
        return;
    }
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.streamFile(file, "application/manifest+json");
    file.close();
}

void Event_icon()
{
    File file = LittleFS.open("/icon.svg", "r");
    if (!file)
    {
        server.send(404, "text/plain", "File not found");
        return;
    }
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.streamFile(file, "image/svg+xml");
    file.close();
}

void Event_Submit()
{
    LOG_INFF("Event_Submit: %s args=%d",
             server.method() == HTTP_GET ? "GET" : "POST", server.args());

    DeviceSettingsPayload payload = {
        server.arg("maxPressure1").toFloat(),
        server.arg("minPressure1").toFloat(),
        server.arg("minVdc1").toFloat(),
        server.arg("maxVdc1").toFloat(),
        server.arg("maxPressure2").toFloat(),
        server.arg("minPressure2").toFloat(),
        server.arg("minVdc2").toFloat(),
        server.arg("maxVdc2").toFloat(),
        server.arg("modo").toInt(),
        server.arg("sensorMode").toInt(),
        server.arg("signalkMaxAttempts").toInt(),
        (unsigned int)server.arg("outPort").toInt(),
        server.arg("signalkIp"),
        server.arg("APpassword"),
        server.arg("adminPassword"),
        totalRuntimeMs,
    };

    applySettingsPayload(payload, false);
}

void appendSettingsJson(String &json)
{
    json += "\"maxPressure1\":" + String(maxPressure1, 3) + ",";
    json += "\"minPressure1\":" + String(minPressure1, 3) + ",";
    json += "\"minVdc1\":" + String(minVoltage1, 3) + ",";
    json += "\"maxVdc1\":" + String(maxVoltage1, 3) + ",";
    json += "\"maxPressure2\":" + String(maxPressure2, 3) + ",";
    json += "\"minPressure2\":" + String(minPressure2, 3) + ",";
    json += "\"minVdc2\":" + String(minVoltage2, 3) + ",";
    json += "\"maxVdc2\":" + String(maxVoltage2, 3) + ",";
    json += "\"modo\":" + String(wifiModeApSta) + ",";
    json += "\"sensorMode\":" + String(sensorMode) + ",";
    json += "\"signalkMaxAttempts\":" + String(signalkMaxAttempts) + ",";
    json += "\"outPort\":" + String(outPort) + ",";
    json += "\"signalkIp\":\"" + (isValidIP(ip1) ? ip1.toString() : String("0.0.0.0")) + "\",";
    json += "\"APpassword\":\"" + jsonEscape(APpassword) + "\",";
    json += "\"adminPassword\":\"" + jsonEscape(adminPassword) + "\",";
    json += "\"totalRuntimeMs\":" + String((unsigned long long)totalRuntimeMs);
}

void sendSettingsJson()
{
    String json;
    json.reserve(384);
    json = "{";
    appendSettingsJson(json);
    json += "}";
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

bool validateSettingsPayload(const DeviceSettingsPayload &payload, String &errorMessage, IPAddress &parsedIp)
{
    if (payload.maxPressure1 <= 0 || payload.maxPressure1 > 150 || payload.minPressure1 < 0 || payload.minPressure1 >= payload.maxPressure1)
    {
        errorMessage = "Invalid Pressure 1 range";
        return false;
    }
    if (payload.maxPressure2 <= 0 || payload.maxPressure2 > 150 || payload.minPressure2 < 0 || payload.minPressure2 >= payload.maxPressure2)
    {
        errorMessage = "Invalid Pressure 2 range";
        return false;
    }
    if (payload.minVoltage1 < 0 || payload.maxVoltage1 > 5 || payload.minVoltage1 >= payload.maxVoltage1)
    {
        errorMessage = "Invalid Voltage 1 range";
        return false;
    }
    if (payload.minVoltage2 < 0 || payload.maxVoltage2 > 5 || payload.minVoltage2 >= payload.maxVoltage2)
    {
        errorMessage = "Invalid Voltage 2 range";
        return false;
    }
    if (payload.wifiModeApSta != 0 && payload.wifiModeApSta != 1)
    {
        errorMessage = "Invalid WiFi mode";
        return false;
    }
    if (payload.sensorMode < SENSOR_MODE_REAL || payload.sensorMode > SENSOR_MODE_DEMO_UDP)
    {
        errorMessage = "Invalid sensor mode";
        return false;
    }
    if (payload.apPassword.length() > MAX_AP_PASSWORD_LENGTH)
    {
        errorMessage = "AP password too long";
        return false;
    }
    if (payload.apPassword.length() > 0 && payload.apPassword.length() < 8)
    {
        errorMessage = "AP password too short";
        return false;
    }
    if (payload.adminPassword.length() == 0)
    {
        errorMessage = "Admin password required";
        return false;
    }
    if (payload.adminPassword.length() < 8)
    {
        errorMessage = "Admin password too short";
        return false;
    }
    if (payload.adminPassword.length() > MAX_AP_PASSWORD_LENGTH)
    {
        errorMessage = "Admin password too long";
        return false;
    }

    String signalkIpStr = payload.signalkIp;
    signalkIpStr.trim();
    if (signalkIpStr.length() == 0 || signalkIpStr == "0.0.0.0")
    {
        parsedIp = IPAddress(0, 0, 0, 0);
        return true;
    }

    if (!parsedIp.fromString(signalkIpStr))
    {
        errorMessage = "Invalid SignalK IP";
        return false;
    }

    return true;
}

bool applySettingsPayload(const DeviceSettingsPayload &payload, bool jsonResponse)
{
    IPAddress parsedIp(0, 0, 0, 0);
    String errorMessage;
    if (!validateSettingsPayload(payload, errorMessage, parsedIp))
    {
        if (jsonResponse)
        {
            String errorJson = "{\"ok\":false,\"error\":\"" + jsonEscape(errorMessage) + "\"}";
            server.send(400, "application/json", errorJson);
        }
        else
        {
            server.send(400, "text/plain", errorMessage);
        }
        return false;
    }

    int oldWifiMode = wifiModeApSta;
    int oldSensorMode = sensorMode;

    maxPressure1 = payload.maxPressure1;
    minPressure1 = payload.minPressure1;
    minVoltage1 = payload.minVoltage1;
    maxVoltage1 = payload.maxVoltage1;
    maxPressure2 = payload.maxPressure2;
    minPressure2 = payload.minPressure2;
    minVoltage2 = payload.minVoltage2;
    maxVoltage2 = payload.maxVoltage2;

    if (payload.apPassword.length() > 0)
        APpassword = payload.apPassword;
    adminPassword = payload.adminPassword;
    wifiModeApSta = payload.wifiModeApSta;
    sensorMode = payload.sensorMode;
    signalkMaxAttempts = (payload.signalkMaxAttempts >= 0 && payload.signalkMaxAttempts <= 60) ? payload.signalkMaxAttempts : DEFAULT_SIGNALK_MAX_ATTEMPTS;
    outPort = (payload.outPort >= 1024 && payload.outPort <= 65535) ? payload.outPort : DEFAULT_UDP_PORT;
    ip1 = parsedIp;
    totalRuntimeMs = payload.totalRuntimeMs;
    runtimePersistedMs = totalRuntimeMs;
    runtimeActivePersisted = runtimeRunning;
    signalkServicePort = 3000;
    signalkServerReachable = false;
    lastSignalkProbeMs = 0;
    updateDeviceError();

    writePreferences();
    writeRuntimePreferences();

    if (ip1 == IPAddress(0, 0, 0, 0) && wifiModeApSta == 1 && WiFi.isConnected())
    {
        signalkDiscoveryPending = true;
        signalkDiscoveryContinuous = false;
        signalkDiscoveryAttempts = 0;
        lastDiscoveryAttempt = 0;
        LOG_INF("SignalK IP cleared - restarting discovery");
    }
    else if (ip1 != IPAddress(0, 0, 0, 0))
    {
        signalkDiscoveryPending = false;
        signalkDiscoveryContinuous = false;
        LOG_INFF("SignalK IP set manually: %s", ip1.toString().c_str());
        probeSignalKServer(true);
    }

    bool shouldRestart = (oldWifiMode != wifiModeApSta);

    if (oldSensorMode != sensorMode)
    {
        pressure1 = 0.0f;
        pressure2 = 0.0f;
        lastSensorUpdateMs = 0;
        updatePressureReadings();
    }

    drawScreen();

    if (jsonResponse)
    {
        String json;
        json.reserve(448);
        json = "{\"ok\":true,\"restartRequired\":";
        json += shouldRestart ? "true" : "false";
        json += ",\"settings\":{";
        appendSettingsJson(json);
        json += "}}";
        server.send(200, "application/json", json);
        if (shouldRestart)
        {
            delay(300);
            ESP.restart();
        }
        return true;
    }

    if (shouldRestart)
    {
        server.send(200, "text/html", "Configuration saved. Device will restart now.");
        delay(500);
        ESP.restart();
        return true;
    }

    String responseHTML = "<html><head><meta http-equiv='refresh' content='1;url=/'></head><body>Configuration saved. Redirecting...</body></html>";
    server.send(200, "text/html", responseHTML);
    return true;
}

bool readSettingsFromJson(DeviceSettingsPayload &payload, String &errorMessage)
{
    if (!server.hasArg("plain"))
    {
        errorMessage = "Missing request body";
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err)
    {
        errorMessage = "Invalid JSON body";
        return false;
    }

    payload.maxPressure1 = doc["maxPressure1"] | maxPressure1;
    payload.minPressure1 = doc["minPressure1"] | minPressure1;
    payload.minVoltage1 = doc["minVdc1"] | minVoltage1;
    payload.maxVoltage1 = doc["maxVdc1"] | maxVoltage1;
    payload.maxPressure2 = doc["maxPressure2"] | maxPressure2;
    payload.minPressure2 = doc["minPressure2"] | minPressure2;
    payload.minVoltage2 = doc["minVdc2"] | minVoltage2;
    payload.maxVoltage2 = doc["maxVdc2"] | maxVoltage2;
    payload.wifiModeApSta = doc["modo"] | wifiModeApSta;
    payload.sensorMode = doc["sensorMode"] | sensorMode;
    payload.signalkMaxAttempts = doc["signalkMaxAttempts"] | signalkMaxAttempts;
    payload.outPort = doc["outPort"] | outPort;
    payload.signalkIp = String((const char *)(doc["signalkIp"] | (isValidIP(ip1) ? ip1.toString().c_str() : "0.0.0.0")));
    payload.apPassword = String((const char *)(doc["APpassword"] | APpassword.c_str()));
    payload.adminPassword = String((const char *)(doc["adminPassword"] | adminPassword.c_str()));
    payload.totalRuntimeMs = doc["totalRuntimeMs"] | totalRuntimeMs;
    return true;
}

void Event_ApiSettingsGet()
{
    sendSettingsJson();
}

void Event_ApiSettingsPost()
{
    DeviceSettingsPayload payload = {
        maxPressure1,
        minPressure1,
        minVoltage1,
        maxVoltage1,
        maxPressure2,
        minPressure2,
        minVoltage2,
        maxVoltage2,
        wifiModeApSta,
        sensorMode,
        signalkMaxAttempts,
        outPort,
        isValidIP(ip1) ? ip1.toString() : String("0.0.0.0"),
        APpassword,
        adminPassword,
        totalRuntimeMs,
    };
    String errorMessage;
    if (!readSettingsFromJson(payload, errorMessage))
    {
        String errorJson = "{\"ok\":false,\"error\":\"" + jsonEscape(errorMessage) + "\"}";
        server.send(400, "application/json", errorJson);
        return;
    }
    applySettingsPayload(payload, true);
}

void Event_Config()
{
    String htmlContent = configPageHTML;
    htmlContent.replace("{maxPressure1}", String(maxPressure1, 3));
    htmlContent.replace("{minPressure1}", String(minPressure1, 3));
    htmlContent.replace("{minVdc1}", String(minVoltage1, 3));
    htmlContent.replace("{maxVdc1}", String(maxVoltage1, 3));
    htmlContent.replace("{maxPressure2}", String(maxPressure2, 3));
    htmlContent.replace("{minPressure2}", String(minPressure2, 3));
    htmlContent.replace("{minVdc2}", String(minVoltage2, 3));
    htmlContent.replace("{maxVdc2}", String(maxVoltage2, 3));
    htmlContent.replace("{wifiMode0}", (wifiModeApSta == 0) ? "selected" : "");
    htmlContent.replace("{wifiMode1}", (wifiModeApSta == 1) ? "selected" : "");
    htmlContent.replace("{sensorMode0}", (sensorMode == SENSOR_MODE_REAL) ? "selected" : "");
    htmlContent.replace("{sensorMode1}", (sensorMode == SENSOR_MODE_DEMO) ? "selected" : "");
    htmlContent.replace("{sensorMode2}", (sensorMode == SENSOR_MODE_DEMO_UDP) ? "selected" : "");
    htmlContent.replace("{APpassword}", String(APpassword));
    htmlContent.replace("{adminPassword}", String(adminPassword));
    htmlContent.replace("{signalkMaxAttempts}", String(signalkMaxAttempts));
    htmlContent.replace("{outPort}", String(outPort));
    htmlContent.replace("{signalkIp}", ip1.toString());
    server.send(200, "text/html", htmlContent);
}

void Event_Index()
{
    File file = LittleFS.open("/index.html", "r");
    if (!file)
    {
        LOG_ERR("Error opening index.html");
        server.send(404, "text/plain", "File not found");
        return;
    }

    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.sendHeader("ETag", "\"index-v2\"");
    server.streamFile(file, "text/html");
    file.close();
}

void Event_meta()
{
    String jsonResponse;
    jsonResponse.reserve(128);
    jsonResponse = "{";
    jsonResponse += "\"minPressure1\":" + String(minPressure1, 3) + ",";
    jsonResponse += "\"maxPressure1\":" + String(maxPressure1, 3) + ",";
    jsonResponse += "\"minPressure2\":" + String(minPressure2, 3) + ",";
    jsonResponse += "\"maxPressure2\":" + String(maxPressure2, 3) + ",";
    jsonResponse += "\"sensorMode\":\"" + String(sensorModeName(sensorMode)) + "\",";
    jsonResponse += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    jsonResponse += "\"error\":\"" + lastDeviceError + "\"";
    jsonResponse += "}";

#if DEBUG >= 1
    Serial.println(jsonResponse);
#endif

    server.send(200, "application/json", jsonResponse);
}

void Event_SetSignalKIp()
{
    String ipArg = server.arg("ip");
    ipArg.trim();

    IPAddress newIp(0, 0, 0, 0);
    if (ipArg.length() > 0 && ipArg != "0.0.0.0")
    {
        if (!newIp.fromString(ipArg))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_ip\"}");
            return;
        }
    }

    ip1 = newIp;
    signalkServicePort = 3000;
    signalkServerReachable = false;
    lastSignalkProbeMs = 0;
    if (isValidIP(ip1))
    {
        signalkDiscoveryPending = false;
        signalkDiscoveryContinuous = false;
        LOG_INFF("SignalK IP set from UI: %s", ip1.toString().c_str());
        addMonitorEvent("SK", "IP set manually from UI: " + ip1.toString());
        probeSignalKServer(true);
    }
    else if (wifiModeApSta == 1 && WiFi.status() == WL_CONNECTED)
    {
        signalkDiscoveryPending = true;
        signalkDiscoveryContinuous = false;
        signalkDiscoveryAttempts = 0;
        lastDiscoveryAttempt = 0;
        LOG_INF("SignalK IP cleared from UI - restarting discovery");
        addMonitorEvent("SK", "IP cleared from UI, rediscovery started");
    }

    prefs.begin("config", false);
    prefs.putUInt("signalkIp", (uint32_t)ip1);
    prefs.end();

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json",
                String("{\"ok\":true,\"ip\":\"") +
                (isValidIP(ip1) ? ip1.toString() : String("0.0.0.0")) + "\"}");
}

void Event_Tools()
{
    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Settings</title>"
        "<style>"
        "body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(180deg,#0d1a2e 0%,#08111d 100%);color:#fff;}"
        ".page{max-width:560px;margin:0 auto;padding:16px;}"
        ".card{background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);border-radius:18px;padding:16px;box-shadow:0 8px 24px rgba(0,0,0,0.25);}"
        ".actions{display:grid;gap:12px;margin-top:14px;}"
        "button{appearance:none;border:none;border-radius:14px;min-height:56px;font-size:18px;font-weight:bold;color:#fff;cursor:pointer;width:100%;}"
        ".secondary{background:linear-gradient(180deg,#303846,#151b24);}"
        ".primary{background:linear-gradient(180deg,#2f7df6,#1459c7);}"
        ".danger{background:linear-gradient(180deg,#8d2b2b,#5d1717);}"
        "p{color:#b9c2d0;line-height:1.4;}"
        "</style></head><body><div class='page'><div class='card'>"
        "<h2>Settings</h2>"
        "<p>Diagnostics, maintenance and protected device settings for this ESP32.</p>"
        "<div class='actions'>"
        "<button class='secondary' onclick=\"window.location='/diagnostics'\">Device diagnostics</button>"
        "<button class='secondary' onclick=\"window.location='/monitor'\">SignalK monitor</button>"
        "<button class='primary' onclick=\"window.location='/device-settings'\">Device settings</button>"
        "<button class='danger' onclick=\"if(confirm('Restore factory settings?')) window.location.href='/factory'\">Factory reset</button>"
        "<button class='secondary' onclick=\"window.location='/update'\">Update firmware</button>"
        "<button class='secondary' onclick=\"window.location='/updatefs'\">Update filesystem</button>"
        "<button class='secondary' onclick=\"window.location='/'\">Back to telemetry</button>"
        "</div></div></div></body></html>";
    server.send(200, "text/html", html);
}

void Event_DiagnosticsData()
{
    String json = "{";
    json += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    json += "\"sensorMode\":\"" + String(sensorModeName(sensorMode)) + "\",";
    json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
    json += "\"hostname\":\"" + String(hostName) + "\",";
    json += "\"localIp\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi\":" + String((WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0) + ",";
    json += "\"signalkIp\":\"" + (isValidIP(ip1) ? ip1.toString() : String("0.0.0.0")) + "\",";
    json += "\"signalkAlive\":" + String(signalkServerReachable ? "true" : "false") + ",";
    json += "\"signalkServicePort\":" + String(signalkServicePort) + ",";
    json += "\"udpPort\":" + String(outPort) + ",";
    json += "\"adsFound\":" + String(Ads1115Found ? "true" : "false") + ",";
    json += "\"voltage1\":" + String(voltage1, 3) + ",";
    json += "\"voltage2\":" + String(voltage2, 3) + ",";
    json += "\"pressure1\":" + String(pressure1, 3) + ",";
    json += "\"pressure2\":" + String(pressure2, 3) + ",";
    json += "\"heapFree\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"heapMin\":" + String(ESP.getMinFreeHeap()) + ",";
    json += "\"cpuMHz\":" + String(getCpuFrequencyMhz()) + ",";
    json += "\"chipModel\":\"" + jsonEscape(ESP.getChipModel()) + "\",";
    json += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
    json += "\"chipCores\":" + String(ESP.getChipCores()) + ",";
    json += "\"flashSize\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"sketchSize\":" + String(ESP.getSketchSize()) + ",";
    json += "\"freeSketchSpace\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"uptimeMs\":" + String(millis()) + ",";
    json += "\"deviceError\":\"" + jsonEscape(lastDeviceError) + "\"";
    json += "}";
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

void Event_DiagnosticsPage()
{
    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Diagnostics</title>"
        "<style>"
        "body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(180deg,#0d1a2e 0%,#08111d 100%);color:#fff;}"
        ".page{max-width:700px;margin:0 auto;padding:16px;}"
        ".grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));}"
        ".card{background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);border-radius:18px;padding:14px;box-shadow:0 8px 24px rgba(0,0,0,0.25);}"
        ".label{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#9bb0c8;margin-bottom:6px;}"
        ".value{font-size:18px;font-weight:bold;word-break:break-word;}"
        ".top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px;}"
        "button{appearance:none;border:none;border-radius:12px;min-height:46px;padding:0 14px;background:linear-gradient(180deg,#303846,#151b24);color:#fff;font-size:16px;font-weight:bold;cursor:pointer;}"
        "</style></head><body><div class='page'>"
        "<div class='top'><h2>Device diagnostics</h2><button onclick=\"window.location='/tools'\">Back</button></div>"
        "<div id='grid' class='grid'></div>"
        "<script>"
        "const fields=[['Firmware','firmwareVersion'],['Mode','sensorMode'],['Device error','deviceError'],['WiFi connected','wifiConnected'],['SSID','ssid'],['Hostname','hostname'],['Local IP','localIp'],['AP IP','apIp'],['MAC','mac'],['RSSI','rssi'],['SignalK IP','signalkIp'],['SignalK alive','signalkAlive'],['SignalK HTTP port','signalkServicePort'],['UDP port','udpPort'],['ADS1115','adsFound'],['Voltage 1','voltage1'],['Voltage 2','voltage2'],['Pressure 1','pressure1'],['Pressure 2','pressure2'],['Heap free','heapFree'],['Heap min','heapMin'],['CPU MHz','cpuMHz'],['Chip model','chipModel'],['Chip revision','chipRevision'],['Chip cores','chipCores'],['Flash size','flashSize'],['Sketch size','sketchSize'],['Free sketch','freeSketchSpace'],['Uptime','uptimeMs']];"
        "function fmtDuration(ms){const sec=Math.floor((Number(ms)||0)/1000);if(sec<60)return sec+' s';const min=Math.floor(sec/60);if(min<60)return min+' min';const hr=Math.floor(min/60);if(hr<24)return hr+' h';const day=Math.floor(hr/24);return day+' d';}"
        "function fmtKb(v){return (Math.round((Number(v)||0)/1024))+' KB';}"
        "function fmtValue(key,val){if(val===undefined||val===null||val==='')return '--';if(key==='uptimeMs')return fmtDuration(val);if(['heapFree','heapMin','flashSize','sketchSize','freeSketchSpace'].includes(key))return fmtKb(val);if(key==='rssi')return val+' dBm';if(key==='cpuMHz')return val+' MHz';if(key==='voltage1'||key==='voltage2')return Number(val).toFixed(3)+' V';if(key==='pressure1'||key==='pressure2')return Number(val).toFixed(1)+' bar';if(key==='wifiConnected'||key==='adsFound'||key==='signalkAlive')return val?'Yes':'No';return val;}"
        "function render(data){const grid=document.getElementById('grid');grid.innerHTML=fields.map(f=>'<div class=\"card\"><div class=\"label\">'+f[0]+'</div><div class=\"value\">'+fmtValue(f[1],data[f[1]])+'</div></div>').join('');}"
        "function poll(){fetch('/api/diagnostics',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>{});}"
        "poll();setInterval(poll,2000);"
        "</script></body></html>";
    server.send(200, "text/html", html);
}

void Event_MonitorData()
{
    unsigned long since = 0;
    if (server.hasArg("since"))
        since = strtoul(server.arg("since").c_str(), nullptr, 10);
    if (since < monitorSeqStart)
        since = monitorSeqStart;

    String text;
    for (unsigned long seq = since; seq < monitorSeqNext; seq++)
    {
        text += String(seq);
        text += "\t";
        text += monitorLines[seq % MONITOR_BUFFER_SIZE];
        text += "\n";
    }

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "text/plain", text);
}

void Event_MonitorPage()
{
    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SignalK Monitor</title>"
        "<style>"
        "body{margin:0;background:#031508;color:#5dff83;font-family:Menlo,Monaco,monospace;}"
        ".top{display:flex;justify-content:space-between;align-items:center;padding:12px 14px;background:#07210f;border-bottom:1px solid #124d20;position:sticky;top:0;}"
        "button{appearance:none;border:none;border-radius:10px;min-height:40px;padding:0 14px;background:#10351a;color:#b9ffc8;font-size:14px;font-weight:bold;cursor:pointer;}"
        "#log{padding:12px 14px;white-space:pre-wrap;word-break:break-word;font-size:13px;line-height:1.45;height:calc(100vh - 65px);overflow:auto;}"
        "</style></head><body><div class='top'><strong>SignalK monitor</strong><button onclick=\"window.location='/tools'\">Back</button></div><div id='log'></div>"
        "<script>"
        "let since=0;const log=document.getElementById('log');"
        "function poll(){fetch('/api/monitor?since='+since,{cache:'no-store'}).then(r=>r.text()).then(t=>{if(!t)return;t.trim().split('\\n').forEach(line=>{if(!line)return;const p=line.indexOf('\\t');if(p===-1)return;since=parseInt(line.slice(0,p),10)+1;log.textContent+=line.slice(p+1)+'\\n';});log.scrollTop=log.scrollHeight;}).catch(()=>{});}"
        "poll();setInterval(poll,1000);"
        "</script></body></html>";
    server.send(200, "text/html", html);
}

void handleNotFound()
{
    LOG_INFF("404 [%s] %s",
             server.method() == HTTP_GET ? "GET" : "POST",
             server.uri().c_str());
    server.send(404, "text/plain", "File Not Found\n\n");
}

void handleConnected()
{
    LOG_INFF("WiFi connected - IP: %s", WiFi.localIP().toString().c_str());
    addMonitorEvent("NET", "WiFi connected " + WiFi.localIP().toString());

    if (!MDNS.begin(hostName))
    {
        LOG_ERR("mDNS failed");
    }
    else
    {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "device", "watermaker");
        MDNS.addServiceTxt("http", "tcp", "model", "pressure-sensor");
        LOG_INF("mDNS OK - watermaker.local advertised");
    }

    ssid_sta = WiFi.SSID();
    password_sta = WiFi.psk();

    if (ip1 == IPAddress(0, 0, 0, 0))
    {
        signalkDiscoveryPending = true;
        signalkDiscoveryContinuous = false;
        lastDiscoveryAttempt = 0;
        signalkDiscoveryAttempts = 0;
        signalkServerReachable = false;
        lastSignalkProbeMs = 0;
        LOG_INF("SignalK IP unknown - starting discovery");
    }
    else
    {
        signalkDiscoveryPending = false;
        signalkDiscoveryContinuous = false;
        signalkServerReachable = false;
        lastSignalkProbeMs = 0;
        LOG_INFF("SignalK IP reused: %s", ip1.toString().c_str());
        probeSignalKServer(true);
    }

    LOG_INFF("UDP port %d", outPort);

    ArduinoOTA
        .onStart([]()
                 {
            String type = (ArduinoOTA.getCommand()==U_FLASH)?"sketch":"filesystem";
            LOG_INFF("OTA Start: %s", type.c_str());
            addMonitorEvent("OTA", "ArduinoOTA start " + type); })
    .onEnd([]()
               { LOG_INF("OTA End"); addMonitorEvent("OTA", "ArduinoOTA end"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("[INF] ArduinoOTA: %u / %u bytes (%u%%)\n",
                        progress, total, progress / (total / 100)); })
        .onError([](ota_error_t error)
                 {
            LOG_ERRF("OTA Error[%u]", error);
            addMonitorEvent("ERR", "ArduinoOTA error " + String(error));
            if      (error == OTA_AUTH_ERROR)    LOG_ERR("OTA Auth Failed");
            else if (error == OTA_BEGIN_ERROR)   LOG_ERR("OTA Begin Failed");
            else if (error == OTA_CONNECT_ERROR) LOG_ERR("OTA Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) LOG_ERR("OTA Receive Failed");
            else if (error == OTA_END_ERROR)     LOG_ERR("OTA End Failed"); });

    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.begin();

    if (!canProduceReadings())
    {
        drawErrorScreen("ADS1115 NOT FOUND");
    }
    else
    {
        drawScreen();
    }
}

// ── ADC Functions ───────────────────────────────────────────────────────────────

float adcToVoltage(int16_t adcValue)
{
    return static_cast<float>(adcValue) * 0.1875f / 1000.0f;
}

float mapToPressure(float voltage, float minVoltage, float maxVoltage, float minPressure, float maxPressure)
{
    if (maxVoltage <= minVoltage) return minPressure;

    float ratio = (voltage - minVoltage) / (maxVoltage - minVoltage);
    float pressure = minPressure + ratio * (maxPressure - minPressure);

    if (pressure < minPressure) pressure = minPressure;
    if (pressure > maxPressure) pressure = maxPressure;

    return pressure;
}

// ── Factory Reset ───────────────────────────────────────────────────────────────

void Event_Factory()
{
    LOG_INF("Factory reset requested");
    prefs.begin("config", false);
    prefs.clear();
    prefs.end();
    adminSessionToken = "";
    adminSessionExpiresMs = 0;

    String html =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='6;url=/'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body{margin:0;font-family:Arial,sans-serif;"
        "background:linear-gradient(180deg,#0d1a2e 0%,#08111d 100%);"
        "color:#fff;display:flex;align-items:center;justify-content:center;"
        "height:100vh;text-align:center;}"
        ".card{background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);"
        "border-radius:18px;padding:32px;}"
        "p{font-size:18px;} .small{font-size:14px;color:#aaa;}"
        "</style></head><body>"
        "<div class='card'>"
        "<p><b>&#10003; Factory reset completed</b></p>"
        "<p>Min Voltage 1: " + String(MIN_VOLTAGE1, 1) + " V</p>"
        "<p>Max Voltage 1: " + String(MAX_VOLTAGE1, 1) + " V</p>"
        "<p>Min Pressure 1: " + String(MIN_PRESSURE1) + " bar</p>"
        "<p>Max Pressure 1: " + String(MAX_PRESSURE1) + " bar</p>"
        "<p>Min Voltage 2: " + String(MIN_VOLTAGE2, 1) + " V</p>"
        "<p>Max Voltage 2: " + String(MAX_VOLTAGE2, 1) + " V</p>"
        "<p>Min Pressure 2: " + String(MIN_PRESSURE2) + " bar</p>"
        "<p>Max Pressure 2: " + String(MAX_PRESSURE2) + " bar</p>"
        "<p>SignalK attempts: " + String(DEFAULT_SIGNALK_MAX_ATTEMPTS) + "</p>"
        "<p>UDP port: " + String(DEFAULT_UDP_PORT) + "</p>"
        "<p>Admin password: " + String(ADMIN_PASSWORD) + "</p>"
        "<p class='small'>Rebooting... redirecting in 6s</p>"
        "</div></body></html>";

    server.send(200, "text/html", html);
    server.client().clear();
    delay(3000);
    ESP.restart();
}

// ── Setup ───────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.printf("\n[INF] Pressure Sensor %s starting... (DEBUG=%d)\n", SW_VERSION, DEBUG);
    Serial.println("[INF] Debug levels: 1=ERR 2=INF 3=VRB");
    addMonitorEvent("SYS", "Boot " + String(SW_VERSION));

    // LED
    pinMode(LED_PIN2, OUTPUT);
    digitalWrite(LED_PIN2, HIGH);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Read preferences
    stateMutex = xSemaphoreCreateMutex();
    readPreferences();

    // Init I2C before probing any device on the bus
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    // Init ADS1115
    LOG_INF("Initializing ADS1115...");
    if (!ads.begin())
    {
        LOG_ERR("ADS1115 not found. Check the connection.");
        Ads1115Found = false;
        pressure1 = 0.0f;
        pressure2 = 0.0f;
        addMonitorEvent("ERR", "ADS1115 not found");
    }
    else
    {
        LOG_INF("ADS1115 found.");
        Ads1115Found = true;
        addMonitorEvent("SYS", "ADS1115 detected");
    }

    // Init OLED
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    displayAvailable = true;
    display.clearDisplay();
    display.display();
    LOG_INF("Display OK");

    // Show startup screen
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("MAB WaterMaker");
    display.setCursor(0, 12);
    display.print("Starting...");
    display.setCursor(0, 26);
    display.print(SW_VERSION);
    if (isDemoMode())
    {
        display.setCursor(0, 38);
        display.print(sensorMode == SENSOR_MODE_DEMO ? "Demo" : "Demo+UDP");
    }
    else if (!Ads1115Found)
    {
        display.setCursor(0, 38);
        display.print("No ADS1115");
    }
    display.display();
    updateDeviceError();
    updatePressureReadings();

    // WiFi setup
    WiFi.setHostname(hostName);
    WiFi.disconnect(true);
    delay(500);

    if (wifiModeApSta == 0)
    {
        WiFi.mode(WIFI_AP);
        delay(200);
        WiFi.softAP(hostName, APpassword.c_str());
        LOG_INFF("AP mode - IP: %s", WiFi.softAPIP().toString().c_str());
        
        if (!MDNS.begin(hostName))
        {
            LOG_ERR("Error starting mDNS");
        }
        else
        {
            LOG_INF("mDNS responder started");
        }
    }
    else
    {
        WiFi.mode(WIFI_AP_STA);
        delay(200);
        WiFi.softAP(hostName, APpassword.c_str());
        WiFi.setHostname(hostName);
        startTime = millis();
        tryingToConnect = true;
        wifiManager.setConfigPortalTimeout(60);
        wifiManager.setConnectTimeout(15);
        if (WiFi.psk().length() == 0)
        {
            LOG_INF("No WiFi credentials - starting portal");
            wifiManager.autoConnect(hostName, APpassword.c_str());
            WiFi.mode(WIFI_AP_STA);
            delay(200);
            WiFi.softAP(hostName, APpassword.c_str());
        }
        else
        {
            WiFi.begin();
        }
    }

    // Init LittleFS
    if (!LittleFS.begin(true))
    {
        LOG_ERR("Error mounting LittleFS");
        addMonitorEvent("ERR", "LittleFS mount failed");
    }
    else
    {
        LOG_INF("LittleFS mounted successfully");
        addMonitorEvent("SYS", "LittleFS mounted");
    }

    // HTTP routes
    const char *headerKeys[] = {"Cookie"};
    server.collectHeaders(headerKeys, 1);
    server.on("/", Event_Index);
    server.on("/auth/login", HTTP_POST, handleAuthLogin);
    server.on("/manifest.webmanifest", HTTP_GET, Event_manifest);
    server.on("/icon.svg", HTTP_GET, Event_icon);
    server.on("/config", HTTP_GET, []()
              {
                Event_Tools(); });
    server.on("/device-settings", HTTP_GET, []()
              {
                if (!ensureAuthenticated()) return;
                Event_Config(); });
    server.on("/submit", HTTP_POST, []()
              {
                if (!ensureAuthenticated()) return;
                Event_Submit(); });
    server.on("/submit", HTTP_GET, []()
              {
                if (!ensureAuthenticated()) return;
                Event_Submit(); });
    server.on("/pressure", HTTP_GET, []()
              {
                server.sendHeader("Access-Control-Allow-Origin", "*");
                Event_pressure(); });
    server.on("/api/state", HTTP_GET, []()
              {
                server.sendHeader("Access-Control-Allow-Origin", "*");
                Event_State(); });
    server.on("/api/settings", HTTP_GET, []()
              {
                server.sendHeader("Access-Control-Allow-Origin", "*");
                Event_ApiSettingsGet(); });
    server.on("/api/settings", HTTP_POST, []()
              {
                server.sendHeader("Access-Control-Allow-Origin", "*");
                if (!ensureAuthenticated()) return;
                Event_ApiSettingsPost(); });
    server.on("/meta", HTTP_GET, []()
              {
                server.sendHeader("Access-Control-Allow-Origin", "*");
                Event_meta(); });
    server.on("/factory", HTTP_GET, Event_Factory);
    server.on("/tools", HTTP_GET, Event_Tools);
    server.on("/diagnostics", HTTP_GET, Event_DiagnosticsPage);
    server.on("/api/diagnostics", HTTP_GET, Event_DiagnosticsData);
    server.on("/monitor", HTTP_GET, Event_MonitorPage);
    server.on("/api/monitor", HTTP_GET, Event_MonitorData);
    server.on("/signalk.txt", []()
              {
                server.sendHeader("Cache-Control", "no-cache");
                server.send(200, "text/plain",
                    (ip1!=IPAddress(0,0,0,0)) ? ip1.toString() : "0.0.0.0"); });
    server.on("/set-signalk", HTTP_GET, []()
              {
                if (!ensureAuthenticated()) return;
                Event_SetSignalKIp(); });
    server.on("/wifi.txt", []()
              {
                server.sendHeader("Cache-Control", "no-cache");
                int rssi = (WiFi.status()==WL_CONNECTED) ? WiFi.RSSI() : 0;
        server.send(200, "text/plain", String(rssi)); });
    server.on("/favicon.ico", []()
              { server.send(204); });
    server.onNotFound(handleNotFound);

    // OTA update pages
    server.on("/update", HTTP_GET, []()
              {
        if (!ensureAuthenticated()) return;
        server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>OTA Update</title>"
            "<style>body{font-family:Arial,sans-serif;background:#0d1a2e;color:#fff;"
            "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
            ".card{background:rgba(255,255,255,0.07);border-radius:16px;padding:32px;"
            "min-width:300px;text-align:center;}"
            "h2{margin-top:0;}input[type=file]{margin:16px 0;width:100%;}"
            "input[type=submit]{background:#2f7df6;color:#fff;border:none;padding:12px 28px;"
            "border-radius:10px;font-size:16px;cursor:pointer;width:100%;}"
            "</style></head><body><div class='card'>"
            "<h2>Firmware Update</h2>"
            "<form method='POST' enctype='multipart/form-data'>"
            "<input type='file' name='firmware' accept='.bin'>"
            "<input type='submit' value='Upload &amp; Update'>"
            "</form></div></body></html>"); });
    server.on("/updatefs", HTTP_GET, []()
              {
        if (!ensureAuthenticated()) return;
        server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>LittleFS Update</title>"
            "<style>body{font-family:Arial,sans-serif;background:#0d1a2e;color:#fff;"
            "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
            ".card{background:rgba(255,255,255,0.07);border-radius:16px;padding:32px;"
            "min-width:300px;text-align:center;}"
            "h2{margin-top:0;}input[type=file]{margin:16px 0;width:100%;}"
            "input[type=submit]{background:#2f7df6;color:#fff;border:none;padding:12px 28px;"
            "border-radius:10px;font-size:16px;cursor:pointer;width:100%;}"
            "</style></head><body><div class='card'>"
            "<h2>Filesystem Update</h2>"
            "<form method='POST' enctype='multipart/form-data'>"
            "<input type='file' name='filesystem' accept='.bin'>"
            "<input type='submit' value='Upload LittleFS'>"
            "</form></div></body></html>"); });
    server.on("/update", HTTP_POST,
        []()
        {
            if (!ensureAuthenticated()) return;
            if (Update.hasError())
                server.send(200, "text/html",
                    "<h2 style='color:red'>Update FAILED: " + otaError + "</h2>");
            else
            {
                server.send(200, "text/html",
                    "<h2>Update OK! Rebooting in 5s...</h2>"
                    "<meta http-equiv='refresh' content='10;URL=/'>");
                server.client().clear();
                delay(500);
                ESP.restart();
            }
        },
        []()
        {
            if (!ensureAuthenticated()) return;
            otaCommand = U_FLASH;
            otaLabel = "firmware";
            handleOtaUpload();
        });
    server.on("/updatefs", HTTP_POST,
        []()
        {
            if (!ensureAuthenticated()) return;
            if (Update.hasError())
                server.send(200, "text/html",
                    "<h2 style='color:red'>Filesystem update FAILED: " + otaError + "</h2>");
            else
            {
                server.send(200, "text/html",
                    "<h2>Filesystem update OK! Rebooting in 5s...</h2>"
                    "<meta http-equiv='refresh' content='10;URL=/'>");
                server.client().clear();
                delay(500);
                ESP.restart();
            }
        },
        []()
        {
            if (!ensureAuthenticated()) return;
            otaCommand = U_SPIFFS;
            otaLabel = "filesystem";
            handleOtaUpload();
        });

    Update.onProgress([](size_t done, size_t /*total*/)
    {
        if (done - otaProgressLogBytes >= 100 * 1024)
        {
            otaProgressLogBytes = done;
            Serial.printf("[INF] HTTP OTA: %u KB written\n", (unsigned)(done / 1024));
        }
    });

    server.begin();
    xTaskCreatePinnedToCore(taskNetwork, "taskNetwork", 8192, nullptr, 1, &networkTaskHandle, 0);
    LOG_INF("HTTP server started");
    LOG_INF("OTA: http://<IP>/update");
    addMonitorEvent("SYS", "HTTP server started");
    addMonitorEvent("SYS", "Setup complete " + String(SW_VERSION));

    LOG_INFF("Setup complete - %s", SW_VERSION);
}

// ── Loop ────────────────────────────────────────────────────────────────────────

void loop()
{
    static unsigned long lastSensorRefreshMs = 0;
    static unsigned long lastDisplayRefreshMs = 0;

    bool otaNow = Update.isRunning();
    if (otaNow && !otaWasRunning)
    {
        otaStartMs = millis();
        Serial.printf("[INF] HTTP OTA started - size: %u bytes, stack free: %u\n",
                      Update.size(), uxTaskGetStackHighWaterMark(NULL));
        otaWasRunning = true;
    }
    else if (!otaNow && otaWasRunning)
    {
        unsigned long elapsed = millis() - otaStartMs;
        if (Update.hasError())
            Serial.printf("[ERR] HTTP OTA FAILED after %lums: %s\n", elapsed, Update.errorString());
        else
            Serial.printf("[INF] HTTP OTA finished OK in %lums\n", elapsed);
        otaWasRunning = false;
    }

    // LED blink
    if (ledBlinkActive && millis() - ledBlinkStart >= 100)
    {
        digitalWrite(LED_PIN, HIGH);
        ledBlinkActive = false;
    }

    // OTA
    ArduinoOTA.handle();

    if (otaInProgress || Update.isRunning())
    {
        server.handleClient();
        delay(1);
        return;
    }

    // Read sensors at 4 Hz. Pressure changes slowly enough that this keeps
    // the UI responsive while reducing unnecessary ADC/I2C work in the loop.
    if (millis() - lastSensorRefreshMs >= SENSOR_REFRESH_MS)
    {
        lastSensorRefreshMs = millis();
        updatePressureReadings();
    }

    updateRuntimeTracking();
    if (canProduceReadings() && millis() - lastDisplayRefreshMs >= DISPLAY_REFRESH_MS)
    {
        lastDisplayRefreshMs = millis();
        drawScreen();
    }
    else
    {
        static unsigned long lastErrorScreenUpdate = 0;
        if (millis() - lastErrorScreenUpdate >= 2000)
        {
            lastErrorScreenUpdate = millis();
            if (WiFi.status() == WL_CONNECTED)
            {
                drawErrorScreen("ADS1115 NOT FOUND");
            }
        }
    }

    // HTTP server
    server.handleClient();
    delay(1);
}
