#!/bin/sh
# SPDX-License-Identifier: MIT
# Host test of the policy code (no kernel, no hardware).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=undefined,address \
    -I"$here/../src" -I"$here/../../m1-thermal/src" \
    "$here/test_policy.c" "$here/../src/sf_cpufreq_policy.c" "$here/../../m1-thermal/src/sf_thermal_policy.c" \
    -lm -o "$out/test_policy"
"$out/test_policy"
rm -rf "$out"
