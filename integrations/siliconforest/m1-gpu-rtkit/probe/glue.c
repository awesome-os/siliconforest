// SPDX-License-Identifier: GPL-2.0
// glue.c - SFRtkitProbe: the FreeBSD-side entry of the kext, as SFRustLKPI's glue.c: no
// newbus driver, it brings the layer up the way a LinuxKPI DRM kext does.

#include <sys/bus.h>

HAIKU_FBSD_DRIVERS_GLUE(rtkitprobe);
// the linux workqueues are FreeBSD taskqueues
HAIKU_DRIVER_REQUIREMENTS(FBSD_TASKQUEUES);
NO_HAIKU_FBSD_MII_DRIVER();
NO_HAIKU_REENABLE_INTERRUPTS();

int
HAIKU_CHECK_DISABLE_INTERRUPTS(device_t dev)
{
	return 1;
}

void kprintf(const char *, ...);
int sf_fbsd_init(void);

// no device to probe through newbus
void
__haiku_init_hardware()
{
}

// no network interface comes out of this kext
void sfos_net_input(void *ifp_cookie, const void *frame, size_t len) { }
void sfos_net_input_chain(void *ifp_cookie, void *fbsd_mbuf) { }
void sfos_net_link(void *ifp_cookie, int up, uint64_t baudrate) { }

// Called by SFRtkitProbe::start() after sfos_xnu_init(), mode 2 only. 0 when the layer is up.
int
sf_rtkitprobe_layer_up(void)
{
	return sf_fbsd_init();
}
