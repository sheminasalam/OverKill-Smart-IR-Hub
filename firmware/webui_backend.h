#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

static ESP8266WebServer        otaHttpServer(8080);
static ESP8266HTTPUpdateServer otaUpdater;
static bool                    otaInitialized = false;

// ── OTA quiet mode ─────────────────────────────────────────
// Called by the WebUI immediately before an OTA upload begins.
// Frees every background service that is not strictly required
// for the transfer so the radio, lwIP heap, and CPU are dedicated
// to receiving the firmware image.
static bool otaQuietMode = false;

static void otaEnterQuietMode() {
  if (otaQuietMode) return;
  otaQuietMode = true;

  // 1. Stop the port-80 HTTP server and SSDP.
  hueStopNetworking();

  // 2. Disconnect MQTT and release its socket.
  if (mqttClient.connected()) {
    (void)mqttPublishAvailability(false);
    mqttClient.disconnect();
  }
  mqttNetworkClient.stop();

  // 3. Close the watchdog UDP socket.
  if (wifiWatchdogSocketOpen) {
    wifiWatchdogUdp.stop();
    wifiWatchdogSocketOpen = false;
  }

  // 4. Stop the recovery AP so the radio only serves STA.
  if (wifiRecoveryState != WifiRecoveryState::OFF) {
    WiFi.softAPdisconnect(true);
    captiveDnsStop();
    wifiRecoveryState = WifiRecoveryState::OFF;
    wifiRecoveryApConnectedAtMs = 0UL;
  }
  WiFi.mode(WIFI_STA);

  // 5. Force the radio into the most stable configuration.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(20.5f);

  // 6. Let everything settle before the upload begins.
  for (uint8_t i = 0; i < 20; ++i) { delay(10); ESP.wdtFeed(); yield(); }
}


static void otaInitialize() {
  if (otaInitialized) return;
  otaUpdater.setup(&otaHttpServer, "/update");
  otaHttpServer.begin();
  otaInitialized = true;
  Serial.println(F("OTA web server ready on port 8080 path /update"));
}

static void otaService() {
  if (!otaInitialized) return;
  otaHttpServer.handleClient();
}
// ============================================================
// WEB UI BACKEND — v12 (command-line driven)
//
// The WebUI does not reimplement any IR/storage logic. Every
// state-changing action is expressed as the SAME command line a
// serial user would type and forwarded to processSerialCommand().
//
// Only two things live here that are not 1:1 serial verbs:
//   - a tiny state machine around the blocking "L" command so the
//     HTTP handler never blocks for the 15-second learn window
//   - read-only JSON getters for the dashboard / config pages
// ============================================================


constexpr uint32_t CONFIG_WEBUI_MAGIC   = 0x57425549UL;   // 'WBUI'
constexpr uint16_t CONFIG_WEBUI_VERSION = 1U;

#pragma pack(push, 1)
struct PersistentWebUiConfiguration {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
  char     hubName[WEBUI_HUB_NAME_MAX + 1U];
};
#pragma pack(pop)

// Forward declaration of the serial command processor that lives
// in the .ino.
void processSerialCommand(char *line);
extern bool gLastCommandOk;
extern char gLastCommandError[64];
void clearLastCapture();
bool transmitLastCapture();
void statusLedPulseTransmit();

// ---- Stage 2: shared WebUI scratch --------------------------
// The four HTTP serialization routes that follow (devices list,
// device detail, rules list, /api/web/cmd body) are mutually
// exclusive: the server handles one connection at a time and does
// not re-enter until the previous response is closed. A single
// union is therefore safe and reclaims ~2.4 KB of BSS.
struct WebUiDevEntry {
  uint32_t id;
  char     name[STORAGE_MAX_NAME + 1U];
  uint8_t  cat;
  bool     cloud;
};
struct WebUiCmdEntry {
  uint32_t id;
  char     name[STORAGE_MAX_NAME + 1U];
  bool     alexa;
  uint16_t rawLen;
  uint16_t hz;
  uint16_t gap;
  char     ch[CHANNEL_MAX_DIGITS + 1U];
  uint8_t  rep;
};
union WebUiScratch {
  WebUiDevEntry devEntries[32];
  WebUiCmdEntry cmdEntries[32];
  char          rulesBuf[512];
  char          cmdLine[640];
};
static WebUiScratch webUiScratch;

// ---- Learn state machine -----------------------------------
// WebUI drives learning by feeding the SAME lines a serial user
// would type:
//   POST /api/web/learn/begin  {"mode":"new_command","device_name":"Living Room"}
//       -> runs "DEVICE Living Room" then queues "L"
//   POST /api/web/learn/begin  {"mode":"relearn","command_id":5}
//       -> runs "RELEARN 5" then queues "RELEARN CONFIRM 5"
//   POST /api/web/learn/save   {"name":"turbo"}
//       -> runs "SAVE turbo"
// The queued blocking line is executed from loop() so the HTTP
// handler never has to wait for the 15-second learn window.
enum class WebLearnKind : uint8_t { NONE, LEARN_RAW, RELEARN_COMMAND };
enum class WebLearnState : uint8_t {
  IDLE, PENDING, LEARNING, AWAITING_NAME, SUCCESS, FAILED
};

struct WebLearnRequest {
  WebLearnKind kind = WebLearnKind::NONE;
  char         line[96] = {0};       // the raw command to run in loop()
  uint32_t     token = 0;
  uint32_t     targetCommandId = 0;  // for RELEARN_COMMAND
};
static WebLearnRequest webLearnReq;
static WebLearnState   webLearnState = WebLearnState::IDLE;
static char            webLearnMessage[64] = {0};
static uint32_t        webLearnTokenSeq = 0;

// ---- Deferred Wi-Fi apply ---------------------------------
// webUiServeWifiConfig() must not disconnect the STA interface while the HTTP
// response is still in flight. We send the response, flush it, then set this
// flag. loop() picks it up ~1 second later and applies the new config.

static bool     webUiWifiApplyPending = false;
static uint32_t webUiWifiApplyAtMs    = 0UL;

static void webUiServiceWifiApply() {
  if (!webUiWifiApplyPending) return;
  if (static_cast<int32_t>(millis() - webUiWifiApplyAtMs) < 0) return;
  webUiWifiApplyPending = false;

  if (wifiConfig.ssid[0] != '\0') {
    wifiConfigurationDirty = false;
    wifiStartConnection();
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    wifiConnectAttemptActive = false;
    wifiConfigurationDirty = false;
    wifiState = WifiState::UNCONFIGURED;
    statusLedSetWifiError(false);
  }
}

// ---- Wi-Fi scan state (unchanged, async) -------------------
enum class WebWifiScanState : uint8_t { IDLE, SCANNING, READY };
static WebWifiScanState webWifiScanState   = WebWifiScanState::IDLE;
static bool             webWifiScanRequested = false;
static uint32_t         webWifiScanStart     = 0UL;
static uint32_t         webWifiScanReadyMs   = 0UL;
static int8_t           webWifiScanResult    = 0;

// Called from wifiWatchdogService() in the .ino, which is compiled
// before this header is included. Tells the watchdog to hold its miss
// counter while the radio is parked scanning individual channels.
bool webUiWifiScanInProgress() {
  return (webWifiScanState == WebWifiScanState::SCANNING) || webWifiScanRequested;
}

// ---------- JSON helpers (bounded) --------------------------
static const char *webJsonFindKey(const char *json, const char *key) {
  if (!json || !key) return nullptr;
  char needle[48];
  const int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(needle)) return nullptr;
  const char *p = strstr(json, needle);
  if (!p) return nullptr;
  p += n;
  while (*p == ' ' || *p == '\t' || *p == ':') ++p;
  return p;
}
static bool webJsonGetString(const char *json, const char *key, char *out, size_t cap) {
  if (!out || cap < 2) return false;
  out[0] = '\0';
  const char *p = webJsonFindKey(json, key);
  if (!p || *p != '"') return false;
  ++p;
  size_t i = 0;
  while (*p && *p != '"' && i + 1U < cap) {
    if (*p == '\\' && p[1]) {
      if      (p[1] == '"')  out[i++] = '"';
      else if (p[1] == '\\') out[i++] = '\\';
      else if (p[1] == 'n')  out[i++] = '\n';
      else if (p[1] == 't')  out[i++] = '\t';
      else                   out[i++] = p[1];
      p += 2;
    } else {
      out[i++] = *p++;
    }
  }
  out[i] = '\0';
  return (*p == '"');
}
static bool webJsonGetUint(const char *json, const char *key, uint32_t &out) {
  const char *p = webJsonFindKey(json, key);
  if (!p) return false;
  uint32_t v = 0; bool any = false;
  while (*p >= '0' && *p <= '9') {
    any = true;
    const uint32_t d = (uint32_t)(*p - '0');
    if (v > (UINT32_MAX - d) / 10UL) return false;
    v = v * 10UL + d; ++p;
  }
  if (!any) return false;
  out = v; return true;
}
static bool webJsonGetInt(const char *json, const char *key, int32_t &out) {
  const char *p = webJsonFindKey(json, key);
  if (!p) return false;
  int sign = 1;
  if (*p == '-') { sign = -1; ++p; }
  else if (*p == '+') ++p;
  uint32_t u = 0; bool any = false;
  while (*p >= '0' && *p <= '9') {
    any = true;
    u = u * 10UL + (uint32_t)(*p - '0');
    if (u > 100000000UL) return false;
    ++p;
  }
  if (!any) return false;
  out = (int32_t)u * sign;
  return true;
}
static bool webJsonGetBool(const char *json, const char *key, bool &out) {
  const char *p = webJsonFindKey(json, key);
  if (!p) return false;
  if (!strncmp(p, "true",  4)) { out = true;  return true; }
  if (!strncmp(p, "false", 5)) { out = false; return true; }
  if (*p == '1')               { out = true;  return true; }
  if (*p == '0')               { out = false; return true; }
  return false;
}

