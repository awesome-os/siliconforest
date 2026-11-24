/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Based on Finch LinearAlgebra/object.m. PD does not export the OS_object
 * metaclass or private allocation entry point; use actual NSObject lifetime
 * with enough zeroed instance storage for Finch's C representation. */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdlib.h>
#include "internal.h"
@interface OS_la_object : NSObject
@end
@implementation OS_la_object
- (void)dealloc {
    la_object_dispose((struct la_s *)self);
    [super dealloc];
}
@end
struct la_s *la_object_alloc(void) {
    Class cls=[OS_la_object class];
    size_t base=class_getInstanceSize(cls);
    if(base>sizeof(struct la_s)) abort();
    struct la_s *result=(struct la_s *)class_createInstance(cls,sizeof(struct la_s)-base);
    if(!result) abort();
    return result;
}
struct la_s *la_object_retain(struct la_s *object) { return (struct la_s *)[(id)object retain]; }
void la_object_release(struct la_s *object) { [(id)object release]; }
