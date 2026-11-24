#!/bin/sh
# SPDX-License-Identifier: MIT
# The SiliconForest rename on an installed system: which files of a root still carry an old
# lookup (a device node, a sysctl, an IOKit class, a Mach service, a launchd label, a tool name
# of before the rename), and whether the staged manifest accounts for each of them. Read-only.
#
#   sf_rename_scan.sh [--root DIR] [--out FILE] [STAGE]
#
#   --root   the system's root (default /); a mounted SiliconForest partition works the same
#   --out    where the full list goes (default: standard output only)
#   STAGE    a staged directory (manifest.tsv). Without it every hit is printed as "old"
#
# One line per file that has an old name in its bytes:
#   row   PATH   a row of the manifest installs over it, removes it, sweeps it or keeps it (also the
#                source of a migrate row). What a keep row names is not searched at all, file or directory
#   OPEN  PATH   nothing in the manifest knows this file: decide before the switch
# Before the switch OPEN must be empty or explained; after the switch the scan of the whole root
# should print only files that are not SiliconForest's (Apple's two applications, Firefox's own
# bundle, Homebrew bottles) and the kept old files under /private/var/db.
# Text that only credits PureDarwin (NOTICE, licence files) matches too: such files are listed,
# they are not errors. The scan looks at file bytes, not at what a program does with them.
set -u
ROOT=/; OUT=; STAGE=
while [ $# -gt 0 ]; do
  case "$1" in
    --root) ROOT=$2; shift;;
    --out) OUT=$2; shift;;
    -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d' | cut -c3-; exit 0;;
    -*) echo "sf_rename_scan: unknown option $1" >&2; exit 2;;
    *) STAGE=$1;;
  esac
  shift
