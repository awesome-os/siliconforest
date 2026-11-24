/* SPDX-License-Identifier: MIT */
/* Pure CPU, float bitmap paths; no view, output device, texture or pixel buffer. */
#import <CoreImage/CoreImage.h>
#import <CoreImage/CIFilterBuiltins.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
@interface CIFilter (SFProbeTypedConstructor)
+ (CIFilter<CIColorControls> *)colorControlsFilter;
@end
static void crashed(int sig) {
    void *frames[32];
    int (*bt)(void **,int)=dlsym(RTLD_DEFAULT,"backtrace");
    void (*symbols)(void *const *,int,int)=dlsym(RTLD_DEFAULT,"backtrace_symbols_fd");
    if (bt && symbols) symbols(frames,bt(frames,32),2);
    _exit(128+sig);
}
static void phase(const char *name) {fprintf(stderr,"phase %s\n",name);}
static void require(BOOL ok,const char *why) { if (!ok) {fprintf(stderr,"FAIL: %s\n",why);exit(1);} }
static void render(CIContext *context,CIImage *image,const char *label,const float expected[4]) {
    float out[4]={0};require(image!=nil,"filter has output");
    [context render:image toBitmap:out rowBytes:sizeof out bounds:CGRectMake(0,0,1,1) format:kCIFormatRGBAf colorSpace:NULL];
    for (int i=0;i<4;i++)require(isfinite(out[i]) && fabsf(out[i]-expected[i])<0.00001f,"float bitmap result");
    printf("%s %.6f %.6f %.6f %.6f\n",label,out[0],out[1],out[2],out[3]);
}
int main(void) { signal(SIGSEGV,crashed);setvbuf(stdout,NULL,_IOLBF,0);@autoreleasepool {
    const float px[4]={0.25f,0.5f,0.75f,1};
    NSData *data=[NSData dataWithBytes:px length:sizeof px];
    phase("image");
    CIImage *image=[CIImage imageWithBitmapData:data bytesPerRow:sizeof px size:CGSizeMake(1,1) format:kCIFormatRGBAf colorSpace:NULL];
    phase("context");
    CIContext *context=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES,kCIContextWorkingColorSpace:[NSNull null],kCIContextOutputColorSpace:[NSNull null]}];
    require(context && image,"CPU image/context");render(context,image,"identity",px);
    CIFilter *invert=[CIFilter filterWithName:@"CIColorInvert"];[invert setValue:image forKey:kCIInputImageKey];
    const float inv[4]={.75f,.5f,.25f,1};render(context,invert.outputImage,"invert",inv);
    CIFilter *exposure=[CIFilter filterWithName:@"CIExposureAdjust"];[exposure setValue:image forKey:kCIInputImageKey];[exposure setValue:@1 forKey:@"inputEV"];
    const float exp[4]={.5f,1,1.5f,1};render(context,exposure.outputImage,"exposure",exp);
    /* Apple typed constructors and Finch's documented legacy typed dispatch. */
    CIFilter<CIColorControls> *controls=[CIFilter respondsToSelector:@selector(colorControlsFilter)] ? [CIFilter colorControlsFilter] : (id)[CIFilter filterWithName:@"CIColorControls"];controls.inputImage=image;controls.saturation=1.0f;controls.brightness=0.0f;controls.contrast=1.0f;
    require(controls.saturation==1.0f && controls.brightness==0.0f && controls.contrast==1.0f,"typed float protocol accessors");render(context,controls.outputImage,"typed-controls",px);
    CIFilter *matrix=[CIFilter filterWithName:@"CIColorMatrix"];[matrix setValue:image forKey:kCIInputImageKey];
    [matrix setValue:[CIVector vectorWithX:0 Y:1 Z:0 W:0] forKey:@"inputRVector"];
    [matrix setValue:[CIVector vectorWithX:1 Y:0 Z:0 W:0] forKey:@"inputGVector"];
    const float swap[4]={.5f,.25f,.75f,1};render(context,matrix.outputImage,"matrix",swap);
    /* Padded input rows, signed/HDR components, alpha and lower-left cropping. */
    const float grid[24]={.25f,.5f,.75f,1, 1.5f,-.25f,.5f,1, 0,0,0,0,
                          .125f,.25f,.375f,.5f, 0,0,0,0, 0,0,0,0};
    CIImage *two=[CIImage imageWithBitmapData:[NSData dataWithBytes:grid length:sizeof grid] bytesPerRow:12*sizeof(float) size:CGSizeMake(2,2) format:kCIFormatRGBAf colorSpace:NULL];
    float whole[16]={0};[context render:two toBitmap:whole rowBytes:8*sizeof(float) bounds:CGRectMake(0,0,2,2) format:kCIFormatRGBAf colorSpace:NULL];
    for (int y=0;y<2;y++)for(int i=0;i<8;i++)require(fabsf(whole[y*8+i]-grid[y*12+i])<0.00001f,"padded rows/HDR/alpha");
    printf("rows stride=48 extent=2x2 signed-HDR-alpha=1\n");
    CIImage *crop=[two imageByCroppingToRect:CGRectMake(0,0,1,1)];
    const float alpha[4]={.125f,.25f,.375f,.5f};render(context,crop,"crop-alpha",alpha);
    [invert setValue:crop forKey:kCIInputImageKey];const float ia[4]={.375f,.25f,.125f,.5f};render(context,invert.outputImage,"invert-alpha",ia);
    const _Float16 half[4]={.25f,.5f,.75f,1};
    CIImage *halfImage=[CIImage imageWithBitmapData:[NSData dataWithBytes:half length:sizeof half] bytesPerRow:sizeof half size:CGSizeMake(1,1) format:kCIFormatRGBAh colorSpace:NULL];
    render(context,halfImage,"half",px);
    printf("PASS: CPU float/half recipes, rows/alpha/HDR, typed filter properties\n");
} return 0; }
