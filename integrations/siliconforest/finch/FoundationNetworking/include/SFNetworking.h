/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class NSURLRequest, NSMutableURLRequest, NSURLResponse, NSHTTPURLResponse, NSHTTPCookie, NSHTTPCookieStorage, NSCachedURLResponse, NSURLCache, NSURLProtocol, NSURLSessionTaskTransactionMetrics, NSURLSessionTaskMetrics, NSURLSessionConfiguration, NSURLSessionTask, NSURLSessionDataTask, NSURLSessionUploadTask, NSURLSessionDownloadTask, NSURLSessionWebSocketMessage, NSURLSessionWebSocketTask, NSURLSessionStreamTask, NSURLCredential, NSURLProtectionSpace, NSURLAuthenticationChallenge, NSURLCredentialStorage, NSURLSession;
#define NSURLRequest SFNSURLRequest
#define NSMutableURLRequest SFNSMutableURLRequest
#define NSURLResponse SFNSURLResponse
#define NSHTTPURLResponse SFNSHTTPURLResponse
#define NSHTTPCookie SFNSHTTPCookie
#define NSHTTPCookieStorage SFNSHTTPCookieStorage
#define NSCachedURLResponse SFNSCachedURLResponse
#define NSURLCache SFNSURLCache
#define NSURLProtocol SFNSURLProtocol
#define NSURLSessionTaskTransactionMetrics SFNSURLSessionTaskTransactionMetrics
#define NSURLSessionTaskMetrics SFNSURLSessionTaskMetrics
#define NSURLSessionConfiguration SFNSURLSessionConfiguration
#define NSURLSessionTask SFNSURLSessionTask
#define NSURLSessionDataTask SFNSURLSessionDataTask
#define NSURLSessionUploadTask SFNSURLSessionUploadTask
#define NSURLSessionDownloadTask SFNSURLSessionDownloadTask
#define NSURLSessionWebSocketMessage SFNSURLSessionWebSocketMessage
#define NSURLSessionWebSocketTask SFNSURLSessionWebSocketTask
#define NSURLSessionStreamTask SFNSURLSessionStreamTask
#define NSURLCredential SFNSURLCredential
#define NSURLProtectionSpace SFNSURLProtectionSpace
#define NSURLAuthenticationChallenge SFNSURLAuthenticationChallenge
#define NSURLCredentialStorage SFNSURLCredentialStorage
#define NSURLSession SFNSURLSession
#define NSURLSessionDelegate SFNSURLSessionDelegate
#define NSURLSessionTaskDelegate SFNSURLSessionTaskDelegate
#define NSURLSessionDataDelegate SFNSURLSessionDataDelegate
#define NSURLSessionDownloadDelegate SFNSURLSessionDownloadDelegate
#define NSURLSessionWebSocketDelegate SFNSURLSessionWebSocketDelegate
#define NSURLSessionStreamDelegate SFNSURLSessionStreamDelegate
#define NSURLProtocolClient SFNSURLProtocolClient
#define NSURLAuthenticationChallengeSender SFNSURLAuthenticationChallengeSender
@class NSURLRequest,NSMutableURLRequest,NSURLResponse,NSHTTPURLResponse,NSHTTPCookie,NSHTTPCookieStorage,NSCachedURLResponse,NSURLCache,NSURLProtocol,NSURLSessionTaskTransactionMetrics,NSURLSessionTaskMetrics,NSURLSessionConfiguration,NSURLSessionTask,NSURLSessionDataTask,NSURLSessionUploadTask,NSURLSessionDownloadTask,NSURLSessionWebSocketMessage,NSURLSessionWebSocketTask,NSURLSessionStreamTask,NSURLCredential,NSURLProtectionSpace,NSURLAuthenticationChallenge,NSURLCredentialStorage,NSURLSession;
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
#define NSURLErrorUnknown (-1)
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

