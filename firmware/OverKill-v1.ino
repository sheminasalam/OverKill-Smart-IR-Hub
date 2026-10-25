#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <SPI.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <time.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "captive_dns.h"
// >>> v11 INTEGRATION
#include "webui_html.h"

struct DatabaseHeader;
struct StorageRecordHeader;
struct FormatIntent;
enum class StableReadResult : uint8_t;
enum class PayloadVerifyResult : uint8_t;
enum class FormatIntentFindResult : uint8_t;
enum class DeviceLookupResult : uint8_t;
struct HueCommandMapEntry;
struct RuleStep;
struct RuleTriggerStored;
struct StoredRule;
struct LegacyStoredRule;
struct RuleTriggerRuntime;
struct RuleSlot;
struct StoredSchedule;
struct AutomationScheduleState;
struct AutomationJob;

#ifndef _IRREMOTEESP8266_VERSION
  #error "IRremoteESP8266 version macro not found. Install IRremoteESP8266 v2.9.0."
#endif
static_assert(
  _IRREMOTEESP8266_VERSION == _IRREMOTEESP8266_VERSION_VAL(2, 9, 0),
  "Stage 1 requires IRremoteESP8266 v2.9.0"
);

#define FIRMWARE_NAME    "ESP8266 Universal IR Hub"
#define FIRMWARE_VERSION "Overkill v2.0"

#define STAGE1_SERIAL_DEBUG 1

bool serialEnabled = true;

#if STAGE1_SERIAL_DEBUG
  #define DBG_PRINT(...)      do { if (serialEnabled) Serial.print(__VA_ARGS__); } while (0)
  #define DBG_PRINTLN(...)    do { if (serialEnabled) Serial.println(__VA_ARGS__); } while (0)
  #define DBG_PRINTLN_EMPTY() do { if (serialEnabled) Serial.println(); } while (0)
#else
  #define DBG_PRINT(...)      do {} while (0)
  #define DBG_PRINTLN(...)    do {} while (0)
  #define DBG_PRINTLN_EMPTY() do {} while (0)
#endif

// ============================================================
// FINAL GPIO ASSIGNMENT
// ============================================================

constexpr uint8_t PIN_LEARN_BUTTON = 0;
constexpr uint8_t PIN_FLASH_MISO   = 12;
constexpr uint8_t PIN_FLASH_MOSI   = 13;
constexpr uint8_t PIN_FLASH_SCLK   = 14;
constexpr uint8_t PIN_FLASH_CS     = 3;

constexpr uint8_t PIN_IR_TX_DEFAULT      = 4;
constexpr uint8_t PIN_IR_RX_DEFAULT      = 5;
constexpr uint8_t PIN_STATUS_LED_DEFAULT = 2;
constexpr uint8_t PIN_DS18B20_DEFAULT    = 16;

uint8_t pinIrTx      = PIN_IR_TX_DEFAULT;
uint8_t pinIrRx      = PIN_IR_RX_DEFAULT;
uint8_t pinStatusLed = PIN_STATUS_LED_DEFAULT;
uint8_t pinDs18b20   = PIN_DS18B20_DEFAULT;

static_assert(PIN_FLASH_MISO != PIN_FLASH_MOSI, "GPIO overlap");
static_assert(PIN_FLASH_MISO != PIN_FLASH_SCLK, "GPIO overlap");
static_assert(PIN_FLASH_MISO != PIN_FLASH_CS, "GPIO overlap");
static_assert(PIN_FLASH_MOSI != PIN_FLASH_SCLK, "GPIO overlap");
static_assert(PIN_FLASH_MOSI != PIN_FLASH_CS, "GPIO overlap");
static_assert(PIN_FLASH_SCLK != PIN_FLASH_CS, "GPIO overlap");

// ============================================================
// IR RECEIVER CONFIGURATION
// ============================================================

constexpr uint16_t RAW_WAVEFORM_CAPACITY = 1024;
constexpr uint16_t IRRECV_BUFFER_SIZE = RAW_WAVEFORM_CAPACITY + 1;
constexpr uint16_t CAPTURE_TIMEOUT_MS = 50;
constexpr bool     SAVE_BUFFER         = false;

static_assert(CAPTURE_TIMEOUT_MS <= UINT8_MAX,
              "CAPTURE_TIMEOUT_MS exceeds IRremoteESP8266 uint8_t limit");

constexpr uint32_t LEARN_SESSION_TIMEOUT_MS = 15000UL;

uint16_t txFrequencyHz = 38000;
uint8_t  txRepeatCount = 1;
uint16_t txRepeatGapMs = 100;

constexpr uint16_t MIN_TX_FREQUENCY_HZ = 10000;
constexpr uint16_t MAX_TX_FREQUENCY_HZ = UINT16_MAX;

constexpr uint16_t TX_WDT_FEED_INTERVAL = 16;
static_assert(TX_WDT_FEED_INTERVAL > 0,
              "TX_WDT_FEED_INTERVAL must be greater than zero");

IRrecv *irrecvPtr = nullptr;
IRsend *irsendPtr = nullptr;
decode_results results;
bool irReceiverActive = false;

enum class IrEngineState : uint8_t {
  IDLE,
  LEARNING,
  TRANSMITTING
};
IrEngineState irState = IrEngineState::IDLE;

static uint16_t lastRaw[RAW_WAVEFORM_CAPACITY];
uint16_t lastRawLen = 0;
uint32_t lastRawCrc32 = 0;

// Shared staging buffer used by both the IR capture path (during learn)
// and the storage record path (during load/save/rename). The two
// subsystems are active at different times, so a single aligned buffer
// replaces what used to be two separate 2 KB buffers (stagingRaw plus
// storageRecordBuffer). Sized for the maximum storage record: header
// + name + 1024-word waveform. The exact value is verified by a
// static_assert() in the storage section further down.
static constexpr size_t SHARED_RECORD_BUFFER_BYTES = 2123;
alignas(4) static uint8_t storageRecordBuffer[SHARED_RECORD_BUFFER_BYTES];

// Metadata captured at the same moment as a successful IR learn. This keeps
// the command's transmit parameters tied to the learned waveform instead of
// whatever happens to be in the mutable TX globals when the save occurs.
static uint16_t lastLearnCarrierHz = 38000U;
static uint8_t  lastLearnRepeatCount = 1U;
static uint16_t lastLearnRepeatGapMs = 100U;
static bool     lastLearnMetadataValid = false;

struct IrStatistics {
  uint32_t learnAttempts = 0;
  uint32_t successfulLearns = 0;
  uint32_t learnTimeouts = 0;
  uint32_t learnOverflows = 0;
  uint32_t malformedCaptures = 0;
  uint32_t storeFailures = 0;
  uint32_t transmissionsStarted = 0;
  uint32_t transmissionsCompleted = 0;
  uint32_t transmissionRejected = 0;
  uint16_t maxCapturedRawLen = 0;
  uint16_t maxStoredRawLen = 0;
  uint32_t maxWaveformDurationUs = 0;
};
IrStatistics irStats;

#if STAGE1_SERIAL_DEBUG
uint32_t minFreeHeap = UINT32_MAX;
#endif

// ============================================================
// STAGE 3 HARDWARE SERVICES
// ============================================================

constexpr bool     DS18B20_ENABLED_DEFAULT = true;
constexpr uint32_t DS18B20_SAMPLE_INTERVAL_DEFAULT_MS = 10000UL;
constexpr uint32_t DS18B20_SAMPLE_INTERVAL_MIN_MS = 1000UL;
constexpr uint32_t DS18B20_SAMPLE_INTERVAL_MAX_MS = 3600000UL;
constexpr uint32_t DS18B20_CONVERSION_WAIT_MS = 800UL;
constexpr uint32_t DS18B20_RETRY_INTERVAL_MS = 5000UL;

struct DS18B20State {
  bool enabled = DS18B20_ENABLED_DEFAULT;
  bool connected = false;
  bool conversionPending = false;
  bool temperatureValid = false;
  int16_t rawTemperature = 0;
  int16_t calibrationCentiDegrees = 0;
  int32_t calibratedTemperatureCentiDegrees = 0;
  uint32_t conversionStartedMs = 0;
  uint32_t sampleIntervalMs = DS18B20_SAMPLE_INTERVAL_DEFAULT_MS;
  uint32_t nextActionMs = 0;
  uint32_t lastReadMs = 0;
  uint32_t sampleSequence = 0;
};
DS18B20State ds18b20State;

enum class StatusLedMode : uint8_t { OFF, LEARNING, ERROR };
StatusLedMode statusLedMode = StatusLedMode::OFF;
bool statusLedOn = false;
bool statusLedPulsePending = false;
bool statusLedErrorActive = false;
bool statusLedManualErrorActive = false;
bool statusLedStorageErrorActive = false;
bool statusLedWifiErrorActive = false;
bool statusLedMqttErrorActive = false;
uint32_t statusLedNextToggleMs = 0;
uint32_t statusLedPulseUntilMs = 0;


enum class WifiRecoveryState : uint8_t { OFF, AP };
enum class DiscoveryMode : uint8_t { NATIVE_HA = 0, CUSTOM = 1 };
DiscoveryMode discoveryMode = DiscoveryMode::CUSTOM;
void mqttSetDiscoveryMode(DiscoveryMode mode);
void mqttClearAllDiscoveryRetained();
void mqttClearAlexaCatalogRetained();
WifiRecoveryState wifiRecoveryState = WifiRecoveryState::OFF;
uint32_t wifiRecoveryNotConnectedSinceMs = 0UL;
char     wifiRecoveryApSsid[24] = {0};
uint32_t wifiRecoveryApConnectedAtMs = 0UL;   // 0 = not yet connected
constexpr uint32_t WIFI_AP_GRACE_PERIOD_MS = 300000UL;   // 5 minutes

constexpr uint32_t WIFI_RECOVERY_TRIGGER_MS = 30000UL;

constexpr uint32_t STATUS_LED_LEARN_BLINK_MS = 300UL;
constexpr uint32_t STATUS_LED_ERROR_BLINK_MS = 1000UL;
constexpr uint32_t STATUS_LED_TX_PULSE_MS = 120UL;

void statusLedInitialize();
void statusLedSetError(bool active);
void statusLedSetStorageError(bool active);
void statusLedSetWifiError(bool active);
void statusLedSetMqttError(bool active);
void statusLedStartLearning();
void statusLedStopLearning();
void statusLedPulseTransmit();
void statusLedService();
void statusLedServiceLearningWait();
void statusLedServiceWifiErrorDelay();   // >>> v1.0 delayed WiFi error LED
void statusLedPrintStatus();
void rebootDevice();

// ============================================================
// WI-FI FOUNDATION
// ============================================================

constexpr size_t WIFI_SSID_MAX_LENGTH = 32U;
constexpr size_t WIFI_PASSWORD_MAX_LENGTH = 63U;
constexpr size_t WIFI_HOSTNAME_MAX_LENGTH = 32U;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000UL;

enum class WifiState : uint8_t {
  UNCONFIGURED, CONNECTING, CONNECTED, DISCONNECTED
};

struct WifiConfiguration {
  char ssid[WIFI_SSID_MAX_LENGTH + 1U] = {0};
  char password[WIFI_PASSWORD_MAX_LENGTH + 1U] = {0};
  char hostname[WIFI_HOSTNAME_MAX_LENGTH + 1U] = {0};
};

struct StaticIpConfig {
  bool      enabled = false;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};
StaticIpConfig staticIpConfig;
static bool lastAppliedStaticIpMode = false;

// ── WebUI hub name (shared by WebUI and MQTT discovery) ─────
constexpr size_t WEBUI_HUB_NAME_MAX = 32U;
char webUiHubName[WEBUI_HUB_NAME_MAX + 1U] = "Universal IR Hub";


WifiConfiguration wifiConfig;
WifiState wifiState = WifiState::UNCONFIGURED;
bool wifiConnectAttemptActive = false;
// ── Wi-Fi recovery AP state (v1.0) ───────────────────────────
bool wifiConfigurationDirty = false;
uint32_t wifiConnectStartedMs = 0UL;
uint32_t wifiNextReconnectMs = 0UL;
uint8_t wifiLastStatus = 0U;

void wifiInitialize();
void wifiService();
void wifiPrintStatus();
bool wifiSetSSID(const char *ssid);
bool wifiSetPassword(const char *password);
bool wifiSetHostname(const char *hostname);
bool wifiApplyConfiguration();
bool wifiIsConfigured();
bool wifiIsConnected();
const char *wifiGetSSID();
bool wifiPasswordIsConfigured();
int32_t wifiGetRSSI();

void configurationStorageInitialize();
bool configurationLoadWifi();
bool configurationSaveWifi();
bool configurationLoadMqtt();
bool configurationLoadStaticIp();          // ← new
bool configurationSaveStaticIp();          // ← new
bool configurationSaveMqtt();
bool configurationLoadHa();
bool configurationSaveHa();
bool configurationLoadPins();
bool configurationSavePins();
bool configurationLoadSystem();
bool configurationSaveSystem();
bool configurationInitializeCommandHue(uint32_t databaseGeneration);
bool storageGetCommandHueEnabled(uint32_t commandId, bool &enabledOut);
bool storageSetCommandHueEnabled(uint32_t commandId, bool enabled);
bool configurationInitializeDeviceCloud(uint32_t databaseGeneration);
bool storageGetDeviceCloud(uint32_t deviceId, uint8_t &categoryOut, bool &cloudOut);
bool storageSetDeviceCloud(uint32_t deviceId, uint8_t category, bool cloud);
const char *deviceCategoryName(uint8_t category);
uint8_t deviceCategoryFromName(const char *name);
void hueInvalidateCache();
bool configurationLoadTempCal();
bool configurationSaveTempCal();

void hueInitialize();
void hueService();
void hueStopNetworking();   // >>> v1.0 watchdog needs this declared early

void mqttRequestDiscoveryRefresh();
bool mqttDiscoveryClearAllCommandConfigs();
bool mqttRemoveHaTemperatureDiscovery();
bool mqttRemoveHaButtonDiscovery(uint32_t commandId);   // >>> ADD

void mqttInitialize();
void mqttService();
void mqttPrintStatus();
bool mqttSetEnabled(bool enabled);
bool mqttSetBroker(const char *broker);
bool mqttSetPort(uint16_t port);
bool mqttSetUsername(const char *username);
bool mqttSetPassword(const char *password);
bool mqttSetClientId(const char *clientId);
bool mqttSetBaseTopic(const char *baseTopic);
bool mqttApplyConfiguration();
bool mqttIsConnected();
const char *mqttGetBaseTopic();
bool mqttHaDiscoverySetEnabled(bool enabled);

// >>> v11 INTEGRATION — WebUI forward declarations
bool webUiLoadHubName();
bool webUiPersistHubName();
const char *webUiIrStateToString(IrEngineState state);
bool parseUint32Text(const char *text, uint32_t &valueOut);
bool webUiParseAutomationCommand(const char *cmd, char *errOut, size_t errCap);
bool webUiDispatch(WiFiClient &client, const char *method, const char *path, const char *body);
// Used by the WiFi watchdog to avoid false positives during a scan.
bool webUiWifiScanInProgress();

// Config backup / restore to the W25Qxx flash.
bool storageWriteConfigBackup();
bool storageReadConfigBackup(uint8_t *out, size_t capacity);

// Per-entity settings on SPI (Stage 2 live path).
bool storageGetDeviceSettings(uint32_t deviceId, uint8_t &categoryOut, bool &cloudOut);
bool storageSetDeviceSettings(uint32_t deviceId, uint8_t category, bool cloud);
bool storageGetCommandAlexa(uint32_t commandId, bool &alexaOut);
bool storageSetCommandAlexa(uint32_t commandId, bool alexaOn);

bool copyWifiField(char *destination, size_t destinationCapacity, const char *source) {
  if (destination == nullptr || source == nullptr || destinationCapacity < 2U) return false;
  const size_t length = strlen(source);
  if (length == 0U || length >= destinationCapacity) return false;
  memcpy(destination, source, length + 1U);
  return true;
}

bool wifiHostnameIsValid(const char *hostname) {
  if (hostname == nullptr) return false;
  const size_t length = strlen(hostname);
  if (length == 0U || length > WIFI_HOSTNAME_MAX_LENGTH) return false;
  if (hostname[0] == '-' || hostname[length - 1U] == '-') return false;
  for (size_t i = 0; i < length; i++) {
    const char c = hostname[i];
    const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-';
    if (!valid) return false;
  }
  return true;
}

const char *wifiStateToString(WifiState state) {
  switch (state) {
    case WifiState::UNCONFIGURED: return "UNCONFIGURED";
    case WifiState::CONNECTING:   return "CONNECTING";
    case WifiState::CONNECTED:    return "CONNECTED";
    case WifiState::DISCONNECTED: return "DISCONNECTED";
    default:                      return "UNKNOWN";
  }
}

void wifiUpdateStateFromHardware() {
  if (wifiConfig.ssid[0] == '\0') { wifiState = WifiState::UNCONFIGURED; return; }
  const wl_status_t currentStatus = WiFi.status();
  wifiLastStatus = static_cast<uint8_t>(currentStatus);
  if (currentStatus == WL_CONNECTED) {
    wifiState = WifiState::CONNECTED;
    wifiConnectAttemptActive = false;
  } else if (wifiConnectAttemptActive) {
    wifiState = WifiState::CONNECTING;
  } else {
    wifiState = WifiState::DISCONNECTED;
  }
}

static void wifiApplyIpConfiguration() {
  const bool useStatic = staticIpConfig.enabled &&
      static_cast<uint32_t>(staticIpConfig.ip) != 0U &&
      static_cast<uint32_t>(staticIpConfig.gateway) != 0U &&
      static_cast<uint32_t>(staticIpConfig.subnet) != 0U;
  if (useStatic) {
    const IPAddress dns = (static_cast<uint32_t>(staticIpConfig.dns) != 0U)
                          ? staticIpConfig.dns : staticIpConfig.gateway;
    if (!WiFi.config(staticIpConfig.ip, staticIpConfig.gateway,
                     staticIpConfig.subnet, dns)) {
      Serial.println(F("WARNING: static IP config rejected; using DHCP instead."));
    }
  } else {
    // Do NOT call WiFi.config(0,0,0,0) here. On ESP8266 core that does
    // not reliably revert a previously-latched static IP. The netif reset
    // in wifiStartConnection() handles the DHCP case instead.
  }
}

void wifiStartConnection() {
  if (wifiConfig.ssid[0] == '\0') {
    wifiConnectAttemptActive = false;
    wifiState = WifiState::UNCONFIGURED;
    statusLedSetWifiError(false);
    return;
  }

  // Detect IP-mode change. On ESP8266 a previously-applied static IP stays
  // latched in LwIP even across WiFi.disconnect(). The only reliable way to
  // switch between static and DHCP is a full netif reset via mode(WIFI_OFF).
  const bool wantStatic = staticIpConfig.enabled &&
      static_cast<uint32_t>(staticIpConfig.ip) != 0U &&
      static_cast<uint32_t>(staticIpConfig.gateway) != 0U &&
      static_cast<uint32_t>(staticIpConfig.subnet) != 0U;
  if (wantStatic != lastAppliedStaticIpMode) {
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
  lastAppliedStaticIpMode = wantStatic;

  WiFi.mode((wifiRecoveryState != WifiRecoveryState::OFF) ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(wifiConfig.hostname);
  WiFi.disconnect();
  delay(1);

  wifiApplyIpConfiguration();

  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  wifiConnectAttemptActive = true;
  wifiConnectStartedMs = millis();
  wifiNextReconnectMs = 0UL;
  wifiLastStatus = static_cast<uint8_t>(WiFi.status());
  wifiState = WifiState::CONNECTING;
}

void wifiInitialize() {
  wifiConfig = WifiConfiguration{};
  snprintf(wifiConfig.hostname, sizeof(wifiConfig.hostname),
           "ir-hub-%06X",
           static_cast<unsigned int>(ESP.getChipId() & 0xFFFFFFUL));
  (void)configurationLoadWifi();
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  wifiState = WifiState::UNCONFIGURED;
  wifiConnectAttemptActive = false;
  wifiConfigurationDirty = false;
  wifiConnectStartedMs = 0UL;
  wifiNextReconnectMs = 0UL;
  wifiLastStatus = static_cast<uint8_t>(WiFi.status());
  statusLedSetWifiError(false);
}

bool wifiStatusIndicatesConnectionFailure(wl_status_t status) {
  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) return true;
#ifdef WL_CONNECT_WRONG_PASSWORD
  if (status == WL_CONNECT_WRONG_PASSWORD) return true;
#endif
  return false;
}

// ============================================================
// Wi-Fi liveness watchdog (v2)
// ============================================================
// The previous implementation probed the DHCP-provided DNS server with a
// hand-rolled UDP/53 packet. Home routers commonly drop or rewrite those
// packets, so the probe always timed out and the watchdog killed a healthy
// link every 15 seconds.
//
// This version:
//   * probes a well-known public NTP server by hardcoded IP (no DNS at all)
//   * sends one query every 30 s
//   * declares the link dead only after 3 consecutive misses (~90 s)
//   * refuses to fire if an HTTP client is mid-request
//   * resets state cleanly on every WiFi reconnect

WiFiUDP  wifiWatchdogUdp;
IPAddress wifiWatchdogTarget(162, 159, 200, 1);   // time.cloudflare.com (anycast)
bool     wifiWatchdogSocketOpen        = false;
uint32_t wifiWatchdogLastSendMs        = 0UL;
uint32_t wifiWatchdogLastRecvMs        = 0UL;
uint8_t  wifiWatchdogConsecutiveMisses = 0U;
bool     wifiWatchdogProbeOutstanding  = false;   // >>> v1.0 tighter watchdog

constexpr uint32_t WIFI_WATCHDOG_SEND_INTERVAL_MS = 30000UL;  // 30 s between probes
constexpr uint32_t WIFI_WATCHDOG_RECV_TIMEOUT_MS  = 30000UL;  // reply window
constexpr uint8_t  WIFI_WATCHDOG_MAX_MISSES       = 3U;       // 3 × 30 s ≈ 90 s

// Declared in the Hue module; true when an HTTP client is being served.
extern bool hueHttpClientActive;

void wifiForceHardReset() {
  Serial.println(F("WiFi watchdog: forcing hard reassociation"));
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  if (wifiConfig.hostname[0] != '\0') WiFi.hostname(wifiConfig.hostname);
  wifiApplyIpConfiguration();
  lastAppliedStaticIpMode = staticIpConfig.enabled &&
      static_cast<uint32_t>(staticIpConfig.ip) != 0U;
  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  wifiConnectAttemptActive      = true;
  wifiConnectStartedMs          = millis();
  wifiNextReconnectMs           = 0UL;
  wifiState                     = WifiState::CONNECTING;
  wifiWatchdogSocketOpen        = false;
  wifiWatchdogLastSendMs        = 0UL;
  wifiWatchdogLastRecvMs        = 0UL;
  wifiWatchdogConsecutiveMisses = 0U;
  hueStopNetworking();
}

void wifiWatchdogSendNtpProbe() {
  if (!wifiWatchdogSocketOpen) {
    if (!wifiWatchdogUdp.begin(0)) return;
    wifiWatchdogSocketOpen = true;
  }
  // Minimal NTPv4 client request: LI=0, VN=4, Mode=3 → 0x23.
  // All other fields zero, which is acceptable for a liveness probe.
  uint8_t packet[48] = {0};
  packet[0] = 0x23;
  wifiWatchdogUdp.beginPacket(wifiWatchdogTarget, 123);
  wifiWatchdogUdp.write(packet, sizeof(packet));
  wifiWatchdogUdp.endPacket();
}

void wifiWatchdogService() {
  // Never touch WiFi while an OTA update is in flight. The transfer
  // saturates the radio, so NTP probes cannot be answered inside the
  // 30 s window and the watchdog would force-reset WiFi mid-upload.
  if (Update.isRunning()) {
    wifiWatchdogConsecutiveMisses = 0U;
    wifiWatchdogProbeOutstanding  = false;
    wifiWatchdogLastSendMs        = millis();
    return;
  }
  // WiFi scan parks the radio on one channel at a time. NTP replies
  // arriving on a different channel are missed. Treat a scan in
  // progress as "radio busy" and hold the miss counter.
  if (webUiWifiScanInProgress()) {
    wifiWatchdogConsecutiveMisses = 0U;
    wifiWatchdogProbeOutstanding  = false;
    wifiWatchdogLastSendMs        = millis();
    return;
  }
  if (!wifiIsConnected()) {
    wifiWatchdogSocketOpen        = false;
    wifiWatchdogLastSendMs        = 0UL;
    wifiWatchdogLastRecvMs        = 0UL;
    wifiWatchdogConsecutiveMisses = 0U;
    wifiWatchdogProbeOutstanding  = false;
    return;
  }

  const uint32_t now = millis();

  // First tick after a successful connect: seed timers, send a probe now.
  if (wifiWatchdogLastRecvMs == 0UL) {
    wifiWatchdogLastRecvMs = now;
    wifiWatchdogLastSendMs = now - WIFI_WATCHDOG_SEND_INTERVAL_MS;
    return;
  }

  // Any datagram back = link alive. Clear the miss counter and mark the
  // outstanding probe as answered.
  if (wifiWatchdogSocketOpen && wifiWatchdogUdp.parsePacket() > 0) {
    while (wifiWatchdogUdp.available()) (void)wifiWatchdogUdp.read();
    if (wifiWatchdogConsecutiveMisses > 0U) {
      Serial.print(F("WiFi watchdog: link recovered after "));
      Serial.print(wifiWatchdogConsecutiveMisses);
      Serial.println(F(" unanswered probe(s)"));
    }
    wifiWatchdogConsecutiveMisses = 0U;
    wifiWatchdogProbeOutstanding  = false;
  }

  // Time to send the next probe?
  if ((now - wifiWatchdogLastSendMs) >= WIFI_WATCHDOG_SEND_INTERVAL_MS) {
    // If the previous probe was never answered by now, count one miss.
    if (wifiWatchdogProbeOutstanding) {
      wifiWatchdogConsecutiveMisses++;
      Serial.print(F("WiFi watchdog: probe miss "));
      Serial.print(wifiWatchdogConsecutiveMisses);
      Serial.print('/');
      Serial.println(WIFI_WATCHDOG_MAX_MISSES);

      if (wifiWatchdogConsecutiveMisses >= WIFI_WATCHDOG_MAX_MISSES) {
        if (hueHttpClientActive || Update.isRunning()) {
          // Do not kill an in-flight HTTP request or OTA write.
          wifiWatchdogConsecutiveMisses = WIFI_WATCHDOG_MAX_MISSES - 1U;
        } else {
          wifiForceHardReset();
          return;
        }
      }
    }

    wifiWatchdogLastSendMs        = now;
    wifiWatchdogSendNtpProbe();
    wifiWatchdogProbeOutstanding  = true;
  }
}

void wifiStartRecoveryAp() {
  if (wifiRecoveryState != WifiRecoveryState::OFF) return;

  snprintf(wifiRecoveryApSsid, sizeof(wifiRecoveryApSsid),
           "Overkill-%06X", (unsigned)(ESP.getChipId() & 0xFFFFFFUL));

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.softAP(wifiRecoveryApSsid);
  captiveDnsStart();

  wifiRecoveryState = WifiRecoveryState::AP;
  hueStopNetworking();

  Serial.print(F("Recovery AP started: "));
  Serial.print(wifiRecoveryApSsid);
  Serial.print(F(" @ "));
  Serial.println(WiFi.softAPIP());
}

// Runs on every idle tick of wifiService(), regardless of STA state.
// The teardown must not live at the end of wifiService() because the
// WL_CONNECTED branch returns early.
static void wifiServiceApGracePeriod() {
  if (wifiRecoveryState == WifiRecoveryState::OFF) return;
  if (wifiRecoveryApConnectedAtMs == 0UL) return;
  if (Update.isRunning()) return;   // never tear down mid-OTA
  const uint32_t now = millis();
  if ((now - wifiRecoveryApConnectedAtMs) < WIFI_AP_GRACE_PERIOD_MS) return;

  hueStopNetworking();          // close HTTP + SSDP first
  captiveDnsStop();             // close captive DNS UDP
  WiFi.softAPdisconnect(true);  // turn the AP off
  WiFi.mode(WIFI_STA);          // now safe to change mode
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  wifiRecoveryState = WifiRecoveryState::OFF;
  wifiRecoveryApConnectedAtMs = 0UL;
  Serial.println(F("Recovery AP grace period expired; AP stopped."));
}

void wifiService() {
  if (irState != IrEngineState::IDLE) return;
  wifiServiceApGracePeriod();
  if (wifiConfig.ssid[0] == '\0') {
    if (wifiState != WifiState::UNCONFIGURED) {
      WiFi.disconnect(); WiFi.mode(WIFI_OFF);
      wifiConnectAttemptActive = false;
      wifiState = WifiState::UNCONFIGURED;
    }
    statusLedSetWifiError(false);

    // ── v1.0: fresh device, no credentials — bring up AP after 10 s ──
    if (wifiRecoveryState == WifiRecoveryState::OFF) {
      const uint32_t now = millis();
      if (wifiRecoveryNotConnectedSinceMs == 0UL) {
        wifiRecoveryNotConnectedSinceMs = now;
      }
      if ((now - wifiRecoveryNotConnectedSinceMs) >= 10000UL) {
        wifiStartRecoveryAp();
      }
    }
    return;
  }
  const uint32_t now = millis();
  const wl_status_t currentStatusBeforeDirtyCheck = WiFi.status();
  wifiLastStatus = static_cast<uint8_t>(currentStatusBeforeDirtyCheck);
  if (wifiConfigurationDirty) {
    if (currentStatusBeforeDirtyCheck == WL_CONNECTED) {
      wifiConnectAttemptActive = false; wifiState = WifiState::CONNECTED;
    } else {
      wifiConnectAttemptActive = false; wifiState = WifiState::DISCONNECTED;
    }
    statusLedSetWifiError(false);
    return;
  }
  const wl_status_t currentStatus = WiFi.status();
// ── Continuous-disconnect tracking ───────────────────────────
if (currentStatus == WL_CONNECTED) {
  wifiRecoveryNotConnectedSinceMs = 0UL;
  if (wifiRecoveryState != WifiRecoveryState::OFF) {
    // Keep the AP alive for a grace period after STA connects so the user,
    // still attached to the AP, can read the new LAN IP from the AP page.
    // We record the moment STA connected and let wifiService() tear the AP
    // down later (see the AP grace-period block further down this function).
    if (wifiRecoveryApConnectedAtMs == 0UL) {
      wifiRecoveryApConnectedAtMs = now;
      Serial.print(F("STA connected on IP "));
      Serial.println(WiFi.localIP());
      Serial.println(F("Recovery AP kept alive so you can read the IP above."));
    }
  }
} else if (wifiRecoveryNotConnectedSinceMs == 0UL) {
  wifiRecoveryNotConnectedSinceMs = now;
}

// ── Trigger AP after 30 s continuous outage ──────────────────
if (!Update.isRunning() &&
    wifiRecoveryState == WifiRecoveryState::OFF &&
    currentStatus != WL_CONNECTED &&
    wifiRecoveryNotConnectedSinceMs != 0UL &&
    (now - wifiRecoveryNotConnectedSinceMs) >= WIFI_RECOVERY_TRIGGER_MS) {
  wifiStartRecoveryAp();
}
  wifiLastStatus = static_cast<uint8_t>(currentStatus);
  if (currentStatus == WL_CONNECTED) {
#if STAGE1_SERIAL_DEBUG
    const bool becameConnected = (wifiState != WifiState::CONNECTED);
#endif
    wifiConnectAttemptActive = false;
    wifiState = WifiState::CONNECTED;
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    statusLedSetWifiError(false);
#if STAGE1_SERIAL_DEBUG
    if (becameConnected && serialEnabled) {
      Serial.print(F("Wi-Fi connected. IP: "));
      Serial.println(WiFi.localIP());
    }
#endif
    return;
  }
  //statusLedSetWifiError(true);
  if (wifiConnectAttemptActive) {
    wifiState = WifiState::CONNECTING;
    if (wifiStatusIndicatesConnectionFailure(currentStatus) ||
        (now - wifiConnectStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect();
      wifiConnectAttemptActive = false;
      wifiState = WifiState::DISCONNECTED;
      wifiNextReconnectMs = now + WIFI_RECONNECT_INTERVAL_MS;
    }
    return;
  }
  wifiState = WifiState::DISCONNECTED;
  if (static_cast<int32_t>(now - wifiNextReconnectMs) >= 0) {
    wifiStartConnection();
  }

}

bool wifiSetSSID(const char *ssid) {
  if (ssid == nullptr || strlen(ssid) > WIFI_SSID_MAX_LENGTH) return false;
  if (ssid[0] == '\0') {
    memset(wifiConfig.ssid, 0, sizeof(wifiConfig.ssid));
    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
    wifiConnectAttemptActive = false;
    wifiConfigurationDirty = false;
    wifiState = WifiState::UNCONFIGURED;
    statusLedSetWifiError(false);
    if (!configurationSaveWifi()) {
      Serial.println(F("WARNING: Wi-Fi SSID was cleared in RAM but the cleared state could not be persisted."));
    }
    return true;
  }
  if (!copyWifiField(wifiConfig.ssid, sizeof(wifiConfig.ssid), ssid)) return false;
  wifiConfigurationDirty = true;
  wifiConnectAttemptActive = false;
  if (WiFi.status() != WL_CONNECTED) wifiState = WifiState::DISCONNECTED;
  else wifiState = WifiState::CONNECTED;
  statusLedSetWifiError(false);
  return true;
}

bool wifiSetPassword(const char *password) {
  if (password == nullptr || strlen(password) > WIFI_PASSWORD_MAX_LENGTH) return false;
  for (size_t i = 0; password[i] != '\0'; i++) {
    const uint8_t c = static_cast<uint8_t>(password[i]);
    if (c < 32U || c > 126U) return false;
  }
  memset(wifiConfig.password, 0, sizeof(wifiConfig.password));
  memcpy(wifiConfig.password, password, strlen(password) + 1U);
  wifiConfigurationDirty = true;
  return true;
}

bool wifiSetHostname(const char *hostname) {
  if (!wifiHostnameIsValid(hostname)) return false;
  if (!copyWifiField(wifiConfig.hostname, sizeof(wifiConfig.hostname), hostname)) return false;
  wifiConfigurationDirty = true;
  return true;
}

bool wifiApplyConfiguration() {
  if (wifiConfig.ssid[0] == '\0') {
    wifiConfigurationDirty = false;
    wifiState = WifiState::UNCONFIGURED;
    statusLedSetWifiError(false);
    return false;
  }
  if (!configurationSaveWifi()) {
    Serial.println(F("WARNING: Wi-Fi configuration applied in RAM but could not be persisted."));
  }
  wifiConfigurationDirty = false;
  wifiStartConnection();
  return true;
}

bool wifiIsConfigured() { return wifiConfig.ssid[0] != '\0'; }
bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
const char *wifiGetSSID() { return wifiConfig.ssid; }
bool wifiPasswordIsConfigured() { return wifiConfig.password[0] != '\0'; }
int32_t wifiGetRSSI() { return wifiIsConnected() ? static_cast<int32_t>(WiFi.RSSI()) : 0; }

void wifiPrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  wifiUpdateStateFromHardware();
  Serial.println();
  Serial.println(F("=== WI-FI ==="));
  Serial.print(F("Configured    : ")); Serial.println(wifiIsConfigured() ? F("YES") : F("NO"));
  Serial.print(F("Pending config: ")); Serial.println(wifiConfigurationDirty ? F("YES") : F("NO"));
  Serial.print(F("SSID          : ")); if (wifiIsConfigured()) Serial.println(wifiConfig.ssid); else Serial.println(F("<not set>"));
  Serial.print(F("Password      : ")); Serial.println(wifiPasswordIsConfigured() ? F("SET") : F("NOT SET"));
  Serial.print(F("Hostname      : ")); Serial.println(wifiConfig.hostname);
  Serial.print(F("State         : ")); Serial.println(wifiStateToString(wifiState));
  Serial.print(F("Status code   : ")); Serial.println(wifiLastStatus);
  Serial.print(F("RSSI          : "));
  if (wifiIsConnected()) {
    Serial.print(wifiGetRSSI()); Serial.println(F(" dBm"));
    Serial.print(F("IP address    : ")); Serial.println(WiFi.localIP());
  } else Serial.println(F("n/a"));
  Serial.print(F("MAC address   : ")); Serial.println(WiFi.macAddress());
  Serial.println(F("=============="));
  Serial.println();
#endif
}

// ============================================================
// TIME / NTP SERVICE
// ============================================================

constexpr uint32_t TIME_MIN_VALID_EPOCH   = 1600000000UL;
constexpr size_t   NTP_SERVER_MAX_LENGTH  = 63U;
constexpr uint32_t NTP_RETRY_INTERVAL_MS  = 60000UL;

bool     ntpEnabled          = true;
int16_t  ntpGmtOffsetMinutes = 0;
int16_t  ntpDstOffsetMinutes = 0;
char     ntpServer[NTP_SERVER_MAX_LENGTH + 1U] = "pool.ntp.org";

bool     ntpTimeValid     = false;
bool     ntpTimeAttempted = false;
uint32_t ntpLastAttemptMs = 0UL;

void timeInitialize();
void timeService();
void timePrintStatus();
void timeApplyConfig();
bool timeIsValid();
time_t timeNow();
bool timeGetLocal(struct tm &out);
bool timeSetEnabled(bool enabled);
bool timeSetServer(const char *server);
bool timeSetGmtOffset(int16_t minutes);
bool timeSetDstOffset(int16_t minutes);

bool timeIsValid() {
  return static_cast<uint32_t>(time(nullptr)) >= TIME_MIN_VALID_EPOCH;
}

time_t timeNow() { return time(nullptr); }

bool timeGetLocal(struct tm &out) {
  const time_t now = time(nullptr);
  if (static_cast<uint32_t>(now) < TIME_MIN_VALID_EPOCH) return false;
  localtime_r(&now, &out);
  return true;
}

void timeApplyConfig() {
  if (!ntpEnabled || !wifiIsConnected()) return;
  const long gmtSec = static_cast<long>(ntpGmtOffsetMinutes) * 60L;
  const long dstSec = static_cast<long>(ntpDstOffsetMinutes) * 60L;
  configTime(gmtSec, dstSec, ntpServer);
  ntpTimeAttempted = true;
  ntpLastAttemptMs = millis();
}

void timeInitialize() {
  ntpTimeValid     = false;
  ntpTimeAttempted = false;
  ntpLastAttemptMs = 0UL;
  if (ntpEnabled && wifiIsConnected()) timeApplyConfig();
}

void timeService() {
  if (!ntpEnabled) { ntpTimeValid = false; return; }
  if (!wifiIsConnected()) return;

  if (!timeIsValid()) {
    const uint32_t now = millis();
    if (!ntpTimeAttempted || (now - ntpLastAttemptMs) >= NTP_RETRY_INTERVAL_MS) {
      timeApplyConfig();
    }
    return;
  }
  if (!ntpTimeValid) {
    ntpTimeValid = true;
#if STAGE1_SERIAL_DEBUG
    if (serialEnabled) {
      struct tm t{};
      if (timeGetLocal(t)) {
        char buf[24] = {0};
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        Serial.print(F("Time synchronized: "));
        Serial.println(buf);
      }
    }
#endif
  }
}

bool timeSetEnabled(bool enabled) {
  ntpEnabled = enabled;
  ntpTimeAttempted = false;
  ntpLastAttemptMs = 0UL;
  if (!enabled) ntpTimeValid = false;
  else if (wifiIsConnected()) timeApplyConfig();
  return true;
}

bool timeSetServer(const char *server) {
  if (server == nullptr) return false;
  const size_t len = strlen(server);
  if (len == 0U || len > NTP_SERVER_MAX_LENGTH) return false;
  memcpy(ntpServer, server, len + 1U);
  ntpTimeAttempted = false;
  ntpLastAttemptMs = 0UL;
  if (ntpEnabled && wifiIsConnected()) timeApplyConfig();
  return true;
}

bool timeSetGmtOffset(int16_t minutes) {
  if (minutes < -1440 || minutes > 1440) return false;
  ntpGmtOffsetMinutes = minutes;
  ntpTimeAttempted = false;
  ntpLastAttemptMs = 0UL;
  if (ntpEnabled && wifiIsConnected()) timeApplyConfig();
  return true;
}

bool timeSetDstOffset(int16_t minutes) {
  if (minutes < 0 || minutes > 120) return false;
  ntpDstOffsetMinutes = minutes;
  ntpTimeAttempted = false;
  ntpLastAttemptMs = 0UL;
  if (ntpEnabled && wifiIsConnected()) timeApplyConfig();
  return true;
}

void timePrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=== TIME / NTP ==="));
  Serial.print(F("NTP enabled : ")); Serial.println(ntpEnabled ? F("YES") : F("NO"));
  Serial.print(F("Server      : ")); Serial.println(ntpServer);
  Serial.print(F("GMT offset  : ")); Serial.print(ntpGmtOffsetMinutes); Serial.println(F(" min"));
  Serial.print(F("DST offset  : ")); Serial.print(ntpDstOffsetMinutes); Serial.println(F(" min"));  Serial.print(F("Wi-Fi       : ")); Serial.println(wifiIsConnected() ? F("CONNECTED") : F("NOT CONNECTED"));
  Serial.print(F("Time valid  : ")); Serial.println(timeIsValid() ? F("YES") : F("NO"));
  if (timeIsValid()) {
    struct tm t{};
    if (timeGetLocal(t)) {
      char buf[24] = {0};
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
      Serial.print(F("Local time  : ")); Serial.println(buf);
    }
    Serial.print(F("Unix time   : ")); Serial.println(static_cast<unsigned long>(timeNow()));
  }
  Serial.println(F("=================="));
  Serial.println();
#endif
}

// ============================================================
// DS18B20 NATIVE 1-WIRE DRIVER
// ============================================================

void ds18b20ReleaseBus() { pinMode(pinDs18b20, INPUT); }

bool ds18b20ResetPresence() {
  bool presence = false;
  noInterrupts();
  pinMode(pinDs18b20, OUTPUT);
  digitalWrite(pinDs18b20, LOW);
  delayMicroseconds(480);
  pinMode(pinDs18b20, INPUT);
  delayMicroseconds(70);
  presence = (digitalRead(pinDs18b20) == LOW);
  delayMicroseconds(410);
  interrupts();
  return presence;
}

void ds18b20WriteBit(bool value) {
  noInterrupts();
  pinMode(pinDs18b20, OUTPUT);
  digitalWrite(pinDs18b20, LOW);
  if (value) {
    delayMicroseconds(6); ds18b20ReleaseBus(); delayMicroseconds(64);
  } else {
    delayMicroseconds(60); ds18b20ReleaseBus(); delayMicroseconds(10);
  }
  interrupts();
}

bool ds18b20ReadBit() {
  bool value = false;
  noInterrupts();
  pinMode(pinDs18b20, OUTPUT);
  digitalWrite(pinDs18b20, LOW);
  delayMicroseconds(3);
  ds18b20ReleaseBus();
  delayMicroseconds(10);
  value = (digitalRead(pinDs18b20) == HIGH);
  delayMicroseconds(53);
  interrupts();
  return value;
}

void ds18b20WriteByte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) { ds18b20WriteBit((value & 0x01U) != 0U); value >>= 1; }
}

uint8_t ds18b20Crc8(const uint8_t *data, size_t length);

uint8_t ds18b20ReadByte() {
  uint8_t value = 0;
  for (uint8_t bit = 0; bit < 8; ++bit) if (ds18b20ReadBit()) value |= static_cast<uint8_t>(1U << bit);
  return value;
}

bool ds18b20ReadRomAndValidate() {
  uint8_t rom[8] = {0};
  if (!ds18b20ResetPresence()) return false;
  ds18b20WriteByte(0x33U);
  for (uint8_t i = 0; i < sizeof(rom); ++i) rom[i] = ds18b20ReadByte();
  if (rom[0] != 0x28U) return false;
  return ds18b20Crc8(rom, 7) == rom[7];
}

uint8_t ds18b20Crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  if (data == nullptr) return 0;
  for (size_t i = 0; i < length; ++i) {
    uint8_t inByte = data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint8_t mix = static_cast<uint8_t>((crc ^ inByte) & 0x01U);
      crc >>= 1;
      if (mix != 0U) crc ^= 0x8CU;
      inByte >>= 1;
    }
  }
  return crc;
}

bool ds18b20StartConversion() {
  if (!ds18b20State.connected && !ds18b20ReadRomAndValidate()) return false;
  if (!ds18b20ResetPresence()) return false;
  ds18b20WriteByte(0xCCU);
  ds18b20WriteByte(0x44U);
  return true;
}

bool ds18b20ReadTemperatureRaw(int16_t &rawTemperature) {
  uint8_t scratchpad[9] = {0};
  if (!ds18b20ResetPresence()) return false;
  ds18b20WriteByte(0xCCU);
  ds18b20WriteByte(0xBEU);
  for (uint8_t i = 0; i < sizeof(scratchpad); ++i) scratchpad[i] = ds18b20ReadByte();
  if (ds18b20Crc8(scratchpad, 8) != scratchpad[8]) return false;
  const uint16_t raw = static_cast<uint16_t>(scratchpad[0]) | (static_cast<uint16_t>(scratchpad[1]) << 8);
  const int16_t temperature = static_cast<int16_t>(raw);
  if (temperature < static_cast<int16_t>(-880) || temperature > static_cast<int16_t>(2000)) return false;
  rawTemperature = temperature;
  return true;
}

void ds18b20Service() {
  if (irState != IrEngineState::IDLE || !ds18b20State.enabled) return;
  const uint32_t now = millis();
  if (ds18b20State.conversionPending) {
    if ((now - ds18b20State.conversionStartedMs) < DS18B20_CONVERSION_WAIT_MS) return;
    ds18b20State.conversionPending = false;
    int16_t rawTemperature = 0;
    if (ds18b20ReadTemperatureRaw(rawTemperature)) {
      ds18b20State.rawTemperature = rawTemperature;
      ds18b20State.calibratedTemperatureCentiDegrees =
          (static_cast<int32_t>(rawTemperature) * 625L) / 100L +
          static_cast<int32_t>(ds18b20State.calibrationCentiDegrees);
      ds18b20State.temperatureValid = true;
      ds18b20State.connected = true;
      ds18b20State.lastReadMs = now;
      ds18b20State.sampleSequence++;
      ds18b20State.nextActionMs = now + ds18b20State.sampleIntervalMs;
    } else {
      ds18b20State.temperatureValid = false;
      ds18b20State.connected = false;
      ds18b20State.nextActionMs = now + DS18B20_RETRY_INTERVAL_MS;
    }
    return;
  }
  if (static_cast<int32_t>(now - ds18b20State.nextActionMs) < 0) return;
  if (ds18b20StartConversion()) {
    ds18b20State.conversionPending = true;
    ds18b20State.conversionStartedMs = now;
    ds18b20State.connected = true;
    return;
  }
  ds18b20State.temperatureValid = false;
  ds18b20State.connected = false;
  ds18b20State.nextActionMs = now + DS18B20_RETRY_INTERVAL_MS;
}

void ds18b20SetEnabled(bool enabled) {
  ds18b20State.enabled = enabled;
  ds18b20State.conversionPending = false;
  ds18b20State.temperatureValid = false;
  ds18b20State.connected = false;
  ds18b20State.nextActionMs = enabled ? 0UL : UINT32_MAX;
  ds18b20ReleaseBus();
}

void ds18b20SetCalibrationCentiDegrees(int16_t offsetCentiDegrees) {
  ds18b20State.calibrationCentiDegrees = offsetCentiDegrees;
  if (ds18b20State.temperatureValid) {
    ds18b20State.calibratedTemperatureCentiDegrees =
        (static_cast<int32_t>(ds18b20State.rawTemperature) * 625L) / 100L +
        static_cast<int32_t>(offsetCentiDegrees);
  }
}

void ds18b20SetSampleIntervalMs(uint32_t intervalMs) {
  if (intervalMs < DS18B20_SAMPLE_INTERVAL_MIN_MS) intervalMs = DS18B20_SAMPLE_INTERVAL_MIN_MS;
  else if (intervalMs > DS18B20_SAMPLE_INTERVAL_MAX_MS) intervalMs = DS18B20_SAMPLE_INTERVAL_MAX_MS;
  ds18b20State.sampleIntervalMs = intervalMs;
  if (ds18b20State.enabled && !ds18b20State.conversionPending && ds18b20State.temperatureValid) {
    ds18b20State.nextActionMs = ds18b20State.lastReadMs + intervalMs;
  }
}

bool ds18b20IsEnabled() { return ds18b20State.enabled; }
bool ds18b20IsConnected() { return ds18b20State.connected; }
bool ds18b20HasValidReading() { return ds18b20State.temperatureValid; }
uint32_t ds18b20GetSampleIntervalMs() { return ds18b20State.sampleIntervalMs; }
int32_t ds18b20GetCalibratedCentiDegrees() { return ds18b20State.calibratedTemperatureCentiDegrees; }
int16_t ds18b20GetCalibrationCentiDegrees() { return ds18b20State.calibrationCentiDegrees; }
uint32_t ds18b20GetSampleSequence() { return ds18b20State.sampleSequence; }

void ds18b20PrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=== DS18B20 ==="));
  Serial.print(F("Enabled      : ")); Serial.println(ds18b20State.enabled ? F("YES") : F("NO"));
  Serial.print(F("GPIO         : GPIO")); Serial.println(pinDs18b20);
  Serial.print(F("Update int.   : ")); Serial.print(ds18b20State.sampleIntervalMs / 1000UL); Serial.println(F(" s"));
  Serial.print(F("Connected    : ")); Serial.println(ds18b20State.connected ? F("YES") : F("NO"));
  Serial.print(F("Conversion   : ")); Serial.println(ds18b20State.conversionPending ? F("IN PROGRESS") : F("IDLE"));
  Serial.print(F("Temperature  : "));
  if (!ds18b20State.temperatureValid) {
    Serial.println(F("UNAVAILABLE"));
  } else {
    const int32_t centiDegrees = ds18b20State.calibratedTemperatureCentiDegrees;
    const bool negative = centiDegrees < 0;
    const int32_t absoluteCenti = negative ? -centiDegrees : centiDegrees;
    if (negative) Serial.print('-');
    Serial.print(absoluteCenti / 100L); Serial.print('.');
    const int32_t fraction = absoluteCenti % 100L;
    if (fraction < 10L) Serial.print('0');
    Serial.print(fraction); Serial.println(F(" C (calibrated)"));
  }
  const int16_t offset = ds18b20State.calibrationCentiDegrees;
  const int32_t signedOffset = static_cast<int32_t>(offset);
  const bool offsetNegative = signedOffset < 0;
  const int32_t absoluteOffset = offsetNegative ? -signedOffset : signedOffset;
  Serial.print(F("Calibration  : "));
  if (offsetNegative) Serial.print('-');
  Serial.print(absoluteOffset / 100L); Serial.print('.');
  const int32_t offsetFraction = absoluteOffset % 100L;
  if (offsetFraction < 10L) Serial.print('0');
  Serial.print(offsetFraction); Serial.println(F(" C"));
  if (ds18b20State.temperatureValid) {
    Serial.print(F("Sample seq   : ")); Serial.println(ds18b20State.sampleSequence);
  }
  Serial.println(F("==============="));
  Serial.println();
#endif
}

// ============================================================
// SERIAL COMMAND INPUT
// ============================================================

#if STAGE1_SERIAL_DEBUG
constexpr size_t SERIAL_LINE_SIZE = 320;
char serialLine[SERIAL_LINE_SIZE];
size_t serialLineLength = 0;
bool serialLineOverflow = false;
bool relearnConfirmationPending = false;
uint32_t relearnPendingCommandId = 0UL;
#endif

// ============================================================
// STATUS LED SERVICE
// ============================================================

void statusLedWrite(bool on) {
  statusLedOn = on;
  digitalWrite(pinStatusLed, on ? LOW : HIGH);
}

void statusLedRefreshErrorState() {
  // v1.0: the error LED reflects WiFi liveness only. Storage, MQTT and
  // manual error flags keep their internal state for diagnostics but no
  // longer drive the LED. Deliberate WiFi disable is not an error, so
  // statusLedWifiErrorActive is only ever set by
  // statusLedServiceWifiErrorDelay().
  statusLedErrorActive = statusLedWifiErrorActive;
  if (!statusLedErrorActive && statusLedMode == StatusLedMode::ERROR) {
    statusLedMode = StatusLedMode::OFF;
    statusLedWrite(false);
  }
}

void statusLedInitialize() {
  pinMode(pinStatusLed, OUTPUT);
  statusLedMode = StatusLedMode::OFF;
  statusLedPulsePending = false;
  statusLedErrorActive = false;
  statusLedManualErrorActive = false;
  statusLedStorageErrorActive = false;
  statusLedWifiErrorActive = false;
  statusLedMqttErrorActive = false;
  statusLedNextToggleMs = 0UL;
  statusLedPulseUntilMs = 0UL;
  statusLedWrite(false);
}

void statusLedSetError(bool active) { statusLedManualErrorActive = active; statusLedRefreshErrorState(); }
void statusLedSetStorageError(bool active) { statusLedStorageErrorActive = active; statusLedRefreshErrorState(); }
void statusLedSetWifiError(bool active) { statusLedWifiErrorActive = active; statusLedRefreshErrorState(); }
void statusLedSetMqttError(bool active) { statusLedMqttErrorActive = active; statusLedRefreshErrorState(); }

void statusLedStartLearning() {
  statusLedMode = StatusLedMode::LEARNING;
  statusLedPulsePending = false;
  statusLedNextToggleMs = millis();
  statusLedWrite(false);
}

void statusLedStopLearning() {
  if (statusLedMode == StatusLedMode::LEARNING) statusLedMode = StatusLedMode::OFF;
  statusLedWrite(false);
}

void statusLedPulseTransmit() {
  statusLedMode = StatusLedMode::OFF;
  statusLedPulsePending = true;
  statusLedPulseUntilMs = millis() + STATUS_LED_TX_PULSE_MS;
  statusLedWrite(true);
}

void statusLedServiceLearningWait() {
  if (irState != IrEngineState::LEARNING || statusLedMode != StatusLedMode::LEARNING) return;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - statusLedNextToggleMs) >= 0) {
    statusLedNextToggleMs = now + STATUS_LED_LEARN_BLINK_MS;
    statusLedWrite(!statusLedOn);
  }
}

void statusLedService() {
  if (irState != IrEngineState::IDLE) return;
  const uint32_t now = millis();
  if (statusLedPulsePending) {
    if (static_cast<int32_t>(now - statusLedPulseUntilMs) >= 0) {
      statusLedPulsePending = false; statusLedWrite(false);
    }
    return;
  }
  if (statusLedErrorActive) {
    if (statusLedMode != StatusLedMode::ERROR) {
      statusLedMode = StatusLedMode::ERROR;
      statusLedNextToggleMs = now;
      statusLedWrite(false);
    }
    if (static_cast<int32_t>(now - statusLedNextToggleMs) >= 0) {
      statusLedNextToggleMs = now + STATUS_LED_ERROR_BLINK_MS;
      statusLedWrite(!statusLedOn);
    }
    return;
  }
  if (statusLedMode == StatusLedMode::ERROR) statusLedMode = StatusLedMode::OFF;
  statusLedWrite(false);
}

// ============================================================
// Delayed WiFi-loss error LED (v1.0)
// ============================================================
// The LED blinks for error only after the WiFi link has been down for
// more than STATUS_LED_WIFI_ERROR_DELAY_MS. Short dropouts and automatic
// reconnects do not trigger it. Deliberate WiFi disable is not an error.
static uint32_t wifiDisconnectStartMs = 0UL;
constexpr uint32_t STATUS_LED_WIFI_ERROR_DELAY_MS = 30000UL;

void statusLedServiceWifiErrorDelay() {
  const bool wifiActive = (wifiConfig.ssid[0] != '\0');
  const bool wifiOnline = (WiFi.status() == WL_CONNECTED);

  if (!wifiActive || wifiOnline) {
    // Either intentionally off, or connected. Clear any pending error.
    wifiDisconnectStartMs = 0UL;
    if (statusLedWifiErrorActive) {
      statusLedWifiErrorActive = false;
      statusLedRefreshErrorState();
    }
    return;
  }

  // WiFi enabled and configured, but not connected.
  const uint32_t now = millis();
  if (wifiDisconnectStartMs == 0UL) {
    // First tick of a fresh disconnect — start the timer, LED stays off.
    wifiDisconnectStartMs = now;
    return;
  }

  const bool shouldBlink = (now - wifiDisconnectStartMs) >= STATUS_LED_WIFI_ERROR_DELAY_MS;
  if (shouldBlink != statusLedWifiErrorActive) {
    statusLedWifiErrorActive = shouldBlink;
    statusLedRefreshErrorState();
  }
}

void statusLedPrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=== STATUS LED ==="));
  Serial.println(F("Type          : Single LED / active LOW"));
  Serial.print(F("GPIO          : GPIO")); Serial.println(pinStatusLed);
  Serial.println(F("Idle          : OFF"));
  Serial.println(F("Transmit      : one brief blink after TX"));
  Serial.println(F("Learning      : continuous blink while waiting"));
  Serial.println(F("Error         : slow blink"));
  Serial.print(F("Error active  : ")); Serial.println(statusLedErrorActive ? F("YES") : F("NO"));
  Serial.print(F("Manual error  : ")); Serial.println(statusLedManualErrorActive ? F("YES") : F("NO"));
  Serial.print(F("Storage error : ")); Serial.println(statusLedStorageErrorActive ? F("YES") : F("NO"));
  Serial.print(F("Wi-Fi error   : ")); Serial.println(statusLedWifiErrorActive ? F("YES") : F("NO"));
  Serial.println(F("=================="));
  Serial.println();
#endif
}

// ============================================================
// UTILITY
// ============================================================

void feedWatchdog() { ESP.wdtFeed(); yield(); }

void rebootDevice() {
  if (serialEnabled) {
    Serial.println(F("Rebooting ESP8266..."));
    Serial.flush();
  }
  delay(100);
  ESP.restart();
}

void serviceSystemBackgroundOnly() {
  ESP.wdtFeed();
  yield();
  statusLedServiceLearningWait();
}

uint32_t crc32Buffer(const uint16_t *data, uint16_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (uint16_t i = 0; i < length; i++) {
    uint16_t value = data[i];
    for (uint8_t byteIndex = 0; byteIndex < 2; byteIndex++) {
      uint8_t byteValue = static_cast<uint8_t>(value & 0xFFU);
      value >>= 8;
      crc ^= byteValue;
      for (uint8_t bit = 0; bit < 8; bit++) {
        const uint32_t mask = 0UL - (crc & 1UL);
        crc = (crc >> 1) ^ (0xEDB88320UL & mask);
      }
    }
  }
  return ~crc;
}

bool storedSignalIntegrityOK() {
  if (lastRawLen == 0 || lastRawLen > RAW_WAVEFORM_CAPACITY) return false;
  return crc32Buffer(lastRaw, lastRawLen) == lastRawCrc32;
}

uint32_t calculateWaveformDurationUs(const uint16_t *data, uint16_t length) {
  uint32_t total = 0;
  for (uint16_t i = 0; i < length; i++) {
    if (UINT32_MAX - total < static_cast<uint32_t>(data[i])) return UINT32_MAX;
    total += static_cast<uint32_t>(data[i]);
  }
  return total;
}

bool hasLearnedSignal() {
  return lastRawLen > 0 && lastRawLen <= RAW_WAVEFORM_CAPACITY && storedSignalIntegrityOK();
}

void clearCurrentCommandRuntimeMetadata();
void clearLastCapture() {
  lastRawLen = 0;
  lastRawCrc32 = 0;
  lastLearnMetadataValid = false;
  clearCurrentCommandRuntimeMetadata();
}

void updateMinimumHeap() {
#if STAGE1_SERIAL_DEBUG
  const uint32_t heap = ESP.getFreeHeap();
  if (heap < minFreeHeap) minFreeHeap = heap;
#endif
}

// ============================================================
// IR RECEIVER CONTROL
// ============================================================

void startIRReceiver() {
  if (!irReceiverActive && irrecvPtr != nullptr) {
    irrecvPtr->enableIRIn();
    irReceiverActive = true;
  }
}
void stopIRReceiver() {
  if (irReceiverActive && irrecvPtr != nullptr) {
    irrecvPtr->disableIRIn();
    irReceiverActive = false;
  }
}

// ============================================================
// SAFE GPIO INITIALIZATION
// ============================================================

void initializeHardwarePins() {
  pinMode(pinIrTx, OUTPUT);
  digitalWrite(pinIrTx, LOW);
  pinMode(pinIrRx, INPUT);
  pinMode(PIN_LEARN_BUTTON, INPUT_PULLUP);
  pinMode(pinDs18b20, INPUT);
  pinMode(PIN_FLASH_CS, OUTPUT);
  digitalWrite(PIN_FLASH_CS, HIGH);
  SPI.begin();
  pinMode(PIN_FLASH_CS, OUTPUT);
  digitalWrite(PIN_FLASH_CS, HIGH);
  statusLedInitialize();
}

void hardwareSelfTest() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("--- HARDWARE FOUNDATION SELF-TEST ---"));
  Serial.println(F("Learn button      : DISABLED; GPIO0 reserved for boot strap"));
  Serial.print(F("IR TX default LOW : ")); Serial.println((digitalRead(pinIrTx) == LOW) ? F("PASS") : F("FAIL"));
  Serial.print(F("W25Qxx CS idle HIGH: ")); Serial.println((digitalRead(PIN_FLASH_CS) == HIGH) ? F("PASS") : F("FAIL"));
  Serial.print(F("DS18B20 GPIO       : GPIO")); Serial.println(pinDs18b20);
  Serial.print(F("IR RX GPIO         : GPIO")); Serial.println(pinIrRx);
  Serial.print(F("IR TX GPIO         : GPIO")); Serial.println(pinIrTx);
  Serial.print(F("Status LED GPIO    : GPIO")); Serial.println(pinStatusLed);
  Serial.println(F("SPI pins           : GPIO14 SCLK / GPIO12 MISO / GPIO13 MOSI"));
  Serial.println(F("W25Qxx access      : persistent storage"));
  Serial.println(F("IR receiver idle   : DISABLED"));
  Serial.println(F("--------------------------------------"));
  Serial.println();
#endif
}

// ============================================================
// CAPTURE VALIDATION
// ============================================================

bool calculateConvertedRawLength(const decode_results &capture, uint16_t destinationCapacity, uint16_t &requiredLength) {
  requiredLength = 0;
  if (capture.rawbuf == nullptr) return false;
  if (capture.rawlen < 2 || capture.rawlen > IRRECV_BUFFER_SIZE) return false;
  uint32_t required = 0;
  for (uint16_t i = 1; i < capture.rawlen; i++) {
    const uint32_t usecs = static_cast<uint32_t>(capture.rawbuf[i]) * kRawTick;
    if (usecs == 0) return false;
    if (usecs > UINT16_MAX) {
      const uint32_t chunks = static_cast<uint32_t>((usecs + static_cast<uint32_t>(UINT16_MAX) - 1UL) / static_cast<uint32_t>(UINT16_MAX));
      required += static_cast<uint32_t>((chunks * 2UL) - 1UL);
    } else required += 1UL;
    if (required > destinationCapacity) return false;
  }
  requiredLength = static_cast<uint16_t>(required);
  return requiredLength > 0;
}

bool validateCapture(const decode_results &capture) {
  if (capture.overflow) { DBG_PRINTLN(F("ERROR: capture overflow.")); return false; }
  if (capture.rawlen < 2) { DBG_PRINTLN(F("ERROR: raw capture is too short.")); return false; }
  if (capture.rawlen > IRRECV_BUFFER_SIZE) { DBG_PRINTLN(F("ERROR: raw capture exceeds receiver buffer size.")); return false; }
  uint16_t requiredLength = 0;
  if (!calculateConvertedRawLength(capture, RAW_WAVEFORM_CAPACITY, requiredLength)) {
    DBG_PRINTLN(F("ERROR: raw capture cannot fit converted buffer.")); return false;
  }
  return true;
}

// ============================================================
// FIXED-BUFFER RAW CONVERSION
// ============================================================

bool convertCaptureToRaw(const decode_results &capture, uint16_t *destination, uint16_t destinationCapacity, uint16_t &outputLength) {
  outputLength = 0;
  uint16_t requiredLength = 0;
  if (!calculateConvertedRawLength(capture, destinationCapacity, requiredLength)) return false;
  uint16_t pos = 0;
  for (uint16_t i = 1; i < capture.rawlen; i++) {
    uint32_t usecs = static_cast<uint32_t>(capture.rawbuf[i]) * kRawTick;
    while (usecs > UINT16_MAX) {
      if (static_cast<uint32_t>(pos) + 2UL > destinationCapacity) { outputLength = 0; return false; }
      destination[pos++] = UINT16_MAX;
      destination[pos++] = 0;
      usecs -= UINT16_MAX;
    }
    if (pos >= destinationCapacity || usecs == 0) { outputLength = 0; return false; }
    destination[pos++] = static_cast<uint16_t>(usecs);
  }
  if (pos != requiredLength || pos == 0) { outputLength = 0; return false; }
  outputLength = pos;
  return true;
}

// ============================================================
// STORE CAPTURE
// ============================================================

bool storeCapture(const decode_results &capture) {
  if (capture.overflow) return false;
  // storageRecordBuffer is idle while an IR learn is in progress; use it
  // as scratch. The buffer is alignas(4), so the uint16_t* cast is safe.
  uint16_t *scratch = reinterpret_cast<uint16_t *>(storageRecordBuffer);
  uint16_t newLength = 0;
  if (!convertCaptureToRaw(capture, scratch, RAW_WAVEFORM_CAPACITY, newLength)) return false;
  const uint32_t newCrc32 = crc32Buffer(scratch, newLength);
  memcpy(lastRaw, scratch, static_cast<size_t>(newLength) * sizeof(uint16_t));
  lastRawLen = newLength;
  lastRawCrc32 = newCrc32;
  if (newLength > irStats.maxStoredRawLen) irStats.maxStoredRawLen = newLength;
  const uint32_t duration = calculateWaveformDurationUs(lastRaw, lastRawLen);
  if (duration > irStats.maxWaveformDurationUs && duration != UINT32_MAX) irStats.maxWaveformDurationUs = duration;
  updateMinimumHeap();
  return storedSignalIntegrityOK();
}

void printCaptureInfo(const decode_results &capture) {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) { (void)capture; return; }
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("IR CAPTURE"));
  Serial.println(F("========================================"));
  Serial.print(F("Protocol : ")); Serial.println(typeToString(capture.decode_type));
  Serial.print(F("Repeat   : ")); Serial.println(capture.repeat ? F("YES") : F("NO"));
  Serial.print(F("Bits     : ")); Serial.println(capture.bits);
  Serial.print(F("Raw len  : ")); Serial.println(capture.rawlen);
  Serial.print(F("Overflow : ")); Serial.println(capture.overflow ? F("YES") : F("NO"));
  Serial.print(F("Code     : ")); Serial.println(resultToHexidecimal(&capture));
  uint16_t convertedLength = 0;
  if (calculateConvertedRawLength(capture, RAW_WAVEFORM_CAPACITY, convertedLength)) {
    Serial.print(F("Converted length : ")); Serial.println(convertedLength);
  } else Serial.println(F("Converted length : INVALID"));
  Serial.print(F("Free heap         : ")); Serial.println(ESP.getFreeHeap());
  Serial.println(F("========================================"));
  Serial.println();
#else
  (void)capture;
#endif
}

void printStoredRaw() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  if (!hasLearnedSignal()) { Serial.println(F("No learned signal is stored.")); return; }
  if (irState != IrEngineState::IDLE) { Serial.println(F("ERROR: IR engine is busy.")); return; }
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("STORED RAW DATA"));
  Serial.println(F("========================================"));
  Serial.print(F("Length: ")); Serial.println(lastRawLen);
  Serial.print(F("CRC32 : 0x")); Serial.println(lastRawCrc32, HEX);
  for (uint16_t i = 0; i < lastRawLen; i++) {
    if ((i % 8) == 0) { Serial.println(); Serial.print(F("  ")); }
    Serial.print(lastRaw[i]);
    if (i < lastRawLen - 1) Serial.print(F(", "));
    if ((i % 32) == 31) feedWatchdog();
  }
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println();
#endif
}

// ============================================================
// WATCHDOG-SAFE RAW TRANSMISSION
// ============================================================

void sendStoredRawFrame() {
  if (irsendPtr == nullptr) return;
  irsendPtr->enableIROut(txFrequencyHz);
  for (uint16_t i = 0; i < lastRawLen; i++) {
    if (i & 1U) irsendPtr->space(lastRaw[i]);
    else irsendPtr->mark(lastRaw[i]);
    if ((i + 1U) % TX_WDT_FEED_INTERVAL == 0) ESP.wdtFeed();
  }
  irsendPtr->space(0);
}

#if STAGE1_SERIAL_DEBUG
void discardSerialInputDuringCriticalTask() {
  if (!serialEnabled) return;
  while (Serial.available()) (void)Serial.read();
  serialLineLength = 0;
  serialLineOverflow = false;
}
#endif

void resynchronizeLearnButtonAfterCriticalTask() {}

// ============================================================
// TRANSMIT RAW CAPTURE
// ============================================================

constexpr uint16_t CHANNEL_MARK_HZ  = MIN_TX_FREQUENCY_HZ;   // marks a "channel" command record
constexpr uint16_t CHANNEL_MARK_GAP = 65535U;
bool irChannelTryTransmit(bool &handledOut);
bool transmitLastCapture() {
  { bool handled = false; const bool queued = irChannelTryTransmit(handled); if (handled) return queued; }
  if (irState != IrEngineState::IDLE) { DBG_PRINTLN(F("ERROR: IR engine is busy.")); irStats.transmissionRejected++; return false; }
  if (irsendPtr == nullptr) { DBG_PRINTLN(F("ERROR: IR transmitter not initialized.")); irStats.transmissionRejected++; return false; }
  if (!hasLearnedSignal()) { DBG_PRINTLN(F("ERROR: stored waveform integrity check failed or no signal is stored.")); irStats.transmissionRejected++; return false; }
  if (txRepeatCount == 0) { DBG_PRINTLN(F("ERROR: repeat count must be at least 1.")); irStats.transmissionRejected++; return false; }
  if (txFrequencyHz < MIN_TX_FREQUENCY_HZ || txFrequencyHz > MAX_TX_FREQUENCY_HZ) {
    DBG_PRINTLN(F("ERROR: TX frequency is outside Stage 1 limits.")); irStats.transmissionRejected++; return false;
  }
  irState = IrEngineState::TRANSMITTING;
  irStats.transmissionsStarted++;
#if STAGE1_SERIAL_DEBUG
  discardSerialInputDuringCriticalTask();
#endif
  DBG_PRINTLN_EMPTY();
  DBG_PRINTLN(F("========================================"));
  DBG_PRINTLN(F("IR TRANSMISSION"));
  DBG_PRINT(F("Raw length : ")); DBG_PRINTLN(lastRawLen);
  DBG_PRINT(F("Frequency  : ")); DBG_PRINT(txFrequencyHz); DBG_PRINTLN(F(" Hz"));
  DBG_PRINT(F("Repeats    : ")); DBG_PRINTLN(txRepeatCount);
  DBG_PRINT(F("Gap        : ")); DBG_PRINT(txRepeatGapMs); DBG_PRINTLN(F(" ms"));
  stopIRReceiver();
  digitalWrite(pinIrTx, LOW);
  delay(2);
  for (uint16_t i = 0; i < txRepeatCount; i++) {
    sendStoredRawFrame();
    ESP.wdtFeed();
    if (i + 1U < txRepeatCount) delay(txRepeatGapMs);
  }
  digitalWrite(pinIrTx, LOW);
  delay(5);
  irState = IrEngineState::IDLE;
  irStats.transmissionsCompleted++;
#if STAGE1_SERIAL_DEBUG
  discardSerialInputDuringCriticalTask();
#endif
  resynchronizeLearnButtonAfterCriticalTask();
  updateMinimumHeap();
  DBG_PRINTLN(F("Transmission complete."));
  DBG_PRINTLN(F("========================================"));
  DBG_PRINTLN_EMPTY();
  return true;
}

// ============================================================
// LEARN ONE SIGNAL
// ============================================================

bool learnSignal() {
  if (irState != IrEngineState::IDLE) { DBG_PRINTLN(F("ERROR: IR engine is busy.")); return false; }
  if (irrecvPtr == nullptr) { DBG_PRINTLN(F("ERROR: IR receiver not initialized.")); return false; }
  irState = IrEngineState::LEARNING;
  irStats.learnAttempts++;
#if STAGE1_SERIAL_DEBUG
  discardSerialInputDuringCriticalTask();
#endif
  DBG_PRINTLN_EMPTY();
  DBG_PRINTLN(F("========================================"));
  DBG_PRINTLN(F("LEARN MODE"));
  DBG_PRINTLN(F("========================================"));
  DBG_PRINTLN(F("Point the remote at the IR receiver."));
  DBG_PRINTLN(F("Press the desired remote button ONCE."));
  DBG_PRINTLN(F("15 second Learn-session timeout."));
  DBG_PRINTLN_EMPTY();
  digitalWrite(pinIrTx, LOW);
  stopIRReceiver();
  startIRReceiver();
  const uint32_t learnStart = millis();
#if STAGE1_SERIAL_DEBUG
  uint32_t lastStatus = learnStart;
#endif
  while ((millis() - learnStart) < LEARN_SESSION_TIMEOUT_MS) {
    serviceSystemBackgroundOnly();
    if (irrecvPtr->decode(&results)) {
      stopIRReceiver();
      if (results.rawlen > irStats.maxCapturedRawLen) irStats.maxCapturedRawLen = results.rawlen;
      DBG_PRINTLN(F("Signal captured."));
      printCaptureInfo(results);
      if (!validateCapture(results)) {
        if (results.overflow) irStats.learnOverflows++; else irStats.malformedCaptures++;
        DBG_PRINTLN(F("Capture rejected; continuing to wait."));
        startIRReceiver();
      } else if (!storeCapture(results)) {
        irStats.storeFailures++;
        DBG_PRINTLN(F("ERROR: captured waveform could not be stored."));
        DBG_PRINTLN(F("Previous learned signal was preserved."));
        startIRReceiver();
      } else {
        lastLearnCarrierHz = txFrequencyHz;
        lastLearnRepeatCount = txRepeatCount;
        lastLearnRepeatGapMs = txRepeatGapMs;
        lastLearnMetadataValid = true;
        irStats.successfulLearns++;
        DBG_PRINT(F("Raw waveform saved: ")); DBG_PRINT(lastRawLen); DBG_PRINTLN(F(" timings."));
        DBG_PRINT(F("Learn TX profile: ")); DBG_PRINT(lastLearnCarrierHz);
        DBG_PRINT(F(" Hz / repeat ")); DBG_PRINT(lastLearnRepeatCount);
        DBG_PRINT(F(" / gap ")); DBG_PRINT(lastLearnRepeatGapMs);
        DBG_PRINTLN(F(" ms"));
        irState = IrEngineState::IDLE;
#if STAGE1_SERIAL_DEBUG
        discardSerialInputDuringCriticalTask();
#endif
        resynchronizeLearnButtonAfterCriticalTask();
        updateMinimumHeap();
        DBG_PRINTLN(F("Learning successful."));
        DBG_PRINTLN(F("Use T to transmit/test the learned signal."));
        DBG_PRINTLN(F("========================================"));
        DBG_PRINTLN_EMPTY();
        return true;
      }
    }
#if STAGE1_SERIAL_DEBUG
    if (serialEnabled && (millis() - lastStatus) >= 1000UL) {
      const uint32_t now = millis();
      lastStatus = now;
      const uint32_t elapsed = now - learnStart;
      const uint32_t remaining = (elapsed < LEARN_SESSION_TIMEOUT_MS) ? (LEARN_SESSION_TIMEOUT_MS - elapsed) : 0;
      DBG_PRINT(F("Learning... ")); DBG_PRINT(remaining / 1000UL); DBG_PRINTLN(F(" s remaining"));
    }
#endif
  }
  stopIRReceiver();
  irState = IrEngineState::IDLE;
  irStats.learnTimeouts++;
#if STAGE1_SERIAL_DEBUG
  discardSerialInputDuringCriticalTask();
#endif
  resynchronizeLearnButtonAfterCriticalTask();
  updateMinimumHeap();
  DBG_PRINTLN(F("Learning timed out."));
  DBG_PRINTLN(F("No valid IR command was stored."));
  DBG_PRINTLN(F("Previous learned signal was preserved."));
  DBG_PRINTLN(F("========================================"));
  DBG_PRINTLN_EMPTY();
  return false;
}

// ============================================================
// W25Qxx PERSISTENT STORAGE ENGINE
// ============================================================

constexpr uint32_t FLASH_MANUFACTURER_WINBOND = 0xEFUL;
constexpr uint8_t FLASH_MEMORY_TYPE_WINBOND_SERIAL_NOR = 0x40;
constexpr uint8_t FLASH_CAPACITY_W25Qxx = 0x16;
constexpr uint8_t FLASH_CAPACITY_W25Q64 = 0x17;

constexpr uint32_t STORAGE_FLASH_SECTOR_SIZE = 4096UL;
constexpr uint16_t STORAGE_FLASH_PAGE_SIZE = 256;
constexpr uint32_t DB_MAGIC = 0x49524842UL;
constexpr uint32_t RECORD_MAGIC = 0x49524352UL;
constexpr uint32_t COMMIT_MAGIC = 0xC0DE17EDUL;
constexpr uint16_t STORAGE_FORMAT_VERSION = 1;
constexpr uint16_t RECORD_FORMAT_VERSION = 1;
constexpr uint8_t RECORD_TYPE_DEVICE = 1;
constexpr uint8_t RECORD_TYPE_COMMAND = 2;
constexpr uint8_t RECORD_TYPE_DELETE_DEVICE = 3;
constexpr uint8_t RECORD_TYPE_DELETE_COMMAND = 4;
constexpr uint8_t RECORD_TYPE_RULE = 5;
constexpr uint8_t RECORD_TYPE_DELETE_RULE = 6;
constexpr uint8_t RECORD_TYPE_SCHEDULE = 7;
constexpr uint8_t RECORD_TYPE_DELETE_SCHEDULE = 8;
constexpr uint8_t RECORD_TYPE_ORDER = 9;
constexpr uint8_t RECORD_TYPE_CONFIG_BACKUP = 10;
constexpr uint8_t RECORD_TYPE_DEVICE_SETTINGS  = 11;
constexpr uint8_t RECORD_TYPE_COMMAND_SETTINGS = 12;

// Must match CONFIG_EEPROM_SIZE in webui_backend.h. The static_assert
// near that definition guarantees the two stay in sync at compile time.
constexpr uint16_t STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES = 1264U;
// Must match sizeof(StoredRule) in the automation module.
constexpr uint16_t RULE_PAYLOAD_BYTES_V1 = 84U;    // legacy single-action blocks
constexpr uint16_t RULE_PAYLOAD_BYTES    = 292U;   // v2 compound rules
constexpr uint16_t SCHEDULE_PAYLOAD_BYTES = 12U;
constexpr uint8_t RECORD_FLAG_COMMITTED = 0x01U;
constexpr uint32_t DB_PRIMARY_SECTOR = 0;
constexpr uint32_t DB_BACKUP_SECTOR = 1;
constexpr uint32_t DB_DATA_START_SECTOR = 2;
constexpr uint8_t STORAGE_SPI_MHZ = 8;
constexpr size_t STORAGE_MAX_NAME = 31;
constexpr uint32_t STORAGE_HEADER_BYTES = 32UL;
constexpr uint32_t STORAGE_COMMIT_OFFSET = STORAGE_FLASH_SECTOR_SIZE - 4UL;

constexpr uint32_t FORMAT_INTENT_MAGIC = 0x46524D54UL;
constexpr uint16_t FORMAT_INTENT_VERSION = 1;
constexpr uint32_t FORMAT_INTENT_SLOT_SIZE = 32UL;

#pragma pack(push, 1)
struct DatabaseHeader {
  uint32_t magic;
  uint16_t formatVersion;
  uint16_t headerSize;
  uint32_t flashSizeBytes;
  uint32_t dataStartSector;
  uint32_t generation;
  uint32_t headerCrc32;
  uint32_t reserved;
  uint32_t commit;
};
struct StorageRecordHeader {
  uint32_t magic;
  uint16_t formatVersion;
  uint8_t type;
  uint8_t flags;
  uint32_t generation;
  uint32_t sequence;
  uint32_t recordId;
  uint32_t deviceId;
  uint16_t nameLength;
  uint16_t rawLength;
  uint16_t carrierHz;
  uint8_t repeatCount;
  uint8_t reserved0;
  uint16_t repeatGapMs;
  uint16_t payloadLength;
  uint32_t headerCrc32;
  uint32_t payloadCrc32;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct FormatIntent {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  uint32_t crc32;
  uint32_t commit;
};
#pragma pack(pop)

constexpr uint32_t FORMAT_INTENT_OFFSET = sizeof(DatabaseHeader);
constexpr uint32_t FORMAT_INTENT_SLOT_COUNT = (STORAGE_FLASH_SECTOR_SIZE - FORMAT_INTENT_OFFSET) / FORMAT_INTENT_SLOT_SIZE;
static_assert(sizeof(FormatIntent) == 20, "FormatIntent size must remain 20 bytes");
static_assert(FORMAT_INTENT_SLOT_SIZE >= sizeof(FormatIntent), "FormatIntent slot must fit the intent record");
static_assert(FORMAT_INTENT_SLOT_COUNT > 1, "FormatIntent journal needs multiple slots");
static_assert(FORMAT_INTENT_OFFSET + FORMAT_INTENT_SLOT_COUNT * FORMAT_INTENT_SLOT_SIZE <= STORAGE_FLASH_SECTOR_SIZE, "FormatIntent journal must fit in metadata sector");
static_assert(sizeof(DatabaseHeader) == STORAGE_HEADER_BYTES, "DatabaseHeader size must remain 32 bytes");
static_assert(sizeof(StorageRecordHeader) == 44, "StorageRecordHeader size must remain 44 bytes");

enum class StableReadResult : uint8_t { VALID, INVALID, UNCERTAIN };
enum class PayloadVerifyResult : uint8_t { VALID, CORRUPT, UNCERTAIN };
enum class FormatIntentFindResult : uint8_t { NONE, FOUND, UNCERTAIN };

StableReadResult storageReadDatabaseHeaderStable(uint32_t sector, DatabaseHeader &header);
bool flashReadDatabaseHeaderWithoutCommit(uint32_t sector, DatabaseHeader &header);
bool flashReadBytes(uint32_t address, uint8_t *buffer, uint16_t length);

bool flashReadSmallConfirmed(uint32_t address, uint8_t *buffer, uint16_t length) {
  if (buffer == nullptr || length == 0 || length > STORAGE_MAX_NAME) return false;
  uint8_t second[STORAGE_MAX_NAME + 1] = {0};
  uint8_t third[STORAGE_MAX_NAME + 1] = {0};
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (!flashReadBytes(address, buffer, length) ||
        !flashReadBytes(address, second, length) ||
        !flashReadBytes(address, third, length) ||
        memcmp(buffer, second, length) != 0 ||
        memcmp(buffer, third, length) != 0) {
      delay(1);
      continue;
    }
    return true;
  }
  return false;
}

bool storageReadRecordHeaderStable(uint32_t sector, StorageRecordHeader &header);
bool storageReadCommitStable(uint32_t sector, uint32_t &commit);
PayloadVerifyResult storageReadPayloadStable(uint32_t sector, const StorageRecordHeader &header);
bool storageReadPayloadVerifiedIntoBuffer(uint32_t sector, const StorageRecordHeader &header);
bool storageValidateRecordHeaderForBody(const StorageRecordHeader *header, uint32_t generation);
void storageQuarantineAppendSector(uint32_t sector, const StorageRecordHeader &header);
void storageQuarantineSector(uint32_t sector);
void storageResetDeviceIndex();
void storageRegisterDeviceIndex(uint32_t deviceId, uint32_t sector, uint16_t nameCrc, uint32_t sequence);
bool storageRecoverDeviceIndexEntry(uint32_t deviceId);
bool storageHasIndexedValidDeviceId(uint32_t deviceId);
DeviceLookupResult storageFindDeviceByNamePersistent(const char *name, uint16_t nameCrc, size_t nameLength, uint32_t &deviceIdOut);
bool storageDeviceIdIsUniquePersistent(uint32_t deviceId, uint32_t canonicalSector);
uint32_t storageMaxPhysicalIdentity();
bool storageIsDeviceIdentityConflicted(uint32_t deviceId);
bool storageIsCommandIdentityConflicted(uint32_t commandId);
bool storageWasDeviceIdentitySeen(uint32_t deviceId);
bool storageWasCommandIdentitySeen(uint32_t commandId);
void storageMarkDeviceIdentitySeen(uint32_t deviceId);
void storageMarkDeviceIdentityConflicted(uint32_t deviceId);
void storageMarkCommandIdentitySeen(uint32_t commandId);
void storageMarkCommandIdentityConflicted(uint32_t commandId);
void storageRemoveDeviceIndexEntries(uint32_t deviceId);
bool storageIsDeviceDeleted(uint32_t deviceId);
bool storageIsCommandDeleted(uint32_t commandId);
bool storageAppendDeleteRecord(uint8_t deleteType, uint32_t targetId, uint32_t targetDeviceId, uint32_t &sectorOut, uint32_t &sequenceOut);
bool storageDeleteDevice(uint32_t deviceId);
bool storageDeleteCommand(uint32_t commandId);
bool storageRelearnCommand(uint32_t commandId);
bool storageRenameDevice(uint32_t deviceId, const char *newName);
bool storageRenameCommand(uint32_t commandId, const char *newName);
bool storageResetDatabaseTargeted();
FormatIntentFindResult storageFindFormatIntent(uint32_t &sectorOut, uint32_t &generationOut);
bool storageDataAreaHeadersBlank();
bool storageWipeDatabase();
bool storageJobIsRunning();
bool storageJobStartReset();
bool storageJobStartWipe();
void storageJobService();
uint8_t storageJobGetPercent();
void storageJobFail(const char *reason);
void storageJobFail(const char *reason);
void storageJobFail(const char *reason);
void storageJobFail(const char *reason);

enum class StorageJobKind : uint8_t { NONE, RESET, WIPE };
enum class StorageJobState : uint8_t { IDLE, RUNNING, DONE, FAILED };

struct StorageJobRuntime {
  StorageJobKind  kind       = StorageJobKind::NONE;
  StorageJobState state      = StorageJobState::IDLE;  uint8_t         phase      = 0;
  uint32_t        cursor     = 0;
  uint32_t        total      = 0;
  uint32_t        processed  = 0;
  uint32_t        failed     = 0;
  uint32_t        newGen     = 0;
  uint32_t        hdrPrimary = DB_PRIMARY_SECTOR;
  uint32_t        hdrBackup  = DB_BACKUP_SECTOR;
  uint32_t        startedMs  = 0;
  uint32_t        finishedMs = 0;
  char            message[80] = {0};
};
static StorageJobRuntime storageJob;
bool commandEqualsIgnoreCase(const char *a, const char *b);
bool commandStartsWithIgnoreCase(const char *text, const char *prefix);

SPISettings flashSpiSettings(1000000UL * STORAGE_SPI_MHZ, MSBFIRST, SPI_MODE0);

struct StorageState {
  bool detected = false;
  bool initialized = false;
  bool headerCorrupt = false;
  uint32_t flashSizeBytes = 0;
  uint32_t sectorCount = 0;
  uint8_t manufacturer = 0;
  uint8_t memoryType = 0;
  uint8_t capacityCode = 0;
  uint32_t generation = 0;
  uint32_t maxSequence = 0;
  uint32_t maxCommandId = 0;
  uint32_t maxDeviceId = 0;
  uint32_t validRecordCount = 0;
  uint32_t corruptRecordCount = 0;
  uint32_t obsoleteRecordCount = 0;
  uint32_t usedSectorCount = 0;
  uint32_t freeSectorCount = 0;
  uint32_t firstFreeSector = UINT32_MAX;
  bool freeSpaceIndexValid = false;
  uint32_t latestCommandSector = UINT32_MAX;
  uint32_t latestCommandId = 0;
  uint32_t latestCommandDeviceId = 0;
  uint32_t currentDeviceId = 0;
  char currentDeviceName[STORAGE_MAX_NAME + 1] = {0};
  uint32_t currentCommandId = 0;
  char currentCommandName[STORAGE_MAX_NAME + 1] = {0};
};
StorageState storage;

void clearCurrentCommandRuntimeMetadata() {
  storage.currentCommandId = 0;
  storage.currentCommandName[0] = '\0';
}

constexpr size_t STORAGE_MAX_PAYLOAD_LENGTH = STORAGE_MAX_NAME + (RAW_WAVEFORM_CAPACITY * sizeof(uint16_t));
constexpr size_t STORAGE_RECORD_BUFFER_SIZE = sizeof(StorageRecordHeader) + STORAGE_MAX_PAYLOAD_LENGTH;
static_assert(STORAGE_RECORD_BUFFER_SIZE <= STORAGE_FLASH_SECTOR_SIZE - 4UL, "record buffer must fit inside a data sector");
static_assert(STORAGE_RECORD_BUFFER_SIZE == SHARED_RECORD_BUFFER_BYTES,
              "shared staging buffer size must match the storage record layout");
// storageRecordBuffer itself is defined near the top of the file so the
// IR capture path can also use it as scratch.

constexpr uint32_t STORAGE_MAX_SECTORS = 2048UL;
constexpr size_t STORAGE_QUARANTINE_BITMAP_BYTES = (STORAGE_MAX_SECTORS + 7UL) / 8UL;

static uint8_t storageSeenDeviceIdBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageDeviceIdConflictBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageSeenCommandIdBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageCommandIdConflictBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageDeletedDeviceBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageDeletedCommandBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageQuarantineBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};

// ── v1.0: RAM caches for per-entity settings ─────────────────
// Populated in storageScanDatabase(); updated by the setters.
// Cleared by storageResetScanState()/storageResetRuntimeDatabaseState().
static uint8_t storageCommandAlexaBitmap[STORAGE_QUARANTINE_BITMAP_BYTES] = {0};
static uint8_t storageDeviceCloudMap[256] = {0};   // bit7=cloud, bits[3:0]=category

constexpr uint16_t STORAGE_DEVICE_CACHE_SIZE = 256;
struct DeviceIndexEntry {
  uint16_t id;
  uint16_t sequence;
  uint16_t sector;
  uint16_t nameCrc;
};
static_assert(sizeof(DeviceIndexEntry) == 8, "unexpected DeviceIndexEntry padding");
static DeviceIndexEntry storageDeviceIndex[STORAGE_DEVICE_CACHE_SIZE] = {};
static uint16_t storageDeviceIndexCount = 0;
static uint16_t storageDeviceIndexReplaceCursor = 0;

uint32_t crc32Bytes(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0UL - (crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

void storageCS(bool high) { digitalWrite(PIN_FLASH_CS, high ? HIGH : LOW); }

bool flashReadBytes(uint32_t address, uint8_t *buffer, uint16_t length) {
  if (!storage.detected || length == 0 || address >= storage.flashSizeBytes ||
      static_cast<uint64_t>(address) + length > storage.flashSizeBytes) return false;
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x03);
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
  for (uint16_t i = 0; i < length; ++i) buffer[i] = SPI.transfer(0x00);
  storageCS(true);
  SPI.endTransaction();
  return true;
}

uint8_t flashReadStatus1() {
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x05);
  const uint8_t value = SPI.transfer(0x00);
  storageCS(true);
  SPI.endTransaction();
  return value;
}

bool flashWaitReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (flashReadStatus1() & 0x01U) {
    yield();
    if ((uint32_t)(millis() - start) >= timeoutMs) return false;
  }
  return true;
}

bool flashWriteEnable() {
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x06);
  storageCS(true);
  SPI.endTransaction();
  return (flashReadStatus1() & 0x02U) != 0;
}

bool flashProgramPage(uint32_t address, const uint8_t *data, uint16_t length) {
  if (!storage.detected || length == 0 || length > STORAGE_FLASH_PAGE_SIZE) return false;
  if (address >= storage.flashSizeBytes || static_cast<uint64_t>(address) + length > storage.flashSizeBytes) return false;
  const uint16_t pageOffset = static_cast<uint16_t>(address & 0xFFUL);
  if (static_cast<uint32_t>(pageOffset) + length > STORAGE_FLASH_PAGE_SIZE) return false;
  if (!flashWriteEnable()) return false;
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x02);
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
  for (uint16_t i = 0; i < length; ++i) SPI.transfer(data[i]);
  storageCS(true);
  SPI.endTransaction();
  return flashWaitReady(1000UL);
}

bool flashProgramBytes(uint32_t address, const uint8_t *data, uint16_t length) {
  while (length > 0) {
    const uint16_t pageOffset = static_cast<uint16_t>(address & 0xFFUL);
    const uint16_t room = static_cast<uint16_t>(STORAGE_FLASH_PAGE_SIZE - pageOffset);
    const uint16_t chunk = (length < room) ? length : room;
    if (!flashProgramPage(address, data, chunk)) return false;
    address += chunk; data += chunk; length = static_cast<uint16_t>(length - chunk);
  }
  return true;
}

bool flashEraseSector(uint32_t sector) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  const uint32_t address = sector * STORAGE_FLASH_SECTOR_SIZE;
  if (!flashWriteEnable()) return false;
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x20);
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
  storageCS(true);
  SPI.endTransaction();
  return flashWaitReady(5000UL);
}

constexpr uint8_t STORAGE_BLANK_CHUNK_VERIFY_ATTEMPTS = 3;
constexpr uint16_t STORAGE_BLANK_CHUNK_RETRY_DELAY_MS = 2;

bool flashIsSectorBlank(uint32_t sector) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  uint8_t first[256];
  uint8_t second[256];
  uint8_t third[256];
  const uint32_t base = sector * STORAGE_FLASH_SECTOR_SIZE;
  for (uint16_t offset = 0; offset < STORAGE_FLASH_SECTOR_SIZE; offset += sizeof(first)) {
    bool chunkVerified = false;
    for (uint8_t attempt = 0; attempt < STORAGE_BLANK_CHUNK_VERIFY_ATTEMPTS; ++attempt) {
      if (attempt > 0) { delay(STORAGE_BLANK_CHUNK_RETRY_DELAY_MS); yield(); }
      if (!flashReadBytes(base + offset, first, sizeof(first)) ||
          !flashReadBytes(base + offset, second, sizeof(second)) ||
          memcmp(first, second, sizeof(first)) != 0) continue;
      if (!flashReadBytes(base + offset, third, sizeof(third)) ||
          memcmp(first, third, sizeof(first)) != 0) continue;
      bool allBlank = true;
      for (size_t i = 0; i < sizeof(first); ++i) if (first[i] != 0xFFU) { allBlank = false; break; }
      if (allBlank) { chunkVerified = true; break; }
    }
    if (!chunkVerified) return false;
    yield();
  }
  return true;
}

constexpr uint8_t STORAGE_ERASE_VERIFY_ATTEMPTS = 3;
constexpr uint8_t STORAGE_ERASE_POSTFAIL_VERIFY_ATTEMPTS = 3;
constexpr uint16_t STORAGE_ERASE_RETRY_DELAY_MS = 5;

bool flashEraseSectorVerified(uint32_t sector) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  if (flashIsSectorBlank(sector)) return true;
  for (uint8_t attempt = 0; attempt < STORAGE_ERASE_VERIFY_ATTEMPTS; ++attempt) {
    if (attempt > 0) { delay(STORAGE_ERASE_RETRY_DELAY_MS); yield(); }
    if (!flashWaitReady(5000UL)) continue;
    const bool eraseOk = flashEraseSector(sector);
    if (!eraseOk) continue;
    if (flashIsSectorBlank(sector)) return true;
    for (uint8_t verifyAttempt = 1; verifyAttempt < STORAGE_ERASE_POSTFAIL_VERIFY_ATTEMPTS; ++verifyAttempt) {
      delay(STORAGE_ERASE_RETRY_DELAY_MS); yield();
      if (flashIsSectorBlank(sector)) return true;
    }
  }
  return false;
}

bool flashReadJedec(uint8_t &manufacturer, uint8_t &memoryType, uint8_t &capacityCode) {
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x9F);
  manufacturer = SPI.transfer(0x00);
  memoryType = SPI.transfer(0x00);
  capacityCode = SPI.transfer(0x00);
  storageCS(true);
  SPI.endTransaction();
  return true;
}

uint32_t flashSizeFromCapacity(uint8_t capacityCode) {
  if (capacityCode == FLASH_CAPACITY_W25Qxx) return 4UL * 1024UL * 1024UL;
  if (capacityCode == FLASH_CAPACITY_W25Q64) return 8UL * 1024UL * 1024UL;
  return 0;
}

bool storageDetectFlash() {
  if (!flashReadJedec(storage.manufacturer, storage.memoryType, storage.capacityCode)) return false;
  storage.flashSizeBytes = flashSizeFromCapacity(storage.capacityCode);
  if (storage.manufacturer != FLASH_MANUFACTURER_WINBOND ||
      storage.memoryType != FLASH_MEMORY_TYPE_WINBOND_SERIAL_NOR ||
      storage.flashSizeBytes == 0) return false;
  storage.sectorCount = storage.flashSizeBytes / STORAGE_FLASH_SECTOR_SIZE;
  storage.detected = true;
  return true;
}

StableReadResult storageReadDatabaseHeaderStable(uint32_t sector, DatabaseHeader &header) {
  if (!storage.detected || sector >= storage.sectorCount) return StableReadResult::UNCERTAIN;
  uint8_t first[sizeof(DatabaseHeader)];
  uint8_t second[sizeof(DatabaseHeader)];
  const uint32_t address = sector * STORAGE_FLASH_SECTOR_SIZE;
  if (!flashReadBytes(address, first, sizeof(first)) ||
      !flashReadBytes(address, second, sizeof(second)) ||
      memcmp(first, second, sizeof(first)) != 0 ||
      !flashReadBytes(address, first, sizeof(first)) ||
      memcmp(first, second, sizeof(first)) != 0) return StableReadResult::UNCERTAIN;
  memcpy(&header, first, sizeof(header));
  if (header.magic != DB_MAGIC || header.formatVersion != STORAGE_FORMAT_VERSION ||
      header.headerSize != sizeof(DatabaseHeader) ||
      header.flashSizeBytes != storage.flashSizeBytes ||
      header.dataStartSector != DB_DATA_START_SECTOR ||
      header.generation == 0 || header.commit != COMMIT_MAGIC) return StableReadResult::INVALID;
  const uint32_t suppliedCrc = header.headerCrc32;
  const uint32_t suppliedCommit = header.commit;
  header.headerCrc32 = 0;
  header.commit = 0;
  const uint32_t calculated = crc32Bytes(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  header.headerCrc32 = suppliedCrc;
  header.commit = suppliedCommit;
  return suppliedCrc == calculated ? StableReadResult::VALID : StableReadResult::INVALID;
}

bool storageReadDatabaseHeader(uint32_t sector, DatabaseHeader &header) {
  return storageReadDatabaseHeaderStable(sector, header) == StableReadResult::VALID;
}

bool storageWriteDatabaseHeader(uint32_t sector, uint32_t generation) {
  if (!storage.detected || sector >= storage.sectorCount || generation == 0) return false;
  if (!flashEraseSectorVerified(sector)) return false;
  DatabaseHeader header{};
  header.magic = DB_MAGIC;
  header.formatVersion = STORAGE_FORMAT_VERSION;
  header.headerSize = sizeof(DatabaseHeader);
  header.flashSizeBytes = storage.flashSizeBytes;
  header.dataStartSector = DB_DATA_START_SECTOR;
  header.generation = generation;
  header.headerCrc32 = 0;
  header.reserved = 0;
  header.commit = 0;
  header.headerCrc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  const uint32_t bodyLength = offsetof(DatabaseHeader, commit);
  if (!flashProgramBytes(sector * STORAGE_FLASH_SECTOR_SIZE,
                         reinterpret_cast<const uint8_t *>(&header),
                         static_cast<uint16_t>(bodyLength))) return false;
  DatabaseHeader verify{};
  if (!flashReadDatabaseHeaderWithoutCommit(sector, verify)) return false;
  const uint32_t commit = COMMIT_MAGIC;
  if (!flashProgramBytes(sector * STORAGE_FLASH_SECTOR_SIZE + offsetof(DatabaseHeader, commit),
                         reinterpret_cast<const uint8_t *>(&commit), sizeof(commit))) return false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    DatabaseHeader finalVerify{};
    if (storageReadDatabaseHeader(sector, finalVerify) &&
        finalVerify.generation == generation) return true;
    delay(2);
  }
  return false;
}

bool flashReadDatabaseHeaderWithoutCommit(uint32_t sector, DatabaseHeader &header) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  uint8_t first[sizeof(DatabaseHeader)];
  uint8_t second[sizeof(DatabaseHeader)];
  const uint32_t address = sector * STORAGE_FLASH_SECTOR_SIZE;
  if (!flashReadBytes(address, first, sizeof(first)) ||
      !flashReadBytes(address, second, sizeof(second)) ||
      memcmp(first, second, sizeof(first)) != 0 ||
      !flashReadBytes(address, first, sizeof(first)) ||
      memcmp(first, second, sizeof(first)) != 0) return false;
  memcpy(&header, first, sizeof(header));
  if (header.magic != DB_MAGIC || header.formatVersion != STORAGE_FORMAT_VERSION ||
      header.headerSize != sizeof(DatabaseHeader) ||
      header.flashSizeBytes != storage.flashSizeBytes ||
      header.dataStartSector != DB_DATA_START_SECTOR ||
      header.generation == 0) return false;
  const uint32_t suppliedCrc = header.headerCrc32;
  const uint32_t suppliedCommit = header.commit;
  header.headerCrc32 = 0;
  header.commit = 0;
  const uint32_t calculated = crc32Bytes(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  header.headerCrc32 = suppliedCrc;
  header.commit = suppliedCommit;
  return suppliedCrc == calculated;
}

bool storageIsRecordHeaderBlank(const StorageRecordHeader &header) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&header);
  for (size_t i = 0; i < sizeof(header); ++i) if (p[i] != 0xFFU) return false;
  return true;
}

bool storageReadRecordHeaderStable(uint32_t sector, StorageRecordHeader &header) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  uint8_t first[sizeof(StorageRecordHeader)];
  uint8_t second[sizeof(StorageRecordHeader)];
  const uint32_t address = sector * STORAGE_FLASH_SECTOR_SIZE;
  if (!flashReadBytes(address, first, sizeof(first)) ||
      !flashReadBytes(address, second, sizeof(second)) ||
      memcmp(first, second, sizeof(first)) != 0 ||
      !flashReadBytes(address, first, sizeof(first)) ||
      memcmp(first, second, sizeof(first)) != 0) return false;
  memcpy(&header, first, sizeof(header));
  return true;
}

bool storageReadCommitStable(uint32_t sector, uint32_t &commit) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  uint8_t first[sizeof(uint32_t)];
  uint8_t second[sizeof(uint32_t)];
  const uint32_t address = sector * STORAGE_FLASH_SECTOR_SIZE + STORAGE_COMMIT_OFFSET;
  if (!flashReadBytes(address, first, sizeof(first)) ||
      !flashReadBytes(address, second, sizeof(second)) ||
      memcmp(first, second, sizeof(first)) != 0 ||
      !flashReadBytes(address, first, sizeof(first)) ||
      memcmp(first, second, sizeof(first)) != 0) return false;
  memcpy(&commit, first, sizeof(commit));
  return true;
}

uint32_t crc32UpdateByte(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint32_t mask = 0UL - (crc & 1UL);
    crc = (crc >> 1) ^ (0xEDB88320UL & mask);
  }
  return crc;
}

bool flashReadCrc32Streaming(uint32_t address, uint16_t length, uint32_t &crcOut) {
  if (!storage.detected || length == 0 || address >= storage.flashSizeBytes ||
      static_cast<uint64_t>(address) + length > storage.flashSizeBytes) return false;
  uint32_t crc = 0xFFFFFFFFUL;
  SPI.beginTransaction(flashSpiSettings);
  storageCS(false);
  SPI.transfer(0x03);
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
  for (uint16_t i = 0; i < length; ++i) {
    crc = crc32UpdateByte(crc, SPI.transfer(0x00));
    if ((i & 0xFFU) == 0xFFU) ESP.wdtFeed();
  }
  storageCS(true);
  SPI.endTransaction();
  crcOut = ~crc;
  return true;
}

PayloadVerifyResult storageReadPayloadStable(uint32_t sector, const StorageRecordHeader &header) {
  if (!storage.detected || sector >= storage.sectorCount || header.payloadLength > sizeof(storageRecordBuffer))
    return PayloadVerifyResult::CORRUPT;
  const uint32_t payloadAddress = sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader);
  if (static_cast<uint64_t>(payloadAddress) + header.payloadLength > storage.flashSizeBytes - 4ULL)
    return PayloadVerifyResult::CORRUPT;
  if (header.payloadLength == 0)
    return (header.payloadCrc32 == crc32Bytes(nullptr, 0)) ? PayloadVerifyResult::VALID : PayloadVerifyResult::CORRUPT;
  if (!flashReadBytes(payloadAddress, storageRecordBuffer, header.payloadLength))
    return PayloadVerifyResult::UNCERTAIN;
  const uint32_t crc1 = crc32Bytes(storageRecordBuffer, header.payloadLength);
  uint32_t crc2 = 0;
  if (!flashReadCrc32Streaming(payloadAddress, header.payloadLength, crc2)) return PayloadVerifyResult::UNCERTAIN;
  uint32_t crc3 = 0;
  if (!flashReadCrc32Streaming(payloadAddress, header.payloadLength, crc3)) return PayloadVerifyResult::UNCERTAIN;
  const uint32_t expected = header.payloadCrc32;
  const uint8_t expectedCount = static_cast<uint8_t>((crc1 == expected) + (crc2 == expected) + (crc3 == expected));
  if (expectedCount >= 2) {
    if (crc1 != expected) {
      if (!flashReadBytes(payloadAddress, storageRecordBuffer, header.payloadLength) ||
          crc32Bytes(storageRecordBuffer, header.payloadLength) != expected)
        return PayloadVerifyResult::UNCERTAIN;
    }
    return PayloadVerifyResult::VALID;
  }
  if (crc1 == crc2 && crc2 == crc3 && crc1 != expected) return PayloadVerifyResult::CORRUPT;
  return PayloadVerifyResult::UNCERTAIN;
}

bool storageReadPayloadVerifiedIntoBuffer(uint32_t sector, const StorageRecordHeader &header) {
  return storageReadPayloadStable(sector, header) == PayloadVerifyResult::VALID;
}

bool storageValidateRecordHeaderIntegrity(const StorageRecordHeader &header) {
  const uint32_t maxPhysicalIdentity = storageMaxPhysicalIdentity();
  if (header.magic != RECORD_MAGIC ||
      header.formatVersion != RECORD_FORMAT_VERSION ||
      header.flags != RECORD_FLAG_COMMITTED ||
      header.generation == 0 ||
      header.recordId == 0 ||
      header.sequence == 0 ||
      maxPhysicalIdentity == 0 ||
      header.sequence > maxPhysicalIdentity ||
      header.recordId > maxPhysicalIdentity ||
      (header.deviceId != 0 && header.deviceId > maxPhysicalIdentity) ||
      header.nameLength > STORAGE_MAX_NAME ||
      header.rawLength > RAW_WAVEFORM_CAPACITY ||
      header.payloadLength > (STORAGE_FLASH_SECTOR_SIZE - 4UL - sizeof(header)) ||
      static_cast<uint32_t>(header.nameLength) +
          static_cast<uint32_t>(header.rawLength) * sizeof(uint16_t) != header.payloadLength) return false;
  if (header.type == RECORD_TYPE_DEVICE) {
    if (header.rawLength != 0 || header.carrierHz != 0 || header.repeatCount != 0 ||
        header.repeatGapMs != 0 || header.deviceId == 0 ||
        header.deviceId != header.recordId || header.nameLength == 0) return false;
  } else if (header.type == RECORD_TYPE_COMMAND) {
    if (header.rawLength == 0 || header.nameLength == 0 || header.deviceId == 0 ||
        header.carrierHz < MIN_TX_FREQUENCY_HZ || header.repeatCount == 0) return false;
  } else if (header.type == RECORD_TYPE_DELETE_DEVICE) {
    if (header.nameLength != 0 || header.rawLength != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 || header.payloadLength != 0 ||
        header.deviceId == 0 || header.recordId == 0 || header.deviceId != header.recordId) return false;
  } else if (header.type == RECORD_TYPE_DELETE_COMMAND) {
    if (header.nameLength != 0 || header.rawLength != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 || header.payloadLength != 0 ||
        header.recordId == 0 || header.deviceId == 0) return false;
  } else if (header.type == RECORD_TYPE_RULE) {
    const bool sizeOk = (header.payloadLength == RULE_PAYLOAD_BYTES ||
                         header.payloadLength == RULE_PAYLOAD_BYTES_V1) &&
                        header.rawLength == (header.payloadLength / 2U);
    if (header.nameLength != 0 || header.deviceId != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 || !sizeOk) return false;

  } else if (header.type == RECORD_TYPE_DELETE_RULE) {
    if (header.nameLength != 0 || header.rawLength != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 || header.payloadLength != 0 ||
        header.deviceId != 0) return false;
  } else if (header.type == RECORD_TYPE_SCHEDULE) {
    if (header.nameLength != 0 || header.deviceId != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 ||
        header.rawLength != (SCHEDULE_PAYLOAD_BYTES / 2U) ||
        header.payloadLength != SCHEDULE_PAYLOAD_BYTES) return false;
  } else if (header.type == RECORD_TYPE_DELETE_SCHEDULE) {
    if (header.nameLength != 0 || header.rawLength != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 || header.payloadLength != 0 ||
        header.recordId == 0 || header.deviceId != 0) return false;
  } else if (header.type == RECORD_TYPE_ORDER) {
    if (header.nameLength != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 ||
        header.rawLength == 0U || header.rawLength > 64U ||
        header.payloadLength != header.rawLength * 2U) return false;        
  } else if (header.type == RECORD_TYPE_CONFIG_BACKUP) {
    if (header.nameLength != 0 || header.deviceId != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 ||
        header.payloadLength != STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES ||
        header.rawLength != (STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES / 2U)) return false;
  } else if (header.type == RECORD_TYPE_DEVICE_SETTINGS) {
    if (header.nameLength != 0 || header.deviceId != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 ||
        header.recordId == 0 ||
        header.payloadLength != 2U || header.rawLength != 1U) return false;
  } else if (header.type == RECORD_TYPE_COMMAND_SETTINGS) {
    if (header.nameLength != 0 || header.deviceId != 0 || header.carrierHz != 0 ||
        header.repeatCount != 0 || header.repeatGapMs != 0 ||
        header.recordId == 0 ||
        header.payloadLength != 2U || header.rawLength != 1U) return false;
  } else return false;
  StorageRecordHeader copy = header;
  const uint32_t supplied = copy.headerCrc32;
  copy.headerCrc32 = 0;
  const uint32_t calculated = crc32Bytes(reinterpret_cast<const uint8_t *>(&copy), sizeof(copy));
  return supplied == calculated;
}

bool storageValidateRecordHeader(const StorageRecordHeader &header) {
  return storageValidateRecordHeaderIntegrity(header) && header.generation == storage.generation;
}

bool storageHeaderHasPlausibleIdentity(const StorageRecordHeader &header) {
  const uint32_t maxPhysicalIdentity = storageMaxPhysicalIdentity();
  if (header.magic != RECORD_MAGIC || header.formatVersion != RECORD_FORMAT_VERSION ||
      header.generation == 0 || header.generation != storage.generation ||
      header.sequence == 0 || header.recordId == 0 || maxPhysicalIdentity == 0 ||
      header.sequence > maxPhysicalIdentity || header.recordId > maxPhysicalIdentity) return false;
  return true;
}

enum class DeviceLookupResult : uint8_t { NOT_FOUND, FOUND, CORRUPT_MATCH, READ_ERROR };
enum class CommandNameLookupResult : uint8_t { NOT_FOUND, FOUND, READ_ERROR };

CommandNameLookupResult storageFindCommandNameOnDevicePersistent(uint32_t deviceId, const char *name);
DeviceLookupResult storageLookupDeviceByName(const char *name, uint32_t &deviceIdOut);
bool storageEnsureFreeSpaceIndex();
bool storageFindVerifiedFreeSector(uint32_t startSector, uint32_t &sectorOut);
bool storageFindDeviceName(uint32_t deviceId, char *name, size_t nameCapacity);
bool storageFindDeviceNameIndexedOnly(uint32_t deviceId, char *name, size_t nameCapacity);
bool storageRebuildDeviceIndex();

bool storageGetRecordName(uint32_t sector, const StorageRecordHeader &header, char *name, size_t nameCapacity) {
  if (name == nullptr || nameCapacity == 0 || header.nameLength >= nameCapacity) return false;
  if (!storageReadPayloadVerifiedIntoBuffer(sector, header)) return false;
  memcpy(name, storageRecordBuffer, header.nameLength);
  name[header.nameLength] = '\0';
  return true;
}

bool storageReadValidatedRecord(uint32_t sector, StorageRecordHeader &header) {
  if (!storageReadRecordHeaderStable(sector, header) || storageIsRecordHeaderBlank(header)) return false;
  if (!storageValidateRecordHeader(header)) return false;
  if ((header.type == RECORD_TYPE_DEVICE && storageIsDeviceDeleted(header.recordId)) ||
      (header.type == RECORD_TYPE_COMMAND && (storageIsCommandDeleted(header.recordId) ||
       storageIsDeviceDeleted(header.deviceId)))) return false;
  uint32_t commit = 0;
  if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) return false;
  return storageReadPayloadVerifiedIntoBuffer(sector, header);
}

void storageReserveIdentityFromHeader(const StorageRecordHeader &header) {
  if (!storageHeaderHasPlausibleIdentity(header)) return;
  if (header.sequence != 0 && header.sequence > storage.maxSequence) storage.maxSequence = header.sequence;
  if (header.recordId != 0) {
    if (header.type == RECORD_TYPE_COMMAND) {
      if (header.recordId > storage.maxCommandId) storage.maxCommandId = header.recordId;
    } else if (header.type == RECORD_TYPE_DEVICE) {
      if (header.recordId > storage.maxDeviceId) storage.maxDeviceId = header.recordId;
    }
  }
  const uint32_t maxPhysicalIdentity = storageMaxPhysicalIdentity();
  if ((header.type == RECORD_TYPE_DEVICE || header.type == RECORD_TYPE_COMMAND) &&
      header.deviceId != 0 && header.deviceId <= maxPhysicalIdentity &&
      header.deviceId > storage.maxDeviceId) storage.maxDeviceId = header.deviceId;
}

// >>> FIX — small command-locator cache.
// Avoids a full 2000-sector flash scan on every press. The cached sector is
// always fully re-validated before use, so delete/rename/relearn invalidation
// happens automatically (the old sector stops validating).
// Small recent-command locator cache. A modest increase keeps the RAM cost
// low while making WebUI command presses far less likely to hit the full-flash
// lookup fallback. Each entry is 8 bytes.
constexpr uint8_t STORAGE_CMD_CACHE_SIZE = 16U;
struct CommandCacheEntry { uint32_t commandId; uint32_t sector; };
static CommandCacheEntry storageCommandCache[STORAGE_CMD_CACHE_SIZE] = {};
static uint8_t storageCommandCacheCursor = 0U;

static void storageResetCommandCache() {
  memset(storageCommandCache, 0, sizeof(storageCommandCache));
  storageCommandCacheCursor = 0U;
}

static void storageCacheCommandLocation(uint32_t commandId, uint32_t sector) {
  if (commandId == 0U) return;
  for (uint8_t i = 0U; i < STORAGE_CMD_CACHE_SIZE; ++i) {
    if (storageCommandCache[i].commandId == commandId) {
      storageCommandCache[i].sector = sector;
      return;
    }
  }
  storageCommandCache[storageCommandCacheCursor].commandId = commandId;
  storageCommandCache[storageCommandCacheCursor].sector    = sector;
  storageCommandCacheCursor =
      static_cast<uint8_t>((storageCommandCacheCursor + 1U) % STORAGE_CMD_CACHE_SIZE);
}

// ============================================================
// RAM ORDER CACHE + COMMAND INDEX
// ============================================================
// Both caches remove the full-flash scans that dominated WebUI latency.
// They are pure accelerators — every lookup re-validates against flash,
// so a stale entry is always harmless.

// ---- Device order list cache (recordId == 1, deviceId == 0)
// ---- Command order list cache (recordId == deviceId)
static uint16_t cachedDeviceOrder[64];
static uint16_t cachedDeviceOrderCount = 0;
static bool     cachedDeviceOrderValid = false;

static uint16_t cachedCmdOrderDevice = 0;
static uint16_t cachedCmdOrder[64];
static uint16_t cachedCmdOrderCount = 0;
static bool     cachedCmdOrderValid = false;

static void storageInvalidateOrderCache() {
  cachedDeviceOrderValid = false;
  cachedDeviceOrderCount = 0;
  cachedCmdOrderValid    = false;
  cachedCmdOrderCount    = 0;
  cachedCmdOrderDevice   = 0;
}

// ---- Live command index ------------------------------------------
// Every live command gets exactly one entry, pointing at its latest sector.
// Populated during storageScanDatabase() and maintained on writes.
struct CommandIndexEntry {
  uint16_t id;       // commandId  (fits because sectorCount <= 2048)
  uint16_t devId;    // deviceId
  uint16_t sector;   // flash sector holding the latest record
};
static_assert(sizeof(CommandIndexEntry) == 6, "unexpected CommandIndexEntry padding");

constexpr uint16_t STORAGE_CMD_INDEX_SIZE = 256U;
static CommandIndexEntry storageCommandIndex[STORAGE_CMD_INDEX_SIZE];
static uint16_t storageCommandIndexCount = 0;

static void storageResetCommandIndex() {
  memset(storageCommandIndex, 0, sizeof(storageCommandIndex));
  storageCommandIndexCount = 0;
}

static void storageRegisterCommandIndex(uint32_t commandId, uint32_t deviceId,
                                        uint32_t sector, uint32_t sequence) {
  (void)sequence;   // sequence only matters during the scan; the scan picks the winner
  if (commandId == 0U || deviceId == 0U ||
      commandId > 0xFFFFU || deviceId > 0xFFFFU ||
      sector >= STORAGE_MAX_SECTORS) return;

  // Already indexed? Overwrite (scan order already guarantees latest wins).
  for (uint16_t i = 0U; i < storageCommandIndexCount; ++i) {
    if (storageCommandIndex[i].id != (uint16_t)commandId) continue;
    storageCommandIndex[i].devId  = (uint16_t)deviceId;
    storageCommandIndex[i].sector = (uint16_t)sector;
    return;
  }
  if (storageCommandIndexCount >= STORAGE_CMD_INDEX_SIZE) return;
  CommandIndexEntry &e = storageCommandIndex[storageCommandIndexCount++];
  e.id     = (uint16_t)commandId;
  e.devId  = (uint16_t)deviceId;
  e.sector = (uint16_t)sector;
}

static void storageRemoveCommandIndex(uint32_t commandId) {
  if (commandId == 0U || commandId > 0xFFFFU) return;
  uint16_t w = 0U;
  for (uint16_t r = 0U; r < storageCommandIndexCount; ++r) {
    if (storageCommandIndex[r].id == (uint16_t)commandId) continue;
    if (w != r) storageCommandIndex[w] = storageCommandIndex[r];
    ++w;
  }
  storageCommandIndexCount = w;
}

void storageResetRuntimeDatabaseState() {
  storageResetCommandIndex();
  memset(storageQuarantineBitmap, 0, sizeof(storageQuarantineBitmap));
  memset(storageCommandAlexaBitmap, 0, sizeof(storageCommandAlexaBitmap));
  memset(storageDeviceCloudMap,     0, sizeof(storageDeviceCloudMap));
  memset(storageSeenDeviceIdBitmap, 0, sizeof(storageSeenDeviceIdBitmap));
  memset(storageDeviceIdConflictBitmap, 0, sizeof(storageDeviceIdConflictBitmap));
  memset(storageSeenCommandIdBitmap, 0, sizeof(storageSeenCommandIdBitmap));
  memset(storageCommandIdConflictBitmap, 0, sizeof(storageCommandIdConflictBitmap));
  memset(storageDeletedDeviceBitmap, 0, sizeof(storageDeletedDeviceBitmap));
  memset(storageDeletedCommandBitmap, 0, sizeof(storageDeletedCommandBitmap));
  storage.initialized = false;
  storage.headerCorrupt = false;
  storage.generation = 0;
  storage.maxSequence = 0;
  storage.maxCommandId = 0;
  storage.maxDeviceId = 0;
  storage.validRecordCount = 0;
  storage.corruptRecordCount = 0;
  storage.obsoleteRecordCount = 0;
  storage.usedSectorCount = 0;
  storage.freeSectorCount = 0;
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = false;
  storage.latestCommandSector = UINT32_MAX;
  storage.latestCommandId = 0;
  storage.latestCommandDeviceId = 0;
  storage.currentDeviceId = 0;
  storage.currentDeviceName[0] = '\0';
  storage.currentCommandId = 0;
  storage.currentCommandName[0] = '\0';
}

bool storageIsSectorQuarantined(uint32_t sector) {
  if (sector >= STORAGE_MAX_SECTORS) return false;
  return (storageQuarantineBitmap[sector >> 3] & static_cast<uint8_t>(1U << (sector & 7U))) != 0;
}
void storageMarkSectorQuarantined(uint32_t sector) {
  if (sector >= STORAGE_MAX_SECTORS) return;
  storageQuarantineBitmap[sector >> 3] |= static_cast<uint8_t>(1U << (sector & 7U));
}
void storageResetDeviceIndex() { storageDeviceIndexCount = 0; storageDeviceIndexReplaceCursor = 0; }

void storageRegisterDeviceIndex(uint32_t deviceId, uint32_t sector, uint16_t nameCrc, uint32_t sequence) {
  if (deviceId == 0 || storageIsDeviceIdentityConflicted(deviceId) ||
      sector >= STORAGE_MAX_SECTORS || storageIsSectorQuarantined(sector)) return;
  int bestIndex = -1;
  uint32_t bestCachedSequence = 0;
  for (uint16_t i = 0; i < storageDeviceIndexCount; ++i) {
    if (storageDeviceIndex[i].id != deviceId) continue;
    if (bestIndex < 0 || storageDeviceIndex[i].sequence > bestCachedSequence) {
      bestIndex = static_cast<int>(i);
      bestCachedSequence = storageDeviceIndex[i].sequence;
    }
  }
  if (bestIndex >= 0) {
    if (sequence > bestCachedSequence) {
      storageDeviceIndex[bestIndex].nameCrc = nameCrc;
      storageDeviceIndex[bestIndex].sector = static_cast<uint16_t>(sector);
      storageDeviceIndex[bestIndex].sequence = static_cast<uint16_t>(sequence);
    }
    return;
  }
  const DeviceIndexEntry entry{
    static_cast<uint16_t>(deviceId),
    static_cast<uint16_t>(sequence),
    static_cast<uint16_t>(sector),
    nameCrc
  };
  if (storageDeviceIndexCount < STORAGE_DEVICE_CACHE_SIZE) {
    storageDeviceIndex[storageDeviceIndexCount++] = entry;
    return;
  }
  storageDeviceIndex[storageDeviceIndexReplaceCursor] = entry;
  storageDeviceIndexReplaceCursor = static_cast<uint16_t>((storageDeviceIndexReplaceCursor + 1U) % STORAGE_DEVICE_CACHE_SIZE);
}

bool storageFindDeviceNameIndexedOnly(uint32_t deviceId, char *name, size_t nameCapacity) {
  if (!storage.initialized || deviceId == 0 || storageIsDeviceIdentityConflicted(deviceId) ||
      storageIsDeviceDeleted(deviceId) || name == nullptr || nameCapacity < 2) return false;
  int bestIndex = -1;
  uint32_t bestSequence = 0;
  for (uint16_t i = 0; i < storageDeviceIndexCount; ++i) {
    if (storageDeviceIndex[i].id != deviceId) continue;
    if (bestIndex < 0 || storageDeviceIndex[i].sequence > bestSequence) {
      bestIndex = static_cast<int>(i);
      bestSequence = storageDeviceIndex[i].sequence;
    }
  }
  if (bestIndex < 0) return false;
  const uint32_t indexedSector = storageDeviceIndex[bestIndex].sector;
  if (storageIsSectorQuarantined(indexedSector)) return false;
  StorageRecordHeader header{};
  if (!storageReadValidatedRecord(indexedSector, header) ||
      header.type != RECORD_TYPE_DEVICE || header.recordId != deviceId ||
      header.deviceId != deviceId || header.nameLength >= nameCapacity) return false;
  memcpy(name, storageRecordBuffer, header.nameLength);
  name[header.nameLength] = '\0';
  return true;
}

bool storageRebuildDeviceIndex() {
  if (!storage.initialized) return false;
  storageResetDeviceIndex();
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_DEVICE ||
        storageIsDeviceDeleted(header.recordId) || header.nameLength == 0 ||
        header.deviceId != header.recordId) continue;
    storageRegisterDeviceIndex(header.recordId, sector,
        static_cast<uint16_t>(crc32Bytes(storageRecordBuffer, header.nameLength)),
        header.sequence);
  }
  return true;
}

bool storageRecoverDeviceIndexEntry(uint32_t deviceId) {
  if (!storage.initialized || deviceId == 0 || storageIsDeviceIdentityConflicted(deviceId)) return false;
  uint32_t bestSector = UINT32_MAX;
  uint32_t bestSequence = 0;
  uint16_t bestNameCrc = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_DEVICE ||
        storageIsDeviceDeleted(deviceId) || header.recordId != deviceId ||
        header.deviceId != deviceId) continue;
    if (bestSector == UINT32_MAX || header.sequence > bestSequence) {
      bestSector = sector;
      bestSequence = header.sequence;
      bestNameCrc = static_cast<uint16_t>(crc32Bytes(storageRecordBuffer, header.nameLength));
    }
  }
  if (bestSector == UINT32_MAX) return false;
  storageRegisterDeviceIndex(deviceId, bestSector, bestNameCrc, bestSequence);
  char ignoredName[STORAGE_MAX_NAME + 1] = {0};
  return storageFindDeviceNameIndexedOnly(deviceId, ignoredName, sizeof(ignoredName));
}

bool storageHasIndexedValidDeviceId(uint32_t deviceId) {
  if (deviceId == 0 || storageIsDeviceIdentityConflicted(deviceId) ||
      storageIsDeviceDeleted(deviceId)) return false;
  char ignoredName[STORAGE_MAX_NAME + 1] = {0};
  return storageFindDeviceNameIndexedOnly(deviceId, ignoredName, sizeof(ignoredName));
}

uint32_t storageMaxPhysicalIdentity() {
  return (storage.sectorCount > DB_DATA_START_SECTOR) ? (storage.sectorCount - DB_DATA_START_SECTOR) : 0;
}

bool storageIdentityBitmapContains(const uint8_t *bitmap, uint32_t id) {
  const uint32_t maxId = storageMaxPhysicalIdentity();
  if (bitmap == nullptr || id == 0 || id > maxId || id >= STORAGE_MAX_SECTORS) return false;
  return (bitmap[id >> 3] & static_cast<uint8_t>(1U << (id & 7U))) != 0;
}
void storageIdentityBitmapSet(uint8_t *bitmap, uint32_t id) {
  const uint32_t maxId = storageMaxPhysicalIdentity();
  if (bitmap == nullptr || id == 0 || id > maxId || id >= STORAGE_MAX_SECTORS) return;
  bitmap[id >> 3] |= static_cast<uint8_t>(1U << (id & 7U));
}
bool storageIsDeviceIdentityConflicted(uint32_t deviceId) { return storageIdentityBitmapContains(storageDeviceIdConflictBitmap, deviceId); }
bool storageIsCommandIdentityConflicted(uint32_t commandId) { return storageIdentityBitmapContains(storageCommandIdConflictBitmap, commandId); }
void storageMarkDeviceIdentitySeen(uint32_t deviceId) { storageIdentityBitmapSet(storageSeenDeviceIdBitmap, deviceId); }
bool storageWasDeviceIdentitySeen(uint32_t deviceId) { return storageIdentityBitmapContains(storageSeenDeviceIdBitmap, deviceId); }
void storageMarkDeviceIdentityConflicted(uint32_t deviceId) { storageIdentityBitmapSet(storageDeviceIdConflictBitmap, deviceId); }
bool storageWasCommandIdentitySeen(uint32_t commandId) { return storageIdentityBitmapContains(storageSeenCommandIdBitmap, commandId); }
void storageMarkCommandIdentitySeen(uint32_t commandId) { storageIdentityBitmapSet(storageSeenCommandIdBitmap, commandId); }
void storageMarkCommandIdentityConflicted(uint32_t commandId) { storageIdentityBitmapSet(storageCommandIdConflictBitmap, commandId); }
bool storageIsDeviceDeleted(uint32_t deviceId) { return storageIdentityBitmapContains(storageDeletedDeviceBitmap, deviceId); }
bool storageIsCommandDeleted(uint32_t commandId) { return storageIdentityBitmapContains(storageDeletedCommandBitmap, commandId); }

void storageRemoveDeviceIndexEntries(uint32_t deviceId) {
  uint16_t writeIndex = 0;
  for (uint16_t readIndex = 0; readIndex < storageDeviceIndexCount; ++readIndex) {
    if (storageDeviceIndex[readIndex].id == deviceId) continue;
    if (writeIndex != readIndex) storageDeviceIndex[writeIndex] = storageDeviceIndex[readIndex];
    ++writeIndex;
  }
  storageDeviceIndexCount = writeIndex;
  if (storageDeviceIndexReplaceCursor >= storageDeviceIndexCount &&
      storageDeviceIndexCount < STORAGE_DEVICE_CACHE_SIZE) storageDeviceIndexReplaceCursor = storageDeviceIndexCount;
}

void storageResetScanState() {
  storageResetDeviceIndex();
  storageResetCommandIndex();       // >>> NEW — rebuild index from scratch
  storageInvalidateOrderCache();    // >>> NEW — force re-read of order records
  memset(storageCommandAlexaBitmap, 0, sizeof(storageCommandAlexaBitmap));
  memset(storageDeviceCloudMap,     0, sizeof(storageDeviceCloudMap));
  memset(storageSeenDeviceIdBitmap, 0, sizeof(storageSeenDeviceIdBitmap));
  memset(storageDeviceIdConflictBitmap, 0, sizeof(storageDeviceIdConflictBitmap));
  memset(storageSeenCommandIdBitmap, 0, sizeof(storageSeenCommandIdBitmap));
  memset(storageCommandIdConflictBitmap, 0, sizeof(storageCommandIdConflictBitmap));
  memset(storageDeletedDeviceBitmap, 0, sizeof(storageDeletedDeviceBitmap));
  memset(storageDeletedCommandBitmap, 0, sizeof(storageDeletedCommandBitmap));
  storage.maxSequence = 0;
  storage.maxCommandId = 0;
  storage.maxDeviceId = 0;
  storage.validRecordCount = 0;
  storage.corruptRecordCount = 0;
  storage.obsoleteRecordCount = 0;
  storage.usedSectorCount = 0;
  storage.freeSectorCount = 0;
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = false;
  storage.latestCommandSector = UINT32_MAX;
  storage.latestCommandId = 0;
  storage.latestCommandDeviceId = 0;
}

bool storageTryReclaimUncommittedSector(uint32_t sector, const StorageRecordHeader &header) {
  uint32_t commit = 0;
  if (!storageReadCommitStable(sector, commit) || commit != 0xFFFFFFFFUL) {
    storageQuarantineSector(sector); return false;
  }
  if (!storageValidateRecordHeaderForBody(&header, storage.generation)) {
    storageQuarantineSector(sector); return false;
  }
  const PayloadVerifyResult payloadResult = storageReadPayloadStable(sector, header);
  if (payloadResult == PayloadVerifyResult::UNCERTAIN) { storageQuarantineSector(sector); return false; }
  if (!flashEraseSectorVerified(sector)) { storageQuarantineSector(sector); return false; }
  return true;
}

bool storageScanDatabase() {
  storageResetScanState();
  storageResetCommandCache();   // >>> FIX
  uint32_t latestCommandSequence = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    if (storageIsSectorQuarantined(sector)) {
      storage.usedSectorCount++; storage.corruptRecordCount++;
      if ((sector & 15UL) == 15UL) yield();
      continue;
    }
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) {
      storageQuarantineSector(sector); storage.usedSectorCount++; storage.corruptRecordCount++;
      continue;
    }
    if ((sector & 15UL) == 15UL) yield();
    if (storageIsRecordHeaderBlank(header)) {
      storage.freeSectorCount++;
      if (storage.firstFreeSector == UINT32_MAX) storage.firstFreeSector = sector;
      continue;
    }
    storage.usedSectorCount++;
    if (storageIsSectorQuarantined(sector)) { storage.corruptRecordCount++; continue; }
    uint32_t commit = 0;
    if (!storageReadCommitStable(sector, commit)) {
      storage.corruptRecordCount++; storageQuarantineSector(sector); continue;
    }
    if (header.magic == RECORD_MAGIC && header.formatVersion == RECORD_FORMAT_VERSION &&
        header.generation != storage.generation) {
      if (commit == COMMIT_MAGIC && storageValidateRecordHeaderIntegrity(header)) {
        storage.obsoleteRecordCount++;
      } else { storageQuarantineSector(sector); storage.corruptRecordCount++; }
      continue;
    }
    if (commit != COMMIT_MAGIC) {
      if (commit == 0xFFFFFFFFUL) {
        if (storageTryReclaimUncommittedSector(sector, header)) {
          storage.usedSectorCount--; storage.freeSectorCount++;
          if (storage.firstFreeSector == UINT32_MAX) storage.firstFreeSector = sector;
        } else storage.corruptRecordCount++;
      } else { storageQuarantineSector(sector); storage.corruptRecordCount++; }
      continue;
    }
    if (!storageValidateRecordHeader(header)) { storageQuarantineSector(sector); storage.corruptRecordCount++; continue; }
    const PayloadVerifyResult payloadResult = storageReadPayloadStable(sector, header);
    if (payloadResult == PayloadVerifyResult::UNCERTAIN) { storageQuarantineSector(sector); storage.corruptRecordCount++; continue; }
    if (payloadResult != PayloadVerifyResult::VALID) { storageQuarantineSector(sector); storage.corruptRecordCount++; continue; }
    if (header.type == RECORD_TYPE_DELETE_DEVICE) {
      storageIdentityBitmapSet(storageDeletedDeviceBitmap, header.recordId);
      storageRemoveDeviceIndexEntries(header.recordId);
      if (storage.latestCommandDeviceId == header.recordId) {
        storage.latestCommandSector = UINT32_MAX;
        storage.latestCommandId = 0;
        storage.latestCommandDeviceId = 0;
      }
      if (storage.currentDeviceId == header.recordId) {
        storage.currentDeviceId = 0; storage.currentDeviceName[0] = '\0';
        storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
      }
      storage.validRecordCount++;
      if (header.sequence > storage.maxSequence) storage.maxSequence = header.sequence;
      continue;
    }
    if (header.type == RECORD_TYPE_DELETE_COMMAND) {
      storageIdentityBitmapSet(storageDeletedCommandBitmap, header.recordId);
      storageRemoveCommandIndex(header.recordId);   // >>> NEW
      if (storage.latestCommandId == header.recordId) {
        storage.latestCommandSector = UINT32_MAX; storage.latestCommandId = 0;
      }
      if (storage.currentCommandId == header.recordId) {
        storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
      }
      storage.validRecordCount++;      if (header.sequence > storage.maxSequence) storage.maxSequence = header.sequence;
      continue;
    }
    storageReserveIdentityFromHeader(header);
    if ((header.type == RECORD_TYPE_DEVICE && storageIsDeviceDeleted(header.recordId)) ||
        (header.type == RECORD_TYPE_COMMAND && (storageIsCommandDeleted(header.recordId) ||
         storageIsDeviceDeleted(header.deviceId)))) { storage.obsoleteRecordCount++; continue; }
    if (header.type == RECORD_TYPE_DEVICE) {
      if (storageWasDeviceIdentitySeen(header.recordId)) {
        storageMarkDeviceIdentityConflicted(header.recordId);
        storageRemoveDeviceIndexEntries(header.recordId);
      } else {
        storageMarkDeviceIdentitySeen(header.recordId);
        storageRegisterDeviceIndex(header.recordId, sector,
            static_cast<uint16_t>(crc32Bytes(storageRecordBuffer, header.nameLength)),
            header.sequence);
      }
    } else if (header.type == RECORD_TYPE_COMMAND) {
      if (storageWasCommandIdentitySeen(header.recordId)) {
        storageMarkCommandIdentityConflicted(header.recordId);
      } else {
        storageMarkCommandIdentitySeen(header.recordId);
        storageCacheCommandLocation(header.recordId, sector);
        storageRegisterCommandIndex(header.recordId, header.deviceId, sector, header.sequence);  // >>> NEW
      }
    }
    // ── v1.0: refresh the two settings RAM caches while the payload is
    // already staged and validated in storageRecordBuffer.
    if (header.payloadLength == 2U) {
      if (header.type == RECORD_TYPE_DEVICE_SETTINGS &&
          header.recordId >= 1U && header.recordId <= 256U) {
        storageDeviceCloudMap[header.recordId - 1U] =
            (uint8_t)((storageRecordBuffer[0] & 0x0FU) |
                      (storageRecordBuffer[1] ? 0x80U : 0x00U));
      } else if (header.type == RECORD_TYPE_COMMAND_SETTINGS &&
                 header.recordId >= 1U &&
                 header.recordId < STORAGE_MAX_SECTORS) {
        const uint32_t cid = header.recordId;
        if (storageRecordBuffer[0] & 0x01U)
          storageCommandAlexaBitmap[cid >> 3] |=  (uint8_t)(1U << (cid & 7U));
        else
          storageCommandAlexaBitmap[cid >> 3] &= (uint8_t)~(1U << (cid & 7U));
      }
    }
    storage.validRecordCount++;
    if (header.sequence > storage.maxSequence) storage.maxSequence = header.sequence;
    if (header.type == RECORD_TYPE_COMMAND) {
      if (header.recordId > storage.maxCommandId) storage.maxCommandId = header.recordId;
      if (storage.latestCommandSector == UINT32_MAX || header.sequence > latestCommandSequence) {
        latestCommandSequence = header.sequence;
        storage.latestCommandSector = sector;
        storage.latestCommandId = header.recordId;
        storage.latestCommandDeviceId = header.deviceId;
      }
    } else if (header.type == RECORD_TYPE_DEVICE) {
      if (header.recordId > storage.maxDeviceId) storage.maxDeviceId = header.recordId;
    }
  }
  storage.freeSpaceIndexValid = true;
  return true;
}

bool storageValidateRecordHeaderForBody(const StorageRecordHeader *header, uint32_t generation) {
  if (header == nullptr || !storageValidateRecordHeaderIntegrity(*header) ||
      header->generation != generation) return false;
  return true;
}

bool storageCommitRecord(uint32_t sector) {
  const uint32_t commit = COMMIT_MAGIC;
  if (!flashProgramBytes(sector * STORAGE_FLASH_SECTOR_SIZE + STORAGE_COMMIT_OFFSET,
                         reinterpret_cast<const uint8_t *>(&commit), sizeof(commit))) {}
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    uint32_t storedCommit = 0;
    if (storageReadCommitStable(sector, storedCommit)) {
      if (storedCommit == COMMIT_MAGIC) return true;
      return false;
    }
    delay(2);
  }
  return false;
}

bool storageNextSequence(uint32_t &next) {
  const uint32_t maxPhysicalIdentity = storageMaxPhysicalIdentity();
  if (maxPhysicalIdentity == 0 || storage.maxSequence >= maxPhysicalIdentity) return false;
  next = storage.maxSequence + 1UL;
  return next <= maxPhysicalIdentity;
}

bool storageNextRecordId(uint8_t type, uint32_t &next) {
  const uint32_t maxPhysicalIdentity = storageMaxPhysicalIdentity();
  if (maxPhysicalIdentity == 0) return false;
  const uint32_t current = (type == RECORD_TYPE_DEVICE) ? storage.maxDeviceId : storage.maxCommandId;
  if (current >= maxPhysicalIdentity) return false;
  next = current + 1UL;
  return next <= maxPhysicalIdentity;
}

bool storageEnsureSectorBlank(uint32_t sector) {
  if (!storage.detected || sector < DB_DATA_START_SECTOR ||
      sector >= storage.sectorCount || storageIsSectorQuarantined(sector)) return false;
  StorageRecordHeader header{};
  if (!storageReadRecordHeaderStable(sector, header) || !storageIsRecordHeaderBlank(header)) return false;
  return flashIsSectorBlank(sector);
}

bool storageRecoverFailedAppendSector(uint32_t sector) {
  if (!flashWaitReady(5000UL)) return false;
  return flashEraseSectorVerified(sector);
}

bool storageFindVerifiedFreeSector(uint32_t startSector, uint32_t &sectorOut) {
  sectorOut = UINT32_MAX;
  if (!storage.initialized || storage.sectorCount <= DB_DATA_START_SECTOR) return false;
  const uint32_t start = (startSector >= DB_DATA_START_SECTOR && startSector < storage.sectorCount)
                             ? startSector : DB_DATA_START_SECTOR;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    const uint32_t begin = (pass == 0) ? start : DB_DATA_START_SECTOR;
    const uint32_t end = (pass == 0) ? storage.sectorCount : start;
    for (uint32_t sector = begin; sector < end; ++sector) {
      feedWatchdog();
      if (storageIsSectorQuarantined(sector)) continue;
      StorageRecordHeader header{};
      if (!storageReadRecordHeaderStable(sector, header)) {
        storageMarkSectorQuarantined(sector);
        if (storage.freeSectorCount > 0) storage.freeSectorCount--;
        storage.usedSectorCount++;
        storage.firstFreeSector = UINT32_MAX;
        storage.freeSpaceIndexValid = false;
        continue;
      }
      if (!storageIsRecordHeaderBlank(header)) continue;
      if (flashIsSectorBlank(sector)) { sectorOut = sector; return true; }
      storageMarkSectorQuarantined(sector);
      if (storage.freeSectorCount > 0) storage.freeSectorCount--;
      storage.usedSectorCount++;
      storage.freeSpaceIndexValid = false;
    }
  }
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = true;
  return false;
}

bool storageUpdateFirstFreeSectorAfterAppend(uint32_t consumedSector) {
  if (storage.freeSectorCount > 0) storage.freeSectorCount--;
  storage.usedSectorCount++;
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = false;
  uint32_t next = UINT32_MAX;
  const uint32_t start = (consumedSector + 1UL < storage.sectorCount) ? consumedSector + 1UL : DB_DATA_START_SECTOR;
  if (storageFindVerifiedFreeSector(start, next)) {
    storage.firstFreeSector = next; storage.freeSpaceIndexValid = true; return true;
  }
  if (storage.freeSpaceIndexValid) storage.firstFreeSector = UINT32_MAX;
  return false;
}

bool storageEnsureFreeSpaceIndex() {
  if (!storage.initialized) return false;
  if (storage.freeSpaceIndexValid) return true;
  return storageScanDatabase();
}

bool storageAppendRecord(uint8_t type, uint32_t recordId, uint32_t deviceId,
                         const char *name, const uint16_t *raw, uint16_t rawLength,
                         uint16_t carrierHz, uint8_t repeatCount, uint16_t repeatGapMs,
                         uint32_t &sectorOut, uint32_t &sequenceOut) {
  if (!storage.initialized || (type != RECORD_TYPE_DEVICE && type != RECORD_TYPE_COMMAND) ||
      recordId == 0 || name == nullptr || name[0] == '\0' || strlen(name) > STORAGE_MAX_NAME) return false;
  if (type == RECORD_TYPE_COMMAND && deviceId == 0) return false;
  if (type == RECORD_TYPE_COMMAND && (raw == nullptr || rawLength == 0 ||
      rawLength > RAW_WAVEFORM_CAPACITY || carrierHz < MIN_TX_FREQUENCY_HZ ||
      repeatCount == 0)) return false;
  if (type == RECORD_TYPE_DEVICE && rawLength != 0) return false;
  if (!storageEnsureFreeSpaceIndex()) { Serial.println(F("ERROR: W25Qxx free-space index could not be verified.")); return false; }
  uint32_t targetSector = UINT32_MAX;
  const uint32_t requestedStart = storage.firstFreeSector;
  if (!storageFindVerifiedFreeSector(requestedStart, targetSector)) {
    Serial.println(F("ERROR: W25Qxx storage has no verified free sector.")); return false;
  }
  const uint16_t nameLength = static_cast<uint16_t>(strlen(name));
  const uint32_t waveformBytes = static_cast<uint32_t>(rawLength) * sizeof(uint16_t);
  const uint32_t payloadLength32 = static_cast<uint32_t>(nameLength) + waveformBytes;
  if (payloadLength32 > STORAGE_COMMIT_OFFSET - sizeof(StorageRecordHeader)) return false;
  const uint16_t payloadLength = static_cast<uint16_t>(payloadLength32);
  memset(storageRecordBuffer, 0xFF, sizeof(storageRecordBuffer));
  uint8_t *payload = storageRecordBuffer + sizeof(StorageRecordHeader);
  memcpy(payload, name, nameLength);
  if (waveformBytes > 0) memcpy(payload + nameLength, raw, waveformBytes);
  StorageRecordHeader header{};
  header.magic = RECORD_MAGIC;
  header.formatVersion = RECORD_FORMAT_VERSION;
  header.type = type;
  header.flags = RECORD_FLAG_COMMITTED;
  header.generation = storage.generation;
  uint32_t nextSequence = 0;
  if (!storageNextSequence(nextSequence)) { Serial.println(F("ERROR: database sequence space exhausted.")); return false; }
  header.sequence = nextSequence;
  header.recordId = recordId;
  header.deviceId = deviceId;
  header.nameLength = nameLength;
  header.rawLength = rawLength;
  header.carrierHz = (type == RECORD_TYPE_COMMAND) ? carrierHz : 0;
  header.repeatCount = (type == RECORD_TYPE_COMMAND) ? repeatCount : 0;
  header.reserved0 = 0;
  header.repeatGapMs = (type == RECORD_TYPE_COMMAND) ? repeatGapMs : 0;
  header.payloadLength = payloadLength;
  header.headerCrc32 = 0;
  header.payloadCrc32 = crc32Bytes(payload, payloadLength);
  StorageRecordHeader headerForCrc = header;
  headerForCrc.headerCrc32 = 0;
  header.headerCrc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&headerForCrc), sizeof(headerForCrc));
  memcpy(storageRecordBuffer, &header, sizeof(header));
  const uint32_t base = targetSector * STORAGE_FLASH_SECTOR_SIZE;
  const uint16_t bodyLength = static_cast<uint16_t>(sizeof(StorageRecordHeader) + payloadLength);
  if (!storageEnsureSectorBlank(targetSector)) {
    Serial.println(F("ERROR: target storage sector could not be revalidated as blank."));
    storageQuarantineAppendSector(targetSector, header); return false;
  }
  if (!flashProgramBytes(base, storageRecordBuffer, bodyLength)) {
    Serial.println(F("ERROR: record body write failed."));
    if (!storageRecoverFailedAppendSector(targetSector)) {
      Serial.println(F("ERROR: failed to recover damaged append sector. It will remain unusable."));
      storageQuarantineAppendSector(targetSector, header);
    }
    return false;
  }
  StorageRecordHeader verifyHeader{};
  if (!storageReadRecordHeaderStable(targetSector, verifyHeader) ||
      !storageValidateRecordHeaderForBody(&verifyHeader, storage.generation) ||
      !storageReadPayloadVerifiedIntoBuffer(targetSector, verifyHeader)) {
    Serial.println(F("ERROR: record body verification failed; commit not written."));
    if (!storageRecoverFailedAppendSector(targetSector)) {
      Serial.println(F("ERROR: failed to recover damaged append sector. It will remain unusable."));
      storageQuarantineAppendSector(targetSector, header);
    }
    return false;
  }
  if (!storageCommitRecord(targetSector)) {
    uint32_t observedCommit = 0;
    if (storageReadCommitStable(targetSector, observedCommit) && observedCommit == COMMIT_MAGIC) {
      sectorOut = targetSector; sequenceOut = header.sequence; return true;
    }
    if (storageReadCommitStable(targetSector, observedCommit) && observedCommit != COMMIT_MAGIC) {
      if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    } else storageQuarantineAppendSector(targetSector, header);
    Serial.println(F("ERROR: commit marker could not be safely verified."));
    return false;
  }
  sectorOut = targetSector;
  sequenceOut = header.sequence;
  return true;
}

bool storageAppendDeleteRecord(uint8_t deleteType, uint32_t targetId, uint32_t targetDeviceId,
                               uint32_t &sectorOut, uint32_t &sequenceOut) {
  if (!storage.initialized || (deleteType != RECORD_TYPE_DELETE_DEVICE &&
      deleteType != RECORD_TYPE_DELETE_COMMAND) || targetId == 0 ||
      targetId > storageMaxPhysicalIdentity()) return false;
  if (deleteType == RECORD_TYPE_DELETE_DEVICE && targetDeviceId != targetId) return false;
  if (deleteType == RECORD_TYPE_DELETE_COMMAND && targetDeviceId == 0) return false;
  if (!storageEnsureFreeSpaceIndex()) { Serial.println(F("ERROR: W25Qxx free-space index could not be verified.")); return false; }
  uint32_t targetSector = UINT32_MAX;
  if (!storageFindVerifiedFreeSector(storage.firstFreeSector, targetSector)) {
    Serial.println(F("ERROR: W25Qxx storage has no verified free sector for delete journal."));
    return false;
  }
  uint32_t nextSequence = 0;
  if (!storageNextSequence(nextSequence)) { Serial.println(F("ERROR: database sequence space exhausted.")); return false; }
  memset(storageRecordBuffer, 0xFF, sizeof(storageRecordBuffer));
  StorageRecordHeader header{};
  header.magic = RECORD_MAGIC;
  header.formatVersion = RECORD_FORMAT_VERSION;
  header.type = deleteType;
  header.flags = RECORD_FLAG_COMMITTED;
  header.generation = storage.generation;
  header.sequence = nextSequence;
  header.recordId = targetId;
  header.deviceId = targetDeviceId;
  header.payloadCrc32 = crc32Bytes(nullptr, 0);
  StorageRecordHeader headerForCrc = header;
  headerForCrc.headerCrc32 = 0;
  header.headerCrc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&headerForCrc), sizeof(headerForCrc));
  memcpy(storageRecordBuffer, &header, sizeof(header));
  const uint32_t base = targetSector * STORAGE_FLASH_SECTOR_SIZE;
  if (!storageEnsureSectorBlank(targetSector)) { storageQuarantineAppendSector(targetSector, header); return false; }
  if (!flashProgramBytes(base, storageRecordBuffer, static_cast<uint16_t>(sizeof(StorageRecordHeader)))) {
    if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  StorageRecordHeader verifyHeader{};
  if (!storageReadRecordHeaderStable(targetSector, verifyHeader) ||
      !storageValidateRecordHeaderForBody(&verifyHeader, storage.generation) ||
      !storageReadPayloadVerifiedIntoBuffer(targetSector, verifyHeader)) {
    if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  if (!storageCommitRecord(targetSector)) {
    uint32_t observedCommit = 0;
    if (!storageReadCommitStable(targetSector, observedCommit) || observedCommit != COMMIT_MAGIC) {
      if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    }
    return false;
  }
  sectorOut = targetSector;
  sequenceOut = nextSequence;
  storage.maxSequence = nextSequence;
  (void)storageUpdateFirstFreeSectorAfterAppend(targetSector);
  return true;
}

static bool storageAppendOrderRecord(uint32_t recordId, uint32_t deviceId,
                                     const uint16_t *ids, uint16_t count) {
  if (!storage.initialized || recordId == 0U || ids == nullptr) return false;
  if (count == 0U || count > 64U) return false;
  if (recordId > storageMaxPhysicalIdentity()) return false;
  if (!storageEnsureFreeSpaceIndex()) return false;

  uint32_t targetSector = UINT32_MAX;
  if (!storageFindVerifiedFreeSector(storage.firstFreeSector, targetSector)) return false;

  uint32_t nextSequence = 0;
  if (!storageNextSequence(nextSequence)) return false;

  const uint16_t payloadLength = static_cast<uint16_t>(count * 2U);
  memset(storageRecordBuffer, 0xFF, sizeof(storageRecordBuffer));
  uint8_t *payload = storageRecordBuffer + sizeof(StorageRecordHeader);
  for (uint16_t i = 0; i < count; ++i) {
    payload[i * 2U + 0U] = static_cast<uint8_t>( ids[i]        & 0xFFU);
    payload[i * 2U + 1U] = static_cast<uint8_t>((ids[i] >> 8U) & 0xFFU);
  }

  StorageRecordHeader header{};
  header.magic         = RECORD_MAGIC;
  header.formatVersion = RECORD_FORMAT_VERSION;
  header.type          = RECORD_TYPE_ORDER;
  header.flags         = RECORD_FLAG_COMMITTED;
  header.generation    = storage.generation;
  header.sequence      = nextSequence;
  header.recordId      = recordId;
  header.deviceId      = deviceId;
  header.nameLength    = 0;
  header.rawLength     = count;
  header.carrierHz     = 0;
  header.repeatCount   = 0;
  header.reserved0     = 0;
  header.repeatGapMs   = 0;
  header.payloadLength = payloadLength;
  header.payloadCrc32  = crc32Bytes(payload, payloadLength);
  StorageRecordHeader headerForCrc = header;
  headerForCrc.headerCrc32 = 0;
  header.headerCrc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&headerForCrc),
                                  sizeof(headerForCrc));
  memcpy(storageRecordBuffer, &header, sizeof(header));

  const uint32_t base       = targetSector * STORAGE_FLASH_SECTOR_SIZE;
  const uint16_t bodyLength = static_cast<uint16_t>(sizeof(StorageRecordHeader) + payloadLength);

  if (!storageEnsureSectorBlank(targetSector)) {
    storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  if (!flashProgramBytes(base, storageRecordBuffer, bodyLength)) {
    if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  StorageRecordHeader verifyHeader{};
  if (!storageReadRecordHeaderStable(targetSector, verifyHeader) ||
      !storageValidateRecordHeaderForBody(&verifyHeader, storage.generation) ||
      !storageReadPayloadVerifiedIntoBuffer(targetSector, verifyHeader)) {
    if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  if (!storageCommitRecord(targetSector)) {
    uint32_t observedCommit = 0;
    if (!(storageReadCommitStable(targetSector, observedCommit) && observedCommit == COMMIT_MAGIC)) {
      if (!storageRecoverFailedAppendSector(targetSector)) storageQuarantineAppendSector(targetSector, header);
      return false;
    }
  }
  storage.maxSequence = nextSequence;
  (void)storageUpdateFirstFreeSectorAfterAppend(targetSector);
  return true;
}

// Returns the newest ORDER record's payload for (recordId, deviceId).
// Payload is loaded into storageRecordBuffer (offset 0). out[] gets the IDs.
// Cached reader for the two ORDER record kinds. First call scans once,
// every subsequent call is O(1). Cache is invalidated by writes, DB reset,
// wipe, and full scan.
static bool storageReadLatestOrderRecord(uint32_t recordId, uint32_t deviceId,
                                         uint16_t *out, uint16_t cap, uint16_t &count) {
  count = 0;
  if (!storage.initialized || recordId == 0U || out == nullptr || cap == 0U) return false;

  // ------- Device order (recordId 1, deviceId 0) -------
  if (recordId == 1U && deviceId == 0U) {
    if (!cachedDeviceOrderValid) {
      uint32_t bestSector = UINT32_MAX;
      uint32_t bestSequence = 0;
      for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
        if ((sector & 31UL) == 31UL) yield();
        if (storageIsSectorQuarantined(sector)) continue;
        StorageRecordHeader h{};
        if (!storageReadRecordHeaderStable(sector, h)) continue;
        if (storageIsRecordHeaderBlank(h)) continue;
        if (h.magic != RECORD_MAGIC) continue;
        if (h.formatVersion != RECORD_FORMAT_VERSION) continue;
        if (h.generation != storage.generation) continue;
        if (h.type != RECORD_TYPE_ORDER) continue;
        if (h.recordId != recordId || h.deviceId != deviceId) continue;
        if (bestSector == UINT32_MAX || h.sequence > bestSequence) {
          bestSector = sector;
          bestSequence = h.sequence;
        }
      }
      cachedDeviceOrderCount = 0;
      if (bestSector != UINT32_MAX) {
        StorageRecordHeader header{};
        if (storageReadValidatedRecord(bestSector, header) &&
            header.type == RECORD_TYPE_ORDER &&
            header.rawLength > 0U && header.rawLength <= 64U &&
            header.payloadLength == header.rawLength * 2U) {
          cachedDeviceOrderCount = header.rawLength;
          for (uint16_t i = 0; i < cachedDeviceOrderCount; ++i) {
            cachedDeviceOrder[i] = (uint16_t)storageRecordBuffer[i * 2U] |
                                   ((uint16_t)storageRecordBuffer[i * 2U + 1U] << 8U);
          }
        }
      }
      cachedDeviceOrderValid = true;
    }
    const uint16_t n = (cachedDeviceOrderCount < cap) ? cachedDeviceOrderCount : cap;
    for (uint16_t i = 0; i < n; ++i) out[i] = cachedDeviceOrder[i];
    count = n;
    return n > 0;
  }

  // ------- Command order (recordId == deviceId) -------
  if (deviceId != 0U && recordId == deviceId) {
    if (!cachedCmdOrderValid || cachedCmdOrderDevice != (uint16_t)deviceId) {
      uint32_t bestSector = UINT32_MAX;
      uint32_t bestSequence = 0;
      for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
        if ((sector & 31UL) == 31UL) yield();
        if (storageIsSectorQuarantined(sector)) continue;
        StorageRecordHeader h{};
        if (!storageReadRecordHeaderStable(sector, h)) continue;
        if (storageIsRecordHeaderBlank(h)) continue;
        if (h.magic != RECORD_MAGIC) continue;
        if (h.formatVersion != RECORD_FORMAT_VERSION) continue;
        if (h.generation != storage.generation) continue;
        if (h.type != RECORD_TYPE_ORDER) continue;
        if (h.recordId != recordId || h.deviceId != deviceId) continue;
        if (bestSector == UINT32_MAX || h.sequence > bestSequence) {
          bestSector = sector;
          bestSequence = h.sequence;
        }
      }
      cachedCmdOrderCount = 0;
      cachedCmdOrderDevice = (uint16_t)deviceId;
      if (bestSector != UINT32_MAX) {
        StorageRecordHeader header{};
        if (storageReadValidatedRecord(bestSector, header) &&
            header.type == RECORD_TYPE_ORDER &&
            header.rawLength > 0U && header.rawLength <= 64U &&
            header.payloadLength == header.rawLength * 2U) {
          cachedCmdOrderCount = header.rawLength;
          for (uint16_t i = 0; i < cachedCmdOrderCount; ++i) {
            cachedCmdOrder[i] = (uint16_t)storageRecordBuffer[i * 2U] |
                                ((uint16_t)storageRecordBuffer[i * 2U + 1U] << 8U);
          }
        }
      }
      cachedCmdOrderValid = true;
    }
    const uint16_t n = (cachedCmdOrderCount < cap) ? cachedCmdOrderCount : cap;
    for (uint16_t i = 0; i < n; ++i) out[i] = cachedCmdOrder[i];
    count = n;
    return n > 0;
  }

  return false;
}

// >>> FIX — replaced with cache-aware version.
bool storageFindActiveCommand(uint32_t commandId, uint32_t &sectorOut, uint32_t &deviceIdOut, uint32_t &sequenceOut) {
  sectorOut = UINT32_MAX; deviceIdOut = 0; sequenceOut = 0;
  if (!storage.initialized || commandId == 0 || storageIsCommandDeleted(commandId)) return false;

  // 1) Per-command locator cache. Every cached hit is fully re-validated so a
  //    stale entry (from delete/rename/relearn) safely falls through.
  for (uint8_t i = 0U; i < STORAGE_CMD_CACHE_SIZE; ++i) {
    if (storageCommandCache[i].commandId != commandId) continue;
    const uint32_t cachedSector = storageCommandCache[i].sector;
    if (cachedSector >= storage.sectorCount || storageIsSectorQuarantined(cachedSector)) break;
    StorageRecordHeader h{};
    if (!storageReadValidatedRecord(cachedSector, h)) break;
    if (h.type != RECORD_TYPE_COMMAND || h.recordId != commandId) break;
    if (storageIsDeviceDeleted(h.deviceId)) break;
    sectorOut = cachedSector; deviceIdOut = h.deviceId; sequenceOut = h.sequence;
    return true;
  }

  // 2) Latest-loaded command fast path.
  if (storage.latestCommandId == commandId &&
      storage.latestCommandSector != UINT32_MAX &&
      storage.latestCommandSector < storage.sectorCount &&
      !storageIsSectorQuarantined(storage.latestCommandSector)) {
    StorageRecordHeader h{};
    if (storageReadValidatedRecord(storage.latestCommandSector, h) &&
        h.type == RECORD_TYPE_COMMAND && h.recordId == commandId &&
        !storageIsDeviceDeleted(h.deviceId)) {
      sectorOut = storage.latestCommandSector;
      deviceIdOut = h.deviceId; sequenceOut = h.sequence;
      storageCacheCommandLocation(commandId, sectorOut);
      return true;
    }
  }

  // 3) Full scan fallback (rare now).
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_COMMAND ||
        header.recordId != commandId || storageIsDeviceDeleted(header.deviceId)) continue;
    if (sectorOut == UINT32_MAX || header.sequence > sequenceOut) {
      sectorOut = sector;
      deviceIdOut = header.deviceId;
      sequenceOut = header.sequence;
    }
  }
  if (sectorOut == UINT32_MAX) return false;
  storageCacheCommandLocation(commandId, sectorOut);
  return true;
}

bool storageDeleteDevice(uint32_t deviceId) {
  if (!storage.initialized || deviceId == 0 || storageIsDeviceDeleted(deviceId)) return false;
  char deviceName[STORAGE_MAX_NAME + 1] = {0};
  if (!storageFindDeviceName(deviceId, deviceName, sizeof(deviceName))) return false;
  uint32_t sectorOut = UINT32_MAX;
  uint32_t sequenceOut = 0;
  if (!storageAppendDeleteRecord(RECORD_TYPE_DELETE_DEVICE, deviceId, deviceId, sectorOut, sequenceOut)) return false;
  storageIdentityBitmapSet(storageDeletedDeviceBitmap, deviceId);
  storageRemoveDeviceIndexEntries(deviceId);
  if (storage.currentDeviceId == deviceId) {
    storage.currentDeviceId = 0; storage.currentDeviceName[0] = '\0';
    storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
  }
  if (storage.latestCommandDeviceId == deviceId) {
    storage.latestCommandSector = UINT32_MAX; storage.latestCommandId = 0; storage.latestCommandDeviceId = 0;
  }
  Serial.print(F("Device deleted. ID=")); Serial.print(deviceId); Serial.print(F(" ")); Serial.println(deviceName);
  // >>> NEW — publish removals for every command that belonged to this device.
  for (uint32_t s = DB_DATA_START_SECTOR; s < storage.sectorCount; ++s) {
    if (storageIsSectorQuarantined(s)) continue;
    StorageRecordHeader h{};
    if (!storageReadRecordHeaderStable(s, h)) continue;
    if (storageIsRecordHeaderBlank(h)) continue;
    if (h.type != RECORD_TYPE_COMMAND) continue;
    if (h.deviceId != deviceId) continue;
    (void)mqttRemoveHaButtonDiscovery(h.recordId);
  }
  mqttRequestDiscoveryRefresh();
  (void)sectorOut; (void)sequenceOut;
  return true;
}

bool storageDeleteCommand(uint32_t commandId) {
  if (!storage.initialized || commandId == 0 || storageIsCommandDeleted(commandId)) return false;
  uint32_t commandSector = UINT32_MAX;
  uint32_t deviceId = 0;
  uint32_t commandSequence = 0;
  if (!storageFindActiveCommand(commandId, commandSector, deviceId, commandSequence)) return false;
  uint32_t journalSector = UINT32_MAX;
  uint32_t journalSequence = 0;
  if (!storageAppendDeleteRecord(RECORD_TYPE_DELETE_COMMAND, commandId, deviceId, journalSector, journalSequence)) return false;
  storageIdentityBitmapSet(storageDeletedCommandBitmap, commandId);
  storageRemoveCommandIndex(commandId);
  if (storage.latestCommandId == commandId) {
    storage.latestCommandSector = UINT32_MAX; storage.latestCommandId = 0; storage.latestCommandDeviceId = 0;
  }
  if (storage.currentCommandId == commandId) {
    storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
  }
  Serial.print(F("Command deleted. ID=")); Serial.println(commandId);
  // >>> NEW — push the removal now while MQTT is likely connected.
  (void)mqttRemoveHaButtonDiscovery(commandId);
  mqttRequestDiscoveryRefresh();
  (void)commandSector; (void)commandSequence; (void)journalSector; (void)journalSequence;
  return true;
}

// ---- v3.0.31 rename operations -------------------------------------------

bool storageRenameDevice(uint32_t deviceId, const char *newName) {
  if (!storage.initialized || deviceId == 0 || newName == nullptr ||
      newName[0] == '\0' || strlen(newName) > STORAGE_MAX_NAME ||
      irState != IrEngineState::IDLE) return false;
  if (storageIsDeviceIdentityConflicted(deviceId) || storageIsDeviceDeleted(deviceId)) return false;

  uint32_t collidedId = 0;
  const DeviceLookupResult lookup = storageLookupDeviceByName(newName, collidedId);
  if (lookup == DeviceLookupResult::FOUND) {
    if (collidedId == deviceId) return true;
    Serial.println(F("ERROR: a device with this name already exists."));
    return false;
  }
  if (lookup == DeviceLookupResult::CORRUPT_MATCH) {
    Serial.println(F("ERROR: a damaged device with this name already exists."));
    return false;
  }
  if (lookup == DeviceLookupResult::READ_ERROR) {
    Serial.println(F("ERROR: device lookup could not be completed safely."));
    return false;
  }

  uint32_t currentSector = UINT32_MAX;
  uint32_t currentSequence = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_DEVICE ||
        header.recordId != deviceId || header.deviceId != deviceId) continue;
    if (currentSector == UINT32_MAX || header.sequence > currentSequence) {
      currentSector = sector;
      currentSequence = header.sequence;
    }
  }
  if (currentSector == UINT32_MAX) { Serial.println(F("ERROR: device record not found.")); return false; }

  char currentName[STORAGE_MAX_NAME + 1] = {0};
  if (!storageFindDeviceNameIndexedOnly(deviceId, currentName, sizeof(currentName))) {
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(currentSector, header) ||
        !storageGetRecordName(currentSector, header, currentName, sizeof(currentName))) {
      Serial.println(F("ERROR: current device name could not be read."));
      return false;
    }
  }
  if (strcmp(currentName, newName) == 0) return true;

  if (!storageEnsureFreeSpaceIndex()) return false;

  if (!flashEraseSectorVerified(currentSector)) {
    storageQuarantineSector(currentSector);
    storage.freeSpaceIndexValid = false;
    return false;
  }
  if (storage.usedSectorCount > 0) storage.usedSectorCount--;
  storage.freeSectorCount++;
  storage.firstFreeSector = currentSector;
  storage.freeSpaceIndexValid = true;

  storageRemoveDeviceIndexEntries(deviceId);

  uint32_t newSector = UINT32_MAX;
  uint32_t newSequence = 0;
  if (!storageAppendRecord(RECORD_TYPE_DEVICE, deviceId, deviceId, newName,
                           nullptr, 0, 0, 0, 0, newSector, newSequence)) {
    uint32_t restoreSector = UINT32_MAX;
    uint32_t restoreSequence = 0;
    if (storageAppendRecord(RECORD_TYPE_DEVICE, deviceId, deviceId, currentName,
                            nullptr, 0, 0, 0, 0, restoreSector, restoreSequence)) {
      storage.maxSequence = restoreSequence;
      storageRegisterDeviceIndex(deviceId, restoreSector,
          static_cast<uint16_t>(crc32Bytes(reinterpret_cast<const uint8_t *>(currentName), strlen(currentName))),
          restoreSequence);
      (void)storageUpdateFirstFreeSectorAfterAppend(restoreSector);
      if (storage.currentDeviceId == deviceId) {
        strncpy(storage.currentDeviceName, currentName, STORAGE_MAX_NAME);
        storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
      }
      Serial.println(F("ERROR: rename failed; original device name restored."));
    } else {
      Serial.println(F("ERROR: rename failed and original device could not be restored."));
    }
    return false;
  }

  storage.maxSequence = newSequence;
  storageRegisterDeviceIndex(deviceId, newSector,
      static_cast<uint16_t>(crc32Bytes(
          reinterpret_cast<const uint8_t *>(newName), strlen(newName))),
      newSequence);
  (void)storageUpdateFirstFreeSectorAfterAppend(newSector);

  if (storage.currentDeviceId == deviceId) {
    strncpy(storage.currentDeviceName, newName, STORAGE_MAX_NAME);
    storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
  }

  mqttRequestDiscoveryRefresh();
  Serial.print(F("Device renamed. ID=")); Serial.print(deviceId);
  Serial.print(F(" ")); Serial.print(currentName);
  Serial.print(F(" -> ")); Serial.println(newName);
  (void)currentSequence;
  return true;
}

bool storageRenameCommand(uint32_t commandId, const char *newName) {
  if (!storage.initialized || commandId == 0 || newName == nullptr ||
      newName[0] == '\0' || strlen(newName) > STORAGE_MAX_NAME ||
      irState != IrEngineState::IDLE) return false;
  if (storageIsCommandIdentityConflicted(commandId) || storageIsCommandDeleted(commandId)) return false;

  uint32_t commandSector = UINT32_MAX;
  uint32_t deviceId = 0;
  uint32_t commandSequence = 0;
  if (!storageFindActiveCommand(commandId, commandSector, deviceId, commandSequence)) {
    Serial.println(F("ERROR: command record not found."));
    return false;
  }

  StorageRecordHeader oldHeader{};
  if (!storageReadValidatedRecord(commandSector, oldHeader) ||
      oldHeader.type != RECORD_TYPE_COMMAND || oldHeader.recordId != commandId ||
      oldHeader.deviceId != deviceId || oldHeader.nameLength == 0 ||
      oldHeader.nameLength > STORAGE_MAX_NAME) {
    Serial.println(F("ERROR: command record could not be read safely."));
    return false;
  }

  // Extract name and waveform from storageRecordBuffer immediately.
  // storageFindCommandNameOnDevicePersistent() below also reads through
  // storageRecordBuffer, so anything still needed must be copied first.
  char currentName[STORAGE_MAX_NAME + 1] = {0};
  memcpy(currentName, storageRecordBuffer, oldHeader.nameLength);
  currentName[oldHeader.nameLength] = '\0';

  const uint32_t rawBytes = static_cast<uint32_t>(oldHeader.rawLength) * sizeof(uint16_t);
  if (rawBytes == 0 || rawBytes > sizeof(lastRaw) ||
      oldHeader.nameLength + rawBytes != oldHeader.payloadLength) {
    Serial.println(F("ERROR: command payload layout is not loadable."));
    return false;
  }
  // lastRaw is temporarily repurposed as scratch for the waveform while
  // the old sector is erased and the replacement is written. The loaded
  // command state is cleared on every exit below because lastRaw will no
  // longer correspond to storage.currentCommandId.
  memcpy(lastRaw, storageRecordBuffer + oldHeader.nameLength, rawBytes);

  const uint16_t savedRawLength  = oldHeader.rawLength;
  const uint16_t savedCarrierHz  = oldHeader.carrierHz;
  const uint8_t  savedRepeatCount = oldHeader.repeatCount;
  const uint16_t savedRepeatGapMs = oldHeader.repeatGapMs;
  const bool wasLatestCommand = (storage.latestCommandSector == commandSector ||
                                 storage.latestCommandId == commandId);

  // Bounds check: storageRegisterCommandIndex() silently no-ops on
  // values > 0xFFFF, which is exactly the failure mode that leaves the
  // rename reported-successful but the command unselectable.
  if (commandId > 0xFFFFU || deviceId > 0xFFFFU) {
    Serial.println(F("ERROR: command/device id out of indexable range."));
    clearLastCapture();
    return false;
  }

  auto cleanupAndReturn = [&](bool result) -> bool {
    clearLastCapture();
    return result;
  };

  if (strcmp(currentName, newName) == 0) return cleanupAndReturn(true);

  const CommandNameLookupResult nameLookup =
      storageFindCommandNameOnDevicePersistent(deviceId, newName);
  if (nameLookup == CommandNameLookupResult::FOUND) {
    Serial.println(F("ERROR: a command with this name already exists in this device."));
    return cleanupAndReturn(false);
  }
  if (nameLookup == CommandNameLookupResult::READ_ERROR) {
    Serial.println(F("ERROR: command-name lookup could not be completed safely."));
    return cleanupAndReturn(false);
  }

  if (!storageEnsureFreeSpaceIndex()) return cleanupAndReturn(false);

  if (!flashEraseSectorVerified(commandSector)) {
    storageQuarantineSector(commandSector);
    storage.freeSpaceIndexValid = false;
    return cleanupAndReturn(false);
  }
  if (storage.usedSectorCount > 0) storage.usedSectorCount--;
  storage.freeSectorCount++;
  storage.firstFreeSector = commandSector;
  storage.freeSpaceIndexValid = true;

  if (storage.latestCommandSector == commandSector || storage.latestCommandId == commandId) {
    storage.latestCommandSector = UINT32_MAX;
    storage.latestCommandId = 0;
    storage.latestCommandDeviceId = 0;
  }

  uint32_t newSector = UINT32_MAX;
  uint32_t newSequence = 0;
  if (!storageAppendRecord(RECORD_TYPE_COMMAND, commandId, deviceId, newName,
                           lastRaw, savedRawLength, savedCarrierHz,
                           savedRepeatCount, savedRepeatGapMs,
                           newSector, newSequence)) {
    uint32_t restoreSector = UINT32_MAX;
    uint32_t restoreSequence = 0;
    if (storageAppendRecord(RECORD_TYPE_COMMAND, commandId, deviceId, currentName,
                            lastRaw, savedRawLength, savedCarrierHz,
                            savedRepeatCount, savedRepeatGapMs,
                            restoreSector, restoreSequence)) {
      storage.maxSequence = restoreSequence;
      if (wasLatestCommand) {
        storage.latestCommandSector = restoreSector;
        storage.latestCommandId = commandId;
        storage.latestCommandDeviceId = deviceId;
      }
      (void)storageUpdateFirstFreeSectorAfterAppend(restoreSector);
      Serial.println(F("ERROR: rename failed; original command name restored."));
    } else {
      Serial.println(F("ERROR: rename failed and original command could not be restored."));
    }
    return cleanupAndReturn(false);
  }

  // Readback verification — mirror what storageSaveCurrentCommand() does.
  // Guarantees the flash actually holds what we think it holds before we
  // commit the in-RAM state.
  StorageRecordHeader verifyHeader{};
  bool verifyOk = storageReadValidatedRecord(newSector, verifyHeader);
  if (verifyOk) {
    const uint32_t waveformBytes = static_cast<uint32_t>(savedRawLength) * sizeof(uint16_t);
    verifyOk =
        verifyHeader.type        == RECORD_TYPE_COMMAND &&
        verifyHeader.recordId    == commandId &&
        verifyHeader.deviceId    == deviceId &&
        verifyHeader.rawLength   == savedRawLength &&
        verifyHeader.carrierHz   == savedCarrierHz &&
        verifyHeader.repeatCount == savedRepeatCount &&
        verifyHeader.repeatGapMs == savedRepeatGapMs &&
        verifyHeader.nameLength + waveformBytes == verifyHeader.payloadLength &&
        memcmp(storageRecordBuffer + verifyHeader.nameLength, lastRaw, waveformBytes) == 0;
  }
  if (!verifyOk) {
    Serial.println(F("ERROR: rename readback mismatch; quarantining new sector."));
    storageQuarantineSector(newSector);
    storage.freeSpaceIndexValid = false;
    return cleanupAndReturn(false);
  }

  storage.maxSequence = newSequence;
  if (wasLatestCommand) {
    storage.latestCommandSector = newSector;
    storage.latestCommandId = commandId;
    storage.latestCommandDeviceId = deviceId;
  }
  (void)storageUpdateFirstFreeSectorAfterAppend(newSector);

  storageRegisterCommandIndex(commandId, deviceId, newSector, newSequence);
  storageCacheCommandLocation(commandId, newSector);   // ← invalidate locator cache
  mqttRequestDiscoveryRefresh();
  Serial.print(F("Command renamed. ID=")); Serial.print(commandId);
  Serial.print(F(" ")); Serial.print(currentName);
  Serial.print(F(" -> ")); Serial.println(newName);
  (void)commandSequence;
  return cleanupAndReturn(true);
}

// --------------------------------------------------------------------------

bool storageResetDatabaseTargeted() {
  if (storageJobIsRunning()) return false;
  if (!storageJobStartReset()) return false;
  const uint32_t startMs = millis();
  while (storageJob.state == StorageJobState::RUNNING) {
    storageJobService();
    feedWatchdog();
    yield();
    if ((millis() - startMs) > 300000UL) {   // 5-minute hard ceiling
      storageJobFail("Timeout");
      return false;
    }
  }
  return storageJob.state == StorageJobState::DONE;
}

bool storageWipeDatabase() {
  if (storageJobIsRunning()) return false;
  if (!storageJobStartWipe()) return false;
  const uint32_t startMs = millis();
  while (storageJob.state == StorageJobState::RUNNING) {
    storageJobService();
    feedWatchdog();
    yield();
    if ((millis() - startMs) > 600000UL) {   // 10-minute hard ceiling
      storageJobFail("Timeout");
      return false;
    }
  }
  return storageJob.state == StorageJobState::DONE;
}

// ============================================================
// ASYNC STORAGE JOBS (v1.0)
// ============================================================
// The reset/wipe operations are long (a full W25Q64 wipe blocks for
// ~100 s). These helpers turn them into a state machine driven from
// loop() so the HTTP handler returns immediately and the WebUI can
// poll progress.

bool storageJobIsRunning() {
  return storageJob.state == StorageJobState::RUNNING;
}

static void storageJobLog(const char *msg) {
#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    Serial.print(F("[storage job] "));
    Serial.println(msg);
  }
#endif
}

void storageJobFail(const char *reason) {
  storageJob.state      = StorageJobState::FAILED;
  storageJob.finishedMs = millis();
  snprintf(storageJob.message, sizeof(storageJob.message), "%s", reason);
  storageJobLog(reason);
}

bool storageJobStartReset() {
  if (storageJobIsRunning()) return false;
  if (!storage.detected || storage.sectorCount <= DB_BACKUP_SECTOR) return false;

  DatabaseHeader primary{}, backup{};
  const StableReadResult ps = storageReadDatabaseHeaderStable(DB_PRIMARY_SECTOR, primary);
  const StableReadResult bs = storageReadDatabaseHeaderStable(DB_BACKUP_SECTOR, backup);
  uint32_t curGen = 0;
  if (ps == StableReadResult::VALID && primary.generation > curGen) curGen = primary.generation;
  if (bs == StableReadResult::VALID && backup.generation  > curGen) curGen = backup.generation;

  uint32_t markerSector = UINT32_MAX, markerGen = 0;
  const FormatIntentFindResult ir = storageFindFormatIntent(markerSector, markerGen);
  if (ir == FormatIntentFindResult::UNCERTAIN) return false;
  if (ir == FormatIntentFindResult::FOUND && markerGen > curGen) curGen = markerGen;
  if (curGen == UINT32_MAX) return false;

  storageJob = StorageJobRuntime{};
  storageJob.kind       = StorageJobKind::RESET;
  storageJob.state      = StorageJobState::RUNNING;
  storageJob.phase      = 0;
  storageJob.newGen     = curGen + 1UL;
  if (storageJob.newGen == 0) storageJob.newGen = 1;
  storageJob.hdrPrimary = (markerSector == DB_PRIMARY_SECTOR) ? DB_BACKUP_SECTOR : DB_PRIMARY_SECTOR;
  storageJob.hdrBackup  = (storageJob.hdrPrimary == DB_PRIMARY_SECTOR) ? DB_BACKUP_SECTOR : DB_PRIMARY_SECTOR;
  storageJob.total      = storage.sectorCount - DB_DATA_START_SECTOR;
  storageJob.startedMs  = millis();
  snprintf(storageJob.message, sizeof(storageJob.message), "Preparing reset...");
  storageJobLog("RESET job started");
  mqttDiscoveryClearAllCommandConfigs();
  return true;
}

bool storageJobStartWipe() {
  if (storageJobIsRunning()) return false;
  if (!storage.detected || storage.sectorCount == 0) return false;

  DatabaseHeader primary{}, backup{};
  uint32_t curGen = 0;
  if (storageReadDatabaseHeaderStable(DB_PRIMARY_SECTOR, primary) == StableReadResult::VALID &&
      primary.generation > curGen) curGen = primary.generation;
  if (storageReadDatabaseHeaderStable(DB_BACKUP_SECTOR, backup) == StableReadResult::VALID &&
      backup.generation  > curGen) curGen = backup.generation;
  uint32_t ms = UINT32_MAX, mg = 0;
  if (storageFindFormatIntent(ms, mg) == FormatIntentFindResult::FOUND && mg > curGen) curGen = mg;
  if (curGen == UINT32_MAX) return false;

  storageJob = StorageJobRuntime{};
  storageJob.kind      = StorageJobKind::WIPE;
  storageJob.state     = StorageJobState::RUNNING;
  storageJob.phase     = 0;
  storageJob.newGen    = curGen + 1UL;
  if (storageJob.newGen == 0) storageJob.newGen = 1;
  storageJob.total     = storage.sectorCount;
  storageJob.startedMs = millis();
  snprintf(storageJob.message, sizeof(storageJob.message), "Preparing wipe...");
  storageJobLog("WIPE job started");
  mqttDiscoveryClearAllCommandConfigs();
  return true;
}

static void storageJobStepReset() {
  switch (storageJob.phase) {
    case 0: {
      snprintf(storageJob.message, sizeof(storageJob.message), "Writing header 1/2");
      storageJobLog("reset phase 1: writing primary header");
      if (!storageWriteDatabaseHeader(storageJob.hdrPrimary, storageJob.newGen)) {
        storageJobFail("Primary header write failed"); return;
      }
      storageJob.phase = 1;      break;
    }
    case 1: {
      snprintf(storageJob.message, sizeof(storageJob.message), "Writing header 2/2");
      storageJobLog("reset phase 2: writing backup header");
      if (!storageWriteDatabaseHeader(storageJob.hdrBackup, storageJob.newGen)) {
        storageJobFail("Backup header write failed"); return;
      }
      storageResetRuntimeDatabaseState();
      storage.generation    = storageJob.newGen;
      storage.initialized   = false;
      storage.headerCorrupt = false;
      storageJob.cursor     = DB_DATA_START_SECTOR;
      storageJob.processed  = 0;
      storageJob.failed     = 0;
      storageJob.phase      = 2;
      storageJobLog("reset phase 3: reclaiming data sectors");
      break;
    }
    case 2: {
      if (storageJob.cursor >= storage.sectorCount) {
        storageJob.phase = 3; break;
      }
      const uint32_t sector = storageJob.cursor++;
      if (storageIsSectorQuarantined(sector)) {
        storageJob.processed++; storageJob.failed++;
      } else {
        StorageRecordHeader h{};
        const bool readable = storageReadRecordHeaderStable(sector, h);
        if (!readable) {
          storageMarkSectorQuarantined(sector);
          storageJob.failed++;
        } else if (!storageIsRecordHeaderBlank(h)) {
          if (!flashEraseSectorVerified(sector)) {
            storageMarkSectorQuarantined(sector);
            storageJob.failed++;
          }
        }
        storageJob.processed++;
      }
      if ((storageJob.processed & 7U) == 0U || storageJob.processed == storageJob.total) {
        snprintf(storageJob.message, sizeof(storageJob.message),
                 "Reclaiming sectors (%u/%u)",
                 (unsigned)storageJob.processed, (unsigned)storageJob.total);
      }
      break;
    }
    case 3: {
      snprintf(storageJob.message, sizeof(storageJob.message), "Rescanning database");
      storageJobLog("reset phase 4: rescanning");
      if (!storageScanDatabase()) { storageJobFail("Post-reset scan failed"); return; }
      storage.initialized = true;
      if (!configurationInitializeCommandHue(storage.generation)) {
        storageJobLog("warning: per-command Hue reset failed");
      }
      if (!configurationInitializeDeviceCloud(storage.generation)) {
        storageJobLog("warning: device Alexa Cloud settings reset failed");
      }      
      clearLastCapture();
      storage.currentDeviceId = 0; storage.currentDeviceName[0] = '\0';
      storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
      storageInvalidateOrderCache();
      mqttRequestDiscoveryRefresh();
      storageJob.state      = StorageJobState::DONE;
      storageJob.finishedMs = millis();
      snprintf(storageJob.message, sizeof(storageJob.message),
               "Reset complete: %u erased, %u failed",
               (unsigned)(storageJob.processed - storageJob.failed),
               (unsigned)storageJob.failed);
      storageJobLog(storageJob.message);
      break;
    }
  }
}

static void storageJobStepWipe() {
  switch (storageJob.phase) {
    case 0: {
      storageResetRuntimeDatabaseState();
      storage.generation    = storageJob.newGen;
      storage.headerCorrupt = false;
      storage.initialized   = false;
      storageJob.cursor     = 0;
      storageJob.phase      = 1;
      storageJobLog("wipe phase 1: erasing all sectors");
      break;
    }
    case 1: {
      if (storageJob.cursor >= storage.sectorCount) {
        storageJob.phase = 2; break;
      }
      const uint32_t sector = storageJob.cursor++;
      if (!flashEraseSectorVerified(sector)) {
        storageMarkSectorQuarantined(sector);
        storageJob.failed++;
      }
      storageJob.processed++;
      if ((storageJob.processed & 7U) == 0U || storageJob.processed == storageJob.total) {
        snprintf(storageJob.message, sizeof(storageJob.message),
                 "Erasing sectors (%u/%u)",
                 (unsigned)storageJob.processed, (unsigned)storageJob.total);
      }
      break;
    }
    case 2: {
      if (storageIsSectorQuarantined(DB_PRIMARY_SECTOR) ||
          storageIsSectorQuarantined(DB_BACKUP_SECTOR)) {
        storageJobFail("Header sectors unusable"); return;
      }
      storageJobLog("wipe phase 2: writing primary header");
      if (!storageWriteDatabaseHeader(DB_PRIMARY_SECTOR, storageJob.newGen)) {
        storageJobFail("Primary header write failed"); return;
      }
      storageJob.phase = 3;
      break;
    }
    case 3: {
      storageJobLog("wipe phase 3: writing backup header");
      if (!storageWriteDatabaseHeader(DB_BACKUP_SECTOR, storageJob.newGen)) {
        storageJobFail("Backup header write failed"); return;
      }
      storageJob.phase = 4;
      break;
    }
    case 4: {
      storageJobLog("wipe phase 4: rescanning");
      if (!storageScanDatabase()) { storageJobFail("Post-wipe scan failed"); return; }
      storage.initialized = true;
      if (!configurationInitializeCommandHue(storage.generation)) {
        storageJobLog("warning: per-command Hue reset failed");
      }
      if (!configurationInitializeDeviceCloud(storage.generation)) {
        storageJobLog("warning: device Alexa Cloud settings reset failed");
      }      
      storageInvalidateOrderCache();
      mqttRequestDiscoveryRefresh();
      storageJob.state      = StorageJobState::DONE;
      storageJob.finishedMs = millis();
      snprintf(storageJob.message, sizeof(storageJob.message),
               "Wipe complete: %u erased, %u failed",
               (unsigned)(storageJob.processed - storageJob.failed),
               (unsigned)storageJob.failed);
      storageJobLog(storageJob.message);
      break;
    }
  }
}

void storageJobService() {
  if (storageJob.state != StorageJobState::RUNNING) return;
  feedWatchdog();
  switch (storageJob.kind) {
    case StorageJobKind::RESET: storageJobStepReset(); break;
    case StorageJobKind::WIPE:  storageJobStepWipe();  break;
    default: break;
  }
}

uint8_t storageJobGetPercent() {
  if (storageJob.state == StorageJobState::IDLE)   return 0;
  if (storageJob.state == StorageJobState::DONE)   return 100;
  if (storageJob.state == StorageJobState::FAILED) return 100;
  const uint32_t t = storageJob.total ? storageJob.total : 1;
  const uint32_t per = (storageJob.processed * 90UL) / t;
  if (storageJob.kind == StorageJobKind::RESET) {
    switch (storageJob.phase) {
      case 0: return 2;
      case 1: return 4;
      case 2: return static_cast<uint8_t>(5 + per);
      case 3: return 97;
      default: return 99;
    }
  } else {
    switch (storageJob.phase) {
      case 0: return 1;
      case 1: return static_cast<uint8_t>(per);
      case 2: return 92;
      case 3: return 95;
      case 4: return 98;
      default: return 99;
    }
  }
}

bool storageReadFormatIntentSlotStable(uint32_t sector, uint32_t slot, FormatIntent &intent, bool &blank) {
  blank = false;
  if (!storage.detected || sector > DB_BACKUP_SECTOR || slot >= FORMAT_INTENT_SLOT_COUNT) return false;
  const uint32_t base = sector * STORAGE_FLASH_SECTOR_SIZE + FORMAT_INTENT_OFFSET + slot * FORMAT_INTENT_SLOT_SIZE;
  uint8_t first[sizeof(FormatIntent)];
  uint8_t second[sizeof(FormatIntent)];
  if (!flashReadBytes(base, first, sizeof(first)) || !flashReadBytes(base, second, sizeof(second)) ||
      memcmp(first, second, sizeof(first)) != 0 || !flashReadBytes(base, first, sizeof(first)) ||
      memcmp(first, second, sizeof(first)) != 0) return false;
  blank = true;
  for (size_t i = 0; i < sizeof(first); ++i) if (first[i] != 0xFFU) { blank = false; break; }
  if (blank) { memset(&intent, 0xFF, sizeof(intent)); return true; }
  memcpy(&intent, first, sizeof(intent));
  return true;
}

FormatIntentFindResult storageFindFormatIntent(uint32_t &sectorOut, uint32_t &generationOut) {
  sectorOut = UINT32_MAX;
  generationOut = 0;
  bool foundAny = false;
  for (uint32_t sector = DB_PRIMARY_SECTOR; sector <= DB_BACKUP_SECTOR; ++sector) {
    for (uint32_t slot = 0; slot < FORMAT_INTENT_SLOT_COUNT; ++slot) {
      bool blank = false;
      FormatIntent intent{};
      if (!storageReadFormatIntentSlotStable(sector, slot, intent, blank)) return FormatIntentFindResult::UNCERTAIN;
      if (blank) continue;
      if (intent.commit != COMMIT_MAGIC) continue;
      if (intent.magic != FORMAT_INTENT_MAGIC || intent.version != FORMAT_INTENT_VERSION ||
          intent.generation == 0) return FormatIntentFindResult::UNCERTAIN;
      FormatIntent copy = intent;
      const uint32_t supplied = copy.crc32;
      copy.crc32 = 0;
      copy.commit = 0;
      if (supplied != crc32Bytes(reinterpret_cast<const uint8_t *>(&copy), sizeof(copy)))
        return FormatIntentFindResult::UNCERTAIN;
      foundAny = true;
      if (intent.generation > generationOut) {
        generationOut = intent.generation;
        sectorOut = sector;
      }
    }
  }
  return foundAny ? FormatIntentFindResult::FOUND : FormatIntentFindResult::NONE;
}

bool storageDataAreaHeadersBlank() {
  if (!storage.detected) return false;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) return false;
    if (!storageIsRecordHeaderBlank(header) || !flashIsSectorBlank(sector)) return false;
    if ((sector & 15UL) == 15UL) yield();
  }
  return true;
}

bool storageInitializeDatabase() {
  DatabaseHeader primary{};
  DatabaseHeader backup{};
  const StableReadResult primaryStatus = storageReadDatabaseHeaderStable(DB_PRIMARY_SECTOR, primary);
  const StableReadResult backupStatus = storageReadDatabaseHeaderStable(DB_BACKUP_SECTOR, backup);
  const bool primaryValid = primaryStatus == StableReadResult::VALID;
  const bool backupValid = backupStatus == StableReadResult::VALID;
  uint32_t newestHeaderGeneration = 0;
  if (primaryValid && primary.generation > newestHeaderGeneration) newestHeaderGeneration = primary.generation;
  if (backupValid && backup.generation > newestHeaderGeneration) newestHeaderGeneration = backup.generation;
  uint32_t markerSector = UINT32_MAX;
  uint32_t markerGeneration = 0;
  const FormatIntentFindResult intentResult = storageFindFormatIntent(markerSector, markerGeneration);
  if (intentResult == FormatIntentFindResult::FOUND &&
      ((!primaryValid && !backupValid) || markerGeneration >= newestHeaderGeneration)) {
    Serial.println(F("WARNING: interrupted legacy destructive reset detected; abandoning full-chip resume."));
    Serial.println(F("Recovering with targeted generation reset."));
    storage.initialized = false;
    return storageResetDatabaseTargeted();
  }
  if (intentResult == FormatIntentFindResult::UNCERTAIN ||
      primaryStatus == StableReadResult::UNCERTAIN || backupStatus == StableReadResult::UNCERTAIN) {
    storage.initialized = false; storage.headerCorrupt = true; return false;
  }
  if (primaryValid || backupValid) {
    const DatabaseHeader &chosen = (primaryValid && backupValid)
        ? ((primary.generation >= backup.generation) ? primary : backup)
        : (primaryValid ? primary : backup);
    storage.generation = chosen.generation;
    storage.initialized = false;
    if (!primaryValid && !storageWriteDatabaseHeader(DB_PRIMARY_SECTOR, storage.generation)) {
      storage.initialized = false; return false;
    }
    if (!backupValid && !storageWriteDatabaseHeader(DB_BACKUP_SECTOR, storage.generation)) {
      storage.initialized = false; return false;
    }
    storage.initialized = true;
    return true;
  }
  if (storageDataAreaHeadersBlank()) {
    storage.generation = 1;
    storage.initialized = false;
    if (!flashEraseSectorVerified(DB_PRIMARY_SECTOR) || !flashEraseSectorVerified(DB_BACKUP_SECTOR)) {
      storage.initialized = false; return false;
    }
    if (!storageWriteDatabaseHeader(DB_PRIMARY_SECTOR, storage.generation) ||
        !storageWriteDatabaseHeader(DB_BACKUP_SECTOR, storage.generation)) {
      storage.initialized = false; return false;
    }
    storage.initialized = true;
    return true;
  }
  storage.headerCorrupt = true;
  storage.initialized = false;
  return false;
}

bool storageDeviceIdIsUniquePersistent(uint32_t deviceId, uint32_t canonicalSector) {
  if (!storage.initialized || deviceId == 0 || storageIsDeviceIdentityConflicted(deviceId)) return false;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (sector == canonicalSector) continue;
    if (storageIsSectorQuarantined(sector)) return false;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) return false;
    if (storageIsRecordHeaderBlank(header)) continue;
    if (header.magic == RECORD_MAGIC && header.formatVersion == RECORD_FORMAT_VERSION &&
        header.generation == storage.generation &&
        header.type != RECORD_TYPE_DELETE_DEVICE &&
        header.type != RECORD_TYPE_DELETE_COMMAND &&
        (header.recordId == deviceId || header.deviceId == deviceId)) return false;
  }
  return true;
}

DeviceLookupResult storageFindDeviceByNamePersistent(const char *name, uint16_t nameCrc,
                                                     size_t nameLength, uint32_t &deviceIdOut) {
  (void)nameCrc;
  deviceIdOut = 0;
  uint32_t bestSequence = 0;
  uint32_t bestSector = UINT32_MAX;
  bool corruptExactMatch = false;
  uint32_t corruptExactId = 0;
  bool readError = false;
  uint8_t nameBytes[STORAGE_MAX_NAME + 1] = {0};
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) { readError = true; continue; }
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) { readError = true; continue; }
    if (storageIsRecordHeaderBlank(header)) continue;
    if (header.magic != RECORD_MAGIC || header.formatVersion != RECORD_FORMAT_VERSION ||
        header.generation != storage.generation) continue;
    if (header.type == RECORD_TYPE_DELETE_DEVICE || header.type == RECORD_TYPE_DELETE_COMMAND) continue;
    if (header.type != RECORD_TYPE_DEVICE) {
      if (header.type != RECORD_TYPE_COMMAND || !storageValidateRecordHeader(header)) readError = true;
      continue;
    }
    if (header.recordId == 0 || storageIsDeviceDeleted(header.recordId) ||
        header.deviceId != header.recordId || header.recordId > storageMaxPhysicalIdentity() ||
        header.nameLength == 0 || header.nameLength > STORAGE_MAX_NAME) { readError = true; continue; }
    if (header.nameLength != nameLength) continue;
    uint32_t commit = 0;
    if (!storageReadCommitStable(sector, commit)) { readError = true; continue; }
    if (commit != COMMIT_MAGIC) { readError = true; continue; }
    const uint32_t nameAddress = sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader);
    if (!flashReadSmallConfirmed(nameAddress, nameBytes, static_cast<uint16_t>(nameLength))) {
      readError = true; continue;
    }
    if (memcmp(nameBytes, name, nameLength) != 0) continue;
    const uint32_t namePayloadCrc = crc32Bytes(nameBytes, nameLength);
    const bool headerAndNameAreValid = storageValidateRecordHeader(header) &&
                                       namePayloadCrc == header.payloadCrc32;
    if (headerAndNameAreValid) {
      if (bestSector == UINT32_MAX || header.sequence > bestSequence) {
        bestSector = sector; bestSequence = header.sequence; deviceIdOut = header.recordId;
      }
    } else {
      corruptExactMatch = true;
      if (corruptExactId == 0 || header.sequence > bestSequence) corruptExactId = header.recordId;
    }
  }
  if (bestSector != UINT32_MAX) {
    if (!storageDeviceIdIsUniquePersistent(deviceIdOut, bestSector)) {
      deviceIdOut = 0; return DeviceLookupResult::READ_ERROR;
    }
    storageRegisterDeviceIndex(deviceIdOut, bestSector,
        static_cast<uint16_t>(crc32Bytes(reinterpret_cast<const uint8_t *>(name), nameLength)),
        bestSequence);
    return DeviceLookupResult::FOUND;
  }
  if (corruptExactMatch && corruptExactId != 0) {
    deviceIdOut = corruptExactId; return DeviceLookupResult::CORRUPT_MATCH;
  }
  // >>> FIX — read errors on *unrelated* records must not block creating a
  // brand-new device. Any corruption here is not evidence the new name
  // collides, so fall through to NOT_FOUND and let the caller create it.
  if (readError) {
    DBG_PRINT(F("storageFindDeviceByNamePersistent: non-fatal read errors during lookup of \""));
    DBG_PRINT(name); DBG_PRINTLN(F("\" - treating as NOT_FOUND."));
  }
  return DeviceLookupResult::NOT_FOUND;
}

DeviceLookupResult storageLookupDeviceByName(const char *name, uint32_t &deviceIdOut) {
  deviceIdOut = 0;
  if (!storage.initialized || name == nullptr || name[0] == '\0' ||
      strlen(name) > STORAGE_MAX_NAME) return DeviceLookupResult::READ_ERROR;
  const size_t requestedLength = strlen(name);
  const uint16_t requestedCrc = static_cast<uint16_t>(
      crc32Bytes(reinterpret_cast<const uint8_t *>(name), requestedLength));
  uint32_t bestCachedId = 0;
  uint32_t bestCachedSequence = 0;
  for (uint16_t i = 0; i < storageDeviceIndexCount; ++i) {
    if (storageDeviceIndex[i].nameCrc != requestedCrc) continue;
    const uint32_t sector = storageDeviceIndex[i].sector;
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_DEVICE ||
        header.recordId != storageDeviceIndex[i].id ||
        header.deviceId != storageDeviceIndex[i].id) continue;
    if (header.nameLength != requestedLength ||
        memcmp(storageRecordBuffer, name, requestedLength) != 0) continue;
    if (bestCachedId == 0 || header.sequence > bestCachedSequence) {
      bestCachedId = header.recordId; bestCachedSequence = header.sequence;
    }
  }
  if (bestCachedId != 0) { deviceIdOut = bestCachedId; return DeviceLookupResult::FOUND; }
  uint32_t persistentId = 0;
  const DeviceLookupResult persistentResult =
      storageFindDeviceByNamePersistent(name, requestedCrc, requestedLength, persistentId);
  if (persistentResult == DeviceLookupResult::FOUND ||
      persistentResult == DeviceLookupResult::CORRUPT_MATCH) deviceIdOut = persistentId;
  return persistentResult;
}

CommandNameLookupResult storageFindCommandNameOnDevicePersistent(uint32_t deviceId, const char *name) {
  if (!storage.initialized || deviceId == 0 || name == nullptr || name[0] == '\0' ||
      strlen(name) > STORAGE_MAX_NAME || storageIsDeviceDeleted(deviceId))
    return CommandNameLookupResult::READ_ERROR;
  const size_t requestedLength = strlen(name);
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) return CommandNameLookupResult::READ_ERROR;
    if (storageIsRecordHeaderBlank(header)) continue;
    if (header.magic != RECORD_MAGIC || header.formatVersion != RECORD_FORMAT_VERSION ||
        header.generation != storage.generation) continue;
    if (header.type != RECORD_TYPE_COMMAND || header.deviceId != deviceId ||
        header.recordId == 0 || storageIsCommandDeleted(header.recordId) ||
        storageIsDeviceDeleted(header.deviceId)) continue;
    if (!storageValidateRecordHeader(header)) return CommandNameLookupResult::READ_ERROR;
    uint32_t commit = 0;
    if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC)
      return CommandNameLookupResult::READ_ERROR;
    if (header.nameLength == 0 || header.nameLength > STORAGE_MAX_NAME ||
        header.nameLength != requestedLength) continue;
    if (!storageReadPayloadVerifiedIntoBuffer(sector, header))
      return CommandNameLookupResult::READ_ERROR;
    if (memcmp(storageRecordBuffer, name, requestedLength) == 0)
      return CommandNameLookupResult::FOUND;
  }
  return CommandNameLookupResult::NOT_FOUND;
}

bool storageSelectOrCreateDevice(const char *name) {
  if (!storage.initialized || name == nullptr || name[0] == '\0' ||
      strlen(name) > STORAGE_MAX_NAME) return false;
  uint32_t existingId = 0;
  const DeviceLookupResult lookup = storageLookupDeviceByName(name, existingId);
  if (lookup == DeviceLookupResult::FOUND) {
    storage.currentDeviceId = existingId;
    strncpy(storage.currentDeviceName, name, STORAGE_MAX_NAME);
    storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
    return true;
  }
  if (lookup == DeviceLookupResult::CORRUPT_MATCH) {
    Serial.println(F("ERROR: a damaged device with this name already exists; refusing duplicate."));
    return false;
  }
  if (lookup == DeviceLookupResult::READ_ERROR) {
    // >>> FIX — diagnostics (should now be unreachable from the persistent path,
    // but keep the guard so the cache path can never silently mis-classify).
    Serial.print(F("storageSelectOrCreateDevice: lookup READ_ERROR for \""));
    Serial.print(name); Serial.println('"');
    Serial.print(F("  initialized=")); Serial.println(storage.initialized);
    Serial.print(F("  maxDeviceId=")); Serial.println(storage.maxDeviceId);
    Serial.print(F("  corrupt="));     Serial.println(storage.corruptRecordCount);
    Serial.print(F("  quarantined=")); Serial.println(storage.usedSectorCount - storage.validRecordCount);
    return false;
  }
  uint32_t newId = 0;
  if (!storageNextRecordId(RECORD_TYPE_DEVICE, newId)) {
    Serial.println(F("ERROR: device ID space exhausted.")); return false;
  }
  uint32_t sectorOut = UINT32_MAX;
  uint32_t sequenceOut = 0;
  if (!storageAppendRecord(RECORD_TYPE_DEVICE, newId, newId, name, nullptr, 0, 0, 0, 0,
                           sectorOut, sequenceOut)) return false;
  storage.currentDeviceId = newId;
  strncpy(storage.currentDeviceName, name, STORAGE_MAX_NAME);
  storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
  storage.maxDeviceId = newId;
  storage.maxSequence = sequenceOut;
  storageRegisterDeviceIndex(newId, sectorOut,
      crc32Bytes(reinterpret_cast<const uint8_t *>(name), strlen(name)), sequenceOut);
  storage.validRecordCount++;
  (void)storageUpdateFirstFreeSectorAfterAppend(sectorOut);
  return true;
}

bool storageSaveCurrentCommand(const char *name) {
  if (!storage.initialized || !hasLearnedSignal() || name == nullptr || name[0] == '\0' ||
      strlen(name) > STORAGE_MAX_NAME) return false;
  if (storage.currentDeviceId == 0) {
    Serial.println(F("ERROR: no device selected; select/create a device before saving the command."));
    return false;
  } else {
    char validatedDeviceName[STORAGE_MAX_NAME + 1] = {0};
    if (!storageFindDeviceName(storage.currentDeviceId, validatedDeviceName,
                               sizeof(validatedDeviceName))) {
      storage.currentDeviceId = 0; storage.currentDeviceName[0] = '\0';
      Serial.println(F("ERROR: current device is no longer valid.")); return false;
    }
    strncpy(storage.currentDeviceName, validatedDeviceName, STORAGE_MAX_NAME);
    storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
  }
  const CommandNameLookupResult nameLookup =
      storageFindCommandNameOnDevicePersistent(storage.currentDeviceId, name);
  if (nameLookup == CommandNameLookupResult::FOUND) {
    Serial.println(F("ERROR: a command with this name already exists in the current device."));
    return false;
  }
  if (nameLookup == CommandNameLookupResult::READ_ERROR) {
    Serial.println(F("ERROR: command-name lookup could not be completed safely.")); return false;
  }
  uint32_t newId = 0;
  if (!storageNextRecordId(RECORD_TYPE_COMMAND, newId)) {
    Serial.println(F("ERROR: command ID space exhausted.")); return false;
  }

  const uint16_t saveCarrierHz = lastLearnMetadataValid ? lastLearnCarrierHz : txFrequencyHz;
  const uint8_t saveRepeatCount = lastLearnMetadataValid ? lastLearnRepeatCount : txRepeatCount;
  const uint16_t saveRepeatGapMs = lastLearnMetadataValid ? lastLearnRepeatGapMs : txRepeatGapMs;

  uint32_t sectorOut = UINT32_MAX;
  uint32_t sequenceOut = 0;
  if (!storageAppendRecord(RECORD_TYPE_COMMAND, newId, storage.currentDeviceId, name,
                           lastRaw, lastRawLen, saveCarrierHz, saveRepeatCount,
                           saveRepeatGapMs, sectorOut, sequenceOut)) return false;

  // Immediate readback verification. The storage layer already verifies its
  // payload CRC before committing, but this extra comparison proves that the
  // exact in-RAM learned waveform and TX metadata survived the complete save/read
  // boundary.
  StorageRecordHeader verifyHeader{};
  bool saveReadbackOk = storageReadValidatedRecord(sectorOut, verifyHeader);
  if (saveReadbackOk) {
    const uint32_t waveformBytes = static_cast<uint32_t>(lastRawLen) * sizeof(uint16_t);
    saveReadbackOk =
        verifyHeader.type == RECORD_TYPE_COMMAND &&
        verifyHeader.recordId == newId &&
        verifyHeader.deviceId == storage.currentDeviceId &&
        verifyHeader.rawLength == lastRawLen &&
        verifyHeader.carrierHz == saveCarrierHz &&
        verifyHeader.repeatCount == saveRepeatCount &&
        verifyHeader.repeatGapMs == saveRepeatGapMs &&
        verifyHeader.nameLength + waveformBytes == verifyHeader.payloadLength &&
        memcmp(storageRecordBuffer + verifyHeader.nameLength, lastRaw, waveformBytes) == 0;
  }
  if (!saveReadbackOk) {
    Serial.println(F("ERROR: command save readback mismatch; quarantining the written sector."));
    storageQuarantineSector(sectorOut);
    storage.freeSpaceIndexValid = false;
    mqttRequestDiscoveryRefresh();
    return false;
  }
  DBG_PRINT(F("Command save verified: ID=")); DBG_PRINT(newId);
  DBG_PRINT(F(" raw=")); DBG_PRINT(lastRawLen);
  DBG_PRINT(F(" crc=0x")); DBG_PRINTLN(lastRawCrc32, HEX);
  storage.maxCommandId = newId;
  storage.maxSequence = sequenceOut;
  storage.validRecordCount++;
  storage.latestCommandSector = sectorOut;
  storage.latestCommandId = newId;
  (void)storageUpdateFirstFreeSectorAfterAppend(sectorOut);
  storage.currentCommandId = newId;
  strncpy(storage.currentCommandName, name, STORAGE_MAX_NAME);
  storage.currentCommandName[STORAGE_MAX_NAME] = '\0';
  storageCacheCommandLocation(newId, sectorOut);
  storageRegisterCommandIndex(newId, storage.currentDeviceId, sectorOut, sequenceOut);
  mqttRequestDiscoveryRefresh();
  return true;
}

bool storageRelearnCommand(uint32_t commandId) {
  if (!storage.initialized || commandId == 0 ||
      storageIsCommandIdentityConflicted(commandId) ||
      storageIsCommandDeleted(commandId) ||
      irState != IrEngineState::IDLE) return false;
  uint32_t commandSector = UINT32_MAX;
  uint32_t deviceId = 0;
  uint32_t commandSequence = 0;
  if (!storageFindActiveCommand(commandId, commandSector, deviceId, commandSequence) ||
      commandSector >= storage.sectorCount) return false;
  StorageRecordHeader oldHeader{};
  if (!storageReadValidatedRecord(commandSector, oldHeader) ||
      oldHeader.type != RECORD_TYPE_COMMAND || oldHeader.recordId != commandId ||
      oldHeader.deviceId != deviceId) return false;
  char commandName[STORAGE_MAX_NAME + 1] = {0};
  if (oldHeader.nameLength == 0 || oldHeader.nameLength > STORAGE_MAX_NAME) return false;
  memcpy(commandName, storageRecordBuffer, oldHeader.nameLength);
  commandName[oldHeader.nameLength] = '\0';
  const uint16_t savedCarrierHz = oldHeader.carrierHz;
  const uint8_t savedRepeatCount = oldHeader.repeatCount;
  const uint16_t savedRepeatGapMs = oldHeader.repeatGapMs;
  if (!storageEnsureFreeSpaceIndex()) return false;
  if (!flashEraseSectorVerified(commandSector)) {
    storageQuarantineSector(commandSector); storage.freeSpaceIndexValid = false; return false;
  }
  if (storage.usedSectorCount > 0) storage.usedSectorCount--;
  storage.freeSectorCount++;
  storage.firstFreeSector = commandSector;
  storage.freeSpaceIndexValid = true;
  if (storage.latestCommandSector == commandSector || storage.latestCommandId == commandId) {
    storage.latestCommandSector = UINT32_MAX;
    storage.latestCommandId = 0;
    storage.latestCommandDeviceId = 0;
  }
  if (storage.currentCommandId == commandId) {
    storage.currentCommandId = 0; storage.currentCommandName[0] = '\0';
  }
  clearLastCapture();
  mqttRequestDiscoveryRefresh();
  Serial.print(F("Existing command deleted. ID=")); Serial.print(commandId);
  Serial.print(F(" ")); Serial.print(commandName); Serial.println(F("."));
  Serial.println(F("Starting Learn mode for replacement command."));
  statusLedStartLearning();
  const bool learned = learnSignal();
  statusLedStopLearning();
  if (!learned) {
    Serial.println(F("ERROR: replacement Learn failed; command remains deleted."));
    return false;
  }
  uint32_t newSector = UINT32_MAX;
  uint32_t newSequence = 0;
  if (!storageAppendRecord(RECORD_TYPE_COMMAND, commandId, deviceId, commandName,
                           lastRaw, lastRawLen, savedCarrierHz,
                           savedRepeatCount, savedRepeatGapMs,
                           newSector, newSequence)) {
    Serial.println(F("ERROR: replacement command could not be saved; command remains deleted."));
    return false;
  }
  storage.maxSequence = newSequence;
  storage.validRecordCount++;
  storage.latestCommandSector = newSector;
  storage.latestCommandId = commandId;
  storage.latestCommandDeviceId = deviceId;
  storage.currentDeviceId = deviceId;
  char deviceName[STORAGE_MAX_NAME + 1] = {0};
  if (storageFindDeviceName(deviceId, deviceName, sizeof(deviceName))) {
    strncpy(storage.currentDeviceName, deviceName, STORAGE_MAX_NAME);
    storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
  } else storage.currentDeviceName[0] = '\0';
  storage.currentCommandId = commandId;
  strncpy(storage.currentCommandName, commandName, STORAGE_MAX_NAME);
  storage.currentCommandName[STORAGE_MAX_NAME] = '\0';
  storageRegisterCommandIndex(commandId, deviceId, newSector, newSequence);
  storageCacheCommandLocation(commandId, newSector);   // ← invalidate locator cache
  mqttRequestDiscoveryRefresh();
  Serial.print(F("Command relearned successfully. ID=")); Serial.println(commandId);
  (void)commandSequence;
  return true;
}

bool storageFindDeviceName(uint32_t deviceId, char *name, size_t nameCapacity) {
  if (!storage.initialized || deviceId == 0 || name == nullptr || nameCapacity < 2) return false;
  if (storageFindDeviceNameIndexedOnly(deviceId, name, nameCapacity)) return true;
  if (storageRecoverDeviceIndexEntry(deviceId) &&
      storageFindDeviceNameIndexedOnly(deviceId, name, nameCapacity)) return true;
  return false;
}

void storageQuarantineSector(uint32_t sector) {
  if (!storage.detected || sector < DB_DATA_START_SECTOR ||
      sector >= storage.sectorCount || storageIsSectorQuarantined(sector)) return;
  storageMarkSectorQuarantined(sector);
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = false;
}

void storageQuarantineAppendSector(uint32_t sector, const StorageRecordHeader &header) {
  storageReserveIdentityFromHeader(header);
  if (!storageIsSectorQuarantined(sector)) {
    storageMarkSectorQuarantined(sector);
    if (storage.freeSectorCount > 0) storage.freeSectorCount--;
    storage.usedSectorCount++;
  }
  storage.firstFreeSector = UINT32_MAX;
  storage.freeSpaceIndexValid = false;
}

bool storageLoadCommandFromSectorInternal(uint32_t sector, uint32_t commandId, bool allowDeviceRecovery) {
  if (storageIsSectorQuarantined(sector)) return false;

  // Phase 1: header-only read so we learn deviceId. Both the device-name
  // lookup and the payload read below use storageRecordBuffer as their
  // destination, so the lookup must complete before the command payload
  // is loaded. This ordering removes the need for an intermediate
  // waveform staging buffer.
  StorageRecordHeader header{};
  if (!storageReadRecordHeaderStable(sector, header) ||
      storageIsRecordHeaderBlank(header) ||
      !storageValidateRecordHeader(header) ||
      header.type != RECORD_TYPE_COMMAND ||
      storageIsCommandDeleted(commandId) ||
      storageIsDeviceDeleted(header.deviceId) ||
      header.recordId != commandId) return false;

  char deviceName[STORAGE_MAX_NAME + 1] = {0};
  if (!storageFindDeviceNameIndexedOnly(header.deviceId, deviceName, sizeof(deviceName))) {
    if (!allowDeviceRecovery ||
        !storageRecoverDeviceIndexEntry(header.deviceId) ||
        !storageFindDeviceNameIndexedOnly(header.deviceId, deviceName, sizeof(deviceName))) return false;
  }

  // Phase 2: full read with payload verification, then extract directly
  // from storageRecordBuffer into lastRaw.
  if (!storageReadValidatedRecord(sector, header)) return false;
  const uint32_t rawBytes = static_cast<uint32_t>(header.rawLength) * sizeof(uint16_t);
  if (header.nameLength == 0 || rawBytes == 0 ||
      header.nameLength + rawBytes != header.payloadLength ||
      rawBytes > sizeof(lastRaw)) return false;

  char commandName[STORAGE_MAX_NAME + 1] = {0};
  memcpy(commandName, storageRecordBuffer, header.nameLength);
  commandName[header.nameLength] = '\0';
  memcpy(lastRaw, storageRecordBuffer + header.nameLength, rawBytes);
  lastRawLen = header.rawLength;
  lastRawCrc32 = crc32Buffer(lastRaw, lastRawLen);
  lastLearnCarrierHz = header.carrierHz;
  lastLearnRepeatCount = header.repeatCount;
  lastLearnRepeatGapMs = header.repeatGapMs;
  lastLearnMetadataValid = true;
  txFrequencyHz = header.carrierHz;
  txRepeatCount = header.repeatCount;
  txRepeatGapMs = header.repeatGapMs;

  DBG_PRINT(F("Command load verified: ID=")); DBG_PRINT(commandId);
  DBG_PRINT(F(" raw=")); DBG_PRINT(lastRawLen);
  DBG_PRINT(F(" crc=0x")); DBG_PRINTLN(lastRawCrc32, HEX);
  storage.currentCommandId = commandId;
  strncpy(storage.currentCommandName, commandName, STORAGE_MAX_NAME);
  storage.currentCommandName[STORAGE_MAX_NAME] = '\0';
  storage.currentDeviceId = header.deviceId;
  strncpy(storage.currentDeviceName, deviceName, STORAGE_MAX_NAME);
  storage.currentDeviceName[STORAGE_MAX_NAME] = '\0';
  return storedSignalIntegrityOK();
}

bool storageLoadCommandFromSector(uint32_t sector, uint32_t commandId) {
  return storageLoadCommandFromSectorInternal(sector, commandId, true);
}
bool storageLoadCommandFromSectorIndexedOnly(uint32_t sector, uint32_t commandId) {
  return storageLoadCommandFromSectorInternal(sector, commandId, false);
}

bool storageLoadCommand(uint32_t commandId) {
  if (!storage.initialized || commandId == 0 ||
      storageIsCommandIdentityConflicted(commandId) ||
      storageIsCommandDeleted(commandId)) return false;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    uint32_t bestSector = UINT32_MAX;
    uint32_t bestSequence = 0;
    for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
      feedWatchdog();
      if (storageIsSectorQuarantined(sector)) continue;
      StorageRecordHeader header{};
      if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_COMMAND ||
          header.recordId != commandId || !storageHasIndexedValidDeviceId(header.deviceId)) continue;
      const uint32_t rawBytes = static_cast<uint32_t>(header.rawLength) * sizeof(uint16_t);
      if (header.nameLength == 0 || rawBytes == 0 ||
          header.nameLength + rawBytes != header.payloadLength ||
          rawBytes > sizeof(lastRaw)) continue;
      if (bestSector == UINT32_MAX || header.sequence > bestSequence) {
        bestSector = sector; bestSequence = header.sequence;
      }
    }
    if (bestSector != UINT32_MAX && storageLoadCommandFromSectorIndexedOnly(bestSector, commandId)) return true;
    if (pass == 0 && storageRebuildDeviceIndex()) continue;
    return false;
  }
  return false;
}

bool storageLoadLatestCommand() {
  if (!storage.initialized) return false;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    StorageRecordHeader latestHeader{};
    uint32_t upperSequence = UINT32_MAX;
    if (storage.latestCommandSector < storage.sectorCount &&
        !storageIsSectorQuarantined(storage.latestCommandSector) &&
        storage.latestCommandId != 0 &&
        !storageIsCommandIdentityConflicted(storage.latestCommandId) &&
        !storageIsCommandDeleted(storage.latestCommandId) &&
        storageReadValidatedRecord(storage.latestCommandSector, latestHeader) &&
        latestHeader.type == RECORD_TYPE_COMMAND &&
        latestHeader.recordId == storage.latestCommandId &&
        latestHeader.sequence != 0) {
      upperSequence = latestHeader.sequence;
      if (storageHasIndexedValidDeviceId(latestHeader.deviceId) &&
          storageLoadCommandFromSectorIndexedOnly(storage.latestCommandSector, storage.latestCommandId)) return true;
    }
    if (pass == 0) {
      if (!storageRebuildDeviceIndex()) return false;
      continue;
    }
    uint32_t bestSector = UINT32_MAX;
    uint32_t bestSequence = 0;
    uint32_t bestCommandId = 0;
    for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
      feedWatchdog();
      if (storageIsSectorQuarantined(sector)) continue;
      StorageRecordHeader header{};
      if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_COMMAND ||
          header.sequence >= upperSequence ||
          storageIsCommandIdentityConflicted(header.recordId) ||
          storageIsCommandDeleted(header.recordId) ||
          storageIsDeviceDeleted(header.deviceId) ||
          !storageHasIndexedValidDeviceId(header.deviceId)) continue;
      const uint32_t rawBytes = static_cast<uint32_t>(header.rawLength) * sizeof(uint16_t);
      if (header.nameLength == 0 || rawBytes == 0 ||
          header.nameLength + rawBytes != header.payloadLength ||
          rawBytes > sizeof(lastRaw)) continue;
      char deviceName[STORAGE_MAX_NAME + 1] = {0};
      if (!storageFindDeviceNameIndexedOnly(header.deviceId, deviceName, sizeof(deviceName))) continue;
      if (bestSector == UINT32_MAX || header.sequence > bestSequence) {
        bestSector = sector; bestSequence = header.sequence; bestCommandId = header.recordId;
      }
    }
    if (bestSector != UINT32_MAX &&
        storageLoadCommandFromSectorIndexedOnly(bestSector, bestCommandId)) {
      storage.latestCommandSector = bestSector;
      storage.latestCommandId = bestCommandId;
      return true;
    }
    return false;
  }
  return false;
}

// ============================================================
// CONFIG BACKUP TO FLASH
// ============================================================
// The payload is a byte-for-byte copy of the whole EEPROM image
// (CONFIG_EEPROM_SIZE bytes). Integrity is checked by the per-slot
// CRCs already stored inside the EEPROM plus the storage layer's
// own record CRC and commit marker.
//
// Repeated backups erase prior backups first so nothing accumulates.

static bool storageSectorIsConfigBackup(uint32_t sector) {
  if (storageIsSectorQuarantined(sector)) return false;
  StorageRecordHeader h{};
  if (!storageReadRecordHeaderStable(sector, h)) return false;
  if (storageIsRecordHeaderBlank(h)) return false;
  if (h.magic != RECORD_MAGIC || h.formatVersion != RECORD_FORMAT_VERSION) return false;
  if (h.generation != storage.generation) return false;
  return h.type == RECORD_TYPE_CONFIG_BACKUP;
}

bool storageWriteConfigBackup() {
  if (!storage.initialized) return false;
  if (irState != IrEngineState::IDLE) return false;

  // 1. Erase prior backups.
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (!storageSectorIsConfigBackup(sector)) continue;
    if (!flashEraseSectorVerified(sector)) continue;
    if (storage.usedSectorCount > 0) storage.usedSectorCount--;
    storage.freeSectorCount++;
    storage.firstFreeSector = sector;
    storage.freeSpaceIndexValid = true;
  }

  // 2. Snapshot EEPROM into storageRecordBuffer (already allocated).
  uint8_t *buf = storageRecordBuffer;
  for (size_t i = 0; i < STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES; ++i) {
    buf[i] = EEPROM.read(i);
    if ((i & 0xFFU) == 0xFFU) ESP.wdtFeed();
  }

  // 3. Append as a CONFIG_BACKUP record.
  uint32_t seq = 0;
  const bool ok = storageAppendRawRecord(RECORD_TYPE_CONFIG_BACKUP,
                                         1U, buf,
                                         static_cast<uint16_t>(STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES),
                                         seq);
  if (!ok) return false;
  storage.maxSequence = seq;
  return true;
}

bool storageReadConfigBackup(uint8_t *out, size_t capacity) {
  if (!storage.initialized || out == nullptr || capacity < STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES) return false;

  uint32_t bestSector = UINT32_MAX;
  uint32_t bestSequence = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (!storageSectorIsConfigBackup(sector)) continue;
    StorageRecordHeader h{};
    if (!storageReadValidatedRecord(sector, h)) continue;
    if (h.payloadLength != STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES) continue;
    if (bestSector == UINT32_MAX || h.sequence > bestSequence) {
      bestSector = sector;
      bestSequence = h.sequence;
    }
  }
  if (bestSector == UINT32_MAX) return false;

  StorageRecordHeader h{};
  if (!storageReadValidatedRecord(bestSector, h)) return false;
  if (h.payloadLength != STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES) return false;
  memcpy(out, storageRecordBuffer, STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES);
  return true;
}

void storagePrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("W25Qxx STORAGE STATUS"));
  Serial.println(F("========================================"));
  Serial.print(F("Detected       : ")); Serial.println(storage.detected ? F("YES") : F("NO"));
  if (storage.detected) {
    Serial.print(F("JEDEC ID       : "));
    if (storage.manufacturer < 0x10) Serial.print('0');
    Serial.print(storage.manufacturer, HEX); Serial.print(' ');
    if (storage.memoryType < 0x10) Serial.print('0');
    Serial.print(storage.memoryType, HEX); Serial.print(' ');
    if (storage.capacityCode < 0x10) Serial.print('0');
    Serial.println(storage.capacityCode, HEX);
    Serial.print(F("Capacity       : ")); Serial.print(storage.flashSizeBytes / 1024UL / 1024UL); Serial.println(F(" MiB"));
    Serial.print(F("Sector count   : ")); Serial.println(storage.sectorCount);
    Serial.print(F("DB generation  : ")); Serial.println(storage.generation);
    Serial.print(F("DB initialized : ")); Serial.println(storage.initialized ? F("YES") : F("NO"));
    Serial.print(F("Header corrupt : ")); Serial.println(storage.headerCorrupt ? F("YES") : F("NO"));
    Serial.print(F("Valid records  : ")); Serial.println(storage.validRecordCount);
    Serial.print(F("Corrupt records: ")); Serial.println(storage.corruptRecordCount);
    Serial.print(F("Obsolete       : ")); Serial.println(storage.obsoleteRecordCount);
    Serial.print(F("Used sectors   : ")); Serial.println(storage.usedSectorCount);
    Serial.print(F("Free sectors   : ")); Serial.println(storage.freeSectorCount);
    if (storage.firstFreeSector != UINT32_MAX) { Serial.print(F("First free sec : ")); Serial.println(storage.firstFreeSector); }
    else Serial.println(F("First free sec : NONE"));
    Serial.print(F("Max device ID  : ")); Serial.println(storage.maxDeviceId);
    Serial.print(F("Max command ID : ")); Serial.println(storage.maxCommandId);
    Serial.print(F("Latest command : ")); Serial.println(storage.latestCommandId);
    Serial.print(F("Current device : "));
    if (storage.currentDeviceId != 0) {
      Serial.print(storage.currentDeviceId); Serial.print(F(" ("));
      Serial.print(storage.currentDeviceName); Serial.println(')');
    } else Serial.println(F("NONE"));
    Serial.print(F("Current command: "));
    if (storage.currentCommandId != 0) {
      Serial.print(storage.currentCommandId); Serial.print(F(" ("));
      Serial.print(storage.currentCommandName); Serial.println(')');
    } else Serial.println(F("NONE"));
  }
  Serial.println(F("========================================"));
  Serial.println();
#endif
}

void storageListDevices() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  if (!storage.initialized) { Serial.println(F("Storage database is not initialized.")); return; }
  Serial.println();
  Serial.println(F("=== DEVICES ==="));
  uint32_t count = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_DEVICE) continue;
    char name[STORAGE_MAX_NAME + 1] = {0};
    if (!storageGetRecordName(sector, header, name, sizeof(name))) continue;
    Serial.print(F("ID=")); Serial.print(header.recordId); Serial.print(F("  ")); Serial.println(name);
    count++;
  }
  if (count == 0) Serial.println(F("(none)"));
  Serial.println();
#endif
}

void storageListCommands() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  if (!storage.initialized) { Serial.println(F("Storage database is not initialized.")); return; }
  Serial.println();
  Serial.println(F("=== COMMANDS ==="));
  uint32_t count = 0;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_COMMAND) continue;
    char name[STORAGE_MAX_NAME + 1] = {0};
    if (!storageGetRecordName(sector, header, name, sizeof(name))) continue;
    Serial.print(F("ID=")); Serial.print(header.recordId);
    Serial.print(F("  Device=")); Serial.print(header.deviceId);
    Serial.print(F("  ")); Serial.print(name);
    Serial.print(F("  Raw=")); Serial.print(header.rawLength);
    Serial.print(F("  ")); Serial.print(header.carrierHz);
    Serial.print(F("Hz  R=")); Serial.print(header.repeatCount);
    Serial.print(F("  G=")); Serial.print(header.repeatGapMs);    Serial.println(F("ms"));
    count++;
  }
  if (count == 0) Serial.println(F("(none)"));
  Serial.println();
#endif
}

void initializeStorageEngine() {
  storage = StorageState{};
  if (!storageDetectFlash()) {
    DBG_PRINTLN(F("W25Qxx detection: FAIL - continuing in RAM-only IR mode.")); return;
  }
#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    Serial.print(F("W25Qxx JEDEC: "));
    if (storage.manufacturer < 0x10) Serial.print('0');
    Serial.print(storage.manufacturer, HEX); Serial.print(' ');
    if (storage.memoryType < 0x10) Serial.print('0');
    Serial.print(storage.memoryType, HEX); Serial.print(' ');
    if (storage.capacityCode < 0x10) Serial.print('0');
    Serial.println(storage.capacityCode, HEX);
    Serial.print(F("W25Qxx capacity: ")); Serial.print(storage.flashSizeBytes / 1024UL / 1024UL);
    Serial.println(F(" MiB"));
  }
#endif
  if (!storageInitializeDatabase()) {
    storage.headerCorrupt = true;
    DBG_PRINTLN(F("ERROR: database header is invalid/corrupt."));
    DBG_PRINTLN(F("Use DELETE ALL / RESETDB for targeted logical reset, or WIPEDB for destructive recovery."));
    return;
  }
  if (!storageScanDatabase()) {
    DBG_PRINTLN(F("ERROR: database scan failed.")); storage.initialized = false; return;
  }
  if (!configurationInitializeCommandHue(storage.generation)) {
    DBG_PRINTLN(F("WARNING: per-command Hue/Alexa settings storage is unavailable; command exposure defaults to OFF."));
  }
  if (!configurationInitializeDeviceCloud(storage.generation)) {
    DBG_PRINTLN(F("WARNING: device category / Alexa Cloud settings storage is unavailable; devices default to not exposed."));
  }  
  if (storage.latestCommandId != 0) {
    if (storageLoadLatestCommand()) {
      DBG_PRINT(F("Loaded latest stored command ID=")); DBG_PRINTLN(storage.latestCommandId);
    } else DBG_PRINTLN(F("WARNING: latest stored command could not be loaded."));
  }
  updateMinimumHeap();
}

// ============================================================
// AUTOMATION RULES
// ============================================================

constexpr uint8_t  AUTOMATION_MAX_RULES        = 10U;
constexpr uint8_t  AUTOMATION_MAX_JOBS         = 4U;
constexpr uint32_t AUTOMATION_EVAL_INTERVAL_MS = 1000UL;
constexpr uint32_t AUTOMATION_STEP_GAP_MS      = 100UL;   // min gap after an IR step

constexpr uint8_t RULE_FLAG_ENABLED = 0x01U;

constexpr uint8_t TRIG_FLAG_USE_TEMP     = 0x01U;
constexpr uint8_t TRIG_FLAG_USE_TIME     = 0x02U;
constexpr uint8_t TRIG_FLAG_USE_INTERVAL = 0x04U;

constexpr uint8_t RULE_OP_NONE = 0U;
constexpr uint8_t RULE_OP_GT   = 1U;
constexpr uint8_t RULE_OP_GE   = 2U;
constexpr uint8_t RULE_OP_LT   = 3U;
constexpr uint8_t RULE_OP_LE   = 4U;
constexpr uint8_t RULE_OP_EQ   = 5U;

constexpr uint8_t RULE_LOGIC_AND = 0U;
constexpr uint8_t RULE_LOGIC_OR  = 1U;

constexpr uint8_t RULE_MAX_TRIGGERS = 6U;
constexpr uint8_t RULE_MAX_STEPS    = 8U;

constexpr uint8_t STEP_NONE = 0U;
constexpr uint8_t STEP_TX   = 1U;   // value = command id
constexpr uint8_t STEP_WAIT = 2U;   // value = seconds

#pragma pack(push, 1)
struct RuleStep {
  uint8_t  type;
  uint8_t  reserved;
  uint16_t value;
};
struct RuleTriggerStored {
  uint8_t  flags;            // TRIG_FLAG_*
  uint8_t  tempOp;
  uint8_t  weekdayMask;      // bit0=Mon .. bit6=Sun, 0 = every day (used with TIME)
  uint8_t  logic;            // AND/OR when TEMP+TIME both set
  int16_t  tempValueCenti;
  uint16_t tempDurationSec;
  uint16_t timeStartMin;
  uint16_t timeEndMin;
  uint16_t intervalSec;
  uint8_t  stepCount;
  uint8_t  reserved;
  RuleStep steps[RULE_MAX_STEPS];
};
struct StoredRule {
  uint8_t           triggerCount;
  uint8_t           ruleFlags;
  uint16_t          cooldownSec;
  RuleTriggerStored triggers[RULE_MAX_TRIGGERS];
};

// Legacy v1 layout (read-only, converted on load)
struct LegacyRuleBlock {
  uint32_t commandId;
  int16_t  tempValueCenti;
  uint16_t tempDurationSec;
  uint16_t timeStartMin;
  uint16_t timeEndMin;
  uint16_t intervalSec;
  uint16_t delaySec;
  uint8_t  tempOp;
  uint8_t  weekdayMask;
  uint8_t  logic;
  uint8_t  flags;
};
struct LegacyStoredRule {
  uint8_t         blockCount;
  uint8_t         ruleFlags;
  uint16_t        cooldownSec;
  LegacyRuleBlock blocks[4];
};
#pragma pack(pop)
static_assert(sizeof(RuleStep) == 4, "RuleStep must be 4 bytes");
static_assert(sizeof(RuleTriggerStored) == 48, "RuleTriggerStored must be 48 bytes");
static_assert(sizeof(StoredRule) == RULE_PAYLOAD_BYTES, "StoredRule must match RULE_PAYLOAD_BYTES");
static_assert(sizeof(LegacyStoredRule) == RULE_PAYLOAD_BYTES_V1, "LegacyStoredRule must match V1 size");

struct RuleTriggerRuntime {
  uint32_t holdSinceMs;      // temperature condition true since
  uint32_t nextIntervalMs;
  uint8_t  holding;
  uint8_t  latched;          // already fired for this activation
  uint8_t  armed;            // interval timer armed
  uint8_t  reserved;
};

struct RuleSlot {
  StoredRule         data;
  uint32_t           lastRuleFireMs;
  uint8_t            everFired;
  RuleTriggerRuntime trig[RULE_MAX_TRIGGERS];
};

constexpr uint8_t AUTOMATION_MAX_SCHEDULES = 10U;
constexpr uint8_t SCHEDULE_FLAG_ENABLED = 0x01U;

#pragma pack(push, 1)
struct StoredSchedule {
  uint32_t commandId;
  uint32_t delaySec;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  weekdayMask;
  uint8_t  flags;
};
#pragma pack(pop)
static_assert(sizeof(StoredSchedule) == SCHEDULE_PAYLOAD_BYTES,
              "StoredSchedule size must match SCHEDULE_PAYLOAD_BYTES");

struct AutomationScheduleState {
  StoredSchedule data;
  uint32_t       id;
  uint32_t       lastFiredDayKey;
  uint32_t       lastFiredMinuteOfDay;
  bool           used;
};

struct AutomationJob {
  bool     active;
  uint8_t  ruleIdx;     // 0xFF = schedule / one-shot
  uint8_t  pos;
  uint8_t  count;
  uint32_t dueMs;
  RuleStep steps[RULE_MAX_STEPS];
};

static RuleSlot                automationRules[AUTOMATION_MAX_RULES];
static AutomationScheduleState automationSchedules[AUTOMATION_MAX_SCHEDULES];
static AutomationJob automationJobs[AUTOMATION_MAX_JOBS];
static uint32_t                automationLastEvalMs        = 0UL;
static uint32_t                automationLastGeneration    = 0UL;
static uint32_t                automationFireCount         = 0UL;
static uint32_t                automationScheduleFireCount = 0UL;
static uint32_t                automationActionFailures    = 0UL;

bool automationInitialize();
static void automationQueueAction(uint32_t commandId, uint32_t delayMs);
static void automationResetJobs();
void automationService();
bool automationSetRuleEnabled(uint32_t ruleId, bool enabled);
bool automationDeleteRule(uint32_t ruleId);

static bool automationValidateRule(const StoredRule &rule) {
  if (rule.triggerCount == 0U || rule.triggerCount > RULE_MAX_TRIGGERS) return false;
  if (rule.cooldownSec > 3600U) return false;
  for (uint8_t i = 0U; i < rule.triggerCount; ++i) {
    const RuleTriggerStored &g = rule.triggers[i];
    const bool useTemp     = (g.flags & TRIG_FLAG_USE_TEMP)     != 0U;
    const bool useTime     = (g.flags & TRIG_FLAG_USE_TIME)     != 0U;
    const bool useInterval = (g.flags & TRIG_FLAG_USE_INTERVAL) != 0U;
    if (!useTemp && !useTime && !useInterval) return false;
    if (useInterval && (useTemp || useTime)) return false;
    if (useInterval && g.intervalSec == 0U) return false;
    if (useTemp && (g.tempOp < RULE_OP_GT || g.tempOp > RULE_OP_EQ)) return false;
    if (useTime && (g.timeStartMin > 1439U || g.timeEndMin > 1439U)) return false;
    if (g.logic > RULE_LOGIC_OR) return false;
    if (g.weekdayMask > 0x7FU) return false;
    if (g.stepCount == 0U || g.stepCount > RULE_MAX_STEPS) return false;
    bool hasTx = false;
    for (uint8_t s = 0U; s < g.stepCount; ++s) {
      const RuleStep &st = g.steps[s];
      if (st.type == STEP_TX)        { if (st.value == 0U) return false; hasTx = true; }
      else if (st.type == STEP_WAIT) { if (st.value == 0U) return false; }
      else return false;
    }
    if (!hasTx) return false;
  }
  return true;
}

static void automationConvertLegacy(const LegacyStoredRule &old, StoredRule &out) {
  memset(&out, 0, sizeof(out));
  const uint8_t n = (old.blockCount > 4U) ? 4U : old.blockCount;
  out.triggerCount = n;
  out.ruleFlags    = old.ruleFlags;
  out.cooldownSec  = old.cooldownSec;
  for (uint8_t i = 0U; i < n; ++i) {
    const LegacyRuleBlock &b = old.blocks[i];
    RuleTriggerStored &t = out.triggers[i];
    t.flags           = static_cast<uint8_t>(b.flags & 0x07U);
    t.tempOp          = b.tempOp;
    t.weekdayMask     = b.weekdayMask;
    t.logic           = b.logic;
    t.tempValueCenti  = b.tempValueCenti;
    t.tempDurationSec = b.tempDurationSec;
    t.timeStartMin    = b.timeStartMin;
    t.timeEndMin      = b.timeEndMin;
    t.intervalSec     = b.intervalSec;
    uint8_t k = 0U;
    if (b.delaySec > 0U) { t.steps[k].type = STEP_WAIT; t.steps[k].value = b.delaySec; ++k; }
    t.steps[k].type  = STEP_TX;
    t.steps[k].value = static_cast<uint16_t>(b.commandId);
    ++k;
    t.stepCount = k;
  }
}

static bool storageAppendRawRecord(uint8_t recordType, uint32_t recordId,
                                   const uint8_t *payload, uint16_t payloadLength,
                                   uint32_t &sequenceOut) {
  if (!storage.initialized) return false;
  if (payloadLength == 0U && payload != nullptr) return false;
  if (payloadLength > 0U && payload == nullptr) return false;
  if (payloadLength > (STORAGE_FLASH_SECTOR_SIZE - 4UL - sizeof(StorageRecordHeader))) return false;
  if ((payloadLength & 1U) != 0U) return false;
  if (recordId == 0U || recordId > storageMaxPhysicalIdentity()) return false;
  if (!storageEnsureFreeSpaceIndex()) return false;

  uint32_t targetSector = UINT32_MAX;
  if (!storageFindVerifiedFreeSector(storage.firstFreeSector, targetSector)) return false;

  uint32_t nextSequence = 0;
  if (!storageNextSequence(nextSequence)) return false;

  memset(storageRecordBuffer, 0xFF, sizeof(storageRecordBuffer));
  if (payloadLength > 0U) {
    memcpy(storageRecordBuffer + sizeof(StorageRecordHeader), payload, payloadLength);
  }

  StorageRecordHeader header{};
  header.magic         = RECORD_MAGIC;
  header.formatVersion = RECORD_FORMAT_VERSION;
  header.type          = recordType;
  header.flags         = RECORD_FLAG_COMMITTED;
  header.generation    = storage.generation;
  header.sequence      = nextSequence;
  header.recordId      = recordId;
  header.deviceId      = 0;
  header.nameLength    = 0;
  header.rawLength     = static_cast<uint16_t>(payloadLength / 2U);
  header.carrierHz     = 0;
  header.repeatCount   = 0;
  header.reserved0     = 0;
  header.repeatGapMs   = 0;
  header.payloadLength = payloadLength;
  header.payloadCrc32  = crc32Bytes(
      storageRecordBuffer + sizeof(StorageRecordHeader), payloadLength);
  StorageRecordHeader headerForCrc = header;
  headerForCrc.headerCrc32 = 0;
  header.headerCrc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&headerForCrc),
                                  sizeof(headerForCrc));
  memcpy(storageRecordBuffer, &header, sizeof(header));

  const uint32_t base       = targetSector * STORAGE_FLASH_SECTOR_SIZE;
  const uint16_t bodyLength = static_cast<uint16_t>(sizeof(StorageRecordHeader) + payloadLength);

  if (!storageEnsureSectorBlank(targetSector)) {
    storageQuarantineAppendSector(targetSector, header);
    return false;
  }
  if (!flashProgramBytes(base, storageRecordBuffer, bodyLength)) {
    if (!storageRecoverFailedAppendSector(targetSector)) {
      storageQuarantineAppendSector(targetSector, header);
    }
    return false;
  }
  StorageRecordHeader verifyHeader{};
  if (!storageReadRecordHeaderStable(targetSector, verifyHeader) ||
      !storageValidateRecordHeaderForBody(&verifyHeader, storage.generation) ||
      !storageReadPayloadVerifiedIntoBuffer(targetSector, verifyHeader)) {
    if (!storageRecoverFailedAppendSector(targetSector)) {
      storageQuarantineAppendSector(targetSector, header);
    }
    return false;
  }
  if (!storageCommitRecord(targetSector)) {
    uint32_t observedCommit = 0;
    if (storageReadCommitStable(targetSector, observedCommit) && observedCommit == COMMIT_MAGIC) {
      /* fall through as success */
    } else {
      if (storageReadCommitStable(targetSector, observedCommit) && observedCommit != COMMIT_MAGIC) {
        if (!storageRecoverFailedAppendSector(targetSector)) {
          storageQuarantineAppendSector(targetSector, header);
        }
      } else {
        storageQuarantineAppendSector(targetSector, header);
      }
      return false;
    }
  }
  sequenceOut = nextSequence;
  storage.maxSequence = nextSequence;
  (void)storageUpdateFirstFreeSectorAfterAppend(targetSector);
  storageInvalidateOrderCache(); 
  return true;
}

static bool storageAppendRule(uint32_t ruleId, const StoredRule &rule) {
  if (ruleId < 1U || ruleId > AUTOMATION_MAX_RULES) return false;
  if (!automationValidateRule(rule)) return false;
  uint32_t seq = 0;
  return storageAppendRawRecord(RECORD_TYPE_RULE, ruleId,
                                reinterpret_cast<const uint8_t *>(&rule),
                                static_cast<uint16_t>(sizeof(rule)), seq);
}

static bool storageAppendRuleDeleteMarker(uint32_t ruleId) {
  if (ruleId < 1U || ruleId > AUTOMATION_MAX_RULES) return false;
  uint32_t seq = 0;
  return storageAppendRawRecord(RECORD_TYPE_DELETE_RULE, ruleId, nullptr, 0, seq);
}

static bool automationValidateSchedule(const StoredSchedule &schedule) {
  if (schedule.commandId == 0U) return false;
  if (schedule.hour > 23U || schedule.minute > 59U) return false;
  if (schedule.weekdayMask > 0x7FU) return false;
  return true;
}

static bool storageAppendSchedule(uint32_t scheduleId,
                                  const StoredSchedule &schedule) {
  if (scheduleId < 1U || scheduleId > AUTOMATION_MAX_SCHEDULES) return false;
  if (!automationValidateSchedule(schedule)) return false;

  uint32_t seq = 0;
  return storageAppendRawRecord(
      RECORD_TYPE_SCHEDULE, scheduleId,
      reinterpret_cast<const uint8_t *>(&schedule),
      static_cast<uint16_t>(sizeof(schedule)), seq);
}

static bool storageAppendScheduleDeleteMarker(uint32_t scheduleId) {
  if (scheduleId < 1U || scheduleId > AUTOMATION_MAX_SCHEDULES) return false;

  uint32_t seq = 0;
  return storageAppendRawRecord(
      RECORD_TYPE_DELETE_SCHEDULE, scheduleId, nullptr, 0U, seq);
}

static void automationLoadRules() {
  for (uint8_t i = 0; i < AUTOMATION_MAX_RULES; ++i) {
    memset(&automationRules[i], 0, sizeof(automationRules[i]));
  }
  automationResetJobs();
  automationLastEvalMs     = 0UL;
  automationFireCount      = 0UL;
  automationActionFailures = 0UL;

  if (!storage.initialized) return;

  uint32_t bestRuleSeq[AUTOMATION_MAX_RULES]   = {0};
  uint32_t bestDeleteSeq[AUTOMATION_MAX_RULES] = {0};
  static StoredRule loaded;

  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) continue;
    if (storageIsRecordHeaderBlank(header)) continue;
    if (header.generation != storage.generation) continue;
    if (header.recordId < 1U || header.recordId > AUTOMATION_MAX_RULES) continue;

    if (header.type == RECORD_TYPE_RULE) {
      if (!storageValidateRecordHeader(header)) continue;
      uint32_t commit = 0;
      if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
      const uint8_t idx = static_cast<uint8_t>(header.recordId - 1U);
      if (header.sequence <= bestRuleSeq[idx]) continue;
      if (!storageReadPayloadVerifiedIntoBuffer(sector, header)) continue;

      if (header.payloadLength == RULE_PAYLOAD_BYTES) {
        memcpy(&loaded, storageRecordBuffer, sizeof(loaded));
      } else if (header.payloadLength == RULE_PAYLOAD_BYTES_V1) {
        LegacyStoredRule old;
        memcpy(&old, storageRecordBuffer, sizeof(old));
        automationConvertLegacy(old, loaded);
      } else continue;
      if (!automationValidateRule(loaded)) continue;

      memset(&automationRules[idx], 0, sizeof(automationRules[idx]));
      automationRules[idx].data = loaded;
      bestRuleSeq[idx] = header.sequence;
    } else if (header.type == RECORD_TYPE_DELETE_RULE) {
      if (!storageValidateRecordHeader(header)) continue;
      uint32_t commit = 0;
      if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
      const uint8_t idx = static_cast<uint8_t>(header.recordId - 1U);
      if (header.sequence > bestDeleteSeq[idx]) bestDeleteSeq[idx] = header.sequence;
    }
  }

  for (uint8_t i = 0; i < AUTOMATION_MAX_RULES; ++i) {
    if (bestDeleteSeq[i] > bestRuleSeq[i]) {
      memset(&automationRules[i], 0, sizeof(automationRules[i]));
    }
  }
}

static void automationLoadSchedules() {
  for (uint8_t i = 0; i < AUTOMATION_MAX_SCHEDULES; ++i) {
    memset(&automationSchedules[i], 0, sizeof(automationSchedules[i]));
    automationSchedules[i].lastFiredMinuteOfDay = 0xFFFFFFFFUL;
  }

  if (!storage.initialized) return;

  uint32_t bestScheduleSeq[AUTOMATION_MAX_SCHEDULES] = {0};
  uint32_t bestDeleteSeq[AUTOMATION_MAX_SCHEDULES] = {0};

  for (uint32_t sector = DB_DATA_START_SECTOR;
       sector < storage.sectorCount; ++sector) {
    feedWatchdog();

    if (storageIsSectorQuarantined(sector)) continue;

    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) continue;
    if (storageIsRecordHeaderBlank(header)) continue;
    if (header.generation != storage.generation) continue;
    if (header.recordId < 1U ||
        header.recordId > AUTOMATION_MAX_SCHEDULES) continue;

    if (header.type == RECORD_TYPE_SCHEDULE) {
      if (!storageValidateRecordHeader(header)) continue;

      uint32_t commit = 0;
      if (!storageReadCommitStable(sector, commit) ||
          commit != COMMIT_MAGIC) continue;

      const uint8_t idx =
          static_cast<uint8_t>(header.recordId - 1U);
      if (header.sequence <= bestScheduleSeq[idx]) continue;

      if (!storageReadPayloadVerifiedIntoBuffer(sector, header)) continue;

      memcpy(&automationSchedules[idx].data,
             storageRecordBuffer,
             sizeof(StoredSchedule));
      automationSchedules[idx].id = header.recordId;
      automationSchedules[idx].used = true;
      automationSchedules[idx].lastFiredDayKey = 0;
      automationSchedules[idx].lastFiredMinuteOfDay = 0xFFFFFFFFUL;
      bestScheduleSeq[idx] = header.sequence;
    } else if (header.type == RECORD_TYPE_DELETE_SCHEDULE) {
      if (!storageValidateRecordHeader(header)) continue;

      uint32_t commit = 0;
      if (!storageReadCommitStable(sector, commit) ||
          commit != COMMIT_MAGIC) continue;

      const uint8_t idx =
          static_cast<uint8_t>(header.recordId - 1U);
      if (header.sequence > bestDeleteSeq[idx])
        bestDeleteSeq[idx] = header.sequence;
    }
  }

  for (uint8_t i = 0; i < AUTOMATION_MAX_SCHEDULES; ++i) {
    if (bestDeleteSeq[i] > bestScheduleSeq[i]) {
      memset(&automationSchedules[i], 0,
             sizeof(automationSchedules[i]));
      automationSchedules[i].lastFiredMinuteOfDay = 0xFFFFFFFFUL;
    }
  }
}

static void automationEvaluateSchedule(AutomationScheduleState &slot) {
  if (!slot.used ||
      (slot.data.flags & SCHEDULE_FLAG_ENABLED) == 0U) {
    return;
  }

  struct tm t{};
  if (!timeGetLocal(t)) return;

  const int bit = (t.tm_wday == 0) ? 6 : (t.tm_wday - 1);
  if (slot.data.weekdayMask != 0U &&
      ((slot.data.weekdayMask >> bit) & 1U) == 0U) {
    return;
  }

  if (t.tm_hour != slot.data.hour ||
      t.tm_min != slot.data.minute) {
    return;
  }

  const uint32_t dayKey =
      static_cast<uint32_t>(t.tm_year) * 366UL +
      static_cast<uint32_t>(t.tm_yday);
  const uint32_t minuteOfDay =
      static_cast<uint32_t>(t.tm_hour * 60 + t.tm_min);

  if (slot.lastFiredDayKey == dayKey &&
      slot.lastFiredMinuteOfDay == minuteOfDay) {
    return;
  }

  slot.lastFiredDayKey = dayKey;
  slot.lastFiredMinuteOfDay = minuteOfDay;

  automationQueueAction(
      slot.data.commandId,
      static_cast<uint32_t>(slot.data.delaySec) * 1000UL);

  automationScheduleFireCount++;
}

static bool automationTempCondition(uint8_t op, int32_t thresholdCenti) {
  if (!ds18b20IsEnabled() || !ds18b20HasValidReading()) return false;
  const int32_t t = ds18b20GetCalibratedCentiDegrees();
  switch (op) {
    case RULE_OP_GT: return t >  thresholdCenti;
    case RULE_OP_GE: return t >= thresholdCenti;
    case RULE_OP_LT: return t <  thresholdCenti;
    case RULE_OP_LE: return t <= thresholdCenti;
    case RULE_OP_EQ: {
      const int32_t d = t - thresholdCenti;
      return d >= -25 && d <= 25;
    }
    default: return false;
  }
}

static bool automationTimeCondition(uint16_t startMin, uint16_t endMin,
                                    uint8_t weekdayMask, const struct tm &t) {
  const uint16_t cur = static_cast<uint16_t>(t.tm_hour * 60 + t.tm_min);
  int wday = t.tm_wday;                       // 0 = Sunday
  bool inWindow;
  if (startMin <= endMin) {
    inWindow = (cur >= startMin && cur <= endMin);
  } else if (cur >= startMin) {
    inWindow = true;
  } else if (cur <= endMin) {
    inWindow = true;
    wday = (wday + 6) % 7;                    // after midnight: window began yesterday
  } else {
    inWindow = false;
  }
  if (!inWindow) return false;
  if (weekdayMask != 0U) {
    const int bit = (wday == 0) ? 6 : (wday - 1);
    if (((weekdayMask >> bit) & 1U) == 0U) return false;
  }
  return true;
}

static void automationResetJobs() {
  memset(automationJobs, 0, sizeof(automationJobs));
}

static void automationCancelRuleJobs(uint8_t ruleIdx) {
  for (uint8_t i = 0; i < AUTOMATION_MAX_JOBS; ++i) {
    if (automationJobs[i].active && automationJobs[i].ruleIdx == ruleIdx) automationJobs[i].active = false;
  }
}

static bool automationRuleBusy(uint8_t ruleIdx) {
  for (uint8_t i = 0; i < AUTOMATION_MAX_JOBS; ++i) {
    if (automationJobs[i].active && automationJobs[i].ruleIdx == ruleIdx) return true;
  }
  return false;
}

static bool automationStartSequence(uint8_t ruleIdx, const RuleStep *steps,
                                    uint8_t count, uint32_t delayMs) {
  if (steps == nullptr || count == 0U || count > RULE_MAX_STEPS) return false;
  for (uint8_t i = 0; i < AUTOMATION_MAX_JOBS; ++i) {
    AutomationJob &j = automationJobs[i];
    if (j.active) continue;
    memset(&j, 0, sizeof(j));
    j.active  = true;
    j.ruleIdx = ruleIdx;
    j.count   = count;
    j.pos     = 0U;
    j.dueMs   = millis() + delayMs;
    memcpy(j.steps, steps, static_cast<size_t>(count) * sizeof(RuleStep));
    return true;
  }
  return false;
}

// Used by schedules: single transmit after an optional delay.
static void automationQueueAction(uint32_t commandId, uint32_t delayMs) {
  if (commandId == 0U || commandId > 0xFFFFU) return;
  RuleStep s{};
  s.type  = STEP_TX;
  s.value = static_cast<uint16_t>(commandId);
  if (!automationStartSequence(0xFFU, &s, 1U, delayMs)) {
    automationActionFailures++;
    Serial.println(F("WARN: automation job queue full; action dropped."));
  }
}

static void automationExecutePending() {
  if (irState != IrEngineState::IDLE) return;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < AUTOMATION_MAX_JOBS; ++i) {
    AutomationJob &j = automationJobs[i];
    if (!j.active) continue;
    if (static_cast<int32_t>(now - j.dueMs) < 0) continue;
    if (j.pos >= j.count) { j.active = false; continue; }

    const RuleStep st = j.steps[j.pos++];
    if (st.type == STEP_WAIT) {
      j.dueMs = now + static_cast<uint32_t>(st.value) * 1000UL;
      continue;                                   // waits cost nothing; keep scanning
    }
    if (st.type == STEP_TX) {
      j.dueMs = now + AUTOMATION_STEP_GAP_MS;
      const uint32_t cid = st.value;
      uint32_t sector = UINT32_MAX, dev = 0, seq = 0;
      const bool loaded =
          (storageFindActiveCommand(cid, sector, dev, seq) &&
           storageLoadCommandFromSector(sector, cid)) ||
          storageLoadCommand(cid);
      if (!loaded) {
        automationActionFailures++;
        j.active = false;
        DBG_PRINT(F("WARN: automation load failed for command ")); DBG_PRINTLN(cid);
        return;
      }
      if (transmitLastCapture()) { statusLedPulseTransmit(); automationFireCount++; }
      else { automationActionFailures++; j.active = false; }
      return;                                     // one IR transmit per pass
    }
    j.active = false;
  }
}

static bool automationCooldownOk(const RuleSlot &s, uint32_t now) {
  return s.data.cooldownSec == 0U || !s.everFired ||
         (now - s.lastRuleFireMs) >= static_cast<uint32_t>(s.data.cooldownSec) * 1000UL;
}

// TEMP (held for DUR seconds) and/or TIME window, combined with AND/OR.
static bool automationTriggerConditionMet(const RuleTriggerStored &g,
                                          RuleTriggerRuntime &rt, uint32_t now) {
  const bool useTemp = (g.flags & TRIG_FLAG_USE_TEMP) != 0U;
  const bool useTime = (g.flags & TRIG_FLAG_USE_TIME) != 0U;
  bool tempReady = false;
  bool timeReady = false;

  if (useTemp) {
    if (automationTempCondition(g.tempOp, g.tempValueCenti)) {
      if (!rt.holding) { rt.holding = 1U; rt.holdSinceMs = now; }
      tempReady = (now - rt.holdSinceMs) >= static_cast<uint32_t>(g.tempDurationSec) * 1000UL;
    } else {
      rt.holding = 0U;
    }
  }
  if (useTime) {
    struct tm t{};
    if (timeGetLocal(t)) {
      timeReady = automationTimeCondition(g.timeStartMin, g.timeEndMin, g.weekdayMask, t);
    }
  }
  if (useTemp && useTime) {
    return (g.logic == RULE_LOGIC_AND) ? (tempReady && timeReady) : (tempReady || timeReady);
  }
  if (useTemp) return tempReady;
  if (useTime) return timeReady;
  return false;
}

static void automationEvaluateRule(uint8_t ruleIdx, RuleSlot &slot) {
  const bool enabled = (slot.data.ruleFlags & RULE_FLAG_ENABLED) != 0U;
  const uint32_t now = millis();

  for (uint8_t ti = 0U; ti < slot.data.triggerCount; ++ti) {
    const RuleTriggerStored &g = slot.data.triggers[ti];
    RuleTriggerRuntime &rt = slot.trig[ti];

    if (!enabled) { memset(&rt, 0, sizeof(rt)); continue; }

    if (g.flags & TRIG_FLAG_USE_INTERVAL) {
      const uint32_t periodMs = static_cast<uint32_t>(g.intervalSec) * 1000UL;
      if (!rt.armed) { rt.armed = 1U; rt.nextIntervalMs = now + periodMs; continue; }
      if (static_cast<int32_t>(now - rt.nextIntervalMs) < 0) continue;
      if (!automationCooldownOk(slot, now) || automationRuleBusy(ruleIdx)) continue;  // retry next pass
      if (automationStartSequence(ruleIdx, g.steps, g.stepCount, 0UL)) {
        slot.lastRuleFireMs = now; slot.everFired = 1U;
        rt.nextIntervalMs = now + periodMs;
      }
      continue;
    }

    if (!automationTriggerConditionMet(g, rt, now)) { rt.latched = 0U; continue; }
    if (rt.latched) continue;                                   // fire once per activation
    if (!automationCooldownOk(slot, now) || automationRuleBusy(ruleIdx)) continue;  // retry next pass
    if (automationStartSequence(ruleIdx, g.steps, g.stepCount, 0UL)) {
      rt.latched = 1U;
      slot.lastRuleFireMs = now; slot.everFired = 1U;
    }
  }
}

static AutomationScheduleState *automationFindSchedule(uint32_t scheduleId) {
  if (scheduleId < 1U || scheduleId > AUTOMATION_MAX_SCHEDULES) return nullptr;
  AutomationScheduleState &slot = automationSchedules[scheduleId - 1U];
  return slot.used ? &slot : nullptr;
}

bool automationAddOrUpdateSchedule(uint32_t scheduleId,
                                    const StoredSchedule &schedule) {
  if (scheduleId < 1U || scheduleId > AUTOMATION_MAX_SCHEDULES) return false;
  if (!automationValidateSchedule(schedule)) return false;
  if (!storageAppendSchedule(scheduleId, schedule)) return false;

  AutomationScheduleState &slot =
      automationSchedules[scheduleId - 1U];
  slot.data = schedule;
  slot.id = scheduleId;
  slot.used = true;
  slot.lastFiredDayKey = 0;
  slot.lastFiredMinuteOfDay = 0xFFFFFFFFUL;
  return true;
}

bool automationDeleteSchedule(uint32_t scheduleId) {
  if (scheduleId < 1U || scheduleId > AUTOMATION_MAX_SCHEDULES) return false;
  if (!automationFindSchedule(scheduleId)) return false;
  if (!storageAppendScheduleDeleteMarker(scheduleId)) return false;

  memset(&automationSchedules[scheduleId - 1U], 0,
         sizeof(automationSchedules[scheduleId - 1U]));
  automationSchedules[scheduleId - 1U].lastFiredMinuteOfDay =
      0xFFFFFFFFUL;
  return true;
}

bool automationSetScheduleEnabled(uint32_t scheduleId, bool enabled) {
  AutomationScheduleState *slot = automationFindSchedule(scheduleId);
  if (slot == nullptr) return false;

  StoredSchedule updated = slot->data;
  if (enabled) {
    updated.flags |= SCHEDULE_FLAG_ENABLED;
  } else {
    updated.flags &= static_cast<uint8_t>(~SCHEDULE_FLAG_ENABLED);
  }

  return automationAddOrUpdateSchedule(scheduleId, updated);
}

void automationService() {
  if (!storage.initialized) return;

  if (storage.generation != automationLastGeneration) {
    automationLastGeneration = storage.generation;
    automationLoadRules();
    automationLoadSchedules();
  }

  automationExecutePending();

  const uint32_t now = millis();
  if ((now - automationLastEvalMs) < AUTOMATION_EVAL_INTERVAL_MS) return;
  automationLastEvalMs = now;

  for (uint8_t i = 0; i < AUTOMATION_MAX_RULES; ++i) {
    if (automationRules[i].data.triggerCount == 0U) continue;
    automationEvaluateRule(i, automationRules[i]);
  }
  for (uint8_t i = 0; i < AUTOMATION_MAX_SCHEDULES; ++i) {
    if (automationSchedules[i].used) automationEvaluateSchedule(automationSchedules[i]);
  }
}

bool automationInitialize() {
  automationLastEvalMs          = 0UL;
  automationLastGeneration      = 0UL;
  automationFireCount           = 0UL;
  automationScheduleFireCount   = 0UL;
  automationActionFailures      = 0UL;
  automationLoadRules();
  automationLoadSchedules();
  return true;
}

bool automationSetRuleEnabled(uint32_t ruleId, bool enabled) {
  if (ruleId < 1U || ruleId > AUTOMATION_MAX_RULES) return false;
  RuleSlot &slot = automationRules[ruleId - 1U];
  if (slot.data.triggerCount == 0U) return false;
  const bool currentlyEnabled = (slot.data.ruleFlags & RULE_FLAG_ENABLED) != 0U;
  if (currentlyEnabled == enabled) return true;

  StoredRule updated = slot.data;
  if (enabled) updated.ruleFlags |= RULE_FLAG_ENABLED;
  else         updated.ruleFlags &= static_cast<uint8_t>(~RULE_FLAG_ENABLED);

  if (!storageAppendRule(ruleId, updated)) return false;
  if (!enabled) automationCancelRuleJobs(static_cast<uint8_t>(ruleId - 1U));
  slot.data = updated;
  slot.lastRuleFireMs = 0U;
  slot.everFired = 0U;
  memset(slot.trig, 0, sizeof(slot.trig));
  return true;
}

bool automationDeleteRule(uint32_t ruleId) {
  if (ruleId < 1U || ruleId > AUTOMATION_MAX_RULES) return false;
  if (automationRules[ruleId - 1U].data.triggerCount == 0U) return false;
  if (!storageAppendRuleDeleteMarker(ruleId)) return false;
  automationCancelRuleJobs(static_cast<uint8_t>(ruleId - 1U));
  memset(&automationRules[ruleId - 1U], 0, sizeof(automationRules[ruleId - 1U]));
  return true;
}

// ============================================================
// MQTT SERVICE — PubSubClient
// ============================================================

constexpr size_t MQTT_BROKER_MAX_LENGTH = 63U;
constexpr size_t MQTT_USERNAME_MAX_LENGTH = 63U;
constexpr size_t MQTT_PASSWORD_MAX_LENGTH = 63U;
constexpr size_t MQTT_CLIENT_ID_MAX_LENGTH = 31U;
constexpr size_t MQTT_BASE_TOPIC_MAX_LENGTH = 95U;
constexpr size_t MQTT_TOPIC_SEGMENT_MAX_LENGTH = (STORAGE_MAX_NAME * 3U);
constexpr size_t MQTT_COMMAND_TOPIC_MAX_LENGTH = MQTT_BASE_TOPIC_MAX_LENGTH + 1U +
    MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U + MQTT_TOPIC_SEGMENT_MAX_LENGTH;
constexpr uint8_t MQTT_COMMAND_QUEUE_SIZE = 4U;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 1024U;
constexpr uint16_t MQTT_KEEPALIVE_SECONDS = 30U;
constexpr uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 5U;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000UL;
constexpr uint32_t MQTT_TELEMETRY_INTERVAL_DEFAULT_MS = 30000UL;
constexpr size_t MQTT_JSON_BUFFER_SIZE = 900U;

constexpr char MQTT_DISCOVERY_PREFIX[] = "homeassistant";
// Sized for the CUSTOM namespace: <base>/_system/ha/<component>/<uniqueId>/config
//   base topic (95) + "/_system/ha" (11) + "/" (1) + component (7) + "/" (1)
//   + uniqueId (48) + "/config" (7) + NUL (1) = 171
constexpr size_t MQTT_DISCOVERY_TOPIC_MAX_LENGTH = 171U;
constexpr uint8_t MQTT_DISCOVERY_MAX_SECTOR_RETRIES = 2U;
constexpr size_t MQTT_HA_SLUG_MAX_LENGTH = STORAGE_MAX_NAME + 2U;
constexpr size_t MQTT_HA_ENTITY_ID_MAX_LENGTH = 7U + (MQTT_HA_SLUG_MAX_LENGTH * 2U) + 1U;
constexpr size_t MQTT_HA_UNIQUE_ID_MAX_LENGTH = 48U;

enum class MqttState : uint8_t {
  DISABLED, UNCONFIGURED, WAITING_WIFI, CONNECTING, CONNECTED, DISCONNECTED
};

struct MqttConfiguration {
  bool enabled = true;
  char broker[MQTT_BROKER_MAX_LENGTH + 1U] = {0};
  uint16_t port = 1883U;
  char username[MQTT_USERNAME_MAX_LENGTH + 1U] = {0};
  char password[MQTT_PASSWORD_MAX_LENGTH + 1U] = {0};
  char clientId[MQTT_CLIENT_ID_MAX_LENGTH + 1U] = {0};
  char baseTopic[MQTT_BASE_TOPIC_MAX_LENGTH + 1U] = {0};
};

struct MqttCommandQueueEntry {
  char deviceSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  char commandSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
};

const char *mqttStateToString(MqttState state);
bool mqttPopCommand(MqttCommandQueueEntry &entry);

// ============================================================
// PERSISTENT NETWORK CONFIGURATION
// ============================================================

constexpr size_t CONFIG_WIFI_OFFSET = 0U;
constexpr size_t CONFIG_WIFI_SLOT_SIZE = 160U;
constexpr size_t CONFIG_MQTT_OFFSET = CONFIG_WIFI_OFFSET + CONFIG_WIFI_SLOT_SIZE;
constexpr size_t CONFIG_MQTT_SLOT_SIZE = 336U;
constexpr size_t CONFIG_HUE_OFFSET = CONFIG_MQTT_OFFSET + CONFIG_MQTT_SLOT_SIZE;
constexpr size_t CONFIG_HUE_BITMAP_BYTES = (STORAGE_MAX_SECTORS + 7UL) / 8UL;
constexpr size_t CONFIG_HUE_SLOT_SIZE = 16U + CONFIG_HUE_BITMAP_BYTES;

constexpr size_t CONFIG_HA_OFFSET = CONFIG_HUE_OFFSET + CONFIG_HUE_SLOT_SIZE;
constexpr size_t CONFIG_HA_SLOT_SIZE = 16U;

constexpr size_t CONFIG_PINS_OFFSET = CONFIG_HA_OFFSET + CONFIG_HA_SLOT_SIZE;
constexpr size_t CONFIG_PINS_SLOT_SIZE = 16U;

constexpr size_t CONFIG_SYSTEM_OFFSET = CONFIG_PINS_OFFSET + CONFIG_PINS_SLOT_SIZE;
constexpr size_t CONFIG_SYSTEM_SLOT_SIZE = 96U;

// >>> v11 INTEGRATION — WebUI slot appended; existing slots unchanged
constexpr size_t CONFIG_EEPROM_BASE_SIZE = CONFIG_SYSTEM_OFFSET + CONFIG_SYSTEM_SLOT_SIZE;
constexpr size_t CONFIG_WEBUI_SLOT_SIZE  = 48U;
constexpr size_t CONFIG_WEBUI_SLOT_OFFSET = CONFIG_EEPROM_BASE_SIZE;

// ── v1.0: static IP slot appended after WebUI ────────────────
constexpr size_t CONFIG_STATICIP_SLOT_SIZE   = 32U;
constexpr size_t CONFIG_STATICIP_SLOT_OFFSET = CONFIG_WEBUI_SLOT_OFFSET + CONFIG_WEBUI_SLOT_SIZE;

// ── v1.0: temperature calibration slot ───────────────────────
constexpr size_t CONFIG_TEMPCAL_SLOT_SIZE   = 16U;
constexpr size_t CONFIG_TEMPCAL_SLOT_OFFSET = CONFIG_STATICIP_SLOT_OFFSET + CONFIG_STATICIP_SLOT_SIZE;

// ── Device category + Alexa Cloud slot ───────────────────────
// One byte per device ID: bits 0-3 = category, bit 7 = Alexa Cloud on.
constexpr uint16_t CONFIG_DEVCLOUD_MAX_ID         = 256U;
constexpr size_t   CONFIG_DEVCLOUD_SLOT_SIZE      = 16U + CONFIG_DEVCLOUD_MAX_ID;
constexpr size_t   CONFIG_DEVCLOUD_SLOT_OFFSET    = CONFIG_TEMPCAL_SLOT_OFFSET + CONFIG_TEMPCAL_SLOT_SIZE;
constexpr uint32_t CONFIG_DEVCLOUD_MAGIC          = 0x44434C44UL;   // 'DCLD'
constexpr uint16_t CONFIG_DEVCLOUD_FORMAT_VERSION = 1U;

constexpr size_t CONFIG_EEPROM_SIZE = CONFIG_DEVCLOUD_SLOT_OFFSET + CONFIG_DEVCLOUD_SLOT_SIZE;

// The storage layer in OverKill-v1.ino references this same value via
// STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES. If you change the EEPROM layout
// above, update that constant too. This assert catches the drift.
static_assert(CONFIG_EEPROM_SIZE == 1264U,
              "EEPROM layout changed; update "
              "STORAGE_CONFIG_BACKUP_PAYLOAD_BYTES in OverKill-v1.ino to match");

constexpr uint32_t CONFIG_WIFI_MAGIC     = 0x57494649UL;
constexpr uint32_t CONFIG_MQTT_MAGIC     = 0x4D515454UL;
constexpr uint32_t CONFIG_HUE_MAGIC      = 0x48554544UL;
constexpr uint32_t CONFIG_HA_MAGIC       = 0x48415353UL;
constexpr uint32_t CONFIG_PINS_MAGIC     = 0x50494E53UL;
constexpr uint32_t CONFIG_SYSTEM_MAGIC   = 0x53595354UL;
constexpr uint32_t CONFIG_STATICIP_MAGIC = 0x53544149UL;   // 'STAI'
constexpr uint16_t CONFIG_STATICIP_FORMAT_VERSION = 1U;
constexpr uint32_t CONFIG_TEMPCAL_MAGIC   = 0x544D5043UL;   // 'TMPC'
constexpr uint16_t CONFIG_TEMPCAL_FORMAT_VERSION = 1U;

constexpr uint16_t CONFIG_HUE_FORMAT_VERSION = 2U;
constexpr uint16_t CONFIG_FORMAT_VERSION = 1U;
constexpr uint16_t CONFIG_HA_FORMAT_VERSION = 1U;
constexpr uint16_t CONFIG_PINS_FORMAT_VERSION = 1U;
constexpr uint16_t CONFIG_SYSTEM_FORMAT_VERSION = 2U;

#pragma pack(push, 1)
struct PersistentWifiConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint8_t enabled;
  char ssid[WIFI_SSID_MAX_LENGTH + 1U];
  char password[WIFI_PASSWORD_MAX_LENGTH + 1U];
  char hostname[WIFI_HOSTNAME_MAX_LENGTH + 1U];
};
struct PersistentMqttConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint8_t enabled;
  char broker[MQTT_BROKER_MAX_LENGTH + 1U];
  uint16_t port;
  char username[MQTT_USERNAME_MAX_LENGTH + 1U];
  char password[MQTT_PASSWORD_MAX_LENGTH + 1U];
  char clientId[MQTT_CLIENT_ID_MAX_LENGTH + 1U];
  char baseTopic[MQTT_BASE_TOPIC_MAX_LENGTH + 1U];
};struct PersistentCommandHueConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint32_t databaseGeneration;
  uint8_t enabledBitmap[CONFIG_HUE_BITMAP_BYTES];
};

struct PersistentDeviceCloudConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint32_t databaseGeneration;
  uint8_t  settings[CONFIG_DEVCLOUD_MAX_ID];
};

struct PersistentHaConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint8_t enabled;
  uint8_t reserved[3];
};
struct PersistentPinConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint8_t irTxPin;
  uint8_t irRxPin;
  uint8_t statusLedPin;
  uint8_t ds18b20Pin;
};
struct PersistentSystemConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  uint8_t  serialEnabled;
  uint8_t  ntpEnabled;
  uint8_t  reserved[2];
  int16_t  ntpGmtOffsetMinutes;
  int16_t  ntpDstOffsetMinutes;
  char     ntpServer[64];
};

struct PersistentStaticIpConfiguration {
  uint32_t magic;         //  4
  uint16_t version;       //  2
  uint16_t payloadSize;   //  2
  uint32_t crc32;         //  4
  uint8_t  enabled;       //  1
  uint8_t  reserved[3];   //  3
  uint32_t ip;            //  4
  uint32_t gateway;       //  4
  uint32_t subnet;        //  4
  uint32_t dns;           //  4
};                        // = 32 bytes exactly

struct PersistentTempCalConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  int16_t  calibrationCenti;
  uint8_t  reserved[2];
};
#pragma pack(pop)

extern MqttConfiguration mqttConfig;
bool mqttTopicIsValid(const char *topic);

static_assert(sizeof(PersistentStaticIpConfiguration) == 32U,
              "PersistentStaticIpConfiguration size changed unexpectedly");
static_assert(CONFIG_STATICIP_SLOT_OFFSET + CONFIG_STATICIP_SLOT_SIZE <= CONFIG_EEPROM_SIZE,
              "Static IP slot exceeds EEPROM area");

static_assert(sizeof(PersistentWifiConfiguration) == 143U, "PersistentWifiConfiguration size changed unexpectedly");
static_assert(sizeof(PersistentMqttConfiguration) == 335U, "PersistentMqttConfiguration size changed unexpectedly");
static_assert(sizeof(PersistentCommandHueConfiguration) == (16U + CONFIG_HUE_BITMAP_BYTES),
              "PersistentCommandHueConfiguration size changed unexpectedly");
static_assert(sizeof(PersistentHaConfiguration) == 16U, "PersistentHaConfiguration size changed unexpectedly");
static_assert(sizeof(PersistentPinConfiguration) == 16U, "PersistentPinConfiguration size changed unexpectedly");
static_assert(sizeof(PersistentSystemConfiguration) == 84U, "PersistentSystemConfiguration size changed unexpectedly");
static_assert(CONFIG_WIFI_OFFSET + CONFIG_WIFI_SLOT_SIZE <= CONFIG_MQTT_OFFSET, "Wi-Fi configuration slot overlaps MQTT slot");
static_assert(CONFIG_MQTT_OFFSET + CONFIG_MQTT_SLOT_SIZE <= CONFIG_HUE_OFFSET, "MQTT configuration slot overlaps Hue configuration slot");
static_assert(CONFIG_HUE_OFFSET + CONFIG_HUE_SLOT_SIZE <= CONFIG_HA_OFFSET, "Hue configuration slot overlaps HA slot");
static_assert(CONFIG_HA_OFFSET + CONFIG_HA_SLOT_SIZE <= CONFIG_PINS_OFFSET, "HA configuration slot overlaps pin slot");
static_assert(CONFIG_PINS_OFFSET + CONFIG_PINS_SLOT_SIZE <= CONFIG_SYSTEM_OFFSET, "Pin configuration slot overlaps system slot");
static_assert(CONFIG_SYSTEM_OFFSET + CONFIG_SYSTEM_SLOT_SIZE <= CONFIG_EEPROM_BASE_SIZE, "System slot exceeds base EEPROM area");
static_assert(CONFIG_EEPROM_BASE_SIZE + CONFIG_WEBUI_SLOT_SIZE <= CONFIG_EEPROM_SIZE, "WebUI slot exceeds EEPROM area");

static_assert(sizeof(PersistentTempCalConfiguration) == 16U,
              "PersistentTempCalConfiguration size changed unexpectedly");
static_assert(CONFIG_TEMPCAL_SLOT_OFFSET + CONFIG_TEMPCAL_SLOT_SIZE <= CONFIG_EEPROM_SIZE,
              "Temp cal slot exceeds EEPROM area");

bool configurationStorageReady = false;
bool configurationCommandHueReady = false;
uint32_t configurationCommandHueGeneration = 0U;

bool haDiscoveryEnabled = true;

bool configurationRecordCRCValid(const uint8_t *record, size_t recordSize, size_t crcOffset, uint32_t expectedCrc) {
  if (record == nullptr || recordSize == 0U || crcOffset + sizeof(uint32_t) > recordSize) return false;
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < recordSize; ++i) {
    const uint8_t value = (i >= crcOffset && i < crcOffset + sizeof(uint32_t)) ? 0U : record[i];
    crc ^= value;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      const uint32_t mask = 0UL - (crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  crc = ~crc;
  return crc == expectedCrc;
}

bool configurationStringStoredSafely(const char *value, size_t capacity) {
  if (value == nullptr || capacity == 0U) return false;
  return value[capacity - 1U] == '\0';
}

void configurationStorageInitialize() {
  EEPROM.begin(CONFIG_EEPROM_SIZE);
  configurationStorageReady = (EEPROM.length() >= CONFIG_EEPROM_SIZE);
  if (!configurationStorageReady) {
    Serial.println(F("WARNING: persistent network configuration storage unavailable; runtime defaults will be used."));
  }
}

bool configurationInitializeCommandHue(uint32_t databaseGeneration) {
  configurationCommandHueReady = false;
  configurationCommandHueGeneration = 0U;
  if (!configurationStorageReady || databaseGeneration == 0U) return false;
  PersistentCommandHueConfiguration record{};
  EEPROM.get(CONFIG_HUE_OFFSET, record);
  const bool valid = record.magic == CONFIG_HUE_MAGIC &&
      record.version == CONFIG_HUE_FORMAT_VERSION &&
      record.payloadSize == (sizeof(record) - offsetof(PersistentCommandHueConfiguration, databaseGeneration)) &&
      record.databaseGeneration == databaseGeneration &&
      configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentCommandHueConfiguration, crc32), record.crc32);
  if (!valid) {
    record = PersistentCommandHueConfiguration{};
    record.magic = CONFIG_HUE_MAGIC;
    record.version = CONFIG_HUE_FORMAT_VERSION;
    record.payloadSize = static_cast<uint16_t>(sizeof(record) -
        offsetof(PersistentCommandHueConfiguration, databaseGeneration));
    record.crc32 = 0U;
    record.databaseGeneration = databaseGeneration;
    record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    EEPROM.put(CONFIG_HUE_OFFSET, record);
    if (!EEPROM.commit()) return false;
  }
  configurationCommandHueGeneration = databaseGeneration;
  configurationCommandHueReady = true;
  return true;
}

bool configurationCommandHueBitLocation(uint32_t commandId, size_t &byteOffset, uint8_t &bitMask) {
  byteOffset = 0U;
  bitMask = 0U;
  if (!configurationStorageReady || !configurationCommandHueReady ||
      !storage.initialized || configurationCommandHueGeneration != storage.generation ||
      commandId == 0U) return false;
  const uint32_t maxCommandId = storage.maxCommandId > 0U ? storage.maxCommandId : storageMaxPhysicalIdentity();
  if (maxCommandId == 0U || commandId > maxCommandId || commandId > STORAGE_MAX_SECTORS) return false;
  const uint32_t bitIndex = commandId - 1U;
  byteOffset = CONFIG_HUE_OFFSET + offsetof(PersistentCommandHueConfiguration, enabledBitmap) +
               static_cast<size_t>(bitIndex >> 3);
  bitMask = static_cast<uint8_t>(1U << (bitIndex & 7U));
  return byteOffset < (CONFIG_HUE_OFFSET + CONFIG_HUE_SLOT_SIZE);
}

bool configurationGetCommandHueEnabled(uint32_t commandId, bool &enabledOut) {
  enabledOut = false;
  size_t byteOffset = 0U;
  uint8_t bitMask = 0U;
  if (!configurationCommandHueBitLocation(commandId, byteOffset, bitMask) ||
      storageIsCommandDeleted(commandId)) return false;
  enabledOut = (EEPROM.read(byteOffset) & bitMask) != 0U;
  return true;
}

bool configurationSetCommandHueEnabled(uint32_t commandId, bool enabled) {
  size_t byteOffset = 0U;
  uint8_t bitMask = 0U;
  if (!configurationCommandHueBitLocation(commandId, byteOffset, bitMask) ||
      storageIsCommandDeleted(commandId)) return false;
  const uint8_t currentByte = EEPROM.read(byteOffset);
  const bool currentEnabled = (currentByte & bitMask) != 0U;
  if (currentEnabled == enabled) return true;
  const uint8_t updatedByte = enabled ? static_cast<uint8_t>(currentByte | bitMask)
                                      : static_cast<uint8_t>(currentByte & static_cast<uint8_t>(~bitMask));
  EEPROM.write(byteOffset, updatedByte);
  PersistentCommandHueConfiguration record{};
  EEPROM.get(CONFIG_HUE_OFFSET, record);
  record.magic = CONFIG_HUE_MAGIC;
  record.version = CONFIG_HUE_FORMAT_VERSION;
  record.payloadSize = static_cast<uint16_t>(sizeof(record) -
      offsetof(PersistentCommandHueConfiguration, databaseGeneration));
  record.databaseGeneration = storage.generation;
  record.crc32 = 0U;
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_HUE_OFFSET, record);
  if (!EEPROM.commit()) {
    configurationCommandHueReady = false;
    configurationCommandHueGeneration = 0U;
    return false;
  }
  return true;
}

// Runtime path for per-command Alexa exposure. Backed by the SPI
// flash rather than EEPROM, so it survives USB re-flashes of the
// ESP8266 internal flash. The EEPROM-based functions above remain
// only for backward compatibility of the config backup format;
// they are no longer read or written at runtime.
bool storageGetCommandHueEnabled(uint32_t commandId, bool &enabledOut) {
  return storageGetCommandAlexa(commandId, enabledOut);
}
bool storageSetCommandHueEnabled(uint32_t commandId, bool enabled) {
  const bool result = storageSetCommandAlexa(commandId, enabled);
  if (result) hueInvalidateCache();
  return result;
}

// ── Device category + Alexa Cloud (per device) ───────────────
static_assert(sizeof(PersistentDeviceCloudConfiguration) == (16U + CONFIG_DEVCLOUD_MAX_ID),
              "PersistentDeviceCloudConfiguration size changed unexpectedly");

constexpr uint8_t DEVICE_CATEGORY_NONE = 0U;
constexpr uint8_t DEVICE_CATEGORY_MAX  = 8U;
static const char *const DEVICE_CATEGORY_NAMES[DEVICE_CATEGORY_MAX + 1U] = {
  "", "TV", "SETTOP", "STREAMER", "SPEAKER", "FAN", "AC", "LIGHT", "SWITCH"
};

bool     configurationDeviceCloudReady = false;
uint32_t configurationDeviceCloudGeneration = 0U;
uint32_t deviceCloudConfigVersion = 0U;   // bumps on every change; used later for discovery config

const char *deviceCategoryName(uint8_t category) {
  return (category <= DEVICE_CATEGORY_MAX) ? DEVICE_CATEGORY_NAMES[category] : "";
}

uint8_t deviceCategoryFromName(const char *name) {
  if (name == nullptr || name[0] == '\0') return DEVICE_CATEGORY_NONE;
  for (uint8_t i = 1U; i <= DEVICE_CATEGORY_MAX; ++i) {
    if (strcasecmp(name, DEVICE_CATEGORY_NAMES[i]) == 0) return i;
  }
  return DEVICE_CATEGORY_NONE;
}

bool configurationInitializeDeviceCloud(uint32_t databaseGeneration) {
  configurationDeviceCloudReady = false;
  configurationDeviceCloudGeneration = 0U;
  if (!configurationStorageReady || databaseGeneration == 0U) return false;
  PersistentDeviceCloudConfiguration record{};
  EEPROM.get(CONFIG_DEVCLOUD_SLOT_OFFSET, record);
  const bool valid = record.magic == CONFIG_DEVCLOUD_MAGIC &&
      record.version == CONFIG_DEVCLOUD_FORMAT_VERSION &&
      record.payloadSize == (sizeof(record) - offsetof(PersistentDeviceCloudConfiguration, databaseGeneration)) &&
      record.databaseGeneration == databaseGeneration &&
      configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentDeviceCloudConfiguration, crc32), record.crc32);
  if (!valid) {
    record = PersistentDeviceCloudConfiguration{};
    record.magic = CONFIG_DEVCLOUD_MAGIC;
    record.version = CONFIG_DEVCLOUD_FORMAT_VERSION;
    record.payloadSize = static_cast<uint16_t>(sizeof(record) -
        offsetof(PersistentDeviceCloudConfiguration, databaseGeneration));
    record.crc32 = 0U;
    record.databaseGeneration = databaseGeneration;
    record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    EEPROM.put(CONFIG_DEVCLOUD_SLOT_OFFSET, record);
    if (!EEPROM.commit()) return false;
  }
  configurationDeviceCloudGeneration = databaseGeneration;
  configurationDeviceCloudReady = true;
  return true;
}

bool configurationDeviceCloudOffset(uint32_t deviceId, size_t &byteOffset) {
  byteOffset = 0U;
  if (!configurationStorageReady || !configurationDeviceCloudReady ||
      !storage.initialized || configurationDeviceCloudGeneration != storage.generation ||
      deviceId == 0U || deviceId > CONFIG_DEVCLOUD_MAX_ID) return false;
  byteOffset = CONFIG_DEVCLOUD_SLOT_OFFSET +
               offsetof(PersistentDeviceCloudConfiguration, settings) + (deviceId - 1U);
  return true;
}

// Runtime path for per-device category + Alexa Cloud. Backed by the
// SPI flash rather than EEPROM, so it survives USB re-flashes of the
// ESP8266 internal flash. The EEPROM-based helpers above remain only
// for backward compatibility of the config backup format; they are
// no longer read or written at runtime.
bool storageGetDeviceCloud(uint32_t deviceId, uint8_t &categoryOut, bool &cloudOut) {
  return storageGetDeviceSettings(deviceId, categoryOut, cloudOut);
}

bool storageSetDeviceCloud(uint32_t deviceId, uint8_t category, bool cloud) {
  if (category > DEVICE_CATEGORY_MAX) return false;
  if (cloud && category == DEVICE_CATEGORY_NONE) return false;
  const bool ok = storageSetDeviceSettings(deviceId, category, cloud);
  if (ok) {
    ++deviceCloudConfigVersion;
    hueInvalidateCache();
  }
  return ok;
}

bool configurationLoadWifi() {
  if (!configurationStorageReady) return false;
  PersistentWifiConfiguration record{};
  EEPROM.get(CONFIG_WIFI_OFFSET, record);
  if (record.magic != CONFIG_WIFI_MAGIC || record.version != CONFIG_FORMAT_VERSION ||
      record.payloadSize != (sizeof(record) - offsetof(PersistentWifiConfiguration, enabled)) ||
      !configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentWifiConfiguration, crc32), record.crc32) ||
      !configurationStringStoredSafely(record.ssid, sizeof(record.ssid)) ||
      !configurationStringStoredSafely(record.password, sizeof(record.password)) ||
      !configurationStringStoredSafely(record.hostname, sizeof(record.hostname)) ||
      record.ssid[0] == '\0' ||
      !wifiHostnameIsValid(record.hostname)) return false;
  memcpy(wifiConfig.ssid, record.ssid, sizeof(wifiConfig.ssid));
  memcpy(wifiConfig.password, record.password, sizeof(wifiConfig.password));
  memcpy(wifiConfig.hostname, record.hostname, sizeof(wifiConfig.hostname));
  return true;
}

bool configurationSaveWifi() {
  if (!configurationStorageReady) return false;
  PersistentWifiConfiguration record{};
  record.magic = CONFIG_WIFI_MAGIC;
  record.version = CONFIG_FORMAT_VERSION;
  record.payloadSize = sizeof(record) - offsetof(PersistentWifiConfiguration, enabled);
  record.crc32 = 0UL;
  record.enabled = 1U;
  memcpy(record.ssid, wifiConfig.ssid, sizeof(record.ssid));
  memcpy(record.password, wifiConfig.password, sizeof(record.password));
  memcpy(record.hostname, wifiConfig.hostname, sizeof(record.hostname));
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_WIFI_OFFSET, record);
  return EEPROM.commit();
}

bool configurationLoadStaticIp() {
  staticIpConfig = StaticIpConfig{};
  if (!configurationStorageReady) return false;

  PersistentStaticIpConfiguration record{};
  EEPROM.get(CONFIG_STATICIP_SLOT_OFFSET, record);

  if (record.magic != CONFIG_STATICIP_MAGIC ||
      record.version != CONFIG_STATICIP_FORMAT_VERSION ||
      record.payloadSize != (sizeof(record) - offsetof(PersistentStaticIpConfiguration, enabled)) ||
      !configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record),
                                   sizeof(record),
                                   offsetof(PersistentStaticIpConfiguration, crc32),
                                   record.crc32)) {
    return false;
  }

  staticIpConfig.enabled = record.enabled != 0U;
  staticIpConfig.ip      = IPAddress(record.ip);
  staticIpConfig.gateway = IPAddress(record.gateway);
  staticIpConfig.subnet  = IPAddress(record.subnet);
  staticIpConfig.dns     = IPAddress(record.dns);
  return true;
}

bool configurationSaveStaticIp() {
  if (!configurationStorageReady) return false;

  PersistentStaticIpConfiguration record{};
  record.magic       = CONFIG_STATICIP_MAGIC;
  record.version     = CONFIG_STATICIP_FORMAT_VERSION;
  record.payloadSize = sizeof(record) - offsetof(PersistentStaticIpConfiguration, enabled);
  record.crc32       = 0UL;
  record.enabled     = staticIpConfig.enabled ? 1U : 0U;
  record.reserved[0] = 0U;
  record.reserved[1] = 0U;
  record.reserved[2] = 0U;
  record.ip      = static_cast<uint32_t>(staticIpConfig.ip);
  record.gateway = static_cast<uint32_t>(staticIpConfig.gateway);
  record.subnet  = static_cast<uint32_t>(staticIpConfig.subnet);
  record.dns     = static_cast<uint32_t>(staticIpConfig.dns);
  record.crc32   = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));

  EEPROM.put(CONFIG_STATICIP_SLOT_OFFSET, record);
  return EEPROM.commit();
}

bool configurationLoadTempCal() {
  if (!configurationStorageReady) return false;
  PersistentTempCalConfiguration record{};
  EEPROM.get(CONFIG_TEMPCAL_SLOT_OFFSET, record);
  const bool valid =
      record.magic == CONFIG_TEMPCAL_MAGIC &&
      record.version == CONFIG_TEMPCAL_FORMAT_VERSION &&
      record.payloadSize == (sizeof(record) - offsetof(PersistentTempCalConfiguration, calibrationCenti)) &&
      configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record),
                                  sizeof(record),
                                  offsetof(PersistentTempCalConfiguration, crc32),
                                  record.crc32);
  if (!valid) return false;
  ds18b20SetCalibrationCentiDegrees(record.calibrationCenti);
  return true;
}

bool configurationSaveTempCal() {
  if (!configurationStorageReady) return false;
  PersistentTempCalConfiguration record{};
  record.magic            = CONFIG_TEMPCAL_MAGIC;
  record.version          = CONFIG_TEMPCAL_FORMAT_VERSION;
  record.payloadSize      = sizeof(record) - offsetof(PersistentTempCalConfiguration, calibrationCenti);
  record.crc32            = 0UL;
  record.calibrationCenti = ds18b20GetCalibrationCentiDegrees();
  for (size_t i = 0; i < sizeof(record.reserved); ++i) record.reserved[i] = 0U;
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_TEMPCAL_SLOT_OFFSET, record);
  return EEPROM.commit();
}

// ============================================================
// PER-ENTITY SETTINGS ON SPI (v1.1 — Stage 1)
// ============================================================
//  Device category + Alexa Cloud and per-command Alexa exposure
//  are stored on the W25Qxx alongside the device and command
//  records they belong to. This survives USB re-flashes, which
//  wipe the ESP8266 internal flash where EEPROM emulation lives.
//
//  Same append-only pattern as device renames: a new record
//  supersedes the old one via sequence number. Reads pick the
//  newest valid record for the target ID. Deletion of the parent
//  device or command is honoured by the standard deleted-ID
//  bitmaps, so settings on deleted entities become invisible
//  without needing a tombstone.
//
//  These functions are unused at the end of Stage 1. Stage 2
//  switches every caller over.



static bool storageAppendSettings(uint8_t type, uint32_t targetId,
                                  uint8_t b0, uint8_t b1) {
  if (!storage.initialized || targetId == 0) return false;
  if (irState != IrEngineState::IDLE) return false;
  const uint8_t payload[2] = { b0, b1 };
  uint32_t seq = 0;
  if (!storageAppendRawRecord(type, targetId, payload, 2U, seq)) return false;
  storage.maxSequence = seq;
  return true;
}

bool storageSetDeviceSettings(uint32_t deviceId, uint8_t category, bool cloud) {
  if (category > DEVICE_CATEGORY_MAX) return false;
  if (cloud && category == DEVICE_CATEGORY_NONE) return false;
  if (!storage.initialized) return false;
  if (storageIsDeviceDeleted(deviceId)) return false;
  if (irState != IrEngineState::IDLE) return false;

  char ignored[STORAGE_MAX_NAME + 1] = {0};
  if (!storageFindDeviceName(deviceId, ignored, sizeof(ignored))) return false;

  if (!storageAppendSettings(RECORD_TYPE_DEVICE_SETTINGS, deviceId,
                             (uint8_t)(category & 0x0FU),
                             cloud ? 0x01U : 0x00U)) return false;

  if (deviceId >= 1U && deviceId <= 256U) {
    storageDeviceCloudMap[deviceId - 1U] =
        (uint8_t)((category & 0x0FU) | (cloud ? 0x80U : 0x00U));
  }
  return true;
}

bool storageGetDeviceSettings(uint32_t deviceId, uint8_t &categoryOut, bool &cloudOut) {
  categoryOut = DEVICE_CATEGORY_NONE;
  cloudOut = false;
  if (!storage.initialized || deviceId == 0 || deviceId > 256U) return false;
  if (storageIsDeviceDeleted(deviceId)) return false;
  const uint8_t packed = storageDeviceCloudMap[deviceId - 1U];
  const uint8_t cat = packed & 0x0FU;
  categoryOut = (cat <= DEVICE_CATEGORY_MAX) ? cat : DEVICE_CATEGORY_NONE;
  cloudOut = (packed & 0x80U) != 0U;
  return true;
}

bool storageSetCommandAlexa(uint32_t commandId, bool alexaOn) {
  if (!storage.initialized || commandId == 0) return false;
  if (storageIsCommandDeleted(commandId)) return false;
  if (irState != IrEngineState::IDLE) return false;

  uint32_t sector = UINT32_MAX, dev = 0, seq = 0;
  if (!storageFindActiveCommand(commandId, sector, dev, seq)) return false;
  if (storageIsDeviceDeleted(dev)) return false;

  if (!storageAppendSettings(RECORD_TYPE_COMMAND_SETTINGS, commandId,
                             alexaOn ? 0x01U : 0x00U, 0x00U)) return false;

  if (commandId < STORAGE_MAX_SECTORS) {
    if (alexaOn) storageCommandAlexaBitmap[commandId >> 3] |=  (uint8_t)(1U << (commandId & 7U));
    else         storageCommandAlexaBitmap[commandId >> 3] &= (uint8_t)~(1U << (commandId & 7U));
  }
  return true;
}

bool storageGetCommandAlexa(uint32_t commandId, bool &alexaOut) {
  alexaOut = false;
  if (!storage.initialized || commandId == 0) return false;
  if (storageIsCommandDeleted(commandId)) return false;
  if (commandId >= STORAGE_MAX_SECTORS) return false;
  alexaOut = (storageCommandAlexaBitmap[commandId >> 3] &
              (uint8_t)(1U << (commandId & 7U))) != 0U;
  return true;
}

bool configurationLoadMqtt() {
  if (!configurationStorageReady) return false;
  PersistentMqttConfiguration record{};
  EEPROM.get(CONFIG_MQTT_OFFSET, record);
  if (record.magic != CONFIG_MQTT_MAGIC || record.version != CONFIG_FORMAT_VERSION ||
      record.payloadSize != (sizeof(record) - offsetof(PersistentMqttConfiguration, enabled)) ||
      !configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentMqttConfiguration, crc32), record.crc32) ||
      !configurationStringStoredSafely(record.broker, sizeof(record.broker)) ||
      !configurationStringStoredSafely(record.username, sizeof(record.username)) ||
      !configurationStringStoredSafely(record.password, sizeof(record.password)) ||
      !configurationStringStoredSafely(record.clientId, sizeof(record.clientId)) ||
      !configurationStringStoredSafely(record.baseTopic, sizeof(record.baseTopic)) ||
      (record.enabled != 0U && record.broker[0] == '\0') ||
      record.port == 0U || record.clientId[0] == '\0' ||
      !mqttTopicIsValid(record.baseTopic)) return false;
  mqttConfig.enabled = record.enabled != 0U;
  memcpy(mqttConfig.broker, record.broker, sizeof(mqttConfig.broker));
  mqttConfig.port = record.port;
  memcpy(mqttConfig.username, record.username, sizeof(mqttConfig.username));
  memcpy(mqttConfig.password, record.password, sizeof(mqttConfig.password));
  memcpy(mqttConfig.clientId, record.clientId, sizeof(mqttConfig.clientId));
  memcpy(mqttConfig.baseTopic, record.baseTopic, sizeof(mqttConfig.baseTopic));
  return true;
}

bool configurationSaveMqtt() {
  if (!configurationStorageReady) return false;
  PersistentMqttConfiguration record{};
  record.magic = CONFIG_MQTT_MAGIC;
  record.version = CONFIG_FORMAT_VERSION;
  record.payloadSize = sizeof(record) - offsetof(PersistentMqttConfiguration, enabled);
  record.crc32 = 0UL;
  record.enabled = mqttConfig.enabled ? 1U : 0U;
  memcpy(record.broker, mqttConfig.broker, sizeof(record.broker));
  record.port = mqttConfig.port;
  memcpy(record.username, mqttConfig.username, sizeof(record.username));
  memcpy(record.password, mqttConfig.password, sizeof(record.password));
  memcpy(record.clientId, mqttConfig.clientId, sizeof(record.clientId));
  memcpy(record.baseTopic, mqttConfig.baseTopic, sizeof(record.baseTopic));
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_MQTT_OFFSET, record);
  return EEPROM.commit();
}

bool configurationLoadHa() {
  if (!configurationStorageReady) return false;
  PersistentHaConfiguration record{};
  EEPROM.get(CONFIG_HA_OFFSET, record);
  if (record.magic != CONFIG_HA_MAGIC ||
      record.version != CONFIG_HA_FORMAT_VERSION ||
      record.payloadSize != (sizeof(record) - offsetof(PersistentHaConfiguration, enabled)) ||
      !configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentHaConfiguration, crc32), record.crc32)) {
    haDiscoveryEnabled = true;
    return false;
  }
  haDiscoveryEnabled = record.enabled != 0U;
  return true;
}

bool configurationSaveHa() {
  if (!configurationStorageReady) return false;
  PersistentHaConfiguration record{};
  record.magic = CONFIG_HA_MAGIC;
  record.version = CONFIG_HA_FORMAT_VERSION;
  record.payloadSize = sizeof(record) - offsetof(PersistentHaConfiguration, enabled);
  record.crc32 = 0UL;
  record.enabled = haDiscoveryEnabled ? 1U : 0U;
  record.reserved[0] = record.reserved[1] = record.reserved[2] = 0;
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_HA_OFFSET, record);
  return EEPROM.commit();
}

bool pinIsAllowedForFunction(uint8_t pin, bool needsInterrupt) {
  switch (pin) {
    case 1: case 2: case 3: case 4: case 5: return true;
    case 16: return !needsInterrupt;
    default: return false;
  }
}

bool pinSetIsConsistent() {
  if (!pinIsAllowedForFunction(pinIrTx, false)) return false;
  if (!pinIsAllowedForFunction(pinIrRx, true)) return false;
  if (!pinIsAllowedForFunction(pinStatusLed, false)) return false;
  if (!pinIsAllowedForFunction(pinDs18b20, false)) return false;
  if (pinIrTx == pinIrRx) return false;
  if (pinIrTx == pinStatusLed) return false;
  if (pinIrTx == pinDs18b20) return false;
  if (pinIrRx == pinStatusLed) return false;
  if (pinIrRx == pinDs18b20) return false;
  if (pinStatusLed == pinDs18b20) return false;
  return true;
}

bool configurationLoadPins() {
  pinIrTx = PIN_IR_TX_DEFAULT;
  pinIrRx = PIN_IR_RX_DEFAULT;
  pinStatusLed = PIN_STATUS_LED_DEFAULT;
  pinDs18b20 = PIN_DS18B20_DEFAULT;
  if (!configurationStorageReady) return false;
  PersistentPinConfiguration record{};
  EEPROM.get(CONFIG_PINS_OFFSET, record);
  if (record.magic != CONFIG_PINS_MAGIC ||
      record.version != CONFIG_PINS_FORMAT_VERSION ||
      record.payloadSize != (sizeof(record) - offsetof(PersistentPinConfiguration, irTxPin)) ||
      !configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
          offsetof(PersistentPinConfiguration, crc32), record.crc32)) return false;
  pinIrTx = record.irTxPin;
  pinIrRx = record.irRxPin;
  pinStatusLed = record.statusLedPin;
  pinDs18b20 = record.ds18b20Pin;
  if (!pinSetIsConsistent()) {
    Serial.println(F("WARNING: persisted pin configuration is invalid; using defaults."));
    pinIrTx = PIN_IR_TX_DEFAULT;
    pinIrRx = PIN_IR_RX_DEFAULT;
    pinStatusLed = PIN_STATUS_LED_DEFAULT;
    pinDs18b20 = PIN_DS18B20_DEFAULT;
    return false;
  }
  return true;
}

bool configurationSavePins() {
  if (!configurationStorageReady) return false;
  PersistentPinConfiguration record{};
  record.magic = CONFIG_PINS_MAGIC;
  record.version = CONFIG_PINS_FORMAT_VERSION;
  record.payloadSize = sizeof(record) - offsetof(PersistentPinConfiguration, irTxPin);
  record.crc32 = 0UL;
  record.irTxPin = pinIrTx;
  record.irRxPin = pinIrRx;
  record.statusLedPin = pinStatusLed;
  record.ds18b20Pin = pinDs18b20;
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_PINS_OFFSET, record);
  return EEPROM.commit();
}

bool configurationLoadSystem() {
  serialEnabled       = true;
  ntpEnabled          = true;
  ntpGmtOffsetMinutes = 0;
  ntpDstOffsetMinutes = 0;
  memcpy(ntpServer, "pool.ntp.org", sizeof("pool.ntp.org"));

  if (!configurationStorageReady) return false;

  PersistentSystemConfiguration record{};
  EEPROM.get(CONFIG_SYSTEM_OFFSET, record);

  const bool valid =
      record.magic == CONFIG_SYSTEM_MAGIC &&
      (record.version == 1U || record.version == CONFIG_SYSTEM_FORMAT_VERSION) &&
      record.payloadSize == (sizeof(record) - offsetof(PersistentSystemConfiguration, serialEnabled)) &&
      configurationRecordCRCValid(reinterpret_cast<const uint8_t *>(&record), sizeof(record),
                                  offsetof(PersistentSystemConfiguration, crc32), record.crc32) &&
      record.ntpServer[sizeof(record.ntpServer) - 1U] == '\0';

  if (!valid) return false;

  serialEnabled       = record.serialEnabled != 0U;
  ntpEnabled          = record.ntpEnabled != 0U;
  ntpGmtOffsetMinutes = record.ntpGmtOffsetMinutes;
  ntpDstOffsetMinutes = record.ntpDstOffsetMinutes;
  memcpy(ntpServer, record.ntpServer, sizeof(ntpServer));
  ntpServer[sizeof(ntpServer) - 1U] = '\0';
  discoveryMode = (record.version >= 2U && record.reserved[0] == 1U)
                    ? DiscoveryMode::CUSTOM
                    : DiscoveryMode::NATIVE_HA;
  return true;
}

bool configurationSaveSystem() {
  if (!configurationStorageReady) return false;
  PersistentSystemConfiguration record{};
  record.magic         = CONFIG_SYSTEM_MAGIC;
  record.version       = CONFIG_SYSTEM_FORMAT_VERSION;
  record.payloadSize   = sizeof(record) - offsetof(PersistentSystemConfiguration, serialEnabled);
  record.crc32         = 0UL;
  record.serialEnabled = serialEnabled ? 1U : 0U;
  record.ntpEnabled    = ntpEnabled    ? 1U : 0U;
  record.reserved[0]   = (discoveryMode == DiscoveryMode::NATIVE_HA) ? 0U : 1U;
  record.reserved[1]   = 0U;
  record.ntpGmtOffsetMinutes = ntpGmtOffsetMinutes;
  record.ntpDstOffsetMinutes = ntpDstOffsetMinutes;
  memcpy(record.ntpServer, ntpServer, sizeof(record.ntpServer));
  record.ntpServer[sizeof(record.ntpServer) - 1U] = '\0';
  record.crc32 = crc32Bytes(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  EEPROM.put(CONFIG_SYSTEM_OFFSET, record);
  return EEPROM.commit();
}


MqttConfiguration mqttConfig;
MqttState mqttState = MqttState::UNCONFIGURED;
WiFiClient mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);

bool mqttConfigurationDirty = false;
uint32_t mqttNextReconnectMs = 0UL;
uint32_t mqttLastTelemetryMs = 0UL;
bool mqttTelemetryImmediatePending = false;
constexpr uint32_t mqttTelemetryIntervalMs = MQTT_TELEMETRY_INTERVAL_DEFAULT_MS;

MqttCommandQueueEntry mqttCommandQueue[MQTT_COMMAND_QUEUE_SIZE];
uint8_t mqttCommandQueueHead = 0U;
uint8_t mqttCommandQueueTail = 0U;
uint8_t mqttCommandQueueCount = 0U;
bool mqttCommandQueueOverflowPending = false;

static char mqttPayloadBuffer[MQTT_JSON_BUFFER_SIZE];

uint32_t mqttLastPublishedTemperatureSequence = UINT32_MAX;
int32_t mqttLastPublishedTemperatureCentiDegrees = INT32_MIN;
bool mqttLastPublishedTemperatureValid = false;
bool mqttLastPublishedTemperatureEnabled = false;

bool mqttDiscoverySyncPending = false;
bool mqttDiscoverySyncActive = false;
bool mqttHaDiscoveryCleanupPending = false;
uint32_t mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
bool mqttDiscoveryTemperaturePublished = false;
uint32_t mqttDiscoveryRetrySector = UINT32_MAX;
uint8_t mqttDiscoverySectorRetryCount = 0U;

const char *mqttStateToString(MqttState state) {
  switch (state) {
    case MqttState::DISABLED:     return "DISABLED";
    case MqttState::UNCONFIGURED: return "UNCONFIGURED";
    case MqttState::WAITING_WIFI: return "WAITING_WIFI";
    case MqttState::CONNECTING:   return "CONNECTING";
    case MqttState::CONNECTED:    return "CONNECTED";
    case MqttState::DISCONNECTED: return "DISCONNECTED";
    default:                      return "UNKNOWN";
  }
}

bool mqttCopyField(char *destination, size_t capacity, const char *source, bool allowEmpty) {
  if (destination == nullptr || source == nullptr || capacity < 2U) return false;
  const size_t length = strlen(source);
  if ((!allowEmpty && length == 0U) || length >= capacity) return false;
  memcpy(destination, source, length + 1U);
  return true;
}

bool mqttTopicIsValid(const char *topic) {
  if (topic == nullptr) return false;
  const size_t length = strlen(topic);
  if (length == 0U || length > MQTT_BASE_TOPIC_MAX_LENGTH ||
      topic[0] == '/' || topic[length - 1U] == '/') return false;
  char previous = '\0';
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(topic[i]);
    if (c < 32U || c == '#' || c == '+') return false;
    if (c == '/' && previous == '/') return false;
    previous = static_cast<char>(c);
  }
  return true;
}

bool mqttEncodeTopicSegment(const char *name, char *destination, size_t destinationCapacity) {
  if (name == nullptr || destination == nullptr || destinationCapacity < 2U) return false;
  size_t out = 0U;
  for (size_t i = 0; name[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.';
    if (safe) {
      if (out + 1U >= destinationCapacity) return false;
      destination[out++] = static_cast<char>(c);
    } else {
      if (out + 3U >= destinationCapacity) return false;
      static const char hex[] = "0123456789ABCDEF";
      destination[out++] = '%';
      destination[out++] = hex[(c >> 4) & 0x0FU];
      destination[out++] = hex[c & 0x0FU];
    }
  }
  if (out == 0U || out >= destinationCapacity) return false;
  destination[out] = '\0';
  return true;
}

bool mqttBuildCommandTopic(const char *deviceName, const char *commandName,
                           char *destination, size_t destinationCapacity) {
  if (deviceName == nullptr || commandName == nullptr ||
      destination == nullptr || destinationCapacity == 0U) return false;
  char deviceSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  char commandSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  if (!mqttEncodeTopicSegment(deviceName, deviceSegment, sizeof(deviceSegment)) ||
      !mqttEncodeTopicSegment(commandName, commandSegment, sizeof(commandSegment))) return false;
  const int written = snprintf(destination, destinationCapacity, "%s/%s/%s",
                               mqttConfig.baseTopic, deviceSegment, commandSegment);
  return written > 0 && static_cast<size_t>(written) < destinationCapacity;
}

void mqttBuildSystemTopic(const char *leaf, char *destination, size_t destinationCapacity) {
  if (destination == nullptr || destinationCapacity == 0U) return;
  const int written = snprintf(destination, destinationCapacity, "%s/_system/%s",
                               mqttConfig.baseTopic, (leaf != nullptr) ? leaf : "status");
  if (written < 0 || static_cast<size_t>(written) >= destinationCapacity) destination[0] = '\0';
}

bool mqttTopicSegmentMatchesName(const char *segment, const char *name) {
  char encoded[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  return mqttEncodeTopicSegment(name, encoded, sizeof(encoded)) && strcmp(segment, encoded) == 0;
}

bool mqttExtractCommandTopic(const char *topic, char *deviceSegment, size_t deviceCapacity,
                             char *commandSegment, size_t commandCapacity) {
  if (topic == nullptr || deviceSegment == nullptr || commandSegment == nullptr ||
      deviceCapacity < 2U || commandCapacity < 2U) return false;
  char prefix[MQTT_BASE_TOPIC_MAX_LENGTH + 2U] = {0};
  const int prefixWritten = snprintf(prefix, sizeof(prefix), "%s/", mqttConfig.baseTopic);
  if (prefixWritten <= 0 || static_cast<size_t>(prefixWritten) >= sizeof(prefix)) return false;
  const size_t prefixLength = strlen(prefix);
  if (strncmp(topic, prefix, prefixLength) != 0) return false;
  const char *rest = topic + prefixLength;
  if (*rest == '\0') return false;
  const char *slash = strchr(rest, '/');
  if (slash == nullptr || slash == rest || slash[1] == '\0') return false;
  if (strchr(slash + 1, '/') != nullptr) return false;
  const size_t deviceLength = static_cast<size_t>(slash - rest);
  const size_t commandLength = strlen(slash + 1);
  if (deviceLength == 0U || deviceLength >= deviceCapacity ||
      commandLength == 0U || commandLength >= commandCapacity) return false;
  memcpy(deviceSegment, rest, deviceLength);
  deviceSegment[deviceLength] = '\0';
  memcpy(commandSegment, slash + 1, commandLength + 1U);
  return true;
}

void mqttRequestDiscoveryRefresh() {
  mqttDiscoverySyncPending = true;
  hueInvalidateCache();
}

bool mqttHaSlugify(const char *source, char *destination, size_t destinationCapacity) {
  if (source == nullptr || destination == nullptr || destinationCapacity < 2U) return false;
  size_t out = 0U;
  bool pendingSeparator = false;
  for (size_t i = 0; source[i] != '\0'; ++i) {
    const uint8_t c = static_cast<uint8_t>(source[i]);
    const bool alphaNumeric = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alphaNumeric) {
      if (pendingSeparator && out > 0U) {
        if (out + 1U >= destinationCapacity) return false;
        destination[out++] = '_';
      }
      pendingSeparator = false;
      if (out + 1U >= destinationCapacity) return false;
      if (c >= 'A' && c <= 'Z') destination[out++] = static_cast<char>(c - 'A' + 'a');
      else destination[out++] = static_cast<char>(c);
    } else if (out > 0U) pendingSeparator = true;
  }
  if (out == 0U) {
    const uint32_t nameCrc = crc32Bytes(reinterpret_cast<const uint8_t *>(source), strlen(source));
    const int written = snprintf(destination, destinationCapacity, "ir_%08lX",
                                 static_cast<unsigned long>(nameCrc));
    return written > 0 && static_cast<size_t>(written) < destinationCapacity;
  }
  if (destination[0] >= '0' && destination[0] <= '9') {
    if (out + 2U >= destinationCapacity) return false;
    memmove(destination + 2U, destination, out + 1U);
    destination[0] = 'i'; destination[1] = '_'; out += 2U;
  }
  destination[out] = '\0';
  return true;
}

bool mqttJsonAppendRaw(char *destination, size_t capacity, size_t &offset, const char *value) {
  if (destination == nullptr || value == nullptr || offset >= capacity) return false;
  const size_t length = strlen(value);
  if (length > capacity - offset - 1U) return false;
  memcpy(destination + offset, value, length);
  offset += length;
  destination[offset] = '\0';
  return true;
}

bool mqttJsonAppendString(char *destination, size_t capacity, size_t &offset, const char *value) {
  if (destination == nullptr || value == nullptr || offset >= capacity) return false;
  if (!mqttJsonAppendRaw(destination, capacity, offset, "\"")) return false;
  for (size_t i = 0; value[i] != '\0'; ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c == '"' || c == '\\') {
      if (offset + 2U >= capacity) return false;
      destination[offset++] = '\\';
      destination[offset++] = static_cast<char>(c);
      destination[offset] = '\0';
    } else if (c < 32U) {
      if (offset + 6U >= capacity) return false;
      const char hex[] = "0123456789ABCDEF";
      destination[offset++] = '\\'; destination[offset++] = 'u';
      destination[offset++] = '0'; destination[offset++] = '0';
      destination[offset++] = '0'; destination[offset++] = hex[c];
      destination[offset] = '\0';
    } else {
      if (offset + 1U >= capacity) return false;
      destination[offset++] = static_cast<char>(c);
      destination[offset] = '\0';
    }
  }
  return mqttJsonAppendRaw(destination, capacity, offset, "\"");
}

void mqttBuildHaHubIdentifier(char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  const int written = snprintf(destination, capacity, "irhub_%06lX", chipId);
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
void mqttBuildHaCommandUniqueId(uint32_t commandId, char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  char hubId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  mqttBuildHaHubIdentifier(hubId, sizeof(hubId));
  const int written = snprintf(destination, capacity, "%s_cmd_%lu", hubId,
                               static_cast<unsigned long>(commandId));
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
void mqttBuildHaTemperatureUniqueId(char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  char hubId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  mqttBuildHaHubIdentifier(hubId, sizeof(hubId));
  const int written = snprintf(destination, capacity, "%s_temperature", hubId);
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
void mqttBuildHaChildDeviceIdentifier(uint32_t deviceId, char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  const int written = snprintf(destination, capacity, "irhub_device_%lu",
                               static_cast<unsigned long>(deviceId));
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
bool mqttBuildHaButtonEntityId(const char *deviceName, const char *commandName,
                               char *destination, size_t destinationCapacity) {
  if (deviceName == nullptr || commandName == nullptr ||
      destination == nullptr || destinationCapacity < 16U) return false;
  char deviceSlug[MQTT_HA_SLUG_MAX_LENGTH + 1U] = {0};
  char commandSlug[MQTT_HA_SLUG_MAX_LENGTH + 1U] = {0};
  if (!mqttHaSlugify(deviceName, deviceSlug, sizeof(deviceSlug)) ||
      !mqttHaSlugify(commandName, commandSlug, sizeof(commandSlug))) return false;
  const int written = snprintf(destination, destinationCapacity, "button.%s_%s",
                               deviceSlug, commandSlug);
  return written > 0 && static_cast<size_t>(written) < destinationCapacity;
}
static char mqttHaPrefixBuf[MQTT_BASE_TOPIC_MAX_LENGTH + 16U];

static const char *mqttHaDiscoveryRoot() {
  if (discoveryMode == DiscoveryMode::NATIVE_HA) return MQTT_DISCOVERY_PREFIX;
  snprintf(mqttHaPrefixBuf, sizeof(mqttHaPrefixBuf),
           "%s/_system/ha", mqttConfig.baseTopic);
  return mqttHaPrefixBuf;
}

bool mqttBuildHaDiscoveryTopic(const char *component, const char *objectId,
                               char *destination, size_t destinationCapacity) {
  if (component == nullptr || objectId == nullptr ||
      destination == nullptr || destinationCapacity == 0U) return false;
  const int written = snprintf(destination, destinationCapacity, "%s/%s/%s/config",
                               mqttHaDiscoveryRoot(), component, objectId);
  return written > 0 && static_cast<size_t>(written) < destinationCapacity;
}

bool mqttPublishHaTemperatureDiscovery() {
  if (!mqttClient.connected()) return false;
  char discoveryTopic[MQTT_DISCOVERY_TOPIC_MAX_LENGTH + 1U] = {0};
  char hubId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char uniqueId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char temperatureTopic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  char availabilityTopic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildHaHubIdentifier(hubId, sizeof(hubId));
  mqttBuildHaTemperatureUniqueId(uniqueId, sizeof(uniqueId));
  mqttBuildSystemTopic("temperature", temperatureTopic, sizeof(temperatureTopic));
  mqttBuildSystemTopic("availability", availabilityTopic, sizeof(availabilityTopic));
  if (hubId[0] == '\0' || uniqueId[0] == '\0' || temperatureTopic[0] == '\0' ||      availabilityTopic[0] == '\0' ||
      !mqttBuildHaDiscoveryTopic("sensor", uniqueId, discoveryTopic, sizeof(discoveryTopic))) return false;
  size_t offset = 0U;
  mqttPayloadBuffer[0] = '\0';
  if (!mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "{\"name\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "Temperature") ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"stat_t\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, temperatureTopic) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset,
          ",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\\u00B0C\",\"stat_cla\":\"measurement\",\"avty_t\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, availabilityTopic) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"uniq_id\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, uniqueId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset,
          ",\"def_ent_id\":\"sensor.universal_ir_hub_temperature\",\"o\":{\"name\":\"OverKill Smart Hub\",\"sw\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, FIRMWARE_VERSION) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "},\"dev\":{\"ids\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, hubId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset,
          ",\"name\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, webUiHubName) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset,
          ",\"mf\":\"OverKill Smart Hub\",\"mdl\":\"ESP8266 IR Hub\",\"sw\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, FIRMWARE_VERSION) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "}}")) return false;
  return offset > 0U && mqttClient.publish(discoveryTopic, mqttPayloadBuffer, true);
}

bool mqttPublishHaButtonDiscovery(uint32_t commandId, uint32_t deviceId,
                                  const char *deviceName, const char *commandName) {
  if (!mqttClient.connected() || commandId == 0U || deviceId == 0U ||
      deviceName == nullptr || commandName == nullptr ||
      deviceName[0] == '\0' || commandName[0] == '\0') return false;
  char uniqueId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char hubId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char childDeviceId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char entityId[MQTT_HA_ENTITY_ID_MAX_LENGTH + 1U] = {0};
  char discoveryTopic[MQTT_DISCOVERY_TOPIC_MAX_LENGTH + 1U] = {0};
  char commandTopic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  char availabilityTopic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildHaCommandUniqueId(commandId, uniqueId, sizeof(uniqueId));
  mqttBuildHaHubIdentifier(hubId, sizeof(hubId));
  mqttBuildHaChildDeviceIdentifier(deviceId, childDeviceId, sizeof(childDeviceId));
  if (uniqueId[0] == '\0' || hubId[0] == '\0' || childDeviceId[0] == '\0' ||
      !mqttBuildHaButtonEntityId(deviceName, commandName, entityId, sizeof(entityId)) ||
      !mqttBuildHaDiscoveryTopic("button", uniqueId, discoveryTopic, sizeof(discoveryTopic)) ||
      !mqttBuildCommandTopic(deviceName, commandName, commandTopic, sizeof(commandTopic))) return false;
  mqttBuildSystemTopic("availability", availabilityTopic, sizeof(availabilityTopic));
  if (availabilityTopic[0] == '\0') return false;
  size_t offset = 0U;
  mqttPayloadBuffer[0] = '\0';
  if (!mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "{\"name\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, commandName) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"cmd_t\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, commandTopic) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"pl_prs\":\"1\",\"avty_t\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, availabilityTopic) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"uniq_id\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, uniqueId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"def_ent_id\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, entityId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset,
          ",\"o\":{\"name\":\"ESP8266 Universal IR Hub\",\"sw\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, FIRMWARE_VERSION) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "},\"dev\":{\"ids\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, childDeviceId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"name\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, deviceName) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, ",\"via_device\":") ||
      !mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, hubId) ||
      !mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), offset, "}}")) return false;
  return offset > 0U && mqttClient.publish(discoveryTopic, mqttPayloadBuffer, true);
}

bool mqttRemoveHaButtonDiscovery(uint32_t commandId) {
  if (!mqttClient.connected() || commandId == 0U) return false;
  char uniqueId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char discoveryTopic[MQTT_DISCOVERY_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildHaCommandUniqueId(commandId, uniqueId, sizeof(uniqueId));
  if (uniqueId[0] == '\0' ||
      !mqttBuildHaDiscoveryTopic("button", uniqueId, discoveryTopic, sizeof(discoveryTopic))) return false;
  return mqttClient.publish(discoveryTopic, "", true);
}

bool mqttRemoveHaTemperatureDiscovery() {
  if (!mqttClient.connected()) return false;
  char uniqueId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};
  char discoveryTopic[MQTT_DISCOVERY_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildHaTemperatureUniqueId(uniqueId, sizeof(uniqueId));
  if (uniqueId[0] == '\0' ||
      !mqttBuildHaDiscoveryTopic("sensor", uniqueId, discoveryTopic, sizeof(discoveryTopic))) return false;
  return mqttClient.publish(discoveryTopic, "", true);
}

bool mqttHaDiscoverySetEnabled(bool enabled) {
  if (haDiscoveryEnabled == enabled) return true;
  const bool previous = haDiscoveryEnabled;
  haDiscoveryEnabled = enabled;
  if (!configurationSaveHa()) {
    haDiscoveryEnabled = previous;
    Serial.println(F("ERROR: HA discovery setting could not be persisted."));
    return false;
  }
  if (!enabled) {
    mqttDiscoverySyncPending = false;
    mqttDiscoverySyncActive = false;
    mqttHaDiscoveryCleanupPending = true;
    if (mqttClient.connected()) {
      const bool commandsCleared = mqttDiscoveryClearAllCommandConfigs();
      const bool temperatureCleared = mqttRemoveHaTemperatureDiscovery();
      if (commandsCleared && temperatureCleared) mqttHaDiscoveryCleanupPending = false;
    }
  } else {
    mqttHaDiscoveryCleanupPending = false;
    mqttRequestDiscoveryRefresh();
  }
  return true;
}

bool mqttDiscoveryClearAllCommandConfigs() {
  if (!mqttClient.connected() || !storage.detected ||
      storage.sectorCount <= DB_DATA_START_SECTOR) return false;
  bool allPublished = true;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header) ||
        storageIsRecordHeaderBlank(header) || header.type != RECORD_TYPE_COMMAND ||
        header.recordId == 0U || !storageValidateRecordHeaderIntegrity(header)) continue;
    uint32_t commit = 0U;
    if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
    if (!mqttRemoveHaButtonDiscovery(header.recordId)) allPublished = false;
  }
  return allPublished;
}

bool mqttDiscoveryScanCurrentDatabaseOneStep() {
  if (!mqttClient.connected() || !storage.initialized) return false;
  while (mqttDiscoveryScanSector < storage.sectorCount) {
    const uint32_t sector = mqttDiscoveryScanSector++;
    feedWatchdog();
    if (mqttDiscoveryRetrySector != sector) {
      mqttDiscoveryRetrySector = sector;
      mqttDiscoverySectorRetryCount = 0U;
    }
    if (storageIsSectorQuarantined(sector)) {
      mqttDiscoveryRetrySector = UINT32_MAX;
      mqttDiscoverySectorRetryCount = 0U;
      continue;
    }
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header) ||
        storageIsRecordHeaderBlank(header) ||
        header.generation != storage.generation ||
        header.type != RECORD_TYPE_COMMAND ||
        header.recordId == 0U || !storageValidateRecordHeader(header)) continue;
    uint32_t commit = 0U;
    if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
    if (storageIsCommandDeleted(header.recordId) || storageIsDeviceDeleted(header.deviceId) ||
        storageIsCommandIdentityConflicted(header.recordId) ||
        storageIsDeviceIdentityConflicted(header.deviceId)) {
      if (!mqttRemoveHaButtonDiscovery(header.recordId)) {
        if (mqttDiscoverySectorRetryCount < MQTT_DISCOVERY_MAX_SECTOR_RETRIES) {
          ++mqttDiscoverySectorRetryCount; --mqttDiscoveryScanSector; return false;
        }
        mqttDiscoveryRetrySector = UINT32_MAX;
        mqttDiscoverySectorRetryCount = 0U;
        continue;
      }
      mqttDiscoveryRetrySector = UINT32_MAX;
      mqttDiscoverySectorRetryCount = 0U;
      return true;
    }
    if (header.nameLength == 0U || header.nameLength > STORAGE_MAX_NAME) continue;
    uint8_t commandNameBytes[STORAGE_MAX_NAME + 1U] = {0};
    const uint32_t nameAddress = sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader);
    if (!flashReadSmallConfirmed(nameAddress, commandNameBytes, header.nameLength)) continue;
    commandNameBytes[header.nameLength] = '\0';
    char deviceName[STORAGE_MAX_NAME + 1U] = {0};
    if (!storageFindDeviceName(header.deviceId, deviceName, sizeof(deviceName))) {
      if (!mqttRemoveHaButtonDiscovery(header.recordId)) {
        if (mqttDiscoverySectorRetryCount < MQTT_DISCOVERY_MAX_SECTOR_RETRIES) {
          ++mqttDiscoverySectorRetryCount; --mqttDiscoveryScanSector; return false;
        }
        mqttDiscoveryRetrySector = UINT32_MAX;
        mqttDiscoverySectorRetryCount = 0U;
        continue;
      }
      mqttDiscoveryRetrySector = UINT32_MAX;
      mqttDiscoverySectorRetryCount = 0U;
      return true;
    }
    const bool published = mqttPublishHaButtonDiscovery(header.recordId, header.deviceId,
        deviceName, reinterpret_cast<const char *>(commandNameBytes));
    if (!published) {
      if (mqttDiscoverySectorRetryCount < MQTT_DISCOVERY_MAX_SECTOR_RETRIES) {
        ++mqttDiscoverySectorRetryCount; --mqttDiscoveryScanSector; return false;
      }
      mqttDiscoveryRetrySector = UINT32_MAX;
      mqttDiscoverySectorRetryCount = 0U;
      continue;
    }
    mqttDiscoveryRetrySector = UINT32_MAX;
    mqttDiscoverySectorRetryCount = 0U;
    return true;
  }
  return false;
}

void mqttStartDiscoverySync() {
  mqttDiscoverySyncActive = true;
  mqttDiscoverySyncPending = false;
  mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
  mqttDiscoveryTemperaturePublished = false;
  mqttDiscoveryRetrySector = UINT32_MAX;
  mqttDiscoverySectorRetryCount = 0U;

  // ── v1.0: clear orphaned discovery configs ────────────────
  // If a command was deleted while MQTT was disconnected, the
  // mqttRemoveHaButtonDiscovery() call inside storageDeleteCommand()
  // was silently dropped. The retained config for that command still
  // lives on the broker, and HA keeps showing a ghost entity.
  //
  // The scan below only walks sectors that still hold a valid
  // RECORD_TYPE_COMMAND. If a command's record was overwritten
  // (rename/relearn) or erased (reset/wipe) between the delete and
  // the next scan, the scan never sees its ID. So we sweep the
  // persistent deleted-command bitmap and re-emit the removals.
  if (mqttClient.connected() && storage.initialized) {
    const uint32_t maxId = storage.maxCommandId ? storage.maxCommandId : 0U;
    for (uint32_t id = 1U; id <= maxId; ++id) {
      if (!storageIsCommandDeleted(id)) continue;
      if (!mqttRemoveHaButtonDiscovery(id)) break;   // MQTT hiccup; retry next scan
      yield();
    }
  }
}

void mqttClearAllDiscoveryRetained() {
  if (!mqttClient.connected()) return;
  char topic[MQTT_DISCOVERY_TOPIC_MAX_LENGTH + 1U] = {0};
  char uniqueId[MQTT_HA_UNIQUE_ID_MAX_LENGTH] = {0};

  mqttBuildHaTemperatureUniqueId(uniqueId, sizeof(uniqueId));
  if (uniqueId[0] != '\0' &&
      mqttBuildHaDiscoveryTopic("sensor", uniqueId, topic, sizeof(topic))) {
    (void)mqttClient.publish(topic, "", true);
    yield();
  }
  for (uint32_t id = 1U; id <= storage.maxCommandId; ++id) {
    mqttBuildHaCommandUniqueId(id, uniqueId, sizeof(uniqueId));
    if (uniqueId[0] == '\0') continue;
    if (!mqttBuildHaDiscoveryTopic("button", uniqueId, topic, sizeof(topic))) continue;
    if (!mqttClient.publish(topic, "", true)) break;
    yield();
  }
}

void mqttClearAlexaCatalogRetained() {
  if (!mqttClient.connected()) return;
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  char leaf[24] = {0};
  mqttBuildSystemTopic("alexa/sync", topic, sizeof(topic));
  if (topic[0] != '\0') (void)mqttClient.publish(topic, "", true);
  for (uint32_t id = 1U; id <= CONFIG_DEVCLOUD_MAX_ID; ++id) {
    snprintf(leaf, sizeof(leaf), "alexa/device/%lu", (unsigned long)id);
    mqttBuildSystemTopic(leaf, topic, sizeof(topic));
    if (topic[0] == '\0') break;
    if (!mqttClient.publish(topic, "", true)) break;
    yield();
  }
}

// Defined here (rather than next to mqttServiceAlexaCatalog() further down)
// because mqttSetDiscoveryMode() below sets it, and the compiler needs the
// declaration in scope first.
static bool mqttAlexaCatalogForce = true;


void mqttSetDiscoveryMode(DiscoveryMode newMode) {
  if (newMode == discoveryMode) return;
  mqttClearAllDiscoveryRetained();
  if (discoveryMode == DiscoveryMode::CUSTOM) mqttClearAlexaCatalogRetained();

  char oldBirth[MQTT_BASE_TOPIC_MAX_LENGTH + 24U] = {0};
  snprintf(oldBirth, sizeof(oldBirth), "%s/birth", mqttHaDiscoveryRoot());
  discoveryMode = newMode;
  char newBirth[MQTT_BASE_TOPIC_MAX_LENGTH + 24U] = {0};
  snprintf(newBirth, sizeof(newBirth), "%s/birth", mqttHaDiscoveryRoot());

  (void)configurationSaveSystem();
  if (mqttClient.connected()) {
    (void)mqttClient.unsubscribe(oldBirth);
    (void)mqttClient.subscribe(newBirth, 0);
  }
  mqttDiscoverySyncActive = false;
  mqttDiscoverySyncPending = false;
  mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
  mqttDiscoveryTemperaturePublished = false;
  mqttHaDiscoveryCleanupPending = false;
  mqttAlexaCatalogForce = true;
  mqttRequestDiscoveryRefresh();

  Serial.print(F("Discovery mode: "));
  Serial.println(newMode == DiscoveryMode::NATIVE_HA ? F("NATIVE HA") : F("CUSTOM"));
}


void mqttServiceDiscovery() {
  if (!mqttClient.connected() || irState != IrEngineState::IDLE) return;
  if (!haDiscoveryEnabled) {
    mqttDiscoverySyncPending = false;
    mqttDiscoverySyncActive = false;
    if (mqttHaDiscoveryCleanupPending) {
      const bool commandsCleared = mqttDiscoveryClearAllCommandConfigs();
      const bool temperatureCleared = mqttRemoveHaTemperatureDiscovery();
      if (commandsCleared && temperatureCleared) mqttHaDiscoveryCleanupPending = false;
    }
    return;
  }
  if (!mqttDiscoverySyncActive) {
    if (!mqttDiscoverySyncPending) return;
    mqttStartDiscoverySync();
  }
  if (!mqttDiscoveryTemperaturePublished) {
    if (mqttPublishHaTemperatureDiscovery()) mqttDiscoveryTemperaturePublished = true;
    return;
  }
  if (mqttDiscoveryScanCurrentDatabaseOneStep()) return;
  mqttDiscoverySyncActive = false;
#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) Serial.println(F("Home Assistant MQTT discovery synchronization complete."));
#endif
}

bool mqttQueueCommand(const char *deviceSegment, const char *commandSegment) {
  if (deviceSegment == nullptr || commandSegment == nullptr ||
      mqttCommandQueueCount >= MQTT_COMMAND_QUEUE_SIZE) return false;
  MqttCommandQueueEntry &entry = mqttCommandQueue[mqttCommandQueueTail];
  strncpy(entry.deviceSegment, deviceSegment, sizeof(entry.deviceSegment) - 1U);
  entry.deviceSegment[sizeof(entry.deviceSegment) - 1U] = '\0';
  strncpy(entry.commandSegment, commandSegment, sizeof(entry.commandSegment) - 1U);
  entry.commandSegment[sizeof(entry.commandSegment) - 1U] = '\0';
  mqttCommandQueueTail = static_cast<uint8_t>((mqttCommandQueueTail + 1U) % MQTT_COMMAND_QUEUE_SIZE);
  mqttCommandQueueCount++;
  return true;
}

bool mqttPopCommand(MqttCommandQueueEntry &entry) {
  if (mqttCommandQueueCount == 0U) return false;
  entry = mqttCommandQueue[mqttCommandQueueHead];
  mqttCommandQueueHead = static_cast<uint8_t>((mqttCommandQueueHead + 1U) % MQTT_COMMAND_QUEUE_SIZE);
  mqttCommandQueueCount--;
  return true;
}

bool mqttAlexaHandleDirective(const char *topic, const uint8_t *payload, unsigned int length);

void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (topic == nullptr || payload == nullptr || length == 0U) return;
  if (mqttAlexaHandleDirective(topic, payload, length)) return;
  char haBirthTopic[MQTT_BASE_TOPIC_MAX_LENGTH + 24U] = {0};
  snprintf(haBirthTopic, sizeof(haBirthTopic), "%s/birth", mqttHaDiscoveryRoot());
  if (strcmp(topic, haBirthTopic) == 0) {
    if (length == 6U && memcmp(payload, "online", 6U) == 0) mqttRequestDiscoveryRefresh();
    return;
  }
  if (irState != IrEngineState::IDLE) return;
  char deviceSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  char commandSegment[MQTT_TOPIC_SEGMENT_MAX_LENGTH + 1U] = {0};
  if (!mqttExtractCommandTopic(topic, deviceSegment, sizeof(deviceSegment),
                               commandSegment, sizeof(commandSegment))) return;
  if (strcmp(deviceSegment, "_system") == 0) return;
  if (length != 1U || payload[0] != '1') return;
  if (!mqttQueueCommand(deviceSegment, commandSegment)) {
    mqttCommandQueueOverflowPending = true;
    return;
  }
}

bool storageFindCommandByMqttTopic(const char *deviceSegment, const char *commandSegment,
                                   uint32_t &deviceIdOut, uint32_t &commandIdOut,
                                   uint32_t &commandSectorOut) {
  deviceIdOut = 0U; commandIdOut = 0U; commandSectorOut = UINT32_MAX;
  if (!storage.initialized || deviceSegment == nullptr || commandSegment == nullptr ||
      deviceSegment[0] == '\0' || commandSegment[0] == '\0') return false;
  bool deviceFound = false;
  uint32_t matchedDeviceId = 0U;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header)) continue;
    if (storageIsRecordHeaderBlank(header) || header.magic != RECORD_MAGIC ||
        header.formatVersion != RECORD_FORMAT_VERSION ||
        header.generation != storage.generation ||
        !storageValidateRecordHeader(header)) continue;
    if ((header.type == RECORD_TYPE_DEVICE && storageIsDeviceDeleted(header.recordId)) ||
        (header.type == RECORD_TYPE_COMMAND && (storageIsCommandDeleted(header.recordId) ||
         storageIsDeviceDeleted(header.deviceId)))) continue;
    uint32_t commit = 0U;
    if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
    if (header.nameLength == 0U || header.nameLength > STORAGE_MAX_NAME) continue;
    const uint32_t nameAddress = sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader);
    uint8_t nameBytes[STORAGE_MAX_NAME + 1U] = {0};
    if (!flashReadSmallConfirmed(nameAddress, nameBytes, static_cast<uint16_t>(header.nameLength))) continue;
    nameBytes[header.nameLength] = '\0';
    if (header.type == RECORD_TYPE_DEVICE) {
      if (header.recordId == 0U || header.deviceId != header.recordId ||
          !mqttTopicSegmentMatchesName(deviceSegment, reinterpret_cast<const char *>(nameBytes))) continue;
      deviceFound = true;
      matchedDeviceId = header.recordId;
      continue;
    }
    if (header.type != RECORD_TYPE_COMMAND || !deviceFound ||
        header.deviceId != matchedDeviceId || header.recordId == 0U ||
        !mqttTopicSegmentMatchesName(commandSegment, reinterpret_cast<const char *>(nameBytes))) continue;
    deviceIdOut = matchedDeviceId;
    commandIdOut = header.recordId;
    commandSectorOut = sector;
    return true;
  }
  return false;
}

void mqttFormatCentiDegrees(int32_t centiDegrees, char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  const bool negative = centiDegrees < 0;
  const int32_t absoluteValue = negative ? -centiDegrees : centiDegrees;
  const int32_t whole = absoluteValue / 100L;
  const int32_t fraction = absoluteValue % 100L;
  snprintf(destination, capacity, "%s%ld.%02ld", negative ? "-" : "",
           static_cast<long>(whole), static_cast<long>(fraction));
}

bool mqttPublishAvailability(bool online) {
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildSystemTopic("availability", topic, sizeof(topic));
  if (topic[0] == '\0' || !mqttClient.connected()) return false;
  return mqttClient.publish(topic, online ? "online" : "offline", true);
}

bool mqttPublishTemperature() {
  if (!mqttClient.connected()) return false;
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildSystemTopic("temperature", topic, sizeof(topic));
  if (topic[0] == '\0') return false;
  bool published = false;
  if (ds18b20IsEnabled() && ds18b20HasValidReading()) {
    char value[24] = {0};
    mqttFormatCentiDegrees(ds18b20GetCalibratedCentiDegrees(), value, sizeof(value));
    published = mqttClient.publish(topic, value, true);
    if (published) {
      mqttLastPublishedTemperatureSequence = ds18b20GetSampleSequence();
      mqttLastPublishedTemperatureCentiDegrees = ds18b20GetCalibratedCentiDegrees();
      mqttLastPublishedTemperatureValid = true;
      mqttLastPublishedTemperatureEnabled = true;
    }
  } else {
    published = mqttClient.publish(topic, "", true);
    if (published) {
      mqttLastPublishedTemperatureSequence = ds18b20GetSampleSequence();
      mqttLastPublishedTemperatureCentiDegrees = INT32_MIN;
      mqttLastPublishedTemperatureValid = false;
      mqttLastPublishedTemperatureEnabled = ds18b20IsEnabled();
    }
  }
  return published;
}

void mqttPublishTemperatureIfChanged() {
  if (!mqttClient.connected()) return;
  const bool enabled = ds18b20IsEnabled();
  const bool valid = ds18b20HasValidReading();
  const uint32_t sequence = ds18b20GetSampleSequence();
  const int32_t temperature = ds18b20GetCalibratedCentiDegrees();
  const bool changed = enabled != mqttLastPublishedTemperatureEnabled ||
                       valid != mqttLastPublishedTemperatureValid ||
                       (valid && sequence != mqttLastPublishedTemperatureSequence) ||
                       (valid && temperature != mqttLastPublishedTemperatureCentiDegrees);
  if (changed) (void)mqttPublishTemperature();
}

bool mqttPublishTelemetry() {
  if (!mqttClient.connected()) return false;
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildSystemTopic("telemetry", topic, sizeof(topic));
  if (topic[0] == '\0') return false;
  char temperatureJson[192] = {0};
  if (ds18b20IsEnabled() && ds18b20HasValidReading()) {
    char tempValue[24] = {0};
    mqttFormatCentiDegrees(ds18b20GetCalibratedCentiDegrees(), tempValue, sizeof(tempValue));
    char calibrationValue[24] = {0};
    mqttFormatCentiDegrees(static_cast<int32_t>(ds18b20GetCalibrationCentiDegrees()),
                           calibrationValue, sizeof(calibrationValue));
    snprintf(temperatureJson, sizeof(temperatureJson),
        "{\"Enabled\":true,\"Connected\":%s,\"Celsius\":%s,\"Calibration\":%s,\"UpdateInterval\":%lu}",
        ds18b20IsConnected() ? "true" : "false", tempValue, calibrationValue,
        static_cast<unsigned long>(ds18b20GetSampleIntervalMs() / 1000UL));
  } else {
    const int16_t calibration = ds18b20GetCalibrationCentiDegrees();
    const int32_t signedCalibration = static_cast<int32_t>(calibration);
    const int32_t absCalibration = signedCalibration < 0 ? -signedCalibration : signedCalibration;
    snprintf(temperatureJson, sizeof(temperatureJson),
        "{\"Enabled\":%s,\"Connected\":%s,\"Celsius\":null,\"Calibration\":%s%d.%02d,\"UpdateInterval\":%lu}",
        ds18b20IsEnabled() ? "true" : "false", ds18b20IsConnected() ? "true" : "false",
        signedCalibration < 0 ? "-" : "",
        static_cast<int>(absCalibration / 100L), static_cast<int>(absCalibration % 100L),
        static_cast<unsigned long>(ds18b20GetSampleIntervalMs() / 1000UL));
  }
  const IPAddress ip = WiFi.localIP();
  const int written = snprintf(mqttPayloadBuffer, sizeof(mqttPayloadBuffer),
      "{\"Firmware\":\"%s\",\"Uptime\":%lu,\"Heap\":%lu,\"WiFi\":{\"Connected\":%s,\"RSSI\":%ld,\"IP\":\"%u.%u.%u.%u\"},\"MQTT\":{\"Connected\":%s},\"Flash\":{\"Detected\":%s,\"Capacity\":%lu},\"Database\":{\"Initialized\":%s,\"Generation\":%lu,\"ValidRecords\":%lu,\"UsedSectors\":%lu,\"FreeSectors\":%lu},\"Temperature\":%s,\"IR\":{\"State\":\"%s\"}}",
      FIRMWARE_VERSION,
      static_cast<unsigned long>(millis() / 1000UL),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      wifiIsConnected() ? "true" : "false",
      static_cast<long>(wifiGetRSSI()),
      ip[0], ip[1], ip[2], ip[3],
      mqttClient.connected() ? "true" : "false",
      storage.detected ? "true" : "false",
      static_cast<unsigned long>(storage.flashSizeBytes),
      storage.initialized ? "true" : "false",
      static_cast<unsigned long>(storage.generation),
      static_cast<unsigned long>(storage.validRecordCount),
      static_cast<unsigned long>(storage.usedSectorCount),
      static_cast<unsigned long>(storage.freeSectorCount),
      temperatureJson,
      irState == IrEngineState::LEARNING ? "LEARNING" :
      (irState == IrEngineState::TRANSMITTING ? "TRANSMITTING" : "IDLE"));
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(mqttPayloadBuffer)) return false;
  return mqttClient.publish(topic, mqttPayloadBuffer, false);
}

bool mqttPublishStatusEvent(const char *eventName, const char *deviceName,
                            const char *commandName, uint32_t deviceId, uint32_t commandId) {
  if (!mqttClient.connected() || eventName == nullptr) return false;
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildSystemTopic("status", topic, sizeof(topic));
  if (topic[0] == '\0') return false;
  const int written = snprintf(mqttPayloadBuffer, sizeof(mqttPayloadBuffer),
      "{\"Event\":\"%s\",\"DeviceID\":%lu,\"CommandID\":%lu,\"Device\":\"%s\",\"Command\":\"%s\"}",
      eventName, static_cast<unsigned long>(deviceId), static_cast<unsigned long>(commandId),
      deviceName != nullptr ? deviceName : "", commandName != nullptr ? commandName : "");
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(mqttPayloadBuffer)) return false;
  return mqttClient.publish(topic, mqttPayloadBuffer, false);
}

void mqttProcessOneQueuedCommand() {
  if (irState != IrEngineState::IDLE || mqttCommandQueueCount == 0U) return;
  MqttCommandQueueEntry entry{};
  if (!mqttPopCommand(entry)) return;
  uint32_t deviceId = 0U;
  uint32_t commandId = 0U;
  uint32_t commandSector = UINT32_MAX;
  if (!storageFindCommandByMqttTopic(entry.deviceSegment, entry.commandSegment,
                                     deviceId, commandId, commandSector)) {
    (void)mqttPublishStatusEvent("COMMAND_NOT_FOUND", nullptr, nullptr, 0U, 0U);
    return;
  }
  if (commandSector == UINT32_MAX ||
      !storageLoadCommandFromSector(commandSector, commandId)) {
    (void)mqttPublishStatusEvent("COMMAND_LOAD_ERROR", nullptr, nullptr, deviceId, commandId);
    return;
  }
  char deviceName[STORAGE_MAX_NAME + 1U] = {0};
  char commandName[STORAGE_MAX_NAME + 1U] = {0};
  if (!storageFindDeviceName(deviceId, deviceName, sizeof(deviceName))) deviceName[0] = '\0';
  strncpy(commandName, storage.currentCommandName, STORAGE_MAX_NAME);
  commandName[STORAGE_MAX_NAME] = '\0';
  if (storage.currentDeviceId != deviceId) {
    (void)mqttPublishStatusEvent("COMMAND_DEVICE_MISMATCH", deviceName, commandName, deviceId, commandId);
    return;
  }
  const bool transmitted = transmitLastCapture();
  if (transmitted) {
    statusLedPulseTransmit();
    (void)mqttPublishStatusEvent("IR_TX", deviceName, commandName, deviceId, commandId);
  } else {
    (void)mqttPublishStatusEvent("IR_TX_ERROR", deviceName, commandName, deviceId, commandId);
  }
}

bool mqttSetEnabled(bool enabled) { mqttConfig.enabled = enabled; mqttConfigurationDirty = true; return true; }
bool mqttSetBroker(const char *broker) {
  if (!mqttCopyField(mqttConfig.broker, sizeof(mqttConfig.broker), broker, false)) return false;
  mqttConfigurationDirty = true; return true;
}
bool mqttSetPort(uint16_t port) { if (port == 0U) return false; mqttConfig.port = port; mqttConfigurationDirty = true; return true; }
bool mqttSetUsername(const char *username) {
  if (!mqttCopyField(mqttConfig.username, sizeof(mqttConfig.username), username, true)) return false;
  mqttConfigurationDirty = true; return true;
}
bool mqttSetPassword(const char *password) {
  if (!mqttCopyField(mqttConfig.password, sizeof(mqttConfig.password), password, true)) return false;
  mqttConfigurationDirty = true; return true;
}
bool mqttSetClientId(const char *clientId) {
  if (!mqttCopyField(mqttConfig.clientId, sizeof(mqttConfig.clientId), clientId, false)) return false;
  mqttConfigurationDirty = true; return true;
}
bool mqttSetBaseTopic(const char *baseTopic) {
  if (!mqttTopicIsValid(baseTopic) || strlen(baseTopic) > MQTT_BASE_TOPIC_MAX_LENGTH) return false;
  memcpy(mqttConfig.baseTopic, baseTopic, strlen(baseTopic) + 1U);
  mqttConfigurationDirty = true; return true;
}

bool mqttApplyConfiguration() {
  if (irState != IrEngineState::IDLE) return false;
  if (mqttConfig.enabled &&
      (mqttConfig.broker[0] == '\0' || mqttConfig.baseTopic[0] == '\0')) return false;
  if (!configurationSaveMqtt()) {
    Serial.println(F("WARNING: MQTT configuration applied in RAM but could not be persisted."));
  }
  if (mqttClient.connected()) {
    (void)mqttPublishAvailability(false);
    mqttClient.disconnect();
  }
  mqttConfigurationDirty = false;
  mqttCommandQueueHead = 0U; mqttCommandQueueTail = 0U; mqttCommandQueueCount = 0U;
  mqttCommandQueueOverflowPending = false;
  mqttLastTelemetryMs = 0UL;
  mqttTelemetryImmediatePending = true;
  mqttLastPublishedTemperatureSequence = UINT32_MAX;
  mqttLastPublishedTemperatureCentiDegrees = INT32_MIN;
  mqttLastPublishedTemperatureValid = false;
  mqttLastPublishedTemperatureEnabled = false;
  mqttDiscoverySyncActive = false;
  mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
  mqttDiscoveryTemperaturePublished = false;
  if (!mqttConfig.enabled) { mqttState = MqttState::DISABLED; statusLedSetMqttError(false); return true; }
  if (mqttConfig.broker[0] == '\0' || mqttConfig.baseTopic[0] == '\0') {
    mqttState = MqttState::UNCONFIGURED; statusLedSetMqttError(false); return false;
  }
  mqttClient.setServer(mqttConfig.broker, mqttConfig.port);
  mqttState = wifiIsConnected() ? MqttState::DISCONNECTED : MqttState::WAITING_WIFI;
  mqttNextReconnectMs = 0UL;
  statusLedSetMqttError(false);
  mqttRequestDiscoveryRefresh();
  return true;
}

void mqttInitialize() {
  mqttConfig = MqttConfiguration{};
  mqttConfig.enabled = true;
  mqttConfig.port = 1883U;
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  snprintf(mqttConfig.clientId, sizeof(mqttConfig.clientId), "ir-hub-%06lX", chipId);
  snprintf(mqttConfig.baseTopic, sizeof(mqttConfig.baseTopic), "irhub/%06lX", chipId);
  (void)configurationLoadMqtt();
  (void)configurationLoadHa();
  mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
  mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
  mqttClient.setCallback(mqttCallback);
  mqttConfigurationDirty = false;
  mqttState = MqttState::UNCONFIGURED;
  mqttNextReconnectMs = 0UL;
  mqttLastTelemetryMs = 0UL;
  mqttTelemetryImmediatePending = false;
  mqttCommandQueueHead = 0U; mqttCommandQueueTail = 0U; mqttCommandQueueCount = 0U;
  mqttLastPublishedTemperatureSequence = UINT32_MAX;
  mqttLastPublishedTemperatureCentiDegrees = INT32_MIN;
  mqttLastPublishedTemperatureValid = false;
  mqttLastPublishedTemperatureEnabled = false;
  mqttDiscoverySyncPending = false;
  mqttDiscoverySyncActive = false;
  mqttHaDiscoveryCleanupPending = false;
  mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
  mqttDiscoveryTemperaturePublished = false;
  statusLedSetMqttError(false);
}

void mqttServiceTemperature() { mqttPublishTemperatureIfChanged(); }

void mqttServiceTelemetry() {
  const uint32_t now = millis();
  if (mqttTelemetryImmediatePending || (now - mqttLastTelemetryMs) >= mqttTelemetryIntervalMs) {
    if (mqttPublishTelemetry()) {
      mqttLastTelemetryMs = now;
      mqttTelemetryImmediatePending = false;
    }
  }
}

void mqttServiceEvents() {
  if (mqttCommandQueueOverflowPending) {
    mqttCommandQueueOverflowPending = false;
    (void)mqttPublishStatusEvent("COMMAND_QUEUE_OVERFLOW", nullptr, nullptr, 0U, 0U);
  }
}

// ── Alexa device catalogue -> local MQTT (for Node-RED) ──────
// Retained message per device:  <base>/_system/alexa/device/<id>
//   {"id":"A1B2_1","name":"Red King","type":"TV","sig":"1A2B3C4D"}
// Then one retained marker:     <base>/_system/alexa/sync
//   {"sig":"1A2B3C4D","count":3}
// sig changes whenever the exposed device list changes. Only devices with a
// category AND Alexa Cloud ON are published. One message per service pass.
constexpr uint32_t MQTT_ALEXA_CATALOG_CHECK_MS = 5000UL;
constexpr uint8_t  MQTT_ALEXA_MAX_RETRIES = 5U;
static uint32_t mqttAlexaCatalogLastCheckMs = 0UL;
static uint32_t mqttAlexaCatalogSyncedSig = 0UL;
static bool     mqttAlexaSendActive = false;
static bool     mqttAlexaSendSkipped = false;
static uint32_t mqttAlexaSendSig = 0UL;
static uint32_t mqttAlexaSendNextId = 1UL;
static uint16_t mqttAlexaSendCount = 0U;
static uint8_t  mqttAlexaSendRetries = 0U;

bool mqttAlexaGetDevice(uint32_t id, char *name, size_t nameCapacity, const char *&typeOut) {
  uint8_t category = DEVICE_CATEGORY_NONE;
  bool cloud = false;
  if (!storageGetDeviceCloud(id, category, cloud) || !cloud || category == DEVICE_CATEGORY_NONE) return false;
  if (!storageFindDeviceName(id, name, nameCapacity)) return false;
  typeOut = deviceCategoryName(category);
  return true;
}

uint32_t mqttAlexaFnv(uint32_t hash, const char *text) {
  while (*text) { hash ^= static_cast<uint8_t>(*text++); hash *= 16777619UL; }
  return hash;
}

uint32_t mqttAlexaComputeSignature() {
  uint32_t hash = 2166136261UL;
  for (uint32_t id = 1U; id <= CONFIG_DEVCLOUD_MAX_ID; ++id) {
    char name[STORAGE_MAX_NAME + 1U] = {0};
    const char *type = "";
    if (!mqttAlexaGetDevice(id, name, sizeof(name), type)) continue;
    char idText[16] = {0};
    snprintf(idText, sizeof(idText), "%lu|", static_cast<unsigned long>(id));
    hash = mqttAlexaFnv(hash, idText);
    hash = mqttAlexaFnv(hash, name);
    hash = mqttAlexaFnv(hash, "|");
    hash = mqttAlexaFnv(hash, type);
    hash = mqttAlexaFnv(hash, ";");
    feedWatchdog();
  }
  return hash;
}

void mqttServiceAlexaCatalog() {
  if (!mqttClient.connected() || !storage.initialized || irState != IrEngineState::IDLE) return;
  if (discoveryMode != DiscoveryMode::CUSTOM) return;
  const uint32_t now = millis();

  if (!mqttAlexaSendActive) {
    const uint32_t interval = mqttAlexaCatalogForce ? 1000UL : MQTT_ALEXA_CATALOG_CHECK_MS;
    if ((now - mqttAlexaCatalogLastCheckMs) < interval) return;
    mqttAlexaCatalogLastCheckMs = now;
    const uint32_t sig = mqttAlexaComputeSignature();
    if (!mqttAlexaCatalogForce && sig == mqttAlexaCatalogSyncedSig) return;
    mqttAlexaSendSig = sig;
    mqttAlexaSendNextId = 1UL;
    mqttAlexaSendCount = 0U;
    mqttAlexaSendRetries = 0U;
    mqttAlexaSendSkipped = false;
    mqttAlexaSendActive = true;
    return;
  }

  char sigText[9] = {0};
  snprintf(sigText, sizeof(sigText), "%08lX", static_cast<unsigned long>(mqttAlexaSendSig));
  char topic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};

  // Publish the next qualifying device (one per pass, never blocks the loop).
  while (mqttAlexaSendNextId <= CONFIG_DEVCLOUD_MAX_ID) {
    const uint32_t id = mqttAlexaSendNextId;
    char name[STORAGE_MAX_NAME + 1U] = {0};
    const char *type = "";
    if (!mqttAlexaGetDevice(id, name, sizeof(name), type)) { ++mqttAlexaSendNextId; continue; }

    char head[48] = {0};
    snprintf(head, sizeof(head), "{\"id\":\"%04X_%lu\",\"name\":",
             static_cast<unsigned int>(ESP.getChipId() & 0xFFFFUL), static_cast<unsigned long>(id));
    size_t off = 0U;
    mqttPayloadBuffer[0] = '\0';
    const bool built =
        mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, head) &&
        mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, name) &&
        mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, ",\"type\":") &&
        mqttJsonAppendString(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, type) &&
        mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, ",\"sig\":\"") &&
        mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, sigText) &&
        mqttJsonAppendRaw(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), off, "\"}");
    char leaf[24] = {0};
    snprintf(leaf, sizeof(leaf), "alexa/device/%lu", static_cast<unsigned long>(id));
    mqttBuildSystemTopic(leaf, topic, sizeof(topic));

    if (built && topic[0] != '\0' && mqttClient.publish(topic, mqttPayloadBuffer, true)) {
      ++mqttAlexaSendCount;
      ++mqttAlexaSendNextId;
      mqttAlexaSendRetries = 0U;
    } else if (++mqttAlexaSendRetries >= MQTT_ALEXA_MAX_RETRIES) {
      DBG_PRINTLN(F("WARNING: Alexa device publish failed; skipping device."));
      mqttAlexaSendSkipped = true;
      ++mqttAlexaSendNextId;
      mqttAlexaSendRetries = 0U;
    }
    return;
  }

  // All devices sent: publish the sync marker.
  snprintf(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), "{\"sig\":\"%s\",\"count\":%u}",
           sigText, static_cast<unsigned int>(mqttAlexaSendCount));
  mqttBuildSystemTopic("alexa/sync", topic, sizeof(topic));
  if (topic[0] != '\0' && mqttClient.publish(topic, mqttPayloadBuffer, true)) {
    mqttAlexaCatalogSyncedSig = mqttAlexaSendSkipped ? 0UL : mqttAlexaSendSig;
    mqttAlexaCatalogForce = false;
    mqttAlexaSendActive = false;
    DBG_PRINT(F("Alexa catalogue synced, devices: "));
    DBG_PRINTLN(mqttAlexaSendCount);
  } else if (++mqttAlexaSendRetries >= MQTT_ALEXA_MAX_RETRIES) {
    mqttAlexaCatalogForce = true;   // try the whole sync again later
    mqttAlexaSendActive = false;
  }
}

// ── Alexa directives -> IR presses ───────────────────────────
// Topic:   alexa/<tag>/directive      (tag = last 4 hex digits of chip ID)
// Payload: {"id":"<msgId>","dev":"4A03_1","do":"Volume Up*3"}
// "do": ';' = steps in order, '|' = alternatives (first learned button
// wins), '*N' = press N times. Names match learned command names,
// ignoring case, spaces, '_' and '-'.
constexpr uint8_t  MQTT_ALEXA_DIR_QUEUE_SIZE  = 4U;
constexpr size_t   MQTT_ALEXA_DO_MAX          = 100U;
constexpr size_t   MQTT_ALEXA_JSON_MAX        = 256U;
constexpr uint8_t  MQTT_ALEXA_MAX_REPEAT      = 50U;
constexpr uint32_t MQTT_ALEXA_REPEAT_GAP_MS   = 300UL;   // same button repeated
constexpr uint32_t MQTT_ALEXA_STEP_GAP_MS     = 450UL;   // between steps (digits)
constexpr uint32_t MQTT_ALEXA_END_GAP_MS      = 250UL;   // between directives

struct MqttAlexaDirective {
  uint32_t deviceId;
  char     text[MQTT_ALEXA_DO_MAX + 1U];
  bool     trusted;
};

static MqttAlexaDirective mqttAlexaQueue[MQTT_ALEXA_DIR_QUEUE_SIZE];
static uint8_t  mqttAlexaQueueHead = 0U;
static uint8_t  mqttAlexaQueueTail = 0U;
static uint8_t  mqttAlexaQueueCount = 0U;
static bool     mqttAlexaQueueFullPending = false;

static bool     mqttAlexaRunActive = false;
static MqttAlexaDirective mqttAlexaRun;
static size_t   mqttAlexaRunPos = 0U;
static bool     mqttAlexaRunMissing = false;
static uint32_t mqttAlexaPressCommandId = 0UL;
static uint32_t mqttAlexaPressSector = 0UL;
static uint8_t  mqttAlexaPressRemaining = 0U;
static uint32_t mqttAlexaNextMs = 0UL;
static uint32_t mqttAlexaRunStartMs = 0UL;

static bool mqttAlexaJsonString(const char *json, const char *key, char *out, size_t cap) {
  out[0] = '\0';
  char pattern[12] = {0};
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (p == nullptr) return false;
  p += strlen(pattern);
  while (*p == ' ' || *p == ':') ++p;
  if (*p != '"') return false;
  ++p;
  size_t n = 0U;
  while (*p != '\0' && *p != '"') {
    if (n + 1U >= cap) return false;
    out[n++] = *p++;
  }
  if (*p != '"') return false;
  out[n] = '\0';
  return true;
}

void mqttAlexaSubscribeDirective() {
  char topic[24] = {0};
  snprintf(topic, sizeof(topic), "alexa/%04X/directive",
           static_cast<unsigned int>(ESP.getChipId() & 0xFFFFUL));
  if (!mqttClient.subscribe(topic, 0)) {
    DBG_PRINTLN(F("WARNING: Alexa directive subscription failed."));
  }
}

// Called from mqttCallback. Only parses and queues; never transmits.
bool mqttAlexaHandleDirective(const char *topic, const uint8_t *payload, unsigned int length) {
  char expected[24] = {0};
  snprintf(expected, sizeof(expected), "alexa/%04X/directive",
           static_cast<unsigned int>(ESP.getChipId() & 0xFFFFUL));
  if (strcmp(topic, expected) != 0) return false;
  DBG_PRINTLN(F("Alexa directive: message received."));
  if (length == 0U || length >= MQTT_ALEXA_JSON_MAX) {
    DBG_PRINTLN(F("Alexa directive: payload empty or too long; ignored."));
    return true;
  }

  static char json[MQTT_ALEXA_JSON_MAX];
  memcpy(json, payload, length);
  json[length] = '\0';

  char msgId[48] = {0};
  char dev[24] = {0};
  char text[MQTT_ALEXA_DO_MAX + 1U] = {0};
  (void)mqttAlexaJsonString(json, "id", msgId, sizeof(msgId));
  if (!mqttAlexaJsonString(json, "dev", dev, sizeof(dev)) ||
      !mqttAlexaJsonString(json, "do", text, sizeof(text)) || text[0] == '\0') {
    DBG_PRINTLN(F("Alexa directive: missing dev or do; ignored."));
    return true;
  }

  char *underscore = strchr(dev, '_');
  if (underscore == nullptr) return true;
  *underscore = '\0';
  char tag[5] = {0};
  snprintf(tag, sizeof(tag), "%04X", static_cast<unsigned int>(ESP.getChipId() & 0xFFFFUL));
  if (strcasecmp(dev, tag) != 0) {
    DBG_PRINTLN(F("Alexa directive: device tag is not this hub; ignored."));
    return true;
  }
  char *end = nullptr;
  const unsigned long id = strtoul(underscore + 1, &end, 10);
  if (end == underscore + 1 || *end != '\0' || id == 0UL || id > CONFIG_DEVCLOUD_MAX_ID) return true;

  if (mqttAlexaQueueCount >= MQTT_ALEXA_DIR_QUEUE_SIZE) {
    DBG_PRINTLN(F("Alexa directive: queue full; dropped."));
    mqttAlexaQueueFullPending = true;
    return true;
  }
  MqttAlexaDirective &slot = mqttAlexaQueue[mqttAlexaQueueTail];
  slot.deviceId = static_cast<uint32_t>(id);
  memcpy(slot.text, text, sizeof(slot.text));
  slot.trusted = false;
  mqttAlexaQueueTail = static_cast<uint8_t>((mqttAlexaQueueTail + 1U) % MQTT_ALEXA_DIR_QUEUE_SIZE);
  mqttAlexaQueueCount++;
  return true;
}

// Names are compared after normalising: lower case, letters and digits only,
// and long forms shortened through the alias table below.
static void mqttAlexaNormalizeName(const char *in, char *out, size_t cap) {
  size_t n = 0U;
  for (; *in != '\0' && n + 1U < cap; ++in) {
    char c = *in;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
  }
  out[n] = '\0';

  // Alias table: { long form, short form }. Add more rows as needed.
  static const char *const ALIASES[][2] = {
    {"temperature", "temp"},
  };
  for (size_t i = 0U; i < sizeof(ALIASES) / sizeof(ALIASES[0]); ++i) {
    const size_t longLen = strlen(ALIASES[i][0]);
    const size_t shortLen = strlen(ALIASES[i][1]);
    char *hit;
    while ((hit = strstr(out, ALIASES[i][0])) != nullptr) {
      memcpy(hit, ALIASES[i][1], shortLen);
      memmove(hit + shortLen, hit + longLen, strlen(hit + longLen) + 1U);
    }
  }
}

static bool mqttAlexaNameEquals(const char *a, const char *b) {
  char na[STORAGE_MAX_NAME + 1U];
  char nb[STORAGE_MAX_NAME + 1U];
  mqttAlexaNormalizeName(a, na, sizeof(na));
  mqttAlexaNormalizeName(b, nb, sizeof(nb));
  return na[0] != '\0' && strcmp(na, nb) == 0;
}

// Finds a learned command on this device by name, using the RAM command index.
static bool mqttAlexaFindCommand(uint32_t deviceId, const char *name,
                                 uint32_t &commandIdOut, uint32_t &sectorOut) {
  for (uint16_t i = 0U; i < storageCommandIndexCount; ++i) {
    feedWatchdog();
    const CommandIndexEntry &entry = storageCommandIndex[i];
    if (entry.devId != deviceId) continue;
    const uint32_t sector = entry.sector;
    if (storageIsSectorQuarantined(sector) || storageIsCommandDeleted(entry.id)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header) || storageIsRecordHeaderBlank(header)) continue;
    if (header.magic != RECORD_MAGIC || header.type != RECORD_TYPE_COMMAND ||
        header.recordId != entry.id || header.deviceId != deviceId ||
        header.generation != storage.generation ||
        header.nameLength == 0U || header.nameLength > STORAGE_MAX_NAME) continue;
    uint8_t nameBytes[STORAGE_MAX_NAME + 1U] = {0};
    if (!flashReadSmallConfirmed(sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader),
                                 nameBytes, header.nameLength)) continue;
    nameBytes[header.nameLength] = '\0';
    if (header.carrierHz == CHANNEL_MARK_HZ && header.repeatGapMs == CHANNEL_MARK_GAP) continue;  // skip channel buttons
    if (!mqttAlexaNameEquals(name, reinterpret_cast<const char *>(nameBytes))) continue;
    commandIdOut = entry.id;
    sectorOut = sector;
    return true;
  }
  return false;
}
// "Speed 6" / "Fan Speed 6" -> "6". Returns false for anything else.
static bool mqttAlexaBareSpeedNumber(const char *name, char *out, size_t cap) {
  char norm[STORAGE_MAX_NAME + 1U];
  mqttAlexaNormalizeName(name, norm, sizeof(norm));
  const char *p = norm;
  if (strncmp(p, "fanspeed", 8) == 0) p += 8;
  else if (strncmp(p, "speed", 5) == 0) p += 5;
  else return false;
  if (*p == '\0' || strlen(p) >= cap) return false;
  for (const char *d = p; *d != '\0'; ++d) {
    if (*d < '0' || *d > '9') return false;
  }
  strcpy(out, p);
  return true;
}

// One step = alternatives separated by '|', each "name" or "name*N".
static bool mqttAlexaResolveStep(uint32_t deviceId, const char *p, const char *end,
                                 uint32_t &commandIdOut, uint32_t &sectorOut, uint8_t &repeatOut) {
  while (p < end) {
    const char *altEnd = p;
    while (altEnd < end && *altEnd != '|') ++altEnd;
    const char *a = p;
    const char *b = altEnd;
    while (a < b && *a == ' ') ++a;
    while (b > a && b[-1] == ' ') --b;

    uint8_t repeat = 1U;
    const char *star = b;
    while (star > a && star[-1] != '*') --star;
    if (star > a) {                               // star[-1] is the '*'
      bool digitsOk = (star < b);
      uint16_t n = 0U;
      for (const char *d = star; d < b && digitsOk; ++d) {
        if (*d < '0' || *d > '9') digitsOk = false;
        else if (n < 1000U) n = static_cast<uint16_t>(n * 10U + static_cast<uint16_t>(*d - '0'));
      }
      if (digitsOk) {
        repeat = (n < 1U) ? 1U : ((n > MQTT_ALEXA_MAX_REPEAT) ? MQTT_ALEXA_MAX_REPEAT : static_cast<uint8_t>(n));
        b = star - 1;
        while (b > a && b[-1] == ' ') --b;
      }
    }

    const size_t len = static_cast<size_t>(b - a);
    if (len > 0U && len <= STORAGE_MAX_NAME) {
      char name[STORAGE_MAX_NAME + 1U] = {0};
      memcpy(name, a, len);
      if (mqttAlexaFindCommand(deviceId, name, commandIdOut, sectorOut)) {
        repeatOut = repeat;
        DBG_PRINT(F("Alexa step -> ")); DBG_PRINT(name);
        DBG_PRINT(F(" x")); DBG_PRINTLN(repeat);
        return true;
      }
      char bare[8] = {0};
      if (mqttAlexaBareSpeedNumber(name, bare, sizeof(bare)) &&
          mqttAlexaFindCommand(deviceId, bare, commandIdOut, sectorOut)) {
        repeatOut = repeat;
        DBG_PRINT(F("Alexa step -> ")); DBG_PRINT(bare);
        DBG_PRINT(F(" (from ")); DBG_PRINT(name); DBG_PRINTLN(F(")"));
        return true;
      }
    }
    p = (altEnd < end) ? altEnd + 1 : end;
  }
  return false;
}

// ── Channel buttons ──────────────────────────────────────────
// A channel is a normal command record: name = channel name, the waveform
// words hold the digits as text, carrier = CHANNEL_MARK_HZ, gap = CHANNEL_MARK_GAP.
constexpr size_t CHANNEL_MAX_DIGITS = 6U;

// Queue "2;2"-style steps for the sequencer below (digits of a channel, or any steps).
bool irSequenceEnqueue(uint32_t deviceId, const char *text) {
  if (text == nullptr || text[0] == '\0' || mqttAlexaQueueCount >= MQTT_ALEXA_DIR_QUEUE_SIZE) return false;
  MqttAlexaDirective &slot = mqttAlexaQueue[mqttAlexaQueueTail];
  slot.deviceId = deviceId;
  slot.trusted = true;
  strncpy(slot.text, text, MQTT_ALEXA_DO_MAX);
  slot.text[MQTT_ALEXA_DO_MAX] = '\0';
  mqttAlexaQueueTail = static_cast<uint8_t>((mqttAlexaQueueTail + 1U) % MQTT_ALEXA_DIR_QUEUE_SIZE);
  mqttAlexaQueueCount++;
  return true;
}

// Called from transmitLastCapture(). If the loaded command is a channel,
// turn its digits into steps and queue them instead of sending IR directly.
bool irChannelTryTransmit(bool &handledOut) {
  handledOut = false;
  if (txFrequencyHz != CHANNEL_MARK_HZ || txRepeatGapMs != CHANNEL_MARK_GAP || !hasLearnedSignal()) return false;
  handledOut = true;
  const char *raw = reinterpret_cast<const char *>(lastRaw);
  const size_t maxBytes = static_cast<size_t>(lastRawLen) * 2U;
  char text[CHANNEL_MAX_DIGITS * 2U] = {0};
  size_t o = 0U;
  for (size_t i = 0U; i < maxBytes && i < CHANNEL_MAX_DIGITS && raw[i] >= '0' && raw[i] <= '9'; ++i) {
    if (i > 0U) text[o++] = ';';
    text[o++] = raw[i];
  }
  if (o == 0U) return false;
  return irSequenceEnqueue(storage.currentDeviceId, text);   // "22" -> "2;2"
}

// Runs from loop(). At most one IR press per pass; never blocks waiting.
void mqttServiceAlexaDirective() {
  if (irState != IrEngineState::IDLE || !storage.initialized) return;
  const uint32_t now = millis();

  if (mqttAlexaRunActive && (now - mqttAlexaRunStartMs) > 30000UL) {
    DBG_PRINTLN(F("WARNING: Alexa directive timed out; resetting."));
    mqttAlexaRunActive = false;
    mqttAlexaPressRemaining = 0U;
  }

  if (mqttAlexaQueueFullPending && mqttClient.connected()) {
    mqttAlexaQueueFullPending = false;
    (void)mqttPublishStatusEvent("ALEXA_QUEUE_FULL", nullptr, nullptr, 0U, 0U);
  }

  if (!mqttAlexaRunActive) {
    if (mqttAlexaQueueCount == 0U) {
      if (static_cast<int32_t>(now - mqttAlexaNextMs) >= 0) mqttAlexaNextMs = now;
      return;
    }
    mqttAlexaRun = mqttAlexaQueue[mqttAlexaQueueHead];
    mqttAlexaQueueHead = static_cast<uint8_t>((mqttAlexaQueueHead + 1U) % MQTT_ALEXA_DIR_QUEUE_SIZE);
    mqttAlexaQueueCount--;

    char ignoredName[STORAGE_MAX_NAME + 1U] = {0};
    const char *ignoredType = "";
    if (!mqttAlexaRun.trusted && !mqttAlexaGetDevice(mqttAlexaRun.deviceId, ignoredName, sizeof(ignoredName), ignoredType)) {
      DBG_PRINTLN(F("Alexa directive: device not exposed; ignored."));
      return;
    }
    mqttAlexaRunActive = true;
    mqttAlexaRunStartMs = now;
    mqttAlexaRunPos = 0U;
    mqttAlexaRunMissing = false;
    mqttAlexaPressRemaining = 0U;
    DBG_PRINT(F("Alexa directive: device ")); DBG_PRINT(mqttAlexaRun.deviceId);
    DBG_PRINT(F(" do=")); DBG_PRINTLN(mqttAlexaRun.text);
  }

  if (static_cast<int32_t>(now - mqttAlexaNextMs) < 0) return;

  // 1) Keep pressing the current button.
  if (mqttAlexaPressRemaining > 0U) {
    const bool sent = storageLoadCommandFromSector(mqttAlexaPressSector, mqttAlexaPressCommandId) &&
                      transmitLastCapture();
    if (sent) {
      statusLedPulseTransmit();
      --mqttAlexaPressRemaining;
      const bool moreSteps = mqttAlexaRun.text[mqttAlexaRunPos] != '\0';
      mqttAlexaNextMs = now + ((mqttAlexaPressRemaining > 0U) ? MQTT_ALEXA_REPEAT_GAP_MS
                                : (moreSteps ? MQTT_ALEXA_STEP_GAP_MS : 0UL));
    } else {
      DBG_PRINTLN(F("WARNING: Alexa press failed; skipping rest of this step."));
      mqttAlexaRunMissing = true;
      mqttAlexaPressRemaining = 0U;
      mqttAlexaNextMs = now;
    }
    return;
  }

  // 2) Nothing left: finish the directive.
  if (mqttAlexaRun.text[mqttAlexaRunPos] == '\0') {
    if (mqttClient.connected()) {
      char deviceName[STORAGE_MAX_NAME + 1U] = {0};
      if (!storageFindDeviceName(mqttAlexaRun.deviceId, deviceName, sizeof(deviceName))) deviceName[0] = '\0';
      (void)mqttPublishStatusEvent(mqttAlexaRunMissing ? "ALEXA_PARTIAL" : "ALEXA_DONE",
                                   deviceName, nullptr, mqttAlexaRun.deviceId, 0U);
    }
    mqttAlexaRunActive = false;
    mqttAlexaNextMs = now + MQTT_ALEXA_END_GAP_MS;
    return;
  }

  // 3) Load the next step and resolve it to a learned button.
  const char *start = mqttAlexaRun.text + mqttAlexaRunPos;
  const char *semi = strchr(start, ';');
  const char *stepEnd = (semi != nullptr) ? semi : (start + strlen(start));
  mqttAlexaRunPos = static_cast<size_t>(stepEnd - mqttAlexaRun.text) + ((semi != nullptr) ? 1U : 0U);
  mqttAlexaNextMs = now;
  if (stepEnd == start) return;                                // empty step

  uint32_t commandId = 0UL;
  uint32_t sector = 0UL;
  uint8_t repeat = 1U;
  if (mqttAlexaResolveStep(mqttAlexaRun.deviceId, start, stepEnd, commandId, sector, repeat)) {
    mqttAlexaPressCommandId = commandId;
    mqttAlexaPressSector = sector;
    mqttAlexaPressRemaining = repeat;
  } else {
    mqttAlexaRunMissing = true;
    DBG_PRINTLN(F("Alexa step: no matching learned button; skipped."));
  }
}

void mqttService() {
  if (irState != IrEngineState::IDLE) return;
  if (!mqttConfig.enabled) {
    if (mqttClient.connected()) {
      (void)mqttPublishAvailability(false);
      mqttClient.disconnect();
    }
    mqttState = MqttState::DISABLED;
    mqttDiscoverySyncActive = false;
    statusLedSetMqttError(false);
    return;
  }
  if (mqttConfig.broker[0] == '\0' || mqttConfig.baseTopic[0] == '\0') {
    if (mqttClient.connected()) {
      (void)mqttPublishAvailability(false);
      mqttClient.disconnect();
    }
    mqttState = MqttState::UNCONFIGURED;
    statusLedSetMqttError(false);
    return;
  }
  if (mqttConfigurationDirty) {
    mqttState = mqttClient.connected() ? MqttState::CONNECTED : MqttState::DISCONNECTED;
    statusLedSetMqttError(false);
    return;
  }
  if (!wifiIsConnected()) {
    if (mqttClient.connected()) mqttClient.disconnect();
    mqttState = MqttState::WAITING_WIFI;
    statusLedSetMqttError(false);
    return;
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
    mqttState = MqttState::CONNECTED;
    statusLedSetMqttError(false);
    mqttServiceTemperature();
    mqttServiceTelemetry();
    mqttServiceEvents();
    mqttServiceAlexaCatalog();
    mqttProcessOneQueuedCommand();
    if (irState == IrEngineState::IDLE) mqttServiceDiscovery();
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - mqttNextReconnectMs) < 0) {
    mqttState = MqttState::DISCONNECTED;
    return;
  }
  mqttState = MqttState::CONNECTING;
  mqttClient.setServer(mqttConfig.broker, mqttConfig.port);
  char availabilityTopic[MQTT_COMMAND_TOPIC_MAX_LENGTH + 1U] = {0};
  mqttBuildSystemTopic("availability", availabilityTopic, sizeof(availabilityTopic));
  bool connected = false;
  if (mqttConfig.username[0] != '\0') {
    connected = mqttClient.connect(mqttConfig.clientId, mqttConfig.username,
        mqttConfig.password, availabilityTopic, 0, true, "offline");
  } else {
    connected = mqttClient.connect(mqttConfig.clientId, availabilityTopic, 0, true, "offline");
  }
  if (!connected) {
    mqttState = MqttState::DISCONNECTED;
    mqttNextReconnectMs = now + MQTT_RECONNECT_INTERVAL_MS;
    statusLedSetMqttError(true);
    return;
  }
  char subscriptionTopic[MQTT_BASE_TOPIC_MAX_LENGTH + 3U] = {0};
  const int subscriptionLength = snprintf(subscriptionTopic, sizeof(subscriptionTopic),
                                          "%s/#", mqttConfig.baseTopic);
  if (subscriptionLength <= 0 ||
      static_cast<size_t>(subscriptionLength) >= sizeof(subscriptionTopic) ||
      !mqttClient.subscribe(subscriptionTopic, 0)) {
    mqttClient.disconnect();
    mqttState = MqttState::DISCONNECTED;
    mqttNextReconnectMs = now + MQTT_RECONNECT_INTERVAL_MS;
    statusLedSetMqttError(true);
    return;
  }
  char haBirthTopic[MQTT_BASE_TOPIC_MAX_LENGTH + 24U] = {0};
  snprintf(haBirthTopic, sizeof(haBirthTopic), "%s/birth", mqttHaDiscoveryRoot());
  if (!mqttClient.subscribe(haBirthTopic, 0)) {
#if STAGE1_SERIAL_DEBUG
    if (serialEnabled) Serial.println(F("WARNING: Home Assistant birth subscription failed; discovery will still be published on MQTT connect."));
#endif
  }
  mqttState = MqttState::CONNECTED;
  statusLedSetMqttError(false);
  mqttNextReconnectMs = now + MQTT_RECONNECT_INTERVAL_MS;
  mqttTelemetryImmediatePending = true;
  mqttLastTelemetryMs = 0UL;
  mqttDiscoverySyncActive = false;
  mqttDiscoveryScanSector = DB_DATA_START_SECTOR;
  mqttDiscoveryTemperaturePublished = false;
  mqttLastPublishedTemperatureSequence = UINT32_MAX;
  mqttLastPublishedTemperatureCentiDegrees = INT32_MIN;
  mqttLastPublishedTemperatureValid = false;
  mqttLastPublishedTemperatureEnabled = false;
  mqttRequestDiscoveryRefresh();
  mqttAlexaCatalogForce = true;
  mqttAlexaCatalogLastCheckMs = 0UL;
  mqttAlexaSendActive = false;
  mqttAlexaSubscribeDirective();
  (void)mqttPublishAvailability(true);
  mqttPublishTemperatureIfChanged();
  if (mqttPublishTelemetry()) {
    mqttLastTelemetryMs = now;
    mqttTelemetryImmediatePending = false;
  }
}

bool mqttIsConnected() { return mqttClient.connected(); }
const char *mqttGetBaseTopic() { return mqttConfig.baseTopic; }

void mqttPrintStatus() {
#if STAGE1_SERIAL_DEBUG
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=== MQTT ==="));
  Serial.print(F("Enabled      : ")); Serial.println(mqttConfig.enabled ? F("YES") : F("NO"));
  Serial.print(F("State        : ")); Serial.println(mqttStateToString(mqttState));
  Serial.print(F("Broker       : ")); Serial.println(mqttConfig.broker[0] != '\0' ? mqttConfig.broker : "not configured");
  Serial.print(F("Port         : ")); Serial.println(mqttConfig.port);
  Serial.print(F("Username     : ")); Serial.println(mqttConfig.username[0] != '\0' ? mqttConfig.username : "not configured");
  Serial.print(F("Password     : ")); Serial.println(mqttConfig.password[0] != '\0' ? F("configured") : F("not configured"));
  Serial.print(F("Client ID    : ")); Serial.println(mqttConfig.clientId);
  Serial.print(F("Base topic   : ")); Serial.println(mqttConfig.baseTopic);
  Serial.print(F("Connected    : ")); Serial.println(mqttClient.connected() ? F("YES") : F("NO"));
  Serial.print(F("Packet buf.  : ")); Serial.println(mqttClient.getBufferSize());
  Serial.print(F("HA discovery : ")); Serial.println(haDiscoveryEnabled ? F("ENABLED") : F("DISABLED"));
  Serial.print(F("Queue        : ")); Serial.print(mqttCommandQueueCount); Serial.print(F("/"));
  Serial.println(MQTT_COMMAND_QUEUE_SIZE);
  Serial.print(F("Telemetry    : ")); Serial.print(mqttTelemetryIntervalMs / 1000UL);
  Serial.println(F(" s (independent from temperature)"));
  Serial.println(F("============="));
  Serial.println();
#endif
}

// ============================================================
// EMULATED HUE / ALEXA TRANSPORT
// ============================================================

constexpr uint8_t HUE_MAX_EXPOSED_COMMANDS = 32U;
constexpr uint16_t HUE_HTTP_PORT = 80U;
constexpr uint16_t HUE_SSDP_PORT = 1900U;
constexpr uint16_t HUE_SSDP_BUFFER_SIZE = 256U;
constexpr uint16_t HUE_HTTP_HEADER_BUFFER_SIZE = 512U;
constexpr uint16_t HUE_HTTP_BODY_BUFFER_SIZE = 1024U;
constexpr uint32_t HUE_HTTP_CLIENT_TIMEOUT_MS = 2500UL;
constexpr uint32_t HUE_SSDP_ANNOUNCE_INTERVAL_MS = 60000UL;
IPAddress HUE_SSDP_MULTICAST(239, 255, 255, 250);

struct HueCommandMapEntry {
  uint32_t commandId;
  uint16_t deviceId;
  uint16_t sector;
};
static_assert(sizeof(HueCommandMapEntry) == 8, "unexpected HueCommandMapEntry padding");

WiFiServer hueHttpServer(HUE_HTTP_PORT);
WiFiUDP hueSsdpUdp;
WiFiClient hueHttpClient;
HueCommandMapEntry hueCommandMap[HUE_MAX_EXPOSED_COMMANDS] = {};
uint8_t hueCommandMapCount = 0U;
bool hueCommandMapValid = false;
bool hueHttpClientActive = false;
char hueHttpHeaderBuffer[HUE_HTTP_HEADER_BUFFER_SIZE] = {0};
size_t hueHttpHeaderLength = 0U;
uint16_t hueHttpContentLength = 0U;
char hueHttpBodyBuffer[HUE_HTTP_BODY_BUFFER_SIZE] = {0};
uint16_t hueHttpBodyLength = 0U;
// Points at whichever buffer currently holds the request body.
// Normally hueHttpBodyBuffer; for /api/web/config/restore-from-file
// it points at storageRecordBuffer (which is idle during that request)
// so the 1276-byte upload never touches a new allocation.
static char *hueHttpBodyActive = hueHttpBodyBuffer;
uint32_t hueHttpRequestStartedMs = 0UL;
bool hueNetworkStarted = false;
IPAddress hueNetworkIp(0, 0, 0, 0);
uint32_t hueNextSsdpAnnounceMs = 0UL;
char hueSsdpPacketBuffer[HUE_SSDP_BUFFER_SIZE] = {0};

void hueInvalidateCache() { hueCommandMapValid = false; hueCommandMapCount = 0U; }

bool hueCopyBoundedString(const char *source, char *destination, size_t destinationCapacity) {
  if (source == nullptr || destination == nullptr || destinationCapacity == 0U) return false;
  const size_t length = strlen(source);
  if (length >= destinationCapacity) { destination[0] = '\0'; return false; }
  memcpy(destination, source, length + 1U);
  return true;
}

void hueWriteJsonString(WiFiClient &client, const char *value) {
  client.write('"');
  if (value != nullptr) {
    for (size_t i = 0; value[i] != '\0'; ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      switch (c) {
        case '"': client.print(F("\\\"")); break;
        case '\\': client.print(F("\\\\")); break;
        case '\b': client.print(F("\\b")); break;
        case '\f': client.print(F("\\f")); break;
        case '\n': client.print(F("\\n")); break;
        case '\r': client.print(F("\\r")); break;
        case '\t': client.print(F("\\t")); break;
        default:
          if (c < 0x20U) { char escape[7] = {0}; snprintf(escape, sizeof(escape), "\\u%04X", c); client.print(escape); }
          else client.write(c);
          break;
      }
    }
  }
  client.write('"');
}

void hueBuildBridgeId(char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  const int written = snprintf(destination, capacity, "%06lXFFFE%06lX", chipId, chipId);
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
void hueBuildBridgeUuid(char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return;
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  const int written = snprintf(destination, capacity,
      "uuid:2f402f80-da50-11e1-9b23-%06lX", chipId);
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
void hueBuildLightUniqueId(uint32_t commandId, char *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U || commandId == 0U) return;
  const uint32_t chipId = static_cast<uint32_t>(ESP.getChipId() & 0xFFFFFFUL);
  const uint8_t chip0 = static_cast<uint8_t>((chipId >> 16) & 0xFFU);
  const uint8_t chip1 = static_cast<uint8_t>((chipId >> 8) & 0xFFU);
  const uint8_t chip2 = static_cast<uint8_t>(chipId & 0xFFU);
  const uint8_t cmd0 = static_cast<uint8_t>((commandId >> 24) & 0xFFU);
  const uint8_t cmd1 = static_cast<uint8_t>((commandId >> 16) & 0xFFU);
  const uint8_t cmd2 = static_cast<uint8_t>((commandId >> 8) & 0xFFU);
  const uint8_t cmd3 = static_cast<uint8_t>((commandId & 0xFFU) ^ 0x10U);
  const int written = snprintf(destination, capacity,
      "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X-01",
      chip0, chip1, chip2, cmd0, cmd1, cmd2, cmd3, 0x11U);
  if (written < 0 || static_cast<size_t>(written) >= capacity) destination[0] = '\0';
}
bool hueParseUint32(const char *text, uint32_t &valueOut) {
  valueOut = 0U;
  if (text == nullptr || *text == '\0') return false;
  uint32_t value = 0U;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (value > (UINT32_MAX - digit) / 10UL) return false;
    value = value * 10UL + digit;
  }
  valueOut = value;
  return true;
}

bool hueReadCommandNames(uint32_t sector, uint32_t commandId, uint32_t deviceId,
                         char *deviceName, size_t deviceNameCapacity,
                         char *commandName, size_t commandNameCapacity) {
  if (deviceName == nullptr || commandName == nullptr ||
      deviceNameCapacity < 2U || commandNameCapacity < 2U) return false;
  deviceName[0] = '\0'; commandName[0] = '\0';
  if (storageIsSectorQuarantined(sector) || storageIsCommandDeleted(commandId) ||
      storageIsDeviceDeleted(deviceId)) return false;
  StorageRecordHeader header{};
  if (!storageReadRecordHeaderStable(sector, header) ||
      storageIsRecordHeaderBlank(header) || !storageValidateRecordHeader(header) ||
      header.type != RECORD_TYPE_COMMAND || header.recordId != commandId ||
      header.deviceId != deviceId || header.nameLength == 0U ||
      header.nameLength > STORAGE_MAX_NAME || header.generation != storage.generation) return false;
  uint32_t commit = 0U;
  if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) return false;
  if (header.nameLength >= commandNameCapacity) return false;
  const uint32_t nameAddress = sector * STORAGE_FLASH_SECTOR_SIZE + sizeof(StorageRecordHeader);
  if (!flashReadSmallConfirmed(nameAddress, reinterpret_cast<uint8_t *>(commandName),
                               static_cast<uint16_t>(header.nameLength))) return false;
  commandName[header.nameLength] = '\0';
  return storageFindDeviceName(deviceId, deviceName, deviceNameCapacity);
}

bool hueRebuildCommandMap() {
  hueCommandMapCount = 0U; hueCommandMapValid = false;
  if (!storage.initialized || storage.sectorCount <= DB_DATA_START_SECTOR) return false;
  for (uint32_t sector = DB_DATA_START_SECTOR; sector < storage.sectorCount; ++sector) {
    feedWatchdog();
    if (storageIsSectorQuarantined(sector)) continue;
    StorageRecordHeader header{};
    if (!storageReadRecordHeaderStable(sector, header) ||
        storageIsRecordHeaderBlank(header) || !storageValidateRecordHeader(header) ||
        header.generation != storage.generation || header.type != RECORD_TYPE_COMMAND ||
        header.recordId == 0U || storageIsCommandDeleted(header.recordId) ||
        storageIsDeviceDeleted(header.deviceId) ||
        storageIsCommandIdentityConflicted(header.recordId) ||
        storageIsDeviceIdentityConflicted(header.deviceId)) continue;
    uint32_t commit = 0U;
    if (!storageReadCommitStable(sector, commit) || commit != COMMIT_MAGIC) continue;
    bool commandHueEnabled = false;
    if (!storageGetCommandHueEnabled(header.recordId, commandHueEnabled) || !commandHueEnabled) continue;
    bool duplicate = false;
    for (uint8_t i = 0U; i < hueCommandMapCount; ++i) {
      if (hueCommandMap[i].commandId == header.recordId) { duplicate = true; break; }
    }
    if (duplicate) continue;
    HueCommandMapEntry &entry = hueCommandMap[hueCommandMapCount++];
    entry.commandId = header.recordId;
    entry.deviceId = header.deviceId;
    entry.sector = sector;
    if (hueCommandMapCount >= HUE_MAX_EXPOSED_COMMANDS) break;
  }
  hueCommandMapValid = true;
  return true;
}

int8_t hueFindMapIndex(uint32_t commandId) {
  if (commandId == 0U) return -1;
  if (!hueCommandMapValid) { if (!hueRebuildCommandMap()) return -1; }
  for (uint8_t i = 0U; i < hueCommandMapCount; ++i) {
    if (hueCommandMap[i].commandId == commandId) return static_cast<int8_t>(i);
  }
  return -1;
}
bool hueGetCommandState(uint32_t commandId, bool &stateOnOut) {
  stateOnOut = false;
  return hueFindMapIndex(commandId) >= 0;
}
bool hueSetCommandState(uint32_t commandId, bool stateOn) { (void)stateOn; return hueFindMapIndex(commandId) >= 0; }

bool hueTransmitCommand(uint32_t commandId) {
  if (irState != IrEngineState::IDLE || commandId == 0U) return false;
  const int8_t index = hueFindMapIndex(commandId);
  if (index < 0) return false;
  const HueCommandMapEntry entry = hueCommandMap[static_cast<uint8_t>(index)];
  if (storageIsCommandDeleted(entry.commandId) || storageIsDeviceDeleted(entry.deviceId)) {
    hueInvalidateCache(); return false;
  }
  if (!storageLoadCommandFromSector(entry.sector, entry.commandId)) {
    hueInvalidateCache(); return false;
  }
  const bool transmitted = transmitLastCapture();
  if (transmitted) statusLedPulseTransmit();
  return transmitted;
}

void hueSendHttpHeaders(WiFiClient &client, const char *status, const char *contentType) {
  client.print(F("HTTP/1.1 "));
  client.print(status != nullptr ? status : "500 Internal Server Error");
  client.print(F("\r\nContent-Type: "));
  client.print(contentType != nullptr ? contentType : "text/plain");
  client.print(F("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"));
}
void hueSendHttpError(WiFiClient &client, const char *status, const char *message) {
  hueSendHttpHeaders(client, status, "text/plain");
  if (message != nullptr) client.print(message);
}

void hueServeDescription(WiFiClient &client) {
  char uuid[48] = {0};
  char serial[20] = {0};
  hueBuildBridgeUuid(uuid, sizeof(uuid));
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  snprintf(serial, sizeof(serial), "%06lX", chipId);
  hueSendHttpHeaders(client, "200 OK", "text/xml");
  client.print(F("<?xml version=\"1.0\" ?>"));
  client.print(F("<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"));
  client.print(F("<URLBase>http://")); client.print(WiFi.localIP()); client.print(F(":80/</URLBase>"));
  client.print(F("<device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
               "<friendlyName>Philips hue (Universal IR Hub)</friendlyName>"
               "<manufacturer>Royal Philips Electronics</manufacturer>"
               "<manufacturerURL>http://www.philips.com</manufacturerURL>"
               "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
               "<modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber>"
               "<modelURL>http://www.meethue.com</modelURL><serialNumber>"));
  client.print(serial);
  client.print(F("</serialNumber><UDN>")); client.print(uuid);
  client.print(F("</UDN><presentationURL>index.html</presentationURL></device></root>"));
}

void hueSendApiUsernameResponse(WiFiClient &client) {
  char username[32] = {0};
  const unsigned long chipId = static_cast<unsigned long>(ESP.getChipId() & 0xFFFFFFUL);
  snprintf(username, sizeof(username), "irhub-%06lX", chipId);
  hueSendHttpHeaders(client, "200 OK", "application/json");
  client.print(F("[{\"success\":{\"username\":\"")); client.print(username); client.print(F("\"}}]"));
}

void hueWriteLightObject(WiFiClient &client, const HueCommandMapEntry &entry,
                         const char *deviceName, const char *commandName) {
  char lightUniqueId[48] = {0};
  hueBuildLightUniqueId(entry.commandId, lightUniqueId, sizeof(lightUniqueId));
  bool stateOn = false;
  (void)hueGetCommandState(entry.commandId, stateOn);
  client.print(F("{\"state\":{\"on\":")); client.print(stateOn ? F("true") : F("false"));
  client.print(F(",\"reachable\":true},\"type\":\"On/Off light\",\"name\":"));
  char combinedName[2U * (STORAGE_MAX_NAME + 1U)] = {0};
  if (deviceName != nullptr && commandName != nullptr) {
    snprintf(combinedName, sizeof(combinedName), "%s %s", deviceName, commandName);
  } else if (commandName != nullptr) hueCopyBoundedString(commandName, combinedName, sizeof(combinedName));
  hueWriteJsonString(client, combinedName);
  client.print(F(",\"modelid\":\"IRHUE\",\"manufacturername\":\"ESP8266 Universal IR Hub\",\"uniqueid\":"));
  hueWriteJsonString(client, lightUniqueId);
  client.print(F(",\"swversion\":")); hueWriteJsonString(client, FIRMWARE_VERSION);
  client.print(F(",\"productname\":\"IR Command\"}"));
}

void hueWriteLightsJson(WiFiClient &client) {
  if (!hueCommandMapValid) (void)hueRebuildCommandMap();
  client.print('{');
  bool first = true;
  for (uint8_t i = 0U; i < hueCommandMapCount; ++i) {
    feedWatchdog();
    char deviceName[STORAGE_MAX_NAME + 1U] = {0};
    char commandName[STORAGE_MAX_NAME + 1U] = {0};
    const HueCommandMapEntry &entry = hueCommandMap[i];
    if (!hueReadCommandNames(entry.sector, entry.commandId, entry.deviceId,
        deviceName, sizeof(deviceName), commandName, sizeof(commandName))) continue;
    if (!first) client.print(',');
    first = false;
    client.print('"'); client.print(static_cast<unsigned long>(entry.commandId)); client.print(F("\":"));
    hueWriteLightObject(client, entry, deviceName, commandName);
  }
  client.print('}');
}

void hueServeLights(WiFiClient &client) { hueSendHttpHeaders(client, "200 OK", "application/json"); hueWriteLightsJson(client); }
void hueServeLightsNew(WiFiClient &client) { hueSendHttpHeaders(client, "200 OK", "application/json"); client.print(F("{}")); }

void hueServeLightDetail(WiFiClient &client, uint32_t commandId) {
  const int8_t index = hueFindMapIndex(commandId);
  if (index < 0) { hueSendHttpError(client, "404 Not Found", "Unknown light"); return; }
  const HueCommandMapEntry entry = hueCommandMap[static_cast<uint8_t>(index)];
  char deviceName[STORAGE_MAX_NAME + 1U] = {0};
  char commandName[STORAGE_MAX_NAME + 1U] = {0};
  if (!hueReadCommandNames(entry.sector, entry.commandId, entry.deviceId,
      deviceName, sizeof(deviceName), commandName, sizeof(commandName))) {
    hueSendHttpError(client, "404 Not Found", "Unknown light"); return;
  }
  hueSendHttpHeaders(client, "200 OK", "application/json");
  hueWriteLightObject(client, entry, deviceName, commandName);
}

bool hueParseOnValue(const char *body, bool &valueOut) {
  valueOut = false;
  if (body == nullptr || body[0] == '\0') return false;
  const char *onKey = strstr(body, "\"on\"");
  if (onKey == nullptr) return false;
  const char *trueValue = strstr(onKey, "true");
  const char *falseValue = strstr(onKey, "false");
  if (trueValue == nullptr && falseValue == nullptr) return false;
  if (trueValue != nullptr && (falseValue == nullptr || trueValue < falseValue)) valueOut = true;
  else valueOut = false;
  return true;
}

void hueServeLightStatePut(WiFiClient &client, uint32_t commandId, const char *body) {
  bool requestedOn = false;
  if (!hueParseOnValue(body, requestedOn)) { hueSendHttpError(client, "400 Bad Request", "Missing on state"); return; }
  if (requestedOn) {
    const bool transmitted = hueTransmitCommand(commandId);
    if (!transmitted) { hueSendHttpError(client, "404 Not Found", "IR command unavailable"); return; }
  }
  if (!hueSetCommandState(commandId, requestedOn)) {
    hueSendHttpError(client, "500 Internal Server Error", "Hue state unavailable"); return;
  }
  hueSendHttpHeaders(client, "200 OK", "application/json");
  client.print(F("[{\"success\":{\"/lights/"));
  client.print(static_cast<unsigned long>(commandId));
  client.print(F("/state/on\":false}}]"));
}

void hueWriteApiConfigJson(WiFiClient &client) {
  char bridgeId[32] = {0};
  hueBuildBridgeId(bridgeId, sizeof(bridgeId));
  client.print(F("{\"name\":\"Universal IR Hub\",\"bridgeid\":"));
  hueWriteJsonString(client, bridgeId);
  client.print(F(",\"modelid\":\"BSB001\",\"swversion\":"));
  hueWriteJsonString(client, FIRMWARE_VERSION);
  client.print('}');
}
void hueServeApiConfig(WiFiClient &client) { hueSendHttpHeaders(client, "200 OK", "application/json"); hueWriteApiConfigJson(client); }
void hueServeApiRoot(WiFiClient &client) {
  hueSendHttpHeaders(client, "200 OK", "application/json");
  client.print(F("{\"lights\":")); hueWriteLightsJson(client);
  client.print(F(",\"groups\":{},\"schedules\":{},\"config\":")); hueWriteApiConfigJson(client);
  client.print('}');
}
void hueServeEmptyCollection(WiFiClient &client) { hueSendHttpHeaders(client, "200 OK", "application/json"); client.print(F("{}")); }

bool hueParseApiLightPath(const char *path, uint32_t &commandIdOut, bool &isStateEndpointOut) {
  commandIdOut = 0U; isStateEndpointOut = false;
  if (path == nullptr) return false;
  const char *lights = strstr(path, "lights/");
  if (lights == nullptr || lights != path) return false;
  lights += 7;
  char idText[12] = {0};
  size_t idLength = 0U;
  while (lights[idLength] >= '0' && lights[idLength] <= '9' && idLength < sizeof(idText) - 1U) {
    idText[idLength] = lights[idLength]; ++idLength;
  }
  if (idLength == 0U) return false;
  idText[idLength] = '\0';
  if (!hueParseUint32(idText, commandIdOut) || commandIdOut == 0U) return false;
  const char *suffix = lights + idLength;
  if (*suffix == '\0') { isStateEndpointOut = false; return true; }
  if (strcmp(suffix, "/state") == 0) { isStateEndpointOut = true; return true; }
  return false;
}

// ---- HTTP request router (routes WebUI first, then Hue) ----------------

void hueProcessHttpRequest() {
  char *firstSpace = strchr(hueHttpHeaderBuffer, ' ');
  if (firstSpace == nullptr) { hueSendHttpError(hueHttpClient, "400 Bad Request", "Malformed request"); return; }
  *firstSpace = '\0';
  char *method = hueHttpHeaderBuffer;
  char *pathStart = firstSpace + 1;
  char *secondSpace = strchr(pathStart, ' ');
  if (secondSpace == nullptr) { hueSendHttpError(hueHttpClient, "400 Bad Request", "Malformed request line"); return; }
  *secondSpace = '\0';
  if (strlen(method) >= 8U || strlen(pathStart) >= 256U) {
    hueSendHttpError(hueHttpClient, "414 URI Too Long", "Request too long"); return;
  }
  char *queryStart = strchr(pathStart, '?');
  const char *queryPayload = nullptr;
  if (queryStart != nullptr) {
    *queryStart = '\0';
    char *equals = strchr(queryStart + 1, '=');
    queryPayload = (equals != nullptr) ? (equals + 1) : (queryStart + 1);
    if (*queryPayload == '\0') queryPayload = nullptr;
  }
  const bool statePutNeedsBody = strcmp(method, "PUT") == 0 && strstr(pathStart, "/state") != nullptr;
// ── v1.0: captive-portal redirect ───────────────────────────
// While in recovery AP mode, if the Host header is not our AP IP,
// force a redirect to / so the OS captive-portal sheet opens.
// ── v1.0: captive-portal redirect ───────────────────────────
// hueHttpHeaderBuffer's request line has been null-terminated by the
// parser above, so the rest of the headers begin at secondSpace + 1.
// Search for Host: from there — NOT from hueHttpHeaderBuffer, whose
// string view ends right after "GET".
if (wifiRecoveryState != WifiRecoveryState::OFF) {
  const char *headerTail = secondSpace + 1;
  const char *hostHdr = strstr(headerTail, "\r\nHost:");
  if (hostHdr == nullptr) hostHdr = strstr(headerTail, "\r\nhost:");
  if (hostHdr == nullptr) hostHdr = strstr(headerTail, "\r\nHOST:");
  bool isApHost = false;
  if (hostHdr != nullptr) {
    hostHdr += 6;                       // skip "\r\nHost"
    while (*hostHdr == ' ' || *hostHdr == ':') ++hostHdr;
    isApHost = (strncmp(hostHdr, "192.168.4.1", 11) == 0);
  }
  if (!isApHost && strcmp(pathStart, "/") != 0) {
    hueHttpClient.print(F("HTTP/1.1 302 Found\r\n"
                          "Location: http://192.168.4.1/\r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n"
                          "Content-Length: 0\r\n\r\n"));
    return;
  }
}
  // >>> v11 INTEGRATION — WebUI takes routing priority on "/" and /api/web/*
  if (webUiDispatch(hueHttpClient, method, pathStart, hueHttpBodyActive)) return;

  if (strcmp(pathStart, "/description.xml") == 0 && strcmp(method, "GET") == 0) { hueServeDescription(hueHttpClient); return; }
  if (strcmp(pathStart, "/api") == 0 && strcmp(method, "POST") == 0) { hueSendApiUsernameResponse(hueHttpClient); return; }
  if (strncmp(pathStart, "/api/", 5U) != 0) { hueSendHttpError(hueHttpClient, "404 Not Found", "Not found"); return; }
  const char *apiPath = pathStart + 5U;
  if (apiPath[0] != '\0' && strchr(apiPath, '/') == nullptr) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeApiRoot(hueHttpClient); return;
  }
  if (strncmp(apiPath, "v2/lights", 9U) == 0) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeLights(hueHttpClient); return;
  }
  const char *resource = strchr(apiPath, '/');
  if (resource == nullptr) { hueSendHttpError(hueHttpClient, "404 Not Found", "Not found"); return; }
  ++resource;
  if (strcmp(resource, "lights/new") == 0) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeLightsNew(hueHttpClient); return;
  }
  if (strcmp(resource, "groups") == 0 || strcmp(resource, "schedules") == 0 ||
      strcmp(resource, "scenes") == 0 || strcmp(resource, "sensors") == 0 ||
      strcmp(resource, "rules") == 0 || strcmp(resource, "resourcelinks") == 0) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeEmptyCollection(hueHttpClient); return;
  }
  if (strcmp(resource, "lights") == 0) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeLights(hueHttpClient); return;
  }
  if (strcmp(resource, "config") == 0) {
    if (strcmp(method, "GET") != 0) { hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed"); return; }
    hueServeApiConfig(hueHttpClient); return;
  }
  uint32_t commandId = 0U;
  bool isStateEndpoint = false;
  if (!hueParseApiLightPath(resource, commandId, isStateEndpoint)) {
    hueSendHttpError(hueHttpClient, "404 Not Found", "Unknown API path"); return;
  }
  if (strcmp(method, "GET") == 0) { hueServeLightDetail(hueHttpClient, commandId); return; }
  if (strcmp(method, "PUT") == 0 && isStateEndpoint && statePutNeedsBody) {
    const char *statePayload = (hueHttpBodyLength > 0U) ? hueHttpBodyBuffer : queryPayload;
    if (statePayload == nullptr || *statePayload == '\0') {
      hueSendHttpError(hueHttpClient, "400 Bad Request", "Missing on state"); return;
    }
    hueServeLightStatePut(hueHttpClient, commandId, statePayload); return;
  }
  hueSendHttpError(hueHttpClient, "405 Method Not Allowed", "Method not allowed");
}

// ---- SSDP / discovery --------------------------------------------------

void hueSendSsdpResponse(const IPAddress &remoteIp, uint16_t remotePort,
                         const char *searchTarget, const char *usn) {
  if (!hueSsdpUdp || remotePort == 0U || searchTarget == nullptr || usn == nullptr) return;
  char location[64] = {0};
  const IPAddress ip = WiFi.localIP();
  snprintf(location, sizeof(location), "http://%u.%u.%u.%u:80/description.xml",
           ip[0], ip[1], ip[2], ip[3]);
  char bridgeId[32] = {0};
  hueBuildBridgeId(bridgeId, sizeof(bridgeId));
  hueSsdpUdp.beginPacket(remoteIp, remotePort);
  hueSsdpUdp.print(F("HTTP/1.1 200 OK\r\n"));
  hueSsdpUdp.print(F("HOST: 239.255.255.250:1900\r\n"));
  hueSsdpUdp.print(F("CACHE-CONTROL: max-age=100\r\n"));
  hueSsdpUdp.print(F("EXT:\r\nLOCATION: "));
  hueSsdpUdp.print(location);
  hueSsdpUdp.print(F("\r\nSERVER: ESP8266/1.0 UPnP/1.0 IpBridge/1.0\r\n"));
  hueSsdpUdp.print(F("hue-bridgeid: "));
  hueSsdpUdp.print(bridgeId);
  hueSsdpUdp.print(F("\r\nST: "));
  hueSsdpUdp.print(searchTarget);
  hueSsdpUdp.print(F("\r\nUSN: "));
  hueSsdpUdp.print(usn);
  hueSsdpUdp.print(F("\r\n\r\n"));
  hueSsdpUdp.endPacket();
}

void hueSendSsdpAlive() {
  if (!hueSsdpUdp || !hueNetworkStarted) return;
  const IPAddress ip = WiFi.localIP();
  char location[64] = {0};
  snprintf(location, sizeof(location), "http://%u.%u.%u.%u:80/description.xml",
           ip[0], ip[1], ip[2], ip[3]);
  char bridgeId[32] = {0};
  hueBuildBridgeId(bridgeId, sizeof(bridgeId));
  char uuid[64] = {0};
  hueBuildBridgeUuid(uuid, sizeof(uuid));
  char rootUsn[96] = {0};
  snprintf(rootUsn, sizeof(rootUsn), "%s::upnp:rootdevice", uuid);
  const char *targets[3] = {"upnp:rootdevice", uuid, "urn:schemas-upnp-org:device:basic:1"};
  const char *usns[3] = {rootUsn, uuid, uuid};
  for (uint8_t i = 0U; i < 3U; ++i) {
    hueSsdpUdp.beginPacket(HUE_SSDP_MULTICAST, HUE_SSDP_PORT);
    hueSsdpUdp.print(F("NOTIFY * HTTP/1.1\r\n"));
    hueSsdpUdp.print(F("HOST: 239.255.255.250:1900\r\n"));
    hueSsdpUdp.print(F("CACHE-CONTROL: max-age=100\r\n"));
    hueSsdpUdp.print(F("LOCATION: "));
    hueSsdpUdp.print(location);
    hueSsdpUdp.print(F("\r\nSERVER: Hue/1.0 UPnP/1.0 IpBridge/1.67.0\r\n"));
    hueSsdpUdp.print(F("NTS: ssdp:alive\r\n"));
    hueSsdpUdp.print(F("hue-bridgeid: "));
    hueSsdpUdp.print(bridgeId);
    hueSsdpUdp.print(F("\r\nNT: "));
    hueSsdpUdp.print(targets[i]);
    hueSsdpUdp.print(F("\r\nUSN: "));
    hueSsdpUdp.print(usns[i]);
    hueSsdpUdp.print(F("\r\n\r\n"));
    hueSsdpUdp.endPacket();
    feedWatchdog();
  }
  hueNextSsdpAnnounceMs = millis() + HUE_SSDP_ANNOUNCE_INTERVAL_MS;
}

void hueServiceSsdp() {
  if (!hueNetworkStarted) return;
  const int packetSize = hueSsdpUdp.parsePacket();
  if (packetSize <= 0) return;
  const int readLength = hueSsdpUdp.read(hueSsdpPacketBuffer, HUE_SSDP_BUFFER_SIZE - 1U);
  if (readLength <= 0) return;
  hueSsdpPacketBuffer[readLength] = '\0';
  if (strstr(hueSsdpPacketBuffer, "M-SEARCH") == nullptr ||
      strstr(hueSsdpPacketBuffer, "ssdp:discover") == nullptr) return;
  char uuid[64] = {0};
  hueBuildBridgeUuid(uuid, sizeof(uuid));
  char rootUsn[96] = {0};
  snprintf(rootUsn, sizeof(rootUsn), "%s::upnp:rootdevice", uuid);
  hueSendSsdpResponse(hueSsdpUdp.remoteIP(), hueSsdpUdp.remotePort(), "upnp:rootdevice", rootUsn);
  hueSendSsdpResponse(hueSsdpUdp.remoteIP(), hueSsdpUdp.remotePort(), uuid, uuid);
  hueSendSsdpResponse(hueSsdpUdp.remoteIP(), hueSsdpUdp.remotePort(),
                      "urn:schemas-upnp-org:device:basic:1", uuid);
}

// ---- Network lifecycle -------------------------------------------------

void hueStopNetworking() {
  if (hueHttpClientActive) { hueHttpClient.stop(); hueHttpClientActive = false; }
  hueSsdpUdp.stop();
  hueHttpServer.stop();
  hueNetworkStarted = false;
  hueNetworkIp = IPAddress(0, 0, 0, 0);
  hueNextSsdpAnnounceMs = 0UL;
  hueHttpHeaderLength = 0U;
  hueHttpBodyLength = 0U;
  hueHttpContentLength = 0U;
}

// ── v1.0: HTTP serves on STA IP when connected, AP IP when recovering ──
static bool wifiHasUsableInterface() {
  return wifiIsConnected() || wifiRecoveryState != WifiRecoveryState::OFF;
}

void hueStartNetworking() {
  if (!wifiHasUsableInterface()) return;

  const IPAddress localIp = wifiIsConnected() ? WiFi.localIP() : WiFi.softAPIP();

  if (hueNetworkStarted && localIp == hueNetworkIp) return;

  hueStopNetworking();
  hueHttpServer.begin();
  hueHttpServer.setNoDelay(true);

  // SSDP is a LAN protocol; skip it entirely while in recovery AP mode.
  if (wifiRecoveryState == WifiRecoveryState::OFF) {
    if (!hueSsdpUdp.beginMulticast(localIp, HUE_SSDP_MULTICAST, HUE_SSDP_PORT)) {
#if STAGE1_SERIAL_DEBUG
      if (serialEnabled) Serial.println(F("Emulated Hue: SSDP multicast initialization failed."));
#endif
    }
  }

  hueNetworkIp = localIp;
  hueNetworkStarted = true;

  if (wifiRecoveryState == WifiRecoveryState::OFF) hueSendSsdpAlive();

#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    Serial.print(F("HTTP/Hue/Web UI ready at ")); Serial.println(localIp);
  }
#endif
}

// ---- HTTP service loop -------------------------------------------------

void hueServiceHttp() {
  if (!hueNetworkStarted) return;
  if (!hueHttpClientActive) {
    WiFiClient incoming = hueHttpServer.available();
    if (incoming) {
      hueHttpClient = incoming;
      hueHttpClient.setNoDelay(true);
      hueHttpClientActive = true;
      hueHttpHeaderLength = 0U;
      hueHttpContentLength = 0U;
      hueHttpBodyLength = 0U;
      hueHttpRequestStartedMs = millis();
      memset(hueHttpHeaderBuffer, 0, sizeof(hueHttpHeaderBuffer));
      memset(hueHttpBodyBuffer, 0, sizeof(hueHttpBodyBuffer));
    } else return;
  }
  if (!hueHttpClient.connected()) { hueHttpClient.stop(); hueHttpClientActive = false; return; }
  if ((millis() - hueHttpRequestStartedMs) >= HUE_HTTP_CLIENT_TIMEOUT_MS) {
    hueHttpClient.stop(); hueHttpClientActive = false; return;
  }
  if (hueHttpHeaderLength < sizeof(hueHttpHeaderBuffer) - 1U) {
    uint16_t bytesThisPass = 0U;
    while (hueHttpClient.available() &&
           hueHttpHeaderLength < sizeof(hueHttpHeaderBuffer) - 1U && bytesThisPass < 192U) {
      const int value = hueHttpClient.read();
      if (value < 0) break;
      hueHttpHeaderBuffer[hueHttpHeaderLength++] = static_cast<char>(value);
      hueHttpHeaderBuffer[hueHttpHeaderLength] = '\0';
      ++bytesThisPass;
      if (hueHttpHeaderLength >= 4U &&
          hueHttpHeaderBuffer[hueHttpHeaderLength - 4U] == '\r' &&
          hueHttpHeaderBuffer[hueHttpHeaderLength - 3U] == '\n' &&
          hueHttpHeaderBuffer[hueHttpHeaderLength - 2U] == '\r' &&
          hueHttpHeaderBuffer[hueHttpHeaderLength - 1U] == '\n') break;
    }
  }
  const bool headerComplete = hueHttpHeaderLength >= 4U &&
      hueHttpHeaderBuffer[hueHttpHeaderLength - 4U] == '\r' &&
      hueHttpHeaderBuffer[hueHttpHeaderLength - 3U] == '\n' &&
      hueHttpHeaderBuffer[hueHttpHeaderLength - 2U] == '\r' &&
      hueHttpHeaderBuffer[hueHttpHeaderLength - 1U] == '\n';
  if (!headerComplete) return;
  // The restore-from-file upload (1276 bytes) does not fit in
  // hueHttpBodyBuffer (1024 bytes). Route it to storageRecordBuffer
  // (2123 bytes), which is idle during this request. Everything else
  // keeps using hueHttpBodyBuffer exactly as before.
  const bool restoreUpload =
      (strstr(hueHttpHeaderBuffer, "restore-from-file") != nullptr);
  char *bodyBuf = restoreUpload
                    ? reinterpret_cast<char *>(storageRecordBuffer)
                    : hueHttpBodyBuffer;
  const size_t bodyBufCap = restoreUpload
                              ? sizeof(storageRecordBuffer)
                              : HUE_HTTP_BODY_BUFFER_SIZE;  hueHttpBodyActive = bodyBuf;

  if (hueHttpContentLength == 0U) {
    const char *contentLengthText = strstr(hueHttpHeaderBuffer, "Content-Length:");
    if (contentLengthText == nullptr) contentLengthText = strstr(hueHttpHeaderBuffer, "content-length:");
    if (contentLengthText != nullptr) {
      contentLengthText += 15;
      while (*contentLengthText == ' ') ++contentLengthText;
      uint32_t parsedLength = 0U;
      const char *p = contentLengthText;
      while (*p >= '0' && *p <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (parsedLength > (UINT16_MAX - digit) / 10UL) {
          hueSendHttpError(hueHttpClient, "413 Payload Too Large", "Body too large");
          hueHttpClient.stop(); hueHttpClientActive = false; return;
        }
        parsedLength = parsedLength * 10UL + digit;
        ++p;
      }
      hueHttpContentLength = static_cast<uint16_t>(parsedLength);
    }
    if (hueHttpContentLength > bodyBufCap - 1U) {
      hueSendHttpError(hueHttpClient, "413 Payload Too Large", "Body too large");
      hueHttpClient.stop(); hueHttpClientActive = false; return;
    }
  }
  while (hueHttpClient.available() && hueHttpBodyLength < hueHttpContentLength) {
    const int value = hueHttpClient.read();
    if (value < 0) break;
    if (hueHttpBodyLength >= bodyBufCap - 1U) break;
    bodyBuf[hueHttpBodyLength++] = static_cast<char>(value);
  }
  bodyBuf[hueHttpBodyLength] = '\0';
  if (hueHttpBodyLength < hueHttpContentLength) return;
  hueProcessHttpRequest();
  hueHttpClient.stop();
  hueHttpClientActive = false;
  hueHttpHeaderLength = 0U;
  hueHttpContentLength = 0U;
  hueHttpBodyLength = 0U;
}

// ---- Public entry points ----------------------------------------------

void hueInitialize() {
  hueInvalidateCache();
  hueHttpClientActive = false;
  hueHttpHeaderLength = 0U;
  hueHttpContentLength = 0U;
  hueHttpBodyLength = 0U;
  hueNetworkStarted = false;
  hueNetworkIp = IPAddress(0, 0, 0, 0);
  hueStartNetworking();
}

void hueService() {
  if (irState != IrEngineState::IDLE) return;

  if (!wifiHasUsableInterface()) {
    if (hueNetworkStarted) hueStopNetworking();
    return;
  }

  hueStartNetworking();

  if (wifiRecoveryState == WifiRecoveryState::OFF &&
      hueNetworkStarted &&
      static_cast<int32_t>(millis() - hueNextSsdpAnnounceMs) >= 0) {
    hueSendSsdpAlive();
  }

  hueServiceHttp();

  if (wifiRecoveryState == WifiRecoveryState::OFF) hueServiceSsdp();
}


// ============================================================
// WEB UI FOUNDATION (v11 integration)
// ============================================================

const char *webUiIrStateToString(IrEngineState state) {
  switch (state) {
    case IrEngineState::IDLE:         return "IDLE";
    case IrEngineState::LEARNING:     return "LEARNING";
    case IrEngineState::TRANSMITTING: return "TRANSMITTING";
    default:                          return "UNKNOWN";
  }
}

// ============================================================
// SERIAL COMMAND HELP
// ============================================================

#if STAGE1_SERIAL_DEBUG
void printHelp() {
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("STAGE 3 v3.0.34 IR HUB COMMANDS"));
  Serial.println(F("========================================"));
  Serial.println(F("L       = Learn one IR signal"));
  Serial.println(F("T       = Transmit last learned signal"));
  Serial.println(F("P       = Print stored raw timings"));
  Serial.println(F("C       = Clear stored capture"));
  Serial.println(F("F       = Show TX frequency"));
  Serial.println(F("F38000  = Set TX frequency (Hz)"));
  Serial.println(F("R       = Show repeat count"));
  Serial.println(F("R3      = Set repeat count"));
  Serial.println(F("G       = Show repeat gap"));
  Serial.println(F("G100    = Set repeat gap (ms)"));
  Serial.println(F("REBOOT  = Reboot the ESP8266"));
  Serial.println(F("H       = Show this help"));
  Serial.println(F("WIFI    = Show Wi-Fi status"));
  Serial.println(F("WIFI SSID <name> | WIFI PASS <pw> | WIFI HOST <name> | WIFI CONNECT"));
  Serial.println(F("MQTT    = Show MQTT status"));
  Serial.println(F("MQTT ON / MQTT OFF | MQTT BROKER <host> | MQTT PORT <port>"));
  Serial.println(F("MQTT USER <u> | MQTT PASS <p> | MQTT CLIENT <id> | MQTT BASE <topic> | MQTT CONNECT"));
  Serial.println(F("HASS           = Show HA discovery setting"));
  Serial.println(F("HASS ON/OFF    = Enable/disable HA MQTT discovery (persistent)"));
  Serial.println(F("PINS           = Show current pin configuration"));
  Serial.println(F("PINS IRTX <gpio> | PINS IRRX <gpio> | PINS LED <gpio> | PINS TEMP <gpio>"));
  Serial.println(F("RENAME DEVICE <id> <new-name>   = Rename a device"));
  Serial.println(F("RENAME COMMAND <id> <new-name>  = Rename a command on the same device"));
  Serial.println(F("D       = Show diagnostics"));
  Serial.println(F("ST      = Show W25Qxx storage status"));
  Serial.println(F("LISTD   = List stored devices"));
  Serial.println(F("LISTC   = List stored commands"));
  Serial.println(F("DEVICE <name> = Select/create device"));
  Serial.println(F("HUE | HUE ON | HUE OFF = Current command Hue/Alexa exposure test"));
  Serial.println(F("SAVE <name>   = Save current learned signal (name unique per device)"));
  Serial.println(F("RELEARN <id>  = Confirm, delete, learn and replace an existing command"));
  Serial.println(F("LOAD <id>     = Load stored command into RAM"));
  Serial.println(F("DELETE COMMAND <id> | DELETE DEVICE <id> | DELETE ALL | RESETDB | WIPEDB"));
  Serial.println(F("TEMP          = Show DS18B20 status/temperature"));
  Serial.println(F("TEMP ON/OFF/NOW | TEMP CAL [+|-]<deg> | TEMP INT <seconds>"));
  Serial.println(F("TIME | TIME ON|OFF | TIME SERVER <host> | TIME TZ <min> | TIME DST <min>"));
  Serial.println(F("SCHEDULE LIST | SCHEDULE SHOW <id> | SCHEDULE ENABLE/DISABLE <id> | SCHEDULE DELETE <id>"));
  Serial.println(F("SCHEDULE ADD <id> CMD <cid> H <h> M <m> [WD <mask>] [DLY <sec>] [F <byte>]"));
  Serial.println(F("SERIAL | SERIAL ON | SERIAL OFF"));
  Serial.println(F("RULE LIST | RULE SHOW <id> | RULE ENABLE|DISABLE <id> | RULE DELETE <id> | RULE ADD ... | INT <sec>"));
  Serial.println(F("LED | ERROR ON | ERROR OFF"));
  Serial.println();
  Serial.println(F("Pin changes require a reboot to take effect."));
  Serial.println(F("Allowed pins: 1, 2, 3, 4, 5, 16 (16 not valid for IR RX)."));
  Serial.println(F("========================================"));
  Serial.println();
}
#endif

// ============================================================
// SERIAL COMMAND PROCESSING
// ============================================================

#if STAGE1_SERIAL_DEBUG

bool isStorageCommandLine(char *line);
void printDiagnostics();

bool parseUnsignedSuffix(const char *line, uint32_t &value) {
  if (line == nullptr || line[1] == '\0') return false;
  uint32_t parsed = 0;
  for (size_t i = 1; line[i] != '\0'; i++) {
    const char c = line[i];
    if (c < '0' || c > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(c - '0');
    if (parsed > (UINT32_MAX - digit) / 10UL) return false;
    parsed = static_cast<uint32_t>(parsed * 10UL + digit);
  }
  value = parsed;
  return true;
}

bool commandEqualsIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *b != '\0') {
    char ca = *a++; char cb = *b++;
    if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

bool commandStartsWithIgnoreCase(const char *text, const char *prefix) {
  if (text == nullptr || prefix == nullptr) return false;
  while (*prefix != '\0') {
    if (*text == '\0') return false;
    char ca = *text++; char cb = *prefix++;
    if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
    if (ca != cb) return false;
  }
  return true;
}

bool parseSignedCentiDegrees(const char *text, int16_t &valueOut) {
  if (text == nullptr || *text == '\0') return false;
  bool negative = false;
  const char *p = text;
  if (*p == '+' || *p == '-') { negative = (*p == '-'); ++p; }
  if (*p == '\0') return false;
  uint32_t whole = 0;
  bool sawWholeDigit = false;
  while (*p >= '0' && *p <= '9') {
    sawWholeDigit = true;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (whole > (32767UL - digit) / 10UL) return false;
    whole = whole * 10UL + digit;
    ++p;
  }
  uint32_t fraction = 0;
  uint8_t fractionDigits = 0;
  if (*p == '.') {
    ++p;
    while (*p >= '0' && *p <= '9') {
      if (fractionDigits >= 2U) return false;
      fraction = fraction * 10UL + static_cast<uint32_t>(*p - '0');
      ++fractionDigits; ++p;
    }
  }
  if (*p != '\0' || (!sawWholeDigit && fractionDigits == 0U)) return false;
  if (fractionDigits == 1U) fraction *= 10UL;
  const uint32_t centi = whole * 100UL + fraction;
  if (centi > 32767UL) return false;
  const int32_t signedValue = negative ? -static_cast<int32_t>(centi) : static_cast<int32_t>(centi);
  valueOut = static_cast<int16_t>(signedValue);
  return true;
}

bool parseUint32Text(const char *text, uint32_t &valueOut) {
  if (text == nullptr || *text == '\0') return false;
  uint32_t value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*text - '0');
    if (value > (UINT32_MAX - digit) / 10UL) return false;
    value = value * 10UL + digit;
  }
  valueOut = value;
  return true;
}

static const char *const RULE_OP_NAMES[] = {"", "GT", "GE", "LT", "LE", "EQ"};

static uint8_t ruleParseOp(const char *s) {
  if (commandEqualsIgnoreCase(s, "GT") || commandEqualsIgnoreCase(s, ">"))  return RULE_OP_GT;
  if (commandEqualsIgnoreCase(s, "GE") || commandEqualsIgnoreCase(s, ">=")) return RULE_OP_GE;
  if (commandEqualsIgnoreCase(s, "LT") || commandEqualsIgnoreCase(s, "<"))  return RULE_OP_LT;
  if (commandEqualsIgnoreCase(s, "LE") || commandEqualsIgnoreCase(s, "<=")) return RULE_OP_LE;
  if (commandEqualsIgnoreCase(s, "EQ") || commandEqualsIgnoreCase(s, "="))  return RULE_OP_EQ;
  return RULE_OP_NONE;
}

// "20:00" or minutes-since-midnight ("1200")
static bool ruleParseClock(const char *s, uint16_t &out) {
  if (s == nullptr || *s == '\0') return false;
  const char *colon = strchr(s, ':');
  if (colon != nullptr) {
    if (colon == s) return false;
    char *e1 = nullptr; const long h = strtol(s, &e1, 10);
    if (e1 != colon) return false;
    char *e2 = nullptr; const long m = strtol(colon + 1, &e2, 10);
    if (e2 == colon + 1 || *e2 != '\0') return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    out = static_cast<uint16_t>(h * 60 + m);
    return true;
  }
  uint32_t v = 0;
  if (!parseUint32Text(s, v) || v > 1439U) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

static void rulePrintCenti(int32_t c) {
  if (c < 0) { Serial.print('-'); c = -c; }
  Serial.print(c / 100); Serial.print('.');
  if ((c % 100) < 10) Serial.print('0');
  Serial.print(c % 100);
}
static void rulePrintClock(uint16_t m) {
  const uint16_t h = m / 60, mm = m % 60;
  if (h < 10) Serial.print('0');
  Serial.print(h); Serial.print(':');
  if (mm < 10) Serial.print('0');
  Serial.print(mm);
}

// Prints "ON ... DO ..." exactly in RULE ADD syntax.
static void rulePrintTrigger(const RuleTriggerStored &g) {
  Serial.print(F("ON"));
  if (g.flags & TRIG_FLAG_USE_INTERVAL) { Serial.print(F(" INT ")); Serial.print(g.intervalSec); }
  if (g.flags & TRIG_FLAG_USE_TEMP) {
    Serial.print(F(" TEMP ")); Serial.print(RULE_OP_NAMES[g.tempOp <= RULE_OP_EQ ? g.tempOp : 0]);
    Serial.print(' '); rulePrintCenti(g.tempValueCenti);
    if (g.tempDurationSec) { Serial.print(F(" DUR ")); Serial.print(g.tempDurationSec); }
  }
  if (g.flags & TRIG_FLAG_USE_TIME) {
    Serial.print(F(" TIME ")); rulePrintClock(g.timeStartMin);
    Serial.print(' '); rulePrintClock(g.timeEndMin);
  }
  if (g.weekdayMask) { Serial.print(F(" WD ")); Serial.print(g.weekdayMask); }
  if ((g.flags & TRIG_FLAG_USE_TEMP) && (g.flags & TRIG_FLAG_USE_TIME)) {
    Serial.print(F(" LOG ")); Serial.print(g.logic ? F("OR") : F("AND"));
  }
  Serial.print(F(" DO"));
  for (uint8_t s = 0; s < g.stepCount; ++s) {
    Serial.print(g.steps[s].type == STEP_TX ? F(" TX ") : F(" WAIT "));
    Serial.print(g.steps[s].value);
  }
}

bool isStorageCommandLine(char *line) {
  if (line == nullptr) return false;
  char *start = line;
  while (*start == ' ' || *start == '\t') ++start;
  size_t length = strlen(start);
  while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) start[--length] = '\0';
  if (*start == '\0') return false;
  if (commandEqualsIgnoreCase(start, "MQTT") || commandStartsWithIgnoreCase(start, "MQTT ")) return true;
  if (commandStartsWithIgnoreCase(start, "DISCOVERY") &&
      (start[9] == '\0' || start[9] == ' ' || start[9] == '\t')) return true;  
  if (commandStartsWithIgnoreCase(start, "ORDER") &&
      (start[5] == '\0' || start[5] == ' ' || start[5] == '\t')) return true;  
  if (commandEqualsIgnoreCase(start, "ST") || commandEqualsIgnoreCase(start, "STORAGE") ||
      commandEqualsIgnoreCase(start, "LISTD") || commandEqualsIgnoreCase(start, "LISTC") ||
      commandEqualsIgnoreCase(start, "LIST") || commandEqualsIgnoreCase(start, "WIPEDB") ||
      commandEqualsIgnoreCase(start, "DELETE ALL") || commandEqualsIgnoreCase(start, "RESETDB") ||
      commandStartsWithIgnoreCase(start, "DELETE COMMAND") ||
      commandStartsWithIgnoreCase(start, "DELETE DEVICE") ||
      commandStartsWithIgnoreCase(start, "RENAME DEVICE") ||
      commandStartsWithIgnoreCase(start, "RENAME COMMAND") ||
      commandEqualsIgnoreCase(start, "PINS") ||
      commandStartsWithIgnoreCase(start, "PINS ")) return true;
  if (commandStartsWithIgnoreCase(start, "RELEARN") &&
      (start[7] == '\0' || start[7] == ' ' || start[7] == '\t')) return true;
  if (commandStartsWithIgnoreCase(start, "DEVICE") &&
      (start[6] == '\0' || start[6] == ' ' || start[6] == '\t')) return true;
  if (commandStartsWithIgnoreCase(start, "SAVE") &&
      (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) return true;
  if (commandStartsWithIgnoreCase(start, "LOAD") &&
      (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) return true;
  if (commandStartsWithIgnoreCase(start, "RULE") &&
      (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) return true;
  if (commandStartsWithIgnoreCase(start, "SCHEDULE") &&
      (start[8] == '\0' || start[8] == ' ' || start[8] == '\t')) return true;
  return false;
}

void discardQueuedSerialInput() {
  if (!serialEnabled) return;
  while (Serial.available()) (void)Serial.read();
  serialLineLength = 0;
  serialLineOverflow = false;
}

void printPinsStatus() {
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=== PIN CONFIGURATION ==="));
  Serial.print(F("IR TX      : GPIO")); Serial.println(pinIrTx);
  Serial.print(F("IR RX      : GPIO")); Serial.println(pinIrRx);
  Serial.print(F("Status LED : GPIO")); Serial.println(pinStatusLed);
  Serial.print(F("DS18B20    : GPIO")); Serial.println(pinDs18b20);
  Serial.println(F("Allowed    : 1, 2, 3, 4, 5, 16 (16 not valid for IR RX)"));
  Serial.println(F("Reboot required for changes to take effect."));
  Serial.println(F("========================="));
  Serial.println();
}

bool setPinValueByName(const char *which, uint8_t pin, const char **errOut) {
  *errOut = nullptr;
  bool needsInterrupt = false;
  if (commandEqualsIgnoreCase(which, "IRRX")) needsInterrupt = true;
  if (!pinIsAllowedForFunction(pin, needsInterrupt)) {
    *errOut = "pin not allowed for this function";
    return false;
  }
  const uint8_t oldIrTx = pinIrTx;
  const uint8_t oldIrRx = pinIrRx;
  const uint8_t oldStatusLed = pinStatusLed;
  const uint8_t oldDs18b20 = pinDs18b20;
  if (commandEqualsIgnoreCase(which, "IRTX")) {
    if (pin == pinIrRx || pin == pinStatusLed || pin == pinDs18b20) { *errOut = "pin already in use"; return false; }
    pinIrTx = pin;
  } else if (commandEqualsIgnoreCase(which, "IRRX")) {
    if (pin == pinIrTx || pin == pinStatusLed || pin == pinDs18b20) { *errOut = "pin already in use"; return false; }
    pinIrRx = pin;
  } else if (commandEqualsIgnoreCase(which, "LED")) {
    if (pin == pinIrTx || pin == pinIrRx || pin == pinDs18b20) { *errOut = "pin already in use"; return false; }
    pinStatusLed = pin;
  } else if (commandEqualsIgnoreCase(which, "TEMP")) {
    if (pin == pinIrTx || pin == pinIrRx || pin == pinStatusLed) { *errOut = "pin already in use"; return false; }
    pinDs18b20 = pin;
  } else {
    *errOut = "unknown pin function";
    return false;
  }
  if (!pinSetIsConsistent() || !configurationSavePins()) {
    pinIrTx = oldIrTx;
    pinIrRx = oldIrRx;
    pinStatusLed = oldStatusLed;
    pinDs18b20 = oldDs18b20;
    *errOut = "pin configuration could not be persisted";
    return false;
  }
  return true;
}

// >>> v11 INTEGRATION — WebUI automation bridge
bool webUiParseAutomationCommand(const char *cmd, char *errOut, size_t errCap) {
  if (errOut && errCap) errOut[0] = '\0';
  if (!cmd || !*cmd) {
    if (errOut && errCap) { strncpy(errOut, "empty", errCap - 1); errOut[errCap-1] = '\0'; }
    return false;
  }
  if (strlen(cmd) > 150) {
    if (errOut && errCap) { strncpy(errOut, "too long", errCap - 1); errOut[errCap-1] = '\0'; }
    return false;
  }
  if (!commandStartsWithIgnoreCase(cmd, "RULE") &&
      !commandStartsWithIgnoreCase(cmd, "SCHEDULE")) {
    if (errOut && errCap) { strncpy(errOut, "unknown command", errCap - 1); errOut[errCap-1] = '\0'; }
    return false;
  }
  char local[160];
  strncpy(local, cmd, sizeof(local) - 1U);
  local[sizeof(local) - 1U] = '\0';
  extern void processSerialCommand(char *line);
  processSerialCommand(local);
  return true;
}

// ── v1.0: last-command status, read by /api/web/cmd ──────────
bool  gLastCommandOk        = true;
char  gLastCommandError[64] = {0};

void processSerialCommand(char *line) {
  gLastCommandOk = true;
  gLastCommandError[0] = '\0';
  char *start = line;
  while (*start == ' ' || *start == '\t') start++;
  size_t length = strlen(start);
  while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) start[--length] = '\0';
  if (*start == '\0') return;
  if (start[0] >= 'a' && start[0] <= 'z') start[0] = static_cast<char>(start[0] - 'a' + 'A');

  if (commandEqualsIgnoreCase(start, "REBOOT")) {
    rebootDevice();
    return;
  }

  if (commandStartsWithIgnoreCase(start, "ORDER") &&
      (start[5] == '\0' || start[5] == ' ' || start[5] == '\t')) {
    char *p = start + 5;
    while (*p == ' ' || *p == '\t') ++p;

    // ORDER DEVICES <id1> <id2> ...
    if (commandStartsWithIgnoreCase(p, "DEVICES") &&
        (p[7] == '\0' || p[7] == ' ' || p[7] == '\t')) {
      char *q = p + 7;
      uint16_t ids[64];
      uint16_t cnt = 0;
      while (*q && cnt < 64U) {
        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) break;
        char *end = nullptr;
        const unsigned long v = strtoul(q, &end, 10);
        if (end == q || v == 0 || v > 0xFFFFU) { Serial.println(F("ERROR: bad device id.")); return; }
        ids[cnt++] = static_cast<uint16_t>(v);
        q = end;
      }
      if (cnt == 0U) { Serial.println(F("Usage: ORDER DEVICES <id1> <id2> ...")); return; }
      if (!storageAppendOrderRecord(1U, 0U, ids, cnt)) { Serial.println(F("ERROR: order write failed.")); return; }
      Serial.println(F("Device order saved."));
      return;
    }

    // ORDER COMMANDS <device_id> <id1> <id2> ...
    if (commandStartsWithIgnoreCase(p, "COMMANDS") &&
        (p[8] == '\0' || p[8] == ' ' || p[8] == '\t')) {
      char *q = p + 8;
      while (*q == ' ' || *q == '\t') ++q;
      if (!*q) { Serial.println(F("Usage: ORDER COMMANDS <device_id> <id1> <id2> ...")); return; }
      char *end = nullptr;
      const unsigned long devId = strtoul(q, &end, 10);
      if (end == q || devId == 0 || devId > 0xFFFFU) { Serial.println(F("ERROR: bad device id.")); return; }
      q = end;
      uint16_t ids[64];
      uint16_t cnt = 0;
      while (*q && cnt < 64U) {
        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) break;
        const unsigned long v = strtoul(q, &end, 10);
        if (end == q || v == 0 || v > 0xFFFFU) { Serial.println(F("ERROR: bad command id.")); return; }
        ids[cnt++] = static_cast<uint16_t>(v);
        q = end;
      }
      if (cnt == 0U) { Serial.println(F("Usage: ORDER COMMANDS <device_id> <id1> <id2> ...")); return; }
      if (!storageAppendOrderRecord(static_cast<uint32_t>(devId), static_cast<uint32_t>(devId), ids, cnt)) {
        Serial.println(F("ERROR: order write failed.")); return;
      }
      Serial.print(F("Command order saved for device ")); Serial.println(devId);
      return;
    }

    Serial.println(F("Usage: ORDER DEVICES <id1> <id2> ... | ORDER COMMANDS <device_id> <id1> <id2> ..."));
    return;
  }

  if (commandStartsWithIgnoreCase(start, "RULE") &&
      (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;

    if (commandEqualsIgnoreCase(p, "LIST") || *p == '\0') {
      Serial.println(F("=== RULES ==="));
      uint8_t n = 0;
      for (uint8_t i = 0; i < AUTOMATION_MAX_RULES; ++i) {
        const RuleSlot &slot = automationRules[i];
        if (slot.data.triggerCount == 0U) continue;
        Serial.print(F("ID=")); Serial.print(i + 1);
        Serial.print(F("  ")); Serial.print((slot.data.ruleFlags & RULE_FLAG_ENABLED) ? F("EN") : F("DIS"));
        Serial.print(F("  CD=")); Serial.print(slot.data.cooldownSec);
        Serial.print(F("s  Triggers=")); Serial.println(slot.data.triggerCount);
        for (uint8_t ti = 0; ti < slot.data.triggerCount; ++ti) {
          Serial.print(F("    #")); Serial.print(ti + 1); Serial.print(' ');
          rulePrintTrigger(slot.data.triggers[ti]);
          Serial.println();
        }
        n++;
      }
      if (n == 0) Serial.println(F("(none)"));
      Serial.println(F("============="));
      return;
    }

    if (commandStartsWithIgnoreCase(p, "SHOW") && (p[4] == ' ' || p[4] == '\t')) {
      char *q = p + 4; while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) || id < 1 || id > AUTOMATION_MAX_RULES) {
        Serial.println(F("Usage: RULE SHOW <id>")); return;
      }
      const RuleSlot &slot = automationRules[id - 1U];
      if (slot.data.triggerCount == 0U) { Serial.println(F("Not found.")); return; }
      Serial.print(F("RULE ADD ")); Serial.print(id);
      Serial.print(F(" CD ")); Serial.print(slot.data.cooldownSec); Serial.print(' ');
      for (uint8_t ti = 0; ti < slot.data.triggerCount; ++ti) {
        if (ti) Serial.print(F(" | "));
        rulePrintTrigger(slot.data.triggers[ti]);
      }
      Serial.println();
      Serial.print(F("Enabled: ")); Serial.println((slot.data.ruleFlags & RULE_FLAG_ENABLED) ? F("YES") : F("NO"));
      for (uint8_t ti = 0; ti < slot.data.triggerCount; ++ti) {
        Serial.print(F("  #")); Serial.print(ti + 1);
        Serial.print(F(" holding=")); Serial.print(slot.trig[ti].holding ? F("Y") : F("N"));
        Serial.print(F(" latched=")); Serial.println(slot.trig[ti].latched ? F("Y") : F("N"));
      }
      return;
    }

    if ((commandStartsWithIgnoreCase(p, "ENABLE") && (p[6] == ' ' || p[6] == '\t')) ||
        (commandStartsWithIgnoreCase(p, "DISABLE") && (p[7] == ' ' || p[7] == '\t')) ||
        (commandStartsWithIgnoreCase(p, "DELETE") && (p[6] == ' ' || p[6] == '\t'))) {
      const bool isEnable = commandStartsWithIgnoreCase(p, "ENABLE");
      const bool isDelete = commandStartsWithIgnoreCase(p, "DELETE");
      char *q = p + (isEnable ? 6 : (isDelete ? 6 : 7));
      while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) || id < 1 || id > AUTOMATION_MAX_RULES) {
        Serial.println(F("Usage: RULE ENABLE|DISABLE|DELETE <id>")); return;
      }
      if (isDelete) {
        Serial.println(automationDeleteRule(id) ? F("Rule deleted.") : F("ERROR: rule not found or delete failed."));
      } else {
        Serial.println(automationSetRuleEnabled(id, isEnable)
            ? (isEnable ? F("Rule enabled.") : F("Rule disabled."))
            : F("ERROR: rule not found or write failed."));
      }
      return;
    }

    if (commandStartsWithIgnoreCase(p, "ADD") && (p[3] == ' ' || p[3] == '\t')) {
      char *q = p + 3;
      auto next = [&]() -> char * {
        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) return nullptr;
        char *t = q;
        while (*q && *q != ' ' && *q != '\t') ++q;
        if (*q) *q++ = '\0';
        return t;
      };

      uint32_t id = 0;
      char *idTok = next();
      if (idTok == nullptr || !parseUint32Text(idTok, id) || id < 1 || id > AUTOMATION_MAX_RULES) {
        Serial.println(F("Usage: RULE ADD <id> [CD <sec>] ON [TEMP <op> <deg> [DUR <sec>]] [TIME <hh:mm> <hh:mm> [WD <mask>]]"));
        Serial.println(F("       [LOG AND|OR] | [INT <sec>]  DO TX <cmd> | WAIT <sec> ...   ( | separates triggers, max 6, 8 steps each )"));
        return;
      }

      static StoredRule rule;
      memset(&rule, 0, sizeof(rule));
      uint8_t  ti = 0U;
      uint32_t cd = 0U;

      auto addStep = [&](uint8_t type, uint32_t v) -> bool {
        RuleTriggerStored &g = rule.triggers[ti];
        if (g.stepCount >= RULE_MAX_STEPS) { Serial.println(F("ERROR: max 8 steps per trigger.")); return false; }
        g.steps[g.stepCount].type  = type;
        g.steps[g.stepCount].value = static_cast<uint16_t>(v);
        g.stepCount++;
        return true;
      };

      char *tok;
      while ((tok = next()) != nullptr) {
        if (!strcmp(tok, "|")) {
          if (ti + 1U >= RULE_MAX_TRIGGERS) { Serial.println(F("ERROR: max 6 triggers per rule.")); return; }
          ++ti;
          continue;
        }
        if (commandEqualsIgnoreCase(tok, "ON") || commandEqualsIgnoreCase(tok, "DO")) continue;

        char *a = next();
        if (a == nullptr) { Serial.print(F("ERROR: missing value for ")); Serial.println(tok); return; }
        RuleTriggerStored &g = rule.triggers[ti];
        uint32_t v = 0;

        if (commandEqualsIgnoreCase(tok, "CD")) {
          if (!parseUint32Text(a, v) || v > 3600U) { Serial.println(F("ERROR: CD must be 0-3600.")); return; }
          cd = v;
        } else if (commandEqualsIgnoreCase(tok, "TEMP")) {
          const uint8_t op = ruleParseOp(a);
          char *vt = next();
          int16_t cv = 0;
          if (op == RULE_OP_NONE || vt == nullptr || !parseSignedCentiDegrees(vt, cv)) {
            Serial.println(F("ERROR: TEMP <GT|GE|LT|LE|EQ> <degrees>, e.g. TEMP GT 29")); return;
          }
          g.tempOp = op; g.tempValueCenti = cv; g.flags |= TRIG_FLAG_USE_TEMP;
        } else if (commandEqualsIgnoreCase(tok, "DUR")) {
          if (!parseUint32Text(a, v) || v > 65535U) { Serial.println(F("ERROR: DUR out of range.")); return; }
          g.tempDurationSec = static_cast<uint16_t>(v);
        } else if (commandEqualsIgnoreCase(tok, "TIME")) {
          uint16_t s = 0, e = 0;
          char *et = next();
          if (!ruleParseClock(a, s) || et == nullptr || !ruleParseClock(et, e)) {
            Serial.println(F("ERROR: TIME <start> <end> as hh:mm (or minutes 0-1439).")); return;
          }
          g.timeStartMin = s; g.timeEndMin = e; g.flags |= TRIG_FLAG_USE_TIME;
        } else if (commandEqualsIgnoreCase(tok, "WD")) {
          if (!parseUint32Text(a, v) || v > 127U) { Serial.println(F("ERROR: WD must be 0..127.")); return; }
          g.weekdayMask = static_cast<uint8_t>(v);
        } else if (commandEqualsIgnoreCase(tok, "LOG")) {
          if (commandEqualsIgnoreCase(a, "AND") || !strcmp(a, "0")) g.logic = RULE_LOGIC_AND;
          else if (commandEqualsIgnoreCase(a, "OR") || !strcmp(a, "1")) g.logic = RULE_LOGIC_OR;
          else { Serial.println(F("ERROR: LOG must be AND or OR.")); return; }
        } else if (commandEqualsIgnoreCase(tok, "INT")) {
          if (!parseUint32Text(a, v) || v == 0U || v > 65535U) { Serial.println(F("ERROR: INT must be 1..65535.")); return; }
          g.intervalSec = static_cast<uint16_t>(v); g.flags |= TRIG_FLAG_USE_INTERVAL;
        } else if (commandEqualsIgnoreCase(tok, "TX")) {
          if (!parseUint32Text(a, v) || v == 0U || v > 65535U) { Serial.println(F("ERROR: invalid command id.")); return; }
          if (!addStep(STEP_TX, v)) return;
        } else if (commandEqualsIgnoreCase(tok, "WAIT")) {
          if (!parseUint32Text(a, v) || v == 0U || v > 65535U) { Serial.println(F("ERROR: WAIT must be 1..65535 s.")); return; }
          if (!addStep(STEP_WAIT, v)) return;
        } else {
          Serial.print(F("ERROR: unknown keyword: ")); Serial.println(tok); return;
        }
      }

      rule.triggerCount = static_cast<uint8_t>(ti + 1U);
      rule.ruleFlags    = RULE_FLAG_ENABLED;
      rule.cooldownSec  = static_cast<uint16_t>(cd);

      for (uint8_t i = 0; i < rule.triggerCount; ++i) {
        const RuleTriggerStored &g = rule.triggers[i];
        if (g.flags == 0U)      { Serial.print(F("ERROR: trigger ")); Serial.print(i + 1); Serial.println(F(" has no TEMP/TIME/INT condition.")); return; }
        if (g.stepCount == 0U)  { Serial.print(F("ERROR: trigger ")); Serial.print(i + 1); Serial.println(F(" has no actions (TX/WAIT).")); return; }
        if (g.tempDurationSec && !(g.flags & TRIG_FLAG_USE_TEMP)) { Serial.println(F("ERROR: DUR needs TEMP.")); return; }
      }
      if (!automationValidateRule(rule)) {
        Serial.println(F("ERROR: rule failed validation (INT can't mix with TEMP/TIME; need at least one TX).")); return;
      }
      if (!storageAppendRule(id, rule)) { Serial.println(F("ERROR: rule write failed.")); return; }

      automationCancelRuleJobs(static_cast<uint8_t>(id - 1U));
      RuleSlot &slot = automationRules[id - 1U];
      memset(&slot, 0, sizeof(slot));
      slot.data = rule;
      Serial.print(F("Rule ")); Serial.print(id); Serial.print(F(" saved with "));
      Serial.print(rule.triggerCount); Serial.println(F(" trigger(s)."));
      return;
    }

    Serial.println(F("Usage: RULE LIST | SHOW <id> | ENABLE <id> | DISABLE <id> | DELETE <id> | ADD <id> ..."));
    return;
  }

  if (commandStartsWithIgnoreCase(start, "SCHEDULE") &&
      (start[8] == '\0' || start[8] == ' ' || start[8] == '\t')) {
    char *p = start + 8;
    while (*p == ' ' || *p == '\t') ++p;

    if (commandEqualsIgnoreCase(p, "LIST") || *p == '\0') {
      Serial.println(F("=== SCHEDULES ==="));
      uint8_t n = 0;
      for (uint8_t i = 0; i < AUTOMATION_MAX_SCHEDULES; ++i) {
        const AutomationScheduleState &slot = automationSchedules[i];
        if (!slot.used) continue;
        const StoredSchedule &sc = slot.data;
        Serial.print(F("ID=")); Serial.print(slot.id);
        Serial.print(F("  "));
        Serial.print((sc.flags & SCHEDULE_FLAG_ENABLED) ? F("EN") : F("DIS"));
        Serial.print(F("  CMD=")); Serial.print(sc.commandId);
        Serial.print(F("  "));
        if (sc.hour < 10U) Serial.print('0');
        Serial.print(sc.hour);
        Serial.print(':');
        if (sc.minute < 10U) Serial.print('0');
        Serial.print(sc.minute);
        Serial.print(F("  WD=0x")); Serial.print(sc.weekdayMask, HEX);
        Serial.print(F("  DLY=")); Serial.print(sc.delaySec);
        Serial.print(F("  F=0x")); Serial.println(sc.flags, HEX);
        n++;
      }
      if (n == 0) Serial.println(F("(none)"));
      Serial.println(F("================="));
      return;
    }

    if (commandStartsWithIgnoreCase(p, "SHOW") &&
        (p[4] == ' ' || p[4] == '\t')) {
      char *q = p + 4;
      while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) ||
          id < 1U || id > AUTOMATION_MAX_SCHEDULES) {
        Serial.println(F("Usage: SCHEDULE SHOW <id>"));
        return;
      }
      AutomationScheduleState *slot = automationFindSchedule(id);
      if (slot == nullptr) {
        Serial.println(F("Not found."));
        return;
      }
      const StoredSchedule &sc = slot->data;
      Serial.print(F("Schedule ")); Serial.println(id);
      Serial.print(F("Enabled   : "));
      Serial.println((sc.flags & SCHEDULE_FLAG_ENABLED) ? F("YES") : F("NO"));
      Serial.print(F("Command   : ")); Serial.println(sc.commandId);
      Serial.print(F("Time      : "));
      if (sc.hour < 10U) Serial.print('0');
      Serial.print(sc.hour);
      Serial.print(':');
      if (sc.minute < 10U) Serial.print('0');
      Serial.println(sc.minute);
      Serial.print(F("Weekdays  : 0x")); Serial.println(sc.weekdayMask, HEX);
      Serial.print(F("Delay     : ")); Serial.print(sc.delaySec); Serial.println(F(" s"));
      Serial.print(F("Flags     : 0x")); Serial.println(sc.flags, HEX);
      return;
    }

    if (commandStartsWithIgnoreCase(p, "ENABLE") &&
        (p[6] == ' ' || p[6] == '\t')) {
      char *q = p + 6;
      while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) ||
          id < 1U || id > AUTOMATION_MAX_SCHEDULES) {
        Serial.println(F("Usage: SCHEDULE ENABLE <id>"));
        return;
      }
      if (!automationSetScheduleEnabled(id, true)) {
        Serial.println(F("ERROR: schedule not found or write failed."));
      } else {
        Serial.println(F("Schedule enabled."));
      }
      return;
    }

    if (commandStartsWithIgnoreCase(p, "DISABLE") &&
        (p[7] == ' ' || p[7] == '\t')) {
      char *q = p + 7;
      while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) ||
          id < 1U || id > AUTOMATION_MAX_SCHEDULES) {
        Serial.println(F("Usage: SCHEDULE DISABLE <id>"));
        return;
      }
      if (!automationSetScheduleEnabled(id, false)) {
        Serial.println(F("ERROR: schedule not found or write failed."));
      } else {
        Serial.println(F("Schedule disabled."));
      }
      return;
    }

    if (commandStartsWithIgnoreCase(p, "DELETE") &&
        (p[6] == ' ' || p[6] == '\t')) {
      char *q = p + 6;
      while (*q == ' ' || *q == '\t') ++q;
      uint32_t id = 0;
      if (!parseUint32Text(q, id) ||
          id < 1U || id > AUTOMATION_MAX_SCHEDULES) {
        Serial.println(F("Usage: SCHEDULE DELETE <id>"));
        return;
      }
      if (!automationDeleteSchedule(id)) {
        Serial.println(F("ERROR: schedule not found or delete failed."));
      } else {
        Serial.println(F("Schedule deleted."));
      }
      return;
    }

    if (commandStartsWithIgnoreCase(p, "ADD") &&
        (p[3] == ' ' || p[3] == '\t')) {
      char *q = p + 3;
      while (*q == ' ' || *q == '\t') ++q;
      char *idEnd = q;
      while (*idEnd && *idEnd != ' ' && *idEnd != '\t') ++idEnd;
      if (*idEnd == '\0') {
        Serial.println(F("Usage: SCHEDULE ADD <id> CMD <cid> H <h> M <m> [WD <mask>] [DLY <sec>] [F <byte>]"));
        return;
      }

      char idBuf[16] = {0};
      const size_t idLen = static_cast<size_t>(idEnd - q);
      if (idLen == 0U || idLen >= sizeof(idBuf)) {
        Serial.println(F("ERROR: invalid schedule ID."));
        return;
      }
      memcpy(idBuf, q, idLen);
      idBuf[idLen] = '\0';

      uint32_t id = 0;
      if (!parseUint32Text(idBuf, id) ||
          id < 1U || id > AUTOMATION_MAX_SCHEDULES) {
        Serial.println(F("ERROR: schedule ID must be 1..10."));
        return;
      }
      q = (*idEnd == '\0') ? idEnd : idEnd + 1;

      StoredSchedule schedule{};
      schedule.flags = SCHEDULE_FLAG_ENABLED;
      bool haveCmd = false;
      bool haveHour = false;
      bool haveMinute = false;

      while (*q) {
        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) break;

        char *tok = q;
        while (*q && *q != ' ' && *q != '\t') ++q;
        if (*q) { *q++ = '\0'; }

        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) {
          Serial.print(F("ERROR: missing value for "));
          Serial.println(tok);
          return;
        }

        char *val = q;
        while (*q && *q != ' ' && *q != '\t') ++q;
        if (*q) { *q++ = '\0'; }

        uint32_t v = 0;
        if (commandEqualsIgnoreCase(tok, "CMD")) {
          if (!parseUint32Text(val, v) || v == 0U) {
            Serial.println(F("ERROR: invalid CMD."));
            return;
          }
          schedule.commandId = v;
          haveCmd = true;
        } else if (commandEqualsIgnoreCase(tok, "H")) {
          if (!parseUint32Text(val, v) || v > 23U) {
            Serial.println(F("ERROR: H must be 0..23."));
            return;
          }
          schedule.hour = static_cast<uint8_t>(v);
          haveHour = true;
        } else if (commandEqualsIgnoreCase(tok, "M")) {
          if (!parseUint32Text(val, v) || v > 59U) {
            Serial.println(F("ERROR: M must be 0..59."));
            return;
          }
          schedule.minute = static_cast<uint8_t>(v);
          haveMinute = true;
        } else if (commandEqualsIgnoreCase(tok, "WD")) {
          if (!parseUint32Text(val, v) || v > 127U) {
            Serial.println(F("ERROR: WD must be 0..127."));
            return;
          }
          schedule.weekdayMask = static_cast<uint8_t>(v);
        } else if (commandEqualsIgnoreCase(tok, "DLY")) {
          if (!parseUint32Text(val, v)) {
            Serial.println(F("ERROR: invalid DLY."));
            return;
          }
          schedule.delaySec = v;
        } else if (commandEqualsIgnoreCase(tok, "F")) {
          if (!parseUint32Text(val, v) || v > 255U) {
            Serial.println(F("ERROR: F must be 0..255."));
            return;
          }
          schedule.flags = static_cast<uint8_t>(v);
        } else {
          Serial.print(F("ERROR: unknown keyword: "));
          Serial.println(tok);
          return;
        }
      }

      if (!haveCmd || !haveHour || !haveMinute) {
        Serial.println(F("ERROR: CMD, H and M are required."));
        return;
      }

      if (!automationAddOrUpdateSchedule(id, schedule)) {
        Serial.println(F("ERROR: schedule could not be saved."));
        return;
      }

      Serial.print(F("Schedule ")); Serial.print(id);
      Serial.println(F(" saved."));
      return;
    }

    Serial.println(F("Usage: SCHEDULE LIST | SCHEDULE SHOW <id> | SCHEDULE ENABLE <id> | SCHEDULE DISABLE <id> | SCHEDULE DELETE <id>"));
    Serial.println(F("       SCHEDULE ADD <id> CMD <cid> H <h> M <m> [WD <mask>] [DLY <sec>] [F <byte>]"));
    return;
  }

  if (commandEqualsIgnoreCase(start, "TIME")) { timePrintStatus(); return; }
  if (commandStartsWithIgnoreCase(start, "TIME") && (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "ON")) {
      if (!timeSetEnabled(true)) Serial.println(F("ERROR: NTP enable failed."));
      else {
        if (!configurationSaveSystem()) Serial.println(F("WARNING: NTP setting could not be persisted."));
        Serial.println(F("NTP enabled."));
      }
    } else if (commandEqualsIgnoreCase(p, "OFF")) {
      if (!timeSetEnabled(false)) Serial.println(F("ERROR: NTP disable failed."));
      else {
        if (!configurationSaveSystem()) Serial.println(F("WARNING: NTP setting could not be persisted."));
        Serial.println(F("NTP disabled."));
      }
    } else if (commandStartsWithIgnoreCase(p, "SERVER") && (p[6] == ' ' || p[6] == '\t')) {
      char *value = p + 6;
      while (*value == ' ' || *value == '\t') ++value;
      if (!timeSetServer(value)) Serial.println(F("ERROR: NTP server name must be 1-63 characters."));
      else {
        if (!configurationSaveSystem()) Serial.println(F("WARNING: NTP server could not be persisted."));
        Serial.println(F("NTP server updated."));
      }
    } else if (commandStartsWithIgnoreCase(p, "TZ") && (p[2] == ' ' || p[2] == '\t')) {
      char *value = p + 2;
      while (*value == ' ' || *value == '\t') ++value;
      bool neg = false;
      if (*value == '-') { neg = true; ++value; }
      else if (*value == '+') ++value;
      char *end = nullptr;
      const long parsed = strtol(value, &end, 10);
      if (end == value || *end != '\0' || parsed > 1440) {
        Serial.println(F("ERROR: GMT offset must be -1440 to +1440 minutes."));
      } else {
        const int16_t off = static_cast<int16_t>(neg ? -parsed : parsed);
        if (!timeSetGmtOffset(off)) Serial.println(F("ERROR: GMT offset out of range."));
        else {
          if (!configurationSaveSystem()) Serial.println(F("WARNING: GMT offset could not be persisted."));
          Serial.print(F("GMT offset set to ")); Serial.print(ntpGmtOffsetMinutes); Serial.println(F(" min."));
        }
      }
    } else if (commandStartsWithIgnoreCase(p, "DST") && (p[3] == ' ' || p[3] == '\t')) {
      char *value = p + 3;
      while (*value == ' ' || *value == '\t') ++value;
      char *end = nullptr;
      const long parsed = strtol(value, &end, 10);
      if (end == value || *end != '\0' || parsed < 0 || parsed > 120) {
        Serial.println(F("ERROR: DST offset must be 0-120 minutes."));
      } else {
        if (!timeSetDstOffset(static_cast<int16_t>(parsed))) Serial.println(F("ERROR: DST offset out of range."));
        else {
          if (!configurationSaveSystem()) Serial.println(F("WARNING: DST offset could not be persisted."));
          Serial.print(F("DST offset set to ")); Serial.print(ntpDstOffsetMinutes); Serial.println(F(" min."));
        }
      }
    } else {
      Serial.println(F("Usage: TIME | TIME ON|OFF | TIME SERVER <host> | TIME TZ <minutes> | TIME DST <minutes>"));
    }
    return;
  }

  if (commandEqualsIgnoreCase(start, "SERIAL")) {    Serial.print(F("Serial: ")); Serial.println(serialEnabled ? F("ENABLED") : F("DISABLED"));
    return;
  }
  if (commandStartsWithIgnoreCase(start, "SERIAL") && (start[6] == ' ' || start[6] == '\t')) {
    char *p = start + 6;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "ON")) {
      serialEnabled = true;
      if (!configurationSaveSystem()) Serial.println(F("WARNING: serial setting could not be persisted."));
      Serial.println(F("Serial enabled."));
    } else if (commandEqualsIgnoreCase(p, "OFF")) {
      Serial.println(F("Serial disabled. Reboot required to release the UART."));
      Serial.flush();
      serialEnabled = false;
      (void)configurationSaveSystem();
    } else {
      Serial.println(F("Usage: SERIAL | SERIAL ON | SERIAL OFF"));
    }
    return;
  }

  if (commandStartsWithIgnoreCase(start, "RENAME DEVICE") &&
      (start[13] == '\0' || start[13] == ' ' || start[13] == '\t')) {
    char *p = start + 13;
    while (*p == ' ' || *p == '\t') ++p;
    char *idEnd = p;
    while (*idEnd && *idEnd != ' ' && *idEnd != '\t') ++idEnd;
    if (*idEnd == '\0') { Serial.println(F("Usage: RENAME DEVICE <id> <new-name>")); return; }
    char idBuf[16] = {0};
    const size_t idLen = static_cast<size_t>(idEnd - p);
    if (idLen == 0 || idLen >= sizeof(idBuf)) { Serial.println(F("ERROR: invalid device ID.")); return; }
    memcpy(idBuf, p, idLen); idBuf[idLen] = '\0';
    uint32_t id = 0;
    if (!parseUint32Text(idBuf, id) || id == 0) { Serial.println(F("ERROR: invalid device ID.")); return; }
    char *namePtr = idEnd;
    while (*namePtr == ' ' || *namePtr == '\t') ++namePtr;
    size_t nameLen = strlen(namePtr);
    while (nameLen > 0 && (namePtr[nameLen - 1] == ' ' || namePtr[nameLen - 1] == '\t')) namePtr[--nameLen] = '\0';
    if (nameLen == 0 || nameLen > STORAGE_MAX_NAME) { Serial.println(F("Usage: RENAME DEVICE <id> <new-name>")); return; }
    if (irState != IrEngineState::IDLE) {
      Serial.println(F("ERROR: IR engine is busy."));
      gLastCommandOk = false;
      snprintf(gLastCommandError, sizeof(gLastCommandError), "IR engine busy");
      return;
    }
    if (!storageRenameCommand(id, namePtr)) {
      Serial.println(F("ERROR: rename failed."));
      gLastCommandOk = false;
      snprintf(gLastCommandError, sizeof(gLastCommandError), "rename failed");
    }
    return;
  }

  if (commandStartsWithIgnoreCase(start, "RENAME COMMAND") &&
      (start[14] == '\0' || start[14] == ' ' || start[14] == '\t')) {
    char *p = start + 14;
    while (*p == ' ' || *p == '\t') ++p;
    char *idEnd = p;
    while (*idEnd && *idEnd != ' ' && *idEnd != '\t') ++idEnd;
    if (*idEnd == '\0') { Serial.println(F("Usage: RENAME COMMAND <id> <new-name>")); return; }
    char idBuf[16] = {0};
    const size_t idLen = static_cast<size_t>(idEnd - p);
    if (idLen == 0 || idLen >= sizeof(idBuf)) { Serial.println(F("ERROR: invalid command ID.")); return; }
    memcpy(idBuf, p, idLen); idBuf[idLen] = '\0';
    uint32_t id = 0;
    if (!parseUint32Text(idBuf, id) || id == 0) { Serial.println(F("ERROR: invalid command ID.")); return; }
    char *namePtr = idEnd;
    while (*namePtr == ' ' || *namePtr == '\t') ++namePtr;
    size_t nameLen = strlen(namePtr);
    while (nameLen > 0 && (namePtr[nameLen - 1] == ' ' || namePtr[nameLen - 1] == '\t')) namePtr[--nameLen] = '\0';
    if (nameLen == 0 || nameLen > STORAGE_MAX_NAME) { Serial.println(F("Usage: RENAME COMMAND <id> <new-name>")); return; }
    if (irState != IrEngineState::IDLE) {
      Serial.println(F("ERROR: IR engine is busy."));
      gLastCommandOk = false;
      snprintf(gLastCommandError, sizeof(gLastCommandError), "IR engine busy");
      return;
    }
    if (!storageRenameCommand(id, namePtr)) {
      Serial.println(F("ERROR: rename failed."));
      gLastCommandOk = false;
      snprintf(gLastCommandError, sizeof(gLastCommandError), "rename failed");
    }
    return;
  }

  if (commandEqualsIgnoreCase(start, "DISCOVERY")) {
    Serial.print(F("Discovery mode: "));
    Serial.println(discoveryMode == DiscoveryMode::NATIVE_HA
                     ? F("NATIVE HA (homeassistant/...)")
                     : F("CUSTOM (integration, Alexa enabled)"));
    return;
  }
  if (commandStartsWithIgnoreCase(start, "DISCOVERY") &&
      (start[9] == ' ' || start[9] == '\t')) {
    char *p = start + 9;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "NATIVE") || commandEqualsIgnoreCase(p, "HA"))
      mqttSetDiscoveryMode(DiscoveryMode::NATIVE_HA);
    else if (commandEqualsIgnoreCase(p, "CUSTOM") || commandEqualsIgnoreCase(p, "INTEGRATION"))
      mqttSetDiscoveryMode(DiscoveryMode::CUSTOM);
    else
      Serial.println(F("Usage: DISCOVERY | DISCOVERY NATIVE | DISCOVERY CUSTOM"));
    return;
  }

  if (commandEqualsIgnoreCase(start, "HASS")) {
    Serial.print(F("HA MQTT discovery: ")); Serial.println(haDiscoveryEnabled ? F("ENABLED") : F("DISABLED"));
    return;
  }
  if (commandStartsWithIgnoreCase(start, "HASS") &&
      (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "ON")) {
      if (mqttHaDiscoverySetEnabled(true)) Serial.println(F("HA MQTT discovery enabled."));
      else Serial.println(F("ERROR: HA MQTT discovery setting was not changed."));
    } else if (commandEqualsIgnoreCase(p, "OFF")) {
      if (mqttHaDiscoverySetEnabled(false)) Serial.println(F("HA MQTT discovery disabled; retained entries will be cleared."));
      else Serial.println(F("ERROR: HA MQTT discovery setting was not changed."));
    } else {
      Serial.println(F("Usage: HASS | HASS ON | HASS OFF"));
    }
    return;
  }

  if (commandEqualsIgnoreCase(start, "PINS")) { printPinsStatus(); return; }
  if (commandStartsWithIgnoreCase(start, "PINS") &&
      (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    char *whichEnd = p;
    while (*whichEnd && *whichEnd != ' ' && *whichEnd != '\t') ++whichEnd;
    if (*whichEnd == '\0') { Serial.println(F("Usage: PINS <IRTX|IRRX|LED|TEMP> <gpio>")); return; }
    char which[8] = {0};
    const size_t whichLen = static_cast<size_t>(whichEnd - p);
    if (whichLen == 0 || whichLen >= sizeof(which)) { Serial.println(F("ERROR: invalid function name.")); return; }
    memcpy(which, p, whichLen); which[whichLen] = '\0';
    char *gpioPtr = whichEnd;
    while (*gpioPtr == ' ' || *gpioPtr == '\t') ++gpioPtr;
    if (*gpioPtr == '\0') { Serial.println(F("Usage: PINS <IRTX|IRRX|LED|TEMP> <gpio>")); return; }
    uint32_t gpio = 0;
    if (!parseUint32Text(gpioPtr, gpio) || gpio > 255U) {
      Serial.println(F("ERROR: invalid GPIO.")); return;
    }
    const char *err = nullptr;
    if (!setPinValueByName(which, static_cast<uint8_t>(gpio), &err)) {
      Serial.print(F("ERROR: ")); Serial.println(err ? err : "invalid pin change");
      return;
    }
    Serial.println(F("Pin updated. Reboot required for the change to take effect."));
    printPinsStatus();
    return;
  }

  if (commandEqualsIgnoreCase(start, "WIFI")) { wifiPrintStatus(); return; }
  if (commandStartsWithIgnoreCase(start, "WIFI") && (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "CONNECT")) {
      if (!wifiApplyConfiguration()) Serial.println(F("ERROR: Wi-Fi SSID is not configured."));
      else Serial.println(F("Wi-Fi connection started."));
    } else if (commandStartsWithIgnoreCase(p, "SSID") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!wifiSetSSID(value)) Serial.println(F("ERROR: SSID must be 1-32 characters (or empty to clear)."));
      else Serial.println(F("Wi-Fi SSID updated; use WIFI CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "PASS") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!wifiSetPassword(value)) Serial.println(F("ERROR: Wi-Fi password must be 0-63 characters."));
      else Serial.println(F("Wi-Fi password updated; use WIFI CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "HOST") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!wifiSetHostname(value)) Serial.println(F("ERROR: hostname must be 1-32 letters/digits/hyphens and not start/end with '-'."));
      else Serial.println(F("Wi-Fi hostname updated; use WIFI CONNECT to apply."));
    } else Serial.println(F("Usage: WIFI | WIFI ON | WIFI OFF | WIFI SSID <name> | WIFI PASS <password> | WIFI HOST <hostname> | WIFI CONNECT"));
    return;
  }

  if (commandEqualsIgnoreCase(start, "MQTT")) { mqttPrintStatus(); return; }
  if (commandStartsWithIgnoreCase(start, "MQTT") && (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "ON")) { mqttSetEnabled(true); Serial.println(F("MQTT enabled; use MQTT CONNECT to apply.")); }
    else if (commandEqualsIgnoreCase(p, "OFF")) { mqttSetEnabled(false); Serial.println(F("MQTT disabled; use MQTT CONNECT to apply.")); }
    else if (commandEqualsIgnoreCase(p, "CONNECT")) {
      if (!mqttApplyConfiguration()) Serial.println(F("ERROR: MQTT configuration incomplete or IR engine busy."));
      else Serial.println(F("MQTT configuration applied; connection will be attempted by the service."));
    } else if (commandStartsWithIgnoreCase(p, "BROKER") && (p[6] == ' ' || p[6] == '\t')) {
      char *value = p + 6;
      while (*value == ' ' || *value == '\t') ++value;
      if (!mqttSetBroker(value)) Serial.println(F("ERROR: broker must be 1-63 characters."));
      else Serial.println(F("MQTT broker updated; use MQTT CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "PORT") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      char *end = nullptr;
      const unsigned long parsed = strtoul(value, &end, 10);
      if (end == value || *end != '\0' || parsed == 0UL || parsed > 65535UL ||
          !mqttSetPort(static_cast<uint16_t>(parsed))) Serial.println(F("ERROR: MQTT port must be 1-65535."));
      else Serial.println(F("MQTT port updated; use MQTT CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "USER") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!mqttSetUsername(value)) Serial.println(F("ERROR: MQTT username must be 0-63 characters."));
      else Serial.println(F("MQTT username updated; use MQTT CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "PASS") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!mqttSetPassword(value)) Serial.println(F("ERROR: MQTT password must be 0-63 characters."));
      else Serial.println(F("MQTT password updated; use MQTT CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "CLIENT") && (p[6] == ' ' || p[6] == '\t')) {
      char *value = p + 6;
      while (*value == ' ' || *value == '\t') ++value;
      if (!mqttSetClientId(value)) Serial.println(F("ERROR: client ID must be 1-31 characters."));
      else Serial.println(F("MQTT client ID updated; use MQTT CONNECT to apply."));
    } else if (commandStartsWithIgnoreCase(p, "BASE") && (p[4] == ' ' || p[4] == '\t')) {
      char *value = p + 4;
      while (*value == ' ' || *value == '\t') ++value;
      if (!mqttSetBaseTopic(value)) Serial.println(F("ERROR: invalid MQTT base topic."));
      else Serial.println(F("MQTT base topic updated; use MQTT CONNECT to apply."));
    } else Serial.println(F("Usage: MQTT | MQTT ON | MQTT OFF | MQTT BROKER <host> | MQTT PORT <port> | MQTT USER <username> | MQTT PASS <password> | MQTT CLIENT <client-id> | MQTT BASE <base-topic> | MQTT CONNECT"));
    return;
  }

  if (commandEqualsIgnoreCase(start, "ST") || commandEqualsIgnoreCase(start, "STORAGE")) { storagePrintStatus(); return; }
  if (commandEqualsIgnoreCase(start, "LISTD")) { storageListDevices(); return; }
  if (commandEqualsIgnoreCase(start, "LISTC") || commandEqualsIgnoreCase(start, "LIST")) { storageListCommands(); return; }
  if (commandEqualsIgnoreCase(start, "DELETE ALL") || commandEqualsIgnoreCase(start, "RESETDB")) {
    if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
    else if (!storageResetDatabaseTargeted()) Serial.println(F("ERROR: targeted database reset failed."));
    else Serial.println(F("All devices and commands deleted. Database is empty."));
    return;
  }
  if (commandStartsWithIgnoreCase(start, "DELETE COMMAND") && (start[14] == '\0' || start[14] == ' ' || start[14] == '\t')) {
    char *p = start + 14;
    while (*p == ' ' || *p == '\t') ++p;
    uint32_t id = 0;
    if (*p == '\0') Serial.println(F("Usage: DELETE COMMAND <id>"));
    else {
      char *end = nullptr;
      unsigned long parsed = strtoul(p, &end, 10);
      if (end == p || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) Serial.println(F("ERROR: invalid command ID."));
      else { id = static_cast<uint32_t>(parsed); if (!storageDeleteCommand(id)) Serial.println(F("ERROR: command not found or could not be deleted.")); }
    }
    return;
  }
  if (commandStartsWithIgnoreCase(start, "DELETE DEVICE") && (start[13] == '\0' || start[13] == ' ' || start[13] == '\t')) {
    char *p = start + 13;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') { Serial.println(F("Usage: DELETE DEVICE <id>")); return; }
    char *end = nullptr;
    unsigned long parsed = strtoul(p, &end, 10);
    if (end == p || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) { Serial.println(F("ERROR: invalid device ID.")); return; }
    if (!storageDeleteDevice(static_cast<uint32_t>(parsed))) Serial.println(F("ERROR: device not found or could not be deleted."));
    return;
  }
  if (commandEqualsIgnoreCase(start, "TEMP")) { ds18b20PrintStatus(); return; }
  if (commandStartsWithIgnoreCase(start, "TEMP") && (start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "ON")) {
      if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
      else { ds18b20SetEnabled(true); Serial.println(F("DS18B20 service enabled.")); }
    } else if (commandEqualsIgnoreCase(p, "OFF")) {
      if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
      else { ds18b20SetEnabled(false); Serial.println(F("DS18B20 service disabled.")); }
    } else if (commandEqualsIgnoreCase(p, "NOW")) {
      if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
      else if (!ds18b20IsEnabled()) Serial.println(F("ERROR: DS18B20 service is disabled."));
      else { ds18b20State.conversionPending = false; ds18b20State.nextActionMs = 0UL; Serial.println(F("DS18B20 sample requested.")); }
    } else if (commandEqualsIgnoreCase(p, "CAL")) {
      const int16_t offset = ds18b20GetCalibrationCentiDegrees();
      const int32_t signedOffset = static_cast<int32_t>(offset);
      const bool negative = signedOffset < 0;
      const int32_t absoluteOffset = negative ? -signedOffset : signedOffset;
      Serial.print(F("DS18B20 calibration offset: "));
      if (negative) Serial.print('-');
      Serial.print(absoluteOffset / 100L); Serial.print('.');
      const int32_t fraction = absoluteOffset % 100L;
      if (fraction < 10L) Serial.print('0');
      Serial.print(fraction); Serial.println(F(" C"));
    } else if (commandEqualsIgnoreCase(p, "INT")) {
      const uint32_t intervalMs = ds18b20GetSampleIntervalMs();
      Serial.print(F("DS18B20 update interval: ")); Serial.print(intervalMs / 1000UL); Serial.println(F(" s"));
    } else if (commandStartsWithIgnoreCase(p, "INT") && (p[3] == ' ' || p[3] == '\t')) {
      if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
      else {
        char *intervalText = p + 3;
        while (*intervalText == ' ' || *intervalText == '\t') ++intervalText;
        if (*intervalText == '\0') Serial.println(F("Usage: TEMP INT <seconds>"));
        else {
          char *end = nullptr;
          unsigned long seconds = strtoul(intervalText, &end, 10);
          if (end == intervalText || *end != '\0' || seconds == 0UL ||
              seconds > (DS18B20_SAMPLE_INTERVAL_MAX_MS / 1000UL)) {
            Serial.print(F("Usage: TEMP INT <seconds> ("));
            Serial.print(DS18B20_SAMPLE_INTERVAL_MIN_MS / 1000UL); Serial.print(F("-"));
            Serial.print(DS18B20_SAMPLE_INTERVAL_MAX_MS / 1000UL); Serial.println(F(" s)"));
          } else {
            ds18b20SetSampleIntervalMs(static_cast<uint32_t>(seconds * 1000UL));
            Serial.print(F("DS18B20 update interval set to "));
            Serial.print(ds18b20GetSampleIntervalMs() / 1000UL); Serial.println(F(" s."));
          }
        }
      }
    } else if (commandStartsWithIgnoreCase(p, "CAL") && (p[3] == ' ' || p[3] == '\t')) {
      if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
      else {
        char *calText = p + 3;
        while (*calText == ' ' || *calText == '\t') ++calText;
        int16_t offset = 0;
        if (!parseSignedCentiDegrees(calText, offset)) Serial.println(F("Usage: TEMP CAL [+|-]degrees[.hundredths]"));
        else {
          ds18b20SetCalibrationCentiDegrees(offset);
          Serial.print(F("DS18B20 calibration set to "));
          const int32_t signedOffset = static_cast<int32_t>(offset);
          const bool negative = signedOffset < 0;
          const int32_t absoluteOffset = negative ? -signedOffset : signedOffset;
          if (negative) Serial.print('-');
          Serial.print(absoluteOffset / 100L); Serial.print('.');
          const int32_t fraction = absoluteOffset % 100L;
          if (fraction < 10L) Serial.print('0');
          Serial.print(fraction); Serial.println(F(" C."));
        }
      }
    } else Serial.println(F("Usage: TEMP | TEMP ON | TEMP OFF | TEMP NOW | TEMP CAL | TEMP CAL <offset>"));
    return;
  }
  if (commandStartsWithIgnoreCase(start, "HUE") && (start[3] == '\0' || start[3] == ' ' || start[3] == '\t')) {
    char *p = start + 3;
    while (*p == ' ' || *p == '\t') ++p;
    if (storage.currentCommandId == 0) Serial.println(F("ERROR: select/load a command first."));
    else if (*p == '\0') {
      bool enabled = false;
      if (!storageGetCommandHueEnabled(storage.currentCommandId, enabled)) Serial.println(F("ERROR: could not read command Hue setting."));
      else { Serial.print(F("Current command Hue/Alexa exposure: ")); Serial.println(enabled ? F("ON") : F("OFF")); }
    } else if (commandEqualsIgnoreCase(p, "ON") || commandEqualsIgnoreCase(p, "OFF")) {
      const bool enabled = commandEqualsIgnoreCase(p, "ON");
      if (!storageSetCommandHueEnabled(storage.currentCommandId, enabled)) Serial.println(F("ERROR: command Hue setting could not be saved."));
      else { Serial.print(F("Command Hue/Alexa exposure set to ")); Serial.println(enabled ? F("ON") : F("OFF")); }
    } else Serial.println(F("Usage: HUE | HUE ON | HUE OFF (current command)"));
    return;
  }
  if (commandEqualsIgnoreCase(start, "LED")) { statusLedPrintStatus(); return; }
  if (commandEqualsIgnoreCase(start, "ERROR ON")) { statusLedSetError(true); Serial.println(F("Status LED error indication enabled.")); return; }
  if (commandEqualsIgnoreCase(start, "ERROR OFF")) { statusLedSetError(false); Serial.println(F("Status LED error indication disabled.")); return; }
  if (commandEqualsIgnoreCase(start, "WIPEDB")) {
    if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
    else if (!storageWipeDatabase()) Serial.println(F("ERROR: WIPEDB failed."));
    else {
      clearLastCapture();
      storage.currentDeviceId = 0; storage.currentCommandId = 0;
      storage.currentDeviceName[0] = '\0'; storage.currentCommandName[0] = '\0';
      Serial.println(F("Database wiped successfully."));
    }
    return;
  }
  if (commandStartsWithIgnoreCase(start, "RELEARN") && (start[7] == '\0' || start[7] == ' ' || start[7] == '\t')) {
    char *p = start + 7;
    while (*p == ' ' || *p == '\t') ++p;
    if (commandEqualsIgnoreCase(p, "CANCEL")) {
      relearnConfirmationPending = false; relearnPendingCommandId = 0UL;
      Serial.println(F("RELEARN cancelled.")); return;
    }
    if (commandStartsWithIgnoreCase(p, "CONFIRM") && (p[7] == ' ' || p[7] == '\t')) {
      char *idText = p + 7;
      while (*idText == ' ' || *idText == '\t') ++idText;
      uint32_t id = 0;
      if (!parseUint32Text(idText, id) || id == 0 || !relearnConfirmationPending ||
          relearnPendingCommandId != id) {
        Serial.println(F("ERROR: no matching pending RELEARN confirmation.")); return;
      }
      relearnConfirmationPending = false; relearnPendingCommandId = 0UL;
      if (!storageRelearnCommand(id)) Serial.println(F("ERROR: RELEARN failed."));
      return;
    }
    uint32_t id = 0;
    if (!parseUint32Text(p, id) || id == 0) {
      Serial.println(F("Usage: RELEARN <id> | RELEARN CONFIRM <id> | RELEARN CANCEL")); return;
    }
    uint32_t sector = UINT32_MAX;
    uint32_t deviceId = 0;
    uint32_t sequence = 0;
    if (irState != IrEngineState::IDLE || !storageFindActiveCommand(id, sector, deviceId, sequence)) {
      Serial.println(F("ERROR: command ID not found or IR engine is busy.")); return;
    }
    StorageRecordHeader header{};
    if (!storageReadValidatedRecord(sector, header) || header.type != RECORD_TYPE_COMMAND ||
        header.recordId != id || header.nameLength == 0 || header.nameLength > STORAGE_MAX_NAME) {
      Serial.println(F("ERROR: command could not be read safely.")); return;
    }
    char name[STORAGE_MAX_NAME + 1] = {0};
    memcpy(name, storageRecordBuffer, header.nameLength);
    name[header.nameLength] = '\0';
    relearnConfirmationPending = true;
    relearnPendingCommandId = id;
    Serial.println(F("RELEARN confirmation required."));
    Serial.print(F("Command ID : ")); Serial.println(id);
    Serial.print(F("Device ID  : ")); Serial.println(deviceId);
    Serial.print(F("Command    : ")); Serial.println(name);
    Serial.println(F("WARNING: confirming will permanently delete the existing waveform before Learn starts."));
    Serial.print(F("Type RELEARN CONFIRM ")); Serial.print(id);
    Serial.println(F(" to continue, or RELEARN CANCEL."));
    (void)sequence;
    return;
  }
  if (commandStartsWithIgnoreCase(start, "DEVICE") && (start[6] == '\0' || start[6] == ' ' || start[6] == '\t')) {
    char name[STORAGE_MAX_NAME + 1] = {0};
    char *p = start + 6;
    while (*p == ' ' || *p == '\t') ++p;
    size_t nameLen = strlen(p);
    while (nameLen > 0 && (p[nameLen - 1] == ' ' || p[nameLen - 1] == '\t')) p[--nameLen] = '\0';
    if (nameLen == 0 || nameLen > STORAGE_MAX_NAME) Serial.println(F("Usage: DEVICE <name>"));
    else {
      memcpy(name, p, nameLen + 1);
      if (!storageSelectOrCreateDevice(name)) Serial.println(F("ERROR: device could not be selected/created."));
      else { Serial.print(F("Current device: ID=")); Serial.print(storage.currentDeviceId); Serial.print(F(" ")); Serial.println(storage.currentDeviceName); }
    }
    return;
  }
  if (commandStartsWithIgnoreCase(start, "SAVE") && (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    size_t nameLen = strlen(p);
    while (nameLen > 0 && (p[nameLen - 1] == ' ' || p[nameLen - 1] == '\t')) p[--nameLen] = '\0';
    if (nameLen == 0 || nameLen > STORAGE_MAX_NAME) Serial.println(F("Usage: SAVE <name>"));
    else if (!storageSaveCurrentCommand(p)) Serial.println(F("ERROR: command could not be saved."));
    else { Serial.print(F("Command saved. ID=")); Serial.println(storage.currentCommandId); }
    return;
  }
  if (commandStartsWithIgnoreCase(start, "LOAD") && (start[4] == '\0' || start[4] == ' ' || start[4] == '\t')) {
    char *p = start + 4;
    while (*p == ' ' || *p == '\t') ++p;
    uint32_t id = 0;
    if (*p == '\0') Serial.println(F("Usage: LOAD <id>"));
    else if (!parseUint32Text(p, id) || id == 0 || !storageLoadCommand(id))
      Serial.println(F("ERROR: command ID not found or failed integrity check."));
    else { Serial.print(F("Loaded command ID=")); Serial.println(id); }
    return;
  }

  uint32_t value = 0;
  switch (start[0]) {
    case 'L':
      if (start[1] == '\0') {
        if (storage.currentDeviceId == 0) Serial.println(F("ERROR: select/create a device before starting Learn."));
        else {
          statusLedStartLearning();
          const bool learned = learnSignal();
          statusLedStopLearning();
          if (learned) clearCurrentCommandRuntimeMetadata();
        }
      } else Serial.println(F("Usage: L"));
      break;
    case 'T':
      if (start[1] == '\0') { const bool transmitted = transmitLastCapture(); if (transmitted) statusLedPulseTransmit(); }
      else Serial.println(F("Usage: T"));
      break;
    case 'P':
      if (start[1] == '\0') printStoredRaw();
      else Serial.println(F("Usage: P"));
      break;
    case 'C':
      if (start[1] == '\0') {
        if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: IR engine is busy."));
        else { clearLastCapture(); Serial.println(F("Stored capture cleared.")); }
      } else Serial.println(F("Usage: C"));
      break;
    case 'F':
      if (start[1] == '\0') { Serial.print(F("TX frequency: ")); Serial.print(txFrequencyHz); Serial.println(F(" Hz")); }
      else if (parseUnsignedSuffix(start, value)) {
        if (value < MIN_TX_FREQUENCY_HZ || value > MAX_TX_FREQUENCY_HZ) {
          Serial.print(F("ERROR: frequency must be ")); Serial.print(MIN_TX_FREQUENCY_HZ);
          Serial.print(F("-")); Serial.print(MAX_TX_FREQUENCY_HZ); Serial.println(F(" Hz."));
        } else if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: cannot change frequency while IR engine is busy."));
        else { txFrequencyHz = static_cast<uint16_t>(value);
               Serial.print(F("TX frequency set to ")); Serial.print(txFrequencyHz); Serial.println(F(" Hz.")); }
      } else Serial.println(F("Usage: F or F38000"));
      break;
    case 'R':
      if (start[1] == '\0') { Serial.print(F("Repeat count: ")); Serial.println(txRepeatCount); }
      else if (parseUnsignedSuffix(start, value)) {
        if (value < 1 || value > 255) Serial.println(F("ERROR: repeat count must be 1-255."));
        else if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: cannot change repeat count while IR engine is busy."));
        else { txRepeatCount = static_cast<uint8_t>(value);
               Serial.print(F("Repeat count set to ")); Serial.println(txRepeatCount); }
      } else Serial.println(F("Usage: R or R3"));
      break;
    case 'G':
      if (start[1] == '\0') { Serial.print(F("Repeat gap: ")); Serial.print(txRepeatGapMs); Serial.println(F(" ms")); }
      else if (parseUnsignedSuffix(start, value)) {
        if (value > UINT16_MAX) Serial.println(F("ERROR: repeat gap must be 0-65535 ms."));
        else if (irState != IrEngineState::IDLE) Serial.println(F("ERROR: cannot change repeat gap while IR engine is busy."));
        else { txRepeatGapMs = static_cast<uint16_t>(value);
               Serial.print(F("Repeat gap set to ")); Serial.print(txRepeatGapMs); Serial.println(F(" ms.")); }
      } else Serial.println(F("Usage: G or G100"));
      break;
    case 'H':
      if (start[1] == '\0') printHelp(); else Serial.println(F("Usage: H"));
      break;
    case 'D':
      if (start[1] == '\0') printDiagnostics(); else Serial.println(F("Usage: D"));
      break;
    default: Serial.println(F("Unknown command. Press H for help.")); break;
  }
}

void serviceSerial() {
  if (!serialEnabled) return;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialLineOverflow) Serial.println(F("ERROR: serial command too long."));
      else {
        serialLine[serialLineLength] = '\0';
        const bool storageCommand = isStorageCommandLine(serialLine);
        processSerialCommand(serialLine);
        if (storageCommand) discardQueuedSerialInput();
      }
      serialLineLength = 0;
      serialLineOverflow = false;
      continue;
    }
    if (static_cast<uint8_t>(c) < 32U) continue;
    if (serialLineOverflow) continue;
    if (serialLineLength < SERIAL_LINE_SIZE - 1U) serialLine[serialLineLength++] = c;
    else serialLineOverflow = true;
  }
}

#endif

#include "webui_backend.h"

// ============================================================
// STAGE INFORMATION
// ============================================================

#if STAGE1_SERIAL_DEBUG
void printStageInfo() {
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("ESP8266 UNIVERSAL IR HUB"));
  Serial.println(F("=================================================="));
  Serial.print(F("Firmware       : ")); Serial.println(FIRMWARE_VERSION);
  Serial.print(F("IR library     : ")); Serial.println(_IRREMOTEESP8266_VERSION_STR);
  Serial.println(F("Architecture   : Stage 3 - IR + Storage + Temperature + Status LED + Wi-Fi + MQTT + NTP + Automation + WebUI"));
  Serial.println();
  Serial.println(F("FINAL GPIO MAP"));
  Serial.println(F("GPIO0  -> Reserved ESP8266 boot strap (physical Learn button removed)"));
  Serial.println(F("GPIO1  -> UART0 TX (available)"));
  Serial.print(F("GPIO")); Serial.print(pinStatusLed); Serial.println(F("  -> Status LED (active LOW)"));
  Serial.println(F("GPIO3  -> UART0 RX (available)"));
  Serial.print(F("GPIO")); Serial.print(pinIrTx); Serial.println(F("  -> IR TX / AO3400 gate"));
  Serial.print(F("GPIO")); Serial.print(pinIrRx); Serial.println(F("  -> IR RX"));
  Serial.println(F("GPIO12 -> W25Qxx MISO"));
  Serial.println(F("GPIO13 -> W25Qxx MOSI"));
  Serial.println(F("GPIO14 -> W25Qxx SCLK"));
  Serial.println(F("GPIO15 -> W25Qxx CS"));
  Serial.print(F("GPIO")); Serial.print(pinDs18b20); Serial.println(F(" -> DS18B20"));
  Serial.println();
  Serial.print(F("Free heap       : ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("HA discovery    : ")); Serial.println(haDiscoveryEnabled ? F("ENABLED") : F("DISABLED"));
  Serial.print(F("NTP enabled     : ")); Serial.println(ntpEnabled ? F("YES") : F("NO"));
  Serial.print(F("Serial enabled  : ")); Serial.println(serialEnabled ? F("YES") : F("NO"));
  Serial.print(F("Hub name        : ")); Serial.println(webUiHubName);
  Serial.println();
  Serial.println(F("IR receiver idle state: DISABLED"));
  Serial.println(F("W25Qxx persistent storage: ENABLED when detected"));
  Serial.println(F("WebUI: http://<ip>/ (v11 UI, single HTTP server on port 80)"));
  Serial.println(F("Pin changes take effect at next boot."));
  Serial.println(F("=================================================="));
  Serial.println();
}
#endif

#if STAGE1_SERIAL_DEBUG
void printDiagnostics() {
  if (!serialEnabled) return;
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("STAGE 1 DIAGNOSTICS"));
  Serial.println(F("========================================"));
  Serial.print(F("Learn attempts       : ")); Serial.println(irStats.learnAttempts);
  Serial.print(F("Successful learns    : ")); Serial.println(irStats.successfulLearns);
  Serial.print(F("Learn timeouts       : ")); Serial.println(irStats.learnTimeouts);
  Serial.print(F("Learn overflows      : ")); Serial.println(irStats.learnOverflows);
  Serial.print(F("Malformed captures   : ")); Serial.println(irStats.malformedCaptures);
  Serial.print(F("Store failures       : ")); Serial.println(irStats.storeFailures);
  Serial.print(F("TX started           : ")); Serial.println(irStats.transmissionsStarted);
  Serial.print(F("TX completed         : ")); Serial.println(irStats.transmissionsCompleted);
  Serial.print(F("TX rejected          : ")); Serial.println(irStats.transmissionRejected);
  Serial.print(F("Max receiver rawlen  : ")); Serial.println(irStats.maxCapturedRawLen);
  Serial.print(F("Max stored rawlen    : ")); Serial.println(irStats.maxStoredRawLen);
  Serial.print(F("Max waveform duration: ")); Serial.print(irStats.maxWaveformDurationUs); Serial.println(F(" us"));
  Serial.print(F("Current free heap    : ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("Minimum free heap    : "));
  if (minFreeHeap == UINT32_MAX) Serial.println(F("not measured"));
  else Serial.println(minFreeHeap);
  Serial.print(F("Stored CRC32         : 0x")); Serial.println(lastRawCrc32, HEX);
  Serial.print(F("Stored integrity     : "));
  Serial.println(storedSignalIntegrityOK() ? F("PASS") : F("NONE/FAIL"));
  Serial.println(F("========================================"));
  Serial.println();
}
#endif

// >>> v11 INTEGRATION — webui_backend.h included here because it depends on
// every type and helper declared above (storage, hue, mqtt, automation, serial).

// ============================================================
// SETUP
// ============================================================

void setup() {
  configurationStorageInitialize();
  configurationLoadSystem();
  configurationLoadPins();
  (void)configurationLoadStaticIp();       // ← new line
  (void)configurationLoadTempCal();
  configurationLoadHa();
  // >>> v11 INTEGRATION — load persisted WebUI hub name
  (void)webUiLoadHubName();

#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    Serial.begin(115200);
    delay(300);
  } else {
    Serial.end();
  }
#endif

  irrecvPtr = new IRrecv(pinIrRx,
                         IRRECV_BUFFER_SIZE,
                         static_cast<uint8_t>(CAPTURE_TIMEOUT_MS),
                         SAVE_BUFFER);
  irsendPtr = new IRsend(pinIrTx);

  initializeHardwarePins();
  wifiInitialize();
  timeInitialize();
  updateMinimumHeap();

  stopIRReceiver();

#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    delay(200);
    printStageInfo();
    hardwareSelfTest();
  }
#endif

  initializeStorageEngine();
  statusLedSetStorageError(!storage.initialized);

  mqttInitialize();

  irsendPtr->begin();
  digitalWrite(pinIrTx, LOW);

  hueInitialize();
  automationInitialize();
  otaInitialize();      // >>> NEW

#if STAGE1_SERIAL_DEBUG
  if (serialEnabled) {
    printHelp();
    Serial.print(FIRMWARE_VERSION);
    Serial.println(F(" + WebUI v11 ready."));
    Serial.println();
  }
#endif
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  feedWatchdog();
  otaService(); 
   if (wifiRecoveryState != WifiRecoveryState::OFF) {
    captiveDnsService();   // ← add this line
    // (wifiService() still runs, otherwise we can't detect recovery)
  } 
  wifiService();
  statusLedServiceWifiErrorDelay();   // >>> v1.0

  // Service the existing port-80 HTTP/Hue server as early as possible. This
  // keeps WebUI button presses and navigation from waiting behind the slower
  // background services below. The same single Hue/WebUI server is used.
  hueService();

  wifiWatchdogService();
  timeService();
  statusLedService();
  ds18b20Service();
  mqttService();
  mqttServiceAlexaDirective();
  storageJobService();          // ← add this
  webUiServiceLearning();
  webUiServiceWifiScan();
  webUiServiceWifiApply();
  automationService();

#if STAGE1_SERIAL_DEBUG
  serviceSerial();
#endif

  updateMinimumHeap();
}