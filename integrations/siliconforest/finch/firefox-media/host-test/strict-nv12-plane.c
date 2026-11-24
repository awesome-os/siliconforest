/* SPDX-License-Identifier: MIT
 * Literal extension policy/failure/state tests, not GPU execution or ABI proof.
 * GL declarations are Mesa's freely available headers; all execution is fake.
 */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

typedef void *IOSurfaceRef;
static void *sf_cgl_current=(void *)1;
static pthread_mutex_t sf_cgl_track_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t sf_cgl_present_once=PTHREAD_ONCE_INIT;
static uint64_t sf_gpu_stats[4];
static struct {
 PFNEGLCREATEIMAGEKHRPROC createImage;
 PFNEGLDESTROYIMAGEKHRPROC destroyImage;
 void (*targetTexture)(GLenum,void *);
} sf_present;
static EGLDisplay sf_cgl_get_display(void){return (void *)1;}

static struct {
 uint32_t pixel;size_t width,height,row,element,offset;
 int use,inc,dec,fd_copies,closes,imports,destroys,blits,finishes,deletes,generated_textures,finish_blits;
 int fail_fd,fail_image,fail_fbo,fail_blit,fail_finish,fail_destroy,pair,wrong_second_storage;
 int fail_fd_on,fail_image_on,fail_blit_on;
 GLint texture,read,draw,storage,tw,th;
 GLboolean scissor,srgb;
 GLenum error;
} fake;
static int source_fd;
static void reset(void){
 memset(&fake,0,sizeof(fake));fake.pixel=0x34323076;fake.width=16;fake.height=8;
 fake.row=32;fake.element=1;fake.offset=4096;fake.texture=7;fake.read=41;fake.draw=42;
 fake.storage=0x8229;fake.tw=16;fake.th=8;fake.scissor=fake.srgb=GL_TRUE;
}
static uint32_t pixel(IOSurfaceRef s){assert(s);return fake.pixel;}
static size_t count(IOSurfaceRef s){assert(s);return 2;}
static size_t width(IOSurfaceRef s,size_t p){assert(s&&p<2);return fake.width;}
static size_t height(IOSurfaceRef s,size_t p){assert(s&&p<2);return fake.height;}
static size_t row(IOSurfaceRef s,size_t p){assert(s&&p<2);return fake.row;}
static size_t element(IOSurfaceRef s,size_t p){assert(s&&p<2);return fake.pair?p+1:fake.element;}
static size_t offset(IOSurfaceRef s){assert(s);return fake.offset;}
static size_t plane_offset(IOSurfaceRef s,size_t p){assert(s&&p<2);return fake.offset+p*4096;}
static void inc(IOSurfaceRef s){assert(s);fake.use++;fake.inc++;}
static void dec(IOSurfaceRef s){assert(s&&fake.use>0);if(fake.blits)assert(fake.finishes>0);fake.use--;fake.dec++;}
static int copy_fd(IOSurfaceRef s){assert(s&&fake.use>0);fake.fd_copies++;return fake.fail_fd||fake.fail_fd_on==fake.fd_copies?-1:dup(source_fd);}
static int mock_close(int fd){assert(fd>=0&&fcntl(fd,F_GETFD)>=0);fake.closes++;return close(fd);}
static EGLImageKHR EGLAPIENTRY image(EGLDisplay d,EGLContext c,EGLenum type,EGLClientBuffer b,const EGLint *a){
 assert(d&&!c&&!b&&type==EGL_LINUX_DMA_BUF_EXT&&fake.use>0);fake.imports++;
 int fd=-1,off=0,pitch=0;for(int i=0;a[i]!=EGL_NONE;i+=2){
  if(a[i]==EGL_DMA_BUF_PLANE0_FD_EXT)fd=a[i+1];
  if(a[i]==EGL_DMA_BUF_PLANE0_OFFSET_EXT)off=a[i+1];
  if(a[i]==EGL_DMA_BUF_PLANE0_PITCH_EXT)pitch=a[i+1];
 }
 assert(fd>=0&&fcntl(fd,F_GETFD)>=0&&off>=4096&&pitch==32);
 return fake.fail_image||fake.fail_image_on==fake.imports?EGL_NO_IMAGE_KHR:(EGLImageKHR)123;
}
static EGLBoolean EGLAPIENTRY destroy(EGLDisplay d,EGLImageKHR i){assert(d&&i==(void *)123);fake.destroys++;return fake.fail_destroy?EGL_FALSE:EGL_TRUE;}
static void target(GLenum t,void *i){assert(t==GL_TEXTURE_2D&&i==(void *)123&&(fake.texture==90||fake.texture==91)&&fake.use>0);}
static void sf_cgl_load_present(void){sf_present.createImage=image;sf_present.destroyImage=destroy;sf_present.targetTexture=target;}
static void *mock_dlopen(const char *name,int mode){
 assert(!strcmp(name,"/System/Library/Frameworks/IOSurface.framework/IOSurface")&&mode==(RTLD_NOW|RTLD_LOCAL));return (void *)1;
}
static void *mock_dlsym(void *lib,const char *name){
 assert(lib==(void *)1);
#define SYM(n,fn) if(!strcmp(name,n))return (void *)fn;
 SYM("IOSurfaceGetPixelFormat",pixel) SYM("IOSurfaceGetPlaneCount",count)
 SYM("IOSurfaceGetWidthOfPlane",width) SYM("IOSurfaceGetHeightOfPlane",height)
 SYM("IOSurfaceGetBytesPerRowOfPlane",row) SYM("IOSurfaceGetBytesPerElementOfPlane",element)
 SYM("IOSurfaceSFGPUGetOffset",offset) SYM("IOSurfaceSFGPUGetPlaneOffset",plane_offset)
 SYM("IOSurfaceSFGPUCopyFD",copy_fd) SYM("IOSurfaceIncrementUseCount",inc) SYM("IOSurfaceDecrementUseCount",dec)
#undef SYM
 return NULL;
}
GLenum glGetError(void){GLenum e=fake.error;fake.error=0;return e;}
void glGetIntegerv(GLenum p,GLint *v){
 if(p==GL_TEXTURE_BINDING_2D)*v=fake.texture;
 else if(p==GL_READ_FRAMEBUFFER_BINDING)*v=fake.read;
 else if(p==GL_DRAW_FRAMEBUFFER_BINDING)*v=fake.draw;
 else assert(0);
}
void glGetTexLevelParameteriv(GLenum t,GLint level,GLenum p,GLint *v){
 assert(t==GL_TEXTURE_2D&&!level&&(fake.texture==7||(fake.pair&&fake.texture==8)));
 if(p==GL_TEXTURE_WIDTH)*v=fake.tw;else if(p==GL_TEXTURE_HEIGHT)*v=fake.th;
 else if(p==GL_TEXTURE_INTERNAL_FORMAT)*v=fake.pair&&fake.texture==8?(fake.wrong_second_storage?GL_RGBA8:0x822b):fake.storage;else assert(0);
}
GLboolean glIsEnabled(GLenum p){assert(p==GL_SCISSOR_TEST||p==GL_FRAMEBUFFER_SRGB);return p==GL_SCISSOR_TEST?fake.scissor:fake.srgb;}
void glEnable(GLenum p){assert(p==GL_SCISSOR_TEST||p==GL_FRAMEBUFFER_SRGB);if(p==GL_SCISSOR_TEST)fake.scissor=1;else fake.srgb=1;}
void glDisable(GLenum p){assert(p==GL_SCISSOR_TEST||p==GL_FRAMEBUFFER_SRGB);if(p==GL_SCISSOR_TEST)fake.scissor=0;else fake.srgb=0;}
void glGenTextures(GLsizei n,GLuint *v){assert(n==1&&fake.use>0&&fake.generated_textures<2);*v=90+(unsigned)fake.generated_textures++;}
void glBindTexture(GLenum t,GLuint v){assert(t==GL_TEXTURE_2D);if(fake.pair&&v==8)assert(fake.use>0);fake.texture=(GLint)v;}
void glDeleteTextures(GLsizei n,const GLuint *v){assert(n==1&&(*v==90||*v==91)&&fake.finishes>=1);fake.deletes++;}
void glGenFramebuffers(GLsizei n,GLuint *v){assert(n==2);v[0]=91;v[1]=92;}
void glBindFramebuffer(GLenum t,GLuint v){if(t==GL_READ_FRAMEBUFFER)fake.read=v;else if(t==GL_DRAW_FRAMEBUFFER)fake.draw=v;else assert(0);}
void glFramebufferTexture2D(GLenum t,GLenum attachment,GLenum target_,GLuint tex,GLint level){
 assert(attachment==GL_COLOR_ATTACHMENT0&&target_==GL_TEXTURE_2D&&!level&&fake.use>0);
 assert((t==GL_READ_FRAMEBUFFER&&fake.read==91&&(tex==90||tex==91))||(t==GL_DRAW_FRAMEBUFFER&&fake.draw==92&&(tex==7||(fake.pair&&tex==8))));
}
GLenum glCheckFramebufferStatus(GLenum t){assert(t==GL_READ_FRAMEBUFFER||t==GL_DRAW_FRAMEBUFFER);return fake.fail_fbo?GL_FRAMEBUFFER_UNSUPPORTED:GL_FRAMEBUFFER_COMPLETE;}
void glBlitFramebuffer(GLint a,GLint b,GLint c,GLint d,GLint e,GLint f,GLint g,GLint h,GLbitfield mask,GLenum filter){
 assert(!a&&!b&&!e&&!f&&c==16&&d==8&&g==16&&h==8&&mask==GL_COLOR_BUFFER_BIT&&filter==GL_NEAREST);
 assert(fake.use>0&&(fake.texture==90||fake.texture==91)&&fake.read==91&&fake.draw==92&&!fake.scissor&&!fake.srgb);
 fake.blits++;if(fake.fail_blit||fake.fail_blit_on==fake.blits)fake.error=GL_INVALID_OPERATION;
}
void glDeleteFramebuffers(GLsizei n,const GLuint *v){assert(n==2&&v[0]==91&&v[1]==92&&fake.finishes>=1);}
void glFinish(void){assert(fake.use>0);if(fake.finish_blits)assert(fake.blits==fake.finish_blits&&!fake.deletes&&!fake.destroys);fake.finishes++;if(fake.fail_finish==1||fake.fail_finish==fake.finishes)fake.error=GL_INVALID_OPERATION;}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
#define PROC(n) if(!strcmp(name,#n))return (__eglMustCastToProperFunctionPointerType)n;
 PROC(glGetError) PROC(glGetIntegerv) PROC(glGetTexLevelParameteriv) PROC(glIsEnabled)
 PROC(glEnable) PROC(glDisable) PROC(glGenTextures) PROC(glBindTexture) PROC(glDeleteTextures)
 PROC(glGenFramebuffers) PROC(glBindFramebuffer) PROC(glFramebufferTexture2D)
 PROC(glCheckFramebufferStatus) PROC(glBlitFramebuffer) PROC(glDeleteFramebuffers) PROC(glFinish)
