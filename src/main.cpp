// Pressure Sensor ESP32 - SignalK Watermaker
// Based on Chain Counter ESP32 mejorado v3.7
// Dual pressure sensor with SignalK integration
// Changelog:
//   v1.5
//     Add: LED state-machine patterns aligned with project status priorities
//     Add: both LEDs blink fast when ADS1115 is missing in real mode
//     Fix: ADS1115 detection scans all valid addresses (0x48-0x4B) with I2C diagnostics
//     Fix: dashboard renders live bars when readings are available even with device warnings
//     Add: voltage values in /pressure JSON and on both dashboard cards
//     Fix: SignalK manual target accepts ip:port format from UI
//     Fix: Last reading timestamp updates reliably in dashboard polling loop
//     Change: min/max range markers now update with periodic one-minute buckets
//     Change: removed duplicated card status text while keeping color label chip
//   v1.4
//     Add: I2C mutex for bus serialization (ADS1115 + OLED)
//     Add: OtaState struct for coherent OTA state management
//     Add: RuntimeState struct for sensor/runtime data encapsulation
//     Add: UDP resync helpers (invalidateUdpLastSent, resetUdpPressureHistory)
//     Add: Display refresh separated from page switching (1s vs 5s)
//     Add: Sensor reading refactored into cohesive helpers
//     Fix: OLED access removed from taskNetwork (no cross-core I2C race)
//     Fix: All I2C operations protected with lockI2C()
//     Fix: OTA handlers use otaState.error as single source of truth
//     Fix: WiFi reconnection uses WiFi.begin() without cached credentials
//     Fix: Event_Submit preserves existing values when fields missing
//     Fix: Event_MonitorData minimizes lock time (snapshot pattern)
//     Fix: SignalK IP handler reads servicePort under lock
//     Fix: OLED startup screen wrapped with i2cMutex
//     Fix: showNetworkPage passed as parameter (no global state)
//     Fix: signalkCtx initialization under lockState in readPreferences()
//     Fix: ConfigSnapshot separated from runtime state
//     Change: updatePressureReadings split into focused helpers
//     Change: writePreferences split into config/signalK/runtime domains
//     Change: applySettingsPayload uses unified SignalK source-of-truth
//     Remove: dead globals (pressure1/2, voltage1/2, previousMillis, intervalForAP, isFirstBoot)
//   v1.3
//     Add: dedicated mutex for monitor ring buffer reads/writes
//     Fix: preserve discovered SignalK service port when saving settings
//     Fix: monitor API now reads buffered lines under monitor lock
//     Fix: sensor/error debug logging now snapshots shared values before logging
//     Fix: pressure/meta/state JSON now use consistent escaping for error text
//     Change: settings JSON parsing now applies overrides on top of the current payload state
//     Change: submit/settings handlers now use runtime snapshots instead of raw shared reads
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
#include <Update.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config_html.h"

// ── Versión del software ───────────────────────────────────────────────────────
#define SW_VERSION "v1.5"

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

// SignalK timing - FAST right after events, SLOW in steady state
#define SIGNALK_PROBE_FAST_MS 5000UL
#define SIGNALK_PROBE_SLOW_MS 60000UL
#define SIGNALK_DISCOVERY_FAST_MS 5000UL
#define SIGNALK_DISCOVERY_SLOW_MS 60000UL
#define SIGNALK_RETRY_MS 60000UL  // Legacy, kept for compatibility

#define SIGNALK_TCP_PROBE_TIMEOUT_MS 500
#define RUNTIME_START_LOW_PRESSURE_BAR 1.0f
#define RUNTIME_STOP_HIGH_PRESSURE_BAR 10.0f
#define UDP_SEND_INTERVAL_MS 2000UL
#define UDP_AVERAGE_WINDOW 5
#define RUNTIME_CHECKPOINT_MS 900000UL
#define SENSOR_DISCONNECT_TOLERANCE_V 0.2f
#define SENSOR_NO_SIGNAL_V 0.05f
#define MONITOR_BUFFER_SIZE 160
#define MONITOR_LINE_MAX_LEN 80
#define SENSOR_REFRESH_MS 250
#define DISPLAY_REFRESH_MS 1000
#define DISPLAY_PAGE_MS 5000
#define NETWORK_TASK_DELAY_MS 50
#define I2C_CLOCK_HZ 100000UL
#define I2C_TIMEOUT_MS 50
#define AUTH_SESSION_TTL_MS 1800000UL

// ── OLED ────────────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 48
#define OLED_ADDR 0x3C
#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(-1);  // RST = -1 (no reset pin)
bool displayAvailable = false;

// ── Variables globales ──────────────────────────────────────────────────────────
Preferences prefs;
SemaphoreHandle_t stateMutex = nullptr;
SemaphoreHandle_t prefsMutex = nullptr;  // Mutex for NVS/Preferences operations
SemaphoreHandle_t monitorMutex = nullptr;
SemaphoreHandle_t i2cMutex = nullptr;    // Mutex for I2C bus (ADS1115, OLED)

// ── SignalK State Machine ────────────────────────────────────────────────────────
enum SignalKState : uint8_t
{
    SK_OFF = 0,
    SK_SEARCHING = 1,
    SK_READY = 2
};

struct SignalKContext
{
    SignalKState state = SK_OFF;
    IPAddress targetIp = IPAddress(0,0,0,0);
    uint16_t servicePort = 3000;
    uint16_t udpPort = DEFAULT_UDP_PORT;

    bool manualTarget = false;
    bool targetHealthy = false;
    bool hadSuccessfulProbe = false;

    uint8_t discoveryAttempts = 0;
    unsigned long lastDiscoveryAttemptMs = 0;
    unsigned long lastProbeMs = 0;
};

SignalKContext signalkCtx;

// Sensor configuration
float minVoltage1 = MIN_VOLTAGE1;
float maxVoltage1 = MAX_VOLTAGE1;
float minPressure1 = MIN_PRESSURE1;
float maxPressure1 = MAX_PRESSURE1;

float minVoltage2 = MIN_VOLTAGE2;
float maxVoltage2 = MAX_VOLTAGE2;
float minPressure2 = MIN_PRESSURE2;
float maxPressure2 = MAX_PRESSURE2;

// Network configuration
int wifiModeApSta = WIFI_MODE;
char APpassword[MAX_AP_PASSWORD_LENGTH + 1] = AP_PASSWORD;
char ssid_sta[33] = "";  // Max SSID length is 32
char password_sta[65] = "";  // Max WiFi password length is 64
int signalkMaxAttempts = DEFAULT_SIGNALK_MAX_ATTEMPTS;

int sensorMode = SENSOR_MODE_REAL;

// Device configuration
const char *hostName = "watermaker";
unsigned long startTime = 0;

// Monitor buffer (fixed-size circular buffer to avoid heap fragmentation)
char monitorLines[MONITOR_BUFFER_SIZE][MONITOR_LINE_MAX_LEN];
unsigned long monitorSeqStart = 0;
unsigned long monitorSeqNext = 0;

// OTA State
struct OtaState
{
    bool inProgress = false;
    bool wasRunning = false;
    unsigned long startMs = 0;
    size_t progressLogBytes = 0;
    char error[128] = "";  // Max error message length
    int command = U_FLASH;
    char label[16] = "firmware";  // "firmware" or "filesystem"
};

OtaState otaState;

// Runtime State
struct RuntimeState
{
    float pressure1 = 0.0f;
    float pressure2 = 0.0f;
    float voltage1 = 0.0f;
    float voltage2 = 0.0f;

    bool ads1115Found = false;
    bool tryingToConnect = false;
    unsigned long lastSensorUpdateMs = 0;

    char lastDeviceError[64] = "";  // Max error message length
    char lastLoggedDeviceError[64] = "";

    uint64_t totalRuntimeMs = 0;
    uint64_t runtimePersistedMs = 0;
    unsigned long runtimeStartMs = 0;
    bool runtimeRunning = false;
    bool runtimeActivePersisted = false;

    float udpPressure1History[UDP_AVERAGE_WINDOW] = {0};
    float udpPressure2History[UDP_AVERAGE_WINDOW] = {0};
    uint8_t udpPressureHistoryCount = 0;
    uint8_t udpPressureHistoryIndex = 0;
    float lastSentUdpPressure1 = NAN;
    float lastSentUdpPressure2 = NAN;
};

RuntimeState runtimeState;

// LED pins
#define LED_PIN 18
#define LED_PIN2 19

enum LedCode : uint8_t
{
    LED_CODE_OFF = 0,
    LED_CODE_ON,
    LED_CODE_BLINK_SLOW,
    LED_CODE_BLINK_MEDIUM,
    LED_CODE_BLINK_FAST,
    LED_CODE_DOUBLE_PULSE,
    LED_CODE_TRIPLE_PULSE,
};

LedCode led1Code = LED_CODE_OFF;
LedCode led2Code = LED_CODE_OFF;
unsigned long led1PatternStartMs = 0;
unsigned long led2PatternStartMs = 0;
unsigned long wifiPortalNoticeUntilMs = 0;

char adminPassword[65] = ADMIN_PASSWORD;  // Max 64 chars + null
char adminSessionToken[33] = "";  // 32 hex chars + null
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
void addMonitorEvent(const char *tag, const char *message);

// Inline wrapper for String compatibility (use sparingly to avoid heap fragmentation)
inline void addMonitorEvent(const char *tag, const String &message)
{
    addMonitorEvent(tag, message.c_str());
}

String jsonEscape(const String &input);
String formatMillisBrief(unsigned long ms);
void taskNetwork(void *parameter);
void handleConnected();

// Sensor reading helpers
void readAdcSensors(float &outVoltage1, float &outVoltage2, float &outPressure1, float &outPressure2);
void calculateDemoReadings(float &outVoltage1, float &outVoltage2, float &outPressure1, float &outPressure2);
void publishSensorReadings(float voltage1, float voltage2, float pressure1, float pressure2);
void logSensorData();

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

struct ConfigSnapshot
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
    char apPassword[MAX_AP_PASSWORD_LENGTH + 1];
    char adminPassword[65];
    char ssidSta[33];
    char passwordSta[65];
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
void lockMonitor();
void unlockMonitor();
void captureConfigSnapshot(ConfigSnapshot &snapshot);

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
    char lastDeviceError[64];
    float udpAvgPressure1;
    float udpAvgPressure2;
    bool udpAverageReady;
};

void captureSharedState(SharedStateSnapshot &snapshot);
void recordUdpPressureSample(float lowPressure, float highPressure);
void resetUdpPressureHistory();
bool shouldSendUdpUpdate(SharedStateSnapshot &snapshot, float &outLow, float &outHigh);
void commitUdpSentValues(float low, float high);
bool sendUDP(const SharedStateSnapshot &snapshot);

// SignalK state management helpers
void tickSignalK();
void setSignalKManualTarget(IPAddress newIp, uint16_t newServicePort, bool persist);
void clearSignalKTarget(bool persist);
void handleLEDControl(unsigned long now);

static inline void setStatusLed(uint8_t pin, bool on)
{
    // Status LEDs are wired active-low on this board.
    digitalWrite(pin, on ? LOW : HIGH);
}

static bool evalLedPattern(unsigned long now, unsigned long startMs,
                           unsigned long onMs, unsigned long offMs,
                           uint8_t pulses, unsigned long gapMs)
{
    if (pulses == 0)
        return false;

    const unsigned long pulseSlot = onMs + offMs;
    const unsigned long activeWindow = pulseSlot * pulses;
    const unsigned long cycle = activeWindow + gapMs;
    if (cycle == 0)
        return false;

    const unsigned long t = (now - startMs) % cycle;
    if (t >= activeWindow)
        return false;

    const unsigned long inSlot = t % pulseSlot;
    return inSlot < onMs;
}

