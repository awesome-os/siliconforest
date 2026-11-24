// SPDX-License-Identifier: GPL-2.0
//! SFRustHello: proves core, compiler_builtins and alloc on the real kernel.
//! The C view of this file is ../../kext/sf_rust_hello.h.

#![no_std]

extern crate alloc;

use alloc::alloc::{alloc_zeroed, dealloc, Layout};
use alloc::boxed::Box;
use alloc::collections::BTreeMap;
use alloc::vec::Vec;
use core::ffi::c_int;
use core::fmt::{self, Write};
use core::hint::black_box;
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicUsize, Ordering};

use sf_xnu::{alloc_stats, ffi, KernelAllocator, LineBuf};

#[global_allocator]
static GLOBAL: KernelAllocator = KernelAllocator;

// Statics with interior atomics: they live in the kext's __DATA and keep their value
// between calls.
static CALLS: AtomicU64 = AtomicU64::new(0);
static STATE: AtomicU32 = AtomicU32::new(0);
static FLAGS: AtomicUsize = AtomicUsize::new(0x1);
static BUSY: AtomicBool = AtomicBool::new(false);
static DROPS: AtomicU32 = AtomicU32::new(0);

#[repr(C)]
pub struct Args {
    n_hi: u64,
    n_lo: u64,
    d_hi: u64,
    d_lo: u64,
    seed: u64,
    index: u64,
}

#[repr(C)]
pub struct Results {
    q_hi: u64,
    q_lo: u64,
    r_hi: u64,
    r_lo: u64,
    mul_hi: u64,
    mul_lo: u64,
    calls: u64,
    live_bytes: u64,
    failures: u64,
}

/// Logs lines and keeps an FNV-1a 64 over them, so the returned value vouches for the text.
struct Report {
    hash: u64,
    failures: u64,
}

impl Report {
    fn new() -> Self {
        Report { hash: 0xcbf2_9ce4_8422_2325, failures: 0 }
    }

    fn mix(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.hash ^= b as u64;
            self.hash = self.hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }

    fn line(&mut self, args: fmt::Arguments<'_>) {
        let mut line = LineBuf::new();
        let _ = line.write_fmt(args);
        self.mix(line.as_bytes());
        line.emit();
    }

    fn check(&mut self, what: &str, ok: bool) {
        if !ok {
            self.failures += 1;
            self.line(format_args!("rust: CHECK FAILED: {}", what));
        }
    }
}

trait Shape {
    fn name(&self) -> &'static str;
    fn area(&self) -> u64;
}

struct Rect(u64, u64);
struct Square(u64);

impl Shape for Rect {
    fn name(&self) -> &'static str {
        "rect"
    }
    fn area(&self) -> u64 {
        self.0 * self.1
    }
}

impl Shape for Square {
    fn name(&self) -> &'static str {
        "square"
    }
    fn area(&self) -> u64 {
        self.0 * self.0
    }
}

impl Drop for Rect {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::Relaxed);
    }
}

impl Drop for Square {
    fn drop(&mut self) {
        DROPS.fetch_add(0x100, Ordering::Relaxed);
    }
}

#[repr(align(64))]
struct CacheLine([u8; 64]);

fn test_atomics(rep: &mut Report) -> u64 {
    let calls = CALLS.fetch_add(1, Ordering::SeqCst) + 1;
    let cas = STATE.compare_exchange(0, 0x5255_5354, Ordering::AcqRel, Ordering::Acquire);
    let flags = FLAGS.fetch_or(0x4, Ordering::Relaxed) | 0x4;
    let was_busy = BUSY.swap(true, Ordering::Acquire);
    BUSY.store(false, Ordering::Release);
    rep.line(format_args!(
        "rust: atomics: calls={} cas_first={} state={:#x} flags={:#x} was_busy={}",
        calls,
        cas.is_ok(),
        STATE.load(Ordering::Relaxed),
        flags,
        was_busy
    ));
    rep.check("atomics", STATE.load(Ordering::Relaxed) == 0x5255_5354 && flags == 0x5 && !was_busy);
    calls
}

