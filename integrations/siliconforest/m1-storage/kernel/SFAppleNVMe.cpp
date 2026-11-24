// SPDX-License-Identifier: GPL-2.0
/*
 * SFAppleNVMe: the M1's internal NVMe storage (ANS2), READ-ONLY.
 *
 * OFF unless the boot arguments say otherwise:
 *   sfnvme=0 or absent  nothing is touched: no power domain, no register, no memory
 *   sfnvme=1            observe: power states and, where the domains are already on, the
 *                       registers as the boot loader left them; nothing is written
 *   sfnvme=2            the controller is started; namespace 1 can be read through
 *                       /dev/sfnvme0 (a character device of this driver, nothing else sees it)
 *   sfnvme=3            as 2, and namespace 1 is published to IOStorageFamily as a
 *                       write-protected, non-removable block device (IOMedia not writable)
 *   sfnvme_ttl=<s>      seconds after which the controller is shut down cleanly whatever the
 *                       system is doing (default 90, 10..570): the bench resets the machine
 *                       at the end of a boot, and a controller must not be running then.
 *                       sfnvme_ttl=0: no time limit (an installed system: the controller runs
 *                       until the system's own halt stops it)
 *   sfnvme_delay=<ms>   wait before the bring-up (default 3000)
 *   sfnvme_verbose=1    every mailbox message
 *   sfnvme_part=<GUID> sfnvme_part_lba=<first block> sfnvme_part_blocks=<length>
 *                       (sfnvme=3) the SiliconForest partition: the one partition the guard
 *                       below does NOT hold, so that a filesystem can be mounted from it
 *                       (read-only: the media stays write-protected). All three must be
 *                       given and must be found exactly so in both GPT copies
 *                       (sf_nvme_part.c); otherwise every partition is guarded as before
 *   sfnvme_readfence=1  (with the three above; for a root on our partition) the block device
 *                       serves reads only of the GPT's own blocks and, once it is identified,
 *                       of our partition: no filesystem or boot-volume driver can read a
 *                       block of the owner's partitions whatever it matches on. Refusals are
 *                       counted in kern.sf_nvme.fence. /dev/sfnvme0 is not affected.
 *                       Guarded partitions also lose their content hint, so that such
 *                       drivers do not consider them in the first place.
 *
 * What makes it read-only is in sf_nvme_gate.c and SFAns::ring(): there is no code that
 * builds a write, flush, trim, format, sanitize, firmware, namespace or set-features
 * command, and the only doorbell write refuses everything but the allowlist.
 * kern.sf_nvme.gate_rejected counts refusals and stays 0 unless something inside this
 * driver tried. Writes arriving from the block layer or the character device are refused
 * before any command is built and counted in kern.sf_nvme.write_refused.
 *
 * Shutdown is the Linux driver's (apple_nvme_shutdown): by kern.sf_nvme.stop=1, at the
 * end of the time to live, or when the system shuts down.
 *
 * Ported from the Omacom Linux tree; see sf_ans.cpp and ../NOTICE.
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include "sf_ans.h"
#include "sf_nvme_part.h"

#define LOG(fmt, ...) IOLog("SFAppleNVMe: " fmt "\n", ##__VA_ARGS__)
#define T8103_CHIP_ID 0x8103
#define IO_TIMEOUT_US (30ULL * 1000000)         /* NVME_IO_TIMEOUT */
#define BLOCK 4096u

enum { kStateOff, kStateObserved, kStateStarting, kStateReady, kStateStopping, kStateStoppedClean, kStateStoppedUnclean, kStateFailed };
static const char *const kStateNames[] = { "off", "observed", "starting", "ready", "stopping", "stopped-clean",
    "stopped-unclean", "failed" };

struct SFReq {
	SFReq   *next;
	uint64_t lba;
	uint32_t blocks;
	int      slot;          /* valid when done with result 0: the caller copies out and releases it */
	IOReturn result;
	bool     done;
	uint64_t micros;
};

class SFAppleNVMeDisk;

static SFAns     gAns;
static IOLock   *gLock;
static IOService *gService;
static SFAppleNVMeDisk *gDisk;
static int       gState = kStateOff;
static uint32_t  gLevel, gTtl = 90, gDelayMs = 3000;
static char      gWhy[160] = "";
static SFReq    *gQueueHead, *gQueueTail;
static bool      gHeld[SF_ANS_SLOTS];   /* a finished read whose data the caller has not copied yet */
static bool      gStopRequested, gSmartRequested;
static const char *gStopReason = "";
static int       gEvent;                /* the engine sleeps on its address */
static uint32_t  gWriteRefused, gSyncIgnored, gStarts;
static uint64_t  gMaxMicros, gSumMicros, gCompleted, gBytes, gReadyAtMs, gStartMs, gStopMs;
static int       gMajor = -1;
static void     *gDevNode;
static uint64_t  gNodeTriedMs;
/* the SiliconForest partition, as asked for in the boot arguments (see sf_nvme_part.h) */
enum { kPartNotAsked, kPartUnchecked, kPartVerified, kPartRefused };
static struct sf_part_want gWant;
static int       gPartState = kPartNotAsked;
static char      gPartWhy[96] = "not asked for";
static IOLock   *gPartLock;
static uint32_t  gExempted;
static uint32_t  gReadFence;
static uint64_t  gFenceRefused, gFenceRefusedBlocks, gOursRequests, gOursBlocks, gGptRequests, gOtherRequests, gOtherBlocks;
#define GPT_HEAD_BLOCKS 6u      /* protective MBR, header, 4 blocks of table */
#define GPT_TAIL_BLOCKS 5u      /* 4 blocks of table, header */

static uint64_t now_ms(void) { return sf_ans_now_us() / 1000; }

/* ---- reads, for the character device and the block device -------------------------------- */

/* Called with gLock held. */
static void
finish(SFReq *r, IOReturn result)
{
	r->result = result;
	r->done = true;
	IOLockWakeup(gLock, r, false);
}

/*
 * Reads `blocks` blocks from `lba`, up to SF_ANS_SLOTS commands at a time, and hands each
 * piece to `sink` (offset in bytes from the start, the data, its length) in order.
 */
typedef int (*sf_sink_t)(void *ctx, uint64_t offset, const uint8_t *data, uint32_t len);

