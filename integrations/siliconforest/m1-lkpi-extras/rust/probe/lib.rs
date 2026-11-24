// SPDX-License-Identifier: GPL-2.0
//! Compile and link probe for m1-lkpi-extras (never loaded, never run).
//!
//! Each function below uses one of the kernel APIs in this directory's scope the way the
//! Asahi driver's G13 path uses it; the comment names the driver file and line it mirrors
//! (linux-aurora 6a08a08e, drivers/gpu/drm/asahi/). The crate is built for the kext target
//! against the kernel crate with this directory's additions, then linked with the C helpers
//! and sf_xarray.o into one relocatable object, and the recipe fails if a `rust_helper_*` or
//! `pd_*` symbol is left undefined. What that proves: the names, types and signatures exist
//! and agree between Rust and C, and `module!` produces valid Mach-O. Nothing more.

#![no_std]

use core::sync::atomic::{AtomicPtr, Ordering};

use kernel::{
    bindings, c_str,
    mm::{Mm, RssCounter},
    page::{Page, PAGE_SIZE},
    prelude::*,
    seq_file::SeqFile,
    str::CString,
    sync::{aref::ARef, Arc, Mutex},
    time::{delay::fsleep, Delta},
    uapi, xarray,
};

// asahi_core.rs:83: `module!` with the parameters the G13 path reads (the g15_* ones are
// the same four types).
module! {
    type: ProbeModule,
    name: "sfextras_probe",
    description: "m1-lkpi-extras compile probe",
    license: "Dual MIT/GPL",
    params: {
        debug_flags: u64 { default: 0, description: "asahi_core.rs:89", },
        compute_preempt: i32 { default: -1, description: "asahi_core.rs:94", },
        fault_control: u32 { default: 0xb, description: "asahi_core.rs:98", },
        initial_tvb_size: usize { default: 0x8, description: "asahi_core.rs:103", },
        robust_isolation: u32 { default: 0, description: "asahi_core.rs:108", },
        stats_export: u8 { default: 1, description: "asahi_core.rs:190", },
        mem_limit_pct: u32 { default: 67, description: "asahi_core.rs:195", },
        client_mem_limit_pct: u32 { default: 50, description: "asahi_core.rs:199", },
        oom_attribution: u8 { default: 1, description: "asahi_core.rs:203", },
    },
}

struct Vm {
    id: u32,
}

#[pin_data]
struct ProbeModule {
    // file.rs:264-268
    #[pin]
    vms: xarray::XArray<KBox<Vm>>,
    #[pin]
    queues: xarray::XArray<Arc<Mutex<KBox<u32>>>>,
}

// driver.rs:117, queue/mod.rs:265
const MODULE: Option<&'static kernel::ThisModule> = Some(&crate::THIS_MODULE);

impl kernel::InPlaceModule for ProbeModule {
    // asahi_core.rs:65
    fn init(_module: &'static kernel::ThisModule) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            // file.rs:329, 335
            vms <- xarray::XArray::<KBox<Vm>>::new(xarray::AllocKind::Alloc1),
            queues <- xarray::XArray::<Arc<Mutex<KBox<u32>>>>::new(xarray::AllocKind::Alloc1),
        })
    }
}

/// debug.rs:94, initdata.rs:787, queue/mod.rs:557, workqueue.rs:669, mmu.rs:3304,
/// channel.rs:644, memacct.rs:167-172
fn params() -> u64 {
    *module_parameters::debug_flags.value()
        ^ (*module_parameters::compute_preempt.value() as u64)
        ^ u64::from(*module_parameters::fault_control.value())
        ^ (*module_parameters::initial_tvb_size.value() as u64)
        ^ u64::from(*module_parameters::robust_isolation.value())
        ^ u64::from(*module_parameters::stats_export.value())
        ^ u64::from(*module_parameters::mem_limit_pct.value())
        ^ u64::from(*module_parameters::client_mem_limit_pct.value())
        ^ u64::from(*module_parameters::oom_attribution.value())
}

/// file.rs:576-636 (vm_create), 667 (vm_destroy), 706, 1125, 1322-1345 (queue_create)
fn xarray_use(m: Pin<&ProbeModule>) -> Result<u32> {
    // SAFETY: structural pinning of the two fields, as file.rs:355-368 does.
    let vms = unsafe { m.map_unchecked(|m| &m.vms) };
    // SAFETY: as above.
    let queues = unsafe { m.map_unchecked(|m| &m.queues) };

    let resv = vms.lock().reserve_limit(1..=u32::MAX, GFP_KERNEL)?;
    let id: u32 = resv.index().try_into()?;
    resv.fill(KBox::new(Vm { id }, GFP_KERNEL)?)?;

    let seen = {
        let guard = vms.lock();
        guard.get(id as usize).ok_or(ENOENT)?.id
    };
    let mut index = 0;
    while let Some((i, vm)) = vms.find(index, usize::MAX) {
        let _ = vm.borrow().id;
        index = i + 1;
    }

    let resv = queues.lock().reserve_limit(1..=u32::MAX, GFP_KERNEL)?;
    let queue_id: u32 = resv.index().try_into()?;
    resv.fill(Arc::pin_init(kernel::new_mutex!(KBox::new(queue_id, GFP_KERNEL)?), GFP_KERNEL)?)?;
    let _ = queues.remove(queue_id as usize);

    vms.remove(id as usize).ok_or(ENOENT)?;
    Ok(seen)
}

