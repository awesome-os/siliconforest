/* SPDX-License-Identifier: MIT */
/* Private extension appended to this bundle's copy of sf_soc.cpp. The PMGR
 * layout follows m1n1 (MIT), Copyright The Asahi Linux Contributors.
 * No writes. Read the physical power-gates, not clock-gates: ACIO clock-gates
 * also lists a virtual CIO1_RECONFIG reset command with no state register.
 * Unknown/malformed/virtual power gates cannot establish quiescence.
 * The port owner serializes claims; no ACIO owner may power up before claim.
 */
int sf_usbc_pmgr_node_off(const char *path, uint32_t *observed)
{
    if (!path || !observed) return EINVAL;
    if (strcmp(path,"/arm-io/acio1")) return ENODEV;
    *observed=0;
    unsigned count=sf_adt_cells(path,"power-gates");
    if (count!=3) return ENOENT;
    pmgr_lock_init();
    if (!gPmgr.lock) return ENOMEM;
    IOLockLock(gPmgr.lock);
    int ret=pmgr_load();
    bool busy=false;
    for (unsigned i=0;!ret && i<count;++i) {
        uint32_t cell=0;
        if (!sf_adt_u32(path,"power-gates",i,&cell) || (cell&~PMGR_DEVICE_ID)) {
            ret=EINVAL;break; // This provider is T8103, die zero only.
        }
        const uint8_t *dev=dev_find((uint16_t)cell);
        if (!cell || !dev || (dev[DEV_FLAGS]&PMGR_FLAG_VIRTUAL)) { ret=ENOENT;break; }
        unsigned idx=dev[DEV_PSREG_IDX];
        if ((idx+1)*12>gPmgr.psRegsLen) { ret=EINVAL;break; }
        uint32_t regIdx=le32(gPmgr.psRegs+idx*12);
        if (regIdx>=gPmgr.regs->getCount()) { ret=EINVAL;break; }
        auto md=OSDynamicCast(IOMemoryDescriptor,gPmgr.regs->getObject(regIdx));
        uint64_t offset=(uint64_t)le32(gPmgr.psRegs+idx*12+4)+((uint64_t)dev[DEV_ADDR_OFFSET]<<3);
        if (!md || offset>md->getLength() || md->getLength()-offset<4) { ret=EINVAL;break; }
        uint64_t base=md->getPhysicalSegment(0,NULL,kIOMemoryMapperNone);
        SFMmio reg;
        if (!base || base>UINT64_MAX-offset || base+offset!=0x23b700458ULL+8*i) { ret=ENXIO;break; }
        if (!reg.mapPhys(base+offset,4)) { ret=ENXIO;break; }
        uint32_t value=reg.r32(0);
        reg.unmap();
        *observed|=value;
        // Clock-gated/auto domains can wake when PHY clocks start. Only a
        // completely off current AND requested state is an initial handoff.
        if (value&(PMGR_PS_ACTUAL|PMGR_PS_TARGET|PMGR_AUTO_ENABLE)) busy=true;
    }
    IOLockUnlock(gPmgr.lock);
    return ret?ret:(busy?EBUSY:0);
}
