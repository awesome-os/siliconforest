#!/bin/bash
# SPDX-License-Identifier: MIT
# Apple's Terminal.app on SiliconForest: docs/roadmap/issues/SF-Y-terminal-app.md.
# Everything the system needs for it is one tree, ../build-macos-apps-root.nix.
#
# Trying it from a scratch prefix (nothing installed):
#   sf_terminal.sh stage          build that tree and lay out runs/sf-terminal/stage:
#                                   root/   the tree: the libraries the application loads instead of the
#                                           system's (DYLD_FRAMEWORK_PATH / DYLD_LIBRARY_PATH)
#                                   pd/     the libraries the wrappers re-export (<name>.pd)
#                                   run.sh, show.sh, sf-catch, sf-nibprobe, libsftrace.dylib, libsfprof.dylib
#   sf_terminal.sh push           send the stage and the application to /private/tmp/sfy on the target
#                                 (the application's arm64e slice, plus a test copy that names
#                                 /tmp/sfy/dyld as its dynamic linker: for a system whose dyld is the old one)
#   sf_terminal.sh run [seconds] [VAR=value ...]
#                                 start it there, wait, print the summarised log, stop it (KEEP=1: leave it)
#   sf_terminal.sh shot <name>    fetch the window pictures (SF_WS_DUMP_DIR) as runs/sf-terminal/shots/<name>-N.png
#   sf_terminal.sh scan           the gap table of the application against the stage
#
# Installing (the order is the order of use):
#   sf_terminal.sh install-stage  runs/sf-terminal/install from the tree:
#                                   core/    the files every program loads, in the layout of
#                                            ../../macos-compat/tools/sf_compat_stage.sh: replace.list and
#                                            replace/<leaf>.new (libSystem, libFoundation, the CoreFoundation and
#                                            Foundation framework wrappers), dyld.new + dyld.sha256, test/ (probes),
#                                            new/ (what the system lacks, for the rehearsal's root image)
#                                   direct/  everything else: manifest, links, wrapped, files/
#   sf_terminal.sh qemu <name> [second]
#                                 the rehearsal of core/ in QEMU on the M1's root image
#                                 (../../macos-compat/tools/sf_compat_qemu.sh with ../guest/qemu-terminal-extra.sh):
#                                 the replacements as on the M1, then (second) a fresh boot of the result
#   sf_terminal.sh install new    on the running target: the files it does not have (Swift runtime, ColorSync ...)
#   sf_terminal.sh install core [leaf]
#                                 the guarded replacement of each core file (../../macos-compat/sf-lib-install.sh:
#                                 written beside, hashed there before anything loads it, programs proven with it
#                                 through DYLD_*, the file in use kept as .old, renamed), then of /usr/lib/dyld
#                                 (../guest/sf-dyld-install.py)
#   sf_terminal.sh install direct the rest (AppKit, CoreText, CoreGraphics, libWindowServer, the other wrappers):
#                                 hashed there, existing files kept as .old-sfy or .pd, renamed
#   sf_terminal.sh install app    the application bundle, unmodified, to /Applications/Utilities/Terminal.app
#   sf_terminal.sh launch [seconds] [VAR=value ...]
#                                 start the installed bundle on the target's desktop (KEEP=1: leave it)
#   sf_terminal.sh revert direct  undo `install direct` and `install new` (the core files: see the installers)
# SF_TERMINAL_APP: the application bundle (default private/terminal-app/m5-26.5.2/Terminal.app; never committed).
# SF_BASE: a copy of the target's root (default runs/sf-gaps/m1root, with runs/sf-compat/stage laid over it).
# SF_DYLD (install-stage): the dyld to stage instead of the recipe's (see there).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
T=integrations/siliconforest/terminal-app; W=runs/sf-terminal; ST=$W/stage; IN=$W/install; C=integrations/siliconforest/macos-compat
APP=${SF_TERMINAL_APP:-private/terminal-app/m5-26.5.2/Terminal.app}
BASE=${SF_BASE:-runs/sf-gaps/m1root}; F=System/Library/Frameworks
SF_HOST=${SF_HOST:-10.1.1.211}; SF_KNOWN=${SF_KNOWN:-$HOME/.ssh/known_hosts_m1_siliconforest}
SSH=(ssh -o UserKnownHostsFile="$SF_KNOWN" -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o HostKeyAlias=m1-siliconforest
     -o PasswordAuthentication=no -o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=10 "root@$SF_HOST")
