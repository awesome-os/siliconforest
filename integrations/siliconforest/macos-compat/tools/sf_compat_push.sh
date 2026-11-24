#!/bin/bash
# SPDX-License-Identifier: MIT
# The macOS-compatibility additions, sent to the RUNNING SiliconForest on the M1 over SSH (the
# disk root is written by SiliconForest itself; nothing is written to its partition from
# Linux). Server, key and known-hosts file are ../../m1-storage/tools/sf_userland_push.sh's.
#   tools/sf_compat_stage.sh            first (runs/sf-compat/stage)
#   sf_compat_push.sh replace [leaf]    each library of replace.list (or the one named): the new
#                                       file is written beside the installed one, and
#                                       ../sf-lib-install.sh checks its SHA-256 there before
#                                       anything loads it, proves that programs run with it, keeps
#                                       the file in use as <path>.old and only then renames
#   sf_compat_push.sh new               the files the root does not have (stage/new), as a tar
#                                       stream unpacked at /; a path that exists there is refused
#                                       beforehand; then every file is hashed there (before any
#                                       program has loaded it) and compared with the list made here
#   sf_compat_push.sh check             ../guest/sf-compat-check.sh on the target (writes under
#                                       /private/tmp/sfcompat only; its own tmux socket)
#   sf_compat_push.sh ssh [command...]
# Way back: for a replaced library `ln <path>.old <path>.back && mv -f <path>.back <path>`;
# the new files are listed in runs/sf-compat/stage/new.sha256 and new.links (nothing else is touched).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); cd "$ROOT"
C=integrations/siliconforest/macos-compat; ST=${SF_COMPAT_STAGE:-runs/sf-compat/stage}; W=runs/sf-compat   # SF_COMPAT_STAGE: another stage of the same layout
SF_HOST=${SF_HOST:-10.1.1.211}; SF_KNOWN=${SF_KNOWN:-$HOME/.ssh/known_hosts_m1_siliconforest}
SSH=(ssh -o UserKnownHostsFile="$SF_KNOWN" -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o HostKeyAlias=m1-siliconforest
     -o PasswordAuthentication=no -o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=10 "root@$SF_HOST")
probes() {  # the probes, to /private/tmp/sf-test
  tar -C "$ST/test" --owner=0 --group=0 --numeric-owner -cf - . | "${SSH[@]}" 'mkdir -p /private/tmp/sf-test && cd /private/tmp/sf-test && tar -xf - && chmod 755 * && ls | tr "\n" " "; echo'
}
case "${1:?replace|new|check|ssh}" in
replace)
  probes
  while read -r path want probe programs; do
    leaf=$(basename "$path"); [ -z "${2:-}" ] || [ "$2" = "$leaf" ] || continue
    f=$ST/replace/$leaf.new; [ "$(sha256sum "$f" | cut -c1-64)" = "$want" ] || { echo "$f is not the file of replace.list" >&2; exit 2; }
    "${SSH[@]}" "cat > '$path.sf-new' && sync" < "$f"
    "${SSH[@]}" "sh -s '$path' '$path.sf-new' $want /private/tmp/sf-test/$(basename "$probe") $programs" < "$C/sf-lib-install.sh" \
      || echo "SFLIB $leaf: status $? (see the lines above; nothing is changed unless a line says installed)"
  done < "$ST/replace.list";;
new)
  [ -s "$ST/new.sha256" ] || { echo "nothing new staged" >&2; exit 2; }
  # refuse beforehand if any path exists there (a file or a link; directories are shared)
  { cut -c66- "$ST/new.sha256"; cut -f2 "$ST/new.links"; } | tr '\n' '\0' | "${SSH[@]}" 'cd / && n=0; while IFS= read -r -d "" f; do if [ -e "$f" ] || [ -L "$f" ]; then echo "EXISTS /$f"; n=$((n + 1)); fi; done; echo "SFNEW existing paths: $n"' > "$W/new.exists" || true
  tail -1 "$W/new.exists"
  grep -q '^SFNEW existing paths: 0$' "$W/new.exists" || { grep '^EXISTS' "$W/new.exists" | head -20; echo "paths exist on the target: nothing was sent (stage against the target's present state, or SF_NEW_RESUME=1 to send only what is missing)" >&2; [ -n "${SF_NEW_RESUME:-}" ] || exit 1; }
  (cd "$ST/new" && find . -mindepth 1 \( -type f -o -type l \) -printf '%P\0') > "$W/new.members"
  if [ -n "${SF_NEW_RESUME:-}" ]; then grep '^EXISTS' "$W/new.exists" | sed 's|^EXISTS /||' > "$W/new.skip"; tr '\0' '\n' < "$W/new.members" | grep -vxFf "$W/new.skip" | tr '\n' '\0' > "$W/new.members.2"; mv "$W/new.members.2" "$W/new.members"; fi
  t0=$(date +%s)
  tar -C "$ST/new" --owner=0 --group=0 --numeric-owner --format=gnu --no-recursion --null -T "$W/new.members" -cf - | gzip -1 | "${SSH[@]}" '
    X=/usr/local/libexec/sf-gtar; [ -x $X ] || { echo "no GNU tar on the target"; exit 1; }
    gunzip | $X -x -p --numeric-owner --keep-old-files -C / -f - 2>&1 | tail -n 5; sync'
  echo "SFNEW sent in $(( $(date +%s) - t0 )) s"
  cut -c66- "$ST/new.sha256" | tr '\n' '\0' | "${SSH[@]}" 'cd / && xargs -0 sha256sum 2>&1' | sed 's/  / /' > "$W/new.remote.sha256" || true
  if diff "$ST/new.sha256" "$W/new.remote.sha256" > "$W/new.diff"; then echo "SFNEW verified: $(grep -c . "$ST/new.sha256") files equal on the target"
  else echo "SFNEW DIFFERS: $(grep -c '^[<>]' "$W/new.diff") lines, e.g. $(head -3 "$W/new.diff" | cut -c1-160 | tr '\n' ' ')"; exit 1; fi
  cut -f2 "$ST/new.links" | tr '\n' '\0' | "${SSH[@]}" 'cd / && xargs -0 readlink 2>&1' > "$W/new.remote.links" || true
  if diff <(cut -f1 "$ST/new.links") "$W/new.remote.links" > "$W/new.linkdiff"; then echo "SFNEW links: $(grep -c . "$ST/new.links" || true) equal"
  else echo "SFNEW LINKS DIFFER: $(grep -c '^[<>]' "$W/new.linkdiff") lines"; exit 1; fi;;
check)
  probes
  "${SSH[@]}" "ED_SECS=${ED_SECS:-5} sh -s" < "$C/guest/sf-compat-check.sh";;
ssh) shift; exec "${SSH[@]}" "$@";;
*) echo "replace|new|check|ssh" >&2; exit 2;;
esac
