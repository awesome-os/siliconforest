/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
@interface NSObject(SFUnavailableProbe)
+ (id)sharedAddressBook;
+ (id)applicationWithBundleIdentifier:(NSString*)identifier;
- (NSArray*)people;
- (BOOL)saveAndReturnError:(NSError**)error;
- (id)initWithSource:(NSString*)source;
- (NSString*)source;
- (BOOL)compileAndReturnError:(NSDictionary**)error;
- (id)executeAndReturnError:(NSDictionary**)error;
- (id)initWithFrame:(NSRect)frame;
- (id)mainFrame;
- (void)setFrameLoadDelegate:(id)delegate;
- (void)setNavigationDelegate:(id)delegate;
- (void)loadHTMLString:(NSString*)html baseURL:(NSURL*)url;
- (void)evaluateJavaScript:(NSString*)script completionHandler:(void(^)(id,NSError*))callback;
+ (id)sharedUpdater;
- (BOOL)automaticallyChecksForUpdates;
- (BOOL)automaticallyDownloadsUpdates;
- (BOOL)updateInProgress;
- (id)lastUpdateCheckDate;
- (void)setDelegate:(id)delegate;
- (void)checkForUpdates:(id)sender;
@end
static unsigned legacy,wk,js,updates;
@interface SFDelegate:NSObject @end
@implementation SFDelegate
- (void)webView:(id)view didFailProvisionalLoadWithError:(NSError*)error forFrame:(id)frame {if(error&&frame)legacy++;}
- (void)webView:(id)view didFailProvisionalNavigation:(id)navigation withError:(NSError*)error {if(error)wk++;}
- (void)updater:(id)updater didAbortWithError:(NSError*)error {if(error)updates++;}
@end
static void need(int condition,const char *where){if(!condition){fprintf(stderr,"UNAVAILABLE_FAIL %s\n",where);exit(1);}}
int main(void){@autoreleasepool {
 const char *names[]={"AddressBook","ScriptingBridge","WebKit","Foundation","CoreFoundation","Sparkle"};
 for(unsigned i=0;i<6;i++) {char path[256];snprintf(path,sizeof(path),"/System/Library/Frameworks/%s.framework/%s",names[i],names[i]);void *image=dlopen(path,RTLD_NOW|RTLD_GLOBAL);if(!image)fprintf(stderr,"%s\n",dlerror());need(image!=NULL,names[i]);}
 const id *calendar=dlsym(RTLD_DEFAULT,"NSGregorianCalendar");need(calendar&&[*calendar isEqualToString:@"gregorian"],"Gregorian identifier");
 Class book=NSClassFromString(@"ABAddressBook"),automation=NSClassFromString(@"SBApplication"),script=NSClassFromString(@"NSAppleScript");
 need(book&&automation&&script,"classes present");need([book sharedAddressBook]==nil,"no contacts store");
 id b=[book new];NSError *error=nil;need([[b people] count]==0&&![b saveAndReturnError:&error]&&error,"contact save fails");[b release];
 need([automation applicationWithBundleIdentifier:@"org.siliconforest.nonexistent"]==nil,"automation unavailable");
 id s=[[script alloc] initWithSource:@"return 1"];NSDictionary *failure=nil;
 need([[s source] isEqualToString:@"return 1"]&&![s compileAndReturnError:&failure]&&failure,"script compile fails");failure=nil;
 need([s executeAndReturnError:&failure]==nil&&failure,"script execute fails");[s release];
 SFDelegate *delegate=[SFDelegate new];id view=[[NSClassFromString(@"WebView") alloc] initWithFrame:NSMakeRect(0,0,8,8)];need(view!=nil,"legacy view");
 [view setFrameLoadDelegate:delegate];[[view mainFrame] loadHTMLString:@"<b>own test</b>" baseURL:nil];
 id modern=[[NSClassFromString(@"WKWebView") alloc] initWithFrame:NSMakeRect(0,0,8,8)];need(modern!=nil,"modern view");
 [modern setNavigationDelegate:delegate];[modern loadHTMLString:@"own test" baseURL:nil];
 [modern evaluateJavaScript:@"1+1" completionHandler:^(id value,NSError *err){if(value==nil&&err)js++;}];
 id updater=[NSClassFromString(@"SUUpdater") sharedUpdater];
 need(updater&&![updater automaticallyChecksForUpdates]&&![updater automaticallyDownloadsUpdates]&&![updater updateInProgress]&&![updater lastUpdateCheckDate],"updater cannot check/download/install");
 [updater setDelegate:delegate];[updater checkForUpdates:nil];
 for(unsigned n=0;n<100&&(legacy!=1||wk!=1||js!=1||updates!=1);n++)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.02,true);
 printf("UNAVAILABLE classes=present calendar=gregorian contacts=failed automation=nil script=failed web-failures=%u/%u js-failures=%u updater-failures=%u\n",legacy,wk,js,updates);
 need(legacy==1&&wk==1&&js==1&&updates==1,"exactly one failure callback per operation");[updater setDelegate:nil];[modern release];[view release];[delegate release];return 0;
}}
