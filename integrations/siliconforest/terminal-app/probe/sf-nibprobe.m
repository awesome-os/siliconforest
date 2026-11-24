// SPDX-License-Identifier: MIT
// sf-nibprobe <file.nib>...: decode a compiled nib the way NSNib does, without instantiating a
// user interface from it, and say what stands in the way: classes the archive names that do
// not exist here, an exception from a class's -initWithCoder:, a nil result.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

@interface Probe : NSObject { @public NSMutableSet *missing; }
@end
@implementation Probe
// what NSNib is to the objects of a nib while they decode: the table of external names
- (NSDictionary *)externalNameTable { return [NSDictionary dictionaryWithObject:[[NSObject new] autorelease] forKey:@"NSOwner"]; }
- (void)replaceObject:(id)object withObject:(id)replacement { }
- (id)unarchiver:(NSKeyedUnarchiver *)unarchiver didDecodeObject:(id)object { return object; }
- (Class)unarchiver:(NSKeyedUnarchiver *)unarchiver cannotDecodeObjectOfClassName:(NSString *)name originalClasses:(NSArray *)classNames
{
    if (![missing containsObject:name]) { [missing addObject:name]; printf("NIBPROBE missing class %s (%s)\n", [name UTF8String], [[classNames componentsJoinedByString:@" < "] UTF8String]); }
    return Nil;
}
@end

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    [NSClassFromString(@"NSApplication") performSelector:@selector(sharedApplication)];
    for (int i = 1; i < argc; i++) {
        NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[i]]];
        Probe *probe = [[Probe new] autorelease];
        probe->missing = [NSMutableSet set];
        printf("NIBPROBE %s: %lu bytes\n", argv[i], (unsigned long)[data length]);
        if (data == nil) continue;
        @try {
            Class nibArchive = NSClassFromString(@"NSNibArchiveUnarchiver");
            NSKeyedUnarchiver *u = (nibArchive && [nibArchive isNibArchiveData:data]) ? [[[nibArchive alloc] initForReadingWithData:data] autorelease]
                                                                                    : [[[NSKeyedUnarchiver alloc] initForReadingWithData:data] autorelease];
            printf("NIBPROBE unarchiver %s\n", class_getName([u class]));
            [u setDelegate:probe];
            id objectData = [u decodeObjectForKey:@"IB.objectdata"];
            printf("NIBPROBE IB.objectdata = %s\n", objectData ? class_getName([objectData class]) : "nil");
        } @catch (NSException *e) {
            printf("NIBPROBE exception %s: %s\n", [[e name] UTF8String], [[e reason] UTF8String]);
        }
    }
    [pool release];
    return 0;
}
