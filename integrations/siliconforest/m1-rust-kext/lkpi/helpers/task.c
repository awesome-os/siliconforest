// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/task.c. LinuxKPI's task_struct has no credentials
 * and no pid namespaces, so the kuid and pid-namespace helpers are left out (the Rust
 * methods that use them are compiled only with sf_mod_cred / sf_mod_pid_namespace).
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>

__rust_helper void rust_helper_might_resched(void)
{
	might_sleep();
}

__rust_helper struct task_struct *rust_helper_get_current(void)
{
	return current;
}

__rust_helper void rust_helper_get_task_struct(struct task_struct *t)
{
	get_task_struct(t);
}

__rust_helper void rust_helper_put_task_struct(struct task_struct *t)
{
	put_task_struct(t);
}

__rust_helper int rust_helper_wake_up_process(struct task_struct *t)
{
	return wake_up_process(t);
}