static IOReturn
sf_read(uint64_t lba, uint64_t blocks, sf_sink_t sink, void *ctx)
{
	uint64_t done = 0;
	IOReturn result = kIOReturnSuccess;

	while (done < blocks && result == kIOReturnSuccess) {
		SFReq reqs[SF_ANS_SLOTS];
		unsigned n = 0;

		IOLockLock(gLock);
		if (gState != kStateReady || gStopRequested) {
			IOLockUnlock(gLock);
			return kIOReturnOffline;
		}
		while (n < SF_ANS_SLOTS && done + (uint64_t)n * SF_NVME_MAX_READ_BLOCKS < blocks) {
			uint64_t at = done + (uint64_t)n * SF_NVME_MAX_READ_BLOCKS, left = blocks - at;
			SFReq *r = &reqs[n++];

			r->next = NULL;
			r->lba = lba + at;
			r->blocks = left > SF_NVME_MAX_READ_BLOCKS ? SF_NVME_MAX_READ_BLOCKS : (uint32_t)left;
			r->slot = -1;
			r->result = kIOReturnSuccess;
			r->done = false;
			r->micros = 0;
			if (gQueueTail != NULL)
				gQueueTail->next = r;
			else
				gQueueHead = r;
			gQueueTail = r;
		}
		IOLockWakeup(gLock, &gEvent, true);
		for (unsigned i = 0; i < n; i++) {
			SFReq *r = &reqs[i];

			while (!r->done)
				IOLockSleep(gLock, r, THREAD_UNINT);
			if (r->result != kIOReturnSuccess) {
				if (result == kIOReturnSuccess)
					result = r->result;
				continue;
			}
			/* the slot is this request's until it is released: copy without the lock */
			IOLockUnlock(gLock);
			if (result == kIOReturnSuccess &&
			    sink(ctx, (done + (uint64_t)i * SF_NVME_MAX_READ_BLOCKS) * BLOCK, gAns.slot[r->slot].data.va, r->blocks * BLOCK) != 0)
				result = kIOReturnVMError;
			IOLockLock(gLock);
			gHeld[r->slot] = false;
			IOLockWakeup(gLock, &gEvent, true);
		}
		IOLockUnlock(gLock);
		done += (uint64_t)n * SF_NVME_MAX_READ_BLOCKS;
	}
	return result;
}

/* ---- /dev/sfnvme0 ------------------------------------------------------------------------- */

static int
uio_sink(void *ctx, __unused uint64_t offset, const uint8_t *data, uint32_t len)
{
	return uiomove((const char *)data, (int)len, (struct uio *)ctx);
}

static int
sf_nvme_open(__unused dev_t dev, int flags, __unused int devtype, __unused struct proc *p)
{
	if (flags & FWRITE) {
		OSIncrementAtomic((volatile SInt32 *)&gWriteRefused);
		return EROFS;
	}
	return gState == kStateReady ? 0 : ENXIO;
}

static int
sf_nvme_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}

static int
sf_nvme_read(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	uint64_t offset = (uint64_t)uio_offset(uio), resid = (uint64_t)uio_resid(uio), blocks = gAns.nsBlocks;

	if ((offset % BLOCK) != 0 || (resid % BLOCK) != 0)
		return EINVAL;
	if (offset / BLOCK >= blocks)
		return 0;
	if (resid / BLOCK > blocks - offset / BLOCK)
		resid = (blocks - offset / BLOCK) * BLOCK;
	if (resid > (64u << 20))
		resid = 64u << 20;      /* a short read; the caller comes back */
	switch (sf_read(offset / BLOCK, resid / BLOCK, uio_sink, uio)) {
	case kIOReturnSuccess: return 0;
	case kIOReturnOffline: return ENXIO;
	case kIOReturnVMError: return EFAULT;
	default:               return EIO;
	}
}

static int
sf_nvme_write(__unused dev_t dev, __unused struct uio *uio, __unused int ioflag)
{
	OSIncrementAtomic((volatile SInt32 *)&gWriteRefused);
	return EROFS;
}

static struct cdevsw sf_nvme_cdevsw = {
	sf_nvme_open, sf_nvme_close, sf_nvme_read, sf_nvme_write,
	eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
	eno_strat, eno_getc, eno_putc, 0
};

static void
publish_node(void)
{
	if (gDevNode != NULL)
		return;
	if (gMajor < 0)
		gMajor = cdevsw_add(-1, &sf_nvme_cdevsw);
	if (gMajor >= 0)
		gDevNode = devfs_make_node(makedev(gMajor, 0), DEVFS_CHAR, 0, 0, 0444, "sfnvme0");
	if (gDevNode != NULL)
		LOG("/dev/sfnvme0 published (major %d), read-only", gMajor);
}

/* ---- the block device (sfnvme=3) ---------------------------------------------------------- */

class SFAppleNVMeDisk : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(SFAppleNVMeDisk);
	char fProduct[48], fRevision[12];

public:
	bool setup(void);
	IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
	IOReturn doSynchronize(UInt64 block, UInt64 nblks, IOStorageSynchronizeOptions options = 0) APPLE_KEXT_OVERRIDE;
	IOReturn doUnmap(IOBlockStorageDeviceExtent *extents, UInt32 extentsCount, IOStorageUnmapOptions options = 0) APPLE_KEXT_OVERRIDE;
	IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE { return kIOReturnUnsupported; }
	IOReturn doFormatMedia(UInt64) APPLE_KEXT_OVERRIDE { OSIncrementAtomic((volatile SInt32 *)&gWriteRefused); return kIOReturnNotWritable; }
	UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
	IOReturn setWriteCacheState(bool) APPLE_KEXT_OVERRIDE { return kIOReturnUnsupported; }
	IOReturn getWriteCacheState(bool *enabled) APPLE_KEXT_OVERRIDE { if (enabled) *enabled = false; return kIOReturnSuccess; }
	char *getVendorString(void) APPLE_KEXT_OVERRIDE { return (char *)"APPLE"; }
	char *getProductString(void) APPLE_KEXT_OVERRIDE { return fProduct; }
	char *getRevisionString(void) APPLE_KEXT_OVERRIDE { return fRevision; }
	char *getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE { return (char *)"read-only"; }
	IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE { *blockSize = BLOCK; return kIOReturnSuccess; }
	IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE { *isEjectable = false; return kIOReturnSuccess; }
	IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
	IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) APPLE_KEXT_OVERRIDE;
	IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE { *isRemovable = false; return kIOReturnSuccess; }
	/* ALWAYS write protected: IOBlockStorageDriver then publishes media that is not writable. */
	IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE { *isWriteProtected = true; return kIOReturnSuccess; }
};

