/* SPDX-License-Identifier: MIT
 * Diagnostic only: test missing AppKit family enumeration in an owned process.
 * Never install/preload this into the owner's launcher. Adopt a proven fix in
 * AppKit's font-family provider, covering its complete API, instead.
 */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <stdio.h>

typedef id (*FamiliesIMP)(id, SEL);
typedef id (*MembersIMP)(id, SEL, id);
static FamiliesIMP originalFamilies;
static MembersIMP originalMembers;
static const char *aliases[]={"Georgia","Times","Times New Roman","serif","DejaVu Serif","DejaVuSerif"};

static BOOL romanAlias(NSString *name) {
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++)
        if([name caseInsensitiveCompare:[NSString stringWithUTF8String:aliases[i]]]==NSOrderedSame)return YES;
    return NO;
}

static id availableFamilies(id self, SEL command) {
    NSArray *physical=originalFamilies(self,command);
    if(![physical containsObject:@"Nimbus Roman"])return physical;
    NSMutableArray *result=[NSMutableArray arrayWithArray:physical];
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++){
        NSString *name=[NSString stringWithUTF8String:aliases[i]];
        if(![result containsObject:name])[result addObject:name];
    }
    return result;
}

static id availableMembers(id self, SEL command, NSString *family) {
    NSArray *physical=originalFamilies(self,@selector(availableFontFamilies));
    BOOL alias=romanAlias(family)&&![physical containsObject:family]&&[physical containsObject:@"Nimbus Roman"];
    NSString *resolved=alias?@"Nimbus Roman":family;
    NSArray *members=originalMembers(self,command,resolved);
    if(![resolved isEqualToString:@"Nimbus Roman"])return members;
    NSMutableArray *result=[NSMutableArray array];
    for(NSArray *row in members){
        if([row count]!=4)return members;
        NSString *name=[row objectAtIndex:0];
        BOOL bold=[name rangeOfString:@"Bold"].location!=NSNotFound;
        BOOL italic=[name rangeOfString:@"Italic"].location!=NSNotFound;
        // Nimbus Roman has four known regular/bold/italic/bold-italic faces.
        // NSFontManager's old wrapper returns 0 for both weight and traits.
        [result addObject:[NSArray arrayWithObjects:name,[row objectAtIndex:1],
            [NSNumber numberWithInt:bold?9:5],[NSNumber numberWithInt:(bold?2:0)|(italic?1:0)],nil]];
    }
    return result;
}

@interface SFFXFontFamilyAliasProbe : NSObject @end
@implementation SFFXFontFamilyAliasProbe
+(void)load {
    Class manager=objc_getClass("NSFontManager");
    Method families=class_getInstanceMethod(manager,@selector(availableFontFamilies));
    Method members=class_getInstanceMethod(manager,@selector(availableMembersOfFontFamily:));
    if(!families||!members){fprintf(stderr,"FXFAMILY probe_not_installed=1\n");return;}
    originalFamilies=(FamiliesIMP)method_getImplementation(families);
    originalMembers=(MembersIMP)method_getImplementation(members);
    method_setImplementation(families,(IMP)availableFamilies);
    method_setImplementation(members,(IMP)availableMembers);
    fprintf(stderr,"FXFAMILY private_probe_installed=1; no canonical files changed\n");
}
@end
