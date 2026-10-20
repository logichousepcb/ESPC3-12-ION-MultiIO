#include "BaseCore.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#if ARDUINO_USB_MODE
#include <hal/usb_serial_jtag_ll.h>
#endif

#include "BasePage.h"

namespace {

const char* PREFS_NS = "basecore";
const char* ADMIN_USER = "admin";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

BaseConfig g_cfg;
WebServer g_server(80);
DNSServer g_dns;
Preferences g_prefs;

String g_ssid, g_pass, g_host, g_admin;
String g_apSsid;
String g_macSuffix;

bool g_portal = false;
bool g_restartPending = false;
uint32_t g_restartAt = 0;

uint32_t g_lastRetry = 0;       // portal mode: when the last STA attempt began
bool g_retrying = false;        // portal mode: STA attempt in progress
uint32_t g_linkLostAt = 0;      // station mode: when the link dropped (0 = up)
uint32_t g_btnDownAt = 0;

bool g_updateStarted = false;
bool g_updateAuthOk = false;
bool g_updateFirstChunk = true;
String g_updateError;

bool g_i2cReady = false;
uint8_t g_i2cDevices[128];
size_t g_i2cDeviceCount = 0;
uint32_t g_i2cLastScanMs = 0;

struct Mcp23017State {
  bool configured = false;
  bool present = false;
  uint8_t address = 0;
  int interruptPin = -1;
  const char* label = nullptr;
  uint16_t directionMask = 0xFFFF;
  uint16_t gpioMask = 0;
  String error;
};

Mcp23017State g_mcp23017[2];
uint32_t g_mcp23017LastRefreshMs = 0;
volatile bool g_mcp23017InterruptPending[2] = {false, false};
uint32_t g_mcp23017ChangeSeq = 0;
bool g_mcp23017UseInterrupts = true;

bool g_ina226Present = false;
float g_ina226BusVoltageV = 0.0f;
float g_ina226ShuntVoltageMv = 0.0f;
float g_ina226CurrentMa = 0.0f;
float g_ina226PowerMw = 0.0f;
String g_ina226Error;

// Log output goes to UART0 (Serial) and, when the build enables it, the
// C3's native USB-Serial/JTAG port (USBSerial) as well.
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#define BASECORE_USB_SERIAL 1
#else
#define BASECORE_USB_SERIAL 0
#endif

class DualLog : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    Serial.write(buf, n);
#if BASECORE_USB_SERIAL
    USBSerial.write(buf, n);
#endif
    return n;
  }
};

DualLog g_log;

#if BASECORE_USB_SERIAL
const size_t USB_TX_BUFFER_SIZE = 4096;
#endif

bool g_wifiDisabledByPin = false;
bool g_wifiDisableRawState = false;
bool g_wifiDisableStableState = false;
uint32_t g_wifiDisableLastEdgeMs = 0;
bool g_powerSwitchOn = false;

void updateAllMcp23017States();
bool connectStation();
void startPortal();

// ---------------------------------------------------------------- helpers --

bool elapsed(uint32_t since, uint32_t ms) { return (uint32_t)(millis() - since) >= ms; }
float absf(float value) { return value < 0.0f ? -value : value; }

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

String jsonStr(const String& s) { return "\"" + jsonEscape(s) + "\""; }

void sendJson(int code, const String& body) {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(code, "application/json", body);
}

// Outcome of a settings/control action; shared by the HTTP and serial front ends.
struct ActionResult {
  bool ok;
  String message;
  bool restart;
};

ActionResult okResult(const String& msg, bool restart = false) { return {true, msg, restart}; }
ActionResult errResult(const String& msg) { return {false, msg, false}; }

String resultJson(const ActionResult& r) {
  return String("{\"ok\":") + (r.ok ? "true" : "false") + ",\"message\":" + jsonStr(r.message) +
         ",\"restart\":" + (r.restart ? "true" : "false") + "}";
}

void sendResult(const ActionResult& r) { sendJson(r.ok ? 200 : 400, resultJson(r)); }

void sendResult(bool ok, const String& msg, bool restart = false) { sendResult({ok, msg, restart}); }

// Named request arguments, from an HTTP form or a serial command line.
class ArgSource {
 public:
  virtual bool has(const char* name) const = 0;
  virtual String get(const char* name) const = 0;
};

class HttpArgs : public ArgSource {
 public:
  bool has(const char* name) const override { return g_server.hasArg(name); }
  String get(const char* name) const override { return g_server.arg(name); }
};

// Parses "1", "true", "on" / "0", "false", "off". Returns false if invalid.
bool parseBoolArg(const String& arg, bool& value) {
  if (arg == "1" || arg == "true" || arg == "on") {
    value = true;
  } else if (arg == "0" || arg == "false" || arg == "off") {
    value = false;
  } else {
    return false;
  }
  return true;
}

bool validHostname(const String& h) {
  if (h.length() < 1 || h.length() > 32) return false;
  if (h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h[i];
    if (!(isdigit(c) || (c >= 'a' && c <= 'z') || c == '-')) return false;
  }
  return true;
}

// ---------------------------------------------------------------- storage --

void loadSettings() {
  g_prefs.begin(PREFS_NS, true);
  g_ssid = g_prefs.getString("ssid", "");
  g_pass = g_prefs.getString("pass", "");
  g_host = g_prefs.getString("host", "");
  g_admin = g_prefs.getString("admin", "");
  g_prefs.end();

  uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  g_macSuffix = suffix;

  if (!validHostname(g_host)) {
    g_host = String(g_cfg.hostnamePrefix) + g_macSuffix;
    g_host.toLowerCase();
  }
  g_apSsid = String(g_cfg.apSsidPrefix) + g_macSuffix;
}

void putString(const char* key, const String& value) {
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putString(key, value);
  g_prefs.end();
}

void forgetWifi() {
  g_prefs.begin(PREFS_NS, false);
  g_prefs.remove("ssid");
  g_prefs.remove("pass");
  g_prefs.end();
  g_ssid = "";
  g_pass = "";
  WiFi.disconnect(false, true);  // also clear anything the IDF stored itself
}

// --------------------------------------------------------------- power IO --

bool powerSwitchConfigured() { return g_cfg.powerSwitchPin >= 0; }

void applyPowerSwitchOutput() {
  if (!powerSwitchConfigured()) return;
  digitalWrite(g_cfg.powerSwitchPin,
               g_powerSwitchOn == g_cfg.powerSwitchActiveHigh ? HIGH : LOW);
}

