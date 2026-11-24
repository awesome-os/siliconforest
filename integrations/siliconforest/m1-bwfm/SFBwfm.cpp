// SPDX-License-Identifier: MIT
// Broadcom BCM4378 PCIe FullMAC 802.11ax (14e4:4425, Apple M1 J293), OpenBSD's bwfm(4) on
// IO80211Family, with the pci calls it makes. Derived from SFQwx.cpp. The interrupt is one
// MSI vector from the host bridge with a slow poll timer behind it, DMA goes through the DART
// mapper, see sf_bwfm_compat.h.
//
// Lifecycle, all of it logged with one line per stage:
//
//   start       pci side attached, a thread loads the firmware once the root file system is
//               there and attaches the 802.11 interface. A bring-up that fails leaves the
//               chip reset, nothing allocated, the interrupt sources off and no interface.
//   radio off   (wifiutil off, APPLE80211_IOC_POWER 0) boot-arg bwfm_off selects what it means:
//               0, the default: OpenBSD's interface stop. Scan aborted, disassociated, flow
//                  rings deleted, radio idle; the firmware stays loaded and served.
//               1: after that the firmware is told (D3 inform), the chip masked and reset and
//                  every ring, buffer and DMA mapping released. Radio on loads the firmware again.
//               2: after that the host bridge shuts the port down and switches the radio's
//                  power off through the SMC (SFApciePortShutdown). Radio on brings the port
//                  back (SFApciePortResume) and loads the firmware. The SMC "off" value and
//                  the port sequence are UNVERIFIED on hardware.
//   sleep/wake  a priority sleep/wake interest on the root domain, nothing else: on sleep the
//               steps of bwfm_off=1, and neither chip nor DART is touched from then on; on
//               wake the bridge is asked for the port (it brings the link up if the SoC took
//               it down), config space, DART and MSI are checked and written again if they
//               were lost and, if the interface ran before, the firmware is loaded again.
//               UNTESTED: the guest cannot sleep. The code is that of bwfm_off=1 and 2, which
//               can be run with wifiutil.
//   stop        see SFBwfm::stop.

#include "sf_dart_pool.h"
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOInterruptController.h>
#include "IO80211Controller.h"
#include "sf_bwfm_glue.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/kpi_mbuf.h>

#include "sf_bwfm_compat.h"

extern "C" {
void *sf80211_malloc(size_t, int, int);
void sf80211_free(void *, int, size_t);
void sf80211_lock(void);
void sf80211_unlock(void);
vnode_t vfs_rootvnode(void);
}

#define LOG(fmt, ...)		IOLog(SF_BWFM_NAME ": " fmt "\n", ##__VA_ARGS__)

#define BWFM_ROOT_WAIT_S	300
#define BWFM_MAX_BARS		2
#define BWFM_MAX_DEVICES	2

// what "radio off" does, boot-arg bwfm_off
enum {
	kOffStop = 0,		// interface stop, firmware stays loaded
	kOffHalt = 1,		// and firmware stopped, everything released
	kOffPower = 2,		// and port shut down, radio power off
};

// how far SFBwfm::chipDown goes after it stopped the firmware
enum {
	kDownHalt = 0,		// no further: the chip stays powered and reachable
	kDownSleep = 1,		// the system sleeps: the port counts as lost until the bridge has it back
	kDownPower = 2,		// the bridge shuts the port down and switches the radio's power off
};

// the config registers a power loss clears: BARs, cache line and interrupt line. The command
// register is kept apart, it goes back last
static const UInt8 kSavedConfig[] = { 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x0c, 0x3c };
#define BWFM_SAVED_CONFIG	(sizeof(kSavedConfig) / sizeof(kSavedConfig[0]))

class SFBwfm;

struct sf_bwfm_dma {
	IOPCIDevice	*pci;
};

class SFBwfm : public IO80211Controller
{
	OSDeclareDefaultStructors(SFBwfm)

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual SInt32 apple80211Request(bool set, int type, int value, void *data, uint32_t *len) APPLE_KEXT_OVERRIDE;
	virtual const OSString *newVendorString() const APPLE_KEXT_OVERRIDE;
	virtual const OSString *newModelString() const APPLE_KEXT_OVERRIDE;

	IOPCIDevice *fPCI;
	IOWorkLoop *fPollLoop;
	IOTimerEventSource *fPollTimer;
	IOInterruptEventSource *fIntSrc;
	UInt32 fPollUS;
	volatile UInt32 fIntrCount;
	bool setupMSI();
	static void intrAction(OSObject *owner, IOInterruptEventSource *sender, int count);
	IOMemoryMap *fBars[BWFM_MAX_BARS];
	int (*fIntrFn)(void *);
	void *fIntrArg;
	volatile bool fPolling;
	static void pollAction(OSObject *owner, IOTimerEventSource *sender);

private:
	static void firmwareThread(void *arg, wait_result_t wr);
	static void wakeThread(void *arg, wait_result_t wr);
	static IOReturn sleepWake(void *target, void *refCon, UInt32 messageType, IOService *provider,
	    void *messageArgument, vm_size_t argSize);
	void bringUp();
	void bringUpFailed(const char *what);
	void systemSleep();
	void systemWake();
	IOReturn chipDown(int how, const char *why);
	IOReturn chipUp(bool askBridge, const char *why);
	IOReturn bridgeCall(const char *name, void *param2, void *param3);
	void saveConfig();
	bool restoreConfig();
	static IOReturn syncAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
	void pauseInterrupts();
	void resumeInterrupts();
	void quiesceInterrupts();
	void teardown(bool stopping);

