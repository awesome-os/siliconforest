/* SPDX-License-Identifier: MIT
 * CGL core-profile + actual framebuffer readback; no window or display mutation.
 * Declarations below describe the public ABI; no Apple SDK headers are used.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
extern int sandbox_init(const char *,uint64_t,char **);
extern void sandbox_free_error(char *);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *CGLPixelFormatObj;
typedef void *CGLContextObj;
typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei;
typedef unsigned char GLubyte;

#define LOAD(ret, name, args) ret (*name) args = dlsym(lib, #name); \
    if (!name) { fprintf(stderr, "missing %s: %s\n", #name, dlerror()); return 2; }

int main(int argc, char **argv) {
    void *lib = dlopen(argc > 1 ? argv[1] :
        "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "OpenGL dlopen: %s\n", dlerror()); return 2; }
    LOAD(int, CGLChoosePixelFormat, (const int *, CGLPixelFormatObj *, int *));
    LOAD(int, CGLCreateContext, (CGLPixelFormatObj, CGLContextObj, CGLContextObj *));
    LOAD(int, CGLSetCurrentContext, (CGLContextObj));
    LOAD(int, CGLDestroyContext, (CGLContextObj));
    LOAD(int, CGLDestroyPixelFormat, (CGLPixelFormatObj));
    LOAD(const GLubyte *, glGetString, (GLenum));
    LOAD(GLenum, glGetError, (void));
    LOAD(void, glGenFramebuffers, (GLsizei, GLuint *));
    LOAD(void, glBindFramebuffer, (GLenum, GLuint));
    LOAD(void, glDeleteFramebuffers, (GLsizei, const GLuint *));
    LOAD(void, glGenTextures, (GLsizei, GLuint *));
    LOAD(void, glBindTexture, (GLenum, GLuint));
    LOAD(void, glDeleteTextures, (GLsizei, const GLuint *));
    LOAD(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
    LOAD(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint));
    LOAD(GLenum, glCheckFramebufferStatus, (GLenum));
    LOAD(void, glClearColor, (float, float, float, float));
    LOAD(void, glClear, (unsigned int));
    LOAD(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
    LOAD(void, glFinish, (void));
    if(argc>2){
        char *error=NULL;
        const char *profile=!strcmp(argv[2],"allow")?
          "(version 1)(deny default)(allow iokit-open (iokit-connection \"IOAccelerator\"))":
          "(version 1)(deny default)(allow iokit-open (iokit-connection \"unrelated\"))";
        int applied=sandbox_init(profile,0,&error);
        printf("FXCGL sandbox=%s applied=%d error=%s\n",argv[2],applied,error?error:"none");sandbox_free_error(error);
        if(applied)return 1;
        errno=0;int primary=open("/dev/dri/card0",O_RDWR),pe=errno;
        errno=0;int codewrite=open("/usr/local/lib/mesa-asahi/libEGL.1.dylib",O_WRONLY),we=errno;
        printf("FXCGL denied_primary=%d errno=%d denied_codewrite=%d errno=%d\n",primary,pe,codewrite,we);
        if(primary>=0)close(primary);if(codewrite>=0)close(codewrite);
        if(primary>=0||codewrite>=0||pe!=EPERM||we!=EPERM)return 1;
    }
    int failures = 0;
    const int profiles[] = {0x3200, 0x4100};
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        /* OpenGLProfile=99, Accelerated=73, AllowOfflineRenderers=96. */
        int attributes[] = {99, profiles[i], 73, 96, 0}, count = 0;
        CGLPixelFormatObj format = NULL;
        CGLContextObj context = NULL;
        int status = CGLChoosePixelFormat(attributes, &format, &count);
        if (!status) status = CGLCreateContext(format, NULL, &context);
        if (!status) status = CGLSetCurrentContext(context);
        if (status || !context) {
            printf("FXCGL profile=0x%x context_error=%d count=%d\n", profiles[i], status, count);
            failures++;
            if (context) CGLDestroyContext(context);
            if (format) CGLDestroyPixelFormat(format);
            continue;
        }
        const char *renderer = (const char *)glGetString(0x1f01);
        printf("FXCGL profile=0x%x vendor=%s renderer=%s version=%s\n", profiles[i],
               glGetString(0x1f00), renderer, glGetString(0x1f02));
        GLuint texture = 0, fbo = 0;
        glGenTextures(1, &texture);
        glBindTexture(0x0de1, texture);
        glTexImage2D(0x0de1, 0, 0x8058, 16, 16, 0, 0x1908, 0x1401, NULL);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(0x8d40, fbo);
        glFramebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, texture, 0);
        GLenum complete = glCheckFramebufferStatus(0x8d40);
        glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
        glClear(0x4000);
        glFinish();
        unsigned char pixels[16 * 16 * 4];
        memset(pixels, 0, sizeof(pixels));
        glReadPixels(0, 0, 16, 16, 0x1908, 0x1401, pixels);
        int mismatch = 0;
        for (int p = 0; p < 16 * 16; p++)
            mismatch += memcmp(pixels + p * 4, (unsigned char[]){51, 102, 153, 255}, 4) != 0;
        GLenum error = glGetError();
        int hardware = renderer && strstr(renderer, "Apple M1");
        printf("FXCGL profile=0x%x fbo=0x%x differing_pixels=%d gl_error=0x%x hardware=%d\n",
               profiles[i], complete, mismatch, error, hardware);
        failures += complete != 0x8cd5 || mismatch || error || !hardware;
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
        CGLSetCurrentContext(NULL);
        CGLDestroyContext(context);
        CGLDestroyPixelFormat(format);
    }
    printf("FXCGL failures=%d\n", failures);
    return failures ? 1 : 0;
}
