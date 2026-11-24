// SPDX-License-Identifier: GPL-2.0
/*
 * SFRustHello: the C++ IOKit shell around a Rust static library (rust/sfrusthello).
 * Matches IOResources, and start() runs the Rust self-test once and logs what it returned.
 * The Rust side imports sf_rust_log and sf_rust_panic from here.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <kern/debug.h>

#define SF_RUST_HELLO_LOG(...) IOLog(__VA_ARGS__)
#include "sf_rust_hello.h"

class SFRustHello : public IOService
{
    OSDeclareDefaultStructors(SFRustHello)

public:
    bool start(IOService *provider) override;
};

#define super IOService
OSDefineMetaClassAndStructors(SFRustHello, IOService)

extern "C" void
sf_rust_log(const char *text, unsigned long len)
{
    IOLog("SFRustHello: %.*s\n", (int)len, text);
}

extern "C" void
sf_rust_panic(const char *text, unsigned long len)
{
    panic("SFRustHello: rust panic: %.*s", (int)len, text);
}

bool SFRustHello::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    IOLog("SFRustHello: start\n");
    bool ok = sf_rust_hello_run();
    setProperty("SFRustHelloResult", ok ? "ok" : "failed");
    registerService();
    return true;
}
