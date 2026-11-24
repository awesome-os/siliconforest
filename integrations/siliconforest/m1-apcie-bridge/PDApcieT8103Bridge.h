/* Original read-only apcie,t8103 port-0 bridge skeleton. Not an IOPCIBridge yet:
 * it publishes no nubs, configures nothing and owns no device. */
#ifndef PD_APCIE_T8103_BRIDGE_H
#define PD_APCIE_T8103_BRIDGE_H
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSArray.h>
#include "observe.h"
class PDApcieT8103Bridge : public IOService {
    OSDeclareDefaultStructors(PDApcieT8103Bridge);
    OSArray *properties;
    IOMemoryDescriptor *descriptors[DLS_REGIONS];
    IOMemoryMap *maps[DLS_REGIONS];
    struct dls_board board;
    struct apo_snapshot snapshot;
    bool attempted;
    static int property(void *, const char *, const char *, const uint8_t **, size_t *);
    static int read32(void *, unsigned, uint32_t, uint32_t *);
    bool mapRegion(unsigned);
    void releaseMaps();
public:
    bool init(OSDictionary *) override;
    IOService *probe(IOService *, SInt32 *) override;
    bool start(IOService *) override;
    void free() override;
};
#endif
