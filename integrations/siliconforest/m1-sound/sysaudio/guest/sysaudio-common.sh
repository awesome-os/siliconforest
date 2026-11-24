# SPDX-License-Identifier: MIT
# Shared start of the sysaudio guest scripts; tools/sa_boot.sh puts it where a template says @COMMON@.
nonce=${1:-none}
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
log=/var/log/coreaudiod.log
t0=$(date +%s)
now() { echo $(( $(date +%s) - t0 )); }
phase() { printf 'SA_PHASE %s t=%s\n' "$1" "$(now)"; }
get() { sysctl -n "$1" 2>/dev/null; }
# one line of the driver's counters
mon() {
    printf 'SA_MON %s t=%s playing=%s sessions=%s frames=%s periods=%s volume_db=%s rate=%s rate_mhz=%s lock_ms=%s late=%s ring_errors=%s jack=%s\n' \
        "$1" "$(now)" "$(get kern.sf_sound.playing)" "$(get kern.sf_sound.sessions)" "$(get kern.sf_sound.frames)" \
        "$(get kern.sf_sound.periods)" "$(get kern.sf_sound.volume_db)" "$(get kern.sf_sound.rate)" \
        "$(get kern.sf_sound.rate_mhz)" "$(get kern.sf_sound.lock_ms)" "$(get kern.sf_sound.late)" \
        "$(get kern.sf_sound.ring_errors)" "$(get kern.sf_sound.jack)"
}
# one line of the speakers' counters
monspk() {
    printf 'SA_SPK %s t=%s playing=%s sessions=%s frames=%s fifo_frames=%s volume_steps=%s hold_probe=%s trips=%s rate=%s state=%s\n' \
        "$1" "$(now)" "$(get kern.sf_speaker.playing)" "$(get kern.sf_speaker.sessions)" "$(get kern.sf_speaker.frames)" \
        "$(get kern.sf_speaker.fifo_frames)" "$(get kern.sf_speaker.volume_steps)" "$(get kern.sf_speaker.hold_probe)" \
        "$(get kern.sf_speaker.trips)" "$(get kern.sf_speaker.rate)" "$(get kern.sf_speaker.state | cut -c1-40)"
}
# bounded SECONDS COMMAND...: runs the command, killing it when it outlasts the bound
bounded() {
    limit=$1; shift
    "$@" 2>&1 &
    bpid=$!
    bn=0
    while kill -0 "$bpid" 2>/dev/null && [ "$bn" -lt "$limit" ]; do sleep 1; bn=$((bn+1)); done
    if kill -0 "$bpid" 2>/dev/null; then
        kill -9 "$bpid" 2>/dev/null
        printf 'SA_FAIL %s still ran after %s s, killed\n' "$1" "$limit"
    fi
    wait "$bpid" 2>/dev/null
}
# wait_pids SECONDS PID...: waits for background commands, killing what outlasts the bound
wait_pids() {
    limit=$1; shift
    wn=0
    for p in "$@"; do
        while kill -0 "$p" 2>/dev/null && [ "$wn" -lt "$limit" ]; do sleep 1; wn=$((wn+1)); done
        if kill -0 "$p" 2>/dev/null; then
            kill -9 "$p" 2>/dev/null
            printf 'SA_FAIL pid %s still ran after %s s, killed\n' "$p" "$limit"
        fi
        wait "$p" 2>/dev/null
    done
}
# waits until the driver has stopped, at most $1 seconds, and says how long that took
idle() {
    n=0
    while [ "$(get kern.sf_sound.playing)" = 1 ] && [ "$n" -lt "$1" ]; do sleep 1; n=$((n+1)); done
    printf 'SA_IDLE %s waited=%s playing=%s\n' "$2" "$n" "$(get kern.sf_sound.playing)"
}
# the same for the speakers
idlespk() {
    n=0
    while [ "$(get kern.sf_speaker.playing)" = 1 ] && [ "$n" -lt "$1" ]; do sleep 1; n=$((n+1)); done
    printf 'SA_IDLE %s speakers waited=%s playing=%s\n' "$2" "$n" "$(get kern.sf_speaker.playing)"
}
# the lines coreaudiod logged since the last call
logpos=0
showlog() {
    total=$(wc -l < "$log" 2>/dev/null); total=${total:-0}
    [ "$total" -gt "$logpos" ] && tail -n $((total - logpos)) "$log"
    logpos=$total
}
# starts coreaudiod (environment assignments may precede the call) and waits for its service
start_server() {
    mkdir -p /var/log /var/run /Library/Preferences
    coreaudiod > "$log" 2>&1 &
    server=$!
    n=0
    while ! grep -q 'coreaudiod: serving' "$log" 2>/dev/null && [ "$n" -lt 10 ]; do sleep 1; n=$((n+1)); done
    printf 'SA_SERVER pid=%s waited=%s alive=%s\n' "$server" "$n" "$(kill -0 "$server" 2>/dev/null && echo 1 || echo 0)"
    showlog
}
# waits for the driver; the script ends here when it cannot play
bringup() {
    phase bringup
    n=0
    while [ "$n" -lt 40 ]; do
        case "$(get kern.sf_sound.state)" in idle*|failed*|codec-only*) break;; esac
        sleep 1; n=$((n+1))
    done
    printf 'SA_STATE %s\n' "$(get kern.sf_sound.state)"
    ls -l /dev/sfaudio 2>&1
    case "$(get kern.sf_sound.state)" in
    idle*) ;;
    *)  printf 'SA_FAIL the driver is not ready for playback\n'
        printf 'SA_END_%s\n' "$nonce"; printf '\nPD_BENCH_DONE\n'; exit 0;;
    esac
    sysctl -w kern.sf_sound.volume_db=@VOLUME@ 2>&1
    # the speakers come up after the headphones say idle: wait for their state before the hold is set
    n=0
    while [ -z "$(get kern.sf_speaker.state)" ] && [ "$n" -lt 10 ]; do sleep 1; n=$((n+1)); done
    # unattended: the speakers stay 20 dB under the model's limit (a listening script lifts this)
    sysctl -w kern.sf_speaker.hold_probe=1 2>&1
}
