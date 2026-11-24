// SPDX-License-Identifier: MIT
#include "sf_tb_pm.h"
#include <errno.h>
#include <string.h>
void sf_tb_pm_init(struct sf_tb_pm_state *v){memset(v,0,sizeof(*v));v->disabled=1;}
int sf_tb_pm_update(struct sf_tb_pm_state *v,enum sf_tb_pm_op op,int arg,bool published,uint64_t now)
{
 if(!v)return -EINVAL;
 switch(op){
 case SF_TB_PM_GET:case SF_TB_PM_GET_SYNC:case SF_TB_PM_RESUME_AND_GET:
  if(v->usage==UINT32_MAX)return -EOVERFLOW;
  ++v->usage;v->active=true;return 0;
 case SF_TB_PM_GET_NORESUME:if(v->usage==UINT32_MAX)return -EOVERFLOW;else ++v->usage;return 0;
 case SF_TB_PM_PUT:case SF_TB_PM_PUT_AUTOSUSPEND:case SF_TB_PM_PUT_NOIDLE:
  if(!v->usage)return -EINVAL;--v->usage;return 0;
 case SF_TB_PM_DISABLE:if(v->disabled==UINT32_MAX)return -EOVERFLOW;else ++v->disabled;return 0;
 case SF_TB_PM_ENABLE:if(!v->disabled)return -EINVAL;else --v->disabled;return 0;
 case SF_TB_PM_RESUME:case SF_TB_PM_SET_ACTIVE:v->active=true;return 0;
 case SF_TB_PM_SET_SUSPENDED:
  if(v->usage || !v->disabled || published)return -EBUSY;v->active=false;return 0;
 case SF_TB_PM_IS_SUSPENDED:return !v->active;
 case SF_TB_PM_ALLOW:v->allowed=true;return 0;
 case SF_TB_PM_NO_CALLBACKS:v->noCallbacks=true;return 0;
 case SF_TB_PM_LAST_BUSY:v->lastBusy=now;return 0;
 case SF_TB_PM_AUTOSUSPEND_DELAY:v->delay=arg;return 0;
 case SF_TB_PM_USE_AUTOSUSPEND:v->autosuspend=true;return 0;
 case SF_TB_PM_REQUEST_AUTOSUSPEND:return 0; // cable power pinned; no suspend reported
 case SF_TB_PM_WAKE_CAPABILITY:v->wakeCapable=arg;if(!arg)v->wakeEnabled=false;return 0;
 case SF_TB_PM_WAKE_POLICY:v->wakeCapable=arg;v->wakeEnabled=arg;return 0;
 default:return -EOPNOTSUPP;
 }
}
