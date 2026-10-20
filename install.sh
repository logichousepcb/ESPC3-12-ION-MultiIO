#!/bin/sh
# Install (or update) the ionctl Debian package.
#
# Straight from GitHub (installs the latest release):
#   curl -fsSL https://raw.githubusercontent.com/logichousepcb/ESPC3-12-ION-MultiIO/main/install.sh | sh
#
# From a clone (builds the package from the checked-out source):
#   git clone https://github.com/logichousepcb/ESPC3-12-ION-MultiIO.git
#   cd ESPC3-12-ION-MultiIO && ./install.sh          # update: git pull && ./install.sh
set -eu

REPO="logichousepcb/ESPC3-12-ION-MultiIO"
LATEST_URL="https://github.com/$REPO/releases/latest/download/ion-multiio-tools_latest_all.deb"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
    TARGET_USER="${SUDO_USER:-}"
else
    SUDO="sudo"
    TARGET_USER="$(id -un)"
fi

# Work in a temp dir that apt (which drops to the _apt user) can read.
WORK="$(mktemp -d)"
chmod 755 "$WORK"
trap 'rm -rf "$WORK"' EXIT

SRC_DIR=""
case "$0" in
    */install.sh | install.sh) SRC_DIR="$(cd "$(dirname "$0")" && pwd)" ;;
esac

if [ -n "$SRC_DIR" ] && [ -f "$SRC_DIR/host/build_deb.py" ]; then
    if ! command -v python3 >/dev/null 2>&1; then
        $SUDO apt-get update
        $SUDO apt-get install -y python3
    fi
    echo "Building package from $SRC_DIR..."
    DEB="$(python3 "$SRC_DIR/host/build_deb.py" --out-dir "$WORK")"
else
    DEB="$WORK/ion-multiio-tools_latest_all.deb"
    echo "Downloading latest release..."
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$DEB" "$LATEST_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$DEB" "$LATEST_URL"
    else
        echo "Need curl or wget: sudo apt install curl" >&2
        exit 1
    fi
fi
chmod 644 "$DEB"

echo "Installing..."
$SUDO apt-get install -y --reinstall "$DEB"

if [ -n "$TARGET_USER" ] && [ "$TARGET_USER" != "root" ]; then
    if ! id -nG "$TARGET_USER" | tr ' ' '\n' | grep -qx dialout; then
        $SUDO usermod -aG dialout "$TARGET_USER"
        echo
        echo "Added $TARGET_USER to the 'dialout' group for USB serial access."
        echo "Log out and back in before running ionctl."
    fi
fi

echo
echo "Installed: $(ionctl --version)"
echo "Try: ionctl list"
