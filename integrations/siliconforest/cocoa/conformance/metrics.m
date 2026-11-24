// SPDX-License-Identifier: MIT
// conformance/metrics.m - what AppKit says about fonts and controls, as JSON on stdout.
//
// The same source is built for macOS (the reference: clang -fobjc-arc? no, plain
// `clang -framework AppKit metrics.m -o metrics`) and for SiliconForest; the two outputs are
// compared by compare.py. Everything here is geometry in points: font metrics, string
// advances, control sizes and baselines. Nothing is drawn on a screen; with a path argument
// one small view tree is also rendered offscreen to PNG files (<path>-1x.png, <path>-2x.png).
// A method the running AppKit does not have is reported as null: that is a finding too.
#import <AppKit/AppKit.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

// Declarations only, for an AppKit whose headers lack these: every use is guarded by
// respondsToSelector at run time, and a missing method is reported as null.
@interface NSView (ConformanceOptional)
- (NSSize)intrinsicContentSize;
- (NSSize)fittingSize;
- (NSEdgeInsets)alignmentRectInsets;
- (CGFloat)firstBaselineOffsetFromTop;
- (CGFloat)baselineOffsetFromBottom;
- (CGFloat)lastBaselineOffsetFromBottom;
- (float)contentHuggingPriorityForOrientation:(NSInteger)orientation;
- (float)contentCompressionResistancePriorityForOrientation:(NSInteger)orientation;
- (void)setTranslatesAutoresizingMaskIntoConstraints:(BOOL)flag;
- (void)addConstraint:(id)constraint;
- (void)layoutSubtreeIfNeeded;
- (void)displayRectIgnoringOpacity:(NSRect)rect inContext:(NSGraphicsContext *)context;
@end
@interface NSFont (ConformanceOptional)
+ (NSFont *)systemFontOfSize:(CGFloat)size weight:(CGFloat)weight;
+ (NSFont *)monospacedSystemFontOfSize:(CGFloat)size weight:(CGFloat)weight;
+ (NSFont *)monospacedDigitSystemFontOfSize:(CGFloat)size weight:(CGFloat)weight;
- (NSRect)boundingRectForFont;
@end
@interface NSScroller (ConformanceOptional)
+ (CGFloat)scrollerWidthForControlSize:(NSControlSize)controlSize scrollerStyle:(NSInteger)style;
@end
@interface NSLayoutManager (ConformanceOptional)
- (CGFloat)defaultLineHeightForFont:(NSFont *)font;
@end

static BOOL first;
static void key(const char *k) { printf("%s\"%s\": ", first ? "" : ", ", k); first = NO; }
static void open_obj(const char *k) { if (k) key(k); printf("{"); first = YES; }
static void close_obj(void) { printf("}"); first = NO; }
static void num(const char *k, double v) { key(k); printf("%.4f", v); }
static void null(const char *k) { key(k); printf("null"); }
static void str(const char *k, NSString *v)
{
    key(k);
    if (v == nil) { printf("null"); return; }
    printf("\"");
    for (const char *p = [v UTF8String]; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if ((unsigned char)*p < 0x20) printf(" ");
        else putchar(*p);
    }
    printf("\"");
}
static void size(const char *k, NSSize s) { open_obj(k); num("w", s.width); num("h", s.height); close_obj(); }
static void rect(const char *k, NSRect r)
{
    open_obj(k); num("x", r.origin.x); num("y", r.origin.y); num("w", r.size.width); num("h", r.size.height); close_obj();
}

static NSArray *strings(void)
{
    return @[ @"Hello, SiliconForest. This is AppKit.", @"The quick brown fox jumps over the lazy dog", @"0123456789",
              @"Press Me", @"WAVE Typography fi fl", @"iiiiiiiiii", @"MMMMMMMMMM" ];
}

