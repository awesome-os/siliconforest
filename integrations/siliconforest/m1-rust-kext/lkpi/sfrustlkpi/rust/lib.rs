// SPDX-License-Identifier: GPL-2.0
//! SFRustLKPI: the Rust-for-Linux `kernel` crate on SiliconForest's LinuxKPI, exercised from a
//! kext. Everything here goes through `kernel::` (no direct `bindings::` calls except to read
//! errno constants), so each line it logs is evidence about one abstraction.
//!
//! The C++ side (kext/SFRustLKPI.cpp) brings the LinuxKPI runtime up and calls
//! `sf_rust_lkpi_run()`. Every wait has a timeout: a missing wakeup fails the test instead of
//! hanging the thread that started the kext.
//!
//! Phase C adds the checks of the layer fixes (lkpi/patches-layer): page size, aligned and
//! non-blocking allocation, lock lifetime, and a register read through `kernel::io`.

#![no_std]

use core::sync::atomic::{AtomicU32, Ordering};

use kernel::{
    alloc::AllocError,
    bindings,
    io::{Io, Mmio, MmioRaw, Region},
    page::{PAGE_SHIFT, PAGE_SIZE},
    error::to_result,
    new_condvar,
    new_mutex,
    new_spinlock,
    prelude::*,
    sync::{
        atomic::{ordering, Atomic},
        Arc,
        Completion,
        CondVar,
        CondVarTimeoutResult,
        Mutex,
        SpinLock, //
    },
    time::{delay::fsleep, msecs_to_jiffies, Delta, Instant, Monotonic},
    workqueue::{self, impl_has_work, new_work, Work, WorkItem},
};

/// What `pr_info!` and friends put in front of every line (`module!` normally defines it).
const __LOG_PREFIX: &[u8] = b"SFRustLKPI\0";

/// How long any single wait may take before the test gives up on it.
const WAIT_MS: u32 = 5000;

static FAILURES: AtomicU32 = AtomicU32::new(0);

// The xnu seam's counters (fbsdkpi/sf_fbsd_os.h, patches-layer/0002). They are the layer's
// own, not Linux API, so they are declared here rather than taken from `bindings`.
unsafe extern "C" {
    fn sfos_page_size() -> usize;
    fn sfos_malloc_nowait_count() -> u64;
    fn sfos_malloc_live() -> i64;
    fn sfos_lock_allocated() -> i64;
    fn sfos_lock_setups() -> u64;
}

/// The AIC (interrupt controller) of the T8103, the address `SFAppleAIC` logs, and its
/// read-only information register: the low 16 bits are the number of interrupt lines.
const AIC_BASE: bindings::phys_addr_t = 0x2_3b10_0000;
const AIC_INFO: usize = 0x4;

fn check(what: &str, ok: bool) {
    if !ok {
        FAILURES.fetch_add(1, Ordering::Relaxed);
        pr_err!("rust: CHECK FAILED: {}\n", what);
    }
}

// --- alloc ---------------------------------------------------------------------------------

fn test_alloc() -> Result {
    let boxed = KBox::new(0x1234_5678_9abc_def0_u64, GFP_KERNEL)?;

    let mut v: KVec<u32> = KVec::new();
    for i in 0..100u32 {
        v.push(i, GFP_KERNEL)?;
    }
    let sum: u32 = v.iter().sum();

    // 80000 bytes: past a page, LinuxKPI serves it from contigmalloc
    let mut big: KVVec<u64> = KVVec::with_capacity(10_000, GFP_KERNEL)?;
    for i in 0..10_000u64 {
        big.push(i, GFP_KERNEL)?;
    }
    let big_sum: u64 = big.iter().sum();

    let zeroed = KBox::<[u8; 64]>::new_uninit(GFP_KERNEL | __GFP_ZERO)?;
    // SAFETY: allocated with __GFP_ZERO, and all-zero is a valid `[u8; 64]`.
    let zeroed = unsafe { zeroed.assume_init() };
    let all_zero = zeroed.iter().all(|&b| b == 0);

    pr_info!(
        "rust: alloc: KBox={:#x} KVec len={} sum={} KVVec len={} sum={} zeroed={}\n",
        *boxed,
        v.len(),
        sum,
        big.len(),
        big_sum,
        all_zero
    );
    check("alloc values", *boxed == 0x1234_5678_9abc_def0 && sum == 4950 && big_sum == 49_995_000 && all_zero);

    Ok(())
}

