// SPDX-License-Identifier: MIT
// Metal.framework for a system that has no Metal device.
//
// SiliconForest has no Metal implementation on arm64 (upstream PureDarwin's is Darling's Metal
// over Vulkan, x86_64 only so far; on the M1 the GPU is reached through Mesa's OpenGL). An
// application that links Metal must still load, and must be told the truth: there is no
// device. MTLCreateSystemDefaultDevice() returns nil and MTLCopyAllDevices() an empty array,
// which is what Metal answers on a Mac without a usable GPU, and what applications already
// handle by falling back (Firefox uses OpenGL or software rendering).
//
// The descriptor classes are the plain value objects they are on macOS: they hold what is
// set on them. Without a device nothing can be created from them. Classes that only exist
// with a device (MTLCaptureManager's scopes, shared events) say so through their API.
#import <Foundation/Foundation.h>

#define MTL_EXPORT __attribute__((visibility("default")))

MTL_EXPORT id MTLCreateSystemDefaultDevice(void) { return nil; }
MTL_EXPORT NSArray *MTLCopyAllDevices(void) { return [[NSArray alloc] init]; }
// no device can appear or go away: the observer is returned as nil and never called
MTL_EXPORT NSArray *MTLCopyAllDevicesWithObserver(id *observer, void (^handler)(id device, NSString *notification))
{
    if (observer != NULL)
        *observer = nil;
    return [[NSArray alloc] init];
}
MTL_EXPORT void MTLRemoveDeviceObserver(id observer) { }

MTL_EXPORT NSString *const MTLDeviceWasAddedNotification = @"MTLDeviceWasAddedNotification";
MTL_EXPORT NSString *const MTLDeviceRemovalRequestedNotification = @"MTLDeviceRemovalRequestedNotification";
MTL_EXPORT NSString *const MTLDeviceWasRemovedNotification = @"MTLDeviceWasRemovedNotification";
MTL_EXPORT NSString *const MTLCommandBufferErrorDomain = @"MTLCommandBufferErrorDomain";
MTL_EXPORT NSString *const MTLLibraryErrorDomain = @"MTLLibraryErrorDomain";
MTL_EXPORT NSString *const MTLCaptureErrorDomain = @"MTLCaptureErrorDomain";

typedef struct { double red, green, blue, alpha; } MTLClearColor;

// a descriptor: copies carry every property set on the original (all are declared below and
// copied through key-value coding by name)
MTL_EXPORT
@interface MTLDescriptorBase : NSObject <NSCopying>
+ (NSArray *)_keys;
@end

@implementation MTLDescriptorBase
+ (NSArray *)_keys { return @[]; }
- (id)copyWithZone:(NSZone *)zone
{
    id copy = [[[self class] allocWithZone:zone] init];
    for (NSString *key in [[self class] _keys]) {
        id value = [self valueForKey:key];
        if ([value conformsToProtocol:@protocol(NSCopying)] && [value isKindOfClass:[MTLDescriptorBase class]])
            value = [[value copy] autorelease];
        [copy setValue:value forKey:key];
    }
    return copy;
}
@end

// an array of descriptors addressed by index, created on first use, as the *Array classes of Metal
MTL_EXPORT
@interface MTLDescriptorArray : NSObject <NSCopying> {
    NSMutableDictionary *_objects;
    Class _class;
}
- (instancetype)initWithClass:(Class)cls;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index;
@end

@implementation MTLDescriptorArray
- (instancetype)initWithClass:(Class)cls
{
    if ((self = [super init])) {
        _class = cls;
        _objects = [[NSMutableDictionary alloc] init];
    }
    return self;
}
- (void)dealloc { [_objects release]; [super dealloc]; }
- (id)objectAtIndexedSubscript:(NSUInteger)index
{
    NSNumber *key = [NSNumber numberWithUnsignedLong:index];
    id object = [_objects objectForKey:key];
    if (object == nil) {
        object = [[[_class alloc] init] autorelease];
        [_objects setObject:object forKey:key];
    }
    return object;
}
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index
{
    NSNumber *key = [NSNumber numberWithUnsignedLong:index];
    if (object == nil)
        [_objects removeObjectForKey:key];
    else
        [_objects setObject:[[object copy] autorelease] forKey:key];
}
- (id)copyWithZone:(NSZone *)zone
{
    MTLDescriptorArray *copy = [[MTLDescriptorArray allocWithZone:zone] initWithClass:_class];
    for (NSNumber *key in _objects)
        [copy->_objects setObject:[[[_objects objectForKey:key] copy] autorelease] forKey:key];
    return copy;
}
@end