static void font(const char *k, NSFont *f)
{
    if (f == nil) { null(k); return; }
    open_obj(k);
    str("fontName", [f fontName]);
    str("familyName", [f familyName]);
    num("pointSize", [f pointSize]);
    num("ascender", [f ascender]);
    num("descender", [f descender]);
    num("leading", [f leading]);
    num("capHeight", [f capHeight]);
    num("xHeight", [f xHeight]);
    if ([f respondsToSelector:@selector(boundingRectForFont)]) rect("boundingRectForFont", [f boundingRectForFont]);
    NSLayoutManager *lm = [[NSLayoutManager alloc] init];
    if ([lm respondsToSelector:@selector(defaultLineHeightForFont:)]) num("defaultLineHeight", [lm defaultLineHeightForFont:f]);
    else null("defaultLineHeight");
    open_obj("advances");
    int i = 0;
    for (NSString *s in strings()) {
        char name[16];
        snprintf(name, sizeof(name), "s%d", i++);
        NSSize sz = [s sizeWithAttributes:@{ NSFontAttributeName : f }];
        size(name, sz);
    }
    close_obj();
    close_obj();
}

static NSFont *systemFont(CGFloat pointSize, CGFloat weight)
{
    if ([NSFont respondsToSelector:@selector(systemFontOfSize:weight:)])
        return [NSFont systemFontOfSize:pointSize weight:weight];
    return nil;
}

static void control(const char *k, NSView *v)
{
    open_obj(k);
    if ([v respondsToSelector:@selector(sizeToFit)]) [(NSControl *)v sizeToFit];
    size("sizeToFit", [v frame].size);
    if ([v respondsToSelector:@selector(intrinsicContentSize)]) size("intrinsicContentSize", [v intrinsicContentSize]); else null("intrinsicContentSize");
    if ([v respondsToSelector:@selector(fittingSize)]) size("fittingSize", [v fittingSize]); else null("fittingSize");
    if ([v respondsToSelector:@selector(alignmentRectInsets)]) {
        NSEdgeInsets in = [v alignmentRectInsets];
        open_obj("alignmentRectInsets"); num("top", in.top); num("left", in.left); num("bottom", in.bottom); num("right", in.right); close_obj();
    } else null("alignmentRectInsets");
    if ([v respondsToSelector:@selector(firstBaselineOffsetFromTop)]) num("firstBaselineOffsetFromTop", [v firstBaselineOffsetFromTop]); else null("firstBaselineOffsetFromTop");
    if ([v respondsToSelector:@selector(baselineOffsetFromBottom)]) num("baselineOffsetFromBottom", [v baselineOffsetFromBottom]); else null("baselineOffsetFromBottom");
    if ([v respondsToSelector:@selector(lastBaselineOffsetFromBottom)]) num("lastBaselineOffsetFromBottom", [v lastBaselineOffsetFromBottom]); else null("lastBaselineOffsetFromBottom");
    if ([v respondsToSelector:@selector(contentHuggingPriorityForOrientation:)]) {
        open_obj("contentHugging"); num("horizontal", [v contentHuggingPriorityForOrientation:0]); num("vertical", [v contentHuggingPriorityForOrientation:1]); close_obj();
    } else null("contentHugging");
    if ([v respondsToSelector:@selector(contentCompressionResistancePriorityForOrientation:)]) {
        open_obj("contentCompressionResistance"); num("horizontal", [v contentCompressionResistancePriorityForOrientation:0]); num("vertical", [v contentCompressionResistancePriorityForOrientation:1]); close_obj();
    } else null("contentCompressionResistance");
    if ([v isKindOfClass:[NSControl class]] && [(NSControl *)v cell] != nil) {
        NSCell *cell = [(NSControl *)v cell];
        size("cellSize", [cell cellSize]);
        NSRect b = [v bounds];
        if ([cell respondsToSelector:@selector(titleRectForBounds:)]) rect("titleRect", [cell titleRectForBounds:b]);
        if ([cell respondsToSelector:@selector(drawingRectForBounds:)]) rect("drawingRect", [cell drawingRectForBounds:b]);
    }
    close_obj();
}

