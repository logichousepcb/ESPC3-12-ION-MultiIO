"""ionctl: control an ION Multi-IO board over USB."""

import argparse
import getpass
import json
import os
import shutil
import subprocess
import sys
import time

from . import __version__
from .device import (CommandFailed, Device, DeviceError, candidate_ports, find_devices,
                     stable_path)

EXIT_OK, EXIT_FAILED, EXIT_USAGE, EXIT_NO_DEVICE = 0, 1, 2, 3


# ------------------------------------------------------------------ helpers --

def out_json(obj):
    print(json.dumps(obj, indent=2, sort_keys=True))


def fmt_uptime(seconds):
    d, rem = divmod(int(seconds), 86400)
    h, rem = divmod(rem, 3600)
    m, s = divmod(rem, 60)
    return ("%dd " % d if d else "") + "%02d:%02d:%02d" % (h, m, s)


def on_off(value):
    return "ON" if value else "OFF"


def resolve_port(args):
    if args.port:
        return args.port
    env = os.environ.get("IONCTL_PORT")
    if env:
        return env
    found = find_devices()
    if not found:
        raise DeviceError("no ION Multi-IO board found on USB; use --port /dev/ttyUSB0 "
                          "(or set IONCTL_PORT)")
    if len(found) > 1:
        ports = ", ".join(p for p, _ in found)
        raise DeviceError("several boards found (%s); pick one with --port" % ports)
    return found[0][0]


def open_device(args):
    on_log = (lambda line: print("log: " + line, file=sys.stderr)) if args.verbose else None
    dev = Device(resolve_port(args), timeout=args.timeout, on_log=on_log)
    try:
        dev.wait_ready(total=args.wait)
    except DeviceError:
        dev.close()
        raise
    return dev


def print_result(args, reply):
    if args.json:
        out_json(reply)
    else:
        print(reply.get("message", "OK"))


def read_secret(prompt, from_stdin):
    if from_stdin or not sys.stdin.isatty():
        return sys.stdin.readline().rstrip("\r\n")
    return getpass.getpass(prompt)


# ----------------------------------------------------------------- commands --

def cmd_list(args):
    rows = find_devices()
    if args.json:
        out_json([{"port": p, "byId": stable_path(p), **info} for p, info in rows])
        return EXIT_OK
    if not rows:
        cands = candidate_ports()
        print("No responding boards." + (" Ports checked: " + ", ".join(p for p, _ in cands)
                                          if cands else " No USB-serial adapters found."))
        return EXIT_NO_DEVICE
    for port, info in rows:
        print("%-14s %-16s v%-8s %s  %s" % (port, info.get("hostname", "?"), info.get("version", "?"),
                                            info.get("mac", "?"), info.get("title", "")))
        link = stable_path(port)
        if link:
            print("%-14s %s" % ("", link))
    return EXIT_OK


def cmd_status(args):
    with open_device(args) as dev:
        s = dev.status()
    if args.json:
        out_json(s)
        return EXIT_OK

    print("%s  v%s  (built %s)" % (s["title"], s["version"], s["build"]))
    print("  Hostname    %s   MAC %s" % (s["hostname"], s["mac"]))
    if s.get("wifiDisabledByPin"):
        net = "WiFi disabled by GPIO%d" % s["wifiDisablePin"]
    elif s["mode"] == "portal":
        net = 'setup AP "%s" at %s, %d client(s)' % (s["apSsid"], s["apIp"], s["apClients"])
        if s["savedSsid"]:
            net += '; retrying "%s"' % s["savedSsid"]
    elif s["connected"]:
        net = '"%s"  IP %s  %d dBm' % (s["savedSsid"], s["ip"], s["rssi"])
    else:
        net = '"%s" (link down, reconnecting)' % s["savedSsid"]
    print("  Network     " + net)
    if s.get("powerSwitchEnabled"):
        print("  %-11s %s (GPIO%d)" % (s["powerSwitchLabel"], on_off(s["powerSwitchOn"]),
                                       s["powerSwitchPin"]))
    if s.get("ina226Enabled"):
        if s["ina226Present"]:
            print("  %-11s %.3f V  %.1f mA  %.2f W" % (s["ina226Label"], s["ina226BusVoltage"],
                                                       s["ina226CurrentMa"], s["ina226PowerMw"] / 1000))
        else:
            print("  %-11s unavailable: %s" % (s["ina226Label"], s["ina226Error"]))
    if s.get("i2cEnabled"):
        print("  I2C         SDA GPIO%d / SCL GPIO%d @ %d kHz, %s" % (
            s["i2cSdaPin"], s["i2cSclPin"], s["i2cClockHz"] // 1000,
            "%d device(s)" % s["i2cDeviceCount"] if s["i2cReady"] else "bus not ready"))
    print("  Uptime      %s   heap %d B   slot %s   admin password %s" % (
        fmt_uptime(s["uptime"]), s["heap"], s["partition"], "set" if s["authEnabled"] else "not set"))
    return EXIT_OK


