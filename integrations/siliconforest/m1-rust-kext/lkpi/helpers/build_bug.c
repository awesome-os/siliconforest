// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/build_bug.c. LinuxKPI has no errname()
 * (lib/errname.c). NULL is what Linux returns without CONFIG_SYMBOLIC_ERRNAME: "no name
 * known", and Error's Debug output then prints the number.
 */
#include <linux/types.h>

__rust_helper const char *rust_helper_errname(int err)
{
	return NULL;
}
