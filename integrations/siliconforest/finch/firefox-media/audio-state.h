/* SPDX-License-Identifier: MIT */
#ifndef SF_AUDIO_STATE_H
#define SF_AUDIO_STATE_H
#include <stdint.h>
struct sf_pcm_state {
    uint64_t calls,frames,samples,errors,bad_buffers,nonfinite;
    uint64_t first_host,last_host,max_gap;
    double square_sum,peak;
};
/* Diagnostic only: caller MUST stop every registered IOProc from a non-render
 * thread before reading/resetting. PD Stop waits for the current cycle.
 * Contexts stay allocated until process exit; never unload the trace image. */
int SFHALPCMRead(unsigned index,struct sf_pcm_state *out,int reset);
#endif
