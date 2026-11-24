/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_PM_H
#define SF_TB_PM_H
#include "sf_tb_native_abi.h"
#include <stdint.h>
/* Logical device state while the native owner holds the actual cable power
 * lease. Caller serializes, checks real power first, and supplies publication
 * state/time. This never powers hardware or acknowledges a physical suspend. */
struct sf_tb_pm_state {
 uint32_t usage,disabled;
 int delay;uint64_t lastBusy;
 bool active,allowed,noCallbacks,autosuspend,wakeCapable,wakeEnabled;
};
#ifdef __cplusplus
extern "C" {
#endif
void sf_tb_pm_init(struct sf_tb_pm_state *);
int sf_tb_pm_update(struct sf_tb_pm_state *,enum sf_tb_pm_op,int,bool published,uint64_t now);
#ifdef __cplusplus
}
#endif
#endif