void initPowerSwitch() {
  if (!powerSwitchConfigured()) return;

  g_prefs.begin(PREFS_NS, true);
  bool haveStoredState = g_prefs.isKey("powerOn");
  g_powerSwitchOn = haveStoredState ? g_prefs.getBool("powerOn", g_cfg.powerSwitchDefaultOn)
                                    : g_cfg.powerSwitchDefaultOn;
  g_prefs.end();

  pinMode(g_cfg.powerSwitchPin, OUTPUT);
  applyPowerSwitchOutput();
  g_log.printf("[power] %s on GPIO%d initialized %s\n",
                g_cfg.powerSwitchLabel ? g_cfg.powerSwitchLabel : "power switch",
                g_cfg.powerSwitchPin, g_powerSwitchOn ? "on" : "off");
}

void setPowerSwitch(bool on) {
  if (!powerSwitchConfigured()) return;
  g_powerSwitchOn = on;
  applyPowerSwitchOutput();
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putBool("powerOn", g_powerSwitchOn);
  g_prefs.end();
}

void loadMcp23017Mode() {
  g_prefs.begin(PREFS_NS, true);
  bool haveStoredMode = g_prefs.isKey("mcpInt");
  g_mcp23017UseInterrupts = haveStoredMode ? g_prefs.getBool("mcpInt", true) : true;
  g_prefs.end();
}

void setMcp23017UseInterrupts(bool useInterrupts) {
  g_mcp23017UseInterrupts = useInterrupts;
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putBool("mcpInt", g_mcp23017UseInterrupts);
  g_prefs.end();
  g_mcp23017InterruptPending[0] = false;
  g_mcp23017InterruptPending[1] = false;
  updateAllMcp23017States();
}

// -------------------------------------------------------------- WiFi gate --

bool wifiDisableConfigured() { return g_cfg.wifiDisablePin >= 0; }

bool wifiDisableAsserted() {
  if (!wifiDisableConfigured()) return false;
  int level = digitalRead(g_cfg.wifiDisablePin);
  return g_cfg.wifiDisableActiveLow ? level == LOW : level == HIGH;
}

void initWifiDisableInput() {
  if (!wifiDisableConfigured()) return;
  g_wifiDisableRawState = wifiDisableAsserted();
  g_wifiDisableStableState = g_wifiDisableRawState;
  g_wifiDisableLastEdgeMs = millis();
}

void disableWifiByPin() {
  if (g_wifiDisabledByPin) return;
  g_wifiDisabledByPin = true;
  g_retrying = false;
  g_linkLostAt = 0;
  g_portal = false;
  MDNS.end();
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  g_log.printf("[wifi] disabled by GPIO%d\n", g_cfg.wifiDisablePin);
}

void enableWifiAfterPin() {
  if (!g_wifiDisabledByPin) return;
  g_wifiDisabledByPin = false;
  g_log.printf("[wifi] re-enabling after GPIO%d released\n", g_cfg.wifiDisablePin);
  if (!connectStation()) startPortal();
}

void wifiDisableTick() {
  if (!wifiDisableConfigured()) return;

  bool asserted = wifiDisableAsserted();
  if (asserted != g_wifiDisableRawState) {
    g_wifiDisableRawState = asserted;
    g_wifiDisableLastEdgeMs = millis();
  }

  if (g_wifiDisableRawState != g_wifiDisableStableState &&
      elapsed(g_wifiDisableLastEdgeMs, g_cfg.wifiDisableDebounceMs)) {
    g_wifiDisableStableState = g_wifiDisableRawState;
  }

  if (g_wifiDisableStableState) {
    disableWifiByPin();
  } else if (g_wifiDisabledByPin) {
    enableWifiAfterPin();
  }
}

// -------------------------------------------------------------------- I2C --

bool i2cConfigured() { return g_cfg.i2cSdaPin >= 0 && g_cfg.i2cSclPin >= 0; }

bool i2cPinsValid() { return i2cConfigured() && g_cfg.i2cSdaPin != g_cfg.i2cSclPin; }

bool ina226Configured() {
  return g_cfg.ina226Address >= 0 && g_cfg.ina226Address <= 0x7F && g_cfg.ina226ShuntMilliOhms > 0.0f;
}

bool readI2cRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)address, 2) != 2) return false;
  value = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

bool writeI2cRegister16(uint8_t address, uint8_t reg, uint16_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write((uint8_t)(value & 0xFF));
  Wire.write((uint8_t)(value >> 8));
  return Wire.endTransmission() == 0;
}

bool readI2cRegisterPairLE(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)address, 2) != 2) return false;
  value = (uint16_t)Wire.read() | ((uint16_t)Wire.read() << 8);
  return true;
}

bool writeI2cRegisterPairLE(uint8_t address, uint8_t reg, uint16_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write((uint8_t)(value & 0xFF));
  Wire.write((uint8_t)(value >> 8));
  return Wire.endTransmission() == 0;
}

void IRAM_ATTR onMcp23017Interrupt0() { g_mcp23017InterruptPending[0] = true; }
void IRAM_ATTR onMcp23017Interrupt1() { g_mcp23017InterruptPending[1] = true; }

bool mcp23017AddressValid(int address) { return address >= 0x20 && address <= 0x27; }
bool mcp23017InterruptConfigured(const Mcp23017State& chip) { return chip.interruptPin >= 0; }
bool mcp23017InterruptActive(const Mcp23017State& chip) {
  return mcp23017InterruptConfigured(chip) && digitalRead(chip.interruptPin) == LOW;
}

void initMcp23017States() {
  g_mcp23017[0].configured = mcp23017AddressValid(g_cfg.mcp23017Address1);
  g_mcp23017[0].address = (uint8_t)g_cfg.mcp23017Address1;
  g_mcp23017[0].interruptPin = g_cfg.mcp23017InterruptPin1;
  g_mcp23017[0].label = g_cfg.mcp23017Label1;
  g_mcp23017[1].configured = mcp23017AddressValid(g_cfg.mcp23017Address2);
  g_mcp23017[1].address = (uint8_t)g_cfg.mcp23017Address2;
  g_mcp23017[1].interruptPin = g_cfg.mcp23017InterruptPin2;
  g_mcp23017[1].label = g_cfg.mcp23017Label2;
}

bool configureMcp23017Interrupts(Mcp23017State& chip) {
  if (!chip.configured || !g_i2cReady) return false;
  if (!writeI2cRegisterPairLE(chip.address, 0x0A, 0x4444) ||
      !writeI2cRegisterPairLE(chip.address, 0x0C, 0xFFFF) ||
      !writeI2cRegisterPairLE(chip.address, 0x04, 0xFFFF) ||
      !writeI2cRegisterPairLE(chip.address, 0x08, 0x0000)) {
    chip.present = false;
    chip.error = "interrupt/pull-up setup failed";
    return false;
  }
  uint16_t discard = 0;
  readI2cRegisterPairLE(chip.address, 0x12, discard);  // clear any pending interrupt state
  return true;
}

