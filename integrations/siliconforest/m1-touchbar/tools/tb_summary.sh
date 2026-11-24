#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a boot's console that matter for SFAppleTouchBar. Usage: tb_summary.sh <console.bin>
strings -n 4 "${1:?console.bin}" |
    grep -a 'SFM1SPIHID: \(Fn\|the keyboard\)\|SFTouchBar:\|SFAdp:\|SFZ2:\|SFSpi:\|SFSoc:\|SFDartFixed:\|SFGpio:\|^TB_\|^TBTEST\|^TBCRC\|TBKEY\|panic\|Panic\|SError\|hw\.\(ncpu\|activecpu\)' |
    cut -c1-900 | head -${LINES_MAX:-400}
