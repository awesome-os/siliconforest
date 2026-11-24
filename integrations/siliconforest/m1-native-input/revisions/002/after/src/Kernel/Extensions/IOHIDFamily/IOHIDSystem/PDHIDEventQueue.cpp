/*
 * Raw HID event queue, shared by the USB and PS/2 drivers because there can
 * only be one /dev node per queue. The node names keep their "usb_" prefix
 * because that is what the X input driver and the wlroots backend open.
 */
#include "PDHIDEventQueue.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <kern/thread_call.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/uio.h>

extern "C" int devfs_is_ready(void);
extern "C" void cons_cinput(char ch);

#define DEADKEY kPDHIDNoADBCode

static const UInt8 gUSBToADB[] = {
    DEADKEY, DEADKEY, DEADKEY, DEADKEY,
    0x00, 0x0b, 0x08, 0x02, 0x0e, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25,
    0x2e, 0x2d, 0x1f, 0x23, 0x0c, 0x0f, 0x01, 0x11, 0x20, 0x09, 0x0d, 0x07,
    0x10, 0x06, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1a, 0x1c, 0x19, 0x1d,
    0x24, 0x35, 0x33, 0x30, 0x31, 0x1b, 0x18, 0x21, 0x1e, 0x2a, 0x2a, 0x29,
    0x27, 0x32, 0x2b, 0x2f, 0x2c, 0x39, 0x7a, 0x78, 0x63, 0x76, 0x60, 0x61,
    0x62, 0x64, 0x65, 0x6d, 0x67, 0x6f, 0x69, 0x6b, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x77, 0x79, 0x7c, 0x7b, 0x7d, 0x7e, 0x47, 0x4b, 0x43, 0x4e, 0x45,
    0x4c, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5b, 0x5c, 0x52, 0x41,
    0x0a, 0x6e, 0x7f, 0x51, 0x69, 0x6b, 0x71, 0x6a, 0x40, 0x4f, 0x50, 0x5a,
    DEADKEY, DEADKEY, DEADKEY, DEADKEY, DEADKEY, 0x72,
};

/* Usages 0xE0-0xE7: L/R control, shift, alt, gui. */
static const UInt8 gUSBModToADB[8] = {
    0x3b, 0x38, 0x3a, 0x37, 0x3e, 0x3c, 0x3d, 0x36
};

UInt8
PDHIDUsageToADB(UInt8 usage)
{
    if (usage >= 0xE0 && usage <= 0xE7)
        return gUSBModToADB[usage - 0xE0];
    if (usage >= sizeof(gUSBToADB) / sizeof(gUSBToADB[0]))
        return kPDHIDNoADBCode;
    return gUSBToADB[usage];
}

static void *gKbdNode;
static void *gPS2KbdNode;
static int gKbdMajor = -1;
static IOLock *gKbdLock;
static UInt32 gKbdSequence;
static UInt32 gKbdReadIndex;
static UInt32 gKbdWriteIndex;
static PDHIDKbdEvent gKbdEvents[64];

// console side, guarded by gKbdLock and drained by gConsoleCall
static thread_call_t gConsoleCall;
static UInt8 gModifiers;
static UInt8 gLockLEDs;
static bool gLEDsDirty;
static UInt32 gConsReadIndex;
static UInt32 gConsWriteIndex;
static char gConsBytes[256];

struct PDHIDLEDEntry {
    PDHIDLEDHandler handler;
    void *target;
};

static IOLock *gLEDLock;
static PDHIDLEDEntry gLEDHandlers[8];

static UInt8 gNextMouseIndex;

static void *gMouseNode;
static void *gPS2MouseNode;
static int gMouseMajor = -1;
static IOLock *gMouseLock;
static UInt32 gMouseSequence;
static UInt32 gMouseReadIndex;
static UInt32 gMouseWriteIndex;
static PDHIDMouseEvent gMouseEvents[64];

static void pd_hid_console_work(thread_call_param_t, thread_call_param_t);
static void pd_hid_publish_work(thread_call_param_t, thread_call_param_t);

/* Only the ONCE worker touches majors and devfs-node pointers. Requested
 * capabilities are monotonic for these boot-lifetime shared queues. A caller
 * arriving during publication re-pends the same worker, never recurses into
 * devfs or IOKit notification registration. */
