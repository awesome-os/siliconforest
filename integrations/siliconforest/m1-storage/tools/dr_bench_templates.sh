#!/bin/bash
# SPDX-License-Identifier: MIT
# The bench's prelude and end script of a disk-root boot ended by the system's own command
# (guest/diskroot-halt-prelude.sh, guest/darwin-diskroot-halt.in) as the TEMPLATES a boot
# without a RAM root runs from the disk (/private/var/db/sf-bench/prelude.in, end.in; filled
# in at boot by system/sf-session): what they read from the memory disk in a boot with a RAM
# root, they read from /private/var/db/sf-bench. The @...@ fields are left as they are.
#   dr_bench_templates.sh <output directory>     writes bench-prelude.in and bench-end.in
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); OUT=${1:?output directory}
for t in "diskroot-halt-prelude.sh=bench-prelude.in" "darwin-diskroot-halt.in=bench-end.in"; do
  sed -e 's|/private/ram/usr/bin|/private/var/db/sf-bench|g' -e 's|/private/ram/etc/sfnvme-ranges|/private/var/db/sf-bench/sfnvme-ranges|g' \
      -e 's|/private/ram/usr/lib/libSystem.B.dylib|/private/var/db/sf-bench/libSystem.B.dylib.ref|g' "$HERE/guest/${t%%=*}" > "$OUT/${t##*=}"
done
