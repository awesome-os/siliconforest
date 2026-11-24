#!/bin/sh
# SPDX-License-Identifier: MIT
# Host test of the session's rule for an end of the compositor (m1-storage/system/sf-session,
# the function decide_after_end between its "policy" markers): nothing but an explicit request
# ends an installed system's session; an end by itself starts the same build once, then the daily
# build, then .safe with a growing pause; 10 minutes of running start the count afresh; bench boots
# keep the old rule. Stub compositors in a temporary directory; nothing of the M1 is touched.
#   sh integrations/siliconforest/desktop/tests/test_sf_session.sh   (exit 0 = all passed)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
S=$HERE/../../m1-storage/system/sf-session
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
sed -n '/^# ---- policy: /,/^# ---- policy end/p' "$S" > "$T/policy.sh"
grep -q 'decide_after_end()' "$T/policy.sh" || { echo "FAIL: no decide_after_end in $S"; exit 1; }
. "$T/policy.sh"
D=$T/chonkstep-darwin-desktop
for f in "$D" "$D.next" "$D.safe"; do printf '#!/bin/sh\n' > "$f"; chmod 755 "$f"; done
fails=0
check() { # name expected-request expected-next expected-failures expected-pause
    if [ "$request" != "$2" ] || { [ -n "$3" ] && [ "$next" != "$3" ]; } || [ "$failures" != "$4" ] || [ "$pause" != "$5" ]; then
        echo "FAIL $1: request=$request next=$next failures=$failures pause=$pause (expected $2 ${3:-any} $4 $5)"; fails=$((fails + 1))
    else echo "ok   $1"; fi
}
end() { request=$1; rc=$2; ran=$3; decide_after_end; }
# the drop of 00:53Z: .next ran 34 minutes, ended with rc 1, nobody asked -> not the end of the boot
timed=; failures=0; desktop=$D.next
end '' 1 2059;  check "crash after 34 min: same build again" relaunch-auto "$D.next" 1 0;  desktop=$next
end '' 1 30;    check "again within 10 min: the daily build" relaunch-auto "$D" 2 0;         desktop=$next
end '' 1 30;    check "third: .safe" relaunch-auto "$D.safe" 3 5;                            desktop=$next
end '' 139 2;   check "fourth (.safe itself): .safe, longer pause" relaunch-auto "$D.safe" 4 10; desktop=$next
failures=20; end '' 1 1; check "pause capped at 60 s" relaunch-auto "$D.safe" 21 60
# a build that ran 10 minutes starts the count afresh
failures=2; desktop=$D; end '' 1 601; check "after 10 minutes: count afresh, same build" relaunch-auto "$D" 1 0
# the daily build failing: next is .safe (no second daily)
failures=1; desktop=$D; end '' 1 5; check "daily fails twice: .safe" relaunch-auto "$D.safe" 2 0
# rc 0 without a request (SIGTERM, a deadline) is not an explicit end either
failures=0; desktop=$D; end '' 0 50; check "rc 0 without a request: relaunch" relaunch-auto "$D" 1 0
end 'garbage' 1 50; check "unknown request text: relaunch (2nd: .safe)" relaunch-auto "$D.safe" 2 0
# explicit requests pass through unchanged
for r in restart reboot poweroff shutdown relaunch relaunch-next relaunch-safe; do
    failures=0; desktop=$D.next; end "$r" 1 5; check "explicit '$r' is kept" "$r" "" 0 0
done
# no .safe on the disk: the daily build instead
rm -f "$D.safe"; failures=2; desktop=$D; end '' 1 5; check "no .safe: daily" relaunch-auto "$D" 3 5
printf '#!/bin/sh\n' > "$D.safe"; chmod 755 "$D.safe"
# bench boots: the old rule
timed=1; failures=0; desktop=$D; end '' 1 5;    check "bench, at once: .safe once" relaunch-safe "" 0 0
desktop=$D.safe; end '' 1 5;                     check "bench, .safe at once: end" "" "" 0 0
desktop=$D; end '' 1 100;                        check "bench, after 100 s: end" "" "" 0 0
[ "$fails" = 0 ] && echo "all passed" || echo "$fails failed"
exit "$fails"
