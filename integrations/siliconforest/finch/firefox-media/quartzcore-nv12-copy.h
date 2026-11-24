/* SPDX-License-Identifier: MIT
 * Private caller proof against the frozen installed CARenderer. Included after
 * its surface API and R8/RG8 definitions; no change to shader/matrix/geometry.
 * Caller-owned textures contain independent storage, never decoder aliases.
 */
#include <stdint.h>

/* YUV frames, GPU attempts/successes, CPU frames/calls/visible bytes/source
 * spans, GPU storage allocations/failures, wide CPU frames. Source spans
 * include row padding between rows; visible bytes describe pixel payloads.
 * Count every non-NULL client upload, including a failed RG attempt retried as
 * luminance. NULL allocation calls are separate, not counted as CPU uploads.
 */
static uint64_t sfca_video_upload_stats[10];
static void sfcaVideoCount(unsigned index,uint64_t value) {
   __atomic_fetch_add(&sfca_video_upload_stats[index],value,__ATOMIC_RELAXED);
}
__attribute__((visibility("default")))
void SFCAReadVideoUploadStats(uint64_t values[10]) {
   if(values)for(unsigned i=0;i<10;i++)
    values[i]=__atomic_load_n(&sfca_video_upload_stats[i],__ATOMIC_RELAXED);
}

static struct {
   int (*copy)(CFTypeRef,const unsigned *);
   void (*increment)(CFTypeRef),(*decrement)(CFTypeRef);
   int enabled;
} sfcaVideoGPU;
static pthread_once_t sfcaVideoGPUOnce=PTHREAD_ONCE_INIT;
static void sfcaVideoLoadGPU(void) {
   sfcaVideoGPU.copy=dlsym(RTLD_DEFAULT,"CGLPDCopyNV12ToTextures2D");
   sfcaVideoGPU.increment=dlsym(RTLD_DEFAULT,"IOSurfaceIncrementUseCount");
   sfcaVideoGPU.decrement=dlsym(RTLD_DEFAULT,"IOSurfaceDecrementUseCount");
   const char *choice=getenv("SF_CA_NV12_GPU_COPY");
   sfcaVideoGPU.enabled=!(choice && !strcmp(choice,"0"));
}

static BOOL sfcaVideoHold(CFTypeRef surface,int kind) {
   if(kind!=CASurfaceYUV8Video && kind!=CASurfaceYUV8Full)return NO;
   pthread_once(&sfcaVideoGPUOnce,sfcaVideoLoadGPU);
   if(!sfcaVideoGPU.increment || !sfcaVideoGPU.decrement)return NO;
   sfcaVideoGPU.increment(surface);
   return YES;
}

static const void *sfcaCPUPlaneAddress(CFTypeRef surface,size_t plane,
   size_t width,size_t height,size_t rowBytes,size_t channels,size_t sampleBytes) {
   const void *base=surfaceAPI.planeBase(surface,plane);
   if(base){
    uint64_t row=(uint64_t)width*channels*sampleBytes;
    sfcaVideoCount(4,1);
    sfcaVideoCount(5,row*height);
    sfcaVideoCount(6,height ? (uint64_t)(height-1)*rowBytes+row : 0);
   }
   return base;
}

static void sfcaVideoSaveUnpack(GLint state[6]) {
   const GLenum names[6]={GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ROW_LENGTH,
      GL_UNPACK_ALIGNMENT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES};
   for(unsigned i=0;i<6;i++)glGetIntegerv(names[i],state+i);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
   for(unsigned i=1;i<6;i++)glPixelStorei(names[i],i==2 ? 1 : 0);
}
static void sfcaVideoRestoreUnpack(const GLint state[6]) {
   const GLenum names[6]={GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ROW_LENGTH,
      GL_UNPACK_ALIGNMENT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES};
   for(unsigned i=1;i<6;i++)glPixelStorei(names[i],state[i]);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)state[0]);
}

static BOOL sfcaVideoStorage(GLuint texture,GLsizei width,GLsizei height,BOOL chroma) {
   GLint oldWidth=0,oldHeight=0,format=0;
   glBindTexture(GL_TEXTURE_2D,texture);
   glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&oldWidth);
   glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&oldHeight);
   glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
   if(glGetError()!=GL_NO_ERROR)return NO;
   GLint wanted=chroma ? SFCA_GL_RG8 : SFCA_GL_R8;
   if(oldWidth!=width || oldHeight!=height || format!=wanted){
    glTexImage2D(GL_TEXTURE_2D,0,wanted,width,height,0,chroma ? GL_RG : GL_RED,GL_UNSIGNED_BYTE,NULL);
    if(glGetError()!=GL_NO_ERROR)return NO;
    sfcaVideoCount(7,1);
   }
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
   return glGetError()==GL_NO_ERROR;
}

/* Called before Lock/base access, while an OUTER frame use is already held.
 * The CGL helper holds its own use only across its completed GPU copies.
 * On false the caller must overwrite BOTH planes while keeping its outer use.
 */
static BOOL sfcaVideoTryGPU(CFTypeRef surface,GLuint texture,BOOL held) {
   if(!held || !sfcaVideoGPU.enabled || !sfcaVideoGPU.copy)return NO;
   sfcaVideoCount(1,1);
   size_t width[2],height[2];
   for(unsigned i=0;i<2;i++){
    width[i]=surfaceAPI.planeWidth(surface,i);height[i]=surfaceAPI.planeHeight(surface,i);
    if(!width[i] || !height[i] || width[i]>INT_MAX || height[i]>INT_MAX){
     sfcaVideoCount(8,1);return NO;
    }
   }
   if(glGetError()!=GL_NO_ERROR){sfcaVideoCount(8,1);return NO;}
   GLint bound=0,unpackBuffer=0;
   glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
   glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
   if(glGetError()!=GL_NO_ERROR){sfcaVideoCount(8,1);return NO;}
   unsigned textures[2]={texture,chromaTextureFor(texture)};
   /* NULL means allocation only with PBO0, regardless of caller unpack state. */
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
   BOOL good=textures[0] && textures[1] && textures[0]!=textures[1];
   if(good)good=sfcaVideoStorage(textures[0],(GLsizei)width[0],(GLsizei)height[0],NO);
   if(good)good=sfcaVideoStorage(textures[1],(GLsizei)width[1],(GLsizei)height[1],YES);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpackBuffer);
   glBindTexture(GL_TEXTURE_2D,(GLuint)bound);
   if(glGetError()!=GL_NO_ERROR)good=NO;
   if(good)good=sfcaVideoGPU.copy(surface,textures)==1;
   if(good){sfcaPlanesAsRG=1;sfcaVideoCount(2,1);}
   else sfcaVideoCount(8,1);
   return good;
}