static thread_call_t gPublishCall;
static volatile UInt32 gPublishRequests;
static volatile UInt32 gPublishReady;
static volatile UInt32 gPublishStopped;
static volatile UInt32 gQueueReady;
static volatile UInt32 gKbdEarlyDrops, gMouseEarlyDrops;
static volatile UInt32 gPublishAllocationFailures;
struct PDHIDPublicationState {
    UInt32 attempts, lastError;
    AbsoluteTime startedAt, nextAttempt;
};
/* The notifier and all non-atomic readiness state belong only to the ONCE
 * worker. Its callback may run synchronously before registration returns. */
static IONotifier *gBSDNotifier;
static volatile UInt32 gBSDSeen;
static bool gBSDWaitStarted, gBSDReady, gBSDWaitRefused;
static AbsoluteTime gBSDWaitStart, gBSDWaitDeadline, gBSDRegisterAfter;
static UInt32 gBSDRegistrationFailures;
static const UInt32 kBSDWaitSeconds = 180;
static const UInt32 kBSDRegistrationLimit = 60;
static PDHIDPublicationState gKbdPublication, gMousePublication;
enum {
    kPublishAttemptLimit = 60,
    kPublishOK = 0, kPublishStorage = 1, kPublishMajor = 2,
    kPublishDevfs = 3, kPublishNode = 4,
    kPublishNotification = 5, kPublishReadinessTimeout = 6
};
enum {
    kPublishKeyboard = 1, kPublishMouse = 2,
    kPublishPS2Keyboard = 4, kPublishPS2Mouse = 8
};

template <typename T> static T
pd_hid_load(T const *slot)
{
    return __atomic_load_n(slot, __ATOMIC_ACQUIRE);
}

static bool
pd_hid_setup_lock(IOLock **slot)
{
    if (!pd_hid_load(slot)) {
        IOLock *lock = IOLockAlloc();
        if (lock && !OSCompareAndSwapPtr(NULL, lock, (void * volatile *)slot))
            IOLockFree(lock);
    }
    return pd_hid_load(slot) != NULL;
}

static int pd_hid_open(dev_t, int, int, struct proc *)
{
    return (OSBitOrAtomic(0, &gQueueReady) & kPublishMouse) ? 0 : ENXIO;
}
static int pd_hid_close(dev_t, int, int, struct proc *) { return 0; }

/* spec_open() calls d_open on every open but spec_close() only reaches d_close
 * once vcount() drops to zero, so this tracks "somebody has it open" rather
 * than a balanced count. */
static volatile bool gKbdGrabbed;

static int
pd_hid_kbd_open(dev_t, int, int, struct proc *)
{
    if (!(OSBitOrAtomic(0, &gQueueReady) & kPublishKeyboard)) return ENXIO;
    __atomic_store_n(&gKbdGrabbed, true, __ATOMIC_RELEASE);
    return 0;
}

static int
pd_hid_kbd_close(dev_t, int, int, struct proc *)
{
    __atomic_store_n(&gKbdGrabbed, false, __ATOMIC_RELEASE);
    return 0;
}

bool
PDHIDKeyboardIsGrabbed(void)
{
    return __atomic_load_n(&gKbdGrabbed, __ATOMIC_ACQUIRE);
}

static int
pd_hid_kbd_read(dev_t, struct uio *uio, int)
{
    PDHIDKbdEvent event;
    int error = 0;

    if (!(OSBitOrAtomic(0, &gQueueReady) & kPublishKeyboard)) return ENXIO;
    if (uio_resid(uio) < (user_ssize_t)sizeof(event)) return EINVAL;

    IOLockLock(pd_hid_load(&gKbdLock));
    if (gKbdReadIndex == gKbdWriteIndex) {
        error = EAGAIN;
    } else {
        event = gKbdEvents[gKbdReadIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]))];
        gKbdReadIndex++;
    }
    IOLockUnlock(pd_hid_load(&gKbdLock));

    if (error) return error;
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static int
pd_hid_mouse_read(dev_t, struct uio *uio, int)
{
    PDHIDMouseEvent event;
    int error = 0;

    if (!(OSBitOrAtomic(0, &gQueueReady) & kPublishMouse)) return ENXIO;
    if (uio_resid(uio) < (user_ssize_t)sizeof(event)) return EINVAL;

    IOLockLock(pd_hid_load(&gMouseLock));
    if (gMouseReadIndex == gMouseWriteIndex) {
        error = EAGAIN;
    } else {
        event = gMouseEvents[gMouseReadIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]))];
        gMouseReadIndex++;
    }
    IOLockUnlock(pd_hid_load(&gMouseLock));

    if (error) return error;
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static struct cdevsw pd_hid_kbd_cdevsw = {
    pd_hid_kbd_open, pd_hid_kbd_close, pd_hid_kbd_read, eno_rdwrt,
    eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
    eno_strat, eno_getc, eno_putc, 0
};

