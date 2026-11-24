#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the SiliconForest stage: the pinned m1n1 (sources/m1n1-baseline at 121fcbf) with
# patches/ and src/ of this directory, in a scratch copy. The pinned tree is not modified.
#   build.sh [output directory, default ./out]
#   B1_WORK=<directory>   keep the scratch copy there (default: a temporary directory, removed)
#   B1_ROOT_UUID=<filesystem UUID>  the one disk root the stage may boot (rd=uuid boot-uuid=<it>); unset: RAM roots only
#   B1_KC=<collection directory>  also pack out/sf-stage-kc.bin (stage + configuration +
#                         collection, no RAM disk) to show the size of an installable image
# Output: m1n1-sf-stage.bin (raw m1n1 image, entry 0x800), its sha256, build.json.
# Host-only: nothing is sent to a device and nothing is installed.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd)
SRC=$ROOT/sources/m1n1-baseline; PIN=121fcbf35d4501814b58ca5347926a4173b6a512
TAG=$PIN-siliconforest-stage-v1
OUT=$(mkdir -p "${1:-$HERE/out}" && cd "${1:-$HERE/out}" && pwd)
[ "$(git -C "$SRC" rev-parse HEAD)" = "$PIN" ] || { echo "sources/m1n1-baseline is not at $PIN" >&2; exit 2; }
[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ] || { echo "sources/m1n1-baseline has local changes" >&2; exit 2; }
if [ -n "${B1_WORK:-}" ]; then WORK=$B1_WORK; mkdir -p "$WORK"; else WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; fi
rm -rf "$WORK/m1n1"; cp -a "$SRC" "$WORK/m1n1"; cd "$WORK/m1n1"
# The pinned tree's build directory is reused for the Rust library only (same sources, same
# flags): every C object is rebuilt.
find build -maxdepth 1 -type f ! -name librust.a -delete; rm -rf build/.deps build/chickens build/dcp build/hv build/math build/tinf build/minilzlib build/dlmalloc build/libfdt
for p in "$HERE"/patches/*.patch; do git apply "$p"; done
cp "$HERE"/src/pdsf.c "$HERE"/src/pdsf.h "$HERE"/src/pdsf_boot.c "$HERE"/src/pdsf_el2.S src/
# The one disk root this stage may boot (src/pdsf.c pdsf_root_check): B1_ROOT_UUID=<filesystem UUID>,
# compared as text with boot-uuid= on the command line. Unset: the stage boots RAM roots only.
# The UUID identifies the owner's disk: it goes into the image (out/, never committed), not into build.json.
case "${B1_ROOT_UUID:-}" in *[!0-9A-Fa-f-]*) echo "B1_ROOT_UUID is not a UUID" >&2; exit 2;; esac
printf '#define PDSF_ROOT_UUID "%s"\n' "${B1_ROOT_UUID:-}" > src/pdsf_root.h
# The resident EL2 page: assembled from the stream's stub, and required to be byte for byte
# the page the native boots nb04..nb08 ran (native_stub.py STUB_SHA256).
mkdir -p build
clang --target=aarch64-none-elf -march=armv8.5-a -c "$HERE/../stub/el2_drop.S" -o build/pdsf_el2_stub.o
llvm-objcopy -O binary build/pdsf_el2_stub.o build/pdsf_el2.bin
WANT=$(sed -n "s/^STUB_SHA256 = '\(.*\)'/\1/p" "$HERE/../native_stub.py")
[ "$(sha256sum build/pdsf_el2.bin | cut -d' ' -f1)" = "$WANT" ] || { echo "el2_drop.S does not assemble to the booted page" >&2; exit 1; }
M1N1_VERSION_TAG=$TAG make -j"$(nproc)" USE_CLANG=1 CARGO_FLAGS="--locked --offline" build/m1n1.bin > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log" >&2; exit 1; }
! grep -n 'warning:' "$OUT/build.log" | grep 'pdsf' || { echo "warnings in the stage's sources" >&2; exit 1; }
grep -a -q "##m1n1_ver##$TAG" build/m1n1.bin || { echo "the image does not carry its tag" >&2; exit 1; }
cp build/m1n1.bin "$OUT/m1n1-sf-stage.bin"
( cd "$OUT" && sha256sum m1n1-sf-stage.bin > m1n1-sf-stage.sha256 )
python3 - "$OUT" "$TAG" "$PIN" "$HERE" <<'PY'
import hashlib, json, pathlib, subprocess, sys
out, tag, pin, here = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], pathlib.Path(sys.argv[4])
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
image = out / 'm1n1-sf-stage.bin'
record = dict(status='BUILT_ONLY_NOT_HARDWARE_VALIDATED', base_commit=pin, build_tag=tag,
              image=dict(name=image.name, bytes=image.stat().st_size, sha256=sha(image)),
              inputs={str(p.relative_to(here)): sha(p) for p in sorted(list((here / 'src').glob('*')) + list((here / 'patches').glob('*')))},
              el2_page_sha256=sha(here.parent / 'stub/el2_drop.S'),
              make='USE_CLANG=1 CARGO_FLAGS="--locked --offline" (no CHAINLOADING: no code path reads storage)',
              clang=subprocess.run(['clang', '--version'], capture_output=True, text=True).stdout.splitlines()[0],
              physical_execution=False, installed=False)
(out / 'build.json').write_text(json.dumps(record, indent=1) + '\n')
print('%s  %d bytes  sha256 %s' % (image, record['image']['bytes'], record['image']['sha256']))
PY
if [ -n "${B1_KC:-}" ]; then
  python3 -B "$HERE/pdsf_pack.py" pack --m1n1 "$OUT/m1n1-sf-stage.bin" --kernel "$B1_KC/kernel" \
    --bootargs "${B1_BOOTARGS:--v debug=0x14e keepsyms=1 serial=0x13 serial-device-name=uart0 disable-uart-irq=1 rd=md0 cpus=8 sfcpuheld=1 sf_quiet_wait=0 sf_rtc_wait=0}" \
    -o "$OUT/sf-stage-kc.bin"
  python3 -B "$HERE/pdsf_pack.py" list "$OUT/sf-stage-kc.bin"
fi
