# Multi-core lock audit of the drivers and the Linux compatibility layer

10 November. Tickets #36 M4, #37 N9, #26 C10. Everything here comes from reading source; the
fixes compile (the desktop collection builds) and none of them has run on the M1 yet.

How it was done: five read-only passes over the code (layer primitives; layer deferred work and
containers; DRM node, dma-buf and sync-file glue; DCP, DART, RTKit and PDAgx glue; Wi-Fi, PCIe,
AIC, SMC, HID). Every finding marked **checked** was re-read against the source by the author of
this file before it was written down or fixed; findings marked **reported** come from a pass and
were not re-read. Line numbers are those of the compiled layer tree
(`m1-rust-kext/lkpi/default.nix -A tree`, patches-layer 0001-0027 applied), paths under
`src/Kernel/fbsdkpi/` unless they start with a directory of `integrations/puredarwin/`.

## What the layer is on 8 CPUs

| Primitive | What it really is | Real on SMP |
|---|---|---|
| `spinlock_t`, `raw_spinlock_t`, all `_irq`/`_irqsave`/`_bh` forms | a sleeping XNU `lck_mtx` (via FreeBSD `mtx`, Haiku mutex); nothing is masked | excludes: yes |
| `struct mutex`, `rw_semaphore`, `rwlock_t` | XNU `lck_rw` | yes |
| `atomic_t`, `atomic64_t`, bit operations, `cmpxchg`, `refcount_t`, `kref` | compiler `__atomic` builtins, sequentially consistent | yes |
| RCU (`rcu_read_lock`, `synchronize_rcu`, `call_rcu`, `kfree_rcu`) | two-phase reader counters with a real grace period, **per kext copy** | yes within one kext (X2) |
| completions, `wait_event`/`wake_up`, condition variables, `msleep`/`wakeup`, sleepqueues | one global `lck_spin` with interrupts off; the waiter is registered before the lock is dropped | yes (W5, P5 fixed) |
| interrupt handlers | the primary interrupt only signals; the Linux handler runs on a work loop thread | never concurrent with itself |
| `local_irq_save`, `preempt_disable`, `local_bh_disable`, `might_sleep`, `in_interrupt` | nothing / always false | **no-ops** |
| per-CPU data, `smp_processor_id`, `num_online_cpus`, `on_each_cpu` | one shared slot, CPU 0, 1 CPU, runs once locally | **not per-CPU** (P13) |
| `synchronize_irq`, `disable_irq`, `enable_irq` on a platform interrupt | empty | **no** (P7 fixed, P8 open) |
| workqueues | FreeBSD taskqueue on Haiku threads; ordered queues have one thread, `system_*wq` share four | state machine is atomic; flush was wrong (W1 fixed) |
| timers | one callout thread per kext | serialised; drain was wrong (W4 fixed) |

Two structural facts behind several findings:

- **Each DRM kext (PDAgx, PDDcp) links its own copy of the layer, the DRM core and RTKit.** Every
  `static` exists once per kext, but buffers (`dma_buf`, `dma_resv`), fences and Linux files cross
  between the copies.
- **All AIC interrupts are routed to CPU 0**; threads run on any CPU.

## Findings

Severity: critical = memory corruption, use after free, deadlock or permanent hang reachable in
normal desktop use (compositor, GL clients, Wi-Fi); high; medium; low.

### Fixed in this pass