static void controlsAt(const char *k, NSControlSize cs)
{
    open_obj(k);
    CGFloat fs = [NSFont systemFontSizeForControlSize:cs];
    num("systemFontSizeForControlSize", fs);
    NSFont *f = [NSFont systemFontOfSize:fs];
#define SIZED(v) do { if ([[(NSControl *)(v) cell] respondsToSelector:@selector(setControlSize:)]) [[(NSControl *)(v) cell] setControlSize:cs]; [(NSControl *)(v) setFont:f]; } while (0)
    NSButton *push = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 32)];
    [push setTitle:@"Press Me"]; [push setBezelStyle:NSRoundedBezelStyle]; [push setButtonType:NSMomentaryPushInButton]; SIZED(push);
    control("pushButton", push);
    NSButton *check = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 18)];
    [check setTitle:@"Enable feature"]; [check setButtonType:NSSwitchButton]; SIZED(check);
    control("checkbox", check);
    NSButton *radio = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 18)];
    [radio setTitle:@"Option"]; [radio setButtonType:NSRadioButton]; SIZED(radio);
    control("radio", radio);
    NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 26) pullsDown:NO];
    [popup addItemWithTitle:@"First choice"]; [popup addItemWithTitle:@"Second"]; SIZED(popup);
    control("popUpButton", popup);
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 22)];
    [field setStringValue:@"editable"]; SIZED(field);
    control("textField", field);
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 17)];
    [label setStringValue:@"Hello, SiliconForest. This is AppKit."]; [label setBezeled:NO]; [label setDrawsBackground:NO]; [label setEditable:NO]; SIZED(label);
    control("label", label);
    NSSlider *slider = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, 160, 21)];
    SIZED(slider);
    control("slider", slider);
    NSSegmentedControl *seg = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(0, 0, 160, 24)];
    [seg setSegmentCount:3]; [seg setLabel:@"One" forSegment:0]; [seg setLabel:@"Two" forSegment:1]; [seg setLabel:@"Three" forSegment:2]; SIZED(seg);
    control("segmented", seg);
    NSProgressIndicator *bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 160, 20)];
    if ([bar respondsToSelector:@selector(setControlSize:)]) [bar setControlSize:cs];
    control("progressBar", bar);
    close_obj();
}

// Auto Layout itself: small arrangements held together by required equalities only, the size they
// fit in and the frames they get. Attribute numbers are AppKit's (NSLayoutAttribute).
enum { A_LEFT = 1, A_RIGHT = 2, A_TOP = 3, A_BOTTOM = 4, A_WIDTH = 7, A_HEIGHT = 8, A_CENTERX = 9, A_CENTERY = 10 };

static void pin(NSView *holder, id a, NSInteger aa, id b, NSInteger ab, CGFloat constant)
{
    Class c = NSClassFromString(@"NSLayoutConstraint");
    id constraint = [c constraintWithItem:a attribute:aa relatedBy:0 toItem:b attribute:ab multiplier:1 constant:constant];

    [holder addConstraint:constraint];
}

static NSTextField *plainLabel(NSString *text)
{
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];

    [label setStringValue:text]; [label setBezeled:NO]; [label setDrawsBackground:NO]; [label setEditable:NO];
    [label setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [label setTranslatesAutoresizingMaskIntoConstraints:NO];
    return label;
}

static NSButton *plainButton(NSString *title)
{
    NSButton *button = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];

    [button setTitle:title]; [button setBezelStyle:NSRoundedBezelStyle]; [button setButtonType:NSMomentaryPushInButton];
    [button setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [button setTranslatesAutoresizingMaskIntoConstraints:NO];
    return button;
}