	struct sf_bwfm_dma fDma;
	struct device *fDev;
	IOLock *fLifecycle;		// one chipDown or chipUp at a time
	IONotifier *fSleepWake;
	bool fAttached;
	bool f80211;
	volatile bool fStopping;
	volatile SInt32 fThreads;	// firmware and wake threads still running
	int fOffMode;			// boot-arg bwfm_off, kOff*
	bool fChipDown;			// the firmware was stopped on purpose: radio off or sleep
	bool fPortDown;			// the bridge shut the port down, no register may be touched
	bool fAsleep;
	bool fSleepTookDown;		// this sleep stopped the firmware, the wake brings it back
	bool fResumeUp;			// the interface ran when the system went to sleep
	bool fHaveMSI;			// the bridge gave a vector, it goes back in stop
	bool fConfigSaved;
	UInt32 fSavedID;
	UInt16 fSavedCommand;
	UInt32 fSavedConfig[BWFM_SAVED_CONFIG];
};

#define super IO80211Controller
OSDefineMetaClassAndStructors(SFBwfm, IO80211Controller)

// the pci tag is the IOPCIDevice, this finds the driver behind it
static struct {
	IOPCIDevice *pci;
	SFBwfm *owner;
} sDevices[BWFM_MAX_DEVICES];
static IOLock *sDevicesLock;

static void setOwner(IOPCIDevice *pci, SFBwfm *owner)
{
	if (sDevicesLock == NULL) {
		IOLock *lock = IOLockAlloc();

		if (!OSCompareAndSwapPtr(NULL, lock, (void * volatile *)&sDevicesLock))
			IOLockFree(lock);
	}
	IOLockLock(sDevicesLock);
	for (int i = 0; i < BWFM_MAX_DEVICES; i++) {
		if (owner != NULL && sDevices[i].pci == NULL) {
			sDevices[i].pci = pci;
			sDevices[i].owner = owner;
			break;
		}
		if (owner == NULL && sDevices[i].pci == pci) {
			sDevices[i].pci = NULL;
			sDevices[i].owner = NULL;
			break;
		}
	}
	IOLockUnlock(sDevicesLock);
}

static SFBwfm *ownerOf(void *pci)
{
	SFBwfm *owner = NULL;

	if (sDevicesLock == NULL)
		return NULL;
	IOLockLock(sDevicesLock);
	for (int i = 0; i < BWFM_MAX_DEVICES; i++) {
		if (sDevices[i].pci == pci)
			owner = sDevices[i].owner;
	}
	IOLockUnlock(sDevicesLock);
	return owner;
}

static bool rootMounted()
{
	vnode_t root = vfs_rootvnode();

	if (root == NULLVP)
		return false;
	vnode_put(root);
	return true;
}

bool SFBwfm::start(IOService *provider)
{
	struct pci_attach_args pa;
	thread_t thread;

	fPCI = OSDynamicCast(IOPCIDevice, provider);
	if (fPCI == NULL || !super::start(provider))
		return false;
	if (!fPCI->open(this)) {
		LOG("cannot open the pci device");
		super::stop(provider);
		return false;
	}
	fPollLoop = IOWorkLoop::workLoop();
	fLifecycle = IOLockAlloc();
	if (fPollLoop == NULL || fLifecycle == NULL) {
		teardown(false);
		super::stop(provider);
		return false;
	}
	fOffMode = kOffStop;
	PE_parse_boot_argn("bwfm_off", &fOffMode, sizeof(fOffMode));
	if (fOffMode < kOffStop || fOffMode > kOffPower)
		fOffMode = kOffStop;
	setOwner(fPCI, this);
	fPCI->setMemoryEnable(true);
	fPCI->setBusMasterEnable(true);

	fDma.pci = fPCI;
	bzero(&pa, sizeof(pa));
	pa.pa_pc = fPCI;
	pa.pa_tag = fPCI;
	pa.pa_dmat = &fDma;
	pa.pa_id = fPCI->configRead32(kIOPCIConfigVendorID);
	fSavedID = pa.pa_id;
	LOG("attach: pci %04x:%04x, command 0x%04x, radio off mode %d (boot-arg bwfm_off)", pa.pa_id & 0xffff,
	    pa.pa_id >> 16, fPCI->configRead16(kIOPCIConfigCommand), fOffMode);

	fDev = sf_bwfm_alloc(SF_BWFM_NAME);
	if (fDev == NULL || !sf_bwfm_match(&pa) || sf_bwfm_attach(fDev, &pa) != 0) {
		LOG("the pci side did not attach");
		teardown(false);
		super::stop(provider);
		return false;
	}
	fAttached = true;

	// the firmware comes off the root file system. Waiting for it here would hold up the
	// matching thread, so the rest of the bring-up has a thread of its own
	OSIncrementAtomic(&fThreads);
	retain();
	if (kernel_thread_start(&SFBwfm::firmwareThread, this, &thread) != KERN_SUCCESS) {
		release();
		OSDecrementAtomic(&fThreads);
		LOG("no thread for the firmware bring-up");
		teardown(false);
		super::stop(provider);
		return false;
	}
	thread_deallocate(thread);
	// Nothing comes of this unless the system really goes to sleep; the driver is not a
	// power child of the pci device (IONetworkController gives that up when the driver
	// registers no power states), so no power state change ever reaches it another way
	fSleepWake = registerPrioritySleepWakeInterest(&SFBwfm::sleepWake, this, NULL);
	if (fSleepWake == NULL)
		LOG("attach: no sleep/wake notification, sleep is not handled");
	LOG("attach: pci side done, firmware bring-up waits for the root file system");
	return true;
}