#define DESCRIPTOR(name, keys, ...) \
    MTL_EXPORT @interface name : MTLDescriptorBase __VA_ARGS__ @end \
    @implementation name + (NSArray *)_keys { return keys; } @end

MTL_EXPORT @interface MTLCompileOptions : MTLDescriptorBase
@property(copy) NSDictionary *preprocessorMacros;
@property BOOL fastMathEnabled;
@property NSInteger mathMode;
@property NSInteger mathFloatingPointFunctions;
@property NSUInteger languageVersion;
@property NSInteger libraryType;
@property(copy) NSString *installName;
@property(copy) NSArray *libraries;
@property BOOL preserveInvariance;
@property NSInteger optimizationLevel;
@property NSInteger compileSymbolVisibility;
@property BOOL allowReferencingUndefinedSymbols;
@property NSUInteger maxTotalThreadsPerThreadgroup;
@property BOOL enableLogging;
@end
@implementation MTLCompileOptions
+ (NSArray *)_keys { return @[ @"preprocessorMacros", @"fastMathEnabled", @"mathMode", @"mathFloatingPointFunctions", @"languageVersion",
    @"libraryType", @"installName", @"libraries", @"preserveInvariance", @"optimizationLevel", @"compileSymbolVisibility",
    @"allowReferencingUndefinedSymbols", @"maxTotalThreadsPerThreadgroup", @"enableLogging" ]; }
- (instancetype)init { if ((self = [super init])) _fastMathEnabled = YES; return self; }
- (void)dealloc { [_preprocessorMacros release]; [_installName release]; [_libraries release]; [super dealloc]; }
@end

MTL_EXPORT @interface MTLStencilDescriptor : MTLDescriptorBase
@property NSUInteger stencilCompareFunction, stencilFailureOperation, depthFailureOperation, depthStencilPassOperation;
@property uint32_t readMask, writeMask;
@end
@implementation MTLStencilDescriptor
+ (NSArray *)_keys { return @[ @"stencilCompareFunction", @"stencilFailureOperation", @"depthFailureOperation", @"depthStencilPassOperation", @"readMask", @"writeMask" ]; }
- (instancetype)init { if ((self = [super init])) { _stencilCompareFunction = 7 /* always */; _readMask = _writeMask = 0xffffffff; } return self; }
@end

MTL_EXPORT @interface MTLDepthStencilDescriptor : MTLDescriptorBase
@property NSUInteger depthCompareFunction;
@property(getter=isDepthWriteEnabled) BOOL depthWriteEnabled;
@property(copy) MTLStencilDescriptor *frontFaceStencil, *backFaceStencil;
@property(copy) NSString *label;
@end
@implementation MTLDepthStencilDescriptor
+ (NSArray *)_keys { return @[ @"depthCompareFunction", @"depthWriteEnabled", @"frontFaceStencil", @"backFaceStencil", @"label" ]; }
- (instancetype)init
{
    if ((self = [super init])) {
        _depthCompareFunction = 7;
        _frontFaceStencil = [[MTLStencilDescriptor alloc] init];
        _backFaceStencil = [[MTLStencilDescriptor alloc] init];
    }
    return self;
}
- (void)dealloc { [_frontFaceStencil release]; [_backFaceStencil release]; [_label release]; [super dealloc]; }
@end

