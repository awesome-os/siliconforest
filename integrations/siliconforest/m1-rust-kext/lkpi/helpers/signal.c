// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/signal.c: the header is linux/sched.h here. */
#include <linux/sched.h>
#include <linux/sched/signal.h>

__rust_helper int rust_helper_signal_pending(struct task_struct *t)
{
	return signal_pending(t);
}
