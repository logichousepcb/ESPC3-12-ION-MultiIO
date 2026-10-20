# ESPC3-12 Base Firmware

Base firmware for ESP-C3-12F (ESP32-C3, 4 MB flash) modules. Flash it once over serial. After that, every
chip can be put on WiFi from a phone and then loaded with application firmware through its web page.
No serial cable is needed after the first flash.

The setup AP, captive portal, credential storage and firmware upload are written from scratch. The only
libraries used are the ones built into Arduino-ESP32 (`WebServer`, `DNSServer`, `Preferences`, `Update`,
`ESPmDNS`).

## Behaviour

1. **Boot.** The firmware loads the saved SSID and password from NVS and tries to connect for 20 s.
2. **Connected (station mode).** The power dashboard is served at `http://<ip>/` or `http://<hostname>.local/`,
   the GPIO page is at `/gpio`, and the configuration page is at `/config`. The default hostname is
   `espc3-xxxx`, where `xxxx` is the last 4 hex digits of the MAC address.
3. **No credentials, or the connection fails (portal mode).** The firmware opens the AP `ESPC3-12-XXXX`
   (open network) at `192.168.4.1`, with a captive-portal DNS server. Phones and laptops pop up the setup
   page automatically. If credentials are saved, it retries that network every 5 minutes, but only while
   no one is connected to the AP. When the retry succeeds, the device reboots into station mode.
4. **Link lost in station mode.** The firmware auto-reconnects. If the link is still down after 5 minutes,
   it also brings up the setup AP.
5. **Factory reset of WiFi.** Hold the **BOOT button (GPIO9) for 5 s** while the device is running. Don't
   hold it at power-up, because that puts the chip into serial download mode. The web page also has a
   **Forget WiFi** button.
6. **Hardware WiFi disable.** Pull **GPIO3 low** to force WiFi off. The input uses the internal pull-up
   and a **50 ms debounce**. Releasing the pin lets the firmware automatically reconnect to the saved
   network or fall back to the setup AP.

## Web pages

- **Power page (`/`):**
  - **Power Control:** this device exposes a **PoE power** switch on **GPIO10** so PoE can be turned on
    or off from the main page. The chosen state is saved and restored after reboot.
  - **Power Monitor:** this board also exposes an **INA226** at **I2C address 0x40**. The firmware reads
    its bus voltage and shunt voltage, then calculates current and power using the board's **R100 (100 mOhm)**
    shunt resistor.
- **GPIO Control page (`/gpio`):**
  - shows the live status of both **MCP23017** expanders at **0x22** and **0x24**
  - includes a toggle to switch between **interrupt mode** and **GPIO polling mode**
  - uses the expander interrupt lines on **GPIO7** (`0x22`) and **GPIO6** (`0x24`) so the firmware can
    refresh cached pin states when inputs change
  - configures **MIRROR** so each chip's **INTA** pin reports interrupts from both port A and port B
  - reads the triggered expander from **INTCAP** when an interrupt fires, instead of polling both chips
  - enables the **internal pull-ups on all MCP23017 pins**
  - shows each expander's interrupt status directly on the page
  - lists all **32 GPIO pins** with each pin's bank/pin name, direction, and current **OPEN** (blue) or
    **LOW** (green) state
- **Configuration page (`/config`):**
  - **Status:** mode, network, IP, signal, hostname, MAC, uptime, firmware version and build date, running
    OTA slot, PoE power state, WiFi disable input state, live PoE voltage/current/load from the INA226,
    I2C bus pins and detected device count, app size and slot size, chip, free heap.
  - **WiFi Setup:** scan for networks (tap one to fill in the SSID), enter the password, then **Save &
    Connect**. The device stores the credentials and reboots. If it can't join the network, the setup AP
    comes back.
  - **I2C Devices:** scans the configured bus and lists any responding device addresses. This firmware is
    configured for **SDA GPIO4 / SCL GPIO5 @ 400 kHz** and also prints the scan result to the serial log
    at boot.
  - **Firmware Update:** choose a `.bin` and upload it, with a progress bar. The firmware checks the image
    magic byte and chip ID (ESP32-C3) before installing. It writes to the inactive OTA slot and reboots.
  - **Device Settings:** hostname and an optional admin password (HTTP Basic auth, user `admin`). With no
    password set, anyone on the network or AP can change settings and upload firmware. Set a password on
    deployed units.
  - **Maintenance:** Reboot, Forget WiFi.