void SFBwfm::firmwareThread(void *arg, wait_result_t wr)
{
	SFBwfm *self = (SFBwfm *)arg;

	self->bringUp();
	OSDecrementAtomic(&self->fThreads);
	self->release();
}

void SFBwfm::bringUp()
{
	for (int i = 0; i < BWFM_ROOT_WAIT_S * 10 && !rootMounted() && !fStopping; i++)
		IOSleep(100);
	if (fStopping)
		return;
	if (!rootMounted()) {
		LOG("no root file system after %d s, giving up", BWFM_ROOT_WAIT_S);
		bringUpFailed("no root file system");
		return;
	}
	LOG("root file system mounted, loading firmware from %s", SF_BWFM_FIRMWARE_DIR);
	if (sf_bwfm_attach_hook(fDev) != 0) {
		bringUpFailed("firmware bring-up");
		return;
	}
	if (fStopping)
		return;
	LOG("firmware bring-up done, attaching the 802.11 interface");
	if (!attach80211(sf_bwfm_ic(fDev))) {
		// attach80211 hooked the interface before it failed
		sf_bwfm_unhook(fDev);
		fIC = NULL;
		sf_bwfm_down(fDev, 0);
		bringUpFailed("attach80211");
		return;
	}
	f80211 = true;
	registerService();
	LOG("802.11 interface attached");
	// There is no configd/InterfaceNamer in the RAM root: ask the stack to name every
	// published interface, which is what IORegisterNetworkInterface does from userspace.
	for (int i = 0; i < 10 && !fStopping; i++) {
		OSObject *bsd = fInterface ? ((IOService *)fInterface)->copyProperty("BSD Name") : NULL;
		OSString *name = OSDynamicCast(OSString, bsd);

		if (name != NULL) {
			LOG("interface is %s", name->getCStringNoCopy());
			OSSafeReleaseNULL(bsd);
			break;
		}
		OSSafeReleaseNULL(bsd);
		OSDictionary *stackMatch = serviceMatching("IONetworkStack");
		IOService *st = stackMatch ? copyMatchingService(stackMatch) : NULL;
		OSDictionary *cmd = OSDictionary::withCapacity(1);
		OSNumber *all = OSNumber::withNumber((unsigned long long)2 /* kIONetworkStackRegisterInterfaceAll */, 32);
		if (st && cmd && all) {
			cmd->setObject("IONetworkStackUserCommand", all);
			st->setProperties(cmd);
		}
		OSSafeReleaseNULL(all);
		OSSafeReleaseNULL(cmd);
		OSSafeReleaseNULL(st);
		OSSafeReleaseNULL(stackMatch);
		IOSleep(500);
	}
}

// The bus side has halted the chip and freed what the bring-up took (bwfm_attachhook); what
// is left here is the interrupt, which has nothing to deliver any more. The driver stays
// attached so nothing matches the device again, and stop() takes the rest apart.
void SFBwfm::bringUpFailed(const char *what)
{
	quiesceInterrupts();
	LOG("%s FAILED: no interface. The chip is quiet (reset, or never started), nothing is allocated for it, "
	    "the interrupt is off; the driver stays attached and idle", what);
}

// stop: nothing of the driver may run or be reachable once the softc is freed. In order:
//  1. the firmware and wake threads have ended (they see fStopping)
//  2. no sleep/wake notification comes any more
//  3. the 802.11 interface is detached, which stops it if it was up (scan aborted,
//     disassociated, flow rings deleted: needs the interrupt, so it comes first), and the
//     stack's hooks into this controller are cleared
//  4. the chip is masked and reset and every ring, buffer and DMA mapping is released
//  5. the MSI source and the poll timer are off the work loop: no handler runs from here on
//  6. net80211 is detached: control messages and the task thread are out of the softc
//  7. the MSI vector goes back to the bridge, bus mastering is off; with bwfm_off=2 the port
//     is shut down
//  8. softc, BAR mappings and work loop are released, the pci device is closed
// Every step looks at what exists, so this is also what a failed start runs.
void SFBwfm::stop(IOService *provider)
{
	LOG("stop: waiting for the driver's threads");
	fStopping = true;
	for (int i = 0; fThreads > 0; i++) {
		IOSleep(50);
		if (i % 200 == 199)
			LOG("stop: a driver thread is still running after %d s", (i + 1) / 20);
	}
	if (fSleepWake != NULL) {
		fSleepWake->remove();
		fSleepWake = NULL;
	}
	if (f80211) {
		LOG("stop: detaching the 802.11 interface");
		detach80211();
		sf_bwfm_unhook(fDev);
	}
	f80211 = false;
	teardown(true);
	LOG("stop: done");
	super::stop(provider);
}