| Id | Sev | Where | What breaks (two interleavings) | Fix |
|---|---|---|---|---|
| **D1** checked | critical | `Extensions/PDDRMDri/PDDRMDri.cpp:169` (`selrecord`), `:185` and `:116` (`selwakeup`) | The select/kqueue record of a `/dev/dri` open has no lock. (a) kqueue, which is also `poll`: the compositor re-arms its knote on CPU A (`selspec_attach` sets `SI_SELSPEC`) while the flip-complete wakeup on CPU B clears the flag having found no knote yet; the knote is attached with the flag clear and is never woken again: the display freezes. (b) `select`: the wakeup tears the wait queue down on B while A's `selrecord` re-initialises and links it; A's link is lost. | layer 0028: one mutex around `selrecord` and around `selwakeup`/`selthreadclear`, never held across the kext's poll |
| **D2** checked | medium | `PDDRMDri.cpp:81-87`, `:112-118` | A clone's slot is freed only by `DEVFS_CLONE_FREE`, which XNU calls just when it cannot make the vnode (`bsd/miscfs/devfs/devfs_tree.c:1346`). Every open since boot keeps its slot: the 1024th open of any dri node fails. Not a race, found by the audit. | 0028: the slot goes back at the last close and after a failed open |
| **D3** reported | low | `PDDRMDri.cpp:225-228` | Two DRM kexts registering at once each create the lock and a character major. | 0028: locks made in the kext start routine, major made under the lock |
| **D4** checked | critical | `drm/src/pd_dma_buf.c:152-158` against `pd_lfile_lkpi.c:47-55` | The dma-buf and its file have separate counts. (a) CPU B closes the last descriptor: `f_count` 1→0, release not yet under `db->lock`; CPU A exports the same buffer (`PRIME_HANDLE_TO_FD`), finds `db->file`, raises the count 0→1 and installs a descriptor; B frees the file. (b) The last `fput` comes from the poll work item while an export revives the file: release runs twice, the buffer is freed twice. | layer 0029: the reference is taken only if the count is not zero, otherwise a new file is made |
| **P1** checked | critical at boot | `xnu/pd_fbsd_os.cpp:62-83` | `pdos_xnu_init` tests `sGroup`, set two statements before the global wait lock exists. (a) CPU B sees `sGroup`, returns, and `pdos_wait_lock` takes a NULL spin lock. (b) Two first callers each make a wait lock; waiters and wakers then use different ones. Window: two nubs of one kext starting on two CPUs (PDDcp matches both `apple,dcp` and `apple,display-subsystem`). | layer 0034: one caller elected by compare-and-swap, "done" published last |
| **P2** checked | high at boot | `xnu/PDDRMPlatformDevice.cpp:203`, `PDDRMDevice.cpp:255`, `PDFBSDEthernet.cpp:62` | The attach lock is `IOLockAlloc`ed at first use without a lock: two nubs each get their own and run `pd_fbsd_init` side by side (`Giant` re-initialised while held, every SYSINIT twice). | 0034: one lock made in `pdos_xnu_init`, handed out by `pdos_attach_iolock()` |
| **P3** checked | medium at boot | `pd/lkpi/pd_linux_platform.c:35-43`, `pd_linux_of.c:21-29`, `pd_linux_clk.c:31-39` | Locks `mutex_init`ed behind a plain flag: a second CPU re-initialises the lock the first holds; both then edit the list. | layer 0033: the flags go; a zero-filled mutex sets itself up by compare-and-swap |
| **P4** checked | medium | `freebsd/linuxkpi/common/include/asm/barrier.h:58-59` | `smp_mb__before_atomic`/`smp_mb__after_atomic` are compiler barriers (right for x86 only). `atomic_inc; smp_mb__after_atomic; load flag` against `store flag; smp_mb; atomic_read`: both sides read the old value. | layer 0030: `smp_mb()` on non-x86 |
| **P5** checked | critical | `include/linux/sched.h:109-111`, `src/linux_schedule.c:116-130` | `set_current_state` is a plain store. (a) `drm_syncobj_array_wait_timeout`: the waiter's state store is still in its store buffer when it loads "fence not signalled"; the signaller reads `TASK_RUNNING` and does not wake it; the waiter sleeps, for ever with an infinite timeout. (b) Any kthread loop `set_current_state; if (!stop) schedule()` against `kthread_stop`. | 0030: full barrier after the state store and after taking the sleep queue lock in the waker |
| **P6** checked | medium | `pd/lkpi/pd_linux_page.c:408-412` | `put_page` loads the count, compares, then decrements: two CPUs dropping the last two references both take the early return; the page is never freed. | layer 0032: compare-and-swap loop |
| **P7** checked | high | `pd/lkpi/pd_lkpi_libkern.c:347-351` | `synchronize_irq` is empty. A driver masks its device, calls it, frees its state; the handler is still running on the work loop thread of another CPU. Also `free_irq` did not wait. Users: `drivers/gpu/drm/apple/dcp-fabric.c:737`. | layer 0035: platform interrupts count handler invocations in progress; `synchronize_irq` and `pd_platform_free_irq` wait for zero |
| **P9** checked | low | `src/linux_schedule.c:408-414` | `wake_up_bit`/`wake_up_atomic_t` wake one waiter; a second one sleeps on. | layer 0031: broadcast |
| **W1** checked | critical | `haiku/freebsd_network/taskqueue.c:149-161`, `fbsd_subr_taskqueue.c:214-228` | `flush_workqueue`/`drain_workqueue` queue a barrier with priority `USHRT_MAX` (255 in the 8-bit field); Linux work has priority 0, so the barrier is inserted at the **head** and the flush returns with the queued work still to run. (a) `apple_rtkit_reinit` (`m1-gpu-rtkit/src/rtkit.c:1256`) flushes, frees the buffers, and the queued receive work then runs on them. (b) On a queue with several threads an idle thread runs the barrier at once while work is running on another CPU. The first half was already wrong on one CPU. | layer 0036: barrier at the tail; the flush also waits for what was running when it began |
| **W4** checked | medium | `haiku/freebsd_network/callout.cpp:223-232` | `del_timer_sync`/`hrtimer_cancel` poll "is it running" without the callout lock; between two runs of a self-re-arming callback there is no current callout, the poll takes that for the end, the caller frees the timer. | layer 0037: the test is made under the lock |
| **W5** checked | medium | `src/linux_schedule.c:289-306` | `wake_up()` stops after one waiter although the layer has no exclusive waiters: two entities flushing on one scheduler, the second sleeps until its timeout. | layer 0040: every waiter is woken |
| **W6** checked | medium | `pd/pd_haiku_kernel.cpp:1057-1098` | The area table (every allocation above 16 KiB) is written under a lock but read and freed without it: `ksize`/`krealloc`/`kfree` on one CPU read a slot another CPU is reusing; the wrong length is copied or the wrong block freed. | layer 0039: lookups and delete take the lock |
| **X1** checked | high | `src/linux_lock.c:72-145` with `haiku/freebsd_network/condvar.cpp:62-66` | A `ww_mutex` (every `dma_resv`) is shared by PDAgx and PDDcp, but its slow path sleeps on a condition variable under the kext's own `ww_mutex_global` and wait lock. (a) The compositor's PRIME import (PDDcp) is about to sleep; the GPU client's unlock (PDAgx) signals before the waiter is on the list: it sleeps with no timeout. (b) Both copies edit the waiter list at once under different locks; a later wakeup writes to a dead stack frame. | layer 0038: the waiter drops the global lock, sleeps one tick and retries the trylock; the condition variable is no longer used |
| **X3** checked | high | drm-kmod `drivers/dma-buf/dma-fence.c:86-95` | Each kext counts fence contexts from 1 while fences cross kexts; "same context" means "same timeline". A display fence whose number equals a GPU scheduler entity's is dropped as the entity's own, or replaces the GPU fence in a buffer's reservation: a wait is skipped. Not a race, found by the audit. | m1-drm72 `patches/drm-kmod/0010`: each copy numbers from a base made of its counter's address |
| **H1** checked | medium | `PDArmPlatformExpert/PDAppleAIC.cpp:260-280` | The handler has no barrier after `interruptActive = 1`; XNU's own has one, and `unregisterInterrupt`/`disableInterrupt` spin on that flag. (a) Wi-Fi off/reload removes its event source on CPU 3 while CPU 0 has stored the flag but loaded "not disabled": the handler calls into the freed event source. (b) The stores ending the handler are seen in the other order by `enableInterrupt`: the source stays masked. | `m1-smp/platform-patches/0004`: barriers, and a re-check after marking the source hard-disabled |