/// mmu/residency.rs:246-326
struct VmShrinker {
    raw: *mut bindings::shrinker,
    pages: usize,
}

impl VmShrinker {
    fn new() -> Result<KBox<Self>> {
        let mut owner = KBox::new(Self { raw: core::ptr::null_mut(), pages: 0 }, GFP_KERNEL)?;
        // SAFETY: The format is static and has no format arguments.
        let raw = unsafe { bindings::shrinker_alloc(0, c_str!("asahi-idle-vm").as_char_ptr()) };
        if raw.is_null() {
            return Err(ENOMEM);
        }
        owner.raw = raw;
        // SAFETY: Newly allocated/unregistered shrinker is exclusively owned.
        unsafe {
            (*raw).count_objects = Some(Self::count);
            (*raw).scan_objects = Some(Self::scan);
            (*raw).private_data = core::ptr::from_ref(&*owner).cast_mut().cast();
            (*raw).seeks = 2;
            (*raw).batch = 256;
            bindings::shrinker_register(raw);
        }
        Ok(owner)
    }

    unsafe extern "C" fn count(
        raw: *mut bindings::shrinker,
        _sc: *mut bindings::shrink_control,
    ) -> kernel::ffi::c_ulong {
        // SAFETY: The registration owns this stable allocation until callbacks drain.
        let owner = unsafe { &*((*raw).private_data as *const Self) };
        owner.pages as _
    }

    unsafe extern "C" fn scan(
        _raw: *mut bindings::shrinker,
        sc: *mut bindings::shrink_control,
    ) -> kernel::ffi::c_ulong {
        // SAFETY: The shrinker core supplies a live initialized control.
        unsafe { (*sc).nr_to_scan }
    }
}

impl Drop for VmShrinker {
    fn drop(&mut self) {
        // SAFETY: This owns the sole registration.
        unsafe { bindings::shrinker_free(self.raw) };
    }
}

/// memacct.rs:161-163 (RAM size), 322-324 and 358 (RSS attribution)
fn memacct_use() -> u64 {
    // SAFETY: `struct sysinfo` is plain integers, for which all zeroes is a valid value.
    let mut info: bindings::sysinfo = unsafe { core::mem::zeroed() };
    // SAFETY: `si_meminfo()` only fills in the structure it is given.
    unsafe { bindings::si_meminfo(&mut info) };
    let total = (info.totalram as u64).saturating_mul(u64::from(info.mem_unit));

    let mm: Option<ARef<Mm>> = kernel::current!().mm().map(|mm| ARef::from(&**mm));
    if let Some(mm) = &mm {
        mm.add_rss_counter(RssCounter::Shmem, 4);
        let _ = mm.rss_counter(RssCounter::Shmem);
        mm.add_rss_counter(RssCounter::Shmem, -4);
    }
    total
}

/// stats_raw.rs:411 (ktime_get), 451-471 (debugfs show callback over a seq file)
unsafe extern "C" fn show(m: *mut bindings::seq_file, _data: *mut core::ffi::c_void) -> core::ffi::c_int {
    // SAFETY: The DRM core passes a valid seq file to the show callback.
    let seq = unsafe { SeqFile::from_raw(m) };
    // SAFETY: The seq file was opened with the DRM entry as private data.
    let entry = unsafe { (*m).private.cast::<bindings::drm_debugfs_entry>() };
    // SAFETY: `entry->file.data` is the payload pointer the driver registered.
    let data = unsafe { (*entry).file.data };
    // SAFETY: `ktime_get()` may be called from any context.
    let now = unsafe { bindings::ktime_get() } as u64;
    kernel::seq_print!(seq, "now={} data={:p}\n", now, data);
    0
}

/// cleanup.rs:33-47 (own workqueue), 115 (Queue::from_raw)
static QUEUE: AtomicPtr<bindings::workqueue_struct> = AtomicPtr::new(core::ptr::null_mut());

