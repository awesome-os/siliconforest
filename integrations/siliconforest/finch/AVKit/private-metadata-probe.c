/* SPDX-License-Identifier: MIT */
/* Read-only runtime declarations, not SDK headers or implementation bytes.
 * Never instantiate a UI/capture object or call a private method. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *Class,*Method,*SEL;
static Class (*getClass)(const char *),(*superclass)(Class),(*objectClass)(void *);
static const char *(*className)(Class);
static const char *(*selectorName)(SEL);
static const char *(*encoding)(Method);
static size_t (*instanceSize)(Class);
static Method *(*methods)(Class,unsigned *);
static SEL (*methodName)(Method);
static int order(const void *a,const void *b){return strcmp(selectorName(methodName(*(Method*)a)),selectorName(methodName(*(Method*)b)));}
static int resolve(void *image,void *slot,const char *name){*(void**)slot=dlsym(image,name);return *(void**)slot!=NULL;}
#define LOAD(var,name) do{if(!resolve(runtime,&var,name)){fprintf(stderr,"MISSING %s\n",name);return 77;}}while(0)
int main(void){
 void *runtime=dlopen("/usr/lib/libobjc.A.dylib",RTLD_NOW|RTLD_GLOBAL);
 void *kit=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
 if(!runtime||!kit){fputs("AVKIT_METADATA load unavailable\n",stderr);return 77;}
 LOAD(getClass,"objc_getClass");LOAD(superclass,"class_getSuperclass");LOAD(objectClass,"object_getClass");
 LOAD(className,"class_getName");LOAD(instanceSize,"class_getInstanceSize");LOAD(methods,"class_copyMethodList");
 LOAD(methodName,"method_getName");LOAD(selectorName,"sel_getName");LOAD(encoding,"method_getTypeEncoding");
 const char *names[]={"AVPlayerController","AVPlayerControllerTimeResolver","AVPlayerControlsViewController","AVVolumeControlSlider","AVCaptureView","AVTouchBarCaptureInputSourceItem","AVTouchBarRecordingControlsProvider"};
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
  Class cls=getClass(names[i]);if(!cls){printf("CLASS %s present=0\n",names[i]);continue;}
  printf("CLASS %s present=1 superclass=%s instance-size=%zu\n",names[i],className(superclass(cls)),instanceSize(cls));
  for(unsigned meta=0;meta<2;meta++){
   unsigned count=0;Method *list=methods(meta?objectClass(cls):cls,&count);
   if(count>4096){free(list);fputs("Unexpected method bound\n",stderr);return 1;}
   qsort(list,count,sizeof(*list),order);
   for(unsigned j=0;j<count;j++)printf("METHOD %s %c %s %s\n",names[i],meta?'+':'-',selectorName(methodName(list[j])),encoding(list[j]));
   free(list);
  }
 }
 return 0;
}
