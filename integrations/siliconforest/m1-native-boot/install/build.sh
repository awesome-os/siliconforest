#!/bin/bash
# SPDX-License-Identifier: MIT
# Build everything the SiliconForest install needs (INSTALL.md), on this workstation. Host only:
# nothing is sent to a device, nothing is installed, nothing is fetched from the network.
#   build.sh [output directory, default ./out]      (out/ is never committed)
# Inputs
#   sources/m1n1-baseline at the pin, patches of ../b1 and of ./patches
#   ../../branding/siliconforest-boot.svg   the boot-screen logo of both stages
#                                           (SF_BOOT_LOGO=<svg or png>: another picture)
#   ../../branding/siliconforest-icon.svg   the startup-picker / volume icon (SF_ICON=<svg or png>)
#   private/disk-root/sf-part.env           the root's identifiers (they go into boot.bin, never into git)
#   the collection ../../m1-storage/build-kc-native-rw-halt.nix   (SF_KC=<directory>: another)
#   the Asahi installer's source at e93743d (SF_INSTALLER_SRC=<checkout>, default runs/m1-storage/ref/asahi-installer)
# Outputs
#   sf-stage1.bin        stage 1: m1n1 that chainloads sf/boot.bin from a FAT partition named by
#                        PARTUUID (RELEASE, CHAINLOADING, SF_INSTALLED, our logo). The installer
#                        appends the chainload= line and puts the result into the stub.
#   sf-stage2.bin        stage 2: the stage of ../b1 (m1n1 + pdsf), RELEASE, SF_INSTALLED, our logo, with
#                        m1n1's FAT reader (CHAINLOADING) and SF_KERNEL_FILE: it reads sf/kernel.bin itself
#   boot.bin             = sf-stage2.bin, nothing behind it: the file sf/boot.bin of the boot partition.
#                        Stage 1 can only start an image that fits in its own footprint (m1n1
#                        chainload.c, "image span overlaps the live heap"): the build FAILS if it does not.
#   kernel.bin           configuration + collection as a record list: the file sf/kernel.bin, read by stage 2
#   kernel-bench.bin     the same with `sfsession_seconds=150 sfmark=1` added to the command line, for the
#                        bench proof (a session that ends itself); differs in the configuration record only
#   boot-bench.bin       stage 2 + the records of kernel-bench.bin behind it (bench only, DR_BOOTBIN: the
#                        appended-records path, entered from RAM; too large for a stage 1)
#   sf-stage1-test.bin   stage 1 as the installer would write it, with a PARTUUID that does not exist (bench)
#   sf-stage1-installed.bin  the same with the PARTUUID of the real boot partition (SF_BOOT_PARTUUID in
#                        sf-part.env, when it is there): the boot object that is in the stub (bench)
#   siliconforest.icns   the icon
#   mac-kit/, siliconforest-mac-kit.tar   what is copied to macOS (INSTALL.md step 2)
#   build.json           sizes and digests (no identifiers)
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); B1=$HERE/../b1
SRC=$ROOT/sources/m1n1-baseline; PIN=121fcbf35d4501814b58ca5347926a4173b6a512
INS=${SF_INSTALLER_SRC:-$ROOT/runs/m1-storage/ref/asahi-installer}; INS_PIN=e93743d
ART=$ROOT/integrations/siliconforest/branding
LOGO=${SF_BOOT_LOGO:-$ART/siliconforest-boot.svg}; ICON=${SF_ICON:-$ART/siliconforest-icon.svg}
XNU_BASE=${SF_XNU_BASE:-0x850000000}
export PATH=$HOME/.nix-profile/bin:$PATH
OUT=$(mkdir -p "${1:-$HERE/out}" && cd "${1:-$HERE/out}" && pwd)
die() { echo "$*" >&2; exit 2; }
[ "$(git -C "$SRC" rev-parse HEAD)" = "$PIN" ] || die "sources/m1n1-baseline is not at $PIN"
[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ] || die "sources/m1n1-baseline has local changes"
case "$(git -C "$INS" rev-parse HEAD)" in $INS_PIN*) ;; *) die "$INS is not the Asahi installer at $INS_PIN";; esac
[ -z "$(git -C "$INS" status --porcelain)" ] || die "$INS has local changes"
[ -f "$LOGO" ] && [ -f "$ICON" ] || die "artwork missing"
. "$ROOT/private/disk-root/sf-part.env"
case "$SF_FSUUID$SF_PARTUUID" in *[!0-9A-Fa-f-]*) die "sf-part.env: not identifiers";; esac
KC=${SF_KC:-$(cd "$ROOT" && env -u SF_PD integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py m1-storage build-kc-native-rw-halt.nix)}
[ -f "$KC/kernel" ] || die "no collection"
WORK=${SF_WORK:-$(mktemp -d)}; [ -n "${SF_WORK:-}" ] || trap 'rm -rf "$WORK"' EXIT; mkdir -p "$WORK"

