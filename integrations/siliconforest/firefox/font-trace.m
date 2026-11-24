/* SPDX-License-Identifier: MIT
 * Transparent, bounded diagnostics for the owned Firefox font fixture.
 * Reports glyph IDs/font names and view geometry, never clipboard data.
 */
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Onyx2D/O2GraphicsState.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
static void text(CFStringRef s,char *out);
static void (*realSetFont)(CGContextRef,CGFontRef);
static void (*realShow)(CGContextRef,const CGGlyph *,const CGSize *,unsigned);
static void (*realDraw)(CTFontRef,const CGGlyph *,const CGPoint *,size_t,CGContextRef);
static CGFontRef (*realCreateFont)(CFStringRef);
static CTFontRef (*realCreateCTFont)(CGFontRef,CGFloat,const CGAffineTransform *,CTFontDescriptorRef);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static unsigned traces;
static void resolve(void){
 realSetFont=dlsym(RTLD_NEXT,"CGContextSetFont");realShow=dlsym(RTLD_NEXT,"CGContextShowGlyphsWithAdvances");realDraw=dlsym(RTLD_NEXT,"CTFontDrawGlyphs");
 realCreateFont=dlsym(RTLD_NEXT,"CGFontCreateWithFontName");realCreateCTFont=dlsym(RTLD_NEXT,"CTFontCreateWithGraphicsFont");
 if(!realSetFont||!realShow||!realDraw){fprintf(stderr,"FXFONTTRACE missing_real_provider\n");abort();}
}
static CGFontRef createFont(CFStringRef requested){
 pthread_once(&once,resolve);CGFontRef result=realCreateFont(requested);char input[256],output[256];
 text(requested?CFRetain(requested):NULL,input);text(result?CGFontCopyPostScriptName(result):NULL,output);
 if(strstr(input,"Noto"))fprintf(stderr,"FXFONTCREATE pid=%d api=CGFont name=%s result=%s errno=%d\n",getpid(),input,output,errno);
 return result;
}
static CTFontRef createCTFont(CGFontRef graphics,CGFloat size,const CGAffineTransform *matrix,CTFontDescriptorRef attributes){
 pthread_once(&once,resolve);CTFontRef result=realCreateCTFont(graphics,size,matrix,attributes);char input[256],output[256];
 text(graphics?CGFontCopyPostScriptName(graphics):NULL,input);text(result?CTFontCopyPostScriptName(result):NULL,output);
 if(strstr(input,"Noto"))fprintf(stderr,"FXFONTCREATE pid=%d api=CTFont graphics=%s result=%s\n",getpid(),input,output);
 return result;
}
static void text(CFStringRef s,char *out){if(!s||!CFStringGetCString(s,out,256,kCFStringEncodingUTF8))snprintf(out,256,"?");if(s)CFRelease(s);}
static int interesting(const CGGlyph *g,size_t count){if(count>12)return 0;for(size_t i=0;i<count;i++)if(g[i]==961||g[i]==296||g[i]==975)return 1;return 0;}
static void logFont(const char *api,CGContextRef context,CGFontRef font,const CGGlyph *glyphs,size_t count){
 char name[256];text(font?CGFontCopyPostScriptName(font):NULL,name);
 if(!strstr(name,"NotoEmoji")&&!interesting(glyphs,count))return;
 if(__sync_fetch_and_add(&traces,1)>=128)return;
 fprintf(stderr,"FXFONTTRACE pid=%d api=%s context=%p font=%s glyphs=",getpid(),api,context,name);
 for(size_t i=0;i<count&&i<12;i++)fprintf(stderr,"%s%u",i?",":"",glyphs[i]);fputc('\n',stderr);
}
static void setFont(CGContextRef context,CGFontRef font){pthread_once(&once,resolve);logFont("setFont",context,font,NULL,0);realSetFont(context,font);}
static void show(CGContextRef context,const CGGlyph *glyphs,const CGSize *advances,unsigned count){
 pthread_once(&once,resolve);CGFontRef font=NULL;
 Ivar ivar=context?class_getInstanceVariable(object_getClass((id)context),"_currentState"):NULL;
 id state=ivar?object_getIvar((id)context,ivar):nil;if(state)font=(CGFontRef)O2GStateFont(state);
 logFont("showGlyphs",context,font,glyphs,count);realShow(context,glyphs,advances,count);
}
static void draw(CTFontRef font,const CGGlyph *glyphs,const CGPoint *positions,size_t count,CGContextRef context){
 pthread_once(&once,resolve);CGFontRef graphics=CTFontCopyGraphicsFont(font,NULL);logFont("CTDrawGlyphs",context,graphics,glyphs,count);
 if(graphics)CGFontRelease(graphics);realDraw(font,glyphs,positions,count,context);
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose[]={
 {(const void *)setFont,(const void *)CGContextSetFont},
 {(const void *)show,(const void *)CGContextShowGlyphsWithAdvances},
 {(const void *)draw,(const void *)CTFontDrawGlyphs},
 {(const void *)createFont,(const void *)CGFontCreateWithFontName},
 {(const void *)createCTFont,(const void *)CTFontCreateWithGraphicsFont}
};
static void viewTree(NSView *view,int depth,int *remaining){
 if(!view||depth>8||(*remaining)--<=0)return;
 fprintf(stderr,"FXVIEW pid=%d depth=%d class=%s frame=%s bounds=%s flipped=%d dragTypes=%s\n",getpid(),depth,object_getClassName(view),[NSStringFromRect([view frame]) UTF8String],[NSStringFromRect([view bounds]) UTF8String],[view isFlipped],[[[view registeredDraggedTypes] description] UTF8String]);
 for(NSView *child in [view subviews])viewTree(child,depth+1,remaining);
}
static id (*originalDestination)(id,SEL,NSPoint,NSArray *,NSWindow **,NSWindow *);
static id destination(id self,SEL selector,NSPoint point,NSArray *types,NSWindow **out,NSWindow *except){
 id result=originalDestination(self,selector,point,types,out,except);
 static unsigned calls;
 if(calls++<8){
  fprintf(stderr,"FXDRAGDEST point=%s offered=%s target=%s\n",[NSStringFromPoint(point) UTF8String],[[types description] UTF8String],object_getClassName(result));
  for(NSWindow *window in [NSApp windows]){
   if(window==except||![window isVisible]||!NSPointInRect(point,[window frame]))continue;
   NSView *frame=[[window contentView] superview]?:[window contentView];
   NSPoint local=[frame convertPoint:[window convertScreenToBase:point] fromView:nil];
   NSView *hit=[frame hitTest:local];
   fprintf(stderr,"FXDRAGDEST window=%ld frame=%s local=%s hit=%s\n",(long)[window windowNumber],[NSStringFromRect([window frame]) UTF8String],[NSStringFromPoint(local) UTF8String],object_getClassName(hit));
   for(NSView *v=hit;v;v=[v superview])fprintf(stderr,"FXDRAGDEST candidate=%s types=%s\n",object_getClassName(v),[[[v registeredDraggedTypes] description] UTF8String]);
  }
 }
 return result;
}
__attribute__((constructor)) static void scheduleViews(void){
 if(getenv("SF_DRAG_TRACE")){
  Class c=objc_getClass("NSDraggingManager");SEL s=sel_registerName("_destinationAtScreenPoint:types:window:except:");
  Method method=class_getInstanceMethod(c,s);
  if(method)originalDestination=(void *)method_setImplementation(method,(IMP)destination);
 }
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,9*NSEC_PER_SEC),dispatch_get_main_queue(),^{@autoreleasepool{
  for(NSWindow *window in [NSApp windows]){
   NSRect frame=[window frame],content=[window contentRectForFrameRect:frame];
   fprintf(stderr,"FXWINDOW pid=%d number=%ld class=%s style=%lu frame=%s content=%s\n",getpid(),(long)[window windowNumber],object_getClassName(window),(unsigned long)[window styleMask],[NSStringFromRect(frame) UTF8String],[NSStringFromRect(content) UTF8String]);
   int remaining=80;viewTree([[window contentView] superview]?:[window contentView],0,&remaining);
  }
 }});
}
