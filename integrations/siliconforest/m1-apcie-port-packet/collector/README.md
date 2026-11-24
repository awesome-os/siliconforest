# Read-only APcie port-0 observation collector

This original userspace collector copies the fixed `IOResources` property
`PDApcieBridgeObservationV1`. It does not open a userclient, connect to a service,
set a property, map MMIO, initialize hardware, or print copied registry strings
or device identities. Source work and Linux host tests do not establish target
execution. A future packet/boot remains root-owned and separately reviewed.

Installed command:

```
/usr/bin/darwin-apcie-port-observe --nonce LOWERHEX48
```

The nonce is an evidence binding, not a secret or device identity. There is one
lookup of `IOService:/IOResources`, one raw MIG property copy into the actual
4096-byte `io_struct_inband_t`, and no retry/fallback/search. Both acquired Mach
rights are released once; any release error prevents success. The actual tiny
IOKit generated header supplies all ABI declarations. Missing property is the
pinned XNU `kIOReturnNoResources`; other RPC failures remain errors. Raw MIG
copies bytes; it does not independently verify the original OSObject's type.
The source-bound provider publishes OSData.

Successful bounded copies, including malformed lengths, are emitted and flushed
**before** source classification. The block is fixed ASCII with lowercase hex:

```
DL31_APCIE_RECORD_BEGIN NONCE PDApcieBridgeObservationV1 bytes=N chunk_bytes=64
DL31_APCIE_RECORD_HEX NONCE PDApcieBridgeObservationV1 index=0 HEX
...
DL31_APCIE_RECORD_END NONCE PDApcieBridgeObservationV1 bytes=N chunks=C sha256=HEX64
DL31_APCIE_VERDICT NONCE verdict=OBSERVED result=0 reads=14 stage=5 valid=16383 region=0 offset=416 initialization_proven=false
```

Indices are consecutive from zero, every nonfinal chunk is 64 bytes, the last
chunk is exact, and the digest covers the original returned bytes. An empty
copy has zero chunks and SHA256 of the empty byte string. A copy longer than
4096 cannot be exported by this bounded RPC and is an error. No raw addresses,
registry strings, or hardware identifiers are decoded/exported.

Exit codes are:

- `0`: exact 84-byte version1, source-coherent `APO_OBSERVED` numeric snapshot.
- `77`: exact 84-byte, source-coherent gated result1..5 (`NOT_READY`).
- `1`: missing property, RPC/copy/cleanup/output error, malformed record, or
  coherent source error (-1/-2/-3). These never become guarded success.
- `2`: invalid command line; no RPC is attempted.

A missing/RPC error emits `DL31_APCIE_COPY NONCE status=missing|rpc_error
 detail=S32 cleanup=S32` and a matching `MISSING|RPC_ERROR` verdict. Copied but
invalid bytes have a `MALFORMED` verdict. Port-release failure after a copy has
`CLEANUP_ERROR`. These generic error verdicts omit numeric snapshot fields and
still include `initialization_proven=false`. A coherent numeric error has
`SOURCE_ERROR` with all numeric fields. Output failure may truncate the block;
that is rejected, never inferred from the process return alone.

The snapshot has 21 little-endian 32-bit words, result signed:

| Words | Field |
| --- | --- |
| 0..6 | version, reads, stage, valid mask, result, last region, last offset |
| 7..10 | initial PMGR guard values |
| 11..14 | final PMGR guard values |
| 15..20 | PHY, RC PHYIF, APPCLK, RESET, STATUS, LINKSTS |

`collector.c` and the independent `decode.py` each replay the exact observer
prefix, not just a successful return value: initial PMGR reads stop at the first
not-on domain; PHY/PHYIF/STATUS gates choose the next stage; all reached APPCLK,
RESET and STATUS are recorded; LINKSTS is read only after STATUS RUN. Read
failures increment reads and update the last coordinate but do not add a valid
bit/store a value. A final four-PMGR guard follows a successful read beyond the
initial stage unless an IO read failed. Final power loss overrides the earlier
stage result. All unread fields are zero, matching the actual zero-initialized
adapter and its failed-read no-store path. Every result, read count, stage,
valid mask, last coordinate and absent-field zero is checked exactly. LIMIT is
unreachable in this fixed bounded sequence and is rejected as inconsistent.

`OBSERVED` requires all 14 reads and final guard success. It does **not** require
LINKSTS.UP, APPCLK.EN or RESET.DIS; those are observed data here. A zero LINKSTS
is explicitly tested as OBSERVED. Neither0 nor77 proves PCIe initialization,
link readiness, BCM chip identity, firmware, DMA, DART setup, or WiFi operation.
No shared PHY/PMGR write permission is introduced.

`decode.py` API is `decode(output: bytes, nonce: str) -> dict`, with original
bytes represented as `raw_hex`, independently recomputed digest and
`expected_exit`. It requires one complete collector-only block, total<=20000,
<=67 lines, <=512 bytes per line, one exact nonce/property, exact indices,
lengths/count/digest, independent snapshot validation, and exact verdict match.
It does not filter unknown/foreign lines or locate a good block inside noise.
A supervisor extracts/binds its own wrapper separately and checks actual rc.
Trailing transport CRs only are normalized; interior CR is rejected. Raw output
SHA and decoded byte SHA are both retained. CLI:

```
python3 decode.py --nonce LOWERHEX48 collector-only-output.txt
```

Host checks (real unchanged `observe.c` CPU oracle, synthetic read callbacks;
no hardware/IOKit invocation):

```
APC_TEST_REPORT=host-test-results.json python3 test_decode.py
```

This compiles/runs C under ASan+UBSan, checks every metadata bit, every short
length, stage/read failures, guard flag bits, zero tails, output failure and
CLI/copy errors. The independent decoder checks the C-emitted corpus plus
framing/nonce/SHA/verdict mutations. Fifteen deliberately modified C classifiers
must compile and then fail those tests; compiler errors are not mutant kills.

Cached-only SDK build and read-only artifact validation:

```
nix --extra-experimental-features 'nix-command flakes' build --offline --impure --expr 'import ./build-collector.nix' --no-link --print-out-paths
python3 validate_build.py /nix/store/OUTPUT
```

The build retains both actual TU dependency lists, copied sources, generated
header/archive identity, compiler, Mach-O metadata and imports. It links the
actual retained libSystem9fc and libdyld428, not a guessed ABI or another dyld.
The validator checks the binary's 24 imports, all49 libSystem libdyld-ordinal
imports, and the exact retained text surface after dead stripping. Prior 9fc/428
qualification is referenced separately; Objective-C dynamic lookup records are
not promoted into a new Objective-C runtime claim. SDK success is not execution.