static struct cdevsw pd_hid_mouse_cdevsw = {
    pd_hid_open, pd_hid_close, pd_hid_mouse_read, eno_rdwrt,
    eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
    eno_strat, eno_getc, eno_putc, 0
};

/* All pointer installations are CAS-once. Uninstalled losers are freed before
 * they can be queued; installed locks and calls remain valid until reboot. */
static bool
pd_hid_kbd_setup(void)
{
    if (!pd_hid_setup_lock(&gKbdLock) || !pd_hid_setup_lock(&gLEDLock)) return false;
    if (!pd_hid_load(&gConsoleCall)) {
        thread_call_t call = thread_call_allocate(pd_hid_console_work, NULL);
        if (call && !OSCompareAndSwapPtr(NULL, call, (void * volatile *)&gConsoleCall))
            thread_call_free(call);
    }
    return pd_hid_load(&gConsoleCall) != NULL;
}

static void
pd_hid_request_publication(UInt32 requested)
{
    const UInt32 previous = OSBitOrAtomic(requested, &gPublishRequests);
    if (!(requested & ~(OSBitOrAtomic(0, &gPublishReady) |
                        OSBitOrAtomic(0, &gPublishStopped)))) return;
    // Repeated requests must not accelerate a one-second failure retry or
    // consume its budget. A new capability may wake the one owner early.
    if (pd_hid_load(&gPublishCall) && !(requested & ~previous)) return;
    if (!pd_hid_load(&gPublishCall)) {
        thread_call_t call = thread_call_allocate_with_options(pd_hid_publish_work,
            NULL, THREAD_CALL_PRIORITY_KERNEL, THREAD_CALL_OPTIONS_ONCE);
        if (call && !OSCompareAndSwapPtr(NULL, call, (void * volatile *)&gPublishCall))
            thread_call_free(call);
    }
    thread_call_t call = pd_hid_load(&gPublishCall);
    /* If even the call cannot be allocated, preserve the request for a later
     * caller. There is no endpoint and no success indication in this API. */
    if (call) thread_call_enter(call);
    else if (__atomic_fetch_add(&gPublishAllocationFailures, 1U, __ATOMIC_RELAXED) == 0)
        IOLog("PDHIDEventQueue: publication call allocation failed; endpoint absent\n");
}

static unsigned long long
pd_hid_milliseconds(AbsoluteTime value)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(value, &ns);
    return (unsigned long long)(ns / 1000000);
}

/* Never inspect/remove the returned notifier here: addMatchingNotification
 * invokes this callback for an existing IOBSD before it returns that pointer.
 * An event marks readiness-to-recheck, not devfs success. Duplicate callbacks
 * do not accelerate deadlines or spend publication attempts. */
static bool
pd_hid_iobsd_published(void *, void *, IOService *, IONotifier *)
{
    if (!(OSBitOrAtomic(1, &gBSDSeen) & 1)) {
        thread_call_t call = pd_hid_load(&gPublishCall);
        if (call) thread_call_enter(call);
    }
    return true;
}

static void
pd_hid_remove_notifier(void)
{
    IONotifier *notifier = gBSDNotifier;
    gBSDNotifier = NULL;
    if (notifier) notifier->remove();
}

