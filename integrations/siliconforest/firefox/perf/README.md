# Firefox performance on SiliconForest (FXPERF)

Root causes of wasted CPU, GPU and memory in Firefox 160 on our stack, each with the measurement that shows it.
Fixes are handed to the owner of the layer they touch; patches here are files for adoption, not installed by FXPERF.

## Method
- One own Firefox instance per hold (`-no-remote`, profile `/private/var/db/sffxperf/p1`, fresh each run), only
  synthetic input through the compositor's virtual seat, Marionette (port 28291) for navigation, the Gecko profiler
  (all processes, all threads, 2 ms, native stacks) and about:memory dumps.
- `tools/fxperf_m1.py stat`: per process and per thread CPU from `proc_pid_rusage`/`proc_pidinfo` over 30 s, context
  switches, wake-ups, footprint. `regions`: a vmmap-like map from `PROC_PIDREGIONPATHINFO` (resident, private, dirty
  per region and per file). `sffxperf-fontprobe.c`: an `__interpose` dylib for the own instance that logs whole-file
  reads and maps of font files with their caller chain (`FXFONT` lines).
- `tools/mk_perf_launcher.sh`: a private copy of the installed launcher for the own instance only; it can select the
  private GPU provider set (`FXPERF_MANIFEST`) and put candidate providers from `/private/var/db/sffxperf` in front
  (`SFFXPERF_FRAMEWORKS`), so a fix is measured in the real browser before its owner adopts it.
- Analysis offline: `tools/gprof.py` (per-thread busy samples, symbolicated: XUL through Mozilla's symbol server, our
  libraries with llvm-symbolizer), `markers.py` (marker rates), `leafpaths.py` (caller chains of a leaf),
  `memrep.py`, `fontmem.py`. Raw captures stay in `runs/fxperf/<tag>/` (not in git).

