#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Author CloudKit header from Finch methods and own field types; no SDK input."""
from pathlib import Path
import re
import subprocess
import sys
here = Path(__file__).resolve().parent
source = Path(sys.argv[1])
texts = [p.read_text() for p in sorted(source.glob('*.m'))]
text = '\n'.join(texts)
names = set(re.findall(r'@implementation\s+(CK\w+)',text))
names.update(re.findall(r'\[(CK\w+)\s+(?:class|alloc|new)\]', text))
names.update('CKDatabaseOperation CKServerChangeToken CKShareMetadata CKUserIdentity CKQueryCursor CKFetchRecordZoneChangesConfiguration CKFetchRecordZoneChangesOptions CKQueryNotification CKRecordZoneNotification CKSyncEnginePendingDatabaseChange'.split())
classes = dict.fromkeys(sorted(names), 'NSObject')
classes['CKOperation']='NSOperation'
classes['CKDatabaseOperation']='CKOperation'
for name in classes:
    if name.endswith('Operation') and name not in ['CKOperation','CKDatabaseOperation']:classes[name]='CKDatabaseOperation'
classes.update(CKQuerySubscription='CKSubscription',CKRecordZoneSubscription='CKSubscription',CKDatabaseSubscription='CKSubscription',CKShare='CKRecord',CKQueryNotification='CKNotification',CKRecordZoneNotification='CKNotification',CKLocationSortDescriptor='NSSortDescriptor',CKSyncEnginePendingZoneSave='CKSyncEnginePendingDatabaseChange',CKSyncEnginePendingZoneDelete='CKSyncEnginePendingDatabaseChange')
# Topological order ensures superclasses are complete when used.
ordered={}
while len(ordered)<len(classes):
    for n,b in classes.items():
        if b not in classes or b in ordered:ordered[n]=b
classes=ordered
out=here/'include/CloudKit/CloudKit.h'
subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),str(source),str(here/'types.h'),str(out),'--categories','--class-map',','.join(n+':'+b for n,b in classes.items())],check=True)
header=out.read_text()
# Field types come from the source's values and how it uses each field.
fields={}
for typ,names in {
 'NSString *':'recordName zoneName ownerName recordType containerIdentifier subscriptionID operationID operationGroupID name emailAddress phoneNumber participantID applicationBundleID recordChangeTag collapseID alertBody alertLocalizationKey alertActionLocalizationKey alertLaunchImage soundName category',
 'CKRecordZoneID *':'zoneID', 'CKRecordID *':'recordID creatorUserRecordID lastModifiedUserRecordID rootRecordID hierarchicalRootRecordID',
 'CKReference *':'parent share', 'CKAsset *':'asset', 'NSURL *':'fileURL URL',
 'NSDate *':'creationDate modificationDate', 'NSPredicate *':'predicate',
 'NSArray *':'desiredKeys sortDescriptors participants shareMetadatas recordIDs recordZoneIDs recordZonesToSave recordZoneIDsToDelete recordsToSave recordIDsToDelete subscriptionsToSave subscriptionIDsToDelete subscriptionIDs userIdentityLookupInfos shareURLs lookupInfos alertLocalizationArgs pendingRecordZoneChanges pendingDatabaseChanges zoneIDs recordTypes recordFieldKeys',
 'NSDictionary *':'recordZoneIDsToOptionsByRecordZoneID configurationsByRecordZoneID containerNameToLongLivedOperationIDs encryptedValues',
 'CKUserIdentity *':'userIdentity ownerIdentity', 'CKShareParticipant *':'owner currentUserParticipant',
 'CKDatabase *':'database', 'CKContainer *':'container', 'CKSyncEngineState *':'state',
 'CKSyncEngineStateSerialization *':'stateSerialization', 'NSData *':'data',
 'CKSyncEngineSendChangesScope *':'scope', 'CKOperationConfiguration *':'configuration defaultConfiguration',
 'CKOperationGroup *':'group', 'CKQuery *':'query', 'CKQueryCursor *':'cursor',
 'CKServerChangeToken *':'previousServerChangeToken', 'CLLocation *':'relativeLocation',
 'NSPersonNameComponents *':'nameComponents', 'CKNotificationInfo *':'notificationInfo',
 'CKNotificationID *':'notificationID', 'BOOL':'allowsCellularAccess longLived atomic fetchAllChanges automaticallySync shouldBadge shouldSendContentAvailable shouldSendMutableContent isPruned isReadOnly ownerIsCurrentUser hasPendingUntrackedChanges',
 'NSTimeInterval':'timeoutIntervalForRequest timeoutIntervalForResource',
 'NSInteger':'qualityOfService expectedSendSize expectedReceiveSize transferSize direction type status role permission publicPermission acceptanceStatus queryNotificationReason subscriptionType databaseScope participantAccessOptions participantPermissionOptions savePolicy priority encryptionScope referenceAction',
 'NSUInteger':'capabilities resultsLimit operationCount quantity options',
 'id<CKSyncEngineDelegate>':'delegate',
}.items():
    for n in names.split():fields[n]=typ
