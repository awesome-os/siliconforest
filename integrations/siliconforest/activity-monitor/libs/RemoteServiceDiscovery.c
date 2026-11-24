// SPDX-License-Identifier: MIT
// RemoteServiceDiscovery.framework (private) for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md): devices reached over remote XPC (a Mac's
// coprocessors, an attached iPhone). There are none here.
#include <stddef.h>
void *remote_device_copy_unique_of_type(int type) { (void)type; return NULL; }
void *remote_device_copy_property(void *device, const char *name) { (void)device; (void)name; return NULL; }
