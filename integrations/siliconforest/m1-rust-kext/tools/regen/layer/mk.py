# splits `diff -ruN a b` of the scratch layer into the topic patches under lkpi/patches-layer
import re, subprocess, sys, os
out = sys.argv[1]
# the directory holding a/ (pristine tree slice) and b/ (edited), see regen.sh
work = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
d = subprocess.run(["diff", "-ruN", "a", "b"], capture_output=True, text=True, cwd=work).stdout
files = re.split(r"(?m)^diff -ruN .*\n", d)[1:]
topics = {
 "0001-page-size-follows-the-kernel-16k": ("""fbsdkpi: the layer's page size is the kernel's (16 KiB on Apple Silicon)

LinuxKPI and the FreeBSD compatibility code were built for 4 KiB pages on every arm64 board,
while the xnu seam (sf_fbsd_os.cpp: sfos_map_pages, sfos_malloc_contig, sfos_vtophys) already
works in xnu pages. PAGESIZE/B_PAGE_SIZE/PAGE_SIZE and PAGE_SHIFT are the only two places
the number is written down; everything else in the layer derives from them.
UPSTREAM: yes, as a per-board setting (boards with 4 KiB kernels keep 4096/12).
""", []),
 "0002-xnu-seam-nonblocking-malloc-and-lock-storage": ("""fbsdkpi: xnu seam: non-blocking sfos_malloc, locks in caller storage, counters

sfos_malloc ignored `canwait` (IOMalloc always may block). It now uses kalloc_data, the kpi
that takes Z_NOWAIT. sfos_mtx_setup/sfos_rw_setup make an unlocked xnu lock in 16 bytes of
caller storage, so the Haiku locks above no longer allocate (next patch). Counters for tests.
UPSTREAM: yes.
""", []),
 "0003-haiku-locks-embed-the-xnu-lock": ("""fbsdkpi: Haiku mutex and rw_lock hold their xnu lock instead of allocating it

The xnu lock behind a mutex/rw_lock (and so behind every FreeBSD mtx/sx and LinuxKPI mutex,
spinlock_t and wait queue) was allocated on first use and freed only by *_destroy. Linux code
never has to destroy a lock (mutex_destroy is a debug no-op there, spinlocks have none), so
every lock that was dropped with its container leaked one xnu lock: the Rust crate's Mutex,
SpinLock and CondVar, and equally the DRM C core. The lock is now two words inside the Haiku
lock; nothing is allocated and nothing needs freeing.
UPSTREAM: yes (fixes the leak for all LinuxKPI/DRM kexts).
""", []),
 "0005-driver-core-surface-of-linux-7.2": ("""fbsdkpi: the driver-core surface Linux 7.2 drivers (and the Rust abstractions) use

- struct device_driver: acpi_match_table, suppress_bind_attrs, p_cb.post_unbind_rust
- struct device_node embeds its fwnode_handle; linux/property.h and linux/of_reserved_mem.h
  with the prototypes of sf_linux_fwnode.c (the file itself is m1-gpu-dt's, added to the
  LinuxKPI source list here)
- sf_linux_drvcore.c: devres_node_* (7.2's managed-resource nodes, carried by LinuxKPI's own
  devres list), __platform_driver_register, platform_device_put,
  platform_get_irq_byname_optional, platform_bus_type, acpi_of_match_device (no ACPI: false),
  of_root and friends set at init
UPSTREAM: yes, with m1-gpu-dt's sf_linux_fwnode.c and sf_linux_of.patch.
""", []),
 "0006-of-text-properties-named-by-the-node": ("""fbsdkpi: of: a node can say which of its properties are text (pd,string-props)

m1-gpu-dt's kernel/sf_linux_of.patch, change 1, re-made against the file because that patch
does not apply as written (hunk line counts). to_fdt() guesses text from the bytes; one value
of the M1's GPU table (opp01 opp-microwatt = 0x003a4148, stored 48 41 3a 00) passes for a
string and would reach the driver unswapped. SFGpuDT lists its text properties.
One addition to that patch, found on hardware (boot rp01: opp1 still read 1212234240):
SFGpuDT sets the list only on nodes that have text, and the opp nodes have none. A node
without the list below a node with one is taken to have no text at all.
UPSTREAM: yes.
""", []),
 "0008-platform-pre-probe-hook": ("""fbsdkpi: platform bus: a hook between making the device and probing it

sf_platform_pre_probe, when a kext sets it, is called with the platform_device right before
the driver's probe; a nonzero return keeps the device from being probed. SFAgx uses it to name the GPU's mailbox (sf_apple_mbox_bind): Apple's
device tree has no mboxes phandle from the GPU to gfx-asc, and the driver is unmodified.
UPSTREAM: only until the published GPU node carries a mailbox reference the layer resolves.

Also in this file (boot dp04): a device is entered in the bus's device table before its probe
runs, as Linux registers a device first, so that a probe, or a component bind running
inside it, finds its own device with of_find_device_by_node(); the entry is removed when the
pre-probe hook or the probe fails. UPSTREAM for this part: yes.
""", []),
 "0009-physmem-is-the-machines": ("""fbsdkpi: si_meminfo() reports the machine's memory, not a constant 4 GiB

sf_lkpi_physmem() was a constant. It now asks the xnu seam (sfos_physmem(): hw.memsize; that
function is in patch 0002's files). Found by m1-lkpi-extras: the Asahi driver sizes its GPU
memory limits from totalram.
UPSTREAM: yes.
""", []),
 "0010-dma-map-sg-for-platform-devices": ("""fbsdkpi: dma_map_sg works for a device-tree platform device, and its failure is an error

- bus_dma: a tag without an IOKit device (bus_get_dma_tag() only knows pci, so every
  platform device) made _bus_dmamap_load_phys() return EINVAL, and so dma_map_sg map nothing.
  Such a device has no IOMapper; the seam's identity mapping applies.
- dma_map_sgtable() returned 0 with nents 0 when nothing was mapped. Linux returns an error,
  and a caller that trusts 0 (drm_gem_shmem_get_pages_sgt) went on with an empty table.
UPSTREAM: yes.
""", []),
 "0011-page-pool-for-single-pages": ("""fbsdkpi: single pages come from a pool of 1 MiB wired chunks; M_NOWAIT waits when it can

Boot ag03: alloc_page() failed after 66 pages (and the Asahi driver's third 1 MiB GEM object
with it) on a machine with 16 GiB. Each page was one physically-contiguous
IOBufferMemoryDescriptor plus a record allocated M_NOWAIT, and since patch 0002 a M_NOWAIT
allocation really is Z_NOWAIT, which fails whenever the kalloc zone has to grow.
- sf_linux_page.c: order-0 pages without GFP_DMA32 are taken from chunks of 64 pages
  (sfos_pages_alloc: one ordinary wired xnu allocation, per-page physical addresses, no
  contiguity or mask), with a free bitmap per chunk; a fully free, unmapped chunk goes back
  to xnu above a 4 MiB reserve. Higher orders and GFP_DMA32 are unchanged. The page hash
  grows to 16384 buckets. sf_linux_page_stats() for diagnostics.
- vmap() of pool pages maps sub-ranges of the buffer descriptors that own them
  (sfos_map_owned_pages) instead of a physical-address descriptor over xnu-managed pages
  (boot ag05: "IOMemoryDescriptor physical with managed page" on a DEBUG kernel); the
  physical path stays as the fallback and for pages that are not from the pool.
- xnu seam (in patch 0002's files): sfos_pages_alloc/free; sfos_malloc(canwait=0) retries
  blocking when the calling thread may block (not interrupt context, interrupts and
  preemption enabled), counted by sfos_malloc_nowait_retried().
UPSTREAM: yes.
""", []),
 "0012-dri-ioctl-restart-codes-are-eintr": ("""fbsdkpi: drm: an ioctl interrupted by a signal returns EINTR, not Linux's 512

sf_dri_ioctl() handed -ERESTARTSYS (and its relatives) to user space as errno 512. Linux
never shows those to a process. EINTR is what libdrm's drmIoctl() retries on.
UPSTREAM: yes.
""", []),
 "0013-layer-tables-off-the-image": ("""fbsdkpi: the thread and area tables are allocated at load instead of being .bss

sThreads (8192 slots, 720 KiB) and sAreas (4096 slots, 128 KiB) were static arrays in every
kext that carries the layer; the kernel collection has 16 MiB for all kexts together. They
are now allocated by a static constructor when the kext loads. Same sizes, same behaviour.
(The page hash of patch 0011 is allocated the same way, in lkpi_page_init.)
UPSTREAM: yes (saves 850 KiB of collection space per LinuxKPI kext).
""", []),
 "0014-thread-slots-follow-the-xnu-thread": ("""fbsdkpi: a thread slot is dropped when its thread_t is reused by another thread

The layer keeps per-thread state (linuxkpi's task_struct among it) in a table keyed by the
thread_t address, and is never told when a thread it did not start exits. A new thread at a
recycled address inherited the dead thread's state. Each slot now records xnu's unique thread
id (thread_tid); on a mismatch the old state is released and the slot starts clean. The
table therefore holds at most one slot per thread_t address ever seen. sf_thread_slot_stats()
for diagnostics. (sfos_thread_tid() is in patch 0002's files.)
UPSTREAM: yes.
""", []),
 "0015-memory-pressure-runs-the-shrinkers": ("""fbsdkpi: xnu memory pressure runs the Linux shrinkers and trims the page pool

One kernel thread per kext waits in mach_vm_pressure_monitor() (com.apple.kpi.mach) and, at
most once a second while the pageout daemon wants pages, calls linuxkpi's shrinkers
(sf_lkpi_lowmem(), the entry FreeBSD's vm_lowmem event would have used) and then returns
every free pool chunk to xnu (sf_linux_page_trim()). The seam half (sfos_pressure_start) is
in patch 0002's files, the pool half in 0011's.
UPSTREAM: yes.
""", []),
 "0016-scoped-guard-labels-are-unique": ("""linuxkpi: two scoped_guard() in one function no longer define the same label

linux/cleanup.h built the label as `___label_ ## __COUNTER__`; the paste keeps __COUNTER__
from expanding, so every scoped_guard() used the label ___label___COUNTER__ and a function
with two of them did not compile (drivers/gpu/drm/apple has 16 such functions).
UPSTREAM: yes (FreeBSD LinuxKPI).
""", []),
 "0017-backlight-and-platform-driver-fields-of-linux-7": ("""linuxkpi: backlight and platform_driver fields a Linux 7 display driver names

- linux/backlight.h: BACKLIGHT_PLATFORM/FIRMWARE, enum backlight_scale,
  backlight_properties.state/.scale, BL_CORE_SUSPENDED/FBBLANK, backlight_ops.controls_device
- platform_driver.driver_managed_dma
Declarations only: nothing in the layer acts on them yet.
UPSTREAM: yes.
""", []),
 "0018-of-find-compatible-node-continues-after-from": ("""fbsdkpi: of_find_compatible_node() continues after `from`

It ignored `from` and returned the first match every time: for_each_compatible_node() and
anything built on it looped forever once a node matched (boot dp01, the display driver's
for_each_matching_node over "apple,dcp"). Now a pre-order walk from the tree root that
returns the first match after `from`. Found and written by m1-dcp-dart (ANSWERS.md, item 3).
UPSTREAM: yes.
""", []),
 "0019-dma-buf-of-another-kext-is-a-dma-buf": ("""fbsdkpi: drm: a dma-buf file made by another DRM kext is recognised

is_dma_buf_file() compared the file's operations with this kext's own table. Every DRM kext
links its own copy of sf_dma_buf.c, so a dma-buf exported by one (SFAgx's render node) was
not a dma-buf to another (SFDcp's card node): dma_buf_get() returned -EINVAL. linux_file gets
a kind word, set for dma-buf files; both copies are built from the same sources, so the
structures match.
UPSTREAM: yes.
""", []),
 "0020-dri-fds-wake-select-poll-kqueue": ("""sf_dri: select(), poll() and kqueue on a DRM fd wake when an event is queued

dri_select() only reported readiness and never recorded the waiter: select() slept until
its timeout, and poll()/kqueue refused the device (xnu implements poll with kqueue, and a
character device must opt in with cdevsw_setkqueueok()).
- SFDRMDri: a selinfo per open file; dri_select() records the waiter and re-checks;
  sf_dri_wakeup(cookie) wakes it; the major opts in to kqueue.
- fbsdkpi/drm/src/sf_drm_cdev.c: each open DRM file keeps an entry on its drm_file's event
  wait queue that forwards the DRM core's wake_up to sf_dri_wakeup().
UPSTREAM: yes (SFDRMDri and fbsdkpi together).
""", []),
 "0021-dma-buf-sync-file-ioctls": ("""fbsdkpi: drm: DMA_BUF_IOCTL_EXPORT_SYNC_FILE and IMPORT_SYNC_FILE

Both returned ENOTTY. Mesa calls them for every shared buffer on every submission and
falls back to waiting on the CPU when they fail. dma_buf_export_sync_file() and
dma_buf_import_sync_file() of the pin's drivers/dma-buf/dma-buf.c (lines 450-551), compared
line by line, on the layer's dma_resv, dma_fence_unwrap and sync_file. DMA_BUF_IOCTL_SYNC,
which returned 0 for anything, is the pin's case of dma_buf_ioctl() as well (flags
validated, dma_buf_begin/end_cpu_access).

The functions are a port of GPL-2.0-only code, so they are in a file of their own,
drm/src/pd_dma_buf_syncfile.c, with that licence and the original notice (pd_dma_buf.c is
MIT and only forwards the two commands). drm.cmake lists the file apart from the MIT ones.
UPSTREAM: yes; the tree then carries a GPL-2.0-only file in the DRM layer, and every DRM
kext that links the layer links it.
""", []),
 "0022-sync-file-and-dma-buf-fds-wake-select-poll-kevent": ("""fbsdkpi: drm: select(), poll() and kevent on sync_file and dma-buf descriptors

A sync_file's poll only reported whether the fence had signalled and a dma-buf had no
poll at all (always ready), and nothing woke a waiter: xnu's sf_lfile select routine
recorded nothing. Needs the kernel side (integrations/siliconforest/m1-dcp/kernel-patches/
0002: sf_lfile_wakeup(), a selinfo, a kevent filter); with it:

- linux/poll.h: poll_table carries linux's _key, with poll_requested_events().
- sf_lfile_lkpi.c: linux_poll_wait() (it was an empty function in sf_drm_os.c) queues the
  file on the wait queue its poll names, the first time a poll with a table names it, and
  the queue's wake_up then calls sf_lfile_wakeup(); the file leaves the queue when its
  last reference goes. sf_lfile's poll op passes a table and maps select's direction to
  events the way linux's select does (POLLIN_SET, POLLOUT_SET, POLLEX_SET). A poll called
  with a NULL table (the DRM device nodes, patch 0020) is unchanged.
- sync_file (sf_sync_file_poll.c, sync_file.h): wq, flags and cb, and sync_file_poll(),
  fence_check_cb_func() and the release/alloc parts of the pin's drivers/dma-buf/
  sync_file.c: the first poll puts a callback on the fence that wakes the queue.
- dma-buf (sf_dma_buf_poll.c, dma-buf.h): poll, cb_in and cb_out, and dma_buf_poll(),
  dma_buf_poll_cb() and dma_buf_poll_add_cb() of the pin's dma-buf.c. One difference:
  the callback's fput() of the reference dma_buf_poll() took is done by a work item,
  because this layer's fput() runs the final release immediately, in the fence's
  signalling context (linux's defers it).

The two new files are ports of GPL-2.0-only code and carry that licence and the original
notices; drm.cmake lists them apart.

Not done: a sync_file made by another DRM kext is still not recognised by
sync_file_get_fence() (it compares f_op, as is_dma_buf_file() did before 0019), so a GPU
fence cannot be given to the display driver as IN_FENCE_FD or imported into its dma-buf.
UPSTREAM: yes, with the kernel patch; same licence note as 0021.
""", []),
 "0027-backlight-class-helpers-as-linux-has-them": ("""fbsdkpi: backlight: ops_lock, and get/set brightness as linux has them

The display driver's backlight (drivers/gpu/drm/apple/dcp_backlight.c) is a backlight
class device; these are the class pieces it and its user-space interface rely on, checked
against include/linux/backlight.h and drivers/video/backlight/backlight.c of the pin:

- struct backlight_device has ops_lock (initialised at registration, which now also copies
  props.scale);
- backlight_get_brightness() is 0 for a blanked backlight (it returned the remembered
  level, so the driver's set path sent that level while powered off);
- backlight_device_set_brightness() takes ops_lock and returns a negative errno (it
  returned positive EINVAL and took no lock);
- BACKLIGHT_POWER_ON/OFF.
backlight_is_blank() (patch 0017) also looks at BL_CORE_SUSPENDED and BL_CORE_FBBLANK now.
There is still no class registry or sysfs here: the SFDcp kext exposes its one backlight
as sysctls (integrations/siliconforest/m1-dcp/kext/sf_dcp_backlight.c).
UPSTREAM: yes.
""", []),
 "0026-linux-file-ops-run-in-the-copy-that-made-the-file": ("""fbsdkpi: drm: a linux file's descriptor ops run in the layer copy that made the file

With two DRM kexts loaded, every ioctl on a dma-buf or sync_file descriptor of the kext
that registered its sf_lfile ops first failed with EFAULT (boot mm01: DMA_BUF_IOCTL_SYNC
and EXPORT_SYNC_FILE on the GPU driver's dma-buf; Mesa's IMPORT_SYNC_FILE at every swap).
xnu's sf_lfile has one ops table, the last registrant's. Its ioctl op parks the argument
buffer in its own copy's `current` task and calls the file's unlocked_ioctl, which is the
other kext's code: that copy's copy_from_user() looks in its own `current`, where no
buffer is parked.

linux_file gains sf_lfile_owner, set when the file gets its first descriptor (always from
the copy that made it), and every op (hold, release, poll, ioctl, seek, mmap_pages) hands
a file of another copy to that copy's op. A file is then handled by one copy throughout.
UPSTREAM: yes; unnecessary once the layer is shared between the DRM kexts.
""", []),
 "0025-wq-highpri-queues-run-above-kernel-threads": ("""fbsdkpi: WQ_HIGHPRI work queues run above ordinary kernel threads

alloc_workqueue() and alloc_ordered_workqueue() dropped their flags, and every layer
thread is a kernel thread at the default priority. linux runs WQ_HIGHPRI work on
high-priority workers; the one that matters here is the RTKit receive queue, where the
display coprocessor's swap-complete message is handled and timestamped. Boot nk01 showed
one completion processed about 14 ms late (kernel event timestamps 30.6 ms then 3.0 ms
apart) with nothing wrong on the panel.

- linux/workqueue.h: the two macros call linux_create_workqueue_highpri() for WQ_HIGHPRI.
- linux_work.c: that function; its taskqueue threads are spawned with a priority value
  spawn_kernel_thread() (sf_haiku_kernel.cpp) hands to sfos_thread_start_highpri().
- xnu seam (in 0002, with the other seam halves): sfos_thread_start_highpri() sets thread
  precedence +8 (priority 89: above default kernel threads at 81, below the preemption
  band at 92). Boot-arg sflkpi_wq_highpri=0 keeps the default priority; one kprintf line
  says which is in effect.

system_highpri_wq is still the shared short queue. No DRM core or driver code in the
SFAgx/SFDcp kexts asks for WQ_HIGHPRI except RTKit, whose compat header bypasses the
macro; the kexts redirect that one call (their CMakeLists).
UPSTREAM: yes.
""", []),
 "0024-sync-file-of-another-kext-is-a-sync-file": ("""fbsdkpi: drm: a sync_file made by another DRM kext is recognised

sync_file_get_fence() and the merge ioctl compared the file's f_op with this kext's own
table, so a sync_file made by the GPU kext was "not a sync_file" to the display kext: a
GPU fence could not be given to the display driver as IN_FENCE_FD, nor imported into one
of its dma-bufs. As 0019 did for dma-bufs: sync_file_create() marks the linux_file
(sf_kind = SF_LFILE_KIND_SYNC_FILE) and the check accepts the mark. The fence inside is
used through its own ops (wait, callbacks, release), which is how the display driver
already waits on the fences of an imported buffer's reservation object.

Limit that stays while each DRM kext has its own copy of the layer: dma_fence_is_array()
and dma_fence_is_chain() compare ops pointers, so the other kext's fence array is one
opaque fence here (waited on as a whole, not unwrapped or merged element by element).
UPSTREAM: yes.
""", []),
 "0023-dma-buf-descriptor-mmap": ("""fbsdkpi: drm: mmap of a dma-buf descriptor

mmap() of a dma-buf descriptor failed with EINVAL in xnu itself. With the kernel side
(integrations/siliconforest/m1-dcp/kernel-patches/0003: sf_lfile_ops.mmap_pages) a shared
mapping is made the way the DRM device nodes' are: xnu asks once for the physical pages.

- linux/mm.h: struct sf_vma_pages and vm_area_struct.sf_pages, the page question carried
  down an mmap op in the vma.
- sf_lfile_lkpi.c: the mmap_pages op. For a dma-buf file it builds the vma and calls
  dma_buf_mmap_internal(); the vma lives as long as the mapping, holds the file reference
  linux's vma->vm_file holds, and vm_ops->close() runs (from a work item) when xnu drops
  the last mapping. An exporter whose mmap op succeeds without answering is refused.
- sf_dma_buf_mmap.c: dma_buf_mmap_range_check() and dma_buf_mmap_internal() of the pin's
  drivers/dma-buf/dma-buf.c (GPL-2.0-only, own file, as 0021 and 0022). The exporter is
  reached through dmabuf->ops->mmap, so the file may belong to another DRM kext.

The exporters: drm_gem_dmabuf_mmap() and drm_gem_prime_mmap() are the DRM core's,
unchanged. shmem objects (SFAgx) answer in drm_gem_shmem_mmap() (m1-drm72 patch 0009), DMA
objects (SFDcp) in the dma_mmap_wc() that drm_gem_dma_mmap() calls (m1-dcp).

DMA_BUF_IOCTL_SYNC (in 0021's file) is now the pin's: flags validated, and
dma_buf_begin/end_cpu_access called. Neither exporter has cpu-access ops, so it changes
nothing for them but the validation.
Not done: MAP_PRIVATE (EINVAL), and a mapping is never revoked.
UPSTREAM: yes, with the kernel patch.
""", []),
 "0004-allocator-exact-sizes-alignment-realloc": ("""fbsdkpi: allocator: exact ksize, malloc_aligned, realloc zeroing, contigmalloc fixes

- heap blocks record the size that was asked for; malloc_usable_size()/ksize() return it
  (they returned the padded size, so krealloc could hand out stale padding as data)
- ksize()/realloc on contigmalloc memory (every kmalloc above a page) read the word in front
  of the area as a heap header; they now ask the area table, and realloc frees with
  _kernel_free
- realloc with M_ZERO (__GFP_ZERO) zeroes the bytes past the old contents
- malloc_aligned() honours its alignment (it was plain malloc)
- kmalloc above a page with GFP_ATOMIC/GFP_NOWAIT fails instead of blocking in contigmalloc
UPSTREAM: yes.
""", []),
}
def topic(path, hunk, lineno):
    if path.endswith("sf_linux_page.c") and re.search(r"sf_pool|pooled|SF_PAGE_HASH|sf_page_fail|sfos_pages|sfos_map_owned|sf_page_hash = |\*\*sf_page_hash|pressure|sf_linux_page_trim", hunk): return "0011-page-pool-for-single-pages"
    if path.endswith(("limits.h", "param.h", "sf_linux_page.c")): return "0001-page-size-follows-the-kernel-16k"
    if path.endswith(("sf_fbsd_os.h", "sf_fbsd_os.cpp")): return "0002-xnu-seam-nonblocking-malloc-and-lock-storage"
    if path.endswith(("linux/device.h", "linux/of.h", "platform_device.h", "fbsdkpi.cmake", "property.h", "of_reserved_mem.h", "sf_drvcore.h", "sf_linux_drvcore.c")): return "0005-driver-core-surface-of-linux-7.2"
    if path.endswith("sf_lkpi_kern.c"): return "0009-physmem-is-the-machines"
    if path.endswith(("bus_dma.cpp", "dma-mapping.h")): return "0010-dma-map-sg-for-platform-devices"
    if path.endswith(("SFDRMDri.cpp", "sf_dri_cdev.h")): return "0020-dri-fds-wake-select-poll-kqueue"
    if path.endswith("sf_drm_cdev.c") and re.search(r"sf_dri_event_wake|event_wait|wait_queue_t wake|drm_file.h", hunk): return "0020-dri-fds-wake-select-poll-kqueue"
    if path.endswith("sf_drm_cdev.c"): return "0012-dri-ioctl-restart-codes-are-eintr"
    if path.endswith(("linux/backlight.h", "linux_pci.c")) and re.search(r"ops_lock|BACKLIGHT_POWER_ON", hunk): return "0027-backlight-class-helpers-as-linux-has-them"
    if path.endswith("linux/backlight.h") or (path.endswith("platform_device.h") and "driver_managed_dma" in hunk): return "0017-backlight-and-platform-driver-fields-of-linux-7"
    if path.endswith(("linux/mm.h", "sf_dma_buf_mmap.c")) or (path.endswith("drm.cmake") and "sf_dma_buf_mmap" in hunk): return "0023-dma-buf-descriptor-mmap"
    if path.endswith(("linux_work.c", "linux/workqueue.h")) or (path.endswith("sf_haiku_kernel.cpp") and "SF_THREAD_PRI_HIGH" in hunk): return "0025-wq-highpri-queues-run-above-kernel-threads"
    if path.endswith(("sf_lfile_lkpi.c", "linux/fs.h")) and re.search(r"sf_lfile_owner", hunk) and not any(l.startswith("+") and "sf_lfile_owner" not in l for l in hunk.split("\n")[1:]): return "0026-linux-file-ops-run-in-the-copy-that-made-the-file"
    if path.endswith("sf_sync_file.c") and re.search(r"sf_kind", hunk): return "0024-sync-file-of-another-kext-is-a-sync-file"
    if path.endswith("sf_lfile_lkpi.c") and re.search(r"mmap_pages|sf_lfile_mapping", hunk): return "0023-dma-buf-descriptor-mmap"
    if path.endswith("sf_dma_buf_syncfile.c") or (path.endswith("drm.cmake") and "sf_dma_buf_syncfile" in hunk): return "0021-dma-buf-sync-file-ioctls"
    if path.endswith(("sf_dma_buf_poll.c", "sf_sync_file_poll.c", "sf_sync_file.c", "linux/sync_file.h", "linux/poll.h", "sf_lfile_lkpi.c", "sf_drm_os.c", "linux/dma-buf.h", "drm.cmake")): return "0022-sync-file-and-dma-buf-fds-wake-select-poll-kevent"
    if path.endswith("sf_dma_buf.c") and re.search(r"sf_dma_buf_poll", hunk): return "0022-sync-file-and-dma-buf-fds-wake-select-poll-kevent"
    if path.endswith("sf_dma_buf.c") and re.search(r"sync_file|SYNC_FILE", hunk): return "0021-dma-buf-sync-file-ioctls"
    if path.endswith(("linux/fs.h", "sf_dma_buf.c")): return "0019-dma-buf-of-another-kext-is-a-dma-buf"
    if path.endswith("linux/cleanup.h"): return "0016-scoped-guard-labels-are-unique"
    if path.endswith("sf_linux_platform.c"): return "0008-platform-pre-probe-hook"
    if path.endswith("sf_linux_of.c") and "sf_of_next" in hunk: return "0018-of-find-compatible-node-continues-after-from"
    if path.endswith("sf_linux_of.c"): return "0006-of-text-properties-named-by-the-node"
    if path.endswith("lock.h"): return "0003-haiku-locks-embed-the-xnu-lock"
    if path.endswith("sf_haiku_kernel.cpp") and re.search(r"sf_tid|sThreadsRecycled|stale", hunk): return "0014-thread-slots-follow-the-xnu-thread"
    if path.endswith("linux_shrinker.c"): return "0015-memory-pressure-runs-the-shrinkers"
    if path.endswith("sf_haiku_kernel.cpp") and re.search(r"sf_tables", hunk): return "0013-layer-tables-off-the-image"
    if path.endswith("sf_haiku_kernel.cpp"):
        return "0004-allocator-exact-sizes-alignment-realloc" if re.search(r"heap|malloc", hunk) else "0003-haiku-locks-embed-the-xnu-lock"
    return "0004-allocator-exact-sizes-alignment-realloc"
