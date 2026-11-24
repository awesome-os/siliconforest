#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=gnu11 -Dat_least=static -O2 -g -fsanitize=address,undefined \
 -include "$here/host-test/crypto-linux/linux/kernel.h" -I "$here/host-test/crypto-linux" -I "$here/pininc" \
 "$here/upstream/lib/crypto/sha256.c" "$here/upstream/lib/crypto/memneq.c" "$here/upstream/lib/uuid.c" "$here/host-test/crypto.c" -o "$tmp/crypto"
python3 "$here/host-test/crypto.py" "$tmp/crypto"
