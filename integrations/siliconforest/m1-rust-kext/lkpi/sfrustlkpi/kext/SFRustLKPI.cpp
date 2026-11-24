// SPDX-License-Identifier: GPL-2.0
/*
 * SFRustLKPI: the IOKit face of the kext. Matches IOResources, initialises the fbsdkpi
 * layer and runs the Rust `kernel`-crate self-test once (glue.c, rust/lib.rs).
 * Replaces fbsdkpi/xnu/SFDRMPlatformDevice.cpp, which would want a device-tree node.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>

#include "sf_fbsd_os.h"
#include "sf_fbsd_xnu.h"

extern "C" int sf_rustlkpi_start(void);

class SFRustLKPI : public IOService
{
    OSDeclareDefaultStructors(SFRustLKPI)

public:
    bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(SFRustLKPI, IOService)

bool SFRustLKPI::start(IOService *provider)
{
    if (!IOService::start(provider))
        return false;

    IOLog("SFRustLKPI: start\n");
    sfos_xnu_init();
    int failures = sf_rustlkpi_start();
    if (failures == 0)
        IOLog("SFRustLKPI: ALL OK\n");
    else
        IOLog("SFRustLKPI: FAILED (%d)\n", failures);
    setProperty("SFRustLKPIResult", failures == 0 ? "ok" : "failed");
    registerService();
    return true;
}
