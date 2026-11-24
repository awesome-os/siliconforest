/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_CM_COMPAT_H
#define SF_TB_CM_COMPAT_H
#include <linux/types.h>
#include <linux/export.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/pci_ids.h>
#include "sf_tb_wire.h"
#include "sf_tb_device_api.h"
#include "sf_tb_cm_helpers.h"
#include "sf_tb_dma_api.h"
#include "sf_tb_nhi.h"
/* These native Linux devices have no FreeBSD newbus bsddev. Keep actual
 * warnings/errors visible without routing a NULL bsddev through device_printf.
 * The implementation has a finite boot budget; no debug/payload dump enabled. */
void sf_tb_native_log(const char *,...) __attribute__((format(printf,1,2)));
#undef dev_err
#undef dev_warn
#undef dev_notice
#undef dev_info
#define dev_err(dev,...) do { (void)(dev);sf_tb_native_log(__VA_ARGS__); } while(0)
#define dev_warn(dev,...) do { (void)(dev);sf_tb_native_log(__VA_ARGS__); } while(0)
#define dev_notice(dev,...) do { (void)(dev);sf_tb_native_log(__VA_ARGS__); } while(0)
#define dev_info(dev,...) do { (void)(dev);sf_tb_native_log(__VA_ARGS__); } while(0)
#define crc32c sf_tb_crc32c
#define cpu_to_be32_array sf_tb_to_be32_array
#define be32_to_cpu_array sf_tb_from_be32_array
#ifndef EXPORT_SYMBOL_FOR_MODULES
#define EXPORT_SYMBOL_FOR_MODULES(symbol,modules) EXPORT_SYMBOL_GPL(symbol)
#endif
#ifndef DECLARE_COMPLETION_ONSTACK
/* LinuxKPI completion contains only its zero-initialized done counter. */
#define DECLARE_COMPLETION_ONSTACK(name) struct completion name = { .done=0 }
#endif
#ifndef MEGA
#define MEGA 1000000UL
#endif
#endif