bool updateMcp23017State(Mcp23017State& chip, bool fromInterrupt = false) {
  bool wasPresent = chip.present;
  uint16_t oldDirectionMask = chip.directionMask;
  uint16_t oldGpioMask = chip.gpioMask;
  if (!chip.configured) {
    chip.present = false;
    chip.error = "disabled";
    return false;
  }
  if (!g_i2cReady) {
    chip.present = false;
    chip.error = "I2C bus unavailable";
    return false;
  }

  uint16_t directionMask = 0;
  uint16_t gpioMask = 0;
  if (!fromInterrupt && !readI2cRegisterPairLE(chip.address, 0x00, directionMask)) {
    chip.present = false;
    chip.error = "MCP23017 not responding";
    return false;
  }
  if (fromInterrupt) {
    if (!readI2cRegisterPairLE(chip.address, 0x10, gpioMask)) {
      chip.present = false;
      chip.error = "INTCAP read failed";
      return false;
    }
    directionMask = chip.directionMask;
  } else if (!readI2cRegisterPairLE(chip.address, 0x12, gpioMask)) {
    chip.present = false;
    chip.error = "GPIO read failed";
    return false;
  }

  bool changed = !wasPresent || oldDirectionMask != directionMask || oldGpioMask != gpioMask;
  chip.directionMask = directionMask;
  chip.gpioMask = gpioMask;
  chip.present = true;
  chip.error = "";
  g_mcp23017LastRefreshMs = millis();
  if (changed) g_mcp23017ChangeSeq++;
  return true;
}

void updateAllMcp23017States() {
  for (size_t i = 0; i < 2; i++) updateMcp23017State(g_mcp23017[i]);
}

void mcp23017Tick() {
  if (!g_i2cReady) return;

  if (!g_mcp23017UseInterrupts) {
    if (elapsed(g_mcp23017LastRefreshMs, 250)) updateAllMcp23017States();
    return;
  }

  bool refreshed = false;
  for (size_t i = 0; i < 2; i++) {
    Mcp23017State& chip = g_mcp23017[i];
    if (!chip.configured) continue;
    bool pending = g_mcp23017InterruptPending[i];
    if (pending || mcp23017InterruptActive(chip)) {
      g_mcp23017InterruptPending[i] = false;
      updateMcp23017State(chip, true);
      refreshed = true;
    }
  }

  if (!refreshed && elapsed(g_mcp23017LastRefreshMs, 5000)) updateAllMcp23017States();
}

void clearIna226Reading(const String& error) {
  g_ina226BusVoltageV = 0.0f;
  g_ina226ShuntVoltageMv = 0.0f;
  g_ina226CurrentMa = 0.0f;
  g_ina226PowerMw = 0.0f;
  g_ina226Error = error;
}

bool detectIna226() {
  g_ina226Present = false;
  if (!ina226Configured()) {
    clearIna226Reading("disabled");
    return false;
  }
  if (!g_i2cReady) {
    clearIna226Reading("I2C bus unavailable");
    return false;
  }

  uint16_t manufacturerId = 0;
  uint16_t dieId = 0;
  if (!readI2cRegister16((uint8_t)g_cfg.ina226Address, 0xFE, manufacturerId) ||
      !readI2cRegister16((uint8_t)g_cfg.ina226Address, 0xFF, dieId)) {
    clearIna226Reading("INA226 not responding");
    return false;
  }
  if (manufacturerId != 0x5449 || dieId != 0x2260) {
    clearIna226Reading("unexpected device at INA226 address");
    return false;
  }

  g_ina226Present = true;
  g_ina226Error = "";
  return true;
}

void updateIna226Reading() {
  if (!ina226Configured()) {
    g_ina226Present = false;
    clearIna226Reading("disabled");
    return;
  }
  if (!g_i2cReady) {
    g_ina226Present = false;
    clearIna226Reading("I2C bus unavailable");
    return;
  }
  if (!g_ina226Present && !detectIna226()) return;

  uint16_t shuntRawWord = 0;
  uint16_t busRawWord = 0;
  if (!readI2cRegister16((uint8_t)g_cfg.ina226Address, 0x01, shuntRawWord) ||
      !readI2cRegister16((uint8_t)g_cfg.ina226Address, 0x02, busRawWord)) {
    g_ina226Present = false;
    clearIna226Reading("INA226 read failed");
    return;
  }

  int16_t shuntRaw = (int16_t)shuntRawWord;
  g_ina226ShuntVoltageMv = (float)shuntRaw * 0.0025f;
  g_ina226BusVoltageV = (float)busRawWord * 0.00125f;

  float shuntOhms = g_cfg.ina226ShuntMilliOhms / 1000.0f;
  g_ina226CurrentMa = shuntOhms > 0.0f ? g_ina226ShuntVoltageMv / shuntOhms : 0.0f;
  g_ina226PowerMw = g_ina226BusVoltageV * g_ina226CurrentMa;
  if (absf(g_ina226ShuntVoltageMv) < 0.01f) g_ina226ShuntVoltageMv = 0.0f;
  if (absf(g_ina226CurrentMa) < 0.1f) g_ina226CurrentMa = 0.0f;
  if (absf(g_ina226PowerMw) < 1.0f) g_ina226PowerMw = 0.0f;
  g_ina226Error = "";
}

void logI2cScanResult() {
  if (!g_i2cReady) {
    g_log.println("[i2c] scan unavailable");
    return;
  }
  if (g_i2cDeviceCount == 0) {
    g_log.println("[i2c] no devices found");
    return;
  }

  g_log.printf("[i2c] found %u device(s):", (unsigned)g_i2cDeviceCount);
  for (size_t i = 0; i < g_i2cDeviceCount; i++) {
    g_log.printf(" 0x%02X", g_i2cDevices[i]);
  }
  g_log.println();
}

void scanI2cBus(bool logResult) {
  g_i2cDeviceCount = 0;
  g_i2cLastScanMs = millis();
  if (!g_i2cReady) {
    if (logResult) logI2cScanResult();
    return;
  }

  for (uint8_t addr = 1; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0 && g_i2cDeviceCount < (sizeof(g_i2cDevices) / sizeof(g_i2cDevices[0]))) {
      g_i2cDevices[g_i2cDeviceCount++] = addr;
    }
  }

  if (logResult) logI2cScanResult();
}

