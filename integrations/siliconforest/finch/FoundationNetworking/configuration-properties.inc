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
