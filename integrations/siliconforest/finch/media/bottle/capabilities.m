/* SPDX-License-Identifier: MIT */
/* M1-only absence/provider assertions: no request for access, input or GUI. */
#import "capture.h"
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
extern id MTLCreateSystemDefaultDevice(void);
extern NSString *const kCIInputImageKey;
int main(void) { @autoreleasepool {
    NSArray *devices=[AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    AVCaptureDeviceDiscoverySession *discovery=[AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeExternal] mediaType:AVMediaTypeVideo position:0];
    AVCaptureSession *session=[[AVCaptureSession alloc] init];
    [session startRunning];
    AVCaptureScreenInput *screen=[[AVCaptureScreenInput alloc] initWithDisplayID:0];
    printf("capture devices=%lu discovery=%lu running=%d screen-input=%s\n",
        (unsigned long)devices.count,(unsigned long)discovery.devices.count,
        session.isRunning,screen?"unexpected":"nil");
    if(devices.count || discovery.devices.count || session.isRunning || screen)return 1;
    id device=MTLCreateSystemDefaultDevice();
    printf("metal device=%s\n",device?"unexpected":"nil");
    if(device)return 2;
    void *provider=dlopen("/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore",RTLD_NOW|RTLD_LOCAL);
    if(!provider)return 3;
    const char *names[]={"CIColor","CIContext","CIFilter","CIImage","CIVector"};
    int matched=0;
    for(int i=0;i<5;i++){
        char name[80];snprintf(name,sizeof(name),"OBJC_CLASS_$_%s",names[i]);
        Class cls=objc_getClass(names[i]);
        if(cls && cls==dlsym(provider,name))matched++;
    }
    BOOL key=[kCIInputImageKey isEqualToString:@"inputImage"];
    printf("CoreImage retained-class-identities=%d/5 input-key=%d\n",matched,key);
    dlclose(provider);
    return matched==5 && key?0:4;
}}
