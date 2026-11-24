// SPDX-License-Identifier: GPL-2.0
//! Minimal XNU runtime for Rust code in a SiliconForest kext: the allocator, a line logger and
//! the panic handler. Phase A only; phase B replaces this with the `kernel` crate on top of
//! the Linux compatibility layer, but the KPI choices below carry over.

#![no_std]

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::{c_char, c_void};
use core::fmt;
use core::sync::atomic::{AtomicU64, Ordering};

pub mod ffi {
    use super::*;

    unsafe extern "C" {
        // com.apple.kpi.iokit (config/IOKit.exports). vm_size_t is unsigned long on arm64.
        // IOMalloc/IOMallocZero: KHEAP_DEFAULT, the heap for allocations that may hold
        // pointers (IOMallocData is for pure data buffers, so it is not the general heap).
        pub fn IOMalloc(size: usize) -> *mut c_void;
        pub fn IOMallocZero(size: usize) -> *mut c_void;
        pub fn IOFree(address: *mut c_void, size: usize);
        pub fn IOMallocAligned(size: usize, alignment: usize) -> *mut c_void;
        pub fn IOFreeAligned(address: *mut c_void, size: usize);
        /// Variadic, called directly from Rust once to check the Darwin arm64 variadic ABI.
        pub fn IOLog(format: *const c_char, ...);

        // SFRustHello.cpp
        pub fn sf_rust_log(text: *const u8, len: usize);
        pub fn sf_rust_panic(text: *const u8, len: usize) -> !;
    }
}

/// Alignment `IOMalloc` is relied on for. IOLib.h promises none ("alignment may vary
/// depending on the kernel configuration"): the allocation comes from a kalloc zone
/// (16-byte aligned), but an IOTRACKING kernel puts its header in front. Pointer alignment
/// holds in every configuration; anything stricter goes to `IOMallocAligned`.
pub const IOMALLOC_ALIGN: usize = 8;

static ALLOCS: AtomicU64 = AtomicU64::new(0);
static FREES: AtomicU64 = AtomicU64::new(0);
static ALIGNED_ALLOCS: AtomicU64 = AtomicU64::new(0);
static LIVE_BYTES: AtomicU64 = AtomicU64::new(0);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AllocStats {
    pub allocs: u64,
    pub frees: u64,
    pub aligned_allocs: u64,
    pub live_bytes: u64,
}

pub fn alloc_stats() -> AllocStats {
    AllocStats {
        allocs: ALLOCS.load(Ordering::Relaxed),
        frees: FREES.load(Ordering::Relaxed),
        aligned_allocs: ALIGNED_ALLOCS.load(Ordering::Relaxed),
        live_bytes: LIVE_BYTES.load(Ordering::Relaxed),
    }
}

/// `GlobalAlloc` over IOKit's allocator. Both families take the size again on free, which
/// `GlobalAlloc::dealloc` supplies. They may block (Z_WAITOK): not for interrupt context.
pub struct KernelAllocator;

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let p = if layout.align() <= IOMALLOC_ALIGN {
            unsafe { ffi::IOMalloc(layout.size()) }
        } else {
            ALIGNED_ALLOCS.fetch_add(1, Ordering::Relaxed);
            unsafe { ffi::IOMallocAligned(layout.size(), layout.align()) }
        };
        if !p.is_null() {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
            LIVE_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
        }
        p.cast()
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        if layout.align() <= IOMALLOC_ALIGN {
            let p = unsafe { ffi::IOMallocZero(layout.size()) };
            if !p.is_null() {
                ALLOCS.fetch_add(1, Ordering::Relaxed);
                LIVE_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
            }
            p.cast()
        } else {
            // IOMallocAligned has no zeroing variant in the KPI
            let p = unsafe { self.alloc(layout) };
            if !p.is_null() {
                unsafe { core::ptr::write_bytes(p, 0, layout.size()) };
            }
            p
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        FREES.fetch_add(1, Ordering::Relaxed);
        LIVE_BYTES.fetch_sub(layout.size() as u64, Ordering::Relaxed);
        if layout.align() <= IOMALLOC_ALIGN {
            unsafe { ffi::IOFree(ptr.cast(), layout.size()) }
        } else {
            unsafe { ffi::IOFreeAligned(ptr.cast(), layout.size()) }
        }
    }
    // realloc: the default (alloc, copy, dealloc). The IOKit KPI has no realloc.
}

/// One log line, formatted on the stack and truncated to fit. No allocation.
pub struct LineBuf {
    buf: [u8; LineBuf::CAPACITY],
    len: usize,
}

impl LineBuf {
    pub const CAPACITY: usize = 200;

    pub const fn new() -> Self {
        LineBuf { buf: [0; Self::CAPACITY], len: 0 }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }

    /// Hands the line to the C side, which prints it with its `SFRustHello: ` prefix.
    pub fn emit(&self) {
        unsafe { ffi::sf_rust_log(self.buf.as_ptr(), self.len) }
    }
}

impl fmt::Write for LineBuf {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let room = Self::CAPACITY - self.len;
        let n = if s.len() < room { s.len() } else { room };
        self.buf[self.len..self.len + n].copy_from_slice(&s.as_bytes()[..n]);
        self.len += n;
        Ok(())
    }
}

#[macro_export]
macro_rules! klog {
    ($($arg:tt)*) => {{
        let mut line = $crate::LineBuf::new();
        let _ = ::core::fmt::Write::write_fmt(&mut line, format_args!($($arg)*));
        line.emit();
    }};
}

/// Rust panics end in the kernel's `panic()`, through the C shim that owns the variadic call.
/// The message carries the location, e.g. `panicked at sfrusthello/lib.rs:123:9: ...`.
#[panic_handler]
fn panic(info: &core::panic::PanicInfo<'_>) -> ! {
    let mut line = LineBuf::new();
    let _ = fmt::Write::write_fmt(&mut line, format_args!("{}", info));
    unsafe { ffi::sf_rust_panic(line.buf.as_ptr(), line.len) }
}
