#!/bin/sh
# Builds speakersafetyd for the SiliconForest M1 guest (arm64 Mach-O, libSystem only),
# the way the compositor is built (runs/m1-desktop-010/wm-darwin-gl-desktop-build-001):
# host cargo, offline, target aarch64-apple-darwin, linked by darwin-linker.py with the
# lab's pinned cross toolchain. Result: build/speakersafetyd (+ build/otool-L.txt).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
toolchain=/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin
sdk=/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
header=$here/../include/sf_speaker_safety.h
out=$here/build
mkdir -p "$out"

# The kernel contract: the layouts and request numbers in src/device.rs (also checked
# by tests/model.rs device_abi) must be what the C header gives a Darwin compiler.
cat > "$out/contract-check.c" <<CHECK
#include "$header"
_Static_assert(sizeof(struct sfspk_info) == 72, "sfspk_info");
_Static_assert(sizeof(struct sfspk_volume) == 16, "sfspk_volume");
_Static_assert(SFSPK_IOC_INFO == 0x40485301, "SFSPK_IOC_INFO");
_Static_assert(SFSPK_IOC_PING == 0x80045302, "SFSPK_IOC_PING");
_Static_assert(SFSPK_IOC_SET_VOLUME == 0x80105303, "SFSPK_IOC_SET_VOLUME");
_Static_assert(SFSPK_MAGIC == 0xdec1be15u && SFSPK_VOLUME_MAX == 201 && SFSPK_VOLUME_LOCKED == 1, "values");
_Static_assert(SFSPK_SPEAKERS == 4 && SFSPK_SENSE_CHANNELS == 8, "counts");
CHECK
DARWIN_SDK_ROOT=$sdk SDKROOT=$sdk "$toolchain/arm64-apple-darwin20.4-clang" \
    -std=c11 -fsyntax-only -isystem "$sdk/usr/include" "$out/contract-check.c"
echo "contract check against $header: ok"

# PREFIX is where the daemon looks for share/speakersafetyd/apple/j293.conf by default
export SDKROOT=$sdk
export PREFIX=${PREFIX:-/usr}
export CARGO_TARGET_DIR=${CARGO_TARGET_DIR:-$here/target}
export CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=$here/darwin-linker.py
export CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS="-C panic=unwind"

cargo rustc --offline --locked --release \
    --manifest-path "$here/Cargo.toml" --bin speakersafetyd \
    --target aarch64-apple-darwin -- -C link-arg=-Wl,-dead_strip_dylibs

bin=$CARGO_TARGET_DIR/aarch64-apple-darwin/release/speakersafetyd
cp "$bin" "$out/speakersafetyd"
"$toolchain/arm64-apple-darwin20.4-otool" -L "$out/speakersafetyd" | tee "$out/otool-L.txt"

# only libSystem may be needed
libs=$("$toolchain/arm64-apple-darwin20.4-otool" -L "$out/speakersafetyd" | tail -n +2 | awk '{print $1}')
if [ "$libs" != "/usr/lib/libSystem.B.dylib" ]; then
    echo "error: needs more than libSystem: $libs" >&2
    exit 1
fi
ls -l "$out/speakersafetyd"
