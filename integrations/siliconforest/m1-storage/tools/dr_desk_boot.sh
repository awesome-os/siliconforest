#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the default desktop with the ROOT ON THE SILICONFOREST PARTITION, read-only
# (disk-root stream, step 2), under the hypervisor or natively. It is ../../m1-native-boot/
# tools/nb_boot.sh's desktop profile (same guest script, same staged files, same launcher
# patch) with: the native collection plus SFAppleNVMe (../build-kc-native.nix), the launcher's
# rd=md0 replaced (../launcher-diskroot.patch), the root named by filesystem UUID, process 1
# = the wrapper that mounts the memory disk for /var and /tmp, the read fence on, and the
# script's end replaced by a clean controller stop. The RAM root is uploaded as always: it is
# the memory disk, it carries this boot's script, and it is the root the boot can be repeated
# with. Programs, libraries and the desktop itself are read from the disk.
# Usage: dr_desk_boot.sh <name, prefix dr> <hv|native> [upper bound s, 120..600, default 200]
#   DR_SECONDS  session length (default 30);  DR_TTL  driver time to live (default bound - 40)
#   DR_RANGES   the range file for the hashes (required);  DR_PROFILE=shell  no desktop, only ../guest/darwin-diskroot.in
#   DR_BOOTARGS further boot arguments
#   DR_RW=1     step 3d: the root READ-WRITE through the write-fenced build (../build-kc-native-rw.nix:
#               SFAppleNVMeRW, ext4 with the downgrade patch, six other-board kexts left out and
#               ../launcher-m1-members.patch for the launcher's required list); process 1 is launchd
#               itself; the partition must have been prepared with tools/dr_linux_root.sh untransform.
#               The boot ends: sync, root downgraded to read-only, fence sealed, controller stopped
#   DR_HALT="<command>"  (with DR_RW=1, native only) the boot is ended by the SYSTEM'S OWN command, e.g.
#               DR_HALT=reboot or DR_HALT="shutdown -r now" (../system: sf-command -> sf-halt), or DR_HALT=menu
#               (the compositor's autopilot clicks Session > Restart in the desktop menu; the script then runs
#               `shutdown -r now` because the session asked for it), not by the
#               guest script: collection ../build-kc-native-rw-halt.nix (SFCpuPower with the restart through the
#               SMC and the hardware clock), the launcher's rule for a guest that restarts the machine itself
#               (../../m1-native-boot/reboot/launcher-self-reboot.diff: no controller reset when the M1's Linux
#               comes back by itself), guests ../guest/diskroot-halt-prelude.sh and ../guest/darwin-diskroot-halt.in
#   DR_INSTALL=1  (with DR_HALT) the boot carries ../system and tools/staged.sh's compositor (DR_COMPOSITOR=<file>:
#               another) in its RAM root and its script installs them on the disk root (through the fenced driver);
#               DR_DOCK=<file>: also the Chonkstep dock with its launcher items (../desktop/system)
#   DR_USERLAND=1  (with DR_INSTALL=1) the boot also carries ../userland (the SSH server and its job, the Homebrew
#               account's wrapper, DR_TERMINAL=<file>: the terminal, DR_USERLAND_KEY=<public key file>, default
#               ~/.ssh/id_rsa.pub: who may log in as root) and userland/sf-userland-install.sh puts them on the disk root
#   DR_AUTO_ONLY=<phases>  the compositor's autopilot runs these phases (e.g. settle,settings); DR_AUTO_KEEP=1:
#               its settings phase picks another theme and display scale from the menu and leaves them
#               With DR_INSTALL=1 the boot also carries the session of a boot without a RAM root (../system/sf-session,
#               the launchd job ../../m1-native-boot/guest/org.siliconforest.desktop.plist) and the bench's own
#               pieces for such a boot (this boot's prelude and end script as templates, sfnvme-test, the range
#               list), which sf-install.sh puts on the disk root.
#   DR_STAGE=1  (with DR_RW=1 and DR_HALT, native) step 4: the launcher uploads ONE image, the SiliconForest stage
#               (../../m1-native-boot/b1: stage m1n1 + configuration + this collection + the RAM root), vectors m1n1
#               into it and only listens; the stage boots the kernel by itself. The stage image (b1/out, b1/build.sh)
#               must have been built for this root: B1_ROOT_UUID=<filesystem UUID of the partition>.
#   DR_STAGE=disk  the same WITHOUT the RAM root record: nothing but the stage and the kernel is uploaded, and
#               nothing of the boot comes from the controller afterwards. Multi-user; the session is the disk's own
#               (launchd's job org.siliconforest.desktop -> /usr/bin/darwin-desktop-test -> /usr/libexec/sf-session),
#               which must have been installed by an earlier boot with DR_INSTALL=1. The boot arguments name the
#               boot to the disk's session (sfbench=<name> sfsession_seconds=<DR_SECONDS>, sfbench_auto=<DR_AUTO_ONLY>):
#               it prints the bench's facts and ends by the system's own `reboot`. DR_HALT must be reboot.
#   DR_PLAIN=1  (with DR_STAGE=disk) the boot arguments do NOT name the boot to the disk's session (no sfbench=):
#               nothing of the bench runs on the M1, the session is the one an installed system has and ends in its
#               plain `reboot`. Only sfsession_seconds=<DR_SECONDS> (else the session lasts four hours) and sfmark=1
#               (sf-halt's last line, so that the capture ends when the machine restarts itself) are added.
#   DR_STAGE_STOP=1  (with DR_STAGE) read-back boot: the stage places everything, does NOT enter the kernel and
#               returns to its proxy; the launcher compares what it placed (b1/native_stage.py readback). The
#               first boot after any change to the stage or to what it is given. Ends with one controller reset.
#   DR_STAGE_WORDS  further stage words (e.g. hostseed)
#   DR_BOOTBIN=<file>  (with DR_STAGE=disk DR_PLAIN=1) nothing is packed on the controller: this file (the install
#               kit's boot-bench.bin or boot.bin, ../../m1-native-boot/install/build.sh: stage 2 + configuration +
#               collection, as stage 1 reads it from the boot partition) is uploaded byte for byte and entered.
#               ITS configuration is the boot's: session length, time to live and every other boot argument are
#               the file's, not this script's. The collection must be the one in the file.
#   DR_STAGE1=<file>  (with DR_STAGE=disk) the install kit's sf-stage1-test.bin (stage 1 with a chainload line that
#               names a partition that does not exist) is uploaded and entered, nothing else: it must find nothing
#               and stop (SF_STOPPED). No kernel. Ends with one controller reset. Any stage 1 with its variable
#               lines can be given (the kit's sf-stage1-installed.bin: the boot object that is in the stub). It
#               is entered as iBoot enters it, top_of_kernel_data directly behind the image;
#               DR_STAGE1_ROOM=1 gives it 64 MiB of room instead (boot dr34; the installed machine has none).
#   DR_STAGE1_GO=<kernel file>  (with DR_STAGE1) stage 1 is expected to START THE SYSTEM from the boot partition
#               it names: sf/boot.bin there, which reads sf/kernel.bin. Nothing but stage 1 is uploaded. The file
#               is a copy of the sf/kernel.bin that is on the partition (the kit's kernel-bench.bin, whose session
#               ends itself): its collection must be this boot's. The slot checks the session as DR_PLAIN's.
# Run inside a slot (Linux before and after): tools/dr_desk_slot.sh.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
NB=$ROOT/integrations/siliconforest/m1-native-boot
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-disk} SF_PD=pin
N=${1:?name}; MODE=${2:?hv or native}; LIMIT=${3:-200}
case "$N" in dr[0-9]*) ;; *) echo "boot names are dr01, dr02, ..." >&2; exit 2;; esac
[ "$LIMIT" -ge 120 ] && [ "$LIMIT" -le 600 ] || { echo "the upper bound is 120..600 seconds" >&2; exit 2; }
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot $N exists" >&2; exit 2; }
[ ! -e runs/bench.finisher.failed ] && [ ! -e runs/m1-storage/STOP ] || { echo "a stop file exists" >&2; exit 3; }
[ -z "${PCIE_CHAIN:-}" ] || { echo "PCIE_CHAIN is not used in this stream" >&2; exit 2; }
# noiolog: driver log lines on the 115200-baud UART delay the display driver until the compositor
# has given up on GL (boot dr07 came up on the software renderer without it)
case "$MODE" in hv) WORDS=;; native) WORDS="native vdm-return vdm-serial noiolog";; *) echo "hv or native" >&2; exit 2;; esac
. private/disk-root/sf-part.env
"$HERE/host-test/run-host-tests.sh" > /dev/null || { echo "host tests failed; not booting" >&2; exit 2; }
NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
RW=${DR_RW:-0}; HALT=${DR_HALT:-}; STAGE=${DR_STAGE:-}
B1=$NB/b1; STAGE_IMAGE=${DR_STAGE_IMAGE:-$B1/out/m1n1-sf-stage.bin}
if [ -n "$STAGE" ]; then
  case "$STAGE" in 1|disk) ;; *) echo "DR_STAGE is 1 or disk" >&2; exit 2;; esac
  [ "$RW" = 1 ] && [ "$MODE" = native ] && [ -n "$HALT" ] || { echo "DR_STAGE needs DR_RW=1, DR_HALT and a native boot" >&2; exit 2; }
  [ "${DR_PROFILE:-desktop}" = desktop ] || { echo "DR_STAGE boots the desktop profile" >&2; exit 2; }
  [ "$STAGE" != disk ] || { [ "$HALT" = reboot ] && [ "${DR_INSTALL:-0}" != 1 ]; } || { echo "DR_STAGE=disk: DR_HALT=reboot, and nothing can be installed without a RAM root" >&2; exit 2; }
  [ -f "$STAGE_IMAGE" ] || { echo "no stage image at $STAGE_IMAGE: run B1_ROOT_UUID=<filesystem UUID> $B1/build.sh" >&2; exit 2; }
  # the stage refuses every disk root but the one it was built for: do not spend a boot on finding that out
  [ "$(grep -a -c -F "$SF_FSUUID" "${DR_BOOTBIN:-$STAGE_IMAGE}")" -ge 1 ] || { echo "the stage image was not built for this root (B1_ROOT_UUID)" >&2; exit 2; }
  WORDS="$WORDS stage ${DR_STAGE_WORDS:-}"
  if [ -n "${DR_BOOTBIN:-}" ]; then
    [ -f "$DR_BOOTBIN" ] && [ "$STAGE" = disk ] && [ "${DR_PLAIN:-0}" = 1 ] && [ -z "${DR_STAGE1:-}" ] || { echo "DR_BOOTBIN=<file> needs DR_STAGE=disk DR_PLAIN=1" >&2; exit 2; }
    WORDS="$WORDS bootbin"
  fi
  if [ -n "${DR_STAGE1:-}" ]; then
    [ -f "$DR_STAGE1" ] && [ "$STAGE" = disk ] && [ "${DR_PLAIN:-0}" != 1 ] && [ "${DR_STAGE_STOP:-0}" != 1 ] || { echo "DR_STAGE1=<file> needs DR_STAGE=disk and nothing else" >&2; exit 2; }
    grep -a -q 'SF_STOPPED' "$DR_STAGE1" || { echo "DR_STAGE1: not the installed variant (it would restart or fall back to the proxy)" >&2; exit 2; }
    WORDS="$WORDS stage1"
    [ "${DR_STAGE1_ROOM:-0}" != 1 ] || WORDS="$WORDS stage1room"
    if [ -n "${DR_STAGE1_GO:-}" ]; then
      [ -f "$DR_STAGE1_GO" ] && grep -a -q 'sfmark=1' <(head -c 4096 "$DR_STAGE1_GO") || { echo "DR_STAGE1_GO=<kernel-bench.bin, as it is on the boot partition>" >&2; exit 2; }
      WORDS="$WORDS stage1go"
    fi
  fi
  [ "$STAGE" != disk ] || WORDS="$WORDS noramdisk multiuser"
  [ "${DR_STAGE_STOP:-0}" != 1 ] || WORDS="$WORDS stagestop"
  [ "${DR_PLAIN:-0}" != 1 ] || { [ "$STAGE" = disk ] && [ "${DR_STAGE_STOP:-0}" != 1 ] && [ -z "${DR_AUTO_ONLY:-}" ]; } || { echo "DR_PLAIN needs DR_STAGE=disk, no read-back and no autopilot" >&2; exit 2; }
