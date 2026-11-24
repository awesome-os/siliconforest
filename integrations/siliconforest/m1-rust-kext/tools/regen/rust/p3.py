"""
kernel: workqueue: LinuxKPI's delayed_work and system queues

struct delayed_work in LinuxKPI holds a FreeBSD callout, initialised as a whole by
INIT_DELAYED_WORK(); there is no timer_list for timer_init_key(). LinuxKPI is at the 6.14
level: the unbound queue is system_unbound_wq (system_dfl_wq from 6.18), and there are no
BH or freezable system queues.
"""
from sfpatch import *
f = "kernel/workqueue.rs"
# DelayedWork: LinuxKPI's delayed_work holds a FreeBSD callout, initialised as a whole by
# INIT_DELAYED_WORK; there is no timer_list to set up with timer_init_key()
sub(f, """                // SAFETY: The `WorkItemPointer` implementation promises that `run` can be used as
                // the work item function.
                unsafe {
                    bindings::init_work_with_key(
                        core::ptr::addr_of_mut!((*slot).work),""", """                // SAFETY: The `WorkItemPointer` implementation promises that `run` can be used as
                // the work item function. `slot` is valid for writes.
                #[cfg(sf_lkpi)]
                unsafe {
                    bindings::sf_init_delayed_work(slot, Some(T::Pointer::run))
                }

                // SAFETY: The `WorkItemPointer` implementation promises that `run` can be used as
                // the work item function.
                #[cfg(not(sf_lkpi))]
                unsafe {
                    bindings::init_work_with_key(
                        core::ptr::addr_of_mut!((*slot).work),""")
sub(f, """                // the core workqueue code, and configured to run in irqsafe context.
                unsafe {""", """                // the core workqueue code, and configured to run in irqsafe context.
                #[cfg(not(sf_lkpi))]
                unsafe {""")
sub(f, "    unsafe { Queue::from_raw(bindings::system_dfl_wq) }", "    #[cfg(not(sf_lkpi))]\n    unsafe { Queue::from_raw(bindings::system_dfl_wq) }\n    // SAFETY: LinuxKPI (6.14 level) still calls this queue `system_unbound_wq`.\n    #[cfg(sf_lkpi)]\n    unsafe { Queue::from_raw(bindings::system_unbound_wq) }")
for fn in ["system_freezable", "system_freezable_power_efficient", "system_bh", "system_bh_highpri"]:
    gate(f, rf"^pub fn {fn}\(\)", "not(sf_lkpi)")
