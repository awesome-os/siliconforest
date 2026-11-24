// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/workqueue.c. LinuxKPI's work_struct is a FreeBSD
 * taskqueue task (no data word, list entry or lockdep map), initialised by INIT_WORK().
 * queue_work_on() is a macro over linux_queue_work_on() there.
 */
#include <linux/workqueue.h>

__rust_helper void rust_helper_init_work_with_key(struct work_struct *work,
						  work_func_t func,
						  bool onstack,
						  const char *name,
						  struct lock_class_key *key)
{
	INIT_WORK(work, func);
}

__rust_helper struct workqueue_struct *
rust_helper_alloc_ordered_workqueue(const char *name, unsigned int flags)
{
	return alloc_ordered_workqueue(name, flags);
}

__rust_helper bool rust_helper_queue_work_on(int cpu, struct workqueue_struct *wq,
					     struct work_struct *work)
{
	return queue_work_on(cpu, wq, work);
}

__rust_helper bool rust_helper_queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
						     struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(cpu, wq, dwork, delay);
}

__rust_helper void rust_helper_destroy_workqueue(struct workqueue_struct *wq)
{
	destroy_workqueue(wq);
}

__rust_helper void rust_helper_drain_workqueue(struct workqueue_struct *wq)
{
	drain_workqueue(wq);
}

/* SiliconForest only: called by kernel::workqueue::DelayedWork::new in place of
 * init_work_with_key() + timer_init_key(). */
__rust_helper void rust_helper_sf_init_delayed_work(struct delayed_work *dwork, work_func_t func)
{
	INIT_DELAYED_WORK(dwork, func);
}
