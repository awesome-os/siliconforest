# Decoder-to-GPU contract and measured qualification

## Current measured state, 16 November

Kernel44 qualified shared capture after the earlier teardown failure;47
retains it and46's reviewed per-context diagnostics. CODEX-TB owns kernel
build/install/recovery. The following current browser pair uses47; the qualified
CV/VT family was cold-staged, then selected by FX at09:48:25UTC. Its
installed-outer gate passed at09:49:13:600/300 pictures, zero reported drops,
zero decoder copies, expected hardware jobs/no faults and actual provider paths.
Both GPU captures were inspected. Natural Quit returned0 and the whole own
family disappeared in1.748s without cleanup signals. The separate installed
sample is RDD7.913/16.327% and family114.850/151.338% of one core, not another
matched comparison. FX owns the checked selection rollback. Full32-buffer pools remain
the default and pool retirement is off.

| Ten-second generated clip | H2641080p60 copy → shared | HEVC4K30 copy → shared |
| --- | --- | --- |
| Decoder frames |600 →600|300 →300|
| CPU decoder pixel copies/frame |1 →0|1 →0|
| Decoder copied pixel bytes |1866240000 →0|3732480000 →0|
| RDD CPU, percent of one core |14.745 →8.558|26.889 →16.533|
| Matched family CPU, percent of one core |119.024 →115.251|151.429 →149.472|
| Reported dropped frames |0 →0|0 →0|
| Median JavaScript frame-callback interval |40.02 →40.02ms|40.04 →40.02ms|
| Maximum callback gap |55.54 →51.64ms|144.86 →112.00ms|
| Actual GPU renderer images, inspected |browser/text/video visible in both modes|browser/text/video visible in both modes|

**Capture correction:** the earlier kernel45 pair's images used `--screencapture`, which
reconstructs the CPU scene and omits external GPU buffers. They cannot prove
on-screen blank media; visibility remains unqualified. Original images and
failed verdicts are retained. Corrected captures use the existing capture-only
renderer helper and require its `source=renderer` receipt plus visual review.
The current pair uses that corrected route; all four images were inspected.
The historical45 pair's89/85 H264 drops and845/883ms callback gaps remain in
the retained results; the new pair does not explain that earlier regression.

This is one unprofiled pair, not a qualified speed improvement. Final diagnostic
canvas hashes match within each codec. CGL CPU transport upload/readback are zero
in both modes; its GPU blits remain nonzero. Those counters exclude whole-browser
QuartzCore uploads and the diagnostic canvas readback, so they do not establish
that the whole browser makes no CPU pixel copies. Each phase has exactly600/300
kernel jobs/done and no new errors, timeouts, DART faults or quarantine. Repeated
VT finalizer log rows must not be summed as additional decoded frames.

FX035's25Hz JavaScript notification follows Gecko's playback-position scheduler:
`MediaDecoderStateMachine::UpdatePlaybackPositionPeriodically` uses
`AUDIO_DURATION_USECS=40000` divided by playback rate; logical-position updates
invalidate the VideoFrameContainer. Observed0.5/1/2 rates yield80.06/40.04/20.02ms
notifications. Isolated M1 CoreVideo callbacks run near60Hz; VT output and GPU
plane bindings also reach the source60/30Hz. No CoreVideo25Hz timer fix follows
from these measurements, and JavaScript notification cadence alone is not panel
presentation cadence. Historical H264 runs nevertheless have real long stalls
and reported drops; no such drops occurred in the current pair.

The separate submit trace places478.258/475.124/275.730ms gaps outside VT calls
during playback; its longest playing-time H264 VT call is5.643ms. CoreVideo
continues ticking during those gaps, while already queued GPU frames drain.
This narrows the upstream submission/backpressure/scheduling investigation but
does not identify its cause. The no-input private launcher also skipped the
installed per-key platform defaults; its corrected private-profile path now
uses the installed helpers and records actual pref values. Native same-cohort
A/B confirms the requested preferences. The corrected actual GPU captures
paint without a view-observer workaround; read-only AppKit observations are
deferred. End-frame visibility is qualified, while continuous panel cadence
and long-running all-content behavior remain separate gates.

## First native import attempt: failed and rolled back

2026-11-16: CODEX-TB's combined Firefox42 kernel exposed ABI1/import capability,
then the first strict H264 capture attempt panicked at `vm_upl.c:1360`,
`VM_OBJECT_WIRED_PAGE_UPDATE_END` (`!__overflow`). HEVC was not attempted.
TB restored the qualified devfs-only kernel and released it at01:20 UTC.
No shared-buffer decode, zero-copy, K14 or cache-coherency success is claimed.
Do not retry that kernel/provider combination before the kernel owner fixes
and qualifies the cause. The failed capsule, full UART and packet stay private.

