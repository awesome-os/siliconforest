# Explicit native BootArgs wire codec

This original portable Python codec addresses the source/ABI mismatch documented
in `bcm4378-bootargs-abi-artifact-001.json`. It is isolated from every existing
host, m1n1 module, capture and boot path. It performs no I/O or actual memory
writes and does not supply kernel, m1n1 or deployment fixes.

`ABI_FACTS.json` binds the genuine Kernel.framework data-only type/offsetof probe,
its 103 header dependencies and the actual standalone kernel's single-function
disassembly. Native layout is 1152 bytes, with a 1024-byte CommandLine at108,
bootFlags at1136 and memSizeActual at1144. The source revision-2 serializer's
736-byte layout has608 command bytes and tails at720/728. The actual kernel reads
the native0x478 tail. These are layout facts, not a proven runtime wrong value,
crash, allocation symptom or explanation of an old failed boot. The final guest
BootArgs blob was not captured.

`encode(values, *, wire_profile)` requires the exact external profile
`darwin-xnu-e7-arm64-bootargs1152-r2v2-v1`. It preserves Revision2 and Version2;
it does not switch their numeric values to select a format. Inputs are plain
dictionaries with exactly the documented fields (see synthetic `values()` in
the tests); all integers are exact Python ints of their declared unsigned width,
not bools, floats or converted strings. `video` is an exact plain dictionary.
The codec consumes explicit trusted source values, including both tail fields;
it never tries to recover them from native or legacy raw offsets.

The payload starts fully zeroed. Reserved padding remains zero. Every ASCII
command byte except embedded NUL is preserved exactly, including whitespace and
control bytes; the byte codec neither strips nor adds text policy. At most1023
bytes leave room for a terminating NUL. Remaining command bytes are zero.
`decode(payload, *, wire_profile)` accepts exactly1152 immutable bytes, the same
explicit profile and R2/V2, canonical padding and a first NUL followed only by
zero fill. It has no length/revision-based fallback or hidden legacy tail read.

The profile tag is external because the exact native ABI has no spare tag field:
reserved bytes must remain zero. A legacy736 payload whose tails are zero and
which is padded to1152 can be byte-identical to a canonical native payload.
Numeric revision and payload inspection cannot authenticate its provenance.
Callers must bind the explicit profile to the reviewed producer; this codec does
not relabel historical bytes or grades. Nonzero legacy tails padded to1152 are
refused as noncanonical command trailing bytes.

`plan_write(values, *, wire_profile, address, allocation)` returns a frozen
`WritePlan` containing immutable payload bytes, address/end, SHA256 and the
supplied allocation binding. Allocation must be the exact plain dictionary
`{base, bytes, review_sha256}`. The plan checks unsigned widths, nonempty,
nonwrapping allocation, 8-byte payload alignment and complete containment. Its
`metadata()` returns a fresh JSON-compatible record without the payload.
A digest is a provenance label, not proof that the allocation was reviewed or
that the address is RAM. Actual guest BootArgs allocation identity, RAM/MCC
policy and caller trust remain external obligations. No backend object, writer,
proxy method or callable is accepted; no target operation can occur here.

A separate later isolated host integration is required at the **final** BootArgs
rewrite. It must be opt-in and hash-bound, construct explicit trusted source
fields, preserve old profiles and ensure no later HV/start path overwrites the
native bytes with the old revision-indexed serializer. It must save the exact
final guest wire bytes and their profile/hash after the last write, before boot.
This source leaf is neither applied to that path nor authorization to boot.

Run host-only validation:

```
python3 integrations/puredarwin/m1-apcie-bridge/native_bootargs/test_native_bootargs.py --mutations
```

The tests cover source-r2-to-native roundtrip/common prefix and both tail layouts,
all video values, width edges/refusals,256 seeded full-width roundtrips, all ASCII
except NUL, command bounds, canonical decoder refusal, ambiguous zero-tail layout
provenance, input immutability and write-plan boundaries/overflow. Eleven mutated
codecs must compile and fail their targeted assertion tests with no test errors;
syntax/import exceptions do not count. Tests use only synthetic values. No raw
saved BootArgs/device data is copied into fixtures or printed.
