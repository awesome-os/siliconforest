/* SPDX-License-Identifier: MIT
 * Private CGL extension, included after CGL.c's EGL/IOSurface definitions.
 * The caller owns independently allocated, exact-size R8/RG8 TEXTURE_2D
 * storage on the current texture unit. This function only fills that storage.
 * It never allocates the destination, aliases decoder memory, maps pixels,
 * uploads client bytes or falls back to a CPU path. False requires the caller
 * to overwrite BOTH NV12 planes through its ordinary fallback.
 * GL error status is consumed; callers must start with no pending GL errors.
 * Use the TWO-plane entry point for an NV12 frame. A metadata retain alone
 * cannot prevent decoder reuse between two independent single-plane calls.
 */
#ifdef SF_CGL_USE_EGL
#include <limits.h>

#define SF_NV12_GL_FUNCTIONS(X) \
    X(glGetError) X(glGetIntegerv) X(glGetTexLevelParameteriv) X(glIsEnabled) \
    X(glEnable) X(glDisable) X(glGenTextures) X(glBindTexture) X(glDeleteTextures) \
    X(glGenFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D) \
    X(glCheckFramebufferStatus) X(glBlitFramebuffer) X(glDeleteFramebuffers) X(glFinish)
static struct {
#define SF_NV12_DECLARE(name) __typeof__(&name) name;
    SF_NV12_GL_FUNCTIONS(SF_NV12_DECLARE)
#undef SF_NV12_DECLARE
} sf_nv12_gl;
static pthread_once_t sf_nv12_gl_once = PTHREAD_ONCE_INIT;
static pthread_once_t sf_nv12_surface_once = PTHREAD_ONCE_INIT;
static uint64_t sf_nv12_copy_stats[4]; /* attempts, successes, bytes, quarantines */
/* A separate immutable table avoids writing CGL's existing lazy IOSurface
 * table while another thread may be using its ordinary surface APIs. */
static struct {
    uint32_t (*pixelFormat)(IOSurfaceRef);
    size_t (*planeCount)(IOSurfaceRef), (*planeWidth)(IOSurfaceRef,size_t), (*planeHeight)(IOSurfaceRef,size_t);
    size_t (*rowBytes)(IOSurfaceRef,size_t), (*elementBytes)(IOSurfaceRef,size_t);
    size_t (*gpuOffset)(IOSurfaceRef), (*gpuPlaneOffset)(IOSurfaceRef,size_t);
    int (*gpuCopyFD)(IOSurfaceRef);
    void (*incrementUse)(IOSurfaceRef), (*decrementUse)(IOSurfaceRef);
} sf_nv12_surface;
static void sf_nv12_load_surface(void) {
    void *lib=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",RTLD_NOW|RTLD_LOCAL);
    if (!lib) return;
#define SF_NV12_SURFACE(field,name) sf_nv12_surface.field=dlsym(lib,name)
    SF_NV12_SURFACE(pixelFormat,"IOSurfaceGetPixelFormat");
    SF_NV12_SURFACE(planeCount,"IOSurfaceGetPlaneCount");
    SF_NV12_SURFACE(planeWidth,"IOSurfaceGetWidthOfPlane");
    SF_NV12_SURFACE(planeHeight,"IOSurfaceGetHeightOfPlane");
    SF_NV12_SURFACE(rowBytes,"IOSurfaceGetBytesPerRowOfPlane");
    SF_NV12_SURFACE(elementBytes,"IOSurfaceGetBytesPerElementOfPlane");
    SF_NV12_SURFACE(gpuOffset,"IOSurfaceSFGPUGetOffset");
    SF_NV12_SURFACE(gpuPlaneOffset,"IOSurfaceSFGPUGetPlaneOffset");
    SF_NV12_SURFACE(gpuCopyFD,"IOSurfaceSFGPUCopyFD");
    SF_NV12_SURFACE(incrementUse,"IOSurfaceIncrementUseCount");
    SF_NV12_SURFACE(decrementUse,"IOSurfaceDecrementUseCount");
#undef SF_NV12_SURFACE
}