### Open, most serious first

| Id | Sev | Where | What breaks | Proposed fix |
|---|---|---|---|---|
| **W2** reported | high | `src/linux_work.c:214-269`, `:344`, `:469-506`; `callout.cpp:231-237` | `mod_delayed_work` while the delayed work is being picked up: (a) the re-armed timer is cancelled by the worker's drain and the work runs now (the GPU scheduler's job timeout is lost); (b) with delay 0 the work is enqueued twice, the state goes IDLE while it runs, and a later `cancel_delayed_work_sync` returns at once. | the worker must wait for "running" without removing a pending timer, and re-enqueue instead of running if the state is TIMER |
| **X2** reported | medium-high | `src/linux_rcu.c:184-258` | RCU reader counts are per kext, but RCU-freed fences cross kexts: a PDAgx reader in `dma_fence_get_rcu_safe` holds a display fence that PDDcp frees after a grace period of its own domain, which has no readers. | one RCU domain for all copies (export `linux_rcu_*` from one kext) |
| **X4** reported | high under load | `src/linux_lock.c:92-118` | `ww_mutex` deadlock detection sees two-thread cycles of one kext only; there is no wound/wait stamp. Three clients locking overlapping reservation sets in different orders all wait for ever. X1's fix does not change this. | a stamp in `ww_acquire_ctx`, wait-die |
| **P8** checked | high | `src/linux_interrupt.c:194-209` | `disable_irq`/`enable_irq` return early for platform interrupts. `drivers/gpu/drm/apple/dcp.c:4081,4135,5043` disable the HDMI hot-plug interrupt and tear state down; the handler can still run. The RTKit mailbox is not affected (`m1-gpu-rtkit` has its own, which honours the depth and drains the work loop). | a seam `pdos_intr_disable/enable(cookie)` (event source disable/enable) plus the wait of P7 |
| **W3** reported | medium | `m1-drm72/patches/puredarwin/0001` (`disable_work_sync` = `cancel_work_sync`), `m1-dcp/include/pd_dcp_compat.h:172-179` | Work can be queued again after "disable": the scheduler's `drm_sched_fini` races a fence callback that queues `work_free_job` on a destroyed queue (every client exit tears a scheduler down); DCP unbind races hot-plug work. | a disabled state in the work state machine that `queue_work` refuses |
| **D5** reported | medium | `drm/src/pd_dma_buf_poll.c:43-64` | The deferred `fput` work lives inside the dma-buf it may free: the work is re-queued while running and the executor touches it after the buffer is gone; or the count is published before `schedule_work` runs on the freed object. | one small `{work, file}` allocation per deferred `fput` |
| **P10** reported | medium | `pd/pd_haiku_kernel.cpp:442-459` against `:227-240` | `wait_for_thread` clears a thread slot without the lock after the slot may have been recycled for a new thread with the same `thread_t`: that thread's `current` changes under it (a recursive lock deadlocks on itself), or `destroy_workqueue` sleeps for ever. | clear under the lock and only if the slot is still the waited thread's |
| **W7** reported | medium | `pd/pd_haiku_kernel.cpp:486-525` | Haiku semaphore ids are reused at once: a worker of a destroyed queue acquires the semaphore of a queue created meanwhile; `destroy_workqueue` or `free_irq` (PCI) hangs. | a generation in the id |
| **W8** reported | medium | `src/linux_work.c:745-769`, `src/linux_shrinker.c:123-130` | `system_wq`, `_unbound`, `_highpri` share four threads whatever the CPU count: four items waiting on a fifth of the same queue deadlock. The memory-pressure pass holds the shrinker lock across every shrinker; unregistering one while holding a lock the pass wants is an ABBA deadlock (asahi registers a shrinker per VM). | pool size from the real CPU count; drop the lock around each shrinker |
| **P13** checked | medium, not on the M1 path | `pd/fbsd-extra/pd_lkpi_pre.h:128-140`, `xnu/pd_fbsd_os.cpp:1352-1362` | Per-CPU data is one shared slot and `preempt_disable` does nothing. `drivers/gpu/drm/asahi/g17/sync_scope.c:154-198` relies on both; the G13 (M1) path does not use it. | real per-CPU storage before any G17 work |
| **P11** reported | perf | `pd/pd_haiku_kernel.cpp:206-226`, `:679-685` | Every lock acquire and every `current` takes one global spin lock with interrupts off and scans a table that only grows: lock convoys on 8 CPUs. | the raw `current_thread()` as owner token; a lock-free `current` |
| **H2** reported | medium | `m1-native-input/after/.../IOHIDSystem/PDHIDEventQueue.cpp:225`, worker `:462-491` | The text console's key drain is a thread call without `THREAD_CALL_OPTIONS_ONCE`: two CPUs drain at once and keystrokes reach the tty out of order. The Wayland session's input path is not this one. | `thread_call_allocate_with_options(..., THREAD_CALL_OPTIONS_ONCE)` |
| **D6** reported | low | `drm/src/pd_drm_cdev.c:134-137` | `read()` sets or clears `O_NONBLOCK` in the shared file: a blocking and a non-blocking reader of one fd get each other's mode. | pass the flag per call |
| **D7** reported | low | `drm/src/pd_lfile_lkpi.c:121` | The "which kext made this file" tag is set at the first `fd_install`, not at creation: the other copy can install first and ioctls then fail with EFAULT. | set it where the file is made |
| **D8** reported | low | `m1-drm72/patches/drm-kmod/0009` lines 52-54 | Two shmem bit-fields are written after the reservation lock is dropped. | write them before the unlock |
| **W9** reported | low | `pd/lkpi/pd_linux_page.c:142-154` | Two concurrent `virt_to_page` of one heap page insert two `struct page`s. | re-check under the page lock before inserting |
| **P14** reported | low | `pd/pd_pci.c:428-440`, `:478-485`; `pd/lkpi/pd_linux_platform.c:354-363` | Interrupt table slots are claimed without a lock (covered by `Giant` during probe only). | claim under a mutex |
| **P12** reported | low, no users | `include/linux/kref.h:79-90`, `refcount.h:67-76` | `kref_put_lock`/`refcount_dec_and_lock_irqsave` reach zero before taking the lock. | the Linux sequence |
| **P15** reported | low | `src/linux_rcu.c:510-544` | Every SRCU domain is the same one: `synchronize_srcu(B)` inside `srcu_read_lock(A)` deadlocks. | per-domain counters |
| **R1** reported | low | `m1-gpu-rtkit/src/PDRtkitPlatform.cpp:603-633` | `pd_rtkit_free_irq` holds its lock across the work loop gate (deadlock with a handler of another line); probe error path only. | remove the event source outside the lock |
| **R2** reported | low | `m1-dcp/kext/pd_dcp_backlight.c:54-121` | The backlight sysctl uses the device without a reference; unbind only. | unregister the sysctl first |
| **H3** reported | low, latent | IO80211Family `compat/pd80211_compat.c:181-211`, `:265-284` | `timeout_del`/`timeout_del_barrier` are not barriers: the worker has dequeued a timeout and waits for the big lock while detach frees the softc. Station mode arms only one timeout, which `bwfm.c:466-469` handles. | a running flag and a real barrier |
| **H4-H12** reported | low | `m1-apcie-host/PDApcieT8103.cpp:356,440-501,562,261-277`; `PDAppleSMC.cpp:771`; `pd_dart_map.cpp:616-634`; `m1-bwfm/PDBwfm.cpp:358,1018`; `PDHIDEventQueue.cpp:345`; `pd80211_compat.c:38-46` | `portDown` check-then-use in the config accessors (only with `bwfm_off=2`); MSI allocator unlocked (one client today); RID2SID programmed after the child nubs are published; the SMC mailbox shared with other kexts without a common lock (only with `m1-cpufreq`); DART error register read and cleared unlocked (diagnostic); a reversed lock order that cannot run today; `bringUpFailed` outside the lifecycle lock; mouse index not atomic; lazy init of the 802.11 lock (two controllers only). | each a lock or an atomic; see the pass notes in the report |

