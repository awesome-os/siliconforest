// SPDX-License-Identifier: MIT
// sf-metal-tri's shaders (ours): one flat triangle, its vertices from a buffer
#include <metal_stdlib>
using namespace metal;

struct VOut {
    float4 position [[position]];
};

vertex VOut sf_vertex(uint vid [[vertex_id]], constant float2 *pos [[buffer(0)]]) {
    VOut o;
    o.position = float4(pos[vid], 0.0, 1.0);
    return o;
}

fragment float4 sf_fragment(VOut in [[stage_in]]) {
    return float4(1.0, 0.2, 0.6, 1.0);
}
