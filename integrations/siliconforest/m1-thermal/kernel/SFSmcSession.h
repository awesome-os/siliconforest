/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A polled Apple SMC client that stays open (RTKit over the ASC mailbox), for sensor
 * polling. See SFSmcSession.cpp for provenance (../../m1-apcie-host/SFAppleSMC.cpp; m1n1
 * src/asc.c, src/rtkit.c, src/smc.c and proxyclient m1n1/fw/smc.py, MIT).
 *
 * Not thread safe: one thread owns a session. It must not be open while the PCIe
 * bridge's SFAppleSMCRadioPowerOn() session runs (that one quiesces the SMC when it is
 * done); SFCpuPower opens it only after the bridge has started.
 */
#ifndef _SFSMCSESSION_H
#define _SFSMCSESSION_H

#include <IOKit/IOReturn.h>
#include <IOKit/IOTypes.h>

/* The step-by-step log ("SFSmc: ..."); on for the first open, see SFCpuPower. */
extern bool gSFSmcVerbose;

/* Big-endian FourCC of a key or type, e.g. SF_SMC_KEY('T','p','0','1'). */
#define SF_SMC_KEY(a, b, c, d) (((UInt32)(a) << 24) | ((UInt32)(b) << 16) | ((UInt32)(c) << 8) | (UInt32)(d))

class SFSmcSession
{
    struct Impl;
    Impl *impl;
    bool isOpen;

public:
    SFSmcSession();
    ~SFSmcSession();

    /* Boot/wake the SMC's RTKit, start the SMC endpoint, take the shared memory. May sleep. */
    IOReturn open();
    /* Quiesce the SMC (AP and IOP power state 0x10) unless the session failed. */
    void close();
    /* The SMC crashed or the mailbox stopped answering: close, and open again later. */
    bool failed() const;

    /* len is 1..255; values are in the SMC's byte order (little endian for numbers). */
    IOReturn readKey(UInt32 key, void *buf, UInt32 len, UInt32 *smcResult);
    IOReturn writeKey(UInt32 key, const void *buf, UInt32 len, UInt32 *smcResult);
    IOReturn keyInfo(UInt32 key, UInt8 *length, UInt32 *type, UInt8 *flags);
    /* The key at a position of the SMC's key table (positions 0 .. "#KEY" - 1). */
    IOReturn keyByIndex(UInt32 index, UInt32 *key);
};

#endif
