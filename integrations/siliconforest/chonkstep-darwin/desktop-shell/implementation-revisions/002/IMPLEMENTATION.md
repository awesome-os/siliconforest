# Isolated native Shell entry — source revision 002

This is source and host-test evidence. It is not a built Darwin frontend,
hardware admission, or a successful native desktop session. Checkpoint001
(`4202fedc…`, patch `add1c06e…`) remains immutable with its original H1 defect
and reviews. `BASE.json` binds that exact checkpoint. No active frontend,
controller, kernel, application or packet was changed.

`shell.patch` is cumulative from the saved deployed717 source;
`from-001.patch` is the delta from checkpoint001. Both reproduce the same
changed files. `source-validation.json` records before/after hashes and
`verify_checkpoint.py` checks both replays, the full source-tree identity and
external dependency hashes. The source tree is ignored working material.

## Actual entry and dependency contract

The new separate binary is `chonkstep-darwin-desktop`, built with the explicit
`native-managed-spawn` feature. The original `chonkstep-darwin` main is unchanged.
The only accepted execution syntax, in this order, is:

```
chonkstep-darwin-desktop --run --socket ABS --records ABS --session HEX48 --terminal ABS --seconds 11..180 --font-regular ABS --font-bold ABS --input pdhid [--startup-terminals 1..2]
```

This entry requires Apple ARM64, `panic=unwind`, actual Both input, fresh
private0700 socket/records directories, a fresh socket and lock pathname, an
explicit executable and real bounded regular font files. Host/other builds
refuse before device opens. A present socket or lock refuses without probing,
unlinking or guessing from errno. Socket paths are capped at100 bytes before
either binding or child launch. Startup checks precede HID/PDGOP acquisition.

The entry creates the actual shared Shell and CPU backend, adopts the private
owned-app manager, then handles real Wayland and PDHID input. Terminal and End
Session use the shared cascade/managed bridge. There is no automatic startup
observer, command injection, detached app or mock menu. Standalone titlebars
expose Close only; the inherited Miniaturize button was removed because this
bounded profile has no restore workflow. The generic profile is unchanged by
that selection. Broader desktop capabilities remain future work.

Omitting `--startup-terminals` queues no startup action. The explicit suffix
accepts only canonical1 or2 and is applied once, before event dispatch. It invokes
the shared Shell's typed SpawnTerminal action, then the same bounded
TerminalRequest queue, manager and real status gates as menu/Super+Return.
Its immutable request provenance is `explicit_startup` (ordinary menu/shortcut
is `input`) in both durable journal and snapshot. It does not synthesize keys,
claim menu activation, create a hidden observer, bypass admission, or manufacture
READY. Tests compare the actual request types and observe zero Wayland key or
held-chord events from startup. This provides an unattended packaged workflow
without changing the normal default or generic config-autostart policy.

The exact root IPC002 leaf is composed as a compile dependency. Its independent
review approves that scope; the standalone profile still creates no control
listener. IPC001 is preserved under `dependency-revisions/ipc001`. Native spawn
uses only the exact external C ABI/object from root, with flags0x400e and four
explicit stdio/status file actions. This tree neither copies nor substitutes
that implementation. Normal Rust Command appears only in Linux test fixtures.

Root must cross-link the actual C object, static PDGOP/IOKit, xkbcommon and the
pinned runtime closure, inspect the resulting artifact, and bind a concrete
packet. The ordinary terminal is the frozen source216de1cd/application contract;
the built binary09643f14 is a separately owned input, not executed here. The
Darwin FD-whitelist fixture and its independently qualified result remain a
separate prerequisite; host source/link flags do not establish that fact. Root
now reports its one fixture completed with only0..3, flags0x400e, child wait0 and
verified Linux; that raw capture is not reviewed here and its independent
qualification is pending at this checkpoint.

## Fonts

