#ifndef PD_GPU_PREFLIGHT_H
#define PD_GPU_PREFLIGHT_H
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSArray.h>
#include "preflight.h"
class PDGPUPreflight : public IOService {
    OSDeclareDefaultStructors(PDGPUPreflight);
    OSArray *properties;
    IOMemoryDescriptor *descriptor;
    IOMemoryMap *mapping;
    struct gpf_board board;
    struct gpf_snapshot snapshot;
    bool attempted;
    static int property(void *, const char *, const char *, const uint8_t **, size_t *);
    static int read32(void *, uint32_t, uint32_t *);
    void releaseMap();
public:
    bool init(OSDictionary *) override;
    IOService *probe(IOService *, SInt32 *) override;
    bool start(IOService *) override;
    void free() override;
};
#endif