extern NSString *const NSURLProtectionSpaceHTTP;
extern NSString *const NSURLProtectionSpaceHTTPS;
extern NSString *const NSURLProtectionSpaceFTP;
extern NSString *const NSURLProtectionSpaceHTTPProxy;
extern NSString *const NSURLProtectionSpaceHTTPSProxy;
extern NSString *const NSURLProtectionSpaceFTPProxy;
extern NSString *const NSURLProtectionSpaceSOCKSProxy;
extern NSString *const NSURLAuthenticationMethodDefault;
extern NSString *const NSURLAuthenticationMethodHTTPBasic;
extern NSString *const NSURLAuthenticationMethodHTTPDigest;
extern NSString *const NSURLAuthenticationMethodHTMLForm;
extern NSString *const NSURLAuthenticationMethodNTLM;
extern NSString *const NSURLAuthenticationMethodNegotiate;
extern NSString *const NSURLAuthenticationMethodClientCertificate;
extern NSString *const NSURLAuthenticationMethodServerTrust;
extern NSString *const NSURLCredentialStorageChangedNotification;
extern NSString *const NSURLCredentialStorageRemoveSynchronizableCredentials;
extern NSString *const NSURLSessionDownloadTaskResumeData;
extern NSString *const NSURLSessionUploadTaskResumeData;

@interface NSURLRequest : NSObject
+ (BOOL)supportsSecureCoding;
+ (instancetype)requestWithURL:(NSURL *)URL;
+ (instancetype)requestWithURL:(NSURL *)URL cachePolicy:(NSURLRequestCachePolicy)cachePolicy timeoutInterval:(NSTimeInterval)timeout;
- (instancetype)init;
- (instancetype)initWithURL:(NSURL *)URL;
- (instancetype)initWithURL:(NSURL *)URL cachePolicy:(NSURLRequestCachePolicy)cachePolicy timeoutInterval:(NSTimeInterval)timeout;
- (void)dealloc;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (NSURL *)URL;
- (NSURLRequestCachePolicy)cachePolicy;
- (NSTimeInterval)timeoutInterval;
- (NSURL *)mainDocumentURL;
- (NSURLRequestNetworkServiceType)networkServiceType;
- (BOOL)allowsCellularAccess;
- (BOOL)allowsExpensiveNetworkAccess;
- (BOOL)allowsConstrainedNetworkAccess;
- (BOOL)assumesHTTP3Capable;
- (NSURLRequestAttribution)attribution;
- (BOOL)requiresDNSSECValidation;
- (BOOL)allowsPersistentDNS;
- (NSString *)cookiePartitionIdentifier;
- (NSString *)HTTPMethod;
- (NSDictionary<NSString *, NSString *> *)allHTTPHeaderFields;
- (NSData *)HTTPBody;
- (NSInputStream *)HTTPBodyStream;
- (BOOL)HTTPShouldHandleCookies;
- (BOOL)HTTPShouldUsePipelining;
- (NSString *)valueForHTTPHeaderField:(NSString *)field;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface NSMutableURLRequest : NSURLRequest
- (void)setURL:(NSURL *)URL;
- (void)setCachePolicy:(NSURLRequestCachePolicy)p;
- (void)setTimeoutInterval:(NSTimeInterval)t;
- (void)setMainDocumentURL:(NSURL *)u;
- (void)setNetworkServiceType:(NSURLRequestNetworkServiceType)t;
- (void)setAllowsCellularAccess:(BOOL)f;
- (void)setAllowsExpensiveNetworkAccess:(BOOL)f;
- (void)setAllowsConstrainedNetworkAccess:(BOOL)f;
- (void)setAssumesHTTP3Capable:(BOOL)f;
- (void)setAttribution:(NSURLRequestAttribution)a;
- (void)setRequiresDNSSECValidation:(BOOL)f;
- (void)setAllowsPersistentDNS:(BOOL)f;
- (void)setCookiePartitionIdentifier:(NSString *)s;
- (void)setHTTPMethod:(NSString *)m;
- (void)setAllHTTPHeaderFields:(NSDictionary<NSString *, NSString *> *)fields;
- (void)setValue:(NSString *)value forHTTPHeaderField:(NSString *)field;
- (void)addValue:(NSString *)value forHTTPHeaderField:(NSString *)field;
- (void)setHTTPBody:(NSData *)body;
- (void)setHTTPBodyStream:(NSInputStream *)s;
- (void)setHTTPShouldHandleCookies:(BOOL)f;
- (void)setHTTPShouldUsePipelining:(BOOL)f;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface NSURLResponse : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithURL:(NSURL *)URL MIMEType:(NSString *)MIMEType expectedContentLength:(NSInteger)length textEncodingName:(NSString *)name;
- (void)dealloc;
- (id)copyWithZone:(NSZone *)zone;
- (NSURL *)URL;
- (NSString *)MIMEType;
- (long long)expectedContentLength;
- (NSString *)textEncodingName;
- (NSString *)suggestedFilename;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface NSHTTPURLResponse : NSURLResponse
- (instancetype)initWithURL:(NSURL *)url statusCode:(NSInteger)statusCode HTTPVersion:(NSString *)version headerFields:(NSDictionary<NSString *, NSString *> *)fields;
- (void)dealloc;
- (NSInteger)statusCode;
- (NSString *)suggestedFilename;
- (NSDictionary *)allHeaderFields;
- (NSString *)valueForHTTPHeaderField:(NSString *)field;
+ (NSString *)localizedStringForStatusCode:(NSInteger)code;
@end

