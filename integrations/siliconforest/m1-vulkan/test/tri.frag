#version 450
// SPDX-License-Identifier: MIT
layout(location = 0) in vec4 smooth_c;
layout(location = 1) flat in int which;
layout(location = 0) out vec4 o;
void main()
{
	// A: flat (255, 64, 0, 255); B: interpolated
	o = which == 0 ? vec4(1.0, 64.0 / 255.0, 0.0, 1.0) : smooth_c;
}
