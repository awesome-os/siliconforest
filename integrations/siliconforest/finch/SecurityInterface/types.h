/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <Security/Security.h>
#import <Security/SecTrustSettings.h>
#import <SecurityFoundation/SFAuthorization.h>
#ifndef CF_CLOSED_ENUM
#define CF_CLOSED_ENUM(...) CF_ENUM(__VA_ARGS__)
#endif
/* This is from published Apple OSS Security source, not an SDK header. */
#import <AuthorizationPlugin.h>
typedef unsigned int SFButtonType, SFViewType, SFAuthorizationViewState;
enum { SFButtonTypeCancel = 0, SFButtonTypeOK = 1, SFViewTypeIdentityAndCredentials = 0,
       SFViewTypeCredentials = 1, SFAuthorizationStartupState = 0,
       SFAuthorizationViewLockedState = 1, SFAuthorizationViewInProgressState = 2,
       SFAuthorizationViewUnlockedState = 3 };
/* Present in SiliconForest's AppKit patch; absent from PD's base headers. */
@interface NSTableCellView : NSView
@property(retain) NSTextField *textField;
@property(retain) NSImageView *imageView;
@end
/* Real Foundation dependency; no replacement class is emitted here. */
@interface NSCache : NSObject
- (id)objectForKey:(id)key;
- (void)setObject:(id)object forKey:(id)key;
- (void)removeObjectForKey:(id)key;
- (void)removeAllObjects;
@end
