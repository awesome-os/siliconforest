# Trusted internal MCC raw capture seam

This original host-only module collects metadata through one supplied synchronous
`read32(relative_offset)` callable. It imports no proxy, device, MMIO, SSH, ADT,
transport or runtime adapter. It is not integrated into a worker or packet.

The pinned MIT source selects `/arm-io/mcc`, compatibility `mcc,t8103`, and **reg
index 1** for the plane aperture. Reg0 is global and reg2 is DCS; neither is
accepted. `SOURCE_FACTS.json` pins the exact source and a read-only audit of
already saved original ADT bytes. That saved reg1 extent is 1MiB and contains the
whole relative plan. No actual physical base is hardcoded in this module.
A future trusted setup must resolve reg1 through the original ADT parent ranges,
supply its exact translated base/extent, bind the callback to that region, and
establish mapping ownership and safe reads separately. A matching declared hash
or numerical range does not establish any of those facts.

Use `Expected(adt_sha256, nonce)` and `Region(profile, node_path, compatible,
reg_index, physical_base, extent, adt_sha256, nonce)` with the fixed constants and
separately trusted values. Then create `Collector(region, expected, read32)` and
call `capture()` once. There is no approval boolean. Expected hash/nonce equality
binds trusted caller declarations; the collector does not authenticate a caller
or recompute the ADT digest. Tests use fabricated addresses and callbacks only.

Before the first callback, strict built-in field types and widths are checked,
including rejection of bool-as-int, PA36 overflow, wrong profile/path/compatible,
wrong reg index, zero or malformed identities, and incomplete/alignment-invalid
apertures. The entire 96-read plan is constructed and bounded before any read;
minimum region extent is `0xc06dc`. The plan is two passes, each four planes,
each four slots, each START/END/ENABLE at 32-bit width. Every offset is relative;
the collector never adds the supplied physical base or passes it to the callback.

The descriptor and expected binding are copied before callbacks. Original object
identities, exact field values and callable identity are checked before and after
every callback, including the final and throwing callbacks. Returned words must
have exact Python int type and lie in `[0,2^32)`. An attempted read is counted
before invocation. Completed words count only valid-u32 returns; a word returned
by a callback which also changes the binding is preserved as evidence, not as a
trusted source sample. No later callbacks or retries follow a failure.

Each instance is single-use. Reentry returns STATE_REFUSED and poisons the outer
invocation, which stops at the current boundary. Ordinary callback exceptions
become READ_EXCEPTION without exception-message leakage. KeyboardInterrupt and
SystemExit propagate, but `last` retains CALLBACK_ABORTED, attempted count and
bounded returned prefix, and the instance remains consumed. Direct mutation of
private collector fields or callback closure internals is outside this trusted
Python boundary; this is not a sandbox or a malicious-caller security boundary.
Caller serialization, immutable inputs between calls, callback lifetime/mapping
stability, and bounded callback return time are required. There is no timeout or
physical cancellation/drain mechanism.

Result fields are immutable. Failure has `raw=None` and a fixed reason plus
attempted/completed counts and at most384 bytes of evidence prefix. Even96 valid
word returns followed by a binding failure are not exported as a complete raw
sample. Only a fully completed unchanged-binding invocation returns exactly384
little-endian bytes with status **RAW_COMPLETE_UNVALIDATED**. It also reports
pass equality and within-pass plane equality, which are comparisons of supplied
values only. Mismatching values still remain raw, unvalidated evidence; no
capture result is labeled semantically valid or safe. Full slot validation must
subsequently use the unchanged frozen MCC/facts decoders. Enabled bad bounds can
therefore be captured successfully as raw bytes and then refused by the decoder.

This covers four planes/four TZ slots of the selected source profile only.
It establishes no global exclusion completeness, memory policy, DMA eligibility,
firmware state or boot admission. The historical80-byte sentinel carveout output
cannot reconstruct the384-byte raw source and is not an alternative input.

Run `python3 -B test_capture.py` for host tests. All96 offsets are compared with
actual frozen C `mcp_plan_read`, compiled with host ASan/UBSan. Tests exercise all
96 callback fault and binding-change positions, including final-call failure,
strict types/widths, reentry/no retry, immutable raw output and deliberately
invalid-but-captured slot semantics. Python mutants are syntax-compiled before
execution and counted only when a test assertion fails. Initial mutation-run
failures are retained in validation and explained in the source receipt; they
are not relabeled passing or counted as killed mutants.