#[repr(C, align(4096))]
struct Aligned4k([u8; 4096]);

#[repr(C, align(16384))]
struct Aligned16k([u8; 16384]);

/// Alignment up to the page size (phase B refused anything above 8), and the krealloc rules
/// across the page boundary, where kmalloc changes from the heap to contiguous memory.
fn test_alloc_aligned() -> Result {
    let wide = KBox::new(1u128 << 100, GFP_KERNEL)?;
    let wide_ok = (&*wide as *const u128 as usize) % 16 == 0 && *wide == 1u128 << 100;

    // new_uninit: no 16 KiB temporary on the kernel stack
    let a4k = KBox::<Aligned4k>::new_uninit(GFP_KERNEL)?;
    let a4k_ok = (a4k.as_ptr() as usize) % 4096 == 0;
    let a16k = KBox::<Aligned16k>::new_uninit(GFP_KERNEL | __GFP_ZERO)?;
    let a16k_addr = a16k.as_ptr() as usize;
    // SAFETY: allocated with __GFP_ZERO, and all-zero is a valid `Aligned16k`.
    let a16k = unsafe { a16k.assume_init() };
    let a16k_ok = a16k_addr % 16384 == 0 && a16k.0.iter().all(|&b| b == 0);
    pr_info!(
        "rust: alloc: aligned: KBox<u128> %16={} 4096-aligned={} 16384-aligned and zeroed={}\n",
        wide_ok,
        a4k_ok,
        a16k_ok
    );
    check("aligned allocations", wide_ok && a4k_ok && a16k_ok);

    // 100000 pushes: the buffer is reallocated from the heap into contiguous memory at
    // 16 KiB and then between contiguous blocks; every byte must survive each move
    let mut grown: KVec<u8> = KVec::new();
    for i in 0..100_000u32 {
        grown.push((i % 251) as u8, GFP_KERNEL)?;
    }
    let intact = grown.iter().enumerate().all(|(i, &b)| b == (i % 251) as u8);
    pr_info!("rust: alloc: KVec<u8> grown to {} bytes across the page boundary, contents intact={}\n", grown.len(), intact);
    check("krealloc keeps contents", grown.len() == 100_000 && intact);
    Ok(())
}

/// GFP_ATOMIC and GFP_NOWAIT must not block: taken while a SpinLock is held. The seam counts
/// the allocations that went down its non-blocking path.
fn test_alloc_nowait() -> Result {
    let lock = KBox::pin_init(new_spinlock!(0u32, "SFRustLKPI::nowait"), GFP_KERNEL)?;
    // SAFETY: FFI call without preconditions.
    let before = unsafe { sfos_malloc_nowait_count() };
    let (atomic, nowait) = {
        let mut guard = lock.lock();
        *guard += 1;
        let atomic = KBox::new(0x5a5a_u64, GFP_ATOMIC);
        let nowait = KVec::<u32>::with_capacity(64, GFP_NOWAIT);
        (atomic, nowait)
    };
    // SAFETY: FFI call without preconditions.
    let counted = unsafe { sfos_malloc_nowait_count() } - before;
    let atomic_ok = matches!(&atomic, Ok(b) if **b == 0x5a5a);
    let nowait_ok = matches!(&nowait, Ok(v) if v.capacity() >= 64);
    let both_counted = counted >= 2;
    pr_info!(
        "rust: alloc: under a SpinLock: GFP_ATOMIC KBox ok={} GFP_NOWAIT KVec ok={} taken without blocking={}\n",
        atomic_ok,
        nowait_ok,
        both_counted
    );
    check("non-blocking allocations", atomic_ok && nowait_ok && both_counted);

    // what cannot be had without blocking fails; it does not sleep
    let huge: Result<KVec<u8>, AllocError> = KVec::with_capacity(1 << 20, GFP_NOWAIT);
    pr_info!(
        "rust: alloc: GFP_NOWAIT 1 MiB = {} (above what can be served without blocking)\n",
        if huge.is_ok() { "Ok" } else { "Err(AllocError)" }
    );
    check("oversized non-blocking allocation fails", huge.is_err());
    Ok(())
}

