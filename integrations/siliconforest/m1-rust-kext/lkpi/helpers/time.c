// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/time.c. */
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>

__rust_helper void rust_helper_fsleep(unsigned long usecs)
{
	fsleep(usecs);
}

/* real functions on Linux, static inlines in LinuxKPI */
__rust_helper ktime_t rust_helper_ktime_get(void)
{
	return ktime_get();
}

__rust_helper ktime_t rust_helper_ktime_get_real(void)
{
	return ktime_get_real();
}

__rust_helper ktime_t rust_helper_ktime_get_boottime(void)
{
	return ktime_get_boottime();
}

/*
 * No TAI clock in LinuxKPI (nor a leap-second offset in this kernel): CLOCK_TAI reads as
 * CLOCK_REALTIME. Listed as a gap in the report; nothing in the test kext uses it.
 */
__rust_helper ktime_t rust_helper_ktime_get_clocktai(void)
{
	return ktime_get_real();
}

__rust_helper s64 rust_helper_ktime_to_us(const ktime_t kt)
{
	return ktime_to_us(kt);
}

__rust_helper s64 rust_helper_ktime_to_ms(const ktime_t kt)
{
	return ktime_to_ms(kt);
}

__rust_helper void rust_helper_udelay(unsigned long usec)
{
	udelay(usec);
}

__rust_helper unsigned long rust_helper___msecs_to_jiffies(const unsigned int m)
{
	return msecs_to_jiffies(m);
}
