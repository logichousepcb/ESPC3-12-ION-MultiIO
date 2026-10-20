"""USB-serial client for the ION Multi-IO firmware.

Wire protocol (115200 8N1, one line each way):

    host   -> device:  @<id> <command> [form-encoded args]\\n
    device -> host:    @<id> <single-line JSON>\\n

Everything else the device prints is log output. Log lines never start
with '@', so replies can be picked out of the stream by their id.
"""

import json
import os
import time
import urllib.parse

import serial
from serial.tools import list_ports

BAUD = 115200
PROTOCOL_VERSION = 1

# USB IDs the board can show up as: common USB-UART bridges, plus the
# ESP32-C3's own USB-Serial/JTAG. Only these are probed by auto-detection.
KNOWN_USB_IDS = {
    (0x10C4, 0xEA60),  # Silicon Labs CP210x
    (0x1A86, 0x7523),  # WCH CH340
    (0x1A86, 0x55D4),  # WCH CH9102
    (0x0403, 0x6001),  # FTDI FT232R
    (0x0403, 0x6015),  # FTDI FT231X
    (0x303A, 0x1001),  # Espressif USB-Serial/JTAG
}


class DeviceError(Exception):
    """The device could not be reached or gave an unusable reply."""


class CommandFailed(DeviceError):
    """The device understood the command but rejected it ({"ok": false})."""

    def __init__(self, message, reply=None):
        super().__init__(message)
        self.reply = reply or {}


