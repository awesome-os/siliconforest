/* SPDX-License-Identifier: GPL-2.0 */
/* HOST TEST ONLY: minimal topology fixtures for tb_port_is_apple_host_dpin,
 * whose body is copied from Linux drivers/thunderbolt/tb.h (6a08a08e).
 * Thunderbolt driver: Copyright (c) 2014 Andreas Noever;
 * Copyright (C) 2018 Intel Corporation. No fake MMIO/NHI is provided.
 */
#include "nhi.h"
#include <linux/types.h>
struct tb_nhi_ops { int unused; };
struct tb_nhi { struct tb_nhi_ops *ops; unsigned long host_dp_policy; };
struct tb { struct tb_nhi *nhi; };
struct tb_switch { struct tb *tb; struct { u32 route_lo, route_hi; } config; };
#define TB_TYPE_DP_HDMI_IN 0xe0101
struct tb_port { struct tb_switch *sw; struct { u32 type; } config; };
static inline u64 tb_route(const struct tb_switch *sw) { return ((u64)sw->config.route_hi<<32)|sw->config.route_lo; }
static inline bool tb_port_is_dpin(const struct tb_port *port) { return port->config.type==TB_TYPE_DP_HDMI_IN; }
static inline bool tb_port_is_apple_host_dpin(const struct tb_port *port)
{
	const struct tb *tb = port->sw->tb;

	/* the KUnit tests build switches without a domain */
	if (!tb || !tb->nhi || tb_route(port->sw) || !tb_port_is_dpin(port))
		return false;
	return tb->nhi->host_dp_policy & TB_HOST_DP_NOTIFY;
}