// values for a shader's function constants, by index or by name
MTL_EXPORT @interface MTLFunctionConstantValues : NSObject <NSCopying> {
    NSMutableDictionary *_values;
}
@end
@implementation MTLFunctionConstantValues
- (instancetype)init { if ((self = [super init])) _values = [[NSMutableDictionary alloc] init]; return self; }
- (void)dealloc { [_values release]; [super dealloc]; }
// the value's size follows from its type: 1, 2, 4 or 8 bytes for the scalar types constants can have
static NSUInteger constantSize(NSUInteger type)
{
    switch (type) {
    case 53: case 45: case 49: return 1;            // bool, char, uchar
    case 16: case 37: case 41: return 2;            // half, short, ushort
    case 81: case 85: return 8;                     // long, ulong
    default: return 4;                              // float, int, uint
    }
}
- (void)setConstantValue:(const void *)value type:(NSUInteger)type atIndex:(NSUInteger)index
{
    [_values setObject:@[ [NSData dataWithBytes:value length:constantSize(type)], [NSNumber numberWithUnsignedLong:type] ]
                forKey:[NSNumber numberWithUnsignedLong:index]];
}
- (void)setConstantValues:(const void *)values type:(NSUInteger)type withRange:(NSRange)range
{
    NSUInteger size = constantSize(type);
    for (NSUInteger i = 0; i < range.length; i++)
        [self setConstantValue:(const char *)values + i * size type:type atIndex:range.location + i];
}
- (void)setConstantValue:(const void *)value type:(NSUInteger)type withName:(NSString *)name
{
    [_values setObject:@[ [NSData dataWithBytes:value length:constantSize(type)], [NSNumber numberWithUnsignedLong:type] ] forKey:name];
}
- (void)reset { [_values removeAllObjects]; }
- (id)copyWithZone:(NSZone *)zone
{
    MTLFunctionConstantValues *copy = [[MTLFunctionConstantValues allocWithZone:zone] init];
    [copy->_values addEntriesFromDictionary:_values];
    return copy;
}
@end

MTL_EXPORT @interface MTLRenderPassAttachmentDescriptor : MTLDescriptorBase
@property(retain) id texture, resolveTexture;
@property NSUInteger level, slice, depthPlane, resolveLevel, resolveSlice, resolveDepthPlane, loadAction, storeAction, storeActionOptions;
@end
@implementation MTLRenderPassAttachmentDescriptor
+ (NSArray *)_keys { return @[ @"texture", @"resolveTexture", @"level", @"slice", @"depthPlane", @"resolveLevel", @"resolveSlice", @"resolveDepthPlane", @"loadAction", @"storeAction", @"storeActionOptions" ]; }
- (void)dealloc { [_texture release]; [_resolveTexture release]; [super dealloc]; }
@end
MTL_EXPORT @interface MTLRenderPassColorAttachmentDescriptor : MTLRenderPassAttachmentDescriptor
@property MTLClearColor clearColor;
@end
@implementation MTLRenderPassColorAttachmentDescriptor
- (instancetype)init { if ((self = [super init])) { self.storeAction = 1; _clearColor.alpha = 1.0; } return self; }
- (id)copyWithZone:(NSZone *)zone { MTLRenderPassColorAttachmentDescriptor *c = [super copyWithZone:zone]; c->_clearColor = _clearColor; return c; }
@end
MTL_EXPORT @interface MTLRenderPassDepthAttachmentDescriptor : MTLRenderPassAttachmentDescriptor
@property double clearDepth;
@property NSUInteger depthResolveFilter;
@end
@implementation MTLRenderPassDepthAttachmentDescriptor
+ (NSArray *)_keys { return [[super _keys] arrayByAddingObjectsFromArray:@[ @"clearDepth", @"depthResolveFilter" ]]; }
- (instancetype)init { if ((self = [super init])) { self.loadAction = 2; _clearDepth = 1.0; } return self; }
@end
MTL_EXPORT @interface MTLRenderPassStencilAttachmentDescriptor : MTLRenderPassAttachmentDescriptor
@property uint32_t clearStencil;
@property NSUInteger stencilResolveFilter;
@end
@implementation MTLRenderPassStencilAttachmentDescriptor
+ (NSArray *)_keys { return [[super _keys] arrayByAddingObjectsFromArray:@[ @"clearStencil", @"stencilResolveFilter" ]]; }
- (instancetype)init { if ((self = [super init])) self.loadAction = 2; return self; }
@end

