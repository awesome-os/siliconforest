/* SPDX-License-Identifier: GPL-2.0 */
/* Constants from drivers/thunderbolt/nhi.h, linux-aurora 6a08a08e.
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 * The pure DP-IN policy does not use an NHI hardware implementation here.
 */
#ifndef SF_TB_POLICY_NHI_H
#define SF_TB_POLICY_NHI_H
#include <linux/bits.h>
#define TB_HOST_DP_NOTIFY BIT(0)
#define TB_HOST_DP_HPD_ON_ACTIVATE BIT(1)
#define TB_HOST_DP_ACTIVE_BEFORE_DPRX BIT(2)
#define TB_HOST_DP_KEEP_DPRX_TIMEOUT BIT(3)
#define TB_HOST_DP_ADAPTER_QUIRKS BIT(4)
#define TB_HOST_DP_INITIAL_BW_GRANT BIT(5)
#endif
