/* Original boot-only private SELF-REPORT service, no userclient. */
#ifndef PD_BCM_PRIVATE_PROBE_H
#define PD_BCM_PRIVATE_PROBE_H
#include <IOKit/IOService.h>
#include "probe.h"
class PDBCMPrivateProbe : public IOService {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
    /* The pinned Kernel.framework macro itself contains empty declarations. */
    OSDeclareDefaultStructors(PDBCMPrivateProbe)
#pragma clang diagnostic pop
    bcp_record record;
    bcp_workspace *workspace;
    bool publish(IOService *);
public:
    bool init(OSDictionary *) override;
    bool start(IOService *) override;
    bool terminate(IOOptionBits options=0) override;
    void stop(IOService *) override;
};
#endif