Exact failed-kext symbolication places the caller at
`sf_avd_os_dma_free -> avd_buf_free -> sf_avd_session_destroy -> context_destroy`.
The DMA-free return address follows the virtual memory descriptor `complete`
call. The assertion adds a signed wire-count delta to the object's wired-page
count; its actual operands and descriptor type are not known from that stack.
This is teardown evidence, not proof that import preparation itself caused it.
Keep the original failure; do not remove accounting assertions, skip unpinning
or release backing without the required ownership/completion contract.

## Required contract and original gap

Before the shared-import extension, AVD allocated private capture pages and
mapped them read-only to the decoder process, with no exported dma-buf.
The current GPU ABI allocates/imports GEM BOs: VM_BIND.userptr names an array
of bind operations, not arbitrary decoder image pages. Consequently an extra
GL upload cannot honestly be called zero CPU copy without a real page-import
path. Existing one-copy GPU-backed NV12 and CGL blits remain the fallback.

The implemented choice is **caller-owned GPU BO import into AVD**, preserving
ABI1 and advertising SFAVD_CAP_EXTERNAL_BUFFERS bit16 only for implemented
support. No second decoder buffer-export ABI or competing IOSurface/CGL
provider is requested. FX's existing BO/fileport/broker keeps the same object
available in the RDD and GPU process.

- Import the entire capture set before MAP/HOLD/decode, owning task and PID
  checked. Validate writable16KiB-aligned ranges, count, overflow, virtual and
  physical aliases, stride, UV offset and all driver/private-tail capacity.
- Data begins at BO base+16384. Exclude the shared atomic header page. Use
  NV12 420v/f, visible dimensions, Y0, driver UV offset/stride and rounded full
  sizeimage. Reject unsupported layouts and10-bit rather than relabeling them.
- Pin/map every descriptor before an allocation-free all-or-nothing commit.
  MD/kernel-map/task/GEM pager ownership must survive caller unmap/process
  death while AVD can still touch the pages. A failed DART invalidation or
  ambiguous decode completion quarantines pages/IOVAs/pins until reboot.
- Establish real CPU↔AVD↔GPU cache ownership for WB data pages. Pinning,
  prepare/complete, DART invalidation, barriers and no-op cache KPIs do not
  establish visibility. Keep the atomic header WB and avoid conflicting aliases.
- Include the8-line IRQ attribution fix41b6f2d34, preserving its actual-source
  race tests. It is not a proved hardware fix for the rare wrong frame.

TB's exact frozen-kernel audit shows ordinary flush APIs are barrier-only;
CleanPoC_DcacheRegion_Force performs actual clean/invalidate and is a candidate.
Its proposed isolated patch uses bounded page calls at initialization,
before device ownership, after real completion before CPU chroma handling and
after CPU writes before publication. **Clean-after-device-write requires no
stale CPU-dirty lines:** exclude CPU writers/readers during device ownership,
otherwise writeback can overwrite fresh decoded bytes. Cache correctness and
exported-BO UPL/pager retention were hardware-unqualified at that review. The
subsequent corrected44 native decode, GPU-held-frame and current browser
qualifications establish their stated tested scope; the original42 teardown
panic remains recorded above.

Producer retains AVFrame/CV/IOSurface storage; consumer acquires before producer
release and holds through actual GPU completion. IsInUse/count zero is not a
GPU fence. Process-death quarantine is safe but not automatic cleanup parity.
The userland lease/IPC/fence chain must be proved with FX independently of the
kernel's pinning. No frame becomes recyclable merely because a callback returns.

Exact source/artifact, cache-order and frozen-base review is in
`runs/m1-coord/requests-for-CODEX-TB.md`. TB owns the one combined future kernel
with the shared-import fix, excluding failed dock diagnostics/DP changes.
Root does not build/install/boot a kernel. The durable owner grant now authorizes Firefox kernel optimizations without
another decision. Owner-idle waits are lifted; TB announces and owns installations;
source/host/SDK results remain separate from native qualification.

Qualification requires independent full-frame hashes/PTS, actual GPU FD/backing,
held64/pool-pressure/flush/process-death controls, matching AVD completions and
no new faults/stale events. Then measure real browser1080p60 and4K30: zero copied
pixel bytes, copies per frame, separate RDD/family CPU, dropped frames and rVFC
cadence. Existing125–140% CPU and40ms callbacks are private one-copy baselines,
not proof of the cost's location or installed-owner performance.

## Consumer source audit and remaining proof

This is an upstream-only source observation. The local Firefox160 bundle names
mozilla-release source stamp `d84e7a6936554efe26097d9d9b24a0e11330e630`;
the public Hg raw URLs were unavailable and GitHub did not resolve that stamp.
The following files were therefore read at pinned upstream Git
`f898416c560977636760a1fdab1d3f468cb62486`, with URLs/SHA256 in the ignored
`sources/ref-firefox-media/provenance.json`. This is not proof that the installed XUL follows every
same path. Do not substitute current upstream for release disassembly/runtime
identity when qualifying actual lifetime behavior.