# One hunk, two topics: the added lines matching rx go to a later patch. Returns the hunk
# without them and the hunk that adds only them (the rest as context, removals gone).
def split_added(h, rx):
    lines = h.split("\n"); head, body = lines[0], [l for l in lines[1:] if l != ""]
    g = re.match(r"@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)", head)
    early = [l for l in body if not (l.startswith("+") and rx.search(l))]
    late = [l if (l.startswith("+") and rx.search(l)) else " " + l[1:] for l in body if not l.startswith("-")]
    def mk(start, ls):
        # patch(1) takes a hunk with more leading than trailing context for one at the end
        # of the file: keep three lines of context on each side, as diff does
        start = int(start)
        lead = next(i for i, l in enumerate(ls) if l[0] != " ")
        if lead > 3: ls = ls[lead - 3:]; start += lead - 3
        trail = next(i for i, l in enumerate(reversed(ls)) if l[0] != " ")
        if trail > 3: ls = ls[:len(ls) - (trail - 3)]
        old = sum(1 for l in ls if l[0] in " -"); new = sum(1 for l in ls if l[0] in " +")
        return "@@ -%s,%d +%s,%d @@%s\n%s\n" % (start, old, start, new, g.group(3), "\n".join(ls))
    e = mk(g.group(1), early) if any(l[0] in "+-" for l in early) else ""
    return e, mk(g.group(2), late)