void SFBwfm::teardown(bool stopping)
{
	if (fLifecycle != NULL)
		IOLockLock(fLifecycle);
	// with the port down the chip was halted on the way there, and the DART is not read
	if (fAttached && !fPortDown) {
		LOG("stop: halting the chip");
		sf_bwfm_down(fDev, 0);
	}
	// no handler may run once the softc goes away
	quiesceInterrupts();
	if (fAttached && sf_bwfm_detach(fDev) != 0) {
		// better a leak than a thread on freed memory
		LOG("stop: something is still inside the driver state, it is NOT freed");
		fDev = NULL;
	} else if (fAttached) {
		LOG("stop: net80211 detached");
	}
	fAttached = false;
	if (fPCI != NULL && fHaveMSI) {
		// with the port down the bridge only takes the vector back
		bridgeCall("SFApcieReleaseMSI", fPCI, NULL);
		fHaveMSI = false;
	}
	if (fPCI != NULL && !fPortDown) {
		fPCI->setBusMasterEnable(false);
		// never on the way out of a failed start: that would be a boot path
		if (stopping && fOffMode == kOffPower) {
			UInt32 portDown = 0;

			LOG("stop: bwfm_off=2, asking the bridge to shut the port down and switch the radio off");
			bridgeCall("SFApciePortShutdown", (void *)(uintptr_t)1, &portDown);
			fPortDown = portDown != 0;
		}
	}
	if (fDev != NULL)
		sf_bwfm_free(fDev);
	fDev = NULL;
	for (int i = 0; i < BWFM_MAX_BARS; i++)
		OSSafeReleaseNULL(fBars[i]);
	OSSafeReleaseNULL(fPollLoop);
	if (fPCI != NULL) {
		setOwner(fPCI, NULL);
		fPCI->close(this);
	}
	// the lock itself stays until free(): a request that came in late may still wait for it
	if (fLifecycle != NULL)
		IOLockUnlock(fLifecycle);
}

void SFBwfm::free()
{
	if (fLifecycle != NULL)
		IOLockFree(fLifecycle);
	fLifecycle = NULL;
	super::free();
}

// the interrupt sources: paused while the port is down (the poll also reads the DART, which
// may not be powered then), gone for good in stop

IOReturn SFBwfm::syncAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
	return kIOReturnSuccess;
}

void SFBwfm::pauseInterrupts()
{
	fPolling = false;
	if (fIntSrc != NULL)
		fIntSrc->disable();
	if (fPollTimer != NULL)
		fPollTimer->cancelTimeout();
	// a handler that was already running is through once this comes back
	if (fPollLoop != NULL)
		fPollLoop->runAction(&SFBwfm::syncAction, this);
}

void SFBwfm::resumeInterrupts()
{
	if (fPollTimer == NULL)
		return;
	fPolling = true;
	if (fIntSrc != NULL)
		fIntSrc->enable();
	fPollTimer->setTimeoutUS(fPollUS);
}

void SFBwfm::quiesceInterrupts()
{
	fPolling = false;
	if (fIntSrc != NULL) {
		fIntSrc->disable();
		if (fPollLoop != NULL)
			fPollLoop->removeEventSource(fIntSrc);
		OSSafeReleaseNULL(fIntSrc);
	}
	if (fPollTimer != NULL) {
		fPollTimer->cancelTimeout();
		fPollTimer->disable();
		if (fPollLoop != NULL)
			fPollLoop->removeEventSource(fPollTimer);
		OSSafeReleaseNULL(fPollTimer);
	}
}

// radio off beyond the interface stop, and system sleep

IOReturn SFBwfm::bridgeCall(const char *name, void *param2, void *param3)
{
	const OSSymbol *fn = OSSymbol::withCString(name);
	IOReturn ret = fn ? fPCI->callPlatformFunction(fn, true, this, param2, param3, NULL) : kIOReturnNoMemory;

	OSSafeReleaseNULL(fn);
	if (ret != kIOReturnSuccess)
		LOG("%s: the host bridge answered 0x%x", name, ret);
	return ret;
}

void SFBwfm::saveConfig()
{
	fSavedCommand = fPCI->configRead16(kIOPCIConfigCommand);
	for (unsigned i = 0; i < BWFM_SAVED_CONFIG; i++)
		fSavedConfig[i] = fPCI->configRead32(kSavedConfig[i]);
	fConfigSaved = true;
}

// true when something had to be written back, which means the function lost its state
bool SFBwfm::restoreConfig()
{
	bool lost = false;

	if (!fConfigSaved)
		return false;
	for (unsigned i = 0; i < BWFM_SAVED_CONFIG; i++) {
		if (fPCI->configRead32(kSavedConfig[i]) != fSavedConfig[i]) {
			fPCI->configWrite32(kSavedConfig[i], fSavedConfig[i]);
			lost = true;
		}
	}
	if (fPCI->configRead16(kIOPCIConfigCommand) != fSavedCommand) {
		fPCI->configWrite16(kIOPCIConfigCommand, fSavedCommand);
		lost = true;
	}
	return lost;
}

// The firmware is stopped and everything it used is released (how: kDown*).
// Order: interface stop, D3 inform, chip masked and reset, buffers and mappings released
// (all in sf_bwfm_down: the last BAR and DART access), config space saved. Beyond a plain
// halt: interrupt sources paused (the poll reads the DART), the bus told to keep off the
// registers, and for kDownPower the port shut down and the radio's power off in the bridge.
// For sleep the port is only counted as down: the SoC may take its power, so nothing reaches
// for the chip or the DART until chipUp has asked the bridge.
IOReturn SFBwfm::chipDown(int how, const char *why)
{
	IOReturn ret = kIOReturnSuccess;

	IOLockLock(fLifecycle);
	if (fPortDown || fStopping || !fAttached) {
		IOLockUnlock(fLifecycle);
		return kIOReturnSuccess;
	}
	LOG("%s: stopping the firmware%s", why, how == kDownPower ? ", then the port and the radio's power" : "");
	int d3 = sf_bwfm_down(fDev, 1);
	saveConfig();
	fChipDown = true;
	LOG("%s: firmware stopped (D3 inform %s), chip reset, config space saved", why,
	    d3 == 0 ? "acknowledged" : "not acknowledged");
	if (how != kDownHalt) {
		pauseInterrupts();
		sf_bwfm_set_gone(fDev, 1);
	}
	if (how == kDownSleep) {
		fPortDown = true;
		LOG("%s: interrupt sources paused, chip and DART not touched until the bridge has the port back", why);
	} else if (how == kDownPower) {
		UInt32 portDown = 0;

		// flags bit 0: also switch the radio's power off through the SMC
		ret = bridgeCall("SFApciePortShutdown", (void *)(uintptr_t)1, &portDown);
		fPortDown = portDown != 0;
		if (!fPortDown) {
			LOG("%s: the port is NOT shut down, the chip stays reachable", why);
			sf_bwfm_set_gone(fDev, 0);
			resumeInterrupts();
		} else {
			LOG("%s: port shut down%s", why, ret == kIOReturnSuccess ? ", radio power off" :
			    ", radio power off FAILED");
		}
	}
	IOLockUnlock(fLifecycle);
	return ret;
}

