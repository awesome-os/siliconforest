// SPDX-License-Identifier: MIT
// CocoaHello: the smallest AppKit application that exercises what the Cocoa stream has to
// show on the SiliconForest desktop: a titled window, a text label, a button, a text field,
// a main menu, keyboard and pointer input. Every observable step prints one line starting
// with COCOAHELLO to stderr (the guest console), which is what the automated check reads.
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static void say(const char *format, ...)
{
    char line[512];
    va_list args;
    int n;

    va_start(args, format);
    n = vsnprintf(line, sizeof(line) - 1, format, args);
    va_end(args);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = sizeof(line) - 2;
    line[n++] = '\n';
    write(2, line, n);
}

@interface HelloView : NSView {
    int _keys, _clicks;
    NSString *_typed;
    NSPoint _last;
}
@end

@implementation HelloView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return NO; }

- (void)drawRect:(NSRect)dirty
{
    NSRect b = [self bounds];
    NSDictionary *attrs = @{ NSFontAttributeName : [NSFont systemFontOfSize:13],
                             NSForegroundColorAttributeName : [NSColor blackColor] };

    [[NSColor colorWithCalibratedRed:0.93 green:0.95 blue:0.98 alpha:1] set];
    NSRectFill(b);
    // a solid 4 point border in a colour nothing else on the desktop has: the automated checks
    // find the view by it on the screen and measure pixels per point from its width
    [[NSColor colorWithCalibratedRed:0.2 green:0.45 blue:0.8 alpha:1] set];
    NSFrameRectWithWidth(b, 4);
    [[NSString stringWithFormat:@"keys %d  clicks %d", _keys, _clicks] drawAtPoint:NSMakePoint(8, b.size.height - 22)
                                                                    withAttributes:attrs];
    [[NSString stringWithFormat:@"typed: %@", _typed ?: @""] drawAtPoint:NSMakePoint(8, b.size.height - 42) withAttributes:attrs];
    if (_clicks > 0) {
        [[NSColor redColor] set];
        NSRectFill(NSMakeRect(_last.x - 3, _last.y - 3, 6, 6));
    }
}

- (void)keyDown:(NSEvent *)event
{
    NSString *chars = [event characters];

    _keys++;
    _typed = [[(_typed ?: @"") stringByAppendingString:chars ?: @""] retain];
    say("COCOAHELLO key code=%d chars=%s modifiers=0x%lx total=%d", (int)[event keyCode], [chars UTF8String] ?: "",
        (unsigned long)[event modifierFlags], _keys);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent *)event
{
    _last = [self convertPoint:[event locationInWindow] fromView:nil];
    _clicks++;
    say("COCOAHELLO mouse view=%.0f,%.0f window=%.0f,%.0f clicks=%d", _last.x, _last.y, [event locationInWindow].x,
        [event locationInWindow].y, _clicks);
    [[self window] makeFirstResponder:self];
    [self setNeedsDisplay:YES];
}

@end

@interface HelloDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *_window;
    NSTextField *_label;
    int _presses;
    BOOL _refusedClose;
}
@end

@implementation HelloDelegate

- (void)choiceMade:(id)sender
{
    say("COCOAHELLO popup selected=%s index=%d", [[sender titleOfSelectedItem] UTF8String], (int)[sender indexOfSelectedItem]);
}

- (void)buttonPressed:(id)sender
{
    _presses++;
    [_label setStringValue:[NSString stringWithFormat:@"Button pressed %d time%s", _presses, _presses == 1 ? "" : "s"]];
    say("COCOAHELLO button presses=%d", _presses);
}

- (void)menuHello:(id)sender
{
    [_label setStringValue:@"Menu item chosen"];
    say("COCOAHELLO menu item=%s", [[sender title] UTF8String]);
}

