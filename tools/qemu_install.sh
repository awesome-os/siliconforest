#!/bin/bash
# SPDX-License-Identifier: MIT
# A persistent SiliconForest install in QEMU (arm64 `virt`, TCG): one virtual disk that keeps
# its root filesystem between boots, next to the throw-away loop of tools/qemu_desktop.sh.
# See integrations/siliconforest/qemu-dev/README.md ("Persistent install").
#
#   tools/qemu_install.sh create [--variant minimal|wayland] [--bin FILE] [--terminal FILE] [--force]
#       makes runs/qemu-dev/install/disk.img: the upstream userland (minimal, or `wayland` =
#       upstream's full no-X image once Nix has built it), our desktop (compositor, terminal,
#       audio server, session started by launchd), a user account `sf` (uid 501, /Users/sf)
#       next to root
#   tools/qemu_install.sh boot [--window] [--seconds N] [--command 'guest command'] [qd_run options]
#       boots that disk. Default: headless, the desktop for --seconds (default 120), then the
#       disk is synced and QEMU ends. --window: a QEMU window for --seconds (default 14400).
#       --command: no desktop; the command is typed at the serial root shell and the run ends
#       after it (and a sync); it passes when the command printed QDINSTALL_DONE
#       (e.g. --command 'ls /Users/sf; echo QDINSTALL_DONE')
#   tools/qemu_install.sh update [--bin FILE] [--terminal FILE]
#       replaces the compositor/terminal/session files on the existing disk, keeps everything else
#   Source: upstream's tip, the default of integrations/siliconforest/rebase/pd.nix; SF_PD=pin in
#       the environment of `create` makes the disk from the old pin e7aa00a6 instead. The first
#       `create --variant wayland` of a source builds upstream's full image (about 100
#       derivations, an arm64 LLVM among them: hours; QD_NIX_ARGS, default `--max-jobs 1
#       --cores 6`, are nix-build's limits for it).
#   The wayland variant also gets what programs built for macOS expect and upstream's image
#       lacks (QD_EXTRAS=0 leaves it out): the libraries, frameworks' additions and locale
#       data of integrations/siliconforest/homebrew/build-overlay.nix, /usr/bin/shasum,
#       /usr/bin/sw_vers and /usr/bin/curl at the macOS paths.
#   tools/qemu_install.sh reset     deletes the disk (create makes a fresh one)
#   tools/qemu_install.sh status    where the disk is, its size, the last boot's result
# Other streams: the disk is runs/qemu-dev/install/disk.img (GPT: 1 = ESP, 2 = ext4 root,
# read-write, boot-uuid in the image); boot it yourself with
#   integrations/siliconforest/qemu-dev/qd_run.py --image runs/qemu-dev/install/disk.img --out DIR --command ...
# One QEMU at a time per disk: `boot` takes runs/qemu-dev/install/lock.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
Q=integrations/siliconforest/qemu-dev; D=runs/qemu-dev/install; DISK=$D/disk.img
BIN=runs/m1-desktop-010/wm-darwin-scale-desktop-build-007/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
TERMINAL=runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
CMD=${1:-status}; shift || true
VARIANT=minimal; FORCE=0; WINDOW=0; SECS=; COMMAND=; RUN_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --variant) VARIANT=$2; shift;; --bin) BIN=$2; shift;; --terminal) TERMINAL=$2; shift;;
    --force) FORCE=1;; --window) WINDOW=1;; --seconds) SECS=$2; shift;; --command) COMMAND=$2; shift;;
    --smp|--memory|--timeout|--shot-every|--qemu-arg) RUN_ARGS+=("$1" "$2"); shift;;
    -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d' | cut -c3-; exit 0;;
    *) echo "unknown option $1 (see --help)" >&2; exit 2;;
  esac; shift
