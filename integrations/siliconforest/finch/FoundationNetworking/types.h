/* SPDX-License-Identifier: MIT */
/* Own declarations from Finch implementations and public API facts, no SDK. */
#import <CFNetwork/CFNetwork.h>
#import <objc/runtime.h>
typedef NSString *NSHTTPCookiePropertyKey,*NSHTTPCookieStringPolicy;
typedef NSUInteger NSHTTPCookieAcceptPolicy,NSURLRequestAttribution,NSURLSessionMultipathServiceType;
typedef NSInteger NSURLSessionTaskMetricsResourceFetchType,NSURLSessionWebSocketMessageType,NSURLSessionWebSocketCloseCode;
#define NSHTTPCookieAcceptPolicyAlways 0
#define NSHTTPCookieAcceptPolicyNever 1
#define NSHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain 2
#define NSURLRequestAttributionDeveloper 0
#define NSURLSessionMultipathServiceTypeNone 0
#define NSURLSessionTaskMetricsResourceFetchTypeUnknown 0
#define NSURLSessionTaskMetricsResourceFetchTypeNetworkLoad 1
#define NSURLSessionTaskMetricsResourceFetchTypeLocalCache 3
#define NSURLSessionWebSocketMessageTypeData 0
#define NSURLSessionWebSocketMessageTypeString 1
#define NSURLSessionWebSocketCloseCodeInvalid 0
#define NSURLSessionWebSocketCloseCodeNormalClosure 1000
#define NSURLSessionWebSocketCloseCodeGoingAway 1001
#define NSURLSessionWebSocketCloseCodeProtocolError 1002
#define NSURLSessionWebSocketCloseCodeUnsupportedData 1003
#define NSURLSessionWebSocketCloseCodeNoStatusReceived 1005
#define NSURLSessionWebSocketCloseCodeAbnormalClosure 1006
#define NSURLSessionWebSocketCloseCodeInvalidFramePayloadData 1007
#define NSURLSessionWebSocketCloseCodePolicyViolation 1008
#define NSURLSessionWebSocketCloseCodeMessageTooBig 1009
#define NSURLSessionWebSocketCloseCodeMandatoryExtensionMissing 1010
#define NSURLSessionWebSocketCloseCodeInternalServerError 1011
#define NSURLSessionWebSocketCloseCodeTLSHandshakeFailure 1015
@protocol NSURLSessionDelegate <NSObject>
@optional
- (void)URLSession:(NSURLSession *)session didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,NSURLCredential *))completion;
@end
@protocol NSURLSessionTaskDelegate <NSURLSessionDelegate>
@optional
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))completion;
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,NSURLCredential *))completion;
@end
@protocol NSURLSessionDataDelegate <NSURLSessionTaskDelegate>
@end
@protocol NSURLSessionDownloadDelegate <NSURLSessionTaskDelegate>
@end
@protocol NSURLSessionStreamDelegate <NSURLSessionTaskDelegate>
@end
@protocol NSURLSessionWebSocketDelegate <NSURLSessionTaskDelegate>
@optional
- (void)URLSession:(id)session webSocketTask:(id)task didOpenWithProtocol:(NSString *)protocol;
- (void)URLSession:(id)session webSocketTask:(id)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData *)reason;
@end
@protocol NSURLProtocolClient <NSObject>
@end
@protocol NSURLAuthenticationChallengeSender <NSObject>
@end
#define SF_COOKIE_KEY(name) extern NSHTTPCookiePropertyKey const name
SF_COOKIE_KEY(NSHTTPCookieName);SF_COOKIE_KEY(NSHTTPCookieValue);SF_COOKIE_KEY(NSHTTPCookieOriginURL);SF_COOKIE_KEY(NSHTTPCookieVersion);SF_COOKIE_KEY(NSHTTPCookieDomain);SF_COOKIE_KEY(NSHTTPCookiePath);SF_COOKIE_KEY(NSHTTPCookieSecure);SF_COOKIE_KEY(NSHTTPCookieExpires);SF_COOKIE_KEY(NSHTTPCookieComment);SF_COOKIE_KEY(NSHTTPCookieCommentURL);SF_COOKIE_KEY(NSHTTPCookieDiscard);SF_COOKIE_KEY(NSHTTPCookieMaximumAge);SF_COOKIE_KEY(NSHTTPCookiePort);SF_COOKIE_KEY(NSHTTPCookieSameSitePolicy);
extern NSHTTPCookieStringPolicy const NSHTTPCookieSameSiteLax,NSHTTPCookieSameSiteStrict;
extern NSString *const NSHTTPCookieManagerCookiesChangedNotification,*const NSHTTPCookieManagerAcceptPolicyChangedNotification;
extern const float NSURLSessionTaskPriorityDefault,NSURLSessionTaskPriorityLow,NSURLSessionTaskPriorityHigh;
extern NSString *const NSURLSessionUploadTaskResumeData;

typedef NSInteger NSURLSessionTaskMetricsDomainResolutionProtocol;
#define NSURLSessionTaskMetricsDomainResolutionProtocolUnknown 0
#define NSURLErrorBadServerResponse (-1011)



#define NSURLErrorCannotWriteToFile (-3003)
#define NSURLErrorCannotCreateFile (-3000)

#define NSURLErrorResourceUnavailable (-1008)
#define NSKeyedArchiveRootObjectKey @"root"
@interface NSKeyedUnarchiver (SFNetworkCacheLegacyDeclaration)
- (void)setRequiresSecureCoding:(BOOL)secure;
@end

@interface NSDateFormatter (SFNetworkDateDeclarations)
@property(copy) NSTimeZone *timeZone;
@end
#ifndef NSURLErrorUnknown
#define NSURLErrorUnknown (-1)
#endif