@interface NSHTTPCookie : NSObject
+ (instancetype)cookieWithProperties:(NSDictionary<NSHTTPCookiePropertyKey, id> *)properties;
- (instancetype)initWithProperties:(NSDictionary<NSHTTPCookiePropertyKey, id> *)properties;
- (void)dealloc;
- (NSDictionary *)properties;
- (NSUInteger)version;
- (NSString *)name;
- (NSString *)value;
- (NSDate *)expiresDate;
- (BOOL)isSessionOnly;
- (NSString *)domain;
- (NSString *)path;
- (BOOL)isSecure;
- (BOOL)isHTTPOnly;
- (NSString *)comment;
- (NSURL *)commentURL;
- (NSArray<NSNumber *> *)portList;
- (NSHTTPCookieStringPolicy)sameSitePolicy;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
+ (NSArray<NSHTTPCookie *> *)cookiesWithResponseHeaderFields:(NSDictionary<NSString *, NSString *> *)headerFields forURL:(NSURL *)URL;
+ (NSDictionary<NSString *, NSString *> *)requestHeaderFieldsWithCookies:(NSArray<NSHTTPCookie *> *)cookies;
@property(readonly,getter=isSessionOnly) BOOL sessionOnly;
@property(readonly,getter=isSecure) BOOL secure;
@property(readonly,getter=isHTTPOnly) BOOL HTTPOnly;
@end

@interface NSHTTPCookieStorage : NSObject
+ (NSHTTPCookieStorage *)sharedHTTPCookieStorage;
+ (NSHTTPCookieStorage *)sharedCookieStorageForGroupContainerIdentifier:(NSString *)identifier;
- (instancetype)init;
- (void)dealloc;
- (NSArray<NSHTTPCookie *> *)cookies;
- (NSHTTPCookieAcceptPolicy)cookieAcceptPolicy;
- (void)setCookieAcceptPolicy:(NSHTTPCookieAcceptPolicy)p;
- (void)setCookie:(NSHTTPCookie *)cookie;
- (void)deleteCookie:(NSHTTPCookie *)cookie;
- (void)removeCookiesSinceDate:(NSDate *)date;
- (NSArray<NSHTTPCookie *> *)cookiesForURL:(NSURL *)URL;
- (void)setCookies:(NSArray<NSHTTPCookie *> *)cookies forURL:(NSURL *)URL mainDocumentURL:(NSURL *)mainDocumentURL;
- (NSArray<NSHTTPCookie *> *)sortedCookiesUsingDescriptors:(NSArray<NSSortDescriptor *> *)sortOrder;
- (void)storeCookies:(NSArray<NSHTTPCookie *> *)cookies forTask:(NSURLSessionTask *)task;
- (void)getCookiesForTask:(NSURLSessionTask *)task completionHandler:(void (^)(NSArray<NSHTTPCookie *> *))completionHandler;
@end

