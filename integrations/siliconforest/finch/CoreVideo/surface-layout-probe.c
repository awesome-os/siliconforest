/* SPDX-License-Identifier: MIT */
/* Own public C ABI declarations; no Apple SDK headers or implementation input.
 * Metadata-only native/reference probe: never prints pointers, IDs or pixels.
 * IOSurface element getters follow Apple's public documentation; unsupported
 * optional getters are reported absent rather than given guessed signatures.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct __CVBuffer *CVPixelBufferRef;
typedef struct __IOSurface *IOSurfaceRef;
typedef int32_t CVReturn;
typedef uint64_t CVPixelBufferLockFlags;

/* Resolve the canonical native framework at runtime. PD's standalone CF dylib
 * has a different install name, so linking it would invalidate the Mac probe. */
static __typeof__(CFDictionaryCreate) *cfDictionaryCreate;
static __typeof__(CFArrayGetTypeID) *cfArrayGetTypeID;
static __typeof__(CFArrayGetCount) *cfArrayGetCount;
static __typeof__(CFArrayGetValueAtIndex) *cfArrayGetValueAtIndex;
static __typeof__(CFDictionaryGetTypeID) *cfDictionaryGetTypeID;
static __typeof__(CFDictionaryGetValue) *cfDictionaryGetValue;
static __typeof__(CFNumberGetTypeID) *cfNumberGetTypeID;
static __typeof__(CFNumberGetValue) *cfNumberGetValue;
static __typeof__(CFGetTypeID) *cfGetTypeID;
static __typeof__(CFRelease) *cfRelease;
static const CFBooleanRef *cfTrue;
static const CFDictionaryKeyCallBacks *cfKeyCallbacks;
static const CFDictionaryValueCallBacks *cfValueCallbacks;
#define CFDictionaryCreate cfDictionaryCreate
#define CFArrayGetTypeID cfArrayGetTypeID
#define CFArrayGetCount cfArrayGetCount
#define CFArrayGetValueAtIndex cfArrayGetValueAtIndex
#define CFDictionaryGetTypeID cfDictionaryGetTypeID
#define CFDictionaryGetValue cfDictionaryGetValue
#define CFNumberGetTypeID cfNumberGetTypeID
#define CFNumberGetValue cfNumberGetValue
#define CFGetTypeID cfGetTypeID
#define CFRelease cfRelease
#define kCFBooleanTrue (*cfTrue)
#define kCFTypeDictionaryKeyCallBacks (*cfKeyCallbacks)
#define kCFTypeDictionaryValueCallBacks (*cfValueCallbacks)

static CVReturn (*createBuffer)(CFAllocatorRef,size_t,size_t,uint32_t,CFDictionaryRef,CVPixelBufferRef*);
static CVReturn (*lockBuffer)(CVPixelBufferRef,CVPixelBufferLockFlags);
static CVReturn (*unlockBuffer)(CVPixelBufferRef,CVPixelBufferLockFlags);
static void (*releaseBuffer)(CVPixelBufferRef);
static size_t (*bufferWidth)(CVPixelBufferRef),(*bufferHeight)(CVPixelBufferRef);
static size_t (*bufferRow)(CVPixelBufferRef),(*bufferSize)(CVPixelBufferRef),(*bufferPlanes)(CVPixelBufferRef);
static size_t (*bufferPlaneWidth)(CVPixelBufferRef,size_t),(*bufferPlaneHeight)(CVPixelBufferRef,size_t),(*bufferPlaneRow)(CVPixelBufferRef,size_t);
static void *(*bufferBase)(CVPixelBufferRef),*(*bufferPlaneBase)(CVPixelBufferRef,size_t);
static IOSurfaceRef (*bufferSurface)(CVPixelBufferRef);
static size_t (*surfaceSize)(IOSurfaceRef),(*surfacePlanes)(IOSurfaceRef);
static size_t (*surfaceWidth)(IOSurfaceRef),(*surfaceHeight)(IOSurfaceRef),(*surfaceRow)(IOSurfaceRef);
static size_t (*surfaceBPE)(IOSurfaceRef),(*surfaceEW)(IOSurfaceRef),(*surfaceEH)(IOSurfaceRef);
static size_t (*surfacePlaneWidth)(IOSurfaceRef,size_t),(*surfacePlaneHeight)(IOSurfaceRef,size_t),(*surfacePlaneRow)(IOSurfaceRef,size_t);
static size_t (*surfacePlaneBPE)(IOSurfaceRef,size_t),(*surfacePlaneEW)(IOSurfaceRef,size_t),(*surfacePlaneEH)(IOSurfaceRef,size_t);
static void *(*surfaceBase)(IOSurfaceRef),*(*surfacePlaneBase)(IOSurfaceRef,size_t);
static CFTypeRef (*surfaceCopyValue)(IOSurfaceRef,CFStringRef);
static int (*gpuCopyFD)(IOSurfaceRef);
static size_t (*gpuOffset)(IOSurfaceRef);
static const CFStringRef *surfacePropertiesKey,*isGlobalKey,*planeInfoKey,*planeSizeKey;