#undef PROC
 return NULL;
}
#define SF_CGL_USE_EGL 1
#define dlopen mock_dlopen
#define dlsym mock_dlsym
#define close mock_close
#include "../strict-nv12-plane.h"
#undef close
#undef dlopen
#undef dlsym
static void state_restored(void){assert(fake.texture==7&&fake.read==41&&fake.draw==42&&fake.scissor&&fake.srgb);}
int main(void){
 source_fd=open("/dev/null",O_RDONLY);assert(source_fd>=0);
 reset();assert(CGLPDCopyNV12PlaneToTexture2D((void *)1,0)==1);state_restored();
 assert(fake.inc==1&&fake.dec==1&&!fake.use&&fake.blits==1&&fake.closes==1&&fake.destroys==1);
 reset();fake.element=2;fake.storage=0x822b;assert(CGLPDCopyNV12PlaneToTexture2D((void *)1,1)==1);state_restored();
 for(unsigned mode=0;mode<8;mode++){
  reset();switch(mode){case 0:fake.pixel=0x78343230;break;case 1:fake.offset=0;break;
   case 2:fake.row=1;break;case 3:fake.element=2;break;case 4:fake.tw=15;break;
   case 5:fake.storage=GL_RGBA8;break;case 6:fake.texture=0;break;case 7:fake.error=GL_INVALID_ENUM;break;}
  assert(CGLPDCopyNV12PlaneToTexture2D((void *)1,0)==0);assert(!fake.inc&&!fake.blits&&!fake.imports);
 }
 for(unsigned mode=0;mode<5;mode++){
  reset();if(mode==0)fake.fail_fd=1;if(mode==1)fake.fail_image=1;
  if(mode==2)fake.fail_fbo=1;if(mode==3)fake.fail_blit=1;if(mode==4)fake.fail_destroy=1;
  assert(CGLPDCopyNV12PlaneToTexture2D((void *)1,0)==0);state_restored();
  assert(fake.inc==1&&fake.dec==1&&!fake.use&&fake.finishes==1);
 }
 reset();fake.fail_finish=1;assert(CGLPDCopyNV12PlaneToTexture2D((void *)1,0)==0);state_restored();
 assert(fake.inc==1&&!fake.dec&&fake.use==1);uint64_t stats[4];CGLPDNV12CopyStats(stats);
 assert(stats[1]==2&&stats[2]==384&&stats[3]==1&&sf_gpu_stats[2]==384);
 reset();fake.pair=1;unsigned textures[2]={7,8};
#ifdef SF_NV12_BATCHED_PAIR
 fake.finish_blits=2;
#endif
 assert(CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
#ifdef SF_NV12_BATCHED_PAIR
 assert(fake.inc==1&&fake.dec==1&&!fake.use&&fake.blits==2&&fake.finishes==1&&fake.deletes==2&&fake.destroys==2);
 reset();fake.pair=1;fake.fail_finish=1;
 assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
 assert(fake.inc==1&&!fake.dec&&fake.use==1&&fake.blits==2&&fake.finishes==1);
 reset();fake.pair=1;fake.wrong_second_storage=1;
 assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
 assert(fake.inc==1&&fake.dec==1&&!fake.use&&!fake.blits&&!fake.finishes);
 for(unsigned mode=0;mode<5;mode++){
  reset();fake.pair=1;if(mode==0)fake.fail_fd=1;if(mode==1)fake.fail_image=1;
  if(mode==2)fake.fail_fbo=1;if(mode==3)fake.fail_blit=1;if(mode==4)fake.fail_destroy=1;
  assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
  assert(fake.inc==1&&fake.dec==1&&!fake.use&&fake.finishes==1);
 }
 /* A second-plane failure must complete the first plane before releasing the
  * source. It must not publish a successful whole-frame counter. */
 for(unsigned mode=0;mode<3;mode++){
  reset();fake.pair=1;
  if(mode==0)fake.fail_fd_on=2;if(mode==1)fake.fail_image_on=2;if(mode==2)fake.fail_blit_on=2;
  fake.finish_blits=mode==2?2:1;
  CGLPDNV12CopyStats(stats);uint64_t successful_before=stats[1];
  assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
  assert(fake.inc==1&&fake.dec==1&&!fake.use&&fake.finishes==1&&fake.fd_copies==2);
  assert(fake.destroys==(mode==2?2:1)&&fake.deletes==(mode==2?2:1));
  CGLPDNV12CopyStats(stats);assert(stats[1]==successful_before);
 }

#else
 assert(fake.inc==3&&fake.dec==3&&!fake.use&&fake.blits==2&&fake.finishes==2);
 reset();fake.pair=1;fake.fail_finish=1;
 assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
 assert(fake.inc==2&&fake.dec==1&&fake.use==1&&fake.blits==1&&fake.finishes==1);
 reset();fake.pair=1;fake.fail_finish=2;
 assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
 assert(fake.inc==3&&fake.dec==2&&fake.use==1&&fake.blits==2&&fake.finishes==2);
 reset();fake.pair=1;fake.wrong_second_storage=1;
 assert(!CGLPDCopyNV12ToTextures2D((void *)1,textures));state_restored();
 assert(fake.inc==2&&fake.dec==2&&!fake.use&&fake.blits==1&&fake.finishes==1);
#endif
 reset();assert(!CGLPDCopyNV12PlaneToTexture2D(NULL,0));assert(!CGLPDCopyNV12PlaneToTexture2D((void *)1,2));
 void *saved=sf_cgl_current;sf_cgl_current=NULL;assert(!CGLPDCopyNV12PlaneToTexture2D((void *)1,0));sf_cgl_current=saved;
 __typeof__(sf_nv12_gl.glBlitFramebuffer) blit=sf_nv12_gl.glBlitFramebuffer;sf_nv12_gl.glBlitFramebuffer=NULL;
 assert(!CGLPDCopyNV12PlaneToTexture2D((void *)1,0));sf_nv12_gl.glBlitFramebuffer=blit;
 close(source_fd);puts("strict NV12 literal policy/state/failure controls PASS; no GPU executed");return 0;
}