class Device:
    """One open connection to a board.

    Opening the port leaves DTR and RTS both asserted (the pyserial default).
    With the usual ESP32 auto-reset wiring that combination neither resets
    the chip nor selects download mode, so the firmware keeps running.
    """

    def __init__(self, port, baud=BAUD, timeout=5.0, on_log=None, _serial=None):
        self.port = port
        self.timeout = timeout
        self.on_log = on_log
        self._buf = b""
        self._seq = 0
        self._tag = "%x" % (os.getpid() & 0xFFF)
        if _serial is not None:
            self._ser = _serial
        else:
            try:
                self._ser = serial.Serial(port, baud, timeout=0.05, exclusive=True)
            except (serial.SerialException, OSError) as e:
                hint = ""
                if "ermission denied" in str(e):
                    hint = " (add your user to the 'dialout' group, then log in again)"
                elif "lock" in str(e).lower():
                    hint = " (another program has the port open)"
                raise DeviceError("cannot open %s: %s%s" % (port, e, hint)) from e
        # Terminate any half-typed line sitting in the device's input buffer.
        self._ser.write(b"\n")

    # -- context manager ----------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._ser is not None:
            self._ser.close()
            self._ser = None

    # -- low level ----------------------------------------------------------

    def _next_id(self):
        self._seq += 1
        return "%s-%d" % (self._tag, self._seq)

    def _read_line(self, deadline):
        """Next complete line (without newline), or None at the deadline."""
        while True:
            nl = self._buf.find(b"\n")
            if nl >= 0:
                line, self._buf = self._buf[:nl], self._buf[nl + 1:]
                return line.rstrip(b"\r").decode("utf-8", "replace")
            if time.monotonic() >= deadline:
                return None
            try:
                chunk = self._ser.read(max(1, self._ser.in_waiting))
            except (serial.SerialException, OSError) as e:
                raise DeviceError("read from %s failed: %s" % (self.port, e)) from e
            self._buf += chunk

    def _log(self, line):
        if self.on_log and line:
            self.on_log(line)

    def request(self, command, timeout=None, **args):
        """Send one command and return its decoded JSON reply.

        Raises CommandFailed if the reply has "ok": false.
        """
        rid = self._next_id()
        args = {k: v for k, v in args.items() if v is not None}
        query = urllib.parse.urlencode(args)
        line = "@%s %s%s\n" % (rid, command, " " + query if query else "")
        if len(line) > 500:
            raise DeviceError("command too long for the device (max ~500 bytes)")
        try:
            self._ser.write(line.encode("utf-8"))
        except (serial.SerialException, OSError) as e:
            raise DeviceError("write to %s failed: %s" % (self.port, e)) from e

        prefix = "@%s " % rid
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            text = self._read_line(deadline)
            if text is None:
                raise DeviceError("no reply from %s to '%s' (is the firmware running and >= 1.10.0?)"
                                  % (self.port, command))
            if text.startswith(prefix):
                try:
                    reply = json.loads(text[len(prefix):])
                except ValueError as e:
                    raise DeviceError("garbled reply to '%s': %s" % (command, text)) from e
                if isinstance(reply, dict) and reply.get("ok") is False:
                    raise CommandFailed(reply.get("message") or "command failed", reply)
                return reply
            if not text.startswith("@"):
                self._log(text)

    def read_log(self, timeout=0.5):
        """Return the next log line, or None if nothing arrived in time."""
        deadline = time.monotonic() + timeout
        while True:
            text = self._read_line(deadline)
            if text is None or not text.startswith("@"):
                return text

    def wait_ready(self, total=25.0, attempt=1.5):
        """Retry 'hello' until the device answers.

        The firmware does not read serial input while it is connecting to
        WiFi at boot (up to ~20 s), so a single short attempt is not enough
        right after a reset.
        """
        deadline = time.monotonic() + total
        while True:
            try:
                info = self.request("hello", timeout=min(attempt, max(0.1, deadline - time.monotonic())))
            except CommandFailed:
                raise
            except DeviceError:
                if time.monotonic() >= deadline:
                    raise
                continue
            if info.get("protocol") != PROTOCOL_VERSION:
                raise DeviceError("device speaks protocol %r, this tool speaks %d"
                                  % (info.get("protocol"), PROTOCOL_VERSION))
            return info

    # -- commands -----------------------------------------------------------

    def hello(self):
        return self.request("hello")

    def status(self):
        return self.request("status")

    def gpio(self):
        return self.request("gpio")

    def i2c(self):
        return self.request("i2c")

    def set_power(self, on):
        return self.request("power", on="1" if on else "0")

    def set_gpio_interrupts(self, use_interrupts):
        return self.request("gpio-mode", useInterrupts="1" if use_interrupts else "0")

    def set_wifi(self, ssid, password=""):
        return self.request("wifi", ssid=ssid, **{"pass": password})

    def set_device(self, hostname=None, admin_password=None, clear_admin=False):
        return self.request("device", hostname=hostname, admin=admin_password,
                            adminClear="1" if clear_admin else None)

    def forget_wifi(self):
        return self.request("forget")

    def reboot(self):
        return self.request("reboot")

    def scan_wifi(self, total=20.0):
        """Run a fresh WiFi scan and return the list of networks."""
        deadline = time.monotonic() + total
        reply = self.request("scan", fresh="1")
        while reply.get("scanning"):
            if time.monotonic() >= deadline:
                raise DeviceError("WiFi scan did not finish in %.0f s" % total)
            time.sleep(0.5)
            reply = self.request("scan")
        return reply.get("networks", [])


def candidate_ports():
    """Serial ports whose USB IDs match a known bridge, as (device, info)."""
    found = []
    for p in sorted(list_ports.comports(), key=lambda p: p.device):
        if (p.vid, p.pid) in KNOWN_USB_IDS:
            found.append((p.device, p))
    return found


def stable_path(device):
    """/dev/serial/by-id/... link for a tty, if udev made one."""
    by_id = "/dev/serial/by-id"
    try:
        for name in sorted(os.listdir(by_id)):
            path = os.path.join(by_id, name)
            if os.path.realpath(path) == os.path.realpath(device):
                return path
    except OSError:
        pass
    return None


def find_devices(probe_timeout=2.0):
    """Probe known USB-serial ports; return [(port, hello_reply)] that answer."""
    result = []
    for device, _info in candidate_ports():
        try:
            with Device(device, timeout=probe_timeout) as dev:
                result.append((device, dev.wait_ready(total=probe_timeout, attempt=probe_timeout)))
        except DeviceError:
            continue
    return result