MTL_EXPORT @interface MTLRenderPassDescriptor : MTLDescriptorBase
@property(readonly) MTLDescriptorArray *colorAttachments;
@property(copy) MTLRenderPassDepthAttachmentDescriptor *depthAttachment;
@property(copy) MTLRenderPassStencilAttachmentDescriptor *stencilAttachment;
@property(retain) id visibilityResultBuffer, rasterizationRateMap;
@property NSUInteger renderTargetArrayLength, renderTargetWidth, renderTargetHeight, defaultRasterSampleCount, imageblockSampleLength,
    threadgroupMemoryLength, tileWidth, tileHeight, visibilityResultType;
+ (MTLRenderPassDescriptor *)renderPassDescriptor;
@end
@implementation MTLRenderPassDescriptor
+ (NSArray *)_keys { return @[ @"depthAttachment", @"stencilAttachment", @"visibilityResultBuffer", @"rasterizationRateMap", @"renderTargetArrayLength",
    @"renderTargetWidth", @"renderTargetHeight", @"defaultRasterSampleCount", @"imageblockSampleLength", @"threadgroupMemoryLength", @"tileWidth",
    @"tileHeight", @"visibilityResultType" ]; }
+ (MTLRenderPassDescriptor *)renderPassDescriptor { return [[[self alloc] init] autorelease]; }
- (instancetype)init
{
    if ((self = [super init])) {
        _colorAttachments = [[MTLDescriptorArray alloc] initWithClass:[MTLRenderPassColorAttachmentDescriptor class]];
        _depthAttachment = [[MTLRenderPassDepthAttachmentDescriptor alloc] init];
        _stencilAttachment = [[MTLRenderPassStencilAttachmentDescriptor alloc] init];
    }
    return self;
}
- (id)copyWithZone:(NSZone *)zone
{
    MTLRenderPassDescriptor *copy = [super copyWithZone:zone];
    [copy->_colorAttachments release];
    copy->_colorAttachments = [_colorAttachments copy];
    return copy;
}
- (void)dealloc
{
    [_colorAttachments release]; [_depthAttachment release]; [_stencilAttachment release];
    [_visibilityResultBuffer release]; [_rasterizationRateMap release];
    [super dealloc];
}
@end

MTL_EXPORT @interface MTLRenderPipelineColorAttachmentDescriptor : MTLDescriptorBase
@property NSUInteger pixelFormat, sourceRGBBlendFactor, destinationRGBBlendFactor, rgbBlendOperation, sourceAlphaBlendFactor,
    destinationAlphaBlendFactor, alphaBlendOperation, writeMask;
@property(getter=isBlendingEnabled) BOOL blendingEnabled;
@end
@implementation MTLRenderPipelineColorAttachmentDescriptor
+ (NSArray *)_keys { return @[ @"pixelFormat", @"sourceRGBBlendFactor", @"destinationRGBBlendFactor", @"rgbBlendOperation", @"sourceAlphaBlendFactor",
    @"destinationAlphaBlendFactor", @"alphaBlendOperation", @"writeMask", @"blendingEnabled" ]; }
- (instancetype)init { if ((self = [super init])) { _sourceRGBBlendFactor = _sourceAlphaBlendFactor = 1; _writeMask = 0xf; } return self; }
@end

