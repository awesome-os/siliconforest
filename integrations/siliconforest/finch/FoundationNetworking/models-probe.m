/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
@interface NSObject (SFNetworkModelDeclarations)
+ (id)cookieWithProperties:(NSDictionary *)properties;
+ (NSArray *)cookiesWithResponseHeaderFields:(NSDictionary *)fields forURL:(NSURL *)url;
+ (NSDictionary *)requestHeaderFieldsWithCookies:(NSArray *)cookies;
- (NSString *)name;- (NSString *)value;- (NSString *)domain;- (NSString *)path;
- (BOOL)isSecure;- (BOOL)isHTTPOnly;- (BOOL)isSessionOnly;- (NSDate *)expiresDate;
- (id)initWithMemoryCapacity:(NSUInteger)memory diskCapacity:(NSUInteger)disk directoryURL:(NSURL *)url;
- (id)initWithResponse:(id)response data:(NSData *)data userInfo:(NSDictionary *)info storagePolicy:(NSUInteger)policy;
- (void)storeCachedResponse:(id)response forRequest:(id)request;
- (id)cachedResponseForRequest:(id)request;
- (void)removeAllCachedResponses;- (NSUInteger)currentMemoryUsage;- (NSData *)data;
@end
static void need(BOOL value,const char *message){if(!value){fprintf(stderr,"MODEL_FAIL %s\n",message);exit(1);}}
static Class klass(BOOL native,const char *name){char text[96];snprintf(text,sizeof(text),"%s%s",native?"":"SF",name);Class c=objc_getClass(text);need(c!=Nil,"model class");return c;}
int main(int argc,char **argv){@autoreleasepool{
 BOOL native=argc==2&&!strcmp(argv[1],"--native");
 if(!native){need(argc==3&&!strcmp(argv[1],"--prefix"),"mode");void *lib=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);if(!lib)fprintf(stderr,"MODEL_LOAD %s\n",dlerror());need(lib!=NULL,"load prefix");}
 Class cookies=klass(native,"NSHTTPCookie");NSURL *url=[NSURL URLWithString:@"https://example.com/path"];
 NSArray *parsed=[cookies cookiesWithResponseHeaderFields:@{@"Set-Cookie":@"session=xyz; Path=/; Secure; HttpOnly; Max-Age=600"} forURL:url];
 need(parsed.count==1,"one parsed cookie");id cookie=[parsed objectAtIndex:0];
 BOOL attributes=[[cookie name] isEqual:@"session"]&&[[cookie value] isEqual:@"xyz"]&&[[cookie domain] isEqual:@"example.com"]&&[[cookie path] isEqual:@"/"];
 BOOL flags=[cookie isSecure]&&[cookie isHTTPOnly]&&![cookie isSessionOnly];
 BOOL expiry=[[cookie expiresDate] timeIntervalSinceNow]>300&&[[cookie expiresDate] timeIntervalSinceNow]<700;
 BOOL header=[[[cookies requestHeaderFieldsWithCookies:parsed] objectForKey:@"Cookie"] isEqual:@"session=xyz"];
 printf("COOKIE parsed=%lu attributes=%d flags=%d expiry=%d request-header=%d\n",(unsigned long)parsed.count,attributes,flags,expiry,header);
 need(attributes&&flags&&expiry&&header,"cookie semantics");
 NSArray *dated=[cookies cookiesWithResponseHeaderFields:@{@"Set-Cookie":@"dated=x; Expires=Wed, 21 Oct 2037 07:28:00 GMT; Path=/"} forURL:url];
 need(dated.count==1,"HTTP date cookie");BOOL bounded=[[[dated objectAtIndex:0] expiresDate] timeIntervalSinceNow]>86400&&[[[dated objectAtIndex:0] expiresDate] timeIntervalSinceNow]<401*86400.;
 printf("COOKIE http-date-future-and-capped=%d\n",bounded);need(bounded,"HTTP GMT date and lifetime cap");
 Class requestClass=klass(native,"NSURLRequest"),responseClass=klass(native,"NSHTTPURLResponse"),cachedClass=klass(native,"NSCachedURLResponse"),cacheClass=klass(native,"NSURLCache");
 id request=[requestClass requestWithURL:url];id response=[[responseClass alloc] initWithURL:url statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:@{@"Content-Type":@"text/plain",@"Cache-Control":@"max-age=600"}];
 NSData *data=[@"hello" dataUsingEncoding:NSUTF8StringEncoding];id stored=[[cachedClass alloc] initWithResponse:response data:data userInfo:nil storagePolicy:1];
 NSString *path=[[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:@"model-private-cache"];
 id cache=[[cacheClass alloc] initWithMemoryCapacity:1048576 diskCapacity:0 directoryURL:[NSURL fileURLWithPath:path]];
 [cache storeCachedResponse:stored forRequest:request];id got=[cache cachedResponseForRequest:request];BOOL match=[[got data] isEqual:data];NSUInteger bytes=[cache currentMemoryUsage];
 [cache removeAllCachedResponses];BOOL removed=[cache cachedResponseForRequest:request]==nil;
 printf("CACHE match=%d bytes=%lu removed=%d\n",match,(unsigned long)bytes,removed);need(match&&bytes>=5&&removed,"private memory cache");
 [cache release];[stored release];[response release];return 0;
}}
