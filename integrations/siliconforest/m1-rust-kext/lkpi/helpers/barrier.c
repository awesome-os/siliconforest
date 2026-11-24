// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/barrier.c: LinuxKPI has no dma_wmb/dma_rmb; on
 * arm64 Linux defines them as full-system store/load barriers, as mb() is here. */
#include <linux/compiler.h>
#include <asm/barrier.h>
#include <asm/atomic.h>

__rust_helper void rust_helper_smp_mb(void)
{
	smp_mb();
}

__rust_helper void rust_helper_smp_wmb(void)
{
	smp_wmb();
}

__rust_helper void rust_helper_smp_rmb(void)
{
	smp_rmb();
}

__rust_helper void rust_helper_dma_wmb(void)
{
	wmb();
}

__rust_helper void rust_helper_dma_rmb(void)
{
	rmb();
}
