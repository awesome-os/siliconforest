# dma-buf fence audit against the deployed kernel

The request to add missing sync-file ioctls was based on an older source audit.
The actual kernel 47 AGX/DCP trees already include layer patches 0021, 0022,
0024 and 0026: export/import ioctls, poll callbacks, cross-kext sync-file
recognition and owner dispatch for the ioctl argument buffer. Their reference
is `sources/gpu-plan/linux-aurora`, commit `6a08a08e`, specifically
`drivers/dma-buf/dma-buf.c` and `sync_file.c`. No replacement success stub or
new fence implementation has been added here.

On 2026-11-16 the existing `m1-vulkan/test/sync-probe.c` was run on the actual
47 kernel with an exact runtime guard. It returned zero failures: timeline
operations, sync-file export/import and POLLIN, dma-buf export/import and
POLLIN/POLLOUT, and SCM_RIGHTS transfer all passed. Clone slots remained three
used, zero failures. This probe uses signalled fences and no GPU jobs. The
missing-ioctl premise is disproven; pending-fence ordering is a separate test.
CODEX-FX withdrew its unsupported missing-ioctl runtime claim.

Tiny existing probe (take the GUI/bench locks; no window or input):

```sh
env DYLD_LIBRARY_PATH=/private/var/db/sfvk/lib /private/var/db/sfvk/bin/sync-probe
```

`test-source.py` compiles the actual frozen ioctl implementation under
ASan/UBSan with fake provider fault/refcount controls. It checks flags,
READ-versus-WRITE filtering, descriptor reservation and EFAULT/ENOMEM/EMFILE
cleanup, and pending-fence references. Stub fences are appropriate only for
an empty reservation. This does not test GPU completion, poll wake-ups,
concurrent reservation updates or real descriptor lifetime.

`pending-probe.c` is a separate native probe, not a kernel change. Its
successful default mode prestarts an exec child, submits finite GPU compute,
imports its real completion sync file as a dma-buf writer fence, and exports
the reservation snapshot for a reader. The original completion descriptor is
closed. The child receives the dma-buf and snapshot through SCM_RIGHTS,
must observe **both not ready**, then polls each independently and verifies
all 1024 words of the GPU-written 4 KiB buffer against an independent CPU
xorshift oracle. Storage is coherent host-visible memory with an explicit
shader-write to host-read barrier. The queue is drained before teardown.
Already-ready work exits 77 (pending state not observed), not PASS or a
kernel failure; poll errors and wrong data fail. Work is finite, without a
GPU busy-wait on a host event. M1/G13's event implementation does not provide
such a host dependency.

On the actual kernel 48, whose GPU/core modules are identical to 47, this
probe passed. Fence fd export took about 20 microseconds, writer import
10 microseconds and reader snapshot export 4 microseconds. The child first
observed both descriptors not ready, then woke after 46.52 ms and verified
the full pattern `33c657f5`. Clone slots returned to three used, zero failures.
This qualifies a GPU producer and independent CPU consumer, pending poll
wake-up, reservation/fd lifetime and cross-process transfer. It does not
qualify a subsequent GPU consumer, IOSurface broker leases, resize, image
reuse or producer death. No GPU kernel code was changed to obtain this result.

Build the standalone Darwin SDK executable with `build-probe.nix`. On the M1
it uses the existing staged Vulkan loader and ICD. The installed test can be
run under the GUI/bench locks, without a window or input:

```sh
env VK_ICD_FILENAMES=/private/var/db/sfvk/icd.d/asahi_icd.json \
  DYLD_LIBRARY_PATH=/private/var/db/sfvk/lib \
  /private/var/db/sftb/firefox0048/pending-probe
```

Earlier failures are retained. The first native attempt failed in dyld before
reaching the driver because it requested the original versioned loader name;
the SDK now rewrites that load command and rehashes its signature. Earlier
SDK failures concerned a deprecated linker flag, feature macros and missing
dsymutil. Two subsequent probes queued work behind an unsubmitted future host
timeline point. Their five-second watchdog released that point before the
child's zero-time poll, so the pending gate failed. Timing located the delay
in `vkGetFenceFdKHR` (about 5.35 seconds), not the reservation ioctls. The exact
pinned Mesa source explains this: threaded submission waits for input-point
availability, and fence export uses `VK_SYNC_WAIT_PENDING` until submission.
The test's own dependency prevented creation of the output fence before its
host signal. This is not an established kernel bug. That design remains as
`--unsubmitted-control`; the default successful probe uses submitted finite
GPU work and a child already waiting for descriptors.

CODEX-FX and TERM11 have the live UAPI result, pending producer/CPU-consumer
proof and tiny probe for the Mesa/EGL native-fence and window-export paths.
Keep completion/VideoToolbox leases until their actual GPU consumer and image
lifetime gates pass; this standalone test does not establish those contracts. No new kernel boot
is required merely to introduce these already-live ioctl commands.
