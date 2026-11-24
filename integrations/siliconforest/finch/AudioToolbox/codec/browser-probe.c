/* SPDX-License-Identifier: MIT */
/* Public-API AAC decoding with raw ASC and ESDS, float PCM without Packed. */
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "SFAACCookie.h"
static void need(int ok,const char *where){if(!ok){fprintf(stderr,"BROWSER_AUDIO_FAIL %s\n",where);exit(1);}}
struct Input {AudioFileID file;SInt64 packet;UInt8 bytes[65536];AudioStreamPacketDescription desc;};
static OSStatus input(AudioConverterRef converter,UInt32 *packets,AudioBufferList *data,
                      AudioStreamPacketDescription **desc,void *opaque){
 (void)converter;struct Input *in=opaque;UInt32 count=1,bytes=sizeof(in->bytes);
 OSStatus err=AudioFileReadPacketData(in->file,false,&bytes,&in->desc,in->packet,&count,in->bytes);
 if(err)return err;
 in->packet+=count;*packets=count;data->mNumberBuffers=1;
 data->mBuffers[0]=(AudioBuffer){0,bytes,in->bytes};*desc=&in->desc;return noErr;
}
static uint64_t decode(CFURLRef url,int wrapped,uint64_t *frames){
 struct Input in={0};need(!AudioFileOpenURL(url,kAudioFileReadPermission,0,&in.file),"open");
 AudioStreamBasicDescription source={0};UInt32 size=sizeof(source);
 need(!AudioFileGetProperty(in.file,kAudioFilePropertyDataFormat,&size,&source),"source format");
 need(source.mFormatID==kAudioFormatMPEG4AAC&&source.mChannelsPerFrame<=8,"AAC source");
 UInt32 length=0;need(!AudioFileGetPropertyInfo(in.file,kAudioFilePropertyMagicCookieData,&length,NULL)&&length>=2&&length<=sizeof(UInt8[512]),"ASC cookie size");
 UInt8 cookie[512]={0};size=length;need(!AudioFileGetProperty(in.file,kAudioFilePropertyMagicCookieData,&size,cookie),"ASC cookie");
 const UInt8 *asc=NULL;size_t ascSize=0;
 need(sf_aac_cookie(cookie,length,&asc,&ascSize)&&ascSize<=100,"normalize own fixture ASC");
 memmove(cookie,asc,ascSize);length=(UInt32)ascSize;
 if(wrapped){
  UInt8 esds[128]={0};esds[4]=3;esds[5]=(UInt8)(20+length);esds[7]=1;
  esds[9]=4;esds[10]=(UInt8)(15+length);esds[11]=0x40;esds[12]=0x15;
  esds[24]=5;esds[25]=(UInt8)length;memcpy(esds+26,cookie,length);
  length+=26;memcpy(cookie,esds,length);
 }
 AudioStreamBasicDescription pcm={source.mSampleRate,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat,
  4*source.mChannelsPerFrame,1,4*source.mChannelsPerFrame,source.mChannelsPerFrame,32,0};
 AudioConverterRef converter=NULL;
 need(!AudioConverterNew(&source,&pcm,&converter)&&converter,"create no-Packed float converter");
 OSStatus cookieStatus=AudioConverterSetProperty(converter,kAudioConverterDecompressionMagicCookie,length,cookie);
 if(cookieStatus)fprintf(stderr,"BROWSER_AUDIO cookie wrapped=%d bytes=%u status=%d\n",wrapped,length,(int)cookieStatus);
 need(!cookieStatus,"magic cookie");
 uint64_t hash=UINT64_C(14695981039346656037);*frames=0;
 for(unsigned i=0;i<10000;i++){
  float samples[1024*8];AudioBufferList data={1,{{source.mChannelsPerFrame,sizeof(samples),samples}}};UInt32 count=1024;
  need(!AudioConverterFillComplexBuffer(converter,input,&in,&count,&data,NULL),"decode");
  if(!count)break;
  need(count<=1024&&data.mBuffers[0].mDataByteSize==count*pcm.mBytesPerFrame,"PCM bounds");
  for(UInt32 j=0;j<data.mBuffers[0].mDataByteSize;j++){hash^=((UInt8*)samples)[j];hash*=UINT64_C(1099511628211);}
  *frames+=count;
 }
 need(!AudioConverterDispose(converter)&&!AudioFileClose(in.file),"close");return hash;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(UInt8*)argv[1],strlen(argv[1]),false);
 uint64_t a=0,b=0,raw=decode(url,0,&a),esds=decode(url,1,&b);CFRelease(url);
 need(a>0&&a==b&&raw==esds,"raw ASC and ESDS PCM equality");
 printf("BROWSER_AUDIO float-without-packed=1 raw-frames=%llu esds-frames=%llu identical-pcm=1 fnv=%016llx\n",(unsigned long long)a,(unsigned long long)b,(unsigned long long)raw);return 0;
}
