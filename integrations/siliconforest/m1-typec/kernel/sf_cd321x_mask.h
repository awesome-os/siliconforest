/* SPDX-License-Identifier: GPL-2.0 */
/* tipd_init()/tipd_cd321x_data from Omacom linux-aurora 6a08a08e,
 * drivers/usb/typec/tipd/core.c and tps6598x.h. Copyright Intel Corporation
 * and The Asahi Linux Contributors. CD3217 on T8103 only; not SN201202x.
 * Startup only, after APP/S0 validation and before publishing cable state.
 * Native delivery still uses its existing1Hz sampler, not a GPIO/AIC enable.
 */
#ifndef SF_CD321X_MASK_H
#define SF_CD321X_MASK_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <sys/errno.h>
struct sf_cd321x_mask_result {
 uint8_t before[8],after[8];
 bool before_valid,after_valid,write_attempted;
};
static inline int sf_cd321x_ensure_mask(void *ctx,
 int (*read)(void *,uint8_t,void *,size_t),
 int (*write)(void *,uint8_t,const void *,size_t),
 struct sf_cd321x_mask_result *out)
{
 if(!ctx || !read || !write || !out)return EINVAL;
 memset(out,0,sizeof(*out));
 /* Plug bit1, power status bit9, data status bit10; exact Linux mask.
  * STATUS_UPDATE bit8 is deliberately absent from tipd_cd321x_data.
  */
 const uint8_t wanted[8]={2,6,0,0,0,0,0,0};
 int ret=read(ctx,0x16,out->before,8);if(ret)return ret;
 out->before_valid=true;
 if(!memcmp(out->before,wanted,8)){
  memcpy(out->after,out->before,8);out->after_valid=true;return 0;
 }
 out->write_attempted=true;ret=write(ctx,0x16,wanted,8);if(ret)return ret;
 ret=read(ctx,0x16,out->after,8);if(ret)return ret;
 out->after_valid=true;
 return memcmp(out->after,wanted,8)?EIO:0;
}
#endif
