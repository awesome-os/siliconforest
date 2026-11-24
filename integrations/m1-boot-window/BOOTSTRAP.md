# Window004 to exact121 RAM bootstrap

`bootstrap.py` is an offline-by-default companion. It does not install the window,
reset a target, access boot storage, contact a server, start HV, or launch XNU.
Execution remains unqualified. The project lead owns first-prefix installation,
manual rollback approval, transport qualification and recovery.
Independent review B1 is a sequencing gate: before any bootstrap vector, the lead
must qualify reset-and-Linux-return from an idle claimed-window proxy. Prior
Linux-halt reset cycles do not establish that result. Recovery confirmation is an
operator assertion, not automated evidence that B1 passed.

The accepted source image is the frozen window004 raw binary, SHA256
`ba5bf4b396168ac540df852e3507c4342dd289eef7bf2c1fc29c50f11f0822bf`, with tag
`121fcbf35d4501814b58ca5347926a4173b6a512-darwin-lab-j293-window-v1`.
The destination is the immutable increment007 raw m1n1 image, SHA256
`05b36cc8288843211e97bd4f68adcf521c81e5e68afd98a4d094a6f03defb32c`, with exact tag
`121fcbf35d4501814b58ca5347926a4173b6a512`. Neither image is changed on disk.

## Verification and execution boundary

The companion hash-checks the frozen007 `host_console.py`, reuses its
`verify_inputs()` for the packet, entry checks, inventory and pinned host-source
bytes, and uses its `PinnedBackend` constructor to create fresh verified imports.
It never invokes that backend's exact-runtime `collect()` on the window variant.
Canonical `proxyclient/tools/chainload.py` and `m1n1/bootmem.py` receive additional
exact source-hash checks. Import/dependency failures occur before opening serial.
There is no `m1n1.setup`, PMU reset, `ProxyUtils` construction or runtime assembler.

Without `--execute`, no device is opened, even if a device argument is present.
Execution additionally requires an explicit `/dev/cu.usbmodem…` endpoint,
`--confirm-recovery J293_RECOVERY_READY`, a new private directory outside the lab,
and all verified inputs. The frozen constructor restricts execution to ARM64 macOS.
M1N1 environment overrides are refused. Short serial writes stop without retry.

The worker acquires with the normal complete NOP, then checks the live base and
the window's exact tag at its raw-image offset, USB0/USB1, the 20-byte CPUFeatures
ABI, BootArgs bounds, J293/MacBookPro17,1, ADT and proxy chip `0x8103`, UART0 at
`0x235200000`, and one ADT CPU marked running. The original BootArgs and ADT bytes
are saved privately immediately after board validation, before memory-map decoding,
allocation or metadata modification. The map decoder excludes only `name`,
the structural `AAPL,phandle` property (exactly four bytes), and `kernel-only`
(exactly `b"true\0"`). XNU's `IODeviceTreeSupport.cpp` uses the presence of
`kernel-only` as node visibility metadata; this loader accepts only the observed
encoding. All three remain in the ADT and are covered by the full preservation check. Every other
property must encode a 16-byte address/size pair; unknown encodings still refuse.
Thus map-decoding refusals retain both original binary metadata files. The complete
decoded memory map and live heap are saved in `original-observation.json` before
range planning, so a refusal leaves diagnostic evidence without retrying. A tag observation
is not a full live-code attestation; source/build/installed-image provenance remains
part of physical qualification.

## RAM handoff

The loader follows the pinned raw-chainload copy/cache/vector algorithm with a
narrow layout choice: firmware is always parked above the live heap, and the ADT
is copied there instead of overwriting the original. One monotonic heapblock
reservation covers the raw staging image, trampoline, firmware, BootArgs and ADT.
Its entire span is checked before reservation; a changed heap pointer fails closed.
No target malloc, host heap reservation, RVBAR write or shared API change is needed.

The raw image is followed by zero padding through a 16K boundary, including the
canonical four-byte payload terminator. Destination remains the live image base;
entry is `base + 0x800`, whose RVBAR page is that same base. The new image must fit
inside the known live-window footprint. All retained ranges must fit bounded DRAM
and end below the live heap. Unknown destination overlaps, firmware aliases,
missing SEPFW/TrustCache, malformed map entries and excessive sizes are refused.
Only existing `Kernel-*` ranges wholly inside the old image may be overwritten.

All present nonempty canonical boot blobs are copied with target `memcpy8`:
SEPFW, SEPPatches, uStuff, preoslog, RTBuddySeg, RAMDisk, SPTMDebug,
ExclaveOSIntegrityCatalog, ExclaveOSTrustCache and TrustCache. Firmware bytes never
traverse the host interface or enter an artifact. These are acknowledged target
copies, not independently byte-verified or quiesced firmware snapshots; mutable
logs can change while being copied. Original source blobs remain intact.

The copied ADT updates retained blob, BootArgs and DeviceTree addresses. Copied
BootArgs retain the original fields and command line except the new ADT address,
size and top-of-kernel-data. The immutable image, metadata and trampoline receive
full host readback checks, then cache maintenance. Exactly one `P_VECTOR` is
attempted; no reload fallback, retry, post-vector NOP or reconnect occurs.
An acknowledged vector is recorded as **runtime unverified**, never boot success.

Before staging, the rebuilt ADT is reparsed through the verified pinned ADT
module. Every node, ordered child path, encoded property and template flag must
equal the original snapshot, except the exact intended blob/BootArgs/DeviceTree
memory-map values. Reparsed BootArgs must equal the original fields except the
three intended updates, whose new values must also match the plan. Construct's
`_io` parser bookkeeping is excluded. A parser error, missing/added property,
unintended change or wrong intended value stops before staging or vectoring.

The 104-byte trampoline is compiled offline from the exact pinned chainload
instructions. All 24 instruction words are tested; only the 8-byte entry literal
at offset `0x60` changes at runtime. It copies the aligned image, cleans data cache,
invalidates instruction cache, issues barriers and branches, preserving x0 BootArgs.
The M5 needs Python dependencies and these reviewed bytes, not GNU binutils.

The supervisor bounds execution to 120 seconds by default, configurable to 1–300.
Timeout kills only its host worker. It cannot recover a hung target. Closing the
host descriptor can cause autonomous target behavior, including reboot on some
pinned error paths. After handoff, the lead must observe enumeration and perform
fresh immutable exact121 checks before using frozen007. Recovery must stop window
acquisition probes before reset so Linux fallback can occur.

## Offline use and evidence

From the lab root:

```sh
python3 integrations/m1-boot-window/bootstrap.py
python3 integrations/m1-boot-window/bootstrap.py \
  --manifest artifacts/m1-firstboot-007-r6/manifest.json \
  --host-source sources/m1n1-baseline \
  --window-image artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin
PYTHONDONTWRITEBYTECODE=1 python3 integrations/m1-boot-window/test_bootstrap.py
```

The tests are host-only: refusal ordering, exact source/image bindings, DRAM and
overlap boundaries, complete trampoline encodings, metadata address policy,
target-only firmware copies, short writes, readback failures, one vector and host
timeout. Metadata comparison/refusal tests use an explicitly fake serializer;
actual Construct/ADT round-trip testing on the M5 is separately owned by the lead.
The runtime parse-back gate is mandatory, but host fake tests do not establish
live codec fidelity, USB behavior, vector execution or recovery. The preparation receipt is
`reports/sprint-008/bootstrap-preparation.json`.

An operator may later add the execution/device/private-directory/recovery flags
after the independent review. No execution example was run during preparation,
and none of these files changes the frozen007 launcher or its acceptance rules.