def cmd_power(args):
    with open_device(args) as dev:
        if args.state is None:
            s = dev.status()
            if not s.get("powerSwitchEnabled"):
                raise CommandFailed("Power switch is not configured.")
            if args.json:
                out_json({"on": s["powerSwitchOn"], "label": s["powerSwitchLabel"]})
            else:
                print(on_off(s["powerSwitchOn"]))
            return EXIT_OK
        if args.state == "toggle":
            on = not dev.status()["powerSwitchOn"]
        elif args.state == "cycle":
            dev.set_power(False)
            time.sleep(args.delay)
            on = True
        else:
            on = args.state == "on"
        print_result(args, dev.set_power(on))
    return EXIT_OK


def read_meter(dev):
    s = dev.status()
    if not s.get("ina226Enabled"):
        raise CommandFailed("No power monitor is configured on this board.")
    if not s["ina226Present"]:
        raise CommandFailed("Power monitor unavailable: " + s["ina226Error"])
    return {"voltage": s["ina226BusVoltage"], "currentMa": s["ina226CurrentMa"],
            "powerMw": s["ina226PowerMw"], "shuntMv": s["ina226ShuntVoltageMv"],
            "powerOn": s.get("powerSwitchOn")}


def cmd_meter(args):
    with open_device(args) as dev:
        while True:
            m = read_meter(dev)
            if args.json:
                m["time"] = time.time()
                print(json.dumps(m), flush=True)
            else:
                stamp = time.strftime("%H:%M:%S ") if args.watch else ""
                print("%s%.3f V  %8.1f mA  %7.2f W" % (stamp, m["voltage"], m["currentMa"],
                                                       m["powerMw"] / 1000), flush=True)
            if not args.watch:
                return EXIT_OK
            time.sleep(args.watch)


def print_gpio_table(g):
    print("Update mode: %s" % ("interrupt" if g["useInterrupts"] else "polling"))
    for chip in g["chips"]:
        head = "%s @ 0x%02X" % (chip["label"], chip["address"])
        if chip["interruptPin"] >= 0:
            head += "  INT GPIO%d %s" % (chip["interruptPin"],
                                         "active" if chip["interruptActive"] else "idle")
        print()
        print(head)
        if not chip["present"]:
            print("  unavailable: " + chip["error"])
            continue
        pins = chip["pins"]
        for row in range(8):
            cells = []
            for p in (pins[row], pins[row + 8]):
                mode = "in " if p["mode"] == "input" else "out"
                cells.append("%s %s %-4s" % (p["name"], mode, "OPEN" if p["high"] else "LOW"))
            print("  " + "    ".join(cells))


def gpio_changes(old, new):
    """Lines describing pins whose level differs between two gpio replies."""
    before = {(c["index"], p["pin"]): p["high"] for c in old["chips"] for p in c["pins"]}
    lines = []
    for chip in new["chips"]:
        for p in chip["pins"]:
            prev = before.get((chip["index"], p["pin"]))
            if prev is not None and prev != p["high"]:
                lines.append("%s %s %s -> %s" % (chip["label"], p["name"],
                                                 "OPEN" if prev else "LOW",
                                                 "OPEN" if p["high"] else "LOW"))
    return lines


def cmd_gpio(args):
    with open_device(args) as dev:
        if args.mode:
            print_result(args, dev.set_gpio_interrupts(args.mode == "interrupt"))
            return EXIT_OK
        g = dev.gpio()
        if args.json:
            print(json.dumps(g) if args.watch else json.dumps(g, indent=2), flush=True)
        else:
            print_gpio_table(g)
        if not args.watch:
            return EXIT_OK
        while True:
            time.sleep(args.watch)
            new = dev.gpio()
            if new["seq"] == g["seq"]:
                continue
            if args.json:
                print(json.dumps(new), flush=True)
            else:
                for line in gpio_changes(g, new):
                    print(time.strftime("%H:%M:%S ") + line, flush=True)
            g = new