done
session() {   # the desktop session script for the installed system: by hand, no time limit that matters
  sed -e "s/@SECONDS@/${1}/g" -e "s/@AUTOPILOT@/0/g" -e "s/@SCALE@//g" -e "s/@THEME@//g" -e "s/@ONLY@//g" \
      -e "s/@STATS@/3600/g" -e "s/@SOUND@/1/g" -e "s/@RENDERER@/cpu/g" -e "s/@TERMINALS@/1/g" -e "s/@FETCH@/0/g" -e "s/@PERSIST@/1/g" -e "s|@ENV@||g" \
      "$Q/guest/qemu-desktop-session.in" > "$D/qemu-desktop-session"
}
fsck() {      # the guest is often stopped without a clean unmount: repair before every use
  python3 -B "$Q/qd_image.py" --fsck "$DISK" > "$D/fsck.json" || exit 2
}
case "$CMD" in
create)
  [ ! -e "$DISK" ] || [ "$FORCE" = 1 ] || { echo "$DISK exists (reset first, or --force)" >&2; exit 2; }
  [ -x "$BIN" ] && [ -x "$TERMINAL" ] || { echo "compositor or terminal binary missing" >&2; exit 2; }
  mkdir -p "$D"; rm -f "$DISK"
  EXTRAS=()
  if [ "$VARIANT" = wayland ]; then
    export PATH=$HOME/.nix-profile/bin:$PATH NIX_CONFIG='build-users-group = '
    NIXB=(nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link ${QD_NIX_ARGS:---max-jobs 1 --cores 6})
    # upstream's full image for this source: built here, with limits, before qd_image.py asks for it
    (cd "$Q" && "${NIXB[@]}" build-image.nix --argstr variant wayland) > "$D/base-build.log" 2>&1 \
      || { tail -5 "$D/base-build.log" >&2; echo "the full image did not build ($D/base-build.log)" >&2; exit 2; }
    if [ "${QD_EXTRAS:-1}" = 1 ]; then
      OVERLAY=$(cd integrations/siliconforest/homebrew && "${NIXB[@]}" build-overlay.nix 2> "$ROOT/$D/overlay-build.log" | tail -1)
      [ -d "$OVERLAY/usr/lib" ] || { echo "the Homebrew overlay did not build ($D/overlay-build.log)" >&2; exit 2; }
      # The overlay without its two diagnostics, and without its curl: the full image has its own.
      rm -rf "$D/extras"; mkdir -p "$D/extras"; cp -a "$OVERLAY/." "$D/extras/"; chmod -R u+w "$D/extras"
      rm -f "$D/extras/usr/lib/libhbcrash.dylib" "$D/extras/usr/bin/hb-runas" "$D/extras/usr/bin/curl"
      EXTRAS=(--overlay "$D/extras" --file "$Q/guest/shasum=/usr/bin/shasum" --link /usr/bin/sw_vers=/bin/sw_vers --link /usr/bin/curl=/bin/curl)
    fi
  fi
  session 14400
  # Accounts: root as upstream has it, plus `sf` (uid 501, group staff). Passwords are unset
  # (`*`): there is no login program in this userland; the console shell is root's.
  python3 -B - "$Q" "$D" "$VARIANT" <<'E'
import sys
sys.path.insert(0, sys.argv[1])
import qd_image
from pathlib import Path
d = Path(sys.argv[2])
class A: kernel = 'release'; variant = sys.argv[3]; mesa = False; libsystem = 'homebrew'; verbose_boot = False; bootargs = ''; resolution = '1024x768'; time_waits = (1, 2)
base = qd_image.template(A)
def plus(path, line, name):
    text = qd_image.read_root(base, path).decode()
    if not any(l.startswith(line.split(':')[0] + ':') for l in text.splitlines()):
        text = text.rstrip('\n') + '\n' + line + '\n'
    (d / name).write_text(text)
plus('/etc/passwd', 'sf:*:501:20:SiliconForest User:/Users/sf:/bin/zsh', 'passwd')
plus('/etc/master.passwd', 'sf:*:501:20::0:0:SiliconForest User:/Users/sf:/bin/zsh', 'master.passwd')
(d / 'welcome.txt').write_text('SiliconForest in QEMU: this home directory is on the persistent disk.\n')
E
  REMOVE=(); [ "$VARIANT" = wayland ] && REMOVE=(--remove /System/Library/LaunchDaemons/org.siliconforest.wayland-session.plist)
  python3 -B "$Q/qd_image.py" --output "$DISK" --variant "$VARIANT" --libsystem homebrew \
    --file "$BIN=/usr/bin/chonkstep-darwin-desktop" --file "$TERMINAL=/usr/bin/chonk-terminal-darwin" \
    --file "$D/qemu-desktop-session=/usr/bin/qemu-desktop-session" \
    --data "$Q/guest/org.siliconforest.qemu-desktop.plist=/System/Library/LaunchDaemons/org.siliconforest.qemu-desktop.plist" \
    --data "$D/passwd=/etc/passwd" --data "$D/master.passwd=/etc/master.passwd" \
    --dir /Users/sf:501:20 --data "$D/welcome.txt=/Users/sf/README.txt" "${EXTRAS[@]}" "${REMOVE[@]}" > "$D/image.json"
  echo "$VARIANT" > "$D/variant"; echo "${SF_PD:-tip}" > "$D/source"
  echo "created $DISK ($(du -h --apparent-size "$DISK" | cut -f1), variant $VARIANT)";;