OSDefineMetaClassAndStructors(SFAppleNVMeDisk, IOBlockStorageDevice);

static void
trimmed(char *out, size_t outLen, const uint8_t *in, size_t inLen)
{
	size_t n = inLen < outLen - 1 ? inLen : outLen - 1;

	memcpy(out, in, n);
	out[n] = 0;
	for (size_t i = 0; i < n; i++)
		if (out[i] < 0x20 || out[i] > 0x7e)
			out[i] = ' ';
	while (n > 0 && out[n - 1] == ' ')
		out[--n] = 0;
}

bool
SFAppleNVMeDisk::setup(void)
{
	if (!init(NULL))
		return false;
	trimmed(fProduct, sizeof(fProduct), gAns.idCtrl + 24, 40);
	trimmed(fRevision, sizeof(fRevision), gAns.idCtrl + 64, 8);

	OSDictionary *protocol = OSDictionary::withCapacity(2);
	OSString *location = OSString::withCString(kIOPropertyInternalKey);
	OSString *interconnect = OSString::withCString("NVMe");
	bool ok = protocol != NULL && location != NULL && interconnect != NULL &&
	    protocol->setObject(kIOPropertyPhysicalInterconnectLocationKey, location) &&
	    protocol->setObject(kIOPropertyPhysicalInterconnectTypeKey, interconnect) &&
	    setProperty(kIOPropertyProtocolCharacteristicsKey, protocol);
	OSSafeReleaseNULL(location);
	OSSafeReleaseNULL(interconnect);
	OSSafeReleaseNULL(protocol);
	setProperty(kIOBlockStorageDeviceTypeKey, kIOBlockStorageDeviceTypeGeneric);
	/* a request of the block layer is at most what one round of read slots carries */
	setProperty(kIOMaximumBlockCountReadKey, (unsigned long long)(SF_ANS_SLOTS * SF_NVME_MAX_READ_BLOCKS), 64);
	setProperty(kIOMaximumBlockCountWriteKey, 0ULL, 64);
	setProperty("SFReadOnly", kOSBooleanTrue);
	setName("sfnvme0");
	setLocation("0");
	return ok;
}

static int
md_sink(void *ctx, uint64_t offset, const uint8_t *data, uint32_t len)
{
	return ((IOMemoryDescriptor *)ctx)->writeBytes((IOByteCount)offset, data, len) == len ? 0 : EFAULT;
}

IOReturn
SFAppleNVMeDisk::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, __unused IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	/* an error return means "not started": IOBlockStorageDriver then completes the request itself */
	if (buffer == NULL)
		return kIOReturnBadArgument;
	if (buffer->getDirection() != kIODirectionIn) {
		OSIncrementAtomic((volatile SInt32 *)&gWriteRefused);
		return kIOReturnNotWritable;
	}
	if (nblks == 0 || block >= gAns.nsBlocks || nblks > gAns.nsBlocks - block || buffer->getLength() < nblks * BLOCK)
		return kIOReturnBadArgument;
	/* where the request lies: the GPT's own blocks, our partition (only once identified), or anything else */
	if (block + nblks <= GPT_HEAD_BLOCKS || block >= gAns.nsBlocks - GPT_TAIL_BLOCKS) {
		OSIncrementAtomic64((volatile SInt64 *)&gGptRequests);
	} else if (gPartState == kPartVerified && block >= gWant.first && nblks <= gWant.blocks && block - gWant.first <= gWant.blocks - nblks) {
		OSIncrementAtomic64((volatile SInt64 *)&gOursRequests);
		OSAddAtomic64((SInt64)nblks, (volatile SInt64 *)&gOursBlocks);
	} else if (gReadFence) {
		OSIncrementAtomic64((volatile SInt64 *)&gFenceRefused);
		OSAddAtomic64((SInt64)nblks, (volatile SInt64 *)&gFenceRefusedBlocks);
		return kIOReturnNotPermitted;
	} else {
		OSIncrementAtomic64((volatile SInt64 *)&gOtherRequests);
		OSAddAtomic64((SInt64)nblks, (volatile SInt64 *)&gOtherBlocks);
	}
	IOReturn ret = buffer->prepare(kIODirectionIn);
	if (ret != kIOReturnSuccess)
		return ret;
	ret = sf_read(block, nblks, md_sink, buffer);
	buffer->complete(kIODirectionIn);
	IOStorage::complete(completion, ret, ret == kIOReturnSuccess ? nblks * BLOCK : 0);
	return kIOReturnSuccess;
}

/* Nothing is ever written, so there is nothing to synchronize: no Flush is sent. */
IOReturn
SFAppleNVMeDisk::doSynchronize(__unused UInt64 block, __unused UInt64 nblks, __unused IOStorageSynchronizeOptions options)
{
	OSIncrementAtomic((volatile SInt32 *)&gSyncIgnored);
	return kIOReturnSuccess;
}

IOReturn
SFAppleNVMeDisk::doUnmap(__unused IOBlockStorageDeviceExtent *extents, __unused UInt32 extentsCount, __unused IOStorageUnmapOptions options)
{
	OSIncrementAtomic((volatile SInt32 *)&gWriteRefused);
	return kIOReturnNotWritable;
}

UInt32
SFAppleNVMeDisk::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
	if (capacities != NULL && capacitiesMaxCount != 0)
		capacities[0] = gAns.nsBlocks * BLOCK;
	return 1;
}

IOReturn
SFAppleNVMeDisk::reportMaxValidBlock(UInt64 *maxBlock)
{
	*maxBlock = gAns.nsBlocks ? gAns.nsBlocks - 1 : 0;
	return kIOReturnSuccess;
}

IOReturn
SFAppleNVMeDisk::reportMediaState(bool *mediaPresent, bool *changedState)
{
	*mediaPresent = gState == kStateReady;
	if (changedState != NULL)
		*changedState = false;
	return kIOReturnSuccess;
}

