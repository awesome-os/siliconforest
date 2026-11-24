# Saved private capture chain auditor revision004 — offline only

Revision004 changes ONLY protocol extraction to use physical LF boundaries.
The old bytes.splitlines(keepends=True) treated each console CR as a separate
line. Record-span bytes are now retained verbatim between the unique matching
PROCESS_BEGIN/END; only the unchanged frozen d1d976 decoder applies its already
specified trailing-CR normalization. No interior blank/noise/CR-only line is
filtered. The exact record/hash/nonce/process-exit requirements remain.

The preserved actual attempt001 overall audit003 (07cc1e3c) is UNQUALIFIED:
P2 original/base physical-source equality failed, so P3–P8 are NOT_EVALUATED.
Revision004 does NOT change P2, any other gate or the source-equality criterion.
It is not used to regrade that attempt. This source proposal cannot remove that
independent remaining P2 blocker. Frozen003 and its actual failed result stay
byte-exact. Added tests use synthetic captures only, preserving all24 previous
regressions and demonstrating old003 rejection/new004 acceptance of CRCRLF plus
strict refusal of interior blank/noise/CR-only lines.

Revision003 additionally preserves BLOCKED002 byte-for-byte (receipt f64b1e65,
review73cf04e5). Its coherent saved guest_base+16KiB / entry+16KiB /
bootargs_off-16KiB variant showed that matching copies and actual image geometry
alone did not establish exact placement. The original three fixes are retained.

P3 now replays the pinned validate_layout / HV.load_raw numeric displacement
from ORIGINAL raw ADT TrustCache and SEPFW sizes, optional original preoslog,
original736 devtree_size (must equal actual original ADT length), selected
actual kernel vmax-vmin/entry/vmin, and supplied native phys_base. Formula:
`guest = phys +16MiB +align(original_dt_size)+align(original_TC_size)`;
`bootargs_off = align(actual_KC_span)+align(original_SEPFW_size)+preoslog_size`;
`entry = guest+actual_KC_entry-actual_KC_vmin`. Both alignments are16KiB.
Original descriptors must be exact16-byte pairs within the original declared
RAM extent; original preoslog must be positive and16KiB-sized if present.
The load region and displacement have explicit type/alignment/overflow/RAM
bounds. Prepared-base post-load TC/SEPFW locations never replace these inputs.
All saved start components must equal the recomputed placement.

Native phys_base is a retained, source-bound supplied starting point, not an
independently captured heap pointer. No proxy heap, actual HV_START7Q frame,
start_calls or missing transport bytes are reconstructed. These exact numeric
checks do not change P1/P10 external trust or promote self-report to hardware.
Five additional test groups cover exact F4, coherently rehashed original TC /
SEPFW size changes, explicit formula rounding, preoslog/source range failures,
and original dt_size/native envelope/image-span bounds. All24 offline groups
pass; no actual private capture has been graded.

This is a separate successor to the preserved, BLOCKED auditor001
(receipt2c3c6d7c, independent review718dadc3). Its passing11 author tests did
not override the three independent all-ten-PASS false positives. No original
source, receipt or capture is rewritten. The API and fixed ten checks remain.

The minimal corrections bind original translated MCC reg1 to frozen capture
ab00's exact descriptor guard and96 relative-read plan (last required byte
0xc06db, extent at least0xc06dc); bind saved BootArgs/start metadata to actual
raw chosen BootArgs allocation, native RAM/retained envelope and selected
kernel bytes; and compare BOTH launched four-artifact maps exactly to the
external approved manifest, including source_patch and build_record. The
kernel geometry comes from actual bounded/hash-checked image bytes using
frozen normal_image ea36952b, not a saved report's asserted entry/vmin.
The independent provisional002 coherent entry+4 substitution is also refused.

Eight focused regression groups each first require a qualified SYNTHETIC
baseline. They cover exact F1/F2/F3 repros, minimum valid/aperture-too-short,
nonzero coherent forged placement, coherent entry/report mutation, and missing
or extra artifacts. The synthetic positive now contains a fabricated FILESET
exercising the genuine frozen inspector, not an opaque fake kernel. The real
known candidate is checked only as static context; no capture is invented.

`audit.audit(cycle_directory, context, recovery=None)` reads the frozen
dd02/e099/388 capture shape, with worker files under `cycle_directory/guest` and
executed `source-<path with / replaced by __>` snapshots in the cycle directory.
All ten P1–P10 entries always remain in the output, each PASS, FAILED or
NOT_EVALUATED. Qualification requires every entry PASS. Missing raw bytes never
get reconstructed; frozen producer/handoff execution is comparison against saved
facts/planned/upload/readback files. Source identity labels and supplied external
documents are trusted inputs, not authentication. This is an AUTHOR implementation
pending independent review, not a runtime grader already used on a target.

