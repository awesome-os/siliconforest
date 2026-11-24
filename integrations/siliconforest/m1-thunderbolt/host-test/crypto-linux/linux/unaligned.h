/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_CRYPTO_HOST_UNALIGNED_H
#define SF_TB_CRYPTO_HOST_UNALIGNED_H
#include <string.h>
#include <linux/types.h>
#define cpu_to_be32(v) __builtin_bswap32((u32)(v))
#define cpu_to_be64(v) __builtin_bswap64((u64)(v))
static inline u32 get_unaligned_be32(const void *p) { u32 v;memcpy(&v,p,4);return __builtin_bswap32(v); }
static inline void put_unaligned_be32(u32 v,void *p) { v=cpu_to_be32(v);memcpy(p,&v,4); }
#define get_unaligned(p) ({ __typeof__(*(p)) v;memcpy((void *)&v,(p),sizeof(v));v; })
#define put_unaligned(v,p) do { __typeof__(*(p)) value=(v);memcpy((p),&value,sizeof(value)); } while(0)
#endif