elif [ "${DR_STAGE_STOP:-0}" = 1 ] || [ "${DR_PLAIN:-0}" = 1 ]; then echo "DR_STAGE_STOP and DR_PLAIN need DR_STAGE" >&2; exit 2
fi
if [ -n "$HALT" ]; then
  [ "$RW" = 1 ] && [ "$MODE" = native ] || { echo "DR_HALT needs DR_RW=1 and a native boot" >&2; exit 2; }
  case "$HALT" in reboot|"shutdown -r now"|menu) ;; *) echo "DR_HALT is reboot, \"shutdown -r now\" or menu (a power-off needs a person at the machine)" >&2; exit 2;; esac
  [ "$HALT" != menu ] || case ",${DR_AUTO_ONLY:-}," in *,settings,*) ;; *) echo "DR_HALT=menu needs DR_AUTO_ONLY with the settings phase" >&2; exit 2;; esac
fi
KC=$(env -u SF_PD $NP m1-storage build-kc-native$([ "$RW" = 1 ] && echo -rw)$([ -n "$HALT" ] && echo -halt).nix); [ -f "$KC/kernel" ] || { echo "no collection" >&2; exit 2; }
TEST=$($NP m1-storage build-sfnvme-test.nix); [ -x "$TEST/usr/bin/sfnvme-test" ] || { echo "no sfnvme-test" >&2; exit 2; }
B=integrations/siliconforest/m1-bringup
QD=$(mktemp -d); S=$(mktemp); trap 'rm -rf "$QD" "$S"' EXIT
echo "$WORDS" > "$QD/native_mode.txt"
python3 -B -c "import sys; sys.path.insert(0, '$NB'); import native_boot; native_boot.Mode(open('$QD/native_mode.txt').read())"
if [ -n "$HALT" ]; then
  # launcher-native.patch with the self-restart rules on top, as one patch (both change the same two files)
  "$NB/reboot/combine_patch.sh" "$QD/launcher-native-self-reboot.patch"
  cat "$QD/launcher-native-self-reboot.patch" "$HERE/launcher-diskroot.patch" > "$QD/launcher.patch"
  echo "the guest of boot $N restarts the M1 itself" > "$QD/self_reboot_mode.txt"
