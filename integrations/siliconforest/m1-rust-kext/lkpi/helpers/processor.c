// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/processor.c: the header is asm/processor.h here. */
#include <asm/processor.h>

__rust_helper void rust_helper_cpu_relax(void)
{
	cpu_relax();
}
