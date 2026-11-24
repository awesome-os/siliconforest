/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <AppKit/NSAppearance.h>
#import <AppKit/NSMenuView.h>
#include <stdio.h>
@interface NSMenu(FXAppearanceProbe)
-(id)appearance;-(void)setAppearance:(id)value;-(id)effectiveAppearance;
@end
static int destroyed;
@interface FXAppearance:NSAppearance @end
@implementation FXAppearance
-(void)dealloc {destroyed++;[super dealloc];}
@end
int main(void){@autoreleasepool{
 [NSApplication sharedApplication];
 NSMenu *parent=[[NSMenu alloc] initWithTitle:@"FX parent"],*child=[[NSMenu alloc] initWithTitle:@"FX child"];
 if(![parent respondsToSelector:@selector(setAppearance:)]){puts("FXMENUAPPEARANCE setter=0");return 1;}
 id dark=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];[parent setAppearance:dark];
 NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:@"FX submenu" action:NULL keyEquivalent:@""];
 [parent addItem:item];[item setSubmenu:child];
 int inherited=[child appearance]==nil&&[child effectiveAppearance]==dark;
 NSAutoreleasePool *temporary=[NSAutoreleasePool new];
 FXAppearance *own=[[FXAppearance alloc] initWithAppearanceNamed:NSAppearanceNameAqua bundle:nil];
 [child setAppearance:own];[own release];
 int retained=destroyed==0&&[child appearance]!=nil&&[child effectiveAppearance]==[child appearance];
 [child setAppearance:nil];int cleared=[child appearance]==nil&&[child effectiveAppearance]==dark;
 // Associated-object getters retain/autorelease the value. Verify release after
 // draining those legal temporary references, not immediately after clearing.
 [temporary drain];cleared&=destroyed==1;
 NSMenuView *view=[[NSMenuView alloc] initWithFrame:NSMakeRect(0,0,100,50)];[view setMenu:child];
 int popup=[view effectiveAppearance]==dark;
 printf("FXMENUAPPEARANCE inherited=%d retained=%d cleared=%d popup=%d destroyed=%d\n",inherited,retained,cleared,popup,destroyed);
 [view setMenu:nil];[view release];[item release];[child release];[parent release];
 return inherited&&retained&&cleared&&popup?0:1;
}}
