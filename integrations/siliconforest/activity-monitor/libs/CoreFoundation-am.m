// SPDX-License-Identifier: MIT
// What Apple's Activity Monitor imports from CoreFoundation beyond what Terminal.app needed
// (docs/roadmap/issues/SF-AA-activity-monitor.md); built into the CoreFoundation wrapper with
// ../../terminal-app/compat/CoreFoundation.m.
//   __NSDictionary0__struct  what clang references for an empty @{}: an NSConstantDictionary of
//                            no entries (libFoundation has the class and the empty array's twin)
//   NSCache                  on macOS in CoreFoundation; a dictionary with count and cost limits
#import <Foundation/Foundation.h>
#include <pthread.h>

extern char sf_NSConstantDictionaryClass __asm__("_OBJC_CLASS_$_NSConstantDictionary");

__attribute__((visibility("default")))
struct { void *isa; NSUInteger options; NSUInteger count; const id *keys; const id *objects; } __NSDictionary0__struct =
    { &sf_NSConstantDictionaryClass, 0, 0, NULL, NULL };

@protocol SFCacheDelegate <NSObject>
@optional
- (void)cache:(id)cache willEvictObject:(id)obj;
@end

__attribute__((visibility("default")))
@interface NSCache : NSObject {
    pthread_mutex_t _lock;
    NSMutableDictionary *_objects;
    NSMutableDictionary *_costs;
    NSMutableArray *_order;      /* keys, oldest first */
    NSString *_name;
    id _delegate;
    NSUInteger _countLimit, _totalCostLimit, _totalCost;
    BOOL _evicts;
}
@end

@implementation NSCache

- (id)init {
    if ((self = [super init]) != nil) {
        pthread_mutex_init(&_lock, NULL);
        _objects = [[NSMutableDictionary alloc] init];
        _costs = [[NSMutableDictionary alloc] init];
        _order = [[NSMutableArray alloc] init];
        _name = [@"" copy];
        _evicts = YES;
    }
    return self;
}

- (void)dealloc {
    [_objects release];
    [_costs release];
    [_order release];
    [_name release];
    pthread_mutex_destroy(&_lock);
    [super dealloc];
}

- (NSString *)name { return [[_name retain] autorelease]; }
- (void)setName:(NSString *)name { name = [name copy]; [_name release]; _name = name; }
- (id)delegate { return _delegate; }
- (void)setDelegate:(id)delegate { _delegate = delegate; }
- (NSUInteger)countLimit { return _countLimit; }
- (NSUInteger)totalCostLimit { return _totalCostLimit; }
- (BOOL)evictsObjectsWithDiscardedContent { return _evicts; }
- (void)setEvictsObjectsWithDiscardedContent:(BOOL)b { _evicts = b; }

/* called with the lock held; the delegate is told outside it */
- (void)_sfRemoveKey:(id)key into:(NSMutableArray *)evicted {
    id obj = [_objects objectForKey:key];
    if (obj == nil)
        return;
    if (evicted)
        [evicted addObject:obj];
    _totalCost -= [[_costs objectForKey:key] unsignedIntegerValue];
    [_costs removeObjectForKey:key];
    [_order removeObject:key];
    [_objects removeObjectForKey:key];
}

- (void)_sfTrimInto:(NSMutableArray *)evicted {
    while ([_order count] > 0 && ((_countLimit && [_order count] > _countLimit) || (_totalCostLimit && _totalCost > _totalCostLimit)))
        [self _sfRemoveKey:[_order objectAtIndex:0] into:evicted];
}

- (void)_sfTellDelegate:(NSArray *)evicted {
    if ([_delegate respondsToSelector:@selector(cache:willEvictObject:)])
        for (id obj in evicted)
            [_delegate cache:self willEvictObject:obj];
}

- (void)setCountLimit:(NSUInteger)n {
    NSMutableArray *evicted = [NSMutableArray array];
    pthread_mutex_lock(&_lock);
    _countLimit = n;
    [self _sfTrimInto:evicted];
    pthread_mutex_unlock(&_lock);
    [self _sfTellDelegate:evicted];
}

- (void)setTotalCostLimit:(NSUInteger)n {
    NSMutableArray *evicted = [NSMutableArray array];
    pthread_mutex_lock(&_lock);
    _totalCostLimit = n;
    [self _sfTrimInto:evicted];
    pthread_mutex_unlock(&_lock);
    [self _sfTellDelegate:evicted];
}

- (id)objectForKey:(id)key {
    id obj;
    pthread_mutex_lock(&_lock);
    obj = key ? [[[_objects objectForKey:key] retain] autorelease] : nil;
    pthread_mutex_unlock(&_lock);
    return obj;
}

- (void)setObject:(id)obj forKey:(id)key cost:(NSUInteger)cost {
    if (key == nil)
        return;
    if (obj == nil) {
        [self removeObjectForKey:key];
        return;
    }
    NSMutableArray *evicted = [NSMutableArray array];
    pthread_mutex_lock(&_lock);
    [self _sfRemoveKey:key into:nil];
    [_objects setObject:obj forKey:key];
    [_costs setObject:[NSNumber numberWithUnsignedInteger:cost] forKey:key];
    [_order addObject:key];
    _totalCost += cost;
    [self _sfTrimInto:evicted];
    pthread_mutex_unlock(&_lock);
    [self _sfTellDelegate:evicted];
}

- (void)setObject:(id)obj forKey:(id)key { [self setObject:obj forKey:key cost:0]; }

- (void)removeObjectForKey:(id)key {
    NSMutableArray *evicted = [NSMutableArray array];
    pthread_mutex_lock(&_lock);
    if (key)
        [self _sfRemoveKey:key into:evicted];
    pthread_mutex_unlock(&_lock);
    [self _sfTellDelegate:evicted];
}

- (void)removeAllObjects {
    NSMutableArray *evicted = [NSMutableArray array];
    pthread_mutex_lock(&_lock);
    while ([_order count] > 0)
        [self _sfRemoveKey:[_order objectAtIndex:0] into:evicted];
    pthread_mutex_unlock(&_lock);
    [self _sfTellDelegate:evicted];
}

@end
