#!/usr/bin/env python3
"""Build the ionctl Debian package (ion-multiio-tools_<ver>_all.deb).

Pure Python, so it runs on Debian or on the Windows build PC alike:

    python3 build_deb.py            # writes dist/ion-multiio-tools_<ver>_all.deb
"""

import gzip
import hashlib
import io
import os
import re
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = "ion-multiio-tools"
PY_DEST = "usr/lib/python3/dist-packages/ion_multiio"

CONTROL = """\
Package: {pkg}
Version: {version}
Architecture: all
Maintainer: logixpcb <logixpcb@gmail.com>
Depends: python3 (>= 3.7), python3-serial
Recommends: esptool
Section: utils
Priority: optional
Installed-Size: {size_kb}
Description: USB control tool for ION Multi-IO boards
 ionctl talks to ION Multi-IO (ESP32-C3) boards over their USB-serial
 port: status, PoE power switching, power metering, GPIO expander state,
 I2C scan, WiFi setup, hostname/admin password and firmware flashing.
"""

LAUNCHER = """\
#!/usr/bin/python3
from ion_multiio.cli import main

raise SystemExit(main())
"""


def read_version():
    with open(os.path.join(HERE, "ion_multiio", "__init__.py"), encoding="utf-8") as f:
        return re.search(r'__version__ = "([^"]+)"', f.read()).group(1)


def text(path):
    """File contents with LF line endings (sources may be checked out on Windows)."""
    with open(path, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def payload():
    """[(path inside package, bytes, mode)]"""
    files = []
    src = os.path.join(HERE, "ion_multiio")
    for name in sorted(os.listdir(src)):
        if name.endswith(".py"):
            files.append(("%s/%s" % (PY_DEST, name), text(os.path.join(src, name)), 0o644))
    files.append(("usr/bin/ionctl", LAUNCHER.encode(), 0o755))
    files.append(("usr/share/doc/%s/README.md" % PKG, text(os.path.join(HERE, "README.md")), 0o644))
    return files


def make_tar(entries, mtime):
    """gzip'd tar of [(path, bytes, mode)], with parent directories, owned by root."""
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=mtime) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as tar:
            dirs = set()
            for path, _, _ in entries:
                parts = path.split("/")[:-1]
                for i in range(1, len(parts) + 1):
                    dirs.add("/".join(parts[:i]))
            members = [(d, None, 0o755) for d in sorted(dirs)] + list(entries)
            for path, data, mode in members:
                info = tarfile.TarInfo("./" + path)
                info.mode, info.mtime = mode, mtime
                info.uid = info.gid = 0
                info.uname = info.gname = "root"
                if data is None:
                    info.type = tarfile.DIRTYPE
                    tar.addfile(info)
                else:
                    info.size = len(data)
                    tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def ar_member(name, data, mtime):
    header = "%-16s%-12d%-6d%-6d%-8s%-10d`\n" % (name, mtime, 0, 0, "100644", len(data))
    return header.encode("ascii") + data + (b"\n" if len(data) % 2 else b"")


def main():
    version = read_version()
    mtime = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    files = payload()

    size_kb = (sum(len(d) for _, d, _ in files) + 1023) // 1024
    md5sums = "".join("%s  %s\n" % (hashlib.md5(d).hexdigest(), p) for p, d, _ in files)
    control = [
        ("control", CONTROL.format(pkg=PKG, version=version, size_kb=size_kb).encode(), 0o644),
        ("md5sums", md5sums.encode(), 0o644),
    ]

    deb = b"!<arch>\n"
    deb += ar_member("debian-binary", b"2.0\n", mtime)
    deb += ar_member("control.tar.gz", make_tar(control, mtime), mtime)
    deb += ar_member("data.tar.gz", make_tar(files, mtime), mtime)

    out_dir = os.path.join(HERE, "dist")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "%s_%s_all.deb" % (PKG, version))
    with open(out, "wb") as f:
        f.write(deb)
    print(out)


if __name__ == "__main__":
    main()
