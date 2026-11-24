/* SPDX-License-Identifier: MIT */
/* Own keyed archive, actual factory allocation + a cyclic outlet graph. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
@interface SFLargeCodedNode:NSObject{unsigned char _padding[512];id _peer;NSInteger _value;}
- (id)peer;
- (NSInteger)value;
- (BOOL)paddingValid;
- (void)clearPeer;
@end
@implementation SFLargeCodedNode
- (id)initWithCoder:(NSCoder*)coder{
 if((self=[super init])){memset(_padding,0xa5,sizeof(_padding));_value=[coder decodeIntegerForKey:@"value"];_peer=[[coder decodeObjectForKey:@"peer"] retain];}return self;
}
- (void)dealloc{[_peer release];[super dealloc];}
- (id)peer{return _peer;}
- (NSInteger)value{return _value;}
- (BOOL)paddingValid{for(unsigned i=0;i<sizeof(_padding);i++)if(_padding[i]!=0xa5)return NO;return YES;}
- (void)clearPeer{[_peer release];_peer=nil;}
@end
@interface SFCodedFactory:NSObject @end
@implementation SFCodedFactory
+ (id)allocWithKeyedUnarchiver:(NSKeyedUnarchiver*)coder{
 NSString *name=[coder decodeObjectForKey:@"NSClassName"];
 if(![name isEqualToString:@"SFLargeCodedNode"])[NSException raise:NSInvalidArgumentException format:@"Wrong current allocator record"];
 return [NSClassFromString(name) alloc];
}
@end
static id uid(unsigned n){return @{@"CF$UID":@(n)};}
int main(void){@autoreleasepool{
 NSDictionary *archive=@{@"$archiver":@"NSKeyedArchiver",@"$version":@100000,@"$top":@{@"root":uid(1)},
  @"$objects":@[@"$null",@{@"$class":uid(3),@"NSClassName":uid(4),@"value":@17,@"peer":uid(2)},
   @{@"$class":uid(3),@"NSClassName":uid(4),@"value":@29,@"peer":uid(1)},
   @{@"$classname":@"SFCodedFactory",@"$classes":@[@"SFCodedFactory",@"NSObject"]},@"SFLargeCodedNode"]};
 NSError *error=nil;NSData *data=[NSPropertyListSerialization dataWithPropertyList:archive format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
 if(!data||error)return 2;
 SFLargeCodedNode *first=[NSKeyedUnarchiver unarchiveObjectWithData:data],*second=[first peer];
 BOOL classes=[first isKindOfClass:[SFLargeCodedNode class]]&&[second isKindOfClass:[SFLargeCodedNode class]];
 BOOL cycle=classes&&[second peer]==first,values=classes&&[first value]==17&&[second value]==29;
 BOOL padding=classes&&[first paddingValid]&&[second paddingValid];
 printf("NIB_FACTORY real-class=%d cycle=%d values=%d large-allocation=%d instance-bytes=%zu\n",classes,cycle,values,padding,class_getInstanceSize([SFLargeCodedNode class]));
 [second retain];[first clearPeer];[second clearPeer];[second release];return classes&&cycle&&values&&padding?0:1;
}}