nb() { (cd "$1" && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link "${@:2}" 2>/dev/null | tail -1); }
WRAPPED="QuartzCore Carbon CoreServices AudioToolbox"
# the files every program loads: <path in the tree> (dyld last, by its own installer)
# (IOKit's framework path: every IOKit user loads it; Activity Monitor's wrapper replaces the link there)
CORE="usr/lib/libSystem.B.dylib usr/lib/libCoreFoundation.dylib usr/lib/libFoundation.dylib $F/CoreFoundation.framework/Versions/A/CoreFoundation $F/Foundation.framework/Versions/C/Foundation $F/IOKit.framework/Versions/A/IOKit"
PROGRAMS="/opt/homebrew/bin/node /usr/local/bin/claude /opt/homebrew/bin/python3 /opt/homebrew/bin/bash /opt/homebrew/bin/git /opt/homebrew/bin/vim /opt/homebrew/bin/nvim /opt/homebrew/bin/tmux"
tree() { local r; r=$(nb $T build-macos-apps-root.nix); [ -d "$r/usr/lib" ] || { echo "build-macos-apps-root.nix did not build" >&2; exit 2; }; echo "$r"; }
case "${1:?stage|push|run|shot|scan|install-stage|qemu|install|launch|revert}" in
stage)
  R=$(tree)
  eval "$(cd integrations/siliconforest/cocoa && nix-instantiate --extra-experimental-features 'nix-command flakes' --eval --strict --json -E 'let a = import ./arm64.nix; in { CC = "${a.common.darwinCrossToolchain}/bin/arm64-apple-darwin20.4-clang"; LD = "${a.common.nativeLd}/bin/ld"; SDK = "${a.common.appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"; I1 = "${a.common.libSystem}/usr/include"; I2 = "${a.base.libobjc}/usr/include"; I3 = "${a.base.corefoundation}/include"; I4 = "${a.base.foundation}/usr/include"; }' 2>/dev/null | python3 -c 'import json,sys; [print("export %s=%s" % kv) for kv in json.load(sys.stdin).items()]')"
  INC="-I$I1 -I$I2 -I$I3 -I$I4"
  # the link root for the tools below (and for the scanner): the target's root as it is with the tree over it
  chmod -R u+w "$W/linkroot" "$ST" 2>/dev/null || true; rm -rf "$ROOT/$W/linkroot" "$ROOT/$ST"; mkdir -p $W/linkroot $ST/sf
  cp -a --reflink=auto $BASE/usr $BASE/System $BASE/lib $W/linkroot/; chmod -R u+w $W/linkroot
  [ -d runs/sf-compat/stage/new ] && cp -a --reflink=auto runs/sf-compat/stage/new/. $W/linkroot/ && chmod -R u+w $W/linkroot
  rsync -a --chmod=u+w --exclude /usr/share --exclude /nix-support $R/ $W/linkroot/
  chmod -R u+w $W/linkroot; LINKROOT=$ROOT/$W/linkroot
  cp -a --no-preserve=ownership $R $ST/root; chmod -R u+w $ST/root; rm -rf $ST/root/usr/share
  for n in $WRAPPED; do mv $ST/root/$F/$n.framework/Versions/A/$n.pd $ST/sf/$n.pd; done
  ln -sfn Versions/A/Resources $ST/root/$F/AppKit.framework/Resources
  CCF="-isysroot $SDK -mmacosx-version-min=26.5 -O1 -fno-objc-arc -fobjc-exceptions -fblocks -Wno-nullability-completeness -Wno-objc-root-class -Wno-objc-method-access $INC"
  LDF="-fuse-ld=$LD -nostdlib -Wl,-syslibroot,$LINKROOT -L$LINKROOT/usr/lib -Wl,-platform_version,macos,26.5,26.5"
  $CC -std=gnu11 -O1 -isystem $SDK/usr/include $T/tools/sf-catch.c $LDF -lSystem -o $ST/sf-catch 2>/dev/null
  $CC $CCF $T/probe/sf-nibprobe.m $LDF -lobjc -lFoundation -lCoreFoundation -lSystem -o $ST/sf-nibprobe 2>/dev/null
  $CC -isysroot $SDK -mmacosx-version-min=26.5 -O1 $INC $LDF -dynamiclib -install_name /tmp/sfy/libsftrace.dylib $T/probe/sf-trace.c $T/probe/sf-trace-thunks.S \
      -lobjc -lFoundation -lCoreFoundation -lSystem -o $ST/libsftrace.dylib 2>/dev/null
  $CC -isysroot $SDK -mmacosx-version-min=26.5 -O1 $INC $LDF -dynamiclib -install_name /tmp/sfy/libsfprof.dylib $T/probe/sf-prof.c -lSystem -o $ST/libsfprof.dylib 2>/dev/null
  install -m755 $T/guest/run.sh $T/guest/show.sh $T/guest/probe.sh $ST/
  # the test copy: the arm64e slice with /tmp/sfy/dyld (same length) as its dynamic linker
  python3 -B - "$APP" "$ST/Terminal.testdyld" <<'PYEOF'
import plistlib, sys
sys.path.insert(0, 'integrations/siliconforest/homebrew')
import hb_macho
app, out = sys.argv[1:3]
exe = app + '/Contents/MacOS/' + plistlib.load(open(app + '/Contents/Info.plist', 'rb'))['CFBundleExecutable']
m = hb_macho.load(exe, 'arm64e')
assert m.data.count(b'/usr/lib/dyld\0') == 1
open(out, 'wb').write(m.data.replace(b'/usr/lib/dyld\0', b'/tmp/sfy/dyld\0'))
PYEOF
  echo "tree: $R"; du -sh $ST | cut -f1;;
push)
  tar -C $ST -cf - . | "${SSH[@]}" 'mkdir -p /private/tmp/sfy/shots && cd /private/tmp/sfy && /opt/homebrew/bin/gtar -xf - && cp root/usr/lib/dyld dyld && chmod 755 dyld Terminal.testdyld run.sh show.sh probe.sh sf-catch sf-nibprobe'
  tar -C "$(dirname "$APP")" -cf - "$(basename "$APP")" | "${SSH[@]}" 'cd /private/tmp/sfy && /opt/homebrew/bin/gtar -xf - && cp Terminal.testdyld Terminal.app/Contents/MacOS/Terminal.testdyld && echo pushed';;