void initI2c() {
  if (!i2cConfigured()) return;

  if (!i2cPinsValid()) {
    g_log.printf("[i2c] invalid pin config: SDA GPIO%d, SCL GPIO%d\n", g_cfg.i2cSdaPin, g_cfg.i2cSclPin);
    return;
  }

  if (!Wire.begin(g_cfg.i2cSdaPin, g_cfg.i2cSclPin, g_cfg.i2cClockHz)) {
    g_log.printf("[i2c] begin failed on SDA GPIO%d / SCL GPIO%d\n", g_cfg.i2cSdaPin, g_cfg.i2cSclPin);
    return;
  }
  Wire.setClock(g_cfg.i2cClockHz);

  g_i2cReady = true;
  g_log.printf("[i2c] initialized on SDA GPIO%d / SCL GPIO%d @ %lu Hz\n",
                g_cfg.i2cSdaPin, g_cfg.i2cSclPin, (unsigned long)g_cfg.i2cClockHz);
  scanI2cBus(true);
  for (size_t i = 0; i < 2; i++) {
    if (!g_mcp23017[i].configured) continue;
    configureMcp23017Interrupts(g_mcp23017[i]);
  }
  updateAllMcp23017States();
  for (size_t i = 0; i < 2; i++) {
    if (!g_mcp23017[i].configured) continue;
    String interruptSuffix = "";
    if (mcp23017InterruptConfigured(g_mcp23017[i])) {
      interruptSuffix = " (INT GPIO";
      interruptSuffix += g_mcp23017[i].interruptPin;
      interruptSuffix += ")";
    }
    if (g_mcp23017[i].present) {
      g_log.printf("[mcp23017] %s detected at 0x%02X%s\n",
                    g_mcp23017[i].label ? g_mcp23017[i].label : "GPIO expander",
                    g_mcp23017[i].address, interruptSuffix.c_str());
    } else {
      g_log.printf("[mcp23017] %s unavailable at 0x%02X%s: %s\n",
                    g_mcp23017[i].label ? g_mcp23017[i].label : "GPIO expander",
                    g_mcp23017[i].address, interruptSuffix.c_str(),
                    g_mcp23017[i].error.c_str());
    }
  }

  if (ina226Configured()) {
    if (detectIna226()) {
      updateIna226Reading();
      g_log.printf("[ina226] detected at 0x%02X, bus %.3f V, current %.1f mA, power %.1f mW\n",
                    g_cfg.ina226Address, g_ina226BusVoltageV, g_ina226CurrentMa, g_ina226PowerMw);
    } else {
      g_log.printf("[ina226] monitor unavailable at 0x%02X: %s\n",
                    g_cfg.ina226Address, g_ina226Error.c_str());
    }
  }
}

// ------------------------------------------------------------------- WiFi --

bool connectStation() {
  if (g_ssid.isEmpty()) return false;

  g_log.printf("[wifi] connecting to \"%s\"\n", g_ssid.c_str());
  WiFi.setHostname(g_host.c_str());  // must precede mode()/begin() on 2.x
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && !elapsed(start, g_cfg.staConnectTimeoutMs)) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    g_log.println("[wifi] connection failed");
    WiFi.disconnect(true);
    return false;
  }

  g_log.printf("[wifi] connected, IP %s, http://%s.local/\n",
                WiFi.localIP().toString().c_str(), g_host.c_str());
  if (MDNS.begin(g_host.c_str())) MDNS.addService("http", "tcp", 80);
  return true;
}

void startPortal() {
  g_log.printf("[portal] starting AP \"%s\" at %s\n", g_apSsid.c_str(),
                AP_IP.toString().c_str());
  WiFi.setHostname(g_host.c_str());
  WiFi.mode(WIFI_AP_STA);  // STA side is needed for scanning and retries
  WiFi.setAutoReconnect(false);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  const char* pass = (g_cfg.apPassword && strlen(g_cfg.apPassword) >= 8) ? g_cfg.apPassword : nullptr;
  WiFi.softAP(g_apSsid.c_str(), pass);

  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", AP_IP);

  g_portal = true;
  g_retrying = false;
  g_lastRetry = millis();
}

// Portal mode: periodically retry the saved network, but only while nobody is
// connected to the AP (a STA attempt can move the radio channel under them).
void portalRetryTick() {
  if (g_ssid.isEmpty()) return;

  if (g_retrying) {
    if (WiFi.status() == WL_CONNECTED) {
      g_log.println("[portal] saved network is back, restarting into station mode");
      BaseCore::scheduleRestart(500);
      g_retrying = false;
    } else if (elapsed(g_lastRetry, g_cfg.staConnectTimeoutMs)) {
      WiFi.disconnect(false);
      g_retrying = false;
      g_lastRetry = millis();
    }
    return;
  }

  if (elapsed(g_lastRetry, g_cfg.staRetryIntervalMs) && WiFi.softAPgetStationNum() == 0) {
    g_log.println("[portal] retrying saved network");
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_retrying = true;
    g_lastRetry = millis();
  }
}

// Station mode: if the link stays down too long, bring up the portal so the
// device can still be reached / reconfigured.
void stationWatchTick() {
  if (WiFi.status() == WL_CONNECTED) {
    g_linkLostAt = 0;
    return;
  }
  if (g_linkLostAt == 0) {
    g_linkLostAt = millis() | 1;
    g_log.println("[wifi] link lost, auto-reconnecting");
  } else if (elapsed(g_linkLostAt, g_cfg.staRetryIntervalMs)) {
    g_log.println("[wifi] link down too long, falling back to setup portal");
    g_linkLostAt = 0;
    MDNS.end();
    startPortal();
  }
}

void resetButtonTick() {
  if (g_cfg.resetButtonPin < 0) return;
  if (digitalRead(g_cfg.resetButtonPin) == LOW) {
    if (g_btnDownAt == 0) {
      g_btnDownAt = millis() | 1;
    } else if (elapsed(g_btnDownAt, g_cfg.resetHoldMs)) {
      g_log.println("[reset] button held, erasing WiFi credentials");
      forgetWifi();
      ESP.restart();
    }
  } else {
    g_btnDownAt = 0;
  }
}

// --------------------------------------------------------------- handlers --

bool isOwnHost() {
  String h = g_server.hostHeader();
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  return h.isEmpty() || h == AP_IP.toString() || h == WiFi.localIP().toString() ||
         h == g_host || h == g_host + ".local";
}

// In portal mode, requests for any other host (OS captive-portal probes,
// typed URLs) are redirected to the setup page so the phone/laptop pops it up.
bool captiveRedirect() {
  if (!g_portal || isOwnHost()) return false;
  g_server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
  g_server.send(302, "text/plain", "");
  return true;
}

