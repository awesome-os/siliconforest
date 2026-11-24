#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Adapt our reviewed exec/reuse probe in a private copy, never peer source."""
from pathlib import Path
import argparse
import json
import sys

args=argparse.ArgumentParser(description=__doc__)
args.add_argument('source',type=Path)
args.add_argument('--geometry',choices=['odd','1080p','4k'],default='odd')
a=args.parse_args()
width,height,stride={'odd':(65,33,128),'1080p':(1920,1080,2048),'4k':(3840,2160,4096)}[a.geometry]
uv_offset=6144 if a.geometry=='odd' else stride*(height+8)
allocation=max(16384,(uv_offset+stride*((height+1)//2+7)+16383)//16384*16384)
p=a.source;s=p.read_text()
def replace(old,new):
    global s
    if s.count(old)!=1:raise ValueError('Exact NV12 probe anchor required: '+old[:80])
    s=s.replace(old,new)

replace('#define W 65',f'#define W {width}')
replace('#define H 33',f'#define H {height}')
replace('#define STRIDE 128',f'#define STRIDE {stride}')
replace('#define UVOFF (STRIDE*48)',f'#define UVOFF {uv_offset}')
replace('#define ALLOC 16384',f'#define ALLOC {allocation}')
if s.count('layout?192:STRIDE')!=2:raise ValueError('Two top-stride anchors required')
s=s.replace('layout?192:STRIDE','layout?(STRIDE+64):STRIDE')
replace('odd_visible=%dx%d','visible=%dx%d')
replace('#include <dlfcn.h>','#include <dlfcn.h>\n#include <mach-o/loader.h>\n#include <mach-o/dyld.h>\n#include <time.h>\n#include <sys/resource.h>')
replace('static int width(int p){return p?(W+1)/2:W;}',r'''
static int images(void){
 unsigned gl=0,surface=0,cgl=0;
 for(uint32_t i=0;i<_dyld_image_count();i++){
  const char *name=_dyld_get_image_name(i);
  if(!name)continue;
  int selected=strstr(name,"/libGL.1.dylib")||strstr(name,"/IOSurface.framework/")||strstr(name,"/OpenGL.framework/");
  if(!selected)continue;
  const struct mach_header_64 *h=(const void *)_dyld_get_image_header(i);REQUIRE(h&&h->magic==MH_MAGIC_64);
  const unsigned char *cmd=(const unsigned char *)(h+1);const unsigned char *end=cmd+h->sizeofcmds;
  const unsigned char *uuid=NULL;
  for(uint32_t k=0;k<h->ncmds;k++){
   const struct load_command *l=(const void *)cmd;REQUIRE(cmd+sizeof(*l)<=end&&l->cmdsize>=sizeof(*l)&&l->cmdsize<=(size_t)(end-cmd));
   if(l->cmd==LC_UUID){REQUIRE(l->cmdsize>=24&&!uuid);uuid=cmd+8;}
   cmd+=l->cmdsize;
  }
  REQUIRE(uuid);printf("SFNV12STRICT image uuid=");for(int k=0;k<16;k++)printf("%02x",uuid[k]);printf(" path=%s\n",name);
  gl+=strstr(name,"/libGL.1.dylib")!=NULL;surface+=strstr(name,"/IOSurface.framework/")!=NULL;cgl+=strstr(name,"/OpenGL.framework/")!=NULL;
 }
 REQUIRE(gl==1&&surface==1&&cgl==1);return 0;
}
static int width(int p){return p?(W+1)/2:W;}''')

replace('LOAD(int,CGLTexImageIOSurface2D,(void *,unsigned,unsigned,int,int,unsigned,unsigned,void *,unsigned));',
        'LOAD(int,CGLPDCopyNV12ToTextures2D,(void *,const unsigned *));')
replace('LOAD(void,glTexParameteri,(unsigned,unsigned,int));LOAD(void,glPixelStorei,(unsigned,int));',r'''
 LOAD(void,glTexParameteri,(unsigned,unsigned,int));LOAD(void,glPixelStorei,(unsigned,int));
 LOAD(void,glTexImage2D,(unsigned,int,int,int,int,int,unsigned,unsigned,const void *));
 LOAD(void,glGetIntegerv,(unsigned,int *));LOAD(void,glEnable,(unsigned));LOAD(void,glDisable,(unsigned));
 LOAD(unsigned char,glIsEnabled,(unsigned));
 LOAD(void,glGenFramebuffers,(int,unsigned *));LOAD(void,glBindFramebuffer,(unsigned,unsigned));
 LOAD(void,glDeleteFramebuffers,(int,const unsigned *));
 LOAD(void,glGenBuffers,(int,unsigned *));LOAD(void,glBindBuffer,(unsigned,unsigned));
 LOAD(void,glBufferData,(unsigned,long,const void *,unsigned));LOAD(void,glDeleteBuffers,(int,const unsigned *));
''')
replace('uint64_t before[4],after[4];CGLSFFXTransferStats(before);unsigned compared=0;',r'''
 REQUIRE(!images());
 for(int p=0;p<2;p++){
  glBindTexture(0x0de1,texture[p]);glTexParameteri(0x0de1,0x2801,0x2600);glTexParameteri(0x0de1,0x2800,0x2600);
  glTexImage2D(0x0de1,0,p?0x822b:0x8229,width(p),height(p),0,p?0x8227:0x1903,0x1401,NULL);
 }
 unsigned original_fbo[2],pbo[2];glGenFramebuffers(2,original_fbo);glGenBuffers(2,pbo);
 glBindBuffer(0x88ec,pbo[0]);glBufferData(0x88ec,32768,NULL,0x88e0);
 glBindBuffer(0x88eb,pbo[1]);glBufferData(0x88eb,32768,NULL,0x88e0);
 glPixelStorei(0xcf2,19);glPixelStorei(0xcf5,8);glPixelStorei(0xcf3,3);glPixelStorei(0xcf4,5);
 glEnable(0xc11);glEnable(0x8db9);
 glBindFramebuffer(0x8ca8,original_fbo[0]);glBindFramebuffer(0x8ca9,original_fbo[1]);
 const unsigned state_names[]={0x8069,0x8caa,0x8ca6,0x88ef,0x88ed,0xcf2,0xcf5,0xcf3,0xcf4};
 int state_before[9],state_after[9];
 for(int k=0;k<9;k++)glGetIntegerv(state_names[k],&state_before[k]);
 REQUIRE(glGetError()==0);
 uint64_t before[4],after[4];CGLSFFXTransferStats(before);unsigned compared=0;
''')
replace('''  for(int p=0;p<2;p++){
   glBindTexture(0x84f5,texture[p]);glTexParameteri(0x84f5,0x2801,0x2600);glTexParameteri(0x84f5,0x2800,0x2600);
   unsigned format=p?0x8227:0x1903;
   REQUIRE(!CGLTexImageIOSurface2D(ctx,0x84f5,format,width(p),height(p),format,0x1401,s,p));
  }''',r'''
  struct rusage cpu_before={0},cpu_after={0};struct timespec wall_before={0},wall_after={0};
  int cpu_begin=getrusage(RUSAGE_SELF,&cpu_before),wall_begin=clock_gettime(CLOCK_MONOTONIC,&wall_before);
  REQUIRE(CGLPDCopyNV12ToTextures2D(s,texture)==1);
  int wall_end=clock_gettime(CLOCK_MONOTONIC,&wall_after),cpu_end=getrusage(RUSAGE_SELF,&cpu_after);
  int64_t wall_ns=(int64_t)(wall_after.tv_sec-wall_before.tv_sec)*1000000000+wall_after.tv_nsec-wall_before.tv_nsec;
  int64_t cpu_first=(int64_t)cpu_before.ru_utime.tv_sec*1000000+cpu_before.ru_utime.tv_usec+(int64_t)cpu_before.ru_stime.tv_sec*1000000+cpu_before.ru_stime.tv_usec;
  int64_t cpu_last=(int64_t)cpu_after.ru_utime.tv_sec*1000000+cpu_after.ru_utime.tv_usec+(int64_t)cpu_after.ru_stime.tv_sec*1000000+cpu_after.ru_stime.tv_usec;
  printf("SFNV12COPYCOST profile=%x revision=%d wall_ns=%lld wall_available=%d process_cpu_us=%lld cpu_available=%d\n",
    profile,revision,(long long)wall_ns,wall_begin==0&&wall_end==0&&wall_ns>=0,
    (long long)(cpu_last-cpu_first),cpu_begin==0&&cpu_end==0&&cpu_last>0&&cpu_last>=cpu_first);
  for(int k=0;k<9;k++){glGetIntegerv(state_names[k],&state_after[k]);REQUIRE(state_after[k]==state_before[k]);}
  REQUIRE(glIsEnabled(0xc11)&&glIsEnabled(0x8db9)&&glGetError()==0);
  REQUIRE(!CGLPDCopyNV12ToTextures2D(NULL,texture));
  unsigned bad[2]={texture[0],texture[0]};REQUIRE(!CGLPDCopyNV12ToTextures2D(s,bad));
''')
replace('''  for(int p=0;p<2;p++){
   unsigned char pixels[W*H*2]={0};glBindTexture(0x84f5,texture[p]);glGetTexImage(0x84f5,0,p?0x8227:0x1903,0x1401,pixels);''',r'''
  glBindBuffer(0x88eb,0); // Verification readback only, not transport.
  for(int p=0;p<2;p++){
   unsigned char *pixels=calloc((size_t)width(p)*height(p),p?2:1);REQUIRE(pixels);
   glBindTexture(0x0de1,texture[p]);glGetTexImage(0x0de1,0,p?0x8227:0x1903,0x1401,pixels);''')
replace('''    REQUIRE(pixels[(y*width(p)+x)*(p?2:1)+c]==sample(p,x,y,c,revision));compared++;
   }
  }''','''    REQUIRE(pixels[(y*width(p)+x)*(p?2:1)+c]==sample(p,x,y,c,revision));compared++;
   }
   free(pixels);
  }''')
replace('  REQUIRE(glGetError()==0&&write(fd,&overwritten,1)==1);',r'''
  glBindBuffer(0x88eb,pbo[1]);glBindTexture(0x0de1,(unsigned)state_before[0]);
  REQUIRE(glGetError()==0&&write(fd,&overwritten,1)==1);''')
replace(' glDeleteTextures(2,texture);REQUIRE(glGetError()==0&&IOSurfaceGetUseCount(s)==0&&!IOSurfaceIsInUse(s));',r'''
 glBindBuffer(0x88ec,0);glBindBuffer(0x88eb,0);glDeleteBuffers(2,pbo);
 glBindFramebuffer(0x8ca8,0);glBindFramebuffer(0x8ca9,0);glDeleteFramebuffers(2,original_fbo);
 glDisable(0xc11);glDisable(0x8db9);
 glDeleteTextures(2,texture);REQUIRE(glGetError()==0&&IOSurfaceGetUseCount(s)==0&&!IOSurfaceIsInUse(s));''')
replace('reuse_after_completion=1 local_use=0 global_in_use=0 pass=1',
        'reuse_after_completion=1 state_restored=9 paired_copy=1 local_use=0 global_in_use=0 pass=1')
s=s.replace('FXNV12','SFNV12STRICT').replace('Existing RECT textures','Independent 2D textures')
p.write_text(s)
p.with_name('probe-layout.json').write_text(json.dumps(dict(width=width,height=height,
    stride=stride,uv_offset=uv_offset,allocation=allocation),sort_keys=True)+'\n')
