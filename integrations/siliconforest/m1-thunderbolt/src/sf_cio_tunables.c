// SPDX-License-Identifier: MIT
/* Copyright The Asahi Linux Contributors
 * Mapping/24-byte validation: m1n1 src/kboot.c (121fcbf35d45).
 * Clear-mask/OR-set and skip-unchanged: Linux drivers/soc/apple/tunable.c
 * (6a08a08e, dual MIT/GPL). This port selects MIT; see NOTICE.
 */
#include "sf_cio_tunables.h"
#include <errno.h>
static const struct group { const char *name;enum sf_cio_window window;uint32_t base; } groups[11]={
    {"top_tunables",SF_CIO_RC,0}, {"hbw_fabric_tunables",SF_CIO_RC,0x4000},
    {"lbw_fabric_tunables",SF_CIO_RC,0x8000},
    {"hi_up_tx_desc_fabric_tunables",SF_CIO_NHI,0xf0000},
    {"hi_up_tx_data_fabric_tunables",SF_CIO_NHI,0xec000},
    {"hi_up_rx_desc_fabric_tunables",SF_CIO_NHI,0xe8000},
    {"hi_up_wr_fabric_tunables",SF_CIO_NHI,0xf4000},
    {"hi_up_merge_fabric_tunables",SF_CIO_NHI,0xf8000},
    {"hi_dn_merge_fabric_tunables",SF_CIO_NHI,0xfc000},
    {"fw_int_ctl_management_tunables",SF_CIO_NHI,0x4000},
    {"pcie_adapter_regs_tunables",SF_CIO_PCIE_ADAPTER,0}
};
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int validate(const struct sf_cio_tunables *b)
{
    if(!b)return EINVAL;
    for(unsigned n=0;n<11;++n) {
        const uint8_t *p=b->entries[n].raw;size_t bytes=b->entries[n].bytes;
        if(!p || !bytes)return ENOENT;
        if(bytes%24)return EINVAL;
        for(size_t i=0;i<bytes;i+=24) {
            unsigned off=le32(p+i),size=le32(p+i+4);
            if(size!=4 || (off&3) || off>0x4000-4 || le32(p+i+12) || le32(p+i+20))return EINVAL;
        }
    }
    return 0;
}
int sf_cio_tunables_load(struct sf_cio_tunables *b,const struct sf_cio_tunable_io *io)
{
    if(!b)return EINVAL;b->checked=false;
    if(!io || !io->getprop || !io->read || !io->write)return EINVAL;
    for(unsigned n=0;n<11;++n) {
        b->entries[n].bytes=0;
        b->entries[n].raw=io->getprop(io->ctx,"/arm-io/acio1",groups[n].name,&b->entries[n].bytes);
    }
    int ret=validate(b);if(!ret)b->checked=true;return ret;
}
int sf_cio_tunables_apply(const struct sf_cio_tunables *b,const struct sf_cio_tunable_io *io,enum sf_cio_window window)
{
    if(!b || !b->checked || !io || !io->read || !io->write || (unsigned)window>SF_CIO_PCIE_ADAPTER)return EINVAL;
    int ret=validate(b);if(ret)return ret;
    for(unsigned n=0;n<11;++n) {
        if(groups[n].window!=window)continue;
        const uint8_t *p=b->entries[n].raw;
        for(size_t i=0;i<b->entries[n].bytes;i+=24) {
            uint32_t off=groups[n].base+le32(p+i),clear=le32(p+i+8),set=le32(p+i+16);
            uint32_t old=io->read(io->ctx,window,off),value=(old&~clear)|set;
            if(value!=old)io->write(io->ctx,window,off,value);
        }
    }
    return 0;
}
int sf_cio_tunables_observe(const struct sf_cio_tunables *b,const struct sf_cio_tunable_io *io,
 struct sf_cio_tunable_observation *out,size_t capacity,size_t *count)
{
    if(!count)return EINVAL;*count=0;
    if(!b || !b->checked || !io || !io->read || !out || !capacity || capacity>512)return EINVAL;
    int ret=validate(b);if(ret)return ret;
    size_t required=0;
    for(unsigned n=0;n<11;++n){
        size_t records=b->entries[n].bytes/24;
        if(records>capacity-required)return EOVERFLOW;
        required+=records;
    }
    size_t index=0;
    for(unsigned n=0;n<11;++n){
        const uint8_t *p=b->entries[n].raw;
        for(size_t off=0;off<b->entries[n].bytes;off+=24){
            uint32_t address=groups[n].base+le32(p+off);
            out[index++]=(struct sf_cio_tunable_observation){groups[n].window,address,io->read(io->ctx,groups[n].window,address)};
        }
    }
    *count=index;return 0;
}