- (void)buildMenu
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"CocoaHello" action:NULL keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"CocoaHello"];
    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *item;

    item = [[NSMenuItem alloc] initWithTitle:@"Say Hello" action:@selector(menuHello:) keyEquivalent:@"h"];
    [item setTarget:self];
    [appMenu addItem:item];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit CocoaHello" action:@selector(terminate:) keyEquivalent:@"q"]];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    item = [[NSMenuItem alloc] initWithTitle:@"Greet" action:@selector(menuHello:) keyEquivalent:@"g"];
    [item setTarget:self];
    [fileMenu addItem:item];
    [fileMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"]];
    [fileItem setSubmenu:fileMenu];
    [mainMenu addItem:fileItem];
    [NSApp setMainMenu:mainMenu];
    say("COCOAHELLO menu-installed items=%d", (int)[mainMenu numberOfItems]);
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    NSRect screen = [[NSScreen mainScreen] frame];
    NSRect frame = NSMakeRect(80, screen.size.height - 80 - 300, 420, 300);
    NSButton *button;
    NSTextField *field;
    HelloView *view;
    NSView *content;

    say("COCOAHELLO launched screen=%.0fx%.0f scale=%.2f", screen.size.width, screen.size.height,
        (double)[[NSScreen mainScreen] backingScaleFactor]);
    [self buildMenu];
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask |
                                                    NSResizableWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"Hello from AppKit"];
    [_window setDelegate:self];
    content = [_window contentView];

    _label = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 250, 380, 24)];
    [_label setStringValue:@"Hello, SiliconForest. This is AppKit."];
    [_label setBezeled:NO];
    [_label setDrawsBackground:NO];
    [_label setEditable:NO];
    [_label setSelectable:NO];
    [content addSubview:_label];

    button = [[NSButton alloc] initWithFrame:NSMakeRect(20, 205, 140, 32)];
    [button setTitle:@"Press Me"];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setTarget:self];
    [button setAction:@selector(buttonPressed:)];
    [content addSubview:button];

    field = [[NSTextField alloc] initWithFrame:NSMakeRect(180, 208, 120, 24)];
    [field setStringValue:@"editable"];
    [content addSubview:field];

    // a pop-up button: its list is a popup window the compositor places (checks/popup.json)
    {
        NSPopUpButton *choice = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(310, 207, 95, 26) pullsDown:NO];

        [choice addItemWithTitle:@"Alpha"];
        [choice addItemWithTitle:@"Beta"];
        [choice addItemWithTitle:@"Gamma"];
        [choice setTarget:self];
        [choice setAction:@selector(choiceMade:)];
        [content addSubview:choice];
    }

    view = [[HelloView alloc] initWithFrame:NSMakeRect(20, 20, 380, 170)];
    [content addSubview:view];

    [_window makeFirstResponder:view];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    say("COCOAHELLO window-shown frame=%.0f,%.0f,%.0f,%.0f visible=%d key=%d", [_window frame].origin.x, [_window frame].origin.y,
        [_window frame].size.width, [_window frame].size.height, (int)[_window isVisible], (int)[_window isKeyWindow]);
}

// COCOAHELLO_REFUSE_CLOSE=1: the first request to close is refused, as a document with unsaved
// changes would; the second closes. Shows that a close button outside the application (the
// compositor's frame) asks the application instead of destroying its window.
- (BOOL)windowShouldClose:(id)sender
{
    if (getenv("COCOAHELLO_REFUSE_CLOSE") != NULL && !_refusedClose) {
        _refusedClose = YES;
        [_label setStringValue:@"Close refused once; ask again"];
        say("COCOAHELLO close-refused");
        return NO;
    }
    say("COCOAHELLO close-allowed");
    return YES;
}

- (void)windowDidChangeBackingProperties:(NSNotification *)note
{
    say("COCOAHELLO backing-scale %.3f", (double)[_window backingScaleFactor]);
}

- (void)windowDidBecomeKey:(NSNotification *)note { say("COCOAHELLO window-key"); }
- (void)windowDidResize:(NSNotification *)note
{
    say("COCOAHELLO window-resized %.0fx%.0f", [_window frame].size.width, [_window frame].size.height);
    // the zoomed state may reach the window just after the size it comes with: reported once things are quiet
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(reportZoom) object:nil];
    [self performSelector:@selector(reportZoom) withObject:nil afterDelay:1.5];
}
- (void)reportZoom
{
    say("COCOAHELLO zoom-state zoomed=%d size=%.0fx%.0f", (int)[_window isZoomed], [_window frame].size.width,
        [_window frame].size.height);
}
- (void)windowDidMiniaturize:(NSNotification *)note { say("COCOAHELLO window-miniaturized is=%d", (int)[_window isMiniaturized]); }
- (void)windowDidDeminiaturize:(NSNotification *)note { say("COCOAHELLO window-deminiaturized is=%d", (int)[_window isMiniaturized]); }
- (void)windowDidMove:(NSNotification *)note
{
    say("COCOAHELLO window-moved %.0f,%.0f", [_window frame].origin.x, [_window frame].origin.y);
}
- (void)windowWillClose:(NSNotification *)note
{
    say("COCOAHELLO window-closed");
    [NSApp terminate:self];
}
- (void)applicationWillTerminate:(NSNotification *)note { say("COCOAHELLO terminating"); }

@end

int main(int argc, const char *argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];
    HelloDelegate *delegate = [[HelloDelegate alloc] init];

    say("COCOAHELLO start pid=%d", (int)getpid());
    [app setDelegate:delegate];
    [app run];
    [pool release];
    return 0;
}
