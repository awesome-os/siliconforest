/* SPDX-License-Identifier: MIT
 * Private WindowServer GPU composition candidate. Public ABI declarations are
 * local; no Apple SDK is used. Caller serializes with the WS pixels lock.
 * CPU window decorations upload only when changed. Layer pixels stay on GPU.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/time.h>
#ifdef __aarch64__
#include <arm_neon.h>
#endif

// Equality needs no ordering. The optional NEON scan checks every byte while
// avoiding the installed scalar memcmp hot path; no dirty-base assumption.
static int sfGPUBaseEqual(const void *left,const void *right,size_t bytes){
 const unsigned char *a=left,*b=right;size_t i=0;
#ifdef __aarch64__
 if(getenv("SFFX_GPU_VECTOR_COMPARE")){
  uint8x16_t x0=vdupq_n_u8(0),x1=x0,x2=x0,x3=x0;
  for(;bytes-i>=64;i+=64){
   x0=vorrq_u8(x0,veorq_u8(vld1q_u8(a+i),vld1q_u8(b+i)));
   x1=vorrq_u8(x1,veorq_u8(vld1q_u8(a+i+16),vld1q_u8(b+i+16)));
   x2=vorrq_u8(x2,veorq_u8(vld1q_u8(a+i+32),vld1q_u8(b+i+32)));
   x3=vorrq_u8(x3,veorq_u8(vld1q_u8(a+i+48),vld1q_u8(b+i+48)));
  }
  if(vmaxvq_u8(vorrq_u8(vorrq_u8(x0,x1),vorrq_u8(x2,x3))))return 0;
  for(;i<bytes;i++)if(a[i]!=b[i])return 0;
  return 1;
 }
#endif
 return memcmp(left,right,bytes)==0;
}

typedef unsigned SFGLuint,SFGLenum;
typedef int SFGLint,SFGLsizei;
static struct {
 int (*CGLChoosePixelFormat)(const int *,void **,int *);
 int (*CGLCreateContext)(void *,void *,void **);
 int (*CGLSetCurrentContext)(void *);
 void *(*CGLGetCurrentContext)(void);
 int (*CGLDestroyContext)(void *),(*CGLDestroyPixelFormat)(void *);
 int (*CGLTexImageIOSurface2D)(void *,unsigned,unsigned,int,int,unsigned,unsigned,void *,unsigned);
 void (*glGenTextures)(int,unsigned *),(*glBindTexture)(unsigned,unsigned),(*glDeleteTextures)(int,const unsigned *);
 void (*glTexImage2D)(unsigned,int,int,int,int,int,unsigned,unsigned,const void *);
 void (*glTexSubImage2D)(unsigned,int,int,int,int,int,unsigned,unsigned,const void *);
 void (*glTexParameteri)(unsigned,unsigned,int),(*glPixelStorei)(unsigned,int);
 void (*glGenFramebuffers)(int,unsigned *),(*glBindFramebuffer)(unsigned,unsigned),(*glDeleteFramebuffers)(int,const unsigned *);
 void (*glFramebufferTexture2D)(unsigned,unsigned,unsigned,unsigned,int);
 unsigned (*glCheckFramebufferStatus)(unsigned),(*glGetError)(void);
 const unsigned char *(*glGetString)(unsigned);
 void (*glReadPixels)(int,int,int,int,unsigned,unsigned,void *);
 void (*glViewport)(int,int,int,int),(*glClearColor)(float,float,float,float),(*glClear)(unsigned);
 void (*glDisable)(unsigned),(*glEnable)(unsigned),(*glBlendFunc)(unsigned,unsigned);
 void (*glMatrixMode)(unsigned),(*glLoadIdentity)(void),(*glOrtho)(double,double,double,double,double,double);
 void (*glBegin)(unsigned),(*glEnd)(void),(*glVertex2f)(float,float),(*glTexCoord2f)(float,float),(*glColor4f)(float,float,float,float),(*glFinish)(void);
 void *(*IOSurfaceCreate)(CFDictionaryRef);
 void *(*IOSurfaceGetBaseAddress)(void *);
 int (*IOSurfaceSFGPUCopyFD)(void *);
 size_t (*IOSurfaceSFGPUGetOffset)(void *),(*IOSurfaceGetWidth)(void *),(*IOSurfaceGetHeight)(void *),(*IOSurfaceGetBytesPerRow)(void *);
 int ready;
} sfGPU;

static int sfGPULoad(void){
 if(sfGPU.ready)return sfGPU.ready>0;
 sfGPU.ready=-1;
 void *gl=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_GLOBAL);
 void *surface=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",RTLD_NOW|RTLD_GLOBAL);
 if(!gl||!surface)return 0;
#define SFLOAD(lib,name) do{sfGPU.name=dlsym(lib,#name);if(!sfGPU.name)return 0;}while(0)
 SFLOAD(gl,CGLChoosePixelFormat);SFLOAD(gl,CGLCreateContext);SFLOAD(gl,CGLSetCurrentContext);SFLOAD(gl,CGLGetCurrentContext);
 SFLOAD(gl,CGLDestroyContext);SFLOAD(gl,CGLDestroyPixelFormat);SFLOAD(gl,CGLTexImageIOSurface2D);
 SFLOAD(gl,glGenTextures);SFLOAD(gl,glBindTexture);SFLOAD(gl,glDeleteTextures);SFLOAD(gl,glTexImage2D);SFLOAD(gl,glTexSubImage2D);
 SFLOAD(gl,glTexParameteri);SFLOAD(gl,glPixelStorei);SFLOAD(gl,glGenFramebuffers);SFLOAD(gl,glBindFramebuffer);SFLOAD(gl,glDeleteFramebuffers);
 SFLOAD(gl,glFramebufferTexture2D);SFLOAD(gl,glCheckFramebufferStatus);SFLOAD(gl,glGetError);SFLOAD(gl,glGetString);
 SFLOAD(gl,glReadPixels);SFLOAD(gl,glViewport);SFLOAD(gl,glClearColor);SFLOAD(gl,glClear);SFLOAD(gl,glDisable);SFLOAD(gl,glEnable);SFLOAD(gl,glBlendFunc);
 SFLOAD(gl,glMatrixMode);SFLOAD(gl,glLoadIdentity);SFLOAD(gl,glOrtho);SFLOAD(gl,glBegin);SFLOAD(gl,glEnd);
 SFLOAD(gl,glVertex2f);SFLOAD(gl,glTexCoord2f);SFLOAD(gl,glColor4f);SFLOAD(gl,glFinish);
 SFLOAD(surface,IOSurfaceGetBaseAddress);SFLOAD(surface,IOSurfaceCreate);SFLOAD(surface,IOSurfaceSFGPUCopyFD);SFLOAD(surface,IOSurfaceSFGPUGetOffset);
 SFLOAD(surface,IOSurfaceGetWidth);SFLOAD(surface,IOSurfaceGetHeight);SFLOAD(surface,IOSurfaceGetBytesPerRow);
#undef SFLOAD
 sfGPU.ready=1;return 1;
}

#define SF_GPU_LAYERS 16
struct sfGPULayer {uintptr_t key;void *surface;unsigned texture;double sx,sy,sw,sh,dx,dy,dw,dh;};
struct sfGPUPresenter {
 void *context,*format;unsigned baseTexture,fbo;void *baseCache;size_t baseBytes;int baseW,baseH,failed;
 struct sfGPULayer layers[SF_GPU_LAYERS];int count;
 uint64_t frames,baseUploadBytes,layerBytes,failures;
};
struct sfGPUFrame {struct sfGPUPresenter *owner;void *surface;unsigned texture;int w,h;};

static struct sfGPUPresenter *sfGPUCreate(void){
 if(!sfGPULoad())return NULL;
 struct sfGPUPresenter *p=calloc(1,sizeof(*p));if(!p)return NULL;
 int attributes[]={8,24,11,8,73,96,0},count=0;
 void *previous=sfGPU.CGLGetCurrentContext();
 int status=sfGPU.CGLChoosePixelFormat(attributes,&p->format,&count);
 if(!status)status=sfGPU.CGLCreateContext(p->format,NULL,&p->context);
 if(!status)status=sfGPU.CGLSetCurrentContext(p->context);
 const char *renderer=!status?(const char *)sfGPU.glGetString(0x1f01):NULL;
 int good=renderer&&strstr(renderer,"Apple M1");
 if(good){sfGPU.glGenTextures(1,&p->baseTexture);sfGPU.glGenFramebuffers(1,&p->fbo);}
 sfGPU.CGLSetCurrentContext(previous);
 if(!good){if(p->context)sfGPU.CGLDestroyContext(p->context);if(p->format)sfGPU.CGLDestroyPixelFormat(p->format);free(p);return NULL;}
 return p;
}

static void sfGPUFrameDestroy(struct sfGPUFrame *f){
 if(!f)return;
 void *previous=sfGPU.CGLGetCurrentContext();sfGPU.CGLSetCurrentContext(f->owner->context);
 if(f->texture)sfGPU.glDeleteTextures(1,&f->texture);
 if(f->surface)CFRelease(f->surface);
 sfGPU.CGLSetCurrentContext(previous);free(f);
}

static struct sfGPUFrame *sfGPUFrameCreate(struct sfGPUPresenter *p,int w,int h){
 if(!p||p->failed||w<=0||h<=0||w>16384||h>16384||(uint64_t)w*h*4>UINT64_C(1073741824))return NULL;
 CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 if(!d)return NULL;
 CFStringRef keys[]={CFSTR("IOSurfaceWidth"),CFSTR("IOSurfaceHeight"),CFSTR("IOSurfaceBytesPerElement"),CFSTR("IOSurfacePixelFormat")};
 int values[]={w,h,4,0x42475241};
 for(int i=0;i<4;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);if(!n){CFRelease(d);return NULL;}CFDictionarySetValue(d,keys[i],n);CFRelease(n);}
 void *surface=sfGPU.IOSurfaceCreate(d);CFRelease(d);
 if(!surface||!sfGPU.IOSurfaceSFGPUGetOffset(surface)){if(surface)CFRelease(surface);return NULL;}
 struct sfGPUFrame *f=calloc(1,sizeof(*f));if(!f){CFRelease(surface);return NULL;}
 f->owner=p;f->surface=surface;f->w=w;f->h=h;
 void *previous=sfGPU.CGLGetCurrentContext();sfGPU.CGLSetCurrentContext(p->context);sfGPU.glGenTextures(1,&f->texture);sfGPU.CGLSetCurrentContext(previous);
 return f;
}

static int sfGPUSetLayer(struct sfGPUPresenter *p,uintptr_t key,void *surface,
 double sx,double sy,double sw,double sh,double dx,double dy,double dw,double dh){
 if(!p||p->failed||!key)return 0;
 int i;for(i=0;i<p->count;i++)if(p->layers[i].key==key)break;
 if(!surface){
  if(i<p->count){
   void *previous=sfGPU.CGLGetCurrentContext();sfGPU.CGLSetCurrentContext(p->context);
   if(p->layers[i].texture)sfGPU.glDeleteTextures(1,&p->layers[i].texture);
   CFRelease(p->layers[i].surface);p->layers[i]=p->layers[--p->count];sfGPU.CGLSetCurrentContext(previous);
  }
  return 1;
 }
 if(!sfGPU.IOSurfaceSFGPUGetOffset(surface)||sw<=0||sh<=0||dw<=0||dh<=0||i==SF_GPU_LAYERS)return 0;
 if(i==p->count)p->count++;
 struct sfGPULayer *l=&p->layers[i];
 if(surface!=l->surface){CFRetain(surface);if(l->surface)CFRelease(l->surface);l->surface=surface;}
 l->key=key;l->sx=sx;l->sy=sy;l->sw=sw;l->sh=sh;l->dx=dx;l->dy=dy;l->dw=dw;l->dh=dh;
 return 1;
}

static void sfGPUQuad(double x,double y,double w,double h,double sx,double sy,double sw,double sh){
 sfGPU.glBegin(7); // GL_QUADS, in this private compatibility context
 sfGPU.glTexCoord2f(sx,sy);sfGPU.glVertex2f(x,y);
 sfGPU.glTexCoord2f(sx+sw,sy);sfGPU.glVertex2f(x+w,y);
 sfGPU.glTexCoord2f(sx+sw,sy+sh);sfGPU.glVertex2f(x+w,y+h);
 sfGPU.glTexCoord2f(sx,sy+sh);sfGPU.glVertex2f(x,y+h);sfGPU.glEnd();
}

static void sfGPUTextureParameters(void){
 sfGPU.glTexParameteri(0x0de1,0x2801,0x2600);sfGPU.glTexParameteri(0x0de1,0x2800,0x2600);
 sfGPU.glTexParameteri(0x0de1,0x2802,0x812f);sfGPU.glTexParameteri(0x0de1,0x2803,0x812f);
}

static double sfGPUClockMs(void){
 struct timeval t;gettimeofday(&t,NULL);return t.tv_sec*1000.0+t.tv_usec/1000.0;
}

static int sfGPUCompose(struct sfGPUPresenter *p,struct sfGPUFrame *f,const void *base,int w,int h){
 if(!p||p->failed||!f||!base||f->w!=w||f->h!=h)return 0;
 int trace=getenv("SFFX_GPU_PHASE_TRACE")!=NULL;
 double started=trace?sfGPUClockMs():0,imported=started,baseDone=started,drawDone=started,finished=started;
 void *previous=sfGPU.CGLGetCurrentContext();int good=sfGPU.CGLSetCurrentContext(p->context)==0;
 if(!good)return 0;
 for(int clear=0;clear<8&&sfGPU.glGetError();clear++){} // this dedicated context never carries another caller's errors
 sfGPU.glBindTexture(0x0de1,f->texture);
 good=sfGPU.CGLTexImageIOSurface2D(p->context,0x0de1,0x1908,w,h,0x80e1,0x8367,f->surface,0)==0;
 sfGPU.glBindFramebuffer(0x8d40,p->fbo);sfGPU.glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,f->texture,0);
 good=good&&sfGPU.glCheckFramebufferStatus(0x8d40)==0x8cd5;
 if(trace)imported=sfGPUClockMs();
 if(good){
  size_t bytes=(size_t)w*h*4;
  sfGPU.glBindTexture(0x0de1,p->baseTexture);sfGPU.glPixelStorei(0x0cf5,4);sfGPU.glPixelStorei(0x0cf2,0);
  if(p->baseW!=w||p->baseH!=h){
   void *cache=realloc(p->baseCache,bytes);
   if(!cache)good=0;
   else{p->baseCache=cache;p->baseBytes=bytes;p->baseW=w;p->baseH=h;
    sfGPU.glTexImage2D(0x0de1,0,0x8058,w,h,0,0x80e1,0x1401,base);memcpy(cache,base,bytes);p->baseUploadBytes+=bytes;}
  }else if(!sfGPUBaseEqual(p->baseCache,base,bytes)){
   sfGPU.glTexSubImage2D(0x0de1,0,0,0,w,h,0x80e1,0x1401,base);memcpy(p->baseCache,base,bytes);p->baseUploadBytes+=bytes;
  }
  if(trace)baseDone=sfGPUClockMs();
  sfGPUTextureParameters();
  sfGPU.glViewport(0,0,w,h);sfGPU.glDisable(0x0b71);sfGPU.glDisable(0x0b90);sfGPU.glDisable(0x0c11);sfGPU.glDisable(0x0be2);
  sfGPU.glDisable(0x8db9);sfGPU.glClearColor(0,0,0,0);sfGPU.glClear(0x4000);
  sfGPU.glMatrixMode(0x1701);sfGPU.glLoadIdentity();sfGPU.glOrtho(0,w,0,h,-1,1);
  sfGPU.glMatrixMode(0x1700);sfGPU.glLoadIdentity();sfGPU.glEnable(0x0de1);sfGPU.glColor4f(1,1,1,1);
  sfGPUQuad(0,0,w,h,0,0,1,1);
  sfGPU.glEnable(0x0be2);sfGPU.glBlendFunc(1,0x0303); // premultiplied source-over
  for(int i=0;i<p->count&&good;i++){
   struct sfGPULayer *l=&p->layers[i];int lw=(int)sfGPU.IOSurfaceGetWidth(l->surface),lh=(int)sfGPU.IOSurfaceGetHeight(l->surface);
   if(!l->texture)sfGPU.glGenTextures(1,&l->texture);sfGPU.glBindTexture(0x0de1,l->texture);
   good=lw>0&&lh>0&&sfGPU.CGLTexImageIOSurface2D(p->context,0x0de1,0x1908,lw,lh,0x80e1,0x8367,l->surface,0)==0;
   if(good){sfGPUTextureParameters();sfGPUQuad(l->dx,l->dy,l->dw,l->dh,l->sx/lw,l->sy/lh,l->sw/lw,l->sh/lh);p->layerBytes+=(uint64_t)lw*lh*4;}
  }
  if(trace)drawDone=sfGPUClockMs();
  sfGPU.glFinish();good=good&&sfGPU.glGetError()==0;
  if(trace)finished=sfGPUClockMs();
  if(getenv("SFFX_GPU_VERIFY")){
   unsigned char a[4]={0},b[4]={0};sfGPU.glReadPixels(w/4,h/2,1,1,0x1908,0x1401,a);sfGPU.glReadPixels(w*3/4,h/2,1,1,0x1908,0x1401,b);
   const unsigned char *raw=sfGPU.IOSurfaceGetBaseAddress(f->surface);size_t stride=sfGPU.IOSurfaceGetBytesPerRow(f->surface);
   if(raw){const unsigned char *l=raw+(h/2)*stride+(w/4)*4,*r=raw+(h/2)*stride+(w*3/4)*4;
    fprintf(stderr,"FXCOMPOSERAW stride=%zu offset=%zu leftBGRA=%u,%u,%u,%u rightBGRA=%u,%u,%u,%u verification_pixels_only=1\n",stride,sfGPU.IOSurfaceSFGPUGetOffset(f->surface),l[0],l[1],l[2],l[3],r[0],r[1],r[2],r[3]);}
   const unsigned char *cpu=base;
   fprintf(stderr,"FXCOMPOSEVERIFY size=%dx%d baseBGRA=%u,%u,%u,%u outputLeftRGBA=%u,%u,%u,%u rightRGBA=%u,%u,%u,%u verification_pixels_only=1\n",w,h,cpu[0],cpu[1],cpu[2],cpu[3],a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);
  }
 }
 sfGPU.CGLSetCurrentContext(previous);
 if(trace)fprintf(stderr,"FXGPUPHASE context=%p frame=%llu epoch_ms=%.3f import_ms=%.3f base_ms=%.3f draw_ms=%.3f finish_ms=%.3f restore_ms=%.3f total_ms=%.3f success=%d\n",
  p->context,(unsigned long long)(p->frames+1),started,imported-started,baseDone-imported,drawDone-baseDone,finished-drawDone,sfGPUClockMs()-finished,sfGPUClockMs()-started,good);
 if(good)p->frames++;else{p->failed=1;p->failures++;}
 return good;
}

static void sfGPUDestroy(struct sfGPUPresenter *p){
 if(!p)return;
 void *previous=sfGPU.CGLGetCurrentContext();sfGPU.CGLSetCurrentContext(p->context);
 for(int i=0;i<p->count;i++){if(p->layers[i].texture)sfGPU.glDeleteTextures(1,&p->layers[i].texture);if(p->layers[i].surface)CFRelease(p->layers[i].surface);}
 if(p->baseTexture)sfGPU.glDeleteTextures(1,&p->baseTexture);if(p->fbo)sfGPU.glDeleteFramebuffers(1,&p->fbo);
 sfGPU.CGLSetCurrentContext(previous);sfGPU.CGLDestroyContext(p->context);sfGPU.CGLDestroyPixelFormat(p->format);free(p->baseCache);free(p);
}