@interface NSCachedURLResponse : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithResponse:(NSURLResponse *)response data:(NSData *)data;
- (instancetype)initWithResponse:(NSURLResponse *)response data:(NSData *)data userInfo:(NSDictionary *)userInfo storagePolicy:(NSURLCacheStoragePolicy)storagePolicy;
- (void)dealloc;
- (NSURLResponse *)response;
- (NSData *)data;
- (NSDictionary *)userInfo;
- (NSURLCacheStoragePolicy)storagePolicy;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface NSURLCache : NSObject
+ (NSURLCache *)sharedURLCache;
+ (void)setSharedURLCache:(NSURLCache *)cache;
- (instancetype)init;
- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity diskCapacity:(NSUInteger)diskCapacity diskPath:(NSString *)path;
- (instancetype)initWithMemoryCapacity:(NSUInteger)memoryCapacity diskCapacity:(NSUInteger)diskCapacity directoryURL:(NSURL *)directoryURL;
- (void)dealloc;
- (NSUInteger)memoryCapacity;
- (void)setMemoryCapacity:(NSUInteger)c;
- (NSUInteger)diskCapacity;
- (void)setDiskCapacity:(NSUInteger)c;
- (NSUInteger)currentMemoryUsage;
- (NSUInteger)currentDiskUsage;
- (NSCachedURLResponse *)cachedResponseForRequest:(NSURLRequest *)request;
- (void)storeCachedResponse:(NSCachedURLResponse *)cachedResponse forRequest:(NSURLRequest *)request;
- (void)removeCachedResponseForRequest:(NSURLRequest *)request;
- (void)removeAllCachedResponses;
- (void)removeCachedResponsesSinceDate:(NSDate *)date;
- (void)storeCachedResponse:(NSCachedURLResponse *)cachedResponse forDataTask:(NSURLSessionDataTask *)dataTask;
- (void)getCachedResponseForDataTask:(NSURLSessionDataTask *)dataTask completionHandler:(void (^)(NSCachedURLResponse *))completionHandler;
- (void)removeCachedResponseForDataTask:(NSURLSessionDataTask *)dataTask;
@end

@interface NSURLProtocol : NSObject
+ (BOOL)registerClass:(Class)protocolClass;
+ (void)unregisterClass:(Class)protocolClass;
+ (BOOL)canInitWithRequest:(NSURLRequest *)request;
+ (BOOL)canInitWithTask:(NSURLSessionTask *)task;
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request;
+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b;
+ (id)propertyForKey:(NSString *)key inRequest:(NSURLRequest *)request;
+ (void)setProperty:(id)value forKey:(NSString *)key inRequest:(NSMutableURLRequest *)request;
+ (void)removePropertyForKey:(NSString *)key inRequest:(NSMutableURLRequest *)request;
- (instancetype)initWithRequest:(NSURLRequest *)request cachedResponse:(NSCachedURLResponse *)cachedResponse client:(id<NSURLProtocolClient>)client;
- (instancetype)initWithTask:(NSURLSessionTask *)task cachedResponse:(NSCachedURLResponse *)cachedResponse client:(id<NSURLProtocolClient>)client;
- (void)dealloc;
- (NSURLRequest *)request;
- (NSCachedURLResponse *)cachedResponse;
- (id<NSURLProtocolClient>)client;
- (NSURLSessionTask *)task;
- (void)startLoading;
- (void)stopLoading;
@end

@interface NSURLSessionTaskTransactionMetrics : NSObject
- (void)dealloc;
- (NSURLRequest *)request;
- (NSURLResponse *)response;
- (NSDate *)fetchStartDate;
- (NSDate *)domainLookupStartDate;
- (NSDate *)domainLookupEndDate;
- (NSDate *)connectStartDate;
- (NSDate *)connectEndDate;
- (NSDate *)secureConnectionStartDate;
- (NSDate *)secureConnectionEndDate;
- (NSDate *)requestStartDate;
- (NSDate *)requestEndDate;
- (NSDate *)responseStartDate;
- (NSDate *)responseEndDate;
- (NSString *)networkProtocolName;
- (BOOL)isProxyConnection;
- (BOOL)isReusedConnection;
- (NSURLSessionTaskMetricsResourceFetchType)resourceFetchType;
- (int64_t)countOfRequestHeaderBytesSent;
- (int64_t)countOfRequestBodyBytesSent;
- (int64_t)countOfRequestBodyBytesBeforeEncoding;
- (int64_t)countOfResponseHeaderBytesReceived;
- (int64_t)countOfResponseBodyBytesReceived;
- (int64_t)countOfResponseBodyBytesAfterDecoding;
- (NSString *)localAddress;
- (NSNumber *)localPort;
- (NSString *)remoteAddress;
- (NSNumber *)remotePort;
- (NSNumber *)negotiatedTLSProtocolVersion;
- (NSNumber *)negotiatedTLSCipherSuite;
- (BOOL)isCellular;
- (BOOL)isExpensive;
- (BOOL)isConstrained;
- (BOOL)isMultipath;
- (NSURLSessionTaskMetricsDomainResolutionProtocol)domainResolutionProtocol;
@property(readonly,getter=isProxyConnection) BOOL proxyConnection;
@property(readonly,getter=isReusedConnection) BOOL reusedConnection;
@property(readonly,getter=isCellular) BOOL cellular;
@property(readonly,getter=isExpensive) BOOL expensive;
@property(readonly,getter=isConstrained) BOOL constrained;
@property(readonly,getter=isMultipath) BOOL multipath;
@end

