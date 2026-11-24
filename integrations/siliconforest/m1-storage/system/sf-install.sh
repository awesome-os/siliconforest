#!/bin/sh
# SPDX-License-Identifier: MIT
# Put the disk root's own system tools into a SiliconForest root. Runs inside the booted
# system (the files come from the RAM root image, the root is written through the fenced
# driver) or on any host against a mounted or unpacked root. Idempotent.
#   sf-install.sh <directory with sf-halt sf-clock sf-boot sf-command darwin-desktop-test.disk sf-session
#                  org.siliconforest.desktop.plist [bench/]> <root directory>
# sf-session and the launchd job are the session of a boot without a RAM root (multi-user; the job is
# not run in a single-user boot). bench/ (optional: prelude.in end.in sfnvme-test sfnvme-ranges) are the
# bench's own pieces for such a boot; they go to /private/var/db/sf-bench and run only when the boot
# arguments carry sfbench=<name> (system/sf-session).
set -eu
S=${1:?source directory}; R=${2:?root directory}
[ -x "$R/sbin/launchd" ] && [ -d "$R/System" ] && [ -d "$R/private" ] || { echo "SF_INSTALL not a SiliconForest root: $R"; exit 2; }
for f in sf-halt sf-clock sf-boot sf-command darwin-desktop-test.disk sf-session org.siliconforest.desktop.plist; do [ -r "$S/$f" ] || { echo "SF_INSTALL missing $S/$f"; exit 2; }; done
put() {  # <source> <destination>: by way of a new name, so a running program's file is replaced, never rewritten
    cp "$S/$1" "$R/$2.sf-new" && chmod 755 "$R/$2.sf-new" && mv -f "$R/$2.sf-new" "$R/$2"
}
mkdir -p "$R/usr/libexec" "$R/private/var/run" "$R/private/tmp" "$R/private/ram"
# where wifiutil keeps the networks it has joined (org.siliconforest.wifi.known). The image this root was copied
# from has no such directory, wifiutil does not make it and says nothing when it cannot write the file: without
# it no network is remembered, and sf-netd, which joins remembered networks only, never has one to join.
mkdir -p "$R/Library/Preferences/SystemConfiguration"
put sf-halt sbin/sf-halt
put sf-clock usr/sbin/sf-clock
put sf-boot sbin/sf-boot
put darwin-desktop-test.disk usr/bin/darwin-desktop-test
put sf-session usr/libexec/sf-session
mkdir -p "$R/System/Library/LaunchDaemons"
put org.siliconforest.desktop.plist System/Library/LaunchDaemons/org.siliconforest.desktop.plist
chmod 644 "$R/System/Library/LaunchDaemons/org.siliconforest.desktop.plist"
if [ -d "$S/bench" ]; then
    B=private/var/db/sf-bench
    for f in prelude.in end.in sfnvme-test sfnvme-ranges; do [ -r "$S/bench/$f" ] || { echo "SF_INSTALL missing $S/bench/$f"; exit 2; }; done
    mkdir -p "$R/$B"
    for f in prelude.in end.in sfnvme-test sfnvme-ranges; do put "bench/$f" "$B/$f"; done
    chmod 644 "$R/$B/prelude.in" "$R/$B/end.in" "$R/$B/sfnvme-ranges"
    # the untouched libSystem of the image this root was copied from: the bench compares the disk's with it
    if [ -r "$S/../../lib/libSystem.B.dylib" ]; then
        cp "$S/../../lib/libSystem.B.dylib" "$R/$B/libSystem.B.dylib.ref.sf-new" && mv -f "$R/$B/libSystem.B.dylib.ref.sf-new" "$R/$B/libSystem.B.dylib.ref"
    fi
    echo "SF_INSTALL bench: $(cd "$R/$B" && ls -l 2>&1 | awk 'NR > 1 {print $NF ":" $5}' | tr '\n' ' ')"
fi
# The Chonkstep dock (../../desktop, optional: present when the boot carries it, DR_DOCK=<file>): the
# program, its launcher items in /usr/share/applications and, only where the person has none yet, its
# pins ($XDG_STATE_HOME/chonkstep/dock of the session: /private/var/db/desktop-state).
if [ -r "$S/chonkstep-dock" ]; then
    put chonkstep-dock usr/bin/chonkstep-dock
    mkdir -p "$R/usr/share/applications" "$R/private/var/db/desktop-state/chonkstep"
    for f in "$S"/applications/*.desktop; do
        [ -r "$f" ] || continue
        cp "$f" "$R/usr/share/applications/$(basename "$f").sf-new" && chmod 644 "$R/usr/share/applications/$(basename "$f").sf-new" \
            && mv -f "$R/usr/share/applications/$(basename "$f").sf-new" "$R/usr/share/applications/$(basename "$f")"
    done
    if [ -r "$S/dock-pins" ] && [ ! -e "$R/private/var/db/desktop-state/chonkstep/dock" ]; then
        cp "$S/dock-pins" "$R/private/var/db/desktop-state/chonkstep/dock" && chmod 644 "$R/private/var/db/desktop-state/chonkstep/dock"
    fi
    echo "SF_INSTALL dock: $(ls -l "$R/usr/bin/chonkstep-dock" | awk '{print $5}') bytes, items: $(cd "$R/usr/share/applications" && ls *.desktop 2>/dev/null | tr '\n' ' ')"
fi
for c in reboot halt poweroff shutdown; do
    if [ -e "$R/bin/$c" ] && ! grep -q '/sbin/sf-halt' "$R/bin/$c" 2>/dev/null; then mv -f "$R/bin/$c" "$R/bin/$c.xnu"; fi
    put sf-command "bin/$c"
done
echo "SF_INSTALL done: $(cd "$R" && ls -l sbin/sf-halt sbin/sf-boot usr/sbin/sf-clock bin/reboot bin/halt bin/poweroff bin/shutdown usr/bin/darwin-desktop-test usr/libexec/sf-session System/Library/LaunchDaemons/org.siliconforest.desktop.plist 2>&1 | awk '{print $1 ":" $5 ":" $NF}' | tr '\n' ' ')"
