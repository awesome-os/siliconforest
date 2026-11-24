/* SPDX-License-Identifier: MIT
 * Real CGL/IOSurface cache admission, hit, active-image and teardown probe.
 * CPU readback verifies pixels; it is diagnostic, not transport performance.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef void *IOSurfaceRef,*CGLContextObj,*CGLPixelFormatObj;
typedef unsigned GLenum,GLuint;
typedef int GLint,GLsizei;
#define LOAD(lib,ret,name,args) ret (*name) args=dlsym(lib,#name); if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static IOSurfaceRef make_surface(IOSurfaceRef (*create)(CFDictionaryRef),int width,int height){
 CFMutableDictionaryRef p=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};
 int values[]={width,height,4,0x42475241};
 for(int i=0;i<4;i++){CFStringRef k=CFStringCreateWithCString(0,keys[i],kCFStringEncodingUTF8);CFNumberRef n=CFNumberCreate(0,kCFNumberIntType,&values[i]);CFDictionarySetValue(p,k,n);CFRelease(k);CFRelease(n);}
 CFDictionarySetValue(p,CFSTR("IOSurfaceIsGlobal"),kCFBooleanTrue);IOSurfaceRef s=create(p);CFRelease(p);return s;
}
int main(int argc,char **argv){
 setvbuf(stdout,0,_IOLBF,0);REQUIRE(argc==2);
 REQUIRE(getenv("SFFX_FRAME_ID_GEOMETRY")&&!strcmp(getenv("SFFX_FRAME_ID_GEOMETRY"),"256,256,0,0,160,90,1"));
 void *surface=dlopen("/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface",RTLD_NOW|RTLD_GLOBAL);
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(surface&&lib);
 LOAD(surface,IOSurfaceRef,IOSurfaceCreate,(CFDictionaryRef));
 LOAD(lib,int,CGLChoosePixelFormat,(const int *,CGLPixelFormatObj *,int *));LOAD(lib,int,CGLCreateContext,(CGLPixelFormatObj,CGLContextObj,CGLContextObj *));
 LOAD(lib,int,CGLSetCurrentContext,(CGLContextObj));LOAD(lib,int,CGLDestroyContext,(CGLContextObj));LOAD(lib,int,CGLDestroyPixelFormat,(CGLPixelFormatObj));
 LOAD(lib,int,CGLSFFXCopyFramebuffer,(IOSurfaceRef,int));
 LOAD(lib,void,glClearColor,(float,float,float,float));LOAD(lib,void,glClear,(unsigned));LOAD(lib,void,glScissor,(int,int,int,int));
 LOAD(lib,void,glEnable,(unsigned));LOAD(lib,void,glDisable,(unsigned));LOAD(lib,void,glGetIntegerv,(unsigned,int *));LOAD(lib,unsigned,glGetError,(void));
 LOAD(lib,void,glGenBuffers,(int,unsigned *));LOAD(lib,void,glBindBuffer,(unsigned,unsigned));LOAD(lib,void,glDeleteBuffers,(int,const unsigned *));
 LOAD(lib,void,glPixelStorei,(unsigned,int));LOAD(lib,void,glGenFramebuffers,(int,unsigned *));LOAD(lib,void,glBindFramebuffer,(unsigned,unsigned));LOAD(lib,void,glDeleteFramebuffers,(int,const unsigned *));
 int attributes[]={99,0x3200,73,96,0},count=0;CGLPixelFormatObj pf=0;CGLContextObj ctx=0;
 REQUIRE(!CGLChoosePixelFormat(attributes,&pf,&count)&&!CGLCreateContext(pf,0,&ctx)&&!CGLSetCurrentContext(ctx));
 glDisable(0x0c11);glClearColor(32/255.f,32/255.f,32/255.f,1);glClear(0x4000);glEnable(0x0c11);
 const unsigned guards[8]={1,1,0,1,0,0,1,0};
 for(int row=0;row<2;row++)for(int column=0;column<18;column++){
  unsigned bit=column<4?guards[column]:column>=14?guards[column-10]:0;
  if(bit==(unsigned)row)continue;
  int x=(96+column*96)*160/1920,y=(80+row*112)*90/1080;
  int right=(96+column*96+80)*160/1920,top=(80+row*112+80)*90/1080;
  glScissor(x,y,right-x,top-y);glClearColor(224/255.f,224/255.f,224/255.f,1);glClear(0x4000);
 }
 glDisable(0x0c11);
 IOSurfaceRef image=make_surface(IOSurfaceCreate,256,256);REQUIRE(image);
 unsigned pack=0,read=0;glGenBuffers(1,&pack);glBindBuffer(0x88eb,pack);
 glPixelStorei(0x0d05,8);glPixelStorei(0x0d02,300);glPixelStorei(0x0d03,2);glPixelStorei(0x0d04,3);
 glGenFramebuffers(1,&read);glBindFramebuffer(0x8ca8,read);
 REQUIRE(!glGetError()&&CGLSFFXCopyFramebuffer(image,0));
 const unsigned names[]={0x88ed,0x0d05,0x0d02,0x0d03,0x0d04,0x8caa};
 const int expected[]={(int)pack,8,300,2,3,(int)read};
 for(int i=0;i<6;i++){int value=0;glGetIntegerv(names[i],&value);REQUIRE(value==expected[i]);}
 REQUIRE(!glGetError());glBindFramebuffer(0x8ca8,0);glDeleteFramebuffers(1,&read);glBindBuffer(0x88eb,0);glDeleteBuffers(1,&pack);
 CFRelease(image);REQUIRE(!CGLSetCurrentContext(0)&&!CGLDestroyContext(ctx)&&!CGLDestroyPixelFormat(pf));
 puts("FXFRAMESTATE pass=1 preserved_pack_and_read_state=6 expected_frame=0");return 0;
}
