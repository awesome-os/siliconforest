// SPDX-License-Identifier: GPL-2.0
// glue.c - SFRustLKPI: the FreeBSD-side entry of the kext. No hardware driver: it brings the
// layer up the way a LinuxKPI DRM kext does and then runs the Rust self-test.

#include <sys/bus.h>

HAIKU_FBSD_DRIVERS_GLUE(rustlkpi);
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
unsigned int sf_rust_lkpi_run(void);

// no device to probe
void
__haiku_init_hardware()
{
}

// no network interface comes out of this kext
void sfos_net_input(void *ifp_cookie, const void *frame, size_t len) { }
void sfos_net_input_chain(void *ifp_cookie, void *fbsd_mbuf) { }
void sfos_net_link(void *ifp_cookie, int up, uint64_t baudrate) { }

// Called by SFRustLKPI::start() after sfos_xnu_init(). Returns the number of failed checks,
// or -1 when the layer itself did not come up.
int
sf_rustlkpi_start(void)
{
	int status = sf_fbsd_init();

	if (status != 0) {
		kprintf("SFRustLKPI: sf_fbsd_init failed: %d\n", status);
		return -1;
	}
	kprintf("SFRustLKPI: LinuxKPI runtime up (mutexes, callouts, taskqueues, sysinits)\n");
	return (int)sf_rust_lkpi_run();
}
