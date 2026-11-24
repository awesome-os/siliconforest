#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal for SF-X-claude-code.md (tools/dr_qemu_userland.sh with DR_QEMU_GUEST=this file,
# phase quick): the M1's root image with the `sys` part and the vendor's Claude Code binary, then
# the starts that were killed on the M1 (`claude doctor`, `claude -p`, the first screen on a PTY)
# and, when the host put them there, the test programs of userland/jit (/usr/share/sf-test/jit).
# With /usr/share/sf-test/libSystem.B.dylib.new and sf-libsystem-install.sh there, the library is
# first replaced the way tools/sf_userland_push.sh libsystem does it on the M1 (twice: the second
# run must find nothing to do). QDDESK_PASS is printed only if doctor ran, -p said "Not logged in",
# the first screen came up and the entitled test program got its JIT memory.
# Every killed start leaves its reason on the console: a line of libxpc ("SiliconForest libxpc
# misuse: ...") or the kernel's EXC_CORPSE_NOTIFY line, whose subcode is the exit reason
# (namespace << 32 | code).
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
step() {  # <name> <seconds> <command...>
    name=$1; secs=$2; shift 2
    "$@" > /private/tmp/step.out 2> /private/tmp/step.err < /dev/null &
    pid=$!
    ( sleep "$secs"; echo "CJ_TIMEOUT $name"; kill -9 $pid ) > /private/tmp/step.to 2>&1 &
    w=$!
    wait $pid; rc=$?
    kill $w 2>/dev/null; wait $w 2>/dev/null
    printf 'CJ_STEP %s rc=%s: %s out=[%s] err=[%s]\n' "$name" "$rc" "$(cat /private/tmp/step.to 2>/dev/null)" \
        "$(head -c "${CUT:-400}" /private/tmp/step.out | tr '\n' '|')" "$(tail -c "${CUT:-400}" /private/tmp/step.err | tr '\n' '|')"
}
sh /usr/share/sf-userland/sf-userland-install.sh /usr/share/sf-userland / 2>&1 | grep 'done\|rc=' | sed 's/^/CJ_INSTALL /'
for d in /dev/disk0s1 /dev/disk1s1 /dev/disk2s1 /dev/disk3s1; do
    [ -b "$d" ] || continue
    if mount -t ext4 "$d" /opt/homebrew 2>/dev/null; then [ -d /opt/homebrew/Cellar ] && break; umount /opt/homebrew; fi
done
sf-dhcp --up en0 > /dev/null 2>&1; sf-net-up en0 > /dev/null 2>&1; echo "CJ_NET up rc=$?"
T=/usr/share/sf-test
[ ! -x $T/sf-xpc-probe ] || { $T/sf-xpc-probe > /private/tmp/probe.out 2>&1; echo "CJ_PROBE before: status $? $(head -c 80 /private/tmp/probe.out | tr '\n' ' ')"; }
if [ -r $T/libSystem.B.dylib.new ]; then
    S=$(sha256sum $T/libSystem.B.dylib.new | cut -c1-64)
    for n in 1 2; do
        cat $T/libSystem.B.dylib.new > /usr/lib/libSystem.B.dylib.sf-new; sync
        sh -s /usr/lib/libSystem.B.dylib.sf-new "$S" $T/sf-xpc-probe < $T/sf-libsystem-install.sh 2>&1 | sed "s/^/CJ_LIBSYSTEM$n /"
    done
    ls /usr/lib | grep -c 'libSystem.B.dylib.sf-new' | sed 's/^/CJ_LIBSYSTEM left-over new files: /'
