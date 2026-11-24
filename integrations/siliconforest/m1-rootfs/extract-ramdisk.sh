#!/bin/sh
# Copy the existing virt-minimal ext4 root out of the Nix store, then shrink
# that copy. The store image is only read. Output is gitignored.
# An existing ramdisk.img is left untouched.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
src=/nix/store/f35jpffq39xq162hylgzvfvykdl7325l-puredarwin-image-0.1/puredarwin-arm64-virt-minimal.img
dir=$root/artifacts/m1-bringup-009
out=$dir/ramdisk.img
tmp=

if [ -e "$out" ]; then
  echo "preserving existing $out" >&2
  exit 0
fi

mkdir -p "$dir"
tmp=$(mktemp "$dir/ramdisk.img.XXXXXX")
trap 'if [ -n "$tmp" ]; then rm -f "$tmp"; fi' EXIT

# e2fsck sums its result: 0 clean, 1 corrected, 2 corrected and reboot
# requested, 3 is 1|2. 4 and above mean uncorrected or operational failure.
# set -e would stop on the successful correction status 1.
check_repair() {
  status=0
  e2fsck -f -y "$1" || status=$?
  case $status in
    0|1|2|3) return 0 ;;
  esac
  echo "e2fsck -f -y rejected status $status" >&2
  exit 1
}

dd if="$src" of="$tmp" bs=512 skip=1574912 count=1048576 status=none
check_repair "$tmp"
resize2fs -M "$tmp"
status=0
e2fsck -fn "$tmp" || status=$?
if [ "$status" -ne 0 ]; then
  echo "post-shrink e2fsck -fn rejected status $status" >&2
  exit 1
fi
chmod 0444 "$tmp"
# ln fails when the destination exists. A lost race keeps the verified file.
# The trap then removes only the temporary name.
if ! ln "$tmp" "$out"; then
  if [ -e "$out" ]; then
    echo "preserving existing $out" >&2
    exit 0
  fi
  echo "failed to publish $out" >&2
  exit 1
fi
rm -f "$tmp"
tmp=
