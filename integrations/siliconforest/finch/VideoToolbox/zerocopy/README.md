# AVD capture sharing, staged source

The current installed VT decoder copies the SFAVD read-only NV12 picture into
its CoreVideo buffer. CODEX-FX owns IOSurface/CGL GPU allocation and import; this
stream owns the producer. A GPU-backed destination with a CPU row copy is not
an end-to-end zero-copy decoder. No such claim is made here.

The optional ABI1 extension in m1-avd preserves existing structs/ioctls/defaults:
SFAVD_CAP_EXTERNAL_BUFFERS (bit16) advertises SFAVD_IOC_IMPORT. One request names
ALL context capture buffers, before any MAP or decode/HOLD. Unsupported kernels
return ENOTTY; recording mode returns ENOTSUP. The producer must explicitly opt
in and retain its mappings until context destruction.

Admission requires the same owner, exact count, zero reserved fields,16KiB
aligned writable current-task ranges, complete driver sizeimage/private-tail
capacity, no arithmetic/virtual overlap and no physical-page aliases. The
kernel prepares/maps every descriptor first, rejects overlap with all existing
AVD DART pages, then maps unused DART IOVAs. Only a fully prepared set reaches
an allocation-free core commit. Failure releases the new pins/maps and retains
the previous capture set. Old buffers are freed only after successful commit.
One-time zero initialization matches original capture allocation; no picture
bytes are copied by the import. Page pinning is InOut (writable reference),
as implemented by the retained IOGeneralMemoryDescriptor. Actual GPU-map pinning
and cache coherence still require a kernel-owner hardware trial.

The host validator tests ABI528 bytes, malformed counts/flags/sizes/addresses,
virtual overlap and10000 physical-page sort/alias/search cases under ASan/UBSan.
The actual core replay tests both ordinary and imported-model capture sets,
a late invalid entry without partial commit, repeat-import refusal and refusal
after decode/HOLD. H264/HEVC/VP9 each300pictures and multi-slice H26460pictures
complete with zero model allocations left. Each instruction log repeats exactly
within its mode; imported IOVAs differ from ordinary allocation by design.
These are model results, not hardware decode or GPU imports. Initial invocations
accidentally passed relative recordings to a script that changed directory;
their vacuous results are retained, not called successful replays. The script
now resolves explicit inputs first and rejects missing inputs.

Linux/Nix SFAppleAVD kext SDK build passes; no collection was built or installed.
CODEX-TB owns kernel review/inclusion/boot. The initial v1 artifact is superseded for hardware review by the safety
follow-up described below; kernel-owner
handover is requests-for-CODEX-TB.md. AVE remains paused and untouched.

Next producer contract (not yet qualified): GPU-backed NV12 IOSurface,420v/420f,
header/data offset16384, Yplane offset0 and UVplane driver uv_offset, driver
stride and private-tail capacity, visible cropped dimensions. CoreVideo wraps
the IOSurface without a row copy. A retained AVFrame lease and producer usecount
must bridge into Firefox's cross-process MacIOSurface usecount, with CGL holding
its count until GPU completion. Count zero alone is not a GPU fence. Pool
pressure, process death and flush/reset lifetime must be tested before enabling
this path. CODEX-FX is implementing the single NV12 IOSurface/CGL path.

Evidence: runs/sffinch/avd-external-host-{legacy,import}-v2.log and the host test
binary/import source here. No1080p60 result, browser smoothness or GPU-copy
claim follows from these builds/model tests.

Safety follow-up: a DART invalidate timeout now poisons the mapper, retains
IOVA reservations and all prepared mappings/descriptors, and rejects further
map/allocate/decode work until reboot. Partial rollback has the same policy.
Context ownership retains task identity in addition to PID. The actual source
extraction fixture injects failures and checks no completion/release or IOVA
reuse before invalidation; this is logical host coverage, not hardware safety.