static void
pd_hid_schedule_readiness(AbsoluteTime deadline, UInt32 observedSeen)
{
    thread_call_enter_delayed(pd_hid_load(&gPublishCall), deadline);
    /* Close the event-before-delayed-arm race. If the event arrives after
     * this check, its callback is instead the last writer and queues us. */
    if (OSBitOrAtomic(0, &gBSDSeen) != observedSeen)
        thread_call_enter(pd_hid_load(&gPublishCall));
}

static bool
pd_hid_refuse_readiness(UInt32 error, AbsoluteTime now)
{
    gBSDWaitRefused = true;
    OSBitOrAtomic(kPublishKeyboard | kPublishMouse | kPublishPS2Keyboard |
                  kPublishPS2Mouse, &gPublishStopped);
    pd_hid_remove_notifier();
    IOLog("PDHIDEventQueue: BSD wait refused error=%u registration_failures=%u seen=%u uptime_ms=%llu elapsed_ms=%llu\n",
        (unsigned)error, (unsigned)gBSDRegistrationFailures,
        (unsigned)OSBitOrAtomic(0, &gBSDSeen), pd_hid_milliseconds(now),
        pd_hid_milliseconds(now - gBSDWaitStart));
    return false;
}

/* Registration failure has its own bounded one-second retry policy. Once a
 * notifier is installed we sleep until IOBSD or the finite readiness watchdog.
 * IOBSD is published after vfsinit/devfs_sinit in pinned bsd_init; still check
 * devfs_is_ready itself and fail closed if that contract is not fulfilled. */
static bool
pd_hid_devfs_gate(void)
{
    AbsoluteTime now;
    clock_get_uptime(&now);
    const UInt32 observedSeen = OSBitOrAtomic(0, &gBSDSeen);
    if (gBSDWaitRefused) return false;
    if (gBSDReady) return true;
    if (gBSDWaitStarted && now >= gBSDWaitDeadline)
        return pd_hid_refuse_readiness(kPublishReadinessTimeout, now);
    if (devfs_is_ready()) {
        gBSDReady = true;
        pd_hid_remove_notifier();
        IOLog("PDHIDEventQueue: BSD ready seen=%u uptime_ms=%llu elapsed_ms=%llu\n",
            (unsigned)observedSeen, pd_hid_milliseconds(now),
            gBSDWaitStarted ? pd_hid_milliseconds(now - gBSDWaitStart) : 0ULL);
        return true;
    }
    if (!gBSDWaitStarted) {
        gBSDWaitStarted = true;
        gBSDWaitStart = now;
        clock_interval_to_deadline(kBSDWaitSeconds, kSecondScale, &gBSDWaitDeadline);
        IOLog("PDHIDEventQueue: BSD wait error=3 uptime_ms=%llu timeout_ms=%u\n",
            pd_hid_milliseconds(now), (unsigned)(kBSDWaitSeconds * 1000));
    }
    if (!observedSeen && !gBSDNotifier && now >= gBSDRegisterAfter) {
        OSDictionary *matching = IOService::resourceMatching("IOBSD");
        IONotifier *notifier = NULL;
        if (matching) {
            notifier = IOService::addMatchingNotification(gIOPublishNotification,
                matching, pd_hid_iobsd_published, NULL, NULL);
            matching->release();
        }
        /* Only the owner stores this, after any synchronous callback returned. */
        gBSDNotifier = notifier;
        if (!notifier) {
            ++gBSDRegistrationFailures;
            if (gBSDRegistrationFailures >= kBSDRegistrationLimit)
                return pd_hid_refuse_readiness(kPublishNotification, now);
            clock_interval_to_deadline(1, kSecondScale, &gBSDRegisterAfter);
            if (gBSDRegistrationFailures == 1)
                IOLog("PDHIDEventQueue: BSD notification deferred error=5 uptime_ms=%llu\n",
                    pd_hid_milliseconds(now));
        }
    }
    /* Recheck after registration, including the synchronous-existing case.
     * Registration itself may have consumed time; do not use a stale sample. */
    clock_get_uptime(&now);
    if (now >= gBSDWaitDeadline)
        return pd_hid_refuse_readiness(kPublishReadinessTimeout, now);
    if (devfs_is_ready()) {
        gBSDReady = true;
        pd_hid_remove_notifier();
        IOLog("PDHIDEventQueue: BSD ready seen=%u uptime_ms=%llu elapsed_ms=%llu\n",
            (unsigned)OSBitOrAtomic(0, &gBSDSeen), pd_hid_milliseconds(now),
            pd_hid_milliseconds(now - gBSDWaitStart));
        return true;
    }
    AbsoluteTime next = gBSDWaitDeadline;
    if (OSBitOrAtomic(0, &gBSDSeen)) {
        /* An IOBSD event is not permission to fabricate a ready devfs. */
        pd_hid_remove_notifier();
        clock_interval_to_deadline(1, kSecondScale, &next);
    } else if (!gBSDNotifier) {
        next = gBSDRegisterAfter;
    }
    if (next > gBSDWaitDeadline) next = gBSDWaitDeadline;
    pd_hid_schedule_readiness(next, observedSeen);
    return false;
}

