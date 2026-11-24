/* SPDX-License-Identifier: MIT */
/* Synthetic-fixture demux/sample-table and H264 compressed-passthrough mux probe.
 * No decoder, encoder, GUI or audio output is requested. */
#import <AVFoundation/AVFoundation.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void require(BOOL ok, const char *why) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",why); exit(1); }
}
static uint64_t hashBytes(uint64_t h, const void *data, size_t size) {
    const unsigned char *p=data;
    for (size_t i=0;i<size;i++) { h^=p[i]; h*=UINT64_C(1099511628211); }
    return h;
}
static AVURLAsset *openAsset(NSString *path, NSUInteger tracks) {
    AVURLAsset *a=[AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path] options:nil];
    require(a && a.tracks.count==tracks,"track count");
    require(CMTIME_IS_NUMERIC(a.duration) && CMTimeGetSeconds(a.duration)>=0.99 && CMTimeGetSeconds(a.duration)<1.1,"one-second duration");
    return a;
}
static uint64_t readVideo(AVAsset *asset, AVAssetWriterInput *input) {
    NSError *error=nil;
    AVAssetTrack *track=[asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
    require(track!=nil,"video track");
    AVAssetReader *reader=[[AVAssetReader alloc] initWithAsset:asset error:&error];
    AVAssetReaderTrackOutput *output=[[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
    require([reader canAddOutput:output],"reader can add output");
    [reader addOutput:output];require([reader startReading],"reader starts");
    uint64_t hash=UINT64_C(14695981039346656037);size_t bytes=0,count=0;
    CMSampleBufferRef sample;
    while ((sample=[output copyNextSampleBuffer])) {
        CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample);
        CMItemCount n=CMSampleBufferGetNumSamples(sample);
        /* Native readers also return zero-sample timing/edit markers. */
        if (!block && n==0) { CFRelease(sample); continue; }
        if (!block) fprintf(stderr,"sample diagnostic: count=%ld ready=%d\n",(long)n,CMSampleBufferDataIsReady(sample));
        size_t length=CMBlockBufferGetDataLength(block);
        void *data=malloc(length);require(data!=NULL && block!=NULL,"sample storage");
        require(CMBlockBufferCopyDataBytes(block,0,length,data)==0,"sample bytes");
        hash=hashBytes(hash,data,length);free(data);bytes+=length;count+=CMSampleBufferGetNumSamples(sample);
        if (input) {
            unsigned waits=0;
            while (!input.readyForMoreMediaData && waits++<2000) usleep(1000);
            require(input.readyForMoreMediaData,"writer ready");
            require([input appendSampleBuffer:sample],"compressed sample append");
        }
        CFRelease(sample);require(count<=10,"sample count bound");
    }
    require(reader.status==AVAssetReaderStatusCompleted && count==10 && bytes>0,"video reader complete");
    printf("video samples=%zu bytes=%zu fnv64=%016" PRIx64 "\n",count,bytes,hash);
    return hash;
}
int main(int argc,char **argv) {
    require(argc==3,"usage: avasset-probe input.mov|mp4 new-output.mov|mp4");
    @autoreleasepool {
        NSString *path=[NSString stringWithUTF8String:argv[1]],*out=[NSString stringWithUTF8String:argv[2]];
        require(![[NSFileManager defaultManager] fileExistsAtPath:out],"output must be new");
        AVURLAsset *asset=openAsset(path,2);
        require([asset tracksWithMediaType:AVMediaTypeAudio].count==1,"audio track");
        AVAssetTrack *video=[asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
        require(video.naturalSize.width==64 && video.naturalSize.height==48,"video geometry");
        printf("asset tracks=%lu video=64x48 duration=%.3f\n",(unsigned long)asset.tracks.count,CMTimeGetSeconds(asset.duration));
        AVSampleCursor *cursor=[video makeSampleCursorAtFirstSampleInDecodeOrder];require(cursor!=nil,"sample cursor");
        NSUInteger index=0;uint64_t tableBytes=0;
        do {
            AVSampleCursorStorageRange r=cursor.currentSampleStorageRange;
            require(r.offset>=0 && r.length>0,"sample table storage");
            tableBytes+=(uint64_t)r.length;
            CMTime d=cursor.decodeTimeStamp,p=cursor.presentationTimeStamp,t=cursor.currentSampleDuration;
            printf("sample %lu offset=%" PRId64 " size=%" PRId64 " dts=%" PRId64 "/%d pts=%" PRId64 "/%d duration=%" PRId64 "/%d sync=%d\n",
                   (unsigned long)index,r.offset,r.length,d.value,d.timescale,p.value,p.timescale,t.value,t.timescale,cursor.currentSampleSyncInfo.sampleIsFullSync);
            require(++index<=10,"cursor bounded");
        } while ([cursor stepInDecodeOrderByCount:1]==1);
        require(index==10 && tableBytes>0,"ten table entries");
        printf("sample-table count=%lu bytes=%" PRIu64 "\n",(unsigned long)index,tableBytes);
        NSError *error=nil;
        AVFileType type=[out.pathExtension isEqualToString:@"mov"]?AVFileTypeQuickTimeMovie:AVFileTypeMPEG4;
        AVAssetWriter *writer=[[AVAssetWriter alloc] initWithURL:[NSURL fileURLWithPath:out] fileType:type error:&error];
        CMFormatDescriptionRef format=(__bridge CMFormatDescriptionRef)video.formatDescriptions.firstObject;
        AVAssetWriterInput *input=[[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:nil sourceFormatHint:format];
        require([writer canAddInput:input],"writer can add input");[writer addInput:input];
        require([writer startWriting],"writer starts");[writer startSessionAtSourceTime:kCMTimeZero];
        uint64_t before=readVideo(asset,input);[input markAsFinished];
        dispatch_semaphore_t done=dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{dispatch_semaphore_signal(done);}];
        require(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))==0,"writer completion");
        require(writer.status==AVAssetWriterStatusCompleted,"writer completed");
        AVURLAsset *roundtrip=openAsset(out,1);
        require(readVideo(roundtrip,nil)==before,"compressed passthrough bytes roundtrip");
        printf("PASS: H264 compressed passthrough reader+writer, no decode/output device\n");
    }
    return 0;
}