`FontState::from_packaged_fonts` constructs an empty font database and loads
only the two supplied files. It requires real DejaVu Sans normal400 and bold700
faces and glyphs for printable ASCII; a regular face silently substituted for
bold is rejected. Locale is explicitly en-US. Existing generic `FontState::new`
retains its behavior. The frontend supplies this state at Server construction,
so it does not first scan ambient system fonts.

Host tests use the recorded local DejaVu fixture hashes. No font bytes are
copied or relicensed here. Root must package the proper license and exact font
bytes and bind both hashes before any guest run. This is not a general Unicode
font-coverage guarantee.

## Outcome and cleanup

H1 is fixed only here. An unsuccessful APPLICATION wait status becomes a sticky
attempt failure, independently of a complete PTY acknowledgement. The actual
application raw wait status is retained. A normally exited app can legitimately
report an intentionally terminated PTY (including raw9); that PTY status is not
required to be zero. Resource `Completion::Complete` remains a cleanup fact;
the entry also requires `has_failures()==false` for an exit0 session result.

Every ordinary frame Result, signal, deadline and End Session drives the same
owned shutdown before display, HID and framebuffer owners are dropped. The
coordinator catches unwinding Rust UI-frame panics. Desktop/Shell handlers now
borrow the stored owner instead of taking it out of Server, so an assertion
does not drop its children during unwind. After leaving the frame loop, cleanup
does not dispatch UI callbacks, repaint, accept clients or acquire more input.
It only starts/polls the private app status and direct-child wait/signal path.
A real PopupHost assertion test verifies retained owner/audit state. A caught
panic always makes the session unsuccessful even if resources are retired.

The existing25-second app READY, TERM5s/KILL3s, post-reap EOF2s bounds remain.
The outer cleanup phase allows10 seconds. A signal goes only to the positively
owned direct application PID, never an arbitrary descendant or process group.
If forced app KILL leaves its separate PTY session behind, missing CLEANUP is
sticky incomplete: app reap alone never claims PTY retirement. No attempt is
made to discover or signal descendants after losing the owning protocol.

SIGABRT, abort-on-OOM, foreign unwinds, a second panic in cleanup/destruction,
and blocked kernel/filesystem/spawn calls are not covered by the UI catch.
They cannot yield a sealed successful result. Root's independent hard timeout
and mandatory Linux recovery remain the fallback; this is not a universal
in-process cleanup guarantee. There is no blocking destructor reaper.

The exclusive private `session-result.txt` and application journal preserve
first failures, actual gates and statuses. READY is the frozen application's
aggregate assertion, not inferred SSD map order or individual gate proof.
A zero-app clean session is not evidence of launching or rendering an app.
Root must qualify actual app READY/results, records and physical/runtime scope
separately. Startup errors may leave an empty result file and exit nonzero;
absence of a final result is never successful cleanup.

## Host evidence and remaining gates

Host tests exercise strict CLI/font/fresh-path checks, actual Wayland SHM/modal
input, fragmented private status, owned Linux waitpid, and error cleanup. The
real Linux exit7 regression uses actual waitpid and a synthetic PTY ACK; it does
not prove Darwin exec descriptor behavior. The synthetic nonzero/signalled
cases retain their actual supplied raw status and never hide app failure.
Concurrent fixture forks can briefly delay Unix EOF, so tests use bounded
polling rather than assuming close is immediately visible.

Source-aware coordinator mutations require each cleanup/error/unwind guard.
The test-only clippy zombie-process exception documents that the manager's
UnixWait, not std::Child, is the one real waitpid owner; it does not bypass reap.
Two inherited renderer lint allowances remain command-line-only as in001.

Pending: independent review of this002 delta and finite entry, native SDK/link
and import closure, qualified C-spawn runtime prerequisite, actual app/font and
private-path package bindings, later root-owned finite workflow and verified
Linux recovery. No new physical input, performance, MT, GPU or Wi-Fi claim.
