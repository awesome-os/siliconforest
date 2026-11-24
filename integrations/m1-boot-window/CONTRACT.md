# J293 payload-preserving CDC bootstrap candidate

This is an offline proposal, not an installed or hardware-qualified bootloader.
The candidate changes only an isolated local clone at
`sources/m1n1-boot-window`, based on
`121fcbf35d4501814b58ca5347926a4173b6a512`. The original baseline, installed boot
storage, stage1, boot policy and frozen increment007 controller are unchanged.

The build identity is deliberately distinct:
`121fcbf35d4501814b58ca5347926a4173b6a512-darwin-lab-j293-window-v1`.
This bootstrap is not the exact121 proxy accepted by the frozen controller.

## Execution contract

Build with `USE_CLANG=1 CHAINLOADING=1 ESP_STAGE2=1 M1_BOOT_WINDOW=1`, the
manifest's explicit version tag, and Cargo `--locked --offline --features chainload`.
The raw entry remains offset `0x800`. No NVRAM/boot-argument change is required.
`ESP_STAGE2` suppresses stage1-only `m1n1-proxy` and embedded stage1 configuration.
`m1n1-debug` causes this new window to be skipped, preserving the existing carrier.
The candidate refuses compilation with `EARLY_PROXY_TIMEOUT`, `USE_DEBUG_USB` or
`BRINGUP` enabled. It retains the ordinary upstream display/platform initialization.

Only chip `0x8103` with a five-byte NUL-terminated ADT `target-type` of `J293`
or `j293` enters the new branch. Missing, malformed or different identities bypass
the window. This source predicate does not claim a live identity observation.

Before `payload_run`, the branch calls native `usb_init` and `usb_iodev_init`
once and records the attempt in `usb_up`, including partial/no-port initialization.
The existing fallback consequently cannot initialize the iodevs a second time.
The acquisition deadline is **10,000 ms from return of USB initialization**.
It does not bound a hardware stall inside initialization, event service or shutdown.

Only USB0/USB1 are polled. The single consumer checks available input and reads
one byte at a time; it never requests an unreceived frame tail. Each port has an
independent 64-byte sliding buffer. A claim requires a complete `REQ_NOP`
(`ff 55 aa 00`) with the existing protocol checksum, received strictly before
expiry. Noise, other request types, incomplete frames, invalid checksums, UART/KIS
traffic, and opening a serial endpoint without a NOP do not acquire the window.
Traffic never extends its deadline; expiry wins on the final byte at the deadline.
No reply or protocol operation executes before acquisition.

The complete accepted request is copied into the existing proxy dispatcher once.
That dispatcher rechecks its checksum and runs its unchanged NOP feature mask,
checksum-disable state update and normal reply serialization. Feature/reserved bytes
are not rewritten by the window. Subsequent proxy behavior is the normal upstream
behavior, including scanning enabled transports; this is not an authenticated or
restricted-command session. A valid NOP from another host program can claim it.

No valid claim: continue through the original `payload_run` and normal next-stage
cleanup. After a claim: stay in proxy until a valid `next_stage.entry` is selected;
never fall through to Linux after arbitrary proxy RAM/controller mutations. The
acquisition timeout is not a proxy-session timeout or target watchdog.
After expiry and successful native payload preparation, the candidate calls
`usb_hpm_restore_irqs(true)` before returning to normal handoff, mirroring
`P_VECTOR`. Failed payload/proxy fallback keeps its existing mask state. The
helper visits only valid port indices with a successfully saved mask, including
partial initialization where a later mask/ack write failed. Restoration is the
upstream best-effort operation: I2C write errors are logged, not returned to this
caller, and previously acknowledged event flags cannot be reconstructed.

For the intended experiment, an independently qualified bootstrap host must
verify this variant, RAM-load/hash-check the immutable exact121 increment007
image, and use the existing vector/handoff mechanism. Its USB shutdown and fresh
enumeration must be observed. Reconnect exclusively to that new runtime and pass
all frozen007 BUILD_TAG, CPUFeatures, identity and entry checks before HV/XNU.
Neither those runtime checks nor RAM chainloading were performed here.

## Original payload and proposal

The original backup is `artifacts/m1-unattended-008/recovery/original-boot.bin`,
10,107,602 bytes, SHA256
`54fa07e4f5cbeecd90e67e335657c40b33c25aede7015a84afb1427333b4f923`.
Its `AURALOAD` header declares a `0x400000` footprint. The packager requires that
exact full-file hash and header, and accepts the suffix only as consecutive valid
DTBs followed by one complete gzip ARM64 U-Boot image, with no unexplained tail.
It never fetches or adds proprietary firmware.

The proposed image is the new raw prefix followed by **all original suffix bytes
unchanged and in their original order**. Its current linked payload boundary is
`0x420000`, not the old boundary. The packager cross-checks the raw header,
`_payload_start`, raw file length, AArch64 ELF and entry address before packaging.
It records the suffix hash, J293 DTBs and decompressed U-Boot hash without
recompressing or modifying them. See the build receipt for the exact proposal path.

The prefix is an upgrade from installed `f5a82fdb-dirty`; that installed source
provenance is not qualified here. Preserved suffix bytes do not prove unchanged
DT selection, display/SMP/platform initialization or Linux handoff behavior.
Native USB is initialized earlier and CDC controllers are active during this
path. Existing `kboot_boot` already calls the guarded `usb_init`; the existing
next-stage path shuts down registered iodevs. W1 adds the saved-mask restoration
described above; no USB ownership bypass is introduced. The raw link currently meets its `_file_end`
limit exactly at `0x120000`; future source growth must retain the linker check.

## Offline preparation and verification

`candidate.patch` covers six files; `manifest.json` binds baseline and resulting
file hashes. `test_candidate.py` reconstructs and applies the patch in a temporary
directory, checks those hashes, compiles/runs the actual pure C policy with
ASan/UBSan, and checks that normal dispatcher/reply source remains unchanged.
LeakSanitizer is disabled because the tool's ptrace environment prevents its
inspection; the tested policy performs no allocation. These tests do not emulate
USB hardware or prove the real-time bound on hardware.

From the lab root, after recreating the exact isolated checkout and applying the
manifest-bound patch:

```sh
python3 integrations/m1-boot-window/test_candidate.py
python3 integrations/m1-boot-window/prepare.py --attempt 3
```

Use a fresh attempt number. Preparation checks source/artwork pins, uses the
existing offline toolchain/cache, preserves build logs and emits an attempt-local
proposal and receipt. It neither installs nor contacts a device. The checkout
contains only local artwork submodule configuration; do not run remote submodule
updates. Host compiler versions are recorded, not a hermetic Nix toolchain pin.

## Physical qualification owned by the project lead

1. Qualify independent external reset first. Killing a host worker is not target
   recovery; disconnect may itself trigger autonomous target reboot on some paths.
2. Review this exact proposal hash, original backup, replacement/readback and
   restoration procedure before the one boot-file setup approval. This preparation
   grants no installation, stage1, boot-policy or firmware-change permission.
3. Verify silent-host Linux fallback, including a connected cable/idle endpoint,
   and verify the original Linux version and expected services remain isolated.
4. Qualify complete-NOP acquisition, variant identity, RAM exact121 handoff and
   enumeration. Then stop all acquisition/probing workers, externally reset, and
   verify silent-host Linux return before an XNU experiment.

Recovery must suppress NOP retransmission/autoconnect, or it will deliberately
reacquire the window after reset. A broken prefix, USB-init hang or failed original
payload cannot be repaired by the acquisition timer. The independent recovery and
backup-restoration path must cover those cases. No Maclab service or server is
part of this design.
