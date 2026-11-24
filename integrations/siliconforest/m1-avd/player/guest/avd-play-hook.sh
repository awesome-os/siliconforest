# SPDX-License-Identifier: MIT
# Sourced by the qemu-dev session script just before it starts the compositor
# (tools/qemu_player_check.sh puts ". /usr/local/bin/avd-play-hook;" into its @ENV@ slot).
# In the background: waits for the compositor's socket, then runs each line of
# /private/var/avd/play.list as a sfvideo command line; everything goes to the console.
# AVD_AUDIO_STANDIN=1 (from play.env): coreaudiod with its documented stand-in device
# (COREAUDIOD_SPEAKER_DEVICE=/dev/null: the real AudioQueue -> coreaudiod path, paced by the
# host clock, heard by nobody), because QEMU's machine has no sound device.
(
    [ -r /private/var/avd/play.env ] && . /private/var/avd/play.env
    if [ "$AVD_AUDIO_STANDIN" = 1 ] && [ -x /usr/sbin/coreaudiod ]; then
        mkdir -p /var/log /var/run /Library/Preferences
        COREAUDIOD_SPEAKER_DEVICE=/dev/null /usr/sbin/coreaudiod > /var/log/coreaudiod.log 2>&1 &
        printf 'AVDPLAY_COREAUDIOD pid=%s standin=/dev/null\n' "$!"
    fi
    n=0
    while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] && [ "$n" -lt 400 ]; do sleep 0.5; n=$((n + 1)); done
    sleep "${AVD_SETTLE:-10}"
    printf 'AVDPLAY_%s socket=%s\n' BEGIN "$([ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] && echo yes || echo no)"
    [ -x /usr/bin/sftone ] && /usr/bin/sftone -l 2>&1 | sed 's/^/AVDPLAY_AUDIO /'
    while read -r line; do
        [ -n "$line" ] || continue
        printf 'AVDPLAY_RUN %s\n' "$line"
        # shellcheck disable=SC2086
        /usr/local/bin/sfvideo $line 2>&1 | sed 's/^/AVDPLAY_OUT /'
        printf 'AVDPLAY_EXIT\n'
    done < /private/var/avd/play.list
    [ -r /var/log/coreaudiod.log ] && tail -5 /var/log/coreaudiod.log | sed 's/^/AVDPLAY_COREAUDIOD_LOG /'
    printf 'AVDPLAY_%s\n' DONE
) > /dev/console 2>&1 &