// The way back up to the point where the firmware can be loaded: bwfm_init does that when
// the interface is brought up. Order: port (the bridge brings the link up if it is down),
// function there, config space, DART, MSI, then the bus may touch registers again and the
// interrupt sources run. Nothing is written that still has its value.
IOReturn SFBwfm::chipUp(bool askBridge, const char *why)
{
	IOReturn ret = kIOReturnSuccess;

	IOLockLock(fLifecycle);
	if (fStopping || !fAttached || (!fChipDown && !fPortDown)) {
		IOLockUnlock(fLifecycle);
		return kIOReturnSuccess;
	}
	if (fPortDown || askBridge) {
		UInt32 linkUp = 0;

		LOG("%s: asking the bridge for the port", why);
		ret = bridgeCall("SFApciePortResume", &linkUp, NULL);
		if (ret != kIOReturnSuccess || !linkUp) {
			LOG("%s: no link, the chip stays down and is not touched", why);
			// from here on nothing may reach for a register, whatever state we were in
			pauseInterrupts();
			sf_bwfm_set_gone(fDev, 2);
			fPortDown = true;
			IOLockUnlock(fLifecycle);
			return ret != kIOReturnSuccess ? ret : kIOReturnNotReady;
		}
	}
	UInt32 id = fPCI->configRead32(kIOPCIConfigVendorID);
	if (id != fSavedID) {
		LOG("%s: the function reads id 0x%08x instead of 0x%08x, the chip stays down and is not touched",
		    why, id, fSavedID);
		pauseInterrupts();
		sf_bwfm_set_gone(fDev, 2);
		fPortDown = true;
		IOLockUnlock(fLifecycle);
		return kIOReturnNoDevice;
	}
	bool lost = restoreConfig();
	int dart = sf_bwfm_dma_resume();
	if (lost && fIntSrc != NULL) {
		UInt32 vector = 0;
		// the bridge gives the same vector again and writes the MSI capability
		ret = bridgeCall("SFApcieSetupMSI", fPCI, &vector);
		if (ret != kIOReturnSuccess)
			LOG("%s: no MSI any more, the poll timer serves the chip", why);
		ret = kIOReturnSuccess;
	}
	LOG("%s: chip reachable, config space %s, DART %s", why, lost ? "was lost and is restored" : "intact",
	    dart == 0 ? "translating" : "NOT translating");
	if (dart != 0) {
		// no DMA without the DART: the firmware must not be loaded
		LOG("%s: no DMA without the DART, the chip stays down", why);
		sf_bwfm_set_gone(fDev, 1);
		fChipDown = true;
		IOLockUnlock(fLifecycle);
		return kIOReturnNotReady;
	}
	sf_bwfm_set_gone(fDev, 0);
	fPortDown = false;
	fChipDown = false;
	resumeInterrupts();
	IOLockUnlock(fLifecycle);
	return ret;
}

IOReturn SFBwfm::enable(IONetworkInterface *interface)
{
	// every way to a running interface goes through chipUp once the chip was taken down
	if ((fChipDown || fPortDown) && chipUp(false, "interface up") != kIOReturnSuccess)
		return kIOReturnNotReady;
	return super::enable(interface);
}

// The requests that change the radio pass through here: power is where bwfm_off takes
// effect, and once the chip was taken down every other one first needs it back.
// FXNET: APPLE80211_IOC_POWERSAVE carries SiliconForest's own layout, not Apple's: a set takes
// version, mode (0 constantly awake, 1 PS-Poll, 2 fast power save) and pm2_ret_ms (0 keeps
// it); a get fills in what init will give the firmware and what the firmware says now (-1 when
// it was not running). sf-wifipm (m1-bwfm/tools) is the userspace side.
struct sf80211_powersave_data {
	uint32_t	version;
	uint32_t	mode;
	uint32_t	pm2_ret_ms;
	int32_t		fw_mode;
	int32_t		fw_pm2_ret_ms;
};

