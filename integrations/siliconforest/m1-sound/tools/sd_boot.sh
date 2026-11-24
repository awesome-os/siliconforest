#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 with the SFAppleSound collection and a guest script, through
# tools/pcie_iterate.sh, which queues for the bench itself: do not wrap it. The collection
# is handed over with PCIE_KC; nothing shared is written.
# Usage: sd_boot.sh <name, prefix sd>
#   SD_KC        collection store path (default: build ../build-kc.nix)
#   SD_GUEST     guest script template (default guest/darwin-sound-test.in)
#   SD_RUNTIME   upper bound of the guest's run in seconds, at most 600 (default 300)
#   SD_BOOTARGS  further kernel boot arguments (sfsound=, sfsound_tone=, sfsound_fe=, ...)
#   SD_CPUS      guest processors (default 8)
#   SD_VOLUME    codec attenuation in dB for the test (default 30); SD_TONEDB tone level in
#                dB below full scale (default 20); SD_SECONDS length of each tone (default 5);
#                SD_CYCLES start/stop cycles of guest/darwin-sound-stress.in (default 20)
#   SD_SPKDB     level of the speaker test's tone, dB below full scale (default 22, not louder;
#                the test keeps the amplifiers another 20 dB down)
#   SD_FILES     further files for the RAM root: "source=/destination;..."
#   SD_AUDIO     tip or pin: the source sfplay is built from
#   SD_WAITS     fast (default): sf_quiet_wait=0 sf_rtc_wait=0 in the boot-args, the kernel does not
#                wait for a clock or a quiet registry before the root mounts (the guest scripts poll
#                for the drivers); stock: the kernel's waits
#   SD_TIMEWAIT  PCIE_TIMEWAIT of tools/pcie_iterate.sh (unset by default: its instruction pattern
#                does not match the tip's kernel; for the pin's with SD_WAITS=stock)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-sound}
# sfplay is built from the source ../rebase/pd.nix selects (the tip); SD_AUDIO=pin stages the build of
# the pinned revision instead (it runs on the tip's kernel and libSystem).
AUDIO_PD=${SD_AUDIO:-tip}; [ "$AUDIO_PD" = tip ] && AUDIO_PD=
N=${1:?name}
case "$N" in sd*) ;; *) echo "boot names are prefixed sd" >&2; exit 2;; esac
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes"
KC=${SD_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { tail -30 "${TMPDIR:-/tmp}/sd-kc-build.$$.log" >&2; echo "no collection" >&2; exit 2; }
rm -f "${TMPDIR:-/tmp}/sd-kc-build.$$.log"
PLAY=$(cd "$HERE" && SF_PD=$AUDIO_PD $NIX build-sfplay.nix --no-out-link 2>/dev/null | tail -1)
RUNTIME=${SD_RUNTIME:-300}
[ "$RUNTIME" -le 600 ] || { echo "boots are capped at 600 s" >&2; exit 2; }
# the speaker test's tone level; the script holds the volume another 20 dB down (hold_probe)
SPKDB=${SD_SPKDB:-22}
[ "$SPKDB" -ge 22 ] || { echo "SD_SPKDB: 22 dB below full scale or quieter" >&2; exit 2; }
W=$(mktemp -d)
sed -e "s/@VOLUME@/${SD_VOLUME:-30}/g" -e "s/@TONEDB@/${SD_TONEDB:-20}/g" -e "s/@SECONDS@/${SD_SECONDS:-5}/g" \
    -e "s/@CYCLES@/${SD_CYCLES:-20}/g" -e "s/@SPKDB@/$SPKDB/g" \
    "${SD_GUEST:-$HERE/guest/darwin-sound-test.in}" > "$W/guest"
# a WAVE file for sfplay: 1 s of 1 kHz left, 1 s right, 16 bit, 44.1 kHz, 30 dB below full scale
python3 -B - "$W/test.wav" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1], 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
amp = 32767 * 10 ** (-30 / 20)
for n in range(2 * 44100):
    s = int(amp * math.sin(2 * math.pi * 1000 * n / 44100))
    w.writeframes(struct.pack('<hh', s if n < 44100 else 0, 0 if n < 44100 else s))
w.close()
PY
FILES="$PLAY/usr/bin/sfplay=/usr/bin/sfplay;$W/test.wav=/usr/share/sfsound-test.wav${SD_FILES:+;$SD_FILES}"
FAST="sf_quiet_wait=0 sf_rtc_wait=0"; [ "${SD_WAITS:-fast}" = stock ] && FAST=
env ${SD_TIMEWAIT:+PCIE_TIMEWAIT="$SD_TIMEWAIT"} PCIE_KC="$KC" PCIE_BOOTARGS="cpus=${SD_CPUS:-8} $FAST ${SD_BOOTARGS:-}" \
  PCIE_GUEST_TEMPLATE="$W/guest" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="$FILES" LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
  grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space\|BENCH_TIMES" | cut -c1-240 || true
rm -rf "$W"
"$HERE/tools/sd_summary.sh" "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin"
