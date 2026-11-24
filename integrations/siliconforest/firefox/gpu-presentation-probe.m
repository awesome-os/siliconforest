/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
@interface CALayer:NSObject
+(id)layer;
@property CGRect frame;
@property(retain) id contents;
@property CGColorRef backgroundColor;
-(void)addSublayer:(id)layer;
@end
@interface CATransaction:NSObject
+(void)begin;+(void)commit;
@end
static void pump(double seconds){
 NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
 while([end timeIntervalSinceNow]>0){
  NSDate *tick=[NSDate dateWithTimeIntervalSinceNow:.02];
  NSEvent *event=[NSApp nextEventMatchingMask:NSAnyEventMask untilDate:tick inMode:NSDefaultRunLoopMode dequeue:YES];
  if(event)[NSApp sendEvent:event];[NSApp updateWindows];
 }
}
static int capture(NSString *directory,NSString *phase,NSString *marker){
 NSString *script=[directory stringByAppendingPathComponent:@"capture.py"];
 char *args[]={"/usr/bin/env","python3",(char *)[script UTF8String],(char *)[phase UTF8String],(char *)[marker UTF8String],NULL};
 pid_t pid=0;int status=0;if(posix_spawn(&pid,args[0],NULL,NULL,args,environ))return 1;
 for(int i=0;i<400;i++){pid_t got=waitpid(pid,&status,WNOHANG);if(got==pid)return WIFEXITED(status)?WEXITSTATUS(status):1;pump(.05);}
 kill(pid,SIGKILL);for(int i=0;i<20;i++){if(waitpid(pid,&status,WNOHANG)==pid)break;pump(.05);}return 1;
}
#define LOAD(ret,name,args) ret (*name) args=dlsym(gl,#name);if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}
int main(int argc,char **argv){@autoreleasepool{
 setvbuf(stdout,NULL,_IOLBF,0);if(argc!=3&&argc!=4)return 2;
 int adopt=argc==4&&!strcmp(argv[3],"--adopt");if(argc==4&&!adopt)return 2;
 int require=atoi(argv[2]);NSString *directory=[NSString stringWithUTF8String:argv[1]];
 [NSApplication sharedApplication];
 void *gl=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_GLOBAL);
 void *io=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",RTLD_NOW|RTLD_GLOBAL);
 if(!gl||!io)return 2;
 LOAD(int,CGLChoosePixelFormat,(const int *,void **,int *));LOAD(int,CGLCreateContext,(void *,void *,void **));
 LOAD(int,CGLSetCurrentContext,(void *));LOAD(void *,CGLGetCurrentContext,(void));
 LOAD(int,CGLDestroyContext,(void *));LOAD(int,CGLDestroyPixelFormat,(void *));
 LOAD(int,CGLTexImageIOSurface2D,(void *,unsigned,unsigned,int,int,unsigned,unsigned,void *,unsigned));
 LOAD(void,glGenTextures,(int,unsigned *));LOAD(void,glBindTexture,(unsigned,unsigned));LOAD(void,glDeleteTextures,(int,const unsigned *));
 LOAD(void,glGenFramebuffers,(int,unsigned *));LOAD(void,glBindFramebuffer,(unsigned,unsigned));LOAD(void,glDeleteFramebuffers,(int,const unsigned *));
 LOAD(void,glFramebufferTexture2D,(unsigned,unsigned,unsigned,unsigned,int));LOAD(unsigned,glCheckFramebufferStatus,(unsigned));
 LOAD(void,glClearColor,(float,float,float,float));LOAD(void,glClear,(unsigned));LOAD(void,glFinish,(void));LOAD(unsigned,glGetError,(void));
 LOAD(void,glEnable,(unsigned));LOAD(void,glDisable,(unsigned));LOAD(void,glScissor,(int,int,int,int));
 void (*stats)(uint64_t[4])=dlsym(gl,"CGLSFFXTransferStats");
 void *(*create)(CFDictionaryRef)=dlsym(io,"IOSurfaceCreate");if(!create)return 2;
 CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFStringRef keys[]={CFSTR("IOSurfaceWidth"),CFSTR("IOSurfaceHeight"),CFSTR("IOSurfaceBytesPerElement"),CFSTR("IOSurfacePixelFormat")};int values[]={32,32,4,0x42475241};
 for(int i=0;i<4;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(props,keys[i],n);CFRelease(n);}
 void *surface=create(props);CFRelease(props);if(!surface)return 2;
 int attributes[]={99,0x3200,73,96,0},count=0;void *format=NULL,*writer=NULL;
 int status=CGLChoosePixelFormat(attributes,&format,&count);if(!status)status=CGLCreateContext(format,NULL,&writer);if(!status)status=CGLSetCurrentContext(writer);if(status)return 3;
 unsigned texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(0x0de1,texture);
 status=CGLTexImageIOSurface2D(writer,0x0de1,0x1908,32,32,0x80e1,0x8367,surface,0);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(0x8d40,fbo);glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,texture,0);
 if(status||glCheckFramebufferStatus(0x8d40)!=0x8cd5)return 3;
 glClearColor(0,1,0,1);glClear(0x4000);
 glEnable(0x0c11);glScissor(0,0,32,16);glClearColor(1,0,0,1);glClear(0x4000);glDisable(0x0c11);
 glFinish();if(glGetError())return 3;CGLSetCurrentContext(NULL);
 NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(60,60,96,64) styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
 [window setReleasedWhenClosed:NO];[window setBackgroundColor:[NSColor redColor]];
 NSString *marker=[NSString stringWithFormat:@"SF-FX GPU native %d",getpid()];[window setTitle:marker];
 CALayer *root=[CALayer layer],*left=[CALayer layer],*right=[CALayer layer];root.frame=CGRectMake(0,0,96,64);left.frame=CGRectMake(0,0,48,64);left.contents=(id)surface;right.frame=CGRectMake(48,0,48,64);
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();CGFloat blue[]={0,0,.5,.5};CGColorRef color=CGColorCreate(space,blue);right.backgroundColor=color;CGColorRelease(color);CGColorSpaceRelease(space);
 [root addSublayer:left];[root addSublayer:right];[[window contentView] setLayer:root];[window makeKeyAndOrderFront:nil];[window display];pump(.7);
 if(adopt)for(int i=0;i<3;i++){[window display];pump(.05);}
 int failed=capture(directory,@"green",marker);CGLSetCurrentContext(writer);glBindFramebuffer(0x8d40,fbo);glClearColor(1,1,0,1);glClear(0x4000);
 glEnable(0x0c11);glScissor(0,0,32,16);glClearColor(0,0,1,1);glClear(0x4000);glDisable(0x0c11);
 glFinish();failed+=glGetError()!=0;CGLSetCurrentContext(NULL);
 [CATransaction begin];[CATransaction commit];pump(.7);failed+=capture(directory,@"yellow",marker);
 // Keep the same context and contents while replacing both export surfaces.
 [window setContentSize:NSMakeSize(128,80)];root.frame=CGRectMake(0,0,128,80);
 left.frame=CGRectMake(0,0,64,80);right.frame=CGRectMake(64,0,64,80);
 [window display];pump(.7);failed+=capture(directory,@"resized",marker);
 if(adopt){
  [window close];[window release];pump(.2);
  window=[[NSWindow alloc] initWithContentRect:NSMakeRect(60,60,128,80) styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
  [window setReleasedWhenClosed:NO];[window setBackgroundColor:[NSColor redColor]];[window setTitle:marker];
  [[window contentView] setLayer:root];[window makeKeyAndOrderFront:nil];[window display];pump(.7);
  failed+=capture(directory,@"adopted",marker);
 }
 uint64_t transfers[4]={0},present[4]={0};if(stats)stats(transfers);
 void (*exportStats)(uint64_t[3])=dlsym(gl,"CGLSFFXExportCacheStats");uint64_t exported[3]={0};if(exportStats)exportStats(exported);
 printf("FXEXPORTCACHE enabled=%d hits=%llu misses=%llu evictions=%llu\n",getenv("SFFX_GPU_EXPORT_CACHE")!=NULL,(unsigned long long)exported[0],(unsigned long long)exported[1],(unsigned long long)exported[2]);
 if(require&&getenv("SFFX_GPU_EXPORT_CACHE")&&(!exportStats||exported[0]==0))failed++;
 int (*wsstats)(int,uint64_t[4])=dlsym(RTLD_DEFAULT,"_windowServerSFFXGPUStats");int observed=wsstats?wsstats((int)[window windowNumber],present):0;
 int transport=!require||(observed&&present[0]>0&&present[1]>0&&present[2]>0&&present[3]==0&&stats&&transfers[0]==0&&transfers[1]==0);
 printf("FXGPUPRESENT require=%d window=%d observed=%d frames=%llu base_upload_bytes=%llu layer_gpu_bytes=%llu failures=%llu CGL_cpu_upload=%llu CGL_cpu_readback=%llu CGL_gpu_blit=%llu CGL_direct_commits=%llu captures_failed=%d transport_pass=%d\n",require,(int)[window windowNumber],observed,(unsigned long long)present[0],(unsigned long long)present[1],(unsigned long long)present[2],(unsigned long long)present[3],(unsigned long long)transfers[0],(unsigned long long)transfers[1],(unsigned long long)transfers[2],(unsigned long long)transfers[3],failed,transport);
 [window close];[window release];pump(.2);
 CGLSetCurrentContext(writer);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);failed+=glGetError()!=0;
 CGLSetCurrentContext(NULL);CGLDestroyContext(writer);CGLDestroyPixelFormat(format);CFRelease(surface);
 return failed||!transport?1:0;
}}
