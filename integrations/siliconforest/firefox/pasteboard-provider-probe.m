/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <AppKit/NSPasteboardItem.h>
#include <stdio.h>
@interface FXProvider:NSObject <NSPasteboardItemDataProvider> {
@public NSPasteboard *expected;int calls,correctBoard;BOOL itemStyle;
}
@end
@implementation FXProvider
- (void)pasteboard:(NSPasteboard *)board item:(NSPasteboardItem *)item provideDataForType:(NSString *)type {
 calls++;correctBoard=board==expected;
 if(itemStyle)[item setString:@"SF-FX promised UTF8 — द 🙂" forType:type];
 else [board setString:@"SF-FX promised UTF8 — द 🙂" forType:type];
}
@end
int main(void){@autoreleasepool{
 int failures=0;
 for(int mode=0;mode<2;mode++){@autoreleasepool{
  NSPasteboard *board=[NSPasteboard pasteboardWithUniqueName];
  FXProvider *provider=[FXProvider new];provider->expected=board;provider->itemStyle=mode;
  NSPasteboardItem *item=[NSPasteboardItem new];[item setDataProvider:provider forTypes:@[NSPasteboardTypeString]];
  [board clearContents];BOOL written=[board writeObjects:@[item]];
  BOOL equal=[[board stringForType:NSPasteboardTypeString] isEqualToString:@"SF-FX promised UTF8 — द 🙂"];
  BOOL passed=written&&equal&&provider->calls==1&&provider->correctBoard;
  printf("FXPBPROVIDER item_style=%d written=%d equal=%d calls=%d correct_board=%d pass=%d\n",mode,written,equal,provider->calls,provider->correctBoard,passed);
  failures+=!passed;[board clearContents];[item release];[provider release];
 }}return failures?1:0;
}}
