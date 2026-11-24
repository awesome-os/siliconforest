/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_WIRE_H
#define SF_TB_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Missing LinuxKPI wire helpers. Native-endian words to/from big-endian
 * protocol bytes; memcpy permits in-place and unaligned word arrays. */
static inline void sf_tb_to_be32_array(void *dest,const void *source,size_t words)
{
    uint8_t *d=dest;const uint8_t *s=source;
    for(size_t i=0;i<words;++i) {
        uint32_t v;memcpy(&v,s+4*i,4);
        d[4*i]=v>>24;d[4*i+1]=v>>16;d[4*i+2]=v>>8;d[4*i+3]=v;
    }
}
static inline void sf_tb_from_be32_array(void *dest,const void *source,size_t words)
{
    uint8_t *d=dest;const uint8_t *s=source;
    for(size_t i=0;i<words;++i) {
        uint32_t v=(uint32_t)s[4*i]<<24|(uint32_t)s[4*i+1]<<16|(uint32_t)s[4*i+2]<<8|s[4*i+3];
        memcpy(d+4*i,&v,4);
    }
}
/* Reflected Castagnoli CRC, Linux crc32c seed/result convention: no
 * implicit complement. Control packets supply ~0 and complement explicitly. */
static inline uint32_t sf_tb_crc32c(uint32_t crc,const void *buffer,size_t bytes)
{
    const uint8_t *p=buffer;
    for(size_t i=0;i<bytes;++i) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0x82f63b78u & (0u-(crc&1)));
    }
    return crc;
}
#endif