update)
  [ -e "$DISK" ] || { echo "no disk: tools/qemu_install.sh create" >&2; exit 2; }
  session 14400; fsck
  python3 -B - "$Q" "$DISK" "$BIN" "$TERMINAL" "$D/qemu-desktop-session" <<'E'
import sys
sys.path.insert(0, sys.argv[1])
import qd_image
from pathlib import Path
qd_image.write_root(Path(sys.argv[2]), {'/usr/bin/chonkstep-darwin-desktop': (Path(sys.argv[3]).resolve(strict=True), True),
                                        '/usr/bin/chonk-terminal-darwin': (Path(sys.argv[4]).resolve(strict=True), True),
                                        '/usr/bin/qemu-desktop-session': (Path(sys.argv[5]).resolve(strict=True), True)})
E
  echo "updated the desktop files on $DISK";;
boot)
  [ -e "$DISK" ] || { echo "no disk: tools/qemu_install.sh create" >&2; exit 2; }
  exec 9> "$D/lock"; flock -n 9 || { echo "$DISK is in use by another boot" >&2; exit 2; }
  fsck
  OUT=$D/boot; mkdir -p "$OUT"
  if [ -n "$COMMAND" ]; then
    # No desktop for this boot: launchd's session job finds a script that does nothing.
    printf '#!/bin/sh\nexit 0\n' > "$D/qemu-desktop-session"
    python3 -B -c "import sys; sys.path.insert(0, '$Q'); import qd_image; from pathlib import Path
qd_image.write_root(Path('$DISK'), {'/usr/bin/qemu-desktop-session': (Path('$D/qemu-desktop-session').resolve(strict=True), True)})"
    python3 -B "$Q/qd_run.py" --image "$DISK" --out "$OUT" --command "$COMMAND; sync; printf 'QDINSTALL_%s\\n' SYNCED" \
      --begin 'root@' --up 'root@' --end 'QDINSTALL_SYNCED' --passed 'QDINSTALL_DONE[\r\n]' --shot-every 0 --timeout 900 "${RUN_ARGS[@]}"
  else
    [ -n "$SECS" ] || { [ "$WINDOW" = 1 ] && SECS=14400 || SECS=120; }
    W=(); [ "$WINDOW" = 1 ] && W=(--window --pointer tablet)
    session "$SECS"
    python3 -B - "$Q" "$DISK" "$D/qemu-desktop-session" <<'E'
import sys
sys.path.insert(0, sys.argv[1])
import qd_image
from pathlib import Path
qd_image.write_root(Path(sys.argv[2]), {'/usr/bin/qemu-desktop-session': (Path(sys.argv[3]).resolve(strict=True), True)})
E
    python3 -B "$Q/qd_run.py" --image "$DISK" --out "$OUT" --input-seen QDDESK_INPUT --passed QDDESK_PASS \
      --timeout $((SECS + 300)) "${W[@]}" "${RUN_ARGS[@]}"
    "$Q/qd_summary.sh" "$OUT" || true
  fi;;
reset)
  rm -f "$DISK" "$D/variant"; rm -rf "$D/boot"; echo "removed $DISK";;
status)
  if [ -e "$DISK" ]; then
    echo "disk: $DISK ($(du -h "$DISK" | cut -f1) used on the host, variant $(cat "$D/variant" 2>/dev/null || echo unknown), source $(cat "$D/source" 2>/dev/null || echo pin))"
    [ -r "$D/boot/result.json" ] && grep -m1 '"status"' "$D/boot/result.json"
  else echo "no disk: tools/qemu_install.sh create"; fi;;
*) echo "usage: tools/qemu_install.sh create|boot|update|reset|status (see --help)" >&2; exit 2;;
esac