/*
 * The guard of the partitions of this disk (sfnvme=3). Partition schemes, the APFS
 * container scheme and every other content driver for IOMedia compete in the match
 * category "IOStorage", one winner per media. This driver enters that category with the
 * highest score for every media that is a partition (not the whole disk) below
 * SFAppleNVMeDisk, wins, and then does nothing: it never opens the media and reads no
 * block. So the GPT is parsed by the stack (the whole media is left to the partition
 * schemes), the partitions get their /dev nodes (IOMediaBSDClient is in another
 * category), and no filesystem or container driver attaches to the owner's macOS and Linux
 * partitions. Media that is not below this disk is declined, so other disks are unaffected.
 */
class SFAppleNVMeMediaGuard : public IOService
{
	OSDeclareDefaultStructors(SFAppleNVMeMediaGuard);

public:
	IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SFAppleNVMeMediaGuard, IOService);
static uint32_t gGuarded;

static int
part_sink(void *ctx, uint64_t offset, const uint8_t *data, uint32_t len)
{
	memcpy((uint8_t *)ctx + offset, data, len);
	return 0;
}

static int
part_read(__unused void *ctx, uint64_t lba, uint32_t blocks, uint8_t *out)
{
	return sf_read(lba, blocks, part_sink, out) == kIOReturnSuccess ? 0 : EIO;
}

/*
 * Is this media the SiliconForest partition? Its position and length and the unique GUID
 * the partition scheme read must be the ones asked for, and (checked once, by this driver's
 * own reads of both GPT copies, not through the storage stack) both copies must say the
 * same. Any doubt answers no, and the partition is guarded like the others.
 */
static bool
is_our_partition(IOMedia *media)
{
	uint8_t guid[16];
	OSString *uuid;
	bool ok;

	if (gPartState == kPartNotAsked || gPartLock == NULL)
		return false;
	if (media->getBase() != gWant.first * BLOCK || media->getSize() != gWant.blocks * BLOCK ||
	    media->getPreferredBlockSize() != BLOCK)
		return false;
	uuid = OSDynamicCast(OSString, media->getProperty(kIOMediaUUIDKey));
	if (uuid == NULL || sf_part_parse_guid(uuid->getCStringNoCopy(), guid) != 0 || memcmp(guid, gWant.guid, 16) != 0)
		return false;
	IOLockLock(gPartLock);
	if (gPartState == kPartUnchecked) {
		uint8_t *work = (uint8_t *)IOMalloc(SF_PART_WORK);
		int r = work == NULL ? -1 : sf_part_verify(&gWant, gAns.nsBlocks, part_read, NULL, work, gPartWhy, sizeof(gPartWhy));

		if (work != NULL)
			IOFree(work, SF_PART_WORK);
		gPartState = r == 0 ? kPartVerified : kPartRefused;
		LOG("the SiliconForest partition (blocks %llu..%llu): %s (%d): %s", gWant.first, gWant.first + gWant.blocks - 1,
		    r == 0 ? "identified, not guarded" : "NOT identified, guarded like the others", r, gPartWhy);
	}
	ok = gPartState == kPartVerified;
	IOLockUnlock(gPartLock);
	return ok;
}

IOService *
SFAppleNVMeMediaGuard::probe(IOService *provider, SInt32 *score)
{
	IOMedia *media = OSDynamicCast(IOMedia, provider);
	bool ours = false;

	if (media == NULL || gDisk == NULL || media->isWhole())
		return NULL;
	for (IORegistryEntry *e = provider; e != NULL && !ours; e = e->getParentEntry(gIOServicePlane))
		ours = e == (IORegistryEntry *)gDisk;
	if (!ours)
		return NULL;
	if (is_our_partition(media)) {
		OSIncrementAtomic((volatile SInt32 *)&gExempted);
		return NULL;
	}
	*score = 1000000;
	return this;
}

bool
SFAppleNVMeMediaGuard::start(IOService *provider)
{
	if (!IOService::start(provider))
		return false;
	setProperty("SFGuarded", kOSBooleanTrue);
	/* boot-volume and filesystem drivers find their media by this hint (the partition's type) */
	provider->setProperty(kIOMediaContentHintKey, "SFAppleNVMe-guarded");
	OSIncrementAtomic((volatile SInt32 *)&gGuarded);
	LOG("partition %s: guarded, no content driver attaches", provider->getName());
	return true;
}

static void
publish_disk(void)
{
	SFAppleNVMeDisk *disk = new SFAppleNVMeDisk;

	if (disk == NULL || !disk->setup() || !disk->attach(gService)) {
		LOG("the block device could not be made");
		OSSafeReleaseNULL(disk);
		return;
	}
	gDisk = disk;
	disk->registerService();
	LOG("namespace 1 published to the storage family: %llu blocks of %u bytes, write-protected", gAns.nsBlocks, BLOCK);
}

/* ---- the engine thread: the only code that touches the controller -------------------------- */

static void
set_state(int state)
{
	IOLockLock(gLock);
	gState = state;
	IOLockUnlock(gLock);
}

static int
pick_slot(void)
{
	if (gAns.dead || gAns.nsBlocks == 0)
		return -1;
	for (unsigned s = 0; s < SF_ANS_SLOTS; s++)
		if (!gAns.slot[s].busy && !gHeld[s])
			return (int)s;
	return -1;
}

static void
fail_queue(IOReturn why)
{
	while (gQueueHead != NULL) {
		SFReq *r = gQueueHead;
		gQueueHead = r->next;
		finish(r, why);
	}
	gQueueTail = NULL;
}