fn workqueue_use() -> Result {
    // SAFETY: The name is NUL terminated and contains no format specifiers.
    let raw = unsafe {
        bindings::alloc_workqueue_noprof(
            c_str!("asahi-ts-cleanup").as_char_ptr(),
            bindings::wq_flags_WQ_UNBOUND | bindings::wq_flags_WQ_MEM_RECLAIM,
            0,
        )
    };
    if raw.is_null() {
        return Err(ENOMEM);
    }
    QUEUE.store(raw, Ordering::Release);
    // SAFETY: `raw` is a live workqueue.
    let _queue: &kernel::workqueue::Queue = unsafe { kernel::workqueue::Queue::from_raw(raw) };
    // SAFETY: This fresh queue has no work queued.
    unsafe { bindings::destroy_workqueue(raw) };
    Ok(())
}

/// file.rs:1313-1314 (capable), pgtable.rs:60-71 (dcache_line_size)
fn misc_use() -> Result<usize> {
    // SAFETY: capable() only reads the calling task's credentials.
    if uapi::drm_asahi_priority_DRM_ASAHI_PRIORITY_HIGH != 0
        && !unsafe { bindings::capable(bindings::CAP_SYS_NICE as _) }
    {
        return Err(EACCES);
    }
    let ctr = (bindings::SYS_CTR_EL0_Op0 << 19)
        | (bindings::SYS_CTR_EL0_Op1 << 16)
        | (bindings::SYS_CTR_EL0_CRn << 12)
        | (bindings::SYS_CTR_EL0_CRm << 8)
        | (bindings::SYS_CTR_EL0_Op2 << 5);
    // SAFETY: Read-only lookup of the arm64 sanitized feature register.
    let value = unsafe { bindings::read_sanitised_ftr_reg(ctr) };
    Ok(4 << ((value >> bindings::CTR_EL0_DminLine_SHIFT) & 0xf))
}

/// pgtable.rs:436-437, 831 (page table pages); alloc.rs:724 (CString); channel.rs:169 (fsleep)
fn core_use() -> Result<usize> {
    let page = Page::alloc_page(GFP_KERNEL | __GFP_ZERO)?;
    let phys = Page::into_phys(page);
    // SAFETY: `phys` came from `into_phys` just above.
    drop(unsafe { Page::from_phys(phys) });
    let name = CString::try_from_fmt(fmt!("{}", PAGE_SIZE))?;
    fsleep(Delta::from_millis(8));
    Ok(name.to_bytes().len())
}

/// The uAPI header (driver.rs:122-150 declares the ioctls from these; file.rs uses the
/// structs). Sizes are part of the ioctl numbers, so they are pinned here.
fn uapi_use() -> usize {
    const _: () = assert!(core::mem::size_of::<uapi::drm_asahi_gem_mmap_offset>() == 16);
    const _: () = assert!(core::mem::size_of::<uapi::drm_asahi_vm_create>() == 24);
    let args = uapi::drm_asahi_gem_mmap_offset::default();
    args.handle as usize + uapi::drm_asahi_ioctl_id_DRM_ASAHI_GEM_MMAP_OFFSET as usize
}

/// Every error code the G13 path names (17). Their values are xnu's here, not Linux's.
fn errnos() -> [Error; 17] {
    [
        EINVAL, EIO, EOVERFLOW, ENODEV, ENOENT, ENOMEM, EFAULT, EBUSY, ERESTARTSYS, ERANGE,
        ENOSPC, ETIMEDOUT, EAGAIN, EACCES, ENODATA, ECANCELED, E2BIG,
    ]
}

/// gem.rs:482, memacct.rs:381 and 545: KUnit suites. `#[kunit_tests]` puts the module under
/// `#[cfg(CONFIG_KUNIT = "y")]`, which is not set here, so they compile to nothing (and with
/// them the driver's only uses of `mm_alloc()` and `mmput()`, memacct.rs:519 and 540).
#[kernel::macros::kunit_tests(sfextras_probe_suite)]
mod tests {
    #[test]
    fn never_built() {
        // SAFETY: not compiled.
        let raw = unsafe { kernel::bindings::mm_alloc() };
        assert!(!raw.is_null());
    }
}

/// Keeps every probe reachable from an exported symbol.
#[no_mangle]
pub extern "C" fn sf_extras_probe(m: *const core::ffi::c_void) -> u64 {
    // SAFETY: never called; `m` would be the module instance.
    let m = unsafe { Pin::new_unchecked(&*m.cast::<ProbeModule>()) };
    let shrinker = VmShrinker::new();
    let mut sum = params() ^ memacct_use();
    sum ^= u64::from(xarray_use(m).unwrap_or(0));
    sum ^= misc_use().unwrap_or(0) as u64;
    sum ^= core_use().unwrap_or(0) as u64;
    sum ^= uapi_use() as u64;
    sum ^= errnos().iter().map(|e| e.to_errno() as u64).sum::<u64>();
    sum ^= workqueue_use().is_ok() as u64;
    sum ^= MODULE.is_some() as u64;
    sum ^= (show as usize) as u64;
    drop(shrinker);
    sum
}
