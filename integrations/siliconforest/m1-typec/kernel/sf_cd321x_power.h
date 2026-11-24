/* SPDX-License-Identifier: GPL-2.0 */
/* CD321x S0/SSPS sequence from Omacom linux-aurora 6a08a08e,
 * drivers/usb/typec/tipd/core.c: cd321x_switch_power_state and exec_cmd_tmo.
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 * Copyright (C) The Asahi Linux Contributors
 * Native caller owns the port's command mailbox. Never called for port 0.
 */
#ifndef SF_CD321X_POWER_H
#define SF_CD321X_POWER_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/errno.h>
struct sf_cd321x_power_io {
    void *ctx;
    int (*read)(void *,uint8_t,void *,size_t);
    int (*write)(void *,uint8_t,const void *,size_t);
    uint64_t (*now_ms)(void *);
    void (*sleep_ms)(void *,unsigned);
};
struct sf_cd321x_power_result { uint8_t before,after;bool command_issued;unsigned polls; };
static inline uint32_t sf_cd321x_cmd_le32(const uint8_t p[4])
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
/* Positive errno convention. Zero requires a readback of S0; no periodic
 * retries. Already-S0 is read-only. A busy foreign command is never replaced.
 * Deadline and iteration bounds both apply; native waits sleep the thread. */
static inline int sf_cd321x_ensure_s0(const struct sf_cd321x_power_io *io,
                                     struct sf_cd321x_power_result *out)
{
    if(!io || !out || !io->read || !io->write || !io->now_ms || !io->sleep_ms)return EINVAL;
    *out=(struct sf_cd321x_power_result){0xff,0xff,false,0};
    int ret=io->read(io->ctx,0x20,&out->before,1);if(ret)return ret;
    out->after=out->before;if(!out->before)return 0;
    uint8_t cmd[4];ret=io->read(io->ctx,0x08,cmd,4);if(ret)return ret;
    uint32_t value=sf_cd321x_cmd_le32(cmd);
    if(value && value!=0x444d4321u)return EBUSY; /* !CMD is a completed invalid command. */
    const uint8_t s0=0;ret=io->write(io->ctx,0x09,&s0,1);if(ret)return ret;
    const uint8_t ssps[4]={'S','S','P','S'};
    /* Once attempted, a failed transport does not prove firmware saw nothing. */
    out->command_issued=true;ret=io->write(io->ctx,0x08,ssps,4);if(ret)return ret;
    uint64_t start=io->now_ms(io->ctx);
    for(;;){
        ret=io->read(io->ctx,0x08,cmd,4);if(ret)return ret;
        ++out->polls;value=sf_cd321x_cmd_le32(cmd);
        if(value==0x444d4321u)return EINVAL;
        if(io->now_ms(io->ctx)-start>=1000 || out->polls>1000)return ETIMEDOUT;
        if(!value)break;
        io->sleep_ms(io->ctx,1);
    }
    uint8_t result;ret=io->read(io->ctx,0x09,&result,1);if(ret)return ret;
    if(result==1)return ETIMEDOUT;
    if(result==3)return EPERM;
    ret=io->read(io->ctx,0x20,&out->after,1);if(ret)return ret;
    return out->after?EIO:0;
}
#endif
