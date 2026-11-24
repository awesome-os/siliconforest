#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal for the macOS-compatibility additions (tools/sf_compat_qemu.sh; the M1's root
# image with the `sys` part, the M1's /opt/homebrew as a second disk): what
# tools/sf_compat_push.sh does on the M1, then the programs the additions are for.
#   /usr/share/sf-test/compat/replace   lines "<installed path> <sha256> <probe> [program ...]":
#       the new file lies beside the installed one as <path>.sf-new; ../sf-lib-install.sh replaces it
#   /usr/share/sf-test/compat/new.tar.gz, new.sha256   files the root does not have (frameworks,
#       Perl): unpacked at /, then every file hashed against the list
# QDDESK_PASS only if every COMPAT_VERDICT is ok.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
T=/usr/share/sf-test; C=$T/compat
step() {  # <name> <seconds> <command...>
    name=$1; secs=$2; shift 2
    "$@" > /private/tmp/step.out 2> /private/tmp/step.err < /dev/null &
    pid=$!
    ( sleep "$secs"; echo "COMPAT_TIMEOUT $name"; kill -9 $pid ) > /private/tmp/step.to 2>&1 &
    w=$!
    wait $pid; rc=$?
    kill $w 2>/dev/null; wait $w 2>/dev/null
    printf 'COMPAT_STEP %s rc=%s: %s out=[%s] err=[%s]\n' "$name" "$rc" "$(cat /private/tmp/step.to 2>/dev/null)" \
        "$(head -c "${CUT:-400}" /private/tmp/step.out | tr '\n' '|')" "$(tail -c "${CUT:-400}" /private/tmp/step.err | tr '\n' '|')"
}
bad=0; ok() { if eval "$2"; then echo "COMPAT_VERDICT ok   $1"; else echo "COMPAT_VERDICT FAIL $1"; bad=$((bad + 1)); fi; }
sh /usr/share/sf-userland/sf-userland-install.sh /usr/share/sf-userland / 2>&1 | grep 'done\|rc=' | sed 's/^/COMPAT_INSTALL /'
for d in /dev/disk0s1 /dev/disk1s1 /dev/disk2s1 /dev/disk3s1; do
    [ -b "$d" ] || continue
    if mount -t ext4 "$d" /opt/homebrew 2>/dev/null; then [ -d /opt/homebrew/Cellar ] && break; umount /opt/homebrew; fi
done
cd /var/root
if [ ! -e /etc/compat-second-boot ]; then
    # ---- files the root does not have
    if [ -r $C/new.tar.gz ]; then
        X=/opt/homebrew/bin/gtar; [ -x $X ] || X=tar
        gunzip < $C/new.tar.gz | $X -x -p -C / -f - 2>&1 | tail -n 3 | sed 's/^/COMPAT_NEW /'; sync
        n=0; wrong=0
        while read -r want f; do n=$((n + 1)); [ "$(sha256sum "/$f" 2>/dev/null | cut -c1-64)" = "$want" ] || { wrong=$((wrong + 1)); echo "COMPAT_NEW differs: /$f"; }; done < $C/new.sha256
        echo "COMPAT_NEW $n files hashed, $wrong differ"
        ok "the new files arrived as built" '[ "$n" -gt 0 ] && [ "$wrong" = 0 ]'
    fi
    # ---- libraries in use, replaced as on the M1
    if [ -r $C/replace ]; then
        while read -r path want probe programs; do
            cat "$C/$(basename "$path").new" > "$path.sf-new"; sync
            sh $T/sf-lib-install.sh "$path" "$path.sf-new" "$want" "$probe" $programs < /dev/null 2>&1 | sed 's/^/COMPAT_REPLACE /'
            sh $T/sf-lib-install.sh "$path" "$path.sf-new" "$want" "$probe" $programs < /dev/null 2>&1 | sed 's/^/COMPAT_REPLACE again: /'
            ok "$(basename "$path") replaced, the file of before kept as .old" '[ -e "$path.old" ] && [ ! -e "$path.sf-new" ]'
        done < $C/replace
    fi
    touch /etc/compat-second-boot
else
    echo "COMPAT_BOOT a fresh boot of the root the first run left: nothing is installed now"
fi
sh $T/sf-compat-check.sh > /private/tmp/check.out 2>&1; cat /private/tmp/check.out
bad=$((bad + $(grep -c '^COMPAT_VERDICT FAIL' /private/tmp/check.out)))
[ ! -r $C/extra.sh ] || . $C/extra.sh
sync; umount /opt/homebrew 2>/dev/null
echo "COMPAT_VERDICT failed=$bad"
[ "$bad" = 0 ] && echo QDDESK_PASS
echo QDDESK_END
