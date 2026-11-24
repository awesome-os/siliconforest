// SPDX-License-Identifier: MIT
// Activity Monitor's privileged helper, in process (docs/roadmap/issues/SF-AA-activity-monitor.md).
//
// On macOS, Activity Monitor asks its helper service com.apple.activitymonitor.helper (an XPC
// service running as root, behind an authorization) to send a signal, sample a process, run spindump
// or sysdiagnose, and list a process's open files (its protocol SMHelperProtocol; the method names and
// argument types are what the application's binary declares). SiliconForest has no such service and
// libFoundation's NSXPCConnection answers every call to an unreachable service with an error and no
// reply. This library, which only Activity Monitor loads (it is libquit), gives the connection to that
// one service an object in the process instead:
//   sendSignal:toProcessID:taskID:result:   kill(2), the reply says what it answered (the desktop's
//                                            user is root here, so no authorization is asked)
//   sampleProcessWithPID:..., spindumpWithProcessID:..., sysdiagnoseWithProcessID:...,
//   listOpenFilesWithProcessID:...          reply with an error that says the tool is not on this
//                                            system (there is no sample, spindump, sysdiagnose or lsof)
//   authenticate:prompt:, deauthenticate     nothing to do
// Any other message is dropped without a reply, as the unreachable service's were.
#import <Foundation/Foundation.h>
#include <errno.h>
#include <objc/runtime.h>
#include <signal.h>
#include <string.h>

static NSString *const SFHelperService = @"com.apple.activitymonitor.helper";

static NSError *sfHelperError(int code, NSString *what)
{
	return [NSError errorWithDomain:NSPOSIXErrorDomain code:code
	                       userInfo:[NSDictionary dictionaryWithObject:what forKey:NSLocalizedDescriptionKey]];
}

static NSError *sfNotHere(NSString *tool)
{
	return sfHelperError(ENOTSUP, [NSString stringWithFormat:@"%@ is not available on this system.", tool]);
}

@interface SFActivityMonitorHelper : NSObject
@end

@implementation SFActivityMonitorHelper

- (void)sendSignal:(int)sig toProcessID:(int)pid taskID:(NSUUID *)task result:(void (^)(NSError *))result
{
	int rc = kill(pid, sig), e = errno;

	if (result)
		result(rc == 0 ? nil : sfHelperError(e, [NSString stringWithFormat:@"kill(%d, %d): %s", pid, sig, strerror(e)]));
}

- (void)sampleProcessWithPID:(int)pid forSeconds:(int)seconds taskID:(NSUUID *)task result:(void (^)(NSData *, NSError *))result
{
	if (result)
		result(nil, sfNotHere(@"sample"));
}

- (void)spindumpWithProcessID:(int)pid taskID:(NSUUID *)task result:(void (^)(NSData *, NSError *))result
{
	if (result)
		result(nil, sfNotHere(@"spindump"));
}

- (void)sysdiagnoseWithProcessID:(int)pid taskID:(NSUUID *)task result:(void (^)(NSError *))result
{
	if (result)
		result(sfNotHere(@"sysdiagnose"));
}

- (void)listOpenFilesWithProcessID:(int)pid taskID:(NSUUID *)task result:(void (^)(NSData *, NSError *))result
{
	if (result)
		result(nil, sfNotHere(@"lsof"));
}

- (void)authenticate:(id)authorization prompt:(id)prompt { }
- (void)deauthenticate { }

// anything else: no reply, as the unreachable service gave none
- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel
{
	NSMethodSignature *s = [super methodSignatureForSelector:sel];

	return s ? s : [NSMethodSignature signatureWithObjCTypes:"v@:"];
}

- (void)forwardInvocation:(NSInvocation *)invocation { }

@end

static id (*sfOriginalProxy)(id, SEL, id);

static id sfProxyWithErrorHandler(id self, SEL _cmd, id handler)
{
	static SFActivityMonitorHelper *helper;

	if ([[self serviceName] isEqualToString:SFHelperService]) {
		if (helper == nil)
			helper = [[SFActivityMonitorHelper alloc] init];
		return helper;
	}
	return sfOriginalProxy(self, _cmd, handler);
}

__attribute__((constructor)) static void sfInstallHelper(void)
{
	Class c = objc_getClass("NSXPCConnection");
	Method m = c ? class_getInstanceMethod(c, @selector(remoteObjectProxyWithErrorHandler:)) : NULL;

	if (m != NULL && sfOriginalProxy == NULL)
		sfOriginalProxy = (id (*)(id, SEL, id))method_setImplementation(m, (IMP)sfProxyWithErrorHandler);
}