/* Worker-only: no lock is held across cdevsw/devfs calls. Even if one of them
 * re-enters a public publisher, it can only schedule another ONCE invocation. */
static UInt32
pd_hid_publish_nodes(int *major, struct cdevsw *ops, void **node,
    const char *name, void **alias, bool wantAlias, const char *aliasName)
{
    if (*major < 0) {
        *major = cdevsw_add(-1, ops);
        if (*major < 0) return kPublishMajor;
    }
    if (!devfs_is_ready()) return kPublishDevfs;
    if (!*node) {
        *node = devfs_make_node(makedev(*major, 0), DEVFS_CHAR, 0, 0, 0666, "%s", name);
        if (*node) IOLog("PDHIDEventQueue: published /dev/%s\n", name);
    }
    if (wantAlias && !*alias) {
        *alias = devfs_make_node(makedev(*major, 0), DEVFS_CHAR, 0, 0, 0666, "%s", aliasName);
        if (*alias) IOLog("PDHIDEventQueue: published /dev/%s\n", aliasName);
    }
    return *node && (!wantAlias || *alias) ? kPublishOK : kPublishNode;
}

/* Worker-only. Retry exhaustion is terminal for that queue for this boot;
 * repeated events/requests cannot silently restart the budget. Error values
 * describe which API failed, not invented errno values from devfs_make_node. */
static bool
pd_hid_publish_one(bool keyboard, UInt32 requested)
{
    const UInt32 primary = keyboard ? kPublishKeyboard : kPublishMouse;
    const UInt32 alias = keyboard ? kPublishPS2Keyboard : kPublishPS2Mouse;
    const UInt32 wanted = requested & (primary | alias);
    if (!(wanted & primary) || (OSBitOrAtomic(0, &gPublishStopped) & primary) ||
        !(wanted & ~OSBitOrAtomic(0, &gPublishReady))) return true;
    PDHIDPublicationState &state = keyboard ? gKbdPublication : gMousePublication;
    AbsoluteTime now;
    clock_get_uptime(&now);
    if (state.attempts && now < state.nextAttempt) return false;
    if (!state.attempts) state.startedAt = now;
    state.attempts++;
    state.lastError = kPublishStorage;
    if (keyboard ? pd_hid_kbd_setup() : pd_hid_setup_lock(&gMouseLock)) {
        /* Storage and its actual startup request precede node visibility.
         * Open/read use this barrier; an early input may now enqueue safely. */
        OSBitOrAtomic(primary, &gQueueReady);
        state.lastError = keyboard ?
            pd_hid_publish_nodes(&gKbdMajor, &pd_hid_kbd_cdevsw, &gKbdNode,
                "usb_hid_kbd", &gPS2KbdNode, wanted & alias, "ps2_kbd") :
            pd_hid_publish_nodes(&gMouseMajor, &pd_hid_mouse_cdevsw, &gMouseNode,
                "usb_hid_mouse", &gPS2MouseNode, wanted & alias, "ps2_mouse");
    }
    if (state.lastError == kPublishOK) {
        OSBitOrAtomic(wanted, &gPublishReady);
        IOLog("PDHIDEventQueue: ready kind=%u attempts=%u early_drops=%u uptime_ms=%llu elapsed_ms=%llu (endpoint, not input proof)\n",
            (unsigned)primary, (unsigned)state.attempts,
            (unsigned)__atomic_load_n(keyboard ? &gKbdEarlyDrops : &gMouseEarlyDrops, __ATOMIC_RELAXED),
            pd_hid_milliseconds(now), pd_hid_milliseconds(now - state.startedAt));
        return true;
    }
    if (state.attempts >= kPublishAttemptLimit) {
        OSBitOrAtomic(primary | alias, &gPublishStopped);
        IOLog("PDHIDEventQueue: publication refused kind=%u attempts=%u error=%u uptime_ms=%llu elapsed_ms=%llu\n",
            (unsigned)primary, (unsigned)state.attempts, (unsigned)state.lastError,
            pd_hid_milliseconds(now), pd_hid_milliseconds(now - state.startedAt));
        return true; // refused, not ready; no retry after the bound
    }
    if (state.attempts == 1)
        IOLog("PDHIDEventQueue: publication deferred kind=%u attempt=1 error=%u uptime_ms=%llu elapsed_ms=0\n",
            (unsigned)primary, (unsigned)state.lastError, pd_hid_milliseconds(now));
    clock_interval_to_deadline(1, kSecondScale, &state.nextAttempt);
    return false;
}

