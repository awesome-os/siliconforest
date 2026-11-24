// SPDX-License-Identifier: MIT
// AssetCacheServicesExtensions.framework (private) for Apple's Activity Monitor on SiliconForest
// (docs/roadmap/issues/SF-AA-activity-monitor.md): the content cache (the Cache tab). There is no
// content cache here: the manager is not activated and never calls its delegate, the metrics
// reader has no database and says so.
#import <Foundation/Foundation.h>
NSString *const kACMetricsDatabaseDirectory = @"/Library/Application Support/Apple/AssetCache/Metrics";
NSString *const kACMetricsDatabaseName = @"Metrics.db";

__attribute__((visibility("default")))
@interface AssetCacheServicesManager : NSObject
@end
@implementation AssetCacheServicesManager
- (id)initWithDelegate:(id)delegate delegateQueue:(id)queue { (void)delegate; (void)queue; return [super init]; }
- (BOOL)isActivated { return NO; }
@end

__attribute__((visibility("default")))
@interface AssetCacheMetricsReader : NSObject
@end
@implementation AssetCacheMetricsReader
- (id)initWithPath:(NSString *)path { (void)path; return [super init]; }
- (NSArray *)metricsSinceDate:(NSDate *)date withError:(NSError **)error
{
	(void)date;
	if (error)
		*error = [NSError errorWithDomain:@"AssetCacheServicesExtensions" code:-1
		    userInfo:[NSDictionary dictionaryWithObject:@"There is no content cache on this system." forKey:NSLocalizedDescriptionKey]];
	return nil;
}
@end
