// SPDX-License-Identifier: GPL-2.0
/*
 * SFAppleAVD: the Apple Video Decoder (AVD) of Apple T8103 as /dev/sfavd.
 *
 * Port of Linux drivers/media/platform/apple/avd (omacom/linux-aurora, integration/aurora,
 * 6a08a08e). The codec files and the hardware file are the Linux files, unchanged
 * (linux/); sf_avd_core.c is avd-drv.c and avd-v4l2.c without V4L2. This file is the
 * platform side: power, register mappings, the decoder's DART, the firmware image, the
 * mailbox interrupt, and the device node with its five ioctls (<sfavd/sfavd_ioctl.h>).
 *
 * Hardware (Apple device tree; Linux t8103.dtsi "avd@269080000"):
 *   /arm-io/avd       reg 0x268000000 + 0x1404000; the driver uses
 *                       code  +0x1080000 (0xc000)   the Cortex-M3's program memory
 *                       mbox  +0x1098000 (0x4000)   mailbox, run control
 *                       ctrl  +0x1100000 (0x10000)  decode control, instruction FIFOs
 *                     clock-gates 0x12b 0x12c 0x12d, interrupts 540 541 (541: mailbox 1)
 *   /arm-io/dart-avd  reg 0x269010000, clock-gates 0x145, stream 0
 *
 * Firmware: the decoder's Cortex-M3 runs the open firmware of
 * https://github.com/aurora-silicon/avd-fw (MIT), image avd-fw-v2-t0.bin (49152 bytes),
 * read from /Library/Firmware/apple/ when the device is first used and copied into the
 * code region at every start, as Linux does.
 *
 * Off unless sfavd=1 (or more, for logging: 2 info, 3 debug) is in the boot arguments.
 * Nothing is powered or touched before the first open of /dev/sfavd.
 *
 * The node is root-only: the codec files trust the control structures to the degree the
 * V4L2 control framework validates them in Linux, and that validation is not ported.
 */
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOInterruptController.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOService.h>
#include <kern/clock.h>
#include <kern/task.h>
#include <kern/task_ref.h>
#include <kern/thread.h>
#include <libkern/OSAtomic.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include "sf_avd_core.h"
#include "../include/sfavd/sfavd_import_validate.h"
#include "../include/sfavd/sfavd_import_pages.h"
#include "sf_avd_dart.h"
#include "sf_soc.h"

extern "C" {
vnode_t vfs_rootvnode(void);
task_t proc_task(proc_t); /* retained XNU BSD/KPI signature; no SDK header copy */
}

#define LOG(fmt, ...) IOLog("SFAppleAVD: " fmt "\n", ##__VA_ARGS__)

#define T8103_CHIP_ID   0x8103
#define AVD_NODE        "/arm-io/avd"
#define DART_NODE       "/arm-io/dart-avd"
#define FIRMWARE_PATH   "/Library/Firmware/apple/avd-fw-v2-t0.bin"
#define AVD_RESET_NAME  "AVD_SYS"
#define OFF_CODE        0x1080000
#define OFF_MBOX        0x1098000
#define OFF_CTRL        0x1100000
#define MAX_CONTEXTS    8
#define MAX_CTRL_BYTES  (1u << 20)

