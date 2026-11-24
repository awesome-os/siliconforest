// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/atomic_ext.c: the i8, i16 and pointer atomics.
 * Linux builds them from READ_ONCE/smp_load_acquire/xchg/try_cmpxchg (asm/rwonce.h,
 * asm/barrier.h), which LinuxKPI has only in part; here they are the compiler's atomic
 * builtins, sequentially consistent. Same helper names and signatures as Linux.
 */
#include <linux/types.h>

#define SF_ATOMIC_EXT(tname, type)								\
__rust_helper type rust_helper_atomic_##tname##_read(type *ptr)					\
{ return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }						\
__rust_helper type rust_helper_atomic_##tname##_read_acquire(type *ptr)				\
{ return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }						\
__rust_helper void rust_helper_atomic_##tname##_set(type *ptr, type val)			\
{ __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST); }						\
__rust_helper void rust_helper_atomic_##tname##_set_release(type *ptr, type val)		\
{ __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST); }

#define SF_ATOMIC_EXT_ORDERED(tname, type, sfx)							\
__rust_helper type rust_helper_atomic_##tname##_xchg##sfx(type *ptr, type new)			\
{ return __atomic_exchange_n(ptr, new, __ATOMIC_SEQ_CST); }					\
__rust_helper bool rust_helper_atomic_##tname##_try_cmpxchg##sfx(type *ptr, type *old, type new)\
{ return __atomic_compare_exchange_n(ptr, old, new, false,					\
	__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }

#define SF_ATOMIC_EXT_ALL(tname, type)								\
SF_ATOMIC_EXT(tname, type)									\
SF_ATOMIC_EXT_ORDERED(tname, type, )								\
SF_ATOMIC_EXT_ORDERED(tname, type, _acquire)							\
SF_ATOMIC_EXT_ORDERED(tname, type, _release)							\
SF_ATOMIC_EXT_ORDERED(tname, type, _relaxed)

SF_ATOMIC_EXT_ALL(i8, s8)
SF_ATOMIC_EXT_ALL(i16, s16)
SF_ATOMIC_EXT_ALL(ptr, const void *)
