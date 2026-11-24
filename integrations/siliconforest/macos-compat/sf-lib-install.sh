#!/bin/sh
# SPDX-License-Identifier: MIT
# Replace one library or framework binary of a running SiliconForest by a new build, on the
# target. The procedure of ../m1-storage/userland/sf-libsystem-install.sh for any library:
#   sh sf-lib-install.sh <installed path> <new file beside it> <its sha256> <probe> [program ...]
#   1. the new file (in the same directory, under another name) has the SHA-256 given. It is
#      hashed BEFORE anything loads it: this kernel rewrites pointer-authentication
#      instructions in the page cache of a signed file once a page is mapped executable, and
#      read(2) then returns the rewritten bytes (docs/roadmap/issues/SF-X-claude-code.md);
#   2. it LOADS here: the probe, which the library in use fails (dyld refuses it or it exits
#      non-zero), runs with status 0 when DYLD_LIBRARY_PATH / DYLD_FRAMEWORK_PATH points at a
#      hard link of the new file, so the programs demonstrably get the new file; then ls, sh
#      and every <program>, run as `<program> --version`: with the new library it must end
#      with status 0 or with the status it has with the library in use (not every program
#      knows --version; a program the library in use cannot start is one the new one may fix);
#   3. only then: the file in use gets the second name <path>.old (a hard link: the same
#      inode, bit for bit; <path>.old2, .old3 ... when an earlier replacement left a .old),
#      the new one takes the name by rename(2) (running programs keep the old file), sync,
#      and the probe and the programs are started without any DYLD variable: the same statuses.
# A failure before 3 changes nothing (SFLIB FAIL, status 1); a failed confirmation renames the
# old file back at once. Run again with the new library in use: "already installed".
# An installed path that is a link (CoreFoundation.framework's and Foundation.framework's name
# /usr/lib's libraries) is kept as a link of the same content and replaced by the file.
# Way back by hand: ln <path>.old <path>.back && mv -f <path>.back <path>
# (for a kept link: ln -s "$(readlink <path>.old)" <path>.back && mv -f <path>.back <path>)
L=$1; NEW=$2; WANT=$3; PROBE=$4; [ $# -ge 4 ] && shift 4
sum() { sha256sum "$1" 2>/dev/null | cut -c1-64; }
[ -n "$WANT" ] && [ -x "$PROBE" ] && [ "$(dirname "$L")" = "$(dirname "$NEW")" ] || { echo "SFLIB usage: <installed path> <new file beside it> <sha256> <probe> [program ...]"; exit 2; }
TRY=$(dirname "$L")/.sf-lib-try; ST=/private/tmp/sflib.status.$$
fail() { echo "SFLIB FAIL $1"; rm -rf "$TRY"; rm -f "$NEW" "$ST"; exit 1; }
[ -r "$L" ] || fail "cannot read $L"
if "$PROBE" > /dev/null 2>&1; then echo "SFLIB already installed: the probe runs with $L as it is ($(ls -l "$L" | awk '{print $5}') bytes)"; rm -f "$NEW"; exit 0; fi
[ "$(sum "$NEW")" = "$WANT" ] || fail "$NEW is not the file expected (sha256 $(sum "$NEW"))"
echo "SFLIB 1 the new file has the sha256 expected: $WANT"
chown 0:0 "$NEW" && chmod 755 "$NEW" || fail "owner/mode of $NEW"
rm -rf "$TRY"
case "$L" in
*.framework/*) rel=$(echo "$L" | sed 's|.*/\([^/]*\.framework/.*\)|\1|'); V=DYLD_FRAMEWORK_PATH;;
*) rel=$(basename "$L"); V=DYLD_LIBRARY_PATH;;
esac
mkdir -p "$TRY/$(dirname "$rel")" && ln "$NEW" "$TRY/$rel" || fail "could not make $TRY"
"$PROBE" > /dev/null 2>&1; echo "SFLIB 2 the probe with the library in use: status $? (it lacks what the new one adds)"
out=$(env $V="$TRY" "$PROBE" 2>&1); rc=$?
echo "SFLIB 2 the probe with the new library ($V): status $rc: $(echo "$out" | tail -n 1)"
[ "$rc" = 0 ] || { echo "$out" | sed 's/^/SFLIB   /'; fail "the probe did not run with the new library: nothing was changed"; }
try() { env $V="$TRY" "$@" > /dev/null 2>&1; r=$?; echo "SFLIB 2 with the new library: $1 status $r"; [ "$r" = 0 ] || fail "$1 does not run with the new library: nothing was changed"; }
try /bin/ls /; try /bin/sh -c 'echo x | cat'
: > "$ST"
for p in "$@"; do
    [ -x "$p" ] || continue
    "$p" --version > /dev/null 2>&1; was=$?
    env $V="$TRY" "$p" --version > /dev/null 2>&1; r=$?
    echo "SFLIB 2 with the new library: $p status $r (with the library in use: $was)"
    [ "$r" = 0 ] || [ "$r" = "$was" ] || fail "$p does not run with the new library as it does with the one in use: nothing was changed"
    echo "$r $p" >> "$ST"
done
rm -rf "$TRY"
K=$L.old; n=2; while [ -e "$K" ]; do K=$L.old$n; n=$((n + 1)); done   # an earlier replacement's .old stays; this one's is .old2, ...
ino=$(ls -i "$NEW" | awk '{print $1}'); oldino=$(ls -i "$L" | awk '{print $1}')
if [ -L "$L" ]; then
    # the installed path is a link (a framework's path that names /usr/lib's library): the link
    # itself is what is kept, as a link of the same content
    WAS=$(readlink "$L"); ln -s "$WAS" "$K" && [ "$(readlink "$K")" = "$WAS" ] && sync || fail "could not keep the link in use as $K"
    oldino="a link to $WAS"
else
    ln "$L" "$K" && [ "$(ls -i "$K" | awk '{print $1}')" = "$oldino" ] && sync || fail "could not keep the file in use as $K"
fi
mv -f "$NEW" "$L" || fail "rename"
sync
now=$(ls -i "$L" | awk '{print $1}'); "$PROBE" > /private/tmp/sflib.probe 2>&1; rc=$?; /bin/ls / > /dev/null 2>&1; rl=$?; rp=0
while read -r want p; do "$p" --version > /dev/null 2>&1 < /dev/null; [ "$?" = "$want" ] || rp=1; done < "$ST"; rm -f "$ST"
echo "SFLIB 3 installed: $L is now inode $now (the verified file: $ino), the file of before is $K (inode $oldino); new processes: probe status $rc ($(tail -n 1 /private/tmp/sflib.probe)), ls status $rl, programs status $rp"
if [ "$now" != "$ino" ] || [ "$rc" != 0 ] || [ "$rl" != 0 ] || [ "$rp" != 0 ]; then
    if [ -L "$K" ]; then ln -s "$(readlink "$K")" "$L.sf-back" && mv -f "$L.sf-back" "$L"; else ln "$K" "$L.sf-back" && mv -f "$L.sf-back" "$L"; fi; sync
    echo "SFLIB FAIL the confirmation failed; the old file was put back: $L is inode $(ls -i "$L" | awk '{print $1}')"; exit 1
fi
ls -l "$L" "$K" | awk '{print "SFLIB", $1, $3, $4, $5, $NF}'
