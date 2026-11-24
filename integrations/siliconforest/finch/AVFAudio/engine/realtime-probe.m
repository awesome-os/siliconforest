/* SPDX-License-Identifier: MIT */
#import <AVFAudio/AVFAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
@interface AVAudioEngine (SFOutputProbe)
- (NSDictionary *)sf_outputStatistics;
@end
static void need(BOOL value,const char *what){if(!value){fprintf(stderr,"ENGINE_FAIL %s\n",what);exit(1);}}
int main(int argc,char **argv){@autoreleasepool{
    need(argc==3,"usage: realtime-probe --offline|--output file");
    BOOL offline=!strcmp(argv[1],"--offline");need(offline||!strcmp(argv[1],"--output"),"mode");
    for(unsigned trial=0;trial<(offline?1:3);trial++){@autoreleasepool{
        NSError *error=nil;
        AVAudioFile *file=[[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[2]]] error:&error];
        need(file&&!error,"open source");
        AVAudioEngine *engine=[AVAudioEngine new];AVAudioPlayerNode *player=[AVAudioPlayerNode new];
        [engine attachNode:player];
        AVAudioFormat *format=[[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
        if(offline)need([engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:format maximumFrameCount:1024 error:&error],"manual mode");
        [engine connect:player to:[engine mainMixerNode] format:file.processingFormat];
        [player scheduleFile:file atTime:nil completionHandler:nil];
        if(!offline)[player play];
        need([engine startAndReturnError:&error]&&!error,"start engine");
        if(offline)[player play];
        if(offline){
            AVAudioPCMBuffer *buffer=[[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:1024];
            uint64_t frames=llround(file.length*48000/file.processingFormat.sampleRate),done=0,nonzero=0;uint32_t hash=2166136261u;
            while(done<frames){UInt32 count=frames-done>1024?1024:(UInt32)(frames-done);
                need([engine renderOffline:count toBuffer:buffer error:&error]==AVAudioEngineManualRenderingStatusSuccess&&!error,"render offline");
                for(UInt32 n=0;n<count;n++)for(UInt32 c=0;c<2;c++){float f=buffer.floatChannelData[c][n];if(f!=0)nonzero++;uint8_t *b=(uint8_t *)&f;for(int x=0;x<4;x++)hash=(hash^b[x])*16777619u;}
                done+=count;
            }
            printf("ENGINE_OFFLINE frames=%llu rate=48000 channels=2 nonzero=%llu fnv=%08x\n",(unsigned long long)done,(unsigned long long)nonzero,hash);
            need(nonzero>0,"nonzero offline samples");[buffer release];
        }else{
            need([engine respondsToSelector:@selector(sf_outputStatistics)],"retained HAL diagnostic available");
            double rate=[[engine outputNode] outputFormatForBus:0].sampleRate;
            uint64_t required=llround(file.length*rate/file.processingFormat.sampleRate);
            NSDictionary *stats=nil;
            for(int n=0;n<500;n++){
                stats=[engine sf_outputStatistics];
                need([[stats objectForKey:@"error"] intValue]==0&&[[stats objectForKey:@"clockError"] intValue]==0,"queue and clock");
                if([[stats objectForKey:@"sampleTime"] doubleValue]>=required)break;
                usleep(10000);
            }
            need([[stats objectForKey:@"sampleTime"] doubleValue]>=required,"HAL consumed source duration");
            need([[stats objectForKey:@"nonzero"] unsignedLongLongValue]>0,"nonzero rendered output");
            need([[stats objectForKey:@"returned"] unsignedLongLongValue]>=1024,"HAL returned buffers");
            printf("ENGINE_HAL trial=%u rate=%.0f required=%llu queued=%llu returned=%llu callbacks=%u sample_time=%.0f nonzero=%llu error=%d\n",trial,rate,(unsigned long long)required,[[stats objectForKey:@"queued"] unsignedLongLongValue],[[stats objectForKey:@"returned"] unsignedLongLongValue],[[stats objectForKey:@"callbacks"] unsignedIntValue],[[stats objectForKey:@"sampleTime"] doubleValue],[[stats objectForKey:@"nonzero"] unsignedLongLongValue],[[stats objectForKey:@"error"] intValue]);
        }
        [engine stop];need(!engine.isRunning,"stopped");[player release];[engine release];[format release];[file release];
    }}
    return 0;
}}
