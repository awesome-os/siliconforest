// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/uaccess.c. LinuxKPI has get_user/put_user and
 * linux_copyin()/linux_copyout() (on xnu copyin/copyout through the seam), not the Linux
 * functions themselves. Linux returns the number of bytes NOT copied; the layer's copy is
 * all or nothing, so that is either 0 or n (and nothing is zero-filled on a short read
 * because there are no short reads).
 */
#include <linux/uaccess.h>
#include <linux/string.h>

__rust_helper unsigned long
rust_helper_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return linux_copyin(from, to, n) == 0 ? 0 : n;
}

__rust_helper unsigned long
rust_helper_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return linux_copyout(from, to, n) == 0 ? 0 : n;
}

/* the out-of-line variants Linux uses when the copy is not inlined */
__rust_helper unsigned long
rust_helper__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return linux_copyin(from, to, n) == 0 ? 0 : n;
}

__rust_helper unsigned long
rust_helper__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return linux_copyout(from, to, n) == 0 ? 0 : n;
}

/*
 * Length of the string without its NUL, `count` if there was none within `count` bytes,
 * -EFAULT on a bad address. Copied a piece at a time, never reading past the page the
 * string ends in.
 */
__rust_helper long
rust_helper_strncpy_from_user(char *dst, const char __user *src, long count)
{
	long done = 0;

	while (done < count) {
		unsigned long addr = (unsigned long)src + done;
		long chunk = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
		const char *nul;

		if (chunk > count - done)
			chunk = count - done;
		if (linux_copyin((const void *)addr, dst + done, chunk) != 0)
			return -EFAULT;
		nul = memchr(dst + done, '\0', chunk);
		if (nul != NULL)
			return nul - dst;
		done += chunk;
	}
	return count;
}