void handleRoot() {
  if (captiveRedirect()) return;
  if (!BaseCore::authorize()) return;
  String page = FPSTR(BASE_PAGE_HTML);
  page.replace("%TITLE%", g_cfg.title);
  page.replace("%PAGE%", "home");
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "text/html", page);
}

void handleConfigPage() {
  if (captiveRedirect()) return;
  if (!BaseCore::authorize()) return;
  String page = FPSTR(BASE_PAGE_HTML);
  page.replace("%TITLE%", g_cfg.title);
  page.replace("%PAGE%", "config");
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "text/html", page);
}

void handleGpioPage() {
  if (captiveRedirect()) return;
  if (!BaseCore::authorize()) return;
  String page = FPSTR(BASE_PAGE_HTML);
  page.replace("%TITLE%", g_cfg.title);
  page.replace("%PAGE%", "gpio");
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "text/html", page);
}

// ------------------------------------------------- JSON builders / actions --
// Used by both the HTTP API and the USB-serial command interface.

String buildStatusJson() {
  bool connected = WiFi.status() == WL_CONNECTED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  updateIna226Reading();

  String j = "{";
  j += "\"title\":" + jsonStr(g_cfg.title);
  j += ",\"version\":" + jsonStr(g_cfg.fwVersion);
  j += ",\"build\":" + jsonStr(String(__DATE__) + " " + __TIME__);
  j += ",\"mode\":" + jsonStr(g_portal ? "portal" : "station");
  j += ",\"savedSsid\":" + jsonStr(g_ssid);
  j += ",\"connected\":" + String(connected ? "true" : "false");
  j += ",\"ip\":" + jsonStr(connected ? WiFi.localIP().toString() : "");
  j += ",\"rssi\":" + String(connected ? WiFi.RSSI() : 0);
  j += ",\"apSsid\":" + jsonStr(g_portal ? g_apSsid : "");
  j += ",\"apIp\":" + jsonStr(g_portal ? AP_IP.toString() : "");
  j += ",\"apClients\":" + String(g_portal ? WiFi.softAPgetStationNum() : 0);
  j += ",\"hostname\":" + jsonStr(g_host);
  j += ",\"mac\":" + jsonStr(WiFi.macAddress());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"chip\":" + jsonStr(String(ESP.getChipModel()) + " rev " + ESP.getChipRevision());
  j += ",\"flash\":" + String(ESP.getFlashChipSize());
  j += ",\"sketch\":" + String(ESP.getSketchSize());
  j += ",\"otaSpace\":" + String(ESP.getFreeSketchSpace());
  j += ",\"partition\":" + jsonStr(running ? running->label : "?");
  j += ",\"sdk\":" + jsonStr(ESP.getSdkVersion());
  j += ",\"authEnabled\":" + String(g_admin.isEmpty() ? "false" : "true");
  j += ",\"i2cEnabled\":" + String(i2cConfigured() ? "true" : "false");
  j += ",\"i2cReady\":" + String(g_i2cReady ? "true" : "false");
  j += ",\"i2cSdaPin\":" + String(g_cfg.i2cSdaPin);
  j += ",\"i2cSclPin\":" + String(g_cfg.i2cSclPin);
  j += ",\"i2cClockHz\":" + String(g_cfg.i2cClockHz);
  j += ",\"i2cDeviceCount\":" + String(g_i2cDeviceCount);
  j += ",\"ina226Enabled\":" + String(ina226Configured() ? "true" : "false");
  j += ",\"ina226Present\":" + String(g_ina226Present ? "true" : "false");
  j += ",\"ina226Address\":" + String(g_cfg.ina226Address);
  j += ",\"ina226Label\":" + jsonStr(g_cfg.ina226Label ? g_cfg.ina226Label : "Power monitor");
  j += ",\"ina226ShuntMilliOhms\":" + String(g_cfg.ina226ShuntMilliOhms, 3);
  j += ",\"ina226BusVoltage\":" + String(g_ina226BusVoltageV, 4);
  j += ",\"ina226ShuntVoltageMv\":" + String(g_ina226ShuntVoltageMv, 4);
  j += ",\"ina226CurrentMa\":" + String(g_ina226CurrentMa, 2);
  j += ",\"ina226PowerMw\":" + String(g_ina226PowerMw, 2);
  j += ",\"ina226Error\":" + jsonStr(g_ina226Error);
  j += ",\"wifiDisableEnabled\":" + String(wifiDisableConfigured() ? "true" : "false");
  j += ",\"wifiDisablePin\":" + String(g_cfg.wifiDisablePin);
  j += ",\"wifiDisableDebounceMs\":" + String(g_cfg.wifiDisableDebounceMs);
  j += ",\"wifiDisabledByPin\":" + String(g_wifiDisabledByPin ? "true" : "false");
  j += ",\"powerSwitchEnabled\":" + String(powerSwitchConfigured() ? "true" : "false");
  j += ",\"powerSwitchPin\":" + String(g_cfg.powerSwitchPin);
  j += ",\"powerSwitchLabel\":" + jsonStr(g_cfg.powerSwitchLabel ? g_cfg.powerSwitchLabel : "Power");
  j += ",\"powerSwitchOn\":" + String(g_powerSwitchOn ? "true" : "false");
  j += "}";
  return j;
}

String buildScanJson(bool fresh) {
  if (g_wifiDisabledByPin) return "{\"scanning\":false,\"networks\":[],\"message\":\"WiFi is disabled.\"}";
  int n = WiFi.scanComplete();
  if (fresh && n >= 0) {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    return "{\"scanning\":true}";
  }
  if (n == WIFI_SCAN_RUNNING) return "{\"scanning\":true}";

  // De-duplicate by SSID, keeping the strongest entry.
  String j = "{\"scanning\":false,\"networks\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int k = 0; k < n && !dup; k++) {
      if (k != i && WiFi.SSID(k) == ssid &&
          (WiFi.RSSI(k) > WiFi.RSSI(i) || (WiFi.RSSI(k) == WiFi.RSSI(i) && k < i))) {
        dup = true;
      }
    }
    if (dup) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"ssid\":" + jsonStr(ssid) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  return j;
}

