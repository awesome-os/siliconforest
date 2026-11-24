# Detached ASC envelope codec

Original bounded C codec for the legacy ASC source layout at Omacom/m1n1
`121fcbf35d4501814b58ca5347926a4173b6a512`. This is a source-only foundation for
the T8103 AGX investigation. Selecting the enum identifies that source layout;
it does not detect a device, establish register compatibility, select firmware
ABI, or admit ASCWrap-v8. Darwin's loaded GPU ABI remains **UNKNOWN**.

`ace_decode_rx` preserves both complete words and extracts EP[7:0],
OUTCNT[56:52], INCNT[51:48], OUTPTR[47:44], INPTR[43:40] from metadata.
Bits[63:57] and [39:8] are **opaque**, not proven reserved or required zero.
Counter/pointer values are numeric observations only: no freshness, coherent
snapshot, occupancy consistency, successful receipt, or overflow inference.

`ace_rtkit_input` separately applies the pinned C RTKit receive policy:
metadata bits[31:8] must be zero. Pinned C truncates metadata to u32 and rejects
values >=256, while pinned Python AGX routes using only EP[7:0]. This stricter
admission choice is not a hardware reserved-bit claim. Full raw metadata remains
available through lossless decoding; bits[39:32] and [63:57] stay opaque even
when admitted. The engine must still validate the normalized message/state.

`ace_encode_tx` creates endpoint-only metadata with all other bits zero, matching
the pinned fresh Python ASCMessage1(EP=...) and C endpoint assignment patterns.
It never accepts an RX object as a TX template. word0 is opaque to this codec;
encoding arbitrary numeric words grants no right to send them or use addresses.

The 16-byte format is our canonical little-endian serialization of detached
numeric word0 followed by word1. It is **not** a captured hardware protocol,
an atomic two-register read, or permission to access the documented register
offsets. Callers supply valid, live, serialized byte/object spans. Input/output
overlap, wrapping spans, unsupported profiles, nonexact lengths and endpoint
overflow fail before writing. Every failed API preserves its output.

There are no transport callbacks, engine mutations, allocations, MMIO accesses,
power operations, doorbells, CPU_RUN writes, endpoint-start actions, pointer
imports, or publication interfaces. EMPTY/FULL/OVERFLOW/ENABLE are separate
control-register source fields, absent from this word-pair API. No message bit
here supplies a delivery acknowledgement. Hardware admission, stable/fresh
capture, transport ordering/barriers, FIFO fault recovery, and ownership under
the existing HV remain separate unresolved integration obligations.

The host test's composition is deliberately explicit:

1. `rtc_tx_peek` yields a frozen-engine ticket and normalized packet.
2. `ace_encode_tx` yields bytes; the engine stays byte-for-byte unchanged.
3. A separate fake delivery outcome is passed to `rtc_tx_commit`. WOULD_BLOCK
   leaves TX pending; failed/uncertain outcomes remain terminal failures.
4. Synthetic RX bytes are decoded, conservatively admitted, then explicitly
   passed to `rtc_receive`. Encoding cannot permit RX past an uncommitted TX.

Successful fake negotiation ends only at
`RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY`, after the final explicit ACK commit.
System-buffer requests remain blocked by the unchanged engine. This establishes
no real mailbox reception, send acceptance, firmware handshake or GPU readiness.
The GPU observer boot HOLD remains absolute.

Run `python3 integrations/puredarwin/m1-gpu-asc-envelope/check.py` from the repo.
The checker compares nine exact MIT source files to pinned Git objects and six
Python files to the existing private resolver004 source snapshot, without
importing that code. It verifies the entire frozen RTKit receipt, runs 2,248
checks under ASan/UBSan and NDEBUG, detects 15 compiled semantic mutants, checks
the static analyzer, and builds an actual pinned SDK ARM64 object with no imports.
No target object is executed. The per-function source basis is in PROVENANCE.md;
validation.json and source-validation-001.json freeze this separate leaf.