// --- page size -----------------------------------------------------------------------------

fn test_page_size() {
    // SAFETY: FFI call without preconditions.
    let xnu = unsafe { sfos_page_size() };
    pr_info!("rust: page: PAGE_SIZE={} PAGE_SHIFT={} xnu page size={}\n", PAGE_SIZE, PAGE_SHIFT, xnu);
    check("page size", PAGE_SIZE == 16384 && PAGE_SHIFT == 14 && xnu == PAGE_SIZE);
}

// --- lock lifetime -------------------------------------------------------------------------

const LOCK_CYCLES: u32 = 10_000;

/// Creating, using and dropping locks must leave nothing behind: no xnu lock allocated, no
/// heap byte kept. (Before patches-layer/0003 every lock that had been locked once leaked
/// one xnu lock when dropped.)
fn test_lock_lifetime() -> Result {
    // SAFETY: FFI calls without preconditions.
    let (locks0, live0, setups0) = unsafe { (sfos_lock_allocated(), sfos_malloc_live(), sfos_lock_setups()) };
    let mut sum = 0u64;
    for i in 0..LOCK_CYCLES {
        let m = KBox::pin_init(new_mutex!(u64::from(i), "SFRustLKPI::cycle_mutex"), GFP_KERNEL)?;
        sum += *m.lock();
        let s = KBox::pin_init(new_spinlock!(1u64, "SFRustLKPI::cycle_spin"), GFP_KERNEL)?;
        sum += *s.lock();
    }
    // SAFETY: FFI calls without preconditions.
    let (locks1, live1, setups1) = unsafe { (sfos_lock_allocated(), sfos_malloc_live(), sfos_lock_setups()) };
    pr_info!(
        "rust: locks: {} Mutex and {} SpinLock made, locked, dropped: xnu locks allocated {:+}, heap bytes {:+}, locks set up in place +{}\n",
        LOCK_CYCLES,
        LOCK_CYCLES,
        locks1 - locks0,
        live1 - live0,
        setups1 - setups0
    );
    let expect = u64::from(LOCK_CYCLES) * u64::from(LOCK_CYCLES - 1) / 2 + u64::from(LOCK_CYCLES);
    check("lock values", sum == expect);
    check("no xnu lock or heap byte left behind", locks1 == locks0 && live1 <= live0);
    check("each lock was set up", setups1 - setups0 >= 2 * u64::from(LOCK_CYCLES));
    Ok(())
}

// --- register access -----------------------------------------------------------------------

/// Maps one page of the interrupt controller (non-posted device memory) and reads its
/// information register through `kernel::io::Mmio`. Nothing is written.
fn test_mmio() -> Result {
    // SAFETY: maps device registers that are always present on this SoC; unmapped below.
    let va = unsafe { bindings::ioremap_np(AIC_BASE, PAGE_SIZE) };
    if va.is_null() {
        pr_err!("rust: io: ioremap_np({:#x}) failed\n", AIC_BASE);
        return Err(ENOMEM);
    }
    let raw = MmioRaw::<Region<0x10>>::new_region(va as usize, PAGE_SIZE)?;
    // SAFETY: `va` is a live mapping of `PAGE_SIZE` bytes until the iounmap below.
    let info = unsafe { Mmio::from_raw(raw) }.read32(AIC_INFO);
    // SAFETY: `va` came from ioremap_np above and is not used afterwards.
    unsafe { bindings::iounmap(va) };
    let lines = info & 0xffff;
    let plausible = (64..=4096).contains(&lines);
    pr_info!("rust: io: AIC info register {:#010x}: {} interrupt lines, plausible={}\n", info, lines, plausible);
    check("AIC info register", plausible);
    Ok(())
}