## Findings
### Memory: ten private copies of the CJK font in the parent (fixed by `patches/onyx2d-font-files-mapped-once.patch`)
Firefox's AsyncFontInfoLoader (`CTFontInfo::LoadFontFamilyData`) makes a CTFont for every face record. CoreText's
`createGraphicsFont` falls back to `O2DataProviderCreateWithURL` for collection faces and variable instances, and
`-[O2DataProvider initWithURL:]` reads the whole file into a private NSData each time. NotoSansMonoCJK-VF.otf.ttc
(31.9 MB) is read 10 times and the copies stay (CoreText keeps the graphics font per record): 304 MB private dirty
in the parent, plus 3 copies of NotoEmoji. macOS keeps font data mapped from the file (clean, shared pages).
The patch maps an installed font file once per process, read-only, and shares it among providers.
Measured on the M1 (runs/fxperf A2 = before, A3 = with the patch; same scenario: fresh profile, example.com, cnn.com):
| | before | after |
|---|---|---|
| whole-file reads of a font (FXFONT) | 10 (CJK) | 0 |
| private whole-file copies in the parent (region map) | 10 x 30.4 MB + 3 x 1.9 MB | 0 |
| parent heap-allocated (about:memory) | 610.8 MB | 301.3 MB |
| parent resident-phys-footprint | 921.1 MB | 633.8 MB |
| family footprint, example.com idle / cnn.com idle | 1411 / 1904 MB | 1087 / 1623 MB |
The 30 FreeType maps of the same file (one per FT_New_Face) are clean shared pages: they inflate RSS sums
(913 MB of the parent's 1.1 GB RSS) but not physical memory.

### FX-029 (idle cnn.com): the page animates; our per-frame cost is the waste
- Gecko side (profile A2): the content refresh driver ticks every vsync, "HasImageAnimations /
  HasPendingRenderingSteps (Update animations and send events)": CSS animations (46 iterations/s), animated images
  and a muted autoplay video. 60 display lists/s, WebRender builds and composites 60 frames/s. A Mac does the same
  Gecko work; there each frame recomposites only the changed layer.
- Ours, per frame (installed c19 GPU path, clean run D-base-1: no tracing, no profiler, no Marionette, 30 s): family
  101 % of a core; the compositor receives 75 commits/s (55 shown/s), each the full 1920x1440 window with full damage.
  The parent main thread is 38 % CPU and busy 85 % of wall time: 80 % of it in `-[CALayerContext _layerTreeChanged]`
  -> `layerContextNeedsDisplay:`: `-[CARenderer render]` of the whole tree 29 %, the export
  (`copySFFXRenderedSurface` -> `CGLSFFXCopyFramebuffer` -> glFinish) 37 %, WindowServer `sfGPUCompose` 9.5 %;
  43.5 % of its wall time is blocked in glFinish (`drmSyncobjWait`).
- Requests (no patch proven yet): EGLImage per IOSurface in CGL; a GPU-side fence instead of the export glFinish;
  damage-limited render/compose/commit (QuartzCore, WindowServer, compositor); no flush for an unchanged frame.
- Tried and NOT effective on the c19 path (one run each, runtime changed between runs, inside run-to-run noise):
  IOSurface lookup of a known port without mapping (`build/0234`, the parent compositor thread is only 3 % on c19;
  its remaining per-frame maps are probably new video surfaces - inference), one glFinish per CGL write-back (GPU Renderer 8.3 -> 8.4 %).
- CODEX-FX's opt-in 0218 (layer-tree notifications batched to 60 Hz ticks), built on c19 QuartzCore and enabled in my
  instance (runs F-base-1 / F-coal-1, frozen runtime): compositor commits 70 -> 61/s, shown 50 -> 44/s, but family CPU
  101.9 -> 102.5 % and parent main thread 38.7 -> 39.6 %: no CPU gain (one pair; not pursued).
- File system (kernel): unlink and fsync of a 4 KB file take 15 / 14 ms (p50) on the M1's ext4; Firefox's IndexedDB,
  cache and quota threads spend their idle time there.

### FX-031 (idle static page) on c19
- Warm profile, example.com, 30 s after a 45 s settle, no tracing (runs E2, E4): family 2.4 % / 2.1 % of a core,
  248 / 210 context switches/s, 7 interrupt wake-ups/s; no GPU commits in steady 10 s windows (compositor WMPACING).
  Left: the parent main thread (~0.85 %) and Glean's dispatcher thread (~0.7 %).
- The first minutes of a fresh profile cost 19-27 % (E1, E3): IndexedDB/quota first-run work whose threads sit in
  ext4 unlink/fsync (15 / 14 ms per 4 KB file on the M1).

### Memory at 1 / 10 / 30 tabs (c19, after the font fix was installed)
Family sum of RSS / of phys_footprint: 1 tab (example.com, warm) 2.0 / 0.95 GB; 10 static tabs 2.6 / 1.28 GB; 30 tabs
(10 static + 20 Wikipedia) 4.1 / 2.25 GB (FXQA's earlier RSS sums: 2.3 / 4.0 / 5.4 GB). RSS sums overstate: every
process maps the same font files (the parent alone maps NotoSansMonoCJK 31 times = ~980 MB of "RSS", clean shared
pages). Per process, our libraries' private dirty pages are small (Mesa ~5 MB, AppKit/Foundation/libSystem ~1 MB each);
XUL's relocated data is 55-66 MB per process (Mozilla's, as on macOS). Gecko's own accounting at 30 tabs: explicit
602 MB, heap-allocated 772 MB across all processes.

### FX-030 (tab switch)
Installed c19, 12 Command-number switches among 10 static tabs (run B2, no profiler, inject request time -> the page's
Date.now, FXQA's method): visibilitychange p50 80 ms, second rAF p50 117 ms, p95 297 ms (unchanged from FXQA's 119 ms).
Partial attribution (profile B1 with 1 ms sampling, inflated):
Key -> AsyncTabSwitch:Start 3-5 ms; the content repaints at once (PaintWhileInterruptingJS) and the GPU process
composites the new tab within ~3 ms (DidComposite); AsyncTabSwitch:Finish at ~21 ms; but the parent's compositor
thread receives the native-layer commit for that frame only ~55 ms after Start and renders it (CARenderer) at ~57 ms,
then the window present + compositor flip (~22 ms commit->flip) follow. The ~50 ms between the GPU composite and the
parent's commit is not explained yet (not a busy thread: the GPU Renderer and the parent compositor thread wait).

### Start-up (installed c19 path, warm profile, 3 runs)
exec -> first presented frame 2.00 / 1.75 / 1.82 s (median 1.82 s); window listed by the compositor ~2.6 s (polled).

## FXPERF2: the damage-only frame path (patches 0300-0303, opt-in, own instance only so far)
Built by `patches/fxperf2-build.nix` against the committed Cocoa series (QuartzCore, AppKit, libWindowServer) and the
installed canonical CGL (`cgl-gpu.nix` canonical + NV12, byte-identical to the installed 52bff2c2); measured with
`tools/fxperf2_ab.py` (own Firefox `-no-remote`, copy of a warm profile per run, interleaved variants in one gui.lock
hold, loaded image paths checked per run) and `tools/fxperf2_sum.py`.
- 0300 QuartzCore (`SFFX_GPU_DAMAGE=1`, with `SFFX_GPU_PRESENT=1`): before a frame, a walk of the layer tree (no GL)
  gives every layer its window box and a signature (paint-order neighbour, depth, matrix, size/clip, contents and both
  seeds, opacity/colours, border/shadow/mask). Changed, added and removed layers give the damage rectangle; the frame
  is drawn into the retained drawable with the scissor on it, layers wholly outside it are not drawn, and a
  rectangular axis-aligned clip is a scissor box with the stencil's exact pixel coverage instead of a stencil pass
  with a full-window stencil clear. No damage: no frame (AppKit then presents nothing). The export copies only what
  the frame being overwritten lacks (buffer age of the two export surfaces). The renderer reads geometry through
  properties when a layer has no animations (KVC boxed every value in NSValue/NSInvocation).
- 0301 AppKit: the GPU layer is registered with its damage; a layer-only flush with nothing drawn into the window's
  backing store says so to the window server.
- 0302w WindowServer: the CPU backing store is compared/uploaded only when AppKit may have drawn (by changed rows);
  each output frame is brought up to date with the damage since the picture it holds (scissored compose); the same
  picture is not presented again; `wl_surface.damage_buffer` carries the real rectangle; one texture per alternating
  layer surface.
- 0303 CGL: `CGLSFFXCopyFramebufferRect`; with `SFFX_GPU_IMAGE_CACHE=1` an IOSurface's EGLImage is kept per surface
  (global iosurfaced ID), so a texture re-pointed at a known surface does not import and VM-bind the dma-buf again.
- Kernel limit found on the way: every flush that touches a shared dma-buf waits for the GPU on this kernel (no dma-buf
  sync-file ioctls, `../../m1-mesa-asahi/KERNEL-NEEDS.md` #1), so replacing the export `glFinish` with a fence alone
  removes no wait; the gain comes from less GPU work and fewer imports/flushes per frame.

| cnn.com idle, % of one core, median [range] of 3 runs | series baseline | 0300-0303 |
|---|---|---|
| family (A2) | 101.0 [99.6-106.2] | 71.3 [70.0-71.9] |
| parent process | 55.7 [54.9-57.2] | 22.0 [21.3-22.8] |
| parent main thread | 48.4 [47.7-49.9] | 18.7 [18.3-19.1] |
| GPU / content process | 21.2 / 16.6 | 23.2 / 18.3 |
| compositor commits / shown / replaced-unshown per s | 85.2 / 59.2 / 26.2 | 59.9 / 58.6 / 1.3 |
| damage per presented frame | 11.06 MB (whole window) | 3.0 MB |
A1 (earlier build, pointer-keyed image cache, base compared every frame): parent 54.7 -> 40.0, main 47.4 -> 32.5.
A3 (cand vs QuartzCore -O2) is void: cnn.com was loading (parent Cache2 I/O 13-18 %) in 5 of 6 runs.

Later measurements, rebased set (series 20bbd0b17 carrying 0300-0302w + deltas 0304/0305w + 0303), one framework set
with the opt-in env off vs on, quiet warm profile (background services off for every variant):
- cnn.com idle (A6, 3+3): parent 58.9 -> 39.7 %, main thread 52.5 -> 36.2 %, family 111.0 -> 88.6 %, replaced 11.0 ->
  0.6/s. Today's cnn.com is heavier (content 21-23 %, damage 5.3 MB/frame) than in A2. Profile P2 of the "on" run:
  444 of 1038 main-thread samples were Mesa converting the autoplay video's NV12 planes, uploaded as
  GL_LUMINANCE/LUMINANCE_ALPHA, pixel by pixel (`_mesa_format_convert`) -> 0306 uploads them as R8/RG8 (copy path).
- local animation page (A7, 3+3): parent 44.1 -> 21.6 %, main 38.2 -> 19.2 %; presented frames bistable in both
  variants (2/3 "on" and 1/3 "off" runs at ~33/s with bursts: shown->shown p50 16.7 ms, gaps to ~145 ms).
- example.com idle (A8, 3+3): family 0.4 / 0.5 % of a core, no commits in either: FX-031's 6.6/13 % is profile
  background work (first-run IndexedDB/cache, safe browsing, ...), not drawing.
- tab switch (T1, 24+24 Command-number switches): 2nd rAF p50 107 -> 104 ms, p95 131 -> 129 ms: unchanged; FX-030 is
  not the parent's frame cost.
- correctness: FXLEAD's self-checking tile page after 5 scroll steps, 12/12 "off" and 12/12 "on" trials with every
  visible cell right (G1, G2); pixel compare of the final window (marker-cropped) on static pages: scrolled page and
  scrolled tile page 0 differing pixels (X1; X2 pending).
- 0306 QuartzCore (always on): an NV12/x420 video surface CARenderer cannot bind on the GPU is uploaded as R8/RG8
  (R16/RG16) planes, which Mesa copies, instead of LUMINANCE/LUMINANCE_ALPHA, which it converted pixel by pixel. 1080p60
  H.264 (V2, damage on in both, 3+3): parent 69.1 -> 29.4 %, main thread 64.4 -> 24.6 %, family 101.5 -> 63.0 %;
  colour bars sampled identical (same RGB). Applies to the whole-frame path too.
- presented frames: with the display link on the compositor clock (0149) 4 of 9 "on" runs and 1 of 3 "off" runs fell
  into a bursty ~30-35 shown/s mode (anim A7, video V1, A9); with SF_CV_TIMER=1 (the old 60 Hz timer) 3/3 runs at
  59.2-59.8/s in A9 but 1/3 slow in A11 (video). Totals: damage-on with the compositor clock 6/15 runs slow, with the
  timer 1/6, damage-off 1/6: more frequent with the damage path, cause open (0149 not established). Scroll (S3):
  53.5 / 53.9 shown/s in both variants (below 59.5 with or without).
- pixel compare (marker-cropped, same layout): scrolled page 0 differing pixels (X1, X2), unscrolled tile page 0 (X2),
  scrolled tile page 0 (X1; X2's two runs ended at different scroll offsets). On three static pages a position:fixed
  marker is missing in BOTH variants (reported to TERM10/FXLEAD), so they could not be compared: 3 pages compared, not 6.
- pacing, more runs (A12/A13 anim 6+6, A14 with SF_WS_TRACE): across all variants 9 of 41 runs fell into the ~30-46
  shown/s mode (damage+cache 6/21, whole-frame 1/6 then 2/3 in A14, damage without the image cache 1/6, old timer
  1/3); slow runs do not track compositor CPU. In A14 the whole-frame path logged 285-412 "no frame free" deferrals per
  10 s (the window server's 4 output frames all held by the compositor; presents deferred to the next release), the
  damage path 8-10: starvation is a mechanism of the old path that the damage path mostly removes; the damage path's
  own occasional slow runs are unexplained. The image cache by itself: parent 28.2 -> 22.2 % on the anim page (A12).
