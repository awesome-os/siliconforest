// SPDX-License-Identifier: MIT
// sf-gllayer-probe [seconds]: a window whose content view hosts a CALayer tree with
//   - a CAOpenGLLayer subclass (made at run time, so this builds against a QuartzCore without the class) that is
//     asynchronous and clears its CGL surface to red, green, blue in turn (glClearColor/glClear), and
//   - a plain CALayer whose delegate draws it (-drawLayer:inContext:, a yellow box on dark blue) after
//     -setNeedsDisplay.
// Prints GLPROBE lines (each draw, the class found or not); the window's own pictures (SF_WS_DUMP_DIR) show what
// reached the window. Without OpenGL the CAOpenGLLayer is expected to say so and draw nothing (no crash).
#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <time.h>

static int frames;
static void (*pglClearColor)(float, float, float, float);
static void (*pglClear)(unsigned);
static void (*pglFlush)(void);

static void drawGL(id self, SEL _cmd, void *ctx, void *pf, CFTimeInterval t, const void *ts) {
    int n = frames++;
    float r = n % 3 == 0 ? 0.85f : 0.1f, g = n % 3 == 1 ? 0.75f : 0.1f, b = n % 3 == 2 ? 0.9f : 0.1f;
    if (n < 3 || n % 30 == 0) { printf("GLPROBE draw %d ctx %p\n", n, ctx); fflush(stdout); }
    if (pglClearColor && pglClear) { pglClearColor(r, g, b, 1); pglClear(0x4000 /* GL_COLOR_BUFFER_BIT */); }
    if (pglFlush) pglFlush();
}

@interface Painter : NSObject
@end
@implementation Painter
- (void)end:(id)unused {
    printf("GLPROBE end frames=%d\n", frames); fflush(stdout);
    [NSApp terminate:nil];
}
- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGRect b = [layer bounds];
    CGContextSetRGBFillColor(ctx, 0.1, 0.15, 0.4, 1);
    CGContextFillRect(ctx, b);
    CGContextSetRGBFillColor(ctx, 1, 0.85, 0.1, 1);
    CGContextFillRect(ctx, CGRectInset(b, b.size.width / 4, b.size.height / 4));
    printf("GLPROBE delegate drew %.0fx%.0f\n", b.size.width, b.size.height); fflush(stdout);
}
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        double seconds = argc > 1 ? atof(argv[1]) : 5;
        pglClearColor = dlsym(RTLD_DEFAULT, "glClearColor");
        pglClear = dlsym(RTLD_DEFAULT, "glClear");
        pglFlush = dlsym(RTLD_DEFAULT, "glFlush");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 480, 220)
                                                  styleMask:NSTitledWindowMask | NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [w setTitle:@"sf-gllayer-probe"];
        NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 220)];
        CALayer *root = [CALayer layer];
        CGColorRef grey = CGColorCreateGenericRGB(0.2, 0.2, 0.2, 1);
        [root setBackgroundColor:grey];
        [v setLayer:root];

        Class base = NSClassFromString(@"CAOpenGLLayer");
        printf("GLPROBE CAOpenGLLayer %s\n", base ? "found" : "MISSING"); fflush(stdout);
        if (base) {
            Class sub = objc_allocateClassPair(base, "SFProbeGLLayer", 0);
            class_addMethod(sub, @selector(drawInCGLContext:pixelFormat:forLayerTime:displayTime:), (IMP)drawGL, "v@:^v^vd^v");
            objc_registerClassPair(sub);
            CALayer *gl = [sub layer];
            [gl setFrame:CGRectMake(20, 50, 200, 120)];
            [(id)gl setAsynchronous:YES];
            [root addSublayer:gl];
        }
        CALayer *drawn = [CALayer layer];
        Painter *painter = [[Painter alloc] init];
        [drawn setDelegate:painter];
        [drawn setFrame:CGRectMake(260, 50, 200, 120)];
        [drawn setNeedsDisplay];
        [root addSublayer:drawn];

        [w setContentView:v];
        [w makeKeyAndOrderFront:nil];
        [painter performSelector:@selector(end:) withObject:nil afterDelay:seconds];
        [NSApp run];
    }
    return 0;
}
