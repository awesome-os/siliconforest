/* SPDX-License-Identifier: MIT */
#import <AVFAudio/AVFAudio.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
static void check(OSStatus status,const char *op){if(status){fprintf(stderr,"%s: %d\n",op,(int)status);exit(1);}}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);@autoreleasepool{
    if(argc!=2 && argc!=3)return 2;BOOL raw=argc==3 && strcmp(argv[2],"--raw")==0;NSError *error=nil;
    NSURL *url=[NSURL fileURLWithPath:[[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:[NSString stringWithUTF8String:argv[1]]]];
    AVAudioFile *file=[[AVAudioFile alloc] initForReading:url commonFormat:AVAudioPCMFormatFloat32 interleaved:NO error:&error];
    if(!file){fprintf(stderr,"AVAudioFile: %s\n",error.description.UTF8String);return 3;}
    AVAudioFormat *format=file.processingFormat;
    AVAudioPCMBuffer *buffer=[[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:1024];
    uint64_t frames=0;double peak=0,energy=0;
    for(int i=0;i<10000;i++){
        BOOL okay=[file readIntoBuffer:buffer error:&error];
        if(!okay && (error || buffer.frameLength)){fprintf(stderr,"read: %s\n",error.description.UTF8String);return 4;}
        if(!buffer.frameLength)break;
        frames+=buffer.frameLength;
        for(unsigned c=0;c<format.channelCount;c++)for(unsigned n=0;n<buffer.frameLength;n++){double x=buffer.floatChannelData[c][n];if(!isfinite(x))return 5;energy+=x*x;if(fabs(x)>peak)peak=fabs(x);}
    }
    printf("file id=%08x length=%lld read=%llu position=%lld rate=%.0f channels=%u peak=%.6f rms=%.6f\n",(unsigned)file.fileFormat.streamDescription->mFormatID,(long long)file.length,(unsigned long long)frames,(long long)file.framePosition,format.sampleRate,(unsigned)format.channelCount,peak,sqrt(energy/(frames*format.channelCount)));
    if(frames!=(uint64_t)file.length || !frames)return 6;
    AudioFileID source=NULL;check(AudioFileOpenURL((CFURLRef)url,kAudioFileReadPermission,0,&source),"source");
    AudioStreamBasicDescription asbd;UInt32 size=sizeof(asbd);check(AudioFileGetProperty(source,kAudioFilePropertyDataFormat,&size,&asbd),"format");
    if(raw && asbd.mFormatID!=kAudioFormatLinearPCM){
        AVAudioFormat *encoded=[file.fileFormat retain];
        AVAudioConverter *converter=[[AVAudioConverter alloc] initFromFormat:encoded toFormat:format];
        if(!converter)return 7;
        size=0;if(!AudioFileGetPropertyInfo(source,kAudioFilePropertyMagicCookieData,&size,NULL) && size){
            NSMutableData *cookie=[NSMutableData dataWithLength:size];check(AudioFileGetProperty(source,kAudioFilePropertyMagicCookieData,&size,cookie.mutableBytes),"cookie");converter.magicCookie=cookie;
        }
        UInt32 maximum=0;size=sizeof(maximum);check(AudioFileGetProperty(source,kAudioFilePropertyMaximumPacketSize,&size,&maximum),"maximum packet");
        AVAudioCompressedBuffer *packet=[[AVAudioCompressedBuffer alloc] initWithFormat:encoded packetCapacity:1 maximumPacketSize:maximum];
        if(!packet)return 8;
        AVAudioPCMBuffer *output=[[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:asbd.mFramesPerPacket>4096?asbd.mFramesPerPacket:4096];
        __block SInt64 position=0;__block OSStatus inputError=0;uint64_t converted=0;double rawPeak=0;BOOL eof=NO;
        for(int i=0;i<10000;i++){
            AVAudioConverterOutputStatus status=[converter convertToBuffer:output error:&error withInputFromBlock:^AVAudioBuffer *(AVAudioPacketCount requested,AVAudioConverterInputStatus *inputStatus){
                (void)requested;UInt32 bytes=maximum,packets=1;
                inputError=AudioFileReadPacketData(source,false,&bytes,packet.packetDescriptions,position,&packets,packet.data);
                if(inputError && inputError!=kAudioFileEndOfFileError){*inputStatus=AVAudioConverterInputStatus_EndOfStream;return nil;}
                if(!packets){*inputStatus=AVAudioConverterInputStatus_EndOfStream;return nil;}
                position+=packets;packet.packetCount=packets;packet.byteLength=bytes;*inputStatus=AVAudioConverterInputStatus_HaveData;return packet;
            }];
            if(inputError && inputError!=kAudioFileEndOfFileError)check(inputError,"packet read");
            if(status==AVAudioConverterOutputStatus_Error){fprintf(stderr,"convert: %s\n",error.description.UTF8String);return 9;}
            converted+=output.frameLength;
            for(unsigned c=0;c<format.channelCount;c++)for(unsigned n=0;n<output.frameLength;n++){double x=fabs(output.floatChannelData[c][n]);if(x>rawPeak)rawPeak=x;}
            if(status==AVAudioConverterOutputStatus_EndOfStream){eof=YES;break;}
        }
        printf("converter packets=%lld frames=%llu eof=%d peak=%.6f\n",(long long)position,(unsigned long long)converted,eof,rawPeak);
        if(!converted || !eof)return 10;
        [output release];[packet release];[converter release];[encoded release];
    }
    check(AudioFileClose(source),"close");[buffer release];[file release];
}return 0;}
