/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal polled Apple SMC (RTKit over the ASC mailbox) client for SiliconForest, used by
 * the T8103 PCIe bridge to power the Wi-Fi/Bluetooth radio (SMC GPIO 13, key "gP0d").
 * See SFAppleSMC.cpp for provenance (m1n1 src/asc.c, src/rtkit.c, src/smc.c and
 * proxyclient m1n1/fw/smc.py, MIT).
 */
#ifndef _SFAPPLESMC_H
#define _SFAPPLESMC_H

#include <IOKit/IOReturn.h>
#include <IOKit/IOTypes.h>

/* Options (the sfapciesmc boot argument, see SFApcieT8103Link.cpp). */
enum {
    kSFAppleSMCEnable      = 1u << 0, /* 0: no SMC access at all (handled by the caller) */
    kSFAppleSMCAlwaysWrite = 1u << 1, /* write the key even when it already reads as enabled */
    kSFAppleSMCKDebugStart = 1u << 2, /* also send the kdebug endpoint start message, as the
                                       * m1n1 Python client does (the m1n1 C client does not) */
};

/* 'gP0d': SMC GPIO 13, the "pwren" pin of the J293 radio. Values as written by m1n1. */
#define SF_SMC_KEY_RADIO_POWER 0x67503064u
#define SF_SMC_RADIO_POWER_ON  0x00800001u
#define SF_SMC_RADIO_POWER_OFF 0x00800000u

/*
 * Power the radio: boot/wake the SMC's RTKit, start the SMC endpoint, read gP0d, write
 * gP0d = 0x00800001 unless it already reads as enabled (bit 0 set), quiesce the SMC
 * again (AP and IOP power state 0x10, as m1n1's stop() does) and, after a write, wait
 * 100 ms. Every step is logged with the prefix "SFAppleSMC:" and every wait is bounded.
 *
 * *wrote (optional) is set when the key was written. May be called again later in the
 * same boot; each call is a complete start/stop session. Concurrent calls get
 * kIOReturnBusy. May sleep.
 */
IOReturn SFAppleSMCRadioPowerOn(UInt32 options, bool *wrote);

/*
 * The reverse: gP0d = 0x00800000. UNVERIFIED: the value has never been written on hardware.
 * Only SFApcieT8103PortShutdown() calls this, and nothing calls that during a normal boot.
 */
IOReturn SFAppleSMCRadioPowerOff(UInt32 options);

/*
 * One complete session around a single key access, for other callers and experiments.
 * key is the big-endian FourCC (e.g. 'gP0d' = 0x67503064); len is 1..255.
 * *smcResult (optional) receives the SMC result byte (0 = success) when the SMC answered.
 */
IOReturn SFAppleSMCReadKey(UInt32 options, UInt32 key, void *buf, UInt32 len, UInt32 *smcResult);
IOReturn SFAppleSMCWriteKey(UInt32 options, UInt32 key, const void *buf, UInt32 len, UInt32 *smcResult);

#endif
