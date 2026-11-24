/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#include <stdio.h>
@interface NSEvent(SFPreciseScrolling)
-(void)_sfSetPreciseScrolling:(BOOL)precise phase:(NSEventPhase)phase;
@end
int main(void){@autoreleasepool{
 NSEvent *event=[NSEvent mouseEventWithType:NSScrollWheel location:NSMakePoint(0,0)
  modifierFlags:NSShiftKeyMask window:nil clickCount:0 deltaX:.5f deltaY:-1.25f];
 int legacy=![event hasPreciseScrollingDeltas]&&[event phase]==NSEventPhaseNone&&[event momentumPhase]==NSEventPhaseNone;
 int present=[event respondsToSelector:@selector(_sfSetPreciseScrolling:phase:)];
 if(!present){printf("FXSCROLLEVENT private_api=0 legacy=%d\n",legacy);return 1;}
 int good=legacy;
 for(int phase=0;phase<4;phase++){
  NSEventPhase expected[]={NSEventPhaseBegan,NSEventPhaseChanged,NSEventPhaseEnded,NSEventPhaseCancelled};
  [event _sfSetPreciseScrolling:YES phase:expected[phase]];
  int match=[event hasPreciseScrollingDeltas]&&[event phase]==expected[phase]&&[event momentumPhase]==NSEventPhaseNone&&
    [event scrollingDeltaX]==.5&&[event scrollingDeltaY]==-1.25&&[event modifierFlags]==NSShiftKeyMask;
  printf("FXSCROLLEVENT phase=%lu dx=%.3f dy=%.3f modifiers=%lu momentum=%lu pass=%d\n",(unsigned long)[event phase],(double)[event scrollingDeltaX],(double)[event scrollingDeltaY],(unsigned long)[event modifierFlags],(unsigned long)[event momentumPhase],match);good&=match;
 }
 return good?0:1;
}}
