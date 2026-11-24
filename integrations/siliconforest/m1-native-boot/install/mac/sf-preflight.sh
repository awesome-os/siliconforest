#!/bin/sh
# SPDX-License-Identifier: MIT
# SiliconForest: read-only look at this Mac before the install (INSTALL.md step 3).
# Changes nothing: it lists, with the installer's own partition code, what the installer will
# be offered. Run as the logged-in user or over ssh; no password is needed.
#   ./sf-preflight.sh <first free block> <free blocks>     (the numbers of INSTALL.md, 4096-byte blocks)
set -e
cd "$(dirname "$0")"
FIRST=${1:?first free block}; BLOCKS=${2:?number of free blocks}
shasum -a 256 -c MANIFEST.sha256 > /dev/null 2>&1 && echo "PRE ok   the kit's files are what was built" || echo "PRE FAIL the kit's files differ from the manifest"
echo "PRE info macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(sysctl -n hw.model), console user: $(stat -f %Su /dev/console)"
echo "PRE info default boot volume (nvram boot-volume): $(nvram boot-volume 2>/dev/null | cut -f2 | cut -c1-40)..."
PY=${SF_PYTHON:-$PWD/asahi-bundle/Frameworks/Python.framework/Versions/Current/bin/python3}
if [ -z "$SF_PYTHON" ] && [ -x "$PY" ]; then
    export DYLD_LIBRARY_PATH=$PWD/asahi-bundle/Frameworks/Python.framework/Versions/Current/lib DYLD_FRAMEWORK_PATH=$PWD/asahi-bundle/Frameworks
fi
[ -x "$PY" ] || PY=$(command -v python3 || true)
[ -n "$PY" ] && "$PY" -c 'import ctypes' 2>/dev/null || { echo "PRE FAIL no Python yet: run ./sf-install.sh --python-only first (it only downloads the Python bundle)"; diskutil list internal; exit 1; }
cd installer
"$PY" - "$FIRST" "$BLOCKS" <<'PY'
import logging, sys
sys.argv, first, blocks = sys.argv[:1], int(sys.argv[1]), int(sys.argv[2])
logging.disable(logging.CRITICAL)
import diskutil, json
from util import psize, align_down, align_up
PART_ALIGN = psize("1MiB"); STUB = align_down(psize("2.5GB"), PART_ALIGN)
d = diskutil.DiskUtil(); d.get_list(); d.ctnr_by_ref = {}; d.ctnr_by_store = {}; d.get_apfs_list(); d.get_disk_info()
disk = d.find_system_disk()
template = json.load(open("installer_data.json"))["os_list"][0]
need = STUB + sum(align_up(psize(p["size"]), PART_ALIGN) for p in template["partitions"])
print("PRE info system disk %s; the installer needs %d bytes (stub %d + boot partition)" % (disk, need, STUB))
offered, ours = [], None
for p in d.get_partitions(disk):
    if p.free:
        ok = p.size >= STUB and align_down(p.size, PART_ALIGN) >= need
        mine = p.offset == first * 4096 and p.size == blocks * 4096
        print("PRE info free space after %s: offset %d (block %d), %d bytes (%d blocks)%s%s" % (
            p.name, p.offset, p.offset // 4096, p.size, p.size // 4096,
            "  <- offered by the installer" if ok else "  (too small: not offered)", "  <- OURS" if mine else ""))
        if ok: offered.append(p)
        if mine: ours = p
    else:
        print("PRE info %-10s %-22s offset block %11d  %11d blocks  %s" % (p.name, p.type, p.offset // 4096, p.size // 4096, p.label or ""))
print("PRE %s the free space of INSTALL.md (block %d, %d blocks) is there exactly" % ("ok  " if ours else "FAIL", first, blocks))
print("PRE %s it is large enough for the installer" % ("ok  " if ours in offered else "FAIL"))
if len(offered) == 1 and ours in offered:
    print("PRE ok   it is the only free space the installer will offer: there will be no question about where")
elif ours in offered:
    print("PRE NOTE the installer will offer %d free areas and ask which: choose the one of %d bytes after %s" % (len(offered), ours.size, ours.name))
PY
