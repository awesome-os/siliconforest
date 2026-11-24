# Independent closed-desktop evidence qualifier

`qualify.py` reads existing saved console bytes and a fresh lowercase hex48
nonce. It is original Python using only the standard library. It does not
invoke the C proof, import a packet policy, acquire target data, run hardware,
change files, or produce an overall runtime PASS verdict.

```
python3 -B qualify.py --console saved-console.bin --nonce HEX48 \
  [--frontend-pid N] [--backend-result saved-backend/result.json]
```

JSON goes to stdout. Exit0 means this bounded closed-record scope verified and
no supplied backend envelope failed validation. **It does not mean backend
correspondence, complete transport, daemon cleanup, routes, Linux recovery,
physical input, GPU or overall runtime passed.** The report always sets
`overall_runtime_PASS_claimed:false`. A later producer `status=FAILED` is
preserved even when the earlier closed desktop scope independently verifies.

The public Python entry point is `qualify(console_bytes, nonce,
frontend_pid=None, backend_result=None)`. It returns a structured failure for
missing/malformed evidence. `observe_records` separately exposes the24 raw
record map, other observation streams, and digest/chunk counts for independent
composition; it is only a parser, not the complete qualifier. Complete clients
are in `closed_desktop.clients`, including all app/shell/client PIDs, ordered
API addresses, full four-tuples, HTTP facts, request/response lengths/hashes.

The qualifier requires all24 raw desktop kinds and their24 raw32-byte SHA256
observations exactly once, in producer order, with every digest immediately
after its raw record. It checks exact64-byte chunks (short last chunk or one
empty chunk), canonical offsets/totals/lowerhex, complete coverage, no duplicate
or interleaved streams, no foreign observation nonce, <=65536 bytes per record,
<=8192 observation lines and <=524288 encoded observation bytes. Other parent
observation kinds can repeat, as process rows do, but remain bounded and nonce
bound. Input console files are bounded32MiB regular NOFOLLOW snapshots. CRLF
is normalized only as a line ending; malformed prefixed observation lines fail.

Independent Python parsers verify the source007 session snapshot and manager
journal, actual native spawn results, second launch after first AppReady,
source-separated clock domains, app cleanup/status byte counts, DS4/pre-exec
facts, PID identities, native-client record and full tuples, actual nonce GET
bytes and complete HTTP framing/content, and ordered BEGIN/DONE0/shell-return0
plus the later real native prompt. Normal PTY typing is permitted and counted;
TX0 is not a predicate. Final grid shape is checked, not framebuffer pixels.
Shell raw wait9 is allowed and never substituted for client normal return0.

The frontend_child observation must report actual normal wait0, both pipe EOFs,
no lost child/error/KILL, and matching captured output lengths. Its56-byte
little-endian native spawn result must match the pinned ABI/flags/PID. These
are source-bound observations, not independent authentication against a
malicious producer. Filesystem metadata cannot be reconstructed from file
contents. Therefore the final fresh `DL30_RESULT` must occur after all
observations, report `desktop_ok=1`, and match every PID. This is necessary
because the C producer can emit all24 raw records before its final file
identity/mtime/ctime/hash recheck fails. The completion seal supplies only that
filesystem-side fact; every Python content predicate is still independently
required. The producer's overall status/error are preserved, not regraded.

JSON records use a strict ASCII integer dialect with duplicate decoded keys
rejected anywhere, bounded64-bit integers, <=4096 tokens and depth32. Wrong
field types, negative zero, floats, overflow, non-ASCII strings and trailing
bytes fail. Unique unknown keys are permitted. HTTP parsing is independently
implemented here; no frozen C/Python response validator is called. The fixed
IPv4 exclusions follow the admitted original policy, not Python's changing
`is_global` database.

Optional backend input is deliberately **NOT_EVALUATED** for correspondence
in this revision. Its envelope schema/profile/nonce and bounded finite JSON
must be valid, and its file hash is recorded. The report exports client tuple
and byte hashes for a separate join. TCP4 terminal-reset events require actual
raw Ethernet/relay packet checks and source-bound causal guard evidence;
separate physical streams do not supply a shared total ordering. Summary
`policy_accepted` or `forwarded` flags alone cannot establish forwarding. The
root's separate DNS/wire qualifier remains outside this module's verdict.

Run `python3 -B check_closed.py` for synthetic saved-console cases, real-file
CLI errors and decision mutations. The synthetic baseline DATA generator is
the hash-bound frozen desktop_proof fixture constructor; its C checker/build
functions are never called. Qualifier logic, JSON/text/PTY/HTTP validation,
chunk reassembly and failure oracle are independent Python. These fixtures
are not captured target observations. `closed-validation-001.json` records
only this leaf's tests. `closed-source-validation-001.json` freezes only the
closed qualifier files; the root-owned DNS evidence modules are separate.

Source bindings include immutable C proof888ffb91, record guard2a897614,
correct frontend007 binding965aba83, clock-correct parser0024f27de0e, native
client746c298a and terminalfa6f13a4, plus exact parent003daccfbd6 completion-seal
source and native child ABI. No predecessor or old capture/receipt is edited.
GPU observer HOLD remains absolute.