fi
ls -l /System/Library/Frameworks/Security.framework/Versions/A/Security /usr/lib/libSystem.B.dylib* 2>&1 | awk '{print "CJ_LS", $1, $5, $NF}'
sha256sum /usr/lib/libSystem.B.dylib | cut -c1-64 | sed 's/^/CJ_LIBSYSTEM in use: /'
[ ! -x $T/sf-xpc-probe ] || { $T/sf-xpc-probe > /private/tmp/probe.out 2>&1; prc=$?; echo "CJ_PROBE now: status $prc $(head -c 80 /private/tmp/probe.out | tr '\n' ' ')"; }
E="env -i PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin HOME=/var/root LANG=en_US.UTF-8 TERM=xterm-256color"
cd /var/root
if [ -d /usr/share/sf-test/jit ]; then
    for t in /usr/share/sf-test/jit/*; do CUT=700 step "jit-$(basename "$t")" 60 "$t"; cp /private/tmp/step.out "/private/tmp/$(basename "$t").out"; done
fi
step claude-version 600 $E claude --version
# /etc/cj-no-security (DR_QEMU_ROOT_FILES): the same starts with Security.framework out of the way
[ ! -e /etc/cj-no-security ] || mv /System/Library/Frameworks/Security.framework /System/Library/Frameworks/Security.framework.off
bad=0; ok() { if eval "$2"; then echo "CJ_VERDICT ok   $1"; else echo "CJ_VERDICT FAIL $1"; bad=$((bad + 1)); fi; }
CUT=900 step claude-doctor "${CJ_SECS:-420}" $E claude doctor
ok "claude doctor ran to its end" '[ "$rc" = 0 ] && grep -q "Claude Code doctor" /private/tmp/step.out'
CUT=900 step claude-print "${CJ_SECS:-420}" $E claude -p hello
ok "claude -p answered that nobody is logged in (no credentials here)" '[ "$rc" = 1 ] && grep -q "Not logged in" /private/tmp/step.out'
$E tmux new-session -d -s c -x 100 -y 30 "claude 2> /private/tmp/tui.err; echo EXIT-STATUS=\$?; sleep 900"
sleep "${CJ_TUI_SECS:-180}"; $E tmux capture-pane -p -t c | sed 's/[[:space:]]*$//' | grep -v '^$' | head -n 24 | cut -c1-110 > /private/tmp/screen.txt; sed 's/^/CJ_SCREEN /' /private/tmp/screen.txt
# (without a network, as in this rehearsal, the program says so after its welcome and ends with status 1)
ok "claude on a terminal shows its first screen and is not killed" 'grep -q "Welcome to Claude Code" /private/tmp/screen.txt && ! grep -q "EXIT-STATUS=1[0-9][0-9]" /private/tmp/screen.txt'
echo "CJ_TUI stderr=[$(tail -c 400 /private/tmp/tui.err | tr '\n' '|')]"
$E tmux kill-server
f=$(ls -t /var/root/.claude/debug/* 2>/dev/null | head -n 1); [ -n "$f" ] && grep -i 'cert\|keychain' "$f" | head -n 6 | cut -c1-200 | sed 's/^/CJ_DEBUGLOG /'
[ ! -x $T/sf-xpc-probe ] || ok "xpc_copy_description of a connection returns (the call that ended claude)" '[ "$prc" = 0 ]'
if [ -d /usr/share/sf-test/jit ]; then
    ok "hardened runtime + allow-jit: MAP_JIT memory is given, written and run" 'grep -q "sf-jit-runtime-entitled map-jit ok" /private/tmp/sf-jit-runtime-entitled.out'
    ok "the same program: memory written, then made executable, runs" 'grep -q "sf-jit-runtime-entitled rw-then-rx ok" /private/tmp/sf-jit-runtime-entitled.out'
    ok "an ad-hoc program (as node): MAP_JIT memory" 'grep -q "sf-jit-adhoc map-jit ok" /private/tmp/sf-jit-adhoc.out'
fi
sync; umount /opt/homebrew 2>/dev/null
echo "CJ_VERDICT failed=$bad"
[ "$bad" = 0 ] && echo QDDESK_PASS
echo QDDESK_END
