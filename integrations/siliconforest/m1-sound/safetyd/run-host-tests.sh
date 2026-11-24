#!/bin/sh
# Host tests (x86_64 Linux): builds the upstream reference harness, then runs the
# port's tests against it. Offline; needs libasound.so.2 (alsa-lib) on the host.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
upstream=${UPSTREAM_SRC:-/home/chrisk/source/ext/speakersafetyd}

# the reference is upstream's code: refuse to run if the copies are not byte-identical
if [ -d "$upstream/src" ]; then
    for f in types.rs helpers.rs; do
        cmp "$upstream/src/$f" "$here/tests/upstream-ref/upstream/$f"
    done
    cmp "$upstream/conf/apple/j293.conf" "$here/conf/apple/j293.conf"
    echo "upstream copies identical to $upstream ($(git -C "$upstream" rev-parse --short HEAD))"
else
    echo "warning: $upstream not found, upstream copies not compared" >&2
fi
(cd "$here/tests/upstream-ref" && sha256sum -c upstream.sha256)

cargo build --offline --locked --release --manifest-path "$here/tests/upstream-ref/Cargo.toml"
export SPEAKERSAFETYD_UPSTREAM_REF="$here/tests/upstream-ref/target/release/upstream-ref"

cd "$here"
cargo test --offline --locked -- --nocapture