static void
engine(void)
{
	uint64_t ttlAt = now_ms() + (uint64_t)gTtl * 1000;
	unsigned idleSpins = 0;

	IOLockLock(gLock);
	for (;;) {
		SFAnsCompletion comps[SF_ANS_SLOTS];
		unsigned n, held = 0;
		int s;

		if (gTtl != 0 && !gStopRequested && now_ms() >= ttlAt) {
			gStopRequested = true;
			gStopReason = "time to live";
		}
		if (gAns.dead && !gStopRequested) {
			gStopRequested = true;
			gStopReason = "controller failure";
		}
		if (gStopRequested) {
			if (gState == kStateReady)
				gState = kStateStopping;
			fail_queue(kIOReturnOffline);
		}
		while (gQueueHead != NULL && (s = pick_slot()) >= 0) {
			SFReq *r = gQueueHead;
			gQueueHead = r->next;
			if (gQueueHead == NULL)
				gQueueTail = NULL;
			int err = gAns.submitRead((unsigned)s, r->lba, r->blocks, r);
			if (err != 0)
				finish(r, err == EPERM ? kIOReturnNotPermitted : kIOReturnBadArgument);
			else
				r->slot = s;
		}
		n = gAns.reap(comps, SF_ANS_SLOTS);
		for (unsigned i = 0; i < n; i++) {
			SFReq *r = (SFReq *)comps[i].cookie;

			if (r == NULL)
				continue;       /* a read that was given up on long ago */
			r->micros = comps[i].micros;
			gCompleted++;
			gSumMicros += comps[i].micros;
			if (comps[i].micros > gMaxMicros)
				gMaxMicros = comps[i].micros;
			if (comps[i].status != 0) {
				LOG("read of %u blocks at %llu failed: NVMe status 0x%x", r->blocks, r->lba, comps[i].status);
				finish(r, kIOReturnIOError);
			} else {
				gBytes += (uint64_t)r->blocks * BLOCK;
				gHeld[comps[i].slot] = true;
				finish(r, kIOReturnSuccess);
			}
		}
		if (gAns.inflight != 0 && gAns.overdue(IO_TIMEOUT_US) != 0) {
			/* the slots stay busy for good: the controller may still write into them */
			for (unsigned i = 0; i < SF_ANS_SLOTS; i++)
				if (gAns.slot[i].busy && gAns.slot[i].cookie != NULL) {
					finish((SFReq *)gAns.slot[i].cookie, kIOReturnTimeout);
					gAns.slot[i].cookie = NULL;
				}
			continue;
		}
		if (gSmartRequested && gAns.inflight == 0 && !gAns.dead && gState == kStateReady) {
			gAns.readSmart();
			gSmartRequested = false;
			IOLockWakeup(gLock, &gSmartRequested, false);
		}
		for (unsigned i = 0; i < SF_ANS_SLOTS; i++)
			held += gHeld[i] ? 1 : 0;
		if (gStopRequested && held == 0 && (gAns.inflight == 0 || gAns.dead))
			break;
		if (n != 0)
			idleSpins = 0;
		if (gAns.inflight != 0) {
			/* a read is out: look again very soon; the lock is free for new requests meanwhile */
			IOLockUnlock(gLock);
			if (++idleSpins < 4000)
				IODelay(5);
			else
				IOSleep(1);
			IOLockLock(gLock);
		} else if (gQueueHead == NULL || pick_slot() < 0) {
			AbsoluteTime deadline;

			if (gDevNode == NULL && now_ms() - gNodeTriedMs >= 1000) {
				gNodeTriedMs = now_ms();
				IOLockUnlock(gLock);
				publish_node();
				IOLockLock(gLock);
				continue;
			}
			clock_interval_to_deadline(50, kMillisecondScale, &deadline);
			IOLockSleepDeadline(gLock, &gEvent, deadline, THREAD_UNINT);
			idleSpins = 0;
		}
	}
	gSmartRequested = false;
	IOLockWakeup(gLock, &gSmartRequested, false);
	IOLockUnlock(gLock);
}

static void
sf_nvme_thread(__unused void *arg, __unused wait_result_t wr)
{
	char why[160];
	int err;

	bool again = gStarts != 0;

	if (!again)
		IOSleep(gDelayMs);
	gStarts++;
	if (gLevel == 1) {
		gAns.observe();
		set_state(kStateObserved);
		return;
	}
	set_state(kStateStarting);
	gStartMs = now_ms();
	LOG("start %u of this boot", gStarts);
	LOG("starting the controller (level %u, time to live %u s); READ-ONLY build: allowlist admin 00 01 02 04 05 06, I/O 02", gLevel, gTtl);
	err = gAns.start(why, sizeof(why));
	if (err != 0) {
		bool clean = gAns.stop(gWhy, sizeof(gWhy));
		LOG("bring-up failed (%d: %s); controller stopped %s", err, why, clean ? "cleanly" : "NOT cleanly");
		snprintf(gWhy, sizeof(gWhy), "%s", why);
		set_state(kStateFailed);
		return;
	}
	gReadyAtMs = now_ms();
	set_state(kStateReady);
	/*
	 * /dev/sfnvme0 needs devfs, which is not there before the root is mounted (at the tip
	 * that can be a minute after this point, and with the root on this disk it is after
	 * the block device has served it): tried once here, then once a second by the engine.
	 */
	publish_node();
	if (gLevel >= 3 && gDisk == NULL)
		publish_disk();

	engine();

	LOG("stopping the controller (%s)", gStopReason);
	if (!gAns.dead)
		gAns.readSmart();
	bool clean = gAns.stop(why, sizeof(why));
	gStopMs = now_ms();
	snprintf(gWhy, sizeof(gWhy), "%s: %s", gStopReason, why);
	LOG("controller stopped %s (%s); gate_rejected=%u write_refused=%u reads=%llu blocks=%llu errors=%llu timeouts=%llu",
	    clean ? "CLEANLY" : "NOT CLEANLY", gWhy, gAns.gateRejected, gWriteRefused, gAns.readCommands, gAns.readBlocks,
	    gAns.cmdErrors, gAns.cmdTimeouts);
	IOLockLock(gLock);
	gState = clean ? kStateStoppedClean : kStateStoppedUnclean;
	IOLockWakeup(gLock, &gState, false);
	IOLockUnlock(gLock);
}

/* Asks the engine to stop and waits for it, at most `ms`. True when the controller is stopped. */
static bool
request_stop(const char *reason, uint32_t ms)
{
	bool stopped;

	IOLockLock(gLock);
	if (gState == kStateReady || gState == kStateStopping) {
		AbsoluteTime deadline;

		if (!gStopRequested) {
			gStopRequested = true;
			gStopReason = reason;
		}
		IOLockWakeup(gLock, &gEvent, true);
		clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
		while (gState == kStateReady || gState == kStateStopping)
			if (IOLockSleepDeadline(gLock, &gState, deadline, THREAD_UNINT) == THREAD_TIMED_OUT)
				break;
	}
	stopped = gState != kStateReady && gState != kStateStopping && gState != kStateStarting;
	IOLockUnlock(gLock);
	return stopped;
}

static void sf_nvme_thread(void *arg, wait_result_t wr);

