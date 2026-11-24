#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${CC:-cc}" -D_GNU_SOURCE -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$here" "$here/observer.c" -ldl -o "$work/observer"
ASAN_OPTIONS=detect_leaks=1 "$work/observer"
