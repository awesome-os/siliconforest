/* SPDX-License-Identifier: MIT */
/* Our normalized level-meter drawing; no capture/audio measurements invented. */
#import <AVKit/AVKit.h>
#include <math.h>
@implementation AVAudioLevelIndicatorView
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self=[super initWithFrame:frame])) _sfLevelValue=0;
    return self;
}
- (double)doubleValue { return _sfLevelValue; }
- (void)setDoubleValue:(double)value {
    _sfLevelValue=value;
    [self setNeedsDisplay:YES];
}
- (BOOL)allowsVibrancy { return YES; }
- (NSSize)intrinsicContentSize { return NSMakeSize(-1,9); }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSRect bounds=self.bounds;
    [[NSColor colorWithCalibratedWhite:.15 alpha:1] set];
    NSRectFill(bounds);
    if (NSWidth(bounds)<=0 || NSHeight(bounds)<=0) return;
    /* Preserve the full caller value, but draw our meter over [0,1]. */
    double level=isfinite(_sfLevelValue)?MAX(0,MIN(1,_sfLevelValue)):0;
    const unsigned segments=24;
    CGFloat step=NSWidth(bounds)/segments;
    for (unsigned n=0;n<segments;n++) {
        if ((n+.5)/segments>level) break;
        if (n>=22) [[NSColor colorWithCalibratedRed:1 green:.25 blue:.15 alpha:1] set];
        else if (n>=18) [[NSColor colorWithCalibratedRed:1 green:.8 blue:.2 alpha:1] set];
        else [[NSColor colorWithCalibratedRed:.2 green:.8 blue:.35 alpha:1] set];
        NSRectFill(NSMakeRect(NSMinX(bounds)+n*step,NSMinY(bounds),MAX(0,step-1),NSHeight(bounds)));
    }
}
@end
