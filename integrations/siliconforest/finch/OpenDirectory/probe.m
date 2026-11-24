/* SPDX-License-Identifier: MIT */
#import <OpenDirectory/OpenDirectory.h>
#import <CFOpenDirectory/CFOpenDirectory.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed: %s\n",#x); return 1; } } while(0)
int main(void) { @autoreleasepool {
    CFErrorRef error = NULL;
    ODSessionRef session = ODSessionCreate(NULL, NULL, &error);
    CHECK(session && !error && CFGetTypeID(session) == ODSessionGetTypeID());
    ODNodeRef node = ODNodeCreateWithNodeType(NULL, session, kODNodeTypeAuthentication, &error);
    CHECK(node && !error && CFGetTypeID(node) == ODNodeGetTypeID());
    CHECK(CFEqual(ODNodeGetName(node), CFSTR("/Local/Default")));
    ODRecordRef record = ODNodeCopyRecord(node, (CFStringRef)kODRecordTypeUsers, (__bridge CFStringRef)NSUserName(), NULL, &error);
    CHECK(record && !error && CFGetTypeID(record) == ODRecordGetTypeID());
    CFArrayRef values = ODRecordCopyValues(record, (CFStringRef)kODAttributeTypeUniqueID, &error);
    CHECK(values && !error && CFArrayGetCount(values) == 1);
    CFRelease(values); CFRelease(record); CFRelease(node);
    node = ODNodeCreateWithName(NULL, session, CFSTR("/SF-Finch-Invalid-Node"), &error);
    CHECK(!node && error && CFErrorGetCode(error) == kODErrorNodeUnknownName);
    CFRelease(error); CFRelease(session);
    ODNode *objectNode = [ODNode nodeWithSession:[ODSession defaultSession] type:kODNodeTypeAuthentication error:NULL];
    ODQuery *query = [ODQuery queryWithNode:objectNode forRecordTypes:kODRecordTypeUsers attribute:kODAttributeTypeRecordName matchType:kODMatchContains queryValues:NSUserName() returnAttributes:nil maximumResults:10 error:NULL];
    CHECK(query && [[query resultsAllowingPartial:NO error:NULL] count] == 1);
    puts("OpenDirectory: PASS (CF types, local node, current-user UID, unknown-node error; reads only)");
} return 0; }
