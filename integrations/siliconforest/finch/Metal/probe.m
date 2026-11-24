/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
extern id MTLCreateSystemDefaultDevice(void);
extern NSArray *MTLCopyAllDevices(void);
extern NSArray *MTLCopyAllDevicesWithObserver(id *,void (^)(id,NSString *));
extern void MTLRemoveDeviceObserver(id);
int main(int argc,char **argv){@autoreleasepool{
    BOOL disabled=argc==2&&!strcmp(argv[1],"--require-disabled");
    if(disabled)unsetenv("SF_METAL_ENABLE_REAL_DEVICE");
    id device=MTLCreateSystemDefaultDevice();
    NSArray *list=MTLCopyAllDevices();id observer=nil;
    NSArray *observed=MTLCopyAllDevicesWithObserver(&observer,^(id changed,NSString *name){(void)changed;(void)name;});
    printf("METAL device=%d listed=%lu observed=%lu observer=%d\n",device!=nil,(unsigned long)list.count,(unsigned long)observed.count,observer!=nil);
    if(disabled && (device || list.count || observed.count || observer))return 1;
    [device release];[list release];[observed release];MTLRemoveDeviceObserver(observer);
    if(disabled){
        const char *negative[]={"0","true","yes","01",""};
        for(unsigned n=0;n<sizeof(negative)/sizeof(*negative);n++){
            setenv("SF_METAL_ENABLE_REAL_DEVICE",negative[n],1);
            device=MTLCreateSystemDefaultDevice();list=MTLCopyAllDevices();
            if(device||list.count)return 2;
            [list release];
        }
        puts("METAL disabled-default-and-invalid-opt-in PASS");
    }
    return 0;
}}
