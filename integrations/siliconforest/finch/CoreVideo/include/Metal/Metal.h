/* SPDX-License-Identifier: MIT */
/* Only the public buffer protocol used by Finch CVMetalBuffer.m; no Metal shim. */
#pragma once
#import <Foundation/Foundation.h>
typedef NSUInteger MTLResourceOptions,MTLPixelFormat;
enum { MTLResourceStorageModeShared = 0 };
@protocol MTLBuffer <NSObject>
@end
@protocol MTLTexture <NSObject>
@end
@protocol MTLDevice <NSObject>
- (id<MTLBuffer>)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length options:(MTLResourceOptions)options;
@end
