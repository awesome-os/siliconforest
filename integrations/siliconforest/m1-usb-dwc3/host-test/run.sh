#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -pthread \
    -I "$here/../kernel" "$here/lifetime.cpp" -o "$tmp/lifetime"
"$tmp/lifetime"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/../kernel" -c "$here/../kernel/sf_dwc3_host.c" -o "$tmp/dwc3.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../kernel" "$here/dwc3.cpp" "$tmp/dwc3.o" -o "$tmp/dwc3"
"$tmp/dwc3"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined \
    -I "$here/../kernel" "$here/phy-lease.cpp" -o "$tmp/phy-lease"
"$tmp/phy-lease"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined \
    -I "$here/../kernel" "$here/pmgr.cpp" -o "$tmp/pmgr"
"$tmp/pmgr"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined \
    -I "$here/../kernel" "$here/acio-pinmux.cpp" -o "$tmp/acio-pinmux"
"$tmp/acio-pinmux"
