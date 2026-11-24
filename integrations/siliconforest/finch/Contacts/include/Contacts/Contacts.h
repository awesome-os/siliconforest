/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CNLabeledValue, CNContact, CNPhoneNumber, CNPostalAddress, CNContactRelation, CNSocialProfile, CNInstantMessageAddress, CNGroup, CNContainer, CNContactProperty, CNContactVCardSerialization, CNFetchRequest, CNFetchResult, CNSaveRequest, CNChangeHistoryEvent, CNContactStore, CNContactsUserDefaults, CNMutableContact, CNMutablePostalAddress, CNMutableGroup, CNContactFormatter, CNPostalAddressFormatter, CNContactFetchRequest, CNChangeHistoryFetchRequest, CNChangeHistoryDropEverythingEvent, CNChangeHistoryAddContactEvent, CNChangeHistoryUpdateContactEvent, CNChangeHistoryDeleteContactEvent, CNChangeHistoryAddGroupEvent, CNChangeHistoryUpdateGroupEvent, CNChangeHistoryDeleteGroupEvent, CNChangeHistoryAddMemberToGroupEvent, CNChangeHistoryRemoveMemberFromGroupEvent, CNChangeHistoryAddSubgroupToGroupEvent, CNChangeHistoryRemoveSubgroupFromGroupEvent;
/* SPDX-License-Identifier: MIT */
/* Own declarations for the values and methods used by Finch; no SDK input. */
typedef NSInteger CNContactType, CNContainerType, CNContactSortOrder, CNEntityType;
typedef NSInteger CNContactFormatterStyle, CNContactDisplayNameOrder, CNPostalAddressFormatterStyle;
typedef NSInteger CNAuthorizationStatus, CNErrorCode;
enum { CNContactTypePerson = 0, CNContactTypeOrganization = 1, CNContainerTypeLocal = 1,
       CNContactSortOrderNone = 0, CNContactSortOrderUserDefault = 1,
       CNContactSortOrderGivenName = 2, CNContactSortOrderFamilyName = 3,
       CNContactFormatterStyleFullName = 0, CNContactFormatterStylePhoneticFullName = 1,
       CNContactDisplayNameOrderUserDefault = 0, CNContactDisplayNameOrderGivenNameFirst = 1,
       CNContactDisplayNameOrderFamilyNameFirst = 2, CNPostalAddressFormatterStyleMailingAddress = 0,
       CNAuthorizationStatusAuthorized = 3, CNEntityTypeContacts = 0,
       CNErrorCodeDataAccessError = 2, CNErrorCodeRecordDoesNotExist = 200,
       CNErrorCodeInsertedRecordAlreadyExists = 201, CNErrorCodeContainmentCycle = 202,
       CNErrorCodeParentRecordDoesNotExist = 204, CNErrorCodePredicateInvalid = 400 };