static bool ledCodeToOn(LedCode code, unsigned long now, unsigned long startMs)
{
    switch (code)
    {
        case LED_CODE_ON:
            return true;
        case LED_CODE_BLINK_SLOW:
            return evalLedPattern(now, startMs, 80, 2920, 1, 0);
        case LED_CODE_BLINK_MEDIUM:
            return evalLedPattern(now, startMs, 300, 300, 1, 0);
        case LED_CODE_BLINK_FAST:
            return evalLedPattern(now, startMs, 120, 120, 1, 0);
        case LED_CODE_DOUBLE_PULSE:
            return evalLedPattern(now, startMs, 140, 160, 2, 1100);
        case LED_CODE_TRIPLE_PULSE:
            return evalLedPattern(now, startMs, 120, 120, 3, 1100);
        case LED_CODE_OFF:
        default:
            return false;
    }
}

static void updateLedCode(LedCode newCode, LedCode &currentCode, unsigned long &patternStartMs, unsigned long now)
{
    if (newCode != currentCode)
    {
        currentCode = newCode;
        patternStartMs = now;
    }
}

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

void lockMonitor()
{
    if (monitorMutex)
        xSemaphoreTake(monitorMutex, portMAX_DELAY);
}

void unlockMonitor()
{
    if (monitorMutex)
        xSemaphoreGive(monitorMutex);
}

void lockI2C()
{
    if (i2cMutex)
        xSemaphoreTake(i2cMutex, portMAX_DELAY);
}

void unlockI2C()
{
    if (i2cMutex)
        xSemaphoreGive(i2cMutex);
}

struct SignalKPrefsSnapshot
{
    IPAddress targetIp;
    uint16_t servicePort;
    uint16_t udpPort;
    bool manualTarget;
};

void captureSharedState(SharedStateSnapshot &snapshot)
{
    lockState();
    snapshot.pressure1 = runtimeState.pressure1;
    snapshot.pressure2 = runtimeState.pressure2;
    snapshot.voltage1 = runtimeState.voltage1;
    snapshot.voltage2 = runtimeState.voltage2;
    snapshot.signalkIp = signalkCtx.targetIp;
    snapshot.udpPort = signalkCtx.udpPort;
    snapshot.signalkServicePort = signalkCtx.servicePort;
    snapshot.adsFound = runtimeState.ads1115Found;
    snapshot.signalkAlive = signalkCtx.targetHealthy;
    snapshot.runtimeRunning = runtimeState.runtimeRunning;
    snapshot.runtimeActivePersisted = runtimeState.runtimeActivePersisted;
    snapshot.tryingToConnect = runtimeState.tryingToConnect;
    snapshot.wifiModeApSta = wifiModeApSta;
    snapshot.sensorMode = sensorMode;
    snapshot.signalkMaxAttempts = signalkMaxAttempts;
    strncpy(snapshot.lastDeviceError, runtimeState.lastDeviceError, sizeof(snapshot.lastDeviceError) - 1);
    snapshot.lastDeviceError[sizeof(snapshot.lastDeviceError) - 1] = '\0';
    snapshot.udpAvgPressure1 = 0.0f;
    snapshot.udpAvgPressure2 = 0.0f;
    snapshot.udpAverageReady = (runtimeState.udpPressureHistoryCount > 0);
    if (runtimeState.udpPressureHistoryCount > 0)
    {
        float lowSum = 0.0f;
        float highSum = 0.0f;
        for (uint8_t i = 0; i < runtimeState.udpPressureHistoryCount; i++)
        {
            lowSum += runtimeState.udpPressure1History[i];
            highSum += runtimeState.udpPressure2History[i];
        }
        snapshot.udpAvgPressure1 = lowSum / runtimeState.udpPressureHistoryCount;
        snapshot.udpAvgPressure2 = highSum / runtimeState.udpPressureHistoryCount;
    }
    unlockState();
}

void captureConfigSnapshot(ConfigSnapshot &snapshot)
{
    lockState();
    snapshot.maxPressure1 = maxPressure1;
    snapshot.minPressure1 = minPressure1;
    snapshot.minVoltage1 = minVoltage1;
    snapshot.maxVoltage1 = maxVoltage1;
    snapshot.maxPressure2 = maxPressure2;
    snapshot.minPressure2 = minPressure2;
    snapshot.minVoltage2 = minVoltage2;
    snapshot.maxVoltage2 = maxVoltage2;
    snapshot.wifiModeApSta = wifiModeApSta;
    snapshot.sensorMode = sensorMode;
    snapshot.signalkMaxAttempts = signalkMaxAttempts;
    strncpy(snapshot.apPassword, APpassword, sizeof(snapshot.apPassword) - 1);
    snapshot.apPassword[sizeof(snapshot.apPassword) - 1] = '\0';
    strncpy(snapshot.adminPassword, adminPassword, sizeof(snapshot.adminPassword) - 1);
    snapshot.adminPassword[sizeof(snapshot.adminPassword) - 1] = '\0';
    strncpy(snapshot.ssidSta, ssid_sta, sizeof(snapshot.ssidSta) - 1);
    snapshot.ssidSta[sizeof(snapshot.ssidSta) - 1] = '\0';
    strncpy(snapshot.passwordSta, password_sta, sizeof(snapshot.passwordSta) - 1);
    snapshot.passwordSta[sizeof(snapshot.passwordSta) - 1] = '\0';
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
    if (strlen(adminSessionToken) == 0)
        return false;

    unsigned long now = millis();
    if (adminSessionExpiresMs != 0 && (long)(now - adminSessionExpiresMs) >= 0)
    {
        adminSessionToken[0] = '\0';
        adminSessionExpiresMs = 0;
        return false;
    }

    if (!server.hasHeader("Cookie"))
        return false;

    String cookie = server.header("Cookie");
    String expected = "wm_admin_session=" + String(adminSessionToken);
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

    String token = makeSessionToken();
    strncpy(adminSessionToken, token.c_str(), sizeof(adminSessionToken) - 1);
    adminSessionToken[sizeof(adminSessionToken) - 1] = '\0';
    adminSessionExpiresMs = millis() + AUTH_SESSION_TTL_MS;
    server.sendHeader("Set-Cookie",
                      ("wm_admin_session=" + String(adminSessionToken) + "; Max-Age=1800; Path=/; HttpOnly; SameSite=Lax").c_str());
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

void addMonitorEvent(const char *tag, const char *message)
{
    unsigned long ms = millis();
    unsigned long totalSec = ms / 1000UL;
    unsigned long h = totalSec / 3600UL;
    unsigned long m = (totalSec % 3600UL) / 60UL;
    unsigned long s = totalSec % 60UL;

    lockMonitor();
    char *line = monitorLines[monitorSeqNext % MONITOR_BUFFER_SIZE];
    int written = snprintf(line, MONITOR_LINE_MAX_LEN, "[%02lu:%02lu:%02lu]", h, m, s);
    if (tag && strlen(tag) > 0 && written < MONITOR_LINE_MAX_LEN - 1)
        written += snprintf(line + written, MONITOR_LINE_MAX_LEN - written, " [%s]", tag);
    if (message && strlen(message) > 0 && written < MONITOR_LINE_MAX_LEN - 1)
        snprintf(line + written, MONITOR_LINE_MAX_LEN - written, " %s", message);
    monitorSeqNext++;
    if (monitorSeqNext - monitorSeqStart > MONITOR_BUFFER_SIZE)
        monitorSeqStart = monitorSeqNext - MONITOR_BUFFER_SIZE;
    unlockMonitor();
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
    lockState();
    int mode = sensorMode;
    unlockState();
    return mode == SENSOR_MODE_DEMO || mode == SENSOR_MODE_DEMO_UDP;
}

bool snapshotCanProduceReadings(const SharedStateSnapshot &snapshot)
{
    return snapshot.sensorMode == SENSOR_MODE_DEMO ||
           snapshot.sensorMode == SENSOR_MODE_DEMO_UDP ||
           snapshot.adsFound;
}

bool canProduceReadings()
{
    lockState();
    int mode = sensorMode;
    bool adsFound = runtimeState.ads1115Found;
    unlockState();
    return mode == SENSOR_MODE_DEMO || mode == SENSOR_MODE_DEMO_UDP || adsFound;
}

void handleLEDControl(unsigned long now)
{
    LedCode nextLed1 = LED_CODE_OFF;
    LedCode nextLed2 = LED_CODE_OFF;

    ConfigSnapshot config;
    captureConfigSnapshot(config);

    SignalKState skState = SK_OFF;
    bool signalKHealthy = false;
    bool tryingToConnectLocal = false;
    bool dataTimeout = false;
    bool adsFound = true;
    lockState();
    skState = signalkCtx.state;
    signalKHealthy = signalkCtx.targetHealthy;
    tryingToConnectLocal = runtimeState.tryingToConnect;
    dataTimeout = (runtimeState.lastSensorUpdateMs == 0 || (now - runtimeState.lastSensorUpdateMs) > 5000);
    adsFound = runtimeState.ads1115Found;
    unlockState();

    if (config.sensorMode == SENSOR_MODE_REAL && !adsFound)
    {
        // Highest priority fault indication: ADS1115 missing in real mode.
        nextLed1 = LED_CODE_BLINK_FAST;
        nextLed2 = LED_CODE_BLINK_FAST;
    }
    else if (config.wifiModeApSta == 0)
    {
        nextLed1 = LED_CODE_DOUBLE_PULSE;
        nextLed2 = LED_CODE_OFF;
    }
    else if (WiFi.status() != WL_CONNECTED || tryingToConnectLocal)
    {
        if (now < wifiPortalNoticeUntilMs)
            nextLed1 = LED_CODE_TRIPLE_PULSE;
        else
            nextLed1 = LED_CODE_BLINK_MEDIUM;
        nextLed2 = LED_CODE_OFF;
    }
    else
    {
        nextLed1 = LED_CODE_OFF;

        switch (skState)
        {
            case SK_SEARCHING:
                nextLed2 = LED_CODE_DOUBLE_PULSE;
                break;
            case SK_READY:
                if (signalKHealthy)
                    nextLed2 = dataTimeout ? LED_CODE_BLINK_FAST : LED_CODE_ON;
                else
                    nextLed2 = LED_CODE_BLINK_MEDIUM;
                break;
            case SK_OFF:
            default:
                nextLed2 = LED_CODE_OFF;
                break;
        }
    }

    updateLedCode(nextLed1, led1Code, led1PatternStartMs, now);
    updateLedCode(nextLed2, led2Code, led2PatternStartMs, now);

    setStatusLed(LED_PIN, ledCodeToOn(led1Code, now, led1PatternStartMs));
    setStatusLed(LED_PIN2, ledCodeToOn(led2Code, now, led2PatternStartMs));
}

bool shouldSendUdpData()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    if (WiFi.status() != WL_CONNECTED || snapshot.wifiModeApSta != 1) return false;
    return snapshotCanProduceReadings(snapshot);
}

void recordUdpPressureSample(float lowPressure, float highPressure)
{
    lockState();
    runtimeState.udpPressure1History[runtimeState.udpPressureHistoryIndex] = lowPressure;
    runtimeState.udpPressure2History[runtimeState.udpPressureHistoryIndex] = highPressure;
    runtimeState.udpPressureHistoryIndex = (runtimeState.udpPressureHistoryIndex + 1) % UDP_AVERAGE_WINDOW;
    if (runtimeState.udpPressureHistoryCount < UDP_AVERAGE_WINDOW)
        runtimeState.udpPressureHistoryCount++;
    unlockState();
}

