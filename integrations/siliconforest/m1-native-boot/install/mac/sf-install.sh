#!/bin/sh
# SPDX-License-Identifier: MIT
# SiliconForest: run the installer on macOS (INSTALL.md step 4). It is the Asahi Linux
# installer's own code (commit e93743d, with one hunk that shortens its menu) with our
# installer data, our stage 1 as the boot object, our icon and our package.
# It is interactive and asks for the macOS password itself: run it in Terminal on the
# machine (or in an ssh session with a terminal while a user is logged in at the screen).
#   ./sf-install.sh            the installer
# Python: the installer needs a Python 3 with ctypes. This script uses the one the Asahi
# project ships inside its installer package (the one that installed Linux on this machine
# came the same way); it is downloaded once, from the address their own bootstrap uses, into
# ./asahi-bundle, and only its Frameworks directory is used. SF_PYTHON=<python3>: use another.
set -e
cd "$(dirname "$0")"
KIT=$PWD
[ -e /System/Library/CoreServices/SystemVersion.plist ] || { echo "This runs on macOS."; exit 1; }
[ "$(arch)" = arm64 ] || { echo "Run this natively (arm64), not under Rosetta."; exit 1; }
echo "Checking the kit's files..."
shasum -a 256 -c MANIFEST.sha256 > /tmp/sf-manifest.$$ 2>&1 || { grep -v ': OK$' /tmp/sf-manifest.$$; echo "The kit's files are not what was built. Stop."; exit 1; }
rm -f /tmp/sf-manifest.$$
if [ -z "$SF_PYTHON" ]; then
    B=$KIT/asahi-bundle
    PY=$B/Frameworks/Python.framework/Versions/Current/bin/python3
    if [ ! -x "$PY" ]; then
        echo "Fetching the Asahi installer package (for its Python only)..."
        mkdir -p "$B"
        VER=$(curl --no-progress-meter -L https://cdn.asahilinux.org/installer/latest)
        case "$VER" in ""|*[!A-Za-z0-9._-]*) echo "Unexpected version string from the server. Stop."; exit 1;; esac
        curl --no-progress-meter -L -o "$B/installer-$VER.tar.gz" "https://cdn.asahilinux.org/installer/installer-$VER.tar.gz"
        tar -xf "$B/installer-$VER.tar.gz" -C "$B"
        [ -x "$PY" ] || { echo "No Python in that package. Stop (nothing was changed)."; exit 1; }
    fi
    export DYLD_LIBRARY_PATH=$B/Frameworks/Python.framework/Versions/Current/lib
    export DYLD_FRAMEWORK_PATH=$B/Frameworks
    export SSL_CERT_FILE=$B/Frameworks/Python.framework/Versions/Current/etc/openssl/cert.pem
else
    PY=$SF_PYTHON
fi
if [ "$1" = "--python-only" ]; then echo "$PY"; "$PY" -c 'import ctypes, plistlib, zipfile, ssl; print("python ok")'; exit 0; fi
if [ "$(id -u)" != 0 ]; then
    echo "The installer needs to run as root: sudo asks for your password."
    exec caffeinate -dis sudo SF_PYTHON="$SF_PYTHON" "$KIT/sf-install.sh" "$@"
fi
export LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 PATH="/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
# Our names; a local package; nothing is reported to anyone; no expert mode.
export DISTRO=SiliconForest DISTRO_DOCS="INSTALL.md of the SiliconForest install kit" REPO_BASE="$KIT/installer"
unset REPORT REPORT_TAG EXPERT IPSW_BASE INSTALLER_BASE INSTALLER_DATA ALLOW_VM
cd "$KIT/installer"
exec </dev/tty >/dev/tty 2>/dev/tty
exec "$PY" main.py "$@"
