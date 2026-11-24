/* SPDX-License-Identifier: MIT */
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
static void check(OSStatus e,const char *operation) { if(e){fprintf(stderr,"%s: %d\n",operation,(int)e);exit(1);} }
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0); if(argc!=2)return 2;
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)argv[1],strlen(argv[1]),false);
    AudioFileID file=NULL;check(AudioFileOpenURL(url,kAudioFileReadPermission,0,&file),"AudioFileOpenURL");
    AudioStreamBasicDescription source={0};UInt32 size=sizeof(source);check(AudioFileGetProperty(file,kAudioFilePropertyDataFormat,&size,&source),"data format");
    UInt64 packets=0;size=sizeof(packets);check(AudioFileGetProperty(file,kAudioFilePropertyAudioDataPacketCount,&size,&packets),"packet count");
    printf("source id=%08x rate=%.0f channels=%u packets=%llu\n",(unsigned)source.mFormatID,source.mSampleRate,(unsigned)source.mChannelsPerFrame,(unsigned long long)packets);
    check(AudioFileClose(file),"AudioFileClose");
    ExtAudioFileRef ext=NULL;check(ExtAudioFileOpenURL(url,&ext),"ExtAudioFileOpenURL");CFRelease(url);
    AudioStreamBasicDescription pcm={source.mSampleRate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,2*source.mChannelsPerFrame,1,2*source.mChannelsPerFrame,source.mChannelsPerFrame,16,0};
    check(ExtAudioFileSetProperty(ext,kExtAudioFileProperty_ClientDataFormat,sizeof(pcm),&pcm),"client format");
    SInt64 length=0;size=sizeof(length);check(ExtAudioFileGetProperty(ext,kExtAudioFileProperty_FileLengthFrames,&size,&length),"length");
    int16_t data[1024*8];uint64_t hash=UINT64_C(14695981039346656037),frames=0,nonzero=0;long double energy=0;int peak=0;
    FILE *dump=fopen("decoded.s16le","wb");if(!dump)return 3;
    for(unsigned iteration=0;iteration<10000;iteration++) {
        UInt32 n=1024;AudioBufferList buffers={1,{{pcm.mChannelsPerFrame,sizeof(data),data}}};
        check(ExtAudioFileRead(ext,&n,&buffers),"ExtAudioFileRead");if(!n)break;
        if(n>1024 || buffers.mBuffers[0].mDataByteSize!=n*pcm.mBytesPerFrame)return 4;
        if(fwrite(data,pcm.mBytesPerFrame,n,dump)!=n)return 5;
        for(UInt32 i=0;i<n*pcm.mChannelsPerFrame;i++){int v=data[i];if(v)nonzero++;if(abs(v)>peak)peak=abs(v);energy+=(long double)v*v;}
        const uint8_t *bytes=(const uint8_t*)data;
        for(UInt32 i=0;i<n*pcm.mBytesPerFrame;i++){hash^=bytes[i];hash*=UINT64_C(1099511628211);}
        frames+=n;
    }
    if(fclose(dump))return 6;
    printf("decoded frames=%llu length=%lld peak=%d nonzero=%llu fnv=%016llx rms=%.3Lf\n",(unsigned long long)frames,(long long)length,peak,(unsigned long long)nonzero,(unsigned long long)hash,sqrtl(energy/(frames*pcm.mChannelsPerFrame)));
    check(ExtAudioFileSeek(ext,64),"seek");UInt32 n=8;AudioBufferList buffers={1,{{pcm.mChannelsPerFrame,sizeof(data),data}}};check(ExtAudioFileRead(ext,&n,&buffers),"seek read");
    SInt64 position=0;check(ExtAudioFileTell(ext,&position),"tell");printf("seek frames=%u tell=%lld samples=",(unsigned)n,(long long)position);
    for(UInt32 i=0;i<n*pcm.mChannelsPerFrame;i++)printf("%s%d",i?",":"",data[i]);putchar('\n');
    check(ExtAudioFileDispose(ext),"dispose");
    return frames>0 && frames==(uint64_t)length && peak>0 && position==72 ? 0:7;
}