void resetUdpPressureHistory()
{
    lockState();
    for (uint8_t i = 0; i < UDP_AVERAGE_WINDOW; i++)
    {
        runtimeState.udpPressure1History[i] = 0.0f;
        runtimeState.udpPressure2History[i] = 0.0f;
    }
    runtimeState.udpPressureHistoryCount = 0;
    runtimeState.udpPressureHistoryIndex = 0;
    runtimeState.lastSentUdpPressure1 = NAN;
    runtimeState.lastSentUdpPressure2 = NAN;
    unlockState();
}

// Returns true if there is a change worth sending
bool shouldSendUdpUpdate(SharedStateSnapshot &snapshot, float &outLow, float &outHigh)
{
    captureSharedState(snapshot);
    if (!snapshot.udpAverageReady) return false;

    float roundedLow = roundf(snapshot.udpAvgPressure1 * 10.0f) / 10.0f;
    float roundedHigh = roundf(snapshot.udpAvgPressure2 * 10.0f) / 10.0f;

    lockState();
    bool changed = isnan(runtimeState.lastSentUdpPressure1) || isnan(runtimeState.lastSentUdpPressure2) ||
                   roundedLow != runtimeState.lastSentUdpPressure1 || roundedHigh != runtimeState.lastSentUdpPressure2;
    unlockState();

    outLow = roundedLow;
    outHigh = roundedHigh;
    return changed;
}

// Called after successful UDP send to update the "last sent" state
void commitUdpSentValues(float low, float high)
{
    lockState();
    runtimeState.lastSentUdpPressure1 = low;
    runtimeState.lastSentUdpPressure2 = high;
    unlockState();
}

// Invalidate last sent UDP values to force resync after reconnection
void invalidateUdpLastSent()
{
    lockState();
    runtimeState.lastSentUdpPressure1 = NAN;
    runtimeState.lastSentUdpPressure2 = NAN;
    unlockState();
}

// ── SignalK State Management ─────────────────────────────────────────────────────

static void resetSignalKSearchLocked()
{
    signalkCtx.targetIp = IPAddress(0, 0, 0, 0);
    signalkCtx.manualTarget = false;
    signalkCtx.targetHealthy = false;
    signalkCtx.discoveryAttempts = 0;
    signalkCtx.lastDiscoveryAttemptMs = 0;
    signalkCtx.lastProbeMs = 0;
}

static void applySignalKTargetLocked(const IPAddress &newIp, uint16_t newServicePort)
{
    signalkCtx.targetIp = newIp;
    signalkCtx.servicePort = newServicePort;
    signalkCtx.targetHealthy = false;
    signalkCtx.discoveryAttempts = 0;
    signalkCtx.lastDiscoveryAttemptMs = 0;
    signalkCtx.lastProbeMs = 0;
    signalkCtx.state = isValidIP(newIp) ? SK_READY : SK_SEARCHING;
}

static void captureSignalKPrefsSnapshot(SignalKPrefsSnapshot &snapshot)
{
    lockState();
    snapshot.targetIp = signalkCtx.targetIp;
    snapshot.servicePort = signalkCtx.servicePort;
    snapshot.udpPort = signalkCtx.udpPort;
    snapshot.manualTarget = signalkCtx.manualTarget;
    unlockState();
}

static void persistSignalKTarget(const IPAddress &ip, uint16_t servicePort, bool manualTarget)
{
    if (prefsMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);
    prefs.begin("config", false);
    prefs.putUInt("signalkIp", (uint32_t)ip);
    prefs.putUInt("signalkServicePort", (uint32_t)servicePort);
    prefs.putBool("signalkManual", manualTarget);
    prefs.end();
    if (prefsMutex)
        xSemaphoreGive(prefsMutex);
}

void setSignalKManualTarget(IPAddress newIp, uint16_t newServicePort, bool persist)
{
    lockState();
    applySignalKTargetLocked(newIp, newServicePort);
    signalkCtx.manualTarget = isValidIP(newIp);
    signalkCtx.hadSuccessfulProbe = false;
    unlockState();

    if (persist)
        persistSignalKTarget(newIp, newServicePort, isValidIP(newIp));
}

void clearSignalKTarget(bool persist)
{
    uint16_t servicePort = 3000;
    bool shouldSearch = false;

    lockState();
    servicePort = signalkCtx.servicePort;
    shouldSearch = (wifiModeApSta == 1 && WiFi.status() == WL_CONNECTED);
    resetSignalKSearchLocked();
    signalkCtx.state = shouldSearch ? SK_SEARCHING : SK_OFF;
    unlockState();

    if (persist)
        persistSignalKTarget(IPAddress(0, 0, 0, 0), servicePort, false);
}

static bool discoverSignalKTarget(IPAddress &resolvedIp, uint16_t &resolvedPort)
{
    int n = MDNS.queryService("signalk-ws", "tcp");
    if (n <= 0)
        return false;

    String signalkHost = MDNS.hostname(0);
    resolvedPort = (MDNS.port(0) > 0) ? (uint16_t)MDNS.port(0) : 3000;
    resolvedIp = MDNS.address(0);

    LOG_INFF("mDNS found: host=%s port=%d addr=%s",
             signalkHost.c_str(),
             resolvedPort,
             resolvedIp.toString().c_str());

    if (!isValidIP(resolvedIp) && signalkHost.length() > 0)
    {
        LOG_INFF("Resolving hostname: %s", signalkHost.c_str());
        if (!WiFi.hostByName(signalkHost.c_str(), resolvedIp) || !isValidIP(resolvedIp))
        {
            String hostLocal = signalkHost.endsWith(".local") ? signalkHost : signalkHost + ".local";
            LOG_INFF("Resolving with .local: %s", hostLocal.c_str());
            if (!WiFi.hostByName(hostLocal.c_str(), resolvedIp) || !isValidIP(resolvedIp))
            {
                LOG_ERR("SignalK: could not resolve hostname or obtain a valid IP");
                addMonitorEvent("ERR", "SignalK hostname resolve failed for " + signalkHost);
                return false;
            }
        }
    }

    if (!isValidIP(resolvedIp))
    {
        LOG_ERR("SignalK: mDNS returned 0.0.0.0");
        addMonitorEvent("ERR", "SignalK mDNS returned 0.0.0.0");
        return false;
    }

    return true;
}

static bool probeSignalKTarget(const IPAddress &ip, uint16_t port)
{
    WiFiClient client;
    client.setTimeout(1);
    bool connected = client.connect(ip, port, SIGNALK_TCP_PROBE_TIMEOUT_MS);
    if (connected)
        client.stop();
    return connected;
}

void tickSignalK()
{
    int wifiMode = 0;
    int maxAttempts = 0;
    SignalKState state;
    IPAddress targetIp;
    uint16_t servicePort = 3000;
    bool manualTarget = false;
    bool hadSuccessfulProbe = false;
    bool targetHealthy = false;
    uint8_t discoveryAttempts = 0;
    unsigned long lastDiscoveryAttemptMs = 0;
    unsigned long lastProbeMs = 0;

    lockState();
    wifiMode = wifiModeApSta;
    maxAttempts = signalkMaxAttempts;
    state = signalkCtx.state;
    targetIp = signalkCtx.targetIp;
    servicePort = signalkCtx.servicePort;
    manualTarget = signalkCtx.manualTarget;
    hadSuccessfulProbe = signalkCtx.hadSuccessfulProbe;
    targetHealthy = signalkCtx.targetHealthy;
    discoveryAttempts = signalkCtx.discoveryAttempts;
    lastDiscoveryAttemptMs = signalkCtx.lastDiscoveryAttemptMs;
    lastProbeMs = signalkCtx.lastProbeMs;
    unlockState();

    if (wifiMode != 1 || WiFi.status() != WL_CONNECTED)
    {
        lockState();
        signalkCtx.state = SK_OFF;
        signalkCtx.targetHealthy = false;
        signalkCtx.discoveryAttempts = 0;
        signalkCtx.lastDiscoveryAttemptMs = 0;
        signalkCtx.lastProbeMs = 0;
        unlockState();
        return;
    }

    unsigned long now = millis();
    unsigned long discoveryInterval = (hadSuccessfulProbe && !manualTarget) ? SIGNALK_DISCOVERY_FAST_MS : SIGNALK_DISCOVERY_SLOW_MS;

    if (!isValidIP(targetIp))
    {
        lockState();
        signalkCtx.state = SK_SEARCHING;
        signalkCtx.targetHealthy = false;
        unlockState();

        if (now - lastDiscoveryAttemptMs < discoveryInterval)
            return;
        if (maxAttempts > 0 && discoveryAttempts >= (uint8_t)maxAttempts)
        {
            // Backoff largo antes de reiniciar la ventana de discovery
            if (now - lastDiscoveryAttemptMs < SIGNALK_DISCOVERY_SLOW_MS)
                return;

            lockState();
            signalkCtx.discoveryAttempts = 0;
            signalkCtx.lastDiscoveryAttemptMs = now;
            unlockState();
            return;
        }

        lockState();
        signalkCtx.lastDiscoveryAttemptMs = now;
        signalkCtx.discoveryAttempts++;
        uint8_t currentAttempts = signalkCtx.discoveryAttempts;
        unlockState();

        LOG_INFF("SignalK discovery attempt %d/%d...", currentAttempts, maxAttempts);

        IPAddress discoveredIp(0, 0, 0, 0);
        uint16_t discoveredPort = 3000;
        if (!discoverSignalKTarget(discoveredIp, discoveredPort))
        {
            addMonitorEvent("SK", "Discovery attempt " + String(currentAttempts) + " no match");
            yield();  // Feed WDT after blocking MDNS call
            return;
        }

        lockState();
        applySignalKTargetLocked(discoveredIp, discoveredPort);
        signalkCtx.manualTarget = false;
        uint16_t udpPortLocal = signalkCtx.udpPort;
        unlockState();
        persistSignalKTarget(discoveredIp, discoveredPort, false);
        LOG_INFF("SignalK found: %s:%d (UDP port %d)",
                 discoveredIp.toString().c_str(), discoveredPort, udpPortLocal);
        addMonitorEvent("SK", "Discovered " + discoveredIp.toString() + " service=" + String(discoveredPort) +
                              " udp=" + String(udpPortLocal));
        yield();  // Feed WDT after MDNS success
        targetIp = discoveredIp;
        servicePort = discoveredPort;
        state = SK_READY;
    }

    if (state != SK_READY)
    {
        lockState();
        signalkCtx.state = SK_READY;
        unlockState();
    }

    if (!manualTarget && hadSuccessfulProbe && !targetHealthy && now - lastDiscoveryAttemptMs >= SIGNALK_DISCOVERY_FAST_MS)
    {
        lockState();
        signalkCtx.state = SK_SEARCHING;
        signalkCtx.lastDiscoveryAttemptMs = now;
        unlockState();

        IPAddress discoveredIp(0, 0, 0, 0);
        uint16_t discoveredPort = servicePort;
        if (discoverSignalKTarget(discoveredIp, discoveredPort) &&
            (discoveredIp != targetIp || discoveredPort != servicePort))
        {
            lockState();
            applySignalKTargetLocked(discoveredIp, discoveredPort);
            signalkCtx.manualTarget = false;
            unlockState();

            persistSignalKTarget(discoveredIp, discoveredPort, false);
            LOG_INFF("SignalK target updated: %s:%d", discoveredIp.toString().c_str(), discoveredPort);
            addMonitorEvent("SK", "Rediscovered " + discoveredIp.toString() + " service=" + String(discoveredPort));
        }

        return;
    }

    unsigned long probeInterval = targetHealthy ? SIGNALK_PROBE_SLOW_MS : SIGNALK_PROBE_FAST_MS;
    if (now - lastProbeMs < probeInterval)
        return;

    lockState();
    signalkCtx.lastProbeMs = now;
    unlockState();

    String target = targetIp.toString() + ":" + String(servicePort);
    bool connected = probeSignalKTarget(targetIp, servicePort);
    if (connected)
    {
        if (!targetHealthy)
        {
            LOG_INFF("SignalK TCP OK: %s", target.c_str());
            addMonitorEvent("SK", "TCP probe OK " + target);
        }

        lockState();
        signalkCtx.state = SK_READY;
        signalkCtx.targetHealthy = true;
        signalkCtx.hadSuccessfulProbe = true;
        unlockState();
        yield();  // Feed WDT after TCP probe
        return;
    }

    if (targetHealthy)
    {
        LOG_ERRF("SignalK TCP probe failed: %s", target.c_str());
        addMonitorEvent("ERR", "SignalK TCP probe failed " + target);
    }

    lockState();
    signalkCtx.targetHealthy = false;
    if (!manualTarget && hadSuccessfulProbe)
        signalkCtx.state = SK_SEARCHING;
    unlockState();
    yield();  // Feed WDT after failed probe
}

