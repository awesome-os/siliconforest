#!/bin/bash
# Create the roadmap issues from docs/roadmap/issues/*.md in chriskearney/siliconforest.
# Waits for the GitHub write limit, paces requests, retries on 403, and records the numbers
# in docs/roadmap/issues/NUMBERS.tsv. Safe to re-run: tickets already in NUMBERS.tsv are skipped.
set -u
cd "$(dirname "$0")/.."
R=chriskearney/siliconforest; D=docs/roadmap/issues; N=$D/NUMBERS.tsv; touch $N
WAIT_UNTIL=${1:-0}
now=$(date +%s); [ "$WAIT_UNTIL" -gt "$now" ] && sleep $((WAIT_UNTIL - now + 20))
try() { # retry a gh command on rate limiting
  for i in 1 2 3 4 5 6 7 8; do
    out=$("$@" 2>&1) && { echo "$out"; return 0; }
    echo "$out" | grep -qi 'rate limit\|secondary' || { echo "$out" >&2; return 1; }
    sleep 90
  done; return 1
}
for l in "roadmap:5319e7:Roadmap ticket with decisions and work items" "area-platform:1d76db:Shared Apple platform services" "area-cpu-boot:0e8a16:CPUs, kernel build, boot experience" "area-graphics:b60205:GPU, Mesa, compositor, display" "area-audio:d93f0b:Sound" "area-wireless:0052cc:Wi-Fi and Bluetooth" "area-power:fbca04:Sleep and power management" "area-usb-tb:c5def5:USB-C, docks, Thunderbolt" "area-bench:bfdadc:Test rig and harness"; do
  IFS=: read n c d <<< "$l"; try gh label create "$n" -R $R --color "$c" --description "$d" --force >/dev/null; sleep 4
done
create() { # code file title labels
  code=$1; file=$2; title=$3; labels=$4
  grep -q "^$code	" $N && return 0
  body=$(mktemp)
  if [ "$code" != SF-0 ]; then
    idx=$(awk -F'\t' '$1=="SF-0"{print $2}' $N)
    printf 'Part of the roadmap: #%s. Ticket code: **%s**. Research and conventions are linked from the index.\n\n' "$idx" "$code" > $body
  fi
  cat $D/$file >> $body
  url=$(try gh issue create -R $R --title "$title" --label "$labels" --body-file $body) || { echo "FAILED $code"; return 1; }
  num=${url##*/}; printf '%s\t%s\t%s\n' "$code" "$num" "$title" >> $N; echo "$code -> #$num"; rm -f $body; sleep 8
}
create SF-0 SF-0-index.md "Roadmap index: start here" "roadmap" || exit 1
create SF-A SF-A-platform.md "[SF-A] Shared Apple platform services: PMGR, RTKit/SMC, DART, GPIO, I2C" "roadmap,area-platform"
create SF-B SF-B-bench.md "[SF-B] Test bench and harness: boot queue, long sessions, multi-CPU and unattended boots" "roadmap,area-bench"
create SF-C SF-C-cpu.md "[SF-C] Full CPU speed and all eight cores" "roadmap,area-cpu-boot"
create SF-D SF-D-boot.md "[SF-D] Optimised kernel, quiet boot with our logo, multi-user boot" "roadmap,area-cpu-boot"
create SF-E SF-E-compositor-cpu.md "[SF-E] Responsive desktop on the CPU: damage tracking in the compositor" "roadmap,area-graphics"
create SF-F SF-F-gpu-kernel.md "[SF-F] GPU kernel driver: Omacom's Rust driver on our Linux compatibility layer" "roadmap,area-graphics"
create SF-G SF-G-gpu-userspace.md "[SF-G] GPU userspace: Omacom Mesa and a GL renderer in the compositor" "roadmap,area-graphics"
create SF-H SF-H-display.md "[SF-H] Display controller: vsync, page flips, brightness, display sleep" "roadmap,area-graphics"
create SF-I SF-I-sound.md "[SF-I] Sound: headphones, then speakers behind a safety cap, then microphones" "roadmap,area-audio"
create SF-J SF-J-bluetooth.md "[SF-J] Bluetooth: BCM4378 controller driver and a userspace host stack" "roadmap,area-wireless"
create SF-K SF-K-sleep.md "[SF-K] Sleep and power: idle sleep on lid close, then real suspend" "roadmap,area-power"
create SF-L SF-L-usbc-docks.md "[SF-L] USB-C, docks and Thunderbolt in four stages" "roadmap,area-usb-tb"
create SF-M SF-M-wifi.md "[SF-M] Wi-Fi hardening: firmware stall, soak, multi-CPU, WPA3, tree patches" "roadmap,area-wireless"
# Index body with issue numbers in the table
idx=$(awk -F'\t' '$1=="SF-0"{print $2}' $N); body=$(mktemp); cp $D/SF-0-index.md $body
while IFS=$'\t' read code num title; do [ "$code" = SF-0 ] && continue; sed -i "s/^| $code |/| $code (#$num) |/" $body; done < $N
try gh issue edit "$idx" -R $R --body-file $body >/dev/null && echo "index #$idx updated"
try gh issue pin "$idx" -R $R >/dev/null 2>&1 && echo "index pinned"
cat $N
