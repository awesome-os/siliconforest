#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../kernel" "$here/pmgr-native.cpp" -o "$tmp/pmgr"
"$tmp/pmgr"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../kernel" "$here/dart-reset.cpp" -o "$tmp/dart-reset"
"$tmp/dart-reset"
