# GPU-specific readiness observation, source slice001

The previous survey stopped before reading **PCIe** `dart-apcie0`. That block
serves WLAN/Bluetooth; it is not the GPU UAT. Its original refusal and guard
remain unchanged. Enabling PCIe/PHY would add unrelated hardware changes and
does not establish any GPU prerequisite.

This isolated slice advances the GPU dependency chain by identifying the actual
J293/T8103 SGX/GFX-ASC metadata and sampling **one real PMGR GFX power word**.
It never accesses GPU, ASC, UAT, DART, PCIe, display or firmware registers.
There is no power/reset write, firmware upload/start, page-table walk/adoption,
mapper publication, interrupt attachment, GPU command or panel mode-set.
No macOS driver or GPL implementation was copied.

The existing saved guest ADT (SHA256
`8f8613d8787f912137b482991dc0d5eca403d93e01bceb212019a02be9b8d8c5`)
has SGX clock IDs314/315 and GFX-ASC316. All three PMGR entries are **virtual**
(flag0x10). Their placeholder PS-index/offset fields are not register addresses.
GFX-SGX314 and GFX-ASC316 name real parent99, GFX. GFX-BUSY315 has no parent;
it cannot be interpreted as an independently observable clock-ready bit.
Only GFX99's actual PS-regs mapping is resolved, checked against arm-io ranges,
and required to be physical `0x23b7003f8`.

`board.c` checks exact board/node identity, both SGX and both GFX-ASC resources,
the declared clock/power IDs and four PMGR identities/flags/parents. Duplicate,
truncated, ambiguous, overflowing or changed inputs refuse before mapping.
The kernel adapter copies bounded ADT properties into owned storage, then maps
only PMGR's first16KiB read-only/uncached. It performs one aligned32-bit load at
offset0x3f8 and releases the map before publishing copied diagnostics. The PMGR
register window has already been read by the prior survey and native keyboard
path. That supports this existing PMGR access context, not GPU MMIO access.

Explicit future boot opt-in: `pdgpupreflight=1`. A module-global CAS admits one
probe per boot and is never cleared. Default boots perform no observation.
The new bundle is `org.puredarwin.driver.PDGPUPreflight`, matched only on
IOResources. It is diagnostic and has no client/IOMapper/GPU ownership API.
No async action survives `start`. Unload is refused. Exact64-byte OSData is
copied to persistent IOResources property `PDGPUPreflightV1` on success and
refusal; publication failure remains separate from the raw observation result.

Layout is16 little-endian u32s on ARM64, with compile-time size/offset checks:

| Words | Meaning |
| --- | --- |
| 0..3 | version1, size64, result, board_valid |
| 4..7 | reads_attempted, read_completed, raw GFX PS value, reserved0 |
| 8..13 | clock IDs314/315/316, virtual-mask7, domain99, offset0x3f8 |
| 14..15 | gpu_mmio_reads0, gpu_mmio_authorized0 |

Result0 means exactly one PMGR word was observed;1 means board refusal;2 means
the PMGR read/map failed. Zero/unread fields are not hardware observations.
Even target=actual=0xf is **not** proof that GPU/ASC clocks, reset, UAT ownership
or firmware state permit MMIO. `decode.py` always leaves GPU authorization false.
Snapshots are one-shot observations, not a lease against another power owner.
A synchronous PMGR load has no software bus-hang recovery guarantee.

`collect.c` reads only IOResources' exact64-byte property through existing tiny
IOKit MIG and emits three nonce-bound indexed/SHA256 lines. It opens no device
or user client, writes no registry property, and performs no memory mapping.
Missing property/wrong size/MIG or close failure exits1. The pure saved-block
decoder independently checks framing, digest, version, exact profile and
reachable result shape. Raw PS values stay in private capture.

## Root handoff (not executed)

1. Independently review source and build `build.nix` and `build-collector.nix`
   offline. This source preparation did **not** build either ARM64 artifact.
   Verify actual SDK imports/MIG signatures, immutable source hashes and bundle.
2. Compose a **new** KC from the qualified native baseline plus this one bundle;
   omit old DART survey and PDSerialHID, and preserve native input/display/kernel.
   Do not alter any existing candidate. Inspect membership and producer layout.
3. Root must add a separately reviewed host opt-in/packet binding for this exact
   bundle and `pdgpupreflight=1`; the existing host has no such admission yet.
   Add only `darwin-gpu-preflight` and a finite collector invocation
   `/usr/bin/darwin-gpu-preflight --nonce HEX48` to a separate RAM candidate.
   Keep existing finite desktop cleanup and the same sole-owner sealed TX path.
4. Fix criteria before a root-owned test: exact sources/KC/RAM/host, one probe,
   one persistent raw64 receipt, valid decoder result, no PCIe/GPU writes or
   firmware start, complete capture, and one recovery to authoritative new Linux.
   This slice authorizes no boot and performs no target action.
5. Use the actual GFX observation to design power/clock/ASC/UAT ownership next.
   A later first GPU register read still needs source-supported reset/clock
   prerequisites and an explicit isolated reviewed candidate. Do not weaken
   the old APCIE guard or infer readiness from virtual clock IDs.

Host checks: `python3 -B integrations/puredarwin/m1-gpu-preflight/test_preflight.py`.
They cover actual saved ADT, malformed/duplicate/ambiguous metadata, virtual
placeholder refusal, one-read policy under ASan/UBSan and strict decoder shapes.
They are not native SDK, GPU hardware, acceleration or panel evidence.

Primary facts are the existing saved ADT; pinned m1n1 `src/pmgr.c` flag0x10 and
virtual-parent handling; `proxyclient/m1n1/agx/__init__.py` and `hw/uat.py` GPU
paths; the acquired T8103 Asahi hardware descriptor; and the earlier DART
prerequisite reviews. See the hash-bound source-validation record for provenance.
