#!/bin/bash
# SPDX-License-Identifier: MIT
# The audio server's check in QEMU (no M1, no bench): one boot of the arm64 `virt` image with the
# audio server, frameworks, sftone, sfvolume and sfplay of this tree, and ../guest/
# qemu-sysaudio-names.sh typed at the root shell. See that script for what it shows; it proves
# nothing about sound (the machine has no sound hardware, the device nodes are stand-ins).
# Usage: sa_qemu.sh [name, default sa-names]     -> runs/qemu-dev/<name>/{serial.log,result.json}
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
Q=integrations/siliconforest/qemu-dev
NAME=${1:-sa-names}
case "$NAME" in *[!A-Za-z0-9._-]*|'') echo "name: letters, digits, . _ - only" >&2; exit 2;; esac
OUT=runs/qemu-dev/$NAME; mkdir -p "$OUT"
python3 -B "$Q/qd_image.py" --output "$OUT/disk.img" \
  --file "$HERE/guest/qemu-sysaudio-names.sh=/usr/bin/qemu-sysaudio-names" > "$OUT/image.json"
rc=0
python3 -B "$Q/qd_run.py" --image "$OUT/disk.img" --out "$OUT" --command /usr/bin/qemu-sysaudio-names \
  --begin SAQ_BEGIN --end SAQ_END --passed SAQ_PASS --up SAQ_UNAME --shot-every 0 --timeout "${SA_TIMEOUT:-600}" || rc=$?
grep -a -E '^SAQ_(OK|FAIL|FAILS|PASS)' "$OUT/serial.log" | tr -d '\r' || true
rm -f "$OUT/disk.img"
exit $rc
