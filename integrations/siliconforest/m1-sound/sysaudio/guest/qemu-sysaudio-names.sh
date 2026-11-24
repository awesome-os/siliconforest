#!/bin/sh
# SPDX-License-Identifier: MIT
# The audio server's names, in QEMU (tools/sa_qemu.sh types this at the guest's root shell). The
# arm64 `virt` machine has no SFAppleSound and no IOAudioEngine, so there is no sound here. What
# this shows: coreaudiod starts and serves; without a device node it has no device and the clients
# fail saying so; device nodes with the names from before the rename are not taken;
# nodes with the names the renamed kernel publishes (/dev/sfspeaker, /dev/sfaudio) are, at their
# real paths (no COREAUDIOD_SPEAKER_DEVICE); clients then play to the server, which writes the
# frames to the node; the level sysctl it fails to set is kern.sf_sound.volume_db. The nodes are
# stand-ins made with mknod from /dev/null's numbers: frames written to them go nowhere.
# The old names are put together from $o here, so that the rename tool (which would turn them into
# the new ones and empty the test) never sees them.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
log=/var/log/coreaudiod.log
o=pd; oldproduct=pure"darwin"
fails=0
ok() { printf 'SAQ_OK %s\n' "$1"; }
bad() { printf 'SAQ_FAIL %s\n' "$1"; fails=$((fails + 1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
inlog() { grep -q "$1" "$log" 2>/dev/null; }
logpos=0
showlog() {
    total=$(wc -l < "$log" 2>/dev/null); total=${total:-0}
    [ "$total" -gt "$logpos" ] && tail -n $((total - logpos)) "$log" | sed 's/^/SAQ_LOG /'
    logpos=$total
}
# waitlog SECONDS PATTERN: until the server has logged the pattern
waitlog() { n=0; while ! inlog "$2" && [ "$n" -lt "$1" ]; do sleep 1; n=$((n + 1)); done; inlog "$2"; }
# bounded SECONDS COMMAND...: output prefixed, the status in $rc; killed when it outlasts the bound
bounded() {
    limit=$1; shift
    "$@" > /tmp/saq.out 2>&1 &
    bpid=$!; bn=0
    while kill -0 "$bpid" 2>/dev/null && [ "$bn" -lt "$limit" ]; do sleep 1; bn=$((bn + 1)); done
    if kill -0 "$bpid" 2>/dev/null; then kill -9 "$bpid" 2>/dev/null; echo "killed after $limit s" >> /tmp/saq.out; fi
    wait "$bpid" 2>/dev/null; rc=$?
    sed "s/^/SAQ_OUT $1: /" /tmp/saq.out
    printf 'SAQ_RC %s %s\n' "$1" "$rc"
}
said() { grep -q "$1" /tmp/saq.out; }

printf '\nSAQ_BEGIN\n'
mkdir -p /var/log /var/run /Library/Preferences /tmp
printf 'SAQ_UNAME %s\n' "$(uname -a | cut -c1-120)"
printf 'SAQ_NODES before: %s\n' "$(ls /dev | grep -E "^(sf|$o)(audio|speaker|mic)" | tr '\n' ' ')"
printf 'SAQ_SYSCTL kern.sf_sound.state=[%s] kern.%s_sound.state=[%s] kern.sf_speaker.state=[%s]\n' \
    "$(sysctl -n kern.sf_sound.state 2>/dev/null)" "$o" "$(sysctl -n kern.${o}_sound.state 2>/dev/null)" "$(sysctl -n kern.sf_speaker.state 2>/dev/null)"
check 'the machine has no sound device node' '[ ! -e /dev/sfaudio ] && [ ! -e /dev/sfspeaker ]'

printf 'SAQ_PHASE server without a device\n'
coreaudiod > "$log" 2>&1 &
server=$!
check 'coreaudiod serves org.siliconforest.audio.coreaudiod' 'waitlog 15 "coreaudiod: serving org.siliconforest.audio.coreaudiod"'
check 'it says there is no device node' 'inlog "no IOAudioEngine and no audio device node yet"'
showlog
bounded 20 sftone -l
check 'the device list is empty' 'said "sftone: 0 devices"'
bounded 20 sftone -r 44100 -f 440 -s 1 -d 30 -n nodevice
check 'a client without a device fails and says so' '[ "$rc" != 0 ] && said "AudioQueueNewOutput failed"'
bounded 20 sfvolume get
check 'sfvolume finds no output device' '[ "$rc" != 0 ]'
bounded 20 sfplay -t 440 -s 1
check 'sfplay fails on the renamed sysctl kern.sf_sound.rate' '[ "$rc" != 0 ] && said "kern.sf_sound.rate"'

printf 'SAQ_PHASE nodes with the old names\n'
set -- $(ls -l /dev/null | sed 's/,/ /g')
major=$5; minor=$6
printf 'SAQ_NULL major=%s minor=%s mknod=%s\n' "$major" "$minor" "$(command -v mknod)"
mknod "/dev/${o}audio" c "$major" "$minor" 2>&1; mknod "/dev/${o}speaker" c "$major" "$minor" 2>&1
printf 'SAQ_OLD %s\n' "$(ls /dev | grep -E "^${o}(audio|speaker)" | tr '\n' ' ')"
check 'the old-name nodes exist' '[ -c "/dev/${o}audio" ] && [ -c "/dev/${o}speaker" ]'
sleep 3
check 'the server takes neither' '! inlog " on /dev/"'
bounded 20 sftone -l
check 'the device list is still empty' 'said "sftone: 0 devices"'

printf 'SAQ_PHASE /dev/sfspeaker\n'
mknod /dev/sfspeaker c "$major" "$minor" 2>&1
check 'the server finds Speakers at /dev/sfspeaker' 'waitlog 5 "Speakers (SFAudio:speakers) on /dev/sfspeaker"'
showlog
bounded 20 sftone -l
check 'the list has SFAudio:speakers as the default output' 'said "SFAudio:speakers.*default output"'
bounded 30 sftone -r 44100 -f 440 -s 2 -d 30 -n queue
check 'an AudioQueue client at 44100 Hz plays to the end' '[ "$rc" = 0 ] && said "done:"'
check 'the server opened /dev/sfspeaker and mixed the client' 'inlog "Speakers: opened /dev/sfspeaker" && inlog "Speakers: mixing 1 client"'
bounded 20 sfvolume set 30
bounded 20 sfvolume get
check 'sfvolume sets and reads the level' 'said "Speakers volume 30 "'
showlog

printf 'SAQ_PHASE /dev/sfaudio\n'
mknod /dev/sfaudio c "$major" "$minor" 2>&1
check 'the server finds Headphones at /dev/sfaudio' 'waitlog 5 "Headphones (SFAudio:headphones) on /dev/sfaudio"'
check 'the level it cannot set is kern.sf_sound.volume_db' 'waitlog 3 "setting kern.sf_sound.volume_db="'
bounded 30 sftone -u SFAudio:headphones -f 440 -s 2 -d 30 -n ioproc
check 'an IOProc client on SFAudio:headphones plays to the end' '[ "$rc" = 0 ] && said "done:"'
check 'the server opened /dev/sfaudio' 'inlog "Headphones: opened /dev/sfaudio"'
sleep 7
showlog
check 'nothing in the log names an old node or sysctl' '! grep -E -q "${o}audio|${o}speaker|${o}_sound|${o}_speaker|$oldproduct" "$log"'
check 'coreaudiod is still running' 'kill -0 "$server"'
kill "$server" 2>/dev/null
rm -f "/dev/${o}audio" "/dev/${o}speaker" /dev/sfspeaker /dev/sfaudio
printf 'SAQ_FAILS %s\n' "$fails"
[ "$fails" = 0 ] && printf 'SAQ_PASS\n'
printf 'SAQ_END\n'