String buildI2cJson() {
  if (!i2cConfigured()) {
    return "{\"enabled\":false,\"ready\":false,\"message\":\"I2C scanning is disabled.\"}";
  }
  if (!g_i2cReady) {
    return String("{\"enabled\":true,\"ready\":false,\"sdaPin\":") + g_cfg.i2cSdaPin +
           ",\"sclPin\":" + g_cfg.i2cSclPin +
           ",\"clockHz\":" + g_cfg.i2cClockHz +
           ",\"message\":\"I2C bus initialization failed.\"}";
  }

  scanI2cBus(false);
  updateAllMcp23017States();
  if (ina226Configured()) detectIna226();

  String j = String("{\"enabled\":true,\"ready\":true,\"sdaPin\":") + g_cfg.i2cSdaPin +
             ",\"sclPin\":" + g_cfg.i2cSclPin +
             ",\"clockHz\":" + g_cfg.i2cClockHz +
             ",\"count\":" + String(g_i2cDeviceCount) +
             ",\"devices\":[";
  for (size_t i = 0; i < g_i2cDeviceCount; i++) {
    if (i) j += ",";
    char hex[7];
    snprintf(hex, sizeof(hex), "0x%02X", g_i2cDevices[i]);
    j += String("{\"address\":") + g_i2cDevices[i] + ",\"hex\":" + jsonStr(hex) + "}";
  }
  j += "]}";
  return j;
}

String buildGpioJson() {
  mcp23017Tick();

  String j = "{\"enabled\":true,\"seq\":" + String(g_mcp23017ChangeSeq) +
             ",\"useInterrupts\":" + String(g_mcp23017UseInterrupts ? "true" : "false") +
             ",\"chips\":[";
  bool firstChip = true;
  for (size_t i = 0; i < 2; i++) {
    const Mcp23017State& chip = g_mcp23017[i];
    if (!chip.configured) continue;
    if (!firstChip) j += ",";
    firstChip = false;
    bool interruptActive = mcp23017InterruptActive(chip);
    j += "{\"index\":" + String(i) +
         ",\"label\":" + jsonStr(chip.label ? chip.label : "GPIO expander") +
         ",\"address\":" + String(chip.address) +
         ",\"interruptPin\":" + String(chip.interruptPin) +
         ",\"interruptActive\":" + String(interruptActive ? "true" : "false") +
         ",\"present\":" + String(chip.present ? "true" : "false") +
         ",\"error\":" + jsonStr(chip.error) +
         ",\"pins\":[";
    for (int pin = 0; pin < 16; pin++) {
      if (pin) j += ",";
      bool isInput = (chip.directionMask & (1u << pin)) != 0;
      bool isHigh = (chip.gpioMask & (1u << pin)) != 0;
      char pinName[4];
      snprintf(pinName, sizeof(pinName), "%c%d", pin < 8 ? 'A' : 'B', pin % 8);
      j += "{\"pin\":" + String(pin) +
           ",\"name\":" + jsonStr(pinName) +
           ",\"mode\":" + jsonStr(isInput ? "input" : "output") +
           ",\"high\":" + String(isHigh ? "true" : "false") + "}";
    }
    j += "]}";
  }
  j += "]}";
  return j;
}

ActionResult actionGpioMode(const ArgSource& args) {
  if (!args.has("useInterrupts")) return errResult("Missing 'useInterrupts' value.");
  bool useInterrupts;
  if (!parseBoolArg(args.get("useInterrupts"), useInterrupts)) return errResult("Invalid 'useInterrupts' value.");

  setMcp23017UseInterrupts(useInterrupts);
  return okResult(useInterrupts ? "GPIO interrupt mode enabled." : "GPIO polling mode enabled.");
}

ActionResult actionWifiSave(const ArgSource& args) {
  String ssid = args.get("ssid");
  String pass = args.get("pass");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32) return errResult("SSID must be 1-32 characters.");
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63))
    return errResult("Password must be empty (open network) or 8-63 characters.");

  g_prefs.begin(PREFS_NS, false);
  g_prefs.putString("ssid", ssid);
  g_prefs.putString("pass", pass);
  g_prefs.end();
  g_ssid = ssid;
  g_pass = pass;

  BaseCore::scheduleRestart(1500);
  return okResult("Saved. Restarting to connect to \"" + ssid + "\". If it can't connect, the setup AP \"" +
                      g_apSsid + "\" will come back.", true);
}

ActionResult actionDeviceSave(const ArgSource& args) {
  bool restart = false;

  if (args.has("hostname")) {
    String h = args.get("hostname");
    h.trim();
    h.toLowerCase();
    if (!validHostname(h))
      return errResult("Hostname: 1-32 chars, a-z, 0-9 and '-', not starting/ending with '-'.");
    if (h != g_host) {
      putString("host", h);
      g_host = h;
      restart = true;
    }
  }

  if (args.get("adminClear") == "1") {
    putString("admin", "");
    g_admin = "";
  } else if (args.get("admin").length() > 0) {
    String a = args.get("admin");
    if (a.length() < 4 || a.length() > 64) return errResult("Admin password must be 4-64 characters.");
    putString("admin", a);
    g_admin = a;
  }

  if (restart) BaseCore::scheduleRestart(1500);
  return okResult(restart ? "Saved. Restarting to apply the new hostname." : "Saved.", restart);
}

ActionResult actionPowerSwitch(const ArgSource& args) {
  if (!powerSwitchConfigured()) return errResult("Power switch is not configured.");
  if (!args.has("on")) return errResult("Missing 'on' value.");
  bool on;
  if (!parseBoolArg(args.get("on"), on)) return errResult("Invalid 'on' value.");

  setPowerSwitch(on);
  return okResult(String(g_cfg.powerSwitchLabel ? g_cfg.powerSwitchLabel : "Power") +
                  (on ? " turned on." : " turned off."));
}

ActionResult actionForget(const ArgSource&) {
  forgetWifi();
  BaseCore::scheduleRestart(1500);
  return okResult("WiFi credentials erased. Restarting into setup AP \"" + g_apSsid + "\".", true);
}

ActionResult actionReboot(const ArgSource&) {
  BaseCore::scheduleRestart(1000);
  return okResult("Restarting.", true);
}

// --------------------------------------------------------------- handlers --

void handleStatus() {
  if (!BaseCore::authorize()) return;
  sendJson(200, buildStatusJson());
}

void handleScan() {
  if (!BaseCore::authorize()) return;
  sendJson(200, buildScanJson(g_server.hasArg("fresh")));
}

void handleI2cScan() {
  if (!BaseCore::authorize()) return;
  sendJson(200, buildI2cJson());
}

void handleGpioStatus() {
  if (!BaseCore::authorize()) return;
  sendJson(200, buildGpioJson());
}

void handleGpioMode() {
  if (!BaseCore::authorize()) return;
  sendResult(actionGpioMode(HttpArgs()));
}

void handleWifiSave() {
  if (!BaseCore::authorize()) return;
  sendResult(actionWifiSave(HttpArgs()));
}

void handleDeviceSave() {
  if (!BaseCore::authorize()) return;
  sendResult(actionDeviceSave(HttpArgs()));
}

void handlePowerSwitch() {
  if (!BaseCore::authorize()) return;
  sendResult(actionPowerSwitch(HttpArgs()));
}

