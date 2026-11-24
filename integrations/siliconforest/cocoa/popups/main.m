// SPDX-License-Identifier: MIT
// CocoaPopups: every kind of popup AppKit shows, in one small window, for the automated checks
// (checks/popups-*.json): a pop-up button, a pull-down button, a combo box, a view with a context
// menu (submenus two deep, a disabled item, a checked item, key equivalents) and a button with a
// tool tip. Every observable event prints one line starting with POPT to stderr (the guest
// console). The window is 300 by 200 points so that it fits the test screen up to 300%.
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

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

// the view the checks find on the screen (a 4 point border in a colour nothing else has) and
// that carries the context menu
@interface MarkView : NSView {
    int _clicks;
}
@end

@implementation MarkView

- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty
{
    NSRect b = [self bounds];

    [[NSColor colorWithCalibratedRed:0.93 green:0.95 blue:0.98 alpha:1] set];
    NSRectFill(b);
    [[NSColor colorWithCalibratedRed:0.2 green:0.45 blue:0.8 alpha:1] set];
    NSFrameRectWithWidth(b, 4);
    [[NSString stringWithFormat:@"right click here   clicks %d", _clicks]
        drawAtPoint:NSMakePoint(10, b.size.height - 24)
     withAttributes:@{ NSFontAttributeName : [NSFont systemFontOfSize:13], NSForegroundColorAttributeName : [NSColor blackColor] }];
}

// a press that reaches the view: a press that only dismisses a menu must not
- (void)mouseDown:(NSEvent *)event
{
    _clicks++;
    say("POPT view-click clicks=%d", _clicks);
    [self setNeedsDisplay:YES];
}

@end

@interface PopupsDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *_window;
    NSPopUpButton *_popup, *_pulldown;
    NSComboBox *_combo;
    NSMenuItem *_checked;
    int _actions;
}
@end

@implementation PopupsDelegate

- (void)popupChanged:(id)sender
{
    say("POPT popup action=%d selected=%s index=%d", ++_actions, [[sender titleOfSelectedItem] UTF8String] ?: "(null)",
        (int)[sender indexOfSelectedItem]);
}

- (void)pulldownChosen:(id)sender
{
    say("POPT pulldown action=%d item=%s title=%s", ++_actions, [[sender title] UTF8String], [[_pulldown title] UTF8String]);
}

- (void)comboChanged:(id)sender
{
    say("POPT combo action=%d value=%s", ++_actions, [[sender stringValue] UTF8String]);
}

- (void)contextChosen:(id)sender
{
    if (sender == _checked)
        [_checked setState:[_checked state] == NSOnState ? NSOffState : NSOnState];
    say("POPT context action=%d item=%s", ++_actions, [[sender title] UTF8String]);
}

- (void)tracking:(NSNotification *)note
{
    say("POPT menu %s", [[note name] isEqualToString:NSMenuDidBeginTrackingNotification] ? "begin" : "end");
}

- (NSMenuItem *)item:(NSString *)title in:(NSMenu *)menu key:(NSString *)key
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(contextChosen:) keyEquivalent:key];

    [item setTarget:self];
    [menu addItem:item];
    return item;
}

- (NSMenu *)contextMenu
{
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Context"];
    NSMenu *more = [[NSMenu alloc] initWithTitle:@"More"];
    NSMenu *deeper = [[NSMenu alloc] initWithTitle:@"Deeper"];
    NSMenuItem *item;

    [menu setAutoenablesItems:NO];
    [more setAutoenablesItems:NO];
    [deeper setAutoenablesItems:NO];
    [self item:@"Cut" in:menu key:@"x"];
    [self item:@"Copy" in:menu key:@"c"];
    item = [self item:@"Paste" in:menu key:@"v"];
    [item setEnabled:NO];
    [menu addItem:[NSMenuItem separatorItem]];
    _checked = [self item:@"Wrap Lines" in:menu key:@""];
    [_checked setState:NSOnState];
    item = [[NSMenuItem alloc] initWithTitle:@"More" action:NULL keyEquivalent:@""];
    [item setSubmenu:more];
    [menu addItem:item];
    [self item:@"Select All" in:menu key:@"a"];

    [self item:@"Upper Case" in:more key:@""];
    [self item:@"Lower Case" in:more key:@""];
    item = [[NSMenuItem alloc] initWithTitle:@"Deeper" action:NULL keyEquivalent:@""];
    [item setSubmenu:deeper];
    [more addItem:item];
    [self item:@"Capitalize" in:more key:@""];

    [self item:@"First Leaf" in:deeper key:@""];
    [self item:@"Second Leaf" in:deeper key:@""];
    return menu;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    NSRect screen = [[NSScreen mainScreen] frame];
    NSRect frame = NSMakeRect(40, screen.size.height - 60 - 200, 300, 200);
    NSView *content;
    NSButton *button;
    MarkView *view;

    say("POPT launched screen=%.0fx%.0f scale=%.2f", screen.size.width, screen.size.height,
        (double)[[NSScreen mainScreen] backingScaleFactor]);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"Popups"];
    [_window setDelegate:self];
    content = [_window contentView];

    _popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(10, 165, 110, 26) pullsDown:NO];
    [_popup addItemsWithTitles:@[ @"Alpha", @"Beta", @"Gamma", @"Delta", @"Epsilon" ]];
    [[_popup itemAtIndex:3] setEnabled:NO];
    [[_popup menu] setAutoenablesItems:NO];
    [_popup setTarget:self];
    [_popup setAction:@selector(popupChanged:)];
    [content addSubview:_popup];

    _pulldown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(130, 165, 110, 26) pullsDown:YES];
    [_pulldown addItemsWithTitles:@[ @"Actions", @"Rename", @"Duplicate", @"Remove" ]];
    for (NSMenuItem *item in [[_pulldown menu] itemArray]) {
        [item setTarget:self];
        [item setAction:@selector(pulldownChosen:)];
    }
    [content addSubview:_pulldown];

    _combo = [[NSComboBox alloc] initWithFrame:NSMakeRect(10, 130, 120, 26)];
    [_combo addItemsWithObjectValues:@[ @"Red", @"Green", @"Blue", @"Violet" ]];
    [_combo setStringValue:@"Green"];
    [_combo setTarget:self];
    [_combo setAction:@selector(comboChanged:)];
    [content addSubview:_combo];

    button = [[NSButton alloc] initWithFrame:NSMakeRect(140, 127, 100, 32)];
    [button setTitle:@"Hover"];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setToolTip:@"A tool tip from AppKit"];
    [content addSubview:button];

    view = [[MarkView alloc] initWithFrame:NSMakeRect(10, 10, 280, 110)];
    [view setMenu:[self contextMenu]];
    [content addSubview:view];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(tracking:) name:NSMenuDidBeginTrackingNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(tracking:) name:NSMenuDidEndTrackingNotification object:nil];

    [_window makeFirstResponder:view];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    say("POPT window-shown frame=%.0f,%.0f,%.0f,%.0f", [_window frame].origin.x, [_window frame].origin.y,
        [_window frame].size.width, [_window frame].size.height);
}

- (void)windowWillClose:(NSNotification *)note
{
    say("POPT window-closed");
    [NSApp terminate:self];
}

@end

int main(int argc, const char *argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];
    PopupsDelegate *delegate = [[PopupsDelegate alloc] init];

    say("POPT start pid=%d", (int)getpid());
    [app setDelegate:delegate];
    [app run];
    [pool release];
    return 0;
}
