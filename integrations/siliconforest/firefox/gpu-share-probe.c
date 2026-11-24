/* SPDX-License-Identifier: MIT
 * Cross-exec Asahi dma-buf sharing without display/card-node access.
 * GPU renders green then blue; another process imports the SAME buffer once,
 * verifies both revisions. Readback is verification only, never frame transport.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <mach/mach.h>
#include <sys/fileport.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <spawn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
#define SIDE 32
static EGLDisplay display;
static EGLContext context;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static void (*image_target)(GLenum,void *);
static int init(void){
 PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
 display=platform?platform(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL):EGL_NO_DISPLAY;
 if(display==EGL_NO_DISPLAY||!eglInitialize(display,NULL,NULL)||!eglBindAPI(EGL_OPENGL_ES_API))return 1;
 EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
 EGLConfig config;EGLint count=0;
 if(!eglChooseConfig(display,attributes,&config,1,&count)||!count)return 2;
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
 if(context==EGL_NO_CONTEXT||!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context))return 3;
 const char *renderer=(const char *)glGetString(GL_RENDERER);
 printf("FXGPUSHARE pid=%d renderer=%s\n",getpid(),renderer?renderer:"missing");
 if(!renderer||!strstr(renderer,"Apple M1"))return 4;
 create_image=(void *)eglGetProcAddress("eglCreateImageKHR");destroy_image=(void *)eglGetProcAddress("eglDestroyImageKHR");image_target=(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 return !create_image||!destroy_image||!image_target;
}
static EGLImage import(int fd,GLuint *texture,GLuint *framebuffer){
 EGLint a[]={EGL_WIDTH,SIDE,EGL_HEIGHT,SIDE,EGL_LINUX_DRM_FOURCC_EXT,DRM_FORMAT_ARGB8888,
  EGL_DMA_BUF_PLANE0_FD_EXT,fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,SIDE*4,
  EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,0,EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,0,EGL_NONE};
 EGLImage image=create_image(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,a);
 if(image==EGL_NO_IMAGE_KHR){printf("FXGPUSHARE import_failed egl=%x\n",eglGetError());return image;}
 glGenTextures(1,texture);glBindTexture(GL_TEXTURE_2D,*texture);image_target(GL_TEXTURE_2D,image);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glGenFramebuffers(1,framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,*framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*texture,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR){printf("FXGPUSHARE framebuffer_failed\n");return EGL_NO_IMAGE_KHR;}
 return image;
}
static int send_fd(int socket,int fd){
 char value='F',control[CMSG_SPACE(sizeof(int))]={0};struct iovec io={&value,1};
 struct msghdr message={0};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);
 struct cmsghdr *header=CMSG_FIRSTHDR(&message);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(header),&fd,sizeof(fd));
 return sendmsg(socket,&message,0)==1?0:1;
}
static int receive_fd(int socket){
 char value=0,control[CMSG_SPACE(sizeof(int))]={0};struct iovec io={&value,1};struct msghdr message={0};
 message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);
 if(recvmsg(socket,&message,0)!=1||value!='F'||message.msg_flags&MSG_CTRUNC)return -1;
 struct cmsghdr *header=CMSG_FIRSTHDR(&message);int fd=-1;
 if(!header||header->cmsg_level!=SOL_SOCKET||header->cmsg_type!=SCM_RIGHTS||header->cmsg_len!=CMSG_LEN(sizeof(int)))return -1;
 memcpy(&fd,CMSG_DATA(header),sizeof(fd));return fd;
}
static int child(int channel){
 alarm(15);int fd=receive_fd(channel);if(fd<0||init())return 10;
 GLuint texture=0,framebuffer=0;EGLImage image=import(fd,&texture,&framebuffer);if(!image)return 11;
 unsigned matches[2]={0};
 for(int revision=0;revision<2;revision++){
  char value=0;if(read(channel,&value,1)!=1||value!='0'+revision)return 12;
  unsigned char pixels[SIDE*SIDE*4];glReadPixels(0,0,SIDE,SIDE,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  for(int i=0;i<SIDE*SIDE;i++)if(pixels[i*4]==0&&pixels[i*4+1]==(revision?0:255)&&pixels[i*4+2]==(revision?255:0)&&pixels[i*4+3]==255)matches[revision]++;
  printf("FXGPUSHARE revision=%d exact_pixels=%u/%d gl_error=%x\n",revision,matches[revision],SIDE*SIDE,glGetError());
  if(write(channel,&value,1)!=1)return 13;
 }
 glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);destroy_image(display,image);close(fd);
 return matches[0]==SIDE*SIDE&&matches[1]==SIDE*SIDE?0:14;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);alarm(20);
 if(argc==3&&!strcmp(argv[1],"--child"))return child(atoi(argv[2]));
 int rfd=-1;
 for(int i=128;i<144;i++){char node[80];snprintf(node,sizeof(node),"/dev/dri/renderD%d",i);int fd=open(node,O_RDWR|O_CLOEXEC);if(fd<0)continue;drmVersionPtr v=drmGetVersion(fd);int match=v&&v->name&&!strcmp(v->name,"asahi");if(v)drmFreeVersion(v);if(match){rfd=fd;break;}close(fd);}
 if(rfd<0){perror("FXGPUSHARE render_node");return 2;}
 struct {uint64_t size;uint32_t flags,vm_id,handle,pad;} create={.size=16384};
 if(drmIoctl(rfd,DRM_IOWR(DRM_COMMAND_BASE+5,create),&create)){perror("FXGPUSHARE GEM_CREATE");return 3;}
 int fd=-1;if(drmPrimeHandleToFD(rfd,create.handle,DRM_CLOEXEC|DRM_RDWR,&fd)){perror("FXGPUSHARE PRIME_export");return 4;}
 void *cpu=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 printf("FXGPUSHARE dmabuf_mmap_status=%d errno=%d\n",cpu==MAP_FAILED?-1:0,cpu==MAP_FAILED?errno:0);
 fileport_t port=MACH_PORT_NULL;int fp=fileport_makeport(fd,&port);int fp_errno=errno,roundtrip=-1;
 if(!fp){roundtrip=fileport_makefd(port);if(roundtrip>=0)close(roundtrip);mach_port_deallocate(mach_task_self(),port);}
 printf("FXGPUSHARE fileport_status=%d errno=%d makefd_status=%d\n",fp,fp?fp_errno:0,roundtrip>=0?0:-1);
 if(init())return 5;GLuint texture=0,framebuffer=0;EGLImage image=import(fd,&texture,&framebuffer);if(!image)return 6;
 int channels[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,channels))return 7;
 char channel[20];snprintf(channel,sizeof(channel),"%d",channels[1]);char *av[]={argv[0],"--child",channel,NULL};
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_addclose(&actions,channels[0]);
 pid_t pid=0;int err=posix_spawn(&pid,argv[0],&actions,NULL,av,environ);posix_spawn_file_actions_destroy(&actions);close(channels[1]);if(err)return 8;
 int good=!send_fd(channels[0],fd);
 for(int revision=0;revision<2&&good;revision++){
  glClearColor(0,revision?0:1,revision?1:0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();char value='0'+revision,reply=0;
  if(cpu!=MAP_FAILED){
   unsigned char *pixels=cpu;unsigned matches=0;
   for(int i=0;i<SIDE*SIDE;i++)if(pixels[i*4]==(revision?255:0)&&pixels[i*4+1]==(revision?0:255)&&pixels[i*4+2]==0&&pixels[i*4+3]==255)matches++;
   printf("FXGPUSHARE revision=%d shared_CPU_map_exact=%u/%d\n",revision,matches,SIDE*SIDE);
  }
  good=write(channels[0],&value,1)==1&&read(channels[0],&reply,1)==1&&reply==value;
 }
 close(channels[0]);int status=0;waitpid(pid,&status,0);
 glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);destroy_image(display,image);
 if(cpu!=MAP_FAILED)munmap(cpu,16384);close(fd);
 struct drm_gem_close close_bo={.handle=create.handle};drmIoctl(rfd,DRM_IOCTL_GEM_CLOSE,&close_bo);close(rfd);
 printf("FXGPUSHARE transport=%s child_status=%d no_display_access=1\n",good?"SCM_RIGHTS":"failed",status);
 return good&&WIFEXITED(status)&&WEXITSTATUS(status)==0?0:9;
}