/* A further start after a clean stop: the sequence Linux runs when its driver is bound again. */
static int
request_start(void)
{
	thread_t thread;

	IOLockLock(gLock);
	if (gState != kStateStoppedClean || gLevel < 2) {
		IOLockUnlock(gLock);
		return EBUSY;
	}
	gState = kStateStarting;
	gStopRequested = false;
	gStopReason = "";
	gWhy[0] = 0;
	IOLockUnlock(gLock);
	if (kernel_thread_start(sf_nvme_thread, NULL, &thread) != KERN_SUCCESS)
		return ENOMEM;
	thread_deallocate(thread);
	return 0;
}

/*
 * What sits below the block device in the registry: partitions, how many of them the guard
 * holds, and every object that is neither the storage family's plumbing nor the guard.
 */
static void
media_census(IORegistryEntry *entry, unsigned depth, unsigned *partitions, unsigned *guarded, unsigned *foreign, unsigned *exempt,
    char *names, size_t len)
{
	OSIterator *iter = entry->getChildIterator(gIOServicePlane);
	OSObject *o;

	if (iter == NULL || depth > 8) {
		OSSafeReleaseNULL(iter);
		return;
	}
	while ((o = iter->getNextObject()) != NULL) {
		IORegistryEntry *child = OSDynamicCast(IORegistryEntry, o);
		IOMedia *media = OSDynamicCast(IOMedia, o);
		const char *cls = o->getMetaClass()->getClassName();

		if (child == NULL)
			continue;
		if (media != NULL) {
			if (!media->isWhole()) {
				(*partitions)++;
				if (is_our_partition(media))
					(*exempt)++;
			}
		} else if (strcmp(cls, "SFAppleNVMeMediaGuard") == 0) {
			(*guarded)++;
		} else if (strcmp(cls, "IOBlockStorageDriver") != 0 && strcmp(cls, "IOGUIDPartitionScheme") != 0 &&
		    strcmp(cls, "IOMediaBSDClient") != 0) {
			(*foreign)++;
			strlcat(names, " ", len);
			strlcat(names, cls, len);
		}
		media_census(child, depth + 1, partitions, guarded, foreign, exempt, names, len);
	}
	iter->release();
}

/* ---- sysctl -------------------------------------------------------------------------------- */

enum { kCtlState, kCtlGate, kCtlWrite, kCtlStats, kCtlIdCtrl, kCtlIdNs, kCtlSmart, kCtlStop, kCtlGateTest, kCtlRegs, kCtlMedia, kCtlSmartRaw, kCtlStart, kCtlPart, kCtlFence };

