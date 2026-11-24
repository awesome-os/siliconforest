#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -I "$here/../native" -c "$here/../native/sf_tb_dma.c" -o "$tmp/dma.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -pthread -I "$here/../native" "$here/dma.cpp" "$tmp/dma.o" -o "$tmp/dma"
for case in ok alloc_fail atomic not_powered admission quarantine mask_shrink bad_iova bad_cpu partial overflow_cpu aperture_end below_aperture capacity busy_destroy boundary retire_fault wrong_device wrong_size duplicate_free; do
 "$tmp/dma" "$case"
done
