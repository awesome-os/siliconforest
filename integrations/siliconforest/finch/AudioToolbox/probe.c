/* SPDX-License-Identifier: MIT */
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s\n",#x);return 1;}}while(0)
static AudioStreamBasicDescription pcm(UInt32 bits,UInt32 flags) {
    return (AudioStreamBasicDescription){48000,kAudioFormatLinearPCM,flags,bits/8,1,bits/8,1,bits,0};
}
int main(void) {
    int16_t input[128];for(int i=0;i<128;i++)input[i]=(int16_t)(i*100-6400);
    AudioStreamBasicDescription client=pcm(16,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked);
    const AudioFileTypeID types[]={kAudioFileWAVEType,kAudioFileAIFFType,kAudioFileCAFType};
    const char *names[]={"probe-wave.wav","probe-aiff.aif","probe-caf.caf"},*magic[]={"RIFF","FORM","caff"};
    char cwd[PATH_MAX];CHECK(getcwd(cwd,sizeof cwd)!=NULL);
    for(int k=0;k<3;k++) {
        char path[PATH_MAX];CHECK(snprintf(path,sizeof path,"%s/%s",cwd,names[k])<(int)sizeof path);
        CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)path,strlen(path),false);CHECK(url);
        AudioStreamBasicDescription disk=client;if(k==1)disk.mFormatFlags|=kAudioFormatFlagIsBigEndian;
        ExtAudioFileRef file=NULL;CHECK(ExtAudioFileCreateWithURL(url,types[k],&disk,NULL,kAudioFileFlags_EraseFile,&file)==0);
        CHECK(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof client,&client)==0);
        AudioBufferList list={1,{{1,sizeof input,input}}};CHECK(ExtAudioFileWrite(file,128,&list)==0);CHECK(ExtAudioFileDispose(file)==0);
        FILE *raw=fopen(path,"rb");CHECK(raw);char head[4];CHECK(fread(head,1,4,raw)==4);fclose(raw);CHECK(memcmp(head,magic[k],4)==0);
        CHECK(ExtAudioFileOpenURL(url,&file)==0);CHECK(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof client,&client)==0);
        int16_t output[128]={0};list.mBuffers[0].mData=output;list.mBuffers[0].mDataByteSize=sizeof output;UInt32 frames=128;
        CHECK(ExtAudioFileRead(file,&frames,&list)==0&&frames==128);CHECK(memcmp(input,output,sizeof input)==0);
        CHECK(ExtAudioFileSeek(file,64)==0);memset(output,0,sizeof output);frames=64;list.mBuffers[0].mDataByteSize=128;
        CHECK(ExtAudioFileRead(file,&frames,&list)==0&&frames==64);CHECK(memcmp(input+64,output,128)==0);
        CHECK(ExtAudioFileDispose(file)==0);CFRelease(url);CHECK(unlink(path)==0);
    }
    AudioStreamBasicDescription floatFormat=pcm(32,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked);
    AudioConverterRef converter=NULL;CHECK(AudioConverterNew(&client,&floatFormat,&converter)==0);
    float output[128]={0};UInt32 bytes=sizeof output;CHECK(AudioConverterConvertBuffer(converter,sizeof input,input,&bytes,output)==0&&bytes==sizeof output);
    for(int i=0;i<128;i++)CHECK(output[i]==input[i]/32768.0f);
    CHECK(AudioConverterDispose(converter)==0);
    puts("PASS AudioFile WAVE/AIFF/CAF headers/PCM roundtrip/seek and int16-to-float conversion");return 0;
}