bool isSystemRunning()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    // Use snapshot-based check instead of global read for consistency
    bool dataValid = snapshotCanProduceReadings(snapshot) && strlen(snapshot.lastDeviceError) == 0;
    if (!dataValid) return false;
    if (snapshot.runtimeRunning) return snapshot.pressure2 > RUNTIME_STOP_HIGH_PRESSURE_BAR;
    if (snapshot.runtimeActivePersisted) return snapshot.pressure2 > RUNTIME_STOP_HIGH_PRESSURE_BAR;
    return snapshot.pressure1 >= RUNTIME_START_LOW_PRESSURE_BAR;
}

uint64_t currentPartialRuntimeMs()
{
    lockState();
    bool running = runtimeState.runtimeRunning;
    unsigned long startMs = runtimeState.runtimeStartMs;
    unlockState();

    if (!running) return 0;
    return (uint64_t)(millis() - startMs);
}

uint64_t currentTotalRuntimeMs()
{
    lockState();
    uint64_t total = runtimeState.totalRuntimeMs;
    unlockState();
    return total + currentPartialRuntimeMs();
}

void updateRuntimeTracking()
{
    bool persistNeeded = false;
    unsigned long nowMs = millis();
    bool nowRunning = isSystemRunning();

    lockState();
    bool wasRunning = runtimeState.runtimeRunning;
    bool wasPersistedActive = runtimeState.runtimeActivePersisted;

    if (nowRunning && !wasRunning)
    {
        runtimeState.runtimeRunning = true;
        runtimeState.runtimeStartMs = nowMs;
        runtimeState.runtimeActivePersisted = true;
        persistNeeded = true;
    }
    else if (!nowRunning && wasRunning)
    {
        runtimeState.totalRuntimeMs += (uint64_t)(nowMs - runtimeState.runtimeStartMs);
        runtimeState.runtimeRunning = false;
        runtimeState.runtimeStartMs = 0;
        runtimeState.runtimeActivePersisted = false;
        runtimeState.runtimePersistedMs = runtimeState.totalRuntimeMs;
        persistNeeded = true;
    }
    else if (!nowRunning && !wasRunning && wasPersistedActive)
    {
        runtimeState.runtimeActivePersisted = false;
        persistNeeded = true;
    }
    else if (nowRunning)
    {
        uint64_t currentTotal = runtimeState.totalRuntimeMs + (uint64_t)(nowMs - runtimeState.runtimeStartMs);
        if (currentTotal >= runtimeState.runtimePersistedMs + (uint64_t)RUNTIME_CHECKPOINT_MS)
        {
            runtimeState.totalRuntimeMs = currentTotal;
            runtimeState.runtimeStartMs = nowMs;
            runtimeState.runtimePersistedMs = currentTotal;
            runtimeState.runtimeActivePersisted = true;
            persistNeeded = true;
        }
    }
    unlockState();

    if (persistNeeded)
        writeRuntimePreferences();
}

void updateDeviceError()
{
    const char *newError = "";
    int currentSensorMode = SENSOR_MODE_REAL;
    bool adsFound = false;
    float v1 = 0.0f;
    float v2 = 0.0f;
    float cfgMinVoltage1 = 0.0f;
    float cfgMinVoltage2 = 0.0f;
    unsigned long lastUpdate = 0;

    lockState();
    currentSensorMode = sensorMode;
    adsFound = runtimeState.ads1115Found;
    v1 = runtimeState.voltage1;
    v2 = runtimeState.voltage2;
    cfgMinVoltage1 = minVoltage1;
    cfgMinVoltage2 = minVoltage2;
    lastUpdate = runtimeState.lastSensorUpdateMs;
    unlockState();

    if (currentSensorMode == SENSOR_MODE_REAL && !adsFound)
    {
        newError = "ADS1115 not found";
    }
    else if (currentSensorMode == SENSOR_MODE_DEMO || currentSensorMode == SENSOR_MODE_DEMO_UDP)
    {
        newError = "";
    }
    else
    {
        if (v1 <= SENSOR_NO_SIGNAL_V && v2 <= SENSOR_NO_SIGNAL_V)
        {
            newError = "No signal from pressure sensors";
        }
        else if (v1 <= SENSOR_NO_SIGNAL_V)
        {
            newError = "No signal from pressure sensor 1";
        }
        else if (v2 <= SENSOR_NO_SIGNAL_V)
        {
            newError = "No signal from pressure sensor 2";
        }
        else if (v1 < (cfgMinVoltage1 - SENSOR_DISCONNECT_TOLERANCE_V) &&
                 v2 < (cfgMinVoltage2 - SENSOR_DISCONNECT_TOLERANCE_V))
        {
            newError = "Pressure sensors disconnected";
        }
        else if (v1 < (cfgMinVoltage1 - SENSOR_DISCONNECT_TOLERANCE_V))
        {
            newError = "Pressure sensor 1 disconnected";
        }
        else if (v2 < (cfgMinVoltage2 - SENSOR_DISCONNECT_TOLERANCE_V))
        {
            newError = "Pressure sensor 2 disconnected";
        }
        else if (lastUpdate == 0 || millis() - lastUpdate > 5000)
        {
            newError = "No fresh sensor data";
        }
        else
        {
            newError = "";
        }
    }

    // Update error under lock (it's a char array, so we want atomic write)
    lockState();
    strncpy(runtimeState.lastDeviceError, newError, sizeof(runtimeState.lastDeviceError) - 1);
    runtimeState.lastDeviceError[sizeof(runtimeState.lastDeviceError) - 1] = '\0';
    unlockState();
}

bool configIsValid()
{
    ConfigSnapshot config;
    captureConfigSnapshot(config);

    bool voltagesOk =
        config.minVoltage1 >= 0.0f && config.maxVoltage1 <= 5.0f && config.minVoltage1 < config.maxVoltage1 &&
        config.minVoltage2 >= 0.0f && config.maxVoltage2 <= 5.0f && config.minVoltage2 < config.maxVoltage2;

    bool pressuresOk =
        config.minPressure1 >= 0.0f && config.maxPressure1 <= 150.0f && config.minPressure1 < config.maxPressure1 &&
        config.minPressure2 >= 0.0f && config.maxPressure2 <= 150.0f && config.minPressure2 < config.maxPressure2;

    bool wifiModeOk = (config.wifiModeApSta == 0 || config.wifiModeApSta == 1);
    bool sensorModeOk = (config.sensorMode >= SENSOR_MODE_REAL && config.sensorMode <= SENSOR_MODE_DEMO_UDP);
    bool apPasswordOk = strlen(config.apPassword) >= 8 && strlen(config.apPassword) < MAX_AP_PASSWORD_LENGTH;
    bool adminPasswordOk = strlen(config.adminPassword) >= 8 && strlen(config.adminPassword) < MAX_AP_PASSWORD_LENGTH;

    return voltagesOk && pressuresOk && wifiModeOk && sensorModeOk && apPasswordOk && adminPasswordOk;
}

// ── OLED helpers ────────────────────────────────────────────────────────────────

void drawScreen(bool showNetworkPage)
{
    if (!displayAvailable) return;
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);

    lockI2C();

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(SW_VERSION);
    display.setCursor(30, 0);
    if (snapshot.sensorMode == SENSOR_MODE_DEMO)
        display.print("DEMO");
    else if (snapshot.sensorMode == SENSOR_MODE_DEMO_UDP)
        display.print("DMO+U");
    else
        display.print(snapshot.adsFound ? "ADS OK" : "NO ADS");
    display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, WHITE);

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
        display.print(String(snapshot.pressure1, 1));
        display.setTextSize(1);
        display.setCursor(52, 16);
        display.print("b1");

        display.setTextSize(2);
        display.setCursor(0, 32);
        display.print(String(snapshot.pressure2, 1));
        display.setTextSize(1);
        display.setCursor(52, 36);
        display.print("b2");
    }

    display.display();
    unlockI2C();

    LOG_VRBF("OLED: %s", showNetworkPage ? "NET" : "PRESSURE");
}

void drawErrorScreen(const char *errorMessage, bool showNetworkPage)
{
    if (!displayAvailable) return;

    lockI2C();

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(SW_VERSION);
    display.setCursor(30, 0);
    display.print("NO ADS");
    display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, WHITE);

    if (showNetworkPage)
    {
        // Network page
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
        // Error page
        display.setTextSize(1);
        display.setCursor(0, 14);
        display.print("ERROR:");
        display.setCursor(0, 24);
        display.print(errorMessage);
        display.setCursor(0, 36);
        display.print("Check sensor");
    }
    display.display();
    unlockI2C();
}

// ── NVS Functions ───────────────────────────────────────────────────────────────

void writeConfigPreferences()
{
    if (!configIsValid())
    {
        LOG_ERR("Invalid config. Preferences not saved.");
        return;
    }

    ConfigSnapshot config;
    captureConfigSnapshot(config);

    if (prefsMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);

    prefs.begin("config", false);
    prefs.putFloat("minV1", config.minVoltage1);
    prefs.putFloat("maxV1", config.maxVoltage1);
    prefs.putFloat("minP1", config.minPressure1);
    prefs.putFloat("maxP1", config.maxPressure1);
    prefs.putFloat("minV2", config.minVoltage2);
    prefs.putFloat("maxV2", config.maxVoltage2);
    prefs.putFloat("minP2", config.minPressure2);
    prefs.putFloat("maxP2", config.maxPressure2);
    prefs.putInt("wifiMode", config.wifiModeApSta);
    prefs.putInt("sensorMode", config.sensorMode);
    prefs.putString("apPassword", config.apPassword);
    prefs.putString("adminPassword", config.adminPassword);
    prefs.putInt("signalkMaxAttempts", config.signalkMaxAttempts);
    prefs.end();

    if (prefsMutex)
        xSemaphoreGive(prefsMutex);

    LOG_INF("Config NVS saved");
}

