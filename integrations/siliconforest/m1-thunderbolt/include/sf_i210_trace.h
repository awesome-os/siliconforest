/* SPDX-License-Identifier: MIT */
/* Saturating, nonblocking diagnostic budget; no timer or retry worker. */
#ifndef SF_I210_TRACE_H
#define SF_I210_TRACE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
void sfos_log(const char *);
void sf_i210_dma_trace(void);
#ifdef __cplusplus
}
#endif
/* FreeBSD printf goes through debug-only dprintf in this shim. Keep this
 * already-budgeted diagnostic independent of that global boot argument. */
static inline void sf_i210_trace_log(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    sfos_log(text);
}
static inline bool sf_i210_trace_take(uint32_t *budget)
{
    uint32_t value = __atomic_load_n(budget, __ATOMIC_RELAXED);
    for (unsigned attempt = 0; value < 8 && attempt < 8; ++attempt)
        if (__atomic_compare_exchange_n(budget, &value, value + 1, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return true;
    return false;
}
#endif
