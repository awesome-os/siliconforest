/* SPDX-License-Identifier: MIT */
#ifndef _HOST_OS_H
#define _HOST_OS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
extern uint8_t host_code[0xc000], host_mbox[0x4000], host_ctrl[0x10000];
void host_set_log(FILE *f, int verbose);
unsigned host_live_allocs(void);
size_t host_peak_bytes(void);
void host_fail_next_job(void);
#endif
