/* SPDX-License-Identifier: MIT */
/* Own value objects from runtime signature/behavior comparisons. PD has no
 * file-reference URL creation: identifiers expose a working path URL and a
 * snapshot of file identity, not a fake persistent reference URL. */
#import <Foundation/Foundation.h>
#include <CoreMedia/CoreMedia.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
@interface NSObject(SFIdentifierAsset)
- (NSURL*)URL;
@end
@interface AVAssetIdentifier:NSObject<NSCopying>{NSURL *_assetURL;NSString *_identity;}
- (id)initWithURL:(NSURL*)url;
- (NSString*)sfIdentity;
@end
@implementation AVAssetIdentifier
+ (id)identifierForURL:(NSURL*)url{return [[[self alloc] initWithURL:url] autorelease];}
+ (id)identifierForAsset:(id)asset{return [[[self alloc] initWithAsset:asset] autorelease];}
+ (id)invalidIdentifier{static id empty;static dispatch_once_t once;dispatch_once(&once,^{empty=[[self alloc] initWithURL:nil];});return empty;}
- (id)initWithAsset:(id)asset{return [self initWithURL:[asset respondsToSelector:@selector(URL)]?[asset URL]:nil];}
- (id)initWithURL:(NSURL*)url{
 if((self=[super init])&&url){
  NSURL *resolved=[url absoluteURL];
  if([url isFileURL]){
   char path[PATH_MAX];const char *source=[[url path] fileSystemRepresentation];struct stat st;
   if(source&&realpath(source,path))resolved=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
   if(source&&stat(source,&st)==0)_identity=[[NSString alloc] initWithFormat:@"file:%llu:%llu",(unsigned long long)st.st_dev,(unsigned long long)st.st_ino];
  }
  _assetURL=[resolved copy];if(!_identity)_identity=[[_assetURL absoluteString] copy];
 }
 return self;
}
- (void)dealloc{[_assetURL release];[_identity release];[super dealloc];}
- (NSURL*)assetURL{return _assetURL;}
- (NSString*)sfIdentity{return _identity;}
- (BOOL)isValid{return _assetURL!=nil;}
- (BOOL)isEqual:(id)object{return object==self||([object isKindOfClass:[AVAssetIdentifier class]]&&_identity&&[_identity isEqual:[object sfIdentity]]);}
- (NSUInteger)hash{return [_identity hash];}
- (id)copyWithZone:(NSZone*)zone{(void)zone;return [self retain];}
- (BOOL)matchesAsset:(id)asset{return [self isEqual:[[self class] identifierForAsset:asset]];}
@end
typedef struct {int16_t subframes,divisor;uint32_t counter,type,flags;int16_t hours,minutes,seconds,frames;} SFKitSMPTETime;
_Static_assert(sizeof(SFKitSMPTETime)==24,"runtime-observed SMPTE value layout");
@interface AVTimecode:NSObject {CMTime _frameDuration;int64_t _frameNumber;uint32_t _tcFlags;SFKitSMPTETime _timecode;}
- (BOOL)stringIsValidNumericValue:(NSString*)string;
@end
@implementation AVTimecode
- (id)initWithTimecodeString:(NSString*)string{
 if(!(self=[super init]))return nil;
 const char *text=[string UTF8String];char *copy=text?strdup(text):NULL,*cursor=copy;int16_t parts[4]={0};BOOL good=copy!=NULL;
 for(unsigned i=0;i<4&&good;i++){
  char *part=strsep(&cursor,":;");if(!part){good=NO;break;}
  for(char *p=part;*p;p++)if(*p<'0'||*p>'9')good=NO;
  errno=0;char *end=NULL;long value=strtol(part,&end,10);
  if(errno||value<0||value>INT16_MAX||(*part&&end&&*end))good=NO;
  if(good)parts[i]=(int16_t)value;
 }
 if(cursor)good=NO;free(copy);
 if(!good){[self release];return nil;}
 _timecode.hours=parts[0];_timecode.minutes=parts[1];_timecode.seconds=parts[2];_timecode.frames=parts[3];return self;
}
- (BOOL)stringIsValidNumericValue:(NSString*)string{
 const char *p=[string UTF8String];if(!p)return NO;
 for(;*p;p++)if(*p<'0'||*p>'9')return NO;return YES;
}
- (CMTime)frameDuration{return _frameDuration;}
- (void)setFrameDuration:(CMTime)value{_frameDuration=value;}
- (int64_t)frameNumber{return _frameNumber;}
- (void)setFrameNumber:(int64_t)value{_frameNumber=value;}
- (uint32_t)tc_flags{return _tcFlags;}
- (void)setTc_flags:(uint32_t)value{_tcFlags=value;}
- (SFKitSMPTETime)timecodeStruct{return _timecode;}
- (void)setTimecodeStruct:(SFKitSMPTETime)value{_timecode=value;}
@end
