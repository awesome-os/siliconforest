# Desktop network parent guards, source slice 001

These are parts of a future distinct finite desktop network owner. They do not
form a running service, a linked guest executable, or a hardware admission.
The qualified one-client resolver004 and its frozen source stay unchanged.

`owned_child` uses the existing reviewed native spawn shim with four explicit
stdio/status duplicates, flags `0x400e` and a fixed environment. It admits only
the foreground `mdnsd -debug` and the unchanged shared frontend004 selecting the
new network-terminal path, two startup terminals, native Both input and a
90-second frontend lifetime. It owns a created positive PID even if local shim
cleanup fails, bounds both private capture pipes at 64 KiB, drains at most 8 KiB
per pipe per tick and waits only with WNOHANG. ECHILD permanently forbids later
signals. Daemon TERM-to-KILL is two seconds; frontend is allowed fifteen seconds
so its own ten-second managed shutdown has scheduling margin. A forced frontend
kill is a failure, never evidence of terminal or descendant cleanup.

`session_text` accepts exactly the successful frontend004 closed
`session-result.txt` serializer: initialized native Both/shared shell, cleanup
Complete, no frame/manager/journal error, exactly two explicit startup attempts
with the expected instance layout, distinct positive app PIDs, native shim
success, READY/CLEANUP/EOF/reap gates and no forced app KILL. The finite frontend
deadline flag is recorded as either boolean: reaching the finite lifetime is an
ordinary reason to close, not a cleanup failure. Raw PTY wait statuses remain
separate and must match each actual app record. Informational exit-reason/code
signature lines are bounded but are not treated as evidence of success.

Actual frontend wait0 and both stdio EOF, private closed-file identity, two
complete app/PTY/client records, real shell return after each network child,
DNS/HTTP wire correspondence, daemon retirement, complete physical channel
credit/EOF, route restoration and verified Linux are still required. Parsing
this text alone establishes none of those facts. File access/JSON checks are a
separate original read-only module under `../record_guard`.

Replay the tests with `python3 check_child.py` and
`python3 check_session_text.py`. The former exercises fourteen real-pipe owner
scenarios and ten assertion-killed compiled mutants. The latter uses synthetic
serializer fixtures only, including valid finite-deadline closure, and six
assertion-killed compiled mutants. It does not regrade old boot receipts.
`build-guards.nix` compiles actual ARM64 Mach-O objects against the retained
PureDarwin SDK; the spawn shim is a separate unresolved link dependency at this
object-only stage. No executable or runtime success is inferred.
