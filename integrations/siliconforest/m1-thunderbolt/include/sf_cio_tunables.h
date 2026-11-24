/* SPDX-License-Identifier: MIT */
/* Copyright The Asahi Linux Contributors. See NOTICE. */
#ifndef SF_CIO_TUNABLES_H
#define SF_CIO_TUNABLES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum sf_cio_window { SF_CIO_RC,SF_CIO_NHI,SF_CIO_PCIE_ADAPTER };
struct sf_cio_tunable_entry { const void *raw;size_t bytes; };
struct sf_cio_tunables { struct sf_cio_tunable_entry entries[11];bool checked; };
struct sf_cio_tunable_io {
    void *ctx;
    const void *(*getprop)(void *,const char *path,const char *name,size_t *);
    uint32_t (*read)(void *,enum sf_cio_window,uint32_t);
    void (*write)(void *,enum sf_cio_window,uint32_t,uint32_t);
};
/* Runtime ADT 24-byte local records, not ATC's 12-byte compact format.
 * Native getter must retain immutable bytes through all applications.
 * T8103/J293 port1 only. All eleven groups are preflighted before any write.
 * Return 0 or positive errno; external ACIO/PHY serialization required. */
int sf_cio_tunables_load(struct sf_cio_tunables *,const struct sf_cio_tunable_io *);
int sf_cio_tunables_apply(const struct sf_cio_tunables *,const struct sf_cio_tunable_io *,enum sf_cio_window);
struct sf_cio_tunable_observation { uint32_t window,offset,value; };
/* Read only the validated immutable tuning address list, never firmware/SRAM.
 * All addresses/counts preflight before any read; count=0 on failure.
 * Native caller must establish actual power/lease/serialization. This helper
 * and its checked bit do not supply that hardware or lifetime proof. */
int sf_cio_tunables_observe(const struct sf_cio_tunables *,const struct sf_cio_tunable_io *,
 struct sf_cio_tunable_observation *,size_t capacity,size_t *count);
#ifdef __cplusplus
}
#endif
#endif
