# NV12 allocation and native layout

The public metadata-only `surface-layout-probe.c` is built on Linux with our
own declarations and resolves public framework APIs. Native M5 execution uses
the same binary SHA-256
`281035d5c6ac45e1a42c9724c9700cde4a047c487f311a66868139eaab0a0dce`,
a fresh temporary directory, verified SHA, 30-second bound, no DYLD override
and cleanup. Only the program's stdout/stderr returned. Its eight cases cover
420v/420f,16x8/1920x1080, heap/IOSurface. Missing optional APIs are reported
absent rather than supplied guessed signatures.

Both native heap and IOSurface allocations retain a512-byte planar header.
At1080p, Y starts at512, UV at2074112 and both planes have1920-byte rows.
The CV top-level flattened row is2884, dataSize3114720; the IOSurface top row
is also2884, allocation3114784 (dataSize+64). At16x8 the planes start at512
and1024 with64-byte rows, CV dataSize1280 and IOSurface allocation1344.
Per-plane APIs are required for planar image access. A separate explicitly
constructed Y0 IOSurface may be wrapped, but Finch's ordinary `place()` writes
the legacy planar header and must not be called over image data beginning at0.

This caught two independent bugs. FX's private GPU admission rejected nonzero
Y offsets and distinct top rows; its owner is correcting its single IOSurface
provider. Finch's CV layout rounded dataSize to flattened row×height but left
allocSize at the last plane end, under-reserving the advertised1080p size by
3744 bytes. The private lifetime override now reserves dataSize+64, guards
addition/multiplication overflow and publishes the correct planar surface top
row. Larger requested surface allocations are retained. Plane offsets and
visible plane rows do not change.

The corrected Linux/Nix CoreVideo artifact is
`/nix/store/vj1bj7a8bf0sdln1kns9qxrb9phy6xn0-siliconforest-corevideo-frame-lifetime-stage-arm64`;
the matching private VT producer is
`/nix/store/bksw818m1j90s2y46vc96s07rdnbvm18-siliconforest-videotoolbox-external-capture-stage-arm64`.
M1 baseline/fixed metadata completed20:01:41–43Z under gui.lock and bench after
the actual CIO29 recovery and TERM8 release. Both CPU and GPU allocation modes
now match every required native public layout field across all eight cases;
the old provider fails the four IOSurface cases. Complete optional-getter and
private-extension differences remain in raw stdout, not relabeled a full
native MATCH. The comparator binds the nested public probe through the
cold-verified launcher/manifest closure; the harness hashes the launcher as
argv[0], not its child. Its first identity check incorrectly compared that
launcher hash to the native probe and refused before comparison; that error
was fixed without changing either program or any captured result.

The corrected600-frame output control completed20:01:43–20:02:03Z: all visible
NV12 hashes and PTS match independent Linux FFmpeg, all600 output buffers are
GPU-backed and the first retained picture is unchanged after64 callbacks.
Actual AVD jobs/done each increase600, no new errors/timeouts/stale/DART faults,
contexts end0. This is explicitly **one CPU copy**,1866240000 bytes; external
import capability is still0. The unmodified strict zero-copy policy correctly
rejects it. CPU hashes dominate the17-second correctness run, so that wall time
is not a playback-rate benchmark. The preceding failed GPU-storage run remains
retained. No canonical replacement, direct hardware import, GPU fence or
browser1080p60 delivery is claimed.
