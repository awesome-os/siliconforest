// SPDX-License-Identifier: MIT
// What Apple's Activity Monitor imports from CoreServices beyond what Terminal.app needed
// (docs/roadmap/issues/SF-AA-activity-monitor.md); built into the CoreServices wrapper with
// ../../terminal-app/compat/CoreServices.c.
//
// Launch Services' process registry, answered from the processes themselves: there is no Launch
// Services server here. A process is an application when its executable is the CFBundleExecutable
// of an application bundle (<name>.app/Contents/MacOS/<executable>, package type APPL); its
// information is that bundle's Info.plist with the registry's own keys (pid, bundle path, display
// name, application type, parent), as macOS's registry returns it. An ASN is a CFNumber
// (high part 1, low part the pid). Notifications are accepted and never sent (nothing announces
// launches here; Activity Monitor polls libsysmon anyway).
//
// Apple Events: descriptors are real (type and data); there is no Apple Event manager to deliver
// an event, so sending one answers procNotFound.
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <libproc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT const CFStringRef _kLSApplicationForegroundTypeKey = CFSTR("Foreground");
EXPORT const CFStringRef _kLSApplicationTypeKey = CFSTR("ApplicationType");
EXPORT const CFStringRef _kLSBundlePathKey = CFSTR("LSBundlePath");
EXPORT const CFStringRef _kLSDisplayNameKey = CFSTR("LSDisplayName");
EXPORT const CFStringRef _kLSPIDKey = CFSTR("pid");
EXPORT const CFStringRef _kLSParentASNKey = CFSTR("LSParentASN");

EXPORT CFTypeRef _LSASNCreateWithPid(CFAllocatorRef allocator, pid_t pid)
{
	int64_t v = (1LL << 32) | (uint32_t)pid;
	if (pid <= 0)
		return NULL;
	return CFNumberCreate(allocator, kCFNumberSInt64Type, &v);
}

static pid_t asn_pid(CFTypeRef asn)
{
	int64_t v = 0;
	if (!asn || CFGetTypeID(asn) != CFNumberGetTypeID() || !CFNumberGetValue(asn, kCFNumberSInt64Type, &v))
		return -1;
	return (pid_t)(uint32_t)v;
}

EXPORT void _LSASNExtractHighAndLowParts(CFTypeRef asn, uint32_t *high, uint32_t *low)
{
	pid_t pid = asn_pid(asn);
	if (high) *high = pid > 0 ? 1 : 0;
	if (low) *low = pid > 0 ? (uint32_t)pid : 0;
}