static void sf_nv12_load_gl(void) {
#define SF_NV12_LOAD(name) sf_nv12_gl.name = (__typeof__(&name))eglGetProcAddress(#name);
    SF_NV12_GL_FUNCTIONS(SF_NV12_LOAD)
#undef SF_NV12_LOAD
}

__attribute__((visibility("default")))
void CGLPDNV12CopyStats(uint64_t values[4]) {
    if (!values) return;
    for (unsigned i = 0; i < 4; i++)
        values[i] = __atomic_load_n(&sf_nv12_copy_stats[i], __ATOMIC_RELAXED);
}

__attribute__((visibility("default")))
int CGLPDCopyNV12PlaneToTexture2D(IOSurfaceRef surface, unsigned plane) {
    __atomic_fetch_add(&sf_nv12_copy_stats[0], 1, __ATOMIC_RELAXED);
    if (!sf_cgl_current || !surface || plane > 1) return 0;
    pthread_once(&sf_nv12_surface_once, sf_nv12_load_surface);
    pthread_once(&sf_cgl_present_once, sf_cgl_load_present);
    pthread_once(&sf_nv12_gl_once, sf_nv12_load_gl);
#define SF_NV12_REQUIRE(name) if (!sf_nv12_gl.name) return 0;
    SF_NV12_GL_FUNCTIONS(SF_NV12_REQUIRE)
#undef SF_NV12_REQUIRE
    if (!sf_present.createImage || !sf_present.destroyImage || !sf_present.targetTexture ||
        !sf_nv12_surface.pixelFormat || !sf_nv12_surface.planeCount ||
        !sf_nv12_surface.planeWidth || !sf_nv12_surface.planeHeight ||
        !sf_nv12_surface.rowBytes || !sf_nv12_surface.elementBytes ||
        !sf_nv12_surface.gpuOffset || !sf_nv12_surface.gpuPlaneOffset || !sf_nv12_surface.gpuCopyFD ||
        !sf_nv12_surface.incrementUse || !sf_nv12_surface.decrementUse) return 0;
    uint32_t pixel = sf_nv12_surface.pixelFormat(surface);
    if ((pixel != 0x34323076 && pixel != 0x34323066) || sf_nv12_surface.planeCount(surface) != 2)
        return 0; /* 420v/420f only; in particular, no P010 interpretation. */
    size_t width = sf_nv12_surface.planeWidth(surface, plane);
    size_t height = sf_nv12_surface.planeHeight(surface, plane);
    size_t row = sf_nv12_surface.rowBytes(surface, plane);
    size_t element = plane ? 2 : 1;
    size_t base = sf_nv12_surface.gpuOffset(surface);
    size_t offset = sf_nv12_surface.gpuPlaneOffset(surface, plane);
    if (!width || !height || width > INT_MAX || height > INT_MAX || row > INT_MAX ||
        width > SIZE_MAX / element || row < width * element ||
        height > SIZE_MAX / row || !base || offset < base || offset > INT_MAX ||
        row * height > SIZE_MAX - offset || sf_nv12_surface.elementBytes(surface, plane) != element)
        return 0;
    if (sf_nv12_gl.glGetError() != GL_NO_ERROR) return 0;
    GLint destination = 0, tw = 0, th = 0, storage = 0;
    sf_nv12_gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &destination);
    if (!destination) return 0;
    sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &storage);
    if (sf_nv12_gl.glGetError() != GL_NO_ERROR || tw != (GLint)width || th != (GLint)height ||
        storage != (GLint)(plane ? 0x822b : 0x8229)) return 0; /* RG8/R8 */

    GLint read_fbo = 0, draw_fbo = 0;
    sf_nv12_gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    sf_nv12_gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    GLboolean scissor = sf_nv12_gl.glIsEnabled(GL_SCISSOR_TEST);
    GLboolean srgb = sf_nv12_gl.glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (sf_nv12_gl.glGetError() != GL_NO_ERROR) return 0;

    GLuint source_texture = 0, framebuffers[2] = {0};
    EGLImage image = EGL_NO_IMAGE_KHR;
    int good = 0;
    /* The use covers import, source GPU reads and actual completion. Merely
     * retaining the IOSurface metadata does not prevent decoder-buffer reuse. */
    sf_nv12_surface.incrementUse(surface);
    int fd = sf_nv12_surface.gpuCopyFD(surface);
    if (fd >= 0) {
        EGLint attrs[] = {EGL_WIDTH, (EGLint)width, EGL_HEIGHT, (EGLint)height,
            EGL_LINUX_DRM_FOURCC_EXT, plane ? 0x38385247 : 0x20203852,
            EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)offset,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)row,
            EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, 0, EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, 0, EGL_NONE};
        image = sf_present.createImage(sf_cgl_get_display(), EGL_NO_CONTEXT,
            EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
        close(fd);
    }
    if (image != EGL_NO_IMAGE_KHR) {
        sf_nv12_gl.glGenTextures(1, &source_texture);
        if (source_texture) {
            sf_nv12_gl.glBindTexture(GL_TEXTURE_2D, source_texture);
            sf_present.targetTexture(GL_TEXTURE_2D, image);
            sf_nv12_gl.glGenFramebuffers(2, framebuffers);
            if (framebuffers[0] && framebuffers[1]) {
                sf_nv12_gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
                sf_nv12_gl.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, source_texture, 0);
                good = sf_nv12_gl.glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                sf_nv12_gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
                sf_nv12_gl.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, (GLuint)destination, 0);
                good = good && sf_nv12_gl.glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                good = (sf_nv12_gl.glGetError() == GL_NO_ERROR) && good;
                if (good) {
                    sf_nv12_gl.glDisable(GL_SCISSOR_TEST);
                    sf_nv12_gl.glDisable(GL_FRAMEBUFFER_SRGB);
                    sf_nv12_gl.glBlitFramebuffer(0, 0, (GLint)width, (GLint)height,
                        0, 0, (GLint)width, (GLint)height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    good = sf_nv12_gl.glGetError() == GL_NO_ERROR;
                }
            }
        }
    }
    /* Clear an operation error separately from the completion result. A failed
     * import/blit with a successful finish can still release its global use. */
    if (sf_nv12_gl.glGetError() != GL_NO_ERROR) good = 0;
    sf_nv12_gl.glFinish();
    int completed = sf_nv12_gl.glGetError() == GL_NO_ERROR;
    if (scissor) sf_nv12_gl.glEnable(GL_SCISSOR_TEST);
    else sf_nv12_gl.glDisable(GL_SCISSOR_TEST);
    if (srgb) sf_nv12_gl.glEnable(GL_FRAMEBUFFER_SRGB);
    else sf_nv12_gl.glDisable(GL_FRAMEBUFFER_SRGB);
    sf_nv12_gl.glBindTexture(GL_TEXTURE_2D, (GLuint)destination);
    sf_nv12_gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    sf_nv12_gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
    if (framebuffers[0] || framebuffers[1]) sf_nv12_gl.glDeleteFramebuffers(2, framebuffers);
    if (source_texture) sf_nv12_gl.glDeleteTextures(1, &source_texture);
    if (image != EGL_NO_IMAGE_KHR &&
        sf_present.destroyImage(sf_cgl_get_display(), image) != EGL_TRUE) good = 0;
    if (sf_nv12_gl.glGetError() != GL_NO_ERROR) good = 0;
    if (completed) sf_nv12_surface.decrementUse(surface);
    else __atomic_fetch_add(&sf_nv12_copy_stats[3], 1, __ATOMIC_RELAXED);
    if (!good || !completed) return 0;
    uint64_t bytes = (uint64_t)width * height * element;
    pthread_mutex_lock(&sf_cgl_track_lock);
    sf_gpu_stats[2] += bytes;
    pthread_mutex_unlock(&sf_cgl_track_lock);
    __atomic_fetch_add(&sf_nv12_copy_stats[1], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&sf_nv12_copy_stats[2], bytes, __ATOMIC_RELAXED);
    return 1;
}