void writeSignalKPreferences()
{
    SignalKPrefsSnapshot signalk;
    captureSignalKPrefsSnapshot(signalk);

    if (prefsMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);

    prefs.begin("config", false);
    prefs.putUInt("signalkIp", (uint32_t)signalk.targetIp);
    prefs.putUInt("signalkServicePort", signalk.servicePort);
    prefs.putUInt("outPort", signalk.udpPort);
    prefs.putBool("signalkManual", signalk.manualTarget);
    prefs.end();

    if (prefsMutex)
        xSemaphoreGive(prefsMutex);

    LOG_INF("SignalK NVS saved");
}

void writePreferences()
{
    writeConfigPreferences();
    writeSignalKPreferences();
}

void writeRuntimePreferences()
{
    uint64_t runtimeToSave = 0;
    bool runtimeActiveToSave = false;

    lockState();
    runtimeToSave = runtimeState.totalRuntimeMs;
    runtimeActiveToSave = runtimeState.runtimeActivePersisted;
    unlockState();

    if (prefsMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);

    prefs.begin("config", false);
    prefs.putULong64("totalRuntimeMs", runtimeToSave);
    prefs.putBool("runtimeActive", runtimeActiveToSave);
    prefs.end();

    if (prefsMutex)
        xSemaphoreGive(prefsMutex);

    LOG_INF("Runtime NVS saved");
}

void readPreferences()
{
    // Note: This is called in setup() before prefsMutex exists
    // Use prefsMutex if available (runtime re-reads), otherwise proceed without it
    bool useMutex = (prefsMutex != nullptr);
    if (useMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);
    
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
        prefs.putUInt("signalkServicePort", 3000);
        prefs.putBool("signalkManual", false);
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
    String apPass = prefs.getString("apPassword", AP_PASSWORD);
    strncpy(APpassword, apPass.c_str(), sizeof(APpassword) - 1);
    APpassword[sizeof(APpassword) - 1] = '\0';
    String adminPass = prefs.getString("adminPassword", ADMIN_PASSWORD);
    strncpy(adminPassword, adminPass.c_str(), sizeof(adminPassword) - 1);
    adminPassword[sizeof(adminPassword) - 1] = '\0';
    uint16_t storedUdpPort = prefs.getUInt("outPort", DEFAULT_UDP_PORT);
    uint16_t storedServicePort = prefs.getUInt("signalkServicePort", 3000);
    bool storedManualTarget = prefs.getBool("signalkManual", false);
    signalkMaxAttempts = prefs.getInt("signalkMaxAttempts", DEFAULT_SIGNALK_MAX_ATTEMPTS);

    lockState();
    runtimeState.totalRuntimeMs = prefs.getULong64("totalRuntimeMs", 0);
    runtimeState.runtimePersistedMs = runtimeState.totalRuntimeMs;
    runtimeState.runtimeActivePersisted = prefs.getBool("runtimeActive", false);
    unlockState();

    IPAddress storedSignalKIp(0, 0, 0, 0);
    {
        uint32_t savedIp = prefs.getUInt("signalkIp", 0);
        if (savedIp != 0)
        {
            storedSignalKIp = IPAddress(savedIp);
            LOG_INFF("SignalK IP from NVS: %s", storedSignalKIp.toString().c_str());
        }
    }
    prefs.end();

    if (useMutex)
        xSemaphoreGive(prefsMutex);

    lockState();
    signalkCtx.targetIp = storedSignalKIp;
    signalkCtx.servicePort = storedServicePort;
    signalkCtx.udpPort = storedUdpPort;
    signalkCtx.manualTarget = storedManualTarget && isValidIP(storedSignalKIp);
    signalkCtx.targetHealthy = false;
    signalkCtx.hadSuccessfulProbe = false;
    signalkCtx.discoveryAttempts = 0;
    signalkCtx.lastDiscoveryAttemptMs = 0;
    signalkCtx.lastProbeMs = 0;
    if (wifiModeApSta != 1)
        signalkCtx.state = SK_OFF;
    else if (isValidIP(storedSignalKIp))
        signalkCtx.state = SK_READY;
    else
        signalkCtx.state = SK_SEARCHING;
    unlockState();

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
        strncpy(APpassword, AP_PASSWORD, sizeof(APpassword) - 1);
        strncpy(adminPassword, ADMIN_PASSWORD, sizeof(adminPassword) - 1);
        lockState();
        runtimeState.totalRuntimeMs = 0;
        runtimeState.runtimePersistedMs = 0;
        runtimeState.runtimeActivePersisted = false;
        runtimeState.runtimeRunning = false;
        runtimeState.runtimeStartMs = 0;
        unlockState();
        writePreferences();
        writeRuntimePreferences();
    }
}

bool sendUDP(const SharedStateSnapshot &snapshot)
{
    char udpmessage[256];
    snprintf(udpmessage, sizeof(udpmessage),
            "{\"updates\":[{\"$source\":\"ESP32.watermaker\","
            "\"values\":[{\"path\":\"environment.watermaker.pressure.high\","
            "\"value\":%.1f},{\"path\":\"environment.watermaker.pressure.low\","
            "\"value\":%.1f}]}]}",
            snapshot.udpAvgPressure2, snapshot.udpAvgPressure1);
    Udp.beginPacket(IPAddress(255, 255, 255, 255), snapshot.udpPort);
    Udp.write((const uint8_t *)udpmessage, strlen(udpmessage));
    int udpResult = Udp.endPacket();
    if (udpResult == 1)
    {
        LOG_VRBF("UDP sent avg: HP=%.1f LP=%.1f", snapshot.udpAvgPressure2, snapshot.udpAvgPressure1);
        addMonitorEvent("UDP", "Broadcast OK 255.255.255.255:" + String(snapshot.udpPort) +
                               " hp=" + String(snapshot.udpAvgPressure2, 1) + " lp=" + String(snapshot.udpAvgPressure1, 1));
        return true;
    }
    else
    {
        LOG_ERRF("UDP send failed: result=%d", udpResult);
        addMonitorEvent("ERR", "UDP broadcast failed 255.255.255.255:" + String(snapshot.udpPort));
        return false;
    }
}

