/* SPDX-License-Identifier: MIT */
/* UTType's interface, written for SiliconForest from the names applications use (the
   implementation is Finch's UTType.m). docs/roadmap/issues/SF-AA-activity-monitor.md */
#import <Foundation/Foundation.h>
#ifndef UT_SUBCLASSING_RESTRICTED
#define UT_SUBCLASSING_RESTRICTED __attribute__((objc_subclassing_restricted))
#endif
UT_SUBCLASSING_RESTRICTED
@interface UTType : NSObject <NSCopying, NSCoding>
+ (instancetype)typeWithIdentifier:(NSString *)identifier;
+ (instancetype)typeWithFilenameExtension:(NSString *)filenameExtension;
+ (instancetype)typeWithFilenameExtension:(NSString *)filenameExtension conformingToType:(UTType *)supertype;
+ (instancetype)typeWithMIMEType:(NSString *)mimeType;
+ (instancetype)typeWithMIMEType:(NSString *)mimeType conformingToType:(UTType *)supertype;
+ (instancetype)typeWithTag:(NSString *)tag tagClass:(NSString *)tagClass conformingToType:(UTType *)supertype;
+ (NSArray *)typesWithTag:(NSString *)tag tagClass:(NSString *)tagClass conformingToType:(UTType *)supertype;
+ (UTType *)exportedTypeWithIdentifier:(NSString *)identifier;
+ (UTType *)exportedTypeWithIdentifier:(NSString *)identifier conformingToType:(UTType *)parentType;
+ (UTType *)importedTypeWithIdentifier:(NSString *)identifier;
+ (UTType *)importedTypeWithIdentifier:(NSString *)identifier conformingToType:(UTType *)parentType;
+ (BOOL)supportsSecureCoding;
@property (readonly, copy) NSString *identifier;
@property (readonly, copy) NSString *preferredFilenameExtension;
@property (readonly, copy) NSString *preferredMIMEType;
@property (readonly, copy) NSString *localizedDescription;
@property (readonly, copy) NSNumber *version;
@property (readonly, copy) NSURL *referenceURL;
@property (readonly, getter=isDynamic) BOOL dynamic;
@property (readonly, getter=isDeclared) BOOL declared;
@property (readonly, getter=isPublicType) BOOL publicType;
@property (readonly) NSDictionary *tags;
@property (readonly, copy) NSSet *supertypes;
- (BOOL)conformsToType:(UTType *)type;
- (BOOL)isSupertypeOfType:(UTType *)type;
- (BOOL)isSubtypeOfType:(UTType *)type;
@end
