/* SPDX-License-Identifier: MIT */
/* Runtime method encodings and constant values only; no service operations,
 * capture/authorization requests or Apple implementation bytes. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
static void methods(Class cls,const char *kind){
 unsigned n=0;Method *methods=class_copyMethodList(cls,&n);
 for(unsigned i=0;i<n;i++)printf("METHOD %s %s %s %s\n",class_getName(cls),kind,sel_getName(method_getName(methods[i])),method_getTypeEncoding(methods[i]));
 free(methods);
}
int main(void){@autoreleasepool{
 const char *frameworks[]={"TCC","PhotosFormats","MediaRemote","GeoKit","MobileAsset","MonitorPanel","PerformanceAnalysis","FeatureFlags","MediaToolbox"};
 for(unsigned i=0;i<9;i++){
  char p[256];snprintf(p,sizeof(p),"/System/Library/%s/%s.framework/Versions/A/%s",i==8?"Frameworks":"PrivateFrameworks",frameworks[i],frameworks[i]);
  void *h=dlopen(p,RTLD_NOW|RTLD_GLOBAL);printf("LOAD %s %d\n",frameworks[i],h!=NULL);
  if(h&&i==4){id *v=dlsym(h,"ASAttributeContentVersion");printf("CONST ASAttributeContentVersion %s\n",v&&*v?[[*v description] UTF8String]:"<absent>");}
 }
 const char *classes[]={"PFSlowMotionUtilities","PFVideoAVObjectBuilder","PFVideoAdjustments","GEOCity","GeoKitPlace","MAAsset","MAAssetQuery","MADownloadOptions","MPDisplayMgr"};
 for(unsigned i=0;i<9;i++){
  Class cls=objc_getClass(classes[i]);printf("CLASS %s %s\n",classes[i],cls&&class_getSuperclass(cls)?class_getName(class_getSuperclass(cls)):"<absent>");
  if(cls){methods(cls,"-");methods(object_getClass(cls),"+");}
 }
 return 0;
}}
