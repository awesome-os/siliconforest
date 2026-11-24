#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a boot's console that matter for SFAopAudio. Usage: mic_summary.sh <console.bin>
strings -n 4 "${1:?console.bin}" |
    grep -a 'SFAopAudio:\|SFAop:\|SFSoc:\|SFDartFixed:\|^MIC_\|^SFMIC \|sfmic\|kern\.sf_aop\|panic\|Panic\|SError\|records \(in\|out\)\|bytes transferred' |
    cut -c1-420 | head -${LINES_MAX:-600}
