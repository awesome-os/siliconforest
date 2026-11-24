#!/bin/sh
# SPDX-License-Identifier: MIT
# Replace /usr/lib/libSystem.B.dylib of a running SiliconForest by a new build, on the target
# (tools/sf_userland_push.sh libsystem; guest/qemu-claude.sh rehearses it):
#   sh sf-libsystem-install.sh <new file in /usr/lib> <its sha256> <probe program>
# A libSystem that does not load leaves a root on which nothing starts, so in this order:
#   1. the new file (beside the old one, under another name) has the SHA-256 given;
#   2. it LOADS here: programs are run against it through DYLD_LIBRARY_PATH (a directory with a
#      hard link to it under the library's name). The probe (userland/jit/sf-xpc-probe.c) must
#      say "SFXPC ok" with it: that only the fixed library does, so the programs demonstrably got
#      the new file; then ls, the shell, sha256sum and, where installed, node and claude;
#   3. only then: the file in use gets the second name /usr/lib/libSystem.B.dylib.old, the
#      new one takes the name by rename(2) (programs already running keep the old file open),
#      sync, and new processes are started without any DYLD variable to confirm.
# Any failure before 3 leaves the system as it was (SFLS FAIL, status 1). If the confirmation
# after the rename fails, the old file is renamed back at once.
# Run again when the library in use is already a fixed one (the probe says so): changes nothing.
# About the SHA-256: it is taken of the new file BEFORE anything loads it and never of a library
# in use. This kernel rewrites pointer-authentication instructions in the page cache when a page
# of a signed file is first mapped executable (xnu osfmk/vm/vm_fault.c, sf_pac_lower_page), and
# read(2) of that file then returns the rewritten bytes: the hash of a library in use differs
# from the file on the disk (in libSystem: four `xpaclri` read as `nop`) and depends on which
# pages have been executed. The disk keeps the original bytes. For the same reason the file in
# use is kept by a hard link (the same inode, bit for bit), not by a copy.
L=/usr/lib/libSystem.B.dylib; NEW=$1; WANT=$2; PROBE=$3; TRY=/usr/lib/sf-libsystem-try
sum() { sha256sum "$1" 2>/dev/null | cut -c1-64; }
fail() { echo "SFLS FAIL $1"; rm -rf $TRY; rm -f "$NEW"; exit 1; }
[ -n "$WANT" ] && [ -x "$PROBE" ] || { echo "SFLS usage: <new file> <sha256> <probe program>"; exit 2; }
[ -r $L ] || fail "cannot read $L"
if "$PROBE" > /dev/null 2>&1; then echo "SFLS already installed: the probe runs with the library in use ($(ls -l $L | awk '{print $5}') bytes)"; rm -f "$NEW"; exit 0; fi
[ "$(sum "$NEW")" = "$WANT" ] || fail "$NEW is not the file expected (sha256 $(sum "$NEW"))"
echo "SFLS 1 the new file has the sha256 expected: $WANT"
chown 0:0 "$NEW" && chmod 755 "$NEW" || fail "owner/mode of $NEW"
rm -rf $TRY; mkdir $TRY && ln "$NEW" $TRY/libSystem.B.dylib || fail "could not make $TRY"
"$PROBE" > /dev/null 2>&1; echo "SFLS 2 the probe with the library in use: status $? (an unfixed library ends it)"
out=$(DYLD_LIBRARY_PATH=$TRY "$PROBE" 2>&1); rc=$?
echo "SFLS 2 the probe with the new library: status $rc: $out"
[ "$rc" = 0 ] && echo "$out" | grep -q '^SFXPC ok' || fail "the probe did not run with the new library: nothing was changed"
try() { DYLD_LIBRARY_PATH=$TRY "$@" > /dev/null 2>&1; r=$?; echo "SFLS 2 with the new library: $1 status $r"; [ "$r" = 0 ] || fail "$1 does not run with the new library: nothing was changed"; }
try /bin/ls /; try /bin/sh -c 'echo x | cat'; try /bin/sha256sum "$PROBE"
[ ! -x /opt/homebrew/bin/node ] || try /opt/homebrew/bin/node -e 'process.exit(6*7-42)'
[ ! -x /usr/local/bin/claude ] || try /usr/local/bin/claude --version
rm -rf $TRY
K=$L.old; ino=$(ls -i "$NEW" | awk '{print $1}'); oldino=$(ls -i $L | awk '{print $1}')
[ ! -e "$K" ] || fail "$K exists already: look at it first (nothing was changed)"
ln $L "$K" && [ "$(ls -i "$K" | awk '{print $1}')" = "$oldino" ] && sync || fail "could not keep the file in use as $K"
mv -f "$NEW" $L || fail "rename"
sync
now=$(ls -i $L | awk '{print $1}'); "$PROBE" > /private/tmp/sfls.probe 2>&1; rc=$?; /bin/ls / > /dev/null 2>&1; rl=$?
echo "SFLS 3 installed: $L is now inode $now (the verified file: $ino), the file of before is $K (inode $oldino); new processes: probe status $rc ($(head -c 60 /private/tmp/sfls.probe | tr '\n' ' ')), ls status $rl"
if [ "$now" != "$ino" ] || [ "$rc" != 0 ] || [ "$rl" != 0 ]; then
    ln "$K" "$L.sf-back" && mv -f "$L.sf-back" $L; sync
    echo "SFLS FAIL the confirmation failed; the old file was put back: $L is inode $(ls -i $L | awk '{print $1}')"; exit 1
fi
ls -l $L "$K" | awk '{print "SFLS", $1, $3, $4, $5, $NF}'