// --- locks, Arc, atomics -------------------------------------------------------------------

#[pin_data]
struct Locked {
    #[pin]
    mutex: Mutex<u64>,
    #[pin]
    spin: SpinLock<u32>,
}

struct CountsDrop(u32);

static DROPS: AtomicU32 = AtomicU32::new(0);

impl Drop for CountsDrop {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::Relaxed);
    }
}

fn test_sync() -> Result {
    let locked = Arc::pin_init(
        pin_init!(Locked {
            mutex <- new_mutex!(1, "SFRustLKPI::mutex"),
            spin <- new_spinlock!(5, "SFRustLKPI::spin"),
        }),
        GFP_KERNEL,
    )?;
    {
        let mut g = locked.mutex.lock();
        *g += 2;
    }
    {
        let mut g = locked.spin.lock();
        *g += 2;
    }
    let m = *locked.mutex.lock();
    let s = *locked.spin.lock();
    pr_info!("rust: sync: Mutex<u64> 1+2={} SpinLock<u32> 5+2={}\n", m, s);
    check("lock values", m == 3 && s == 7);

    let a = Arc::new(CountsDrop(5), GFP_KERNEL)?;
    let b = a.clone();
    let same = Arc::ptr_eq(&a, &b);
    let value = b.0;
    drop(a);
    let after_first = DROPS.load(Ordering::Relaxed);
    drop(b);
    let after_second = DROPS.load(Ordering::Relaxed);
    pr_info!(
        "rust: sync: Arc value={} ptr_eq={} drops after first/second drop={}/{}\n",
        value,
        same,
        after_first,
        after_second
    );
    check("arc", value == 5 && same && after_first == 0 && after_second == 1);

    let counter = Atomic::<i32>::new(40);
    let old = counter.fetch_add(2, ordering::Full);
    let swapped = counter.xchg(7, ordering::Full);
    let wide = Atomic::<u64>::new(u64::MAX - 1);
    wide.add(1, ordering::Relaxed);
    pr_info!(
        "rust: sync: Atomic<i32> fetch_add old={} xchg old={} now={} Atomic<u64>={:#x}\n",
        old,
        swapped,
        counter.load(ordering::Relaxed),
        wide.load(ordering::Relaxed)
    );
    check("atomics", old == 40 && swapped == 42 && counter.load(ordering::Relaxed) == 7 && wide.load(ordering::Relaxed) == u64::MAX);
    Ok(())
}

// --- errors --------------------------------------------------------------------------------

fn test_error() {
    let from_c = Error::from_errno(-(bindings::EINVAL as i32));
    let nomem = to_result(-(bindings::ENOMEM as i32));
    let fine = to_result(0);
    let is_einval = from_c == EINVAL;
    // XNU numbering reaches Rust through the bindings: EAGAIN is 35 here, 11 on Linux
    pr_info!(
        "rust: error: from_errno(-22)==EINVAL:{} to_errno={} to_result(-12)={:?} to_result(0)={:?} EAGAIN={}\n",
        is_einval,
        from_c.to_errno(),
        nomem,
        fine,
        EAGAIN.to_errno()
    );
    check("errors", is_einval && nomem == Err(ENOMEM) && fine.is_ok());
    let propagated: Result<u32> = (|| {
        to_result(-(bindings::EBUSY as i32))?;
        Ok(1)
    })();
    check("? propagates", propagated == Err(EBUSY));
}