The public lifetime probe originally used GetUseCount to decide recycling. A
real native cross-process test caught early AVBuffer release while the child
still held its surface. That failure is retained. The corrected bridge uses
IOSurfaceIsInUse: native GetUseCount is process-local, whereas IsInUse observes
any process ([public API definitions](https://developer.apple.com/documentation/iosurface/iosurface-functions?language=objc)).
With an explicit child-held handshake, the parent sees local count0 and in-use1
while retaining the frame; after child release it sees in-use0 and releases
exactly once. This does not prove a GPU fence or prevent a consumer that first
acquires its hold after producer release. Consumer handoff ordering and
completion-held counts remain prerequisites. FX owns the one IOSurface/CGL
implementation; its shared numeric count differs from native local diagnostics.

The extended `cross-probe --process-exit` control also passes on native: the
child calls _exit with a held usecount and no explicit decrement/CFRelease;
after the parent reaps it, global use clears and AVBuffer releases exactly once.
That is process bookkeeping only. Current M1 canonical IOSurfaceLookup cannot
find the producer's global surface: both original same-byte modes return77
(unsupported), retained as FAIL rather than a lifetime pass. FX supplies a
private standalone broker cohort for the next staged ownership tests. Its
abnormal-exit behavior intentionally may quarantine counts; no native automatic
cleanup or GPU-completion parity is claimed for that provider.

The private VT producer's M1 default and best-effort controls match2400 original
H264/HEVC HW/SW PTS/dimension/hash rows, including the actual hardware-selection
query. Best effort explicitly reports zero shared frames and414720000 copied
bytes per300720p frames. Strict requests fail-closed with-12900 and no frames
when the installed kernel reports external-import capability0. Those results
qualify unchanged copy/fallback policy, not real import or1080p60 zero-copy.

`capture-probe.nix` builds a dedicated M1-only client from our existing MIT VT
CLI decoder. It requests global IOSurface output, requires real GPU FD/16KiB
data-offset/two-plane storage for every picture, retains the first CV buffer
through64 subsequent callbacks, then verifies its visible hash is unchanged.
It keeps the original per-picture PTS/hash output for independent FFmpeg
comparison. This program hashes pixels on the CPU as a correctness control;
its wall time is not a no-copy playback benchmark. It builds on Linux but
has not run against actual kernel-import support. Use only a reviewed private
VT/CV/FX IOSurface cohort, SF_VT_ZERO_COPY=require, and an authorized kernel
owner's import-capable trial. No success may be inferred from the build.

`qualify-capture.py` requires independent visible NV12 framemd5/PTS, the
fixture movie SHA, an actual successful capture client, strict import-capability
and zero-copy counters, the held-frame check, and before/after kernel statistics
with matching job/completion deltas and no faults/quarantine. Missing counters,
old-kernel stats, CPU copies, empty runs or wrong pictures cannot qualify.
The inherited CLI client is restricted here to time bases with numerator1.
Use the private producer, not the default decoder preserved by the separate
QuickTime query wrapper. The six host policy tests use explicitly synthetic
receipts and prove only classifier behavior; no hardware event is inferred.
Actual shared-capture results and browser1080p60 still require owner trials.

The source cache-ownership contract remains unqualified too: the import path
pins/maps InOut pages, zeroes them and programs DART, without an explicit data
cache handoff around decode. FX's exported BO uses writeback storage because
header atomics require exclusive accesses. Pinning, prepare/complete, DART
invalidation and `dsb` are not independent CPU/AVD/GPU coherency evidence.
Before an import-capable trial, establish actual AVD coherence or a correct
data-subrange ownership/cache protocol and test old/new contents under reuse.
Do not disable caching for the entire BO as a shortcut; shared header atomics
and conflicting aliases must be respected. This is a future prototype review
requirement, not a failure observed on the installed importcap0 kernel.

The subsequent M1 `media-private-controls-v3` establishes actual exportable GPU
storage in both parent and looked-up child (valid copied FD and16KiB data
offset), explicit cross-process H/R lifetime controls and exact byte transport.
The process-exit control remains rawFAIL: a child exiting without decrement
leaves its global count held and the decoder frame quarantined. Native clears
that count after reap. This is a real implementation limit, not a successful
automatic-cleanup or GPU-fence result.

The first `gpu-nv12-one-copy-v1` run completed600 AVD jobs with no new faults,
but all output GPU-storage checks failed. The old FX validator required Y0;
our same-byte native CoreVideo probe proves standard NV12 IOSurfaces have a
512-byte planar header and top-level row2884 distinct from plane stride1920 at
1920x1080. Do not remove the header or reinterpret the flattened row as Y stride.
FX's bounded-offset admission fix is independent of a CoreVideo allocation bug:
the last plane ends at3110912, but row rounding advertises3114720 bytes. The
private CV override now reserves the advertised size+64 and publishes the
correct flattened IOSurface row, preserving every plane offset. Corrected M1
CPU/GPU allocation metadata now matches all required native layout fields;
the optional-getter differences remain raw. Actual600-frame corrected output
also passes every independent NV12 hash/PTS and retained-picture check, with
600AVD jobs/done, no new faults and contexts0. It still copies1866240000 bytes.
The failed first run remains retained; see
`../../CoreVideo/SURFACE-LAYOUT-2026-11-15.md` for exact scope and receipts.

This current-kernel fallback is explicitly **one CPU copy** from AVD into
GPU-backed NV12 storage. A successful correctness control could remove a
second CPU texture upload through FX's GPU rectangle blit, but would not prove
direct hardware capture import, IPC/fence ordering, browser60fps or end-to-end
zero-copy. `qualify-capture.py` continues to reject every copied capture run.

FX's actual browser control on the same private cohort completes600/600 H264
1080p60-source pictures and300/300 HEVC4K30-source pictures without reported
drops. Its CGL transport counters show zero CPU upload/readback, with GPU
rectangle blits; VT still reports1866240000/3732480000 copied bytes. Browser
family CPU is125.54%/140.05% of one core over10.706/10.546 seconds. These are
**not**60fps presentation results: rVFC delivers252/250 callbacks over10seconds,
median40.02ms and p9550ms. This removes the second CPU upload, while both the
first CPU copy and presentation-cadence problem remain. Raw owner evidence is
recorded in requests-for-CODEX.md at20:05:18Z; no canonical provider changed.

The separate public `CoreVideo/display-clock-probe.nix` measures actual callback
arrival independently of advertised input/output timestamps. Same Linux-built
SHA5c23eee17c8fa69cfbc83e53d45cac40c7758ad7719907e676ec5fcdaf22c90a
runs on M5 and M1. M1 installed/private CV each produce121callbacks at measured
60.0016/59.9832Hz; native M5 is119.963Hz on its own display. The retained M1
GetActualOutputVideoRefreshPeriod returns the nominal constant, so measured
arrival is the relevant control. This isolated observation does not locate
Firefox's40ms cause or establish panel delivery, and motivates no timer patch.

## Capture-pool measurement (private, not enabled)

The shared-capture FFmpeg adapter uses the full32-buffer pool by default.
`SF_VT_CAPTURE_BUFFERS=20` is a private measurement opt-in: a request must be a
bounded decimal count at least as large as FFmpeg's codec minimum. Invalid,
overflowing or undersized requests retain32. The codec minimum alone does not
bound outstanding browser/GPU consumers, so a smaller count needs held-picture,
complete frame/PTS and browser pressure gates before selection. It changes no
reuse predicate, IOSurface/GPU hold, kernel lifetime or default policy.

The Linux/Nix candidate builds, and hosted ASan/UBSan codec-bound/input/overflow
controls pass. Native20-buffer H264 passes600 independent pixels/PTS/held64 plus clean kernel
accounting. HEVC300 matches all pixels/PTS/held64 and zero-copy counters, but its
strict global classifier FAILS:585 jobs included concurrent decoder activity,
with one preexisting context. No new faults occurred; raw failure is retained.
There is no whole-browser smaller-pool qualification or measured memory saving.
The new `firefox-media/prepare-shared-capture.py --capture-buffers 20` and
`run-shared-capture.py` retain the full independent NV12/PTS/held64 gate, use a
fresh cold prefix for each codec, and require the actual selected count in the
native log before calling it a20-buffer result. These correctness clients hash
pixels on the CPU; their runtime is not a playback-FPS/CPU benchmark.