SInt32 SFBwfm::apple80211Request(bool set, int type, int value, void *data, uint32_t *len)
{
	if (type == APPLE80211_IOC_POWERSAVE && fIC != NULL && fDev != NULL && len != NULL && data != NULL) {
		struct sf80211_powersave_data *ps = (struct sf80211_powersave_data *)data;

		if (set) {
			if (*len < 3 * sizeof(uint32_t) || ps->version != APPLE80211_VERSION ||
			    ps->mode > 2 || ps->pm2_ret_ms > 60000)
				return EINVAL;
			if ((fChipDown || fPortDown) && chipUp(false, "power save") != kIOReturnSuccess)
				return EIO;
			LOG("power save: mode %u, pm2_sleep_ret %u ms requested", ps->mode, ps->pm2_ret_ms);
			return sf_bwfm_set_pm(fDev, (int)ps->mode, (int)ps->pm2_ret_ms);
		}
		if (*len < sizeof(*ps))
			return EINVAL;
		int fwMode, fwRet, wantMode, wantRet;
		sf_bwfm_get_pm(fDev, &fwMode, &fwRet, &wantMode, &wantRet);
		ps->version = APPLE80211_VERSION;
		ps->mode = (uint32_t)wantMode;
		ps->pm2_ret_ms = (uint32_t)wantRet;
		ps->fw_mode = fwMode;
		ps->fw_pm2_ret_ms = fwRet;
		*len = sizeof(*ps);
		return 0;
	}
	if (!set || fIC == NULL)
		return super::apple80211Request(set, type, value, data, len);
	if (type == APPLE80211_IOC_POWER && len != NULL && *len >= sizeof(struct apple80211_power_data)) {
		bool on = ((struct apple80211_power_data *)data)->power_state != 0;
		SInt32 error;

		if (on) {
			LOG("radio on");
			if ((fChipDown || fPortDown) && chipUp(false, "radio on") != kIOReturnSuccess)
				return EIO;
			error = super::apple80211Request(set, type, value, data, len);
			if (error == 0)
				LOG("radio on: interface running");
			else
				LOG("radio on: FAILED (error %d), last control message: %s", error, sf_bwfm_ctlwhy(fDev));
			return error;
		}
		LOG("radio off, mode %d", fOffMode);
		error = fPortDown ? 0 : super::apple80211Request(set, type, value, data, len);
		if (fOffMode >= kOffHalt)
			chipDown(fOffMode == kOffPower ? kDownPower : kDownHalt, "radio off");
		LOG("radio off: done (interface stop result %d)", error);
		return error;
	}
	// scan, join and the like start the interface themselves
	if ((fChipDown || fPortDown) && chipUp(false, "request") != kIOReturnSuccess)
		return EIO;
	return super::apple80211Request(set, type, value, data, len);
}

// System sleep and wake. UNTESTED: the guest under the hypervisor cannot sleep. Priority
// clients get the capability change on current kernels and the classic messages on older
// ones; both are taken, fAsleep makes the second of a pair a no-op. Returning without
// touching the argument acknowledges at once: the work on the way down is done right here,
// so the system does not go on to sleep before the firmware is quiet.
IOReturn SFBwfm::sleepWake(void *target, void *refCon, UInt32 messageType, IOService *provider,
    void *messageArgument, vm_size_t argSize)
{
	SFBwfm *self = (SFBwfm *)target;
	bool sleep = messageType == kIOMessageSystemWillSleep;
	bool wake = messageType == kIOMessageSystemHasPoweredOn;

	if (messageType == kIOMessageSystemCapabilityChange &&
	    argSize == sizeof(IOPMSystemCapabilityChangeParameters)) {
		IOPMSystemCapabilityChangeParameters *params = (IOPMSystemCapabilityChangeParameters *)messageArgument;
		bool fromCPU = (params->fromCapabilities & kIOPMSystemCapabilityCPU) != 0;
		bool toCPU = (params->toCapabilities & kIOPMSystemCapabilityCPU) != 0;

		if ((params->changeFlags & kIOPMSystemCapabilityWillChange) && fromCPU && !toCPU)
			sleep = true;
		if ((params->changeFlags & kIOPMSystemCapabilityDidChange) && !fromCPU && toCPU)
			wake = true;
	}
	if (sleep)
		self->systemSleep();
	else if (wake)
		self->systemWake();
	return kIOReturnSuccess;
}

void SFBwfm::systemSleep()
{
	if (fAsleep || fStopping)
		return;
	fAsleep = true;
	fSleepTookDown = false;
	if (!fAttached || fPortDown) {
		LOG("sleep: the port is down already, nothing to do");
		return;
	}
	// also with the firmware stopped by a radio off: the poll must not read the DART
	// across a sleep, and the wake has to ask the bridge for the port
	fResumeUp = f80211 && !fChipDown && sf_bwfm_running(fDev) != 0;
	LOG("sleep: interface %s", fResumeUp ? "running, it comes back on wake" : "down, it stays down on wake");
	chipDown(kDownSleep, "sleep");
	fSleepTookDown = true;
}

void SFBwfm::systemWake()
{
	thread_t thread;

	if (!fAsleep || fStopping)
		return;
	fAsleep = false;
	if (!fSleepTookDown) {
		LOG("wake: the port was down before sleep, nothing to do");
		return;
	}
	fSleepTookDown = false;
	// loading the firmware takes seconds and needs the file system: not on the power thread
	OSIncrementAtomic(&fThreads);
	retain();
	if (kernel_thread_start(&SFBwfm::wakeThread, this, &thread) != KERN_SUCCESS) {
		release();
		OSDecrementAtomic(&fThreads);
		LOG("wake: no thread, the radio stays off until it is switched on");
		return;
	}
	thread_deallocate(thread);
}

