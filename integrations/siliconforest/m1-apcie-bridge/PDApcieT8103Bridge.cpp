/* Source-only, one-shot, read-only apcie,t8103 port-0 observation.
 * Kernel build of this adapter and any runtime use are unqualified. */
#include "PDApcieT8103Bridge.h"
#include "observe_layout.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/OSAtomic.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#if !defined(__arm64__) && !defined(__aarch64__)
#error PDApcieT8103Bridge requires the ARM64 J293 profile
#endif
#define super IOService
OSDefineMetaClassAndStructors(PDApcieT8103Bridge, IOService);
static volatile UInt32 gBridgeClaimed = 0;
extern "C" kern_return_t pd_apcie_bridge_stop(kmod_info_t *, void *) { return KERN_FAILURE; }

bool PDApcieT8103Bridge::init(OSDictionary *dict)
{
    properties = nullptr; attempted = false;
    bzero(descriptors, sizeof(descriptors)); bzero(maps, sizeof(maps));
    bzero(&board, sizeof(board)); bzero(&snapshot, sizeof(snapshot));
    return super::init(dict);
}

IOService *PDApcieT8103Bridge::probe(IOService *provider, SInt32 *score)
{
    int enabled = 0;
    if (!PE_parse_boot_argn("pdapciebridge", &enabled, sizeof(enabled)) || enabled != 1) return nullptr;
    // Never cleared: IOResources rematching must not repeat an observation.
    if (!OSCompareAndSwap(0, 1, &gBridgeClaimed)) return nullptr;
    return super::probe(provider, score);
}

int PDApcieT8103Bridge::property(void *ctx, const char *path, const char *key,
                                 const uint8_t **bytes, size_t *length)
{
    PDApcieT8103Bridge *self = static_cast<PDApcieT8103Bridge *>(ctx);
    if (!self->properties || self->properties->getCount() >= 128) return -1;
    IORegistryEntry *node = IORegistryEntry::fromPath(path, gIODTPlane);
    if (!node) return -1;
    OSObject *object = node->copyProperty(key); node->release();
    OSData *data = OSDynamicCast(OSData, object);
    OSData *copy = data && data->getLength() <= 32768
        ? OSData::withBytes(data->getBytesNoCopy(), data->getLength()) : nullptr;
    int result = -1;
    if (copy && self->properties->setObject(copy)) {
        *bytes = static_cast<const uint8_t *>(copy->getBytesNoCopy());
        *length = copy->getLength(); result = 0;
    }
    if (copy) copy->release();
    if (object) object->release();
    return result;
}

bool PDApcieT8103Bridge::mapRegion(unsigned r)
{
    // DART is validated by the frozen board profile but never mapped here.
    if (r != DLS_PMGR && r != DLS_RC && r != DLS_PHY && r != DLS_PORT) return false;
    if (maps[r]) return true;
    descriptors[r] = IOMemoryDescriptor::withPhysicalAddress(board.base[r], board.size[r], kIODirectionIn);
    if (!descriptors[r]) return false;
    maps[r] = descriptors[r]->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly);
    return maps[r] && maps[r]->getVirtualAddress() && maps[r]->getSize() >= board.size[r];
}

int PDApcieT8103Bridge::read32(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    PDApcieT8103Bridge *self = static_cast<PDApcieT8103Bridge *>(ctx);
    if (!out || !apo_read_allowed(r, off) || r >= DLS_REGIONS ||
        self->board.size[r] < 4 || off > self->board.size[r] - 4) return -1;
    // Lazy: a region is mapped only when the staged guard first reaches it.
    if (!self->mapRegion(r)) return -1;
    volatile const uint32_t *reg =
        reinterpret_cast<volatile const uint32_t *>(self->maps[r]->getVirtualAddress() + off);
    __asm__ volatile("dmb osh" ::: "memory");
    *out = *reg;
    __asm__ volatile("dmb osh" ::: "memory");
    return 0;
}

void PDApcieT8103Bridge::releaseMaps()
{
    for (unsigned i = DLS_REGIONS; i > 0; i--) {
        if (maps[i - 1]) { maps[i - 1]->release(); maps[i - 1] = nullptr; }
        if (descriptors[i - 1]) { descriptors[i - 1]->release(); descriptors[i - 1] = nullptr; }
    }
}

bool PDApcieT8103Bridge::start(IOService *provider)
{
    int enabled = 0;
    if (attempted || !PE_parse_boot_argn("pdapciebridge", &enabled, sizeof(enabled)) || enabled != 1)
        return false;
    attempted = true;
    if (!super::start(provider)) return false;
    properties = OSArray::withCapacity(64);
    // Exact J293 profile from the frozen m1-dart-survey board check, then the
    // port-0 binding: pci-bridge0 is the BCM4378 WLAN bridge, no other ports.
    int result = properties ? dls_validate_board(property, this, &board) : DLS_IO;
    if (result == DLS_SAMPLED) result = apo_validate_bridge(property, this);
    else result = result == DLS_IO ? APO_IO : APO_BOARD;
    if (properties) { properties->release(); properties = nullptr; }
    if (result == APO_OBSERVED) {
        const struct dls_io io = {this, read32};
        result = apo_observe(&board, &io, &snapshot);
    } else {
        snapshot.version = 1; snapshot.result = result; snapshot.last_region = DLS_REGIONS;
    }
    // Synchronous start owns every map; no timer, interrupt or callback remains.
    releaseMaps();
    OSData *data = OSData::withBytes(&snapshot, sizeof(snapshot));
    bool providerRecorded = data && provider && provider->setProperty("PDApcieBridgeObservationV1", data);
    bool instanceRecorded = data && setProperty("PDApcieBridgeObservationV1", data);
    IOLog("PDApcieT8103Bridge: result=%d stage=%u reads=%u valid=0x%x phy_ctrl=0x%08x phyif=0x%08x "
          "appclk=0x%08x perst_reset=0x%08x status=0x%08x linksts=0x%08x provider_preserved=%u "
          "instance_recorded=%u (read-only port0 observation; no PCIe initialization)\n",
          result, snapshot.stage_reached, snapshot.reads_attempted, snapshot.valid_mask,
          snapshot.phy_ctrl, snapshot.rc_phyif_ctrl, snapshot.port0_appclk, snapshot.port0_reset,
          snapshot.port0_status, snapshot.port0_linksts, providerRecorded ? 1u : 0u,
          instanceRecorded ? 1u : 0u);
    if (data) data->release();
    // No registerService and no nubs: nothing may match or use this skeleton.
    if (result != APO_OBSERVED || !providerRecorded || !instanceRecorded) { super::stop(provider); return false; }
    return true;
}

void PDApcieT8103Bridge::free()
{
    releaseMaps();
    if (properties) { properties->release(); properties = nullptr; }
    super::free();
}
