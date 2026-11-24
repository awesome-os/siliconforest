/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
@interface NSWindow(FXMenuGeometry)
+(void)_sfSetMenuStripsEnabled:(BOOL)enabled;
@end
static int near(CGFloat a,CGFloat b){return fabs(a-b)<.01;}
int main(void){@autoreleasepool{
 [NSApplication sharedApplication];
 NSMenu *main=[[[NSMenu alloc] initWithTitle:@"FX geometry"] autorelease];
 [main addItemWithTitle:@"Firefox probe" action:NULL keyEquivalent:@""];
 [main addItemWithTitle:@"File" action:NULL keyEquivalent:@""];
 [NSApp setMainMenu:main];
 Method method=class_getClassMethod([NSWindow class],@selector(contentRectForFrameRect:styleMask:));
 Dl_info provider={0};if(method)dladdr((void *)method_getImplementation(method),&provider);
 printf("FXMENU provider=%s\n",provider.dli_fname?provider.dli_fname:"missing");
 int failures=0;
 for(int menus=0;menus<=1;menus++){
  [NSWindow _sfSetMenuStripsEnabled:menus];
  for(int kind=0;kind<4;kind++){
   Class cls=kind==2?[NSPanel class]:[NSWindow class];
   unsigned style=kind==3?NSBorderlessWindowMask:NSTitledWindowMask|NSClosableWindowMask;
   if(kind==1)style|=NSFullSizeContentViewWindowMask;
   NSRect requested=NSMakeRect(30,30,160,100);
   NSRect predicted=[cls frameRectForContentRect:requested styleMask:style];
   NSRect inverse=[cls contentRectForFrameRect:predicted styleMask:style];
   NSWindow *window=[[cls alloc] initWithContentRect:requested styleMask:style backing:NSBackingStoreBuffered defer:NO];
   [window setReleasedWhenClosed:NO];
   NSRect actual=[window frame],content=[[window contentView] frame];
   int pass=window&&near(predicted.size.width,actual.size.width)&&near(predicted.size.height,actual.size.height)&&near(inverse.size.width,requested.size.width)&&near(inverse.size.height,requested.size.height)&&near(content.size.width,requested.size.width)&&near(content.size.height,requested.size.height);
   printf("FXMENU menus=%d kind=%d predicted=%.0fx%.0f actual=%.0fx%.0f content=%.0fx%.0f inverse=%.0fx%.0f pass=%d\n",menus,kind,predicted.size.width,predicted.size.height,actual.size.width,actual.size.height,content.size.width,content.size.height,inverse.size.width,inverse.size.height,pass);
   failures+=!pass;[window close];[window release];
  }
 }
 printf("FXMENU failures=%d\n",failures);return failures?1:0;
}}