static void layoutCases(const char *k)
{
    open_obj(k);
    if (NSClassFromString(@"NSLayoutConstraint") == Nil || ![NSView instancesRespondToSelector:@selector(fittingSize)] ||
        ![NSView instancesRespondToSelector:@selector(layoutSubtreeIfNeeded)]) {
        null("row"); null("form"); null("centered");
        close_obj();
        return;
    }
    {   // a label and a button side by side, 20 points from the sides, 8 apart, 12 above and below the label
        NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 120)];
        NSTextField *label = plainLabel(@"Name of the thing:");
        NSButton *button = plainButton(@"Choose");

        [box addSubview:label]; [box addSubview:button];
        pin(box, label, A_LEFT, box, A_LEFT, 20);
        pin(box, button, A_LEFT, label, A_RIGHT, 8);
        pin(box, box, A_RIGHT, button, A_RIGHT, 20);
        pin(box, label, A_TOP, box, A_TOP, 12);
        pin(box, box, A_BOTTOM, label, A_BOTTOM, 12);
        pin(box, button, A_CENTERY, label, A_CENTERY, 0);
        open_obj("row");
        size("fittingSize", [box fittingSize]);
        [box setFrameSize:[box fittingSize]];
        [box layoutSubtreeIfNeeded];
        rect("label", [label frame]); rect("button", [button frame]);
        close_obj();
    }
    {   // a label over a field that fills the width, a button under it at the right
        NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];
        NSTextField *label = plainLabel(@"Address");
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
        NSButton *button = plainButton(@"OK");

        [field setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
        [field setTranslatesAutoresizingMaskIntoConstraints:NO];
        [box addSubview:label]; [box addSubview:field]; [box addSubview:button];
        pin(box, label, A_LEFT, box, A_LEFT, 20);
        pin(box, label, A_TOP, box, A_TOP, 20);
        pin(box, field, A_LEFT, box, A_LEFT, 20);
        pin(box, box, A_RIGHT, field, A_RIGHT, 20);
        pin(box, field, A_TOP, label, A_BOTTOM, 8);
        pin(box, button, A_TOP, field, A_BOTTOM, 12);
        pin(box, box, A_RIGHT, button, A_RIGHT, 20);
        pin(box, box, A_BOTTOM, button, A_BOTTOM, 20);
        open_obj("form");
        size("fittingSize", [box fittingSize]);
        [box setFrameSize:NSMakeSize(320, [box fittingSize].height)];
        [box layoutSubtreeIfNeeded];
        rect("label", [label frame]); rect("field", [field frame]); rect("button", [button frame]);
        close_obj();
    }
    {   // one button in the middle of a box of a given size
        NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 100)];
        NSButton *button = plainButton(@"Centered");

        [box addSubview:button];
        pin(box, button, A_CENTERX, box, A_CENTERX, 0);
        pin(box, button, A_CENTERY, box, A_CENTERY, 0);
        open_obj("centered");
        size("fittingSize", [box fittingSize]);
        [box layoutSubtreeIfNeeded];
        rect("button", [button frame]);
        close_obj();
    }
    close_obj();
}

static void color(const char *k, NSColor *c)
{
    NSColor *rgb = [c colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    if (rgb == nil) { null(k); return; }
    open_obj(k); num("r", [rgb redComponent]); num("g", [rgb greenComponent]); num("b", [rgb blueComponent]); num("a", [rgb alphaComponent]); close_obj();
}

static void colors(const char *k)
{
    open_obj(k);
#define C(name) if ([NSColor respondsToSelector:@selector(name)]) color(#name, [NSColor name]); else null(#name)
    C(windowBackgroundColor); C(controlBackgroundColor); C(controlColor); C(controlTextColor); C(disabledControlTextColor);
    C(labelColor); C(secondaryLabelColor); C(tertiaryLabelColor); C(textColor); C(textBackgroundColor); C(selectedTextBackgroundColor);
    C(selectedControlColor); C(controlAccentColor); C(keyboardFocusIndicatorColor); C(separatorColor); C(gridColor); C(linkColor);
    C(placeholderTextColor); C(selectedContentBackgroundColor); C(underPageBackgroundColor);
    close_obj();
}

// One small view tree in a window that is never shown, drawn into a bitmap the way AppKit caches
// a view's display (at the window's backing scale: 2 on the reference Mac, the compositor's scale
// on SiliconForest). The window is not key, so controls are in their inactive-window state on
// both sides. <path>-window.png, and the scale it was drawn at on stderr and in the JSON.
static double render(NSString *path)
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 420, 120)
                                                   styleMask:NSTitledWindowMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    NSView *root = [window contentView];
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 84, 380, 17)];
    [label setStringValue:@"Hello, SiliconForest. This is AppKit."]; [label setBezeled:NO]; [label setDrawsBackground:NO]; [label setEditable:NO];
    [root addSubview:label];
    NSButton *push = [[NSButton alloc] initWithFrame:NSMakeRect(14, 44, 110, 32)];
    [push setTitle:@"Press Me"]; [push setBezelStyle:NSRoundedBezelStyle];
    [root addSubview:push];
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(140, 50, 160, 22)];
    [field setStringValue:@"editable"];
    [root addSubview:field];
    NSButton *check = [[NSButton alloc] initWithFrame:NSMakeRect(18, 16, 160, 18)];
    [check setTitle:@"Enable feature"]; [check setButtonType:NSSwitchButton]; [check setState:NSOnState];
    [root addSubview:check];
    NSSlider *slider = [[NSSlider alloc] initWithFrame:NSMakeRect(200, 14, 160, 21)];
    [slider setDoubleValue:0.4];
    [root addSubview:slider];
    NSBitmapImageRep *rep = [root bitmapImageRepForCachingDisplayInRect:[root bounds]];
    if (rep == nil) { fprintf(stderr, "metrics: no bitmap for caching display\n"); return 0; }
    [root cacheDisplayInRect:[root bounds] toBitmapImageRep:rep];
    double scale = (double)[rep pixelsWide] / 420.0;
    NSData *png = [rep representationUsingType:NSPNGFileType properties:@{}];
    NSString *file = [NSString stringWithFormat:@"%@-window.png", path];
    if (![png writeToFile:file atomically:YES]) fprintf(stderr, "metrics: could not write %s\n", [file UTF8String]);
    else fprintf(stderr, "metrics: wrote %s (%ldx%ld pixels, scale %.2f)\n", [file UTF8String], (long)[rep pixelsWide], (long)[rep pixelsHigh], scale);
    return scale;
}

