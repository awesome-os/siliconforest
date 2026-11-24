// SPDX-License-Identifier: MIT
// Metal.framework's two device-list functions for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md). There is no Metal here: the list of devices is
// empty and no observer is registered. (Only these two names; a program that needs Metal itself
// still does not run.)
#import <Foundation/Foundation.h>
NSArray *MTLCopyAllDevicesWithObserver(id *observer, void (^handler)(id device, NSString *notification))
{
	(void)handler;
	if (observer)
		*observer = nil;
	return [[NSArray alloc] init];
}
void MTLRemoveDeviceObserver(id observer) { (void)observer; }