class SFAppleAVD : public IOService {
	OSDeclareDefaultStructors(SFAppleAVD);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(SFAppleAVD, IOService);

struct Context {
	struct sf_avd_session *session;
	struct sfavd_format    format;
	pid_t                  owner;
    task_t ownerTask;
	uint32_t               buffers;
	IOMemoryMap           *maps[SFAVD_MAX_BUFFERS];
    bool submitted, imported;
};

struct DmaCookie {
	IOMemoryDescriptor *md;
    IOMemoryMap *kernelMap;
    bool prepared;
	uint64_t                  iova;
	size_t                    bytes;
    DmaCookie *quarantineNext;
    bool quarantined;
};

static DmaCookie *gQuarantined; /* prepared pins/maps deliberately retained until reboot */
static SFAppleAVD  *gService;
static IOLock      *gLock;          /* every ioctl, open and close */
static IOLock      *gWait;          /* the completion wait */
static uint32_t     gLevel;         /* sfavd=: 1 errors and warnings, 2 info, 3 debug */
static int          gMajor = -1;
static void        *gDevNode;
static SFMmio       gRegs;
static SFAvdDart    gDart;
static struct sf_avd_device *gDev;
static uint8_t     *gFirmware;
static size_t       gFirmwareBytes;
static char         gFirmwareText[48];
static char         gState[160] = "idle: not used yet";
static bool         gUp, gIrqOk;
static uint32_t     gIrqVector, gOpens, gResets;
static volatile SInt32 gWakes;
static volatile UInt32 gIrqs;
static Context      gContexts[MAX_CONTEXTS];

/* ---- sf_avd_os.h ----------------------------------------------------------------------- */

extern "C" void *
sf_avd_os_zalloc(size_t bytes)
{
	size_t total = bytes + 16;
	uint64_t *p = (uint64_t *)IOMalloc(total);

	if (p == NULL)
		return NULL;
	bzero(p, total);
	p[0] = total;
	return p + 2;
}

extern "C" void
sf_avd_os_free(void *ptr)
{
	if (ptr != NULL) {
		uint64_t *p = (uint64_t *)ptr - 2;
		IOFree(p, (size_t)p[0]);
	}
}

extern "C" void
sf_avd_os_log(int level, const char *fmt, ...)
{
	char line[256];
	va_list ap;

	if ((uint32_t)level > gLevel)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	IOLog("SFAppleAVD: avd: %s\n", line);
}

extern "C" void
sf_avd_os_usleep(uint32_t us)
{
	if (us >= 1000)
		IOSleep(us / 1000);
	else
		IODelay(us);
}

extern "C" uint64_t
sf_avd_os_now_us(void)
{
	uint64_t ns = 0;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000;
}

extern "C" uint32_t
sf_avd_os_readl(const void *addr)
{
	uint32_t v = *(const volatile uint32_t *)addr;
	OSSynchronizeIO();
	return v;
}

extern "C" void
sf_avd_os_writel(uint32_t value, void *addr)
{
	*(volatile uint32_t *)addr = value;
	OSSynchronizeIO();
}

extern "C" void
sf_avd_os_memcpy_toio(void *to, const void *from, size_t bytes)
{
	volatile uint32_t *d = (volatile uint32_t *)to;
	const uint8_t *s = (const uint8_t *)from;

	for (size_t i = 0; i + 4 <= bytes; i += 4) {
		uint32_t w;
		memcpy(&w, s + i, 4);
		d[i / 4] = w;
	}
	OSSynchronizeIO();
}

extern "C" void *
sf_avd_os_lock_alloc(void)
{
	return IOSimpleLockAlloc();
}

extern "C" void
sf_avd_os_lock_free(void *lock)
{
	if (lock != NULL)
		IOSimpleLockFree((IOSimpleLock *)lock);
}

extern "C" unsigned long
sf_avd_os_lock(void *lock)
{
	return (unsigned long)IOSimpleLockLockDisableInterrupt((IOSimpleLock *)lock);
}

extern "C" void
sf_avd_os_unlock(void *lock, unsigned long flags)
{
	IOSimpleLockUnlockEnableInterrupt((IOSimpleLock *)lock, (IOInterruptState)flags);
}

extern "C" void *
sf_avd_os_dma_alloc(size_t bytes, uint64_t *iova, void **cookie)
{
	DmaCookie *c = (DmaCookie *)IOMalloc(sizeof(DmaCookie));

	if (c == NULL)
		return NULL;
    bzero(c,sizeof(*c));
	c->bytes = bytes;
    c->kernelMap=NULL;c->prepared=true;
    IOBufferMemoryDescriptor *memory=gDart.alloc(bytes, &c->iova);
	c->md = memory;
	if (c->md == NULL) {
		IOFree(c, sizeof(DmaCookie));
		return NULL;
	}
	*iova = c->iova;
	*cookie = c;
	return memory->getBytesNoCopy();
}

static void quarantine_dma(DmaCookie *c)
{
    if(c->quarantined)return;
    c->quarantined=true;c->quarantineNext=gQuarantined;gQuarantined=c;
    gDart.quarantinedDescriptors++;
    gDart.quarantinedBytes+=(c->bytes+16383)&~(size_t)16383;
}
extern "C" void
sf_avd_os_dma_free(void *cookie)
{
    DmaCookie *c=(DmaCookie*)cookie;if(!c)return;
    if(gDart.poisoned || (c->md&&c->iova&&gDart.unmapMemory(c->iova,c->bytes))) {
        /* Never complete/unmap/release pinned backing unless invalidation is
         * established. No further AVD jobs are admitted after poison. */
        gDart.poisoned=true;
        quarantine_dma(c);return;
    }
    if(c->kernelMap){c->kernelMap->unmap();c->kernelMap->release();}
    if(c->md){if(c->prepared)c->md->complete();c->md->release();}
    IOFree(c,sizeof(DmaCookie));
}

extern "C" void
sf_avd_os_job_wake(__unused struct sf_avd_device *dev)
{
	OSIncrementAtomic(&gWakes);
	thread_wakeup((event_t)&gWakes);
}

/*
 * The wake comes from the interrupt handler, which cannot take gWait: a wake between the
 * test and the sleep is not seen by the sleep, so the sleep is cut into 2 ms pieces.
 */
extern "C" bool
sf_avd_os_job_wait(__unused struct sf_avd_device *dev, uint32_t timeout_ms)
{
	uint64_t deadline_us = sf_avd_os_now_us() + (uint64_t)timeout_ms * 1000;

	for (;;) {
		SInt32 wakes = gWakes;

		if (wakes > 0) {
			if (OSCompareAndSwap((UInt32)wakes, (UInt32)(wakes - 1), (volatile UInt32 *)&gWakes))
				return true;
			continue;
		}
		if (sf_avd_os_now_us() >= deadline_us)
			return false;
		IOLockLock(gWait);
		if (gWakes == 0) {
			AbsoluteTime deadline;
			clock_interval_to_deadline(2, kMillisecondScale, &deadline);
			IOLockSleepDeadline(gWait, (void *)&gWakes, deadline, THREAD_UNINT);
		}
		IOLockUnlock(gWait);
	}
}

extern "C" int
sf_avd_os_reset(__unused struct sf_avd_device *dev)
{
	int ret;

	gResets++;
	gDart.check("before the reset");
	LOG("resetting the decoder: mailbox status 0x%08x, irq enable 0x%08x, flag0 0x%08x; %u interrupts so far",
	    gRegs.r32(OFF_MBOX + 0x5c), gRegs.r32(OFF_MBOX + 0x48), gRegs.r32(OFF_MBOX + 0x90), gIrqs);
	ret = sf_pmgr_reset_named(AVD_RESET_NAME, true);
	if (ret != 0)
		return -ret;
	ret = gDart.reprogram();
	return -ret;
}

/* ---- bring-up -------------------------------------------------------------------------- */

static void
set_state(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(gState, sizeof(gState), fmt, ap);
	va_end(ap);
	LOG("%s", gState);
}

static void
avd_interrupt(__unused OSObject *target, __unused void *refCon, __unused IOService *nub, __unused int source)
{
	OSIncrementAtomic((volatile SInt32 *)&gIrqs);
	if (gDev != NULL)
		sf_avd_irq(gDev);
}

/* The whole file in IOMalloc memory; 0 or an errno value. */
static int
read_file(const char *path, uint8_t **out, size_t *size)
{
	vfs_context_t ctx = vfs_context_create(NULL);
	vnode_t vp = NULLVP;
	struct vnode_attr va;
	uint8_t *buf = NULL;
	size_t len = 0;
	int resid = 0;
	int error = vnode_open(path, FREAD, 0, 0, &vp, ctx);

	if (error != 0) {
		vfs_context_rele(ctx);
		return error;
	}
	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	error = vnode_getattr(vp, &va, ctx);
	if (error == 0 && (va.va_data_size < 0x1000 || va.va_data_size > 0xc000 || (va.va_data_size & 3)))
		error = EFBIG;
	if (error == 0) {
		len = (size_t)va.va_data_size;
		buf = (uint8_t *)IOMalloc(len);
		if (buf == NULL)
			error = ENOMEM;
	}
	if (error == 0) {
		error = vn_rdwr(UIO_READ, vp, (caddr_t)buf, (int)len, 0, UIO_SYSSPACE, 0, vfs_context_ucred(ctx), &resid,
		    vfs_context_proc(ctx));
		if (error == 0 && resid != 0)
			error = EIO;
	}
	vnode_close(vp, FREAD, ctx);
	vfs_context_rele(ctx);
	if (error != 0) {
		if (buf != NULL)
			IOFree(buf, len);
		return error;
	}
	*out = buf;
	*size = len;
	return 0;
}

/* Power, mappings, DART, firmware image, interrupt. gLock held. 0 or an errno value. */
static int
bring_up(void)
{
	uint32_t vector = 0;
	int err;

	if (gUp)
		return 0;
	if (gFirmware == NULL) {
		err = read_file(FIRMWARE_PATH, &gFirmware, &gFirmwareBytes);
		if (err != 0) {
			set_state("no firmware: %s: error %d", FIRMWARE_PATH, err);
			return ENOENT;
		}
		if (gFirmwareBytes >= 0x40 && memcmp(gFirmware + gFirmwareBytes - 0x40, "CODECS:", 7) == 0)
			snprintf(gFirmwareText, sizeof(gFirmwareText), "%.40s", (const char *)gFirmware + gFirmwareBytes - 0x40 + 7);
		else
			snprintf(gFirmwareText, sizeof(gFirmwareText), "(no codec list)");
	}
	if (sf_pmgr_enable_node(AVD_NODE, -1, 0, gLevel >= 2) != 0) {
		set_state("failed: the decoder's power domains did not turn on");
		return ENXIO;
	}
	if (gRegs.va == NULL && !gRegs.mapNode(AVD_NODE, 0)) {
		set_state("failed: no register mapping for %s", AVD_NODE);
		return ENXIO;
	}
	if (gRegs.len < OFF_CTRL + 0x10000) {
		set_state("failed: %s reg is 0x%llx bytes, too short", AVD_NODE, gRegs.len);
		return ENXIO;
	}
	err = gDart.attach(DART_NODE, 0);
	if (err != 0) {
		set_state("failed: DART %s: error %d", DART_NODE, err);
		return err;
	}
	if (gDev == NULL) {
		gDev = sf_avd_device_create("apple,t8103-avd", (void *)(gRegs.va + OFF_CODE), (void *)(gRegs.va + OFF_MBOX),
		    (void *)(gRegs.va + OFF_CTRL), gFirmware, gFirmwareBytes);
		if (gDev == NULL) {
			set_state("failed: no device object");
			return ENOMEM;
		}
	}
	if (!gIrqOk && sf_adt_u32(AVD_NODE, "interrupts", 1, &vector) && vector != 0) {
		IOInterruptVectorNumber number = vector;
		const OSSymbol *controller = gIODTDefaultInterruptController;
		OSData *spec = OSData::withBytes(&number, sizeof(number));
		OSArray *controllers = OSArray::withObjects((const OSObject **)&controller, 1);
		OSArray *specs = spec ? OSArray::withObjects((const OSObject **)&spec, 1) : NULL;

		gIrqVector = vector;
		gIrqOk = controllers && specs && gService->setProperty(gIOInterruptControllersKey, controllers) &&
		    gService->setProperty(gIOInterruptSpecifiersKey, specs) &&
		    gService->registerInterrupt(0, gService, avd_interrupt, NULL) == kIOReturnSuccess &&
		    gService->enableInterrupt(0) == kIOReturnSuccess;
		OSSafeReleaseNULL(spec);
		OSSafeReleaseNULL(controllers);
		OSSafeReleaseNULL(specs);
	}
	if (!gIrqOk) {
		set_state("failed: the mailbox interrupt (AIC %u) could not be registered", gIrqVector);
		return ENXIO;
	}
	gUp = true;
	set_state("ready: firmware %u bytes (%s), codecs 0x%x, registers at 0x%llx, hw version 0x%08x, AIC interrupt %u",
	    (uint32_t)gFirmwareBytes, gFirmwareText, sf_avd_device_caps(gDev), gRegs.phys, gRegs.r32(OFF_CTRL), gIrqVector);
	return 0;
}

/* ---- contexts -------------------------------------------------------------------------- */

static void
context_destroy(Context *c)
{
	if (c->session == NULL)
		return;
	for (unsigned i = 0; i < SFAVD_MAX_BUFFERS; i++) {
		if (c->maps[i] != NULL) {
			c->maps[i]->unmap();
			c->maps[i]->release();
			c->maps[i] = NULL;
		}
	}
	sf_avd_session_destroy(c->session);
    if(c->ownerTask)task_deallocate(c->ownerTask);
	bzero(c, sizeof(*c));
}

static unsigned
contexts_in_use(void)
{
	unsigned n = 0;

	for (unsigned i = 0; i < MAX_CONTEXTS; i++)
		n += gContexts[i].session != NULL;
	return n;
}

/* Contexts of processes that are gone (they could not close the node while others have it open). */
static void
reap_contexts(void)
{
	for (unsigned i = 0; i < MAX_CONTEXTS; i++) {
		Context *c = &gContexts[i];
		proc_t p;

		if (c->session == NULL)
			continue;
		p = proc_find(c->owner);
        bool sameTask=p && proc_task(p)==c->ownerTask;
        if(p)proc_rele(p);
        if(sameTask)continue;
		LOG("context %u: its original task for pid %d is gone; released", i, c->owner);
		/* the task's mappings went with the task */
		for (unsigned b = 0; b < SFAVD_MAX_BUFFERS; b++)
			OSSafeReleaseNULL(c->maps[b]);
		context_destroy(c);
	}
}

static int
copy_ctrl(const struct sfavd_ctrl *ctrl, uint8_t **out)
{
	uint8_t *p;
	int err;

	if (ctrl->size == 0 || ctrl->size > MAX_CTRL_BYTES || ctrl->ptr == 0)
		return EINVAL;
	p = (uint8_t *)IOMalloc(ctrl->size);
	if (p == NULL)
		return ENOMEM;
	err = copyin((user_addr_t)ctrl->ptr, p, ctrl->size);
	if (err != 0) {
		IOFree(p, ctrl->size);
		return err;
	}
	*out = p;
	return 0;
}

static int
do_create(struct sfavd_create *req)
{
	Context *c = NULL;
	uint8_t *ctrl = NULL;
	unsigned slot;
	int err, ret;

	if (req->buffers == 0 || req->buffers > SFAVD_MAX_BUFFERS)
		return EINVAL;
	reap_contexts();
	for (slot = 0; slot < MAX_CONTEXTS; slot++)
		if (gContexts[slot].session == NULL)
			break;
	if (slot == MAX_CONTEXTS)
		return EBUSY;
	c = &gContexts[slot];
	err = copy_ctrl(&req->format_ctrl, &ctrl);
	if (err != 0)
		return err;
	bzero(c, sizeof(*c));
	ret = sf_avd_session_create(gDev, &c->session);
	if (ret == 0)
		ret = sf_avd_session_set_format(c->session, req->codec, req->width, req->height, req->format_ctrl.id, ctrl,
		    req->format_ctrl.size, &c->format);
	if (ret == 0)
		ret = sf_avd_session_alloc(c->session, req->buffers);
	IOFree(ctrl, req->format_ctrl.size);
	if (ret != 0) {
		if (c->session != NULL)
			sf_avd_session_destroy(c->session);
		bzero(c, sizeof(*c));
		gDart.check("create");
		return -ret;
	}
	c->owner = proc_selfpid();
    c->ownerTask=current_task();task_reference(c->ownerTask);
	c->buffers = req->buffers;
	req->ctx = slot;
	req->format = c->format;
	if (gLevel >= 2)
		LOG("context %u for pid %d: %.4s %ux%u -> %.4s %ux%u, %u buffers of %u bytes; %llu KiB mapped for the decoder",
		    slot, c->owner, (const char *)&req->codec, req->width, req->height, (const char *)&c->format.pixelformat,
		    c->format.width, c->format.height, c->buffers, c->format.sizeimage, gDart.mappedBytes / 1024);
	return 0;
}

static Context *
context_of(uint32_t ctx)
{
	if (ctx >= MAX_CONTEXTS || gContexts[ctx].session == NULL)
		return NULL;
	return &gContexts[ctx];
}

static int
do_map(struct sfavd_map *req)
{
	Context *c = context_of(req->ctx);
	void *vaddr, *cookie;
	size_t size;

	if (c == NULL || req->index >= c->buffers || (c->owner != proc_selfpid() || c->ownerTask != current_task()))
		return EINVAL;
	if (sf_avd_session_buffer(c->session, req->index, &vaddr, &size, &cookie) != 0)
		return EINVAL;
	if (c->maps[req->index] == NULL) {
		DmaCookie *d = (DmaCookie *)cookie;

		c->maps[req->index] = d->md->createMappingInTask(current_task(), 0, kIOMapAnywhere | kIOMapReadOnly);
		if (c->maps[req->index] == NULL)
			return ENOMEM;
	}
	req->address = c->maps[req->index]->getAddress();
	req->size = size;
	return 0;
}

/* All capture backing is prepared before any session mutation. Both virtual
 * overlaps and physical aliases (including existing AVD-private pages) refuse.
 * gLock excludes decoder jobs throughout this transaction. */
static int do_import(struct sfavd_import *req)
{
    Context *c=context_of(req->ctx);
    DmaCookie *next[SFAVD_MAX_BUFFERS]={};
    void *cpu[SFAVD_MAX_BUFFERS]={},*cookies[SFAVD_MAX_BUFFERS]={},*old[SFAVD_MAX_BUFFERS]={};
    uint64_t iova[SFAVD_MAX_BUFFERS]={};
    uint64_t *physical=NULL;
    size_t pageCount=0,needed=0,at=0;
    int error=EINVAL;
    if (!c || (c->owner!=proc_selfpid() || c->ownerTask!=current_task())) return EINVAL;
    if (c->submitted || c->imported) return EBUSY;
    for(unsigned i=0;i<c->buffers;i++) if(c->maps[i]) return EBUSY;
    error=sfavd_import_validate(req,c->buffers,c->format.sizeimage);
    if(error)return error;
    needed=((size_t)c->format.sizeimage+16383)&~(size_t)16383;
    pageCount=(needed/16384)*req->count;
    if(pageCount>SF_AVD_DART_SPAN/16384)return ENOMEM;
    physical=(uint64_t*)IOMalloc(pageCount*sizeof(uint64_t));
    if(!physical)return ENOMEM;
    error=ENOMEM;
    for(unsigned i=0;i<req->count;i++) {
        next[i]=(DmaCookie*)IOMalloc(sizeof(DmaCookie));
        if(!next[i])goto done;
        bzero(next[i],sizeof(DmaCookie));next[i]->bytes=c->format.sizeimage;
        /* InOut requires a writable current-task memory reference, unlike Out
         * (see IOGeneralMemoryDescriptor::memoryReferenceProt/wireVirtual). */
        next[i]->md=IOMemoryDescriptor::withAddressRange(req->buffers[i].address,
            needed,kIODirectionInOut,current_task());
        if(!next[i]->md)goto done;
        if(next[i]->md->prepare(kIODirectionInOut)!=kIOReturnSuccess){error=EFAULT;goto done;}
        next[i]->prepared=true;
        next[i]->kernelMap=next[i]->md->createMappingInTask(kernel_task,0,kIOMapAnywhere);
        if(!next[i]->kernelMap){error=EFAULT;goto done;}
        cpu[i]=(void*)(uintptr_t)next[i]->kernelMap->getAddress();
        for(size_t offset=0;offset<needed;offset+=16384) {
            IOByteCount length=0;
            uint64_t pa=next[i]->md->getPhysicalSegment(offset,&length,kIOMemoryMapperNone);
            if(!pa || (pa&16383) || length<16384 || (pa&~UINT64_C(0x0000000ffffff000))){error=EINVAL;goto done;}
            physical[at++]=pa;
        }
    }
    if(!sfavd_pages_unique(physical,pageCount)||gDart.aliasesMapped(physical,pageCount)) {
        error=EINVAL;goto done;
    }
    for(unsigned i=0;i<req->count;i++) {
        /* One-time initialization matches the old zeroed capture allocation.
         * No frame contents are copied by this import operation. */
        bzero(cpu[i],needed);
        error=gDart.mapMemory(next[i]->md,needed,&next[i]->iova);
        if(error)goto done;
        iova[i]=next[i]->iova;cookies[i]=next[i];
    }
    error=-sf_avd_session_import(c->session,req->count,cpu,iova,cookies,old);
    if(error)goto done;
    c->imported=true;
    for(unsigned i=0;i<req->count;i++) {next[i]=NULL;sf_avd_os_dma_free(old[i]);}
 done:
    for(unsigned i=0;i<req->count;i++) if(next[i])sf_avd_os_dma_free(next[i]);
    IOFree(physical,pageCount*sizeof(uint64_t));
    return error;
}

static int
do_decode(struct sfavd_decode *req)
{
	Context *c = context_of(req->ctx);
	uint64_t t0;
	void *coded;
	int err, ret;

	if (c == NULL || (c->owner != proc_selfpid() || c->ownerTask != current_task()) || req->nctrls > SFAVD_MAX_CTRLS || req->size == 0 || req->data == 0)
		return EINVAL;
	for (uint32_t i = 0; i < req->nctrls; i++) {
		uint8_t *ctrl = NULL;

		err = copy_ctrl(&req->ctrls[i], &ctrl);
		if (err != 0)
			return err;
		ret = sf_avd_session_set_ctrl(c->session, req->ctrls[i].id, ctrl, req->ctrls[i].size);
		IOFree(ctrl, req->ctrls[i].size);
		if (ret != 0)
			return -ret;
	}
	coded = sf_avd_session_coded(c->session, req->size);
	if (coded == NULL)
		return ENOMEM;
	err = copyin((user_addr_t)req->data, coded, req->size);
	if (err != 0)
		return err;
	c->submitted = true;
	t0 = sf_avd_os_now_us();
	ret = sf_avd_session_decode(c->session, req->size, req->dst, req->timestamp, req->flags, &req->status);
	req->decode_us = sf_avd_os_now_us() - t0;
	if (gDart.check("decode") != 0 && ret == 0)
		ret = -EIO;
	return ret == 0 ? 0 : (ret < 0 ? -ret : EIO);
}

/* ---- the device node ------------------------------------------------------------------- */

static int
sf_avd_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	int err;

