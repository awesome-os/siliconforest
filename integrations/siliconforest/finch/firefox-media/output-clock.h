/* SPDX-License-Identifier: MIT */
/* Pure timestamp arithmetic. Not a hardware/acoustic latency estimator. */
#ifndef SF_OUTPUT_CLOCK_H
#define SF_OUTPUT_CLOCK_H
#include <stdint.h>
static inline uint64_t sf_audio_callback_host(uint64_t audible, unsigned valid,
        uint64_t fixed_ticks,double client_offset,double ticks_per_client_frame,uint64_t fallback)
{
    if(!valid||audible<fixed_ticks||!(client_offset>=0)||!(ticks_per_client_frame>0))return fallback;
    double extra=client_offset*ticks_per_client_frame;
    if(!(extra>=0&&extra<9223372036854775808.0))return fallback;
    uint64_t base=audible-fixed_ticks,offset=(uint64_t)extra;
    if(offset>UINT64_MAX-base)return fallback;
    return base+offset;
}
#endif
