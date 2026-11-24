#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a boot's console that matter for SFAppleSound. Usage: sd_summary.sh <console.bin>
strings -n 4 "${1:?console.bin}" |
    grep -a 'microphone\|SFSpeakers:\|SFSPK_\|SFSound:\|SFSoc:\|SFI2C:\|SFCs42l83:\|SFDartFixed:\|SFMca:\|SFGpio:\|^SD_\|sfplay:\|sfaudio\|kern\.sf_sound\|panic\|Panic\|SError\|records \(in\|out\)\|bytes transferred' |
    cut -c1-420 | head -${LINES_MAX:-400}