/* The control sheet: every control the suite knows, at its own size (-sizeToFit), in the three
 * control sizes side by side, drawn like render() above into <path>-controls.png. Where each one
 * was put is written to the JSON ("sheet"), so the picture can be cut up and compared control by
 * control without guessing. Rows are 36 points apart from the top, columns 220. */
/* A window that says it is the key and main window without being shown: controls ask their
 * window whether to draw in the active (accent coloured) or the inactive (grey) state, and a
 * window that is never on screen is otherwise never active. Nothing on the screen is touched. */
@interface ActiveWindow : NSWindow
@end

@implementation ActiveWindow
- (BOOL)isKeyWindow { return YES; }
- (BOOL)isMainWindow { return YES; }
- (BOOL)hasKeyAppearance { return YES; }
- (BOOL)hasMainAppearance { return YES; }
- (BOOL)_hasKeyAppearance { return YES; }
- (BOOL)_hasMainAppearance { return YES; }
- (BOOL)_hasActiveAppearance { return YES; }
- (BOOL)_hasActiveControls { return YES; }
@end

static NSView *sheetRoot;
static int sheetRow;
static NSControlSize sheetSize;
static int sheetColumn;

static void place(const char *name, NSView *v, CGFloat width)
{
    char key[64];
    NSRect frame;

    if ([v respondsToSelector:@selector(cell)] && [[(NSControl *)v cell] respondsToSelector:@selector(setControlSize:)])
        [[(NSControl *)v cell] setControlSize:sheetSize];
    else if ([v respondsToSelector:@selector(setControlSize:)]) [(NSProgressIndicator *)v setControlSize:sheetSize];
    if ([v respondsToSelector:@selector(setFont:)])
        [(NSControl *)v setFont:[NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:sheetSize]]];
    if ([v respondsToSelector:@selector(sizeToFit)]) [(NSControl *)v sizeToFit];
    frame = [v frame];
    if (width > 0) frame.size.width = width;
    frame.origin.x = 20 + sheetColumn * 220;
    frame.origin.y = [sheetRoot bounds].size.height - 16 - sheetRow * 36 - 28 + floor((28 - frame.size.height) / 2);
    [v setFrame:frame];
    [sheetRoot addSubview:v];
    snprintf(key, sizeof(key), "%s.%s", sheetSize == NSRegularControlSize ? "regular" : sheetSize == NSSmallControlSize ? "small" : "mini", name);
    rect(key, frame);
    sheetRow++;
}

static NSButton *sheetButton(NSString *title, NSButtonType type)
{
    NSButton *b = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 32)];

    [b setTitle:title]; [b setButtonType:type];
    if (type == NSMomentaryPushInButton) [b setBezelStyle:NSRoundedBezelStyle];
    return b;
}

