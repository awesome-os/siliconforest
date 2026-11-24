#!/bin/bash
# SPDX-License-Identifier: MIT
# The desktop stream's M1 chores (SiliconForest on the disk), from the workstation.
#   desk_m1.sh state                       compositor, marks, inject directory, dock
#   desk_m1.sh install <next-bin> <daily-bin> [tag]
#                                          /usr/bin/chonkstep-darwin-desktop.next and /usr/bin/chonkstep-darwin-desktop
#                                          replaced; the files of before are kept as <path>.before-<tag> (hard links)
#   desk_m1.sh put <local> <remote> [tag]  any other file, the one of before kept as <remote>.before-<tag>
#   desk_m1.sh dock <chonk-dock>           the dock as /usr/bin/chonkstep-dock (the one of before kept), its launcher
#                                          items (../system/applications) in /usr/share/applications, the default
#                                          pins (../system/dock-pins) as /usr/share/chonkstep/dock-pins (what a dock
#                                          that finds no pins starts with, DESK6) and as the session's pins where it
#                                          has none yet; it starts with the next compositor start (sf-session)
#   desk_m1.sh dock-defaults [tag]         the session's pins set to the default set (the file of before kept as
#                                          dock.before-<tag>); the running dock reloads them at once
#   desk_m1.sh cursors                     the pointer set as an Xcursor theme in /usr/share/icons/default, written by
#                                          the installed .next itself (DESK7); `cursors-remove` takes it away
#   desk_m1.sh relaunch next|daily         the session starts its compositor again (sf-session of 14 November,
#                                          night, or later): every window ends. Take runs/m1-coord/gui.lock first.
#                                          DESK8: refused unless the owner has been idle 5 min, no process is stuck
#                                          in the kernel (ps state U or E: a GPU client stuck so made the next
#                                          compositor hang opening the DRM device, 23:20Z on 15 November) and a
#                                          test open of every /dev/dri/card* returns within 3 s (DESK_FORCE=1 skips)
#   desk_m1.sh preflight                   those checks alone
#   desk_m1.sh policy [key=value ...]      the desk's switches that change without a relaunch (DESK8, policy.rs):
#                                          shown, or set in /private/var/db/desktop-state/chonkstep/policy
#                                          (DESK10: also scroll_speed=25..400, the trackpad's scrolling speed in %)
#   desk_m1.sh theme [<id>]                THEME: the desk dressed in a built-in theme and its wallpaper, kept for the
#                                          next start, as a pick in the menu's Theme rows (no relaunch; the change
#                                          cross-fades); without an id: the theme and wallpaper worn now and the ids.
#                                          The SiliconForest themes: sf-clearing sf-canopy sf-wafer sf-dieshot
#   desk_m1.sh wallpaper <id>              THEME: the desk's wallpaper by id, kept (a built-in one, host:<file name>
#                                          for a file of /usr/share/backgrounds/siliconforest, a SiliconForest theme's
#                                          id); the owner's own before the themes: host:siliconforest
#   desk_m1.sh wallpapers [tag]            THEME: ../themes/wallpapers/sf-*.jpg into /usr/share/backgrounds/siliconforest
#                                          (a file of before kept as <file>.before-<tag>); hashed on the M1. The desk
#                                          lists that directory when it starts; a theme's own picture is read when the
#                                          theme is put on.
#   desk_m1.sh reboot-next                 restart into Linux, bless SiliconForest for the next boot only, restart,
#                                          wait for SiliconForest with .next and the inject directory (gui.lock first)
set -euo pipefail
SF=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes
    -o HostKeyAlias=m1-siliconforest -o BatchMode=yes -o ConnectTimeout=6 root@10.1.1.211)