else
  cat "$NB/launcher-native.patch" "$HERE/launcher-diskroot.patch" > "$QD/launcher.patch"
fi
[ "$RW" != 1 ] || cat "$HERE/launcher-m1-members.patch" >> "$QD/launcher.patch"
FILES="$NB/native_boot.py=$B/native_boot.py;$NB/native_stub.py=$B/native_stub.py;$QD/native_mode.txt=$B/native_mode.txt"
[ -z "$HALT" ] || FILES="$FILES;$QD/self_reboot_mode.txt=$B/self_reboot_mode.txt"
[ -z "$STAGE" ] || FILES="$FILES;$B1/native_stage.py=$B/native_stage.py;$B1/pdsf_pack.py=$B/pdsf_pack.py;$STAGE_IMAGE=$B/m1n1-sf-stage.bin"
[ -z "${DR_BOOTBIN:-}" ] || FILES="$FILES;$DR_BOOTBIN=$B/sf-boot.bin"
[ -z "${DR_STAGE1:-}" ] || FILES="$FILES;$DR_STAGE1=$B/sf-stage1-test.bin"
[ -z "${DR_STAGE1_GO:-}" ] || FILES="$FILES;$DR_STAGE1_GO=$B/sf-kernel.bin"
TTL=${DR_TTL:-$((LIMIT - 40))}
DISK="rd=uuid boot-uuid=$SF_FSUUID launchdsuffix=sfro wdt=-1 sfnvme=3 sfnvme_ttl=$TTL sfnvme_part=$SF_PARTUUID sfnvme_part_lba=$SF_PART_LBA sfnvme_part_blocks=$SF_PART_BLOCKS sfnvme_readfence=1 sf_quiet_wait=0 sf_rtc_wait=0"
[ -f "${DR_RANGES:-}" ] || { echo "DR_RANGES=<range file> is needed" >&2; exit 2; }
END=$HERE/guest/darwin-diskroot.in; PRELUDE=$HERE/guest/diskroot-prelude.sh
if [ "$RW" = 1 ]; then
  DISK="rd=uuid boot-uuid=$SF_FSUUID wdt=-1 sfnvme=3 sfnvme_ttl=$TTL sfnvme_part=$SF_PARTUUID sfnvme_part_lba=$SF_PART_LBA sfnvme_part_blocks=$SF_PART_BLOCKS sfnvme_write=1 sfnvme_readfence=1 sf_quiet_wait=0 sf_rtc_wait=0"
  END=$HERE/guest/darwin-diskroot-rw.in; PRELUDE=$HERE/guest/diskroot-rw-prelude.sh