MTL_EXPORT @interface MTLVertexAttributeDescriptor : MTLDescriptorBase
@property NSUInteger format, offset, bufferIndex;
@end
@implementation MTLVertexAttributeDescriptor
+ (NSArray *)_keys { return @[ @"format", @"offset", @"bufferIndex" ]; }
@end
MTL_EXPORT @interface MTLVertexBufferLayoutDescriptor : MTLDescriptorBase
@property NSUInteger stride, stepFunction, stepRate;
@end
@implementation MTLVertexBufferLayoutDescriptor
+ (NSArray *)_keys { return @[ @"stride", @"stepFunction", @"stepRate" ]; }
- (instancetype)init { if ((self = [super init])) { _stepFunction = 1; _stepRate = 1; } return self; }
@end
MTL_EXPORT @interface MTLVertexDescriptor : MTLDescriptorBase
@property(readonly) MTLDescriptorArray *layouts, *attributes;
+ (MTLVertexDescriptor *)vertexDescriptor;
- (void)reset;
@end
@implementation MTLVertexDescriptor
+ (MTLVertexDescriptor *)vertexDescriptor { return [[[self alloc] init] autorelease]; }
- (instancetype)init
{
    if ((self = [super init])) {
        _layouts = [[MTLDescriptorArray alloc] initWithClass:[MTLVertexBufferLayoutDescriptor class]];
        _attributes = [[MTLDescriptorArray alloc] initWithClass:[MTLVertexAttributeDescriptor class]];
    }
    return self;
}
- (void)reset
{
    [_layouts release]; [_attributes release];
    _layouts = [[MTLDescriptorArray alloc] initWithClass:[MTLVertexBufferLayoutDescriptor class]];
    _attributes = [[MTLDescriptorArray alloc] initWithClass:[MTLVertexAttributeDescriptor class]];
}
- (id)copyWithZone:(NSZone *)zone
{
    MTLVertexDescriptor *copy = [[MTLVertexDescriptor allocWithZone:zone] init];
    [copy->_layouts release]; [copy->_attributes release];
    copy->_layouts = [_layouts copy];
    copy->_attributes = [_attributes copy];
    return copy;
}
- (void)dealloc { [_layouts release]; [_attributes release]; [super dealloc]; }
@end

MTL_EXPORT @interface MTLRenderPipelineDescriptor : MTLDescriptorBase
@property(copy) NSString *label;
@property(retain) id vertexFunction, fragmentFunction;
@property(copy) MTLVertexDescriptor *vertexDescriptor;
@property(readonly) MTLDescriptorArray *colorAttachments;
@property NSUInteger sampleCount, rasterSampleCount, depthAttachmentPixelFormat, stencilAttachmentPixelFormat, inputPrimitiveTopology,
    tessellationPartitionMode, maxTessellationFactor, tessellationFactorFormat, tessellationControlPointIndexType, tessellationFactorStepFunction,
    tessellationOutputWindingOrder, maxVertexAmplificationCount, shaderValidation;
@property(getter=isAlphaToCoverageEnabled) BOOL alphaToCoverageEnabled;
@property(getter=isAlphaToOneEnabled) BOOL alphaToOneEnabled;
@property(getter=isRasterizationEnabled) BOOL rasterizationEnabled;
@property(getter=isTessellationFactorScaleEnabled) BOOL tessellationFactorScaleEnabled;
@property BOOL supportIndirectCommandBuffers;
@property(copy) NSArray *binaryArchives;
- (void)reset;
@end
@implementation MTLRenderPipelineDescriptor
+ (NSArray *)_keys { return @[ @"label", @"vertexFunction", @"fragmentFunction", @"vertexDescriptor", @"sampleCount", @"rasterSampleCount",
    @"depthAttachmentPixelFormat", @"stencilAttachmentPixelFormat", @"inputPrimitiveTopology", @"tessellationPartitionMode", @"maxTessellationFactor",
    @"tessellationFactorFormat", @"tessellationControlPointIndexType", @"tessellationFactorStepFunction", @"tessellationOutputWindingOrder",
    @"maxVertexAmplificationCount", @"shaderValidation", @"alphaToCoverageEnabled", @"alphaToOneEnabled", @"rasterizationEnabled",
    @"tessellationFactorScaleEnabled", @"supportIndirectCommandBuffers", @"binaryArchives" ]; }
- (void)_defaults
{
    _colorAttachments = [[MTLDescriptorArray alloc] initWithClass:[MTLRenderPipelineColorAttachmentDescriptor class]];
    _sampleCount = _rasterSampleCount = 1;
    _rasterizationEnabled = YES;
    _maxTessellationFactor = 16;
    _maxVertexAmplificationCount = 1;
}
- (instancetype)init { if ((self = [super init])) [self _defaults]; return self; }
- (void)reset
{
    MTLRenderPipelineDescriptor *fresh = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    for (NSString *key in [[self class] _keys])
        [self setValue:[fresh valueForKey:key] forKey:key];
    [_colorAttachments release];
    _colorAttachments = [[MTLDescriptorArray alloc] initWithClass:[MTLRenderPipelineColorAttachmentDescriptor class]];
}
- (id)copyWithZone:(NSZone *)zone
{
    MTLRenderPipelineDescriptor *copy = [super copyWithZone:zone];
    [copy->_colorAttachments release];
    copy->_colorAttachments = [_colorAttachments copy];
    return copy;
}
- (void)dealloc
{
    [_label release]; [_vertexFunction release]; [_fragmentFunction release]; [_vertexDescriptor release];
    [_colorAttachments release]; [_binaryArchives release];
    [super dealloc];
}
@end