// ---- Persistence ------------------------------------------
static bool webUiLoadConfiguration(size_t slotOffset, bool storageReady,
                                   bool (*crcCheck)(const uint8_t*, size_t, size_t, uint32_t)) {
  if (!storageReady) return false;
  PersistentWebUiConfiguration rec{};
  EEPROM.get(slotOffset, rec);
  const bool valid =
      rec.magic == CONFIG_WEBUI_MAGIC &&
      rec.version == CONFIG_WEBUI_VERSION &&
      rec.payloadSize == sizeof(rec) - offsetof(PersistentWebUiConfiguration, hubName) &&
      crcCheck(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec),
               offsetof(PersistentWebUiConfiguration, crc32), rec.crc32) &&
      rec.hubName[sizeof(rec.hubName) - 1U] == '\0' &&
      rec.hubName[0] != '\0';
  if (!valid) return false;
  memcpy(webUiHubName, rec.hubName, sizeof(webUiHubName));
  return true;
}
static bool webUiSaveConfiguration(size_t slotOffset, bool storageReady,
                                   uint32_t (*crc32)(const uint8_t*, size_t)) {
  if (!storageReady) return false;
  PersistentWebUiConfiguration rec{};
  rec.magic = CONFIG_WEBUI_MAGIC;
  rec.version = CONFIG_WEBUI_VERSION;
  rec.payloadSize = sizeof(rec) - offsetof(PersistentWebUiConfiguration, hubName);
  rec.crc32 = 0U;
  memcpy(rec.hubName, webUiHubName, sizeof(rec.hubName));
  rec.hubName[sizeof(rec.hubName) - 1U] = '\0';
  rec.crc32 = crc32(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  EEPROM.put(slotOffset, rec);
  return EEPROM.commit();
}

bool webUiLoadHubName() {
  return webUiLoadConfiguration(CONFIG_WEBUI_SLOT_OFFSET,
                                configurationStorageReady,
                                configurationRecordCRCValid);
}
bool webUiPersistHubName() {
  return webUiSaveConfiguration(CONFIG_WEBUI_SLOT_OFFSET,
                                configurationStorageReady,
                                crc32Bytes);
}

// ---- Command-line passthrough ------------------------------
// processSerialCommand() mutates its buffer (upper-cases the first
// character). We always hand it a writable copy.
static void webUiRunSerialLine(const char *line) {
  if (!line || !*line) return;
  char local[160];
  strncpy(local, line, sizeof(local) - 1);
  local[sizeof(local) - 1] = '\0';
  processSerialCommand(local);
}

// ---- Learn state machine (called from loop()) --------------
static void webUiServiceLearning() {
  if (webLearnState != WebLearnState::PENDING) return;
  if (irState != IrEngineState::IDLE) return;

  webLearnState = WebLearnState::LEARNING;

  // Run the same command the user would have typed. Both "L" and
  // "RELEARN CONFIRM <id>" block for the full learn window; that is
  // exactly why this lives in loop() and not in the HTTP handler.
  webUiRunSerialLine(webLearnReq.line);

  // Interpret the result: we do not reimplement anything, we just
  // look at the same state the command left behind.
  if (webLearnReq.kind == WebLearnKind::LEARN_RAW) {
    if (hasLearnedSignal()) {
      webLearnState = WebLearnState::AWAITING_NAME;
      strncpy(webLearnMessage,
              "Captured. Enter a name and press Save.",
              sizeof(webLearnMessage) - 1U);
    } else {
      webLearnState = WebLearnState::FAILED;
      strncpy(webLearnMessage,
              "Capture failed or timed out.",
              sizeof(webLearnMessage) - 1U);
    }
  } else if (webLearnReq.kind == WebLearnKind::RELEARN_COMMAND) {
    // storageRelearnCommand() calls clearLastCapture() first, then
    // learnSignal(). So a fresh capture is exactly the success signal.
    if (hasLearnedSignal()) {
      webLearnState = WebLearnState::SUCCESS;
      strncpy(webLearnMessage, "Relearn successful.",
              sizeof(webLearnMessage) - 1U);
    } else {
      webLearnState = WebLearnState::FAILED;
      strncpy(webLearnMessage, "Relearn failed.",
              sizeof(webLearnMessage) - 1U);
    }
  } else {
    webLearnState = WebLearnState::FAILED;
    strncpy(webLearnMessage, "Unknown learn kind.",
            sizeof(webLearnMessage) - 1U);
  }
}

// ---- Wi-Fi scan service (unchanged) ------------------------
static void webUiServiceWifiScan() {
  // A new request may start from IDLE or from READY (old results still held).
  if (webWifiScanRequested && webWifiScanState != WebWifiScanState::SCANNING) {
    webWifiScanRequested = false;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    webWifiScanStart = millis();
    webWifiScanState = WebWifiScanState::SCANNING;
    return;
  }
  switch (webWifiScanState) {
    case WebWifiScanState::IDLE:
      if (webWifiScanRequested) {
        webWifiScanRequested = false;
        WiFi.scanDelete();
        WiFi.scanNetworks(true, true);
        webWifiScanStart = millis();
        webWifiScanState = WebWifiScanState::SCANNING;
      }
      return;
    case WebWifiScanState::SCANNING: {
      const int8_t n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        if (millis() - webWifiScanStart >= 15000UL) {
          WiFi.scanDelete();
          webWifiScanResult  = WIFI_SCAN_FAILED;
          webWifiScanReadyMs = millis();
          webWifiScanState   = WebWifiScanState::READY;
        }
        return;
      }
      webWifiScanResult  = n;
      webWifiScanReadyMs = millis();
      webWifiScanState   = WebWifiScanState::READY;
      return;
    }
    case WebWifiScanState::READY:
      if (millis() - webWifiScanReadyMs >= 60000UL) {
        WiFi.scanDelete();
        webWifiScanState = WebWifiScanState::IDLE;
      }
      return;
    default:
      return;
  }
}

// ---- HTTP helpers -----------------------------------------
static void webUiSendJsonHeader(WiFiClient &c) {
  hueSendHttpHeaders(c, "200 OK", "application/json");
}
static void webUiSendError(WiFiClient &c, const char *status, const char *msg) {
  hueSendHttpHeaders(c, status, "application/json");
  c.print(F("{\"ok\":false,\"error\":"));
  hueWriteJsonString(c, msg ? msg : "error");
  c.print('}');
}
static void webUiWriteOk(WiFiClient &c, const char *extraKv = nullptr) {
  c.print(F("{\"ok\":true"));
  if (extraKv && *extraKv) { c.print(','); c.print(extraKv); }
  c.print('}');
}

// ============================================================
// CONFIG BACKUP / RESTORE
// ============================================================
//  File format: 12-byte header + raw EEPROM dump.
//  Two destinations, two sources, all independent.

constexpr uint32_t CONFIG_BACKUP_MAGIC   = 0x4B425249UL;   // 'IRBK'
constexpr uint16_t CONFIG_BACKUP_VERSION = 1U;
constexpr size_t   CONFIG_BACKUP_HEADER_BYTES = 12U;
constexpr size_t   CONFIG_BACKUP_FILE_BYTES   = CONFIG_BACKUP_HEADER_BYTES + CONFIG_EEPROM_SIZE;

#pragma pack(push, 1)
struct ConfigBackupHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadBytes;
  uint32_t crc32;
};
#pragma pack(pop)
static_assert(sizeof(ConfigBackupHeader) == CONFIG_BACKUP_HEADER_BYTES, "header size");

static const char *configBackupValidate(const uint8_t *file, size_t len) {
  if (file == nullptr || len < CONFIG_BACKUP_FILE_BYTES) return "File too short";
  const ConfigBackupHeader *hdr = reinterpret_cast<const ConfigBackupHeader *>(file);
  if (hdr->magic != CONFIG_BACKUP_MAGIC) return "Not a config backup";
  if (hdr->version != CONFIG_BACKUP_VERSION) return "Unsupported version";
  if (hdr->payloadBytes != CONFIG_EEPROM_SIZE) return "Payload size mismatch";
  const uint8_t *payload = file + CONFIG_BACKUP_HEADER_BYTES;
  if (crc32Bytes(payload, CONFIG_EEPROM_SIZE) != hdr->crc32) return "CRC mismatch";
  return nullptr;
}

static bool configBackupWriteEeprom(const uint8_t *payload) {
  for (size_t i = 0; i < CONFIG_EEPROM_SIZE; ++i) EEPROM.write(i, payload[i]);
  return EEPROM.commit();
}

