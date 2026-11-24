/* SPDX-License-Identifier: MIT */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
enum { SF_SC_ROUTE_CHANGED=1, SF_SC_DNS_CHANGED=2 };
typedef void (*SFSCNetworkNotification)(CFTypeRef target, unsigned changes);
/* A real blocking kqueue watcher, attached to the caller's CF run loop.
 * It never changes routes, interfaces or resolver settings. dnsPath=NULL uses
 * /etc/resolv.conf; an explicit path permits independent native file tests. */
CFRunLoopSourceRef SFSCNetworkSourceCreate(CFAllocatorRef allocator, CFTypeRef target,
                                         CFIndex order, SFSCNetworkNotification notify,
                                         const char *dnsPath);
