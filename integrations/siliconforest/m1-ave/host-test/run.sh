#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$here/include" "$here/kernel/sfave_preload.c" "$here/kernel/sfave_identity.c" "$here/host-test/preload_test.c" -o "$work/preload-test"
"$work/preload-test"
python3 "$here/host-test/test_extract.py"
# The copied GPL tests compile the owned copied source, never private inputs.
for name in abi adt; do
 unit="$here/host-test/upstream/${name}_selftest"
 source=ave_cmd
 if [[ $name == adt ]]; then source=ave_adt; fi
 "${CC:-cc}" -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror -fsanitize=address,undefined -g -I"$unit/shim" -include "$unit/kshim.h" -I"$here/kernel/upstream" "$unit/${name}_selftest.c" "$here/kernel/upstream/$source.c" -o "$work/$name-test"
 "$work/$name-test"
done

"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$here/kernel/upstream" "$here/host-test/upstream/ipc_selftest/ipc_selftest.c" -o "$work/ipc-test"
"$work/ipc-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$here/include" -I"$here/kernel" "$here/kernel/sfave_preload.c" "$here/kernel/sfave_ipc.c" "$here/host-test/ipc_test.c" -o "$work/transport-test"
"$work/transport-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$here/include" "$here/kernel/sfave_preload.c" "$here/kernel/sfave_dapf.c" "$here/host-test/dapf_test.c" -o "$work/dapf-test"
"$work/dapf-test"
