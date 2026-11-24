
/*
 * SiliconForest additions to the reduced bindings_helper.h (appended by the build).
 */
#include <sf_linux_compat.h>

/* Headers Linux reaches indirectly (linux/mm.h, linux/sched.h, ...) that LinuxKPI only
 * provides when asked for by name. */
#include <linux/maple_tree.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/kstrtox.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/property.h>
#include <linux/of_reserved_mem.h>
#include <linux/sf_drvcore.h>

/* Macros bindgen cannot turn into constants (casts, expressions). */
const clockid_t RUST_CONST_HELPER_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
const clockid_t RUST_CONST_HELPER_CLOCK_REALTIME = CLOCK_REALTIME;
const clockid_t RUST_CONST_HELPER_CLOCK_BOOTTIME = CLOCK_BOOTTIME;
const clockid_t RUST_CONST_HELPER_CLOCK_TAI = CLOCK_TAI;
const int RUST_CONST_HELPER_TASK_FREEZABLE = TASK_FREEZABLE;
const int RUST_CONST_HELPER_MAX_NUMNODES = MAX_NUMNODES;
const int RUST_CONST_HELPER_NUMA_NO_NODE = NUMA_NO_NODE;
const unsigned long RUST_CONST_HELPER_MAX_UDELAY_MS = MAX_UDELAY_MS;
const int RUST_CONST_HELPER_wq_misc_consts_WORK_CPU_UNBOUND = WORK_CPU_UNBOUND;
