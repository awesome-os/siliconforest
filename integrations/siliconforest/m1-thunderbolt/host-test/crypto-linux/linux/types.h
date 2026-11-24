/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_CRYPTO_HOST_TYPES_H
#define SF_TB_CRYPTO_HOST_TYPES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
typedef uint8_t __u8;typedef uint16_t __u16;typedef uint32_t __u32;typedef uint64_t __u64;
typedef uint32_t __be32;typedef uint64_t __be64;
#define __aligned(n) __attribute__((aligned(n)))
#define __maybe_unused __attribute__((unused))
#define __initconst
#define noinline __attribute__((noinline))
#endif
