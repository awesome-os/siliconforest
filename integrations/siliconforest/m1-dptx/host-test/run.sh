#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_dcpext_plan.c" -o "$tmp/plan.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/plan.cpp" "$tmp/plan.o" -o "$tmp/plan"
"$tmp/plan"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -I "$here/upstream" -c "$here/src/sf_dpxbar.c" -o "$tmp/xbar.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/xbar.cpp" "$tmp/xbar.o" -o "$tmp/xbar"
"$tmp/xbar"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../m1-dcp/include" "$here/host-test/mailbox.cpp" -o "$tmp/mailbox"
"$tmp/mailbox"
