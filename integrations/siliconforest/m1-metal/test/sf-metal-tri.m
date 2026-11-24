// SPDX-License-Identifier: MIT
// sf-metal-tri (SiliconForest m1-metal, ours): one flat triangle through the Metal API (darling-metal's
// Metal.framework over Indium and Vulkan), drawn into a 256x256 RGBA8 texture, copied to a buffer by a
// blit, and compared pixel by pixel with a CPU rasterisation of the same triangle. The shaders are our
// MSL (test/sf-tri.metal, compiled at run time by mslc through newLibraryWithSource:).
// Vertices lie on a 1/8-pixel grid with no pixel centre on an edge, so coverage is exact; the colour
// (1, 0.2, 0.6) converts exactly to (255, 51, 153). Exit 0 and "SFMETAL PASS" when every pixel matches.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { W = 256, H = 256 };

static const char *kSource =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VOut { float4 position [[position]]; };\n"
    "vertex VOut sf_vertex(uint vid [[vertex_id]], constant float2 *pos [[buffer(0)]]) {\n"
    "    VOut o; o.position = float4(pos[vid], 0.0, 1.0); return o; }\n"
    "fragment float4 sf_fragment(VOut in [[stage_in]]) { return float4(1.0, 0.2, 0.6, 1.0); }\n";

// window coordinates (y down, pixels) of the three vertices
static const float kWin[3][2] = { { 32.125f, 24.375f }, { 230.625f, 64.875f }, { 88.375f, 228.125f } };

static float edge(const float *a, const float *b, float x, float y)
{
    return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   // every line out at once: where a run stops is visible
    @autoreleasepool {
        printf("SFMETAL step device\n");
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev == nil) { printf("SFMETAL FAIL no device\n"); return 1; }
        printf("SFMETAL device %s\n", [[dev name] UTF8String]);

        printf("SFMETAL step library\n");
        NSError *err = nil;
        NSString *src = [NSString stringWithUTF8String:kSource];
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (lib == nil) { printf("SFMETAL FAIL library: %s\n", err ? [[err description] UTF8String] : "?"); return 1; }
        id<MTLFunction> vf = [lib newFunctionWithName:@"sf_vertex"];
        id<MTLFunction> ff = [lib newFunctionWithName:@"sf_fragment"];
        if (vf == nil || ff == nil) { printf("SFMETAL FAIL functions %p %p\n", (void *)vf, (void *)ff); return 1; }

        printf("SFMETAL step pipeline\n");
        MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
        [pd setVertexFunction:vf];
        [pd setFragmentFunction:ff];
        [[[pd colorAttachments] objectAtIndexedSubscript:0] setPixelFormat:MTLPixelFormatRGBA8Unorm];
        id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (ps == nil) { printf("SFMETAL FAIL pipeline: %s\n", err ? [[err description] UTF8String] : "?"); return 1; }

        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       width:W height:H mipmapped:NO];
        [td setUsage:MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead];
        [td setStorageMode:MTLStorageModePrivate];
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        id<MTLBuffer> out = [dev newBufferWithLength:W * H * 4 options:MTLResourceStorageModeShared];
        memset([out contents], 0xee, W * H * 4);

        // NDC of the window-space vertices (y up)
        float ndc[3][2];
        for (int i = 0; i < 3; i++) {
            ndc[i][0] = kWin[i][0] / W * 2.0f - 1.0f;
            ndc[i][1] = 1.0f - kWin[i][1] / H * 2.0f;
        }

        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        MTLRenderPassColorAttachmentDescriptor *ca = [[rp colorAttachments] objectAtIndexedSubscript:0];
        [ca setTexture:tex];
        [ca setLoadAction:MTLLoadActionClear];
        [ca setStoreAction:MTLStoreActionStore];
        [ca setClearColor:MTLClearColorMake(0.0, 0.0, 0.0, 1.0)];

        printf("SFMETAL step encode\n");
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:ps];
        [enc setVertexBytes:ndc length:sizeof(ndc) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(W, H, 1) toBuffer:out destinationOffset:0
       destinationBytesPerRow:W * 4 destinationBytesPerImage:W * H * 4];
        [blit endEncoding];
        [cb commit];
        printf("SFMETAL step wait\n");
        [cb waitUntilCompleted];
        printf("SFMETAL step done\n");

        // the CPU reference: a pixel is inside when its centre is on the inner side of all three edges
        static uint8_t ref[W * H * 4];
        float area = edge(kWin[0], kWin[1], kWin[2][0], kWin[2][1]);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float e0 = edge(kWin[0], kWin[1], px, py), e1 = edge(kWin[1], kWin[2], px, py), e2 = edge(kWin[2], kWin[0], px, py);
                int in = area > 0 ? (e0 > 0 && e1 > 0 && e2 > 0) : (e0 < 0 && e1 < 0 && e2 < 0);
                uint8_t *p = ref + (y * W + x) * 4;
                p[0] = in ? 255 : 0; p[1] = in ? 51 : 0; p[2] = in ? 153 : 0; p[3] = 255;
            }
        const uint8_t *img = [out contents];
        int bad = 0, inside = 0, first = -1;
        for (int i = 0; i < W * H; i++) {
            if (ref[i * 4] == 255) inside++;
            if (memcmp(img + i * 4, ref + i * 4, 4)) { if (first < 0) first = i; bad++; }
        }
        printf("SFMETAL image fnv 0x%08x reference fnv 0x%08x, %d of %d pixels differ, %d inside\n",
               fnv(img, W * H * 4), fnv(ref, W * H * 4), bad, W * H, inside);
        if (first >= 0)
            printf("SFMETAL first difference at (%d,%d): %u %u %u %u, reference %u %u %u %u\n", first % W, first / W,
                   img[first * 4], img[first * 4 + 1], img[first * 4 + 2], img[first * 4 + 3],
                   ref[first * 4], ref[first * 4 + 1], ref[first * 4 + 2], ref[first * 4 + 3]);
        FILE *f = fopen("/private/tmp/sf-metal-tri.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int i = 0; i < W * H; i++) fwrite(img + i * 4, 1, 3, f);
            fclose(f);
        }
        printf(bad == 0 ? "SFMETAL PASS\n" : "SFMETAL FAIL\n");
        // A normal return does not end the process here: on the M1 it stayed alive after its last line
        // (first run, 15 November: killed after 200 s). Ended with _exit unless SFMETAL_RETURN is set.
        fflush(stdout);
        if (getenv("SFMETAL_RETURN") == NULL)
            _exit(bad == 0 ? 0 : 1);
        return bad == 0 ? 0 : 1;
    }
}