void SFBwfm::wakeThread(void *arg, wait_result_t wr)
{
	SFBwfm *self = (SFBwfm *)arg;

	LOG("wake: bringing the chip back");
	if (self->chipUp(true, "wake") != kIOReturnSuccess)
		LOG("wake: the chip did not come back, the radio stays off");
	else if (!self->fResumeUp)
		LOG("wake: chip back, the interface was down and stays down");
	else if (self->fStopping)
		LOG("wake: the driver is stopping");
	else
		LOG("wake: interface %s", sf_bwfm_up(self->fDev) == 0 ? "running again" : "did NOT come up");
	OSDecrementAtomic(&self->fThreads);
	self->release();
}

const OSString *SFBwfm::newVendorString() const
{
	return OSString::withCString("Broadcom");
}

const OSString *SFBwfm::newModelString() const
{
	return OSString::withCString("BCM4378 802.11ax");
}

// openbsd runs the interrupt handler at IPL_NET, here it runs on the poll work loop under the stack lock
void SFBwfm::pollAction(OSObject *owner, IOTimerEventSource *sender)
{
	static unsigned ticks;
	SFBwfm *self = (SFBwfm *)owner;

	// paused while the port is down: neither the chip nor the DART may be read then
	if (!self->fPolling)
		return;
	// a DART fault is logged and cleared here; with MSI this timer is only a slow safety net
	if (++ticks % 16 == 0)
		sf_dart_pool_check("poll");
	sf80211_lock();
	if (self->fIntrFn != NULL)
		self->fIntrFn(self->fIntrArg);
	sf80211_unlock();
	if (self->fPolling)
		sender->setTimeoutUS(self->fPollUS);
}

// the openbsd calls bwfm makes

extern "C" {

// every file asked for is logged with its result, the names are bwfm's fallback chain
int loadfirmware(const char *name, u_char **bufp, size_t *sizep)
{
	char path[MAXPATHLEN];
	vfs_context_t ctx;
	vnode_t vp = NULLVP;
	struct vnode_attr va;
	u_char *buf = NULL;
	size_t len = 0;
	int error, resid = 0;

	snprintf(path, sizeof(path), "%s/%s", SF_BWFM_FIRMWARE_DIR, name);
	if (!rootMounted()) {
		LOG("firmware file %s: no root file system", path);
		return ENOENT;
	}
	ctx = vfs_context_create(NULL);
	error = vnode_open(path, FREAD, 0, 0, &vp, ctx);
	if (error != 0) {
		vfs_context_rele(ctx);
		LOG("firmware file %s: not found (error %d)", path, error);
		return error;
	}

	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	error = vnode_getattr(vp, &va, ctx);
	if (error == 0 && (va.va_data_size == 0 || va.va_data_size > 32 * 1024 * 1024))
		error = EFBIG;
	if (error == 0) {
		len = (size_t)va.va_data_size;
		buf = (u_char *)sf80211_malloc(len, 2, 0);
		if (buf == NULL)
			error = ENOMEM;
	}
	if (error == 0) {
		error = vn_rdwr(UIO_READ, vp, (caddr_t)buf, (int)len, 0, UIO_SYSSPACE, 0,
		    vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
		if (error == 0 && resid != 0)
			error = EIO;
	}

	vnode_close(vp, FREAD, ctx);
	vfs_context_rele(ctx);
	if (error != 0) {
		if (buf != NULL)
			sf80211_free(buf, 2, len);
		LOG("firmware file %s: found but unreadable (error %d)", path, error);
		return error;
	}
	LOG("firmware file %s: found, %zu bytes", path, len);
	*bufp = buf;
	*sizep = len;
	return 0;
}

// a property of the Apple device tree, as XNU publishes it in the IORegistry
int sf_bwfm_adt_prop(const char *path, const char *name, void *buf, int len)
{
	IORegistryEntry *entry = IORegistryEntry::fromPath(path, gIODTPlane);
	OSData *data;
	int n = -1;

	if (entry == NULL)
		return -1;
	data = OSDynamicCast(OSData, entry->getProperty(name));
	if (data != NULL) {
		n = (int)data->getLength();
		if (buf != NULL && len > 0)
			memcpy(buf, data->getBytesNoCopy(), n < len ? n : len);
	}
	entry->release();
	return n;
}

int sf_bwfm_bootarg(const char *name, char *buf, int len)
{
	char value[64];

	if (!PE_parse_boot_argn(name, value, sizeof(value)) || value[0] == '\0')
		return 0;
	strlcpy(buf, value, len);
	return 1;
}

int sf_bwfm_want_fwsup(void)
{
	int value = 1;

	PE_parse_boot_argn("bwfm_fwsup", &value, sizeof(value));
	return value != 0;
}

// Off by default: the driver never acknowledged the request on hardware, and with the
// acknowledgement the firmware may really go to sleep, also while associated
int sf_bwfm_want_dsack(void)
{
	int value = 0;

	PE_parse_boot_argn("bwfm_dsack", &value, sizeof(value));
	return value != 0;
}

// FXNET: replies that come more than the firmware's 200 ms after our last frame waited for
// the next DTIM beacon in fast power save (LAN ping at 1 s: p50 159 ms, max 318 ms)
int sf_bwfm_want_pm(void)
{
	int value = 2;

	PE_parse_boot_argn("bwfm_pm", &value, sizeof(value));
	return value >= 0 && value <= 2 ? value : 2;
}

int sf_bwfm_want_pm2_ret(void)
{
	int value = 2000;

	PE_parse_boot_argn("bwfm_pm2_ret", &value, sizeof(value));
	return value >= 0 && value <= 60000 ? value : 2000;
}

// config space, the tag is the IOPCIDevice

pcireg_t pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	return ((IOPCIDevice *)tag)->configRead32(reg);
}

void pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t val)
{
	((IOPCIDevice *)tag)->configWrite32(reg, val);
}

