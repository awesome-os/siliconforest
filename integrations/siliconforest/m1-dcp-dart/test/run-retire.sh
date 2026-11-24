#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/src/sf_dart_table.c" "$here/test/retire_test.c" -o "$tmp/retire"
"$tmp/retire"