// --- time ----------------------------------------------------------------------------------

fn test_time() {
    let start = Instant::<Monotonic>::now();
    fsleep(Delta::from_millis(20));
    let ms = start.elapsed().as_millis();
    let in_range = (15..2000).contains(&ms);
    pr_info!("rust: time: Monotonic elapsed over fsleep(20 ms) within 15..2000 ms: {}\n", in_range);
    if !in_range {
        pr_err!("rust: time: measured {} ms\n", ms);
    }
    check("monotonic time", in_range);
}

// --- workqueue, Completion, CondVar --------------------------------------------------------

#[pin_data]
struct Job {
    value: u32,
    result: Atomic<u32>,
    #[pin]
    done: Completion,
    #[pin]
    flag: Mutex<bool>,
    #[pin]
    flag_changed: CondVar,
    #[pin]
    work: Work<Job>,
}

impl_has_work! {
    impl HasWork<Self> for Job { self.work }
}

impl WorkItem for Job {
    type Pointer = Arc<Job>;

    fn run(this: Arc<Job>) {
        pr_info!("rust: workqueue: work item running, value={}\n", this.value);
        this.result.store(this.value + 1, ordering::Release);
        this.done.complete_all();
        // let the submitter get to its CondVar wait, then wake it
        fsleep(Delta::from_millis(50));
        *this.flag.lock() = true;
        this.flag_changed.notify_all();
    }
}

fn test_workqueue() -> Result {
    let job = Arc::pin_init(
        pin_init!(Job {
            value: 42,
            result: Atomic::new(0),
            done <- Completion::new(),
            flag <- new_mutex!(false, "SFRustLKPI::flag"),
            flag_changed <- new_condvar!("SFRustLKPI::flag_changed"),
            work <- new_work!("SFRustLKPI::work"),
        }),
        GFP_KERNEL,
    )?;

    let queued = workqueue::system().enqueue(job.clone()).is_ok();
    let completed = job.done.wait_for_completion_timeout(msecs_to_jiffies(WAIT_MS));
    let result = job.result.load(ordering::Acquire);
    pr_info!(
        "rust: workqueue: queued={} Completion within {} ms={} result={}\n",
        queued,
        WAIT_MS,
        completed,
        result
    );
    check("work item completed", queued && completed && result == 43);

    let mut woken = false;
    let mut timed_out = false;
    {
        let mut guard = job.flag.lock();
        while !*guard {
            match job.flag_changed.wait_timeout(&mut guard, msecs_to_jiffies(WAIT_MS)) {
                CondVarTimeoutResult::Timeout => {
                    timed_out = true;
                    break;
                }
                _ => woken = true,
            }
        }
        woken = woken || *guard;
    }
    pr_info!("rust: sync: CondVar wait ended: flag set={} timed out={}\n", woken && !timed_out, timed_out);
    check("condvar wakeup", woken && !timed_out);
    Ok(())
}

fn run_step(name: &str, r: Result) {
    if let Err(e) = r {
        pr_err!("rust: {} returned {:?}\n", name, e);
        FAILURES.fetch_add(1, Ordering::Relaxed);
    }
}

/// Runs every test once and returns the number of failed checks.
#[unsafe(no_mangle)]
pub extern "C" fn sf_rust_lkpi_run() -> u32 {
    pr_info!("rust: pr_info! through kernel::print: {} {:#x} {}\n", 42, 42, "hello");
    run_step("alloc", test_alloc());
    run_step("sync", test_sync());
    test_error();
    test_time();
    run_step("workqueue", test_workqueue());
    // phase C
    test_page_size();
    run_step("aligned alloc", test_alloc_aligned());
    run_step("lock lifetime", test_lock_lifetime());
    run_step("non-blocking alloc", test_alloc_nowait());
    run_step("io", test_mmio());
    let failures = FAILURES.load(Ordering::Relaxed);
    pr_info!("rust: done, {} failed checks\n", failures);
    failures
}