fn test_core_fmt(rep: &mut Report, args: &Args) {
    let neg: i32 = -12345 - args.seed as i32;
    let small: u8 = 0xa5;
    rep.line(format_args!(
        "rust: core::fmt: dec={} hex={:#x} HEX={:08X} bin={:#b} pad=[{:>6}] [{:<6}] [{:06}] u64max={} str={:?} char={}",
        neg,
        0xdead_beef_u32,
        0xbeef_u32,
        small,
        args.seed,
        args.index,
        args.seed * 6,
        u64::MAX,
        "hello",
        'R'
    ));
    // 64-bit arithmetic: native instructions on arm64, no library calls
    let a = args.n_lo;
    let b = args.d_lo;
    rep.line(format_args!(
        "rust: u64: {:#x} / {:#x} = {} rem {} ; wrapping_mul={:#018x} rotl={:#018x} lz={} pop={}",
        a,
        b,
        a / b,
        a % b,
        a.wrapping_mul(b),
        a.rotate_left(13),
        b.leading_zeros(),
        a.count_ones()
    ));
}

fn test_u128(rep: &mut Report, args: &Args, res: &mut Results) {
    let n = ((args.n_hi as u128) << 64) | args.n_lo as u128;
    let d = ((args.d_hi as u128) << 64) | args.d_lo as u128;
    // 128-bit division and remainder are compiler_builtins calls (__udivti3, __umodti3)
    let (q, r) = match (n.checked_div(d), n.checked_rem(d)) {
        (Some(q), Some(r)) => (q, r),
        _ => {
            rep.check("u128 divisor is zero", false);
            (0, 0)
        }
    };
    let mul = (args.n_lo as u128) * (args.d_lo as u128);
    // signed variant (__divti3 / __modti3)
    let sn = -(n as i128);
    let sd = (args.d_lo as i128) + 1;
    let (sq, sr) = (sn / sd, sn % sd);

    res.q_hi = (q >> 64) as u64;
    res.q_lo = q as u64;
    res.r_hi = (r >> 64) as u64;
    res.r_lo = r as u64;
    res.mul_hi = (mul >> 64) as u64;
    res.mul_lo = mul as u64;

    rep.line(format_args!("rust: u128: n={:#034x} d={:#x}", n, d));
    rep.line(format_args!("rust: u128: q={:#x} r={:#x} mul={:#x}", q, r, mul));
    rep.line(format_args!("rust: i128: {} / {} = {} rem {} ; n as decimal {}", sn, sd, sq, sr, n));
    rep.check("u128 q*d+r", q.wrapping_mul(d).wrapping_add(r) == n && r < d);
    rep.check("i128 q*d+r", sq.wrapping_mul(sd).wrapping_add(sr) == sn);
}

