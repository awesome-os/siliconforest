// SPDX-License-Identifier: GPL-2.0
// glue.c - SFRustPlat: the FreeBSD-side entry of the kext. The Rust platform driver is
// registered here, where a C driver's module_platform_driver() sysinit would have run: the
// layer calls __haiku_init_hardware() right before it offers the device-tree nub to the
// platform bus (sf_fbsd_attach_platform_device).

#include <sys/bus.h>

HAIKU_FBSD_DRIVERS_GLUE(rustplat);
HAIKU_DRIVER_REQUIREMENTS(FBSD_TASKQUEUES);
NO_HAIKU_FBSD_MII_DRIVER();
NO_HAIKU_REENABLE_INTERRUPTS();

int
HAIKU_CHECK_DISABLE_INTERRUPTS(device_t dev)
{
	return 1;
}

void kprintf(const char *, ...);
int sf_rust_plat_register(void);

void
__haiku_init_hardware()
{
	static int registered;

	if (registered)
		return;
	registered = 1;
	kprintf("SFRustPlat: LinuxKPI runtime up, registering the Rust platform driver\n");
	if (sf_rust_plat_register() != 0)
		kprintf("SFRustPlat: registration FAILED\n");
}

// no network interface comes out of this kext
void sfos_net_input(void *ifp_cookie, const void *frame, size_t len) { }
void sfos_net_input_chain(void *ifp_cookie, void *fbsd_mbuf) { }
void sfos_net_link(void *ifp_cookie, int up, uint64_t baudrate) { }