MTL_EXPORT @interface MTLSamplerDescriptor : MTLDescriptorBase
@property NSUInteger minFilter, magFilter, mipFilter, maxAnisotropy, sAddressMode, tAddressMode, rAddressMode, borderColor, compareFunction;
@property BOOL normalizedCoordinates, lodAverage, supportArgumentBuffers;
@property float lodMinClamp, lodMaxClamp, lodBias;
@property(copy) NSString *label;
@end
@implementation MTLSamplerDescriptor
+ (NSArray *)_keys { return @[ @"minFilter", @"magFilter", @"mipFilter", @"maxAnisotropy", @"sAddressMode", @"tAddressMode", @"rAddressMode", @"borderColor",
    @"compareFunction", @"normalizedCoordinates", @"lodAverage", @"supportArgumentBuffers", @"lodMinClamp", @"lodMaxClamp", @"lodBias", @"label" ]; }
- (instancetype)init { if ((self = [super init])) { _maxAnisotropy = 1; _normalizedCoordinates = YES; _lodMaxClamp = 3.402823466e+38F; } return self; }
- (void)dealloc { [_label release]; [super dealloc]; }
@end

typedef struct { uint8_t red, green, blue, alpha; } MTLTextureSwizzleChannels;

MTL_EXPORT @interface MTLTextureDescriptor : MTLDescriptorBase
@property NSUInteger textureType, pixelFormat, width, height, depth, mipmapLevelCount, sampleCount, arrayLength, resourceOptions,
    cpuCacheMode, storageMode, hazardTrackingMode, usage, compressionType, placementSparsePageSize;
@property BOOL allowGPUOptimizedContents;
@property MTLTextureSwizzleChannels swizzle;
+ (MTLTextureDescriptor *)texture2DDescriptorWithPixelFormat:(NSUInteger)format width:(NSUInteger)width height:(NSUInteger)height mipmapped:(BOOL)mipmapped;
+ (MTLTextureDescriptor *)textureCubeDescriptorWithPixelFormat:(NSUInteger)format size:(NSUInteger)size mipmapped:(BOOL)mipmapped;
+ (MTLTextureDescriptor *)textureBufferDescriptorWithPixelFormat:(NSUInteger)format width:(NSUInteger)width resourceOptions:(NSUInteger)options usage:(NSUInteger)usage;
@end
@implementation MTLTextureDescriptor
+ (NSArray *)_keys { return @[ @"textureType", @"pixelFormat", @"width", @"height", @"depth", @"mipmapLevelCount", @"sampleCount", @"arrayLength",
    @"resourceOptions", @"cpuCacheMode", @"storageMode", @"hazardTrackingMode", @"usage", @"compressionType", @"placementSparsePageSize",
    @"allowGPUOptimizedContents" ]; }
