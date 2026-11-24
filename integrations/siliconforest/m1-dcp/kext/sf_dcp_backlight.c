// SPDX-License-Identifier: GPL-2.0-only
/*
 * sf_dcp_backlight.c - the panel backlight for user space.
 *
 * The driver's dcp_backlight.c registers a backlight class device, "apple-panel-bl", and on
 * Linux that class gives it files under /sys/class/backlight/apple-panel-bl. Darwin has no
 * sysfs; the same four values are sysctls, kern.sf_backlight.* (SFDcpSysctl.cpp), and the
 * functions here are what the files' show and store routines do, from
 * drivers/video/backlight/backlight.c of linux-aurora at 6a08a08e (brightness_store ->
 * backlight_device_set_brightness, actual_brightness_show, bl_power_store, bl_power_show,
 * max_brightness_show):
 *
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 */
#include <linux/backlight.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sched.h>

void kprintf(const char *, ...);

enum { SF_DCP_BL_BRIGHTNESS, SF_DCP_BL_MAX, SF_DCP_BL_ACTUAL, SF_DCP_BL_POWER };

int sf_dcp_backlight_get(int what, int *value);
int sf_dcp_backlight_set(int what, int value);
const char *sf_dcp_backlight_name(void);

/* one panel; set when the driver registers its backlight, never cleared (see below) */
static struct backlight_device *sf_dcp_bl;

/* not device-managed: the backlight lives until the kext does */
struct backlight_device *
devm_backlight_device_register(struct device *dev, const char *name, struct device *parent,
    void *devdata, const struct backlight_ops *ops, const struct backlight_properties *props)
{
	struct backlight_device *bd;

	bd = backlight_device_register(name, parent, devdata, ops, (struct backlight_properties *)props);
	if (bd == NULL)
		return (ERR_PTR(-ENOMEM));
	kprintf("SFDcp: backlight: %s registered, brightness %d of %d (kern.sf_backlight.*)\n", name,
	    bd->props.brightness, bd->props.max_brightness);
	WRITE_ONCE(sf_dcp_bl, bd);
	return (bd);
}

void
devm_backlight_device_unregister(struct device *dev, struct backlight_device *bd)
{
	if (READ_ONCE(sf_dcp_bl) == bd)
		WRITE_ONCE(sf_dcp_bl, NULL);
	backlight_device_unregister(bd);
}

const char *
sf_dcp_backlight_name(void)
{
	struct backlight_device *bd = READ_ONCE(sf_dcp_bl);

	return (bd != NULL ? bd->name : NULL);
}

/* 0 and *value, or an errno (positive, for the sysctl handler) */
int
sf_dcp_backlight_get(int what, int *value)
{
	struct backlight_device *bd = READ_ONCE(sf_dcp_bl);
	int rc = 0;

	if (bd == NULL)
		return (ENODEV);
	linux_set_current(curthread);
	switch (what) {
	case SF_DCP_BL_BRIGHTNESS:
		*value = bd->props.brightness;
		break;
	case SF_DCP_BL_MAX:
		*value = bd->props.max_brightness;
		break;
	case SF_DCP_BL_POWER:
		*value = bd->props.power;
		break;
	case SF_DCP_BL_ACTUAL:
		/* actual_brightness_show() */
		mutex_lock(&bd->ops_lock);
		if (bd->ops && bd->ops->get_brightness) {
			rc = bd->ops->get_brightness(bd);
			if (rc >= 0) {
				*value = rc;
				rc = 0;
			}
		} else {
			*value = bd->props.brightness;
		}
		mutex_unlock(&bd->ops_lock);
		break;
	default:
		rc = -EINVAL;
	}
	return (rc < 0 ? -rc : rc);
}

int
sf_dcp_backlight_set(int what, int value)
{
	struct backlight_device *bd = READ_ONCE(sf_dcp_bl);
	int rc, old_power;

	if (bd == NULL)
		return (ENODEV);
	if (value < 0)
		return (EINVAL);
	linux_set_current(curthread);
	switch (what) {
	case SF_DCP_BL_BRIGHTNESS:
		/* brightness_store() */
		rc = backlight_device_set_brightness(bd, (unsigned long)value);
		kprintf("SFDcp: backlight: brightness %d of %d requested: %d\n", value,
		    bd->props.max_brightness, rc);
		break;
	case SF_DCP_BL_POWER:
		/* bl_power_store() */
		rc = -ENXIO;
		mutex_lock(&bd->ops_lock);
		if (bd->ops) {
			if (bd->props.power != value) {
				old_power = bd->props.power;
				bd->props.power = value;
				rc = backlight_update_status(bd);
				if (rc)
					bd->props.power = old_power;
			} else {
				rc = 0;
			}
		}
		mutex_unlock(&bd->ops_lock);
		kprintf("SFDcp: backlight: bl_power %d requested: %d\n", value, rc);
		break;
	default:
		rc = -EINVAL;
	}
	return (rc < 0 ? -rc : rc);
}
