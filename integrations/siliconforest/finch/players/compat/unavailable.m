/* SPDX-License-Identifier: MIT */
/* Feature absence is explicit: no contacts store, Apple Events, or web engine. */
#import <Foundation/Foundation.h>
#import <AppKit/NSView.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#ifndef SF_KIND
#error SF_KIND required
#endif
static NSError *unavailable(NSString *feature) {
 return [NSError errorWithDomain:@"org.siliconforest.Unavailable" code:1
  userInfo:@{NSLocalizedDescriptionKey:[feature stringByAppendingString:@" is unavailable on SiliconForest"]}];
}
#if SF_KIND == 1
@interface ABRecord:NSObject @end
@implementation ABRecord
- (NSString*)uniqueId{return nil;}
- (id)valueForProperty:(NSString*)property{return nil;}
- (BOOL)setValue:(id)value forProperty:(NSString*)property{return NO;}
- (BOOL)removeValueForProperty:(NSString*)property{return NO;}
@end
@interface ABPerson:ABRecord @end
@implementation ABPerson
+ (id)person{return nil;}
@end
@interface ABGroup:ABRecord @end
@implementation ABGroup
+ (id)group{return nil;}
- (NSArray*)members{return @[];}
- (BOOL)addMember:(id)person{return NO;}
- (BOOL)removeMember:(id)person{return NO;}
@end
@interface ABAddressBook:NSObject @end
@implementation ABAddressBook
+ (id)sharedAddressBook{return nil;}
- (NSArray*)people{return @[];}
- (NSArray*)groups{return @[];}
- (id)me{return nil;}
- (BOOL)save{return NO;}
- (BOOL)saveAndReturnError:(NSError**)error{if(error)*error=unavailable(@"Contacts storage");return NO;}
- (BOOL)hasUnsavedChanges{return NO;}
- (BOOL)addRecord:(id)record{return NO;}
- (BOOL)removeRecord:(id)record{return NO;}
- (id)recordForUniqueId:(NSString*)identifier{return nil;}
@end
void *ABGetSharedAddressBook(void){return NULL;}
#elif SF_KIND == 2
@interface SBObject:NSObject @end
@implementation SBObject
- (id)get{return nil;}
- (id)propertyWithCode:(uint32_t)code{return nil;}
- (id)propertyWithClass:(Class)kind code:(uint32_t)code{return nil;}
- (id)elementArrayWithCode:(uint32_t)code{return nil;}
- (NSError*)lastError{return unavailable(@"Apple Events automation");}
@end
@interface SBApplication:SBObject @end
@implementation SBApplication
+ (id)applicationWithBundleIdentifier:(NSString*)identifier{return nil;}
+ (id)applicationWithProcessIdentifier:(int)pid{return nil;}
+ (id)applicationWithURL:(NSURL*)url{return nil;}
- (id)initWithBundleIdentifier:(NSString*)identifier{[self release];return nil;}
- (id)initWithProcessIdentifier:(int)pid{[self release];return nil;}
- (id)initWithURL:(NSURL*)url{[self release];return nil;}
- (BOOL)isRunning{return NO;}
- (NSError*)lastError{return unavailable(@"Apple Events automation");}
@end
@interface SBElementArray:NSArray @end
@implementation SBElementArray
- (NSUInteger)count{return 0;}
- (id)objectAtIndex:(NSUInteger)index{[NSException raise:NSRangeException format:@"Empty unavailable automation collection"];return nil;}
- (id)get{return @[];}
@end
#elif SF_KIND == 3
@class WebView;
@interface NSObject(SFWebFailure)
- (void)webView:(id)view didFailProvisionalLoadWithError:(NSError*)error forFrame:(id)frame;
- (void)webView:(id)view didFailLoadWithError:(NSError*)error forFrame:(id)frame;
- (void)webView:(id)view didFailProvisionalNavigation:(id)navigation withError:(NSError*)error;
@end
@interface WebPreferences:NSObject {NSMutableDictionary *_values;} @end
@implementation WebPreferences
+ (id)standardPreferences{static id shared;static dispatch_once_t once;dispatch_once(&once,^{shared=[self new];});return shared;}
- (id)init{if((self=[super init]))_values=[NSMutableDictionary new];return self;}
- (void)dealloc{[_values release];[super dealloc];}
- (void)setJavaScriptEnabled:(BOOL)value{[_values setObject:@(value) forKey:@"JavaScriptEnabled"];}
- (BOOL)javaScriptEnabled{return [[_values objectForKey:@"JavaScriptEnabled"] boolValue];}
- (void)setUserStyleSheetEnabled:(BOOL)value{[_values setObject:@(value) forKey:@"UserStyleSheetEnabled"];}
- (void)setUserStyleSheetLocation:(NSURL*)url{if(url)[_values setObject:url forKey:@"UserStyleSheetLocation"];else [_values removeObjectForKey:@"UserStyleSheetLocation"];}
@end
@interface WebFrame:NSObject {WebView *_view;} - (id)initWithWebView:(WebView*)view; - (void)sfDetach; @end
@interface WebView:NSView {WebFrame *_mainFrame;WebPreferences *_preferences;id _frameLoadDelegate,_policyDelegate,_UIDelegate;NSString *_groupName;BOOL _maintainsBackForwardList,_drawsBackground;} - (void)sfFail:(id)frame; @end
@implementation WebFrame
- (id)initWithWebView:(WebView*)view{if((self=[super init]))_view=view;return self;}
- (void)sfDetach{_view=nil;}
- (void)loadHTMLString:(NSString*)html baseURL:(NSURL*)base{[_view sfFail:self];}
- (void)loadRequest:(NSURLRequest*)request{[_view sfFail:self];}
- (id)dataSource{return nil;}
- (id)DOMDocument{return nil;}
@end
@implementation WebView
- (void)sfSetup{if(!_mainFrame)_mainFrame=[[WebFrame alloc] initWithWebView:self];if(!_preferences)_preferences=[WebPreferences new];}
- (id)initWithFrame:(NSRect)frame{if((self=[super initWithFrame:frame]))[self sfSetup];return self;}
- (id)initWithCoder:(NSCoder*)coder{if((self=[super initWithCoder:coder]))[self sfSetup];return self;}
- (void)dealloc{[_mainFrame sfDetach];[_mainFrame release];[_preferences release];[_groupName release];[super dealloc];}
- (WebFrame*)mainFrame{[self sfSetup];return _mainFrame;}
- (id)preferences{[self sfSetup];return _preferences;}
- (void)setPreferences:(WebPreferences*)value{[value retain];[_preferences release];_preferences=value;}
- (void)setFrameLoadDelegate:(id)value{_frameLoadDelegate=value;}
- (id)frameLoadDelegate{return _frameLoadDelegate;}
- (void)setPolicyDelegate:(id)value{_policyDelegate=value;}
- (void)setUIDelegate:(id)value{_UIDelegate=value;}
- (void)setGroupName:(NSString*)name{NSString *copy=[name copy];[_groupName release];_groupName=copy;}
- (NSString*)groupName{return _groupName;}
- (BOOL)isLoading{return NO;}
- (BOOL)canGoBack{return NO;}
- (BOOL)canGoForward{return NO;}
- (BOOL)goBack{return NO;}
- (BOOL)goForward{return NO;}
- (NSString*)stringByEvaluatingJavaScriptFromString:(NSString*)script{return nil;}
- (void)stopLoading:(id)sender{}
- (void)setMaintainsBackForwardList:(BOOL)flag{_maintainsBackForwardList=flag;}
- (void)setDrawsBackground:(BOOL)flag{_drawsBackground=flag;}
- (BOOL)drawsBackground{return _drawsBackground;}
- (void)sfFail:(id)frame{
 id delegate=[_frameLoadDelegate retain];NSError *error=[unavailable(@"Web rendering") retain];
 dispatch_async(dispatch_get_main_queue(),^{
  if([delegate respondsToSelector:@selector(webView:didFailProvisionalLoadWithError:forFrame:)])
   [delegate webView:self didFailProvisionalLoadWithError:error forFrame:frame];
  [error release];[delegate release];
 });
}
@end
@interface WKUserScript:NSObject {NSString *_source;NSInteger _injectionTime;BOOL _forMainFrameOnly;} @end
@implementation WKUserScript
- (id)initWithSource:(NSString*)source injectionTime:(NSInteger)time forMainFrameOnly:(BOOL)only{if((self=[super init])){_source=[source copy];_injectionTime=time;_forMainFrameOnly=only;}return self;}
- (void)dealloc{[_source release];[super dealloc];}
- (NSString*)source{return _source;}
- (NSInteger)injectionTime{return _injectionTime;}
- (BOOL)isForMainFrameOnly{return _forMainFrameOnly;}
- (id)copyWithZone:(NSZone*)zone{return [self retain];}
@end
@interface WKUserContentController:NSObject {NSMutableArray *_scripts;} @end
@implementation WKUserContentController
- (id)init{if((self=[super init]))_scripts=[NSMutableArray new];return self;}
- (void)dealloc{[_scripts release];[super dealloc];}
- (NSArray*)userScripts{return [[_scripts copy] autorelease];}
- (void)addUserScript:(id)value{if(value)[_scripts addObject:value];}
- (void)removeAllUserScripts{[_scripts removeAllObjects];}
@end
@interface WKWebViewConfiguration:NSObject {id _controller;} @end
@implementation WKWebViewConfiguration
- (id)init{if((self=[super init]))_controller=[WKUserContentController new];return self;}
- (void)dealloc{[_controller release];[super dealloc];}
- (id)userContentController{return _controller;}
- (id)copyWithZone:(NSZone*)zone{return [self retain];}
@end
@interface WKNavigation:NSObject @end
@implementation WKNavigation @end
@interface WKWebView:NSView {id _navigationDelegate,_UIDelegate,_configuration;} @end
@implementation WKWebView
- (id)initWithFrame:(NSRect)frame configuration:(id)configuration{if((self=[super initWithFrame:frame]))_configuration=configuration?[configuration retain]:[WKWebViewConfiguration new];return self;}
- (id)initWithFrame:(NSRect)frame{return [self initWithFrame:frame configuration:nil];}
- (void)dealloc{[_configuration release];[super dealloc];}
- (id)configuration{return _configuration;}
- (void)setNavigationDelegate:(id)value{_navigationDelegate=value;}
- (id)navigationDelegate{return _navigationDelegate;}
- (void)setUIDelegate:(id)value{_UIDelegate=value;}
- (BOOL)isLoading{return NO;}
- (BOOL)canGoBack{return NO;}
- (BOOL)canGoForward{return NO;}
- (id)sfFail{
 id delegate=[_navigationDelegate retain];NSError *error=[unavailable(@"Web rendering") retain];
 dispatch_async(dispatch_get_main_queue(),^{
  if([delegate respondsToSelector:@selector(webView:didFailProvisionalNavigation:withError:)])
   [delegate webView:self didFailProvisionalNavigation:nil withError:error];
  [error release];[delegate release];
 });return nil;
}
- (id)loadRequest:(id)request{return [self sfFail];}
- (id)loadHTMLString:(NSString*)html baseURL:(NSURL*)url{return [self sfFail];}
- (id)loadFileURL:(NSURL*)url allowingReadAccessToURL:(NSURL*)allowed{return [self sfFail];}
- (void)evaluateJavaScript:(NSString*)script completionHandler:(void(^)(id,NSError*))handler{
 if(handler)dispatch_async(dispatch_get_main_queue(),^{handler(nil,unavailable(@"JavaScript evaluation"));});
}
- (void)stopLoading{}
@end
#elif SF_KIND == 4
/* Sparkle is an application updater, not a media backend. Its original Mac
 * replacement payload cannot update this Linux-built port. The public v1
 * configuration API is observed from upstream SUUpdater.h (1.26.0); checks
 * fail explicitly and there is no download, privilege request or installer. */