// bwfm maps two BARs, the register window (0x10) and the chip's memory (0x18)
int pci_mapreg_map(struct pci_attach_args *pa, int reg, pcireg_t type, int flags,
    bus_space_tag_t *tagp, bus_space_handle_t *handlep, bus_addr_t *basep,
    bus_size_t *sizep, bus_size_t maxsize)
{
	IOPCIDevice *pci = (IOPCIDevice *)pa->pa_tag;
	SFBwfm *owner = ownerOf(pci);
	IOMemoryMap *map;
	int slot = -1;

	if (owner == NULL)
		return ENXIO;
	for (int i = 0; i < BWFM_MAX_BARS && slot < 0; i++) {
		if (owner->fBars[i] == NULL)
			slot = i;
	}
	if (slot < 0)
		return ENOMEM;
	// Apple PCIe windows must be mapped posted-write (Device-nGnRE): a non-posted write faults (SError)
	map = pci->mapDeviceMemoryWithRegister((UInt8)reg, kIOMapPostedWrite);
	if (map == NULL) {
		LOG("attach: BAR at config 0x%02x did not map", reg);
		return ENOMEM;
	}
	owner->fBars[slot] = map;
	LOG("attach: BAR at config 0x%02x mapped, physical 0x%llx, %llu bytes", reg,
	    (unsigned long long)map->getPhysicalAddress(), (unsigned long long)map->getLength());
	if (tagp != NULL)
		*tagp = 0;
	if (handlep != NULL)
		*handlep = (bus_space_handle_t)map->getVirtualAddress();
	if (basep != NULL)
		*basep = map->getPhysicalAddress();
	if (sizep != NULL)
		*sizep = map->getLength();
	return 0;
}

int pci_matchbyid(struct pci_attach_args *pa, const struct pci_matchid *ids, int n)
{
	for (int i = 0; i < n; i++) {
		if (PCI_VENDOR(pa->pa_id) == ids[i].pm_vid && PCI_PRODUCT(pa->pa_id) == ids[i].pm_pid)
			return 1;
	}
	return 0;
}

// "establishing" the interrupt sets up one MSI vector and a slow fallback poll (fast poll if MSI is unavailable)

int pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ih)
{
	ih->ih_index = 0;
	return 0;
}

int pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ih)
{
	ih->ih_index = 0;
	return 0;
}

const char *pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	return "msi";
}

void *pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*fn)(void *), void *arg, const char *name)
{
	SFBwfm *owner = ownerOf(pc);

	if (owner == NULL || owner->fPollTimer != NULL)
		return NULL;
	owner->fIntrFn = fn;
	owner->fIntrArg = arg;
	owner->fPollTimer = IOTimerEventSource::timerEventSource(owner, &SFBwfm::pollAction);
	if (owner->fPollTimer == NULL ||
	    owner->fPollLoop->addEventSource(owner->fPollTimer) != kIOReturnSuccess) {
		OSSafeReleaseNULL(owner->fPollTimer);
		return NULL;
	}
	owner->fPolling = true;
	owner->fPollUS = SF_BWFM_POLL_US;
	if (owner->setupMSI())
		owner->fPollUS = SF_BWFM_MSI_FALLBACK_POLL_US;
	owner->fPollTimer->enable();
	owner->fPollTimer->setTimeoutUS(owner->fPollUS);
	LOG("attach: %s, handler also polled every %u us", owner->fIntSrc ? "MSI interrupt" : "no interrupt", owner->fPollUS);
	return owner;
}

void SFBwfm::intrAction(OSObject *owner, IOInterruptEventSource *sender, int count)
{
	SFBwfm *self = (SFBwfm *)owner;

	if (self->fIntrCount++ == 0)
		LOG("first MSI interrupt delivered");
	if (!self->fPolling)
		return;
	sf80211_lock();
	if (self->fIntrFn != NULL)
		self->fIntrFn(self->fIntrArg);
	sf80211_unlock();
}

/*
 * One MSI vector, allocated and programmed by the host bridge (SFApcieSetupMSI), which
 * also describes the interrupt on this service. The handler then hangs off an ordinary
 * IOInterruptEventSource on this service, not the PCI nub, whose interrupt state
 * IOPCIFamily already resolved without a controller.
 */
bool SFBwfm::setupMSI()
{
	const OSSymbol *fn = OSSymbol::withCString("SFApcieSetupMSI");
	UInt32 vector = 0;
	IOReturn ret = fn ? fPCI->callPlatformFunction(fn, true, this, fPCI, &vector, NULL) : kIOReturnNoMemory;

	OSSafeReleaseNULL(fn);
	if (ret != kIOReturnSuccess) {
		LOG("msi: the host bridge gave no vector (0x%x)", ret);
		return false;
	}
	fHaveMSI = true;
	fIntSrc = IOInterruptEventSource::interruptEventSource(this, &SFBwfm::intrAction, this, 0);
	if (fIntSrc == NULL || fPollLoop->addEventSource(fIntSrc) != kIOReturnSuccess) {
		LOG("msi: no interrupt event source for AIC vector %u", vector);
		OSSafeReleaseNULL(fIntSrc);
		return false;
	}
	fIntSrc->enable();
	LOG("msi: AIC vector %u from the host bridge", vector);
	return true;
}


void pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	SFBwfm *owner = (SFBwfm *)cookie;

	if (owner == NULL || owner->fPollTimer == NULL)
		return;
	owner->fPolling = false;
	owner->fPollTimer->cancelTimeout();
}

}
