// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/bug.c: BUG() lives in linux/kernel.h here. */
#include <linux/bug.h>
#include <linux/kernel.h>

__rust_helper __dead2 void rust_helper_BUG(void)
{
	BUG();
	for (;;)
		;
}

__rust_helper bool rust_helper_WARN_ON(bool cond)
{
	return WARN_ON(cond);
}