Not SMP, found on the way (each fatal if reached; no user found in the GPU or display trees):
`dma_pool` hands out an 8-byte block used as a 32-byte object (`pd/lkpi/pd_lkpi_uma.c:40-64`,
`src/linux_pci.c:2387-2491`); `kthread_run` dereferences a thread id as a pointer
(`haiku/freebsd_network/kthread.cpp:29-30`, `src/linux_kthread.c:132-136`);
`schedule_timeout()` in `TASK_RUNNING` sleeps uninterruptibly for the whole timeout
(`src/linux_schedule.c:382-385`).

### Read and found sound

- **Wi-Fi (`m1-bwfm`)**: one recursive IOKit lock (`pd80211_compat.c:14`) covers the interrupt
  action, the poll timer, the timeout/task thread, output, every ioctl and the lifecycle paths;
  `splnet` is that lock, not a no-op. Every `tsleep` tests its condition and every `wakeup` is
  issued with the lock held, and the sleep registers the wait before unlocking: no lost wakeup.
  Rings, packet-id tables and flow rings are only touched under it. Nothing runs at primary
  interrupt level.
- **DART for the display (`m1-dcp-dart`)**: one lock serialises table walks, map, unmap, the IOVA
  allocator and the multi-register invalidate; intermediate tables are never freed.
