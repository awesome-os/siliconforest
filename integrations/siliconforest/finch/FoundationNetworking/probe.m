/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
static void need(BOOL condition,const char *message){if(!condition){fprintf(stderr,"NETWORK_FAIL %s\n",message);exit(1);}}
int main(int argc,char **argv){@autoreleasepool{
    BOOL native=argc>=3&&!strcmp(argv[1],"--native");
    need(native||(argc>=4&&!strcmp(argv[1],"--prefix")),"--native URL | --prefix dylib URL [--expect-error]");
    int urlIndex=native?2:3;BOOL reject=argc>urlIndex+1&&!strcmp(argv[urlIndex+1],"--expect-error");
    if(!native){void *library=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);if(!library)fprintf(stderr,"NETWORK_LOAD %s\n",dlerror());need(library!=NULL,"load isolated networking");}
    Class configClass=objc_getClass(native?"NSURLSessionConfiguration":"SFNSURLSessionConfiguration");
    Class sessionClass=objc_getClass(native?"NSURLSession":"SFNSURLSession");
    need(configClass&&sessionClass,"session classes");
    id config=[configClass ephemeralSessionConfiguration];
    [config setTimeoutIntervalForRequest:10];
    id session=[sessionClass sessionWithConfiguration:config];
    need(session!=nil,"ephemeral session");
    _Atomic int done=0;_Atomic int *ready=&done;
    __block NSData *result=nil;__block NSURLResponse *response=nil;__block NSError *failure=nil;
    id task=[session dataTaskWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:argv[urlIndex]]]
       completionHandler:^(NSData *data,NSURLResponse *reply,NSError *error){result=[data retain];response=[reply retain];failure=[error retain];atomic_store(ready,1);}];
    need(task!=nil,"data task");[task resume];
    for(int n=0;n<2000&&!atomic_load(&done);n++)usleep(10000);
    need(atomic_load(&done),"completion deadline");
    printf("NETWORK_BACKEND %s\n",native?"native":"isolated-finch");
    if(reject){printf("NETWORK rejected=%d code=%ld bytes=%lu\n",failure!=nil,(long)[failure code],(unsigned long)[result length]);need(failure!=nil&&[result length]==0,"certificate rejection");}
    else{
        if(failure)fprintf(stderr,"NETWORK_ERROR %s\n",[[failure description] UTF8String]);
        need(!failure&&[response respondsToSelector:@selector(statusCode)],"HTTP response without error");
        NSInteger status=[(NSHTTPURLResponse *)response statusCode];
        NSString *text=[[[NSString alloc] initWithData:result encoding:NSUTF8StringEncoding] autorelease];
        BOOL example=[text rangeOfString:@"Example Domain"].location!=NSNotFound;
        printf("NETWORK status=%ld bytes=%lu example-domain=%d\n",(long)status,(unsigned long)[result length],example);
        need(status==200&&example,"HTTPS example content");
    }
    [session finishTasksAndInvalidate];[result release];[response release];[failure release];return 0;
}}
