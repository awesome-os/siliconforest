/* SPDX-License-Identifier: MIT */
/* Our bounded movie exerciser: AVFoundation demux -> VideoToolbox, ExtAudioFile
 * decode -> retained AudioQueue/HAL, audio sample clock -> ordered layer submits.
 * --decode-only never opens an output device or window (native reference mode).
 * --headless measures delivery timing without claiming screen presentation.
 * --window draws our frame layer through CPU CoreGraphics; drawing is not scanout.
 */
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CATransaction.h>
#include <AudioToolbox/AudioQueue.h>
#include "VideoToolbox.h"
#include <libavutil/md5.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { CMTime pts; size_t width,height; UInt8 *nv12; char hash[33]; } MovieFrame;
typedef struct { pthread_mutex_t lock; MovieFrame *frames; size_t count,capacity,bytes; unsigned errors; } Pictures;
typedef struct {
    pthread_mutex_t lock; UInt8 *pcm; size_t bytes,next,completed;
    UInt32 callbacks,bytesPerFrame; OSStatus error;
} Audio;
static double now_seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void require(BOOL ok,const char *message){if(!ok){fprintf(stderr,"MOVIE_FAIL %s\n",message);exit(1);}}
static void check(OSStatus status,const char *message){if(status){fprintf(stderr,"MOVIE_FAIL %s status=%d\n",message,(int)status);exit(1);}}
/* Own frame-layer presenter. Hosted CALayer rendering needs CGL/libEGL on PD;
 * a CPU view instead draws this layer's CGImage into the real window context. */
@interface SFMovieView : NSView {
    CALayer *_sfFrameLayer;
    size_t _sfFrameIndex,_sfDrawnFrames,_sfLastDrawn;
    double _sfPTS;
    char _sfMD5[33];
    BOOL _sfHasFrame;
}
- (id)initWithFrame:(NSRect)frame frameLayer:(CALayer *)layer;
- (void)setMovieFrame:(const MovieFrame *)frame index:(size_t)index;
- (size_t)drawnFrameCount;
@end
@implementation SFMovieView
- (id)initWithFrame:(NSRect)frame frameLayer:(CALayer *)layer {
    if((self=[super initWithFrame:frame])){_sfFrameLayer=[layer retain];_sfLastDrawn=SIZE_MAX;}
    return self;
}
- (void)dealloc {[_sfFrameLayer release];[super dealloc];}
- (void)setMovieFrame:(const MovieFrame *)frame index:(size_t)index {
    _sfFrameIndex=index;_sfPTS=CMTimeGetSeconds(frame->pts);memcpy(_sfMD5,frame->hash,33);_sfHasFrame=YES;[self setNeedsDisplay:YES];
}
- (size_t)drawnFrameCount {return _sfDrawnFrames;}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef context=[[NSGraphicsContext currentContext] graphicsPort];
    require(context!=NULL,"window graphics context");
    if(!_sfHasFrame){[[NSColor blackColor] set];NSRectFill([self bounds]);return;}
    CGImageRef image=(CGImageRef)[_sfFrameLayer contents];require(image!=NULL,"frame-layer contents");
    CGContextDrawImage(context,NSRectToCGRect([self bounds]),image);
    if(_sfLastDrawn!=_sfFrameIndex){
        require(_sfFrameIndex==_sfDrawnFrames,"CPU draw order");_sfLastDrawn=_sfFrameIndex;_sfDrawnFrames++;
        printf("MOVIE_VIEW_DRAW frame=%zu pts=%.6f md5=%s\n",_sfFrameIndex,_sfPTS,_sfMD5);
    }
}
@end
static void digest(const UInt8 *bytes,size_t size,char out[33]){
    UInt8 hash[16];av_md5_sum(hash,bytes,size);for(int n=0;n<16;n++)sprintf(out+2*n,"%02x",hash[n]);out[32]=0;
}
static void decoded(void *context,void *source,OSStatus status,VTDecodeInfoFlags flags,
                    CVImageBufferRef image,CMTime pts,CMTime duration){
    (void)source;(void)flags;(void)duration;Pictures *p=context;
    pthread_mutex_lock(&p->lock);
    if(status || !image || !isfinite(CMTimeGetSeconds(pts)) || p->count>=4096){p->errors++;goto out;}
    size_t w=CVPixelBufferGetWidth(image),h=CVPixelBufferGetHeight(image);
    if(!w || !h || w>4096 || h>2160 || (w&1) || (h&1) ||
       CVPixelBufferGetPixelFormatType(image)!=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
       CVPixelBufferGetPlaneCount(image)!=2){p->errors++;goto out;}
    size_t size=w*h*3/2;
    if(p->bytes+size>512u*1024u*1024u){p->errors++;goto out;}
    UInt8 *data=malloc(size);if(!data){p->errors++;goto out;}
    if(CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly)){free(data);p->errors++;goto out;}
    UInt8 *y=CVPixelBufferGetBaseAddressOfPlane(image,0),*uv=CVPixelBufferGetBaseAddressOfPlane(image,1);
    size_t ys=CVPixelBufferGetBytesPerRowOfPlane(image,0),us=CVPixelBufferGetBytesPerRowOfPlane(image,1);
    if(!y || !uv || ys<w || us<w){CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);free(data);p->errors++;goto out;}
    for(size_t row=0;row<h;row++)memcpy(data+row*w,y+row*ys,w);
    for(size_t row=0;row<h/2;row++)memcpy(data+w*h+row*w,uv+row*us,w);
    CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
    if(p->count==p->capacity){size_t n=p->capacity?p->capacity*2:64;void *next=realloc(p->frames,n*sizeof(MovieFrame));if(!next){free(data);p->errors++;goto out;}p->frames=next;p->capacity=n;}
    MovieFrame *f=&p->frames[p->count++];*f=(MovieFrame){.pts=pts,.width=w,.height=h,.nv12=data};digest(data,size,f->hash);p->bytes+=size;
