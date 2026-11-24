#!/bin/sh
# SPDX-License-Identifier: MIT
# Undoes tools/sf-root-transform.sh for a READ-WRITE disk root (step 3d): the directories kept
# beside the links (*.disk) go back to their places, the links are kept as *.rolink (the way
# back: sf-root-transform.sh again moves the directories aside and makes the links anew; remove
# the *.rolink files first). The read-only wrapper launchd.sfro stays installed (unused without
# launchdsuffix=sfro). /usr/bin/darwin-desktop-test, which the bench types, still runs each
# boot's own script from the RAM root image, and now mounts that memory disk itself.
# The fenced-write test files of step 3b/3c (/private/sf-wtest) are removed. Idempotent.
# Usage: sf-root-untransform.sh <root directory>
set -eu
R=${1:?root}
cd "$R"
[ -x sbin/launchd ] && [ -d System ] && [ -d private ] || { echo "not a SiliconForest root: $R" >&2; exit 2; }
back() {  # <path>
    if [ -L "$1" ] && [ -e "$1.disk" ]; then mv "$1" "$1.rolink"; mv "$1.disk" "$1"; fi
}
back var
back private/var
back private/tmp
back etc/resolv.conf
mkdir -p private/ram private/var/sf-persist
rm -rf private/sf-wtest
cat > usr/bin/darwin-desktop-test <<'DISPATCH'
#!/bin/sh
# Disk root, read-write: this boot's guest script is in the RAM root image (/dev/md0), which
# nothing mounts in this arrangement; it is mounted here, read-only, on /private/ram.
if [ ! -r /private/ram/usr/bin/darwin-desktop-test ]; then
    /sbin/mount -t ext4 -o rdonly /dev/md0 /private/ram || { echo "SF_DISKROOT the memory disk could not be mounted"; exit 1; }
fi
[ -r /private/ram/usr/bin/darwin-desktop-test ] || { echo "SF_DISKROOT no script in /private/ram"; exit 1; }
exec /bin/sh /private/ram/usr/bin/darwin-desktop-test "$@"
DISPATCH
chmod 755 usr/bin/darwin-desktop-test
echo "untransformed: $(ls -ld var private/var private/tmp etc/resolv.conf | awk '{print $1 ":" $9 $10 $11}' | tr '\n' ' ')"