static int loadRequired(void *image,void *slot,const char *name) {
 void *p=dlsym(image,name);*(void**)slot=p;
 if(!p)fprintf(stderr,"SURFACE_LAYOUT missing-required=%s\n",name);
 return p!=NULL;
}
#define REQUIRED(h,var,name) do{if(!loadRequired(h,&var,name))return 2;}while(0)
#define OPTIONAL(h,var,name) do{*(void**)&var=dlsym(h,name);}while(0)
static int64_t offset(const void *pointer,const void *base) {
 if(!pointer||!base)return -1;
 uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)base;
 return p>=b?(int64_t)(p-b):-(int64_t)(b-p);
}
static int planeDeclaredSize(IOSurfaceRef surface,size_t plane,int64_t *out) {
 if(!surfaceCopyValue||!planeInfoKey||!planeSizeKey)return 0;
 CFTypeRef info=surfaceCopyValue(surface,*planeInfoKey);int ok=0;
 if(info&&CFGetTypeID(info)==CFArrayGetTypeID()&&plane<(size_t)CFArrayGetCount(info)) {
  CFTypeRef dictionary=CFArrayGetValueAtIndex(info,(CFIndex)plane);
  if(dictionary&&CFGetTypeID(dictionary)==CFDictionaryGetTypeID()) {
   CFTypeRef value=CFDictionaryGetValue(dictionary,*planeSizeKey);
   if(value&&CFGetTypeID(value)==CFNumberGetTypeID())ok=CFNumberGetValue(value,kCFNumberSInt64Type,out);
  }
 }
 if(info)CFRelease(info);return ok;
}
static int run(size_t width,size_t height,uint32_t format,const char *formatName,int wantsSurface) {
 CFDictionaryRef properties=NULL,attributes=NULL;
 if(wantsSurface) {
  const void *keys[]={*isGlobalKey},*values[]={kCFBooleanTrue};
  properties=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  const void *attributeKeys[]={*surfacePropertiesKey},*attributeValues[]={properties};
  attributes=CFDictionaryCreate(NULL,attributeKeys,attributeValues,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 }
 CVPixelBufferRef buffer=NULL;
 CVReturn result=createBuffer(NULL,width,height,format,attributes,&buffer);
 if(attributes)CFRelease(attributes);if(properties)CFRelease(properties);
 printf("CREATE format=%s requested=%zux%zu surface-requested=%d status=%d buffer=%d\n",formatName,width,height,wantsSurface,result,buffer!=NULL);
 if(result||!buffer)return 1;
 result=lockBuffer(buffer,1);printf("LOCK format=%s requested=%zux%zu surface-requested=%d status=%d\n",formatName,width,height,wantsSurface,result);
 if(result){releaseBuffer(buffer);return 1;}
 IOSurfaceRef surface=bufferSurface(buffer);
 const void *cvBase=bufferBase(buffer),*ioBase=surface?surfaceBase(surface):NULL;
 size_t planes=bufferPlanes(buffer);
 printf("CV width=%zu height=%zu row=%zu size=%zu planes=%zu base-present=%d iosurface=%d\n",bufferWidth(buffer),bufferHeight(buffer),bufferRow(buffer),bufferSize(buffer),planes,cvBase!=NULL,surface!=NULL);
 for(size_t i=0;i<planes;i++)printf("CV_PLANE index=%zu width=%zu height=%zu row=%zu offset-cv=%"PRId64" offset-io=%"PRId64"\n",i,bufferPlaneWidth(buffer,i),bufferPlaneHeight(buffer,i),bufferPlaneRow(buffer,i),offset(bufferPlaneBase(buffer,i),cvBase),offset(bufferPlaneBase(buffer,i),ioBase));
 if(surface) {
  printf("IO width=%zu height=%zu row=%zu alloc=%zu planes=%zu base-present=%d bpe=%zu ew=%zu eh=%zu element-getters=%d%d%d\n",surfaceWidth(surface),surfaceHeight(surface),surfaceRow(surface),surfaceSize(surface),surfacePlanes(surface),ioBase!=NULL,surfaceBPE?surfaceBPE(surface):0,surfaceEW?surfaceEW(surface):0,surfaceEH?surfaceEH(surface):0,surfaceBPE!=NULL,surfaceEW!=NULL,surfaceEH!=NULL);
  for(size_t i=0;i<surfacePlanes(surface);i++) {
   int64_t declared=0;int available=planeDeclaredSize(surface,i,&declared);
   printf("IO_PLANE index=%zu width=%zu height=%zu row=%zu offset=%"PRId64" bpe=%zu ew=%zu eh=%zu element-getters=%d%d%d row-height-bytes=%zu declared-size-available=%d declared-size=%"PRId64"\n",i,surfacePlaneWidth(surface,i),surfacePlaneHeight(surface,i),surfacePlaneRow(surface,i),offset(surfacePlaneBase(surface,i),ioBase),surfacePlaneBPE?surfacePlaneBPE(surface,i):0,surfacePlaneEW?surfacePlaneEW(surface,i):0,surfacePlaneEH?surfacePlaneEH(surface,i):0,surfacePlaneBPE!=NULL,surfacePlaneEW!=NULL,surfacePlaneEH!=NULL,surfacePlaneRow(surface,i)*surfacePlaneHeight(surface,i),available,declared);
  }
  int fd=gpuCopyFD?gpuCopyFD(surface):-1;
  printf("SF_GPU fd-api=%d copied-fd-valid=%d offset-api=%d offset=%zu\n",gpuCopyFD!=NULL,fd>=0,gpuOffset!=NULL,gpuOffset?gpuOffset(surface):0);
  if(fd>=0)close(fd);
 }
 result=unlockBuffer(buffer,1);printf("UNLOCK status=%d\n",result);releaseBuffer(buffer);return result!=0;
}
int main(void) {
 void *cf=dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation",RTLD_NOW|RTLD_GLOBAL);
 if(!cf){fputs("SURFACE_LAYOUT CoreFoundation load failed\n",stderr);return 2;}
 REQUIRED(cf,cfDictionaryCreate,"CFDictionaryCreate");REQUIRED(cf,cfArrayGetTypeID,"CFArrayGetTypeID");REQUIRED(cf,cfArrayGetCount,"CFArrayGetCount");REQUIRED(cf,cfArrayGetValueAtIndex,"CFArrayGetValueAtIndex");
 REQUIRED(cf,cfDictionaryGetTypeID,"CFDictionaryGetTypeID");REQUIRED(cf,cfDictionaryGetValue,"CFDictionaryGetValue");REQUIRED(cf,cfNumberGetTypeID,"CFNumberGetTypeID");REQUIRED(cf,cfNumberGetValue,"CFNumberGetValue");REQUIRED(cf,cfGetTypeID,"CFGetTypeID");REQUIRED(cf,cfRelease,"CFRelease");
 REQUIRED(cf,cfTrue,"kCFBooleanTrue");REQUIRED(cf,cfKeyCallbacks,"kCFTypeDictionaryKeyCallBacks");REQUIRED(cf,cfValueCallbacks,"kCFTypeDictionaryValueCallBacks");
 void *cv=dlopen("/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo",RTLD_NOW|RTLD_GLOBAL);
 void *io=dlopen("/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface",RTLD_NOW|RTLD_GLOBAL);
 if(!cv||!io){fputs("SURFACE_LAYOUT framework load failed\n",stderr);return 2;}
 REQUIRED(cv,createBuffer,"CVPixelBufferCreate");REQUIRED(cv,lockBuffer,"CVPixelBufferLockBaseAddress");REQUIRED(cv,unlockBuffer,"CVPixelBufferUnlockBaseAddress");REQUIRED(cv,releaseBuffer,"CVPixelBufferRelease");
 REQUIRED(cv,bufferWidth,"CVPixelBufferGetWidth");REQUIRED(cv,bufferHeight,"CVPixelBufferGetHeight");REQUIRED(cv,bufferRow,"CVPixelBufferGetBytesPerRow");REQUIRED(cv,bufferSize,"CVPixelBufferGetDataSize");REQUIRED(cv,bufferPlanes,"CVPixelBufferGetPlaneCount");REQUIRED(cv,bufferBase,"CVPixelBufferGetBaseAddress");REQUIRED(cv,bufferSurface,"CVPixelBufferGetIOSurface");
 REQUIRED(cv,bufferPlaneWidth,"CVPixelBufferGetWidthOfPlane");REQUIRED(cv,bufferPlaneHeight,"CVPixelBufferGetHeightOfPlane");REQUIRED(cv,bufferPlaneRow,"CVPixelBufferGetBytesPerRowOfPlane");REQUIRED(cv,bufferPlaneBase,"CVPixelBufferGetBaseAddressOfPlane");
 REQUIRED(io,surfaceSize,"IOSurfaceGetAllocSize");REQUIRED(io,surfacePlanes,"IOSurfaceGetPlaneCount");REQUIRED(io,surfaceWidth,"IOSurfaceGetWidth");REQUIRED(io,surfaceHeight,"IOSurfaceGetHeight");REQUIRED(io,surfaceRow,"IOSurfaceGetBytesPerRow");REQUIRED(io,surfaceBase,"IOSurfaceGetBaseAddress");
 REQUIRED(io,surfacePlaneWidth,"IOSurfaceGetWidthOfPlane");REQUIRED(io,surfacePlaneHeight,"IOSurfaceGetHeightOfPlane");REQUIRED(io,surfacePlaneRow,"IOSurfaceGetBytesPerRowOfPlane");REQUIRED(io,surfacePlaneBase,"IOSurfaceGetBaseAddressOfPlane");
 REQUIRED(cv,surfacePropertiesKey,"kCVPixelBufferIOSurfacePropertiesKey");REQUIRED(io,isGlobalKey,"kIOSurfaceIsGlobal");
 OPTIONAL(io,surfaceBPE,"IOSurfaceGetBytesPerElement");OPTIONAL(io,surfaceEW,"IOSurfaceGetElementWidth");OPTIONAL(io,surfaceEH,"IOSurfaceGetElementHeight");
 OPTIONAL(io,surfacePlaneBPE,"IOSurfaceGetBytesPerElementOfPlane");OPTIONAL(io,surfacePlaneEW,"IOSurfaceGetElementWidthOfPlane");OPTIONAL(io,surfacePlaneEH,"IOSurfaceGetElementHeightOfPlane");
 OPTIONAL(io,surfaceCopyValue,"IOSurfaceCopyValue");OPTIONAL(io,planeInfoKey,"kIOSurfacePlaneInfo");OPTIONAL(io,planeSizeKey,"kIOSurfacePlaneSize");
 OPTIONAL(io,gpuCopyFD,"IOSurfaceSFGPUCopyFD");OPTIONAL(io,gpuOffset,"IOSurfaceSFGPUGetOffset");
 printf("SURFACE_LAYOUT own-declarations=1 metadata-only=1 absent-offset=-1 plane-size-derived-is-allocation=0\n");
 int failed=0;
 for(unsigned f=0;f<2;f++)for(unsigned n=0;n<2;n++)for(unsigned s=0;s<2;s++)failed|=run(n?1920:16,n?1080:8,f?UINT32_C(0x34323066):UINT32_C(0x34323076),f?"420f":"420v",s);
 dlclose(cv);dlclose(io);dlclose(cf);return failed;
}
