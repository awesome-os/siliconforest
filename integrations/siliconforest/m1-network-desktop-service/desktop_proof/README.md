# Closed desktop record proof

Original read-only C composition for `desktop-native-resolver-two-http-v1`.
This is source and host-test evidence, not a desktop or network runtime result.
License selection for original code remains pending.

Call `dnds_verify_closed(nonce, frontend_pid, observe, context, &facts)` only
**after** the exclusive parent has waited the actual source-bound frontend to
exit normally with status zero, observed both frontend stdout/stderr EOFs, and
established the private record writers are quiescent. The frontend007 manager
must be the exclusive waiter of the two actual applications. The selected
terminal variant must remain bound to its fixed foreground-child ENV script
and native client. File metadata alone cannot establish any of these premises.

Zero means every local proof predicate passed. Nonzero preserves the caller's
facts object. Arguments, nonce[49] and output must be valid disjoint storage;
serialize the call and keep the callback/context alive throughout it. Do not
reenter or mutate the borrowed callback bytes. The callback returns void: the
parent must separately reject its own sticky observation/write failure even
when this function returns zero. There are no writes, signals, process waits,
network calls, descriptor transfers, retry loops, or hidden process operations
in this module. Reader dependencies open/read/stat/close only.

The output contains two app, shell and client PIDs. All six and the supplied
frontend PID must be positive and pairwise different. This function cannot
check the caller or mdnsd PID: the parent retains those distinct-role checks.
Its facts are evidence from closed source-bound records, not reusable process
ownership authority or permission to signal any PID.

The verifier reads these exact fixed files:

* Session: `session-result.txt`, `manager-status.log`.
* Each derived `0001`/`0002` + nonce[:44] instance: `result.json`,
  `child-ready.json`, `child.json`, `network-client.json`, `network-client.pid`,
  `network-shell-return.txt`, `pty-rx.bin`, `pty-tx.bin`, `grid-final.txt`,
  `application-stdout.log`, `application-stderr.log`.

`application.json` is not a verdict input: the actual child argv/env/script
binding is supplied by the frozen terminal source and executable receipt.
Each snapshot uses unchanged record_guard, exact private path and UID/mode,
NOFOLLOW checks, stable dev/ino/size/mtime/ctime, and same acquired directory
identity for all reads within each slot. Each file is limited to 65536 bytes
(and the reader's tighter named-file limits). After all predicates, every one
of the 24 files is read again and compared by directory/file identity, length,
mtime, ctime, and SHA256. All descriptors are closed by record_guard on return.
This detects changes; it cannot make concurrent writers safe.

Raw callbacks are `session_result`, `manager_status`, and `app1_`/`app2_` plus
`result`, `child_ready`, `child`, `client`, `client_pid`, `shell_return`, `pty_rx`,
`pty_tx`, `grid`, `stdout`, `stderr`. Immediately following each raw callback,
`<kind>_sha256` carries the 32 raw digest bytes, including empty files. The raw
view is hashed before and after callback delivery. Callbacks can precede a
later proof failure; they are observations, not a success announcement. The
module budgets its exact encoded DL30 lines (64-byte chunks) to 524288 bytes;
the parent separately enforces the shared aggregate budget, including its own
observations. No callback span exceeds 65536 bytes. Callback bytes expire on
return, and no descriptor remains exposed.

The strict integer/ASCII JSON parser rejects duplicate decoded keys anywhere,
including unknown objects. The verifier binds typed app cleanup, ready/DS4/
pre-exec descriptor facts, PTY byte counts, recorded live foreground identity,
startup success and child identities. A complete shell cleanup may have raw
wait status 9. It does **not** prove a normal client return. That separate
predicate requires the exact canonical client PID file, successful private
shell-return line, matching ordered PTY `DL30_CLIENT_BEGIN`, `DL30_CLIENT_DONE`
status0, `DL30_SHELL_RETURN` status0, and a later real prompt byte pattern
`$ \x1b[?2004h`. The source-bound foreground shell waits the client before
writing its marker; no grandchild wait result is invented from killpg.

Normal incidental user typing is permitted. TX need not be empty; accepted,
written and retained TX bytes must agree and unsent bytes must be zero. The
final grid must match the bounded serializer's rectangular 20..160 by 6..60
cell shape, printable ASCII or replacement characters. This is not framebuffer
or pixel-replay proof. Successful app stdout/stderr logs must be empty; the
selected main only prints an error on its unsuccessful return.

Client records require the actual ordered 1..32 global IPv4 answers with first
API address selected, source-bound canonical tuples, same shell SID/PGID/live
foreground PID, entry-only descriptor inventory, completed socket close,
request half-close and receive EOF. The request is re-encoded independently
from the nonce and compared byte-for-byte. The actual response bytes are
re-parsed with unchanged egress HTTP validation and compared to every reported
HTTP fact. Two flows must have distinct **full tuples**; the same local port
to different peers is permitted. Packet/backend wire correspondence is a
separate offline qualification and is not synthesized here.

The manager journal requires exact successful spawn flags/result, known gate
names and exact snapshot gate timestamps, one request and native result per
app, bounded monotonic manager-clock values, and final matching zero app wait,
status-byte count and shell cleanup. Source007 starts slot2 only after slot1's
AppReady, which is independently required. Manager and NativeIo clocks have
different origins: manager values are never compared to NativeIo elapsed_ms.
The new session parser002 preserves this distinction; old parser16f4 remains
unchanged and is not linked here. Bounded known diagnostics are not verdict
inputs. Unknown event/status/failure lines fail closed. Unsupported diagnostic
kernel-message detail lines also fail closed; no success interpretation is
invented for them.

Build composition:

```
desktop_proof/desktop_proof.c
desktop_proof/manager_proof.c
record_guard/read.c
record_guard/json.c
session/revisions/002/session_text.c
../m1-network-egress-guest/codec/egress_validate.c
```

Production uses pinned SDK `CC_SHA256` (verified export in retained libSystem).
Host tests use OpenSSL solely for the equivalent digest primitive; production
objects contain no test hooks or OpenSSL imports. Large reader/parser/snapshot
storage and HTTP/hex scratch are heap allocated. New functions compile with
`-Wframe-larger-than=2048`; the unchanged inherited HTTP validator has its own
bounded 2048-entry header array on the userspace stack.

Run `python3 check.py` for actual C filesystem composition, sanitizer failures,
semantic mutations, independent full-tuple/normal-return negatives, path races,
pinned ARM64 object builds, import resolution and compiler static analysis.
The tests create isolated host files under a temporary root using the frozen
reader's compile-time test hook. They do not touch production record paths or
execute target objects. SHA receipts bind the actual input source set and its
source-only limits; tests do not authorize a boot or relax GPU observer HOLD.
