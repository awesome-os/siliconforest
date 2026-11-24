/* SPDX-License-Identifier: MIT */
/* Classic File Manager metadata over actual FSRef/stat/statfs. No fake devices. */
#include "catalog.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
struct volume {dev_t device;fsid_t fsid;char mount[PATH_MAX],name[256];};
static struct volume volumes[128];static unsigned count;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int16_t volumeFor(const char *path,const struct stat *st) {
 struct statfs fs;if(statfs(path,&fs))return 0;
 pthread_mutex_lock(&lock);unsigned index;
 for(index=0;index<count;index++)if(volumes[index].device==st->st_dev&&!memcmp(&volumes[index].fsid,&fs.f_fsid,sizeof(fsid_t)))break;
 if(index==count){
  if(count==128){pthread_mutex_unlock(&lock);return 0;}
  volumes[index].device=st->st_dev;volumes[index].fsid=fs.f_fsid;
  strlcpy(volumes[index].mount,fs.f_mntonname,sizeof(volumes[index].mount));
  if(!strncmp(fs.f_mntfromname,"/dev/",5))strlcpy(volumes[index].name,fs.f_mntfromname+5,sizeof(volumes[index].name));
  count++;
 }
 pthread_mutex_unlock(&lock);return (int16_t)-(int)(index+1);
}
OSStatus FSGetCatalogInfo(const FSRef *ref,uint32_t requested,SFCatalogInfo *info,void *name,void *spec,FSRef *parent) {
 /* Implement requested node/volume/IDs/data sizes; reject unavailable metadata. */
 if(!ref||spec||(requested&~(uint32_t)(2|4|8|16|0x4000)))return -4;
 if(requested&&!info)return paramErr;
 char path[PATH_MAX];OSStatus status=FSRefMakePath(ref,(UInt8*)path,sizeof(path));if(status)return status;
 struct stat st;if(stat(path,&st))return fnfErr;
 SFCatalogInfo result={0};
 if(requested&2)result.nodeFlags=S_ISDIR(st.st_mode)?0x10:0;
 if(requested&4){result.volume=volumeFor(path,&st);if(!result.volume)return ioErr;}
 if(requested&16){if((uint64_t)st.st_ino>UINT32_MAX)return -4;result.nodeID=(uint32_t)st.st_ino;}
 if(requested&0x4000){result.dataLogicalSize=st.st_size;result.dataPhysicalSize=(uint64_t)st.st_blocks*512;}
 if(parent||(requested&8)){
  char dir[PATH_MAX];strlcpy(dir,path,sizeof(dir));char *slash=strrchr(dir,'/');if(slash==dir)slash[1]=0;else if(slash)*slash=0;else return paramErr;
  if(parent){status=FSPathMakeRef((const UInt8*)dir,parent,NULL);if(status)return status;}
  if(requested&8){struct stat p;if(stat(dir,&p))return fnfErr;if((uint64_t)p.st_ino>UINT32_MAX)return -4;result.parentDirID=(uint32_t)p.st_ino;}
 }
 if(name){
  const char *base=strrchr(path,'/');base=base?base+1:path;
  CFStringRef text=CFStringCreateWithCString(NULL,base,kCFStringEncodingUTF8);if(!text)return bdNamErr;
  CFIndex length=CFStringGetLength(text);if(length>255){CFRelease(text);return bdNamErr;}
  uint16_t n=(uint16_t)length;memcpy(name,&n,2);CFStringGetCharacters(text,CFRangeMake(0,length),(UniChar*)((uint8_t*)name+2));CFRelease(text);
 }
 if(info)*info=result;return noErr;
}
OSStatus FSGetVolumeParms(FSVolumeRefNum ref,SFVolumeParms *out,uint32_t bytes) {
 if(!out||bytes<sizeof(*out))return paramErr;
 unsigned index=ref<0?(unsigned)(-ref-1):128;
 pthread_mutex_lock(&lock);
 if(index>=count){pthread_mutex_unlock(&lock);return -35;}
 const struct volume *v=&volumes[index];struct stat st;struct statfs fs;
 if(stat(v->mount,&st)||statfs(v->mount,&fs)||st.st_dev!=v->device||memcmp(&fs.f_fsid,&v->fsid,sizeof(fsid_t))){pthread_mutex_unlock(&lock);return -35;}
 SFVolumeParms result={0};result.version=5;result.deviceID=v->name[0]?v->name:NULL;
 long maximum=pathconf(v->mount,_PC_NAME_MAX);if(maximum>0&&maximum<=UINT32_MAX)result.maximumNameLength=(uint32_t)maximum;
 memcpy(out,&result,sizeof(result));pthread_mutex_unlock(&lock);return noErr;
}
