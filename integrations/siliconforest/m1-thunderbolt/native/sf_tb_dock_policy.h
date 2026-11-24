/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_DOCK_POLICY_H
#define SF_TB_DOCK_POLICY_H
#include <stdint.h>
#include <stdbool.h>
/* Model/route allowlist only, NOT physical presence or authorization proof.
 * The actual CM must check current topology/power under its domain lock. */
static inline bool sf_tb_ts3_direct(uint32_t vendor,uint32_t device,uint64_t route)
{return vendor==0x003d && device==0x0011 && (route==1 || route==2);}
#endif
