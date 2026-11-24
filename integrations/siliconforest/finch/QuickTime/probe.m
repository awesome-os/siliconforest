/* SPDX-License-Identifier: MIT */
/* M1-only unavailable-feature contracts. Never run authorization requests or
 * service operations against Apple's native frameworks on the controller. */
#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
@interface NSObject(SFPrivateProbe)
+ (id)sharedMgr;
- (id)initWithType:(NSString *)type;
- (id)initWithVideoAsset:(id)asset videoAdjustments:(id)adjustments;
+ (id)timeRangeMapperForSourceDuration:(double)duration slowMotionRate:(float)rate slowMotionTimeRange:(CMTimeRange)range forExport:(BOOL)export;
- (long)queryMetaDataSync;
- (NSArray *)results;
- (id)attributes;
+ (void)startCatalogDownload:(NSString *)type options:(id)options then:(void (^)(long))callback;
- (void)queryMetaData:(void (^)(long))callback;
- (void)setTimeoutIntervalForResource:(long)timeout;
- (long)timeoutIntervalForResource;
@end
@interface SFQTDownloadOptions:NSObject
- (void)setTimeoutIntervalForResource:(long)timeout;
- (long)timeoutIntervalForResource;
@end
static void need(int ok,const char *what){if(!ok){fprintf(stderr,"PRIVATE_QT_FAIL %s\n",what);exit(1);}}
static unsigned denied,catalog,query;
int main(void){@autoreleasepool{
 struct stat st;if(!getenv("SF_QT_PD_ONLY")||stat("/dev/sfavd",&st)){fputs("M1-only probe refused\n",stderr);return 77;}
 const char *names[]={"TCC","PhotosFormats","MediaRemote","GeoKit","MobileAsset","MonitorPanel","PerformanceAnalysis","FeatureFlags"};
 void *images[8];
 for(unsigned i=0;i<8;i++){
  char path[256];snprintf(path,sizeof(path),"/System/Library/PrivateFrameworks/%s.framework/Versions/A/%s",names[i],names[i]);
  images[i]=dlopen(path,RTLD_NOW|RTLD_GLOBAL);if(!images[i])fprintf(stderr,"%s\n",dlerror());need(images[i]!=NULL,names[i]);
 }
 int32_t (*preflight)(CFStringRef,CFDictionaryRef)=dlsym(images[0],"TCCAccessPreflight");
 void (*request)(CFStringRef,CFDictionaryRef,void(^)(Boolean))=dlsym(images[0],"TCCAccessRequest");
 const CFStringRef *service=dlsym(images[0],"kTCCServiceScreenCapture");
 need(preflight&&request&&service&&preflight(*service,NULL)==1,"TCC denied, no authorization grant");
 request(*service,NULL,^(Boolean allowed){if(!allowed)__atomic_add_fetch(&denied,1,__ATOMIC_RELEASE);});
 const void *(*origin)(void)=dlsym(images[2],"MRMediaRemoteGetLocalOrigin");
 void (*tint)(const void*,float,float,float,float)=dlsym(images[2],"MRMediaRemoteSetNowPlayingTintColor");
 need(origin&&tint&&origin()==NULL,"no now-playing origin");tint(NULL,1,0,0,1);
 id (*context)(BOOL)=dlsym(images[3],"GEOManagedObjectContext");need(context&&context(YES)==nil,"no geolocation store");
 const char *classes[]={"PFVideoAVObjectBuilder","PFVideoAdjustments","PFSlowMotionUtilities","GEOCity","GeoKitPlace","MAAsset","MAAssetQuery","MADownloadOptions","MPDisplayMgr"};
 for(unsigned i=0;i<9;i++)need(objc_getClass(classes[i])!=Nil,classes[i]);
 id builder=[[objc_getClass("PFVideoAVObjectBuilder") alloc] initWithVideoAsset:nil videoAdjustments:nil];need(!builder,"builder cannot create private Photos asset");
 CMTimeRange range={0};need([objc_getClass("PFSlowMotionUtilities") timeRangeMapperForSourceDuration:10.0 slowMotionRate:.5f slowMotionTimeRange:range forExport:NO]==nil,"no private slow-motion mapper");
 need([objc_getClass("MPDisplayMgr") sharedMgr]==nil,"no private display manager");
 id q=[[objc_getClass("MAAssetQuery") alloc] initWithType:@"org.siliconforest.invalid"];
 need(q&&[q queryMetaDataSync]!=0&&[[q results] count]==0,"catalog query fails with empty results");
 [q queryMetaData:^(long status){if(status!=0)__atomic_add_fetch(&query,1,__ATOMIC_RELEASE);}];
 [objc_getClass("MAAsset") startCatalogDownload:@"org.siliconforest.invalid" options:nil then:^(long status){if(status!=0)__atomic_add_fetch(&catalog,1,__ATOMIC_RELEASE);}];
 SFQTDownloadOptions *options=(SFQTDownloadOptions *)[objc_getClass("MADownloadOptions") new];[options setTimeoutIntervalForResource:23];need([options timeoutIntervalForResource]==23,"download options store metadata");[options release];
 id *version=dlsym(images[4],"ASAttributeContentVersion");need(version&&[*version isEqualToString:@"_ContentVersion"],"version attribute value");
 uint64_t (*start)(const void*,const void*)=dlsym(images[6],"PAMarkOperationStart");void (*end)(const void*,const void*)=dlsym(images[6],"PAMarkOperationEnd");
 need(start&&end&&!start("own", "probe"),"no performance recording token");end("own","probe");
 const char *swift[]={"$s12FeatureFlags0aB3KeyMp","$s12FeatureFlags0aB3KeyP6domains12StaticStringVvgTq","$s12FeatureFlags0aB3KeyP7features12StaticStringVvgTq"};
 for(unsigned i=0;i<3;i++)need(dlsym(images[7],swift[i])!=NULL,"Swift protocol ABI metadata");
 void *mt=dlopen("/System/Library/Frameworks/MediaToolbox.framework/Versions/A/MediaToolbox",RTLD_NOW|RTLD_GLOBAL);need(mt!=NULL,"MediaToolbox");
 void(*registerReaders)(void)=dlsym(mt,"MTRegisterProfessionalVideoWorkflowFormatReaders");need(registerReaders!=NULL,"professional reader opt-in diagnostic");registerReaders();registerReaders();
 for(unsigned i=0;i<100&&(__atomic_load_n(&denied,__ATOMIC_ACQUIRE)!=1||__atomic_load_n(&query,__ATOMIC_ACQUIRE)!=1||__atomic_load_n(&catalog,__ATOMIC_ACQUIRE)!=1);i++)usleep(10000);
 need(denied==1&&query==1&&catalog==1,"exactly once asynchronous unavailable callbacks");
 [q release];printf("PRIVATE_QT loaded=8 classes=9 TCC_denied=%u catalog_failed=%u query_failed=%u Swift_protocol=present no_services=1\n",denied,catalog,query);return 0;
}}
