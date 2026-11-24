#!/bin/sh
# SPDX-License-Identifier: MIT
# Turns a copy of the RAM root into a root that can be mounted READ-ONLY: everything the
# system writes is reached through symbolic links into /private/ram, where process 1
# (user/sf-roinit.c, installed as /usr/appleinternal/sbin/launchd.sfro and chosen with the boot
# argument launchdsuffix=sfro) mounts the memory disk. The directories it replaces are kept
# beside the links (*.disk), so the change is undone by moving them back. Idempotent.
# Usage: sf-root-transform.sh <root directory> <sf-roinit binary> [state device list file]
# Run by tools/dr_linux_root.sh on the M1's Linux (on the mounted SiliconForest partition
# only) and on the host over a copy of the source image, for the reference digest.
set -eu
R=${1:?root}; INIT=${2:?sf-roinit}; DEVS=${3:-}
cd "$R"
[ -x sbin/launchd ] && [ -d System ] && [ -d private ] || { echo "not a SiliconForest root: $R" >&2; exit 2; }
mkdir -p private/ram
swap() {  # <path> <link target>
    if [ ! -L "$1" ]; then mv "$1" "$1.disk"; ln -s "$2" "$1"; fi
}
swap var private/ram/var
swap private/var ram/private/var
swap private/tmp ram/private/tmp
swap etc/resolv.conf ../private/ram/etc/resolv.conf
mkdir -p usr/appleinternal/sbin
cp "$INIT" usr/appleinternal/sbin/launchd.sfro; chmod 755 usr/appleinternal/sbin/launchd.sfro
[ -z "$DEVS" ] || cp "$DEVS" etc/sf-state-devices
# the bench types /usr/bin/darwin-desktop-test '<nonce>': each boot's own script is in the
# RAM root image, which is the memory disk
if [ ! -e usr/bin/darwin-desktop-test.staged ]; then mv usr/bin/darwin-desktop-test usr/bin/darwin-desktop-test.staged; fi
cat > usr/bin/darwin-desktop-test <<'DISPATCH'
#!/bin/sh
# Disk root: this boot's guest script is in the RAM root image (memory disk on /private/ram).
[ -r /private/ram/usr/bin/darwin-desktop-test ] || { echo "SF_DISKROOT no script in /private/ram"; exit 1; }
exec /bin/sh /private/ram/usr/bin/darwin-desktop-test "$@"
DISPATCH
chmod 755 usr/bin/darwin-desktop-test
echo "transformed: $(ls -ld var private/var private/tmp etc/resolv.conf | awk '{print $9 $10 $11}' | tr '\n' ' ')"