static void
pd_hid_publish_work(thread_call_param_t, thread_call_param_t)
{
    const UInt32 requested = OSBitOrAtomic(0, &gPublishRequests);
    if (!(requested & ~(OSBitOrAtomic(0, &gPublishReady) |
                        OSBitOrAtomic(0, &gPublishStopped)))) return;
    /* Prepare real storage so arriving reports can enqueue while BSD starts.
     * No major/node allocation and no publication-failure budget before BSD. */
    if (!devfs_is_ready()) {
        if ((requested & kPublishKeyboard) && pd_hid_kbd_setup())
            OSBitOrAtomic(kPublishKeyboard, &gQueueReady);
        if ((requested & kPublishMouse) && pd_hid_setup_lock(&gMouseLock))
            OSBitOrAtomic(kPublishMouse, &gQueueReady);
    }
    if (!pd_hid_devfs_gate()) return;
    const bool keyboardDone = pd_hid_publish_one(true, requested);
    const bool mouseDone = pd_hid_publish_one(false, requested);
    if (!keyboardDone || !mouseDone) {
        AbsoluteTime deadline = !keyboardDone ? gKbdPublication.nextAttempt : gMousePublication.nextAttempt;
        if (!mouseDone && gMousePublication.nextAttempt < deadline)
            deadline = gMousePublication.nextAttempt;
        thread_call_enter_delayed(pd_hid_load(&gPublishCall), deadline);
    }
}

void PDHIDPublishKeyboardDevice(void)
{
    pd_hid_request_publication(kPublishKeyboard);
}

void PDHIDPublishPS2KeyboardDevice(void)
{
    pd_hid_request_publication(kPublishKeyboard | kPublishPS2Keyboard);
}

UInt8 PDHIDAllocateMouseIndex(void)
{
    return gNextMouseIndex++;
}

void PDHIDPublishMouseDevice(void)
{
    pd_hid_request_publication(kPublishMouse);
}

void PDHIDPublishPS2MouseDevice(void)
{
    pd_hid_request_publication(kPublishMouse | kPublishPS2Mouse);
}

// US layout for keyboard page usages 0x04-0x38 (HID Usage Tables 1.12, section 10)
static const char gUsageToASCII[2][0x39] = {
    {
        0, 0, 0, 0,
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '\r', 0x1b, 0x7f, '\t', ' ', '-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',', '.', '/',
    },
    {
        0, 0, 0, 0,
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
        '\r', 0x1b, 0x7f, '\t', ' ', '_', '+', '{', '}', '|', '~', ':', '"', '~', '<', '>', '?',
    },
};

// keypad usages 0x54-0x63 with Num Lock on
static const char gKeypadToASCII[] = "/*-+\r1234567890.";

// gKbdLock held
static void
pd_hid_console_put(const char *bytes, unsigned count)
{
    const UInt32 size = (UInt32)sizeof(gConsBytes);

    for (unsigned i = 0; i < count; i++) {
        if (gConsWriteIndex - gConsReadIndex >= size) return;
        gConsBytes[gConsWriteIndex % size] = bytes[i];
        gConsWriteIndex++;
    }
}

