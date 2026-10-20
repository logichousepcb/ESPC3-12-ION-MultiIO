// ESPC3-12 Base Firmware
//
// Brings up WiFi (saved credentials, or a setup access point + captive portal
// when none are saved / the network can't be reached) and a web page for
// WiFi setup, device settings and firmware upload. All of that lives in
// lib/BaseCore so application firmware can reuse it unchanged.

#include <Arduino.h>
#include <BaseCore.h>

void setup() {
  Serial.begin(115200);
  delay(200);

  BaseConfig cfg;
  cfg.title = "ION Multi-IO add-on";
  cfg.fwVersion = "1.11.2";
  cfg.i2cSdaPin = 4;
  cfg.i2cSclPin = 5;
  cfg.i2cClockHz = 400000;
  cfg.mcp23017Address1 = 0x22;
  cfg.mcp23017Address2 = 0x24;
  cfg.mcp23017InterruptPin1 = 7;
  cfg.mcp23017InterruptPin2 = 6;
  cfg.mcp23017Label1 = "GPIO expander A";
  cfg.mcp23017Label2 = "GPIO expander B";
  cfg.ina226Address = 0x40;
  cfg.ina226ShuntMilliOhms = 100.0f;
  cfg.ina226Label = "PoE monitor";
  cfg.wifiDisablePin = 3;
  cfg.wifiDisableActiveLow = true;
  cfg.wifiDisableDebounceMs = 50;
  cfg.powerSwitchPin = 10;
  cfg.powerSwitchActiveHigh = true;
  cfg.powerSwitchDefaultOn = true;
  cfg.powerSwitchLabel = "PoE power";
  BaseCore::begin(cfg);

  // Application firmware: add your own routes here, e.g.
  // BaseCore::server().on("/api/hello", HTTP_GET, [] {
  //   if (!BaseCore::authorize()) return;
  //   BaseCore::server().send(200, "text/plain", "hello");
  // });
}

void loop() {
  BaseCore::loop();
}
