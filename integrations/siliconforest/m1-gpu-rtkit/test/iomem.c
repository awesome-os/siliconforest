/* SPDX-License-Identifier: GPL-2.0-only */
/* Tests the actual Linux-derived I/O copy with instrumented raw reads. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
static uintptr_t begin,end;
static size_t copied;
static uint8_t byte_read(const volatile void *p)
{
    uintptr_t a=(uintptr_t)p;assert(a>=begin && a<end);++copied;
    return *(const volatile uint8_t *)p;
}
static uint64_t word_read(const volatile void *p)
{
    uintptr_t a=(uintptr_t)p;assert(!(a&7) && a>=begin && a<=end && end-a>=8);copied+=8;
    return *(const volatile uint64_t *)p;
}
#define SF_RTKIT_IO_READ8(p) byte_read(p)
#define SF_RTKIT_IO_READ64(p) word_read(p)
#include "sf_rtkit_iomem.h"
int main(void)
{
    _Alignas(16) uint8_t source[512],dest[512];
    for(unsigned i=0;i<sizeof(source);++i)source[i]=(uint8_t)(i*197+29);
    sf_rtkit_memcpy_fromio(NULL,NULL,0);
    unsigned tests=0;
    for(unsigned s=0;s<32;++s)for(unsigned d=0;d<32;++d)for(size_t n=0;n<=256;++n){
        memset(dest,0xcc,sizeof(dest));begin=(uintptr_t)(source+s);end=begin+n;copied=0;
        sf_rtkit_memcpy_fromio(dest+d,source+s,n);
        assert(copied==n && !memcmp(dest+d,source+s,n));
        for(unsigned i=0;i<d;++i)assert(dest[i]==0xcc);
        for(size_t i=d+n;i<sizeof(dest);++i)assert(dest[i]==0xcc);
        ++tests;
    }
    // Exact panic shape: SYSLOG context begins twelve bytes into device SRAM.
    memset(dest,0xcc,sizeof(dest));begin=(uintptr_t)source+12;end=begin+16;copied=0;
    sf_rtkit_memcpy_fromio(dest,source+12,16);assert(copied==16 && !memcmp(dest,source+12,16));
    printf("I/O copy: %u source/destination alignment and length cases, byte-exact reads/no MMIO overread, zero-size and actual SYSLOG+12/16 shape passed\n",tests);
}
