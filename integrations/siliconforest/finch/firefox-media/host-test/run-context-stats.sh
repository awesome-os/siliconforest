#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cp "$here/../../m1-avd/user/sfavd.c" "$scratch/sfavd-traced.c"
cp "$here/context-stats-trace.h" "$scratch/SFContextStatsTrace.h"
python3 "$here/patch-context-stats.py" "$scratch/sfavd-traced.c"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 -I"$scratch" -I"$here/../../m1-avd/include" -I"$here/../../m1-avd/user" \
 "$here/host-test/context-stats.c" -o "$scratch/probe"
"$scratch/probe"
