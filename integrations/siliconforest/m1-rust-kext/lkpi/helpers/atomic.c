// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/atomic.c (generated on Linux, 170 helpers).
 *
 * LinuxKPI's asm/atomic.h lacks 119 of the operations that file wraps (every _acquire,
 * _release and _relaxed variant, most fetch_* and all of try_cmpxchg for atomic64). The
 * kernel crate itself calls 21 operations per width (and atomic_sub once) (rust/kernel/sync/atomic/internal.rs).
 * Those are implemented here directly with the compiler's atomic builtins on the counter
 * field, sequentially consistent, which satisfies every weaker ordering the name asks for.
 * The other operations have no Rust caller and are not provided.
 */
#include <linux/types.h>
#include <linux/atomic.h>
#include <asm/atomic64.h>

#define SF_ATOMIC_ORDERINGS(X) X() X(_acquire) X(_release) X(_relaxed)

#define SF_ATOMIC_HELPERS(prefix, atype, itype)							\
__rust_helper itype rust_helper_##prefix##_read(const atype *v)					\
{ return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST); }					\
__rust_helper itype rust_helper_##prefix##_read_acquire(const atype *v)				\
{ return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST); }					\
__rust_helper void rust_helper_##prefix##_set(atype *v, itype i)				\
{ __atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST); }						\
__rust_helper void rust_helper_##prefix##_set_release(atype *v, itype i)			\
{ __atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST); }						\
__rust_helper void rust_helper_##prefix##_add(itype i, atype *v)				\
{ (void)__atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }

#define SF_ATOMIC_ORDERED(prefix, atype, itype, sfx)						\
__rust_helper itype rust_helper_##prefix##_xchg##sfx(atype *v, itype new)			\
{ return __atomic_exchange_n(&v->counter, new, __ATOMIC_SEQ_CST); }				\
__rust_helper bool rust_helper_##prefix##_try_cmpxchg##sfx(atype *v, itype *old, itype new)	\
{ return __atomic_compare_exchange_n(&v->counter, old, new, false,				\
	__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }							\
__rust_helper itype rust_helper_##prefix##_fetch_add##sfx(itype i, atype *v)			\
{ return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }				\
__rust_helper itype rust_helper_##prefix##_fetch_sub##sfx(itype i, atype *v)			\
{ return __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST); }

/* rust/kernel/drm/sched.rs (credit accounting); not one of the crate's own atomics */
__rust_helper void rust_helper_atomic_sub(int i, atomic_t *v)
{
	(void)__atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST);
}

SF_ATOMIC_HELPERS(atomic, atomic_t, int)
#define X(sfx) SF_ATOMIC_ORDERED(atomic, atomic_t, int, sfx)
SF_ATOMIC_ORDERINGS(X)
#undef X

SF_ATOMIC_HELPERS(atomic64, atomic64_t, s64)
#define X(sfx) SF_ATOMIC_ORDERED(atomic64, atomic64_t, s64, sfx)
SF_ATOMIC_ORDERINGS(X)
#undef X