void taskNetwork(void *parameter)
{
    unsigned long lastSendTime = 0;

    for (;;)
    {
        bool otaBlocked = false;
        lockState();
        otaBlocked = otaState.inProgress;
        unlockState();

        if (otaBlocked || Update.isRunning())
        {
            vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_DELAY_MS));
            continue;
        }

        tickSignalK();

        ConfigSnapshot config;
        captureConfigSnapshot(config);

        if (config.wifiModeApSta == 1)
        {
            bool isTryingToConnect = false;
            unsigned long connStartTime = 0;
            lockState();
            isTryingToConnect = runtimeState.tryingToConnect;
            connStartTime = startTime;
            unlockState();

            if (WiFi.status() != WL_CONNECTED)
            {
                if (!isTryingToConnect)
                {
                    LOG_INF("WiFi lost - reconnecting");
                    WiFi.begin();
                    lockState();
                    startTime = millis();
                    runtimeState.tryingToConnect = true;
                    unlockState();
                }
                else if (millis() - connStartTime > 7000)
                {
                    LOG_INF("WiFi reconnect retry");
                    WiFi.disconnect(false, false);
                    WiFi.begin();
                    lockState();
                    startTime = millis();
                    unlockState();
                }
            }
            else if (isTryingToConnect)
            {
                LOG_INF("WiFi reconnected");
                handleConnected();
                // handleConnected() already sets tryingToConnect = false
            }
            else
            {
                if (millis() - lastSendTime >= UDP_SEND_INTERVAL_MS)
                {
                    lastSendTime = millis();
                    if (shouldSendUdpData())
                    {
                        SharedStateSnapshot snapshot;
                        float low, high;
                        if (shouldSendUdpUpdate(snapshot, low, high))
                        {
                            if (sendUDP(snapshot))
                            {
                                commitUdpSentValues(low, high);
                            }
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_DELAY_MS));
    }
}

void readAdcSensors(float &outVoltage1, float &outVoltage2, float &outPressure1, float &outPressure2)
{
    ConfigSnapshot config;
    captureConfigSnapshot(config);

    int16_t adc0 = 0;
    int16_t adc1 = 0;
    lockI2C();
    adc0 = ads.readADC_SingleEnded(0);
    adc1 = ads.readADC_SingleEnded(1);
    unlockI2C();

    outVoltage1 = adcToVoltage(adc0);
    outVoltage2 = adcToVoltage(adc1);
    outPressure1 = mapToPressure(outVoltage1, config.minVoltage1, config.maxVoltage1, config.minPressure1, config.maxPressure1);
    outPressure2 = mapToPressure(outVoltage2, config.minVoltage2, config.maxVoltage2, config.minPressure2, config.maxPressure2);
}

void calculateDemoReadings(float &outVoltage1, float &outVoltage2, float &outPressure1, float &outPressure2)
{
    ConfigSnapshot config;
    captureConfigSnapshot(config);

    float t = millis() / 1000.0f;
    float lowBase = 1.8f + 0.25f * sinf(t * 0.31f) + 0.12f * sinf(t * 1.07f);
    float highBase = 55.0f + 1.1f * sinf(t * 0.22f) + 0.4f * sinf(t * 1.17f);

    outVoltage1 = config.minVoltage1 + 0.15f;
    outVoltage2 = config.minVoltage2 + 0.15f;
    outPressure1 = constrain(lowBase, config.minPressure1, min(config.maxPressure1, 4.0f));
    outPressure2 = constrain(highBase, 0.0f, 70.0f);
}

void publishSensorReadings(float voltage1, float voltage2, float pressure1, float pressure2)
{
    lockState();
    runtimeState.voltage1 = voltage1;
    runtimeState.voltage2 = voltage2;
    runtimeState.pressure1 = pressure1;
    runtimeState.pressure2 = pressure2;
    runtimeState.lastSensorUpdateMs = millis();
    unlockState();

    recordUdpPressureSample(pressure1, pressure2);
}

void logSensorData()
{
    static unsigned long lastVoltageLogMs = 0;
    float loggedVoltage1 = 0.0f;
    float loggedVoltage2 = 0.0f;
    char loggedDeviceError[64];
    bool shouldLogDeviceError = false;

    lockState();
    loggedVoltage1 = runtimeState.voltage1;
    loggedVoltage2 = runtimeState.voltage2;
    strncpy(loggedDeviceError, runtimeState.lastDeviceError, sizeof(loggedDeviceError) - 1);
    loggedDeviceError[sizeof(loggedDeviceError) - 1] = '\0';
    shouldLogDeviceError = (DEBUG >= DEBUG_LEVEL_INFO &&
                            strcmp(runtimeState.lastDeviceError, runtimeState.lastLoggedDeviceError) != 0);
    if (shouldLogDeviceError)
    {
        strncpy(runtimeState.lastLoggedDeviceError, runtimeState.lastDeviceError, sizeof(runtimeState.lastLoggedDeviceError) - 1);
        runtimeState.lastLoggedDeviceError[sizeof(runtimeState.lastLoggedDeviceError) - 1] = '\0';
    }
    unlockState();

    if (DEBUG >= 2 && millis() - lastVoltageLogMs >= 2000)
    {
        lastVoltageLogMs = millis();
        LOG_INFF("Sensor voltages: V1=%.3fV V2=%.3fV", loggedVoltage1, loggedVoltage2);
    }

    if (shouldLogDeviceError)
    {
        if (strlen(loggedDeviceError) > 0)
        {
            LOG_INFF("Device error: %s (V1=%.3fV V2=%.3fV)", loggedDeviceError, loggedVoltage1, loggedVoltage2);
            addMonitorEvent("ERR", ("Device: " + String(loggedDeviceError)).c_str());
        }
        else
        {
            LOG_INFF("Device error cleared (V1=%.3fV V2=%.3fV)", loggedVoltage1, loggedVoltage2);
            addMonitorEvent("INF", "Device error cleared");
        }
    }
}

void updatePressureReadings()
{
    ConfigSnapshot config;
    captureConfigSnapshot(config);

    bool adsFound = false;
    lockState();
    adsFound = runtimeState.ads1115Found;
    unlockState();

    if (config.sensorMode == SENSOR_MODE_REAL)
    {
        if (!adsFound)
        {
            updateDeviceError();
            return;
        }

        float voltage1, voltage2, pressure1, pressure2;
        readAdcSensors(voltage1, voltage2, pressure1, pressure2);
        publishSensorReadings(voltage1, voltage2, pressure1, pressure2);
    }
    else
    {
        float voltage1, voltage2, pressure1, pressure2;
        calculateDemoReadings(voltage1, voltage2, pressure1, pressure2);
        publishSensorReadings(voltage1, voltage2, pressure1, pressure2);
    }

    updateDeviceError();
    logSensorData();
}

static void handleOtaUpload()
{
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        lockState();
        otaState.error[0] = '\0';
        otaState.progressLogBytes = 0;
        otaState.inProgress = true;
        char label[16];
        strncpy(label, otaState.label, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        unlockState();

        uint32_t slotMax = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        Serial.printf("[INF] OTA %s upload start: %s  slot_max=%u bytes  stack_free=%u\n",
                      label, upload.filename.c_str(), slotMax,
                      uxTaskGetStackHighWaterMark(NULL));

        int command = U_FLASH;
        lockState();
        command = otaState.command;
        unlockState();

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, command))
        {
            lockState();
            strncpy(otaState.error, Update.errorString(), sizeof(otaState.error) - 1);
            otaState.error[sizeof(otaState.error) - 1] = '\0';
            otaState.inProgress = false;
            unlockState();
            Serial.printf("[ERR] OTA begin failed: %s\n", Update.errorString());
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        char currentError[128];
        lockState();
        strncpy(currentError, otaState.error, sizeof(currentError) - 1);
        currentError[sizeof(currentError) - 1] = '\0';
        unlockState();

        if (currentError[0] == '\0')
        {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
                lockState();
                strncpy(otaState.error, Update.errorString(), sizeof(otaState.error) - 1);
                otaState.error[sizeof(otaState.error) - 1] = '\0';
                otaState.inProgress = false;
                unlockState();
                Serial.printf("[ERR] OTA write failed at %u: %s\n",
                              Update.progress(), Update.errorString());
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        char currentError[128];
        lockState();
        strncpy(currentError, otaState.error, sizeof(currentError) - 1);
        currentError[sizeof(currentError) - 1] = '\0';
        unlockState();

        Serial.printf("[INF] OTA upload END: received=%u  written=%u\n",
                      upload.totalSize, Update.progress());

        if (currentError[0] == '\0')
        {
            if (!Update.end(true))
            {
                lockState();
                strncpy(otaState.error, Update.errorString(), sizeof(otaState.error) - 1);
                otaState.error[sizeof(otaState.error) - 1] = '\0';
                otaState.inProgress = false;
                unlockState();
                Serial.printf("[ERR] OTA end failed: %s\n", Update.errorString());
            }
            else
            {
                Serial.println("[INF] OTA end OK - rebooting");
                lockState();
                otaState.inProgress = false;
                unlockState();
            }
        }
        else
        {
            lockState();
            otaState.inProgress = false;
            unlockState();
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        Update.end();
        lockState();
        strncpy(otaState.error, "aborted", sizeof(otaState.error) - 1);
        otaState.error[sizeof(otaState.error) - 1] = '\0';
        otaState.inProgress = false;
        unlockState();
        Serial.printf("[ERR] OTA aborted at %u bytes\n", Update.progress());
    }
}

static const char *jsonBool(bool value)
{
    return value ? "true" : "false";
}

static String signalKIpText(const IPAddress &ip)
{
    return isValidIP(ip) ? ip.toString() : String("0.0.0.0");
}

static void appendRuntimeSummaryJson(String &json,
                                     const SharedStateSnapshot &snapshot,
                                     bool dataValid,
                                     bool systemRunning,
                                     bool udpAllowed)
{
    json += "\"adsFound\":";
    json += jsonBool(snapshot.adsFound);
    json += ",\"dataValid\":";
    json += jsonBool(dataValid);
    json += ",\"systemRunning\":";
    json += jsonBool(systemRunning);
    json += ",\"partialRuntimeMs\":";
    json += String((unsigned long long)currentPartialRuntimeMs());
    json += ",\"totalRuntimeMs\":";
    json += String((unsigned long long)currentTotalRuntimeMs());
    json += ",\"udpEnabled\":";
    json += jsonBool(udpAllowed);
}

static void appendPressureReadingsJson(String &json,
                                       const SharedStateSnapshot &snapshot,
                                       bool includeVoltages)
{
    json += "\"pressure1\":";
    json += String(snapshot.pressure1, 3);
    json += ",\"pressure2\":";
    json += String(snapshot.pressure2, 3);
    if (includeVoltages)
    {
        json += ",\"voltage1\":";
        json += String(snapshot.voltage1, 3);
        json += ",\"voltage2\":";
        json += String(snapshot.voltage2, 3);
    }
}

static void appendSignalKSummaryJson(String &json,
                                     const SharedStateSnapshot &snapshot,
                                     bool includeServicePort)
{
    json += "\"signalkIp\":\"";
    json += signalKIpText(snapshot.signalkIp);
    json += "\",\"signalkAlive\":";
    json += jsonBool(snapshot.signalkAlive);
    if (includeServicePort)
    {
        json += ",\"signalkServicePort\":";
        json += String(snapshot.signalkServicePort);
    }
    json += ",\"udpPort\":";
    json += String(snapshot.udpPort);
}

static void appendSettingsFieldsJson(String &json,
                                     const ConfigSnapshot &config,
                                     const SharedStateSnapshot &snapshot,
                                     uint64_t totalRuntimeMsValue)
{
    json += "\"maxPressure1\":" + String(config.maxPressure1, 3) + ",";
    json += "\"minPressure1\":" + String(config.minPressure1, 3) + ",";
    json += "\"minVdc1\":" + String(config.minVoltage1, 3) + ",";
    json += "\"maxVdc1\":" + String(config.maxVoltage1, 3) + ",";
    json += "\"maxPressure2\":" + String(config.maxPressure2, 3) + ",";
    json += "\"minPressure2\":" + String(config.minPressure2, 3) + ",";
    json += "\"minVdc2\":" + String(config.minVoltage2, 3) + ",";
    json += "\"maxVdc2\":" + String(config.maxVoltage2, 3) + ",";
    json += "\"modo\":" + String(config.wifiModeApSta) + ",";
    json += "\"sensorMode\":" + String(config.sensorMode) + ",";
    json += "\"signalkMaxAttempts\":" + String(config.signalkMaxAttempts) + ",";
    json += "\"outPort\":" + String(snapshot.udpPort) + ",";
    json += "\"signalkIp\":\"" + signalKIpText(snapshot.signalkIp) + "\",";
    json += "\"APpassword\":\"" + jsonEscape(config.apPassword) + "\",";
    json += "\"adminPassword\":\"" + jsonEscape(config.adminPassword) + "\",";
    json += "\"totalRuntimeMs\":" + String((unsigned long long)totalRuntimeMsValue);
}

static DeviceSettingsPayload buildCurrentSettingsPayload()
{
    SharedStateSnapshot snapshot;
    ConfigSnapshot config;
    captureSharedState(snapshot);
    captureConfigSnapshot(config);

    return {
        config.maxPressure1,
        config.minPressure1,
        config.minVoltage1,
        config.maxVoltage1,
        config.maxPressure2,
        config.minPressure2,
        config.minVoltage2,
        config.maxVoltage2,
        config.wifiModeApSta,
        config.sensorMode,
        config.signalkMaxAttempts,
        snapshot.udpPort,
        signalKIpText(snapshot.signalkIp),
        config.apPassword,
        config.adminPassword,
        currentTotalRuntimeMs(),
    };
}

// ── HTTP handlers ───────────────────────────────────────────────────────────────

void Event_pressure()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    String jsonResponse;
    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    bool dataValid = snapshotCanProduceReadings(snapshot) && strlen(snapshot.lastDeviceError) == 0;
    bool systemRunning = isSystemRunning();
    bool udpAllowed = shouldSendUdpData();
    jsonResponse.reserve(320);
    jsonResponse = "{";
    appendPressureReadingsJson(jsonResponse, snapshot, true);
    jsonResponse += ",\"rssi\":" + String(rssi) + ",";
    appendRuntimeSummaryJson(jsonResponse, snapshot, dataValid, systemRunning, udpAllowed);
    jsonResponse += ",\"sensorMode\":\"" + String(sensorModeName(snapshot.sensorMode)) + "\",";
    jsonResponse += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    appendSignalKSummaryJson(jsonResponse, snapshot, false);
    jsonResponse += ",";
    jsonResponse += "\"error\":\"" + jsonEscape(snapshot.lastDeviceError) + "\"";
    jsonResponse += "}";

    LOG_VRBF("Event_pressure JSON: %s", jsonResponse.c_str());

    server.send(200, "application/json", jsonResponse);
}

void Event_State()
{
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    String json;
    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    bool dataValid = snapshotCanProduceReadings(snapshot) && strlen(snapshot.lastDeviceError) == 0;
    bool systemRunning = isSystemRunning();
    bool udpAllowed = shouldSendUdpData();

    json.reserve(512);
    json = "{";
    json += "\"device\":\"pressure-sensor-esp32\",";
    json += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    json += "\"hostname\":\"" + String(hostName) + "\",";
    json += "\"wifiConnected\":";
    json += jsonBool(WiFi.status() == WL_CONNECTED);
    json += ",";
    json += "\"localIp\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"sensorMode\":\"" + String(sensorModeName(snapshot.sensorMode)) + "\",";
    appendRuntimeSummaryJson(json, snapshot, dataValid, systemRunning, udpAllowed);
    json += ",";
    json += "\"deviceError\":\"" + jsonEscape(snapshot.lastDeviceError) + "\",";
    appendPressureReadingsJson(json, snapshot, true);
    json += ",";
    appendSignalKSummaryJson(json, snapshot, true);
    json += ",";
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

    DeviceSettingsPayload payload = buildCurrentSettingsPayload();

    // Only overwrite fields that are actually present in the request
    // This prevents missing fields from being reset to 0
    String arg;
    
    arg = server.arg("maxPressure1");
    if (arg.length() > 0) payload.maxPressure1 = arg.toFloat();
    
    arg = server.arg("minPressure1");
    if (arg.length() > 0) payload.minPressure1 = arg.toFloat();
    
    arg = server.arg("minVdc1");
    if (arg.length() > 0) payload.minVoltage1 = arg.toFloat();
    
    arg = server.arg("maxVdc1");
    if (arg.length() > 0) payload.maxVoltage1 = arg.toFloat();
    
    arg = server.arg("maxPressure2");
    if (arg.length() > 0) payload.maxPressure2 = arg.toFloat();
    
    arg = server.arg("minPressure2");
    if (arg.length() > 0) payload.minPressure2 = arg.toFloat();
    
    arg = server.arg("minVdc2");
    if (arg.length() > 0) payload.minVoltage2 = arg.toFloat();
    
    arg = server.arg("maxVdc2");
    if (arg.length() > 0) payload.maxVoltage2 = arg.toFloat();
    
    arg = server.arg("modo");
    if (arg.length() > 0) payload.wifiModeApSta = arg.toInt();
    
    arg = server.arg("sensorMode");
    if (arg.length() > 0) payload.sensorMode = arg.toInt();
    
    arg = server.arg("signalkMaxAttempts");
    if (arg.length() > 0) payload.signalkMaxAttempts = arg.toInt();
    
    arg = server.arg("outPort");
    if (arg.length() > 0) payload.outPort = (unsigned int)arg.toInt();
    
    arg = server.arg("signalkIp");
    if (arg.length() > 0) payload.signalkIp = arg;
    
    arg = server.arg("APpassword");
    if (arg.length() > 0) payload.apPassword = arg;
    
    arg = server.arg("adminPassword");
    if (arg.length() > 0) payload.adminPassword = arg;

    applySettingsPayload(payload, false);
}

void appendSettingsJson(String &json)
{
    SharedStateSnapshot snapshot;
    ConfigSnapshot config;
    captureSharedState(snapshot);
    captureConfigSnapshot(config);
    appendSettingsFieldsJson(json, config, snapshot, currentTotalRuntimeMs());
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
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
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

    uint16_t newUdpPort = (payload.outPort >= 1024 && payload.outPort <= 65535)
                              ? payload.outPort
                              : DEFAULT_UDP_PORT;

    int newSignalkMaxAttempts = (payload.signalkMaxAttempts >= 0 && payload.signalkMaxAttempts <= 60)
                                    ? payload.signalkMaxAttempts
                                    : DEFAULT_SIGNALK_MAX_ATTEMPTS;

    int oldWifiMode = 0;
    int oldSensorMode = 0;

    lockState();
    oldWifiMode = wifiModeApSta;
    oldSensorMode = sensorMode;

    minPressure1 = payload.minPressure1;
    maxPressure1 = payload.maxPressure1;
    minVoltage1 = payload.minVoltage1;
    maxVoltage1 = payload.maxVoltage1;
    minPressure2 = payload.minPressure2;
    maxPressure2 = payload.maxPressure2;
    minVoltage2 = payload.minVoltage2;
    maxVoltage2 = payload.maxVoltage2;

    if (payload.apPassword.length() > 0)
    {
        strncpy(APpassword, payload.apPassword.c_str(), sizeof(APpassword) - 1);
        APpassword[sizeof(APpassword) - 1] = '\0';
    }
    strncpy(adminPassword, payload.adminPassword.c_str(), sizeof(adminPassword) - 1);
    adminPassword[sizeof(adminPassword) - 1] = '\0';

    wifiModeApSta = payload.wifiModeApSta;
    sensorMode = payload.sensorMode;
    signalkMaxAttempts = newSignalkMaxAttempts;

    signalkCtx.udpPort = newUdpPort;

    runtimeState.totalRuntimeMs = payload.totalRuntimeMs;
    runtimeState.runtimePersistedMs = payload.totalRuntimeMs;
    runtimeState.runtimeActivePersisted = runtimeState.runtimeRunning;

    if (isValidIP(parsedIp))
    {
        applySignalKTargetLocked(parsedIp, signalkCtx.servicePort);
        signalkCtx.manualTarget = true;
        signalkCtx.hadSuccessfulProbe = false;
    }
    else
    {
        resetSignalKSearchLocked();
        signalkCtx.servicePort = (signalkCtx.servicePort == 0) ? 3000 : signalkCtx.servicePort;
        signalkCtx.state = (wifiModeApSta == 1 && WiFi.status() == WL_CONNECTED) ? SK_SEARCHING : SK_OFF;
    }
    unlockState();

    updateDeviceError();
    writeConfigPreferences();
    writeSignalKPreferences();
    writeRuntimePreferences();

    bool shouldRestart = (oldWifiMode != payload.wifiModeApSta);

    if (oldSensorMode != payload.sensorMode)
    {
        lockState();
        runtimeState.pressure1 = 0.0f;
        runtimeState.pressure2 = 0.0f;
        runtimeState.lastSensorUpdateMs = 0;
        unlockState();

        resetUdpPressureHistory();
        invalidateUdpLastSent();
        updatePressureReadings();
    }
    else
    {
        invalidateUdpLastSent();
    }

    drawScreen(false);

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

    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);

    payload.maxPressure1 = doc["maxPressure1"] | payload.maxPressure1;
    payload.minPressure1 = doc["minPressure1"] | payload.minPressure1;
    payload.minVoltage1 = doc["minVdc1"] | payload.minVoltage1;
    payload.maxVoltage1 = doc["maxVdc1"] | payload.maxVoltage1;
    payload.maxPressure2 = doc["maxPressure2"] | payload.maxPressure2;
    payload.minPressure2 = doc["minPressure2"] | payload.minPressure2;
    payload.minVoltage2 = doc["minVdc2"] | payload.minVoltage2;
    payload.maxVoltage2 = doc["maxVdc2"] | payload.maxVoltage2;
    payload.wifiModeApSta = doc["modo"] | payload.wifiModeApSta;
    payload.sensorMode = doc["sensorMode"] | payload.sensorMode;
    payload.signalkMaxAttempts = doc["signalkMaxAttempts"] | payload.signalkMaxAttempts;
    payload.outPort = doc["outPort"] | payload.outPort;
    payload.signalkIp = String((const char *)(doc["signalkIp"] | payload.signalkIp.c_str()));
    payload.apPassword = String((const char *)(doc["APpassword"] | payload.apPassword.c_str()));
    payload.adminPassword = String((const char *)(doc["adminPassword"] | payload.adminPassword.c_str()));
    payload.totalRuntimeMs = doc["totalRuntimeMs"] | payload.totalRuntimeMs;
    return true;
}

