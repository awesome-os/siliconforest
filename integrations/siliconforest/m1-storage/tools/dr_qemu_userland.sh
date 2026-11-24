#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU rehearsal of the installed system's userland on the M1's own root image (no M1, no bench):
# guest/qemu-userland.sh. The root is a copy of the image the disk root was made from, grown to
# 2 GiB, read-write on a 4096-byte-sector GPT disk; it carries userland/ as a bench boot's RAM
# root does, the `sys` part as tools/sf_userland_push.sh sends it, and the Claude Code CLI where
# the push puts it; /opt/homebrew is a second disk made from the staged prefix (owner 501:20).
#   tools/sf_userland_push.sh stage      first
#   tools/dr_qemu_userland.sh <name> [quick|plain|tui]      output in runs/m1-storage/qemu-dr/<name>/
# quick: the install, the SSH server and client, a handful of programs (the host puts the sys part there).
# DR_QEMU_TIMEOUT (default 900 s) ends the guest; a full run needs about an hour of emulation.
# DR_QEMU_GUEST=<script>   another guest script on the same root (guest/qemu-claude.sh: SF-X-claude-code.md)
# DR_QEMU_ROOT_FILES="src=/dest ..."   further files written into the root image, mode 755 (the parent directory is made, its parent must exist)
# DR_QEMU_PREFIX=<directory>   the /opt/homebrew to put on the second disk instead of the staged one (a copy of the M1's)
# DR_QEMU_FS_SIZE=<size>       the root file system's size (default 2G)
# DR_QEMU_STAGE=<directory>    another stage directory (sys, sys.sha256, brew, claude as sf_userland_push.sh stage makes them)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; PHASE=${2:-plain}; M=integrations/siliconforest/m1-storage; W=runs/m1-storage/qemu-dr; U=$M/userland; ST=${DR_QEMU_STAGE:-runs/sf-userland/stage}
SRC=${DR_QEMU_ROOT_IMAGE:-artifacts/m1-desktop-010/pcie-enum-candidate-sw01/ramdisk.img}
[ -s "$ST/sys.sha256" ] && [ -d "$ST/brew/opt/homebrew/Cellar" ] || { echo "run tools/sf_userland_push.sh stage first" >&2; exit 2; }
BASE=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-qemu-image.nix 2>/dev/null | tail -1)
SSH=$(integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py m1-storage/userland build-openssh.nix)
FS=$W/$N-fs.img; DISK=$W/$N-disk.img; PFX=$W/$N-prefix.img; GUID=5F1D0A7E-0000-4000-8000-0000000000A1
GUEST=${DR_QEMU_GUEST:-$M/guest/qemu-userland.sh}
mkdir -p "$W"; cp --reflink=auto "$SRC" "$FS"; chmod u+w "$FS"
e2fsck -fy "$FS" > /dev/null 2>&1 || true; resize2fs "$FS" "${DR_QEMU_FS_SIZE:-2G}" > /dev/null 2>&1
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
tar -C "$ST/sys" --owner=0 --group=0 --numeric-owner --format=gnu -cf - . | gzip -1 > "$T/sys.tar.gz"
sed 's|  | |' "$ST/sys.sha256" > "$T/sys.sha256"
ssh-keygen -q -t ed25519 -N '' -C qemu-rehearsal-only -f "$T/k"      # stands for the workstation's public key
printf '%s\n' "$PHASE" > "$T/phase"
CL=$(cd "$ST/claude" && find usr/local/lib -type f -name claude)
{ for d in /usr/share/sf-userland /usr/share/sf-userland/ssh /usr/share/sf-userland/ssh/sbin /usr/share/sf-userland/ssh/libexec /usr/share/sf-userland/ssh/bin \
           /usr/share/sf-test /usr/local /usr/local/bin /usr/local/lib /usr/local/lib/claude-code "/$(dirname "$CL")"; do echo "mkdir $d"; done
  for f in sf-userland-install.sh sshd_config sf-sshd org.siliconforest.sshd.plist brew sf-environment shasum; do echo "write $U/$f /usr/share/sf-userland/$f"; done
  for f in sbin/sshd libexec/sshd-session libexec/sshd-auth bin/ssh-keygen; do echo "write $SSH/$f /usr/share/sf-userland/ssh/$f"; done
  echo "write $T/k.pub /usr/share/sf-userland/authorized_key.pub"
  echo "write $T/sys.sha256 /usr/share/sf-test/sys.sha256"
  if [ "$PHASE" = quick ]; then   # the sys part put there by the host: directories, files, links
    (cd "$ST/sys" && find . -mindepth 1 -type d -printf '%P\n' | LC_ALL=C sort) | while read -r d; do echo "mkdir /$d"; done
    (cd "$ST/sys" && find . -type f -printf '%P\n') | while read -r f; do echo "write $ST/sys/$f /$f"; done
    (cd "$ST/sys" && find . -type l -printf '%P %l\n') | while read -r f t; do echo "symlink /$f $t"; done
  else echo "write $T/sys.tar.gz /usr/share/sf-test/sys.tar.gz"; fi
  # the guest script itself: the root is this disk, not the system image dr_qemu.sh prepares
  echo "write $GUEST /usr/bin/dr-guest"
  for f in ${DR_QEMU_ROOT_FILES:-}; do echo "mkdir $(dirname "${f#*=}")"; echo "rm ${f#*=}"; echo "write ${f%%=*} ${f#*=}"; echo "sif ${f#*=} mode 0100755"; done
  echo "write $ST/claude/$CL /$CL"; echo "symlink /usr/local/bin/claude ../lib/claude-code/$(basename "$(dirname "$CL")")/claude"
  echo "write $T/phase /etc/dr-phase"
} | debugfs -w "$FS" > "$W/$N-inject.log" 2>&1
python3 -B $M/tools/dr_qemu_disk.py "$FS" "$DISK" --guid "$GUID" > "$W/$N-disk.txt"
python3 -B -c 'import sys; sys.path.insert(0, "integrations/siliconforest/homebrew"); import hb_pour; from pathlib import Path; print(hb_pour.make_image(Path(sys.argv[1]), Path(sys.argv[2]), 256, "501:20"))' "${DR_QEMU_PREFIX:-$ST/brew/opt/homebrew}" "$PFX" > /dev/null
QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_SINGLE=1 DR_QEMU_KERNEL=debug DR_QEMU_DISK2=$PFX \
  DR_QEMU_BOOTARGS="rd=uuid boot-uuid=$GUID -s" $M/tools/dr_qemu.sh "$N" "$GUEST" "$DISK" rw "${DR_QEMU_TIMEOUT:-900}" || true
grep -a '^UL_\|^CJ_\|^COMPAT_\|libxpc misuse\|QDDESK_PASS\|panic' "$W/$N/serial.log" | cut -c1-700 || true
FIRST=$((6 + 4096)); BLOCKS=$(( $(stat -c %s "$FS") / 4096 ))
dd if="$DISK" of="$W/$N-after.img" bs=4096 skip=$FIRST count=$BLOCKS status=none
e2fsck -fn "$W/$N-after.img" 2>&1 | tail -1 | sed 's/^/UL_HOST fsck: /'