SPLITS = [("sf_lfile_lkpi.c", re.compile(r"sf_lfile_owner"), "0026-linux-file-ops-run-in-the-copy-that-made-the-file"),
          ("linux/fs.h", re.compile(r"sf_lfile_owner"), "0026-linux-file-ops-run-in-the-copy-that-made-the-file"),
          ("sf_sync_file.c", re.compile(r"sf_kind|SF_LFILE_KIND_SYNC_FILE"), "0024-sync-file-of-another-kext-is-a-sync-file")]
for f in files:
    m = re.match(r"--- (a/\S+).*\n\+\+\+ (b/\S+).*\n", f)
    path = m.group(1)[2:]
    hunks = re.split(r"(?m)^(?=@@ )", f[m.end():])
    per = {}
    for h in hunks:
        if not h.strip(): continue
        for suffix, rx, later in SPLITS:
            if path.endswith(suffix) and any(l.startswith("+") and rx.search(l) for l in h.split("\n")) \
               and any(l[:1] == "+" and not rx.search(l) for l in h.split("\n")[1:]):
                h, late = split_added(h, rx)
                per.setdefault(later, []).append(late)
        if not h.strip(): continue
        per.setdefault(topic(path, h, 0), []).append(h)
    for t, hs in per.items():
        topics[t][1].append(f"--- a/{path}\n+++ b/{path}\n" + "".join(hs))
for t, (msg, parts) in topics.items():
    text = msg + "\n" + "".join(parts)
    open(f"{out}/{t}.patch", "w").write(text)
    print(t, "+%d -%d" % (len(re.findall(r"(?m)^\+[^+]", text)), len(re.findall(r"(?m)^-[^-]", text))))
