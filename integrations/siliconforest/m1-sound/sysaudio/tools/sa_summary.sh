#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a boot's console that matter for the system audio path. Usage: sa_summary.sh <console.bin>
strings -n 4 "${1:?console.bin}" |
    grep -a '^SA_\|coreaudiod\|sftone\|sfvolume\|pcmplay\|SFSound: \(playing\|stopped\|codec input\|could not\|jack\)\|SFSpeakers: \(playing\|stopped\|SHUT\|speaker volumes\|unavailable\|ready\)\|kern\.sf_sound\.volume_db\|sfaudio\|panic\|Panic\|SError\|dyld\|not found\|Killed\|Abort\|Segmentation\|Bus error' |
    cut -c1-420 | head -${LINES_MAX:-400}
