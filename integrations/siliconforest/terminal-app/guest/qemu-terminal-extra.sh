# SPDX-License-Identifier: MIT
# Sourced by ../../macos-compat/guest/qemu-compat.sh at the end of its run (tools/sf_terminal.sh
# qemu): the last replacement of the macOS-application set, /usr/lib/dyld, as sf_terminal.sh
# does it on the M1 (sf-dyld-install.py), then the probes of all five replaced files and a list
# of programs as new processes. In the second boot (the root the first run left) nothing is
# installed: the probes and the programs run on the system as it starts.
D=$T/compat; PY=/opt/homebrew/bin/python3
PROGRAMS="/opt/homebrew/bin/node /usr/local/bin/claude /opt/homebrew/bin/python3 /opt/homebrew/bin/bash /opt/homebrew/bin/git /opt/homebrew/bin/vim /opt/homebrew/bin/nvim /opt/homebrew/bin/tmux"
if [ ! -e /etc/compat-dyld-done ] && [ -r $D/dyld.new ]; then
    cat $D/dyld.new > /usr/lib/dyl2; sync
    DP=sf-arm64e-probe; [ -r $D/dyld.probe ] && DP=$(cat $D/dyld.probe)
    $PY $T/sf-dyld-install.py /usr/lib/dyl2 "$(cat $D/dyld.sha256)" $T/$DP $PROGRAMS < /dev/null 2>&1 | sed 's/^/COMPAT_DYLD /'
    $PY $T/sf-dyld-install.py /usr/lib/dyl2 "$(cat $D/dyld.sha256)" $T/$DP $PROGRAMS < /dev/null 2>&1 | sed 's/^/COMPAT_DYLD again: /'
    ok "dyld replaced, the file of before kept as .old" '[ -e /usr/lib/dyld.old ] && [ ! -e /usr/lib/dyl2 ]'
    touch /etc/compat-dyld-done
else
    echo "COMPAT_DYLD nothing to install in this boot: $(ls -l /usr/lib/dyld /usr/lib/dyld.old 2>&1 | awk '{print $5, $NF}' | tr '\n' ' ')"
fi
# sf-dyld-probe only when the rehearsed dyld is the one it proves (dyld.probe names it); the others when staged
DPS=; [ -r $D/dyld.probe ] && [ "$(cat $D/dyld.probe)" = sf-dyld-probe ] && DPS=sf-dyld-probe
for p in sf-arm64e-probe $DPS sf-objc-probe sf-libsystem26-probe sf-kvo-probe sf-corefoundation-framework-probe sf-foundation-framework-probe sf-cf-o2-probe sf-cf-autorelease-probe; do
    [ -x $T/$p ] || { echo "COMPAT_PROBE $p not in this stage"; continue; }
    $T/$p > /private/tmp/p.out 2>&1; rc=$?
    echo "COMPAT_PROBE $p rc=$rc $(tail -n 1 /private/tmp/p.out | cut -c1-140)"
    ok "$p runs as a new process" '[ "$rc" = 0 ]'
done
/bin/ls / > /dev/null 2>&1; rc=$?; ok "ls" '[ "$rc" = 0 ]'
[ "$(/bin/sh -c 'echo x | cat' 2>&1)" = x ]; rc=$?; ok "sh with a pipe" '[ "$rc" = 0 ]'
for p in $PROGRAMS; do
    [ -x "$p" ] || { echo "COMPAT_PROGRAM $p is not there"; continue; }
    v=--version; [ "$(basename "$p")" = tmux ] && v=-V
    "$p" $v > /private/tmp/p.out 2>&1 < /dev/null; rc=$?
    echo "COMPAT_PROGRAM $p $v rc=$rc $(head -n 1 /private/tmp/p.out | cut -c1-80)"
    ok "$(basename "$p") $v" '[ "$rc" = 0 ]'
done
ls -l /usr/lib/dyld* /usr/lib/libSystem.B.dylib* /usr/lib/libCoreFoundation.dylib* /usr/lib/libFoundation.dylib* /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation* /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation* 2>&1 | awk '{print "COMPAT_FILES", $1, $5, $9, $10, $11}'