- **RTKit and mailbox (`m1-gpu-rtkit`)**: send and receive use separate locks and FIFOs; the
  interrupt line's disable depth is honoured under a lock and drained through the work loop gate.
- **`/dev/dri` file lifetime**: XNU does not run `d_close` while an fd operation is in flight, so
  a cookie cannot be freed under an ioctl, select or mmap; the ioctl argument window is per thread.
- **sync files**: fence callbacks are added, removed and run under the fence lock; the XNU-side
  select record of Linux files (`pd_lfile`) is locked correctly.
- **GEM mmap through the node and dma-buf descriptor mmap**: object and page references are held
  by the mapping; release only schedules work.
- **PDAppleAIC**: mask set/clear are write-one registers, the event register is banked per CPU,
  IPIs and timers do not go through it; CPU 0, the only target, cannot be taken offline.
- **Keyboard and trackpad transport (`PDM1SPIHID`)**: one work loop, fully polled.
- **Rust side (PDAgx)**: `SpinLock`, `Mutex`, `CondVar`, `Arc`, `Completion`, RCU guard, xarray and
  workqueue enqueue all resolve to primitives that hold on SMP within one kext. Nothing on the
  G13 path registers an interrupt or uses per-CPU data.

### Not covered

The upstream logic of the DRM core, the scheduler, gpuvm, the asahi Rust driver and the DCP driver
was assumed correct given working primitives; only the lab's changes to them were read.
net80211, the Haiku network stack below the Wi-Fi glue, USB, and most of `PDApcieT8103Link.cpp`
were not read. Whether IOKit really starts the two PDDcp nubs concurrently, the code the compiler
emits for the atomics with `-mcpu=apple-a10`, and how the m1n1 hypervisor virtualises the AIC
under SMP were not determined.