# ---- artwork -------------------------------------------------------------------------------
png() { # <svg or png> <pixels> <out.png>
  case "$1" in *.svg) rsvg-convert -w "$2" -h "$2" "$1" -o "$3";; *) magick "$1" -resize "$2x$2" "$3";; esac
}
for s in 128 256; do
  png "$LOGO" $s "$WORK/boot_$s.png"
  # m1n1's built-in logo: raw RGBA on black, as data/makelogo.sh makes it
  magick "$WORK/boot_$s.png" -background black -flatten -depth 8 "rgba:$WORK/siliconforest_$s.bin"
  [ "$(stat -c %s "$WORK/siliconforest_$s.bin")" = $((s * s * 4)) ] || die "logo $s: wrong size"
done
python3 -B - "$ICON" "$WORK" "$OUT/siliconforest.icns" <<'PY'
import struct, subprocess, sys
icon, work, out = sys.argv[1:]
# Apple icon file: PNG members by pixel size (ic07 128, ic08 256, ic09 512, ic10 1024; ic11..ic14 the @2x names)
members = [(b'ic11', 32), (b'ic12', 64), (b'ic07', 128), (b'ic13', 256), (b'ic08', 256), (b'ic14', 512), (b'ic09', 512), (b'ic10', 1024)]
body = b''
for name, size in members:
    png = '%s/icon_%d.png' % (work, size)
    if icon.endswith('.svg'):
        subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), icon, '-o', png], check=True)
    else:
        subprocess.run(['magick', icon, '-resize', '%dx%d' % (size, size), png], check=True)
    data = open(png, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    body += name + struct.pack('>I', 8 + len(data)) + data
open(out, 'wb').write(b'icns' + struct.pack('>I', 8 + len(body)) + body)
PY

# ---- the two m1n1 builds ------------------------------------------------------------------
m1n1() { # <name> <version tag> <with the stage's records: 0|1> <make variables...>
  local name=$1 tag=$2 pdsf=$3; shift 3
  local W=$WORK/$name; rm -rf "$W"; cp -a "$SRC" "$W"
  ( cd "$W"
    find build -maxdepth 1 -type f ! -name librust.a -delete
    rm -rf build/.deps build/chickens build/dcp build/hv build/math build/tinf build/minilzlib build/dlmalloc build/libfdt
    # the Rust library is rebuilt when the build's features differ from the pinned tree's (chainloading)
    # (a command-line CARGO_FLAGS replaces the Makefile's own: the feature is named here)
    local cargo="--locked --offline"
    case " $* " in *" CHAINLOADING=1 "*) rm -f build/librust.a; cargo="$cargo --features chainload";; esac
    if [ "$pdsf" = 1 ]; then
      for p in "$B1"/patches/*.patch; do git apply "$p"; done
      cp "$B1"/src/pdsf.c "$B1"/src/pdsf.h "$B1"/src/pdsf_boot.c "$B1"/src/pdsf_el2.S src/
      printf '#define PDSF_ROOT_UUID "%s"\n' "$SF_FSUUID" > src/pdsf_root.h
      mkdir -p build
      clang --target=aarch64-none-elf -march=armv8.5-a -c "$B1/../stub/el2_drop.S" -o build/pdsf_el2_stub.o
      llvm-objcopy -O binary build/pdsf_el2_stub.o build/pdsf_el2.bin
      [ "$(sha256sum build/pdsf_el2.bin | cut -d' ' -f1)" = "$(sed -n "s/^STUB_SHA256 = '\(.*\)'/\1/p" "$B1/../native_stub.py")" ] || die "el2_drop.S does not assemble to the booted page"
    fi
    for p in "$HERE"/patches/*.patch; do git apply "$p"; done
    # stage 2 only (stage 1's source stays exactly what the stage 1 in the stub was built from)
    [ "$pdsf" != 1 ] || { for p in "$HERE"/patches/stage2/*.patch; do git apply "$p"; done; cp "$HERE/src/pdsf_file.c" src/; }
    cp "$WORK/siliconforest_128.bin" "$WORK/siliconforest_256.bin" data/
    M1N1_VERSION_TAG=$tag make -j"$(nproc)" USE_CLANG=1 CARGO_FLAGS="$cargo" RELEASE=1 SF_INSTALLED=1 \
      BUILTIN_LOGO=siliconforest "$@" build/m1n1.bin > "$OUT/$name.log" 2>&1 || { tail -30 "$OUT/$name.log" >&2; exit 1; }
    ! grep -n 'warning:' "$OUT/$name.log" | grep 'pdsf\|utils.c\|main.c' || die "warnings in our sources"
    grep -a -q "##m1n1_ver##$tag" build/m1n1.bin || die "the image does not carry its tag"
    grep -a -q 'SF_STOPPED' build/m1n1.bin || die "the image is not the installed variant"
    grep -q '^#define SF_INSTALLED$' build/build_cfg.h && grep -q '^#define RELEASE$' build/build_cfg.h || die "build configuration"
    cp build/m1n1.bin "$OUT/$name.bin" )
}
m1n1 sf-stage1 "$PIN-siliconforest-stage1-v1" 0 CHAINLOADING=1
grep -q '^#define CHAINLOADING$' "$WORK/sf-stage1/build/build_cfg.h" || die "stage 1 cannot chainload"
# The stage 1 that is in the stub of the installed machine (13 November 2026). It can only be replaced
# from recovery mode, and everything below is fitted to it: a build that makes another one stops here.
STAGE1_INSTALLED=44c1ef0bda96224f5fe8c6228cc12ec1966b7781f38844cea960b21b47654318
[ "$(sha256sum "$OUT/sf-stage1.bin" | cut -d' ' -f1)" = "$STAGE1_INSTALLED" ] || [ "${SF_NEW_STAGE1:-0}" = 1 ] ||
  die "sf-stage1.bin is not the stage 1 that is installed (SF_NEW_STAGE1=1: build a new one, which needs a new install)"
m1n1 sf-stage2 "$PIN-siliconforest-boot-v2" 1 CHAINLOADING=1 SF_KERNEL_FILE=1
grep -q '^#define SF_KERNEL_FILE$' "$WORK/sf-stage2/build/build_cfg.h" || die "stage 2 does not read sf/kernel.bin"
! grep -q '^#define SF_KERNEL_FILE$' "$WORK/sf-stage1/build/build_cfg.h" || die "stage 1 is built as a stage 2"
grep -a -q 'sf/kernel.bin' "$OUT/sf-stage2.bin" || die "stage 2 does not name sf/kernel.bin"
[ "$(grep -a -c -F "$SF_FSUUID" "$OUT/sf-stage2.bin")" -ge 1 ] || die "stage 2 does not carry the root's filesystem UUID"

# ---- boot.bin -----------------------------------------------------------------------------
# The command line of boot dr30 (the stage, the disk's own session, no bench) with sfnvme_ttl=0
# (no time limit on the storage controller) and without the two words that end a bench session.
ARGS="-v debug=0x14e keepsyms=1 serial=0x13 serial-device-name=uart0 disable-uart-irq=1 serial_video_mirror=0 sftextboot=0 -noprogress"
ARGS="$ARGS sfm1kbd=1 sfm1trackpadenum=1 sfm1trackpadchild=1 cpus=8 sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2"
ARGS="$ARGS rd=uuid boot-uuid=$SF_FSUUID wdt=-1 sfnvme=3 sfnvme_ttl=0 sfnvme_part=$SF_PARTUUID sfnvme_part_lba=$SF_PART_LBA sfnvme_part_blocks=$SF_PART_BLOCKS"
ARGS="$ARGS sfnvme_write=1 sfnvme_readfence=1 sf_quiet_wait=0 sf_rtc_wait=0 sfthermal_delay=4 sfcpuheld=1 sfapciebridge=1"
# the video decoder (SFAppleAVD, in the collection): /dev/sfavd; nothing is powered before its first open
ARGS="$ARGS sfavd=1"
# no Skywalk compat netif on legacy interfaces (macOS's default 0x2f without IF_ATTACH_NX_NETIF_COMPAT):
# en0 (IO80211Family, legacy TX model) got one that is never activated, since no flowswitch is attached
# to a legacy-TX interface, and DLIL does not count input on an interface marked IFCAP_SKYWALK (the
# compat netif would): en0's ipackets/ibytes stayed 0 (SF-AC, "Wi-Fi receive statistics")
ARGS="$ARGS if_attach_nx=46${SF_EXTRA_BOOTARGS:+ $SF_EXTRA_BOOTARGS}"
pack() { # <command line> <output> [--m1n1 <stage>]
  local args=$1 out=$2; shift 2
  python3 -B "$B1/pdsf_pack.py" pack "$@" --kernel "$KC/kernel" --bootargs "$args" \
    --set display=1 --set usb-nodes=remove0 --set sprr=1 --set amx=1 --set "xnu-base=$XNU_BASE" -o "$out" > /dev/null
  python3 -B "$B1/pdsf_pack.py" list "$out" > /dev/null || die "$out: records"; }
cp "$OUT/sf-stage2.bin" "$OUT/boot.bin"; chmod 644 "$OUT/boot.bin"
pack "$ARGS" "$OUT/kernel.bin"
pack "$ARGS sfsession_seconds=150 sfmark=1" "$OUT/kernel-bench.bin"
pack "$ARGS sfsession_seconds=150 sfmark=1" "$OUT/boot-bench.bin" --m1n1 "$OUT/sf-stage2.bin"
# What stage 1 can start (sources/m1n1-baseline src/chainload.c, chainload_image): the next image is copied
# to stage 1's own base, and once stage 1's heap is in use (it always is: the file was read into it) an
# image whose end, ALIGN_UP(file + the variable lines stage 1 passes on + 4, 16 KiB), lies above the
# top_of_kernel_data stage 1 inherited is refused. iBoot starts our stage 1 as a raw object with that top
# somewhere behind the image and its variable lines; the only room that is certain is stage 1's own image.
# (The installed machine, 13 November: stage 1 at 0x805494000, its variable lines end at 0x8058b4082; the
# 14,423,700-byte file was refused, so that top was below 0x806258000.) Variable lines: 512 bytes allowed
# (stage 1 passes its version and the partition, 169 bytes). Margin asked for: 32 KiB.
S1=$(stat -c %s "$OUT/sf-stage1.bin"); S2=$(stat -c %s "$OUT/boot.bin")
SPAN=$(( (S2 + 512 + 4 + 16383) / 16384 * 16384 )); SF_FIT_MARGIN=$((32 << 10))
[ $((SPAN + SF_FIT_MARGIN)) -le "$S1" ] || die "boot.bin does not fit behind stage 1's base: its span $SPAN + margin $SF_FIT_MARGIN > stage 1's $S1 bytes (stage 1 would refuse it: image span overlaps the live heap)"
echo "boot.bin fits in stage 1's footprint: span $SPAN of $S1 bytes, $((S1 - SPAN)) to spare"
[ "$(stat -c %s "$OUT/kernel.bin")" -le $((200 << 20)) ] || die "kernel.bin is larger than stage 2 reads (256 MiB) with room to spare"

# ---- stage 1 as the installer writes it (asahi-installer src/m1n1.py build(): image, the variable
# lines, four zero bytes), with a partition identifier that no partition has: the bench's fail-safe test
{ cat "$OUT/sf-stage1.bin"; printf 'chosen.asahi,efi-system-partition=%s\nchainload=%s;sf/boot.bin\n' \
    5f5f5f5f-0000-4000-8000-53465f544553 5f5f5f5f-0000-4000-8000-53465f544553; printf '\0\0\0\0'; } > "$OUT/sf-stage1-test.bin"
rm -f "$OUT/sf-stage1-installed.bin"
if [ -n "${SF_BOOT_PARTUUID:-}" ]; then
  case "$SF_BOOT_PARTUUID" in *[!0-9a-f-]*) die "sf-part.env: SF_BOOT_PARTUUID is lower-case hexadecimal, as the installer writes it";; esac
  { cat "$OUT/sf-stage1.bin"; printf 'chosen.asahi,efi-system-partition=%s\nchainload=%s;sf/boot.bin\n' \
      "$SF_BOOT_PARTUUID" "$SF_BOOT_PARTUUID"; printf '\0\0\0\0'; } > "$OUT/sf-stage1-installed.bin"
fi

# ---- what goes to macOS -------------------------------------------------------------------
K=$OUT/mac-kit; rm -rf "$K"; mkdir -p "$K/installer/boot" "$K/installer/os" "$WORK/pkg/esp/sf"
( cd "$INS/src" && cp -R -L -- *.py step2 "$K/installer/" )
cp -R -L "$INS/asahi_firmware" "$K/installer/"; rm -rf "$K/installer/asahi_firmware/__pycache__"
cp "$INS/LICENSE" "$K/installer/LICENSE.asahi-installer"
patch -s -d "$K/installer" -p1 < "$HERE/installer/main-menu.patch"
cp "$HERE/installer/installer_data.json" "$K/installer/"
cp "$OUT/sf-stage1.bin" "$K/installer/boot/m1n1.bin"
cp "$OUT/siliconforest.icns" "$K/installer/logo.icns"
echo "asahi-installer-$INS_PIN-siliconforest-1" > "$K/installer/version.tag"
python3 -B - "$OUT" "$K/installer/os/siliconforest.zip" <<'PY'
import sys, zipfile
out, target = sys.argv[1:]
# the installer's extract_tree makes a directory only for a directory member: they come first
with zipfile.ZipFile(target, 'w', zipfile.ZIP_STORED) as z:
    for name in ('esp/', 'esp/sf/'):
        info = zipfile.ZipInfo(name); info.external_attr = (0o40755 << 16) | 0x10; z.writestr(info, b'')
    z.write(out + '/boot.bin', 'esp/sf/boot.bin')
    z.write(out + '/kernel.bin', 'esp/sf/kernel.bin')
    z.write(out + '/siliconforest.icns', 'siliconforest.icns')
PY
cp "$HERE/mac/sf-install.sh" "$HERE/mac/sf-preflight.sh" "$HERE/mac/sf-uninstall.sh" "$K/"; chmod 755 "$K"/*.sh
( cd "$K" && find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > MANIFEST.sha256 )
tar -C "$OUT" -cf "$OUT/siliconforest-mac-kit.tar" mac-kit

python3 -B - "$OUT" "$PIN" "$INS_PIN" "$XNU_BASE" "$KC" <<'PY'
import hashlib, json, pathlib, sys
out, pin, ins, xnu, kc = pathlib.Path(sys.argv[1]), *sys.argv[2:]
f = lambda n: dict(bytes=(out / n).stat().st_size, sha256=hashlib.sha256((out / n).read_bytes()).hexdigest())
names = ['sf-stage1.bin', 'sf-stage2.bin', 'boot.bin', 'kernel.bin', 'kernel-bench.bin', 'boot-bench.bin', 'sf-stage1-test.bin', 'siliconforest.icns', 'siliconforest-mac-kit.tar']
record = dict(status='BUILT_ONLY', m1n1_base_commit=pin, asahi_installer_commit=ins, xnu_base=xnu, collection=kc,
              files={n: f(n) for n in names}, installed=False)
(out / 'build.json').write_text(json.dumps(record, indent=1) + '\n')
for n in names:
    print('%-28s %10d  %s' % (n, record['files'][n]['bytes'], record['files'][n]['sha256']))
PY
