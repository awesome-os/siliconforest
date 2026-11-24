/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include <CoreText/CoreText.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <unistd.h>
@interface NSObject(SFFXFontFamilyCatalog)
+(id)sharedFontManager;
-(id)availableFontFamilies;
-(id)availableMembersOfFontFamily:(NSString *)name;
@end
int main(void) {
    alarm(20);
    @autoreleasepool {
        id manager=[(id)objc_getClass("NSFontManager") sharedFontManager];
        if(!manager)return 2;
        NSArray *families=[manager availableFontFamilies];
        NSMutableArray *rows=[NSMutableArray array];
        for(NSString *name in [NSArray arrayWithObjects:@"Georgia",@"Times",@"Times New Roman",@"serif",@"Nimbus Roman",@"Inter",nil]){
            NSArray *members=[manager availableMembersOfFontFamily:name];
            [rows addObject:[NSDictionary dictionaryWithObjectsAndKeys:name,@"requested",
                [NSNumber numberWithBool:[families containsObject:name]],@"enumerated",
                members?:[NSArray array],@"members",nil]];
        }
        NSDictionary *result=[NSDictionary dictionaryWithObjectsAndKeys:rows,@"families",
            [NSNumber numberWithUnsignedLong:[families count]],@"physical_or_virtual_count",nil];
        CFArrayRef ctFamilies=CTFontManagerCopyAvailableFontFamilyNames();
        NSMutableArray *ctRows=[NSMutableArray array];
        for(NSDictionary *row in rows){
            NSString *name=[row objectForKey:@"requested"];
            NSDictionary *attrs=[NSDictionary dictionaryWithObject:name forKey:(id)kCTFontFamilyNameAttribute];
            CTFontDescriptorRef descriptor=CTFontDescriptorCreateWithAttributes((CFDictionaryRef)attrs);
            CFArrayRef matches=descriptor?CTFontDescriptorCreateMatchingFontDescriptors(descriptor,NULL):NULL;
            [ctRows addObject:[NSDictionary dictionaryWithObjectsAndKeys:name,@"requested",
                [NSNumber numberWithBool:[(NSArray *)ctFamilies containsObject:name]],@"enumerated",
                [NSNumber numberWithLong:matches?CFArrayGetCount(matches):0],@"matching_faces",nil]];
            if(matches)CFRelease(matches);
            if(descriptor)CFRelease(descriptor);
        }
        result=[NSDictionary dictionaryWithObjectsAndKeys:result,@"appkit",ctRows,@"coretext_families",nil];
        if(ctFamilies)CFRelease(ctFamilies);
        NSData *data=[NSJSONSerialization dataWithJSONObject:result options:0 error:NULL];
        if(!data)return 3;
        fwrite([data bytes],1,[data length],stdout);fputc('\n',stdout);
    }
    return 0;
}
