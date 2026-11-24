/* SPDX-License-Identifier: MIT */
/* Own runtime-only property observations. No window, device or native build. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
#include <math.h>
typedef struct {double x,y;} SFPoint;
typedef struct {double width,height;} SFSize;
typedef struct {SFPoint origin;SFSize size;} SFRect;
int main(void){@autoreleasepool{
 if(!dlopen("/System/Library/Frameworks/AVKit.framework/AVKit",RTLD_NOW|RTLD_GLOBAL)){fputs("LEVEL load failed\n",stderr);return 1;}
 Class c=objc_getClass("AVAudioLevelIndicatorView");if(!c){fputs("LEVEL missing class\n",stderr);return 1;}
 id object=((id(*)(id,SEL,SFRect))objc_msgSend)([c alloc],sel_registerName("initWithFrame:"),(SFRect){{0,0},{128,16}});
 if(!object){fputs("LEVEL allocation failed\n",stderr);return 1;}
 SEL get=sel_registerName("doubleValue"),set=sel_registerName("setDoubleValue:");
 printf("LEVEL initial=%.6g\n",((double(*)(id,SEL))objc_msgSend)(object,get));
 double inputs[]={-60,-1,0,.25,.5,1,2,60};
 for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);i++){
  ((void(*)(id,SEL,double))objc_msgSend)(object,set,inputs[i]);
  printf("LEVEL input=%.6g value=%.6g\n",inputs[i],((double(*)(id,SEL))objc_msgSend)(object,get));
 }
 SFSize size=((SFSize(*)(id,SEL))objc_msgSend)(object,sel_registerName("intrinsicContentSize"));
 printf("LEVEL intrinsic=%.6g,%.6g\n",size.width,size.height);
 printf("LEVEL vibrancy=%d\n",((BOOL(*)(id,SEL))objc_msgSend)(object,sel_registerName("allowsVibrancy"))!=0);
 [object release];return 0;
}}
