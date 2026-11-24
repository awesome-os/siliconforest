/* SPDX-License-Identifier: MIT */
/*
 * SFGpuDT: builds the Linux-style GPU device-tree nodes for T8103 from Apple's
 * device tree, logs a summary, and (sfgpudt=2) publishes them in gIODTPlane.
 */
#ifndef SF_GPU_DT_H
#define SF_GPU_DT_H

#include <IOKit/IOService.h>

extern "C" {
#include "gpudt.h"
}

class SFGpuDT : public IOService
{
    OSDeclareDefaultStructors(SFGpuDT);

public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

private:
    bool publish(const struct gpudt_tree *tree, bool ofHasStringProps);
};

#endif
