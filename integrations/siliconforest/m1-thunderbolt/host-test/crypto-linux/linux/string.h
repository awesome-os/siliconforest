/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_CRYPTO_HOST_STRING_H
#define SF_TB_CRYPTO_HOST_STRING_H
#include <string.h>
#include <linux/types.h>
static inline void memzero_explicit(void *p,size_t n) { volatile unsigned char *b=p;while(n--)*b++=0; }
#endif
