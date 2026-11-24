/* SPDX-License-Identifier: MIT */
/* Own declarations for the models explicitly used in Finch source and tests. */

@interface INIntentResponse : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSUserActivity *userActivity;
@end
@interface INBooleanResolutionResult : INIntentResolutionResult
+ (instancetype)successWithResolvedValue:(BOOL)resolvedValue;
@end
@interface INSendMessageAttachment : NSObject <NSCopying, NSSecureCoding>
@property(copy) INFile *audioMessageFile;
+ (instancetype)attachmentWithAudioMessageFile:(INFile *)audioMessageFile;
@end
@interface INVoiceShortcut : NSObject <NSCopying, NSSecureCoding>
@property(copy) NSUUID *identifier;
@property(copy) INShortcut *shortcut;
@end
@interface INRelevantShortcut : NSObject <NSCopying, NSSecureCoding>
@property(copy) INShortcut *shortcut;
- (instancetype)initWithShortcut:(INShortcut *)shortcut;
@end
