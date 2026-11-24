// SPDX-License-Identifier: GPL-2.0-only OR MIT
// SFDcpHost.cpp - finds the SFDcp IOService from the nub the platform bus was given (the
// RTKit port needs it, sf_rtkit_platform_init). The IOKit class is the tree's
// fbsdkpi/xnu/SFDRMPlatformDevice.cpp, unchanged, one instance per matched display node.

#include <IOKit/IOService.h>
#include <libkern/c++/OSIterator.h>

extern "C" void *
sf_dcp_host_of_nub(void *nub)
{
	IOService *provider = OSDynamicCast(IOService, (OSObject *)nub);
	IOService *found = NULL;

	if (provider == NULL)
		return NULL;
	OSIterator *it = provider->getClientIterator();
	if (it == NULL)
		return NULL;
	while (OSObject *o = it->getNextObject()) {
		IOService *client = OSDynamicCast(IOService, o);
		if (client != NULL && client->metaCast("SFDcp") != NULL) {
			found = client;
			break;
		}
	}
	it->release();
	return found;
}
