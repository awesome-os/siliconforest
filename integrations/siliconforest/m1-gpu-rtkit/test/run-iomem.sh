#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I "$here/../src" "$here/iomem.c" -o "$tmp/iomem"
"$tmp/iomem"
