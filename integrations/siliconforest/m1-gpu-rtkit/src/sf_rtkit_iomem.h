/* SPDX-License-Identifier: GPL-2.0-only */
/* Aligned I/O-memory copy from Linux lib/iomem_copy.c (linux-aurora 6a08a08e).
 * Copyright 2024 Kalray, Inc. All Rights Reserved.
 * Native namespace/types and byte-pointer arithmetic; the source alignment,
 * word-copy and tail sequence follows memcpy_fromio(). RAM stores may be
 * unaligned, device-memory reads may not. Never pass an MMIO source to libc.
 */
#ifndef SF_RTKIT_IOMEM_H
#define SF_RTKIT_IOMEM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef SF_RTKIT_IO_READ8
#define SF_RTKIT_IO_READ8(p) (*(const volatile uint8_t *)(p))
#define SF_RTKIT_IO_READ8_LOCAL
#endif
#ifndef SF_RTKIT_IO_READ64
#define SF_RTKIT_IO_READ64(p) (*(const volatile uint64_t *)(p))
#define SF_RTKIT_IO_READ64_LOCAL
#endif
static inline void sf_rtkit_memcpy_fromio(void *dst,const volatile void *src,size_t count)
{
    uint8_t *to=(uint8_t *)dst;
    const volatile uint8_t *from=(const volatile uint8_t *)src;
    while(count && ((uintptr_t)from&(sizeof(uint64_t)-1))){
        *to++=SF_RTKIT_IO_READ8(from++);
        --count;
    }
    while(count>=sizeof(uint64_t)){
        uint64_t value=SF_RTKIT_IO_READ64(from);
        // Ordinary memcpy is used only from a RAM local into RAM, never
        // from the device mapping. This is Linux put_unaligned's role.
        memcpy(to,&value,sizeof(value));
        from+=sizeof(value);to+=sizeof(value);count-=sizeof(value);
    }
    while(count){*to++=SF_RTKIT_IO_READ8(from++);--count;}
}
#ifdef SF_RTKIT_IO_READ8_LOCAL
#undef SF_RTKIT_IO_READ8
#undef SF_RTKIT_IO_READ8_LOCAL
#endif
#ifdef SF_RTKIT_IO_READ64_LOCAL
#undef SF_RTKIT_IO_READ64
#undef SF_RTKIT_IO_READ64_LOCAL
#endif
#endif