run)
  S=${2:-20}; shift; shift || true
  "${SSH[@]}" "cd /private/tmp/sfy; pkill -f 'MacOS/Terminal.testdyld' 2>/dev/null; sleep 1; rm -f shots/*; (env SF_WS_DUMP_DIR=/private/tmp/sfy/shots $* CATCH=/private/tmp/sfy/sf-catch ./run.sh > out.log 2>&1; echo rc=\$? >> out.log) > /dev/null 2>&1 < /dev/null & sleep $S; cd /private/tmp/sfy; ./show.sh; [ -n \"${KEEP:-}\" ] || pkill -f 'MacOS/Terminal.testdyld'" < /dev/null;;
shot)
  N=${2:?name}; mkdir -p $W/shots; i=0
  for f in $("${SSH[@]}" 'ls /private/tmp/sfy/shots/*.ppm 2>/dev/null' < /dev/null); do
    i=$((i + 1)); "${SSH[@]}" "cat $f" < /dev/null > $W/shots/$N-$i.ppm
    python3 -c "from PIL import Image; import sys; im = Image.open(sys.argv[1]); im.save(sys.argv[2]); print(sys.argv[2], im.size)" $W/shots/$N-$i.ppm $W/shots/$N-$i.png
  done;;
scan)
  mkdir -p $W/scanpd; for n in $WRAPPED; do mkdir -p $W/scanpd/$F/$n.framework/Versions/A; cp $ST/sf/$n.pd $W/scanpd/$F/$n.framework/Versions/A/$n.pd; done
  python3 -B integrations/siliconforest/homebrew/sf_gapscan.py --root $W/linkroot --overlay $ST/root --overlay $W/scanpd \
    --app "$APP=/Applications/Utilities/Terminal.app" --arch arm64e --name terminal-stage;;