// the bundle whose executable this process runs, if it is an application's
static CFMutableDictionaryRef app_info(pid_t pid)
{
	char path[PROC_PIDPATHINFO_MAXSIZE], *macos;
	CFURLRef url;
	CFBundleRef bundle;
	CFDictionaryRef plist;
	CFMutableDictionaryRef d = NULL;

	if (proc_pidpath(pid, path, sizeof(path)) <= 0 || (macos = strstr(path, ".app/Contents/MacOS/")) == NULL)
		return NULL;
	const char *exe = macos + strlen(".app/Contents/MacOS/");
	macos[4] = '\0';   /* path is now the bundle */
	url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), true);
	if (!url)
		return NULL;
	bundle = CFBundleCreate(NULL, url);
	CFRelease(url);
	if (!bundle)
		return NULL;
	plist = CFBundleGetInfoDictionary(bundle);
	CFStringRef type = plist ? CFDictionaryGetValue(plist, CFSTR("CFBundlePackageType")) : NULL;
	CFStringRef executable = plist ? CFDictionaryGetValue(plist, CFSTR("CFBundleExecutable")) : NULL;
	CFStringRef exes = CFStringCreateWithCString(NULL, exe, kCFStringEncodingUTF8);
	if (plist && type && CFGetTypeID(type) == CFStringGetTypeID() && CFEqual(type, CFSTR("APPL")) &&
	    executable && exes && CFEqual(executable, exes)) {
		d = CFDictionaryCreateMutableCopy(NULL, 0, plist);
		CFStringRef bpath = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
		CFDictionarySetValue(d, _kLSBundlePathKey, bpath);
		CFStringRef name = CFDictionaryGetValue(plist, CFSTR("CFBundleDisplayName"));
		if (!name || CFGetTypeID(name) != CFStringGetTypeID())
			name = CFDictionaryGetValue(plist, CFSTR("CFBundleName"));
		CFStringRef own = NULL;
		if (!name || CFGetTypeID(name) != CFStringGetTypeID()) {
			const char *leaf = strrchr(path, '/');
			char base[256];
			snprintf(base, sizeof(base), "%s", leaf ? leaf + 1 : path);
			if (strlen(base) > 4) base[strlen(base) - 4] = '\0';
			name = own = CFStringCreateWithCString(NULL, base, kCFStringEncodingUTF8);
		}
		CFDictionarySetValue(d, _kLSDisplayNameKey, name);
		if (own) CFRelease(own);
		char full[PROC_PIDPATHINFO_MAXSIZE];
		snprintf(full, sizeof(full), "%s/Contents/MacOS/%s", path, exe);
		CFStringRef epath = CFStringCreateWithCString(NULL, full, kCFStringEncodingUTF8);
		CFDictionarySetValue(d, CFSTR("CFBundleExecutablePath"), epath);
		CFRelease(epath);
		CFRelease(bpath);
		CFBooleanRef ui = CFDictionaryGetValue(plist, CFSTR("LSUIElement"));
		CFBooleanRef bg = CFDictionaryGetValue(plist, CFSTR("LSBackgroundOnly"));
		CFStringRef apptype = _kLSApplicationForegroundTypeKey;
		if (bg && CFGetTypeID(bg) == CFBooleanGetTypeID() && CFBooleanGetValue(bg))
			apptype = CFSTR("BackgroundOnly");
		else if (ui && CFGetTypeID(ui) == CFBooleanGetTypeID() && CFBooleanGetValue(ui))
			apptype = CFSTR("UIElement");
		CFDictionarySetValue(d, _kLSApplicationTypeKey, apptype);
		int ipid = pid;
		CFNumberRef npid = CFNumberCreate(NULL, kCFNumberIntType, &ipid);
		CFDictionarySetValue(d, _kLSPIDKey, npid);
		CFRelease(npid);
		struct proc_bsdshortinfo si;
		if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &si, sizeof(si)) == sizeof(si) && si.pbsi_ppid > 0) {
			CFTypeRef parent = _LSASNCreateWithPid(NULL, (pid_t)si.pbsi_ppid);
			if (parent) {
				CFDictionarySetValue(d, _kLSParentASNKey, parent);
				CFRelease(parent);
			}
		}
	}
	if (exes) CFRelease(exes);
	CFRelease(bundle);
	return d;
}

EXPORT CFDictionaryRef _LSCopyApplicationInformation(int session, CFTypeRef asn, CFArrayRef keys)
{
	(void)session;
	pid_t pid = asn_pid(asn);
	CFMutableDictionaryRef all = pid > 0 ? app_info(pid) : NULL;
	if (!all || !keys)
		return all;
	CFMutableDictionaryRef some = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	for (CFIndex i = 0; i < CFArrayGetCount(keys); i++) {
		CFTypeRef k = CFArrayGetValueAtIndex(keys, i), v = CFDictionaryGetValue(all, k);
		if (v)
			CFDictionarySetValue(some, k, v);
	}
	CFRelease(all);
	return some;
}

EXPORT CFTypeRef _LSCopyApplicationInformationItem(int session, CFTypeRef asn, CFStringRef key)
{
	CFDictionaryRef d = _LSCopyApplicationInformation(session, asn, NULL);
	CFTypeRef v = d && key ? CFDictionaryGetValue(d, key) : NULL;
	if (v)
		CFRetain(v);
	if (d)
		CFRelease(d);
	return v;
}

static uint64_t next_notification = 1;