- (instancetype)init
{
    if ((self = [super init])) {
        _textureType = 2;                                     // 2D
        _pixelFormat = 70;                                    // RGBA8Unorm
        _width = _height = _depth = _mipmapLevelCount = _sampleCount = _arrayLength = 1;
        _usage = 1;                                           // shader read
        _allowGPUOptimizedContents = YES;
        _swizzle = (MTLTextureSwizzleChannels){ 2, 3, 4, 5 }; // red, green, blue, alpha
    }
    return self;
}
- (id)copyWithZone:(NSZone *)zone { MTLTextureDescriptor *c = [super copyWithZone:zone]; c->_swizzle = _swizzle; return c; }
static NSUInteger mipLevels(NSUInteger w, NSUInteger h)
{
    NSUInteger n = 1, m = w > h ? w : h;
    while (m > 1) { m >>= 1; n++; }
    return n;
}
+ (MTLTextureDescriptor *)texture2DDescriptorWithPixelFormat:(NSUInteger)format width:(NSUInteger)width height:(NSUInteger)height mipmapped:(BOOL)mipmapped
{
    MTLTextureDescriptor *d = [[[self alloc] init] autorelease];
    d.pixelFormat = format; d.width = width; d.height = height;
    d.mipmapLevelCount = mipmapped ? mipLevels(width, height) : 1;
    return d;
}
+ (MTLTextureDescriptor *)textureCubeDescriptorWithPixelFormat:(NSUInteger)format size:(NSUInteger)size mipmapped:(BOOL)mipmapped
{
    MTLTextureDescriptor *d = [self texture2DDescriptorWithPixelFormat:format width:size height:size mipmapped:mipmapped];
    d.textureType = 5;
    return d;
}
+ (MTLTextureDescriptor *)textureBufferDescriptorWithPixelFormat:(NSUInteger)format width:(NSUInteger)width resourceOptions:(NSUInteger)options usage:(NSUInteger)usage
{
    MTLTextureDescriptor *d = [[[self alloc] init] autorelease];
    d.textureType = 9; d.pixelFormat = format; d.width = width; d.resourceOptions = options; d.usage = usage;
    return d;
}
@end

MTL_EXPORT @interface MTLResidencySetDescriptor : MTLDescriptorBase
@property(copy) NSString *label;
@property NSUInteger initialCapacity;
@end
@implementation MTLResidencySetDescriptor
+ (NSArray *)_keys { return @[ @"label", @"initialCapacity" ]; }
- (void)dealloc { [_label release]; [super dealloc]; }
@end

// shared events come from a device; a listener without one only keeps its queue
MTL_EXPORT @interface MTLSharedEventListener : NSObject {
    dispatch_queue_t _queue;
}
- (instancetype)initWithDispatchQueue:(dispatch_queue_t)queue;
@property(readonly) dispatch_queue_t dispatchQueue;
@end
@implementation MTLSharedEventListener
- (instancetype)init { return [self initWithDispatchQueue:dispatch_get_global_queue(0, 0)]; }
- (instancetype)initWithDispatchQueue:(dispatch_queue_t)queue
{
    if ((self = [super init])) {
        _queue = queue;
        dispatch_retain(_queue);
    }
    return self;
}
- (void)dealloc { dispatch_release(_queue); [super dealloc]; }
- (dispatch_queue_t)dispatchQueue { return _queue; }
@end

MTL_EXPORT @interface MTLCaptureDescriptor : MTLDescriptorBase
@property(retain) id captureObject;
@property NSInteger destination;
@property(copy) NSURL *outputURL;
@end
@implementation MTLCaptureDescriptor
+ (NSArray *)_keys { return @[ @"captureObject", @"destination", @"outputURL" ]; }
- (void)dealloc { [_captureObject release]; [_outputURL release]; [super dealloc]; }
@end

// GPU frame capture needs a device and Xcode's capture service: neither exists, and the manager says so
MTL_EXPORT @interface MTLCaptureManager : NSObject
@end
@implementation MTLCaptureManager
+ (MTLCaptureManager *)sharedCaptureManager
{
    static MTLCaptureManager *shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [[self alloc] init]; });
    return shared;
}
- (BOOL)supportsDestination:(NSInteger)destination { return NO; }
- (BOOL)startCaptureWithDescriptor:(MTLCaptureDescriptor *)descriptor error:(NSError **)error
{
    if (error != NULL)
        *error = [NSError errorWithDomain:MTLCaptureErrorDomain code:1 /* MTLCaptureErrorNotSupported */ userInfo:nil];
    return NO;
}
- (void)stopCapture { }
- (BOOL)isCapturing { return NO; }
- (id)defaultCaptureScope { return nil; }
- (void)setDefaultCaptureScope:(id)scope { }
- (id)newCaptureScopeWithDevice:(id)device { return nil; }
- (id)newCaptureScopeWithCommandQueue:(id)queue { return nil; }
@end