install-stage)
  R=$(tree); PR=$(nb $C build-probe.nix)
  chmod -R u+w "$IN" 2>/dev/null || true; rm -rf "$IN"; mkdir -p $IN/core/replace $IN/core/test $IN/core/new $IN/direct/files
  cp $R/usr/share/sf-test/* $IN/core/test/; [ -d "$PR/usr/share/sf-test" ] && cp -n $PR/usr/share/sf-test/* $IN/core/test/
  cp $T/guest/sf-dyld-install.py $IN/core/test/
  probe_of() { case "$1" in *libobjc*) echo sf-objc-probe;; *libSystem*) echo sf-libsystem26-probe;; *libCoreFoundation*) echo sf-cf-autorelease-probe;; *libFoundation*) echo sf-kvo-probe;; *CoreFoundation) echo sf-corefoundation-framework-probe;; *Foundation) echo sf-foundation-framework-probe;; *IOKit) echo sf-iokit-framework-probe;; esac; }
  : > $IN/core/replace.list
  # the rehearsal's root is the image the disk root was made from: what ../../macos-compat brought to the
  # M1 since (its replaced libraries other than libSystem, its new files) goes first, so that the root
  # rehearsed is the root the M1 has. On the M1 those are installed already: the installer says so.
  CS=runs/sf-compat/stage
  if [ -s $CS/replace.list ]; then
    grep -v '^/usr/lib/libSystem.B.dylib ' $CS/replace.list >> $IN/core/replace.list || true
    for f in $CS/replace/*.new; do [ "$(basename $f)" = libSystem.B.dylib.new ] || cp $f $IN/core/replace/; done
    cp -n $CS/test/* $IN/core/test/ 2>/dev/null || true
    cp -a --reflink=auto $CS/new/. $IN/core/new/; chmod -R u+w $IN/core/new
  fi
  # libobjc: when the tree has it (../build-macos-apps-root.nix; SF_LIBOBJC=old leaves it out)
  [ -e $R/usr/lib/libobjc.A.dylib ] && CORE="$CORE usr/lib/libobjc.A.dylib"
  for f in $CORE; do
    leaf=$(basename $f); cp $R/$f $IN/core/replace/$leaf.new
    echo "/$f $(sha256sum $IN/core/replace/$leaf.new | cut -c1-64) /usr/share/sf-test/$(probe_of $f) $PROGRAMS" >> $IN/core/replace.list
  done
  # SF_DYLD: a dyld that a rehearsal passed with, where the recipe's rebuild of the same source differs
  # only in its debug map (object file times) and code signature
  cp ${SF_DYLD:-$R/usr/lib/dyld} $IN/core/dyld.new; sha256sum $IN/core/dyld.new | cut -c1-64 > $IN/core/dyld.sha256
  # the dyld's proof: sf-dyld-probe when the tree has it (the optimised dyld), else sf-arm64e-probe
  if [ -z "${SF_DYLD:-}" ] && [ -e $R/usr/share/sf-test/sf-dyld-probe ]; then echo sf-dyld-probe; else echo sf-arm64e-probe; fi > $IN/core/dyld.probe
  # everything else; the wrappers that stand in front of a library the system has
  (cd $R && find . -type f -not -path './usr/share/sf-test/*' -not -path './nix-support/*' -printf '%P\n' | LC_ALL=C sort) | while read -r f; do
    case " $CORE usr/lib/dyld " in *" $f "*) continue;; esac
    mkdir -p "$IN/direct/files/$(dirname "$f")"; cp "$R/$f" "$IN/direct/files/$f"
  done
  chmod -R u+w $IN
  (cd $IN/direct/files && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sed 's|  \./| |') > $IN/direct/manifest
  (cd $R && find . -type l -not -path './usr/share/*' -printf '%l\t%P\n' | LC_ALL=C sort) > $IN/direct/links
  for n in $WRAPPED; do echo "/$F/$n.framework/Versions/A/$n"; done > $IN/direct/wrapped
  # what the target lacks, for the rehearsal's root image (sf_compat_qemu.sh's layout)
  while read -r sum f; do
    case "$f" in *.pd) continue;; esac
    if [ ! -e "$BASE/$f" ] && [ ! -L "$BASE/$f" ] && [ ! -e "$IN/core/new/$f" ]; then mkdir -p "$IN/core/new/$(dirname "$f")"; cp "$IN/direct/files/$f" "$IN/core/new/$f"; fi
  done < $IN/direct/manifest
  while IFS=$'\t' read -r target f; do
    if [ ! -e "$BASE/$f" ] && [ ! -L "$BASE/$f" ] && [ ! -e "$IN/core/new/$f" ] && [ ! -L "$IN/core/new/$f" ]; then mkdir -p "$IN/core/new/$(dirname "$f")"; ln -sfn "$target" "$IN/core/new/$f"; fi
  done < $IN/direct/links
  (cd $IN/core/new && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum | sed 's|  \./| |') > $IN/core/new.sha256
  (cd $IN/core/new && find . -type l -printf '%l\t%P\n' | LC_ALL=C sort) > $IN/core/new.links
  echo "tree: $R"
  echo "core: $(grep -c . $IN/core/replace.list) libraries and dyld; direct: $(grep -c . $IN/direct/manifest) files, $(grep -c . $IN/direct/links) links, $(du -sm $IN/direct/files | cut -f1) MB; of them the root copy lacks $(grep -c . $IN/core/new.sha256)";;
qemu)
  N=${2:?name}; [ -s $IN/core/replace.list ] || { echo "run sf_terminal.sh install-stage first" >&2; exit 2; }
  TT=/usr/share/sf-test
  # the root starts with the libSystem the target has (SF_QEMU_LIBSYSTEM: a file; default the one
  # ../../macos-compat last staged for it), not with the default recipe's, which is the new one
  export SF_COMPAT_LIBSYSTEM=${SF_QEMU_LIBSYSTEM:-runs/sf-compat/stage/replace/libSystem.B.dylib.new}
  [ -s "$SF_COMPAT_LIBSYSTEM" ] || { echo "no libSystem for the rehearsal's root at $SF_COMPAT_LIBSYSTEM (SF_QEMU_LIBSYSTEM)" >&2; exit 2; }
  SF_COMPAT_STAGE=$IN/core SF_COMPAT_HOST_NEW=1 SF_COMPAT_EXTRA=$T/guest/qemu-terminal-extra.sh \
  SF_COMPAT_MORE_FILES="$IN/core/dyld.new=$TT/compat/dyld.new $IN/core/dyld.sha256=$TT/compat/dyld.sha256 $IN/core/dyld.probe=$TT/compat/dyld.probe" \
    $C/tools/sf_compat_qemu.sh "$N" ${3:-};;
install)
  [ -s $IN/core/replace.list ] || { echo "run sf_terminal.sh install-stage first" >&2; exit 2; }
  direct() {  # new|all
    local mode=$1
    cat $T/guest/sf-apps-install.sh | "${SSH[@]}" 'rm -rf /private/var/db/sf-apps-stage; mkdir -p /private/var/db/sf-apps-stage && cat > /private/var/db/sf-apps-stage/sf-apps-install.sh'
    tar -C $IN/direct --owner=0 --group=0 --numeric-owner -cf - manifest links wrapped files | gzip -1 | "${SSH[@]}" 'cd /private/var/db/sf-apps-stage && gunzip | /usr/local/libexec/sf-gtar -xf - && sync && echo staged'
    "${SSH[@]}" "sh /private/var/db/sf-apps-stage/sf-apps-install.sh /private/var/db/sf-apps-stage $mode; rc=\$?; rm -rf /private/var/db/sf-apps-stage; exit \$rc" < /dev/null
  }
  case "${2:?new|core|direct|app}" in
  new) direct new;;
  direct) direct all;;
  core)
    if [ "${3:-}" = dyld ]; then  # dyld alone: only the probes and the installer to /private/tmp/sf-test
      tar -C $IN/core/test --owner=0 --group=0 --numeric-owner -cf - . | "${SSH[@]}" 'mkdir -p /private/tmp/sf-test && cd /private/tmp/sf-test && tar -xf - && chmod 755 *'
    else SF_COMPAT_STAGE=$IN/core $C/tools/sf_compat_push.sh replace ${3:-}; fi
    if [ -z "${3:-}" ] || [ "$3" = dyld ]; then
      want=$(cat $IN/core/dyld.sha256); dp=$(cat $IN/core/dyld.probe 2>/dev/null || echo sf-arm64e-probe)
      "${SSH[@]}" 'cat > /usr/lib/dyl2 && sync' < $IN/core/dyld.new
      "${SSH[@]}" "/opt/homebrew/bin/python3 /private/tmp/sf-test/sf-dyld-install.py /usr/lib/dyl2 $want /private/tmp/sf-test/$dp $PROGRAMS" < /dev/null \
        || echo "SFDYLD status $? (see the lines above; nothing is changed unless a line says installed)"
    fi;;
  app)
    tar -C "$(dirname "$APP")" --owner=0 --group=0 --numeric-owner -cf - "$(basename "$APP")" | "${SSH[@]}" '
      mkdir -p /Applications/Utilities && cd /Applications/Utilities && rm -rf Terminal.app.sf-new && mkdir Terminal.app.sf-new && /usr/local/libexec/sf-gtar -xf - -C Terminal.app.sf-new && sync &&
      { [ ! -e Terminal.app ] || { rm -rf Terminal.app.old; mv Terminal.app Terminal.app.old; }; } && mv Terminal.app.sf-new/Terminal.app Terminal.app && rmdir Terminal.app.sf-new && sync && ls -ld /Applications/Utilities/Terminal.app'
    # the bundle there is the bundle here, file by file
    (cd "$APP" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > $W/app.sha256
    "${SSH[@]}" 'cd /Applications/Utilities/Terminal.app && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum' < /dev/null > $W/app.remote.sha256
    if cmp -s $W/app.sha256 $W/app.remote.sha256; then echo "the bundle on the target is the bundle here: $(grep -c . $W/app.sha256) files equal"; else echo "THE BUNDLE DIFFERS: $(diff $W/app.sha256 $W/app.remote.sha256 | grep -c '^[<>]') lines"; exit 1; fi;;
  esac;;
launch)
  S=${2:-20}; shift; shift || true
  "${SSH[@]}" "pkill -x Terminal 2>/dev/null; sleep 1; mkdir -p /private/tmp/sf-terminal; cd /private/tmp/sf-terminal; (env XDG_RUNTIME_DIR=/private/var/run/gpu-desktop WAYLAND_DISPLAY=wayland-0 HOME=/var/root $* /Applications/Utilities/Terminal.app/Contents/MacOS/Terminal > out.log 2>&1; echo rc=\$? >> out.log) > /dev/null 2>&1 < /dev/null & sleep $S; ps ax -o pid,ppid,tty,command | grep -E 'MacOS/Terminal|ttys' | grep -v grep; tail -n 12 /private/tmp/sf-terminal/out.log | cut -c1-220; [ -n \"${KEEP:-}\" ] || pkill -x Terminal" < /dev/null;;
revert)
  [ "${2:-}" = direct ] || { echo "revert direct" >&2; exit 2; }
  "${SSH[@]}" 'sh -s -- --revert' < $T/guest/sf-apps-install.sh;;
*) echo "stage|push|run|shot|scan|install-stage|qemu|install|launch|revert" >&2; exit 2;;
esac
