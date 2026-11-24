#include "SFM1KeyboardHW.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <kern/clock.h>
#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>

#if !defined(__arm64__) && !defined(__aarch64__)
#error SFM1KeyboardHW is an ARM64 J293-only kernel adapter
#endif

SFM1KeyboardHW::SFM1KeyboardHW() : propertyCache(nullptr), opened(false)
{
    bzero(&hw,sizeof(hw)); bzero(descriptors,sizeof(descriptors)); bzero(maps,sizeof(maps));
}
int SFM1KeyboardHW::property(void *ctx, const char *path, const char *key,
                            const uint8_t **bytes, size_t *length)
{
    SFM1KeyboardHW *self=static_cast<SFM1KeyboardHW *>(ctx);
    if (!self->propertyCache || self->propertyCache->getCount()>=128) return -1;
    IORegistryEntry *node=IORegistryEntry::fromPath(path,gIODTPlane);
    if (!node) return -1;
    OSObject *value=node->copyProperty(key);
    node->release();
    OSData *data=OSDynamicCast(OSData,value);
    int result=-1;
    if (data && self->propertyCache->setObject(data)) {
        *length=data->getLength();
        *bytes=static_cast<const uint8_t *>(data->getBytesNoCopy());
        result=0;
    }
    if (value) value->release();
    return result;
}
int SFM1KeyboardHW::open()
{
    if (opened || hw.initialized) return M1KBD_STATE;
    int enabled=0;
    if (!PE_parse_boot_argn("sfm1kbd",&enabled,sizeof(enabled)) || enabled!=1) return M1KBD_BOARD;
    propertyCache=OSArray::withCapacity(64);
    if (!propertyCache) return M1KBD_IO;
    struct m1kbd_board board;
    int e=m1kbd_validate_board(property,this,&board);
    propertyCache->release(); propertyCache=nullptr;
    if (e) return e;
    /* Mapping creates no register access. Only the qualified apertures can
     * be mapped; failures release maps before their backing descriptors. */
    for (unsigned i=0;i<M1KBD_REGIONS;i++) {
        descriptors[i]=IOMemoryDescriptor::withPhysicalAddress(board.base[i],board.size[i],kIODirectionInOut);
        if (!descriptors[i]) { releaseMaps(); return M1KBD_IO; }
        maps[i]=descriptors[i]->map(kIOMapAnywhere|kIOMapInhibitCache);
        if (!maps[i] || !maps[i]->getVirtualAddress() || maps[i]->getSize()<board.size[i]) {
            releaseMaps(); return M1KBD_IO;
        }
    }
    struct m1kbd_io io={this,read32,write32,nowUS,delayUS};
    e=m1kbd_hw_init(&hw,&board,&io);
    if (e) { releaseMaps(); return e; }
    opened=true;
    return M1KBD_OK;
}
int SFM1KeyboardHW::read32(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    SFM1KeyboardHW *self=static_cast<SFM1KeyboardHW *>(ctx);
    if (!out || r>=M1KBD_REGIONS || !self->maps[r] || (off&3) ||
        self->maps[r]->getSize()<4 || off>self->maps[r]->getSize()-4) return -1;
    volatile uint32_t *p=reinterpret_cast<volatile uint32_t *>(self->maps[r]->getVirtualAddress()+off);
    __asm__ volatile("dmb osh" ::: "memory");
    *out=*p;
    __asm__ volatile("dmb osh" ::: "memory");
    return 0;
}
int SFM1KeyboardHW::write32(void *ctx, unsigned r, uint32_t off, uint32_t value)
{
    SFM1KeyboardHW *self=static_cast<SFM1KeyboardHW *>(ctx);
    if (r>=M1KBD_REGIONS || !self->maps[r] || (off&3) ||
        self->maps[r]->getSize()<4 || off>self->maps[r]->getSize()-4) return -1;
    volatile uint32_t *p=reinterpret_cast<volatile uint32_t *>(self->maps[r]->getVirtualAddress()+off);
    __asm__ volatile("dmb osh" ::: "memory");
    *p=value;
    __asm__ volatile("dsb oshst" ::: "memory");
    return 0;
}
uint64_t SFM1KeyboardHW::clockUS()
{
    uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    return ns/1000;
}
uint64_t SFM1KeyboardHW::nowUS(void *) { return clockUS(); }
void SFM1KeyboardHW::delayUS(void *, uint32_t us)
{
    /* Reset waits can yield; FIFO/CS timing remains sub-millisecond busy wait. */
    if (us>=1000) {
        IOSleep(us/1000);
        us%=1000;
    }
    if (us) IODelay(us);
}
int SFM1KeyboardHW::start() { return opened?m1kbd_hw_start(&hw):M1KBD_STATE; }
int SFM1KeyboardHW::readPacket(uint8_t packet[M1KBD_PACKET_SIZE])
{ return opened?m1kbd_hw_read_packet(&hw,packet):M1KBD_STATE; }
int SFM1KeyboardHW::writePacket(const uint8_t packet[M1KBD_PACKET_SIZE])
{ return opened?m1kbd_hw_write_packet(&hw,packet):M1KBD_STATE; }
int SFM1KeyboardHW::closeAfterQuiesce()
{
    if (hw.busy) return M1KBD_STATE;
    int e=M1KBD_OK;
    if (opened) e=m1kbd_hw_stop(&hw);
    opened=false;
    /* No producer can enter after the caller's join. Cleanup errors remain
     * failures. These are MMIO maps, not DMA or exported RAM mappings. */
    releaseMaps();
    return e;
}
void SFM1KeyboardHW::releaseMaps()
{
    for (unsigned i=M1KBD_REGIONS;i>0;i--) {
        if (maps[i-1]) { maps[i-1]->release(); maps[i-1]=nullptr; }
        if (descriptors[i-1]) { descriptors[i-1]->release(); descriptors[i-1]=nullptr; }
    }
}
