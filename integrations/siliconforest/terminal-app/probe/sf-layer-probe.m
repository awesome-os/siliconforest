// SPDX-License-Identifier: MIT
// sf-layer-probe [seconds]: a window whose content view hosts a CALayer (-setLayer:, no -setWantsLayer:) whose
// contents is a CGImage replaced ten times a second (a solid colour that steps through red, green, blue, with a
// white square that moves), plus a sublayer with a background colour and a corner radius and a second sublayer
// holding a 32-bit BGRA CVPixelBuffer when CoreVideo is loaded. What a movie player does with its frames.
// Prints LAYERPROBE frame=<n> t=<ms> for each contents set; the window's own pictures (SF_WS_DUMP_DIR) show
// whether they reached the window.
#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CATransaction.h>
#include <dlfcn.h>
#include <time.h>

static CGImageRef frameImage(int n, size_t w, size_t h) {
    uint32_t *px = malloc(w * h * 4);
    uint32_t colours[3] = { 0xffd03020, 0xff20b040, 0xff2050e0 };   // ARGB: red, green, blue
    uint32_t c = colours[(n / 10) % 3];
    size_t sx = (n % 10) * (w / 10), sy = h / 3, s = w / 10;
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++)
            px[y * w + x] = (x >= sx && x < sx + s && y >= sy && y < sy + s) ? 0xffffffff : c;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, px, w * h * 4, NULL);
    CGImageRef image = CGImageCreate(w, h, 8, 32, w * 4, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, p, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(p);
    CGColorSpaceRelease(space);
    return image;   // the pixels stay with it (a probe: never freed)
}

static double nowMs(void) {
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

@interface Probe : NSObject { CALayer *layer; int n; NSTimeInterval end; }
@end
@implementation Probe
- (instancetype)initWithLayer:(CALayer *)l seconds:(double)s { if ((self = [super init])) { layer = [l retain]; end = [NSDate timeIntervalSinceReferenceDate] + s; } return self; }
- (void)tick:(NSTimer *)t {
    CGImageRef image = frameImage(n, 320, 180);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer setContents:(id)image];
    [CATransaction commit];
    CGImageRelease(image);
    printf("LAYERPROBE frame=%d t=%.1f\n", n, nowMs()); fflush(stdout);
    n++;
    if ([NSDate timeIntervalSinceReferenceDate] >= end) [NSApp terminate:nil];
}
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        double seconds = argc > 1 ? atof(argv[1]) : 5;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 480, 300)
                                                  styleMask:NSTitledWindowMask | NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [w setTitle:@"sf-layer-probe"];
        NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 300)];
        CALayer *root = [CALayer layer];
        [root setContentsGravity:kCAGravityResizeAspect];
        CGColorRef grey = CGColorCreateGenericRGB(0.2, 0.2, 0.2, 1);
        [root setBackgroundColor:grey];
        [v setLayer:root];
        CALayer *badge = [CALayer layer];
        [badge setFrame:CGRectMake(10, 10, 60, 30)];
        CGColorRef yellow = CGColorCreateGenericRGB(1, 0.85, 0.1, 1);
        [badge setBackgroundColor:yellow];
        [badge setCornerRadius:8];
        [root addSublayer:badge];
        // a CVPixelBuffer sublayer, when CoreVideo is there
        void *cv = dlopen("/System/Library/Frameworks/CoreVideo.framework/CoreVideo", RTLD_NOW);
        int (*create)(CFAllocatorRef, size_t, size_t, uint32_t, CFDictionaryRef, void **) = cv ? dlsym(cv, "CVPixelBufferCreate") : NULL;
        int (*lock)(void *, uint64_t) = cv ? dlsym(cv, "CVPixelBufferLockBaseAddress") : NULL;
        int (*unlock)(void *, uint64_t) = cv ? dlsym(cv, "CVPixelBufferUnlockBaseAddress") : NULL;
        void *(*base)(void *) = cv ? dlsym(cv, "CVPixelBufferGetBaseAddress") : NULL;
        size_t (*rowb)(void *) = cv ? dlsym(cv, "CVPixelBufferGetBytesPerRow") : NULL;
        void *pb = NULL;
        if (create && lock && unlock && base && rowb && create(NULL, 64, 64, 'BGRA', NULL, &pb) == 0 && pb) {
            lock(pb, 0);
            uint8_t *b = base(pb); size_t r = rowb(pb);
            for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { uint8_t *p = b + y * r + x * 4; p[0] = 0xe0; p[1] = 0x20; p[2] = 0xe0; p[3] = 0xff; }   // magenta
            unlock(pb, 0);
            CALayer *pix = [CALayer layer];
            [pix setFrame:CGRectMake(400, 220, 64, 64)];
            [pix setContents:(id)pb];
            [root addSublayer:pix];
            printf("LAYERPROBE pixelbuffer sublayer\n");
        } else printf("LAYERPROBE no CoreVideo pixel buffer\n");
        [w setContentView:v];
        [w makeKeyAndOrderFront:nil];
        Probe *probe = [[Probe alloc] initWithLayer:root seconds:seconds];
        [NSTimer scheduledTimerWithTimeInterval:0.1 target:probe selector:@selector(tick:) userInfo:nil repeats:YES];
        [NSApp run];
    }
    return 0;
}
