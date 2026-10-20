#pragma once

#include <Arduino.h>
#include <WebServer.h>

struct BaseConfig {
  const char* title = "ESPC3-12 Base Firmware";  // web page title / heading
  const char* fwVersion = "1.0.0";
  const char* apSsidPrefix = "ESPC3-12-";        // + last 4 hex of MAC
  const char* apPassword = "";                   // "" = open AP, else 8-63 chars
  const char* hostnamePrefix = "espc3-";         // default hostname + MAC suffix
  uint32_t staConnectTimeoutMs = 20000;          // per connection attempt
  uint32_t staRetryIntervalMs = 300000;          // portal-mode retry / lost-link fallback
  int i2cSdaPin = -1;                            // -1 disables I2C scanning/reporting
  int i2cSclPin = -1;
  uint32_t i2cClockHz = 100000;
  int mcp23017Address1 = -1;                    // -1 disables the first MCP23017 monitor
  int mcp23017Address2 = -1;                    // -1 disables the second MCP23017 monitor
  int mcp23017InterruptPin1 = -1;               // optional MCU interrupt input for the first MCP23017
  int mcp23017InterruptPin2 = -1;               // optional MCU interrupt input for the second MCP23017
  const char* mcp23017Label1 = "MCP23017 #1";   // label shown on the GPIO page
  const char* mcp23017Label2 = "MCP23017 #2";
  int ina226Address = -1;                       // -1 disables INA226 power monitoring
  float ina226ShuntMilliOhms = 0.0f;            // shunt resistor value for current/power calculation
  const char* ina226Label = "Power monitor";    // label shown on the main page
  int wifiDisablePin = -1;                      // optional input that disables WiFi while asserted
  bool wifiDisableActiveLow = true;             // true: LOW disables WiFi, false: HIGH disables WiFi
  uint32_t wifiDisableDebounceMs = 50;          // debounce time for the WiFi disable input
  int powerSwitchPin = -1;                      // -1 disables the main-page power switch
  bool powerSwitchActiveHigh = true;            // true: HIGH = on, false: LOW = on
  bool powerSwitchDefaultOn = false;            // initial state when no value is stored yet
  const char* powerSwitchLabel = "Power";       // label shown on the main page
  int resetButtonPin = 9;                        // BOOT button, active low; -1 disables
  uint32_t resetHoldMs = 5000;                   // hold to erase WiFi credentials
};

namespace BaseCore {

void begin(const BaseConfig& cfg = BaseConfig());
void loop();

// The shared web server (port 80). Application code may register extra routes.
WebServer& server();

// Call at the top of a route handler; returns false (and has already sent a
// 401 challenge) when an admin password is set and the request lacks it.
bool authorize();

bool isPortalMode();
bool isConnected();
String hostname();
void scheduleRestart(uint32_t delayMs = 1000);

}  // namespace BaseCore
