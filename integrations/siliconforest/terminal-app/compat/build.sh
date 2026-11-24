#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the macOS-application compatibility libraries of this directory.
#   build.sh <out> : a tree rooted at / with
#     wrappers  <framework path>          = additions + LC_REEXPORT of the real library, which the
#                                           installer keeps beside it as <framework path>.pd
#                                           (CoreFoundation and Foundation re-export /usr/lib/lib*.dylib,
#                                           as upstream's framework-compat does, and need no .pd)
#     new       ColorSync, DataDetectorsCore, CoreAnalytics, libScreenReader
#     with Apple's Activity Monitor's additions ($AM, default ../../activity-monitor/libs;
#     docs/roadmap/issues/SF-AA-activity-monitor.md) in the CoreFoundation, Foundation and CoreServices
#     wrappers, and IOKit's wrapper (at that path the root has a link to /usr/lib/libIOKitCF.dylib, as
#     for CoreFoundation: it re-exports the library and needs no .pd)
# Environment: CC (cross clang), LD (ld for -fuse-ld), SDK, LINKROOT (a root holding the real
# libraries at their install paths, to link against), INC (extra -I flags), PY (python3), HB (directory of hb_macho.py).
set -euo pipefail
OUT=${1:?out}; S=$(cd "$(dirname "$0")" && pwd); AM=${AM:-$S/../../activity-monitor/libs}; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
F=/System/Library/Frameworks; PF=/System/Library/PrivateFrameworks
CF="-isysroot $SDK -mmacosx-version-min=26.5 -O2 -Wall -Wno-deprecated-declarations -Wno-unused-parameter -Wno-nullability-completeness -Wno-objc-root-class -fno-objc-arc -fobjc-exceptions -fblocks -D_DARWIN_C_SOURCE -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $INC"
LF="-fuse-ld=$LD -nostdlib -isysroot $SDK -Wl,-syslibroot,$LINKROOT -mmacosx-version-min=26.5 -dynamiclib -Wl,-platform_version,macos,26.5,26.5 -Wl,-headerpad_max_install_names -L$LINKROOT/usr/lib"
objs() { local o= i=0; for s in "$@"; do i=$((i+1)); local b=$W/$i-$(basename "$s").o; case $s in *.m) $CC $CF -x objective-c -c "$s" -o "$b";; *) $CC $CF -c "$s" -o "$b";; esac; o="$o $b"; done; echo "$o"; }
# lib <install path> <compat.current version> <sources> -- <link flags>
lib() { local p=$1 v=$2; shift 2; local src=(); while [ "$1" != -- ]; do src+=("$1"); shift; done; shift
  mkdir -p "$OUT$(dirname "$p")"
  $CC $LF -install_name "$p" -compatibility_version ${v%%:*} -current_version ${v##*:} $(objs "${src[@]}") "$@" -lSystem -o "$OUT$p"; }
# wrap <framework> <version dir> <compat:current> <sources> -- <link flags>: the real library is $LINKROOT<path>
wrap() { local n=$1 ver=$2 v=$3; shift 3; local p=$F/$n.framework/Versions/$ver/$n
  local src=(); while [ "$1" != -- ]; do src+=("$1"); shift; done; shift
  # dyld keeps one image per install name: the wrapper cannot carry the name of the library it
  # re-exports, so it is built under <path>.sfcompat (dyld does not compare a file's name with the
  # path it was asked for) and put at <path>
  lib "$p.sfcompat" "$v" "${src[@]}" -- -Wl,-reexport_library,"$LINKROOT$p" "$@"
  mv "$OUT$p.sfcompat" "$OUT$p"
  $PY -B - "$OUT$p" "$p" <<'PYEOF'
import sys
sys.path.insert(0, __import__('os').environ['HB'])
import hb_macho
f, p = sys.argv[1:3]
d = open(f, 'rb').read()
m = hb_macho.MachO(d)
# the re-exported real library is the one command (besides the id) that carries this path
out = bytearray(); n = 0
import struct
for cmd, off, size in m.commands:
    raw = d[off:off + size]
    if cmd == hb_macho.LC_REEXPORT_DYLIB and hb_macho.cstr(raw, struct.unpack_from('<I', raw, 8)[0]) == p:
        noff = struct.unpack_from('<I', raw, 8)[0]
        body = raw[:noff] + (p + '.pd').encode() + b'\0'; body += b'\0' * (-len(body) % 8)
        raw = body[:4] + struct.pack('<I', len(body)) + body[8:]; n += 1
    out += raw
assert n == 1, 'expected one re-export of %s, found %d' % (p, n)
limit = m.first_section if m.first_section is not None else 32 + m.sizeofcmds
assert 32 + len(out) <= limit, 'load commands do not fit'
new = bytearray(d); new[32:32 + max(len(out), m.sizeofcmds)] = bytes(out) + b'\0' * max(0, m.sizeofcmds - len(out))
struct.pack_into('<I', new, 20, len(out))
new, _c, _note = hb_macho.resign(bytes(new))
open(f, 'wb').write(new)
PYEOF
}
OBJC="-lobjc -lFoundation -lCoreFoundation"
# CoreFoundation and Foundation at their framework paths (the root has links to /usr/lib there)
lib $F/CoreFoundation.framework/Versions/A/CoreFoundation 150:1338 $S/CoreFoundation.m $AM/CoreFoundation-am.m -- \
  -Wl,-reexport_library,$LINKROOT/usr/lib/libFoundation.dylib -Wl,-reexport_library,$LINKROOT/usr/lib/libCoreFoundation.dylib -lobjc -licucore
lib $F/Foundation.framework/Versions/C/Foundation 300:5026.5.4 $S/Foundation.m $AM/Foundation-am.m -- \
  -Wl,-reexport_library,$LINKROOT/usr/lib/libFoundation.dylib -Wl,-reexport_library,$LINKROOT/usr/lib/swift/libswiftFoundation.dylib -lobjc -lCoreFoundation
# (CoreGraphics' and CoreText's additions are in those frameworks themselves: ../../cocoa/patches 0023, 0025)
wrap QuartzCore A 1:1 $S/QuartzCore.m -- $OBJC
wrap Carbon A 2:170 $S/Carbon.c --
wrap CoreServices A 1:1226 $S/CoreServices.c $AM/CoreServices-am.c -- -lCoreFoundation
lib $F/IOKit.framework/Versions/A/IOKit 1:275 $AM/IOKit-am.c -- -Wl,-reexport_library,$LINKROOT/usr/lib/libIOKitCF.dylib -lCoreFoundation
wrap AudioToolbox A 1:1000 $S/AudioToolbox.c -- -lCoreFoundation
# new libraries
lib $F/ColorSync.framework/Versions/A/ColorSync 1:1 $S/ColorSync.c -- -lCoreFoundation
lib $PF/DataDetectorsCore.framework/Versions/A/DataDetectorsCore 1:1 $S/DataDetectorsCore.c -- -lCoreFoundation
lib $PF/CoreAnalytics.framework/Versions/A/CoreAnalytics 1:1 $S/CoreAnalytics.c -- -lCoreFoundation
lib /usr/lib/libScreenReader.dylib 1:1 $S/ScreenReader.c --
for n in ColorSync; do ln -sfn A $OUT$F/$n.framework/Versions/Current; ln -sfn Versions/Current/$n $OUT$F/$n.framework/$n; done
for n in DataDetectorsCore CoreAnalytics; do ln -sfn A $OUT$PF/$n.framework/Versions/Current; ln -sfn Versions/Current/$n $OUT$PF/$n.framework/$n; done
find "$OUT" -type f | sort
