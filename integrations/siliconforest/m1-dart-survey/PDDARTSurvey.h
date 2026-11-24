/* Original diagnostic adapter. No IOMapper or device ownership interface. */
#ifndef PD_DART_SURVEY_H
#define PD_DART_SURVEY_H
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSArray.h>
#include "survey.h"
class PDDARTSurvey : public IOService {
    OSDeclareDefaultStructors(PDDARTSurvey);
    OSArray *properties;
    IOMemoryDescriptor *descriptors[DLS_REGIONS];
    IOMemoryMap *maps[DLS_REGIONS];
    struct dls_board board;
    struct dls_snapshot snapshot;
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
