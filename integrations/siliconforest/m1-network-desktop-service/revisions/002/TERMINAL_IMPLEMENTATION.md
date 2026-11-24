# Fixed-ENV terminal implementation002

Canonical implementation is terminal/ here. The sibling client/ and startup.env
are Root-owned; session/, desktop_proof/, record_guard/ and backend/ have their
own owners/receipts. The older top-level client/ is an unbuilt authoring draft,
excluded from every terminal build/freeze and never a candidate payload.

The actual retained frontend is revision007, binaryfda8368021285a604251ab5e94be6baee74ecb127d07068844ae935d773311a1,
SDK report shared-shell-native-arm64-build-005.json. Revision004 is the instance
layout lineage, not the current executable. Existing frontend007 and app002
remain unchanged. STARTUP_CONTRACT.md was already dependency-bound by the frozen
record guard; this addendum corrects that historical frontend reference without
changing its byte hash or the accepted file/JSON schemas.

The new binary keeps the same absolute-socket/instance/records/lifetime CLI,
CHONKTERM/1 frames, PS1, TERM=dumb, XKB/SSD/grid/SHM and accepted-write handling.
Only the source-bound pre-fork ENV additions enable the fixed foreground script.
Initial /bin/sh -i +m and its actual bounded FD_CHECKED/internal READY/exec EOF
sequence are preserved. No exec or post-libinfo close/sweep is introduced.

startup.rs verifies exact source-embedded script bytes, absent .zwc, trusted root
ancestors, modes, identity ancestry and fresh fixed output names before fork.
It parses unique-key ASCII integer JSON and canonical file/PID/marker fields.
The private return file must exactly match the normalized own-session/instance/
shell/client/status PTY line. Actual later native prompt suffix "$ \\x1b[?2004h"
and live tcgetpgrp==owned shell PID are separate required observations. Neither
READY nor a prompt by itself proves the client ran. No raw response HTML goes
through this parser; the fixed client prints only bounded ASCII summaries.

The final old result keys remain; startup.complete additionally requires complete
successful client bytes/tuples, matching PID record, normal status0 shell return,
real prompt and foreground observation. client_exit_status is shell "$?", not an
invented app waitpid result for a grandchild. App direct-shell raw wait remains
its actual wait (possibly TERM/KILL at finite shutdown). A forced shell/group
kill without normal source-bound client return leaves descendant cleanup unproven.
Startup failure makes the app exit nonzero even if its direct PTY cleanup finished.
Root's actual closed-record validator independently rechecks all private records;
these JSON flags never substitute for real source/stream/file evidence.

The revised pure coordinator is coordinator/ here: whole180s, desktop90s,
frontend drain15s, daemon drain5s and link drain40s. Normal frontend source owns
its internal10-second cleanup; the model's15-second stage permits bounded outer
reap/drain margin. Root's native child owner separately owns its actual TERM/KILL
grace and remains bounded by the whole owner deadline. The pure module performs
no signals or proof-producing I/O. Integer masks are requests/validated inputs,
not authenticated runtime evidence. Initial coordinator001 is unchanged.

Validation: 50 serial host tests pass (real host PTYs/Wayland with synthetic input
plus strict startup parser/marker/identity tests). No startup network/Darwin claim
from those tests. Pure C revision002 passes708 ASan/UBSan assertions. SDK build003
is the current artifact; build002 is a superseded, unbooted authoring build.
Canonical build report is desktop-network-terminal-arm64-build-002.json.
Strict all-target clippy also exposes inherited baseline style lints (input,
status, PTY state match, host fixture and session argument count); these are not
runtime failures and inherited approved source was not rewritten to silence them.
New startup warnings were corrected. SDK's two remaining messages concern xcrun
auto-discovery on Linux (wrapper supplies pinned SDK) and duplicate identical
macOS minimum-version flags. New app has not been staged or target executed.