static uint64_t le64(const uint8_t *p) { return OSReadLittleInt64(p, 0); }
static uint32_t le32(const uint8_t *p) { return OSReadLittleInt32(p, 0); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Every opcode on both queues through the gate, as bare commands and as plausible ones. Touches no hardware and no counter. */
static void
gate_selftest(char *text, size_t len)
{
	unsigned allowed = 0, refused = 0, wrong = 0;
	static const uint8_t kMustRefuseIo[] = { 0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d, 0x0e, 0x11, 0x15, 0x19 };
	static const uint8_t kMustRefuseAdmin[] = { 0x09, 0x0d, 0x10, 0x11, 0x15, 0x80, 0x81, 0x82, 0x84, 0xd8 };

	for (unsigned q = 0; q < 2; q++)
		for (unsigned op = 0; op < 256; op++) {
			uint8_t sqe[64] = { 0 };
			sqe[0] = (uint8_t)op;
			OSWriteLittleInt32(sqe, 4, q == SF_NVME_Q_IO ? 1 : 0);
			if (q == SF_NVME_Q_IO)
				OSWriteLittleInt32(sqe, 48, 0);     /* one block at LBA 0 */
			else
				OSWriteLittleInt32(sqe, 40, 1);
			if (sf_nvme_opcode_allowed(q, (uint8_t)op))
				allowed++;
			else if (sf_nvme_gate(q, sqe, 1000) != SF_NVME_GATE_OK)
				refused++;
			else
				wrong++;
		}
	for (unsigned i = 0; i < sizeof(kMustRefuseIo); i++)
		if (sf_nvme_opcode_allowed(SF_NVME_Q_IO, kMustRefuseIo[i]))
			wrong++;
	for (unsigned i = 0; i < sizeof(kMustRefuseAdmin); i++)
		if (sf_nvme_opcode_allowed(SF_NVME_Q_ADMIN, kMustRefuseAdmin[i]))
			wrong++;
	snprintf(text, len, "allowed=%u refused=%u wrong=%u (512 queue/opcode pairs; expected 7/505/0)", allowed, refused, wrong);
}

static int
sf_sysctl(struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	char text[1100];
	int value = 0, error;
	const uint8_t *c = gAns.idCtrl, *n = gAns.idNs, *s = gAns.smart;

	text[0] = 0;
	switch (arg2) {
	case kCtlState:
		snprintf(text, sizeof(text), "%s level=%u ttl=%u%s%s", kStateNames[gState], gLevel, gTtl, gWhy[0] ? " : " : "", gWhy);
		break;
	case kCtlGate:
		value = (int)gAns.gateRejected;
		return SYSCTL_OUT(req, &value, sizeof(value));
	case kCtlWrite:
		value = (int)gWriteRefused;
		return SYSCTL_OUT(req, &value, sizeof(value));
	case kCtlStats:
		snprintf(text, sizeof(text), "admin=%llu reads=%llu blocks=%llu bytes=%llu errors=%llu timeouts=%llu stray=%llu "
		    "tcb_inval_fail=%llu avg_us=%llu max_us=%llu doorbells_admin=%llu doorbells_io=%llu "
		    "admin_ops[00,01,02,04,05,06]=%llu,%llu,%llu,%llu,%llu,%llu io_ops[02]=%llu sync_ignored=%u syslog=%u "
		    "rtkit_rx=%u rtkit_tx=%u rtkit_unknown=%u sart_used=0x%x sart_protected=0x%x start_ms=%llu stop_ms=%llu guarded=%u",
		    gAns.adminCommands, gAns.readCommands, gAns.readBlocks, gBytes, gAns.cmdErrors, gAns.cmdTimeouts,
		    gAns.strayCompletions, gAns.tcbInvalFailures, gCompleted ? gSumMicros / gCompleted : 0, gMaxMicros,
		    gAns.doorbells[0], gAns.doorbells[1], gAns.opcodeCount[0][0], gAns.opcodeCount[0][1], gAns.opcodeCount[0][2],
		    gAns.opcodeCount[0][4], gAns.opcodeCount[0][5], gAns.opcodeCount[0][6], gAns.opcodeCount[1][2], gSyncIgnored,
		    gAns.syslogLines, gAns.rxMessages, gAns.txMessages, gAns.unknownMessages, gAns.sartUsed, gAns.sartProtected,
		    gReadyAtMs ? gReadyAtMs - gStartMs : 0, gStopMs, gGuarded);
		break;
	case kCtlIdCtrl: {
		/* the fields `nvme id-ctrl` prints, without the serial number and other identities */
		char mn[41], fr[9];
		trimmed(mn, sizeof(mn), c + 24, 40);
		trimmed(fr, sizeof(fr), c + 64, 8);
		snprintf(text, sizeof(text), "vid=0x%x ssvid=0x%x mn=\"%s\" fr=\"%s\" rab=%u ieee=%02x%02x%02x cmic=0x%x mdts=%u "
		    "cntlid=0x%x ver=0x%x rtd3r=0x%x rtd3e=0x%x oaes=0x%x ctratt=0x%x oacs=0x%x acl=%u aerl=%u frmw=0x%x lpa=0x%x "
		    "elpe=%u npss=%u avscc=0x%x apsta=0x%x wctemp=%u cctemp=%u sqes=0x%x cqes=0x%x maxcmd=%u nn=%u oncs=0x%x "
		    "fuses=0x%x fna=0x%x vwc=0x%x awun=%u awupf=%u nvscc=%u sgls=0x%x",
		    le16(c + 0), le16(c + 2), mn, fr, c[72], c[75], c[74], c[73], c[76], c[77], le16(c + 78), le32(c + 80),
		    le32(c + 84), le32(c + 88), le32(c + 92), le32(c + 96), le16(c + 256), c[258], c[259], c[260], c[261], c[262],
		    c[263], c[264], c[265], le16(c + 266), le16(c + 268), c[512], c[513], le16(c + 514), le32(c + 516),
		    le16(c + 520), le16(c + 522), c[524], c[525], le16(c + 526), le16(c + 528), c[530], le32(c + 536));
		break;
	}
	case kCtlIdNs: {
		uint32_t lbaf = le32(n + 128 + 4 * (n[26] & 0xf));
		snprintf(text, sizeof(text), "nsze=%llu ncap=%llu nuse=%llu nsfeat=0x%x nlbaf=%u flbas=0x%x mc=0x%x dpc=0x%x dps=0x%x "
		    "lbaf_ms=%u lbaf_lbads=%u lbaf_rp=%u", le64(n + 0), le64(n + 8), le64(n + 16), n[24], n[25], n[26], n[27], n[28],
		    n[29], lbaf & 0xffff, (lbaf >> 16) & 0xff, (lbaf >> 24) & 3);
		break;
	}
	case kCtlSmart:
		if (req->newptr) {
			/* any write: read the log again and wait for it */
			IOLockLock(gLock);
			if (gState == kStateReady) {
				gSmartRequested = true;
				IOLockWakeup(gLock, &gEvent, true);
				while (gSmartRequested)
					IOLockSleep(gLock, &gSmartRequested, THREAD_UNINT);
			}
			IOLockUnlock(gLock);
		}
		snprintf(text, sizeof(text), "critical_warning=0x%x temperature_k=%u available_spare=%u spare_thresh=%u percent_used=%u "
		    "data_units_read=%llu data_units_written=%llu host_read_commands=%llu host_write_commands=%llu "
		    "controller_busy_time=%llu power_cycles=%llu power_on_hours=%llu unsafe_shutdowns=%llu media_errors=%llu "
		    "num_err_log_entries=%llu (low 64 bits of each counter)",
		    s[0], le16(s + 1), s[3], s[4], s[5], le64(s + 32), le64(s + 48), le64(s + 64), le64(s + 80), le64(s + 96),
		    le64(s + 112), le64(s + 128), le64(s + 144), le64(s + 160), le64(s + 176));
		break;
	case kCtlStop:
		if (gState == kStateStoppedClean)
			value = 1;
		else if (gState == kStateStoppedUnclean || gState == kStateFailed)
			value = 2;
		error = sysctl_handle_int(oidp, &value, 0, req);
		if (error != 0 || req->newptr == USER_ADDR_NULL)
			return error;
		if (value == 1 && !request_stop("requested", 30000))
			return EBUSY;
		return 0;
	case kCtlGateTest:
		gate_selftest(text, sizeof(text));
		break;
	case kCtlMedia: {
		unsigned partitions = 0, guarded = 0, foreign = 0, exempt = 0;
		char names[512] = "", ours[40] = "";

		if (gDisk != NULL)
			media_census((IORegistryEntry *)gDisk, 0, &partitions, &guarded, &foreign, &exempt, names, sizeof(names));
		if (gPartState != kPartNotAsked)
			snprintf(ours, sizeof(ours), " ours=%u", exempt);
		snprintf(text, sizeof(text), "published=%d partitions=%u guarded=%u foreign=%u%s%s", gDisk != NULL, partitions, guarded,
		    foreign, ours, names);
		break;
	}
	case kCtlFence:
		snprintf(text, sizeof(text), "readfence=%u refused=%llu refused_blocks=%llu ours=%llu ours_blocks=%llu gpt=%llu other=%llu other_blocks=%llu",
		    gReadFence, gFenceRefused, gFenceRefusedBlocks, gOursRequests, gOursBlocks, gGptRequests, gOtherRequests, gOtherBlocks);
		break;
	case kCtlPart: {
		static const char *const names[] = { "not-asked", "unchecked", "verified", "refused" };
		/* no GUID here: it is in the boot arguments already and belongs in no log of ours */
		snprintf(text, sizeof(text), "%s first=%llu blocks=%llu exempted=%u: %s", names[gPartState], gWant.first, gWant.blocks,
		    gExempted, gPartWhy);
		break;
	}
	case kCtlSmartRaw:
		/* the whole 512-byte page as last read; it holds counters only, no identity */
		for (unsigned i = 0; i < 512; i++)
			snprintf(text + 2 * i, 3, "%02x", s[i]);
		break;
	case kCtlStart:
		value = (int)gStarts;
		error = sysctl_handle_int(oidp, &value, 0, req);
		if (error != 0 || req->newptr == USER_ADDR_NULL)
			return error;
		return value == 1 ? request_start() : EINVAL;
	}
	if (arg2 == kCtlSmart && req->newptr) {
		/* consume the written value */
		char in[16];
		(void)SYSCTL_IN(req, in, req->newlen < sizeof(in) ? req->newlen : sizeof(in));
	}
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_NODE(_kern, OID_AUTO, sf_nvme, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "internal NVMe storage, read-only");
#define SF_STR(name, id, text) \
	SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, name, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, id, sf_sysctl, "A", text)
SF_STR(state, kCtlState, "driver state");
SF_STR(stats, kCtlStats, "counters");
SF_STR(idctrl, kCtlIdCtrl, "Identify Controller fields");
SF_STR(idns, kCtlIdNs, "Identify Namespace 1 fields");
SF_STR(gate_selftest, kCtlGateTest, "every opcode through the command gate (no hardware access)");
SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, smart, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED, 0, kCtlSmart, sf_sysctl, "A",
    "SMART / health log; write anything to read it again");
SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, gate_rejected, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED, 0, kCtlGate, sf_sysctl, "I",
    "commands refused at the doorbell (must be 0)");
SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, write_refused, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED, 0, kCtlWrite, sf_sysctl, "I",
    "write, format and unmap requests refused before any command was built");
SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, stop, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, 0, kCtlStop, sf_sysctl, "I",
    "write 1: clean controller shutdown; reads 0 running, 1 stopped cleanly, 2 not cleanly or failed");

SF_STR(media, kCtlMedia, "what is attached below the block device");
SF_STR(smart_raw, kCtlSmartRaw, "SMART / health log page as last read, hexadecimal");
SF_STR(fence, kCtlFence, "requests through the block device by where they lie; refused ones when sfnvme_readfence=1");
SF_STR(part, kCtlPart, "the SiliconForest partition: asked for, verified in both GPT copies, or refused");
SYSCTL_PROC(_kern_sf_nvme, OID_AUTO, start, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, 0, kCtlStart, sf_sysctl, "I",
    "write 1 after a clean stop: start the controller again; reads the number of starts");

static struct sysctl_oid *const gOids[] = {
	&sysctl__kern_sf_nvme_media, &sysctl__kern_sf_nvme_part, &sysctl__kern_sf_nvme_fence, &sysctl__kern_sf_nvme_smart_raw, &sysctl__kern_sf_nvme_start,
	&sysctl__kern_sf_nvme, &sysctl__kern_sf_nvme_state, &sysctl__kern_sf_nvme_stats, &sysctl__kern_sf_nvme_idctrl,
	&sysctl__kern_sf_nvme_idns, &sysctl__kern_sf_nvme_gate_selftest, &sysctl__kern_sf_nvme_smart,
	&sysctl__kern_sf_nvme_gate_rejected, &sysctl__kern_sf_nvme_write_refused, &sysctl__kern_sf_nvme_stop,
};

/* ---- the service --------------------------------------------------------------------------- */

class SFAppleNVMe : public IOService
{
	OSDeclareDefaultStructors(SFAppleNVMe);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void systemWillShutdown(IOOptionBits specifier) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SFAppleNVMe, IOService);

bool
SFAppleNVMe::start(IOService *provider)
{
	IORegistryEntry *chosen;
	OSData *chip;
	uint32_t chipID = 0, value = 0;
	thread_t thread;

	if (!IOService::start(provider))
		return false;
	/* first of all: without the boot argument nothing below runs */
	if (!PE_parse_boot_argn("sfnvme", &gLevel, sizeof(gLevel)) || gLevel == 0 || gLevel > 3) {
		LOG("off (sfnvme=%u): the internal storage is not touched", gLevel);
		return false;
	}
	chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	chip = chosen ? OSDynamicCast(OSData, chosen->getProperty("chip-id")) : NULL;
	if (chip != NULL && chip->getLength() >= sizeof(uint32_t))
		chipID = *(const uint32_t *)chip->getBytesNoCopy();
	OSSafeReleaseNULL(chosen);
	if (chipID != T8103_CHIP_ID) {
		LOG("chip 0x%x is not T8103; nothing is done", chipID);
		return false;
	}
	if (PE_parse_boot_argn("sfnvme_ttl", &value, sizeof(value)) && (value == 0 || (value >= 10 && value <= 570)))
		gTtl = value;
	if (PE_parse_boot_argn("sfnvme_delay", &value, sizeof(value)) && value <= 60000)
		gDelayMs = value;
	if (PE_parse_boot_argn("sfnvme_verbose", &value, sizeof(value)))
		gAns.verbose = value != 0;

	gLock = IOLockAlloc();
	gPartLock = IOLockAlloc();
	if (gLock == NULL || gPartLock == NULL)
		return false;
	{
		char guid[40] = "";
		uint64_t first = 0, blocks = 0;
		bool a = PE_parse_boot_argn("sfnvme_part", guid, sizeof(guid)), b = PE_parse_boot_argn("sfnvme_part_lba", &first, sizeof(first)),
		    c = PE_parse_boot_argn("sfnvme_part_blocks", &blocks, sizeof(blocks));

		if (a || b || c) {
			if (gLevel == 3 && a && b && c && blocks != 0 && sf_part_parse_guid(guid, gWant.guid) == 0) {
				gWant.first = first;
				gWant.blocks = blocks;
				gPartState = kPartUnchecked;
				if (PE_parse_boot_argn("sfnvme_readfence", &value, sizeof(value)) && value == 1)
					gReadFence = 1;
				snprintf(gPartWhy, sizeof(gPartWhy), "not looked at yet");
			} else {
				gPartState = kPartRefused;
				snprintf(gPartWhy, sizeof(gPartWhy), "the three boot arguments are incomplete or malformed, or sfnvme is not 3");
			}
			LOG("a SiliconForest partition was asked for: %s", gPartWhy);
		}
	}
	gService = this;
	for (unsigned i = 0; i < sizeof(gOids) / sizeof(gOids[0]); i++)
		sysctl_register_oid(gOids[i]);
	if (kernel_thread_start(sf_nvme_thread, NULL, &thread) != KERN_SUCCESS)
		return false;
	thread_deallocate(thread);
	registerService();
	return true;
}

void
SFAppleNVMe::systemWillShutdown(IOOptionBits specifier)
{
	if (gLock != NULL && !request_stop("system shutdown", 30000))
		LOG("the controller was still running when the system went down");
	IOService::systemWillShutdown(specifier);
}