	IOLockLock(gLock);
	err = bring_up();
	if (err == 0)
		gOpens++;
	IOLockUnlock(gLock);
	return err;
}

/* the last close of the node */
static int
sf_avd_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	IOLockLock(gLock);
	for (unsigned i = 0; i < MAX_CONTEXTS; i++)
		context_destroy(&gContexts[i]);
	IOLockUnlock(gLock);
	return 0;
}

static int
sf_avd_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag, __unused struct proc *p)
{
	int err = ENOTTY;

	IOLockLock(gLock);
	if (!gUp || (gDart.poisoned && cmd!=SFAVD_IOC_INFO && cmd!=SFAVD_IOC_DESTROY)) {
		IOLockUnlock(gLock);
		return ENXIO;
	}
	switch (cmd) {
	case SFAVD_IOC_INFO: {
		struct sfavd_info *info = (struct sfavd_info *)data;
		bzero(info, sizeof(*info));
		info->abi = SFAVD_ABI_VERSION;
		info->caps = sf_avd_device_caps(gDev) | SFAVD_CAP_EXTERNAL_BUFFERS;
		info->booted = contexts_in_use() != 0;
		info->contexts = contexts_in_use();
		strlcpy(info->firmware, gFirmwareText, sizeof(info->firmware));
		err = 0;
		break;
	}
	case SFAVD_IOC_CREATE:
		err = do_create((struct sfavd_create *)data);
		break;
	case SFAVD_IOC_IMPORT:
        err=do_import((struct sfavd_import*)data);
        break;
	case SFAVD_IOC_MAP:
		err = do_map((struct sfavd_map *)data);
		break;
	case SFAVD_IOC_DECODE:
		err = do_decode((struct sfavd_decode *)data);
		break;
	case SFAVD_IOC_DESTROY: {
		Context *c = context_of(((struct sfavd_destroy *)data)->ctx);
		if (c == NULL || (c->owner != proc_selfpid() || c->ownerTask != current_task())) {
			err = EINVAL;
		} else {
			context_destroy(c);
			err = 0;
		}
		break;
	}
	}
	IOLockUnlock(gLock);
	return err;
}

