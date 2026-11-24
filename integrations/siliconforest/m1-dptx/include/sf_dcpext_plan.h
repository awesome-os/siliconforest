/* SPDX-License-Identifier: MIT */
/* T8103/J293 external DCP resources, following m1n1 and Linux's t8103 DT.
 * Copyright The Asahi Linux Contributors (m1n1 conversion conventions).
 */
#ifndef SF_DCPEXT_PLAN_H
#define SF_DCPEXT_PLAN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_dcpext_adt {
    void *ctx;
    const void *(*property)(void *,const char *path,const char *name,size_t *bytes);
};
struct sf_dcpext_region { uint64_t phys,bytes; };
struct sf_dcpext_plan {
    struct sf_dcpext_region coproc,disp[5],dcp_dart,disp_dart,piodma_dart;
    struct sf_dcpext_region mailbox,bw_scratch,bw_doorbell,firmware[2]; // ADT73,74
    uint32_t mailbox_irq[4],firmware_version[3],firmware_compat[3];
    unsigned typec_port,dptx_phy,remote_core,stream_count;
};
/* A side-effect-free native resource plan. No mapping, power, DMA, RTKit,
 * fabricated EDID, output publication or guessed firmware IOVA. Failure
 * leaves out unchanged. Only J293/T8103 port1 is qualified. */
int sf_dcpext_plan_t8103(const struct sf_dcpext_adt *,uint32_t chip,
                        const uint32_t version[3],struct sf_dcpext_plan *out);
#ifdef __cplusplus
}
#endif
#endif