The replay binds actual original ADT and736-byte R2/V2 BootArgs, full384 MCC
sample and prefix/96attempted+completed counts, original-tree reg1 translation,
unchanged known source properties in prepared base, final1152 native wire and its
readback, exact facts/insertion, original ADT reservation and actual final upload
AND readback. Both the frozen producer9dc and handoff087 are loaded only after
hash checks, and dependencies remain separately pinned. Exact frozen source
snapshots are derived from the controller's constants, not a guessed file count.
The current controller saves25 such files; it is not the complete execution
workspace or every40 after-tree asset. No import or path opens a transport.

P3 explicitly checks saved actual upload/readback/native pointer/layout metadata
and executed-source guard consistency. Frozen host code does NOT save a separate
raw proxy HV_START seven-word frame or `start_calls`; neither is reconstructed or
claimed independently observed. Fresh guest output and a source-bound guard
are not raw transport capture. The saved actual-upload file is the pinned host
guard's recorded successful write/readback result, not an independent bus trace.

The auditor requires one matching PROCESS_BEGIN/END, actual collector_exit0 and
the frozen d1d976 strict decoder over the exact interior span. It independently
compares the guest216 labels to hashes of the actual supplied images, retained
base ADT/original736/native1152 and frozen HV identity. Correct codec/digest alone
cannot satisfy this check. API snapshots must use actual phase3→6, historical
owned pairs3, balanced/reset3 and identity0; copy status is NEVER data readback.
GATED77/zero identity, PENDING, incomplete records and every failed call stay
unqualified. General `command_line_observed` is only a prerequisite.

P9 requires exact sealed TX: optional `/bin/sh\r`, the exact split-literal
challenge, and one fixed quoted launcher with the independent policy nonce.
It rejects extra commands/DATA/schedulers and foreign mode flags, checks actual
raw/receipt hashes and completion, parent-observed CLI1/worker1, and finalized
core/error-null/complete diagnostics. Worker1 is expected by the frozen design;
it is not incorrectly required to be0. No captured bytes or old receipts are edited.

External context JSON has only these keys; refs contain exactly `path,sha256`:

```
{"kernel":REF_OF_ACTUAL_KERNEL, "ramdisk":REF_OF_ACTUAL_RAM,
 "packet_binding":REF_OF_ROOT_EXTERNAL_BINDING}
```

Images are hashed by bounded stable-file streaming; the bounded kernel is then
read stably, hash-rechecked and inspected for actual entry/vmin. The Root-owned binding
document, if supplied, has:

```
{"kind":"PRIVATE_PACKET_EXTERNAL_BINDING_V1", "status":"VERIFIED_EXTERNALLY",
 "kernel_sha256":"...", "ramdisk_sha256":"...",
 "manifest":REF_OF_ACTUAL_MANIFEST, "review":REF_OF_APPROVED_STATIC_PACKET_REVIEW}
```

Both referenced document bytes are hash-checked; actual image hashes must match
manifest artifact entries. Review must have no blockers, all declared checks
PASS, and one `four_artifacts` entry equal to the manifest's exact artifact map.
This accepts external reviewed static binding ONLY. It is not boot permission,
source authentication or this auditor independently reviewing all RAM dumps.
Root may explicitly record `boot_admitted:false` in the binding without changing
its status or scope. Unknown future artifact hashes are never frozen in the code.

P10 is a SEPARATE optional externally verified document ref. Do not create one
without actual recovery. Supported external adapter shape:

```
{"kind":"PRIVATE_CAPTURE_RECOVERY_EXTERNAL_V1", "status":"VERIFIED_EXTERNALLY",
 "console_sha256":"...", "core_sha256":"...", "packet_binding_sha256":"...",
 "baseline_boot_id":"...", "new_boot_id":"...", "reset_attempts":1,
 "latch_absent":true, "lock_free":true, "controller_complete":true,
 "cycle_result_sha256":"...", "baseline_sha256":"...",
 "returned_linux_sha256":"..."}
```

The adapter is a Root-owned statement of independent external verification,
not a certificate manufactured by this module. Actual saved baseline/returned
boot IDs must be unequal, cycle result says Linux verified and reset0..1, and
all exact cycle/console/core/packet hashes must agree. Missing/unsupported
certificate schema yields NOT_EVALUATED; malformed/contradictory supported
schema fails. No boolean is promoted to an independently authenticated Linux
observation. Raw boot IDs/physical placement/guest token data are not exported.

CLI writes one exclusive0600 JSON result:

```
python3 -B audit_capture.py --cycle CYCLE --context CONTEXT.json --output NEW.json
```

Optional `--recovery FILE --recovery-sha256 HEX64` supplies an actual external
document. It does not inspect an active host, contact Linux or clear a latch.
No new runtime verdict is issued by source tests. `validate.py --output-dir NEW`
runs original synthetic file/codec/identity/negative tests and, when available,
hashes the already-known static candidate001 images. Synthetic external
document stand-ins test classification ONLY. The real static packet without a
capture/recovery stays unqualified, and absent actual artifacts explicitly SKIP.
Hardware STOP, GPU HOLD, shared-writer exclusion and serial-bridge STOP remain.
