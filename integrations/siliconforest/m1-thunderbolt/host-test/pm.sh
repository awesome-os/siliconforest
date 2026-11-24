#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Wno-misleading-indentation -I "$here/../native" "$here/../native/sf_tb_pm.c" "$here/pm.c" -o "$tmp/pm"
"$tmp/pm"