done
ROOT=${ROOT%/}
[ -d "$ROOT/" ] || { echo "sf_rename_scan: no root $ROOT/" >&2; exit 2; }
[ -z "$STAGE" ] || [ -f "$STAGE/manifest.tsv" ] || { echo "sf_rename_scan: STAGE needs manifest.tsv" >&2; exit 2; }
TAB=$(printf '\t')
# the old lookups (the same list as the stage's own strings check)
PAT='org\.puredarwin\.|/dev/pd[a-z]|kern\.pd_|machdep\.pd|hw\.pd[a-z]|PD(DRM|Apple|M1|Bwfm|Dcp|Agx|Cpu|Aop|GOP|HID|IOSurface|Gpu|Arm|Surface|NVMe|Nvme|Audio)[A-Za-z0-9]|pd_window_position|pd(m1|avd|agx|nvme|dcp|gpu|disp|cpu|text|apcie|speaker)[a-z0-9_]*=|pd-(netd|dhcp|net-up|backlight|wifipm|console-login|set-hostname|dmesg-save)|pd_(cgl|vk|dri|drm|avd|sound|speaker|backlight|touchbar|trackpad|dcp|agx|nvme|hid|gop|libdyld)_|AudioToolbox\.output-pd|/usr/bin/pd(tone|volume|play|video)|puredarwin(gop|input)_drv'
T=$(mktemp "${TMPDIR:-/tmp}/sf-rename-scan.XXXXXX") || exit 1
: > "$T.rows"; : > "$T.dirs"; : > "$T.sweep"
if [ -n "$STAGE" ]; then
  grep -v '^#' "$STAGE/manifest.tsv" | grep -v '^$' | while IFS=$TAB read -r act path old new mode class note; do
    printf '%s\n' "$path" >> "$T.rows"
    case "$act" in
      keep) printf '%s\n' "$path" >> "$T.dirs";;
      sweep) printf '%s\n' "$path" >> "$T.sweep";;
      migrate) from=${note#from=}; printf '%s\n' "${from%%;sed=*}" >> "$T.rows";;
    esac
  done
fi
# not searched: other file systems, the kept files of this rename, device nodes, and what a keep row names (a
# directory that is not ours or is left on purpose: Homebrew's bottles, Apple's bundles, logs, retained runtimes)
set -- -path "$ROOT/private/var/db/sf-rename" -o -path "$ROOT/private/var/db/sf-audio-pretest" -o -path "$ROOT/dev" -o -path "$ROOT/nix"
while IFS= read -r d; do set -- "$@" -o -path "$ROOT$d"; done < "$T.dirs"
# one grep for many files (xargs), not one per file; /dev/null keeps grep from reading its input when find gives nothing.
# The size limit is in k: toybox's find (SiliconForest's) takes a bare number another way than GNU's
# perl, where there is one (the M1 has /usr/bin/perl): one process per batch of files reads each file whole and tries
# the pattern once; some hundred MiB a second. Without perl (the QEMU guest): grep, in two passes (below).
if command -v perl > /dev/null 2>&1 && perl -e 1 2>/dev/null; then
  find "$ROOT/" -xdev \( "$@" \) -prune -o -type f -size -409600k -print0 2>/dev/null |
    SF_SCAN_PAT=$PAT xargs -0 perl -e 'my $re = qr/$ENV{SF_SCAN_PAT}/; undef $/;
      for my $f (@ARGV) { open(my $h, "<", $f) or next; binmode $h; my $d = <$h>; close $h; print "$f\n" if defined $d && $d =~ $re; }' > "$T.hits" 2>/dev/null
else
# Two passes: fixed strings first (fast, and a superset of the pattern), the pattern itself only on those files
  cat > "$T.fixed" <<'FIXED'
org.puredarwin.
/dev/pd
kern.pd_
machdep.pd
hw.pd
PDDRM
PDApple
PDM1
PDBwfm
PDDcp
PDAgx
PDCpu
PDAop
PDGOP
PDHID
PDIOSurface
PDGpu
PDArm
PDSurface
PDNVMe
PDNvme
PDAudio
pd_window_position
pdm1
pdavd
pdagx
pdnvme
pddcp
pdgpu
pddisp
pdcpu
pdtext
pdapcie
pdspeaker
pd-netd
pd-dhcp
pd-net-up
pd-backlight
pd-wifipm
pd-console-login
pd-set-hostname
pd-dmesg-save
pd_cgl_
pd_vk_
pd_dri_
pd_drm_
pd_avd_
pd_sound_
pd_speaker_
pd_backlight_
pd_touchbar_
pd_trackpad_
pd_dcp_
pd_agx_
pd_nvme_
pd_hid_
pd_gop_
pd_libdyld_
AudioToolbox.output-pd
/usr/bin/pdtone
/usr/bin/pdvolume
/usr/bin/pdplay
/usr/bin/pdvideo
puredarwingop_drv
puredarwininput_drv
FIXED
  find "$ROOT/" -xdev \( "$@" \) -prune -o -type f -size -409600k -print0 2>/dev/null |
    xargs -0 grep -a -l -F -f "$T.fixed" /dev/null 2>/dev/null > "$T.first"
  : > "$T.hits"
  while IFS= read -r f; do grep -a -q -E "$PAT" "$f" 2>/dev/null && printf '%s\n' "$f" >> "$T.hits"; done < "$T.first"
fi
: > "$T"
while IFS= read -r f; do
  p=${f#"$ROOT"}
  hit=0; grep -q -x -F "$p" "$T.rows" && hit=1
  [ $hit = 1 ] || while IFS= read -r pre; do case "$p" in "$pre"*) hit=1; break;; esac; done < "$T.sweep"   # a sweep row takes it
  if [ $hit = 1 ]; then printf 'row\t%s\n' "$p" >> "$T"; else printf 'OPEN\t%s\n' "$p" >> "$T"; fi
done < "$T.hits"
[ -z "$OUT" ] || cp "$T" "$OUT"
grep "^OPEN" "$T"
[ -s "$T.dirs" ] && echo "not searched (keep rows): $(tr '\n' ' ' < "$T.dirs")"
echo "scanned ${ROOT:-/}: $(grep -c . "$T.hits") files with an old name in their bytes: $(grep -c '^row' "$T") that the manifest accounts for, $(grep -c '^OPEN' "$T") that it does not"
n=$(grep -c '^OPEN' "$T"); rm -f "$T" "$T.rows" "$T.dirs" "$T.sweep" "$T.hits" "$T.first" "$T.fixed"
[ "$n" = 0 ]
