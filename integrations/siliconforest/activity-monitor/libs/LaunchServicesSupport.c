// SPDX-License-Identifier: MIT
// libLaunchServicesSupport for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md). There is no Launch Services server to ask an
// application to quit: the call says it did not, and Activity Monitor then uses libquit.
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
const CFStringRef _kLSForceQuitApplicationPresentFirstForceQuitDialogKey = CFSTR("LSForceQuitApplicationPresentFirstForceQuitDialog");
bool _LSForceQuitApplication(int session, CFTypeRef asn, CFDictionaryRef options, void *block)
{
	(void)session; (void)asn; (void)options; (void)block;
	return false;
}
