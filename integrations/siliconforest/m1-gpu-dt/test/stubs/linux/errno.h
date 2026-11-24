/* SPDX-License-Identifier: MIT */
/* glibc's errno.h includes <linux/errno.h>: pass it on to the real one. Test only. */
#include_next <linux/errno.h>