LX=(ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=6 chrisk@10.1.1.211)
D=/usr/bin/chonkstep-darwin-desktop
state() {
  "${SF[@]}" "ls -la $D $D.next /private/var/db/desktop-* /usr/bin/chonkstep-dock 2>&1 | grep -v '^total'; ls -d /private/var/run/gpu-desktop/inject 2>&1;
    ps axo pid,etime,command | grep -E 'chonkstep-darwin-desktop|chonkstep-dock|sf-session' | grep -v grep | cut -c1-150; shasum -a 256 $D $D.next 2>/dev/null | cut -c1-16"
}
put() {
  local src=$1 dst=$2 tag=$3
  "${SF[@]}" "cat > $dst.desk-new && chmod 755 $dst.desk-new" < "$src"
  "${SF[@]}" "[ -e $dst ] && [ ! -e $dst.before-$tag ] && ln $dst $dst.before-$tag; mv -f $dst.desk-new $dst; shasum -a 256 $dst | cut -c1-16"
}
preflight() {
  "${SF[@]}" 'F=/private/var/run/gpu-desktop/owner-input; ok=1
    now=$(($(date +%s)*1000)); act=$(grep "^active=" $F 2>/dev/null | cut -d= -f2); last=$(grep "^last_real_input_unix_ms=" $F 2>/dev/null | cut -d= -f2)
    pid=$(pgrep -x chonkstep-darwin-desktop.next || pgrep -x chonkstep-darwin-desktop); up=$(ps -o etime= -p "$pid" 2>/dev/null | tr -d " ")
    upm=$(echo "${up:-0}" | awk -F"[-:]" "{n=NF; m=\$(n-1); if (n>=3) m+=\$(n-2)*60; if (n>=4) m+=\$(n-3)*1440; print int(m)}")
    if [ "$act" != 0 ]; then echo "PREFLIGHT owner active or no owner-input file (active=$act)"; ok=0
    elif [ "${last:-0}" -gt 0 ] && [ $(( (now - last) / 1000 )) -lt 300 ]; then echo "PREFLIGHT owner input $(( (now - last) / 1000 )) s ago (< 300)"; ok=0
    elif [ "${last:-0}" = 0 ] && [ "$upm" -lt 5 ]; then echo "PREFLIGHT no owner input since the compositor started $up ago (< 5 min): unknown"; ok=0
    else echo "PREFLIGHT owner idle (last real input: ${last:-0}, compositor up $up)"; fi
    stuck=$(ps axo stat=,pid=,comm= | awk "\$1 ~ /^[UE]/" | head -5)
    if [ -n "$stuck" ]; then echo "PREFLIGHT processes stuck in the kernel (U/E): $stuck"; ok=0; fi
    for c in /dev/dri/card*; do
      ( exec 3<>"$c" ) 2>/dev/null & p=$!; i=0; while kill -0 $p 2>/dev/null && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
      if kill -0 $p 2>/dev/null; then echo "PREFLIGHT open of $c did not return in 3 s (pid $p): a new compositor would hang"; ok=0; else echo "PREFLIGHT open of $c returned"; fi
    done
    [ $ok = 1 ]'
}
case "${1:-}" in
  state) state;;
  preflight) preflight;;
  policy)
    shift
    if [ $# = 0 ]; then "${SF[@]}" 'cat /private/var/db/desktop-state/chonkstep/policy 2>/dev/null || echo "(no policy file: defaults)"; grep -a WMPOLICY /var/log/desktop-session.log | tail -3'
    else
      lines=$(printf '%s\n' "$@")
      printf '%s\n' "$lines" | grep -qvE '^[a-z_]+=(on|off)$|^scroll_speed=[0-9]+$' && { echo "key=on|off or scroll_speed=25..400 only" >&2; exit 2; }
      printf '%s\n' "$lines" | "${SF[@]}" 'P=/private/var/db/desktop-state/chonkstep/policy; T=$P.desk-new; [ -e $P ] && cp $P $T || : > $T
        while read -r l; do k=${l%%=*}; grep -v "^$k=" $T > $T.2; echo "$l" >> $T.2; mv $T.2 $T; done; mv $T $P; cat $P'
      echo "$(date -u +%FT%TZ) DESK policy $* (no relaunch; policy.rs)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt"
    fi;;
  install) put "$2" $D.next "${4:-desk}"; put "$3" $D "${4:-desk}";;
  put) put "$2" "$3" "${4:-desk}";;
  dock)
    put "$2" /usr/bin/chonkstep-dock "${3:-desk}"
    (cd "$(dirname "$0")/../system" && tar -cf - applications dock-pins) | "${SF[@]}" 'T=/private/tmp/desk-items.$$; mkdir -p $T /usr/share/applications /private/var/db/desktop-state/chonkstep && tar -xf - -C $T &&
      for f in $T/applications/*.desktop; do cp "$f" /usr/share/applications/ && chmod 644 "/usr/share/applications/$(basename "$f")"; done;
      mkdir -p /usr/share/chonkstep && cp $T/dock-pins /usr/share/chonkstep/dock-pins && chmod 644 /usr/share/chonkstep/dock-pins;
      [ -e /private/var/db/desktop-state/chonkstep/dock ] || { cp $T/dock-pins /private/var/db/desktop-state/chonkstep/dock && chmod 644 /private/var/db/desktop-state/chonkstep/dock; };
      rm -rf $T; ls /usr/share/applications; cat /private/var/db/desktop-state/chonkstep/dock';;
  dock-defaults)
    "${SF[@]}" "S=/private/var/db/desktop-state/chonkstep/dock; [ -e \$S ] && [ ! -e \$S.before-${2:-desk} ] && cp -p \$S \$S.before-${2:-desk};
      XDG_STATE_HOME=/private/var/db/desktop-state XDG_RUNTIME_DIR=/private/var/run/gpu-desktop WAYLAND_DISPLAY=wayland-0 /usr/bin/chonkstep-dock --defaults; cat \$S"
    echo "$(date -u +%FT%TZ) DESK dock pins set to the default set (dock-defaults)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt";;
  cursors)
    "${SF[@]}" '[ -e /usr/share/icons/default ] && { echo "/usr/share/icons/default exists: not touched"; exit 2; }
      mkdir -p /usr/share/icons && /usr/bin/chonkstep-darwin-desktop.next --write-cursor-theme /usr/share/icons/default.desk-new &&
      mv /usr/share/icons/default.desk-new /usr/share/icons/default && ls /usr/share/icons/default/cursors | wc -l'
    echo "$(date -u +%FT%TZ) DESK pointer set installed as the Xcursor theme /usr/share/icons/default" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt";;
  cursors-remove)
    "${SF[@]}" 'rm -rf /usr/share/icons/default; ls /usr/share/icons';;
  relaunch)
    case "$2" in next) word=relaunch-next;; daily) word=relaunch;; *) echo "next|daily" >&2; exit 2;; esac
    [ "${DESK_FORCE:-0}" = 1 ] || preflight || { echo "relaunch refused by the preflight (DESK_FORCE=1 to override)"; exit 3; }
    "${SF[@]}" "grep -q relaunch-next /usr/libexec/sf-session || { echo 'the running session has no relaunch: reboot-next'; exit 1; }
      [ -e /private/var/db/desktop-inject ] || touch /private/var/db/desktop-inject
      echo $word > /private/var/run/gpu-desktop/session-end-request; pkill -TERM -x chonkstep-darwin-desktop || pkill -TERM -x chonkstep-darwin-desktop.next
      i=0; while [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); [ -S /private/var/run/gpu-desktop/wayland-0 ] && [ ! -e /private/var/run/gpu-desktop/session-end-request ] && break; done; sleep 2"
    echo "$(date -u +%FT%TZ) DESK compositor relaunch ($2)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt"
    state;;
  theme)
    if [ -z "${2:-}" ]; then
      "${SF[@]}" 'cd /private/var/db/desktop-state/chonkstep && echo "theme=$(cat theme 2>/dev/null) wallpaper=$(cat wallpaper 2>/dev/null)"; /usr/bin/chonkstep-darwin-desktop --desktop-request theme - 2>&1 | tail -1'
    else
      case "$2" in *[!a-z0-9-]*) echo "a theme id" >&2; exit 2;; esac
      "${SF[@]}" "/usr/bin/chonkstep-darwin-desktop --desktop-request theme $2 && cd /private/var/db/desktop-state/chonkstep && echo \"theme=\$(cat theme) wallpaper=\$(cat wallpaper)\""
      echo "$(date -u +%FT%TZ) DESK theme $2 (desktop request; no relaunch)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt"
    fi;;
  wallpaper)
    case "${2:-}" in ""|*[!a-z0-9:_-]*) echo "a wallpaper id" >&2; exit 2;; esac
    "${SF[@]}" "/usr/bin/chonkstep-darwin-desktop --desktop-request wallpaper $2 && cd /private/var/db/desktop-state/chonkstep && echo \"theme=\$(cat theme) wallpaper=\$(cat wallpaper)\""
    echo "$(date -u +%FT%TZ) DESK wallpaper $2 (desktop request; no relaunch)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt";;
  wallpapers)
    for f in "$(dirname "$0")"/../themes/wallpapers/sf-*.jpg; do
      n=$(basename "$f"); R=/usr/share/backgrounds/siliconforest/$n
      "${SF[@]}" "mkdir -p /usr/share/backgrounds/siliconforest && cat > $R.desk-new && chmod 644 $R.desk-new" < "$f"
      "${SF[@]}" "[ -e $R ] && [ ! -e $R.before-${2:-theme} ] && ln $R $R.before-${2:-theme}; mv -f $R.desk-new $R; shasum -a 256 $R | cut -c1-16" | sed "s|^|$n $(sha256sum "$f" | cut -c1-16) on the M1: |"
    done;;
  reboot-next)
    "${SF[@]}" "touch /private/var/db/desktop-next-try /private/var/db/desktop-inject; sync; (sleep 1; /bin/reboot) > /dev/null 2>&1 &" || true
    echo "$(date -u +%FT%TZ) DESK reboot (SiliconForest -> Linux -> SiliconForest, .next with inject)" >> "$(dirname "$0")/../../../../runs/m1-coord/log.txt"
    sleep 25
    for i in $(seq 1 40); do "${LX[@]}" true 2>/dev/null && break; sleep 9; done
    "${LX[@]}" 'sudo asahi-bless --next --set-boot SiliconForest -y && (sleep 1; sudo systemctl reboot) > /dev/null 2>&1 &'
    sleep 30
    for i in $(seq 1 60); do "${SF[@]}" true 2>/dev/null && break; sleep 9; done
    for i in $(seq 1 30); do "${SF[@]}" 'pgrep -x chonkstep-darwin-desktop.next > /dev/null && [ -d /private/var/run/gpu-desktop/inject ]' && break; sleep 5; done
    state;;
  *) sed -n 3,21p "$0"; exit 2;;
esac
