/* SPDX-License-Identifier: MIT */
#include <linux/types.h>
#define ror32(v,n) (((u32)(v)>>(n))|((u32)(v)<<(32-(n))))
#define OPTIMIZER_HIDE_VAR(v) __asm__ __volatile__("" : "+r" (v))
#define IS_ENABLED(v) 0
#define likely(x) __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define noinline_for_stack __attribute__((__noinline__))
#define __must_check __attribute__((warn_unused_result))
