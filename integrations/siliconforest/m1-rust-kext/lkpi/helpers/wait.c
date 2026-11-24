// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/wait.c, plus the wait-queue calls CondVar makes.
 * On Linux those are exported functions; in LinuxKPI they are macros over linux_*().
 * LinuxKPI has no exclusive waiters: prepare_to_wait_exclusive queues an ordinary waiter, so
 * wake-one (__wake_up with nr = 1) wakes one thread but without Linux's exclusive ordering.
 */
#include <linux/sched.h>
#include <linux/wait.h>

__rust_helper void rust_helper_init_wait(struct wait_queue_entry *wq_entry)
{
	init_wait_entry(wq_entry, 0);
}

__rust_helper void rust_helper___init_waitqueue_head(struct wait_queue_head *wq_head,
						     const char *name, struct lock_class_key *key)
{
	init_waitqueue_head(wq_head);
}

__rust_helper void rust_helper_prepare_to_wait_exclusive(struct wait_queue_head *wq_head,
							 struct wait_queue_entry *wq_entry, int state)
{
	prepare_to_wait(wq_head, wq_entry, state);
}

__rust_helper void rust_helper_finish_wait(struct wait_queue_head *wq_head,
					   struct wait_queue_entry *wq_entry)
{
	finish_wait(wq_head, wq_entry);
}

__rust_helper int rust_helper___wake_up(struct wait_queue_head *wq_head, unsigned int mode,
					int nr_exclusive, void *key)
{
	linux_wake_up(wq_head, mode, nr_exclusive, false);
	return 0;
}

__rust_helper void rust_helper___wake_up_sync(struct wait_queue_head *wq_head, unsigned int mode)
{
	linux_wake_up(wq_head, mode, 1, false);
}

__rust_helper long rust_helper_schedule_timeout(long timeout)
{
	return schedule_timeout(timeout);
}