static struct cdevsw sf_avd_cdevsw = {
	.d_open = sf_avd_open,
	.d_close = sf_avd_close,
	.d_read = eno_rdwrt,
	.d_write = eno_rdwrt,
	.d_ioctl = sf_avd_ioctl,
	.d_stop = eno_stop,
	.d_reset = eno_reset,
	.d_ttys = NULL,
	.d_select = eno_select,
	.d_mmap = eno_mmap,
	.d_strategy = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type = 0,
};

/* ---- sysctl ---------------------------------------------------------------------------- */

static int
sf_sysctl_text(__unused struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	char text[512];

	if (arg2 == 0) {
		strlcpy(text, gState, sizeof(text));
	} else {
		struct sf_avd_stats s;

		bzero(&s, sizeof(s));
		if (gDev != NULL)
			sf_avd_device_stats(gDev, &s);
		snprintf(text, sizeof(text),
		    "jobs %llu done %llu errors %llu timeouts %llu resets %u stale %llu irqs %u vp_done %llu unhandled %llu "
		    "last_status 0x%x last_us %llu max_us %llu mean_us %llu contexts %u opens %u dart_faults %u dart_kib %llu "
		    "dart_peak_kib %llu dart_poisoned %u quarantined_desc %u quarantine_kib %llu",
		    s.jobs, s.done, s.errors, s.timeouts, gResets, s.stale, gIrqs, s.vp_done, s.unhandled, s.last_status,
		    s.last_decode_us, s.max_decode_us, s.done ? s.total_decode_us / s.done : 0, contexts_in_use(), gOpens,
		    gDart.faults, gDart.mappedBytes / 1024, gDart.peakBytes / 1024, (unsigned)gDart.poisoned, gDart.quarantinedDescriptors, gDart.quarantinedBytes/1024);
	}
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_NODE(_kern, OID_AUTO, sf_avd, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "Apple Video Decoder");
SYSCTL_PROC(_kern_sf_avd, OID_AUTO, state, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0, sf_sysctl_text, "A",
    "what the driver is doing");
SYSCTL_PROC(_kern_sf_avd, OID_AUTO, stats, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 1, sf_sysctl_text, "A",
    "decode counters and times");

static struct sysctl_oid *const gOids[] = { &sysctl__kern_sf_avd, &sysctl__kern_sf_avd_state, &sysctl__kern_sf_avd_stats };

/* ---- start ----------------------------------------------------------------------------- */

bool
SFAppleAVD::start(IOService *provider)
{
	IORegistryEntry *chosen, *node;
	OSData *chip;
	uint32_t chipID = 0;

	if (!IOService::start(provider))
		return false;
	PE_parse_boot_argn("sfavd", &gLevel, sizeof(gLevel));
	if (gLevel == 0)
		return false;       /* off unless asked for; nothing is logged */
	chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	chip = chosen ? OSDynamicCast(OSData, chosen->getProperty("chip-id")) : NULL;
	if (chip != NULL && chip->getLength() >= sizeof(uint32_t))
		chipID = *(const uint32_t *)chip->getBytesNoCopy();
	OSSafeReleaseNULL(chosen);
	node = IORegistryEntry::fromPath(AVD_NODE, gIODTPlane);
	if (chipID != T8103_CHIP_ID || node == NULL) {
		LOG("chip 0x%x %s %s: not a T8103 machine with a video decoder; nothing is done", chipID,
		    node ? "with" : "without", AVD_NODE);
		OSSafeReleaseNULL(node);
		return false;
	}
	OSSafeReleaseNULL(node);

	gLock = IOLockAlloc();
	gWait = IOLockAlloc();
	if (gLock == NULL || gWait == NULL)
		return false;
	gService = this;
	gMajor = cdevsw_add(-1, &sf_avd_cdevsw);
	if (gMajor < 0) {
		LOG("no character device number");
		return false;
	}
	gDevNode = devfs_make_node(makedev(gMajor, 0), DEVFS_CHAR, 0, 0, 0600, "sfavd");
	for (unsigned i = 0; i < sizeof(gOids) / sizeof(gOids[0]); i++)
		sysctl_register_oid(gOids[i]);
	LOG("/dev/sfavd (sfavd=%u); the decoder is powered at its first use", gLevel);
	registerService();
	return true;
}