void Event_ApiSettingsGet()
{
    sendSettingsJson();
}

void Event_ApiSettingsPost()
{
    DeviceSettingsPayload payload = buildCurrentSettingsPayload();
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
    SharedStateSnapshot snapshot;
    ConfigSnapshot config;
    captureSharedState(snapshot);
    captureConfigSnapshot(config);

    String htmlContent = configPageHTML;
    htmlContent.replace("{maxPressure1}", String(config.maxPressure1, 3));
    htmlContent.replace("{minPressure1}", String(config.minPressure1, 3));
    htmlContent.replace("{minVdc1}", String(config.minVoltage1, 3));
    htmlContent.replace("{maxVdc1}", String(config.maxVoltage1, 3));
    htmlContent.replace("{maxPressure2}", String(config.maxPressure2, 3));
    htmlContent.replace("{minPressure2}", String(config.minPressure2, 3));
    htmlContent.replace("{minVdc2}", String(config.minVoltage2, 3));
    htmlContent.replace("{maxVdc2}", String(config.maxVoltage2, 3));
    htmlContent.replace("{wifiMode0}", (config.wifiModeApSta == 0) ? "selected" : "");
    htmlContent.replace("{wifiMode1}", (config.wifiModeApSta == 1) ? "selected" : "");
    htmlContent.replace("{sensorMode0}", (config.sensorMode == SENSOR_MODE_REAL) ? "selected" : "");
    htmlContent.replace("{sensorMode1}", (config.sensorMode == SENSOR_MODE_DEMO) ? "selected" : "");
    htmlContent.replace("{sensorMode2}", (config.sensorMode == SENSOR_MODE_DEMO_UDP) ? "selected" : "");
    htmlContent.replace("{APpassword}", String(config.apPassword));
    htmlContent.replace("{adminPassword}", String(config.adminPassword));
    htmlContent.replace("{signalkMaxAttempts}", String(config.signalkMaxAttempts));
    htmlContent.replace("{outPort}", String(snapshot.udpPort));
    htmlContent.replace("{signalkIp}", isValidIP(snapshot.signalkIp) ? snapshot.signalkIp.toString() : String("0.0.0.0"));
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
    SharedStateSnapshot snapshot;
    ConfigSnapshot config;
    captureSharedState(snapshot);
    captureConfigSnapshot(config);

    String jsonResponse;
    jsonResponse.reserve(128);
    jsonResponse = "{";
    jsonResponse += "\"minPressure1\":" + String(config.minPressure1, 3) + ",";
    jsonResponse += "\"maxPressure1\":" + String(config.maxPressure1, 3) + ",";
    jsonResponse += "\"minPressure2\":" + String(config.minPressure2, 3) + ",";
    jsonResponse += "\"maxPressure2\":" + String(config.maxPressure2, 3) + ",";
    jsonResponse += "\"sensorMode\":\"" + String(sensorModeName(snapshot.sensorMode)) + "\",";
    jsonResponse += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    jsonResponse += "\"error\":\"" + jsonEscape(snapshot.lastDeviceError) + "\"";
    jsonResponse += "}";

    LOG_VRBF("Event_State JSON: %s", jsonResponse.c_str());

    server.send(200, "application/json", jsonResponse);
}

