/* SPDX-License-Identifier: MIT */
/* Read-only runtime interface observations, not a framework/header dump.
 * Linux-built program: no native build, code extraction, or UI interaction. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int compareMethods(const void *a,const void *b) {
 return strcmp(sel_getName(method_getName(*(Method const *)a)),sel_getName(method_getName(*(Method const *)b)));
}
static void methods(Class c,const char *name,const char *kind) {
 unsigned count=0;Method *list=class_copyMethodList(c,&count);
 qsort(list,count,sizeof(*list),compareMethods);
 printf("AVKIT_INTERFACE %s %s total=%u\n",name,kind,count);
 unsigned limit=count<1024?count:1024;
 for(unsigned i=0;i<limit;i++)printf("AVKIT_METHOD %s %s %s %s\n",name,kind,
  sel_getName(method_getName(list[i])),method_getTypeEncoding(list[i]));
 if(limit<count)printf("AVKIT_TRUNCATED %s %s\n",name,kind);
 free(list);
}
int main(void){@autoreleasepool{
 void *image=dlopen("/System/Library/Frameworks/AVKit.framework/AVKit",RTLD_NOW|RTLD_GLOBAL);
 if(!image){fprintf(stderr,"AVKIT load failed: %s\n",dlerror());return 1;}
 const char *names[]={"AVAssetIdentifier","AVAudioLevelIndicatorView","AVCaptureView",
  "AVPlayerController","AVPlayerControllerTimeResolver","AVPlayerControlsViewController",
  "AVTimecode","AVTouchBarCaptureInputSourceItem","AVTouchBarRecordingControlsProvider",
  "AVVolumeControlSlider"};
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
  Class c=objc_getClass(names[i]);
  printf("AVKIT_CLASS %s present=%d superclass=%s\n",names[i],c!=Nil,
   c&&class_getSuperclass(c)?class_getName(class_getSuperclass(c)):"none");
  if(c){methods(c,names[i],"instance");methods(object_getClass(c),names[i],"class");}
 }
 return 0;
}}
