/* SPDX-License-Identifier: MIT */
/* Real production logical PM policy, with supplied publication/time inputs.
 * Host only: these assertions establish no actual controller power state. */
#include "sf_tb_pm.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static int update(struct sf_tb_pm_state *p,enum sf_tb_pm_op op,int arg,bool published)
{return sf_tb_pm_update(p,op,arg,published,12345);}
int main(void)
{
 struct sf_tb_pm_state p;sf_tb_pm_init(&p);
 // The upstream not-yet-published XDomain failure sequence must balance.
 assert(!update(&p,SF_TB_PM_SET_ACTIVE,0,false));
 assert(!update(&p,SF_TB_PM_GET_NORESUME,0,false));
 assert(!update(&p,SF_TB_PM_ENABLE,0,false));
 assert(update(&p,SF_TB_PM_SET_SUSPENDED,0,false)==-EBUSY);
 assert(!update(&p,SF_TB_PM_DISABLE,0,false));
 assert(!update(&p,SF_TB_PM_PUT_NOIDLE,0,false));
 assert(!update(&p,SF_TB_PM_SET_SUSPENDED,0,false));
 assert(update(&p,SF_TB_PM_IS_SUSPENDED,0,false)==1);
 // A published router stays powered across autosuspend hints and balanced
 // concurrent-user sequences; forgetting metadata is not a drain proof.
 assert(!update(&p,SF_TB_PM_SET_ACTIVE,0,true));
 assert(!update(&p,SF_TB_PM_ENABLE,0,true));
 assert(!update(&p,SF_TB_PM_USE_AUTOSUSPEND,0,true));
 assert(!update(&p,SF_TB_PM_AUTOSUSPEND_DELAY,15000,true));
 for(unsigned i=0;i<10000;++i){
  assert(!update(&p,SF_TB_PM_GET_SYNC,0,true));
  assert(!update(&p,SF_TB_PM_RESUME_AND_GET,0,true));
  assert(p.usage==2 && !update(&p,SF_TB_PM_IS_SUSPENDED,0,true));
  assert(!update(&p,SF_TB_PM_LAST_BUSY,0,true) && p.lastBusy==12345);
  assert(!update(&p,SF_TB_PM_PUT_AUTOSUSPEND,0,true));
  assert(!update(&p,SF_TB_PM_PUT,0,true));
  assert(!update(&p,SF_TB_PM_REQUEST_AUTOSUSPEND,0,true));
  assert(p.active && !p.usage && !update(&p,SF_TB_PM_IS_SUSPENDED,0,true));
 }
 assert(!update(&p,SF_TB_PM_DISABLE,0,true));
 assert(update(&p,SF_TB_PM_SET_SUSPENDED,0,true)==-EBUSY && p.active);
 assert(!update(&p,SF_TB_PM_SET_SUSPENDED,0,false) && !p.active);
 assert(update(&p,SF_TB_PM_PUT,0,false)==-EINVAL && p.usage==0);
 assert(!update(&p,SF_TB_PM_ENABLE,0,false));
 assert(update(&p,SF_TB_PM_ENABLE,0,false)==-EINVAL && p.disabled==0);
 p.usage=UINT32_MAX;assert(update(&p,SF_TB_PM_GET,0,false)==-EOVERFLOW && p.usage==UINT32_MAX);
 p.disabled=UINT32_MAX;assert(update(&p,SF_TB_PM_DISABLE,0,false)==-EOVERFLOW && p.disabled==UINT32_MAX);
 assert(!update(&p,SF_TB_PM_WAKE_POLICY,1,false) && p.wakeCapable && p.wakeEnabled);
 assert(!update(&p,SF_TB_PM_WAKE_CAPABILITY,0,false) && !p.wakeCapable && !p.wakeEnabled);
 assert(!update(&p,SF_TB_PM_WAKE_CAPABILITY,1,false) && p.wakeCapable && !p.wakeEnabled);
 assert(update(&p,(enum sf_tb_pm_op)99,0,false)==-EOPNOTSUPP);
 puts("native cable-power-held PM: upstream failed-XDomain cleanup, 10000 balanced router sequences, publication/usage/disable refusal, overflow and wake policy PASS (host only)");
}