static double renderSheet(NSString *path, BOOL active)
{
    static const NSControlSize sizes[3] = { NSRegularControlSize, NSSmallControlSize, NSMiniControlSize };
    NSWindow *window = [[(active ? [ActiveWindow class] : [NSWindow class]) alloc] initWithContentRect:NSMakeRect(0, 0, 680, 560)
                                                   styleMask:NSTitledWindowMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];

    sheetRoot = [window contentView];
    open_obj(active ? "sheetActive" : "sheet");
    size("size", [sheetRoot bounds].size);
    for (sheetColumn = 0; sheetColumn < 3; sheetColumn++) {
        NSButton *b;
        NSTextField *f;

        sheetSize = sizes[sheetColumn];
        sheetRow = 0;
        place("push", sheetButton(@"Button", NSMomentaryPushInButton), 0);
        b = sheetButton(@"Disabled", NSMomentaryPushInButton); [b setEnabled:NO];
        place("pushDisabled", b, 0);
        b = sheetButton(@"Default", NSMomentaryPushInButton); [b setKeyEquivalent:@"\r"];
        place("pushDefault", b, 0);
        b = sheetButton(@"Checked box", NSSwitchButton); [b setState:NSOnState];
        place("checkOn", b, 0);
        place("checkOff", sheetButton(@"Empty box", NSSwitchButton), 0);
        b = sheetButton(@"Disabled box", NSSwitchButton); [b setState:NSOnState]; [b setEnabled:NO];
        place("checkDisabled", b, 0);
        b = sheetButton(@"Chosen", NSRadioButton); [b setState:NSOnState];
        place("radioOn", b, 0);
        place("radioOff", sheetButton(@"Not chosen", NSRadioButton), 0);
        {
            NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 26) pullsDown:NO];

            [popup addItemWithTitle:@"First choice"]; [popup addItemWithTitle:@"Second"];
            place("popUp", popup, 0);
        }
        f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 22)]; [f setStringValue:@"Some text"];
        place("field", f, 160);
        f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 22)]; [f setStringValue:@"Disabled"]; [f setEnabled:NO];
        place("fieldDisabled", f, 160);
        {
            NSSlider *slider = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, 160, 21)];

            [slider setDoubleValue:0.4];
            place("slider", slider, 160);
        }
        {
            NSProgressIndicator *bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 160, 20)];

            [bar setIndeterminate:NO]; [bar setMinValue:0]; [bar setMaxValue:100]; [bar setDoubleValue:60];
            place("progress", bar, 160);
        }
        {
            NSSegmentedControl *seg = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(0, 0, 160, 24)];

            [seg setSegmentCount:3]; [seg setLabel:@"One" forSegment:0]; [seg setLabel:@"Two" forSegment:1]; [seg setLabel:@"Three" forSegment:2];
            [seg setSelectedSegment:1];
            place("segmented", seg, 0);
        }
        f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 17)];
        [f setStringValue:@"A label, 0123456789"]; [f setBezeled:NO]; [f setDrawsBackground:NO]; [f setEditable:NO];
        place("label", f, 0);
    }
    close_obj();

    NSBitmapImageRep *rep = [sheetRoot bitmapImageRepForCachingDisplayInRect:[sheetRoot bounds]];
    if (rep == nil) { fprintf(stderr, "metrics: no bitmap for the control sheet\n"); return 0; }
    [sheetRoot cacheDisplayInRect:[sheetRoot bounds] toBitmapImageRep:rep];
    NSData *png = [rep representationUsingType:NSPNGFileType properties:@{}];
    NSString *file = [NSString stringWithFormat:@"%@-controls%s.png", path, active ? "-active" : ""];
    if (![png writeToFile:file atomically:YES]) fprintf(stderr, "metrics: could not write %s\n", [file UTF8String]);
    else fprintf(stderr, "metrics: wrote %s (%ldx%ld pixels)\n", [file UTF8String], (long)[rep pixelsWide], (long)[rep pixelsHigh]);
    return (double)[rep pixelsWide] / 680.0;
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        open_obj(NULL);
        num("systemFontSize", [NSFont systemFontSize]);
        num("smallSystemFontSize", [NSFont smallSystemFontSize]);
        num("labelFontSize", [NSFont labelFontSize]);
        open_obj("systemFont");
        static const double sizes[] = { 9, 10, 11, 12, 13, 14, 15, 17, 20, 24, 28 };
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            char name[16];
            snprintf(name, sizeof(name), "%g", sizes[i]);
            font(name, [NSFont systemFontOfSize:sizes[i]]);
        }
        close_obj();
        open_obj("boldSystemFont");
        font("11", [NSFont boldSystemFontOfSize:11]); font("13", [NSFont boldSystemFontOfSize:13]); font("17", [NSFont boldSystemFontOfSize:17]);
        close_obj();
        // NSFontWeight values, spelled out: the constants do not exist in every AppKit this builds against
        open_obj("systemFont13ByWeight");
        static const struct { const char *name; double weight; } weights[] = {
            { "ultraLight", -0.8 }, { "thin", -0.6 }, { "light", -0.4 }, { "regular", 0 }, { "medium", 0.23 },
            { "semibold", 0.3 }, { "bold", 0.4 }, { "heavy", 0.56 }, { "black", 0.62 } };
        for (unsigned i = 0; i < sizeof(weights) / sizeof(weights[0]); i++)
            font(weights[i].name, systemFont(13, weights[i].weight));
        close_obj();
        open_obj("otherFonts");
        font("userFixedPitch11", [NSFont userFixedPitchFontOfSize:11]);
        if ([NSFont respondsToSelector:@selector(monospacedSystemFontOfSize:weight:)]) font("monospacedSystem13", [NSFont monospacedSystemFontOfSize:13 weight:0]);
        else null("monospacedSystem13");
        if ([NSFont respondsToSelector:@selector(monospacedDigitSystemFontOfSize:weight:)]) font("monospacedDigit13", [NSFont monospacedDigitSystemFontOfSize:13 weight:0]);
        else null("monospacedDigit13");
        font("menu", [NSFont menuFontOfSize:0]);
        font("menuBar", [NSFont menuBarFontOfSize:0]);
        font("message", [NSFont messageFontOfSize:0]);
        font("titleBar", [NSFont titleBarFontOfSize:0]);
        font("toolTips", [NSFont toolTipsFontOfSize:0]);
        font("controlContent", [NSFont controlContentFontOfSize:0]);
        font("label", [NSFont labelFontOfSize:0]);
        close_obj();
        open_obj("controls");
        controlsAt("regular", NSRegularControlSize);
        controlsAt("small", NSSmallControlSize);
        controlsAt("mini", NSMiniControlSize);
        close_obj();
        colors("colors");
        layoutCases("layout");
        {   // the per-user cache directory, which sandboxed applications ask for before they start
            char dir[1024] = "", real[1024] = "";
            size_t n = confstr(_CS_DARWIN_USER_CACHE_DIR, dir, sizeof(dir));
            char *r = n ? realpath(dir, real) : NULL;

            open_obj("userCacheDirectory");
            num("confstrLength", (double)n);
            num("confstrErrno", n ? 0 : errno);
            num("realpathWorks", r != NULL);
            num("realpathErrno", r ? 0 : errno);
            fprintf(stderr, "metrics: user cache directory \"%s\" (confstr %zu) realpath \"%s\" errno %d\n", dir, n, r ? real : "", r ? 0 : errno);
            close_obj();
        }
        open_obj("scroller");
        if ([NSScroller respondsToSelector:@selector(scrollerWidthForControlSize:scrollerStyle:)]) {
            num("legacyWidth", [NSScroller scrollerWidthForControlSize:NSRegularControlSize scrollerStyle:0]);
            num("overlayWidth", [NSScroller scrollerWidthForControlSize:NSRegularControlSize scrollerStyle:1]);
        } else num("width", [NSScroller scrollerWidth]);
        close_obj();
        open_obj("window");
        rect("frameForContent400x300Titled", [NSWindow frameRectForContentRect:NSMakeRect(0, 0, 400, 300)
            styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask]);
        close_obj();
        if (argc > 1) {
            num("renderScale", render([NSString stringWithUTF8String:argv[1]]));
            renderSheet([NSString stringWithUTF8String:argv[1]], NO);
            renderSheet([NSString stringWithUTF8String:argv[1]], YES);
        }
        close_obj();
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
