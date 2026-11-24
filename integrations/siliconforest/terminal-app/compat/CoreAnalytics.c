// SPDX-License-Identifier: MIT
// CoreAnalytics.framework (private on macOS; Terminal.app imports two functions): usage
// events for Apple's analytics daemon. Nothing collects events on this system: no event is
// "used", and the block that would build an event's payload is never called.
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
bool AnalyticsIsEventUsed(CFStringRef eventName) { return false; }
void AnalyticsSendEventLazy(CFStringRef eventName, void *payloadBlock) { }
