/* SPDX-License-Identifier: GPL-2.0 */
/* Read-only CD321x registers from pinned Linux tipd/core.c.
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 * Copyright (C) The Asahi Linux Contributors
 * Native caller serializes the real port1 transport. No IRQ acknowledge,
 * mask/role/mode write, task, firmware operation, retry or timer here.
 */
#ifndef SF_CD321X_CONTROL_H
#define SF_CD321X_CONTROL_H
#include <stdint.h>
#include <stddef.h>
#include <sys/errno.h>
struct sf_cd321x_control {
 uint8_t event1[8],mask1[8],power,status[4],data[4];
};
typedef int (*sf_cd321x_control_reader)(void *,uint8_t,void *,size_t);
static inline int sf_cd321x_control_observe(void *ctx,sf_cd321x_control_reader read,
 struct sf_cd321x_control *out)
{
 if(!ctx || !read || !out)return EINVAL;
 struct sf_cd321x_control value;
 int ret=read(ctx,0x14,value.event1,8);if(ret)return ret;
 ret=read(ctx,0x16,value.mask1,8);if(ret)return ret;
 ret=read(ctx,0x20,&value.power,1);if(ret)return ret;
 ret=read(ctx,0x1a,value.status,4);if(ret)return ret;
 ret=read(ctx,0x5f,value.data,4);if(ret)return ret;
 *out=value;return 0;
}
#endif