fi
if [ -n "$HALT" ]; then
  # sfthermal_delay: SFCpuPower's first SMC access (default 20 s into the boot), which is also when the clock is read
  DISK="$DISK sfthermal_delay=${DR_THERMAL_DELAY:-4}"
  END=$HERE/guest/darwin-diskroot-halt.in; PRELUDE=$HERE/guest/diskroot-halt-prelude.sh
fi
sed -e "s/@PART_LBA@/$SF_PART_LBA/g" -e "s/@PART_BLOCKS@/$SF_PART_BLOCKS/g" -e "s/@NAME@/$N/g" -e "s/@HALT_COMMAND@/$HALT/g" "$END" > "$QD/dr-diskroot-end"
X="$TEST/usr/bin/sfnvme-test=/usr/bin/sfnvme-test;$DR_RANGES=/etc/sfnvme-ranges;$QD/dr-diskroot-end=/usr/bin/dr-diskroot-end"
if [ -n "$HALT" ] && [ "${DR_INSTALL:-0}" = 1 ]; then
  SYS=$(env -u SF_PD $NP m1-storage build-system-tools.nix); [ -x "$SYS/sbin/sf-halt" ] || { echo "no system tools" >&2; exit 2; }
  COMP=${DR_COMPOSITOR:-$(env -u SF_PD tools/staged.sh compositor)}; [ -x "$COMP" ] || { echo "no compositor at $COMP" >&2; exit 2; }
  X="$X;$SYS/sbin/sf-halt=/usr/share/sf-system/sf-halt;$SYS/usr/sbin/sf-clock=/usr/share/sf-system/sf-clock;$COMP=/usr/bin/chonkstep-darwin-desktop"
  for f in sf-boot sf-command sf-install.sh darwin-desktop-test.disk sf-session; do X="$X;$HERE/system/$f=/usr/share/sf-system/$f"; done
  X="$X;$NB/guest/org.siliconforest.desktop.plist=/usr/share/sf-system/org.siliconforest.desktop.plist"
  # the Chonkstep dock (../desktop): DR_DOCK=<chonk-dock built by ../desktop/tools/build_dock.sh>; its launcher
  # items and pins go with it (system/sf-install.sh puts them on the disk root)
  if [ -n "${DR_DOCK:-}" ]; then
    [ -x "$DR_DOCK" ] || { echo "no dock at $DR_DOCK" >&2; exit 2; }
    X="$X;$DR_DOCK=/usr/share/sf-system/chonkstep-dock;$HERE/../desktop/system/dock-pins=/usr/share/sf-system/dock-pins"
    for f in "$HERE"/../desktop/system/applications/*.desktop; do X="$X;$f=/usr/share/sf-system/applications/$(basename "$f")"; done
  fi
  if [ "${DR_USERLAND:-0}" = 1 ]; then
    # daily use (../userland): the SSH server (upstream's OpenSSH; the client and the rest follow over SSH,
    # tools/sf_userland_push.sh), its configuration and launchd job, the Homebrew account's wrapper, the
    # terminal (DR_TERMINAL=<file>), and ONE public key that may log in as root (DR_USERLAND_KEY, public half only)
    U=$HERE/userland; SSH=$(env -u SF_PD $NP m1-storage/userland build-openssh.nix); [ -x "$SSH/sbin/sshd" ] || { echo "no OpenSSH" >&2; exit 2; }
    KEY=${DR_USERLAND_KEY:-$HOME/.ssh/id_rsa.pub}
    case "$KEY" in *.pub) ;; *) echo "DR_USERLAND_KEY must be a public key file (*.pub)" >&2; exit 2;; esac
    [ "$(grep -c . "$KEY")" = 1 ] && ! grep -q 'PRIVATE KEY' "$KEY" || { echo "DR_USERLAND_KEY: not one public key" >&2; exit 2; }
    for f in sf-userland-install.sh sshd_config sf-sshd org.siliconforest.sshd.plist brew sf-environment shasum; do X="$X;$U/$f=/usr/share/sf-userland/$f"; done
    for f in sbin/sshd libexec/sshd-session libexec/sshd-auth bin/ssh-keygen; do X="$X;$SSH/$f=/usr/share/sf-userland/ssh/$f"; done
    X="$X;$KEY=/usr/share/sf-userland/authorized_key.pub"
    if [ -n "${DR_TERMINAL:-}" ]; then [ -x "$DR_TERMINAL" ] || { echo "no terminal at $DR_TERMINAL" >&2; exit 2; }; X="$X;$DR_TERMINAL=/usr/share/sf-userland/chonk-terminal-darwin"; fi
  fi
  # the bench's own pieces for a boot without a RAM root: this boot's prelude and end script as templates
  # (system/sf-session fills them in at boot), reading from the disk what they read from the memory disk here
  "$HERE/tools/dr_bench_templates.sh" "$QD"
  X="$X;$QD/bench-prelude.in=/usr/share/sf-system/bench/prelude.in;$QD/bench-end.in=/usr/share/sf-system/bench/end.in"
  X="$X;$TEST/usr/bin/sfnvme-test=/usr/share/sf-system/bench/sfnvme-test;$DR_RANGES=/usr/share/sf-system/bench/sfnvme-ranges"
fi
if [ "${DR_PROFILE:-desktop}" = desktop ]; then
  BG=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
  if [ -f "$BG" ]; then
    python3 integrations/siliconforest/m1-quiet-boot/boot_logo.py pack "$BG" "$QD" > /dev/null
    FILES="$FILES;$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json"
  fi
  # the desktop's own guest script (speakers held 20 dB down: nobody listens), with facts about
  # the root in front and, instead of its end marker, the stop of the controller from memory
  G=integrations/siliconforest/m1-boot-speed/guest/darwin-gpu-desktop-fast.in
  [ "$(tail -n 1 "$G")" = "printf '\\nPD_BENCH_DONE\\n'" ] || { echo "the desktop script does not end with the end marker" >&2; exit 2; }
  { sed -n '1,/^nonce=/p' "$G"; sed -e "s/@NAME@/$N/g" -e "s/@INSTALL@/${DR_INSTALL:-0}/g" -e "s/@AUTO_ONLY@/${DR_AUTO_ONLY:-}/g" -e "s/@AUTO_KEEP@/${DR_AUTO_KEEP:-0}/g" -e "s/@AUTO_END@/$([ "$HALT" = menu ] && echo restart)/g" "$PRELUDE"
    sed -e '1,/^nonce=/d' -e '$ d' "$G"; cat "$HERE/guest/diskroot-end.sh"; } |
    sed -e "s/@SECONDS@/${DR_SECONDS:-30}/g" -e "s/@HOLD@/1/g" -e "s/@VOLUME@/30/g" > "$S"
  ARGS="cpus=8 sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $DISK ${DR_BOOTARGS:-}"
else
  cp "$QD/dr-diskroot-end" "$S"
  ARGS="cpus=8 $DISK ${DR_BOOTARGS:-}"
fi
case "$MODE" in native) ARGS="$ARGS sfcpuheld=1";; esac
if [ "$STAGE" = disk ]; then
  case "$N${DR_AUTO_ONLY:-}" in *[!a-z0-9,]*) echo "DR_STAGE=disk: the name and DR_AUTO_ONLY travel as boot arguments (a-z, 0-9, comma)" >&2; exit 2;; esac
  if [ "${DR_PLAIN:-0}" = 1 ]; then ARGS="$ARGS sfsession_seconds=${DR_SECONDS:-30} sfmark=1"
  else ARGS="$ARGS sfbench=$N sfsession_seconds=${DR_SECONDS:-30}${DR_AUTO_ONLY:+ sfbench_auto=$DR_AUTO_ONLY}"; fi
fi
R=runs/m1-desktop-010/pcie-enum-attempt-$N
env PCIE_LAB_PATCH="$QD/launcher.patch" PCIE_LAB_FILES="$FILES" \
  PCIE_STAGER="$ROOT/integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py" \
  PCIE_KC="$KC" PCIE_BOOTARGS="$ARGS" PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" PCIE_DONE=marker \
  PCIE_USB_CHUNK=1048576 PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$LIMIT" 2>&1 | grep -a '"status"\|rror\|delivered\|No space\|BENCH_TIMES' | cut -c1-240 || true
[ -f "$R/times.txt" ] || { echo "no boot was recorded in $R" >&2; exit 1; }
printf 'mode=%s\nprofile=%s\nkc=%s\nlimit=%s\nrw=%s\nhalt=%s\ninstall=%s\nauto=%s keep=%s\n' "$MODE" "${DR_PROFILE:-desktop}" "$KC" "$LIMIT" "$RW" "$HALT" "${DR_INSTALL:-0}" "${DR_AUTO_ONLY:-}" "${DR_AUTO_KEEP:-0}" > "$R/dr-config.txt"
[ -z "$STAGE" ] || printf 'stage=%s stop=%s plain=%s words=%s image_sha256=%s\n' "$STAGE" "${DR_STAGE_STOP:-0}" "${DR_PLAIN:-0}" "$WORDS" "$(sha256sum "$STAGE_IMAGE" | cut -d' ' -f1)" >> "$R/dr-config.txt"
[ -z "${DR_BOOTBIN:-}" ] || printf 'bootbin_sha256=%s\n' "$(sha256sum "$DR_BOOTBIN" | cut -d' ' -f1)" >> "$R/dr-config.txt"
[ -z "${DR_STAGE1:-}" ] || printf 'stage1_test_sha256=%s\n' "$(sha256sum "$DR_STAGE1" | cut -d' ' -f1)" >> "$R/dr-config.txt"
[ -z "${DR_STAGE1_GO:-}" ] || printf 'stage1_go_kernel_file_sha256=%s\n' "$(sha256sum "$DR_STAGE1_GO" | cut -d' ' -f1)" >> "$R/dr-config.txt"
[ -z "$STAGE" ] || { grep -a 'NATIVE_STAGE_START\|STAGE_READBACK' "$R/guest/worker.log" | sed 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' | cut -c1-700 || true; }
grep -o '"status": *"[^"]*"' "$R/result.json" 2>/dev/null || echo "no result.json"
