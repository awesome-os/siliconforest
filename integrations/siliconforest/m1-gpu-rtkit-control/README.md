# RTKit control discovery

This original C engine completes a bounded RTKit11/12 management negotiation
against a **fake mailbox**. Its only successful terminal label is
`CONTROL_DISCOVERED_PROTOCOL_ONLY`. It has no hardware transport, CPU RUN
operation, endpoint-start command, power-ready result, GPU initdata, doorbell,
firmware write, UAT import, MMIO or boot entry point. GPU observer HOLD remains
absolute. Host protocol success does not establish firmware or GPU readiness.

The engine queues IOP_INIT (`0x220`) at init. Explicit acceptance of that pending
packet permits HELLO. A valid overlapping version range queues HELLO_ACK; its
acceptance commits the negotiated version and permits endpoint maps. Each map
chunk queues an ACK. Its endpoint bits become committed only after acceptance.
Discovery requires validated advertised endpoints `0x20` and `0x21`, a LAST map,
and acceptance of that final ACK. Unknown system endpoints, duplicate map bases,
reserved fields and unexpected messages fail closed. There is no automatic
progress from queued bytes, a quiet mailbox, elapsed time or an advertised bit.

The API in `control.h` uses caller-owned, completely zeroed stable storage and
external serialization. No callbacks or allocations occur in the core.

- `rtc_init(ctx, now_ns, timeout_ns)` creates one pending wake packet.
- `rtc_tx_peek` returns immutable pending bytes and an owner/sequence ticket.
- `rtc_tx_commit` accepts that ticket with `ACCEPTED`, `WOULD_BLOCK`, `FAILED`
  or `UNCERTAIN`. Commit requires a preceding peek. Only ACCEPTED advances state;
  WOULD_BLOCK preserves bytes/ticket; FAILED and UNCERTAIN permanently fail.
- `rtc_receive` takes a normalized endpoint number and a64-bit message. The
  future transport must decode mailbox metadata separately; this is not a raw
  register adapter. RX cannot bypass an unaccepted management TX.
- `rtc_poll` enforces deadline and step budget without protocol advancement.
- `rtc_finish` checks time and full discovery before returning a successful
  result. `rtc_inspect` is an untimed diagnostic snapshot and cannot validate
  success, a clock, or device ownership.

Every timed call consumes one of128 steps, including repeated peeks, polls and
WOULD_BLOCK commits. Init counts as step1. The caller supplies monotonic
nanoseconds; zero, rollback, deadline overflow and reaching the fixed deadline
fail. Equal timestamps are permitted, but cannot evade the step budget. The
maximum timeout is5 seconds, a conservative software policy. The engine has no
thread or autonomous clock and cannot enforce a wall-clock deadline while its
caller is not executing. It does not infer physical delivery from a queued
packet: ACCEPTED is a trusted transport report that the complete word pair was
accepted, not a firmware response. An uncertain partial delivery must never be
replayed. Tickets must not outlive their context, and context addresses must not
be recycled while stale tickets exist.

Committed system-endpoint advertisements appear separately in
`required_system_endpoints`. They are not actual allocation requests. Actual
crash/syslog/ioreport buffer requests are retained as raw words and cause sticky
`BLOCKED_SYSTEM_BUFFER`. Only the common42-bit address-field subset of the C
and Python references is decoded diagnostically. Bits43:42 and OSLog's divergent
wire layouts cause `AMBIGUOUS_BUFFER`; the complete raw word remains recorded.
Zero-size requests fail as malformed. Neither parsed address metadata nor an
advertised endpoint is imported as RAM, physical address, mapping authority or
an allocation. No reply, dummy buffer or success stub is produced. A request
received while TX is queued records the failure and cannot advance the queued
transaction. The first failure/evidence is sticky.

`transcript.c` is the host executable source. It reads only a finite stdin
transcript with EOF, at most128 newline-terminated lines of at most190 characters.
It is not an interactive or real-time transport. Commands are:

```text
INIT <decimal-now-ns> <decimal-timeout-ns>
TX <decimal-now-ns>
COMMIT <decimal-now-ns> <decimal-ticket-sequence> <ACCEPTED|WOULD_BLOCK|FAILED|UNCERTAIN>
RX <decimal-now-ns> <decimal-endpoint> <hex-message-word>
POLL <decimal-now-ns>
END <decimal-now-ns>
```

END calls `rtc_finish`; the executable emits the success label only after EOF,
so trailing commands cannot hide behind an earlier successful END. Missing END,
extra fields, embedded NULs, numeric overflow, long lines and trailing input fail.
Exit0 means protocol-only discovery. Exit1 means protocol failure; exit2 means
transcript/output failure. Unresolved buffers print their raw diagnostic word.

Run the full checks with:

```sh
python3 -B integrations/puredarwin/m1-gpu-rtkit-control/test_control.py
```

The script builds and runs the fake executable in temporary storage, checks exact
literal TX output, negative transcripts, host sanitizer/NDEBUG tests and compiled
mutants, and compiles the core alone against pinned ARM64 SDK/kernel headers.
`test_control.py` contains the complete positive transcript and golden output.
`validation.json` records6 groups/2136 checks per host build,22 detected mutants,
35 transcripts, zero analyzer diagnostics and a relocatable ARM64 object with
zero undefined imports. No target object is executed or packaged as a kext.

A future driver still requires exclusive/inherited GPU ownership, a positively
identified T8103 firmware ABI, owned firmware-buffer mappings, an admissible
TTBR1 strategy and proven ordering/invalidation/quiescence. This engine does not
weaken any of those gates or the observer hold.
