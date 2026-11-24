// SPDX-License-Identifier: MIT
#include "sf_tb_wire.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint8_t bytes[]="123456789";
    assert(~sf_tb_crc32c(~0u,bytes,9)==0xe3069283);
    assert(sf_tb_crc32c(0x12345678,NULL,0)==0x12345678);
    for(unsigned split=0;split<=9;++split)
        assert(sf_tb_crc32c(sf_tb_crc32c(~0u,bytes,split),bytes+split,9-split)==0x1cf96d7c);
    uint8_t native[1026],wire[1026],back[1026];
    for(unsigned i=0;i<sizeof(native);++i)native[i]=i*23;
    for(unsigned offset=0;offset<2;++offset)for(unsigned words=0;words<=256;++words) {
        memset(wire,0x55,sizeof(wire));memset(back,0x77,sizeof(back));
        sf_tb_to_be32_array(wire+offset,native+offset,words);
        sf_tb_from_be32_array(back+offset,wire+offset,words);
        assert(!memcmp(back+offset,native+offset,4*words));
        assert(wire[offset+4*words]==0x55 && back[offset+4*words]==0x77);
        sf_tb_from_be32_array(wire+offset,wire+offset,words);
        assert(!memcmp(wire+offset,native+offset,4*words));
    }
    uint32_t word=0x12345678;sf_tb_to_be32_array(wire,&word,1);
    assert(!memcmp(wire,"\x12\x34\x56\x78",4));
    puts("Thunderbolt wire helpers: Castagnoli vector/split seed convention; 514 unaligned/in-place endian-array roundtrips and boundary cases pass");
}
