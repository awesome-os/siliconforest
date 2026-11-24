// SPDX-License-Identifier: GPL-2.0
// glue.c - SFAgx: the FreeBSD-side entry of the kext. The layer calls __haiku_init_hardware()
// right before it offers the device-tree nub to the platform bus
// (sf_fbsd_attach_platform_device); that is where the driver's module init runs, as an
// initcall would on Linux.

#include <sys/bus.h>

HAIKU_FBSD_DRIVERS_GLUE(agx);
HAIKU_DRIVER_REQUIREMENTS(FBSD_TASKQUEUES);
NO_HAIKU_FBSD_MII_DRIVER();
NO_HAIKU_REENABLE_INTERRUPTS();

int
HAIKU_CHECK_DISABLE_INTERRUPTS(device_t dev)
{
	return 1;
}

void kprintf(const char *, ...);
void sf_agx_linux_init(void);
// rust/kernel module! (m1-lkpi-extras patch 1001): reads the asahi.<param> boot-args, then
// runs the driver's init, which registers the platform driver
int __asahi_init(void);

void
__haiku_init_hardware()
{
	static int registered;
	int ret;

	if (registered)
		return;
	registered = 1;
	sf_agx_linux_init();
	kprintf("SFAgx: LinuxKPI runtime up, registering the asahi driver\n");
	ret = __asahi_init();
	if (ret != 0)
		kprintf("SFAgx: asahi module init FAILED: %d\n", ret);
	else
		kprintf("SFAgx: asahi module init done\n");
}

// no network interface comes out of this kext
void sfos_net_input(void *ifp_cookie, const void *frame, size_t len) { }
void sfos_net_input_chain(void *ifp_cookie, void *fbsd_mbuf) { }
void sfos_net_link(void *ifp_cookie, int up, uint64_t baudrate) { }