@interface NSURLSessionTaskMetrics : NSObject
- (void)dealloc;
- (NSArray *)transactionMetrics;
- (NSDateInterval *)taskInterval;
- (NSUInteger)redirectCount;
@end

@interface NSURLSessionConfiguration : NSObject
+ (NSURLSessionConfiguration *)defaultSessionConfiguration;
+ (NSURLSessionConfiguration *)ephemeralSessionConfiguration;
+ (NSURLSessionConfiguration *)backgroundSessionConfigurationWithIdentifier:(NSString *)identifier;
- (void)dealloc;
- (NSString *)identifier;
- (id)copyWithZone:(NSZone *)zone;
/* Own storage for properties synthesized by the Finch implementation. */
@property NSTimeInterval timeoutIntervalForRequest,timeoutIntervalForResource;
@property BOOL allowsCellularAccess,allowsExpensiveNetworkAccess,allowsConstrainedNetworkAccess,waitsForConnectivity,HTTPShouldUsePipelining,HTTPShouldSetCookies;
@property NSInteger HTTPMaximumConnectionsPerHost;
@property NSURLRequestCachePolicy requestCachePolicy;
@property NSHTTPCookieAcceptPolicy HTTPCookieAcceptPolicy;
@property NSURLRequestNetworkServiceType networkServiceType;
@property(copy) NSDictionary *HTTPAdditionalHeaders;
@property(copy) NSArray<Class> *protocolClasses;
@property(retain) NSURLCache *URLCache;
@property(retain) NSURLCredentialStorage *URLCredentialStorage;
@property(retain) NSHTTPCookieStorage *HTTPCookieStorage;

@end

@interface NSURLSessionTask : NSObject
- (void)dealloc;
- (NSUInteger)taskIdentifier;
- (NSURLRequest *)originalRequest;
- (NSURLRequest *)currentRequest;
- (NSURLResponse *)response;
- (NSError *)error;
- (NSURLSessionTaskState)state;
- (int64_t)countOfBytesReceived;
- (int64_t)countOfBytesSent;
- (int64_t)countOfBytesExpectedToSend;
- (int64_t)countOfBytesExpectedToReceive;
- (NSString *)taskDescription;
- (void)setTaskDescription:(NSString *)d;
- (float)priority;
- (void)setPriority:(float)p;
- (NSProgress *)progress;
- (void)resume;
- (void)cancel;
- (void)suspend;
@end

@interface NSURLSessionDataTask : NSURLSessionTask

@end

@interface NSURLSessionUploadTask : NSURLSessionDataTask
- (void)cancelByProducingResumeData:(void (^)(NSData *))completionHandler;
@end

@interface NSURLSessionDownloadTask : NSURLSessionTask
- (void)cancelByProducingResumeData:(void (^)(NSData *))handler;
@end

@interface NSURLSessionWebSocketMessage : NSObject
- (instancetype)initWithData:(NSData *)data;
- (instancetype)initWithString:(NSString *)string;
- (void)dealloc;
- (NSURLSessionWebSocketMessageType)type;
- (NSData *)data;
- (NSString *)string;
@end

