/* SPDX-License-Identifier: MIT */
/* Included after the private VT session structure. No IOSurface/CGL fork: the
 * GPU allocator is the existing FX provider, detected through its private API. */
#include <IOSurface/IOSurface.h>
#include <sfavd/sfavd_ioctl.h>
#include <dlfcn.h>
#include <unistd.h>
#include "lease.h"
extern const CFStringRef kIOSurfaceAllocSize,kIOSurfaceIsGlobal;
static int (*sfGPUCopyFD)(IOSurfaceRef);
static size_t (*sfGPUOffset)(IOSurfaceRef);
static CVReturn (*sfSetLifetime)(CVPixelBufferRef,CFTypeRef);
static pthread_once_t sfGPUOnce=PTHREAD_ONCE_INIT;
static void sf_gpu_symbols(void) {
 sfGPUCopyFD=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUCopyFD");
 sfGPUOffset=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUGetOffset");
 sfSetLifetime=dlsym(RTLD_DEFAULT,"CVPixelBufferSFSetLifetimeObject");
}
static int sf_gpu_number(CFMutableDictionaryRef d,CFStringRef key,uint64_t value){
 int64_t v=(int64_t)value;CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&v);
 if(!n)return -1;CFDictionarySetValue(d,key,n);CFRelease(n);return 0;
}
static void sf_gpu_free(AVPDAVDExternalBuffer *buffer){
 if(buffer->opaque)CFRelease(buffer->opaque);
 memset(buffer,0,sizeof(*buffer));
}
static int sf_gpu_allocate(void *opaque,const AVPDAVDFramesContext *f,
                            AVPDAVDExternalBuffer *buffers,unsigned count){
 VTDecompressionSessionRef s=opaque;CMVideoDimensions visible=CMVideoFormatDescriptionGetDimensions(s->format);
 if(!s->zeroRequested||!sfGPUCopyFD||!sfGPUOffset||!sfSetLifetime||
    f->pixelformat!=SFAVD_PIX_NV12||count>32||visible.width<=0||visible.height<=0||
    (uint32_t)visible.width>f->width||(uint32_t)visible.height>f->height||
    f->bytesperline<(uint32_t)visible.width||f->uv_offset<(uint64_t)f->bytesperline*visible.height||
    !f->sizeimage||f->sizeimage>UINT32_C(134217728))return -1;
 uint64_t bytes=((uint64_t)f->sizeimage+16383)&~UINT64_C(16383);
 uint64_t uvEnd=(uint64_t)f->uv_offset+(uint64_t)f->bytesperline*((visible.height+1)/2);
 if(uvEnd>bytes)return -1;
 CFTypeRef original=s->surface?CFDictionaryGetValue(s->surface,kCVPixelBufferIOSurfacePropertiesKey):NULL;
 if(!original||CFGetTypeID(original)!=CFDictionaryGetTypeID()||
    CFDictionaryGetValue((CFDictionaryRef)original,kIOSurfaceIsGlobal)!=kCFBooleanTrue)return -1;
 for(unsigned i=0;i<count;i++){
  CFMutableDictionaryRef p=CFDictionaryCreateMutableCopy(NULL,0,(CFDictionaryRef)original);
  CFMutableArrayRef planes=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);
  if(!p||!planes){if(p)CFRelease(p);if(planes)CFRelease(planes);return -1;}
  int failed=sf_gpu_number(p,kIOSurfaceWidth,visible.width)||sf_gpu_number(p,kIOSurfaceHeight,visible.height)||
   sf_gpu_number(p,kIOSurfacePixelFormat,s->avctx->color_range==AVCOL_RANGE_JPEG?0x34323066:0x34323076)||
   sf_gpu_number(p,kIOSurfaceBytesPerRow,f->bytesperline)||sf_gpu_number(p,kIOSurfaceAllocSize,bytes);
  for(unsigned plane=0;plane<2;plane++){
   CFMutableDictionaryRef q=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
   if(!q){failed=1;break;}
   uint64_t width=plane?(visible.width+1)/2:visible.width,height=plane?(visible.height+1)/2:visible.height;
   failed|=sf_gpu_number(q,kIOSurfacePlaneWidth,width)||sf_gpu_number(q,kIOSurfacePlaneHeight,height)||
    sf_gpu_number(q,kIOSurfacePlaneBytesPerRow,f->bytesperline)||sf_gpu_number(q,kIOSurfacePlaneOffset,plane?f->uv_offset:0)||
    sf_gpu_number(q,kIOSurfacePlaneSize,(uint64_t)f->bytesperline*height)||
    sf_gpu_number(q,kIOSurfacePlaneBytesPerElement,plane?2:1)||sf_gpu_number(q,kIOSurfacePlaneElementWidth,1);
   CFArrayAppendValue(planes,q);CFRelease(q);
  }
  CFDictionarySetValue(p,kIOSurfacePlaneInfo,planes);CFRelease(planes);
  IOSurfaceRef surface=failed?NULL:IOSurfaceCreate(p);CFRelease(p);
  if(!surface)return -1;
  int fd=sfGPUCopyFD(surface);void *data=IOSurfaceGetBaseAddress(surface);
  int valid=fd>=0&&sfGPUOffset(surface)==16384&&data&&!((uintptr_t)data&16383)&&
      IOSurfaceGetID(surface)!=0&&IOSurfaceGetAllocSize(surface)>=bytes&&IOSurfaceGetPlaneCount(surface)==2&&
      IOSurfaceGetWidth(surface)==(size_t)visible.width&&IOSurfaceGetHeight(surface)==(size_t)visible.height&&
      IOSurfaceGetBytesPerRowOfPlane(surface,0)==f->bytesperline&&IOSurfaceGetBytesPerRowOfPlane(surface,1)==f->bytesperline&&
      IOSurfaceGetBaseAddressOfPlane(surface,0)==data&&
      IOSurfaceGetBaseAddressOfPlane(surface,1)==(uint8_t*)data+f->uv_offset&&
      IOSurfaceGetWidthOfPlane(surface,0)==(size_t)visible.width&&IOSurfaceGetHeightOfPlane(surface,0)==(size_t)visible.height&&
      IOSurfaceGetWidthOfPlane(surface,1)==(size_t)(visible.width+1)/2&&
      IOSurfaceGetHeightOfPlane(surface,1)==(size_t)(visible.height+1)/2;
  if(fd>=0)close(fd);
  if(!valid){CFRelease(surface);return -1;}
  buffers[i]=(AVPDAVDExternalBuffer){.data=data,.size=bytes,.opaque=surface};
 }
 return 0;
}
static void sf_gpu_modes(VTDecompressionSessionRef s){
 const char *mode=getenv("SF_VT_ZERO_COPY");
 s->zeroRequested=mode&&(!strcmp(mode,"1")||!strcmp(mode,"require"));
 s->zeroRequired=mode&&!strcmp(mode,"require");
}
static int sf_gpu_configure(VTDecompressionSessionRef s,AVPDAVDDeviceContext *device){
 if(!s->zeroRequested)return 0;
 pthread_once(&sfGPUOnce,sf_gpu_symbols);
 int available=s->surface&&sfGPUCopyFD&&sfGPUOffset&&sfSetLifetime&&
   (device->capabilities&SFAVD_CAP_EXTERNAL_BUFFERS)&&
   s->wanted!=kCVPixelFormatType_420YpCbCr8Planar&&s->wanted!=kCVPixelFormatType_420YpCbCr8PlanarFullRange;
 fprintf(stderr,"SF_VT_GPU requested=1 required=%d available=%d kernel-cap=%d cv-lifetime=%d\n",
  (int)s->zeroRequired,available,!!(device->capabilities&SFAVD_CAP_EXTERNAL_BUFFERS),!!sfSetLifetime);
 if(!available)return s->zeroRequired?-1:0;
 const char *retire=getenv("SF_VT_RETIRE_POOL_AT_INVALIDATE");
 if(retire&&!strcmp(retire,"1")&&!s->captureGroup){
  s->captureGroup=SFVTFrameLeaseGroupCreate();if(!s->captureGroup)return -1;
 }
 device->allocate_external=sf_gpu_allocate;device->free_external=sf_gpu_free;device->external_opaque=s;
 return 0;
}
static CVPixelBufferRef sf_gpu_image(VTDecompressionSessionRef s,AVFrame *frame){
 if(!s->zeroRequested||!frame->hw_frames_ctx||!sfSetLifetime)return NULL;
 AVHWFramesContext *hw=(AVHWFramesContext*)frame->hw_frames_ctx->data;
 AVPDAVDFramesContext *layout=hw->hwctx;
 AVPDAVDFrame *picture=(AVPDAVDFrame*)frame->data[0];
 if(!layout->external_buffers||!picture||!picture->external||hw->sw_format!=AV_PIX_FMT_NV12||
    frame->crop_left||frame->crop_top)return NULL;
 IOSurfaceRef surface=picture->external;
 int width=frame->width-(int)frame->crop_right,height=frame->height-(int)frame->crop_bottom;
 uint32_t format=frame->color_range==AVCOL_RANGE_JPEG?0x34323066:0x34323076;
 if(width<=0||height<=0||IOSurfaceGetWidth(surface)!=(size_t)width||IOSurfaceGetHeight(surface)!=(size_t)height||
    IOSurfaceGetPixelFormat(surface)!=format)return NULL;
 /* Hardware completion precedes this callback. Publish a fresh content seed
  * without touching pixel bytes; a bare Unlock would unbalance lockCount. */
 if(IOSurfaceLock(surface,0,NULL))return NULL;
 if(IOSurfaceUnlock(surface,0,NULL))return NULL;
 CFTypeRef lease=SFVTCreateGroupedFrameLease(frame,surface,s->captureGroup);if(!lease)return NULL;
 CVPixelBufferRef image=NULL;
 if(CVPixelBufferCreateWithIOSurface(NULL,surface,NULL,&image)||!image||sfSetLifetime(image,lease)){
  if(image)CVPixelBufferRelease(image);CFRelease(lease);return NULL;
 }
 CFRelease(lease);return image;
}