/* Paired-only experiment: enqueue BOTH independent plane blits, retain both
 * imported images/source textures and ONE outer global use, then complete the
 * whole queue with one real glFinish. No cache or asynchronous completion.
 * A failed completion quarantines that outer use; failure still requires the
 * caller to overwrite BOTH destination planes. Single-plane API is unchanged.
 */
#define SF_NV12_BATCHED_PAIR 1
__attribute__((visibility("default")))
int CGLPDCopyNV12ToTextures2D(IOSurfaceRef surface,const unsigned textures[2]) {
    if (!sf_cgl_current || !surface || !textures || !textures[0] || !textures[1] ||
        textures[0]==textures[1]) return 0;
    pthread_once(&sf_nv12_surface_once,sf_nv12_load_surface);
    pthread_once(&sf_cgl_present_once,sf_cgl_load_present);
    pthread_once(&sf_nv12_gl_once,sf_nv12_load_gl);
#define SF_NV12_BATCH_REQUIRE(name) if (!sf_nv12_gl.name) return 0;
    SF_NV12_GL_FUNCTIONS(SF_NV12_BATCH_REQUIRE)
#undef SF_NV12_BATCH_REQUIRE
    if (!sf_present.createImage || !sf_present.destroyImage || !sf_present.targetTexture ||
        !sf_nv12_surface.pixelFormat || !sf_nv12_surface.planeCount ||
        !sf_nv12_surface.planeWidth || !sf_nv12_surface.planeHeight ||
        !sf_nv12_surface.rowBytes || !sf_nv12_surface.elementBytes ||
        !sf_nv12_surface.gpuOffset || !sf_nv12_surface.gpuPlaneOffset || !sf_nv12_surface.gpuCopyFD ||
        !sf_nv12_surface.incrementUse || !sf_nv12_surface.decrementUse) return 0;
    uint32_t pixel=sf_nv12_surface.pixelFormat(surface);
    if ((pixel!=0x34323076 && pixel!=0x34323066) || sf_nv12_surface.planeCount(surface)!=2) return 0;
    size_t width[2],height[2],row[2],offset[2],base=sf_nv12_surface.gpuOffset(surface);
    for (unsigned p=0;p<2;p++) {
        __atomic_fetch_add(&sf_nv12_copy_stats[0],1,__ATOMIC_RELAXED);
        width[p]=sf_nv12_surface.planeWidth(surface,p);height[p]=sf_nv12_surface.planeHeight(surface,p);
        row[p]=sf_nv12_surface.rowBytes(surface,p);offset[p]=sf_nv12_surface.gpuPlaneOffset(surface,p);
        size_t element=p?2:1;
        if (!width[p] || !height[p] || width[p]>INT_MAX || height[p]>INT_MAX || row[p]>INT_MAX ||
            width[p]>SIZE_MAX/element || row[p]<width[p]*element || height[p]>SIZE_MAX/row[p] ||
            !base || offset[p]<base || offset[p]>INT_MAX || row[p]*height[p]>SIZE_MAX-offset[p] ||
            sf_nv12_surface.elementBytes(surface,p)!=element) return 0;
    }
    if (sf_nv12_gl.glGetError()!=GL_NO_ERROR) return 0;
    GLint saved=0,read=0,draw=0;
    sf_nv12_gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&saved);
    sf_nv12_gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    sf_nv12_gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    GLboolean scissor=sf_nv12_gl.glIsEnabled(GL_SCISSOR_TEST),srgb=sf_nv12_gl.glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (sf_nv12_gl.glGetError()!=GL_NO_ERROR) return 0;
    GLuint source[2]={0},fb[2]={0};
    EGLImage images[2]={EGL_NO_IMAGE_KHR,EGL_NO_IMAGE_KHR};
    int good=1,completed=1,submitted=0;
    sf_nv12_surface.incrementUse(surface);
    /* Validate BOTH destinations before submitting any source read. */
    for (unsigned p=0;p<2;p++) {
        GLint w=0,h=0,format=0;
        sf_nv12_gl.glBindTexture(GL_TEXTURE_2D,textures[p]);
        sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
        sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        sf_nv12_gl.glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        if (sf_nv12_gl.glGetError()!=GL_NO_ERROR || w!=(GLint)width[p] || h!=(GLint)height[p] ||
            format!=(GLint)(p?0x822b:0x8229)) {good=0;break;}
    }
    if (good) {
        submitted=1;
        sf_nv12_gl.glGenFramebuffers(2,fb);
        good=fb[0]&&fb[1]&&sf_nv12_gl.glGetError()==GL_NO_ERROR;
        sf_nv12_gl.glDisable(GL_SCISSOR_TEST);sf_nv12_gl.glDisable(GL_FRAMEBUFFER_SRGB);
        for (unsigned p=0;good && p<2;p++) {
            int fd=sf_nv12_surface.gpuCopyFD(surface);
            if (fd<0) {good=0;break;}
            EGLint attributes[]={EGL_WIDTH,(EGLint)width[p],EGL_HEIGHT,(EGLint)height[p],
                EGL_LINUX_DRM_FOURCC_EXT,p?0x38385247:0x20203852,
                EGL_DMA_BUF_PLANE0_FD_EXT,fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)offset[p],
                EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)row[p],
                EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,0,EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,0,EGL_NONE};
            images[p]=sf_present.createImage(sf_cgl_get_display(),EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attributes);
            close(fd);
            if (images[p]==EGL_NO_IMAGE_KHR) {good=0;break;}
            sf_nv12_gl.glGenTextures(1,&source[p]);
            if (!source[p]) {good=0;break;}
            sf_nv12_gl.glBindTexture(GL_TEXTURE_2D,source[p]);sf_present.targetTexture(GL_TEXTURE_2D,images[p]);
            sf_nv12_gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,fb[0]);
            sf_nv12_gl.glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source[p],0);
            good=sf_nv12_gl.glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            sf_nv12_gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fb[1]);
            sf_nv12_gl.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[p],0);
            good=(sf_nv12_gl.glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE)&&good;
            good=(sf_nv12_gl.glGetError()==GL_NO_ERROR)&&good;
            if (good) {
                sf_nv12_gl.glBlitFramebuffer(0,0,(GLint)width[p],(GLint)height[p],0,0,
                    (GLint)width[p],(GLint)height[p],GL_COLOR_BUFFER_BIT,GL_NEAREST);
                good=sf_nv12_gl.glGetError()==GL_NO_ERROR;
            }
        }
        if (sf_nv12_gl.glGetError()!=GL_NO_ERROR) good=0;
        /* Both imports, source textures and the global use remain live. This
         * completion covers every preceding plane read, including partial
         * work when an operation failed. No producer reuse before this point. */
        sf_nv12_gl.glFinish();completed=sf_nv12_gl.glGetError()==GL_NO_ERROR;
    }
    if (scissor) sf_nv12_gl.glEnable(GL_SCISSOR_TEST);else sf_nv12_gl.glDisable(GL_SCISSOR_TEST);
    if (srgb) sf_nv12_gl.glEnable(GL_FRAMEBUFFER_SRGB);else sf_nv12_gl.glDisable(GL_FRAMEBUFFER_SRGB);
    sf_nv12_gl.glBindTexture(GL_TEXTURE_2D,(GLuint)saved);
    sf_nv12_gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read);
    sf_nv12_gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw);
    if (fb[0]||fb[1]) sf_nv12_gl.glDeleteFramebuffers(2,fb);
    for (unsigned p=0;p<2;p++) {
        if (source[p]) sf_nv12_gl.glDeleteTextures(1,&source[p]);
        if (images[p]!=EGL_NO_IMAGE_KHR && sf_present.destroyImage(sf_cgl_get_display(),images[p])!=EGL_TRUE) good=0;
    }
    if (sf_nv12_gl.glGetError()!=GL_NO_ERROR) good=0;
    if (completed) sf_nv12_surface.decrementUse(surface);
    else __atomic_fetch_add(&sf_nv12_copy_stats[3],1,__ATOMIC_RELAXED);
    if (!submitted || !good || !completed) return 0;
    uint64_t bytes=(uint64_t)width[0]*height[0]+2*(uint64_t)width[1]*height[1];
    pthread_mutex_lock(&sf_cgl_track_lock);sf_gpu_stats[2]+=bytes;pthread_mutex_unlock(&sf_cgl_track_lock);
    __atomic_fetch_add(&sf_nv12_copy_stats[1],2,__ATOMIC_RELAXED);
    __atomic_fetch_add(&sf_nv12_copy_stats[2],bytes,__ATOMIC_RELAXED);
    return 1;
}
#undef SF_NV12_GL_FUNCTIONS
#endif