# Coding calls provide scalar field types directly from the implementation.
for selector,typ in [('encodeInteger','NSInteger'),('encodeInt64','int64_t'),('encodeBool','BOOL'),('encodeDouble','double')]:
    for name in re.findall(selector+r':\s*self\.(\w+)',text):fields.setdefault(name,typ)
for name in re.findall(r'self\.(\w+)\s*=\s*(?:YES|NO)\b',text):fields.setdefault(name,'BOOL')
fields['qualityOfService']='NSQualityOfService'
fields['allowedParticipantPermissionOptions']='CKSharingParticipantPermissionOption'
fields['allowedParticipantAccessOptions']='CKSharingParticipantAccessOption'
properties={n:{} for n in classes}
for txt in texts:
    for match in re.finditer(r'@implementation\s+(CK\w+)[^\n]*\n(.*?)@end',txt,re.S):
        name,body=match.groups()
        for field in set(re.findall(r'\b(?:self|copy)\.(\w+)',body)):
            # Methods already implemented need no synthesized storage.
            if re.search(r'^[ \t]*-\s*\([^)]*\)\s*'+field+r'\s*\{',body,re.M):continue
            properties[name][field]=fields.get(field,'id')
        for statement in re.findall(r'\[self setValue:([^;]+)\];',body):
            keys=re.findall(r'forKey:@"(\w+)"',statement)
            if keys:
                field=keys[-1]
                properties[name][field]=fields.get(field,'id')
