#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extend only this target's private LinuxKPI metadata, retaining its notices."""
import pathlib,sys
p=pathlib.Path(sys.argv[1])/'src/Kernel/fbsdkpi/freebsd/linuxkpi/common/include/linux/device.h'
s=p.read_text()
def change(old,new):
 global s
 assert s.count(old)==1,old;s=s.replace(old,new)
change('struct device;\n','struct device;\nstruct bus_type;\nstruct kobj_uevent_env;\n')
change('struct device_driver {','struct device_driver {\n\tconst struct bus_type *bus;')
change('struct device_type {\n\tconst char\t*name;\n};','''struct device_type {
 const char *name;
 const struct attribute_group **groups;
 const struct dev_pm_ops *pm;
 void (*release)(struct device *);
 int (*uevent)(const struct device *,struct kobj_uevent_env *);
};''')
change('struct device {','''struct device {
 const struct bus_type *bus;
 bool sf_uevent_suppress,sf_wakeup_enabled,sf_wakeup_capable,sf_registered,sf_parent_held;
 void *sf_native_token;''')
change('\tint (*suspend_noirq)(struct device *dev);','''\tint (*suspend_noirq)(struct device *dev);
 int (*resume_noirq)(struct device *dev);
 int (*freeze_noirq)(struct device *dev);
 int (*thaw_noirq)(struct device *dev);
 int (*poweroff_noirq)(struct device *dev);
 int (*restore_noirq)(struct device *dev);''')
p.write_text(s)
