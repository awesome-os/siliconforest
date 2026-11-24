/* SPDX-License-Identifier: MIT */
/* Copyright The Asahi Linux Contributors
 * Compact ADT validation/application adapted from m1n1 src/tunables.c.
 * See NOTICE for the full MIT permission and warranty notice.
 */
#ifndef SF_ATC_TUNABLES_H
#define SF_ATC_TUNABLES_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
static inline uint32_t sf_atc_le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int sf_atc_tunable_validate(const void *raw, size_t bytes, size_t span)
{
    if ((!raw && bytes) || bytes % 12 || span < 4) return EINVAL;
    const uint8_t *records = (const uint8_t *)raw;
    for (size_t i = 0; i < bytes; i += 12) {
        uint32_t location = sf_atc_le32(records + i), off = location & 0xffffff;
        if ((location >> 24) != 32 || (off & 3) || off > span - 4) return EINVAL;
    }
    return 0;
}
/* Observation plans distinguish a missing property from a present empty
 * common group. The existing common apply path accepts the latter; lane
 * calibration requires nonempty data. Keep this contract shared with the
 * native readback adapter so an empty optional table is not ENOENT. */
static inline int sf_atc_tunable_observation_validate(int present,const void *raw,
                                                      size_t bytes,size_t span,
                                                      int allow_empty)
{
    if(!present || (!allow_empty && !bytes))return ENOENT;
    return sf_atc_tunable_validate(raw,bytes,span);
}
/* Caller validates the entire group before applying any member. A masked
 * value cannot change unrelated bits, even if firmware has set bits outside
 * its mask. Callback addresses are offsets relative to the selected window. */
static inline int sf_atc_tunable_apply(const void *raw, size_t bytes, size_t span,
                                      uint32_t base, void *ctx,
                                      uint32_t (*read)(void *, uint32_t),
                                      void (*write)(void *, uint32_t, uint32_t))
{
    int ret = sf_atc_tunable_validate(raw, bytes, span);
    if (ret || !read || !write || span > UINT32_MAX || base > UINT32_MAX - span) return ret ? ret : EINVAL;
    const uint8_t *records = (const uint8_t *)raw;
    for (size_t i = 0; i < bytes; i += 12) {
        uint32_t off = base + (sf_atc_le32(records + i) & 0xffffff);
        uint32_t mask = sf_atc_le32(records + i + 4), value = sf_atc_le32(records + i + 8);
        write(ctx, off, (read(ctx, off) & ~mask) | (value & mask));
    }
    return 0;
}
#endif
