#!/bin/bash
# Rebuilds and runs the comparison of the kernel speaker model with the reference.
# Host only (x86_64 Linux, offline; needs gcc, cargo and alsa-lib's libasound.so.2).
# Exit status: 0 only if no scenario shows the kernel model less conservative than the
# reference and the sanitizer run is clean. Results: results/summary.txt (+ -san).
#   RANDOM_N (default 4500), RANDOM_N_SAN (default 900), THREADS (default: all cores)
set -u
here=$(cd "$(dirname "$0")" && pwd)
kernel=$here/../kernel
ref=$here/../safetyd/tests/upstream-ref
threads=${THREADS:-$(nproc)}
mkdir -p "$here/build/plain" "$here/build/san" "$here/results"
status=0

# the reference must be upstream's code
(cd "$ref" && sha256sum -c upstream.sha256) || exit 2

# what is being tested
(cd "$kernel" && sha256sum sf_speaker_model.c sf_speaker_model.h) | tee "$here/results/model-sha256.txt"

# the kext must not need a compiler helper (__divti3 and the like): no undefined symbol
# at all in the object built freestanding for arm64 with the kext recipe's toolchain
tc=/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin
sdk=/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
if [ -x "$tc/arm64-apple-darwin20.4-clang" ]; then
    for opt in -O0 -O2 -Os; do
        DARWIN_SDK_ROOT=$sdk SDKROOT=$sdk "$tc/arm64-apple-darwin20.4-clang" -std=c11 $opt -ffreestanding -fno-builtin \
            -isystem "$sdk/usr/include" -c "$kernel/sf_speaker_model.c" -o "$here/build/model-arm64$opt.o" || exit 2
        undef=$("$tc/arm64-apple-darwin20.4-nm" -u "$here/build/model-arm64$opt.o")
        if [ -n "$undef" ]; then
            echo "arm64 freestanding $opt: UNDEFINED SYMBOLS: $undef"
            status=1
        else
            echo "arm64 freestanding $opt: no undefined symbols"
        fi
    done 2>&1 | tee "$here/results/arm64-undefined.txt"
    grep -q UNDEFINED "$here/results/arm64-undefined.txt" && status=1
else
    echo "no arm64 cross compiler: undefined-symbol check NOT run" | tee "$here/results/arm64-undefined.txt"
    status=1
fi

cflags="-std=c11 -Wall -Wextra -Wconversion -g"
echo "== compiling the kernel model (gcc $cflags)"
gcc $cflags -O2 -c "$kernel/sf_speaker_model.c" -o "$here/build/plain/model.o" 2> "$here/results/warnings.txt" || exit 2
gcc $cflags -O2 -c "$here/shim.c" -o "$here/build/plain/shim.o" || exit 2
rm -f "$here/build/plain/libsfspkmodel.a"
ar rcs "$here/build/plain/libsfspkmodel.a" "$here/build/plain/model.o" "$here/build/plain/shim.o"
san="-fsanitize=undefined,address -fno-sanitize-recover=all -fno-omit-frame-pointer"
gcc $cflags -O1 $san -c "$kernel/sf_speaker_model.c" -o "$here/build/san/model.o" 2>> "$here/results/warnings.txt" || exit 2
gcc $cflags -O1 $san -c "$here/shim.c" -o "$here/build/san/shim.o" || exit 2
rm -f "$here/build/san/libsfspkmodel.a"
ar rcs "$here/build/san/libsfspkmodel.a" "$here/build/san/model.o" "$here/build/san/shim.o"
if [ -s "$here/results/warnings.txt" ]; then
    echo "compiler warnings for sf_speaker_model.c (results/warnings.txt):"
    cat "$here/results/warnings.txt"
    status=1
else
    echo "no warnings with -Wall -Wextra -Wconversion"
fi

echo "== edge cases under ASan + UBSan, C only (san-edges.c)"
gcc $cflags -O1 $san "$here/san-edges.c" "$kernel/sf_speaker_model.c" -o "$here/build/san/san-edges" || exit 2
if ASAN_OPTIONS=abort_on_error=0 "$here/build/san/san-edges" > "$here/results/san-edges.log" 2>&1; then
    tail -n 1 "$here/results/san-edges.log"
else
    echo "san-edges FAILED (results/san-edges.log):"; tail -n 20 "$here/results/san-edges.log"
    status=1
fi

echo "== plain build: all scenarios"
SFMODEL_LIBDIR=$here/build/plain SFMODEL_SAN=0 CARGO_TARGET_DIR=$here/target/plain \
    cargo build --offline --locked --release --manifest-path "$here/Cargo.toml" 2>&1 | grep -v '^warning\|^ *|\|^ *=\|^ *-->\|^$' 
"$here/target/plain/release/model-test" --threads "$threads" --random "${RANDOM_N:-4500}" \
    --out "$here/results/summary.txt" > "$here/results/run.log" 2>&1 || status=1
tail -n 4 "$here/results/run.log"

echo "== sanitizer build (UBSan + ASan on the kernel model): all scenarios"
SFMODEL_LIBDIR=$here/build/san SFMODEL_SAN=1 CARGO_TARGET_DIR=$here/target/san \
    cargo build --offline --locked --release --manifest-path "$here/Cargo.toml" 2>&1 | grep -v '^warning\|^ *|\|^ *=\|^ *-->\|^$'
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:verify_asan_link_order=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$here/target/san/release/model-test" --threads "$threads" --random "${RANDOM_N_SAN:-900}" \
    --label "sanitizer build" --out "$here/results/summary-san.txt" > "$here/results/run-san.log" 2>&1
san_rc=$?
tail -n 3 "$here/results/run-san.log"
if grep -q 'runtime error\|AddressSanitizer\|SUMMARY:' "$here/results/run-san.log"; then
    echo "SANITIZER REPORTS (results/run-san.log):"
    grep -m 20 'runtime error\|AddressSanitizer\|SUMMARY:' "$here/results/run-san.log"
    status=1
elif [ "$san_rc" -gt 1 ]; then
    echo "sanitizer run ended abnormally (status $san_rc), see results/run-san.log"
    status=1
else
    echo "sanitizers: no report"
    [ "$san_rc" -ne 0 ] && status=1
fi
# the sanitizer build must agree with the plain one on the scenarios both ran
if ! diff <(grep -E '^(ok|FAIL) ' "$here/results/summary.txt" | grep -v random) \
          <(grep -E '^(ok|FAIL) ' "$here/results/summary-san.txt" | grep -v random) > /dev/null; then
    echo "the sanitizer build's results differ from the plain build's"
    status=1
fi

{
    echo "model under test:"; cat "$here/results/model-sha256.txt"
    cat "$here/results/arm64-undefined.txt"
    if [ -s "$here/results/warnings.txt" ]; then echo "compiler warnings: see warnings.txt"; else echo "gcc -Wall -Wextra -Wconversion: no warnings"; fi
    cat "$here/results/san-edges.log" | tail -n 1
    echo "sanitizer build (UBSan + ASan), all scenarios: $(tail -n 2 "$here/results/summary-san.txt" | head -n 1); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' "$here/results/run-san.log")"
    echo
    cat "$here/results/summary.txt"
} > "$here/results/summary.tmp" && mv "$here/results/summary.tmp" "$here/results/summary.txt"
echo "== exit status $status (results in $here/results)"
exit $status
