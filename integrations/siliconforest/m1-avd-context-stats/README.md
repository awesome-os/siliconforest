# Per-context decoder diagnostics candidate

This is source and SDK work, **not installed on the M1**. Kernel 45 retains the
existing global counters. Concurrent playback makes those counters unsuitable
for assigning a particular test's jobs: media's 300-picture HEVC test observed
585 global jobs while another context existed. That was an attribution failure,
not evidence that this test decoded 585 pictures.

The four-file patch applies only to the exact deployed shared-import source
`yw38kh…`, whose existing SDK output is `5238qc…`. `manifest.json` verifies each
before/after source hash. `build-kext.nix` asserts the baseline derivation and
builds SFAppleAVD with the pinned arm64 kernel SDK. The bundle preserves upstream
GPL-2.0 notices in the decoder core and the ioctl header's MIT notice. No AVE
source, firmware, kernel core, filesystem, cache policy or admission rule changes.

The additive ABI1 extension advertises `SFAVD_CAP_CONTEXT_STATS` (bit 17) and
accepts `SFAVD_IOC_CONTEXT_STATS` (number 7, Darwin `0xc0805607`). Its structure is
128 bytes. Existing six structure sizes and ioctl opcodes stay unchanged.
Inputs are `size=128`, `version=1`, caller-owned `ctx`, `reserved=0`, and
`generation=0` for discovery. Subsequent queries should retain the returned
nonzero generation to reject slot reuse. Output flags/counters may be passed
back unchanged. Unknown versions, sizes, reserved bits, stale generations,
foreign PID/task and absent contexts return EINVAL. Failed queries do not copy
a partial snapshot. This is context ownership, not per-file-descriptor isolation;
other descriptors in the same task already share its context access.

Reads hold the existing ioctl lock, which also serializes decode and destroy.
They do not allocate, reap, decode, expose addresses, program hardware or add
a timer. Session counters are initialized by the existing zeroed session
allocation. Only synchronous decode paths write them; interrupt handlers do not.
The getter is available while the global DART poison gate rejects new work,
provided the service remains up; its poison flag explicitly describes global
state and cannot attribute a fault to this context.

| Field | Meaning and limit |
| --- | --- |
| jobs | Same publication event as the old global jobs counter; includes HOLD slices and jobs whose run subsequently fails. |
| done | Successful final pictures after required capture synchronization; HOLD alone does not increment it. |
| errors / timeouts | Same separate run/completion-error and watchdog events as global counters. Not every API or DMA-sync failure increments errors. |
| last / total / max decode microseconds | Same successful-final-picture timing points as the existing global measurements. |
| api_decode_calls / api_decode_errors | Calls for this live, owned context and their nonzero ioctl results, including validation or synchronization errors before publication. Foreign calls are excluded. |
| capture_backing_bytes | Sum of the current capture cookies' sizes rounded to 16 KiB. Allocated/pinned backing, not unique physical resident memory. |
| capture_iova_bytes / capture_prepared_bytes | Subsets of that capture sum whose cookies currently have an IOVA or prepared descriptor. Metadata observations, not a DART page-table walk. |
| capture_buffers | Number in the current capture set. Replaced internal captures are excluded after import. |

These byte fields **exclude coded, private decoder and firmware allocations,
quarantine and all post-destroy retirement**. This is a partial response to
media's DART lifetime request. EINVAL after destroy proves no readable live
context, not zero mappings, safe GPU retirement or completion of device DMA.
Existing global teardown counters and independent consumer-lifetime checks
remain necessary. The extension does not select a smaller pool or claim better
video performance.

Run the offline checks:

```sh
python3 integrations/siliconforest/m1-avd-context-stats/test-source.py
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/m1-avd-context-stats/build-abi.nix --no-out-link
```

`test-source.py` applies the real patch with no fuzz to verified immutable
sources, then compiles the literal getter and decode function with ASan/UBSan.
Fake metadata/hardware providers exercise ownership and ABI rejection, repeated
queries, overflow/error atomicity, independent contexts, success, HOLD, run
failure, bad completion, watchdog timeout and synchronization failures. A
two-session interleaving gives 66 and 34 jobs against 100 global jobs. These
tests establish branch/accounting behavior with fake providers; they do not
establish real interrupts, locking, pinning, hardware reset or native attribution.
The SDK check builds both old/new target ABI assertions and the real kext;
cross-compiled programs are not executed on the builder.

Before installation, request source review and freeze a one-module collection
against qualified 45. Native acceptance must use two owned contexts with
interleaved real packets, check generation/foreign-task refusal and per-context
job/result totals, compare decoded pixels/PTS, and query live capture metadata
before/after import. Preserve any failed cases. Do not force decoder watchdogs
or poison the owner's machine to test diagnostic flags. Require both installed
Firefox launch routes to paint, retain the exact rollback, and restore the
standard desktop. No install or new kernel cycle is reserved by this directory.
