/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Private sf_soc.cpp extension, PMGR sequences from Linux pmgr-pwrstate.c,
 * Copyright The Asahi Linux Contributors. T8103 ACIO1 owner only.
 */
static int sf_cio_gate_map(uint32_t cell,SFMmio &reg)
{
    if (!cell || (cell&~PMGR_DEVICE_ID)) return EINVAL;
    const uint8_t *dev=dev_find((uint16_t)cell);
    if (!dev || (dev[DEV_FLAGS]&PMGR_FLAG_VIRTUAL)) return ENOENT;
    unsigned idx=dev[DEV_PSREG_IDX];
    if ((idx+1)*12>gPmgr.psRegsLen) return EINVAL;
    unsigned n=le32(gPmgr.psRegs+idx*12);
    if(n>=gPmgr.regs->getCount())return EINVAL;
    auto md=OSDynamicCast(IOMemoryDescriptor,gPmgr.regs->getObject(n));
    uint64_t off=(uint64_t)le32(gPmgr.psRegs+idx*12+4)+((uint64_t)dev[DEV_ADDR_OFFSET]<<3);
    if (!md || off>md->getLength() || md->getLength()-off<4)return EINVAL;
    uint64_t base=md->getPhysicalSegment(0,NULL,kIOMemoryMapperNone);
    return base && base<=UINT64_MAX-off && reg.mapPhys(base+off,4)?0:ENXIO;
}
/* Called only after all children and RTKit have stopped. Never walks up to
 * shared parents or disables PCIe-C's always-on domain. Partial failure keeps
 * parent/PHY ownership; retry may finish remaining gates. */
int sf_cio_power_off()
{
    pmgr_lock_init();if(!gPmgr.lock)return ENOMEM;
    IOLockLock(gPmgr.lock);
    int ret=pmgr_load();SFMmio maps[16];
    unsigned count=sf_adt_cells("/arm-io/acio1","power-gates");
    if(count!=3)ret=EINVAL;
    for(unsigned i=0;!ret && i<count;++i){
        uint32_t cell=0;
        if(!sf_adt_u32("/arm-io/acio1","power-gates",i,&cell)){ret=EINVAL;break;}
        ret=sf_cio_gate_map(cell,maps[i]);
        if(!ret && maps[i].phys!=0x23b700458ULL+8*i)ret=ENODEV;
    }
    for(unsigned i=count;!ret && i>0;--i){
        auto &reg=maps[i-1];
        uint32_t value=reg.r32(0);
        if(value==UINT32_MAX || (value&PMGR_RESET)){ret=EBUSY;break;}
        // Do not replay hardware BUSY or latched WAS_* bits.
        value&=~(PMGR_AUTO_ENABLE|PMGR_WAS_CLKGATED|PMGR_WAS_PWRGATED|
                  (1u<<11)|PMGR_DEV_DISABLE|(1u<<12)|PMGR_PS_TARGET);
        reg.w32(0,value);
        ret=ETIMEDOUT;
        for(unsigned wait=0;wait<1000;++wait){
            uint32_t now=reg.r32(0);
            if(!(now&(PMGR_PS_ACTUAL|PMGR_PS_TARGET|PMGR_AUTO_ENABLE))){ret=0;break;}
            IODelay(1);
        }
    }
    for(unsigned i=0;i<16;++i)maps[i].unmap();
    IOLockUnlock(gPmgr.lock);return ret;
}
/* The caller owns PCIe-C and verified its host/DART unbound. Reset its
 * physical ATC1_PCIE domain; the clock-gate list contains virtual AON/PCIe
 * votes, not the reset target. Never reset the AON parent or Wi-Fi bridge. */
int sf_cio_pcie_reset()
{
    pmgr_lock_init();if(!gPmgr.lock)return ENOMEM;
    IOLockLock(gPmgr.lock);
    int ret=pmgr_load();uint32_t cell=0;SFMmio reg;
    if(!ret){
        const uint8_t *dev=dev_find_named("ATC1_PCIE");
        if(!dev || (dev[DEV_FLAGS]&PMGR_FLAG_VIRTUAL))ret=ENOENT;
        else cell=dev_id(dev);
    }
    if(!ret)ret=sf_cio_gate_map(cell,reg);
    if(!ret && reg.phys!=0x23b700450ULL)ret=ENODEV;
    if(!ret){
        uint32_t before=reg.r32(0);
        if(before==UINT32_MAX || ((before&PMGR_PS_ACTUAL)>>4)!=PMGR_PS_ACTIVE)ret=EBUSY;
        else{
            uint32_t clear=PMGR_WAS_CLKGATED|PMGR_WAS_PWRGATED|(1u<<11);
            reg.mask32(0,clear,PMGR_DEV_DISABLE);
            reg.mask32(0,clear,PMGR_RESET);IODelay(1);
            reg.mask32(0,clear|PMGR_RESET,0);
            reg.mask32(0,clear|PMGR_DEV_DISABLE,0);
            uint32_t now=reg.r32(0);
            if((now&(PMGR_RESET|PMGR_DEV_DISABLE)) || ((now&PMGR_PS_ACTUAL)>>4)!=PMGR_PS_ACTIVE)ret=EIO;
        }
    }
    reg.unmap();IOLockUnlock(gPmgr.lock);return ret;
}
