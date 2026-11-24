#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a GPU-storage/held-frame control from our existing public VT decoder.

Only the test client changes. Decoder hashes keep their existing format. This
requires actual GPU-backed output; it is not a CPU-copy performance benchmark.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
def change(old,new):
 global s
 if s.count(old)!=1:raise ValueError('decoder fixture anchor changed: '+old[:80])
 s=s.replace(old,new)
change('#include <time.h>','#include <time.h>\n#include <dlfcn.h>\n#include <unistd.h>\n#include <sys/sysctl.h>\n#include <IOSurface/IOSurface.h>')
change('static int64_t g_last_pts = INT64_MIN;', '''#include "final-gpu-picture.h"
static int64_t g_last_pts = INT64_MIN;
static CVPixelBufferRef held;
static char heldHash[33];
static unsigned gpuFrames,heldChecks;
static int holdFinal,gpuFinal;
static CVPixelBufferRef finalHeld;
static char finalHash[33];
static void poolSnapshot(const char *phase) {
 char text[4096]={0};size_t bytes=sizeof(text)-1;
 if(sysctlbyname("kern.sf_avd.stats",text,&bytes,NULL,0)){
  printf("CAPTURE_FAIL pool stats unavailable\\n");g_errors++;return;
 }
 text[sizeof(text)-1]=0;printf("CAPTURE_POOL phase=%s %s\\n",phase,text);
}
static void visibleHash(CVPixelBufferRef image,char hex[33]) {
 struct sf_md5 md5;sf_md5_init(&md5);
 size_t width=CVPixelBufferGetWidth(image),height=CVPixelBufferGetHeight(image);
 CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
 for(unsigned plane=0;plane<2;plane++){
  const uint8_t *data=CVPixelBufferGetBaseAddressOfPlane(image,plane);
  size_t stride=CVPixelBufferGetBytesPerRowOfPlane(image,plane);
  size_t rows=plane?(height+1)/2:height,bytes=plane?(width+1)&~(size_t)1:width;
  for(size_t row=0;row<rows;row++)sf_md5_update(&md5,data+row*stride,bytes);
 }
 CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);sf_md5_final(&md5,hex);
}''')
change('    // the refCon of each sample is its own presentation time:', '''    IOSurfaceRef surface=CVPixelBufferGetIOSurface(image);
    int (*copyFD)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUCopyFD");
    size_t (*offset)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUGetOffset");
    int fd=surface&&copyFD?copyFD(surface):-1;
    int valid=fd>=0&&offset&&offset(surface)==16384&&IOSurfaceGetPlaneCount(surface)==2;
    if(fd>=0)close(fd);
    if(!valid){puts("CAPTURE_FAIL actual GPU-backed two-plane surface required");g_errors++;return;}
    gpuFrames++;
    // the refCon of each sample is its own presentation time:''')
change('    g_frames++;', '''    if(!held&&!g_frames){held=CVPixelBufferRetain(image);memcpy(heldHash,hex,sizeof(heldHash));}
    if(held&&g_frames==64){
        char hash[33];visibleHash(held,hash);heldChecks++;
        if(strcmp(hash,heldHash)){puts("CAPTURE_FAIL held picture overwritten");g_errors++;}
        else puts("CAPTURE_HELD stable-after-64-frames=1");
        CVPixelBufferRelease(held);held=NULL;
    }
    if(holdFinal){
        if(finalHeld)CVPixelBufferRelease(finalHeld);
        finalHeld=CVPixelBufferRetain(image);memcpy(finalHash,hex,sizeof(finalHash));
    }
    g_frames++;''')
change('int main(int argc, char **argv)\n{','''int main(int argc, char **argv)
{
    const char *hold=getenv("SF_CAPTURE_HOLD_FINAL");holdFinal=hold&&!strcmp(hold,"1");
    const char *gpu=getenv("SF_CAPTURE_GPU_FINAL");gpuFinal=gpu&&!strcmp(gpu,"1");
    if(gpuFinal&&!holdFinal){puts("CAPTURE_FAIL GPU control requires held picture");return 1;}''')
change('    VTDecompressionSessionInvalidate(session);','''    if(gpuFinal&&(!finalHeld||sfFinalGPUPrepare(finalHeld))){
        puts("CAPTURE_FAIL real GPU final-picture preparation");g_errors++;gpuFinal=0;
    }
    if(holdFinal)poolSnapshot("before-invalidate");
    VTDecompressionSessionInvalidate(session);
    if(holdFinal){
        poolSnapshot("after-invalidate-held");
        char hash[33]={0};
        if(finalHeld)visibleHash(finalHeld,hash);
        IOSurfaceRef storage=finalHeld?CVPixelBufferGetIOSurface(finalHeld):NULL;
        int (*copyFD)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUCopyFD");
        int fd=storage&&copyFD?copyFD(storage):-1;
        if(!finalHeld||strcmp(hash,finalHash)||fd<0){puts("CAPTURE_FAIL final GPU picture lost after invalidation");g_errors++;}
        else puts("CAPTURE_INVALIDATED held-visible-pixels-stable=1 GPU-handle-alive=1 real-GPU-consumer-fence=0");
        if(fd>=0)close(fd);
        if(finalHeld){CVPixelBufferRelease(finalHeld);finalHeld=NULL;}
        usleep(30000);poolSnapshot("after-picture-release");
        if(gpuFinal&&sfFinalGPUVerify(finalHash)){
            puts("CAPTURE_FAIL real GPU pixels after invalidation/release");g_errors++;
        }
    }''')
change('    err = VTDecompressionSessionCreate(NULL, format, spec, attributes, &record, &session);', '''    const CFStringRef *global=dlsym(RTLD_DEFAULT,"kIOSurfaceIsGlobal");
    if(!global||!*global){puts("CAPTURE_FAIL missing global surface key");return 1;}
    CFMutableDictionaryRef surfaceProperties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(surfaceProperties,*global,kCFBooleanTrue);
    CFDictionarySetValue(attributes,kCVPixelBufferIOSurfacePropertiesKey,surfaceProperties);
    CFRelease(surfaceProperties);
    err = VTDecompressionSessionCreate(NULL, format, spec, attributes, &record, &session);''')
change('    return g_errors || g_frames == 0 ? 1 : 0;', '''    if(held){CVPixelBufferRelease(held);held=NULL;}
    printf("CAPTURE_SUMMARY GPU-frames=%u held-checks=%u errors=%llu\\n",gpuFrames,heldChecks,(unsigned long long)g_errors);
    return g_errors || g_frames<65 || gpuFrames!=g_frames || heldChecks!=1 ? 1 : 0;''')
p.write_text(s)
