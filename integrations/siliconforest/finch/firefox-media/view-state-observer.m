/* SPDX-License-Identifier: MIT */
/* Public getter observations in an owned process, on its main queue only.
 * No AppKit/Foundation dependency or dlopen: do not introduce UI frameworks
 * into RDD/GPU helpers. No layer getter, focus/input, drawing or setters. */
#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef struct {double x,y,width,height;} SFRect;
_Static_assert(sizeof(SFRect)==32,"arm64 public NSRect ABI");
typedef struct dispatch_queue_s *dispatch_queue_t;
extern struct dispatch_queue_s _dispatch_main_q;
extern uint64_t dispatch_time(uint64_t,int64_t);
extern void dispatch_after_f(uint64_t,dispatch_queue_t,void *,void (*)(void *));

__attribute__((objc_root_class)) @interface SFPeekObject
- (BOOL)respondsToSelector:(SEL)selector;
@end
@interface SFPeekArray : SFPeekObject
- (unsigned long)count;
- (id)objectAtIndex:(unsigned long)index;
@end
@interface SFPeekView : SFPeekObject
- (SFRect)frame;
- (SFRect)bounds;
- (BOOL)isHidden;
- (BOOL)wantsLayer;
- (double)alphaValue;
- (SFPeekArray *)subviews;
@end
@interface SFPeekWindow : SFPeekObject
- (SFRect)frame;
- (BOOL)isVisible;
- (BOOL)isKeyWindow;
- (BOOL)isMainWindow;
- (BOOL)isMiniaturized;
- (double)alphaValue;
- (SFPeekView *)contentView;
- (id)firstResponder;
@end
@interface SFPeekApplication : SFPeekObject
- (BOOL)isActive;
- (BOOL)isHidden;
- (SFPeekArray *)windows;
@end

static const char *class_name(id object){return object?class_getName(object_getClass(object)):"nil";}
static int boolean(id object,SEL selector)
{
    if(!object||![object respondsToSelector:selector])return -1;
    BOOL (*send)(id,SEL)=(void *)objc_msgSend;
    return send(object,selector)?1:0;
}
static void view(SFPeekView *v,unsigned window,unsigned depth,unsigned *remaining)
{
    if(!v||depth>4||!*remaining)return;(*remaining)--;
    BOOL has_frame=[v respondsToSelector:@selector(frame)],has_bounds=[v respondsToSelector:@selector(bounds)];
    SFRect f=has_frame?[v frame]:(SFRect){0},b=has_bounds?[v bounds]:(SFRect){0};
    BOOL has_alpha=[v respondsToSelector:@selector(alphaValue)];
    fprintf(stderr,"SFVIEW_VIEW pid=%d window=%u depth=%u class=%s frame-valid=%d frame=%.3f,%.3f,%.3f,%.3f bounds-valid=%d bounds=%.3f,%.3f,%.3f,%.3f hidden=%d wants-layer=%d alpha-valid=%d alpha=%.6f\n",
        getpid(),window,depth,class_name(v),(int)has_frame,f.x,f.y,f.width,f.height,(int)has_bounds,b.x,b.y,b.width,b.height,
        boolean(v,@selector(isHidden)),boolean(v,@selector(wantsLayer)),(int)has_alpha,has_alpha?[v alphaValue]:0);
    if(depth<4&&[v respondsToSelector:@selector(subviews)]){
        SFPeekArray *children=[v subviews];unsigned long count=[children count];
        for(unsigned long i=0;i<count&&i<24&&*remaining;i++)view([children objectAtIndex:i],window,depth+1,remaining);
    }
}
static void observe(void *opaque)
{
    unsigned tick=(unsigned)(uintptr_t)opaque;struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);
    id *address=(id *)dlsym(RTLD_DEFAULT,"NSApp");
    SFPeekApplication *app=address?(SFPeekApplication *)*address:nil;
    if(!app){fprintf(stderr,"SFVIEW_APP pid=%d tick=%u observed=0 reason=no-existing-NSApp\n",getpid(),tick);return;}
    @try{
        SFPeekArray *windows=[app windows];unsigned long count=[windows count];
        fprintf(stderr,"SFVIEW_APP pid=%d tick=%u observed=1 epoch=%.6f class=%s active=%d hidden=%d windows=%lu\n",
            getpid(),tick,ts.tv_sec+ts.tv_nsec/1e9,class_name(app),boolean(app,@selector(isActive)),boolean(app,@selector(isHidden)),count);
        for(unsigned long i=0;i<count&&i<8;i++){
            SFPeekWindow *w=[windows objectAtIndex:i];SFRect f=[w frame];BOOL has_alpha=[w respondsToSelector:@selector(alphaValue)];
            fprintf(stderr,"SFVIEW_WINDOW pid=%d tick=%u index=%lu class=%s frame=%.3f,%.3f,%.3f,%.3f visible=%d key=%d main=%d mini=%d alpha-valid=%d alpha=%.6f responder=%s\n",
                getpid(),tick,i,class_name(w),f.x,f.y,f.width,f.height,boolean(w,@selector(isVisible)),boolean(w,@selector(isKeyWindow)),
                boolean(w,@selector(isMainWindow)),boolean(w,@selector(isMiniaturized)),(int)has_alpha,has_alpha?[w alphaValue]:0,
                [w respondsToSelector:@selector(firstResponder)]?class_name([w firstResponder]):"unsupported");
            unsigned remaining=24;view([w contentView],(unsigned)i,0,&remaining);
        }
    }@catch(id exception){fprintf(stderr,"SFVIEW_ERROR pid=%d tick=%u exception-class=%s\n",getpid(),tick,class_name(exception));}
}
__attribute__((constructor)) static void start(void)
{
    const char *enabled=getenv("SF_FIREFOX_VIEW_STATE");
    if(!enabled||strcmp(enabled,"1"))return;
    fprintf(stderr,"SFVIEW_LOADED pid=%d read-only=1 appkit-linked=0 no-focus-or-input=1\n",getpid());
    for(unsigned tick=3;tick<=15;tick+=6)
        dispatch_after_f(dispatch_time(0,(int64_t)tick*1000000000),&_dispatch_main_q,(void *)(uintptr_t)tick,observe);
}
