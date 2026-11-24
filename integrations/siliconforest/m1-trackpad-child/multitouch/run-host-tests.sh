#!/bin/sh
# Host tests of the multitouch decoder (no hardware, no kernel).
set -eu
cd "$(dirname "$0")"
out=${TMPDIR:-/tmp}/m1tp_mt_test.$$
trap 'rm -f "$out"' EXIT
for cc in "${CC:-cc}"; do
  "$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -o "$out" test_m1tp_mt.c m1tp_mt.c m1tp_mt_selftest.c -lm
  "$out"
  "$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -o "$out" test_m1tp_pinch.c -lm
  "$out"
done
