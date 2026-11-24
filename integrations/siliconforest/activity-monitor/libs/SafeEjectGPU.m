// SPDX-License-Identifier: MIT
// SafeEjectGPU.framework (private) for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md): the GPUs and the processes using each (the
// GPU column, the GPU history window). No GPU is reported (the system's GPU driver keeps no
// per-process accounting a client could read).
#import <Foundation/Foundation.h>
__attribute__((visibility("default")))
@interface SafeEjectGPU : NSObject
@end
@implementation SafeEjectGPU
- (NSArray *)gpus { return [NSArray array]; }
- (id)newSessionWithGPUID:(NSUInteger)gpuID { (void)gpuID; return nil; }
- (double)getPercentUtilization { return 0; }
@end
