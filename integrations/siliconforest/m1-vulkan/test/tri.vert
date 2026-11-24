#version 450
// SPDX-License-Identifier: MIT
// vk-tri's two triangles; pixel coordinates (vk-tri.c, A and B) as NDC = p / 128 - 1 on 256x256
const vec2 P[6] = vec2[](
	vec2(20.25, 30.25), vec2(200.75, 60.5), vec2(90.5, 230.25),
	vec2(180.25, 170.25), vec2(250.5, 200.75), vec2(220.75, 250.25));
const vec4 K[3] = vec4[](vec4(1, 0, 0, 1), vec4(0, 1, 0, 1), vec4(0, 0, 1, 1));
layout(location = 0) out vec4 smooth_c;
layout(location = 1) flat out int which;
void main()
{
	gl_Position = vec4(P[gl_VertexIndex] / 128.0 - 1.0, 0.0, 1.0);
	smooth_c = K[gl_VertexIndex % 3];
	which = gl_VertexIndex / 3;
}