## Fixes made

| Patch | Findings |
|---|---|
| `m1-rust-kext/lkpi/patches-layer/0028-dri-select-record-locked-clone-slots-released.patch` | D1, D2, D3 |
| `.../0029-dma-buf-fd-does-not-revive-a-dying-file.patch` | D4 |
| `.../0030-full-barriers-where-arm64-needs-them.patch` | P4, P5 |
| `.../0031-wake-up-bit-wakes-every-waiter.patch` | P9 |
| `.../0032-put-page-drops-its-reference-in-one-step.patch` | P6 |
| `.../0033-registry-locks-need-no-first-use-flag.patch` | P3 |
| `.../0034-layer-init-and-attach-lock-made-once.patch` | P1, P2 |
| `.../0035-synchronize-irq-waits-for-a-platform-handler.patch` | P7 |
| `.../0036-flush-workqueue-waits-for-queued-and-running-work.patch` | W1 |
| `.../0037-del-timer-sync-tests-the-running-callout-under-the-lock.patch` | W4 |
| `.../0038-ww-mutex-waiters-do-not-share-a-condvar-between-kexts.patch` | X1 |
| `.../0039-area-table-lookups-take-the-area-lock.patch` | W6 |
| `.../0040-wake-up-wakes-every-non-exclusive-waiter.patch` | W5 |
| `m1-drm72/patches/drm-kmod/0010-dma-fence-contexts-differ-between-kext-copies.patch` | X3 |
| `m1-smp/platform-patches/0004-PDAppleAIC-handler-barriers-for-disable-and-unregister.patch` (listed in `m1-smp/build-platform.nix`) | H1 |

The layer has no host tests for these files, so none were added.

## Stress and soak

`integrations/puredarwin/m1-smp-audit/` holds the stress programs and guest scripts written
against these findings; see its README for the cases, the boot commands and the 2-hour soak
script.

### Boots (M1, eight cores unless noted; M1 back in Linux after each)

| Boot | Collection | Stages | Result |
| --- | --- | --- | --- |
| la01 | `l5xps6z0…` (all fixes, first version of 0034) | storm,gl | console stops in early kext start, no panic text, no userland |
| la02 | `q773qrq7…` (same fixes on the committed tree) | storm,gl | same, console ends at `PDGpuDT: published under the device-tree root` |
| la04 | `0m4arjgr…` (same fixes), `cpus=1` | storm | kernel panic in `PDAgx::start` + 0x3fc, visible on one core |
| la05 | `69226378…` (0034 corrected) | storm,gl, 5 s per case | `LA_RESULT stages=3 failed=0 status=PASS`; all `DRMSTORM` cases `result=ok`, `unexpected=0`; all `LA_GL_LOOP fail=0`; `hw.activecpu: 8` |
| la06 | `69226378…` | wifi,session, 5 s scale | `LA_RESULT stages=3 failed=0 status=PASS`; three Wi-Fi off/on rounds `ping ok`; desktop session `WMGL3_DESKTOP_RC=0`; no panic |

The la01/la02/la04 failure was a mistake in patch 0034, not a race: `pdos_attach_iolock()`
was defined in a C++ file without `extern "C"` while its three callers declared it
`extern "C"`. The kexts link with undefined symbols allowed, so the callers were left with an
import nothing provides and the first nub attach jumped through it. On eight cores the
resulting panic produced no console output at all; on one core it printed. The definition now
carries `extern "C"` (PDAgx.map shows `_pdos_attach_iolock` defined). A kext link that
refused undefined `pd*` symbols would have caught this at build time; it is not done here.

These boots are short (5 s per case). They show the fixes do not break the paths they touch;
they do not show the races are gone. The 2-hour soak (`m1-smp-audit/tools/la_soak.sh`) has
not been run.
