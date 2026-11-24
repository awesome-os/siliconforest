#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a boot's console that matter for SFAppleNVMe. Usage: nv_summary.sh <console.bin>
strings -n 6 "${1:?console}" | grep -aE '^(NV_|NVT_)|SFAppleNVMe:|SFAns:|SFSoc: pmgr|panic\(|ApfsFileSystemDriver|DART|SError|ext4' | grep -av 'NV_BEGIN\|NV_END' | cut -c1-900 | head -${LINES_MAX:-400}
