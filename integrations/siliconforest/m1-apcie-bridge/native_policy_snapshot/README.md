# Bounded native platform facts codec

This leaf provides an original Python encoder/reference decoder and freestanding
C decoder for the exact versioned little-endian format in WIRE_DESIGN.md. It has
no registry reader, physical access, producer, writer, backend or conversion to
bpl_policy. Success is structurally consistent facts, not permission to configure
an owner or evidence that exclusions are globally complete.

The largest wire is 9,472 bytes: 1,280 fixed bytes and up to 256 source records.
All raw source records fit this independent limit or the input refuses; they are
never truncated to the future policy's separate eight-range capacity. Original
RAM intervals remain separate even when adjacent. The derived relevant union
merges only overlapping/adjacent authoritative-source intervals and retains full
source boundaries rather than clipping at RAM. Remap records remain numerical
source-only facts and are excluded from that union. A union needing nine ranges
refuses. Counts and source status labels attest no actual source completeness.

The full frozen 384-byte MCC snapshot is checked by mcp_decode, with exact
pass/plane stability and enabled-slot provenance. The old compact 80-byte list
cannot substitute. MCC coverage remains FOUR_PLANES_FOUR_TZ_ONLY. Expected
nonce and all six identity digests are explicit caller-supplied trusted values;
matching them does not authenticate that caller. Source descriptor hashes are
copied declared provenance labels, not recomputed source-byte hashes.

C callers supply a separate, suitably aligned workspace in heap/static storage.
Its large staged result and sort buffer must stay off the kernel stack. The
immutable wire, expected identities, workspace and output spans must be mutually
disjoint. Workspace contents may change on failure; output is committed once,
after all validation. Accessible immutable inputs, serialization and trusted
boot-phase quiescence remain caller obligations. No retry or allocation occurs.

Run `python3 validate.py --output-dir /private/scratch/path` for an independent
rerun. Tests use synthetic metadata, sanitizer-host and KERNEL-defined host
builds, Python/C comparisons, interval oracles and compiled assertion-killed
mutants. A defined KERNEL macro is not kernel execution. The separate ARM64
objects and partial links check frame limits, C symbol resolution and imports;
no object, native API or target is executed. Existing frozen MCC code is linked
unchanged. Target compiler wrappers and headers are bound, not represented as a
freshly built transitive toolchain.

The initial mutable C clearing expression created a large O0 temporary and
failed the 1,024-byte frame guard; workspace byte clearing removed it without
weakening the guard. Initial target build001 failed because the compiler wrapper
pointed at an absent default sysroot. Build002 supplies the cached actual SDK.
Both failures remain documented; neither was a native runtime failure. An early
RAM-containment test mutant survived the first harness, so a specific
inside-DRAM/outside-envelope case was added before the source freeze.
