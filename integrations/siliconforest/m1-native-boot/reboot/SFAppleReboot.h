/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Restart and power-off of an Apple T8103 machine through the SMC, and the PMU's boot
 * state bytes, as Linux macsmc-reboot does both. Part of SFCpuPower.kext, which owns the
 * only long-lived SMC session of this system (its thermal thread). See SFAppleReboot.cpp.
 */
#ifndef _SFAPPLEREBOOT_H
#define _SFAPPLEREBOOT_H

class SFSmcSession;

/* Once, from SFCpuPower::start() on T8103, before the thermal thread exists. */
void SFAppleRebootStart(void);

/*
 * The thermal thread brackets every use of its SMC session with these. A restart or
 * power-off takes the bracket's lock and never gives it back: the thread then waits
 * for ever at its next poll and the session is the halting thread's.
 */
void SFAppleRebootSmcBegin(SFSmcSession *session);
void SFAppleRebootSmcEnd(void);

#endif