void handleForget() {
  if (!BaseCore::authorize()) return;
  sendResult(actionForget(HttpArgs()));
}

void handleReboot() {
  if (!BaseCore::authorize()) return;
  sendResult(actionReboot(HttpArgs()));
}

// Streams the uploaded .bin straight into the inactive OTA partition.
void handleUpdateUpload() {
  HTTPUpload& up = g_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    g_updateStarted = true;
    g_updateFirstChunk = true;
    g_updateError = "";
    g_updateAuthOk = g_admin.isEmpty() || g_server.authenticate(ADMIN_USER, g_admin.c_str());
    if (!g_updateAuthOk) {
      g_updateError = "Unauthorized";
      return;
    }
    g_log.printf("[ota] receiving %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) g_updateError = Update.errorString();
    return;
  }

  if (!g_updateAuthOk || !g_updateError.isEmpty()) {
    if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
      if (Update.isRunning()) Update.abort();
    }
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    if (g_updateFirstChunk) {
      g_updateFirstChunk = false;
      if (up.currentSize < 16 || up.buf[0] != 0xE9) {
        g_updateError = "Not an ESP32 application image (.bin).";
        Update.abort();
        return;
      }
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
      uint16_t chipId = up.buf[12] | (up.buf[13] << 8);
      if (chipId != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        g_updateError = "Image was built for a different chip (expected ESP32-C3).";
        Update.abort();
        return;
      }
#endif
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      g_updateError = Update.errorString();
      if (Update.isRunning()) Update.abort();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      g_updateError = Update.errorString();
    } else {
      g_log.printf("[ota] success, %u bytes\n", (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    g_updateError = "Upload aborted.";
  }
}

void handleUpdateDone() {
  g_server.sendHeader("Connection", "close");
  if (!g_updateStarted) {
    if (!BaseCore::authorize()) return;
    return sendResult(false, "No firmware file received.");
  }
  g_updateStarted = false;
  if (!g_updateAuthOk) {
    g_server.requestAuthentication();
    return;
  }
  if (!g_updateError.isEmpty()) {
    g_log.printf("[ota] failed: %s\n", g_updateError.c_str());
    return sendResult(false, "Update failed: " + g_updateError);
  }
  sendResult(true, "Update successful. Restarting into the new firmware.", true);
  BaseCore::scheduleRestart(1500);
}

void handleNotFound() {
  if (captiveRedirect()) return;
  g_server.send(404, "text/plain", "Not found");
}

// ------------------------------------------------------ USB-serial control --
//
// Request:  @<id> <command> [form-encoded args]      e.g. "@7 power on=0"
// Response: @<id> <single-line JSON>
//
// <id> is 1-16 chars of [A-Za-z0-9_-] chosen by the host and echoed back so
// replies can be told apart from log output (log lines never start with '@').
// Commands mirror the HTTP API. The admin password is not required: anyone
// with the USB cable can reflash the chip anyway.

const size_t SERIAL_LINE_MAX = 512;
const int SERIAL_PROTOCOL_VERSION = 1;

// One input line buffer per port; replies go back to the port that asked.
struct SerialPort {
  Stream* stream;
  String line;
  bool overflow;
};

SerialPort g_serialPorts[] = {
    {&Serial, String(), false},
#if BASECORE_USB_SERIAL
    {&USBSerial, String(), false},
#endif
};

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

String urlDecode(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length() &&hexNibble(s[i + 1]) >= 0 && hexNibble(s[i + 2]) >= 0) {
      out += (char)((hexNibble(s[i + 1]) << 4) | hexNibble(s[i + 2]));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

class SerialArgs : public ArgSource {
 public:
  explicit SerialArgs(const String& query) {
    int start = 0;
    while (start < (int)query.length() && count_ < MAX_ARGS) {
      int amp = query.indexOf('&', start);
      if (amp < 0) amp = query.length();
      String pair = query.substring(start, amp);
      start = amp + 1;
      if (pair.isEmpty()) continue;
      int eq = pair.indexOf('=');
      names_[count_] = urlDecode(eq < 0 ? pair : pair.substring(0, eq));
      values_[count_] = eq < 0 ? String() : urlDecode(pair.substring(eq + 1));
      count_++;
    }
  }
  bool has(const char* name) const override { return find(name) >= 0; }
  String get(const char* name) const override {
    int i = find(name);
    return i < 0 ? String() : values_[i];
  }

 private:
  static const int MAX_ARGS = 8;
  String names_[MAX_ARGS];
  String values_[MAX_ARGS];
  int count_ = 0;
  int find(const char* name) const {
    for (int i = 0; i < count_; i++) {
      if (names_[i] == name) return i;
    }
    return -1;
  }
};

String buildHelloJson() {
  return String("{\"ok\":true,\"protocol\":") + SERIAL_PROTOCOL_VERSION +
         ",\"title\":" + jsonStr(g_cfg.title) +
         ",\"version\":" + jsonStr(g_cfg.fwVersion) +
         ",\"hostname\":" + jsonStr(g_host) +
         ",\"mac\":" + jsonStr(WiFi.macAddress()) + "}";
}

String runSerialCommand(const String& cmd, const SerialArgs& args) {
  if (cmd == "hello") return buildHelloJson();
  if (cmd == "status") return buildStatusJson();
  if (cmd == "gpio") return buildGpioJson();
  if (cmd == "i2c") return buildI2cJson();
  if (cmd == "scan") return buildScanJson(args.has("fresh"));
  if (cmd == "gpio-mode") return resultJson(actionGpioMode(args));
  if (cmd == "power") return resultJson(actionPowerSwitch(args));
  if (cmd == "wifi") return resultJson(actionWifiSave(args));
  if (cmd == "device") return resultJson(actionDeviceSave(args));
  if (cmd == "forget") return resultJson(actionForget(args));
  if (cmd == "reboot") return resultJson(actionReboot(args));
  return resultJson(errResult("Unknown command '" + cmd + "'."));
}

#if BASECORE_USB_SERIAL
// Push queued USB data all the way out now. The core's HWCDC driver can
// leave the end of a reply unsent until something else is written:
//  - it can disable its TX-empty interrupt while data is still queued in its
//    ring buffer, so we keep re-arming the interrupt until the queue drains;
//  - it marks each chunk ready with a single "write done" (wr_done), which the
//    hardware ignores while the previous packet is still being sent, so the
//    last chunk can sit in the 64-byte FIFO. Repeating wr_done a few times
//    afterwards sends it (or, if the FIFO is already empty, a harmless
//    zero-length packet).
// Also, HWCDC::flush() discards the queue whenever the driver's "host
// connected" guess is false, so it is not used here. Gives up if the host
// stops reading.
void usbSerialDrain(uint32_t stallMs = 200) {
  int free = USBSerial.availableForWrite();
  uint32_t progressAt = millis();
  while (free < (int)USB_TX_BUFFER_SIZE) {
    usb_serial_jtag_ll_txfifo_flush();
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    delay(1);
    int now = USBSerial.availableForWrite();
    if (now != free) {
      free = now;
      progressAt = millis();
    } else if (elapsed(progressAt, stallMs)) {
      return;
    }
  }
  for (int i = 0; i < 5; i++) {
    delay(1);
    usb_serial_jtag_ll_txfifo_flush();
  }
}
#endif

bool validSerialId(const String& id) {
  if (id.isEmpty() || id.length() > 16) return false;
  for (size_t i = 0; i < id.length(); i++) {
    char c = id[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

void handleSerialLine(Stream& out, String line, bool overflow) {
  line.trim();
  if (line.length() < 2 || line[0] != '@') return;  // not addressed to us

  int sp = line.indexOf(' ');
  String id = line.substring(1, sp < 0 ? line.length() : sp);
  if (!validSerialId(id)) return;

  String reply;
  if (overflow) {
    reply = resultJson(errResult("Command line too long."));
  } else {
    String rest = sp < 0 ? String() : line.substring(sp + 1);
    rest.trim();
    int sp2 = rest.indexOf(' ');
    String cmd = sp2 < 0 ? rest : rest.substring(0, sp2);
    String query = sp2 < 0 ? String() : rest.substring(sp2 + 1);
    query.trim();
    reply = cmd.isEmpty() ? resultJson(errResult("Missing command.")) : runSerialCommand(cmd, SerialArgs(query));
  }
#if BASECORE_USB_SERIAL
  if (&out == &USBSerial) {
    out.print("@" + id + " " + reply + "\n");
    usbSerialDrain();
    return;
  }
#endif
  out.print("@" + id + " " + reply + "\n");
}

void serialTick() {
  for (SerialPort& port : g_serialPorts) {
    int budget = 256;  // bound the time spent here per loop()
    while (budget-- > 0 && port.stream->available() > 0) {
      char c = (char)port.stream->read();
      if (c == '\r') continue;
      if (c == '\n') {
        handleSerialLine(*port.stream, port.line, port.overflow);
        port.line = "";
        port.overflow = false;
      } else if (port.line.length() < SERIAL_LINE_MAX) {
        port.line += c;
      } else {
        port.overflow = true;  // keep the head so the id can still be echoed
      }
    }
  }
}

void registerRoutes() {
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/config", HTTP_GET, handleConfigPage);
  g_server.on("/gpio", HTTP_GET, handleGpioPage);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/scan", HTTP_GET, handleScan);
  g_server.on("/api/i2c", HTTP_GET, handleI2cScan);
  g_server.on("/api/gpio", HTTP_GET, handleGpioStatus);
  g_server.on("/api/gpio-mode", HTTP_POST, handleGpioMode);
  g_server.on("/api/wifi", HTTP_POST, handleWifiSave);
  g_server.on("/api/device", HTTP_POST, handleDeviceSave);
  g_server.on("/api/power", HTTP_POST, handlePowerSwitch);
  g_server.on("/api/forget", HTTP_POST, handleForget);
  g_server.on("/api/reboot", HTTP_POST, handleReboot);
  g_server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  g_server.onNotFound(handleNotFound);
}

}  // namespace

// ------------------------------------------------------------- public API --

namespace BaseCore {

void begin(const BaseConfig& cfg) {
  g_cfg = cfg;
#if BASECORE_USB_SERIAL
  // Big enough for a full status reply. Writes drop rather than block while
  // no host has the port open.
  USBSerial.setRxBufferSize(1024);
  USBSerial.setTxBufferSize(USB_TX_BUFFER_SIZE);
  USBSerial.begin();
#endif
  for (SerialPort& port : g_serialPorts) port.line.reserve(SERIAL_LINE_MAX);
  initMcp23017States();
  if (g_cfg.resetButtonPin >= 0) pinMode(g_cfg.resetButtonPin, INPUT_PULLUP);
  if (wifiDisableConfigured()) {
    pinMode(g_cfg.wifiDisablePin, INPUT_PULLUP);
    initWifiDisableInput();
  }
  for (size_t i = 0; i < 2; i++) {
    if (mcp23017InterruptConfigured(g_mcp23017[i])) pinMode(g_mcp23017[i].interruptPin, INPUT_PULLUP);
  }

  // A freshly uploaded image boots once "pending verify" when rollback is
  // enabled; we got this far, so mark it good.
  esp_ota_mark_app_valid_cancel_rollback();

  loadSettings();
  loadMcp23017Mode();
  g_log.printf("\n%s v%s (%s)\n", g_cfg.title, g_cfg.fwVersion, g_host.c_str());
  initI2c();
  if (mcp23017InterruptConfigured(g_mcp23017[0])) {
    attachInterrupt(digitalPinToInterrupt(g_mcp23017[0].interruptPin), onMcp23017Interrupt0, FALLING);
  }
  if (mcp23017InterruptConfigured(g_mcp23017[1])) {
    attachInterrupt(digitalPinToInterrupt(g_mcp23017[1].interruptPin), onMcp23017Interrupt1, FALLING);
  }
  initPowerSwitch();

  WiFi.persistent(false);  // credentials live in our own Preferences namespace
  wifiDisableTick();
  if (!g_wifiDisabledByPin && !connectStation()) startPortal();

  registerRoutes();
  g_server.begin();
}

void loop() {
  wifiDisableTick();
  if (!g_wifiDisabledByPin) {
    if (g_portal) {
      g_dns.processNextRequest();
      portalRetryTick();
    } else {
      stationWatchTick();
    }
  }
  g_server.handleClient();
  serialTick();
  mcp23017Tick();
  resetButtonTick();

  if (g_restartPending && (int32_t)(millis() - g_restartAt) >= 0) {
    g_log.println("[sys] restarting");
    delay(100);
    ESP.restart();
  }
}

WebServer& server() { return g_server; }

bool authorize() {
  if (g_admin.isEmpty() || g_server.authenticate(ADMIN_USER, g_admin.c_str())) return true;
  g_server.requestAuthentication(BASIC_AUTH, g_cfg.title);
  return false;
}

bool isPortalMode() { return g_portal; }
bool isConnected() { return WiFi.status() == WL_CONNECTED; }
String hostname() { return g_host; }

void scheduleRestart(uint32_t delayMs) {
  g_restartPending = true;
  g_restartAt = millis() + delayMs;
}

}  // namespace BaseCore