def cmd_i2c(args):
    with open_device(args) as dev:
        r = dev.i2c()
    if args.json:
        out_json(r)
    elif not r.get("ready"):
        print(r.get("message", "I2C not available"))
    else:
        print("SDA GPIO%d / SCL GPIO%d @ %d kHz: %d device(s)" % (r["sdaPin"], r["sclPin"],
                                                                  r["clockHz"] // 1000, r["count"]))
        for d in r["devices"]:
            print("  " + d["hex"])
    return EXIT_OK


def cmd_wifi(args):
    with open_device(args) as dev:
        if args.wifi_cmd == "scan":
            nets = sorted(dev.scan_wifi(), key=lambda n: -n["rssi"])
            if args.json:
                out_json(nets)
            else:
                for n in nets:
                    print("%4d dBm  %s  %s" % (n["rssi"], "lock" if n["secure"] else "open", n["ssid"]))
            return EXIT_OK
        if args.wifi_cmd == "set":
            password = "" if args.open else read_secret("WiFi password: ", args.password_stdin)
            print_result(args, dev.set_wifi(args.ssid, password))
            return EXIT_OK
        if args.wifi_cmd == "forget":
            print_result(args, dev.forget_wifi())
            return EXIT_OK
    return EXIT_USAGE


def cmd_hostname(args):
    with open_device(args) as dev:
        if args.name is None:
            print(dev.hello()["hostname"])
            return EXIT_OK
        print_result(args, dev.set_device(hostname=args.name))
    return EXIT_OK


def cmd_admin_password(args):
    with open_device(args) as dev:
        if args.action == "clear":
            reply = dev.set_device(clear_admin=True)
        else:
            pw = read_secret("New admin password: ", args.password_stdin)
            if sys.stdin.isatty() and not args.password_stdin:
                if getpass.getpass("Repeat: ") != pw:
                    raise CommandFailed("Passwords do not match.")
            if not pw:
                raise CommandFailed("Empty password; use 'admin-password clear' to remove it.")
            reply = dev.set_device(admin_password=pw)
        print_result(args, reply)
    return EXIT_OK


def cmd_reboot(args):
    with open_device(args) as dev:
        print_result(args, dev.reboot())
        if args.wait_boot:
            time.sleep(2)
            info = dev.wait_ready(total=30)
            print("Back up: %s v%s" % (info["hostname"], info["version"]))
    return EXIT_OK


def cmd_log(args):
    port = resolve_port(args)
    with Device(port, timeout=args.timeout) as dev:
        print("Logging %s (Ctrl-C to stop)" % port, file=sys.stderr)
        while True:
            line = dev.read_log(timeout=1.0)
            if line:
                print((time.strftime("%H:%M:%S ") if args.timestamps else "") + line, flush=True)


def cmd_raw(args):
    params = {}
    for item in args.params:
        key, sep, value = item.partition("=")
        if not sep:
            raise CommandFailed("Parameters must be key=value, got '%s'." % item)
        params[key] = value
    with open_device(args) as dev:
        try:
            out_json(dev.request(args.command, **params))
        except CommandFailed as e:
            out_json(e.reply)
            return EXIT_FAILED
    return EXIT_OK


def find_esptool():
    for name in ("esptool", "esptool.py"):
        path = shutil.which(name)
        if path:
            return [path]
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        return None


def cmd_flash(args):
    with open(args.image, "rb") as f:
        head = f.read(0x8002)
    # A merged image has the bootloader at 0x0 and the partition table
    # (magic AA 50) at 0x8000. A plain application .bin would brick the
    # boot chain if written to 0x0, so refuse it.
    if len(head) < 0x8002 or head[0] != 0xE9 or head[0x8000:0x8002] != b"\xaa\x50":
        print("%s is not a merged image. Flash .pio/build/esp32c3/firmware-merged.bin, "
              "or upload firmware.bin through the web page." % args.image, file=sys.stderr)
        return EXIT_USAGE
    esptool = find_esptool()
    if not esptool:
        print("esptool not found; install it with: sudo apt install esptool", file=sys.stderr)
        return EXIT_FAILED
    port = resolve_port(args)
    cmd = esptool + ["--chip", "esp32c3", "--port", port, "--baud", str(args.baud),
                     "write_flash", "0x0", args.image]
    if args.erase:
        cmd.insert(-2, "--erase-all")
    print("+ " + " ".join(cmd), file=sys.stderr)
    rc = subprocess.call(cmd)
    if rc != 0:
        return EXIT_FAILED
    with Device(port, timeout=args.timeout) as dev:
        info = dev.wait_ready(total=30)
        print("Running: %s v%s (%s)" % (info["title"], info["version"], info["hostname"]))
    return EXIT_OK


# --------------------------------------------------------------------- main --

def build_parser():
    p = argparse.ArgumentParser(prog="ionctl", description="Control an ION Multi-IO board over USB.")
    p.add_argument("-p", "--port", help="serial port (default: $IONCTL_PORT, else auto-detect)")
    p.add_argument("-j", "--json", action="store_true", help="machine-readable JSON output")
    p.add_argument("-t", "--timeout", type=float, default=5.0, help="per-command timeout, s (default 5)")
    p.add_argument("--wait", type=float, default=25.0,
                   help="how long to wait for the board to answer at all, s (default 25)")
    p.add_argument("-v", "--verbose", action="store_true", help="show device log lines on stderr")
    p.add_argument("-V", "--version", action="version", version="%(prog)s " + __version__)
    sub = p.add_subparsers(dest="cmd", metavar="COMMAND")

    s = sub.add_parser("list", help="list boards connected over USB")
    s.set_defaults(func=cmd_list)

    s = sub.add_parser("status", help="show device status")
    s.set_defaults(func=cmd_status)

    s = sub.add_parser("power", help="show or switch the PoE power output")
    s.add_argument("state", nargs="?", choices=["on", "off", "toggle", "cycle"])
    s.add_argument("--delay", type=float, default=5.0, help="off time for 'cycle', s (default 5)")
    s.set_defaults(func=cmd_power)

    s = sub.add_parser("meter", help="read PoE voltage / current / power")
    s.add_argument("-w", "--watch", type=float, metavar="SECS", help="repeat every SECS seconds")
    s.set_defaults(func=cmd_meter)

    s = sub.add_parser("gpio", help="show MCP23017 pin states, or set the update mode")
    s.add_argument("-w", "--watch", type=float, metavar="SECS",
                   help="then poll every SECS seconds and print pin changes")
    s.add_argument("--mode", choices=["interrupt", "polling"], help="set the firmware's update mode")
    s.set_defaults(func=cmd_gpio)

    s = sub.add_parser("i2c", help="scan the I2C bus")
    s.set_defaults(func=cmd_i2c)

    s = sub.add_parser("wifi", help="scan, set or forget WiFi")
    wsub = s.add_subparsers(dest="wifi_cmd", metavar="ACTION")
    wsub.required = True
    wsub.add_parser("scan", help="list nearby networks")
    w = wsub.add_parser("set", help="save network credentials (device reboots)")
    w.add_argument("ssid")
    g = w.add_mutually_exclusive_group()
    g.add_argument("--open", action="store_true", help="network has no password")
    g.add_argument("--password-stdin", action="store_true", help="read the password from stdin")
    wsub.add_parser("forget", help="erase saved credentials (device reboots into setup AP)")
    s.set_defaults(func=cmd_wifi)

    s = sub.add_parser("hostname", help="show or set the hostname (setting it reboots)")
    s.add_argument("name", nargs="?")
    s.set_defaults(func=cmd_hostname)

    s = sub.add_parser("admin-password", help="set or clear the web admin password")
    s.add_argument("action", choices=["set", "clear"])
    s.add_argument("--password-stdin", action="store_true", help="read the password from stdin")
    s.set_defaults(func=cmd_admin_password)

    s = sub.add_parser("reboot", help="restart the device")
    s.add_argument("--wait-boot", action="store_true", help="wait until it answers again")
    s.set_defaults(func=cmd_reboot)

    s = sub.add_parser("log", help="stream the device's log output")
    s.add_argument("-T", "--timestamps", action="store_true", help="prefix each line with the time")
    s.set_defaults(func=cmd_log)

    s = sub.add_parser("raw", help="send a raw protocol command, print the JSON reply")
    s.add_argument("command")
    s.add_argument("params", nargs="*", metavar="KEY=VALUE")
    s.set_defaults(func=cmd_raw)

    s = sub.add_parser("flash", help="write a merged firmware image with esptool")
    s.add_argument("image", help="firmware-merged.bin")
    s.add_argument("--baud", type=int, default=460800)
    s.add_argument("--erase", action="store_true",
                   help="erase the whole chip first (also wipes WiFi and settings)")
    s.set_defaults(func=cmd_flash)
    return p


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help()
        return EXIT_USAGE
    try:
        return args.func(args)
    except CommandFailed as e:
        print("ionctl: %s" % e, file=sys.stderr)
        return EXIT_FAILED
    except DeviceError as e:
        print("ionctl: %s" % e, file=sys.stderr)
        return EXIT_NO_DEVICE
    except KeyboardInterrupt:
        return 130
    except BrokenPipeError:
        return EXIT_OK
