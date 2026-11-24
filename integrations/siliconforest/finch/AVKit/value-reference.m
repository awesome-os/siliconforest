/* SPDX-License-Identifier: MIT */
/* Own file/rational-time inputs, native interface observed through runtime APIs. */
#import <Foundation/Foundation.h>
#include <CoreMedia/CoreMedia.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
typedef struct {int16_t subframes,divisor;uint32_t counter,type,flags;int16_t hours,minutes,seconds,frames;} SFSMPTE;
@interface NSObject(SFKitValues)
+ (id)identifierForURL:(NSURL*)url;
+ (id)invalidIdentifier;
- (id)initWithURL:(NSURL*)url;
- (id)assetURL;
- (BOOL)isFileReferenceURL;
- (id)filePathURL;
- (BOOL)isValid;
- (id)initWithTimecodeString:(NSString*)string;
- (CMTime)frameDuration;
- (void)setFrameDuration:(CMTime)value;
- (int64_t)frameNumber;
- (void)setFrameNumber:(int64_t)value;
- (uint32_t)tc_flags;
- (void)setTc_flags:(uint32_t)value;
- (SFSMPTE)timecodeStruct;
- (BOOL)stringIsValidNumericValue:(NSString*)string;
@end
static void timecode(id object,const char *label){
 if(!object){printf("KIT_TIMECODE %s object=0\n",label);return;}
 CMTime d=[object frameDuration];SFSMPTE s=[object timecodeStruct];
 printf("KIT_TIMECODE %s object=%d frame=%lld duration=%lld/%d/%u flags=%u smpte=%d,%d,%u,%u,%u,%d,%d,%d,%d\n",label,object!=nil,(long long)[object frameNumber],(long long)d.value,d.timescale,d.flags,[object tc_flags],s.subframes,s.divisor,s.counter,s.type,s.flags,s.hours,s.minutes,s.seconds,s.frames);
}
int main(void){@autoreleasepool{
 if(!dlopen("/System/Library/Frameworks/AVKit.framework/AVKit",RTLD_NOW|RTLD_GLOBAL))return 1;
 Class identifier=NSClassFromString(@"AVAssetIdentifier"),tc=NSClassFromString(@"AVTimecode");if(!identifier||!tc)return 2;
 NSFileManager *fm=[NSFileManager defaultManager];NSString *base=[fm currentDirectoryPath];
 NSString *path=[base stringByAppendingPathComponent:@"own-asset.dat"],*other=[base stringByAppendingPathComponent:@"own-other.dat"],*link=[base stringByAppendingPathComponent:@"own-link.dat"];
 if(![@"our source-owned file" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL]||![@"our other file" writeToFile:other atomically:YES encoding:NSUTF8StringEncoding error:NULL])return 3;
 if(symlink("own-asset.dat",[link fileSystemRepresentation]))return 4;
 NSURL *url=[NSURL fileURLWithPath:path];id a=[identifier identifierForURL:url],b=[identifier identifierForURL:url],c=[identifier identifierForURL:[NSURL fileURLWithPath:other]],l=[identifier identifierForURL:[NSURL fileURLWithPath:link]],missing=[identifier identifierForURL:[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"own-absent.dat"]]],invalid=[identifier invalidIdentifier];
 printf("KIT_IDENTIFIER object=%d valid=%d url-equal=%d same-equal=%d same-hash=%d other-equal=%d link-equal=%d absent-valid=%d invalid-valid=%d invalid-self-equal=%d copy-equal=%d\n",a!=nil,[a isValid],[[a assetURL] isEqual:url],[a isEqual:b],[a hash]==[b hash],[a isEqual:c],[a isEqual:l],[missing isValid],[invalid isValid],[invalid isEqual:invalid],[a isEqual:[[a copy] autorelease]]);
 id stored=[a assetURL];
 printf("KIT_IDENTIFIER stored-present=%d stored-class=%s string-path-equal=%d string-url-equal=%d\n",stored!=nil,stored?[[NSStringFromClass([stored class]) description] UTF8String]:"nil",[stored isEqual:path],[stored isEqual:[url absoluteString]]);
 if([stored isKindOfClass:[NSURL class]]){CFURLRef pathURL=CFURLCreateFilePathURL(NULL,(CFURLRef)stored,NULL);printf("KIT_IDENTIFIER stored-file-reference=%d file-path-equal=%d\n",CFURLIsFileReferenceURL((CFURLRef)stored),[(id)pathURL isEqual:url]);if(pathURL)CFRelease(pathURL);}
 const char *numeric[]={"0","123","-1","1.5"," 12 ","12a","","00:00:01:00"};
 id checker=[[[tc alloc] init] autorelease];
 for(unsigned i=0;i<8;i++)printf("KIT_NUMERIC case=%u valid=%d\n",i,[checker stringIsValidNumericValue:[NSString stringWithUTF8String:numeric[i]]]);
 [fm removeItemAtPath:path error:NULL];printf("KIT_IDENTIFIER deleted-valid=%d\n",[a isValid]);
 [fm removeItemAtPath:other error:NULL];[fm removeItemAtPath:link error:NULL];
 const char *strings[]={"00:00:10:15","01:02:03:04","00:01:00;02"};
 for(unsigned i=0;i<3;i++){@try{id t=[[[tc alloc] initWithTimecodeString:[NSString stringWithUTF8String:strings[i]]] autorelease];timecode(t,strings[i]);}@catch(NSException *exception){printf("KIT_TIMECODE %s exception=%s\n",strings[i],[[exception name] UTF8String]);}}
 id t=[[[tc alloc] init] autorelease];[t setFrameDuration:CMTimeMake(1,30)];[t setTc_flags:0];
 const int64_t numbers[]={0,1,30,315,108000,-1};
 for(unsigned i=0;i<6;i++){@try{[t setFrameNumber:numbers[i]];char label[64];snprintf(label,sizeof(label),"frame-%lld",(long long)numbers[i]);timecode(t,label);}@catch(NSException *exception){printf("KIT_TIMECODE frame=%lld exception=%s\n",(long long)numbers[i],[[exception name] UTF8String]);}}
 return 0;
}}
