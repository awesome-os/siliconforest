// SPDX-License-Identifier: GPL-2.0
/* From drivers/thunderbolt/apple.c, linux-aurora 6a08a08e.
 * Copyright (c) Sven Peter <sven@kernel.org>
 */
#include <linux/of.h>
#include "apple-dpin-state.h"
#include "apple-dpin-platform.h"
static const struct of_device_id apple_dpin_qualified_soc[] = {
	{ .compatible = "apple,t8103" },
	/* The M2 (t8112) ACIO, NHI and display crossbar are t8103-compatible. */
	{ .compatible = "apple,t8112" },
	{ .compatible = "apple,t6000" },
	{ .compatible = "apple,t6001" },
	{ .compatible = "apple,t6020" },
	{ .compatible = "apple,t6021" },
	{ .compatible = "apple,t6030" },
	{},
};

const struct apple_dpin_policy *
apple_dpin_policy_select(const struct apple_dpin_policy *hw,
			 const struct device_node *root, bool dual_stream)
{
	if (!of_match_node(apple_dpin_qualified_soc, root))
		return &apple_dpin_disabled;
	if (hw->flow == APPLE_DPIN_PRE_POST && !dual_stream)
		return &apple_dpin_disabled;
	return hw;
}

