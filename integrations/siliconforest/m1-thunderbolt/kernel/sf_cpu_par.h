/* SPDX-License-Identifier: MIT */
/* Read the translation attributes of our own retained, wired kernel backing.
 * Same AT S1E1R sequence as frozen XNU mmu_kvtop; preserve PAR and DAIF.
 * No page-table/cache writes. PAR bit0 is failure; ATTR is bits63:56.
 */
#ifndef SF_CPU_PAR_H
#define SF_CPU_PAR_H
#include <stdint.h>
static inline uint64_t sf_cpu_par(const void *address)
{
    uint64_t interrupt_state, prior_par, result;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #0xf\n"
        "mrs %1, par_el1\n"
        "at s1e1r, %3\n"
        "isb\n"
        "mrs %2, par_el1\n"
        "msr par_el1, %1\n"
        "isb\n"
        "msr daif, %0\n"
        : "=&r"(interrupt_state), "=&r"(prior_par), "=&r"(result)
        : "r"(address) : "memory");
    return result;
}
#endif