fn test_alloc(rep: &mut Report, args: &Args) {
    let seed = args.seed;

    // Vec: growth by reallocation (GlobalAlloc::realloc), sort, search
    let mut v: Vec<u64> = Vec::new();
    for i in 0..1000u64 {
        v.push((i * 7919 + seed) % 1009);
    }
    let sum: u64 = v.iter().sum();
    v.sort_unstable();
    v.dedup();
    let pos = v.binary_search(&(seed % 1009));
    rep.line(format_args!(
        "rust: Vec: pushed=1000 sum={} unique={} min={} max={} find({})={:?}",
        sum,
        v.len(),
        v[0],
        v[v.len() - 1],
        seed % 1009,
        pos
    ));

    // Bounds-checked indexing with an index and a length the compiler cannot see through:
    // the check is in the code, and the normal path does not take it.
    let table = &v[..16];
    let idx = black_box(args.index as usize);
    let elem = table[idx];
    let beyond = table.get(black_box(table.len() + idx));
    rep.line(format_args!(
        "rust: bounds: table[{}]={} table.get({})={:?} (no panic)",
        idx,
        elem,
        table.len() + idx,
        beyond
    ));
    rep.check("bounds", beyond.is_none());

    // Box<dyn Trait>: vtable dispatch and drop glue through the vtable
    let drops_before = DROPS.load(Ordering::Relaxed);
    {
        let mut shapes: Vec<Box<dyn Shape>> = Vec::new();
        shapes.push(Box::new(Rect(3, seed)));
        shapes.push(Box::new(Square(seed + 2)));
        shapes.push(Box::new(Rect(seed, seed + 1)));
        let total: u64 = shapes.iter().map(|s| s.area()).sum();
        rep.line(format_args!(
            "rust: Box<dyn Shape>: {}={} {}={} {}={} total={}",
            shapes[0].name(),
            shapes[0].area(),
            shapes[1].name(),
            shapes[1].area(),
            shapes[2].name(),
            shapes[2].area(),
            total
        ));
    }
    let dropped = DROPS.load(Ordering::Relaxed) - drops_before;
    rep.line(format_args!("rust: Box<dyn Shape>: drops={:#x} (2 rect, 1 square = 0x102)", dropped));
    rep.check("dyn drop", dropped == 0x102);

    // BTreeMap: node allocation, ordered iteration, range, removal
    let mut map: BTreeMap<u32, u64> = BTreeMap::new();
    for i in 0..300u64 {
        let k = ((i * 7919 + seed) % 1009) as u32;
        map.insert(k, (k as u64) * (k as u64) + i);
    }
    let sorted = map.keys().zip(map.keys().skip(1)).all(|(a, b)| a < b);
    let in_range = map.range(100..200).count();
    let first = map.iter().next().map(|(k, v)| (*k, *v));
    let last = map.iter().next_back().map(|(k, v)| (*k, *v));
    let before = map.len();
    map.retain(|k, _| k % 3 != 0);
    let value_sum: u64 = map.values().sum();
    rep.line(format_args!(
        "rust: BTreeMap: len={} sorted={} range(100..200)={} first={:?} last={:?} after_retain={} sum={}",
        before,
        sorted,
        in_range,
        first,
        last,
        map.len(),
        value_sum
    ));
    rep.check("btreemap sorted", sorted);

    // Alignment: 16 (u128), 64 (cache line) and a 16 KiB page go to IOMallocAligned;
    // a zeroed 100000 byte Vec goes to IOMallocZero above the kalloc zone sizes.
    let wide: Box<u128> = Box::new(((seed as u128) << 100) | 1);
    let line: Box<CacheLine> = Box::new(CacheLine([seed as u8; 64]));
    let wide_addr = &*wide as *const u128 as usize;
    let line_addr = &*line as *const CacheLine as usize;
    let big = alloc::vec![0u8; 100_000];
    let big_zero = big.iter().all(|&b| b == 0);
    let page_layout = Layout::from_size_align(32768, 16384).unwrap();
    let page = unsafe { alloc_zeroed(page_layout) };
    let page_ok = !page.is_null() && (page as usize) % 16384 == 0;
    let page_zero = page_ok && unsafe { core::slice::from_raw_parts(page, 32768) }.iter().all(|&b| b == 0);
    if page_ok {
        unsafe {
            page.add(32767).write(0x5a);
            dealloc(page, page_layout);
        }
    }
    rep.line(format_args!(
        "rust: align: u128%16={} cacheline%64={} page%16384_ok={} page_zero={} big_len={} big_zero={} wide={:#x} line[63]={}",
        wide_addr % 16,
        line_addr % 64,
        page_ok,
        page_zero,
        big.len(),
        big_zero,
        *wide,
        line.0[63]
    ));
    rep.check("alignment", wide_addr % 16 == 0 && line_addr % 64 == 0 && page_ok && page_zero && big_zero);
}

/// The entry point SFRustHello::start() calls. See sf_rust_hello.h.
///
/// # Safety
/// `args` and `result` must point to valid structures.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sf_rust_hello_entry(args: *const Args, result: *mut Results) -> u64 {
    let args = unsafe { &*args };
    let res = unsafe { &mut *result };
    let mut rep = Report::new();

    let calls = test_atomics(&mut rep);
    test_core_fmt(&mut rep, args);
    test_u128(&mut rep, args, res);

    let before = alloc_stats();
    test_alloc(&mut rep, args);
    let after = alloc_stats();
    rep.line(format_args!(
        "rust: allocator: allocs={} frees={} aligned={} live_bytes={}",
        after.allocs - before.allocs,
        after.frees - before.frees,
        after.aligned_allocs - before.aligned_allocs,
        after.live_bytes
    ));
    rep.check("allocator balance", after.allocs - before.allocs == after.frees - before.frees && after.live_bytes == 0);

    // A variadic C function called straight from Rust (not part of the checksum).
    unsafe {
        ffi::IOLog(
            c"SFRustHello: rust: variadic IOLog: %d %s 0x%llx %c\n".as_ptr(),
            -42 as c_int,
            c"ok".as_ptr(),
            0x0123_4567_89ab_cdef_u64,
            b'!' as c_int,
        );
    }

    res.calls = calls;
    res.live_bytes = after.live_bytes;
    res.failures = rep.failures;
    let words = [res.q_hi, res.q_lo, res.r_hi, res.r_lo, res.mul_hi, res.mul_lo, res.calls, res.live_bytes, res.failures];
    for w in words {
        rep.mix(&w.to_le_bytes());
    }
    rep.hash
}

/// The static call counter, read back by the C side after the entry returned.
#[unsafe(no_mangle)]
pub extern "C" fn sf_rust_hello_calls() -> u64 {
    CALLS.load(Ordering::SeqCst)
}
