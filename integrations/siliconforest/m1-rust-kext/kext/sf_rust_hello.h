/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The C view of the Rust entry point of SFRustHello (rust/sfrusthello/lib.rs).
 * Shared by the kext (SFRustHello.cpp) and the host check (host-check/host.c).
 */
#ifndef SF_RUST_HELLO_H
#define SF_RUST_HELLO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inputs come from the caller at run time so the Rust side cannot fold the arithmetic. */
struct sf_rust_hello_args {
	uint64_t n_hi, n_lo;    /* 128-bit dividend */
	uint64_t d_hi, d_lo;    /* 128-bit divisor, not zero */
	uint64_t seed;          /* small number mixed into the alloc tests */
	uint64_t index;         /* index for the bounds-checked access, must be < 16 */
};

struct sf_rust_hello_result {
	uint64_t q_hi, q_lo;    /* n / d  (compiler_builtins __udivti3) */
	uint64_t r_hi, r_lo;    /* n % d  (compiler_builtins __umodti3) */
	uint64_t mul_hi, mul_lo; /* n_lo * d_lo as a 128-bit product */
	uint64_t calls;         /* the static atomic call counter after this call */
	uint64_t live_bytes;    /* heap bytes still allocated when the entry returns: 0 */
	uint64_t failures;      /* number of self-checks that failed on the Rust side: 0 */
};

/* Returns an FNV-1a 64 over every line it logged (and the result fields). */
uint64_t sf_rust_hello_entry(const struct sf_rust_hello_args *args,
    struct sf_rust_hello_result *result);
uint64_t sf_rust_hello_calls(void);

/* Implemented by the caller. */
void sf_rust_log(const char *text, unsigned long len);
void sf_rust_panic(const char *text, unsigned long len) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

/* What start() passes. */
#define SF_RUST_HELLO_N_HI  0x0123456789abcdefULL
#define SF_RUST_HELLO_N_LO  0xfedcba9876543210ULL
#define SF_RUST_HELLO_D_HI  0x0000000000000003ULL
#define SF_RUST_HELLO_D_LO  0x00000000deadbeefULL
#define SF_RUST_HELLO_SEED  7ULL
#define SF_RUST_HELLO_INDEX 5ULL

/*
 * The value sf_rust_hello_entry returns for those inputs on its first call. Produced by
 * host-check.nix, which runs the same Rust source on the build host and fails if this
 * constant is stale.
 */
#define SF_RUST_HELLO_EXPECT 0xc2c91db8c4ef571fULL

/* q * d + r == n and r < d, in the caller's own 128-bit arithmetic (no library calls). */
static inline int
sf_rust_hello_check_div(const struct sf_rust_hello_args *a, const struct sf_rust_hello_result *r)
{
	unsigned __int128 n = ((unsigned __int128)a->n_hi << 64) | a->n_lo;
	unsigned __int128 d = ((unsigned __int128)a->d_hi << 64) | a->d_lo;
	unsigned __int128 q = ((unsigned __int128)r->q_hi << 64) | r->q_lo;
	unsigned __int128 rem = ((unsigned __int128)r->r_hi << 64) | r->r_lo;
	unsigned __int128 mul = ((unsigned __int128)r->mul_hi << 64) | r->mul_lo;

	return q * d + rem == n && rem < d &&
	    mul == (unsigned __int128)a->n_lo * a->d_lo;
}

/*
 * The whole test as start() runs it. The includer defines SF_RUST_HELLO_LOG(fmt, ...) as its
 * printf (IOLog in the kext). Returns 1 when everything matched.
 */
#ifdef SF_RUST_HELLO_LOG
static inline int
sf_rust_hello_run(void)
{
	struct sf_rust_hello_args args = {
		SF_RUST_HELLO_N_HI, SF_RUST_HELLO_N_LO, SF_RUST_HELLO_D_HI, SF_RUST_HELLO_D_LO,
		SF_RUST_HELLO_SEED, SF_RUST_HELLO_INDEX,
	};
	struct sf_rust_hello_result res = { 0, 0, 0, 0, 0, 0, 0, 0, ~0ULL };
	uint64_t value;
	int div_ok, ok;

	SF_RUST_HELLO_LOG("SFRustHello: calling rust, counter before = %llu\n",
	    (unsigned long long)sf_rust_hello_calls());
	value = sf_rust_hello_entry(&args, &res);
	div_ok = sf_rust_hello_check_div(&args, &res);
	ok = value == SF_RUST_HELLO_EXPECT && div_ok && res.failures == 0 &&
	    res.live_bytes == 0 && res.calls == 1 && sf_rust_hello_calls() == 1;

	SF_RUST_HELLO_LOG("SFRustHello: rust returned 0x%016llx, expected 0x%016llx: %s\n",
	    (unsigned long long)value, (unsigned long long)SF_RUST_HELLO_EXPECT,
	    value == SF_RUST_HELLO_EXPECT ? "match" : "MISMATCH");
	SF_RUST_HELLO_LOG("SFRustHello: C check of u128 q*d+r==n, r<d, mul: %s (q=0x%llx%016llx r=0x%llx%016llx)\n",
	    div_ok ? "ok" : "FAILED",
	    (unsigned long long)res.q_hi, (unsigned long long)res.q_lo,
	    (unsigned long long)res.r_hi, (unsigned long long)res.r_lo);
	SF_RUST_HELLO_LOG("SFRustHello: rust failures=%llu live_bytes=%llu calls=%llu counter after = %llu\n",
	    (unsigned long long)res.failures, (unsigned long long)res.live_bytes,
	    (unsigned long long)res.calls, (unsigned long long)sf_rust_hello_calls());
	SF_RUST_HELLO_LOG("SFRustHello: %s\n", ok ? "ALL OK" : "FAILED");
	return ok;
}
#endif

#endif /* SF_RUST_HELLO_H */
