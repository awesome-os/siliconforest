#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU rehearsal of the macOS-compatibility additions on the M1's root image (no M1, no bench):
# ../guest/qemu-compat.sh through ../../m1-storage/tools/dr_qemu_userland.sh.
#   tools/sf_compat_stage.sh; ../../m1-storage/tools/sf_userland_push.sh stage      first
#   tools/sf_compat_qemu.sh <name> [second]
# The root is the image the disk root was made from with the `sys` part and the libSystem the
# M1 has today; /opt/homebrew is SF_COMPAT_PREFIX (default: the copy of the M1's prefix in
# runs/sf-gaps/m1root). `second`: boot the root the run <name> left (runs/m1-storage/qemu-dr/
# <name>-after.img) again as <name>-2, with nothing installed: the fresh boot of the result.
# About 12 minutes and more; output in runs/m1-storage/qemu-dr/<name>/serial.log (COMPAT_ lines).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; C=integrations/siliconforest/macos-compat; ST=${SF_COMPAT_STAGE:-runs/sf-compat/stage}; T=/usr/share/sf-test
[ -s "$ST/replace.list" ] || { echo "run $C/tools/sf_compat_stage.sh first" >&2; exit 2; }
LS=$(cd integrations/siliconforest/libsystem-default && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-libsystem.nix 2>/dev/null | tail -1)
W=runs/sf-compat/qemu-$N; rm -rf "$W"; mkdir -p "$W"
# SF_COMPAT_LIBSYSTEM: the libSystem the root starts with (default: the lab's default build; to rehearse
# a libSystem replacement, the build the M1 has at that moment)
FILES="$C/sf-lib-install.sh=$T/sf-lib-install.sh $C/guest/sf-compat-check.sh=$T/sf-compat-check.sh"
# (a second boot keeps every library the first run left, libSystem included)
[ "${2:-}" = second ] || FILES="${SF_COMPAT_LIBSYSTEM:-$LS/usr/lib/libSystem.B.dylib}=/usr/lib/libSystem.B.dylib $FILES"
for f in "$ST"/test/*; do FILES="$FILES $f=$T/$(basename "$f")"; done
if [ "${2:-}" = second ]; then
    export DR_QEMU_ROOT_IMAGE=runs/m1-storage/qemu-dr/$N-after.img; N=$N-2
    [ -s "$DR_QEMU_ROOT_IMAGE" ] || { echo "no $DR_QEMU_ROOT_IMAGE" >&2; exit 2; }
else
    sed 's|^\(/[^ ]*\) \([0-9a-f]*\) |\1 \2 |' "$ST/replace.list" > "$W/replace"
    FILES="$FILES $W/replace=$T/compat/replace"
    for f in "$ST"/replace/*.new; do FILES="$FILES $f=$T/compat/$(basename "$f")"; done
    if [ -s "$ST/new.sha256" ] && [ -z "${SF_COMPAT_HOST_NEW:-}" ]; then
        tar -C "$ST/new" --owner=0 --group=0 --numeric-owner --format=gnu -cf - . | gzip -1 > "$W/new.tar.gz"
        FILES="$FILES $W/new.tar.gz=$T/compat/new.tar.gz $ST/new.sha256=$T/compat/new.sha256"
    elif [ -s "$ST/new.sha256" ]; then
        # SF_COMPAT_HOST_NEW=1: the new files are put into the root image by the host, with the sys part
        # (the guest's ext4 driver writes about 0.2 MB/s under emulation: the tar stream takes 25 minutes)
        U=runs/sf-userland/stage; mkdir -p "$W/stage"
        cp -a --reflink=auto "$U/sys" "$W/stage/sys"; chmod -R u+w "$W/stage/sys"; cp -a --reflink=auto "$ST/new/." "$W/stage/sys/"
        cp "$U/sys.sha256" "$W/stage/sys.sha256"; ln -s "$(readlink -f "$U/brew")" "$W/stage/brew"; ln -s "$(readlink -f "$U/claude")" "$W/stage/claude"
        export DR_QEMU_STAGE=$W/stage
    fi
fi
[ -z "${SF_COMPAT_EXTRA:-}" ] || FILES="$FILES $SF_COMPAT_EXTRA=$T/compat/extra.sh"
# SF_COMPAT_STAGE: another stage directory of the same layout (../../terminal-app/tools/sf_terminal.sh
# rehearses its own replacements with it); SF_COMPAT_MORE_FILES="src=/dest ...": further files for the root
FILES="$FILES ${SF_COMPAT_MORE_FILES:-}"
DR_QEMU_GUEST=$C/guest/qemu-compat.sh DR_QEMU_TIMEOUT=${DR_QEMU_TIMEOUT:-2400} DR_QEMU_FS_SIZE=${DR_QEMU_FS_SIZE:-3G} \
DR_QEMU_PREFIX=${SF_COMPAT_PREFIX:-runs/sf-gaps/m1root/opt/homebrew} DR_QEMU_ROOT_FILES="$FILES" \
  integrations/siliconforest/m1-storage/tools/dr_qemu_userland.sh "$N" quick