# Callback signatures and fields used outside their owner's implementations.
callbacks={
'CKModifyRecordsOperation':{'perRecordSaveBlock':'void (^)(CKRecordID *, CKRecord *, NSError *)','perRecordDeleteBlock':'void (^)(CKRecordID *, NSError *)','modifyRecordsCompletionBlock':'void (^)(NSArray *, NSArray *, NSError *)'},
'CKFetchRecordsOperation':{'perRecordCompletionBlock':'void (^)(CKRecord *, CKRecordID *, NSError *)','fetchRecordsCompletionBlock':'void (^)(NSDictionary *, NSError *)'},
'CKModifyRecordZonesOperation':{'perRecordZoneSaveBlock':'void (^)(CKRecordZoneID *, CKRecordZone *, NSError *)','perRecordZoneDeleteBlock':'void (^)(CKRecordZoneID *, NSError *)','modifyRecordZonesCompletionBlock':'void (^)(NSArray *, NSArray *, NSError *)'},
'CKFetchRecordZonesOperation':{'perRecordZoneCompletionBlock':'void (^)(CKRecordZoneID *, CKRecordZone *, NSError *)','fetchRecordZonesCompletionBlock':'void (^)(NSDictionary *, NSError *)'},
'CKModifySubscriptionsOperation':{'perSubscriptionSaveBlock':'void (^)(CKSubscriptionID, CKSubscription *, NSError *)','perSubscriptionDeleteBlock':'void (^)(CKSubscriptionID, NSError *)','modifySubscriptionsCompletionBlock':'void (^)(NSArray *, NSArray *, NSError *)'},
'CKFetchSubscriptionsOperation':{'perSubscriptionCompletionBlock':'void (^)(CKSubscriptionID, CKSubscription *, NSError *)','fetchSubscriptionCompletionBlock':'void (^)(NSDictionary *, NSError *)'},
'CKFetchDatabaseChangesOperation':{'fetchDatabaseChangesCompletionBlock':'void (^)(CKServerChangeToken *, BOOL, NSError *)'},
'CKFetchRecordZoneChangesOperation':{'fetchRecordZoneChangesCompletionBlock':'void (^)(NSError *)'},
'CKQueryOperation':{'queryCompletionBlock':'void (^)(CKQueryCursor *, NSError *)'},
'CKAcceptSharesOperation':{'perShareCompletionBlock':'void (^)(CKShareMetadata *, CKShare *, NSError *)','acceptSharesCompletionBlock':'void (^)(NSError *)'},
'CKDiscoverUserIdentitiesOperation':{'discoverUserIdentitiesCompletionBlock':'void (^)(NSError *)'},
'CKDiscoverAllUserIdentitiesOperation':{'discoverAllUserIdentitiesCompletionBlock':'void (^)(NSError *)'},
'CKFetchShareMetadataOperation':{'perShareMetadataBlock':'void (^)(NSURL *, CKShareMetadata *, NSError *)','fetchShareMetadataCompletionBlock':'void (^)(NSError *)'},
'CKFetchShareParticipantsOperation':{'perShareParticipantCompletionBlock':'void (^)(CKUserIdentityLookupInfo *, CKShareParticipant *, NSError *)','fetchShareParticipantsCompletionBlock':'void (^)(NSError *)'},
'CKFetchWebAuthTokenOperation':{'fetchWebAuthTokenCompletionBlock':'void (^)(NSString *, NSError *)'},
}
properties['CKDatabaseOperation']['database']='CKDatabase *'
properties['CKUserIdentity'].update(hasiCloudAccount='BOOL',contactIdentifiers='NSArray *',nameComponents='NSPersonNameComponents *',userRecordID='CKRecordID *',lookupInfo='CKUserIdentityLookupInfo *')
properties['CKNotification'].update(notificationType='CKNotificationType',subscriptionID='CKSubscriptionID',containerIdentifier='NSString *',notificationID='CKNotificationID *')
properties['CKQueryNotification'].update(recordID='CKRecordID *',databaseScope='CKDatabaseScope',queryNotificationReason='NSInteger')
properties['CKRecordZoneNotification'].update(recordZoneID='CKRecordZoneID *',databaseScope='CKDatabaseScope')
properties['CKSyncEnginePendingDatabaseChange'].update(zoneID='CKRecordZoneID *',type='CKSyncEnginePendingDatabaseChangeType')
properties['CKSyncEngineFetchChangesOptions']['scope']='CKSyncEngineFetchChangesScope *'
for name,props in callbacks.items():properties[name].update(props)
# Their factory initializers assign these through accessors rather than KVC.
for name,fieldnames in {
'CKModifyRecordsOperation':'recordsToSave recordIDsToDelete', 'CKFetchRecordsOperation':'recordIDs',
'CKModifyRecordZonesOperation':'recordZonesToSave recordZoneIDsToDelete', 'CKFetchRecordZonesOperation':'recordZoneIDs',
'CKModifySubscriptionsOperation':'subscriptionsToSave subscriptionIDsToDelete', 'CKFetchSubscriptionsOperation':'subscriptionIDs',
'CKAcceptSharesOperation':'shareMetadatas','CKDiscoverUserIdentitiesOperation':'userIdentityLookupInfos',
'CKFetchShareMetadataOperation':'shareURLs','CKFetchShareParticipantsOperation':'userIdentityLookupInfos',
}.items():
 for field in fieldnames.split():properties[name][field]=fields.get(field,'id')
# Reuse inherited storage rather than synthesizing another private ivar.
for name in classes:
    if name=='CKLocationSortDescriptor':
        for field in ['key','ascending','selector','comparator']:properties[name].pop(field,None)
    if name=='CKOperation':properties[name].pop('qualityOfService',None)
    base=classes[name]
    while base in classes:
        for field in properties[base]:properties[name].pop(field,None)
        base=classes[base]
for name,props in properties.items():
    declarations=[]
    for n,t in sorted(props.items()):
        if n in ['class','superclass','hash','isCancelled','isExecuting','isFinished']:continue
        attributes='copy' if '^' in t or t.startswith(('NSString','NSArray','NSDictionary','NSData','NSDate','NSURL','NSNumber','NSUUID','NSSet')) else ('retain' if '*' in t or t=='id' or t.startswith('id<') else 'assign')
        if name=='CKSyncEngineConfiguration' and n=='delegate':attributes='weak'
        if n in ('longLived','atomic'):attributes+=',getter=is'+n[0].upper()+n[1:]
        declaration=t.replace('(^)', '(^'+n+')') if '^' in t else t+' '+n
        declarations.append('@property('+attributes+') '+declaration+';')
    marker='@interface '+name+' : '+classes[name]+'\n'
    header=header.replace(marker,marker+'\n'.join(declarations)+'\n')
# Shared private methods have their declarations in the Finch header.
header=re.sub(r'^.*\)_initWith(?:Identifier|Container|SubscriptionID):.*\n','',header,flags=re.M)
# NSString constants have nonstandard spacing or attributes in the original file.
constants=re.findall(r'NSString\s*\*\s*(?:const\s+)?(CK\w+)\s*=',text)
header+='\n'+''.join('extern NSString *const '+n+';\n' for n in constants if not re.search(r'extern [^;]*\b'+n+r';',header))
out.write_text(header)
