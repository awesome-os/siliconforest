// SPDX-License-Identifier: GPL-2.0
// SFAgxHost.cpp - finds the SFAgx IOService from the nub the platform bus was given. The
// IOKit class itself is the tree's fbsdkpi/xnu/SFDRMPlatformDevice.cpp, unchanged; the RTKit
// port needs that IOService (sf_rtkit_platform_init) and the LinuxKPI side only has the nub.

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSIterator.h>

extern "C" void *
sf_agx_host_of_nub(void *nub)
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
		// start() of the kext's class is what is running; it is attached already
		if (client != NULL && client->metaCast("SFAgx") != NULL) {
			found = client;
			break;
		}
	}
	it->release();
	return found;
}

// sfagx_diag: `count` kernel threads, one at a time; each calls enter() and terminates.
// Returns how many ran. For the layer's thread-slot test (sf_agx_linux.c).
#include <kern/thread.h>
#include <libkern/OSAtomic.h>

static void (*sChurnEnter)(void);
static volatile SInt32 sChurnDone;

static void
sf_agx_churn_thread(void *arg, wait_result_t wr)
{
	sChurnEnter();
	OSIncrementAtomic(&sChurnDone);
	thread_terminate(current_thread());
}

extern "C" int
sf_agx_thread_churn(int count, void (*enter)(void))
{
	int started = 0;

	sChurnEnter = enter;
	sChurnDone = 0;
	for (int i = 0; i < count; i++) {
		thread_t thread;

		if (kernel_thread_start(sf_agx_churn_thread, NULL, &thread) != KERN_SUCCESS)
			break;
		thread_deallocate(thread);
		started++;
		// one at a time, and gone before the next starts, so that xnu can reuse the thread_t
		for (int spin = 0; sChurnDone < started && spin < 2000; spin++)
			IOSleep(1);
		IOSleep(1);
	}
	return sChurnDone;
}