// gKbdLock held, returns true when bytes or LED changes are queued for the thread call
static bool
pd_hid_console_key(UInt8 usage, bool down)
{
    const char *esc = NULL;
    bool shift, ctrl;

    if (usage >= 0xE0 && usage <= 0xE7) {
        if (down)
            gModifiers |= (UInt8)(1U << (usage - 0xE0));
        else
            gModifiers &= (UInt8)~(1U << (usage - 0xE0));
        return false;
    }
    if (!down) return false;

    if (usage == 0x39 || usage == 0x53 || usage == 0x47) {
        gLockLEDs ^= usage == 0x39 ? kPDHIDLEDCapsLock : usage == 0x53 ? kPDHIDLEDNumLock : kPDHIDLEDScrollLock;
        gLEDsDirty = true;
        return true;
    }
    if (PDHIDKeyboardIsGrabbed()) return false;

    shift = (gModifiers & 0x22) != 0;
    ctrl = (gModifiers & 0x11) != 0;

    if (usage >= 0x04 && usage < 0x39) {
        char c = gUsageToASCII[shift ? 1 : 0][usage];

        if (usage <= 0x1D && (gLockLEDs & kPDHIDLEDCapsLock))
            c = gUsageToASCII[shift ? 0 : 1][usage];
        if (ctrl) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c >= '@' && c <= '_')
                c = (char)(c & 0x1f);
            else if (c == ' ')
                c = 0;
            else if (c == '/' || c == '?')
                c = 0x1f;
        }
        pd_hid_console_put(&c, 1);
        return true;
    }
    if (usage >= 0x54 && usage <= 0x63) {
        if (usage != 0x58 && !(gLockLEDs & kPDHIDLEDNumLock)) return false;
        pd_hid_console_put(&gKeypadToASCII[usage - 0x54], 1);
        return true;
    }

    // ANSI cursor and editing sequences as xterm sends them
    switch (usage) {
    case 0x4F: esc = "\033[C"; break;
    case 0x50: esc = "\033[D"; break;
    case 0x51: esc = "\033[B"; break;
    case 0x52: esc = "\033[A"; break;
    case 0x4A: esc = "\033[H"; break;
    case 0x4D: esc = "\033[F"; break;
    case 0x49: esc = "\033[2~"; break;
    case 0x4C: esc = "\033[3~"; break;
    case 0x4B: esc = "\033[5~"; break;
    case 0x4E: esc = "\033[6~"; break;
    default: return false;
    }
    pd_hid_console_put(esc, (unsigned)strlen(esc));
    return true;
}

static void
pd_hid_console_work(thread_call_param_t, thread_call_param_t)
{
    char bytes[32];

    for (;;) {
        unsigned count = 0;
        bool leds;
        UInt8 state;

        IOLockLock(pd_hid_load(&gKbdLock));
        while (count < sizeof(bytes) && gConsReadIndex != gConsWriteIndex) {
            bytes[count++] = gConsBytes[gConsReadIndex % (UInt32)sizeof(gConsBytes)];
            gConsReadIndex++;
        }
        leds = gLEDsDirty;
        gLEDsDirty = false;
        state = gLockLEDs;
        IOLockUnlock(pd_hid_load(&gKbdLock));

        if (!count && !leds) break;

        // cons_cinput takes the tty mutex and echoes, so it runs here and never in a driver's path
        for (unsigned i = 0; i < count; i++) {
            cons_cinput(bytes[i]);
        }

        if (leds) {
            IOLockLock(pd_hid_load(&gLEDLock));
            for (unsigned i = 0; i < sizeof(gLEDHandlers) / sizeof(gLEDHandlers[0]); i++) {
                if (gLEDHandlers[i].handler) gLEDHandlers[i].handler(gLEDHandlers[i].target, state);
            }
            IOLockUnlock(pd_hid_load(&gLEDLock));
        }
    }
}

