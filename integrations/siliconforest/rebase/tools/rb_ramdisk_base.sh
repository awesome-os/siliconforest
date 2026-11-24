#!/bin/bash
# SPDX-License-Identifier: MIT
# The RAM root base for boots of the rebase target: the ext4 root of the tip's arm64 minimal
# image, copied out and shrunk, as ../../m1-rootfs/extract-ramdisk.sh did for the pin
# (artifacts/m1-bringup-009/ramdisk.img). The partition is found in the image's GPT, not by a
# written-down offset. Writes artifacts/rebase/ramdisk-tip.img and ramdisk-tip.img.json (its
# hash, the store paths it came from, and the hash of each file the stagers replace).
# An existing image is left as it is.
# Usage: rb_ramdisk_base.sh [image store path]   (default: SF_PD=tip nix-build of image-arm64-virt-minimal)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"; export PATH=$HOME/.nix-profile/bin:$PATH
OUT=artifacts/rebase/ramdisk-tip.img
[ -e "$OUT" ] && { echo "keeping $OUT"; exit 0; }
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
R=integrations/siliconforest/rebase
SRC=${1:-$(cd $R && $NIX -E '(import ./pd.nix).pd.packages.x86_64-linux.image-arm64-virt-minimal' | tail -1)}
IMG=$(ls "$SRC"/*.img | head -1)
LIB=${RB_LIBSYSTEM:-$(cd integrations/siliconforest/libsystem-default && $NIX build-libsystem.nix | tail -1)}
mkdir -p artifacts/rebase; TMP=$(mktemp artifacts/rebase/ramdisk-tip.XXXXXX); trap 'rm -f "$TMP"' EXIT
read -r FIRST COUNT < <(python3 - "$IMG" <<'PY'
import struct, sys
with open(sys.argv[1], 'rb') as f:
    f.seek(512); h = f.read(92); assert h[:8] == b'EFI PART'
    lba, count, size = struct.unpack_from('<QII', h, 72); f.seek(lba * 512)
    parts = []
    for _ in range(count):
        e = f.read(size)
        if e[:16] != bytes(16):
            a, b = struct.unpack_from('<QQ', e, 32); parts.append((a, b - a + 1))
assert len(parts) >= 2, parts
print(*parts[1])
PY
)
dd if="$IMG" of="$TMP" bs=512 skip="$FIRST" count="$COUNT" status=none
st=0; e2fsck -f -y "$TMP" > artifacts/rebase/ramdisk-tip.log 2>&1 || st=$?; [ $st -le 3 ] || { echo "e2fsck: $st" >&2; exit 1; }
resize2fs -M "$TMP" >> artifacts/rebase/ramdisk-tip.log 2>&1
e2fsck -fn "$TMP" >> artifacts/rebase/ramdisk-tip.log 2>&1
D=$(mktemp -d); debugfs -R "dump /usr/lib/libSystem.B.dylib $D/libSystem" "$TMP" 2>/dev/null
python3 - "$TMP" "$IMG" "$LIB" "$D/libSystem" "$FIRST" "$COUNT" > "$OUT.json" <<'PY'
import hashlib, json, sys
sha = lambda p: hashlib.file_digest(open(p, 'rb'), 'sha256').hexdigest()
img, src, lib, libsystem, first, count = sys.argv[1:]
print(json.dumps(dict(sha256=sha(img), source_image=src, partition=dict(first_sector=int(first), sectors=int(count)),
                      siliconforest='c713d8fb9b048a38e2242d67e29b9957ced8c495', libsystem=lib,
                      files={'/usr/lib/libSystem.B.dylib': sha(libsystem)}), indent=1))
PY
rm -rf "$D"; chmod 444 "$TMP"; mv "$TMP" "$OUT"; trap - EXIT
ls -l "$OUT"; cat "$OUT.json"