out:pthread_mutex_unlock(&p->lock);
}
static int order(const void *a,const void *b){return CMTimeCompare(((const MovieFrame*)a)->pts,((const MovieFrame*)b)->pts);}

static BOOL decode_video(NSURL *url,Pictures *pictures,BOOL software){
    AVURLAsset *asset=[AVURLAsset URLAssetWithURL:url options:nil];
    AVAssetTrack *track=[asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
    require(track!=nil,"video track");NSError *error=nil;
    AVAssetReader *reader=[[AVAssetReader alloc] initWithAsset:asset error:&error];
    AVAssetReaderTrackOutput *output=[[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
    require([reader canAddOutput:output],"video output");[reader addOutput:output];require([reader startReading],"reader start");
    VTDecompressionSessionRef session=NULL;CMSampleBufferRef sample;size_t submitted=0;
    while((sample=[output copyNextSampleBuffer])){
        if(!CMSampleBufferGetDataBuffer(sample) && CMSampleBufferGetNumSamples(sample)==0){CFRelease(sample);continue;}
        if(!session){
            CFMutableDictionaryRef spec=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue(spec,kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder,software?kCFBooleanFalse:kCFBooleanTrue);
            CFMutableDictionaryRef attrs=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            int32_t type=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;CFNumberRef number=CFNumberCreate(NULL,kCFNumberSInt32Type,&type);
            CFDictionarySetValue(attrs,kCVPixelBufferPixelFormatTypeKey,number);CFRelease(number);
            VTDecompressionOutputCallbackRecord callback={decoded,pictures};
            check(VTDecompressionSessionCreate(NULL,CMSampleBufferGetFormatDescription(sample),spec,attrs,&callback,&session),"VT create");CFRelease(spec);CFRelease(attrs);
        }
        check(VTDecompressionSessionDecodeFrame(session,sample,0,NULL,NULL),"VT decode");CFRelease(sample);submitted++;
        require(submitted<=4096,"bounded sample count");
    }
    require(reader.status==AVAssetReaderStatusCompleted && session,"reader completed");
    check(VTDecompressionSessionFinishDelayedFrames(session),"VT finish");check(VTDecompressionSessionWaitForAsynchronousFrames(session),"VT wait");
    CFTypeRef hardware=NULL;check(VTSessionCopyProperty(session,kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder,NULL,&hardware),"VT hardware property");
    BOOL usingHardware=hardware==kCFBooleanTrue;if(hardware)CFRelease(hardware);
    VTDecompressionSessionInvalidate(session);CFRelease(session);[output release];[reader release];
    require(pictures->errors==0 && pictures->count==submitted,"decoded frame count");
    qsort(pictures->frames,pictures->count,sizeof(MovieFrame),order);
    for(size_t n=1;n<pictures->count;n++)require(CMTimeCompare(pictures->frames[n-1].pts,pictures->frames[n].pts)<0,"strict PTS order");
    printf("MOVIE_VIDEO samples=%zu pictures=%zu hardware=%d stored-bytes=%zu\n",submitted,pictures->count,usingHardware,pictures->bytes);
    return usingHardware;
}
static AudioStreamBasicDescription decode_audio(NSURL *url,Audio *audio){
    ExtAudioFileRef file=NULL;check(ExtAudioFileOpenURL((CFURLRef)url,&file),"audio open");
    AudioStreamBasicDescription source;UInt32 size=sizeof(source);check(ExtAudioFileGetProperty(file,kExtAudioFileProperty_FileDataFormat,&size,&source),"audio format");
    require(source.mChannelsPerFrame>0 && source.mChannelsPerFrame<=2 && source.mSampleRate==48000,"48k mono/stereo fixture");
    AudioStreamBasicDescription pcm={source.mSampleRate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,2*source.mChannelsPerFrame,1,2*source.mChannelsPerFrame,source.mChannelsPerFrame,16,0};
    check(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof(pcm),&pcm),"audio PCM client");
    for(;;){UInt8 bytes[16384];UInt32 frames=sizeof(bytes)/pcm.mBytesPerFrame;AudioBufferList list={1,{{pcm.mChannelsPerFrame,sizeof(bytes),bytes}}};
        check(ExtAudioFileRead(file,&frames,&list),"audio decode");if(!frames)break;size_t count=frames*pcm.mBytesPerFrame;
        require(audio->bytes+count<=64u*1024u*1024u,"bounded audio storage");void *next=realloc(audio->pcm,audio->bytes+count);require(next!=NULL,"audio allocation");audio->pcm=next;memcpy(audio->pcm+audio->bytes,bytes,count);audio->bytes+=count;
    }
    check(ExtAudioFileDispose(file),"audio close");audio->bytesPerFrame=pcm.mBytesPerFrame;
    printf("MOVIE_AUDIO frames=%zu rate=%.0f channels=%u\n",audio->bytes/pcm.mBytesPerFrame,pcm.mSampleRate,(unsigned)pcm.mChannelsPerFrame);
    return pcm;
}
static void feed(Audio *a,AudioQueueRef queue,AudioQueueBufferRef buffer){
    size_t bytes=a->bytes-a->next;if(bytes>buffer->mAudioDataBytesCapacity)bytes=buffer->mAudioDataBytesCapacity;if(!bytes)return;
    memcpy(buffer->mAudioData,a->pcm+a->next,bytes);buffer->mAudioDataByteSize=(UInt32)bytes;
    OSStatus error=AudioQueueEnqueueBuffer(queue,buffer,0,NULL);if(error)a->error=error;else a->next+=bytes;
}
static void returned(void *context,AudioQueueRef queue,AudioQueueBufferRef buffer){
    Audio *a=context;pthread_mutex_lock(&a->lock);a->completed+=buffer->mAudioDataByteSize;a->callbacks++;feed(a,queue,buffer);pthread_mutex_unlock(&a->lock);
}
static void release_rgb(void *info,const void *data,size_t size){(void)info;(void)size;free((void *)data);}
static void layer_submit(CALayer *layer,MovieFrame *frame,struct SwsContext **scale){
    int w=(int)frame->width,h=(int)frame->height;UInt8 *rgb=malloc((size_t)w*h*4);require(rgb!=NULL,"RGB allocation");
    *scale=sws_getCachedContext(*scale,w,h,AV_PIX_FMT_NV12,w,h,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);require(*scale!=NULL,"RGB converter");
    const UInt8 *src[4]={frame->nv12,frame->nv12+w*h,NULL,NULL};int strides[4]={w,w,0,0};UInt8 *dst[4]={rgb,NULL,NULL,NULL};int out[4]={w*4,0,0,0};
    require(sws_scale(*scale,src,strides,0,h,dst,out)==h,"RGB conversion");
    CGDataProviderRef provider=CGDataProviderCreateWithData(NULL,rgb,(size_t)w*h*4,release_rgb);CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(w,h,8,32,w*4,space,kCGImageAlphaNoneSkipLast|kCGBitmapByteOrderDefault,provider,NULL,false,kCGRenderingIntentDefault);
    require(image!=NULL,"layer image");[layer setContents:(id)image];CGImageRelease(image);CGColorSpaceRelease(space);CGDataProviderRelease(provider);[CATransaction flush];
}

int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);@autoreleasepool{
    BOOL window=NO,decodeOnly=NO,software=NO;const char *path=NULL;
    for(int n=1;n<argc;n++){if(!strcmp(argv[n],"--window"))window=YES;else if(!strcmp(argv[n],"--decode-only"))decodeOnly=YES;else if(!strcmp(argv[n],"--software"))software=YES;else if(!strcmp(argv[n],"--headless"))window=NO;else path=argv[n];}
    require(path && !(window && decodeOnly),"usage: movie-test --headless|--window|--decode-only [--software] file.mov");
    NSURL *url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];Pictures pictures={.lock=PTHREAD_MUTEX_INITIALIZER};Audio audio={.lock=PTHREAD_MUTEX_INITIALIZER};
    BOOL hardware=decode_video(url,&pictures,software);AudioStreamBasicDescription pcm=decode_audio(url,&audio);
    require(pictures.count>=2 && audio.bytes>0,"nonempty streams");double first=CMTimeGetSeconds(pictures.frames[0].pts),step=CMTimeGetSeconds(pictures.frames[1].pts)-first;
    require(fabs(first)<0.001 && step>0,"zero-based video timeline");
    double videoDuration=CMTimeGetSeconds(pictures.frames[pictures.count-1].pts)+step,audioDuration=(double)(audio.bytes/pcm.mBytesPerFrame)/pcm.mSampleRate;
    require(fabs(videoDuration-audioDuration)<0.05,"source stream duration agreement");
    if(decodeOnly){for(size_t n=0;n<pictures.count;n++)printf("MOVIE_DECODE frame=%zu pts=%.6f md5=%s\n",n,CMTimeGetSeconds(pictures.frames[n].pts),pictures.frames[n].hash);}
    else {
        NSWindow *host=nil;SFMovieView *view=nil;CALayer *layer=nil;struct SwsContext *scale=NULL;
        if(window){[NSApplication sharedApplication];host=[[NSWindow alloc] initWithContentRect:NSMakeRect(120,120,320,192) styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
            [host setTitle:@"Finch Movie Test"];layer=[CALayer layer];view=[[SFMovieView alloc] initWithFrame:NSMakeRect(0,0,320,192) frameLayer:layer];[host setContentView:view];[host makeKeyAndOrderFront:nil];[NSApp finishLaunching];[host displayIfNeeded];[host flushWindow];}
        AudioQueueRef queue=NULL;check(AudioQueueNewOutput(&pcm,returned,&audio,NULL,NULL,0,&queue),"queue output");check(AudioQueueSetParameter(queue,kAudioQueueParam_Volume,0.1f),"queue local volume");
        for(int n=0;n<3;n++){AudioQueueBufferRef buffer=NULL;check(AudioQueueAllocateBuffer(queue,1024*pcm.mBytesPerFrame,&buffer),"queue buffer");feed(&audio,queue,buffer);}
        double start=now_seconds(),maximumLateness=0,totalLateness=0,lastClock=0;
        double earlyOffset=0,lateOffset=0;size_t earlySamples=0,lateSamples=0;size_t delivered=0;check(AudioQueueStart(queue,NULL),"queue start");
        while(now_seconds()-start<audioDuration+10){@autoreleasepool{
            AudioTimeStamp time={0};check(AudioQueueGetCurrentTime(queue,NULL,&time,NULL),"queue sample clock");double clock=time.mSampleTime/pcm.mSampleRate;if(clock<0)clock=0;lastClock=clock;
            /* Compare change in wall/queue offset after startup, not the fixed
             * HAL buffer lead at the end. Both quantities remain in evidence. */
            double offset=now_seconds()-start-clock;
            if(clock>=audioDuration*.05 && clock<audioDuration*.10){earlyOffset+=offset;earlySamples++;}
            if(clock>=audioDuration*.90 && clock<audioDuration*.95){lateOffset+=offset;lateSamples++;}
            while(delivered<pictures.count && CMTimeGetSeconds(pictures.frames[delivered].pts)<=clock){
                MovieFrame *frame=&pictures.frames[delivered];double pts=CMTimeGetSeconds(frame->pts),late=clock-pts;if(late>maximumLateness)maximumLateness=late;totalLateness+=late;
                if(window){layer_submit(layer,frame,&scale);[view setMovieFrame:frame index:delivered];[host displayIfNeeded];[host flushWindow];require([view drawnFrameCount]==delivered+1,"CPU view draw callback");}
                printf("%s frame=%zu pts=%.6f audio=%.6f late_ms=%.3f md5=%s\n",window?"MOVIE_LAYER_SUBMIT":"MOVIE_HEADLESS_DELIVER",delivered,pts,clock,late*1000,frame->hash);delivered++;
            }
            pthread_mutex_lock(&audio.lock);BOOL complete=audio.completed==audio.bytes;OSStatus error=audio.error;pthread_mutex_unlock(&audio.lock);require(!error,"audio enqueue callback");
            if(complete && delivered==pictures.count)break;
            if(window){NSEvent *event=[NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate dateWithTimeIntervalSinceNow:0.002] inMode:NSDefaultRunLoopMode dequeue:YES];if(event)[NSApp sendEvent:event];[host displayIfNeeded];[host flushWindow];}
            else usleep(2000);
        }}
        double elapsed=now_seconds()-start;check(AudioQueueStop(queue,true),"queue stop");check(AudioQueueDispose(queue,true),"queue dispose");
        printf("MOVIE_SYNC mode=%s frames=%zu/%zu audio_queued=%zu audio_completed=%zu callbacks=%u rate=%.0f wall=%.6f audio_clock=%.6f clock_wall_drift_ms=%.3f max_delivery_late_ms=%.3f mean_delivery_late_ms=%.3f hardware=%d view_draws=%zu clock_wall_offset_ms=%.3f clock_relative_drift_ms=%.3f clock_early_samples=%zu clock_late_samples=%zu\n",window?"cpu-layer":"headless",delivered,pictures.count,audio.next/pcm.mBytesPerFrame,audio.completed/pcm.mBytesPerFrame,(unsigned)audio.callbacks,pcm.mSampleRate,elapsed,lastClock,(elapsed-lastClock)*1000,maximumLateness*1000,totalLateness*1000/pictures.count,hardware,window?[view drawnFrameCount]:0,(elapsed-lastClock)*1000,
            earlySamples&&lateSamples?(lateOffset/lateSamples-earlyOffset/earlySamples)*1000:NAN,earlySamples,lateSamples);
        require(delivered==pictures.count && audio.completed==audio.bytes && !audio.error,"all streams consumed");require(fabs(elapsed-audioDuration)<1.0 && maximumLateness<0.1,"clock pacing/drift bound");
        sws_freeContext(scale);if(window){[host orderOut:nil];[host release];[view release];}
    }
    for(size_t n=0;n<pictures.count;n++)free(pictures.frames[n].nv12);free(pictures.frames);free(audio.pcm);pthread_mutex_destroy(&pictures.lock);pthread_mutex_destroy(&audio.lock);
    return 0;
}}