- [AppleVTDecoder::OutputFrame](https://github.com/mozilla-firefox/firefox/blob/f898416c560977636760a1fdab1d3f468cb62486/dom/media/platforms/apple/AppleVTDecoder.cpp)
  synchronously wraps CVPixelBufferGetIOSurface in a MacIOSurface and stores it
  in MacIOSurfaceImage before returning from its hardware-image callback.
  [MacIOSurface](https://github.com/mozilla-firefox/firefox/blob/f898416c560977636760a1fdab1d3f468cb62486/gfx/2d/MacIOSurface.cpp)
  increments use in its constructor and decrements in its destructor. This
  supports the proposed initial producer-to-RDD hold, rather than assuming
  that CF retention by itself prevents decoder-slot reuse.
- [MacIOSurfaceTextureData](https://github.com/mozilla-firefox/firefox/blob/f898416c560977636760a1fdab1d3f468cb62486/gfx/layers/opengl/MacIOSurfaceTextureClientOGL.cpp)
  retains that surface and serializes its ID; this path supplies no fence-holder
  ID. The [texture host](https://github.com/mozilla-firefox/firefox/blob/f898416c560977636760a1fdab1d3f468cb62486/gfx/layers/opengl/MacIOSurfaceTextureHostOGL.cpp)
  looks it up into another MacIOSurface and retains it for its renderer. The
  release160 TextureClient/IPC destruction acknowledgement must still be
  checked: these two endpoint holds alone do not establish overlap across IPC.
- [RenderMacIOSurfaceTextureHost](https://github.com/mozilla-firefox/firefox/blob/f898416c560977636760a1fdab1d3f468cb62486/gfx/webrender_bindings/RenderMacIOSurfaceTextureHost.cpp)
  binds each plane and waits for a write fence only when one was supplied.
  Hardware decode completion and kernel cache publication must therefore
  precede our VT callback. A renderer-side optional-fence branch does not
  manufacture the missing decoder completion/cache contract.

Our matching FX CGL0005 patch adds a global use around NV12 plane import/blit
and real glFinish, then drops it only on successful completion. It copies on
GPU into an independent rectangle texture, so later rendering does not use
AVD storage. Failed completion quarantines the hold. Keep this conservative
completion policy until measured replacement fencing is qualified; removing
glFinish because IsInUse appears false would reopen the race.

There are currently two plane import/blit/completion operations per NV12 image.
Their wait time is a profiling candidate, not the demonstrated cause of40ms
callbacks. Record separate GPU-process/CGL wait cost before considering a
nonblocking completion queue. Any alternative must keep both plane holds until
actual GPU completion, handle context loss/process death, and retain the old
safe fallback. Kernel-import qualification must include the whole RDD→IPC→GPU
lease chain, not merely the producer's held-frame CLI test.

FX subsequently matched the installed Breakpad FILE entries to Mozilla revision
`2887d9de1f69c0e9776413da80e7ba0d1225dd9b`. Its separate CA swapchain audit
(`runs/firefox/fx020-source`) identifies a concrete consumer contract: writer
use ends before front publication, pooled old surfaces are excluded by
IOSurfaceIsInUse, and CALayer/WindowServer is expected to hold reader use.
TERM8 owns the CA snapshot/retirement fix. This is a stronger source-contract
candidate for foreign1024px tiles, not runtime attribution of FX011/012. It
does not retroactively make the five upstream decoder files above release
proof. CV frame lifetime and final BGRA CA tile lifetime are separate holds.

## Corrected native import gate

The corrected combined kernel passed three native WB-BO duplicate-map/unmap,
16KiB data-offset import and context-destroy rounds before any decoder jobs.
Then strict public-VT capture passed6001080p H264 frames and3004K HEVC frames:
every visible NV12 hash and PTS matched independent Linux references, the first
picture stayed unchanged across64 later callbacks, GPU-backed output was
present, and producer counters reported zero copied pixel bytes. Kernel job/done
counts matched, contexts returned to zero, and no new errors, timeouts, DART
faults, poisoned objects or quarantine were reported. H264/HEVC capture took
17.32/31.07s including expensive diagnostic hashing; these are not playback FPS.
This follows the retained failed UPL teardown attempt; it does not erase it.

This qualifies the private decoder-output/storage contract. The program did not
sample those frames in Firefox's GPU compositor or prove its cross-context GPU
completion. Firefox copies/frame, CPU, dropped frames, cadence and actual visible
presentation still require the coherent consumer test. No kernel build or boot
was performed by the media agent; CODEX-TB owns those operations.
