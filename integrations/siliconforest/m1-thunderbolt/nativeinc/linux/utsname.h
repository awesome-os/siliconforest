/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_UTSNAME_H
#define SF_TB_UTSNAME_H
#include <uapi/linux/utsname.h>
/* Required native OS-info snapshot; no fabricated hostname implementation. */
struct new_utsname *sf_tb_utsname(void);
#define utsname sf_tb_utsname
#endif