EXPORT uint64_t _LSScheduleNotificationFunctionOnQueue_f(int session, void *function, void *context, void *a, dispatch_queue_t queue)
{
	(void)session; (void)function; (void)context; (void)a; (void)queue;
	return __atomic_fetch_add(&next_notification, 1, __ATOMIC_RELAXED);
}

EXPORT int _LSModifyNotification(uint64_t id, int count, const uint32_t *codes, int b, void *c, void *d, void *e)
{
	(void)id; (void)count; (void)codes; (void)b; (void)c; (void)d; (void)e;
	return 0;
}

EXPORT int _LSUnscheduleNotificationFunction(uint64_t id) { (void)id; return 0; }

/* ---- Apple Event descriptors ---- */
typedef uint32_t DescType;
typedef int16_t OSErr;
typedef int32_t OSStatus;
struct sf_aedata { long size; unsigned char bytes[]; };
typedef struct { DescType descriptorType; struct sf_aedata *dataHandle; } AEDesc;
enum { sfNoErr = 0, sfMemFullErr = -108, sfProcNotFound = -600, sfErrAEDescNotFound = -1701, sfTypeNull = 0x6e756c6c /* 'null' */ };

EXPORT OSErr AECreateDesc(DescType type, const void *data, long size, AEDesc *result)
{
	if (!result)
		return -50;
	result->descriptorType = type;
	result->dataHandle = NULL;
	if (size > 0) {
		struct sf_aedata *h = malloc(sizeof(*h) + (size_t)size);
		if (!h)
			return sfMemFullErr;
		h->size = size;
		if (data) memcpy(h->bytes, data, (size_t)size); else memset(h->bytes, 0, (size_t)size);
		result->dataHandle = h;
	}
	return sfNoErr;
}

// an Apple Event holds its target's address; its attributes and parameters are not kept (nothing reads them here)
EXPORT OSErr AECreateAppleEvent(uint32_t eventClass, uint32_t eventID, const AEDesc *target, int16_t returnID, int32_t transactionID, AEDesc *result)
{
	(void)eventClass; (void)eventID; (void)returnID; (void)transactionID;
	return AECreateDesc(0x61657674 /* 'aevt' */, target && target->dataHandle ? target->dataHandle->bytes : NULL,
	    target && target->dataHandle ? target->dataHandle->size : 0, result);
}

EXPORT OSErr AEDisposeDesc(AEDesc *d)
{
	if (d) {
		free(d->dataHandle);
		d->dataHandle = NULL;
		d->descriptorType = sfTypeNull;
	}
	return sfNoErr;
}

EXPORT long AEGetDescDataSize(const AEDesc *d) { return d && d->dataHandle ? d->dataHandle->size : 0; }

EXPORT OSErr AEGetDescData(const AEDesc *d, void *data, long max)
{
	long n = AEGetDescDataSize(d);
	if (data && n > 0)
		memcpy(data, d->dataHandle->bytes, (size_t)(n < max ? n : max));
	return sfNoErr;
}

EXPORT OSErr AEGetAttributePtr(const AEDesc *e, uint32_t key, DescType want, DescType *type, void *data, long max, long *actual)
{
	(void)e; (void)key; (void)want; (void)data; (void)max;
	if (type) *type = sfTypeNull;
	if (actual) *actual = 0;
	return sfErrAEDescNotFound;
}

EXPORT OSErr AEGetParamDesc(const AEDesc *e, uint32_t key, DescType want, AEDesc *result)
{
	(void)e; (void)key; (void)want;
	if (result) { result->descriptorType = sfTypeNull; result->dataHandle = NULL; }
	return sfErrAEDescNotFound;
}

EXPORT OSErr AEInstallSpecialHandler(uint32_t functionClass, void *handler, unsigned char isSysHandler)
{
	(void)functionClass; (void)handler; (void)isSysHandler;
	return sfNoErr;
}

EXPORT OSStatus AESendMessage(const AEDesc *event, AEDesc *reply, int32_t mode, long timeout)
{
	(void)event; (void)mode; (void)timeout;
	if (reply) { reply->descriptorType = sfTypeNull; reply->dataHandle = NULL; }
	return sfProcNotFound;
}
