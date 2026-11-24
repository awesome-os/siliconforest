/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CD321x register framing/status bits from Omacom linux-aurora 6a08a08e,
 * drivers/usb/typec/tipd/core.c and tps6598x.h:
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 * Copyright (C) The Asahi Linux Contributors
 *
 * This is the register transport, not the Type-C mux/role state machine.
 */
#ifndef SF_CD321X_H
#define SF_CD321X_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>

#define SF_TPS_MAX_LEN 64
#define SF_TPS_REG_VID 0x00
#define SF_TPS_REG_MODE 0x03
#define SF_TPS_REG_STATUS 0x1a
#define SF_TPS_REG_INTEL_VID_STATUS 0x59
#define SF_TPS_REG_DATA_STATUS 0x5f
#define SF_TPS_STATUS_PLUG_PRESENT (1u << 0)
#define SF_TPS_STATUS_UPSIDE_DOWN (1u << 4)
#define SF_TPS_STATUS_PORTROLE (1u << 5)
#define SF_TPS_STATUS_DATAROLE (1u << 6)
#define SF_TPS_DATA_USB2 (1u << 4)
#define SF_TPS_DATA_USB3 (1u << 5)
#define SF_TPS_DATA_USB_DEVICE (1u << 7)
#define SF_TPS_DATA_DP (1u << 8)
#define SF_TPS_DATA_TBT (1u << 16)
#define SF_CD321X_DATA_HPD (1u << 15)
#define SF_CD321X_DATA_USB4 (1u << 23)

/* Linux's I2C protocol reads len+1 bytes and validates the advertised length.
 * An advertised register size larger than the requested prefix is valid.
 * Leave the caller's destination unchanged on an incomplete/malformed read. */
static inline int sf_cd321x_unpack(const uint8_t *wire, size_t available,
                                  void *dst, size_t len)
{
    if (!wire || !dst || !len || len > SF_TPS_MAX_LEN)
        return EINVAL;
    if (available < len + 1 || wire[0] < len)
        return EIO;
    memcpy(dst, wire + 1, len);
    return 0;
}

static inline uint32_t sf_cd321x_le32(const uint8_t v[4])
{
    return (uint32_t)v[0] | (uint32_t)v[1] << 8 |
           (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24;
}

enum sf_cd321x_usb_role {
    SF_CD321X_USB_NONE, SF_CD321X_USB_HOST, SF_CD321X_USB_DEVICE
};

/* One immutable IORegistry OSData property ("port-state"), copied as a unit
 * by kernel clients. Separate diagnostic properties are not a snapshot ABI.
 * An error is unavailable state, not a physical disconnect. */
struct sf_cd321x_snapshot {
    uint32_t status, data_status, sample, error;
};
/* Prefix-compatible v2: publish all cable inputs in the SAME OSData. The
 * Intel register is packed: mode[0], attention[1:4], enter[5:6], device[7:8].
 * protocol_vdo contains the raw device mode (low16) and enter VDO (high16).
 * No unique identity, firmware bytes or independently sampled property here. */
#define SF_CD321X_TBT_VALID 1u
struct sf_cd321x_cable_snapshot {
    struct sf_cd321x_snapshot port;
    uint32_t protocol_vdo, valid_flags;
};
static inline uint32_t sf_cd321x_tbt_vdo(const uint8_t intel[9])
{
    return (uint32_t)intel[7] | (uint32_t)intel[8] << 8 |
           (uint32_t)intel[5] << 16 | (uint32_t)intel[6] << 24;
}
static inline int sf_cd321x_snapshot_decode(const void *bytes, size_t len,
                                           struct sf_cd321x_cable_snapshot *dst)
{
    if (!bytes || !dst || (len != sizeof(struct sf_cd321x_snapshot) && len != sizeof(*dst)))
        return EINVAL;
    struct sf_cd321x_cable_snapshot state = {{0,0,0,0},0,0};
    memcpy(&state, bytes, len);
    *dst = state;
    return 0;
}
/* apple.c's CABLE_INFO value for TBT, from the same TIPD inputs. Linux's
 * TBT_CABLE_10_AND_20GBPS value is 3, and TBT_ADAPTER is device-mode bit0.
 * This is a protocol value; it does not assert PHY/NHI readiness. */
static inline int sf_cd321x_tbt_cable_info(const struct sf_cd321x_cable_snapshot *s,
                                          uint32_t *info)
{
    if (!s || !info || s->port.error || !s->port.sample ||
        !(s->port.status & SF_TPS_STATUS_PLUG_PRESENT) ||
        !(s->port.data_status & SF_TPS_DATA_TBT) ||
        !(s->valid_flags & SF_CD321X_TBT_VALID)) return ENODEV;
    uint32_t d=s->port.data_status, v=1u | (1u<<10);
    if (s->port.status & SF_TPS_STATUS_UPSIDE_DOWN) v|=1u<<1;
    if (d & (1u<<22)) {
        v|=1u<<2;
        if (!(d & (1u<<20))) v|=1u<<3;
    }
    if (s->protocol_vdo & 1u) v|=1u<<9;
    if (((d>>25)&7u)==3u) v|=1u<<4;
    *info=v;
    return 0;
}

/* CD321x uses DATA_STATUS for the DWC3 role, distinct from its PD data role
 * in STATUS. See cd321x_update_work(): a TB-only cable does not start DWC3. */
static inline enum sf_cd321x_usb_role sf_cd321x_usb_role(uint32_t status, uint32_t data)
{
    if (!(status & SF_TPS_STATUS_PLUG_PRESENT) || !(data & (SF_TPS_DATA_USB2 | SF_TPS_DATA_USB3)))
        return SF_CD321X_USB_NONE;
    return data & SF_TPS_DATA_USB_DEVICE ? SF_CD321X_USB_DEVICE : SF_CD321X_USB_HOST;
}
#endif
