/* SPDX-License-Identifier: MIT */
#import <CloudKit/CloudKit.h>
#import <objc/message.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed: %s\n",#x);return 1;} }while(0)
int main(void) { @autoreleasepool {
    CKOperationConfiguration *configuration=[CKOperationConfiguration new];
    configuration.longLived=YES;
    CHECK([configuration isLongLived]);
    CHECK(((BOOL(*)(id,SEL))objc_msgSend)(configuration,@selector(longLived)));
    configuration.longLived=NO; CHECK(![configuration isLongLived]);
    puts("PASS CloudKit isLongLived and initial-declaration alias");
    return 0;
} }
