/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * SiliconForest: linux/soc/apple/rtkit.h includes <linux/mailbox_client.h>, a
 * leftover from when RTKit sat on Linux's generic mailbox framework. Nothing
 * of it is used (the Apple mailbox is called directly, linux/soc/apple/
 * mailbox.h), and LinuxKPI has no mailbox framework. Empty on purpose, so
 * rtkit.h stays byte-identical to Linux's.
 */
#ifndef _SF_LINUX_MAILBOX_CLIENT_H
#define _SF_LINUX_MAILBOX_CLIENT_H
#endif