bool
PDHIDRegisterLEDHandler(PDHIDLEDHandler handler, void *target)
{
    bool ok = false;

    if (!handler || !pd_hid_kbd_setup()) return false;

    IOLockLock(pd_hid_load(&gLEDLock));
    for (unsigned i = 0; i < sizeof(gLEDHandlers) / sizeof(gLEDHandlers[0]); i++) {
        if (gLEDHandlers[i].handler) continue;
        gLEDHandlers[i].handler = handler;
        gLEDHandlers[i].target = target;
        ok = true;
        break;
    }
    IOLockUnlock(pd_hid_load(&gLEDLock));

    // a keyboard plugged in later picks up the current lock state
    if (ok) {
        IOLockLock(pd_hid_load(&gKbdLock));
        gLEDsDirty = true;
        IOLockUnlock(pd_hid_load(&gKbdLock));
        thread_call_enter(pd_hid_load(&gConsoleCall));
    }
    return ok;
}

void
PDHIDUnregisterLEDHandler(PDHIDLEDHandler handler, void *target)
{
    if (!pd_hid_load(&gLEDLock)) return;

    IOLockLock(pd_hid_load(&gLEDLock));
    for (unsigned i = 0; i < sizeof(gLEDHandlers) / sizeof(gLEDHandlers[0]); i++) {
        if (gLEDHandlers[i].handler != handler || gLEDHandlers[i].target != target) continue;
        gLEDHandlers[i].handler = NULL;
        gLEDHandlers[i].target = NULL;
    }
    IOLockUnlock(pd_hid_load(&gLEDLock));
}

UInt8
PDHIDKeyboardLEDState(void)
{
    return gLockLEDs;
}

void
PDHIDPushKeyboardEvent(UInt8 usage, bool down)
{
    PDHIDKbdEvent event;
    UInt32 slot;
    bool kick;

    if (!(OSBitOrAtomic(0, &gQueueReady) & kPublishKeyboard)) {
        __atomic_fetch_add(&gKbdEarlyDrops, 1U, __ATOMIC_RELAXED);
        return;
    }
    event.usage = usage;
    event.down = down ? 1 : 0;
    event.reserved[0] = event.reserved[1] = 0;

    IOLockLock(pd_hid_load(&gKbdLock));
    event.sequence = ++gKbdSequence;
    slot = gKbdWriteIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    gKbdEvents[slot] = event;
    gKbdWriteIndex++;
    if (gKbdWriteIndex - gKbdReadIndex > (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0])))
        gKbdReadIndex = gKbdWriteIndex - (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    kick = pd_hid_console_key(usage, down);
    IOLockUnlock(pd_hid_load(&gKbdLock));

    if (kick) thread_call_enter(pd_hid_load(&gConsoleCall));
}

static void
pd_hid_mouse_enqueue(PDHIDMouseEvent &event)
{
    UInt32 slot;

    if (!(OSBitOrAtomic(0, &gQueueReady) & kPublishMouse)) {
        __atomic_fetch_add(&gMouseEarlyDrops, 1U, __ATOMIC_RELAXED);
        return;
    }

    IOLockLock(pd_hid_load(&gMouseLock));
    event.sequence = ++gMouseSequence;
    slot = gMouseWriteIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    gMouseEvents[slot] = event;
    gMouseWriteIndex++;
    if (gMouseWriteIndex - gMouseReadIndex > (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0])))
        gMouseReadIndex = gMouseWriteIndex - (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    IOLockUnlock(pd_hid_load(&gMouseLock));
}

void
PDHIDPushMouseEvent(UInt8 mouseIndex, UInt8 buttons, SInt8 dx, SInt8 dy, SInt8 wheel)
{
    PDHIDMouseEvent event;

    bzero(&event, sizeof(event));
    event.mouseIndex = mouseIndex;
    event.buttons = buttons;
    event.dx = dx;
    event.dy = dy;
    event.wheel = wheel;
    pd_hid_mouse_enqueue(event);
}

void
PDHIDPushAbsoluteMouseEvent(UInt8 mouseIndex, UInt8 buttons, UInt16 x, UInt16 y, SInt8 wheel)
{
    PDHIDMouseEvent event;

    bzero(&event, sizeof(event));
    event.mouseIndex = mouseIndex;
    event.buttons = buttons;
    bcopy(&x, &event.dx, sizeof(x));
    event.wheel = wheel;
    event.flags = kPDHIDMouseAbsolute;
    event.absY = y;
    pd_hid_mouse_enqueue(event);
}