@interface NSURLSessionWebSocketTask : NSURLSessionTask
- (void)dealloc;
- (NSInteger)maximumMessageSize;
- (void)setMaximumMessageSize:(NSInteger)size;
- (NSURLSessionWebSocketCloseCode)closeCode;
- (NSData *)closeReason;
- (void)resume;
- (void)sendMessage:(NSURLSessionWebSocketMessage *)message completionHandler:(void (^)(NSError *))completionHandler;
- (void)receiveMessageWithCompletionHandler:(void (^)(NSURLSessionWebSocketMessage *, NSError *))completionHandler;
- (void)sendPingWithPongReceiveHandler:(void (^)(NSError *))pongReceiveHandler;
- (void)cancelWithCloseCode:(NSURLSessionWebSocketCloseCode)closeCode reason:(NSData *)reason;
- (void)cancel;
@end

@interface NSURLSessionStreamTask : NSURLSessionTask
- (void)dealloc;
- (void)resume;
- (void)readDataOfMinLength:(NSUInteger)minBytes maxLength:(NSUInteger)maxBytes timeout:(NSTimeInterval)timeout completionHandler:(void (^)(NSData *, BOOL, NSError *))completionHandler;
- (void)writeData:(NSData *)data timeout:(NSTimeInterval)timeout completionHandler:(void (^)(NSError *))completionHandler;
- (void)closeWrite;
- (void)closeRead;
- (void)startSecureConnection;
- (void)stopSecureConnection;
- (void)captureStreams;
- (void)cancel;
@end

@interface NSURLCredential : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithUser:(NSString *)user password:(NSString *)password persistence:(NSURLCredentialPersistence)persistence;
+ (NSURLCredential *)credentialWithUser:(NSString *)user password:(NSString *)password persistence:(NSURLCredentialPersistence)persistence;
- (instancetype)initWithIdentity:(SecIdentityRef)identity certificates:(NSArray *)certArray persistence:(NSURLCredentialPersistence)persistence;
+ (NSURLCredential *)credentialWithIdentity:(SecIdentityRef)identity certificates:(NSArray *)certArray persistence:(NSURLCredentialPersistence)persistence;
- (instancetype)initWithTrust:(SecTrustRef)trust;
+ (NSURLCredential *)credentialForTrust:(SecTrustRef)trust;
- (void)dealloc;
- (NSString *)user;
- (NSString *)password;
- (BOOL)hasPassword;
- (NSURLCredentialPersistence)persistence;
- (SecIdentityRef)identity;
- (NSArray *)certificates;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface NSURLProtectionSpace : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithHost:(NSString *)host port:(NSInteger)port protocol:(NSString *)protocol realm:(NSString *)realm authenticationMethod:(NSString *)authenticationMethod;
- (instancetype)initWithProxyHost:(NSString *)host port:(NSInteger)port type:(NSString *)type realm:(NSString *)realm authenticationMethod:(NSString *)authenticationMethod;
- (void)dealloc;
- (NSString *)host;
- (NSInteger)port;
- (NSString *)protocol;
- (NSString *)realm;
- (NSString *)authenticationMethod;
- (NSString *)proxyType;
- (BOOL)isProxy;
- (NSArray<NSData *> *)distinguishedNames;
- (SecTrustRef)serverTrust;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)receivesCredentialSecurely;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@property(readonly,getter=isProxy) BOOL proxy;
@end

@interface NSURLAuthenticationChallenge : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithProtectionSpace:(NSURLProtectionSpace *)space proposedCredential:(NSURLCredential *)credential previousFailureCount:(NSInteger)previousFailureCount failureResponse:(NSURLResponse *)response error:(NSError *)error sender:(id<NSURLAuthenticationChallengeSender>)sender;
- (instancetype)initWithAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge sender:(id<NSURLAuthenticationChallengeSender>)sender;
- (void)dealloc;
- (NSURLProtectionSpace *)protectionSpace;
- (NSURLCredential *)proposedCredential;
- (NSInteger)previousFailureCount;
- (NSURLResponse *)failureResponse;
- (NSError *)error;
- (id<NSURLAuthenticationChallengeSender>)sender;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface NSURLCredentialStorage : NSObject
+ (NSURLCredentialStorage *)sharedCredentialStorage;
- (instancetype)init;
- (void)dealloc;
- (NSDictionary<NSString *, NSURLCredential *> *)credentialsForProtectionSpace:(NSURLProtectionSpace *)space;
- (NSDictionary<NSURLProtectionSpace *, NSDictionary<NSString *, NSURLCredential *> *> *)allCredentials;
- (void)setCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)space;
- (void)removeCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)space;
- (void)removeCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)space options:(NSDictionary<NSString *, id> *)options;
- (NSURLCredential *)defaultCredentialForProtectionSpace:(NSURLProtectionSpace *)space;
- (void)setDefaultCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)space;
- (void)getCredentialsForProtectionSpace:(NSURLProtectionSpace *)protectionSpace task:(NSURLSessionTask *)task completionHandler:(void (^)(NSDictionary<NSString *, NSURLCredential *> *))completionHandler;
- (void)setCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)protectionSpace task:(NSURLSessionTask *)task;
- (void)removeCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)protectionSpace options:(NSDictionary<NSString *, id> *)options task:(NSURLSessionTask *)task;
- (void)getDefaultCredentialForProtectionSpace:(NSURLProtectionSpace *)space task:(NSURLSessionTask *)task completionHandler:(void (^)(NSURLCredential *))completionHandler;
- (void)setDefaultCredential:(NSURLCredential *)credential forProtectionSpace:(NSURLProtectionSpace *)protectionSpace task:(NSURLSessionTask *)task;
@end

