/* SPDX-License-Identifier: MIT */
/* Own implementation from value/lifetime observations, not Apple source or
 * SDK headers. This class is a model item, not a touch bar or capture service. */
#import <Foundation/Foundation.h>

@interface AVTouchBarCaptureInputSourceItem : NSObject {
    NSString *_title;
    id _representedObject;
    BOOL _builtIn, _noAudioItem, _enabled;
}
- (id)initWithTitle:(NSString *)title builtIn:(BOOL)builtIn noAudioItem:(BOOL)noAudio enabled:(BOOL)enabled;
- (NSString *)title;
- (BOOL)isBuiltIn;
- (BOOL)isNoAudioItem;
- (BOOL)isEnabled;
- (id)representedObject;
@end

@implementation AVTouchBarCaptureInputSourceItem
- (id)initWithTitle:(NSString *)title {
    return [self initWithTitle:title builtIn:NO noAudioItem:NO enabled:YES];
}
- (id)initWithTitle:(NSString *)title builtIn:(BOOL)builtIn noAudioItem:(BOOL)noAudio enabled:(BOOL)enabled {
    if ((self=[super init])) {
        /* Native retains mutable title objects: do not silently copy them. */
        _title=[title retain];
        _builtIn=builtIn; _noAudioItem=noAudio; _enabled=enabled;
    }
    return self;
}
- (void)dealloc { [_title release];[_representedObject release];[super dealloc]; }
- (NSString *)title { return [[_title retain] autorelease]; }
- (BOOL)isBuiltIn { return _builtIn; }
- (BOOL)isNoAudioItem { return _noAudioItem; }
- (BOOL)isEnabled { return _enabled; }
- (id)representedObject {
    @synchronized(self) { return [[_representedObject retain] autorelease]; }
}
- (void)setRepresentedObject:(id)value {
    @synchronized(self) {
        [value retain];[_representedObject release];_representedObject=value;
    }
}
- (NSUInteger)hash { return [_title hash]; }
- (BOOL)isEqual:(id)object {
    if (object==self) return YES;
    if (![object isKindOfClass:[AVTouchBarCaptureInputSourceItem class]]) return NO;
    AVTouchBarCaptureInputSourceItem *other=object;
    if (_builtIn!=[other isBuiltIn] || _noAudioItem!=[other isNoAudioItem] || _enabled!=[other isEnabled]) return NO;
    NSString *title=[other title];
    if (_title!=title && ![_title isEqual:title]) return NO;
    id represented=[self representedObject],otherRepresented=[other representedObject];
    return represented==otherRepresented || [represented isEqual:otherRepresented];
}
@end