## Build and first flash

```
pio run                 # build
pio run -t upload       # flash over USB-serial (GPIO20 RX / GPIO21 TX)
pio device monitor      # 115200 baud
```

Each build also writes `.pio/build/esp32c3/firmware-merged.bin`. This single image contains the
bootloader, partition table and app. Flash it to a blank chip at offset `0x0` with any tool, for example:

```
esptool.py --chip esp32c3 write_flash 0x0 firmware-merged.bin
```

To put a module into download mode, hold GPIO9 low while resetting it.

## Building application firmware

Upload `.pio/build/<env>/firmware.bin` (not the merged image) through the **Firmware Update** section.

> **Important:** the uploaded firmware *replaces* this one. If the application doesn't include its own WiFi
> setup and OTA upload, the only way to update it again is over serial.

The easiest approach is to start each application project as a copy of this one and keep `lib/BaseCore`:

```cpp
#include <BaseCore.h>

void setup() {
  BaseConfig cfg;
  cfg.title = "Pool Pump Controller";
  cfg.fwVersion = "1.0.0";
  BaseCore::begin(cfg);                        // WiFi, portal, settings, OTA page at "/"

  BaseCore::server().on("/api/pump", HTTP_GET, [] {
    if (!BaseCore::authorize()) return;        // honours the admin password
    BaseCore::server().send(200, "application/json", "{\"on\":true}");
  });
}

void loop() {
  BaseCore::loop();                            // must be called often; don't block
  // application code...
}
```

Because WiFi credentials, hostname and admin password are stored in NVS (namespace `basecore`), they
carry over from the base firmware to the application firmware. Application firmware must:

- target ESP32-C3,
- use the same partition table (`board_build.partitions = min_spiffs.csv`, two 1.9 MB app slots); OTA
  cannot change the partition table,
- fit in 1.9 MB.

## HTTP API

| Method | Path | Body (form-encoded) | Notes |
| --- | --- | --- | --- |
| GET | `/` | | Power page |
| GET | `/gpio` | | GPIO Control page |
| GET | `/config` | | Configuration page |
| GET | `/api/status` | | JSON status, including INA226 power-monitor readings when configured |
| GET | `/api/gpio` | | JSON GPIO status for configured MCP23017 expanders |
| POST | `/api/gpio-mode` | `useInterrupts=1` or `useInterrupts=0` | Switch MCP23017 update mode between interrupts and polling |
| GET | `/api/scan[?fresh=1]` | | `{"scanning":true}` while running, then `{"networks":[{ssid,rssi,secure}]}` |
| GET | `/api/i2c` | | I2C scan result with configured pins and responding addresses |
| POST | `/api/power` | `on=1` or `on=0` | Toggle the configured main-page power switch |
| POST | `/api/wifi` | `ssid`, `pass` | Save credentials and reboot |
| POST | `/api/device` | `hostname`, `admin`, `adminClear=1` | Hostname change reboots |
| POST | `/api/forget` | | Erase WiFi credentials and reboot |
| POST | `/api/reboot` | | |
| POST | `/update` | multipart, file field | Firmware upload; reboots on success |

When an admin password is set, every endpoint requires Basic auth (user `admin`).

## USB control

From firmware 1.10.0 the same functions are also available over the USB-serial port (115200 baud),
without WiFi. Send `@<id> <command> [form-encoded args]` and the device replies `@<id> <json>`. The
commands are `hello`, `status`, `gpio`, `i2c`, `scan`, `power`, `gpio-mode`, `wifi`, `device`,
`forget` and `reboot`. USB commands don't ask for the admin password.

The `host/` folder contains `ionctl`, a Debian command-line tool for this protocol. Run
`curl -fsSL https://raw.githubusercontent.com/logichousepcb/ESPC3-12-ION-MultiIO/main/install.sh | sh` to install it on a Debian server. See [host/README.md](host/README.md).

## Configuration

The defaults are fields of `BaseConfig` in `lib/BaseCore/src/BaseCore.h`: AP name prefix, optional AP
password, hostname prefix, timeouts, optional I2C pins/clock, optional MCP23017 addresses/labels/interrupt
pins, optional INA226 address/shunt/label, optional WiFi-disable pin/polarity, optional power-switch
pin/label/polarity, reset pin and hold time. Override them in `setup()` before calling `BaseCore::begin()`.