@interface NSURLSession : NSObject
+ (NSURLSession *)sharedSession;
+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration;
+ (NSURLSession *)sessionWithConfiguration:(NSURLSessionConfiguration *)configuration delegate:(id<NSURLSessionDelegate>)delegate delegateQueue:(NSOperationQueue *)queue;
- (void)dealloc;
- (NSURLSessionConfiguration *)configuration;
- (id<NSURLSessionDelegate>)delegate;
- (NSOperationQueue *)delegateQueue;
- (NSString *)sessionDescription;
- (void)setSessionDescription:(NSString *)d;
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url;
- (NSURLSessionDataTask *)dataTaskWithRequest:(NSURLRequest *)request completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionDataTask *)dataTaskWithURL:(NSURL *)url completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionDownloadTask *)downloadTaskWithURL:(NSURL *)url;
- (NSURLSessionDownloadTask *)downloadTaskWithRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionDownloadTask *)downloadTaskWithURL:(NSURL *)url completionHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionDownloadTask *)downloadTaskWithResumeData:(NSData *)resumeData completionHandler:(void (^)(NSURL *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionDownloadTask *)downloadTaskWithResumeData:(NSData *)resumeData;
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromData:(NSData *)bodyData completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromData:(NSData *)bodyData;
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromFile:(NSURL *)fileURL completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionUploadTask *)uploadTaskWithRequest:(NSURLRequest *)request fromFile:(NSURL *)fileURL;
- (NSURLSessionUploadTask *)uploadTaskWithStreamedRequest:(NSURLRequest *)request;
- (NSURLSessionUploadTask *)uploadTaskWithResumeData:(NSData *)resumeData completionHandler:(void (^)(NSData *, NSURLResponse *, NSError *))completionHandler;
- (NSURLSessionUploadTask *)uploadTaskWithResumeData:(NSData *)resumeData;
- (void)finishTasksAndInvalidate;
- (void)invalidateAndCancel;
- (void)getTasksWithCompletionHandler:(void (^)(NSArray<NSURLSessionDataTask *> *, NSArray<NSURLSessionUploadTask *> *, NSArray<NSURLSessionDownloadTask *> *))completionHandler;
- (void)resetWithCompletionHandler:(void (^)(void))completionHandler;
- (void)flushWithCompletionHandler:(void (^)(void))completionHandler;
- (void)getAllTasksWithCompletionHandler:(void (^)(NSArray<__kindof NSURLSessionTask *> *))completionHandler;
- (NSURLSessionWebSocketTask *)webSocketTaskWithURL:(NSURL *)url;
- (NSURLSessionWebSocketTask *)webSocketTaskWithURL:(NSURL *)url protocols:(NSArray<NSString *> *)protocols;
- (NSURLSessionWebSocketTask *)webSocketTaskWithRequest:(NSURLRequest *)request;
- (NSURLSessionStreamTask *)streamTaskWithHostName:(NSString *)hostname port:(NSInteger)port;
- (NSURLSessionStreamTask *)streamTaskWithNetService:(NSNetService *)service;
@end
