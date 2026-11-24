/* SPDX-License-Identifier: MIT */
/* Emit our decoded PCM as observations; no generated file is copied from M5. */
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void check(OSStatus status,const char *name){if(status){fprintf(stderr,"%s:%d\n",name,(int)status);exit(1);}}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)argv[1],strlen(argv[1]),false);
 ExtAudioFileRef file=NULL;check(ExtAudioFileOpenURL(url,&file),"open");CFRelease(url);
 AudioStreamBasicDescription input={0};UInt32 size=sizeof(input);check(ExtAudioFileGetProperty(file,kExtAudioFileProperty_FileDataFormat,&size,&input),"format");
 if(!input.mSampleRate||!input.mChannelsPerFrame||input.mChannelsPerFrame>8)return 3;
 AudioStreamBasicDescription pcm={input.mSampleRate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,2*input.mChannelsPerFrame,1,2*input.mChannelsPerFrame,input.mChannelsPerFrame,16,0};
 check(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof(pcm),&pcm),"client");
 int16_t data[1024*8];size_t frames=0;
 printf("PCM rate=%.0f channels=%u\n",pcm.mSampleRate,(unsigned)pcm.mChannelsPerFrame);
 for(unsigned iteration=0;iteration<1024;iteration++){
  UInt32 n=1024;AudioBufferList buffers={1,{{pcm.mChannelsPerFrame,sizeof(data),data}}};check(ExtAudioFileRead(file,&n,&buffers),"read");
  if(!n)break;if(n>1024||buffers.mBuffers[0].mDataByteSize!=n*pcm.mBytesPerFrame)return 4;
  for(unsigned i=0;i<n;i++){printf("PCM %zu",frames+i);for(unsigned c=0;c<pcm.mChannelsPerFrame;c++)printf(" %d",data[i*pcm.mChannelsPerFrame+c]);putchar('\n');}
  frames+=n;
 }
 printf("PCM frames=%zu\n",frames);check(ExtAudioFileDispose(file),"close");return frames==12000?0:5;
}
