# ionctl: USB control for ION Multi-IO boards

`ionctl` is a command-line tool for Debian (and other Linux systems) that controls an ION Multi-IO
board through its USB-serial port. It needs no WiFi or network access. It works even while WiFi is
turned off by the GPIO3 disable input.

It requires firmware **1.10.0 or newer**. Older firmware only writes logs to USB and doesn't read
commands from it.

## Install

On any Debian or Ubuntu server, this one command installs the latest release:

```
curl -fsSL https://raw.githubusercontent.com/logichousepcb/ESPC3-12-ION-MultiIO/main/install.sh | sh
```

It does three things:

- installs the package and its dependency, `python3-serial`
- adds you to the `dialout` group so you can use the USB port; log out and back in afterwards
- reinstalls cleanly, so running it again is also how you update

To install from a clone instead, which builds the package from the checked-out source:

```
git clone https://github.com/logichousepcb/ESPC3-12-ION-MultiIO.git
cd ESPC3-12-ION-MultiIO && ./install.sh        # update later: git pull && ./install.sh
```

You can also download the `.deb` from the
[releases page](https://github.com/logichousepcb/ESPC3-12-ION-MultiIO/releases) and install it with
`sudo apt install ./ion-multiio-tools_<version>_all.deb`. `python3 build_deb.py` builds the same
package on any machine with Python 3.

`esptool` is a recommended dependency. Only `ionctl flash` uses it.

### Releasing a new version

1. Bump `__version__` in `ion_multiio/__init__.py`.
2. Commit, then push a matching tag:

   ```
   git tag ionctl-v1.0.1 && git push origin ionctl-v1.0.1
   ```

A GitHub Actions workflow builds the `.deb` and publishes the release. The one-line installer then
picks it up.

To run without installing, use `python3 -m ion_multiio ...` from this folder. It needs `python3-serial`.

## Usage

```
ionctl list                       # find boards on USB
ionctl status                     # summary (add -j for full JSON)
ionctl power                      # show PoE power state
ionctl power on|off|toggle
ionctl power cycle --delay 10     # off, wait 10 s, on
ionctl meter                      # PoE voltage / current / power
ionctl meter -w 1 -j              # one JSON line per second, for logging
ionctl gpio                       # all 32 expander pins
ionctl gpio -w 0.2                # then print each pin change as it happens
ionctl gpio --mode polling|interrupt
ionctl i2c                        # scan the I2C bus
ionctl wifi scan
ionctl wifi set "MyNetwork"       # prompts for the password; device reboots
ionctl wifi set "MyNetwork" --password-stdin < pass.txt
ionctl wifi forget
ionctl hostname [new-name]
ionctl admin-password set|clear   # web UI admin password
ionctl reboot [--wait-boot]
ionctl log -T                     # stream the device's log output
ionctl raw status                 # any protocol command, raw JSON reply
ionctl flash firmware-merged.bin  # reflash via esptool, then confirm it boots
```

### Choosing the port

`ionctl` auto-detects the board when exactly one is connected. It only probes ports with known
ESP32 USB IDs: CP210x, CH340/CH9102, FTDI, and the ESP32-C3's native USB. To pin a port, use
`--port`, or set `IONCTL_PORT`. The `/dev/serial/by-id/...` path (`ionctl list` shows it) survives
re-plugging and reboots:

```
export IONCTL_PORT=/dev/serial/by-id/usb-Silicon_Labs_CP2102_..._0001-if00-port0
```

### Scripting

- `-j` gives JSON on stdout.
- Errors go to stderr.
- Exit codes: `0` ok, `1` command rejected by the device, `2` usage error, `3` device not found or
  not answering.
- Only one `ionctl` can use a port at a time. A second one fails with "another program has the port
  open" instead of interleaving.

The `ion_multiio` Python module can also be used directly:

```python
from ion_multiio import Device
with Device("/dev/ttyUSB0") as dev:
    dev.wait_ready()
    dev.set_power(False)
    print(dev.status()["ina226BusVoltage"])
```

## Notes

- **No admin password over USB.** USB commands don't ask for the web admin password. Anyone with
  physical access to the USB port can reflash the chip anyway.
- **Commands at boot.** The firmware doesn't read commands while it is joining WiFi at boot (up to
  about 20 s). `ionctl` keeps retrying for `--wait` seconds (default 25).
- **Opening the port.** `ionctl` opens the port with DTR and RTS both asserted. With the standard
  ESP32 auto-reset circuit, that combination doesn't reset the chip. If a board resets every time a
  command runs, its reset wiring differs from the standard circuit.

## Protocol

The link runs at 115200 baud, 8N1, one line per message:

```
host   -> device:  @<id> <command> [form-encoded args]
device -> host:    @<id> <single-line JSON>
```

`<id>` is 1-16 characters from `[A-Za-z0-9_-]` and is echoed back. All other lines are log output,
and no log line starts with `@`.

| Command | Args | Reply |
| --- | --- | --- |
| `hello` | | `protocol`, `title`, `version`, `hostname`, `mac` |
| `status` | | same as HTTP `/api/status` |
| `gpio` | | same as `/api/gpio` |
| `i2c` | | same as `/api/i2c` |
| `scan` | `fresh=1` | same as `/api/scan` |
| `power` | `on=0\|1` | `{ok, message, restart}` |
| `gpio-mode` | `useInterrupts=0\|1` | `{ok, message, restart}` |
| `wifi` | `ssid`, `pass` | `{ok, message, restart}` (reboots) |
| `device` | `hostname`, `admin`, `adminClear=1` | `{ok, message, restart}` |
| `forget` | | `{ok, message, restart}` (reboots) |
| `reboot` | | `{ok, message, restart}` |

Example session, for instance from `picocom -b 115200 /dev/ttyUSB0`:

```
@1 power on=0
@1 {"ok":true,"message":"PoE power turned off.","restart":false}
```
