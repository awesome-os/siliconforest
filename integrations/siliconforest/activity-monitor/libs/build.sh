#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the libraries Apple's Activity Monitor needs beyond Terminal.app's
# (docs/roadmap/issues/SF-AA-activity-monitor.md, GitHub issue 53).
#   build.sh <out> : a tree rooted at / with
#     new       libsysmon, NetworkStatistics, CoreSymbolication, UniformTypeIdentifiers,
#               AssetCacheServicesExtensions, libIOReport, libsystemstats, libquit,
#               libLaunchServicesSupport, RemoteServiceDiscovery, SafeEjectGPU, Metal (two names),
#               SecurityFoundation, Suggestions, OpenCL, CrashReporterSupport (empty); libquit also carries
#               Activity Monitor's helper service in process (ActivityMonitorHelper.m: Send Signal)
#     wrappers  CoreServices, CoreFoundation, Foundation: Terminal.app's wrapper sources
#               (../../terminal-app/compat, unchanged) and this directory's additions in one
#               library each; IOKit: a new wrapper that re-exports /usr/lib/libIOKitCF.dylib (the
#               root has a link to it at the framework's path)
# Environment: CC (cross clang), LD (ld for -fuse-ld), SDK, LINKROOT (a root holding the real
# libraries at their install paths; for a wrapped framework the real one at <path>.pd), INC
# (extra -I flags), PY (python3), HB (directory of hb_macho.py), TERMCOMPAT (Terminal's compat
# directory; default ../../terminal-app/compat).
set -euo pipefail
OUT=${1:?out}; S=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
TC=${TERMCOMPAT:-$S/../../terminal-app/compat}
F=/System/Library/Frameworks; PF=/System/Library/PrivateFrameworks
CF="${NTSTAT_INC:+-idirafter $NTSTAT_INC} -Wno-arc-bridge-casts-disallowed-in-nonarc -isysroot $SDK -mmacosx-version-min=26.5 -O2 -Wall -Wno-deprecated-declarations -Wno-unused-parameter -Wno-nullability-completeness -Wno-objc-root-class -fno-objc-arc -fobjc-exceptions -fblocks -D_DARWIN_C_SOURCE -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I$S/UniformTypeIdentifiers/include $INC"
LF="-fuse-ld=$LD -nostdlib -isysroot $SDK -Wl,-syslibroot,$LINKROOT -mmacosx-version-min=26.5 -dynamiclib -Wl,-platform_version,macos,26.5,26.5 -Wl,-headerpad_max_install_names -L$LINKROOT/usr/lib -F$LINKROOT$F -F$LINKROOT$PF"
objs() { local o= i=0; for s in "$@"; do i=$((i+1)); local b=$W/$i-$(basename "$s").o; case $s in *.m) $CC $CF -x objective-c -c "$s" -o "$b";; *) $CC $CF -c "$s" -o "$b";; esac; o="$o $b"; done; echo "$o"; }
# lib <install path> <compat:current> <sources> -- <link flags>
lib() { local p=$1 v=$2; shift 2; local src=(); while [ "$1" != -- ]; do src+=("$1"); shift; done; shift
  mkdir -p "$OUT$(dirname "$p")"
  $CC $LF -install_name "$p" -compatibility_version ${v%%:*} -current_version ${v##*:} $(objs "${src[@]}") "$@" -lSystem -o "$OUT$p"; }
# fw <F or PF> <name> <compat:current> <sources> -- <link flags>: a framework at Versions/A with the usual links
fw() { local d=$1 n=$2 v=$3; shift 3; lib $d/$n.framework/Versions/A/$n "$v" "$@"
  ln -sfn A $OUT$d/$n.framework/Versions/Current; ln -sfn Versions/Current/$n $OUT$d/$n.framework/$n; }
# wrap <framework> <compat:current> <sources> -- <link flags>: re-exports $LINKROOT<path>.pd, the real library
# (as ../../terminal-app/compat/build.sh: built under <path>.sfcompat, its re-export renamed to <path>.pd)
wrap() { local n=$1 v=$2; shift 2; local p=$F/$n.framework/Versions/A/$n
  local src=(); while [ "$1" != -- ]; do src+=("$1"); shift; done; shift
  local real=$LINKROOT$p.pd; [ -e "$real" ] || real=$LINKROOT$p
  lib "$p.sfcompat" "$v" "${src[@]}" -- -Wl,-reexport_library,"$real" "$@"
  mv "$OUT$p.sfcompat" "$OUT$p"
  $PY -B - "$OUT$p" "$p" <<'PYEOF'
import os, struct, sys
sys.path.insert(0, os.environ['HB'])
import hb_macho
f, p = sys.argv[1:3]
d = open(f, 'rb').read()
m = hb_macho.MachO(d)
out = bytearray(); n = 0
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
# wrappers: Terminal's sources and ours together
lib $F/CoreFoundation.framework/Versions/A/CoreFoundation 150:1338 $TC/CoreFoundation.m $S/CoreFoundation-am.m -- \
  -Wl,-reexport_library,$LINKROOT/usr/lib/libFoundation.dylib -Wl,-reexport_library,$LINKROOT/usr/lib/libCoreFoundation.dylib -lobjc -licucore
lib $F/Foundation.framework/Versions/C/Foundation 300:5026.5.4 $TC/Foundation.m $S/Foundation-am.m -- \
  -Wl,-reexport_library,$LINKROOT/usr/lib/libFoundation.dylib -Wl,-reexport_library,$LINKROOT/usr/lib/swift/libswiftFoundation.dylib -lobjc -lCoreFoundation
wrap CoreServices 1:1226 $TC/CoreServices.c $S/CoreServices-am.c -- -lCoreFoundation
# IOKit: at the framework's path the root has a link to /usr/lib/libIOKitCF.dylib (as for CoreFoundation)
lib $F/IOKit.framework/Versions/A/IOKit 1:275 $S/IOKit-am.c -- -Wl,-reexport_library,$LINKROOT/usr/lib/libIOKitCF.dylib -lCoreFoundation
# new libraries
lib /usr/lib/libsysmon.dylib 1:1 $S/sysmon.m -- $OBJC -framework IOKit
lib /usr/lib/libIOReport.dylib 1:1 $S/IOReport.c -- -lCoreFoundation
lib /usr/lib/libsystemstats.dylib 1:510.120.2 $S/systemstats.c --
lib /usr/lib/libquit.dylib 1:1 $S/quit.c $S/ActivityMonitorHelper.m -- $OBJC
lib /usr/lib/libLaunchServicesSupport.dylib 1:1 $S/LaunchServicesSupport.c -- -lCoreFoundation
fw $PF NetworkStatistics 1:1 $S/NetworkStatistics.c -- -lCoreFoundation
fw $PF CoreSymbolication 1:64575.53.2 $S/CoreSymbolication.c -- -lCoreFoundation
fw $PF RemoteServiceDiscovery 1:219.120.2 $S/RemoteServiceDiscovery.c --
fw $PF SafeEjectGPU 1:1 $S/SafeEjectGPU.m -- $OBJC
fw $PF AssetCacheServicesExtensions 1:136 $S/AssetCacheServicesExtensions.m -- $OBJC
fw $F Metal 1:373.2 $S/Metal.m -- $OBJC
fw $F UniformTypeIdentifiers 1:709 $S/UniformTypeIdentifiers/UTType.m $S/UniformTypeIdentifiers/UTCoreTypes.m $S/UniformTypeIdentifiers/UTAdditions.m -- $OBJC
fw $F SecurityFoundation 1:55293 $S/empty.c --
fw $F OpenCL 1:1 $S/empty.c --
fw $PF Suggestions 1:333 $S/empty.c --
fw $PF CrashReporterSupport 1:15140 $S/empty.c --
find "$OUT" -type f | sort
