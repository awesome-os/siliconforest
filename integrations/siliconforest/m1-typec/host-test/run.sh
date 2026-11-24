#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$HERE/../kernel" "$HERE/test_cd321x.c" -o "$TEMP/test"
"$TEMP/test"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$HERE/../kernel" "$HERE/test_power.c" -o "$TEMP/test-power"
"$TEMP/test-power"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$HERE/../kernel" "$HERE/test_control.c" -o "$TEMP/test-control"
"$TEMP/test-control"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$HERE/../kernel" "$HERE/test_events.c" -o "$TEMP/test-events"
"$TEMP/test-events"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I"$HERE/../kernel" "$HERE/test_mask.c" -o "$TEMP/test-mask"
"$TEMP/test-mask"
