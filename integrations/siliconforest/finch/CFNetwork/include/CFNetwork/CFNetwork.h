/* SPDX-License-Identifier: MIT */
/* Own declarations from Finch implementations, plus PD's open proxy header. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFStream.h>
#include <CFNetwork/CFProxySupport.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct __CFHTTPMessage *CFHTTPMessageRef;
typedef struct _CFHTTPAuthentication *CFHTTPAuthenticationRef;
typedef struct __CFHost *CFHostRef;
typedef struct __CFNetService *CFNetServiceRef;
typedef struct __CFNetServiceBrowser *CFNetServiceBrowserRef;
typedef struct __CFNetServiceMonitor *CFNetServiceMonitorRef;
typedef struct __CFNetDiagnostic *CFNetDiagnosticRef;
typedef int CFHostInfoType, CFNetServiceMonitorType;
typedef CFIndex CFNetDiagnosticStatus;
typedef CFOptionFlags CFNetServiceRegisterFlags, CFNetServiceBrowserFlags;
typedef struct {
    CFIndex version;
    void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    CFStringRef (*copyDescription)(const void *);
} CFHostClientContext;
typedef CFHostClientContext CFNetServiceClientContext;
typedef void (*CFHostClientCallBack)(CFHostRef, CFHostInfoType, const CFStreamError *, void *);
typedef void (*CFNetServiceClientCallBack)(CFNetServiceRef, CFStreamError *, void *);
typedef void (*CFNetServiceBrowserClientCallBack)(CFNetServiceBrowserRef, CFOptionFlags, CFTypeRef, CFStreamError *, void *);
typedef void (*CFNetServiceMonitorClientCallBack)(CFNetServiceMonitorRef, CFNetServiceRef, CFNetServiceMonitorType, CFDataRef, CFStreamError *, void *);
enum { kCFHostErrorUnknown=2, kCFErrorHTTPBadURL=305, kCFErrorHTTPConnectionLost=302, kCFErrorHTTPParseFailure=303, kCFErrorHTTPRedirectionLoopDetected=304, kCFErrorPACFileError=308, kCFStreamErrorHTTPParseFailure=-1, kCFStreamErrorHTTPRedirectionLoop=-2, kCFStreamErrorHTTPBadURL=-3, kCFStreamErrorHTTPAuthenticationTypeUnsupported=-1000, kCFStreamErrorHTTPAuthenticationBadUserName=-1001, kCFStreamErrorHTTPAuthenticationBadPassword=-1002, kCFHostAddresses=0, kCFHostNames=1, kCFHostReachability=2,
       kCFNetServiceMonitorTXT=1, kCFNetServiceFlagNoAutoRename=1,
       kCFNetServiceFlagMoreComing=1, kCFNetServiceFlagIsDomain=2,
       kCFNetServiceFlagIsDefault=4, kCFNetServiceFlagRemove=8,
       kCFNetServicesErrorUnknown=-72000, kCFNetServicesErrorCollision=-72001,
       kCFNetServicesErrorNotFound=-72002, kCFNetServicesErrorInProgress=-72003,
       kCFNetServicesErrorBadArgument=-72004, kCFNetServicesErrorCancel=-72005,
       kCFNetServicesErrorInvalid=-72006, kCFNetServicesErrorTimeout=-72007,
       kCFNetDiagnosticNoErr=0, kCFNetDiagnosticErr=-66560,
       kCFNetDiagnosticConnectionUp=-66559, kCFNetDiagnosticConnectionIndeterminate=-66558,
       kCFNetDiagnosticConnectionDown=-66557 };
/* Public signatures and constants below are derived during the Nix build. */
#ifndef SF_CFNETWORK_PREPROCESS
#include <CFNetwork/Functions.h>
#endif
