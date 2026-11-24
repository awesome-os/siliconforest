#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 with coreaudiod, the audio frameworks and the test tools staged into the RAM
# root and a guest script from ../guest, through tools/pcie_iterate.sh, which queues for the bench
# itself: do not wrap it. Nothing shared is written; the collection is handed over with PCIE_KC.
# Usage: sa_boot.sh <name, sd20..sd39> [guest template, default ../guest/darwin-sysaudio-test.in]
#   SD_KC        collection store path (default: the collection the path was proven with; build
#                ../../build-kc.nix for the current kext)
#   SD_FILES     further files for the RAM root: "source=/destination;..."
#   SD_RUNTIME   upper bound of the guest's run in seconds, at most 600 (default 300)
#   SD_BOOTARGS  further kernel boot arguments; SD_CPUS guest processors (default 8)
#   SD_VOLUME    codec attenuation in dB at the start (default 30); SD_TONEDB tone level in dB
#                below full scale on the headphones (default 20); SD_SPKDB the same for what may
#                reach the speakers (default 30, not below)
#   SD_AUDIO     tip or pin: the source the audio server and tools are built from
#   SD_WAITS     fast (default): sf_quiet_wait=0 sf_rtc_wait=0 in the boot-args, the kernel does not
#                wait for a clock or a quiet registry before the root mounts (the guest scripts poll
#                for the drivers); stock: the kernel's waits
#   SD_TIMEWAIT  PCIE_TIMEWAIT of tools/pcie_iterate.sh (unset by default: its instruction pattern
#                does not match the tip's kernel; for the pin's with SD_WAITS=stock)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-sound}
# The audio server, frameworks and tools are built from the source ../../rebase/pd.nix selects (the tip);
# SD_AUDIO=pin stages the builds of the pinned revision instead (they run on the tip's kernel and libSystem).
AUDIO_PD=${SD_AUDIO:-tip}; [ "$AUDIO_PD" = tip ] && AUDIO_PD=
N=${1:?name}
GUEST=${2:-$HERE/guest/darwin-sysaudio-test.in}
case "$N" in sd2[0-9]|sd3[0-9]) ;; *) echo "boot names for the system audio path are sd20 to sd39" >&2; exit 2;; esac
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot $N exists" >&2; exit 2; }
[ ! -e runs/bench.finisher.failed ] || { echo "runs/bench.finisher.failed exists: the bench needs a person" >&2; exit 2; }
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes"
KC=${SD_KC:-$("$ROOT/tools/default_kc.sh")}
SPKDB=${SD_SPKDB:-30}
[ "$SPKDB" -ge 30 ] || { echo "SD_SPKDB: 30 dB below full scale or quieter" >&2; exit 2; }
EXTRA=${SD_FILES:-}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
A=$(cd "$HERE" && SF_PD=$AUDIO_PD $NIX build-sysaudio.nix -A all --no-out-link 2> "${TMPDIR:-/tmp}/sa-build.$$.log" | tail -1)
[ -x "$A/usr/sbin/coreaudiod" ] || { tail -30 "${TMPDIR:-/tmp}/sa-build.$$.log" >&2; echo "build failed" >&2; exit 2; }
rm -f "${TMPDIR:-/tmp}/sa-build.$$.log"
RUNTIME=${SD_RUNTIME:-300}
[ "$RUNTIME" -le 600 ] || { echo "boots are capped at 600 s" >&2; exit 2; }
W=$(mktemp -d)
python3 -B - "$GUEST" "$HERE/guest/sysaudio-common.sh" "$W/guest" "${SD_VOLUME:-30}" "${SD_TONEDB:-20}" "$SPKDB" <<'PY'
import sys
guest, common, out, volume, tonedb, spkdb = sys.argv[1:]
text = open(guest).read().replace('@COMMON@', open(common).read())
open(out, 'w').write(text.replace('@VOLUME@', volume).replace('@TONEDB@', tonedb).replace('@SPKDB@', spkdb))
PY
# raw frames for pcmplay: 2 s of 1 kHz, 16 bit stereo, 48 kHz, 30 dB below full scale
python3 -B - "$W/test.pcm" <<'PY'
import math, struct, sys
amp = 32767 * 10 ** (-30 / 20)
with open(sys.argv[1], 'wb') as f:
    for n in range(2 * 48000):
        s = int(amp * math.sin(2 * math.pi * 1000 * n / 48000))
        f.write(struct.pack('<hh', s, s))
PY
FW=System/Library/Frameworks
FILES="$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/pcmplay=/usr/bin/pcmplay"
FILES="$FILES;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume"
for f in CoreAudio AudioToolbox AudioUnit; do
    FILES="$FILES;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"
done
FILES="$FILES;$W/test.pcm=/usr/share/sysaudio-test.pcm${EXTRA:+;$EXTRA}"
FAST="sf_quiet_wait=0 sf_rtc_wait=0"; [ "${SD_WAITS:-fast}" = stock ] && FAST=
env ${SD_TIMEWAIT:+PCIE_TIMEWAIT="$SD_TIMEWAIT"} PCIE_KC="$KC" PCIE_BOOTARGS="cpus=${SD_CPUS:-8} $FAST ${SD_BOOTARGS:-}" \
  PCIE_GUEST_TEMPLATE="$W/guest" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="$FILES" LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
  grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space\|BENCH_TIMES" | cut -c1-240 || true
rm -rf "$W"
R=runs/m1-desktop-010/pcie-enum-attempt-$N
grep -o '"status": *"[^"]*"' "$R/result.json" 2>/dev/null || echo "no result.json: check the M1 before anything else"
[ ! -e runs/bench.finisher.failed ] || echo "runs/bench.finisher.failed exists: STOP, the bench needs a person"
"$HERE/tools/sa_summary.sh" "$R/guest/console.bin"