// GET /api/web/config/backup  → download
static void webUiServeBackupDownload(WiFiClient &c) {
  ConfigBackupHeader hdr{};
  hdr.magic        = CONFIG_BACKUP_MAGIC;
  hdr.version      = CONFIG_BACKUP_VERSION;
  hdr.payloadBytes = static_cast<uint16_t>(CONFIG_EEPROM_SIZE);

  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < CONFIG_EEPROM_SIZE; ++i) {
    crc = crc32UpdateByte(crc, EEPROM.read(i));
    if ((i & 0xFFU) == 0xFFU) ESP.wdtFeed();
  }
  hdr.crc32 = ~crc;

  c.print(F("HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"irhub-config.bin\"\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "Content-Length: "));
  c.print((unsigned)CONFIG_BACKUP_FILE_BYTES);
  c.print(F("\r\n\r\n"));
  c.write(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
  uint8_t chunk[64];
  for (size_t i = 0; i < CONFIG_EEPROM_SIZE; i += sizeof(chunk)) {
    const size_t n = (CONFIG_EEPROM_SIZE - i > sizeof(chunk))
                       ? sizeof(chunk) : (CONFIG_EEPROM_SIZE - i);
    for (size_t j = 0; j < n; ++j) chunk[j] = EEPROM.read(i + j);
    c.write(chunk, n);
  }
}

// POST /api/web/config/backup-to-flash
static void webUiServeBackupToFlash(WiFiClient &c) {
  if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR engine busy"); return; }
  if (storageJobIsRunning())          { webUiSendError(c, "409 Conflict", "Storage job running"); return; }
  if (!storageWriteConfigBackup())    { webUiSendError(c, "500 Internal Server Error", "Flash backup failed"); return; }
  webUiWriteOk(c, "\"dest\":\"flash\"");
}

// POST /api/web/config/restore-from-flash
static void webUiServeRestoreFromFlash(WiFiClient &c) {
  if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR engine busy"); return; }
  uint8_t *buf = storageRecordBuffer;
  if (!storageReadConfigBackup(buf, sizeof(storageRecordBuffer))) {
    webUiSendError(c, "404 Not Found", "No flash backup found"); return;
  }
  if (!configBackupWriteEeprom(buf)) {
    webUiSendError(c, "500 Internal Server Error", "EEPROM write failed"); return;
  }
  webUiWriteOk(c, "\"rebooting\":true");
  c.flush();
  delay(500);
  ESP.restart();
}

// POST /api/web/config/restore-from-file  (raw .bin upload)
static void webUiServeRestoreFromFile(WiFiClient &c, const char *body) {
  if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR engine busy"); return; }
  const uint8_t *file = reinterpret_cast<const uint8_t *>(body);
  const size_t len = hueHttpBodyLength;
  const char *err = configBackupValidate(file, len);
  if (err != nullptr) { webUiSendError(c, "400 Bad Request", err); return; }
  const uint8_t *payload = file + CONFIG_BACKUP_HEADER_BYTES;
  if (!configBackupWriteEeprom(payload)) {
    webUiSendError(c, "500 Internal Server Error", "EEPROM write failed"); return;
  }
  webUiWriteOk(c, "\"rebooting\":true");
  c.flush();
  delay(500);
  ESP.restart();
}

static void webUiServeStatus(WiFiClient &c) {
  // Build the entire response in RAM, then send it in a single write with an
  // explicit Content-Length header. Streaming ~40 small client.print() calls
  // was silently truncating around byte 880 on the ESP8266 TCP stack, which
  // produced the "Expected ',' or '}' after property value" JSON parse errors.
  //
  // NOTE: hub_name, wifi ssid, mqtt broker, mqtt base_topic, NTP server and
  // webLearnMessage are all user-controlled and are not JSON-escaped here
  // (this matches the previous behaviour). Use WebUI-safe values with no
  // embedded quote or backslash characters.

  const char *learnStateStr = "IDLE";
  switch (webLearnState) {
    case WebLearnState::IDLE:     learnStateStr = "IDLE";     break;
    case WebLearnState::PENDING:  learnStateStr = "PENDING";  break;
    case WebLearnState::LEARNING: learnStateStr = "LEARNING"; break;
    case WebLearnState::SUCCESS:  learnStateStr = "SUCCESS";  break;
    case WebLearnState::FAILED:   learnStateStr = "FAILED";   break;
  }

  String s;
  s.reserve(1500);

  s += F("{\"firmware\":\"");          s += FIRMWARE_VERSION;
  s += F("\",\"hub_name\":\"");        s += webUiHubName;
  s += F("\",\"uptime_s\":");          s += millis() / 1000UL;
  s += F(",\"free_heap\":");           s += ESP.getFreeHeap();

  s += F(",\"wifi\":{\"state\":\"");   s += wifiStateToString(wifiState);
  s += F("\",\"enabled\":true");
  s += F(",\"connected\":");           s += wifiIsConnected() ? F("true") : F("false");
  s += F(",\"ssid\":\"");              s += wifiGetSSID();
  s += F("\",\"rssi\":");
  if (wifiIsConnected()) s += wifiGetRSSI(); else s += F("null");
  s += F(",\"ip\":");
  if (wifiIsConnected()) { s += '"'; s += WiFi.localIP().toString(); s += '"'; }
  else s += F("null");
  s += F(",\"static_ip\":{\"enabled\":"); s += staticIpConfig.enabled ? F("true") : F("false");
  s += F(",\"ip\":\"");      s += staticIpConfig.ip.toString();
  s += F("\",\"gateway\":\""); s += staticIpConfig.gateway.toString();
  s += F("\",\"subnet\":\"");  s += staticIpConfig.subnet.toString();
  s += F("\",\"dns\":\"");     s += staticIpConfig.dns.toString();
  s += F("\"}");
  s += F(",\"ap\":{\"active\":");
  s += (wifiRecoveryState != WifiRecoveryState::OFF) ? F("true") : F("false");
  s += F(",\"ip\":\"");     s += WiFi.softAPIP().toString();
  s += F("\",\"ssid\":\"");  s += wifiRecoveryApSsid;
  s += F("\"}");
  s += F("}");

  s += F(",\"mqtt\":{\"state\":\"");   s += mqttStateToString(mqttState);
  s += F("\",\"enabled\":");           s += mqttConfig.enabled ? F("true") : F("false");
  s += F(",\"connected\":");           s += mqttIsConnected() ? F("true") : F("false");
  s += F(",\"broker\":\"");            s += mqttConfig.broker;
  s += F("\",\"port\":");              s += mqttConfig.port;
  s += F(",\"base_topic\":\"");        s += mqttGetBaseTopic();
  s += F("\",\"ha_discovery\":");      s += haDiscoveryEnabled ? F("true") : F("false");
  s += F("}");

  s += F(",\"flash\":{\"detected\":"); s += storage.detected ? F("true") : F("false");
  s += F(",\"healthy\":");             s += storage.initialized ? F("true") : F("false");
  s += F(",\"capacity_bytes\":");      s += storage.flashSizeBytes;
  s += F(",\"generation\":");          s += storage.generation;
  s += F(",\"used_sectors\":");        s += storage.usedSectorCount;
  s += F(",\"free_sectors\":");        s += storage.freeSectorCount;
  s += F("}");

  s += F(",\"temperature\":{\"enabled\":"); s += ds18b20IsEnabled() ? F("true") : F("false");
  s += F(",\"connected\":");           s += ds18b20IsConnected() ? F("true") : F("false");
  s += F(",\"valid\":");               s += ds18b20HasValidReading() ? F("true") : F("false");
  s += F(",\"centi_c\":");
  if (ds18b20HasValidReading()) s += ds18b20GetCalibratedCentiDegrees(); else s += F("null");
  s += F(",\"raw_centi\":");
  if (ds18b20HasValidReading())
    s += (int32_t)((int32_t)ds18b20State.rawTemperature * 625L / 100L);
  else s += F("null");
  s += F(",\"calibration_centi\":");   s += ds18b20GetCalibrationCentiDegrees();
  s += F(",\"interval_ms\":");         s += ds18b20GetSampleIntervalMs();
  s += F(",\"pin\":");                 s += pinDs18b20;
  s += F("}");

  s += F(",\"ir\":{\"state\":\"");     s += webUiIrStateToString(irState);
  s += F("\",\"tx_pin\":");            s += pinIrTx;
  s += F(",\"rx_pin\":");              s += pinIrRx;
  s += F(",\"led_pin\":");             s += pinStatusLed;
  s += F(",\"frequency_hz\":");        s += txFrequencyHz;
  s += F(",\"repeat_count\":");        s += txRepeatCount;
  s += F(",\"repeat_gap_ms\":");       s += txRepeatGapMs;
  s += F("}");

  s += F(",\"time\":{\"enabled\":");   s += ntpEnabled ? F("true") : F("false");
  s += F(",\"valid\":");               s += timeIsValid() ? F("true") : F("false");
  s += F(",\"server\":\"");            s += ntpServer;
  s += F("\",\"gmt_offset_min\":");    s += ntpGmtOffsetMinutes;
  s += F(",\"dst_offset_min\":");      s += ntpDstOffsetMinutes;
  s += F(",\"local\":\"");
  {
    struct tm t{};
    if (timeGetLocal(t)) {
      char buf[24] = {0};
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
      s += buf;
    } else {
      s += F("unavailable");
    }
  }
  s += F("\",\"utc\":\"");
  if (timeIsValid()) {
    const time_t nowT = timeNow();
    struct tm utc{};
    gmtime_r(&nowT, &utc);
    char buf[24] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &utc);
    s += buf;
  } else {
    s += F("unavailable");
  }
  s += F("\"}");

  s += F(",\"serial\":{\"enabled\":"); s += serialEnabled ? F("true") : F("false");
  s += F("}");

  s += F(",\"discovery_mode\":\"");
  s += (discoveryMode == DiscoveryMode::NATIVE_HA) ? F("native") : F("custom");
  s += F("\"");

  s += F(",\"hue\":{\"http_ready\":"); s += hueNetworkStarted ? F("true") : F("false");
  s += F("}");

  s += F(",\"learn\":{\"state\":\"");  s += learnStateStr;
  s += F("\",\"message\":\"");         s += webLearnMessage;
  s += F("\",\"token\":");             s += webLearnReq.token;
  s += F("}");

  s += F("}");

  c.print(F("HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Length: "));
  c.print((unsigned)s.length());
  c.print(F("\r\n\r\n"));
  c.write(reinterpret_cast<const uint8_t *>(s.c_str()), s.length());
}


static void webUiCountEntities(uint32_t &dvOut, uint32_t &cmOut, uint32_t &axOut) {
  dvOut = storageDeviceIndexCount;
  cmOut = storageCommandIndexCount;
  axOut = 0;
  for (uint16_t i = 0; i < storageCommandIndexCount; ++i) {
    const uint16_t id = storageCommandIndex[i].id;
    if (id < STORAGE_MAX_SECTORS &&
        (storageCommandAlexaBitmap[id >> 3] & (uint8_t)(1U << (id & 7U)))) ++axOut;
  }
}

static void webUiServeInfo(WiFiClient &c) {
  uint32_t dv = 0, cm = 0, ax = 0;
  webUiCountEntities(dv, cm, ax);

  // Build the whole body in RAM, then send it in one write with an explicit
  // Content-Length. This is the same pattern the rest of the WebUI uses for
  // short responses, and it avoids the partial-read failures that come from
  // streaming many small client.print() calls over a closing socket.
  char body[768];
  int n = 0;

  n += snprintf(body + n, sizeof(body) - n,
      "{\"firmware\":\"%s\",\"ir_library\":\"%s\",\"free_heap\":%lu,"
      "\"uptime_s\":%lu,\"devices\":%lu,\"commands\":%lu,\"alexa_exposed\":%lu,"
      "\"wifi_state\":\"%s\",",
      FIRMWARE_VERSION,
      _IRREMOTEESP8266_VERSION_STR,
      (unsigned long)ESP.getFreeHeap(),
      (unsigned long)(millis() / 1000UL),
      (unsigned long)dv, (unsigned long)cm, (unsigned long)ax,
      wifiStateToString(wifiState));

  if (n <= 0 || n >= (int)sizeof(body)) {
    webUiSendError(c, "500 Internal Server Error", "Info buffer overflow");
    return;
  }

  if (wifiIsConnected()) {
    IPAddress ip = WiFi.localIP();
    n += snprintf(body + n, sizeof(body) - n,
        "\"wifi_ip\":\"%u.%u.%u.%u\",\"wifi_rssi\":%ld,",
        ip[0], ip[1], ip[2], ip[3], (long)wifiGetRSSI());
  } else {
    n += snprintf(body + n, sizeof(body) - n,
        "\"wifi_ip\":null,\"wifi_rssi\":null,");
  }

  // Sanitize the broker string: a stray quote or backslash would break the
  // entire JSON response. Replace those characters with '?' locally.
  char safeBroker[MQTT_BROKER_MAX_LENGTH + 1U];
  {
    size_t i = 0;
    for (; i < sizeof(safeBroker) - 1U && mqttConfig.broker[i] != '\0'; ++i) {
      char ch = mqttConfig.broker[i];
      if (ch == '"' || ch == '\\' || (uint8_t)ch < 0x20U) ch = '?';
      safeBroker[i] = ch;
    }
    safeBroker[i] = '\0';
  }

  n += snprintf(body + n, sizeof(body) - n,
      "\"mqtt_state\":\"%s\",\"mqtt_broker\":\"%s\",\"mqtt_port\":%u,"
      "\"flash_detected\":%s,\"flash_healthy\":%s,"
      "\"flash_capacity\":%lu,\"flash_generation\":%lu,"
      "\"flash_used\":%lu,\"flash_free\":%lu,"
      "\"temp_valid\":%s,",
      mqttStateToString(mqttState),
      safeBroker,
      (unsigned)mqttConfig.port,
      storage.detected ? "true" : "false",
      storage.initialized ? "true" : "false",
      (unsigned long)storage.flashSizeBytes,
      (unsigned long)storage.generation,
      (unsigned long)storage.usedSectorCount,
      (unsigned long)storage.freeSectorCount,
      ds18b20HasValidReading() ? "true" : "false");

  if (ds18b20HasValidReading()) {
    n += snprintf(body + n, sizeof(body) - n, "\"temp_centi_c\":%ld",
        (long)ds18b20GetCalibratedCentiDegrees());
  } else {
    n += snprintf(body + n, sizeof(body) - n, "\"temp_centi_c\":null");
  }

  n += snprintf(body + n, sizeof(body) - n, "}");

  if (n <= 0 || n >= (int)sizeof(body)) {
    webUiSendError(c, "500 Internal Server Error", "Info buffer overflow");
    return;
  }

  c.print(F("HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Length: "));
  c.print((unsigned)n);
  c.print(F("\r\n\r\n"));
  c.write(reinterpret_cast<const uint8_t *>(body), (size_t)n);
}

static void webUiPrintDeviceExtras(WiFiClient &c, uint8_t cat, bool cloud) {
  c.print(F(",\"category\":\""));
  c.print(deviceCategoryName(cat));
  c.print(F("\",\"cloud\":"));
  c.print(cloud ? F("true") : F("false"));
}

// ---- Device / command JSON readers -------------------------
static void webUiServeDevices(WiFiClient &c) {
  const uint8_t MAX_DEV = 32;
  WebUiDevEntry *entries = webUiScratch.devEntries;
  uint8_t count = 0;

  for (uint16_t i = 0; i < storageDeviceIndexCount && count < MAX_DEV; ++i) {
    const uint32_t s = storageDeviceIndex[i].sector;
    if (storageIsSectorQuarantined(s)) continue;
    StorageRecordHeader h{};
    if (!storageReadValidatedRecord(s, h)) continue;
    if (h.type != RECORD_TYPE_DEVICE) continue;
    if (h.recordId != storageDeviceIndex[i].id) continue;
    char name[STORAGE_MAX_NAME + 1U] = {0};
    if (!storageGetRecordName(s, h, name, sizeof(name))) continue;
    entries[count].id = h.recordId;
    strncpy(entries[count].name, name, STORAGE_MAX_NAME);
    entries[count].name[STORAGE_MAX_NAME] = '\0';
    entries[count].cat = 0; entries[count].cloud = false;
    (void)storageGetDeviceCloud(h.recordId, entries[count].cat, entries[count].cloud);
    ++count;
  }

  uint16_t orderIds[64];
  uint16_t orderCount = 0;
  (void)storageReadLatestOrderRecord(1U, 0U, orderIds, 64U, orderCount);

  bool emitted[MAX_DEV] = {false};
  webUiSendJsonHeader(c);
  c.print('[');
  bool first = true;

  for (uint16_t oi = 0; oi < orderCount; ++oi) {
    for (uint8_t i = 0; i < count; ++i) {
      if (emitted[i]) continue;
      if (entries[i].id != orderIds[oi]) continue;
      if (!first) c.print(',');
      first = false;
      c.print(F("{\"id\":")); c.print(entries[i].id);
      c.print(F(",\"name\":")); hueWriteJsonString(c, entries[i].name); webUiPrintDeviceExtras(c, entries[i].cat, entries[i].cloud);
      c.print('}');
      emitted[i] = true;
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (emitted[i]) continue;
    if (!first) c.print(',');
    first = false;
    c.print(F("{\"id\":")); c.print(entries[i].id);
    c.print(F(",\"name\":")); hueWriteJsonString(c, entries[i].name); webUiPrintDeviceExtras(c, entries[i].cat, entries[i].cloud);
    c.print('}');
  }
  c.print(']');
}

static bool webUiSectorLooksBlank(uint32_t sector) {
  if (!storage.detected || sector >= storage.sectorCount) return false;
  uint8_t b1[4], b2[4], b3[4];
  const uint32_t addr = sector * STORAGE_FLASH_SECTOR_SIZE;
  if (!flashReadBytes(addr, b1, 4)) return false;
  if (!flashReadBytes(addr, b2, 4)) return false;
  if (!flashReadBytes(addr, b3, 4)) return false;
  if (memcmp(b1, b2, 4) || memcmp(b1, b3, 4)) return false;
  return (b1[0] & b1[1] & b1[2] & b1[3]) == 0xFF;
}

static void webUiServeDeviceDetail(WiFiClient &c, uint32_t deviceId) {
  char devName[STORAGE_MAX_NAME + 1U] = {0};
  if (!storage.initialized || deviceId == 0 ||
      storageIsDeviceDeleted(deviceId) ||
      !storageFindDeviceName(deviceId, devName, sizeof(devName))) {
    webUiSendError(c, "404 Not Found", "Unknown device");
    return;
  }

  const uint8_t MAX_CMDS = 32;
  WebUiCmdEntry *entries = webUiScratch.cmdEntries;
  uint8_t count = 0;

  // Walk the RAM command index — no full-flash scan. Each hit is validated
  // against its sector before being accepted.
  for (uint16_t i = 0U; i < storageCommandIndexCount && count < MAX_CMDS; ++i) {
    const CommandIndexEntry &e = storageCommandIndex[i];
    if (e.devId != deviceId) continue;
    if (storageIsCommandDeleted(e.id)) continue;

    StorageRecordHeader h{};
    if (!storageReadValidatedRecord(e.sector, h)) continue;
    if (h.type != RECORD_TYPE_COMMAND || h.recordId != e.id) continue;

    WebUiCmdEntry &ce = entries[count];
    ce.id = e.id;
    ce.ch[0] = '\0';
    if (h.carrierHz == CHANNEL_MARK_HZ && h.repeatGapMs == CHANNEL_MARK_GAP && h.rawLength > 0U) {
      // digits sit right after the name in the record payload
      const char *d = reinterpret_cast<const char *>(storageRecordBuffer + h.nameLength);
      const size_t maxN = static_cast<size_t>(h.rawLength) * 2U;
      size_t n = 0U;
      while (n < CHANNEL_MAX_DIGITS && n < maxN && d[n] >= '0' && d[n] <= '9') { ce.ch[n] = d[n]; ++n; }
      ce.ch[n] = '\0';
    }
    if (!storageGetRecordName(e.sector, h, ce.name, sizeof(ce.name))) continue;
    bool on = false;
    (void)storageGetCommandHueEnabled(e.id, on);
    ce.alexa  = on;
    ce.rawLen = h.rawLength;
    ce.hz     = h.carrierHz;
    ce.rep    = h.repeatCount;
    ce.gap    = h.repeatGapMs;
    ++count;
  }

  // Order list — now cached in RAM, no flash scan.
  uint16_t orderIds[64];
  uint16_t orderCount = 0;
  (void)storageReadLatestOrderRecord(deviceId, deviceId, orderIds, 64U, orderCount);

  bool emitted[MAX_CMDS] = {false};
  webUiSendJsonHeader(c);
  c.print(F("{\"id\":")); c.print(deviceId);
  c.print(F(",\"name\":")); hueWriteJsonString(c, devName);
  {
    uint8_t devCat = 0; bool devCloud = false;
    (void)storageGetDeviceCloud(deviceId, devCat, devCloud);
    webUiPrintDeviceExtras(c, devCat, devCloud);
  }
  c.print(F(",\"commands\":["));
  bool first = true;

  auto emitEntry = [&](const WebUiCmdEntry &ce) {
    if (!first) c.print(',');
    first = false;
    c.print(F("{\"id\":")); c.print(ce.id);
    c.print(F(",\"name\":")); hueWriteJsonString(c, ce.name);
    c.print(F(",\"alexa\":")); c.print(ce.alexa ? F("true") : F("false"));
    c.print(F(",\"raw_len\":")); c.print(ce.rawLen);
    c.print(F(",\"frequency_hz\":")); c.print(ce.hz);
    c.print(F(",\"repeat_count\":")); c.print(ce.rep);
    c.print(F(",\"repeat_gap_ms\":")); c.print(ce.gap);
    if (ce.ch[0]) { c.print(F(",\"ch\":\"")); c.print(ce.ch); c.print('"'); }
    c.print('}');
  };

  for (uint16_t oi = 0; oi < orderCount; ++oi) {
    for (uint8_t i = 0; i < count; ++i) {
      if (emitted[i]) continue;
      if (entries[i].id != orderIds[oi]) continue;
      emitEntry(entries[i]);
      emitted[i] = true;
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (emitted[i]) continue;
    emitEntry(entries[i]);
  }
  c.print(F("]}"));
}

static void webUiServeCommandDetail(WiFiClient &c, uint32_t commandId) {
  uint32_t sector = UINT32_MAX, deviceId = 0, seq = 0;
  if (!storage.initialized || commandId == 0 ||
      !storageFindActiveCommand(commandId, sector, deviceId, seq)) {
    webUiSendError(c, "404 Not Found", "Unknown command"); return;
  }
  StorageRecordHeader h{};
  char cmdName[STORAGE_MAX_NAME + 1U] = {0};
  if (!storageReadValidatedRecord(sector, h) ||
      !storageGetRecordName(sector, h, cmdName, sizeof(cmdName))) {
    webUiSendError(c, "500 Internal Server Error", "Command read failed"); return;
  }
  char devName[STORAGE_MAX_NAME + 1U] = {0};
  if (!storageFindDeviceName(deviceId, devName, sizeof(devName))) devName[0] = '\0';
  bool alexaOn = false;
  (void)storageGetCommandHueEnabled(commandId, alexaOn);
  webUiSendJsonHeader(c);
  c.print(F("{\"id\":")); c.print(commandId);
  c.print(F(",\"device_id\":")); c.print(deviceId);
  c.print(F(",\"device_name\":")); hueWriteJsonString(c, devName);
  c.print(F(",\"name\":")); hueWriteJsonString(c, cmdName);
  c.print(F(",\"alexa\":")); c.print(alexaOn ? F("true") : F("false"));
  c.print(F(",\"raw_len\":")); c.print(h.rawLength);
  c.print(F(",\"frequency_hz\":")); c.print(h.carrierHz);
  c.print(F(",\"repeat_count\":")); c.print(h.repeatCount);
  c.print(F(",\"repeat_gap_ms\":")); c.print(h.repeatGapMs);
  c.print('}');
}

// ---- Learn endpoints --------------------------------------
// mode=new_command  {"mode":"new_command","device_name":"Living Room"}
//                   -> runs DEVICE <name>, then queues "L"
// mode=relearn      {"mode":"relearn","command_id":5}
//                   -> runs "RELEARN 5", then queues "RELEARN CONFIRM 5"
static void webUiServeLearnBegin(WiFiClient &c, const char *body) {  if (webLearnState == WebLearnState::PENDING ||
      webLearnState == WebLearnState::LEARNING ||
      webLearnState == WebLearnState::AWAITING_NAME) {
    webUiSendError(c, "409 Conflict", "Learn already in progress"); return;
  }
  if (irState != IrEngineState::IDLE) {
    webUiSendError(c, "409 Conflict", "IR engine busy"); return;
  }

  char mode[16] = {0};
  if (!webJsonGetString(body, "mode", mode, sizeof(mode)) || !mode[0]) {
    webUiSendError(c, "400 Bad Request", "Missing mode"); return;
  }

  webLearnReq = WebLearnRequest{};
  webLearnReq.token = ++webLearnTokenSeq;
  webLearnMessage[0] = '\0';

  if (!strcmp(mode, "new_command")) {
    char deviceName[STORAGE_MAX_NAME + 1U] = {0};
    if (!webJsonGetString(body, "device_name", deviceName, sizeof(deviceName)) ||
        !deviceName[0]) {
      webUiSendError(c, "400 Bad Request", "Missing device_name"); return;
    }
    if (strlen(deviceName) > STORAGE_MAX_NAME) {
      webUiSendError(c, "400 Bad Request", "Device name too long"); return;
    }
    // Selecting / creating the device is a quick operation. Run it
    // synchronously here so the loop only has to block on "L".
    char sel[96];
    snprintf(sel, sizeof(sel), "DEVICE %s", deviceName);
    webUiRunSerialLine(sel);

    if (storage.currentDeviceId == 0) {
      webUiSendError(c, "500 Internal Server Error",
                     "Device could not be selected"); return;
    }

    // Optional category / Alexa Cloud choice from the Add New Remote page.
    {
      char catName[16] = {0};
      bool cloud = false;
      if (webJsonGetString(body, "category", catName, sizeof(catName)) && catName[0]) {
        (void)webJsonGetBool(body, "cloud", cloud);
        const uint8_t cat = deviceCategoryFromName(catName);
        if (cat != 0) (void)storageSetDeviceCloud(storage.currentDeviceId, cat, cloud);
      }
    }
    webLearnReq.kind = WebLearnKind::LEARN_RAW;
    strncpy(webLearnReq.line, "L", sizeof(webLearnReq.line) - 1U);
  } else if (!strcmp(mode, "relearn")) {
    uint32_t id = 0;
    if (!webJsonGetUint(body, "command_id", id) || id == 0) {
      webUiSendError(c, "400 Bad Request", "Missing command_id"); return;
    }
    // The serial RELEARN command is a two-step flow: "RELEARN <id>"
    // arms the confirmation flag, "RELEARN CONFIRM <id>" actually
    // deletes and relearns. The arming step only reads the record,
    // so we can do it synchronously here.
    char arm[48];
    snprintf(arm, sizeof(arm), "RELEARN %lu", (unsigned long)id);
    webUiRunSerialLine(arm);

    webLearnReq.kind = WebLearnKind::RELEARN_COMMAND;
    webLearnReq.targetCommandId = id;
    snprintf(webLearnReq.line, sizeof(webLearnReq.line),
             "RELEARN CONFIRM %lu", (unsigned long)id);
  } else {
    webUiSendError(c, "400 Bad Request", "Unknown mode"); return;
  }

  webLearnState = WebLearnState::PENDING;
  webUiWriteOk(c);
}

// {"name":"turbo"} -> runs "SAVE turbo"
static void webUiServeLearnSave(WiFiClient &c, const char *body) {
  if (webLearnState != WebLearnState::AWAITING_NAME) {
    webUiSendError(c, "409 Conflict", "No capture awaiting a name"); return;
  }
  char name[STORAGE_MAX_NAME + 1U] = {0};
  if (!webJsonGetString(body, "name", name, sizeof(name)) || !name[0]) {
    webUiSendError(c, "400 Bad Request", "Missing name"); return;
  }
  if (strlen(name) > STORAGE_MAX_NAME) {
    webUiSendError(c, "400 Bad Request", "Name too long"); return;
  }
  char line[96];
  snprintf(line, sizeof(line), "SAVE %s", name);
  webUiRunSerialLine(line);

  if (storage.currentCommandId != 0) {
    webLearnState = WebLearnState::SUCCESS;
    strncpy(webLearnMessage, "Command saved.", sizeof(webLearnMessage) - 1U);
    webUiWriteOk(c);
  } else {
    webLearnState = WebLearnState::FAILED;
    strncpy(webLearnMessage, "Save failed.", sizeof(webLearnMessage) - 1U);
    webUiSendError(c, "409 Conflict",
                   "Save failed (name may already exist)");
  }
}

constexpr uint8_t CHANNEL_MAX_PER_DEVICE = 10U;

static uint8_t webUiCountChannels(uint32_t deviceId) {
  uint8_t n = 0U;
  for (uint16_t i = 0U; i < storageCommandIndexCount; ++i) {
    feedWatchdog();
    const CommandIndexEntry &e = storageCommandIndex[i];
    if (e.devId != deviceId || storageIsCommandDeleted(e.id)) continue;
    StorageRecordHeader h{};
    if (!storageReadValidatedRecord(e.sector, h)) continue;
    if (h.type != RECORD_TYPE_COMMAND || h.recordId != e.id) continue;
    if (h.carrierHz == CHANNEL_MARK_HZ && h.repeatGapMs == CHANNEL_MARK_GAP) ++n;
  }
  return n;
}

// POST /api/web/channel {"device_name":"TV","name":"Asianet","number":"22","alexa":true}
// A channel is stored as a normal command; the digits replace the IR waveform.
static void webUiServeChannelSave(WiFiClient &c, const char *body) {
  if (webLearnState == WebLearnState::PENDING || webLearnState == WebLearnState::LEARNING ||
      webLearnState == WebLearnState::AWAITING_NAME || irState != IrEngineState::IDLE) {
    webUiSendError(c, "409 Conflict", "Busy"); return;
  }
  char deviceName[STORAGE_MAX_NAME + 1U] = {0};
  char name[STORAGE_MAX_NAME + 1U] = {0};
  char number[16] = {0};
  bool alexa = false;
  if (!webJsonGetString(body, "device_name", deviceName, sizeof(deviceName)) || !deviceName[0] ||
      !webJsonGetString(body, "name", name, sizeof(name)) || !name[0] ||
      !webJsonGetString(body, "number", number, sizeof(number)) || !number[0]) {
    webUiSendError(c, "400 Bad Request", "Missing device_name, name or number"); return;
  }
  (void)webJsonGetBool(body, "alexa", alexa);
  const size_t digits = strlen(number);
  bool numeric = digits <= CHANNEL_MAX_DIGITS;
  for (size_t i = 0U; i < digits && numeric; ++i) numeric = (number[i] >= '0' && number[i] <= '9');
  if (!numeric) { webUiSendError(c, "400 Bad Request", "Number must be 1-6 digits"); return; }

  char sel[96];
  snprintf(sel, sizeof(sel), "DEVICE %s", deviceName);
  webUiRunSerialLine(sel);
  if (storage.currentDeviceId == 0) {
    webUiSendError(c, "500 Internal Server Error", "Device could not be selected"); return;
  }

  if (webUiCountChannels(storage.currentDeviceId) >= CHANNEL_MAX_PER_DEVICE) {
    webUiSendError(c, "409 Conflict", "Channel limit reached (10 per device)"); return;
  }

  const uint16_t words = static_cast<uint16_t>((digits + 2U) / 2U);   // digits + terminating zero
  memset(lastRaw, 0, static_cast<size_t>(words) * sizeof(uint16_t));
  memcpy(lastRaw, number, digits);
  lastRawLen = words;
  lastRawCrc32 = crc32Buffer(lastRaw, lastRawLen);
  lastLearnCarrierHz = CHANNEL_MARK_HZ;
  lastLearnRepeatCount = 1U;
  lastLearnRepeatGapMs = CHANNEL_MARK_GAP;
  lastLearnMetadataValid = true;

  const bool saved = storageSaveCurrentCommand(name);
  const uint32_t newId = storage.currentCommandId;
  if (saved && alexa && newId != 0U) (void)storageSetCommandHueEnabled(newId, true);
  clearLastCapture();
  if (!saved) { webUiSendError(c, "409 Conflict", "Save failed (name may already exist)"); return; }
  webUiWriteOk(c);
}

static void webUiServeLearnCancel(WiFiClient &c) {
  if (webLearnState == WebLearnState::PENDING) {
    webLearnState = WebLearnState::FAILED;
    strncpy(webLearnMessage, "Cancelled.", sizeof(webLearnMessage) - 1U);
  } else if (webLearnState == WebLearnState::AWAITING_NAME) {
    // Discard the captured signal that is waiting for a name.
    clearLastCapture();
    webLearnState = WebLearnState::FAILED;
    strncpy(webLearnMessage, "Cancelled.", sizeof(webLearnMessage) - 1U);
  }
  // LEARNING cannot be cancelled — the 15-second window is blocking.
  webUiWriteOk(c);
}

static void webUiServeLearnStatus(WiFiClient &c) {
  webUiSendJsonHeader(c);
  c.print(F("{\"state\":"));
  switch (webLearnState) {
    case WebLearnState::IDLE:          hueWriteJsonString(c, "IDLE");          break;
    case WebLearnState::PENDING:       hueWriteJsonString(c, "PENDING");       break;
    case WebLearnState::LEARNING:      hueWriteJsonString(c, "LEARNING");      break;
    case WebLearnState::AWAITING_NAME: hueWriteJsonString(c, "AWAITING_NAME"); break;
    case WebLearnState::SUCCESS:       hueWriteJsonString(c, "SUCCESS");       break;
    case WebLearnState::FAILED:        hueWriteJsonString(c, "FAILED");        break;
  }
  c.print(F(",\"message\":")); hueWriteJsonString(c, webLearnMessage);
  c.print(F(",\"token\":"));   c.print(webLearnReq.token);
  c.print(F(",\"has_capture\":")); c.print(hasLearnedSignal() ? F("true") : F("false"));
  c.print(F(",\"current_device_id\":")); c.print(storage.currentDeviceId);
  c.print(F(",\"current_command_id\":")); c.print(storage.currentCommandId);
  c.print('}');
}

// ---- WiFi scan / config -----------------------------------
static void webUiServeWifiScanStart(WiFiClient &c) {
  if (webWifiScanState == WebWifiScanState::SCANNING || webWifiScanRequested) {
    webUiSendError(c, "409 Conflict", "Scan already running"); return;
  }
  if (irState != IrEngineState::IDLE) {
    webUiSendError(c, "409 Conflict", "IR busy"); return;
  }
  webWifiScanRequested = true;
  webUiWriteOk(c);
}

static void webUiServeWifiScanResults(WiFiClient &c) {
  if (webWifiScanRequested) {   // new scan queued; don't hand back stale results
    webUiSendJsonHeader(c);
    c.print(F("{\"state\":\"RUNNING\"}"));
    return;
  }
  switch (webWifiScanState) {
    case WebWifiScanState::IDLE:
      webUiSendJsonHeader(c);
      c.print(F("{\"state\":\"IDLE\",\"networks\":[]}"));
      return;
    case WebWifiScanState::SCANNING:
      webUiSendJsonHeader(c);
      c.print(F("{\"state\":\"RUNNING\"}"));
      return;
    case WebWifiScanState::READY: {
      webUiSendJsonHeader(c);
      c.print(F("{\"state\":\"DONE\",\"networks\":["));
      const int8_t n = webWifiScanResult;
      if (n > 0) {
        for (int i = 0; i < n && i < 20; ++i) {
          if (i) c.print(',');
          String ss = WiFi.SSID(i);
          c.print(F("{\"ssid\":")); hueWriteJsonString(c, ss.c_str());
          c.print(F(",\"rssi\":")); c.print(WiFi.RSSI(i));
          c.print(F(",\"encrypted\":"));
          c.print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? F("false") : F("true"));
          c.print('}');
          yield();
        }
      }
      c.print(F("]}"));
      return;
    }
    default:
      webUiSendJsonHeader(c);
      c.print(F("{\"state\":\"IDLE\",\"networks\":[]}"));
      return;
  }
}

static void webUiServeWifiConfig(WiFiClient &c, const char *body) {
  // The WebUI must never be able to disable WiFi. Doing so locks the
  // user out of both the port-80 UI and the port-8080 OTA server, and
  // the recovery AP never starts while wifiConfig.enabled is false.
  // The serial "WIFI OFF" command remains available for physical
  // access. Any "enabled":false from the body is silently ignored.
  char ssid[WIFI_SSID_MAX_LENGTH + 1U] = {0};
  char pass[WIFI_PASSWORD_MAX_LENGTH + 1U] = {0};
  char host[WIFI_HOSTNAME_MAX_LENGTH + 1U] = {0};
  const bool hasSsid = webJsonGetString(body, "ssid", ssid, sizeof(ssid));
  const bool hasPass = webJsonGetString(body, "password", pass, sizeof(pass));
  const bool hasHost = webJsonGetString(body, "hostname", host, sizeof(host));

  // ── v1.0: static IP fields ──────────────────────────────────
  bool staticEnabled = staticIpConfig.enabled;
  (void)webJsonGetBool(body, "static_enabled", staticEnabled);

  char ipStr[16]   = {0};
  char gwStr[16]   = {0};
  char snStr[16]   = {0};
  char dnsStr[16]  = {0};
  const bool hasIp  = webJsonGetString(body, "static_ip",      ipStr,  sizeof(ipStr));
  const bool hasGw  = webJsonGetString(body, "static_gateway", gwStr,  sizeof(gwStr));
  const bool hasSn  = webJsonGetString(body, "static_subnet",  snStr,  sizeof(snStr));
  const bool hasDns = webJsonGetString(body, "static_dns",     dnsStr, sizeof(dnsStr));

  IPAddress parsedIp, parsedGw, parsedSn, parsedDns;
  if (staticEnabled) {
    if (!(hasIp && parsedIp.fromString(ipStr))) {
      webUiSendError(c, "400 Bad Request", "Invalid static IP"); return;
    }
    if (!(hasGw && parsedGw.fromString(gwStr))) {
      webUiSendError(c, "400 Bad Request", "Invalid gateway"); return;
    }
    if (!(hasSn && parsedSn.fromString(snStr))) {
      webUiSendError(c, "400 Bad Request", "Invalid subnet"); return;
    }
    if (hasDns && dnsStr[0] && !parsedDns.fromString(dnsStr)) {
      webUiSendError(c, "400 Bad Request", "Invalid DNS"); return;
    }
  }

  staticIpConfig.enabled = staticEnabled;
  if (staticEnabled) {
    staticIpConfig.ip      = parsedIp;
    staticIpConfig.gateway = parsedGw;
    staticIpConfig.subnet  = parsedSn;
    staticIpConfig.dns     = (hasDns && dnsStr[0]) ? parsedDns : IPAddress(0,0,0,0);
  } else {
    staticIpConfig.ip      = IPAddress(0,0,0,0);
    staticIpConfig.gateway = IPAddress(0,0,0,0);
    staticIpConfig.subnet  = IPAddress(0,0,0,0);
    staticIpConfig.dns     = IPAddress(0,0,0,0);
  }
  configurationSaveStaticIp();
  // ────────────────────────────────────────────────────────────

  if (hasSsid && ssid[0] && !wifiSetSSID(ssid)) { webUiSendError(c, "400 Bad Request", "Invalid SSID"); return; }
  if (hasPass && pass[0] && !wifiSetPassword(pass)) { webUiSendError(c, "400 Bad Request", "Invalid password"); return; }
  if (hasHost && host[0] && !wifiSetHostname(host)) { webUiSendError(c, "400 Bad Request", "Invalid hostname"); return; }

  const bool saved = configurationSaveWifi();
  if (!saved) Serial.println(F("WARNING: WiFi settings staged in RAM but could not be persisted."));

  webUiWriteOk(c, saved ? nullptr : "\"warning\":\"saved to RAM only\"");
  c.flush();

  webUiWifiApplyPending = true;
  webUiWifiApplyAtMs    = millis() + 1000UL;
}
// ---- MQTT config ------------------------------------------
static void webUiServeMqttConfig(WiFiClient &c, const char *body) {
  bool enabled = true;
  (void)webJsonGetBool(body, "enabled", enabled);
  char broker[MQTT_BROKER_MAX_LENGTH + 1U] = {0};
  char user[MQTT_USERNAME_MAX_LENGTH + 1U] = {0};
  char pass[MQTT_PASSWORD_MAX_LENGTH + 1U] = {0};
  char cid[MQTT_CLIENT_ID_MAX_LENGTH + 1U] = {0};
  char base[MQTT_BASE_TOPIC_MAX_LENGTH + 1U] = {0};
  uint32_t port = 0;
  bool hasBroker = webJsonGetString(body, "broker", broker, sizeof(broker));
  bool hasUser   = webJsonGetString(body, "username", user, sizeof(user));
  bool hasPass   = webJsonGetString(body, "password", pass, sizeof(pass));
  bool hasCid    = webJsonGetString(body, "client_id", cid, sizeof(cid));
  bool hasBase   = webJsonGetString(body, "base_topic", base, sizeof(base));
  (void)webJsonGetUint(body, "port", port);
  bool ha = haDiscoveryEnabled;
  (void)webJsonGetBool(body, "ha_discovery", ha);

  (void)mqttSetEnabled(enabled);
  if (hasBroker && broker[0] && !mqttSetBroker(broker)) {
    webUiSendError(c, "400 Bad Request", "Invalid broker"); return;
  }
  if (port && !mqttSetPort((uint16_t)port)) {
    webUiSendError(c, "400 Bad Request", "Invalid port"); return;
  }
  if (hasUser && !mqttSetUsername(user)) {
    webUiSendError(c, "400 Bad Request", "Invalid username"); return;
  }
  if (hasPass && pass[0] && !mqttSetPassword(pass)) {
    webUiSendError(c, "400 Bad Request", "Invalid password"); return;
  }
  if (hasCid && cid[0] && !mqttSetClientId(cid)) {
    webUiSendError(c, "400 Bad Request", "Invalid client id"); return;
  }
  if (hasBase && base[0] && !mqttSetBaseTopic(base)) {
    webUiSendError(c, "400 Bad Request", "Invalid base topic"); return;
  }
  (void)mqttHaDiscoverySetEnabled(ha);
  if (!mqttApplyConfiguration()) {
    webUiWriteOk(c, "\"warning\":\"saved but not connected\"");
    return;
  }
  webUiWriteOk(c);
}

// ---- Hub config -------------------------------------------
static void webUiServeHubConfig(WiFiClient &c, const char *body) {
  char name[WEBUI_HUB_NAME_MAX + 1U] = {0};
  if (webJsonGetString(body, "hub_name", name, sizeof(name)) && name[0]) {
    const bool nameChanged = (strcmp(name, webUiHubName) != 0);
    strncpy(webUiHubName, name, WEBUI_HUB_NAME_MAX);
    webUiHubName[WEBUI_HUB_NAME_MAX] = '\0';
    (void)webUiPersistHubName();
    if (nameChanged) {
      // Home Assistant caches the parent device name from the first
      // discovery payload it sees. Republish everything so the new
      // hub name propagates.
      mqttRequestDiscoveryRefresh();
    }
  }

  // Discovery-mode switch. mqttSetDiscoveryMode() handles the whole
  // migration: it clears the previous namespace's retained topics
  // (homeassistant/... or <base>/_system/ha/...), clears the Alexa
  // catalogue if we are leaving CUSTOM, flips the persisted mode in
  // EEPROM, rebinds the birth subscription, and kicks off a fresh
  // discovery republish in the new namespace.
  {
    char mode[16] = {0};
    if (webJsonGetString(body, "discovery_mode", mode, sizeof(mode)) && mode[0]) {
      if (!strcmp(mode, "native")) {
        mqttSetDiscoveryMode(DiscoveryMode::NATIVE_HA);
      } else if (!strcmp(mode, "custom")) {
        mqttSetDiscoveryMode(DiscoveryMode::CUSTOM);
      }
    }
  }

  uint32_t v = 0;
  if (webJsonGetUint(body, "frequency_hz", v)) {
    if (v < MIN_TX_FREQUENCY_HZ || v > MAX_TX_FREQUENCY_HZ) {
      webUiSendError(c, "400 Bad Request", "Frequency out of range"); return;
    }
    if (irState != IrEngineState::IDLE) {
      webUiSendError(c, "409 Conflict", "IR engine busy"); return;
    }
    txFrequencyHz = (uint16_t)v;
  }
  if (webJsonGetUint(body, "repeat_count", v)) {
    if (v < 1 || v > 255) { webUiSendError(c, "400 Bad Request", "Repeat out of range"); return; }
    if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR busy"); return; }
    txRepeatCount = (uint8_t)v;
  }
  if (webJsonGetUint(body, "repeat_gap_ms", v)) {
    if (v > UINT16_MAX) { webUiSendError(c, "400 Bad Request", "Gap out of range"); return; }
    if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR busy"); return; }
    txRepeatGapMs = (uint16_t)v;
  }

  bool pinsChanged = false;
  uint8_t oldTx = pinIrTx, oldRx = pinIrRx, oldLed = pinStatusLed, oldTemp = pinDs18b20;
  uint32_t p = 0;
  if (webJsonGetUint(body, "ir_tx_pin", p)) { if (p > 255) { webUiSendError(c, "400 Bad Request", "Bad pin"); return; } pinIrTx = (uint8_t)p; pinsChanged = true; }
  if (webJsonGetUint(body, "ir_rx_pin", p)) { if (p > 255) { webUiSendError(c, "400 Bad Request", "Bad pin"); return; } pinIrRx = (uint8_t)p; pinsChanged = true; }
  if (webJsonGetUint(body, "led_pin",   p)) { if (p > 255) { webUiSendError(c, "400 Bad Request", "Bad pin"); return; } pinStatusLed = (uint8_t)p; pinsChanged = true; }
  if (webJsonGetUint(body, "temp_pin",  p)) { if (p > 255) { webUiSendError(c, "400 Bad Request", "Bad pin"); return; } pinDs18b20 = (uint8_t)p; pinsChanged = true; }
  if (pinsChanged) {
    if (!pinSetIsConsistent() || !configurationSavePins()) {
      pinIrTx = oldTx; pinIrRx = oldRx; pinStatusLed = oldLed; pinDs18b20 = oldTemp;
      webUiSendError(c, "400 Bad Request", "Conflicting or unpreservable pin set"); return;
    }
  }

  bool b = false;
  if (webJsonGetBool(body, "temp_enabled", b)) {
    if (irState != IrEngineState::IDLE) { webUiSendError(c, "409 Conflict", "IR busy"); return; }
    ds18b20SetEnabled(b);
  }
  int32_t iv = 0;
  if (webJsonGetInt(body, "temp_cal_centi", iv)) {
    if (iv < -32768) iv = -32768; else if (iv > 32767) iv = 32767;
    ds18b20SetCalibrationCentiDegrees((int16_t)iv);
    (void)configurationSaveTempCal();
  }
  if (webJsonGetUint(body, "temp_interval_ms", v)) {
    ds18b20SetSampleIntervalMs(v);
  }

  if (webJsonGetBool(body, "serial_enabled", b)) {
    serialEnabled = b;
    (void)configurationSaveSystem();
  }
  if (webJsonGetBool(body, "ntp_enabled", b)) {
    timeSetEnabled(b);
    (void)configurationSaveSystem();
  }
  char srv[64] = {0};
  if (webJsonGetString(body, "ntp_server", srv, sizeof(srv)) && srv[0]) {
    if (!timeSetServer(srv)) { webUiSendError(c, "400 Bad Request", "Bad NTP server"); return; }
    (void)configurationSaveSystem();
  }
  if (webJsonGetInt(body, "ntp_gmt_offset_min", iv)) {
    (void)timeSetGmtOffset((int16_t)iv);
    (void)configurationSaveSystem();
  }
  if (webJsonGetInt(body, "ntp_dst_offset_min", iv)) {
    (void)timeSetDstOffset((int16_t)iv);
    (void)configurationSaveSystem();
  }
  webUiWriteOk(c, pinsChanged ? "\"reboot_required\":true" : nullptr);
}

// ---- Auto-calibrate temperature from a reference reading ----------
static void webUiServeTempCalibrate(WiFiClient &c, const char *body) {
  if (irState != IrEngineState::IDLE) {
    webUiSendError(c, "409 Conflict", "IR engine busy"); return;
  }
  if (!ds18b20IsEnabled()) {
    webUiSendError(c, "409 Conflict", "Temperature sensor is disabled"); return;
  }
  if (!ds18b20HasValidReading()) {
    webUiSendError(c, "409 Conflict", "No valid temperature reading yet"); return;
  }

  int32_t referenceCenti = 0;
  if (!webJsonGetInt(body, "reference_centi", referenceCenti)) {
    webUiSendError(c, "400 Bad Request", "Missing reference_centi"); return;
  }
  // Sanity: −100 °C … +150 °C
  if (referenceCenti < -10000 || referenceCenti > 15000) {
    webUiSendError(c, "400 Bad Request", "Reference out of range"); return;
  }
  const int32_t currentCenti  = ds18b20GetCalibratedCentiDegrees();
  const int32_t currentCal    = ds18b20GetCalibrationCentiDegrees();
  int32_t newCal = currentCal + (referenceCenti - currentCenti);
  if (newCal < -32768) newCal = -32768;
  if (newCal >  32767) newCal =  32767;

  ds18b20SetCalibrationCentiDegrees(static_cast<int16_t>(newCal));
  const bool persisted = configurationSaveTempCal();

  char extra[128];
  snprintf(extra, sizeof(extra),
           "\"new_calibration_centi\":%ld,\"previous_reading_centi\":%ld,\"new_reading_centi\":%ld%s",
           (long)newCal,
           (long)currentCenti,
           (long)(referenceCenti),
           persisted ? "" : ",\"warning\":\"not persisted\"");
  webUiWriteOk(c, extra);
}

// ---- Automation ------------------------------------------
// Write side is fully command-line driven: the WebUI sends the
// literal RULE / SCHEDULE lines through /api/web/cmd. Read side
// stays JSON because it feeds a table.
static void webUiServeRulesList(WiFiClient &c) {
  webUiSendJsonHeader(c);
  char *rb = webUiScratch.rulesBuf;
  constexpr size_t RB_CAP = sizeof(webUiScratch.rulesBuf);
  size_t rn = 0;
  auto flushBuf = [&]() { if (rn) { c.write(reinterpret_cast<const uint8_t *>(rb), rn); rn = 0; feedWatchdog(); } };
  auto put = [&](const char *s) {
    const size_t l = strlen(s);
    if (rn + l >= RB_CAP) flushBuf();
    if (l >= RB_CAP) { c.write(reinterpret_cast<const uint8_t *>(s), l); return; }
    memcpy(rb + rn, s, l); rn += l;
  };
  char tmp[288];

  put("{\"rules\":[");
  bool firstRule = true;
  for (uint8_t i = 0; i < AUTOMATION_MAX_RULES; ++i) {
    const RuleSlot &s = automationRules[i];
    if (s.data.triggerCount == 0U) continue;
    snprintf(tmp, sizeof(tmp), "%s{\"id\":%u,\"enabled\":%s,\"cooldown_s\":%u,\"triggers\":[",
             firstRule ? "" : ",", (unsigned)(i + 1),
             (s.data.ruleFlags & RULE_FLAG_ENABLED) ? "true" : "false",
             (unsigned)s.data.cooldownSec);
    put(tmp);
    firstRule = false;
    for (uint8_t ti = 0; ti < s.data.triggerCount; ++ti) {
      const RuleTriggerStored &g = s.data.triggers[ti];
      snprintf(tmp, sizeof(tmp),
          "%s{\"use_temp\":%s,\"use_time\":%s,\"use_interval\":%s,\"temp_op\":%u,\"temp_centi\":%d,"
          "\"temp_dur_s\":%u,\"time_start_min\":%u,\"time_end_min\":%u,\"weekday_mask\":%u,"
          "\"logic\":%u,\"interval_s\":%u,\"steps\":[",
          ti ? "," : "",
          (g.flags & TRIG_FLAG_USE_TEMP) ? "true" : "false",
          (g.flags & TRIG_FLAG_USE_TIME) ? "true" : "false",
          (g.flags & TRIG_FLAG_USE_INTERVAL) ? "true" : "false",
          (unsigned)g.tempOp, (int)g.tempValueCenti, (unsigned)g.tempDurationSec,
          (unsigned)g.timeStartMin, (unsigned)g.timeEndMin, (unsigned)g.weekdayMask,
          (unsigned)g.logic, (unsigned)g.intervalSec);
      put(tmp);
      for (uint8_t k = 0; k < g.stepCount; ++k) {
        snprintf(tmp, sizeof(tmp), "%s{\"t\":\"%s\",\"v\":%u}", k ? "," : "",
                 g.steps[k].type == STEP_TX ? "tx" : "wait", (unsigned)g.steps[k].value);
        put(tmp);
      }
      put("]}");
    }
    put("]}");
  }
  put("]}");
  flushBuf();
  c.flush();
}

static void webUiServeSchedulesList(WiFiClient &c) {
  webUiSendJsonHeader(c);
  c.print(F("{\"schedules\":["));
  bool first = true;
  for (uint8_t i = 0; i < AUTOMATION_MAX_SCHEDULES; ++i) {
    const AutomationScheduleState &s = automationSchedules[i];
    if (!s.used) continue;
    if (!first) c.print(',');
    first = false;
    c.print(F("{\"id\":")); c.print(s.id);
    c.print(F(",\"enabled\":")); c.print((s.data.flags & SCHEDULE_FLAG_ENABLED) ? F("true") : F("false"));
    c.print(F(",\"command_id\":")); c.print(s.data.commandId);
    c.print(F(",\"hour\":")); c.print(s.data.hour);
    c.print(F(",\"minute\":")); c.print(s.data.minute);
    c.print(F(",\"weekday_mask\":")); c.print(s.data.weekdayMask);
    c.print(F(",\"delay_s\":")); c.print(s.data.delaySec);
    c.print('}');
  }
  c.print(F("]}"));
}

// ---- Root HTML (streamed from PROGMEM) --------------------
static void webUiServeRootHtml(WiFiClient &c) {
  c.print(F("HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: max-age=86400\r\n"
            "Connection: close\r\n\r\n"));

  const size_t len = strlen_P(WEBUI_HTML);
  const size_t CHUNK = 256;
  char buf[CHUNK + 1];

  for (size_t i = 0; i < len; ) {
    const size_t n = (len - i > CHUNK) ? CHUNK : (len - i);
    memcpy_P(buf, WEBUI_HTML + i, n);

    size_t written = 0;
    while (written < n) {
      if (!c.connected()) return;                    // client gone, bail
      const size_t w = c.write(
          reinterpret_cast<const uint8_t*>(buf) + written, n - written);
      if (w == 0) {
        delay(1);
        ESP.wdtFeed();
        continue;
      }
      written += w;
    }
    i += n;
    yield();
  }
  c.flush();
}

static void webUiServeStorageJob(WiFiClient &c) {
  const char *stateStr;
  switch (storageJob.state) {
    case StorageJobState::IDLE:    stateStr = "IDLE";    break;
    case StorageJobState::RUNNING: stateStr = "RUNNING"; break;
    case StorageJobState::DONE:    stateStr = "DONE";    break;
    case StorageJobState::FAILED:  stateStr = "FAILED";  break;
    default:                       stateStr = "IDLE";    break;
  }
  const char *kindStr;
  switch (storageJob.kind) {
    case StorageJobKind::RESET: kindStr = "RESET"; break;
    case StorageJobKind::WIPE:  kindStr = "WIPE";  break;
    default:                    kindStr = "NONE";  break;
  }
  webUiSendJsonHeader(c);
  c.print(F("{\"state\":\"")); c.print(stateStr);
  c.print(F("\",\"kind\":\"")); c.print(kindStr);
  c.print(F("\",\"percent\":")); c.print(storageJobGetPercent());
  c.print(F(",\"processed\":")); c.print(storageJob.processed);
  c.print(F(",\"total\":"));     c.print(storageJob.total);
  c.print(F(",\"failed\":"));    c.print(storageJob.failed);
  c.print(F(",\"message\":"));   hueWriteJsonString(c, storageJob.message);
  c.print('}');
}

// ---- WebUI router -----------------------------------------
bool webUiDispatch(WiFiClient &client, const char *method, const char *path,
                   const char *body) {
  if ((!strcmp(path, "/") || !strcmp(path, "/index.html")) && !strcmp(method, "GET")) {
    webUiServeRootHtml(client); return true;
  }
  if (strncmp(path, "/api/web/", 9U) != 0) return false;
  const char *p = path + 9U;

  if (!strcmp(method, "GET")) {
    if (!strcmp(p, "status"))           { webUiServeStatus(client);         return true; }
    if (!strcmp(p, "info"))             { webUiServeInfo(client);           return true; }
    if (!strcmp(p, "devices"))          { webUiServeDevices(client);        return true; }
    if (!strcmp(p, "rules"))            { webUiServeRulesList(client);      return true; }
    if (!strcmp(p, "schedules"))        { webUiServeSchedulesList(client);  return true; }
    if (!strcmp(p, "learn/status"))     { webUiServeLearnStatus(client);    return true; }
    if (!strcmp(p, "config/wifi/scan")) { webUiServeWifiScanResults(client);return true; }
    if (!strcmp(p, "storage/job"))      { webUiServeStorageJob(client);     return true; }
    if (!strcmp(p, "config/backup"))    { webUiServeBackupDownload(client); return true; }    
    if (!strncmp(p, "devices/", 8U)) {
      uint32_t id = 0;
      if (!parseUint32Text(p + 8U, id) || id == 0) {
        webUiSendError(client, "400 Bad Request", "Bad device id"); return true;
      }
      webUiServeDeviceDetail(client, id); return true;
    }
    if (!strncmp(p, "commands/", 9U)) {
      uint32_t id = 0;
      if (!parseUint32Text(p + 9U, id) || id == 0) {
        webUiSendError(client, "400 Bad Request", "Bad command id"); return true;
      }
      webUiServeCommandDetail(client, id); return true;
    }
    webUiSendError(client, "404 Not Found", "Unknown WebUI GET"); return true;
  }

  if (!strcmp(method, "POST")) {
    // Generic command-line passthrough. Front-end can call this with
    // the literal line a serial user would type.
    if (!strcmp(p, "cmd")) {
      char *line = webUiScratch.cmdLine;
      line[0] = '\0';
      if (!webJsonGetString(body, "line", line, sizeof(webUiScratch.cmdLine)) || !line[0]) {
        webUiSendError(client, "400 Bad Request", "Missing line"); return true;
      }
      if (strlen(line) > 600) {
        webUiSendError(client, "400 Bad Request", "Line too long"); return true;
      }
      processSerialCommand(line);
      if (gLastCommandOk) {
        webUiWriteOk(client);
      } else {
        webUiSendError(client, "409 Conflict",
                       gLastCommandError[0] ? gLastCommandError : "command failed");
      }
      return true;
    }

    // Blocking learn flow (state machine in loop()).
    if (!strcmp(p, "learn/begin"))  { webUiServeLearnBegin(client, body);  return true; }
    if (!strcmp(p, "learn/save"))   { webUiServeLearnSave(client, body);   return true; }
    if (!strcmp(p, "channel")) { webUiServeChannelSave(client, body); return true; }
    if (!strcmp(p, "learn/cancel")) { webUiServeLearnCancel(client);       return true; }
    if (!strcmp(p, "ota/prepare")) {
      if (irState != IrEngineState::IDLE) {
        webUiSendError(client, "409 Conflict", "IR engine busy");
        return true;
      }
      webUiWriteOk(client);
      client.flush();
      delay(50);                    // let the response reach the browser
      otaEnterQuietMode();
      return true;
    }
    // Structured forms (binary / encoded fields, multi-field validation).
    if (!strcmp(p, "config/wifi"))      { webUiServeWifiConfig(client, body); return true; }
    if (!strcmp(p, "config/wifi/scan")) { webUiServeWifiScanStart(client);    return true; }
    if (!strcmp(p, "config/mqtt"))      { webUiServeMqttConfig(client, body); return true; }
    if (!strcmp(p, "config/hub"))       { webUiServeHubConfig(client, body);  return true; }
    if (!strcmp(p, "config/temp_calibrate")) { webUiServeTempCalibrate(client, body); return true; }
    if (!strcmp(p, "config/backup-to-flash"))    { webUiServeBackupToFlash(client);        return true; }
    if (!strcmp(p, "config/restore-from-flash")) { webUiServeRestoreFromFlash(client);     return true; }
    if (!strcmp(p, "config/restore-from-file"))  { webUiServeRestoreFromFile(client, body); return true; }    

    // Command press is a hot path: LOAD <id> then T, both on the
    // command line, but bundled into one round-trip.
    if (!strcmp(p, "command/press")) {
      uint32_t id = 0;
      if (!webJsonGetUint(body, "id", id) || id == 0) {
        webUiSendError(client, "400 Bad Request", "Invalid command id"); return true;
      }
      char line[48];
      snprintf(line, sizeof(line), "LOAD %lu", (unsigned long)id);
      webUiRunSerialLine(line);
      if (storage.currentCommandId != id) {
        webUiSendError(client, "404 Not Found", "Command unavailable"); return true;
      }
      if (!transmitLastCapture()) {
        webUiSendError(client, "500 Internal Server Error", "Transmit failed"); return true;
      }
      statusLedPulseTransmit();
      webUiWriteOk(client);
      return true;
    }

    // Alexa toggle is a single bit in EEPROM; there is no clean
    // serial verb for "set this specific command", so keep it
    // structured.
    if (!strcmp(p, "command/alexa")) {
      uint32_t id = 0; bool on = false;
      if (!webJsonGetUint(body, "id", id) || id == 0 ||
          !webJsonGetBool(body, "enabled", on)) {
        webUiSendError(client, "400 Bad Request", "Invalid body"); return true;
      }
      if (!storageSetCommandHueEnabled(id, on)) {
        webUiSendError(client, "409 Conflict",
                       "Alexa setting could not be persisted"); return true;
      }
      webUiWriteOk(client);
      return true;
    }

    // Per-device category + Alexa Cloud exposure.
    //   POST /api/web/device/settings {"id":3,"category":"TV","cloud":true}
    if (!strcmp(p, "device/settings")) {
      uint32_t id = 0; bool cloud = false;
      char catName[16] = {0};
      if (!webJsonGetUint(body, "id", id) || id == 0 ||
          !webJsonGetString(body, "category", catName, sizeof(catName)) ||
          !webJsonGetBool(body, "cloud", cloud)) {
        webUiSendError(client, "400 Bad Request", "Invalid body"); return true;
      }
      const uint8_t cat = deviceCategoryFromName(catName);
      if (cloud && cat == 0) {
        webUiSendError(client, "400 Bad Request", "Pick a category first"); return true;
      }
      if (!storageSetDeviceCloud(id, cat, cloud)) {
        webUiSendError(client, "409 Conflict", "Device settings could not be saved"); return true;
      }
      webUiWriteOk(client);
      return true;
    }

if (!strcmp(p, "storage/reset")) {
  if (irState != IrEngineState::IDLE) {
    webUiSendError(client, "409 Conflict", "IR engine busy"); return true;
  }
  if (storageJobIsRunning()) {
    webUiSendError(client, "409 Conflict", "Storage job already running"); return true;
  }
  if (!storageJobStartReset()) {
    webUiSendError(client, "500 Internal Server Error", "Reset could not start"); return true;
  }
  webUiWriteOk(client, "\"started\":true");
  return true;
}
if (!strcmp(p, "storage/wipe")) {
  if (irState != IrEngineState::IDLE) {
    webUiSendError(client, "409 Conflict", "IR engine busy"); return true;
  }
  if (storageJobIsRunning()) {
    webUiSendError(client, "409 Conflict", "Storage job already running"); return true;
  }
  if (!storageJobStartWipe()) {
    webUiSendError(client, "500 Internal Server Error", "Wipe could not start"); return true;
  }
  webUiWriteOk(client, "\"started\":true");
  return true;
}

    // ---- Reordering endpoints ----------------------------------------
    if (!strcmp(p, "device/order")) {
      uint32_t id = 0;
      if (!webJsonGetUint(body, "id", id) || id == 0) {
        webUiSendError(client, "400 Bad Request", "Missing id"); return true;
      }
      const char *key = strstr(body, "\"command_ids\"");
      if (key == nullptr || (key = strchr(key, '[')) == nullptr) {
        webUiSendError(client, "400 Bad Request", "Missing command_ids"); return true;
      }
      ++key;
      uint16_t ids[64];
      uint16_t cnt = 0;
      while (*key && *key != ']' && cnt < 64U) {
        while (*key == ' ' || *key == ',' || *key == '\t' || *key == '\n') ++key;
        if (*key < '0' || *key > '9') break;
        uint32_t v = 0;
        while (*key >= '0' && *key <= '9') { v = v * 10U + (uint32_t)(*key - '0'); ++key; }
        if (v == 0 || v > 0xFFFFU) continue;
        ids[cnt++] = static_cast<uint16_t>(v);
      }
      if (cnt == 0U) { webUiSendError(client, "400 Bad Request", "Empty order"); return true; }
      if (!storageAppendOrderRecord(id, id, ids, cnt)) {
        webUiSendError(client, "500 Internal Server Error", "Order write failed"); return true;
      }
      webUiWriteOk(client);
      return true;
    }

    if (!strcmp(p, "devices/order")) {
      const char *key = strstr(body, "\"device_ids\"");
      if (key == nullptr || (key = strchr(key, '[')) == nullptr) {
        webUiSendError(client, "400 Bad Request", "Missing device_ids"); return true;
      }
      ++key;
      uint16_t ids[64];
      uint16_t cnt = 0;
      while (*key && *key != ']' && cnt < 64U) {
        while (*key == ' ' || *key == ',' || *key == '\t' || *key == '\n') ++key;
        if (*key < '0' || *key > '9') break;
        uint32_t v = 0;
        while (*key >= '0' && *key <= '9') { v = v * 10U + (uint32_t)(*key - '0'); ++key; }
        if (v == 0 || v > 0xFFFFU) continue;
        ids[cnt++] = static_cast<uint16_t>(v);
      }
      if (cnt == 0U) { webUiSendError(client, "400 Bad Request", "Empty order"); return true; }
      if (!storageAppendOrderRecord(1U, 0U, ids, cnt)) {
        webUiSendError(client, "500 Internal Server Error", "Order write failed"); return true;
      }
      webUiWriteOk(client);
      return true;
    }

    webUiSendError(client, "404 Not Found", "Unknown WebUI POST"); return true;
  }

  webUiSendError(client, "405 Method Not Allowed", "Method not allowed");
  return true;
}