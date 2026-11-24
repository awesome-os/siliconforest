/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class SFCertificateView, SFCertificatePanel, SFCertificateTrustPanel, SFChooseIdentityPanel, SFChooseIdentityTableCellView, SFKeychainSavePanel, SFKeychainSettingsPanel, SFAuthorizationView, SFAuthorizationPluginView;
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

extern NSString *const SFAuthorizationPluginViewUserNameKey;
extern NSString *const SFAuthorizationPluginViewUserShortNameKey;

@interface SFCertificateView : NSView
- (instancetype)initWithFrame:(NSRect)frame;
- (void)dealloc;
- (void)setCertificate:(SecCertificateRef)certificate;
- (SecCertificateRef)certificate;
- (void)setPolicies:(id)policies;
- (NSArray *)policies;
- (void)setEditableTrust:(BOOL)editable;
- (BOOL)isEditable;
- (void)setDisplayTrust:(BOOL)display;
- (BOOL)isTrustDisplayed;
- (void)setDisplayDetails:(BOOL)display;
- (BOOL)detailsDisplayed;
- (void)setDetailsDisclosed:(BOOL)disclosed;
- (BOOL)detailsDisclosed;
- (void)setPoliciesDisclosed:(BOOL)disclosed;
- (BOOL)policiesDisclosed;
- (void)saveTrustSettings;
- (void)drawRect:(NSRect)dirtyRect;
@end

@interface SFCertificatePanel : NSPanel
- (instancetype)init;
+ (SFCertificatePanel *)sharedCertificatePanel;
- (NSInteger)runModalForCertificates:(NSArray *)certificates showGroup:(BOOL)showGroup;
- (NSInteger)runModalForTrust:(SecTrustRef)trust showGroup:(BOOL)showGroup;
- (void)beginSheetForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo trust:(SecTrustRef)trust showGroup:(BOOL)showGroup;
- (void)beginSheetForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo certificates:(NSArray *)certificates showGroup:(BOOL)showGroup;
- (void)setPolicies:(id)policies;
- (NSArray *)policies;
- (void)setDefaultButtonTitle:(NSString *)title;
- (void)setAlternateButtonTitle:(NSString *)title;
- (void)setShowsHelp:(BOOL)showsHelp;
- (BOOL)showsHelp;
- (void)setHelpAnchor:(NSString *)anchor;
- (NSString *)helpAnchor;
- (SFCertificateView *)certificateView;
- (void)_accept:(id)sender;
- (void)_cancel:(id)sender;
@end

@interface SFCertificateTrustPanel : SFCertificatePanel
+ (SFCertificateTrustPanel *)sharedCertificateTrustPanel;
- (NSInteger)runModalForTrust:(SecTrustRef)trust message:(NSString *)message;
- (void)_accept:(id)sender;
- (void)beginSheetForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo trust:(SecTrustRef)trust message:(NSString *)message;
- (void)setInformativeText:(NSString *)text;
- (NSString *)informativeText;
@end

@interface SFChooseIdentityPanel : NSPanel
- (instancetype)init;
+ (SFChooseIdentityPanel *)sharedChooseIdentityPanel;
- (NSInteger)runModalForIdentities:(NSArray *)identities message:(NSString *)message;
- (void)beginSheetForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo identities:(NSArray *)identities message:(NSString *)message;
- (SecIdentityRef)identity;
- (void)setPolicies:(id)policies;
- (NSArray *)policies;
- (void)setDefaultButtonTitle:(NSString *)title;
- (void)setAlternateButtonTitle:(NSString *)title;
- (void)setShowsHelp:(BOOL)showsHelp;
- (BOOL)showsHelp;
- (void)setHelpAnchor:(NSString *)anchor;
- (NSString *)helpAnchor;
- (void)setInformativeText:(NSString *)text;
- (NSString *)informativeText;
- (void)setDomain:(NSString *)domain;
- (NSString *)domain;
- (void)_choose:(id)sender;
- (void)_cancel:(id)sender;
@end

@interface SFChooseIdentityTableCellView : NSTableCellView
@property(retain) NSTextField *issuerTextField;

@end

@interface SFKeychainSavePanel : NSSavePanel
+ (SFKeychainSavePanel *)sharedKeychainSavePanel;
- (void)setPassword:(NSString *)password;
- (SecKeychainRef)keychain;
- (NSError *)error;
- (void)_ensurePasswordField;
- (OSStatus)_createKeychainAtURL:(NSURL *)url;
- (NSInteger)runModalForDirectory:(NSString *)path file:(NSString *)name;
- (void)beginSheetForDirectory:(NSString *)path file:(NSString *)name modalForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo;
@end

@interface SFKeychainSettingsPanel : NSPanel
- (instancetype)init;
+ (SFKeychainSettingsPanel *)sharedKeychainSettingsPanel;
- (NSInteger)runModalForSettings:(SecKeychainSettings *)settings keychain:(SecKeychainRef)keychain;
- (void)beginSheetForWindow:(NSWindow *)window modalDelegate:(id)delegate didEndSelector:(SEL)selector contextInfo:(void *)contextInfo settings:(SecKeychainSettings *)settings keychain:(SecKeychainRef)keychain;
- (void)_cancel:(id)sender;
@end

@interface SFAuthorizationView : NSView
- (instancetype)initWithFrame:(NSRect)frame;
- (void)dealloc;
- (void)setString:(AuthorizationString)string;
- (void)setAuthorizationRights:(const AuthorizationRights *)rights;
- (AuthorizationRights *)authorizationRights;
- (SFAuthorization *)authorization;
- (BOOL)updateStatus:(id)sender;
- (void)setAutoupdate:(BOOL)enabled;
- (void)setAutoupdate:(BOOL)enabled interval:(NSTimeInterval)interval;
- (SFAuthorizationViewState)authorizationState;
- (void)setEnabled:(BOOL)enabled;
- (BOOL)isEnabled;
- (void)setFlags:(AuthorizationFlags)flags;
- (void)setDelegate:(id)delegate;
- (id)delegate;
- (BOOL)authorize:(id)sender;
- (BOOL)deauthorize:(id)sender;
- (void)_toggle:(id)sender;
@end

@interface SFAuthorizationPluginView : NSObject
- (instancetype)initWithCallbacks:(const AuthorizationCallbacks *)callbacks andEngineRef:(AuthorizationEngineRef)engineRef;
- (AuthorizationEngineRef)engineRef;
- (const AuthorizationCallbacks *)callbacks;
- (void)buttonPressed:(SFButtonType)button;
- (NSError *)lastError;
- (void)didActivate;
- (void)willActivateWithUser:(NSDictionary *)user;
- (void)didDeactivate;
- (NSView *)viewForType:(SFViewType)type;
- (NSView *)firstKeyView;
- (NSResponder *)firstResponder;
- (NSView *)lastKeyView;
- (void)setEnabled:(BOOL)enabled;
- (void)displayView;
- (void)setButton:(SFButtonType)button enabled:(BOOL)enabled;
- (void)updateView;
@end
