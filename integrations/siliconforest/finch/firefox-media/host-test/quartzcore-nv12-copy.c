/* SPDX-License-Identifier: MIT
 * Execute the literal patched upload functions with fake GL/surface callbacks.
 * This is error/lifetime/counter policy coverage, never native pixel evidence.
 */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef const void *CFTypeRef;
typedef int BOOL;
#define YES 1
#define NO 0
#define GL_BGRA_EXT 0x80e1
enum { CASurfaceBGRA=1,CASurfaceYUV8Video,CASurfaceYUV8Full,CASurfaceYUV16Video,CASurfaceYUV16Full };
static int kind,uses,locks,cpuCalls,gpuCalls,missingCopy,missingUse,failCopy,failRG;
static GLuint active=0,bound[2]={1,0};
static GLint unpack[6]={17,19,8,3,5,1};
static const GLenum unpackNames[6]={GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ROW_LENGTH,
   GL_UNPACK_ALIGNMENT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES};
static struct {GLint w,h,format;int contents;} textures[4];
static GLenum error;
static unsigned char bytes[2]={9,10};
static size_t width(CFTypeRef s){(void)s;return 1920;}
static size_t height(CFTypeRef s){(void)s;return 1080;}
static size_t row(CFTypeRef s){(void)s;return kind>=CASurfaceYUV16Video?4096:2048;}
static size_t planeWidth(CFTypeRef s,size_t p){return p?width(s)/2:width(s);}
static size_t planeHeight(CFTypeRef s,size_t p){return p?height(s)/2:height(s);}
static size_t planeRow(CFTypeRef s,size_t p){(void)p;return row(s);}
static size_t planes(CFTypeRef s){(void)s;return 2;}
static void *base(CFTypeRef s){(void)s;return bytes;}
static void *planeBase(CFTypeRef s,size_t p){(void)s;assert(locks==1);return bytes+p;}
static int lockSurface(CFTypeRef s,unsigned flags,void *seed){(void)s;(void)flags;(void)seed;assert(!locks);locks++;return 0;}
static int unlockSurface(CFTypeRef s,unsigned flags,void *seed){(void)s;(void)flags;(void)seed;assert(locks==1);locks--;return 0;}
static struct {
 size_t (*width)(CFTypeRef),(*height)(CFTypeRef),(*rowBytes)(CFTypeRef),(*planeCount)(CFTypeRef);
 size_t (*planeWidth)(CFTypeRef,size_t),(*planeHeight)(CFTypeRef,size_t),(*planeRowBytes)(CFTypeRef,size_t);
 void *(*base)(CFTypeRef),*(*planeBase)(CFTypeRef,size_t);
 int (*lock)(CFTypeRef,unsigned,void *),(*unlock)(CFTypeRef,unsigned,void *);
} surfaceAPI={width,height,row,planes,planeWidth,planeHeight,planeRow,base,planeBase,lockSurface,unlockSurface};
static int surfaceKind(CFTypeRef s){(void)s;return kind;}
static BOOL kindIsYUV(int k){return k>=CASurfaceYUV8Video;}
static GLuint chromaTextureFor(GLuint texture){assert(texture==1);return 2;}
GLenum glGetError(void){GLenum value=error;error=0;return value;}
void glGetIntegerv(GLenum name,GLint *value){
 if(name==GL_TEXTURE_BINDING_2D){*value=bound[active];return;}
 for(unsigned i=0;i<6;i++)if(name==unpackNames[i]){*value=unpack[i];return;}
 abort();
}
void glBindTexture(GLenum target,GLuint value){assert(target==GL_TEXTURE_2D&&value<4);bound[active]=value;}
void glBindBuffer(GLenum target,GLuint value){assert(target==GL_PIXEL_UNPACK_BUFFER);unpack[0]=(GLint)value;}
void glActiveTexture(GLenum unit){assert(unit==GL_TEXTURE0||unit==GL_TEXTURE1);active=unit-GL_TEXTURE0;}
void glPixelStorei(GLenum name,GLint value){for(unsigned i=1;i<6;i++)if(name==unpackNames[i]){unpack[i]=value;return;}abort();}
void glGetTexLevelParameteriv(GLenum target,GLint level,GLenum name,GLint *value){
 assert(target==GL_TEXTURE_2D&&level==0);unsigned t=bound[active];assert(t);
 switch(name){case GL_TEXTURE_WIDTH:*value=textures[t].w;break;case GL_TEXTURE_HEIGHT:*value=textures[t].h;break;
 case GL_TEXTURE_INTERNAL_FORMAT:*value=textures[t].format;break;default:abort();}
}
void glTexParameteri(GLenum target,GLenum name,GLint value){
 assert(target==GL_TEXTURE_2D);
 assert(((name==GL_TEXTURE_MIN_FILTER||name==GL_TEXTURE_MAG_FILTER)&&value==GL_LINEAR)||
        ((name==GL_TEXTURE_WRAP_S||name==GL_TEXTURE_WRAP_T)&&value==GL_CLAMP_TO_EDGE));
}
void glTexImage2D(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *pixels){
 (void)format;(void)type;assert(target==GL_TEXTURE_2D&&level==0&&border==0&&unpack[0]==0);
 unsigned t=bound[active];assert(t);
 if(pixels){
  assert(locks==1&&!unpack[3]&&!unpack[4]&&!unpack[5]);
  if(kind==CASurfaceYUV8Video||kind==CASurfaceYUV8Full)assert(uses==(missingUse?0:1));
  cpuCalls++;
  if(failRG&&(internal==GL_R8||internal==GL_RG8)){failRG=0;error=GL_INVALID_ENUM;return;}
  textures[t].contents=*(const unsigned char *)pixels;
 }
 textures[t].w=w;textures[t].h=h;textures[t].format=internal;
}
static void increment(CFTypeRef s){assert(s);uses++;}
static void decrement(CFTypeRef s){assert(s&&uses>0);uses--;}
static int copy(CFTypeRef s,const unsigned *destination){
 assert(s&&uses==1&&!locks&&destination[0]==1&&destination[1]==2&&unpack[0]==17);
 gpuCalls++;textures[1].contents=99;if(failCopy)return 0;textures[2].contents=98;return 1;
}
static void *mockDlsym(void *library,const char *name){
 (void)library;
 if(!strcmp(name,"CGLPDCopyNV12ToTextures2D"))return missingCopy?NULL:(void *)copy;
 if(!strcmp(name,"IOSurfaceIncrementUseCount"))return missingUse?NULL:(void *)increment;
 if(!strcmp(name,"IOSurfaceDecrementUseCount"))return missingUse?NULL:(void *)decrement;
 abort();
}
#define dlsym mockDlsym
#include "quartzcore-upload-body.h"
#undef dlsym
static void reset(void){
 memset(textures,0,sizeof(textures));memset(sfca_video_upload_stats,0,sizeof(sfca_video_upload_stats));
 memset(&sfcaVideoGPU,0,sizeof(sfcaVideoGPU));sfcaVideoGPUOnce=(pthread_once_t)PTHREAD_ONCE_INIT;
 uses=locks=cpuCalls=gpuCalls=missingCopy=missingUse=failCopy=failRG=0;error=0;kind=CASurfaceYUV8Video;
 active=0;bound[0]=1;bound[1]=0;GLint initial[6]={17,19,8,3,5,1};memcpy(unpack,initial,sizeof(unpack));
 sfcaPlanesAsRG=-1;unsetenv("SF_CA_NV12_GPU_COPY");
}
static void finished(void){GLint expected[6]={17,19,8,3,5,1};assert(!uses&&!locks&&!error&&active==0&&bound[0]==1&&!memcmp(unpack,expected,sizeof(unpack)));}
int main(void){
 reset();CATexImage2DSurface(bytes,1);finished();assert(gpuCalls==1&&!cpuCalls&&sfca_video_upload_stats[7]==2&&sfcaPlanesAsRG==1);
 CATexImage2DSurface(bytes,1);finished();assert(gpuCalls==2&&sfca_video_upload_stats[7]==2);
 textures[2].w=1;CATexImage2DSurface(bytes,1);finished();assert(sfca_video_upload_stats[7]==3);
 reset();failCopy=1;CATexImage2DSurface(bytes,1);finished();assert(gpuCalls==1&&cpuCalls==2&&textures[1].contents==9&&textures[2].contents==10);
 assert(sfca_video_upload_stats[5]==3110400&&sfca_video_upload_stats[6]==3317504&&sfca_video_upload_stats[8]==1);
 reset();missingCopy=1;CATexImage2DSurface(bytes,1);finished();assert(!gpuCalls&&cpuCalls==2&&!sfca_video_upload_stats[7]);
 reset();missingUse=1;CATexImage2DSurface(bytes,1);finished();assert(!gpuCalls&&cpuCalls==2);
 reset();setenv("SF_CA_NV12_GPU_COPY","0",1);CATexImage2DSurface(bytes,1);finished();assert(!gpuCalls&&cpuCalls==2);
 reset();failCopy=failRG=1;CATexImage2DSurface(bytes,1);finished();assert(cpuCalls==3&&sfca_video_upload_stats[4]==3&&sfcaPlanesAsRG==0);
 reset();kind=CASurfaceYUV16Video;CATexImage2DSurface(bytes,1);finished();assert(!gpuCalls&&cpuCalls==2&&sfca_video_upload_stats[9]==1&&sfca_video_upload_stats[5]==6220800);
 reset();kind=0;CATexImage2DSurface(bytes,1);finished();assert(!gpuCalls&&!cpuCalls&&!sfca_video_upload_stats[0]);
 puts("PASS literal QuartzCore paired caller: storage reuse/resize, outer lifetime, partial failure, both-plane fallback, missing APIs, opt-out, P010 CPU, exact upload counters, unpack state");
}