@protocol CNKeyDescriptor <NSObject, NSCopying, NSSecureCoding>
@end
@interface NSString (SFFinchContactKeyDescriptor) <CNKeyDescriptor>
@end
@protocol CNChangeHistoryEventVisitor <NSObject>
- (void)visitDropEverythingEvent:(CNChangeHistoryDropEverythingEvent *)event;
- (void)visitAddContactEvent:(CNChangeHistoryAddContactEvent *)event;
- (void)visitUpdateContactEvent:(CNChangeHistoryUpdateContactEvent *)event;
- (void)visitDeleteContactEvent:(CNChangeHistoryDeleteContactEvent *)event;
@optional
- (void)visitAddGroupEvent:(CNChangeHistoryAddGroupEvent *)event;
- (void)visitUpdateGroupEvent:(CNChangeHistoryUpdateGroupEvent *)event;
- (void)visitDeleteGroupEvent:(CNChangeHistoryDeleteGroupEvent *)event;
- (void)visitAddMemberToGroupEvent:(CNChangeHistoryAddMemberToGroupEvent *)event;
- (void)visitRemoveMemberFromGroupEvent:(CNChangeHistoryRemoveMemberFromGroupEvent *)event;
- (void)visitAddSubgroupToGroupEvent:(CNChangeHistoryAddSubgroupToGroupEvent *)event;
- (void)visitRemoveSubgroupFromGroupEvent:(CNChangeHistoryRemoveSubgroupFromGroupEvent *)event;
@end
@interface NSCoder (SFFinchContactsDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
- (id)decodeObjectOfClasses:(NSSet *)classes forKey:(NSString *)key;
@end
@interface NSKeyedArchiver (SFFinchContactsDeclarations)
+ (NSData *)archivedDataWithRootObject:(id)object requiringSecureCoding:(BOOL)secure error:(NSError **)error;
@end
@interface NSKeyedUnarchiver (SFFinchContactsDeclarations)
+ (id)unarchivedObjectOfClasses:(NSSet *)classes fromData:(NSData *)data error:(NSError **)error;
@end
@interface NSDateComponents (SFFinchContactsDeclarations)
@property NSInteger year, month, day, era;
@property(copy) NSCalendar *calendar;
@end
#ifndef NSDateComponentUndefined
#define NSDateComponentUndefined NSIntegerMax
#endif
@interface NSScanner (SFFinchContactsDeclarations)
@property(readonly, getter=isAtEnd) BOOL atEnd;
@end

@class FinchContactsDB;
#ifndef NS_NOESCAPE
#define NS_NOESCAPE __attribute__((noescape))
#endif
@interface NSString (SFFinchContactsComparison)
- (NSComparisonResult)localizedStandardCompare:(NSString *)other;
@end
@interface NSSet (SFFinchContactsSet)
- (NSSet *)setByAddingObject:(id)object;
@end

enum { NSDiacriticInsensitiveSearch = 1UL << 7 };
/* The archive format uses this literal key; PD does not export the alias. */
#define NSKeyedArchiveRootObjectKey @"root"
@interface NSKeyedUnarchiver (SFFinchContactsSecureCoding)
@property BOOL requiresSecureCoding;
@end
/* arm64 cross target is little endian. */
static inline unsigned long long NSSwapHostLongLongToLittle(unsigned long long x) { return x; }
static inline unsigned long long NSSwapLittleLongLongToHost(unsigned long long x) { return x; }
@interface NSCalendar (SFFinchContactsCalendar)
+ (instancetype)calendarWithIdentifier:(NSString *)identifier;
@property(readonly, copy) NSString *calendarIdentifier;
@end
@interface NSString (SFFinchContactsLines)
- (void)enumerateLinesUsingBlock:(void (^)(NSString *line, BOOL *stop))block;
@end

extern NSString *const CNAccountExternalIdentifierStringKey;
extern NSString *const CNAccountIdentifierKey;
extern NSString *const CNAccountPropertyCardDAVLocalDataMigrationHasTakenPlace;
extern NSString *const CNAccountPropertyCardDAVServerSyncHasTakenPlace;
extern NSString *const CNActionBundleIdentifierCalendar;
extern NSString *const CNActionBundleIdentifierContacts;
extern NSString *const CNActionBundleIdentifierFaceTime;
extern NSString *const CNActionBundleIdentifierFindMyFriends;
extern NSString *const CNActionBundleIdentifierMail;
extern NSString *const CNActionBundleIdentifierMaps;
extern NSString *const CNActionBundleIdentifierMessages;
extern NSString *const CNActionBundleIdentifierMessagesCatalyst;
extern NSString *const CNActionBundleIdentifierPeople;
extern NSString *const CNActionBundleIdentifierPhone;
extern NSString *const CNActionBundleIdentifierWallet;
extern NSString *const CNActionTypeAudioCall;
extern NSString *const CNActionTypeDate;
extern NSString *const CNActionTypeDirections;
extern NSString *const CNActionTypeExpanse;
extern NSString *const CNActionTypeMail;
extern NSString *const CNActionTypeMessage;
extern NSString *const CNActionTypePay;
extern NSString *const CNActionTypeSocialProfile;
extern NSString *const CNActionTypeTTYCall;
extern NSString *const CNActionTypeTTYRelayCall;
extern NSString *const CNActionTypeVideoCall;
extern NSString *const CNActivityAlertIgnoreMuteKey;
extern NSString *const CNActivityAlertSoundKey;
extern NSString *const CNActivityAlertVibrationKey;
extern NSString *const CNApplicationProxyServiceName;
extern NSString *const CNContactAccountIdentifierKey;
extern NSString *const CNContactAddressingGrammarEncryptedKey;
extern NSString *const CNContactAddressingGrammarKey;
extern NSString *const CNContactAvatarRecipeDataKey;
extern NSString *const CNContactBirthdayKey;
extern NSString *const CNContactBirthdaysKey;
extern NSString *const CNContactCalendarURIsKey;
extern NSString *const CNContactCallAlertKey;
extern NSString *const CNContactCreationDateKey;
extern NSString *const CNContactCropRectHashKey;
extern NSString *const CNContactCropRectIDKey;
extern NSString *const CNContactCropRectKey;
extern NSString *const CNContactDatesKey;
extern NSString *const CNContactDepartmentNameKey;
extern NSString *const CNContactDeviceNameKey;
extern NSString *const CNContactDisplayNameOrderKey;
extern NSString *const CNContactDowntimeWhitelistKey;
extern NSString *const CNContactEmailAddressesKey;
extern NSString *const CNContactExpiryDateKey;
extern NSString *const CNContactExplicitDisplayNameKey;
extern NSString *const CNContactExternalDetailsEntityName;
extern NSString *const CNContactExternalIdentifierKey;
extern NSString *const CNContactExternalImageURIKey;
extern NSString *const CNContactExternalModificationTagKey;
extern NSString *const CNContactExternalRepresentationKey;
extern NSString *const CNContactExternalUUIDKey;
extern NSString *const CNContactFamilyNameKey;
extern NSString *const CNContactFetchRequestAPIMisuseExceptionName;
extern NSString *const CNContactFullscreenImageDataKey;
extern NSString *const CNContactGivenNameKey;
extern NSString *const CNContactIOSLegacyIdentifierKey;
extern NSString *const CNContactIdentifierKey;
extern NSString *const CNContactIdentifiersPboardType;
extern NSString *const CNContactImageAttributeNameContactIdentifier;
extern NSString *const CNContactImageAttributeNameDeletionDate;
extern NSString *const CNContactImageAttributeNameIdentifier;
extern NSString *const CNContactImageAttributeNameIsCurrent;
extern NSString *const CNContactImageAttributeNameLastUsedDate;
extern NSString *const CNContactImageDataAvailableKey;
extern NSString *const CNContactImageDataKey;
extern NSString *const CNContactImageEntityName;
extern NSString *const CNContactImageHashKey;
extern NSString *const CNContactImageRelationshipNamePairedPoster;
extern NSString *const CNContactImageSyncFailedTimeKey;
extern NSString *const CNContactImageTypeAvatarValue;
extern NSString *const CNContactImageTypeEmojiValue;
extern NSString *const CNContactImageTypeKey;
extern NSString *const CNContactImageTypeMonogramValue;
extern NSString *const CNContactImageTypePhotoValue;
extern NSString *const CNContactInstantMessageAddressesKey;
extern NSString *const CNContactInternalPersistedUTI;
extern NSString *const CNContactInternalUTI;
extern NSString *const CNContactIsSharingPairedKey;
extern NSString *const CNContactJobTitleKey;
extern NSString *const CNContactLikenessSourceApplePersona;
extern NSString *const CNContactLikenessSourceImage;
extern NSString *const CNContactLinkIdentifierKey;
extern NSString *const CNContactMapsDataKey;
extern NSString *const CNContactMemojiMetadataKey;
extern NSString *const CNContactMiddleNameKey;
extern NSString *const CNContactModificationDateKey;
extern NSString *const CNContactNameEmphasisAttribute;
extern NSString *const CNContactNameEmphasisFontAttribute;
extern NSString *const CNContactNamePrefixKey;
extern NSString *const CNContactNameSuffixKey;
extern NSString *const CNContactNicknameKey;
extern NSString *const CNContactNonGregorianBirthdayKey;
extern NSString *const CNContactNoteKey;
extern NSString *const CNContactOrganizationNameKey;
extern NSString *const CNContactPhoneNumbersKey;
extern NSString *const CNContactPhonemeDataKey;
extern NSString *const CNContactPhoneticCompanyNameKey;
extern NSString *const CNContactPhoneticFamilyNameKey;
extern NSString *const CNContactPhoneticGivenNameKey;
extern NSString *const CNContactPhoneticMiddleNameKey;
extern NSString *const CNContactPhoneticOrganizationNameKey;
extern NSString *const CNContactPostalAddressesKey;
extern NSString *const CNContactPosterAttributeNameContactIdentifier;
extern NSString *const CNContactPosterAttributeNameContentIsSensitive;
extern NSString *const CNContactPosterAttributeNameDeletionDate;
extern NSString *const CNContactPosterAttributeNameIdentifier;
extern NSString *const CNContactPosterAttributeNameIgnoredForRevert;
extern NSString *const CNContactPosterAttributeNameIsCurrent;
extern NSString *const CNContactPosterAttributeNameLastUsedDate;
extern NSString *const CNContactPosterDataMachServiceName;
extern NSString *const CNContactPosterDataStoreDidChangeNotification;
extern NSString *const CNContactPosterEntityName;
extern NSString *const CNContactPosterRelationshipNamePairedImage;
extern NSString *const CNContactPreferredApplePersonaIdentifierKey;
extern NSString *const CNContactPreferredForImageKey;
extern NSString *const CNContactPreferredForNameKey;
extern NSString *const CNContactPreferredLikenessSourceKey;
extern NSString *const CNContactPreviousFamilyNameKey;
extern NSString *const CNContactPropertyAttribute;
extern NSString *const CNContactPropertyNotFetchedExceptionName;
extern NSString *const CNContactProviderSupportSynchronizationReasonAppContainerHasChanges;
extern NSString *const CNContactProviderSupportSynchronizationReasonAppRequested;
extern NSString *const CNContactProviderSupportSynchronizationReasonExtensionWasEnabled;
extern NSString *const CNContactProviderSupportSynchronizationReasonScheduledEvent;
extern NSString *const CNContactRelatedNamesKey;
extern NSString *const CNContactRelationsKey;
extern NSString *const CNContactSensitiveContentConfigurationKey;
extern NSString *const CNContactSharedPhotoDisplayPreferenceKey;
extern NSString *const CNContactSocialProfilesKey;
extern NSString *const CNContactSortingFamilyNameKey;
extern NSString *const CNContactSortingGivenNameKey;
extern NSString *const CNContactStoreDidChangeNotification;
extern NSString *const CNContactStoreLimitedAccessDidChangeNotification;
extern NSString *const CNContactStoreMeContactDidChangeNotification;
extern NSString *const CNContactStoreSettingsDefaultAppsDidChangeNotification;
extern NSString *const CNContactTextAlertKey;
extern NSString *const CNContactThumbnailImageDataKey;
extern NSString *const CNContactTypeKey;
extern NSString *const CNContactUrlAddressesKey;
extern NSString *const CNContactWallpaperKey;
extern NSString *const CNContactWallpaperMetadataKey;
extern NSString *const CNContactWallpaperSyncFailedTimeKey;
extern NSString *const CNContactWallpaperURIKey;
extern NSString *const CNContactWatchWallpaperImageDataKey;
extern NSString *const CNContactisUsingSharedPhotoKey;
extern NSString *const CNContactsSupportServiceName;
extern NSString *const CNContainerAccountIdentifierKey;
extern NSString *const CNContainerConstraintsPathKey;
extern NSString *const CNContainerEnabledKey;
extern NSString *const CNContainerExternalIdentifierKey;
extern NSString *const CNContainerExternalModificationTagKey;
extern NSString *const CNContainerExternalSyncDataKey;
extern NSString *const CNContainerExternalSyncTagKey;
extern NSString *const CNContainerGuardianRestrictedKey;
extern NSString *const CNContainerGuardianStateDirtyKey;
extern NSString *const CNContainerIOSLegacyIdentifierKey;
extern NSString *const CNContainerIdentifierKey;
extern NSString *const CNContainerLastSyncDateKey;
extern NSString *const CNContainerMeIdentifierKey;
extern NSString *const CNContainerNameKey;
extern NSString *const CNContainerPermissionsKey;
extern NSString *const CNContainerProviderIdentifierKey;
extern NSString *const CNContainerProviderMetadataKey;
extern NSString *const CNContainerRestrictionsKey;
extern NSString *const CNContainerTypeKey;
extern NSString *const CNDataMapperContactProviderServiceName;
extern NSString *const CNDataMapperServiceName;
extern NSString *const CNDowntimeWhitelistDidChangeNotification;
extern NSString *const CNErrorDomain;
extern NSString *const CNErrorUserInfoAffectedRecordIdentifiersKey;
extern NSString *const CNErrorUserInfoAffectedRecordsKey;
extern NSString *const CNErrorUserInfoKeyPathsKey;
extern NSString *const CNErrorUserInfoValidationErrorsKey;
extern NSString *const CNFavoritesChangedExternallyNotification;
extern NSString *const CNFavoritesChangedNotification;
extern NSString *const CNGroupCreationDateKey;
extern NSString *const CNGroupExternalIdentifierKey;
extern NSString *const CNGroupExternalModificationTagKey;
extern NSString *const CNGroupExternalRepresentationKey;
extern NSString *const CNGroupExternalURIKey;
extern NSString *const CNGroupExternalUUIDKey;
extern NSString *const CNGroupIOSLegacyIdentifierKey;
extern NSString *const CNGroupIdentifierKey;
extern NSString *const CNGroupModificationDateKey;
extern NSString *const CNGroupNameKey;
extern NSString *const CNHomeLabel;
extern NSString *const CNHomePageLabel;
extern NSString *const CNInstantMessageAddressBundleIdentifiersKey;
extern NSString *const CNInstantMessageAddressServiceKey;
extern NSString *const CNInstantMessageAddressTeamIdentifierKey;
extern NSString *const CNInstantMessageAddressUserIdentifierKey;
extern NSString *const CNInstantMessageAddressUsernameKey;
extern NSString *const CNInstantMessageServiceAIM;
extern NSString *const CNInstantMessageServiceFacebook;
extern NSString *const CNInstantMessageServiceGaduGadu;
extern NSString *const CNInstantMessageServiceGoogleTalk;
extern NSString *const CNInstantMessageServiceICQ;
extern NSString *const CNInstantMessageServiceJabber;
extern NSString *const CNInstantMessageServiceMSN;
extern NSString *const CNInstantMessageServiceQQ;
extern NSString *const CNInstantMessageServiceSkype;
extern NSString *const CNInstantMessageServiceYahoo;
extern NSString *const CNLDAPAttributeAppleAIMPreferred;
extern NSString *const CNLDAPAttributeAppleManager;
extern NSString *const CNLDAPAttributeApplePhotoOfficialJPEG;
extern NSString *const CNLDAPAttributeApplePhotoOfficialThumbnailJPEG;
extern NSString *const CNLDAPAttributeApplePhotoPreferredJPEG;
extern NSString *const CNLDAPAttributeC;
extern NSString *const CNLDAPAttributeCNHomeAddresses;
extern NSString *const CNLDAPAttributeCNPhotos;
extern NSString *const CNLDAPAttributeCNPostalAddresses;
extern NSString *const CNLDAPAttributeCNThumbnailPhotos;
extern NSString *const CNLDAPAttributeCNWorkAddresses;
extern NSString *const CNLDAPAttributeCO;
extern NSString *const CNLDAPAttributeCellPhone;
extern NSString *const CNLDAPAttributeCommonName;
extern NSString *const CNLDAPAttributeCompany;
extern NSString *const CNLDAPAttributeCountryName;
extern NSString *const CNLDAPAttributeDepartment;
extern NSString *const CNLDAPAttributeDestinationIndicator;
extern NSString *const CNLDAPAttributeDistinguishedName;
extern NSString *const CNLDAPAttributeEmailAddresses;
extern NSString *const CNLDAPAttributeFacsimileTelephoneNumber;
extern NSString *const CNLDAPAttributeFax;
extern NSString *const CNLDAPAttributeGivenName;
extern NSString *const CNLDAPAttributeHomePhone;
extern NSString *const CNLDAPAttributeHomePostalAddress;
extern NSString *const CNLDAPAttributeHomeURL;
extern NSString *const CNLDAPAttributeJPEGPhoto;
extern NSString *const CNLDAPAttributeL;
extern NSString *const CNLDAPAttributeLocality;
extern NSString *const CNLDAPAttributeMobile;
extern NSString *const CNLDAPAttributeMozHomeCountryName;
extern NSString *const CNLDAPAttributeMozHomeLocalityName;
extern NSString *const CNLDAPAttributeMozHomePostalCode;
extern NSString *const CNLDAPAttributeMozHomeState;
extern NSString *const CNLDAPAttributeOrganization;
extern NSString *const CNLDAPAttributeOrganizationUnit;
extern NSString *const CNLDAPAttributePager;
extern NSString *const CNLDAPAttributePagerPhone;
extern NSString *const CNLDAPAttributePersonalTitle;
extern NSString *const CNLDAPAttributePostOfficeBox;
extern NSString *const CNLDAPAttributePostalAddress;
extern NSString *const CNLDAPAttributePostalCode;
extern NSString *const CNLDAPAttributeState;
extern NSString *const CNLDAPAttributeStreet;
extern NSString *const CNLDAPAttributeSurname;
extern NSString *const CNLDAPAttributeTelephoneNumber;
extern NSString *const CNLDAPAttributeTitle;
extern NSString *const CNLDAPAttributeWorkURL;
extern NSString *const CNLDAPErrorDomain;
extern NSString *const CNLabelContactRelationAssistant;
extern NSString *const CNLabelContactRelationAunt;
extern NSString *const CNLabelContactRelationAuntFathersBrothersWife;
extern NSString *const CNLabelContactRelationAuntFathersElderBrothersWife;
extern NSString *const CNLabelContactRelationAuntFathersElderSister;
extern NSString *const CNLabelContactRelationAuntFathersSister;
extern NSString *const CNLabelContactRelationAuntFathersYoungerBrothersWife;
extern NSString *const CNLabelContactRelationAuntFathersYoungerSister;
extern NSString *const CNLabelContactRelationAuntMothersBrothersWife;
extern NSString *const CNLabelContactRelationAuntMothersElderSister;
extern NSString *const CNLabelContactRelationAuntMothersSister;
extern NSString *const CNLabelContactRelationAuntMothersYoungerSister;
extern NSString *const CNLabelContactRelationAuntParentsElderSister;
extern NSString *const CNLabelContactRelationAuntParentsSister;
extern NSString *const CNLabelContactRelationAuntParentsYoungerSister;
extern NSString *const CNLabelContactRelationBoyfriend;
extern NSString *const CNLabelContactRelationBrother;
extern NSString *const CNLabelContactRelationBrotherInLaw;
extern NSString *const CNLabelContactRelationBrotherInLawElderSistersHusband;
extern NSString *const CNLabelContactRelationBrotherInLawHusbandsBrother;
extern NSString *const CNLabelContactRelationBrotherInLawHusbandsSistersHusband;
extern NSString *const CNLabelContactRelationBrotherInLawSistersHusband;
extern NSString *const CNLabelContactRelationBrotherInLawSpousesBrother;
extern NSString *const CNLabelContactRelationBrotherInLawWifesBrother;
extern NSString *const CNLabelContactRelationBrotherInLawWifesSistersHusband;
extern NSString *const CNLabelContactRelationBrotherInLawYoungerSistersHusband;
extern NSString *const CNLabelContactRelationChild;
extern NSString *const CNLabelContactRelationChildInLaw;
extern NSString *const CNLabelContactRelationCoBrotherInLaw;
extern NSString *const CNLabelContactRelationCoFatherInLaw;
extern NSString *const CNLabelContactRelationCoMotherInLaw;
extern NSString *const CNLabelContactRelationCoParentInLaw;
extern NSString *const CNLabelContactRelationCoSiblingInLaw;
extern NSString *const CNLabelContactRelationCoSisterInLaw;
extern NSString *const CNLabelContactRelationColleague;
extern NSString *const CNLabelContactRelationCousin;
extern NSString *const CNLabelContactRelationCousinFathersBrothersDaughter;
extern NSString *const CNLabelContactRelationCousinFathersBrothersSon;
extern NSString *const CNLabelContactRelationCousinFathersSistersDaughter;
extern NSString *const CNLabelContactRelationCousinFathersSistersSon;
extern NSString *const CNLabelContactRelationCousinGrandparentsSiblingsChild;
extern NSString *const CNLabelContactRelationCousinGrandparentsSiblingsDaughter;
extern NSString *const CNLabelContactRelationCousinGrandparentsSiblingsSon;
extern NSString *const CNLabelContactRelationCousinMothersBrothersDaughter;
extern NSString *const CNLabelContactRelationCousinMothersBrothersSon;
extern NSString *const CNLabelContactRelationCousinMothersSistersDaughter;
extern NSString *const CNLabelContactRelationCousinMothersSistersSon;
extern NSString *const CNLabelContactRelationCousinOrSiblingsChild;
extern NSString *const CNLabelContactRelationCousinParentsSiblingsChild;
extern NSString *const CNLabelContactRelationCousinParentsSiblingsDaughter;
extern NSString *const CNLabelContactRelationCousinParentsSiblingsSon;
extern NSString *const CNLabelContactRelationDaughter;
extern NSString *const CNLabelContactRelationDaughterInLaw;
extern NSString *const CNLabelContactRelationDaughterInLawOrSisterInLaw;
extern NSString *const CNLabelContactRelationDaughterInLawOrStepdaughter;
extern NSString *const CNLabelContactRelationElderBrother;
extern NSString *const CNLabelContactRelationElderBrotherInLaw;
extern NSString *const CNLabelContactRelationElderCousin;
extern NSString *const CNLabelContactRelationElderCousinFathersBrothersDaughter;
extern NSString *const CNLabelContactRelationElderCousinFathersBrothersSon;
extern NSString *const CNLabelContactRelationElderCousinFathersSistersDaughter;
extern NSString *const CNLabelContactRelationElderCousinFathersSistersSon;
extern NSString *const CNLabelContactRelationElderCousinMothersBrothersDaughter;
extern NSString *const CNLabelContactRelationElderCousinMothersBrothersSon;
extern NSString *const CNLabelContactRelationElderCousinMothersSiblingsDaughterOrFathersSistersDaughter;
extern NSString *const CNLabelContactRelationElderCousinMothersSiblingsSonOrFathersSistersSon;
extern NSString *const CNLabelContactRelationElderCousinMothersSistersDaughter;
extern NSString *const CNLabelContactRelationElderCousinMothersSistersSon;
extern NSString *const CNLabelContactRelationElderCousinParentsSiblingsDaughter;
extern NSString *const CNLabelContactRelationElderCousinParentsSiblingsSon;
extern NSString *const CNLabelContactRelationElderSibling;
extern NSString *const CNLabelContactRelationElderSiblingInLaw;
extern NSString *const CNLabelContactRelationElderSister;
extern NSString *const CNLabelContactRelationElderSisterInLaw;
extern NSString *const CNLabelContactRelationEldestBrother;
extern NSString *const CNLabelContactRelationEldestSister;
extern NSString *const CNLabelContactRelationFather;
extern NSString *const CNLabelContactRelationFatherInLaw;
extern NSString *const CNLabelContactRelationFatherInLawHusbandsFather;
extern NSString *const CNLabelContactRelationFatherInLawOrStepfather;
extern NSString *const CNLabelContactRelationFatherInLawWifesFather;
extern NSString *const CNLabelContactRelationFemaleCousin;
extern NSString *const CNLabelContactRelationFemaleFriend;
extern NSString *const CNLabelContactRelationFemalePartner;
extern NSString *const CNLabelContactRelationFriend;
extern NSString *const CNLabelContactRelationGirlfriend;
extern NSString *const CNLabelContactRelationGirlfriendOrBoyfriend;
extern NSString *const CNLabelContactRelationGrandaunt;
extern NSString *const CNLabelContactRelationGrandchild;
extern NSString *const CNLabelContactRelationGrandchildOrSiblingsChild;
extern NSString *const CNLabelContactRelationGranddaughter;
extern NSString *const CNLabelContactRelationGranddaughterDaughtersDaughter;
extern NSString *const CNLabelContactRelationGranddaughterOrNiece;
extern NSString *const CNLabelContactRelationGranddaughterSonsDaughter;
extern NSString *const CNLabelContactRelationGrandfather;
extern NSString *const CNLabelContactRelationGrandfatherFathersFather;
extern NSString *const CNLabelContactRelationGrandfatherMothersFather;
extern NSString *const CNLabelContactRelationGrandmother;
extern NSString *const CNLabelContactRelationGrandmotherFathersMother;
extern NSString *const CNLabelContactRelationGrandmotherMothersMother;
extern NSString *const CNLabelContactRelationGrandnephew;
extern NSString *const CNLabelContactRelationGrandnephewBrothersGrandson;
extern NSString *const CNLabelContactRelationGrandnephewSistersGrandson;
extern NSString *const CNLabelContactRelationGrandniece;
extern NSString *const CNLabelContactRelationGrandnieceBrothersGranddaughter;
extern NSString *const CNLabelContactRelationGrandnieceSistersGranddaughter;
extern NSString *const CNLabelContactRelationGrandparent;
extern NSString *const CNLabelContactRelationGrandson;
extern NSString *const CNLabelContactRelationGrandsonDaughtersSon;
extern NSString *const CNLabelContactRelationGrandsonOrNephew;
extern NSString *const CNLabelContactRelationGrandsonSonsSon;
extern NSString *const CNLabelContactRelationGranduncle;
extern NSString *const CNLabelContactRelationGreatGrandchild;
extern NSString *const CNLabelContactRelationGreatGrandchildOrSiblingsGrandchild;
extern NSString *const CNLabelContactRelationGreatGranddaughter;
extern NSString *const CNLabelContactRelationGreatGrandfather;
extern NSString *const CNLabelContactRelationGreatGrandmother;
extern NSString *const CNLabelContactRelationGreatGrandparent;
extern NSString *const CNLabelContactRelationGreatGrandson;
extern NSString *const CNLabelContactRelationHusband;
extern NSString *const CNLabelContactRelationMaleCousin;
extern NSString *const CNLabelContactRelationMaleFriend;
extern NSString *const CNLabelContactRelationMalePartner;
extern NSString *const CNLabelContactRelationManager;
extern NSString *const CNLabelContactRelationMother;
extern NSString *const CNLabelContactRelationMotherInLaw;
extern NSString *const CNLabelContactRelationMotherInLawHusbandsMother;
extern NSString *const CNLabelContactRelationMotherInLawOrStepmother;
extern NSString *const CNLabelContactRelationMotherInLawWifesMother;
extern NSString *const CNLabelContactRelationNephew;
extern NSString *const CNLabelContactRelationNephewBrothersSon;
extern NSString *const CNLabelContactRelationNephewBrothersSonOrHusbandsSiblingsSon;
extern NSString *const CNLabelContactRelationNephewOrCousin;
extern NSString *const CNLabelContactRelationNephewSistersSon;
extern NSString *const CNLabelContactRelationNephewSistersSonOrWifesSiblingsSon;
extern NSString *const CNLabelContactRelationNiece;
extern NSString *const CNLabelContactRelationNieceBrothersDaughter;
extern NSString *const CNLabelContactRelationNieceBrothersDaughterOrHusbandsSiblingsDaughter;
extern NSString *const CNLabelContactRelationNieceOrCousin;
extern NSString *const CNLabelContactRelationNieceSistersDaughter;
extern NSString *const CNLabelContactRelationNieceSistersDaughterOrWifesSiblingsDaughter;
extern NSString *const CNLabelContactRelationParent;
extern NSString *const CNLabelContactRelationParentInLaw;
extern NSString *const CNLabelContactRelationParentsElderSibling;
extern NSString *const CNLabelContactRelationParentsSibling;
extern NSString *const CNLabelContactRelationParentsSiblingFathersElderSibling;
extern NSString *const CNLabelContactRelationParentsSiblingFathersSibling;
extern NSString *const CNLabelContactRelationParentsSiblingFathersYoungerSibling;
extern NSString *const CNLabelContactRelationParentsSiblingMothersElderSibling;
extern NSString *const CNLabelContactRelationParentsSiblingMothersSibling;
extern NSString *const CNLabelContactRelationParentsSiblingMothersYoungerSibling;
extern NSString *const CNLabelContactRelationParentsYoungerSibling;
extern NSString *const CNLabelContactRelationPartner;
extern NSString *const CNLabelContactRelationSibling;
extern NSString *const CNLabelContactRelationSiblingInLaw;
extern NSString *const CNLabelContactRelationSiblingsChild;
extern NSString *const CNLabelContactRelationSister;
extern NSString *const CNLabelContactRelationSisterInLaw;
extern NSString *const CNLabelContactRelationSisterInLawBrothersWife;
extern NSString *const CNLabelContactRelationSisterInLawElderBrothersWife;
extern NSString *const CNLabelContactRelationSisterInLawHusbandsBrothersWife;
extern NSString *const CNLabelContactRelationSisterInLawHusbandsSister;
extern NSString *const CNLabelContactRelationSisterInLawSpousesSister;
extern NSString *const CNLabelContactRelationSisterInLawWifesBrothersWife;
extern NSString *const CNLabelContactRelationSisterInLawWifesSister;
extern NSString *const CNLabelContactRelationSisterInLawYoungerBrothersWife;
extern NSString *const CNLabelContactRelationSon;
extern NSString *const CNLabelContactRelationSonInLaw;
extern NSString *const CNLabelContactRelationSonInLawOrBrotherInLaw;
extern NSString *const CNLabelContactRelationSonInLawOrStepson;
extern NSString *const CNLabelContactRelationSpouse;
extern NSString *const CNLabelContactRelationStepbrother;
extern NSString *const CNLabelContactRelationStepchild;
extern NSString *const CNLabelContactRelationStepdaughter;
extern NSString *const CNLabelContactRelationStepfather;
extern NSString *const CNLabelContactRelationStepmother;
extern NSString *const CNLabelContactRelationStepparent;
extern NSString *const CNLabelContactRelationStepsister;
extern NSString *const CNLabelContactRelationStepson;
extern NSString *const CNLabelContactRelationTeacher;
extern NSString *const CNLabelContactRelationUncle;
extern NSString *const CNLabelContactRelationUncleFathersBrother;
extern NSString *const CNLabelContactRelationUncleFathersElderBrother;
extern NSString *const CNLabelContactRelationUncleFathersElderSistersHusband;
extern NSString *const CNLabelContactRelationUncleFathersSistersHusband;
extern NSString *const CNLabelContactRelationUncleFathersYoungerBrother;
extern NSString *const CNLabelContactRelationUncleFathersYoungerSistersHusband;
extern NSString *const CNLabelContactRelationUncleMothersBrother;
extern NSString *const CNLabelContactRelationUncleMothersElderBrother;
extern NSString *const CNLabelContactRelationUncleMothersSistersHusband;
extern NSString *const CNLabelContactRelationUncleMothersYoungerBrother;
extern NSString *const CNLabelContactRelationUncleParentsBrother;
extern NSString *const CNLabelContactRelationUncleParentsElderBrother;
extern NSString *const CNLabelContactRelationUncleParentsYoungerBrother;
extern NSString *const CNLabelContactRelationWife;
extern NSString *const CNLabelContactRelationYoungerBrother;
extern NSString *const CNLabelContactRelationYoungerBrotherInLaw;
extern NSString *const CNLabelContactRelationYoungerCousin;
extern NSString *const CNLabelContactRelationYoungerCousinFathersBrothersDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinFathersBrothersSon;
extern NSString *const CNLabelContactRelationYoungerCousinFathersSistersDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinFathersSistersSon;
extern NSString *const CNLabelContactRelationYoungerCousinMothersBrothersDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinMothersBrothersSon;
extern NSString *const CNLabelContactRelationYoungerCousinMothersSiblingsDaughterOrFathersSistersDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinMothersSiblingsSonOrFathersSistersSon;
extern NSString *const CNLabelContactRelationYoungerCousinMothersSistersDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinMothersSistersSon;
extern NSString *const CNLabelContactRelationYoungerCousinParentsSiblingsDaughter;
extern NSString *const CNLabelContactRelationYoungerCousinParentsSiblingsSon;
extern NSString *const CNLabelContactRelationYoungerSibling;
extern NSString *const CNLabelContactRelationYoungerSiblingInLaw;
extern NSString *const CNLabelContactRelationYoungerSister;
extern NSString *const CNLabelContactRelationYoungerSisterInLaw;
extern NSString *const CNLabelContactRelationYoungestBrother;
extern NSString *const CNLabelContactRelationYoungestSister;
extern NSString *const CNLabelDateAnniversary;
extern NSString *const CNLabelEmailiCloud;
extern NSString *const CNLabelHome;
extern NSString *const CNLabelOther;
extern NSString *const CNLabelPhoneNumberAppleWatch;
extern NSString *const CNLabelPhoneNumberAssistant;
extern NSString *const CNLabelPhoneNumberCallback;
extern NSString *const CNLabelPhoneNumberCar;
extern NSString *const CNLabelPhoneNumberCompanyMain;
extern NSString *const CNLabelPhoneNumberHomeFax;
extern NSString *const CNLabelPhoneNumberISDN;
extern NSString *const CNLabelPhoneNumberMain;
extern NSString *const CNLabelPhoneNumberMobile;
extern NSString *const CNLabelPhoneNumberOtherFax;
extern NSString *const CNLabelPhoneNumberPager;
extern NSString *const CNLabelPhoneNumberRadio;
extern NSString *const CNLabelPhoneNumberTTY;
extern NSString *const CNLabelPhoneNumberTelex;
extern NSString *const CNLabelPhoneNumberWorkFax;
extern NSString *const CNLabelPhoneNumberiPhone;
extern NSString *const CNLabelSchool;
extern NSString *const CNLabelURLAddressHomePage;
extern NSString *const CNLabelUnknown;
extern NSString *const CNLabelWork;
extern NSString *const CNLabeledValueIdentifierKey;
extern NSString *const CNLabeledValueLabelKey;
extern NSString *const CNLabeledValueValueKey;
extern NSString *const CNLinkedContactsIdentifiersPboardType;
extern NSString *const CNMatchInfoExcerptEmphasisAttributeName;
extern NSString *const CNNicknameStoreChangedNotification;
extern NSString *const CNNotificationOriginatedExternally;
extern NSString *const CNNotificationSaveIdentifiers;
extern NSString *const CNNotificationSources;
extern NSString *const CNOtherLabel;
extern NSString *const CNPhoneAssistantLabel;
extern NSString *const CNPhoneCarLabel;
extern NSString *const CNPhoneCompanyMainLabel;
extern NSString *const CNPhoneHomeFaxLabel;
extern NSString *const CNPhoneIPhoneLabel;
extern NSString *const CNPhoneMainLabel;
extern NSString *const CNPhoneMobileLabel;
extern NSString *const CNPhoneNumberCountryCodeKey;
extern NSString *const CNPhoneNumberDigitsKey;
extern NSString *const CNPhoneNumberStringValueKey;
extern NSString *const CNPhonePagerLabel;
extern NSString *const CNPhoneRadioLabel;
extern NSString *const CNPhoneWorkFaxLabel;
extern NSString *const CNPostalAddressCityKey;
extern NSString *const CNPostalAddressCountryKey;
extern NSString *const CNPostalAddressFormattedAddressKey;
extern NSString *const CNPostalAddressISOCountryCodeKey;
extern NSString *const CNPostalAddressLocalizedPropertyNameAttribute;
extern NSString *const CNPostalAddressPostalCodeKey;
extern NSString *const CNPostalAddressPropertyAttribute;
extern NSString *const CNPostalAddressStateKey;
extern NSString *const CNPostalAddressStreetKey;
extern NSString *const CNPostalAddressSubAdministrativeAreaKey;
extern NSString *const CNPostalAddressSubLocalityKey;
extern NSString *const CNSocialProfileBundleIdentifiersKey;
extern NSString *const CNSocialProfileDisplayNameKey;
extern NSString *const CNSocialProfileServiceFacebook;
extern NSString *const CNSocialProfileServiceFlickr;
extern NSString *const CNSocialProfileServiceGameCenter;
extern NSString *const CNSocialProfileServiceKey;
extern NSString *const CNSocialProfileServiceLinkedIn;
extern NSString *const CNSocialProfileServiceMySpace;
extern NSString *const CNSocialProfileServiceSinaWeibo;
extern NSString *const CNSocialProfileServiceTencentWeibo;
extern NSString *const CNSocialProfileServiceTwitter;
extern NSString *const CNSocialProfileServiceYelp;
extern NSString *const CNSocialProfileTeamIdentifierKey;
extern NSString *const CNSocialProfileURLStringKey;
extern NSString *const CNSocialProfileUserIdentifierKey;
extern NSString *const CNSocialProfileUsernameKey;
extern NSString *const CNTelephonyServiceName;
extern NSString *const CNUserActivityContactIdentifierKey;
extern NSString *const CNUserActivityContactNameKey;
extern NSString *const CNUserActivityEmailAddressesKey;
extern NSString *const CNUserActivityFamilyNameKey;
extern NSString *const CNUserActivityGivenNameKey;
extern NSString *const CNUserActivityIsOrganizationKey;
extern NSString *const CNUserActivityOrganizationNameKey;
extern NSString *const CNUserActivityPhoneNumbersKey;
extern NSString *const CNUserActivitySearchQueryKey;
extern NSString *const CNUserActivityServerUUIDsKey;
extern NSString *const CNUserActivityShowUnifiedKey;
extern NSString *const CNUserActivityTypeCreateContact;
extern NSString *const CNUserActivityTypeSearchContacts;
extern NSString *const CNUserActivityTypeViewContact;
extern NSString *const CNUserActivityVCardKey;
extern NSString *const CNUserActivityVersionKey;
extern NSString *const CNVCardDisplayNamesType;
extern NSString *const CNValueOriginStoreInfoKey;
extern NSString *const CNWallpaperColorDescriptionKeyAlpha;
extern NSString *const CNWallpaperColorDescriptionKeyBlue;
extern NSString *const CNWallpaperColorDescriptionKeyBrightness;
extern NSString *const CNWallpaperColorDescriptionKeyGreen;
extern NSString *const CNWallpaperColorDescriptionKeyHue;
extern NSString *const CNWallpaperColorDescriptionKeyRed;
extern NSString *const CNWallpaperColorDescriptionKeySaturation;
extern NSString *const CNWallpaperColorDescriptionKeyWhite;
extern NSString *const CNWallpaperFontDescriptionKeyName;
extern NSString *const CNWallpaperFontDescriptionKeyPointSize;
extern NSString *const CNWallpaperFontDescriptionKeyWeight;
extern NSString *const CNWallpaperTypeMemoji;
extern NSString *const CNWallpaperTypeMonogram;
extern NSString *const CNWallpaperTypePhoto;
extern NSString *const CNWallpaperUserInfoKeyBackgroundColorDescription;
extern NSString *const CNWallpaperUserInfoKeyDataRepresentation;
extern NSString *const CNWorkLabel;

@interface CNLabeledValue : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
+ (instancetype)labeledValueWithLabel:(NSString *)label value:(id)value;
- (instancetype)initWithLabel:(NSString *)label value:(id)value;
- (instancetype)init;
- (NSString *)identifier;
- (NSString *)label;
- (id)value;
- (instancetype)labeledValueBySettingLabel:(NSString *)label;
- (instancetype)labeledValueBySettingValue:(id)value;
- (instancetype)labeledValueBySettingLabel:(NSString *)label value:(id)value;
+ (NSString *)localizedStringForLabel:(NSString *)label;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContact : NSObject <NSCopying, NSMutableCopying, NSSecureCoding>
- (NSString *)namePrefix;
- (NSString *)givenName;
- (NSString *)middleName;
- (NSString *)familyName;
- (NSString *)previousFamilyName;
- (NSString *)nameSuffix;
- (NSString *)nickname;
- (NSString *)organizationName;
- (NSString *)departmentName;
- (NSString *)jobTitle;
- (NSString *)phoneticGivenName;
- (NSString *)phoneticMiddleName;
- (NSString *)phoneticFamilyName;
- (NSString *)phoneticOrganizationName;
- (NSString *)note;
- (NSArray *)phoneNumbers;
- (NSArray *)emailAddresses;
- (NSArray *)postalAddresses;
- (NSArray *)urlAddresses;
- (NSArray *)contactRelations;
- (NSArray *)socialProfiles;
- (NSArray *)instantMessageAddresses;
- (NSArray *)dates;
+ (BOOL)supportsSecureCoding;
- (instancetype)init;
- (BOOL)isKeyAvailable:(NSString *)key;
- (BOOL)areKeysAvailable:(NSArray *)keyDescriptors;
- (void)_finchCheck:(NSString *)key;
- (id)_finchValue:(NSString *)key;
- (NSString *)_finchString:(NSString *)key;
- (NSArray *)_finchArray:(NSString *)key;
- (NSString *)identifier;
- (CNContactType)contactType;
- (NSData *)imageData;
- (NSData *)thumbnailImageData;
- (BOOL)imageDataAvailable;
- (NSDateComponents *)birthday;
- (NSDateComponents *)nonGregorianBirthday;
- (id)valueForUndefinedKey:(NSString *)key;
- (BOOL)isUnifiedWithContactWithIdentifier:(NSString *)contactIdentifier;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (NSString *)localizedStringForKey:(NSString *)key;
+ (NSComparator)comparatorForNameSortOrder:(CNContactSortOrder)sortOrder;
+ (id<CNKeyDescriptor>)descriptorForAllComparatorKeys;
+ (NSPredicate *)predicateForContactsMatchingName:(NSString *)name;
+ (NSPredicate *)predicateForContactsMatchingEmailAddress:(NSString *)emailAddress;
+ (NSPredicate *)predicateForContactsMatchingPhoneNumber:(CNPhoneNumber *)phoneNumber;
+ (NSPredicate *)predicateForContactsWithIdentifiers:(NSArray<NSString *> *)identifiers;
+ (NSPredicate *)predicateForContactsInGroupWithIdentifier:(NSString *)groupIdentifier;
+ (NSPredicate *)predicateForContactsInContainerWithIdentifier:(NSString *)containerIdentifier;
@end

@interface CNPhoneNumber : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
+ (instancetype)phoneNumberWithStringValue:(NSString *)stringValue;
- (instancetype)initWithStringValue:(NSString *)string;
- (instancetype)init;
- (NSString *)stringValue;
- (NSString *)countryCode;
- (NSString *)digits;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNPostalAddress : NSObject <NSCopying, NSMutableCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
- (instancetype)init;
- (NSString *)_finchPart:(NSString *)key;
- (NSString *)street;
- (NSString *)subLocality;
- (NSString *)city;
- (NSString *)subAdministrativeArea;
- (NSString *)state;
- (NSString *)postalCode;
- (NSString *)country;
- (NSString *)ISOCountryCode;
+ (NSString *)localizedStringForKey:(NSString *)key;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContactRelation : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
+ (instancetype)contactRelationWithName:(NSString *)name;
- (instancetype)initWithName:(NSString *)name;
- (instancetype)init;
- (NSString *)name;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNSocialProfile : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithUrlString:(NSString *)urlString username:(NSString *)username userIdentifier:(NSString *)userIdentifier service:(NSString *)service;
- (NSString *)urlString;
- (NSString *)username;
- (NSString *)userIdentifier;
- (NSString *)service;
+ (NSString *)localizedStringForKey:(NSString *)key;
+ (NSString *)localizedStringForService:(NSString *)service;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNInstantMessageAddress : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithUsername:(NSString *)username service:(NSString *)service;
- (NSString *)username;
- (NSString *)service;
+ (NSString *)localizedStringForKey:(NSString *)key;
+ (NSString *)localizedStringForService:(NSString *)service;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNGroup : NSObject <NSCopying, NSMutableCopying, NSSecureCoding>
+ (NSPredicate *)predicateForGroupsWithIdentifiers:(NSArray<NSString *> *)identifiers;
+ (NSPredicate *)predicateForSubgroupsInGroupWithIdentifier:(NSString *)parentGroupIdentifier;
+ (NSPredicate *)predicateForGroupsInContainerWithIdentifier:(NSString *)containerIdentifier;
+ (BOOL)supportsSecureCoding;
- (instancetype)init;
- (NSString *)identifier;
- (NSString *)name;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContainer : NSObject <NSCopying, NSSecureCoding>
+ (NSPredicate *)predicateForContainersWithIdentifiers:(NSArray<NSString *> *)identifiers;
+ (NSPredicate *)predicateForContainerOfContactWithIdentifier:(NSString *)contactIdentifier;
+ (NSPredicate *)predicateForContainerOfGroupWithIdentifier:(NSString *)groupIdentifier;
+ (BOOL)supportsSecureCoding;
- (NSString *)identifier;
- (NSString *)name;
- (CNContainerType)type;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContactProperty : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
+ (instancetype)contactPropertyWithContact:(CNContact *)contact propertyKey:(NSString *)key identifier:(NSString *)identifier;
- (CNContact *)contact;
- (NSString *)key;
- (id)value;
- (NSString *)identifier;
- (NSString *)label;
- (id)copyWithZone:(NSZone *)zone;
- (BOOL)isEqual:(id)o;
- (NSUInteger)hash;
- (NSString *)description;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContactVCardSerialization : NSObject
+ (id<CNKeyDescriptor>)descriptorForRequiredKeys;
+ (NSData *)dataWithContacts:(NSArray<CNContact *> *)contacts error:(NSError **)error;
+ (NSArray<CNContact *> *)contactsWithData:(NSData *)data error:(NSError **)error;
@end

@interface CNFetchRequest : NSObject

@end

@interface CNFetchResult : NSObject
- (id)value;
- (NSData *)currentHistoryToken;
@end

@interface CNSaveRequest : NSObject
- (instancetype)init;
- (void)_finchAdd:(NSString *)op object:(id)o argument:(id)a;
- (void)addContact:(CNMutableContact *)contact toContainerWithIdentifier:(NSString *)identifier;
- (void)updateContact:(CNMutableContact *)contact;
- (void)deleteContact:(CNMutableContact *)contact;
- (void)addGroup:(CNMutableGroup *)group toContainerWithIdentifier:(NSString *)identifier;
- (void)updateGroup:(CNMutableGroup *)group;
- (void)deleteGroup:(CNMutableGroup *)group;
- (void)addSubgroup:(CNGroup *)subgroup toGroup:(CNGroup *)group;
- (void)removeSubgroup:(CNGroup *)subgroup fromGroup:(CNGroup *)group;
- (void)addMember:(CNContact *)contact toGroup:(CNGroup *)group;
- (void)removeMember:(CNContact *)contact fromGroup:(CNGroup *)group;
@end

@interface CNChangeHistoryEvent : NSObject <NSCopying, NSSecureCoding>
+ (BOOL)supportsSecureCoding;
- (void)acceptEventVisitor:(id<CNChangeHistoryEventVisitor>)visitor;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNContactStore : NSObject
+ (CNAuthorizationStatus)authorizationStatusForEntityType:(CNEntityType)entityType;
+ (void)_finchForwardChanges;
- (instancetype)init;
- (instancetype)initWithConfiguration:(id)configuration;
- (void)requestAccessForEntityType:(CNEntityType)entityType completionHandler:(void (^)(BOOL, NSError *))completionHandler;
- (FinchContactsDB *)_finchRead;
- (NSArray<CNContact *> *)_finchContactsIn:(FinchContactsDB *)db predicate:(NSPredicate *)predicate keys:(NSArray *)keyDescriptors mutable:(BOOL)mutableObjects sortOrder:(CNContactSortOrder)order error:(NSError **)error;
- (NSArray<CNContact *> *)unifiedContactsMatchingPredicate:(NSPredicate *)predicate keysToFetch:(NSArray *)keys error:(NSError **)error;
- (CNContact *)unifiedContactWithIdentifier:(NSString *)identifier keysToFetch:(NSArray *)keys error:(NSError **)error;
- (CNContact *)unifiedMeContactWithKeysToFetch:(NSArray *)keys error:(NSError **)error;
- (BOOL)setMeContact:(CNContact *)contact error:(NSError **)error;
- (BOOL)enumerateContactsWithFetchRequest:(CNContactFetchRequest *)fetchRequest error:(NSError **)error usingBlock:(void (NS_NOESCAPE ^)(CNContact *, BOOL *))block;
- (CNFetchResult *)enumeratorForContactFetchRequest:(CNContactFetchRequest *)request error:(NSError **)error;
- (NSArray<CNGroup *> *)groupsMatchingPredicate:(NSPredicate *)predicate error:(NSError **)error;
- (CNContainer *)_finchContainer:(FinchContactsDB *)db;
- (NSArray<CNContainer *> *)containersMatchingPredicate:(NSPredicate *)predicate error:(NSError **)error;
- (NSString *)defaultContainerIdentifier;
- (NSData *)currentHistoryToken;
- (void)_finchPostChange;
- (BOOL)executeSaveRequest:(CNSaveRequest *)saveRequest error:(NSError **)error;
- (CNFetchResult *)enumeratorForChangeHistoryFetchRequest:(CNChangeHistoryFetchRequest *)request error:(NSError **)error;
@end

@interface CNContactsUserDefaults : NSObject
+ (instancetype)sharedDefaults;
- (CNContactSortOrder)sortOrder;
- (NSString *)countryCode;
@end

@interface CNMutableContact : CNContact
- (void)setNamePrefix:(NSString *)value;
- (void)setGivenName:(NSString *)value;
- (void)setMiddleName:(NSString *)value;
- (void)setFamilyName:(NSString *)value;
- (void)setPreviousFamilyName:(NSString *)value;
- (void)setNameSuffix:(NSString *)value;
- (void)setNickname:(NSString *)value;
- (void)setOrganizationName:(NSString *)value;
- (void)setDepartmentName:(NSString *)value;
- (void)setJobTitle:(NSString *)value;
- (void)setPhoneticGivenName:(NSString *)value;
- (void)setPhoneticMiddleName:(NSString *)value;
- (void)setPhoneticFamilyName:(NSString *)value;
- (void)setPhoneticOrganizationName:(NSString *)value;
- (void)setNote:(NSString *)value;
- (void)setPhoneNumbers:(NSArray *)value;
- (void)setEmailAddresses:(NSArray *)value;
- (void)setPostalAddresses:(NSArray *)value;
- (void)setUrlAddresses:(NSArray *)value;
- (void)setContactRelations:(NSArray *)value;
- (void)setSocialProfiles:(NSArray *)value;
- (void)setInstantMessageAddresses:(NSArray *)value;
- (void)setDates:(NSArray *)value;
- (void)_finchSet:(id)value forKey:(NSString *)key;
- (void)setContactType:(CNContactType)t;
- (void)setImageData:(NSData *)v;
- (void)setBirthday:(NSDateComponents *)v;
- (void)setNonGregorianBirthday:(NSDateComponents *)v;
- (CNContactType)contactType;
@end

@interface CNMutablePostalAddress : CNPostalAddress
- (void)_finchSet:(NSString *)v key:(NSString *)k;
- (void)setStreet:(NSString *)v;
- (void)setSubLocality:(NSString *)v;
- (void)setCity:(NSString *)v;
- (void)setSubAdministrativeArea:(NSString *)v;
- (void)setState:(NSString *)v;
- (void)setPostalCode:(NSString *)v;
- (void)setCountry:(NSString *)v;
- (void)setISOCountryCode:(NSString *)v;
- (NSString *)street;
- (NSString *)subLocality;
- (NSString *)city;
- (NSString *)subAdministrativeArea;
- (NSString *)state;
- (NSString *)postalCode;
- (NSString *)country;
- (NSString *)ISOCountryCode;
@end

@interface CNMutableGroup : CNGroup
- (void)setName:(NSString *)name;
- (NSString *)name;
@end

@interface CNContactFormatter : NSFormatter
+ (BOOL)supportsSecureCoding;
+ (id<CNKeyDescriptor>)descriptorForRequiredKeysForStyle:(CNContactFormatterStyle)style;
+ (id<CNKeyDescriptor>)descriptorForRequiredKeysForNameOrder;
+ (id<CNKeyDescriptor>)descriptorForRequiredKeysForDelimiter;
+ (NSString *)stringFromContact:(CNContact *)contact style:(CNContactFormatterStyle)style;
+ (NSAttributedString *)attributedStringFromContact:(CNContact *)contact style:(CNContactFormatterStyle)style defaultAttributes:(NSDictionary *)attributes;
+ (CNContactDisplayNameOrder)nameOrderForContact:(CNContact *)contact;
+ (NSString *)delimiterForContact:(CNContact *)contact;
- (CNContactFormatterStyle)style;
- (void)setStyle:(CNContactFormatterStyle)style;
- (NSString *)stringFromContact:(CNContact *)contact;
- (NSAttributedString *)attributedStringFromContact:(CNContact *)contact defaultAttributes:(NSDictionary *)attributes;
- (NSString *)stringForObjectValue:(id)obj;
- (NSAttributedString *)attributedStringForObjectValue:(id)obj withDefaultAttributes:(NSDictionary *)attrs;
- (BOOL)getObjectValue:(out id *)obj forString:(NSString *)string errorDescription:(out NSString **)error;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNPostalAddressFormatter : NSFormatter
+ (NSString *)stringFromPostalAddress:(CNPostalAddress *)postalAddress style:(CNPostalAddressFormatterStyle)style;
+ (NSAttributedString *)attributedStringFromPostalAddress:(CNPostalAddress *)postalAddress style:(CNPostalAddressFormatterStyle)style withDefaultAttributes:(NSDictionary *)attributes;
- (CNPostalAddressFormatterStyle)style;
- (void)setStyle:(CNPostalAddressFormatterStyle)style;
- (NSString *)stringFromPostalAddress:(CNPostalAddress *)postalAddress;
- (NSAttributedString *)attributedStringFromPostalAddress:(CNPostalAddress *)postalAddress withDefaultAttributes:(NSDictionary *)attributes;
- (NSString *)stringForObjectValue:(id)obj;
- (BOOL)getObjectValue:(out id *)obj forString:(NSString *)string errorDescription:(out NSString **)error;
@end

@interface CNContactFetchRequest : CNFetchRequest
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithKeysToFetch:(NSArray *)keysToFetch;
- (NSPredicate *)predicate;
- (void)setPredicate:(NSPredicate *)p;
- (NSArray *)keysToFetch;
- (void)setKeysToFetch:(NSArray *)k;
- (BOOL)mutableObjects;
- (void)setMutableObjects:(BOOL)m;
- (BOOL)unifyResults;
- (void)setUnifyResults:(BOOL)u;
- (CNContactSortOrder)sortOrder;
- (void)setSortOrder:(CNContactSortOrder)s;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNChangeHistoryFetchRequest : CNFetchRequest

@property(copy) NSData *startingToken;
@property(copy) NSArray *additionalContactKeyDescriptors;
@property BOOL shouldUnifyResults, mutableObjects, includeGroupChanges;
@property(copy) NSArray *excludedTransactionAuthors;
+ (BOOL)supportsSecureCoding;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CNChangeHistoryDropEverythingEvent : CNChangeHistoryEvent
- (void)acceptEventVisitor:(id<CNChangeHistoryEventVisitor>)visitor;
@end

@interface CNChangeHistoryAddContactEvent : CNChangeHistoryEvent
-(CNContact *)contact;
- (NSString *)containerIdentifier;

@end

@interface CNChangeHistoryUpdateContactEvent : CNChangeHistoryEvent
-(CNContact *)contact;

@end

@interface CNChangeHistoryDeleteContactEvent : CNChangeHistoryEvent
-(NSString *)contactIdentifier;

@end

@interface CNChangeHistoryAddGroupEvent : CNChangeHistoryEvent
-(CNGroup *)group;
- (NSString *)containerIdentifier;

@end

@interface CNChangeHistoryUpdateGroupEvent : CNChangeHistoryEvent
-(CNGroup *)group;

@end

@interface CNChangeHistoryDeleteGroupEvent : CNChangeHistoryEvent
-(NSString *)groupIdentifier;

@end

@interface CNChangeHistoryAddMemberToGroupEvent : CNChangeHistoryEvent
-(CNContact *)member;
- (CNGroup *)group;

@end

@interface CNChangeHistoryRemoveMemberFromGroupEvent : CNChangeHistoryEvent
-(CNContact *)member;
- (CNGroup *)group;

@end

@interface CNChangeHistoryAddSubgroupToGroupEvent : CNChangeHistoryEvent
-(CNGroup *)subgroup;
- (CNGroup *)group;

@end

@interface CNChangeHistoryRemoveSubgroupFromGroupEvent : CNChangeHistoryEvent
-(CNGroup *)subgroup;
- (CNGroup *)group;

@end