void Event_SetSignalKIp()
{
    String ipArg = server.arg("ip");
    ipArg.trim();

    // Accept optional ip:port format — strip port if present
    uint16_t explicitPort = 0;
    int colonIdx = ipArg.indexOf(':');
    if (colonIdx >= 0)
    {
        String portStr = ipArg.substring(colonIdx + 1);
        ipArg = ipArg.substring(0, colonIdx);
        int p = portStr.toInt();
        if (p > 0 && p <= 65535)
            explicitPort = (uint16_t)p;
    }

    IPAddress newIp(0, 0, 0, 0);
    if (ipArg.length() > 0 && ipArg != "0.0.0.0")
    {
        if (!newIp.fromString(ipArg))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_ip\"}");
            return;
        }
    }

    uint16_t currentServicePort = 3000;
    lockState();
    currentServicePort = signalkCtx.servicePort;
    unlockState();
    if (explicitPort > 0)
        currentServicePort = explicitPort;

    if (isValidIP(newIp))
    {
        setSignalKManualTarget(newIp, currentServicePort, true);
        invalidateUdpLastSent();
        LOG_INFF("SignalK IP set from UI: %s", newIp.toString().c_str());
        addMonitorEvent("SK", "IP set manually from UI: " + newIp.toString());
    }
    else
    {
        clearSignalKTarget(true);
        invalidateUdpLastSent();
        LOG_INF("SignalK IP cleared from UI");
        addMonitorEvent("SK", "IP cleared from UI");
    }

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json",
                String("{\"ok\":true,\"ip\":\"") +
                (isValidIP(newIp) ? newIp.toString() : String("0.0.0.0")) + "\"}");
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
    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);
    String json = "{";
    json += "\"firmwareVersion\":\"" + String(SW_VERSION) + "\",";
    json += "\"sensorMode\":\"" + String(sensorModeName(snapshot.sensorMode)) + "\",";
    json += "\"wifiConnected\":";
    json += jsonBool(WiFi.status() == WL_CONNECTED);
    json += ",";
    json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
    json += "\"hostname\":\"" + String(hostName) + "\",";
    json += "\"localIp\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi\":" + String((WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0) + ",";
    appendSignalKSummaryJson(json, snapshot, true);
    json += ",\"adsFound\":";
    json += jsonBool(snapshot.adsFound);
    json += ",";
    appendPressureReadingsJson(json, snapshot, true);
    json += ",";
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
    json += "\"deviceError\":\"" + jsonEscape(snapshot.lastDeviceError) + "\"";
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

    // Copy buffer content under lock to avoid race conditions
    struct MonitorLineCopy {
        unsigned long seq;
        char text[MONITOR_LINE_MAX_LEN];
    };
    static MonitorLineCopy local[MONITOR_BUFFER_SIZE];
    unsigned int count = 0;

    lockMonitor();
    if (since < monitorSeqStart)
        since = monitorSeqStart;
    unsigned long until = monitorSeqNext;

    for (unsigned long seq = since; seq < until && count < MONITOR_BUFFER_SIZE; seq++)
    {
        local[count].seq = seq;
        strncpy(local[count].text, monitorLines[seq % MONITOR_BUFFER_SIZE], MONITOR_LINE_MAX_LEN - 1);
        local[count].text[MONITOR_LINE_MAX_LEN - 1] = '\0';
        count++;
    }
    unlockMonitor();

    // Build response outside lock
    String text;
    text.reserve(count * 64);  // Pre-allocate estimate
    for (unsigned int i = 0; i < count; i++)
    {
        text += String(local[i].seq);
        text += "\t";
        text += local[i].text;
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

    MDNS.end();
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

    lockState();
    strncpy(ssid_sta, WiFi.SSID().c_str(), sizeof(ssid_sta) - 1);
    ssid_sta[sizeof(ssid_sta) - 1] = '\0';
    strncpy(password_sta, WiFi.psk().c_str(), sizeof(password_sta) - 1);
    password_sta[sizeof(password_sta) - 1] = '\0';
    startTime = millis();
    runtimeState.tryingToConnect = false;
    unlockState();

    wifiPortalNoticeUntilMs = 0;

    SharedStateSnapshot snapshot;
    captureSharedState(snapshot);

    if (!isValidIP(snapshot.signalkIp))
        LOG_INF("SignalK IP unknown - waiting for periodic discovery");
    else
        LOG_INFF("SignalK IP reused: %s", snapshot.signalkIp.toString().c_str());

    LOG_INFF("UDP port %d", snapshot.udpPort);

    // Force UDP resync after reconnection
    invalidateUdpLastSent();
    resetUdpPressureHistory();
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
    
    if (prefsMutex)
        xSemaphoreTake(prefsMutex, portMAX_DELAY);
    prefs.begin("config", false);
    prefs.clear();
    prefs.end();
    if (prefsMutex)
        xSemaphoreGive(prefsMutex);
    
    adminSessionToken[0] = '\0';
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

    // LED
    pinMode(LED_PIN2, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    setStatusLed(LED_PIN, false);
    setStatusLed(LED_PIN2, false);

    // Create state mutex - critical for concurrency safety
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr)
    {
        LOG_ERR("CRITICAL: Failed to create state mutex. System cannot operate safely.");
        // Halt - system cannot operate without mutex protection
        while (true) {
            delay(1000);
            Serial.println("[FATAL] No state mutex - system halted");
        }
    }
    LOG_INF("State mutex created successfully");

    monitorMutex = xSemaphoreCreateMutex();
    if (monitorMutex == nullptr)
    {
        LOG_ERR("WARNING: Failed to create monitor mutex. Monitor buffer will be unprotected.");
    }
    else
    {
        LOG_INF("Monitor mutex created successfully");
    }

    // Create I2C mutex for bus protection
    i2cMutex = xSemaphoreCreateMutex();
    if (i2cMutex == nullptr)
    {
        LOG_ERR("CRITICAL: Failed to create I2C mutex.");
        while (true)
        {
            delay(1000);
            Serial.println("[FATAL] No I2C mutex - system halted");
        }
    }
    LOG_INF("I2C mutex created successfully");

    // Create preferences mutex for NVS protection
    prefsMutex = xSemaphoreCreateMutex();
    if (prefsMutex == nullptr)
    {
        LOG_ERR("WARNING: Failed to create prefs mutex. NVS operations will be unprotected.");
        // Continue without prefs mutex - not fatal but not ideal
    }
    else
    {
        LOG_INF("Prefs mutex created successfully");
    }

    readPreferences();

    // Init I2C before probing any device on the bus
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    // Init ADS1115. Try all valid addresses because ADDR pin wiring changes it.
    LOG_INF("Initializing ADS1115...");
    const uint8_t adsCandidates[] = {0x48, 0x49, 0x4A, 0x4B};
    bool adsDetected = false;
    uint8_t adsDetectedAddr = 0;

    lockI2C();
    for (uint8_t i = 0; i < sizeof(adsCandidates); i++)
    {
        uint8_t addr = adsCandidates[i];
        if (ads.begin(addr, &Wire))
        {
            adsDetected = true;
            adsDetectedAddr = addr;
            break;
        }
    }

    if (!adsDetected)
    {
        // Helpful diagnostic when ADS1115 is wired but on a different bus/pins.
        for (uint8_t addr = 0x03; addr <= 0x77; addr++)
        {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0)
            {
                LOG_INFF("I2C device detected at 0x%02X", addr);
            }
        }
    }
    unlockI2C();

    if (!adsDetected)
    {
        LOG_ERR("ADS1115 not found. Check wiring, I2C pins, and address (0x48-0x4B).");
        lockState();
        runtimeState.ads1115Found = false;
        runtimeState.pressure1 = 0.0f;
        runtimeState.pressure2 = 0.0f;
        unlockState();
        addMonitorEvent("ERR", "ADS1115 not found");
    }
    else
    {
        LOG_INFF("ADS1115 found at 0x%02X", adsDetectedAddr);
        lockState();
        runtimeState.ads1115Found = true;
        unlockState();
        addMonitorEvent("SYS", "ADS1115 detected");
    }

    // Init OLED - try to detect and initialize
    lockI2C();
    Wire.beginTransmission(OLED_ADDR);
    bool oledPresent = (Wire.endTransmission() == 0);

    if (oledPresent)
    {
        display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
        display.clearDisplay();
        display.display();
        unlockI2C();
        displayAvailable = true;
        LOG_INF("Display OK");
    }
    else
    {
        unlockI2C();
        displayAvailable = false;
        LOG_ERR("OLED display not found at address 0x3C");
        addMonitorEvent("ERR", "OLED not found");
    }

    // Now that monitorMutex exists, we can add monitor events
    addMonitorEvent("SYS", "Boot " + String(SW_VERSION));

    // Show startup screen (only if display is available)
    if (displayAvailable)
    {
        lockI2C();
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print("MAB WaterMaker");
        display.setCursor(0, 12);
        display.print("Starting...");
        display.setCursor(0, 26);
        display.print(SW_VERSION);

        bool demoMode = false;
        bool adsFound = false;
        lockState();
        demoMode = (sensorMode == SENSOR_MODE_DEMO || sensorMode == SENSOR_MODE_DEMO_UDP);
        adsFound = runtimeState.ads1115Found;
        unlockState();

        if (demoMode)
        {
            display.setCursor(0, 38);
            display.print(sensorMode == SENSOR_MODE_DEMO ? "Demo" : "Demo+UDP");
        }
        else if (!adsFound)
        {
            display.setCursor(0, 38);
            display.print("No ADS1115");
        }

        display.display();
        unlockI2C();
    }
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
        WiFi.softAP(hostName, APpassword);
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
        WiFi.softAP(hostName, APpassword);
        WiFi.setHostname(hostName);
        startTime = millis();
        lockState();
        runtimeState.tryingToConnect = true;
        unlockState();
        wifiManager.setConfigPortalTimeout(60);
        wifiManager.setConnectTimeout(15);
        if (WiFi.psk().length() == 0)
        {
            LOG_INF("No WiFi credentials - starting portal");
            wifiPortalNoticeUntilMs = millis() + 20000UL;
            bool connected = wifiManager.autoConnect(hostName, APpassword);
            WiFi.mode(WIFI_AP_STA);
            delay(200);
            WiFi.softAP(hostName, APpassword);

            if (connected && WiFi.status() == WL_CONNECTED)
            {
                handleConnected();
                // handleConnected() already sets tryingToConnect = false
            }
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
                SharedStateSnapshot snapshot;
                captureSharedState(snapshot);
                server.send(200, "text/plain",
                    isValidIP(snapshot.signalkIp) ? snapshot.signalkIp.toString() : "0.0.0.0"); });
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
            String currentError;
            lockState();
            currentError = otaState.error;
            unlockState();
            if (currentError.length() > 0)
                server.send(200, "text/html",
                    "<h2 style='color:red'>Update FAILED: " + currentError + "</h2>");
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
            lockState();
            otaState.command = U_FLASH;
            strncpy(otaState.label, "firmware", sizeof(otaState.label) - 1);
            otaState.label[sizeof(otaState.label) - 1] = '\0';
            unlockState();
            handleOtaUpload();
        });
    server.on("/updatefs", HTTP_POST,
        []()
        {
            if (!ensureAuthenticated()) return;
            String currentError;
            lockState();
            currentError = otaState.error;
            unlockState();
            if (currentError.length() > 0)
                server.send(200, "text/html",
                    "<h2 style='color:red'>Filesystem update FAILED: " + currentError + "</h2>");
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
            lockState();
            otaState.command = U_SPIFFS;
            strncpy(otaState.label, "filesystem", sizeof(otaState.label) - 1);
            otaState.label[sizeof(otaState.label) - 1] = '\0';
            unlockState();
            handleOtaUpload();
        });

    Update.onProgress([](size_t done, size_t /*total*/)
    {
        bool shouldLog = false;
        lockState();
        if (done - otaState.progressLogBytes >= 100 * 1024)
        {
            otaState.progressLogBytes = done;
            shouldLog = true;
        }
        unlockState();

        if (shouldLog)
            Serial.printf("[INF] HTTP OTA: %u KB written\n", (unsigned)(done / 1024));
    });

    server.begin();
    xTaskCreatePinnedToCore(taskNetwork, "taskNetwork", 8192, nullptr, 1, &networkTaskHandle, 1);
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
    static unsigned long lastPageChangeMs = 0;
    static bool showNetworkPage = false;

    bool otaNow = Update.isRunning();
    bool otaWasRunningLocal = false;

    lockState();
    otaWasRunningLocal = otaState.wasRunning;
    unlockState();

    if (otaNow && !otaWasRunningLocal)
    {
        lockState();
        otaState.startMs = millis();
        otaState.wasRunning = true;
        unlockState();

        Serial.printf("[INF] HTTP OTA started - size: %u bytes, stack free: %u\n",
                      Update.size(), uxTaskGetStackHighWaterMark(NULL));
    }
    else if (!otaNow && otaWasRunningLocal)
    {
        unsigned long elapsed = 0;
        String otaError;
        lockState();
        elapsed = millis() - otaState.startMs;
        otaError = otaState.error;
        otaState.wasRunning = false;
        unlockState();

        if (otaError.length() > 0)
            Serial.printf("[ERR] HTTP OTA FAILED after %lums: %s\n", elapsed, otaError.c_str());
        else
            Serial.printf("[INF] HTTP OTA finished OK in %lums\n", elapsed);
    }

    unsigned long now = millis();
    handleLEDControl(now);

    bool otaBlocked = false;
    lockState();
    otaBlocked = otaState.inProgress;
    unlockState();

    if (otaBlocked || Update.isRunning())
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

    // Page switching: alternate between pressure and network pages every 5 seconds
    if (millis() - lastPageChangeMs >= DISPLAY_PAGE_MS)
    {
        lastPageChangeMs = millis();
        showNetworkPage = !showNetworkPage;
    }

    // Display refresh at DISPLAY_REFRESH_MS rate
    if (millis() - lastDisplayRefreshMs >= DISPLAY_REFRESH_MS)
    {
        lastDisplayRefreshMs = millis();
        if (canProduceReadings())
        {
            drawScreen(showNetworkPage);
        }
        else
        {
            drawErrorScreen("ADS1115 NOT FOUND", showNetworkPage);
        }
    }

    // HTTP server
    server.handleClient();
    delay(1);
}