@interface NSObject(SFUpdaterFailure)
- (void)updater:(id)updater didAbortWithError:(NSError*)error;
@end
@interface SUUpdater:NSObject {
 NSBundle *_bundle;id _delegate;NSURL *_feedURL;NSString *_userAgent;
 NSDictionary *_httpHeaders;NSTimeInterval _interval;BOOL _sendsProfile;
}
+ (id)updaterForBundle:(NSBundle*)bundle;
- (id)initForBundle:(NSBundle*)bundle;
@end
@implementation SUUpdater
+ (id)sharedUpdater{return [self updaterForBundle:[NSBundle mainBundle]];}
+ (id)updaterForBundle:(NSBundle*)bundle{
 if(!bundle)return nil;
 static NSMutableDictionary *instances;static dispatch_once_t once;
 dispatch_once(&once,^{instances=[NSMutableDictionary new];});
 @synchronized(instances){
  NSString *key=[bundle bundlePath];if(!key)return nil;
  id updater=[instances objectForKey:key];
  if(!updater){updater=[[[self alloc] initForBundle:bundle] autorelease];[instances setObject:updater forKey:key];}
  return updater;
 }
}
- (id)init{return [self initForBundle:[NSBundle mainBundle]];}
- (id)initForBundle:(NSBundle*)bundle{if(!bundle){[self release];return nil;}if((self=[super init])){_bundle=[bundle retain];_interval=86400;}return self;}
- (void)dealloc{[_bundle release];[_feedURL release];[_userAgent release];[_httpHeaders release];[super dealloc];}
- (id)delegate{return _delegate;}
- (void)setDelegate:(id)value{_delegate=value;}
- (NSBundle*)hostBundle{return _bundle;}
- (NSBundle*)sparkleBundle{return [NSBundle bundleForClass:[self class]];}
- (NSURL*)feedURL{return _feedURL;}
- (void)setFeedURL:(NSURL*)value{NSURL *copy=[value copy];[_feedURL release];_feedURL=copy;}
- (NSString*)userAgentString{return _userAgent;}
- (void)setUserAgentString:(NSString*)value{NSString *copy=[value copy];[_userAgent release];_userAgent=copy;}
- (NSDictionary*)httpHeaders{return _httpHeaders;}
- (void)setHttpHeaders:(NSDictionary*)value{NSDictionary *copy=[value copy];[_httpHeaders release];_httpHeaders=copy;}
- (NSTimeInterval)updateCheckInterval{return _interval;}
- (void)setUpdateCheckInterval:(NSTimeInterval)value{_interval=value;}
- (BOOL)sendsSystemProfile{return _sendsProfile;}
- (void)setSendsSystemProfile:(BOOL)value{_sendsProfile=value;}
- (BOOL)automaticallyChecksForUpdates{return NO;}
- (void)setAutomaticallyChecksForUpdates:(BOOL)value{(void)value;}
- (BOOL)automaticallyDownloadsUpdates{return NO;}
- (void)setAutomaticallyDownloadsUpdates:(BOOL)value{(void)value;}
- (BOOL)updateInProgress{return NO;}
- (NSDate*)lastUpdateCheckDate{return nil;}
- (BOOL)validateMenuItem:(id)item{return NO;}
- (void)resetUpdateCycle{} /* No timer exists while checking is unavailable. */
- (void)sfFailUpdate{
 id delegate=[_delegate retain];NSError *error=[unavailable(@"Automatic application updates") retain];
 fprintf(stderr,"SF_UPDATER unavailable; no update checked, downloaded or installed\n");
 dispatch_async(dispatch_get_main_queue(),^{
  if([delegate respondsToSelector:@selector(updater:didAbortWithError:)])[delegate updater:self didAbortWithError:error];
  [error release];[delegate release];
 });
}
- (void)checkForUpdates:(id)sender{[self sfFailUpdate];}
- (void)checkForUpdatesInBackground{[self sfFailUpdate];}
- (void)checkForUpdateInformation{[self sfFailUpdate];}
- (void)installUpdatesIfAvailable{[self sfFailUpdate];}
@end
#endif
