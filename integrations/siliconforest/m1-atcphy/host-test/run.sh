#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/../include" -c "$here/../src/sf_atc_usb2.c" -o "$tmp/atc.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/atc.cpp" "$tmp/atc.o" -o "$tmp/atc"
"$tmp/atc"
${CC:-cc} -std=gnu11 -O1 -g -fsanitize=address,undefined -I "$here/../include" -I "$here/../upstream" -c "$here/../src/sf_atc_dp.c" -o "$tmp/dp.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/dp.cpp" "$tmp/dp.o" -o "$tmp/dp"
"$tmp/dp"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/../include" -c "$here/../src/sf_atc_modes.c" -o "$tmp/modes.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/modes.cpp" "$tmp/atc.o" "$tmp/dp.o" "$tmp/modes.o" -o "$tmp/modes"
"$tmp/modes"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/../include" -c "$here/../src/sf_atc_calibration.c" -o "$tmp/cal.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/calibration.cpp" "$tmp/modes.o" "$tmp/atc.o" "$tmp/dp.o" "$tmp/cal.o" -o "$tmp/cal"
"$tmp/cal"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/../include" -c "$here/../src/sf_atc_pipe.c" -o "$tmp/pipe.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/pipe.cpp" "$tmp/pipe.o" -o "$tmp/pipe"
"$tmp/pipe"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/observation.c" -o "$tmp/observation"
"$tmp/observation"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/../include" -c "$here/../src/sf_atc_pipe_bootstrap.c" -o "$tmp/bootstrap.o"
${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/../include" "$here/bootstrap.cpp" "$tmp/bootstrap.o" -o "$tmp/bootstrap"
"$tmp/bootstrap"
