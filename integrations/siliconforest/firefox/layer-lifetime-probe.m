/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
@interface CALayer : NSObject
+(id)layer;
-(void)addSublayer:(id)layer;
-(id)_context;
-(void)setGeometryFlipped:(BOOL)value;
@end
@interface CALayerContext : NSObject
-(id)initWithFrame:(CGRect)frame;
-(void)setLayer:(id)layer;
-(void)invalidate;
@end
int main(void){@autoreleasepool{
 int failures=0;
 for(int replace=0;replace<2;replace++){
  CALayer *root=[[CALayer layer] retain],*child=[[CALayer layer] retain],*other=[[CALayer layer] retain];
  [root addSublayer:child];
  CALayerContext *context=[[CALayerContext alloc] initWithFrame:CGRectMake(0,0,32,32)];
  [context setLayer:root];
  int attached=[root _context]==context&&[child _context]==context;
  if(replace)[context setLayer:other];else [context invalidate];
  int detached=[root _context]==nil&&[child _context]==nil;
  int replacement=!replace||[other _context]==context;
  [context invalidate];
  int replacementDetached=[other _context]==nil;
  [context release];
  // Never message a known stale pointer in the control. Firefox's separate
  // close-tab crash is the concrete failure; this checks the lifetime contract.
  if(detached){[root setGeometryFlipped:YES];[child setGeometryFlipped:YES];}
  int pass=attached&&detached&&replacement&&replacementDetached;
  printf("FXLIFETIME replace=%d attached=%d old_tree_detached=%d replacement_live=%d replacement_detached=%d postrelease_mutation=%d pass=%d\n",replace,attached,detached,replacement,replacementDetached,detached,pass);
  failures+=!pass;[root release];[child release];[other release];
 }
 printf("FXLIFETIME failures=%d\n",failures);return failures?1:0;
}}
