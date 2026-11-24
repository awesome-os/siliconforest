// SPDX-License-Identifier: MIT
// agx-panel-demo: GLES3 scenes rendered off-screen on the GPU at panel resolution and shown on
// the built-in panel through the CPU framebuffer mapping (SiliconForest SFGOP), with timings.
// The same source builds on Linux (no panel: the present step writes to a heap buffer of the
// panel's layout, or is skipped with --no-present).
//
// Scenes:  plasma  full-screen procedural fragment shader
//          quads   10000 textured, alpha-blended quads in one draw call
//          mesh    nine lit, rotating tori with a depth buffer
// Modes:   render    draw + glFinish
//          readback  draw + glReadPixels
//          present   draw + glReadPixels + rows into the panel mapping
// Paths:   rgba8   RGBA8 target, canonical orientation; present converts to x2r10g10b10 and flips
//          rgb10   RGB10_A2 target drawn y-flipped with red/blue swapped, so that a
//                  GL_UNSIGNED_INT_2_10_10_10_REV readback is already the panel's word; present
//                  is one copy
//          direct  as rgb10, glReadPixels straight into the panel mapping (present mode only)
//
// Output (stdout), one line per measurement and two hashes per scene:
//   AGXBENCH scene=S mode=M frames=N median_us=.. min_us=.. max_us=.. fps=.. path=P sink=K read_us=.. copy_us=..
//   AGXHASH scene=S target=rgba8|rgb10 frame=7 sha256=...
//   AGXCHECK scene=S differ_px=.. of=.. max_diff=.. flat=0|1 result=ok|FAIL
// AGXCHECK needs no reference: it converts the rgba8 frame the way the present step does and
// compares it with the rgb10 frame of the same scene (flip, channel swap and conversion must all
// agree; a few edge pixels and 8- versus 10-bit blending may differ, at most 2% over 16/1023;
// a frame that is one flat colour fails).
// Frames are a function of the frame index only, never of the clock, so hashes are repeatable.
// Whatever was on the panel (the text console) is saved first and put back after every present
// run and at exit; lines are printed only while the saved image is on the panel.
//
// Diagnostics, for localising a difference between two systems without moving an image
// (coordinates are in readback-buffer space: x from the left, y = row of the buffer, which is the
// bottom-up GL row for rgba8 and the top-down panel row for rgb10):
//   --tiles            AGXTILES: a 16x10 grid of 32-bit tile hashes per fixed frame, one line per
//                      row; AGXSTAT: byte-lane sums and the count of pixels equal to the first;
//                      AGXDIFFHIST: histogram of the rgba8-versus-rgb10 differences
//   --repeat-hash N    AGXREPEAT: the fixed frame N more times in the same process; stable or not
//   --variants         AGXVAR: the rgb10 frame again with glFinish before the readback, and with
//                      a depth buffer of its own; AGXDEPTH: the depth buffer left by each mesh
//                      frame, sampled by 60 depth-tested planes after the frame was read back
//   --fresh            AGXGEM: fresh kernel GEM objects scanned for non-zero bytes (twice, the
//                      first set dirtied and freed); AGXFRESH: never-written GL targets read back
//   --fp-out FILE      write 16x16-block hashes of every fixed frame (the reference side)
//   --fp-ref FILE      compare with such a file: AGXFPDIFF lists differing blocks, AGXPIX dumps
//                      the pixels of the first three
//   --pixels S:T:X:Y   AGXPIX dump of the 16x16 block at X,Y of scene S target T (up to 8 times)
//   --no-bench         fixed frames and diagnostics only
//   --diag             --tiles --repeat-hash 3 --variants
//
// KMS present path (--kms): three linear 32-bit buffers that are GL render targets and KMS
// framebuffers at once, no CPU copy.  Each is a GEM object of the GPU driver (asahi GEM_CREATE on
// the render node), exported as a PRIME fd; the fd is imported into the display's card node
// (ADDFB2 XRGB8888) and into GL (EGL_EXT_image_dma_buf_import, DRM_FORMAT_MOD_LINEAR, EGLImage,
// texture on an FBO).  Per scene: a fixed-frame check (AGXKMSCHECK: glReadPixels of the buffer
// against a CPU mapping of the same buffer and against the same frame rendered into an ordinary
// GL target), then --kms-seconds (default 5) of render, glFinish, atomic page flip, blocking
// read() of the flip-complete event (AGXKMS line).  Nodes are picked by DRM driver name
// ("asahi" render node, "apple" card node).  Every risky call is announced first on stdout and
// on /dev/console ("step:" lines).
//   --kms-first     one buffer, one frame, its check, one modeset commit, exit
//   --kms-noflip    no card node: buffers, import, rendering and checks only (Linux reference)
//
// usage: agx-panel-demo [--size WxH] [--frames N] [--budget seconds] [--scenes plasma,quads,mesh]
//                       [--no-present] [--hold-ms N] [--require-renderer substr] [--dump prefix]

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#ifdef HAVE_SFGOP
#include <SFGOP.h>
#endif

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#define T "agx-panel-demo: "
#define HASH_FRAME 7
#define NQUADS 10000

// ---- SHA-256 ----
static const uint32_t K[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(uint32_t h[8], const uint8_t *p)
{
	uint32_t w[64], a[8];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
	for (int i = 16; i < 64; i++)
		w[i] = w[i-16] + (ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3)) + w[i-7] +
		    (ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10));
	memcpy(a, h, sizeof(a));
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) +
		    ((a[4] & a[5]) ^ (~a[4] & a[6])) + K[i] + w[i];
		uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) +
		    ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
		memmove(a + 1, a, 7 * sizeof(a[0]));
		a[4] += t1;
		a[0] = t1 + t2;
	}
	for (int i = 0; i < 8; i++)
		h[i] += a[i];
}
static void sha256_hex(const uint8_t *data, size_t n, char out[65])
{
	uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
	uint8_t tail[128] = { 0 };
	size_t full = n / 64, rem = n % 64, tl = rem < 56 ? 64 : 128;
	for (size_t i = 0; i < full; i++)
		sha256_block(h, data + 64 * i);
	memcpy(tail, data + 64 * full, rem);
	tail[rem] = 0x80;
	for (int i = 0; i < 8; i++)
		tail[tl - 1 - i] = (uint8_t)(((uint64_t)n * 8) >> (8 * i));
	sha256_block(h, tail);
	if (tl == 128)
		sha256_block(h, tail + 64);
	for (int i = 0; i < 8; i++)
		sprintf(out + 8 * i, "%08x", h[i]);
}

// ---- small helpers ----
static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)	// fixed sequence: the scenes must be identical on every run
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state >> 8;
}
static float frand(void) { return (float)(rng() & 0xffff) / 65535.0f; }

static volatile sig_atomic_t stop_requested;
static void on_signal(int sig) { (void)sig; stop_requested = 1; }

typedef struct { float m[16]; } mat4;	// column-major
static mat4 mat_mul(mat4 a, mat4 b)
{
	mat4 r;
	for (int c = 0; c < 4; c++)
		for (int w = 0; w < 4; w++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a.m[k * 4 + w] * b.m[c * 4 + k];
			r.m[c * 4 + w] = s;
		}
	return r;
}
static mat4 mat_identity(void) { mat4 r = { { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } }; return r; }
static mat4 mat_translate(float x, float y, float z) { mat4 r = mat_identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static mat4 mat_rot(float ax, float ay, float az, float angle)
{
	float c = cosf(angle), s = sinf(angle), l = sqrtf(ax * ax + ay * ay + az * az), t = 1 - c;
	mat4 r = mat_identity();
	ax /= l; ay /= l; az /= l;
	r.m[0] = t*ax*ax + c;    r.m[1] = t*ax*ay + s*az; r.m[2] = t*ax*az - s*ay;
	r.m[4] = t*ax*ay - s*az; r.m[5] = t*ay*ay + c;    r.m[6] = t*ay*az + s*ax;
	r.m[8] = t*ax*az + s*ay; r.m[9] = t*ay*az - s*ax; r.m[10] = t*az*az + c;
	return r;
}
static mat4 mat_perspective(float fovy, float aspect, float n, float f)
{
	float t = 1.0f / tanf(fovy / 2);
	mat4 r = { { 0 } };
	r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
	return r;
}

// ---- GL objects ----
// Every vertex shader ends with gl_Position.y *= u_flip and every fragment shader with
// mix(c, c.bgra, u_bgr): the rgb10 path uses both to produce panel-ready words.
static const char VS_PLASMA[] =
	"#version 300 es\nlayout(location=0) in vec2 a_pos; out vec2 v_uv; uniform float u_flip;\n"
	"void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos.x, a_pos.y * u_flip, 0.0, 1.0); }\n";
static const char FS_PLASMA[] =
	"#version 300 es\nprecision highp float;\nin vec2 v_uv; out vec4 frag; uniform float u_t; uniform float u_bgr; uniform float u_aspect;\n"
	"void main() {\n"
	"  vec2 p = vec2(v_uv.x * u_aspect, v_uv.y);\n"
	"  float v = sin(p.x * 10.0 + u_t) + sin(10.0 * (p.x * sin(u_t * 0.5) + p.y * cos(u_t * 0.33)) + u_t);\n"
	"  vec2 c = p - vec2(0.8 + 0.5 * sin(u_t * 0.2), 0.5 + 0.5 * cos(u_t * 0.33));\n"
	"  v += sin(sqrt(100.0 * dot(c, c) + 1.0) + u_t);\n"
	"  vec3 col = 0.5 + 0.5 * sin(v * 3.14159265 * 0.5 + vec3(0.0, 2.0943951, 4.1887902));\n"
	"  vec4 o = vec4(col * (0.35 + 0.65 * v_uv.y), 1.0);\n"
	"  frag = mix(o, o.bgra, u_bgr);\n}\n";
static const char VS_QUADS[] =
	"#version 300 es\nlayout(location=0) in vec2 a_corner; layout(location=1) in vec2 a_center;\n"
	"layout(location=2) in vec3 a_param; layout(location=3) in vec4 a_color;\n"
	"out vec2 v_uv; out vec4 v_color; uniform float u_t; uniform float u_flip; uniform float u_aspect;\n"
	"void main() {\n"
	"  float ph = a_param.y;\n"
	"  vec2 drift = 0.06 * vec2(cos(u_t * 0.7 + ph), sin(u_t * 0.9 + ph * 1.7));\n"
	"  vec2 p = a_center + drift + (a_corner - 0.5) * a_param.x * vec2(1.0, u_aspect);\n"
	"  float tile = a_param.z;\n"
	"  v_uv = (vec2(mod(tile, 4.0), floor(tile / 4.0)) + a_corner) * 0.25;\n"
	"  v_color = a_color;\n"
	"  gl_Position = vec4(p.x, p.y * u_flip, 0.0, 1.0);\n}\n";
static const char FS_QUADS[] =
	"#version 300 es\nprecision mediump float;\nin vec2 v_uv; in vec4 v_color; out vec4 frag; uniform sampler2D u_tex; uniform float u_bgr;\n"
	"void main() { vec4 o = texture(u_tex, v_uv) * v_color; frag = mix(o, o.bgra, u_bgr); }\n";
static const char VS_MESH[] =
	"#version 300 es\nlayout(location=0) in vec3 a_pos; layout(location=1) in vec3 a_normal;\n"
	"out vec3 v_normal; out vec3 v_eye; uniform mat4 u_mv; uniform mat4 u_proj; uniform float u_flip;\n"
	"void main() {\n"
	"  vec4 e = u_mv * vec4(a_pos, 1.0);\n"
	"  v_eye = e.xyz; v_normal = mat3(u_mv) * a_normal;\n"
	"  gl_Position = u_proj * e; gl_Position.y *= u_flip;\n}\n";
static const char FS_MESH[] =
	"#version 300 es\nprecision highp float;\nin vec3 v_normal; in vec3 v_eye; out vec4 frag; uniform vec3 u_albedo; uniform float u_bgr;\n"
	"void main() {\n"
	"  vec3 n = normalize(v_normal), l = normalize(vec3(0.4, 0.7, 0.6)), v = normalize(-v_eye);\n"
	"  float d = max(dot(n, l), 0.0), s = pow(max(dot(n, normalize(l + v)), 0.0), 48.0);\n"
	"  vec4 o = vec4(u_albedo * (0.12 + 0.88 * d) + vec3(0.6) * s, 1.0);\n"
	"  frag = mix(o, o.bgra, u_bgr);\n}\n";

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf(T "shader compile failed: %s\n", log);
		exit(2);
	}
	return s;
}
static GLuint program(const char *vs, const char *fs)
{
	GLuint p = glCreateProgram();
	GLint ok = 0;
	glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
	glLinkProgram(p);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		printf(T "program link failed: %s\n", log);
		exit(2);
	}
	return p;
}

static int W = 2560, H = 1600;
static GLuint prog_plasma, prog_quads, prog_mesh;
static GLuint vao_plasma, vao_quads, vao_mesh, tex_atlas;
static GLsizei mesh_indices;

static void setup_plasma(void)
{
	static const float tri[] = { -1, -1, 3, -1, -1, 3 };
	GLuint vbo;
	prog_plasma = program(VS_PLASMA, FS_PLASMA);
	glGenVertexArrays(1, &vao_plasma);
	glBindVertexArray(vao_plasma);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void setup_quads(void)
{
	// 4x4 atlas of 64x64 tiles: soft discs, rings, bars and frames, premade with straight alpha
	enum { TS = 64, AS = 256 };
	uint8_t *atlas = malloc(AS * AS * 4);
	for (int ty = 0; ty < 4; ty++)
		for (int tx = 0; tx < 4; tx++) {
			int kind = (ty * 4 + tx) % 4;
			for (int y = 0; y < TS; y++)
				for (int x = 0; x < TS; x++) {
					float u = (x + 0.5f) / TS - 0.5f, v = (y + 0.5f) / TS - 0.5f, r = sqrtf(u * u + v * v), a;
					if (kind == 0) a = r < 0.45f ? 1.0f - r / 0.45f * 0.5f : 0.0f;
					else if (kind == 1) a = fabsf(r - 0.33f) < 0.09f ? 0.9f : 0.0f;
					else if (kind == 2) a = (fabsf(u) < 0.46f && fabsf(v) < 0.46f) ? 0.55f : 0.0f;
					else a = (fabsf(u) < 0.46f && fabsf(v) < 0.46f && (fabsf(u) > 0.36f || fabsf(v) > 0.36f || ((x / 8 + y / 8) & 1))) ? 0.8f : 0.0f;
					uint8_t *p = atlas + 4 * ((ty * TS + y) * AS + tx * TS + x);
					p[0] = (uint8_t)(160 + 95 * (tx / 3.0f));
					p[1] = (uint8_t)(160 + 95 * (ty / 3.0f));
					p[2] = (uint8_t)(255 - 60 * ((tx + ty) / 6.0f));
					p[3] = (uint8_t)(a * 255.0f + 0.5f);
				}
		}
	glGenTextures(1, &tex_atlas);
	glBindTexture(GL_TEXTURE_2D, tex_atlas);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AS, AS, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free(atlas);

	// per vertex: corner(2) centre(2) size,phase,tile(3) colour(4)
	enum { STRIDE = 11 };
	float *v = malloc(sizeof(float) * STRIDE * 4 * NQUADS);
	uint16_t *idx = malloc(sizeof(uint16_t) * 6 * NQUADS);
	for (int q = 0; q < NQUADS; q++) {
		float cx = frand() * 2.2f - 1.1f, cy = frand() * 2.2f - 1.1f;
		float size = 0.03f + 0.07f * frand(), phase = frand() * 6.2831853f, tile = (float)(rng() % 16);
		float col[4] = { 0.4f + 0.6f * frand(), 0.4f + 0.6f * frand(), 0.4f + 0.6f * frand(), 0.35f + 0.5f * frand() };
		for (int k = 0; k < 4; k++) {
			float *o = v + STRIDE * (4 * q + k);
			o[0] = (float)(k & 1); o[1] = (float)(k >> 1);
			o[2] = cx; o[3] = cy; o[4] = size; o[5] = phase; o[6] = tile;
			memcpy(o + 7, col, sizeof(col));
		}
		uint16_t b = (uint16_t)(4 * q), *i = idx + 6 * q;
		i[0] = b; i[1] = b + 1; i[2] = b + 2; i[3] = b + 2; i[4] = b + 1; i[5] = b + 3;
	}
	GLuint vbo, ibo;
	prog_quads = program(VS_QUADS, FS_QUADS);
	glGenVertexArrays(1, &vao_quads);
	glBindVertexArray(vao_quads);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * STRIDE * 4 * NQUADS, v, GL_STATIC_DRAW);
	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * 6 * NQUADS, idx, GL_STATIC_DRAW);
	const GLsizei st = STRIDE * sizeof(float);
	for (int a = 0; a < 4; a++)
		glEnableVertexAttribArray(a);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void *)(2 * sizeof(float)));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void *)(4 * sizeof(float)));
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, st, (void *)(7 * sizeof(float)));
	free(v);
	free(idx);
}

static void setup_mesh(void)
{
	enum { NU = 128, NV = 64 };	// torus: 8192 quads, 16384 triangles
	const float R = 1.0f, r = 0.42f;
	int nverts = (NU + 1) * (NV + 1);
	float *v = malloc(sizeof(float) * 6 * nverts);
	uint16_t *idx = malloc(sizeof(uint16_t) * 6 * NU * NV);
	for (int i = 0; i <= NU; i++)
		for (int j = 0; j <= NV; j++) {
			float a = 6.2831853f * i / NU, b = 6.2831853f * j / NV, *o = v + 6 * (i * (NV + 1) + j);
			o[0] = (R + r * cosf(b)) * cosf(a); o[1] = (R + r * cosf(b)) * sinf(a); o[2] = r * sinf(b);
			o[3] = cosf(b) * cosf(a); o[4] = cosf(b) * sinf(a); o[5] = sinf(b);
		}
	mesh_indices = 0;
	for (int i = 0; i < NU; i++)
		for (int j = 0; j < NV; j++) {
			uint16_t p = (uint16_t)(i * (NV + 1) + j), q = (uint16_t)(p + NV + 1);
			uint16_t *o = idx + mesh_indices;
			o[0] = p; o[1] = q; o[2] = p + 1; o[3] = p + 1; o[4] = q; o[5] = q + 1;
			mesh_indices += 6;
		}
	GLuint vbo, ibo;
	prog_mesh = program(VS_MESH, FS_MESH);
	glGenVertexArrays(1, &vao_mesh);
	glBindVertexArray(vao_mesh);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * nverts, v, GL_STATIC_DRAW);
	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * mesh_indices, idx, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
	free(v);
	free(idx);
}

enum { SCENE_PLASMA, SCENE_QUADS, SCENE_MESH, NSCENES };
static const char *const scene_names[NSCENES] = { "plasma", "quads", "mesh" };

// Draw frame `frame` of a scene into the bound framebuffer. panel != 0 selects the y-flipped,
// red/blue-swapped output of the rgb10 path.
static void draw_scene_ex(int scene, int frame, int yflip, int swap)
{
	const float t = (float)frame / 60.0f, flip = yflip ? -1.0f : 1.0f, bgr = swap ? 1.0f : 0.0f;
	const float aspect = (float)W / (float)H;
	float bg[3] = { 0.05f, 0.07f, 0.12f };
	if (swap) { float s = bg[0]; bg[0] = bg[2]; bg[2] = s; }

	glViewport(0, 0, W, H);
	if (scene == SCENE_PLASMA) {
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glUseProgram(prog_plasma);
		glUniform1f(glGetUniformLocation(prog_plasma, "u_t"), t * 2.0f);
		glUniform1f(glGetUniformLocation(prog_plasma, "u_flip"), flip);
		glUniform1f(glGetUniformLocation(prog_plasma, "u_bgr"), bgr);
		glUniform1f(glGetUniformLocation(prog_plasma, "u_aspect"), aspect);
		glBindVertexArray(vao_plasma);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	} else if (scene == SCENE_QUADS) {
		glClearColor(bg[0], bg[1], bg[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(prog_quads);
		glUniform1f(glGetUniformLocation(prog_quads, "u_t"), t * 3.0f);
		glUniform1f(glGetUniformLocation(prog_quads, "u_flip"), flip);
		glUniform1f(glGetUniformLocation(prog_quads, "u_bgr"), bgr);
		glUniform1f(glGetUniformLocation(prog_quads, "u_aspect"), aspect);
		glUniform1i(glGetUniformLocation(prog_quads, "u_tex"), 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex_atlas);
		glBindVertexArray(vao_quads);
		glDrawElements(GL_TRIANGLES, 6 * NQUADS, GL_UNSIGNED_SHORT, 0);
	} else {
		glClearColor(bg[0], bg[1], bg[2], 1.0f);
		glClearDepthf(1.0f);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUseProgram(prog_mesh);
		mat4 proj = mat_perspective(0.9f, aspect, 0.5f, 50.0f);
		glUniformMatrix4fv(glGetUniformLocation(prog_mesh, "u_proj"), 1, GL_FALSE, proj.m);
		glUniform1f(glGetUniformLocation(prog_mesh, "u_flip"), flip);
		glUniform1f(glGetUniformLocation(prog_mesh, "u_bgr"), bgr);
		glBindVertexArray(vao_mesh);
		for (int i = 0; i < 9; i++) {
			int gx = i % 3 - 1, gy = i / 3 - 1;
			mat4 mv = mat_mul(mat_translate(gx * 3.1f, gy * 2.0f, -7.5f + 0.6f * (float)((i * 5) % 3)),
			    mat_mul(mat_rot(0.3f + gx, 1.0f, 0.2f * gy, t * 1.5f + i), mat_rot(1, 0, 0, t * 0.9f + 0.7f * i)));
			glUniformMatrix4fv(glGetUniformLocation(prog_mesh, "u_mv"), 1, GL_FALSE, mv.m);
			glUniform3f(glGetUniformLocation(prog_mesh, "u_albedo"), 0.35f + 0.2f * (i % 3), 0.3f + 0.2f * ((i + 1) % 3),
			    0.3f + 0.2f * ((i + 2) % 3));
			glDrawElements(GL_TRIANGLES, mesh_indices, GL_UNSIGNED_SHORT, 0);
		}
		glDisable(GL_DEPTH_TEST);
	}
}

static void draw_scene(int scene, int frame, int panel)
{
	draw_scene_ex(scene, frame, panel, panel);
}

// ---- the panel (or a heap stand-in with the same layout) ----
struct sink {
	uint8_t *base;		// first visible pixel of row 0
	uint32_t stride;	// bytes
	const char *kind;	// "panel" or "heap"
	uint8_t *saved;		// console image, visible pixels only (panel)
	uint8_t *row;		// one converted row, so the mapping is only ever written
#ifdef HAVE_SFGOP
	SFGOPFramebuffer fb;
#endif
	int open;
};

static void sink_restore(struct sink *s)
{
	if (!s->saved)
		return;
	for (int y = 0; y < H; y++)
		memcpy(s->base + (size_t)y * s->stride, s->saved + (size_t)y * W * 4, (size_t)W * 4);
}

// RGBA8, bottom row first -> x2r10g10b10, top row first
static void sink_present_rgba8(struct sink *s, const uint8_t *rgba)
{
	uint32_t *row = (uint32_t *)s->row;
	for (int y = 0; y < H; y++) {
		const uint8_t *src = rgba + (size_t)(H - 1 - y) * W * 4;
		for (int x = 0; x < W; x++) {
			uint32_t r = src[4 * x], g = src[4 * x + 1], b = src[4 * x + 2];
			row[x] = ((r << 2 | r >> 6) << 20) | ((g << 2 | g >> 6) << 10) | (b << 2 | b >> 6);
		}
		memcpy(s->base + (size_t)y * s->stride, row, (size_t)W * 4);
	}
}

// already panel words, top row first
static void sink_present_words(struct sink *s, const uint8_t *words)
{
	if (s->stride == (uint32_t)W * 4) {
		memcpy(s->base, words, (size_t)W * 4 * H);
		return;
	}
	for (int y = 0; y < H; y++)
		memcpy(s->base + (size_t)y * s->stride, words + (size_t)y * W * 4, (size_t)W * 4);
}

// ---- measurement ----
enum { MODE_RENDER, MODE_READBACK, MODE_PRESENT };
enum { PATH_RGBA8, PATH_RGB10, PATH_DIRECT };
static const char *const mode_names[] = { "render", "readback", "present" };
static const char *const path_names[] = { "rgba8", "rgb10", "direct" };

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}
static uint64_t median(uint64_t *v, int n)
{
	qsort(v, n, sizeof(*v), cmp_u64);
	return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

static GLuint fbo8, fbo10;
static uint8_t *pixels;		// W*H*4 readback buffer

static void run(int scene, int mode, int path, int frames, uint64_t cap_us, struct sink *s, int hold_ms)
{
	uint64_t *total = calloc(frames, sizeof(uint64_t)), *rd = calloc(frames, sizeof(uint64_t)), *cp = calloc(frames, sizeof(uint64_t));
	const int panel = path != PATH_RGBA8;
	const GLenum type = panel ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE;
	const uint64_t begin = now_us();
	int n = 0;

	glBindFramebuffer(GL_FRAMEBUFFER, panel ? fbo10 : fbo8);
	// one untimed frame: first use of a program/target pair compiles and allocates
	draw_scene(scene, 0, panel);
	glFinish();
	for (int f = 0; f < frames && !stop_requested; f++) {
		uint64_t t0 = now_us(), t1, t2;
		draw_scene(scene, f, panel);
		if (mode == MODE_RENDER) {
			glFinish();
			t1 = t2 = now_us();
		} else if (mode == MODE_PRESENT && path == PATH_DIRECT) {
			glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(s->stride / 4));
			glReadPixels(0, 0, W, H, GL_RGBA, type, s->base);
			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			t1 = t2 = now_us();
		} else {
			glReadPixels(0, 0, W, H, GL_RGBA, type, pixels);
			t1 = t2 = now_us();
			if (mode == MODE_PRESENT) {
				if (panel)
					sink_present_words(s, pixels);
				else
					sink_present_rgba8(s, pixels);
				t2 = now_us();
			}
		}
		total[n] = t2 - t0; rd[n] = t1 - t0; cp[n] = t2 - t1;
		n++;
		if (now_us() - begin > cap_us && n >= 5)
			break;
	}
	GLenum err = glGetError();
	if (mode == MODE_PRESENT && s->saved) {
		if (hold_ms > 0)
			usleep((useconds_t)hold_ms * 1000);
		sink_restore(s);	// console back before anything is printed
	}
	if (n > 0) {
		uint64_t mn = total[0], mx = total[0];
		for (int i = 1; i < n; i++) {
			if (total[i] < mn) mn = total[i];
			if (total[i] > mx) mx = total[i];
		}
		uint64_t med = median(total, n);
		printf("AGXBENCH scene=%s mode=%s frames=%d median_us=%llu min_us=%llu max_us=%llu fps=%.1f path=%s sink=%s read_us=%llu copy_us=%llu%s\n",
		    scene_names[scene], mode_names[mode], n, (unsigned long long)med, (unsigned long long)mn,
		    (unsigned long long)mx, med ? 1e6 / (double)med : 0.0, path_names[path],
		    mode == MODE_PRESENT ? s->kind : "none", (unsigned long long)median(rd, n),
		    (unsigned long long)median(cp, n), err != GL_NO_ERROR ? " gl_error=1" : "");
	}
	free(total); free(rd); free(cp);
}

static int opt_tiles, opt_repeat, opt_variants, opt_fresh, opt_no_bench;

// rgba8 frame (bottom row first) against the panel words of the rgb10 frame (top row first)
static int check_paths(int scene, const uint8_t *rgba, const uint8_t *words)
{
	size_t differ = 0, flat = 1, hist[7] = { 0 };
	unsigned max_diff = 0, wv[6] = { 0 };
	int wx = 0, wy = 0;
	// a frame of one single value is a failed render, however well the two paths agree
	for (size_t i = 1; i < (size_t)W * H && flat; i++)
		flat = memcmp(rgba, rgba + 4 * i, 4) == 0;
	for (int y = 0; y < H; y++) {
		const uint8_t *src = rgba + (size_t)(H - 1 - y) * W * 4;
		const uint32_t *w = (const uint32_t *)(words + (size_t)y * W * 4);
		for (int x = 0; x < W; x++) {
			unsigned worst = 0;
			for (int ch = 0; ch < 3; ch++) {
				unsigned v8 = src[4 * x + ch], a = v8 << 2 | v8 >> 6, b = (w[x] >> (20 - 10 * ch)) & 0x3ff;
				unsigned d = a > b ? a - b : b - a;
				if (d > worst) worst = d;
			}
			if (worst > max_diff) {
				max_diff = worst; wx = x; wy = y;
				for (int ch = 0; ch < 3; ch++) {
					wv[ch] = src[4 * x + ch];
					wv[3 + ch] = (w[x] >> (20 - 10 * ch)) & 0x3ff;
				}
			}
			hist[worst == 0 ? 0 : worst == 1 ? 1 : worst < 4 ? 2 : worst < 8 ? 3 : worst < 16 ? 4 : worst < 64 ? 5 : 6]++;
			differ += worst > 16;
		}
	}
	int ok = differ * 50 <= (size_t)W * H && !flat;
	printf("AGXCHECK scene=%s differ_px=%zu of=%d max_diff=%u flat=%d result=%s worst_at=%d,%d worst_rgba8=%u,%u,%u worst_rgb10=%u,%u,%u\n",
	    scene_names[scene], differ, W * H, max_diff, (int)flat, ok ? "ok" : "FAIL", wx, wy, wv[0], wv[1], wv[2],
	    wv[3], wv[4], wv[5]);
	if (opt_tiles)
		printf("AGXDIFFHIST scene=%s d0=%zu d1=%zu d2_3=%zu d4_7=%zu d8_15=%zu d16_63=%zu d64_up=%zu\n", scene_names[scene],
		    hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6]);
	return ok;
}


// ---- diagnostics ----
#define BLK 16
static FILE *fp_out;
static char *fp_ref;
static struct { const char *scene, *target; int x, y; } pix_req[8];
static int pix_reqs;
static GLuint prog_probe, depth_rb;
static const char *fp_section;	// when set: compare against this section of --fp-ref, write nothing

static uint32_t fnv_block(const uint8_t *px, int x0, int y0, int w, int h)
{
	uint32_t hsh = 2166136261u;
	for (int y = 0; y < h; y++) {
		const uint8_t *r = px + ((size_t)(y0 + y) * W + x0) * 4;
		for (int i = 0; i < w * 4; i++)
			hsh = (hsh ^ r[i]) * 16777619u;
	}
	return hsh;
}

static void dump_block(const char *scene, const char *target, const uint8_t *px, int x0, int y0)
{
	for (int y = y0; y < y0 + BLK && y < H; y++) {
		printf("AGXPIX scene=%s target=%s x=%d y=%d", scene, target, x0, y);
		for (int x = x0; x < x0 + BLK && x < W; x++) {
			uint32_t w;
			memcpy(&w, px + ((size_t)y * W + x) * 4, 4);
			printf(" %08x", w);
		}
		printf("\n");
	}
}

// blocks of BLK x BLK pixels in which two frames differ
static size_t diff_blocks(const uint8_t *a, const uint8_t *b, int bbox[4], int first[2])
{
	size_t n = 0;
	bbox[0] = W; bbox[1] = H; bbox[2] = bbox[3] = -1;
	for (int y = 0; y < H; y += BLK)
		for (int x = 0; x < W; x += BLK) {
			int w = W - x < BLK ? W - x : BLK, h = H - y < BLK ? H - y : BLK, d = 0;
			for (int r = 0; r < h && !d; r++)
				d = memcmp(a + ((size_t)(y + r) * W + x) * 4, b + ((size_t)(y + r) * W + x) * 4, (size_t)w * 4) != 0;
			if (!d)
				continue;
			if (!n) { first[0] = x; first[1] = y; }
			n++;
			if (x < bbox[0]) bbox[0] = x;
			if (y < bbox[1]) bbox[1] = y;
			if (x + w - 1 > bbox[2]) bbox[2] = x + w - 1;
			if (y + h - 1 > bbox[3]) bbox[3] = y + h - 1;
		}
	return n;
}

// everything that describes one fixed frame without the frame itself
static void report_frame(const char *scene, const char *target, const uint8_t *px)
{
	if (opt_tiles) {
		uint64_t lane[4] = { 0 }, same = 0;
		for (int ty = 0; ty < 10; ty++) {
			printf("AGXTILES scene=%s target=%s row=%d", scene, target, ty);
			for (int tx = 0; tx < 16; tx++) {
				int x0 = tx * W / 16, y0 = ty * H / 10;
				printf(" %08x", fnv_block(px, x0, y0, (tx + 1) * W / 16 - x0, (ty + 1) * H / 10 - y0));
			}
			printf("\n");
		}
		for (size_t i = 0; i < (size_t)W * H; i++) {
			for (int l = 0; l < 4; l++)
				lane[l] += px[4 * i + l];
			same += memcmp(px, px + 4 * i, 4) == 0;
		}
		printf("AGXSTAT scene=%s target=%s same_as_first_px=%llu lane_sums=%llu,%llu,%llu,%llu\n", scene, target,
		    (unsigned long long)same, (unsigned long long)lane[0], (unsigned long long)lane[1],
		    (unsigned long long)lane[2], (unsigned long long)lane[3]);
	}
	const int nbx = (W + BLK - 1) / BLK, nby = (H + BLK - 1) / BLK;
	if (fp_out && !fp_section) {
		fprintf(fp_out, "F %s %s %d %d\n", scene, target, W, H);
		for (int by = 0; by < nby; by++) {
			for (int bx = 0; bx < nbx; bx++)
				fprintf(fp_out, "%08x ", fnv_block(px, bx * BLK, by * BLK, W - bx * BLK < BLK ? W - bx * BLK : BLK,
				    H - by * BLK < BLK ? H - by * BLK : BLK));
			fprintf(fp_out, "\n");
		}
	}
	if (fp_ref) {
		char head[128];
		snprintf(head, sizeof(head), "F %s %s %d %d\n", scene, fp_section ? fp_section : target, W, H);
		char *q = strstr(fp_ref, head);
		if (!q) {
			printf("AGXFPDIFF scene=%s target=%s result=no-reference\n", scene, target);
		} else {
			size_t n = 0;
			int bbox[4] = { W, H, -1, -1 }, list[24][2];
			q += strlen(head);
			for (int by = 0; by < nby; by++)
				for (int bx = 0; bx < nbx; bx++) {
					uint32_t ref = (uint32_t)strtoul(q, &q, 16);
					int x = bx * BLK, y = by * BLK;
					if (ref == fnv_block(px, x, y, W - x < BLK ? W - x : BLK, H - y < BLK ? H - y : BLK))
						continue;
					if (n < 24) { list[n][0] = x; list[n][1] = y; }
					n++;
					if (x < bbox[0]) bbox[0] = x;
					if (y < bbox[1]) bbox[1] = y;
					if (x > bbox[2]) bbox[2] = x;
					if (y > bbox[3]) bbox[3] = y;
				}
			printf("AGXFPDIFF scene=%s target=%s blocks_differ=%zu of=%d", scene, target, n, nbx * nby);
			if (n) {
				printf(" bbox=%d,%d-%d,%d blocks=", bbox[0], bbox[1], bbox[2] + BLK - 1, bbox[3] + BLK - 1);
				for (size_t i = 0; i < n && i < 24; i++)
					printf("%s%d,%d", i ? ";" : "", list[i][0], list[i][1]);
			}
			printf("\n");
			for (size_t i = 0; i < n && i < 3; i++)
				dump_block(scene, target, px, list[i][0], list[i][1]);
		}
	}
	for (int i = 0; i < pix_reqs; i++)
		if (!strcmp(pix_req[i].scene, scene) && !strcmp(pix_req[i].target, target) && pix_req[i].x >= 0 &&
		    pix_req[i].y >= 0 && pix_req[i].x < W && pix_req[i].y < H)
			dump_block(scene, target, px, pix_req[i].x, pix_req[i].y);
}

static void capture(int scene, int panel, GLuint fbo, uint8_t *buf, int finish_first)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	draw_scene(scene, HASH_FRAME, panel);
	if (finish_first)
		glFinish();
	glReadPixels(0, 0, W, H, GL_RGBA, panel ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE, buf);
}

static void report_compare(const char *tag, const char *scene, const char *target, const char *what,
    const uint8_t *base, const uint8_t *other)
{
	int bbox[4], first[2] = { 0, 0 };
	size_t n = diff_blocks(base, other, bbox, first);
	printf("%s scene=%s target=%s %s same_as_base=%s blocks_differ=%zu", tag, scene, target, what, n ? "no" : "yes", n);
	if (n)
		printf(" bbox=%d,%d-%d,%d first_block=%d,%d", bbox[0], bbox[1], bbox[2], bbox[3], first[0], first[1]);
	printf("\n");
	if (n) {
		dump_block(scene, "base", base, first[0], first[1]);
		dump_block(scene, "other", other, first[0], first[1]);
	}
}

// What the depth buffer shared by fbo8 and fbo10 holds now, as a picture: 60 full-screen planes
// at window depths between the nearest and the farthest torus are drawn depth-tested (LESS, no
// depth writes) and each adds 4 to every channel, so a pixel reads 4 x (planes in front of the
// stored depth): 0 = nearer than all planes, 240 = behind all of them (background).  It runs
// after the frame was read back, so the depth has to survive a store and a load.
static void depth_probe(GLuint fbo, uint8_t *buf)
{
	static const float tri[] = { -1, -1, 3, -1, -1, 3 };
	static GLuint vao;
	if (!vao) {
		GLuint vbo;
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
		prog_probe = program(
		    "#version 300 es\nlayout(location=0) in vec2 a_pos; uniform float u_z;\n"
		    "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }\n",
		    "#version 300 es\nprecision mediump float;\nout vec4 frag;\nvoid main() { frag = vec4(4.0 / 255.0); }\n");
	}
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, W, H);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glUseProgram(prog_probe);
	glBindVertexArray(vao);
	for (int k = 0; k < 60; k++) {
		glUniform1f(glGetUniformLocation(prog_probe, "u_z"), 0.80f + 0.13f * (float)k / 59.0f);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

static void report_depth(const char *scene, const char *after, const uint8_t *px)
{
	size_t zero = 0, far = 0, odd = 0;
	char hex[65], target[32];
	for (size_t i = 0; i < (size_t)W * H; i++) {
		zero += px[4 * i] == 0;
		far += px[4 * i] == 240;
		odd += (px[4 * i] & 3) != 0 || px[4 * i] > 240;
	}
	sha256_hex(px, (size_t)W * H * 4, hex);
	printf("AGXDEPTH scene=%s after=%s nearest_px=%zu background_px=%zu between_px=%zu unexpected_px=%zu sha256=%s\n",
	    scene, after, zero, far, (size_t)W * H - zero - far, odd, hex);
	snprintf(target, sizeof(target), "depth-after-%s", after);
	report_frame(scene, target, px);
}

static size_t nonzero_bytes(const uint8_t *p, size_t n, size_t *first)
{
	size_t c = 0;
	for (size_t i = 0; i < n; i++)
		if (p[i]) {
			if (!c) *first = i;
			c++;
		}
	return c;
}

// Fresh GEM objects straight from the kernel, with no Mesa in between: are their pages zero?
// Round 1 creates and scans eight 4 MiB objects, fills them with 0xa5 and frees them; round 2
// does it again, which is where a kernel that recycles pages without clearing them shows.
static void fresh_gem(void)
{
	struct { int32_t major, minor, patch; size_t name_len; char *name; size_t date_len; char *date; size_t desc_len; char *desc; } ver;
	struct gem_create { uint64_t size; uint32_t flags, vm_id, handle, pad; };
	struct gem_mmap { uint32_t handle, flags; uint64_t offset; };
	struct gem_close { uint32_t handle, pad; };
	enum { N = 8, SZ = 4 << 20 };
	char path[64], name[32];
	int fd = -1;
	for (int n = 128; n < 136 && fd < 0; n++) {
		snprintf(path, sizeof(path), "/dev/dri/renderD%d", n);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(&ver, 0, sizeof(ver));
		memset(name, 0, sizeof(name));
		ver.name = name;
		ver.name_len = sizeof(name) - 1;
		if (ioctl(fd, _IOWR('d', 0x00, ver), &ver) || strcmp(name, "asahi")) {
			close(fd);
			fd = -1;
		}
	}
	if (fd < 0) {
		printf("AGXGEM result=skipped reason=no-asahi-node\n");
		return;
	}
	for (int round = 1; round <= 2; round++) {
		size_t nonzero = 0, first = 0, firstobj = 0, bytes = 0;
		void *map[N] = { 0 };
		uint32_t handle[N] = { 0 };
		int made = 0;
		for (int i = 0; i < N; i++) {
			struct gem_create c = { .size = SZ };	// shared object: no VM needed
			struct gem_mmap m = { 0 };
			if (ioctl(fd, _IOWR('d', 0x45, struct gem_create), &c))
				break;
			handle[i] = m.handle = c.handle;
			made++;
			if (ioctl(fd, _IOWR('d', 0x46, struct gem_mmap), &m))
				break;
			map[i] = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
			if (map[i] == MAP_FAILED) {
				map[i] = NULL;
				break;
			}
			size_t f = 0, c2 = nonzero_bytes(map[i], SZ, &f);
			if (c2 && !nonzero) { first = f; firstobj = (size_t)i; }
			nonzero += c2;
			bytes += SZ;
		}
		printf("AGXGEM round=%d objects=%d scanned_bytes=%zu nonzero_bytes=%zu", round, made, bytes, nonzero);
		if (nonzero)
			printf(" first=object%zu+0x%zx value=0x%02x", firstobj, first, ((uint8_t *)map[firstobj])[first]);
		printf(" result=%s\n", !bytes ? "no-data" : nonzero ? "NOT-ZEROED" : "zero");
		for (int i = 0; i < N; i++) {
			if (map[i]) {
				memset(map[i], 0xa5, SZ);
				munmap(map[i], SZ);
			}
			if (handle[i]) {
				struct gem_close cl = { .handle = handle[i] };
				ioctl(fd, _IOW('d', 0x09, struct gem_close), &cl);
			}
		}
	}
	close(fd);
}

// A GL target that was never drawn to or uploaded, read back.  Non-zero bytes mean an
// application can see stale memory (from the kernel or from Mesa's buffer cache).  Zero is weak
// evidence: the driver may answer for an uninitialised target without touching its memory.
static void fresh_gl(const char *kind, GLenum internal, GLenum type, int w, int h, uint8_t *buf)
{
	GLuint fbo, tex;
	size_t first = 0, n;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, internal, w, h);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	memset(buf, 0, (size_t)w * h * 4);
	glReadPixels(0, 0, w, h, GL_RGBA, type, buf);
	n = nonzero_bytes(buf, (size_t)w * h * 4, &first);
	printf("AGXFRESH kind=%s size=%dx%d nonzero_bytes=%zu", kind, w, h, n);
	if (n)
		printf(" first_offset=0x%zx value=0x%02x", first, buf[first]);
	printf(" gl_error=0x%x\n", glGetError());
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &tex);
}

// ---- KMS present path ----
static int console_fd = -1;

// stdout and, unbuffered, /dev/console: if the kernel dies in the next call, the last step shows
static void step(const char *fmt, ...)
{
	char line[600];
	va_list ap;
	int n = snprintf(line, sizeof(line), T "step: ");
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - (size_t)n - 2, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 2)
		n = sizeof(line) - 2;
	line[n++] = '\n';
	fwrite(line, 1, (size_t)n, stdout);
	fflush(stdout);
	if (console_fd >= 0)
		(void)!write(console_fd, line, (size_t)n);
}

static int kfail(const char *what)
{
	int e = errno;
	printf("AGXKMS_FAIL call=\"%s\" errno=%d (%s)\n", what, e, strerror(e));
	return 1;
}

static int open_by_driver(const char *prefix, const char *driver, char *path, size_t path_len)
{
	DIR *d = opendir("/dev/dri");
	struct dirent *e;
	int found = -1;
	if (!d)
		return -1;
	while (found < 0 && (e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, prefix, strlen(prefix)))
			continue;
		snprintf(path, path_len, "/dev/dri/%s", e->d_name);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmVersion *v = drmGetVersion(fd);
		printf(T "%s: driver %s\n", path, v ? v->name : "(no version)");
		if (v && !strcmp(v->name, driver))
			found = fd;
		else
			close(fd);
		drmFreeVersion(v);
	}
	closedir(d);
	return found;
}

static void print_words(const char *label, const char *list)
{
	// extension strings are long; 180 columns per line survives the console
	char line[200];
	size_t n = 0;
	for (const char *p = list ? list : ""; ; ) {
		size_t w = strcspn(p, " ");
		if (n && (n + w + 1 > 170 || !*p)) {
			printf(T "%s: %.*s\n", label, (int)n, line);
			n = 0;
		}
		if (!*p)
			break;
		if (w < 160) {
			memcpy(line + n, p, w);
			n += w;
			line[n++] = ' ';
		}
		p += w;
		while (*p == ' ')
			p++;
	}
}

struct kbuf {
	uint32_t rhandle, chandle, fb, pitch;
	size_t size;
	uint8_t *map;	// CPU mapping through the render node
	int prime_fd;
	EGLImageKHR img;
	GLuint tex, fbo;
};

struct kms {
	int fd, rfd, noflip, modeset_done;
	uint32_t crtc_id, plane_id, conn_id, p_conn_crtc, p_mode, p_active, pp[10], mode_blob;
	drmModeModeInfo mode;
	unsigned ebusy;
	uint32_t gl_fourcc;
};

static int kbuf_create(struct kms *k, struct kbuf *b, int index, EGLDisplay dpy)
{
	struct { uint64_t size; uint32_t flags, vm_id, handle, pad; } create = { 0 };
	struct { uint32_t handle, flags; uint64_t offset; } mo = { 0 };
	PFNEGLCREATEIMAGEKHRPROC create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	void (*image_target)(GLenum, void *) = (void (*)(GLenum, void *))eglGetProcAddress("glEGLImageTargetTexture2DOES");

	if (!create_image || !image_target) {
		printf("AGXKMS_FAIL call=\"eglGetProcAddress(eglCreateImageKHR/glEGLImageTargetTexture2DOES)\" errno=0 (missing)\n");
		return 1;
	}
	b->pitch = (uint32_t)W * 4;
	b->size = ((size_t)b->pitch * H + 0x3fff) & ~(size_t)0x3fff;
	b->prime_fd = -1;
	create.size = b->size;
	step("buffer %d: asahi GEM_CREATE %zu bytes on the render node", index, b->size);
	if (drmIoctl(k->rfd, DRM_IOWR(DRM_COMMAND_BASE + 5, create), &create))
		return kfail("DRM_IOCTL_ASAHI_GEM_CREATE (render node)");
	b->rhandle = mo.handle = create.handle;
	if (drmIoctl(k->rfd, DRM_IOWR(DRM_COMMAND_BASE + 6, mo), &mo))
		return kfail("DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET (render node)");
	step("buffer %d: mmap through the render node", index);
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, k->rfd, (off_t)mo.offset);
	if (b->map == MAP_FAILED)
		return kfail("mmap of the GPU buffer (render node)");
	step("buffer %d: PRIME export from the render node (handle %u)", index, b->rhandle);
	if (drmPrimeHandleToFD(k->rfd, b->rhandle, DRM_CLOEXEC | DRM_RDWR, &b->prime_fd))
		return kfail("drmPrimeHandleToFD (render node)");

	// GL side: ARGB8888 first (a renderable format with alpha), XRGB8888 if that is refused.
	// Both describe the same bytes; the display reads them as XRGB8888.
	static const uint32_t fourccs[2] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
	for (int f = 0; f < 2 && !b->img; f++) {
		uint32_t fourcc = k->gl_fourcc ? k->gl_fourcc : fourccs[f];
		const EGLint at[] = {
			EGL_WIDTH, W, EGL_HEIGHT, H, EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc,
			EGL_DMA_BUF_PLANE0_FD_EXT, b->prime_fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
			EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)b->pitch,
			EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(DRM_FORMAT_MOD_LINEAR & 0xffffffff),
			EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(DRM_FORMAT_MOD_LINEAR >> 32), EGL_NONE,
		};
		step("buffer %d: eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, %.4s, linear, pitch %u, fd %d)", index,
		    (const char *)&fourcc, b->pitch, b->prime_fd);
		b->img = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, at);
		if (b->img == EGL_NO_IMAGE_KHR) {
			printf(T "eglCreateImageKHR %.4s failed: EGL error 0x%x\n", (const char *)&fourcc, eglGetError());
			b->img = 0;
			if (k->gl_fourcc)
				break;
		} else {
			k->gl_fourcc = fourcc;
		}
	}
	if (!b->img) {
		printf("AGXKMS_FAIL call=\"eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)\" errno=0 (see EGL error above)\n");
		return 1;
	}
	step("buffer %d: glEGLImageTargetTexture2DOES and framebuffer", index);
	glGenTextures(1, &b->tex);
	glBindTexture(GL_TEXTURE_2D, b->tex);
	image_target(GL_TEXTURE_2D, b->img);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	GLenum err = glGetError();
	glGenFramebuffers(1, &b->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, b->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b->tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (err != GL_NO_ERROR || status != GL_FRAMEBUFFER_COMPLETE) {
		printf("AGXKMS_FAIL call=\"imported texture as framebuffer\" errno=0 (GL error 0x%x, framebuffer status 0x%x)\n", err, status);
		return 1;
	}
	if (!k->noflip) {
		uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
		step("buffer %d: PRIME import into the card node (fd %d)", index, b->prime_fd);
		if (drmPrimeFDToHandle(k->fd, b->prime_fd, &b->chandle))
			return kfail("drmPrimeFDToHandle (card node)");
		handles[0] = b->chandle;
		pitches[0] = b->pitch;
		step("buffer %d: ADDFB2 XRGB8888", index);
		if (drmModeAddFB2(k->fd, (uint32_t)W, (uint32_t)H, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->fb, 0))
			return kfail("drmModeAddFB2 (XRGB8888, card node)");
	}
	printf(T "buffer %d: render handle %u, card handle %u, fb %u, GL format %.4s, texture %u, fbo %u\n", index,
	    b->rhandle, b->chandle, b->fb, (const char *)&k->gl_fourcc, b->tex, b->fbo);
	return 0;
}

static uint32_t kprop(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *value)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;
	if (!props)
		return 0;
	for (uint32_t i = 0; i < props->count_props && !id; i++) {
		drmModePropertyRes *pr = drmModeGetProperty(fd, props->props[i]);
		if (pr && !strcmp(pr->name, name)) {
			id = pr->prop_id;
			if (value)
				*value = props->prop_values[i];
		}
		drmModeFreeProperty(pr);
	}
	drmModeFreeObjectProperties(props);
	if (!id)
		printf(T "object %u has no property %s\n", obj, name);
	return id;
}

// connector, mode, CRTC, primary plane and property ids: the sequence of m1-dcp's dcp-flip-test
static int kms_setup(struct kms *k)
{
	if (drmSetClientCap(k->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
		return kfail("DRM_CLIENT_CAP_UNIVERSAL_PLANES");
	if (drmSetClientCap(k->fd, DRM_CLIENT_CAP_ATOMIC, 1))
		return kfail("DRM_CLIENT_CAP_ATOMIC");
	step("drmModeGetResources");
	drmModeRes *res = drmModeGetResources(k->fd);
	if (!res)
		return kfail("drmModeGetResources");
	drmModeConnector *conn = NULL;
	for (int i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(k->fd, res->connectors[i]);
		if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
			conn = c;
		else
			drmModeFreeConnector(c);
	}
	if (!conn) {
		printf("AGXKMS_FAIL call=\"find a connected connector\" errno=0 (none)\n");
		return 1;
	}
	k->conn_id = conn->connector_id;
	k->mode = conn->modes[0];
	for (int i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
			k->mode = conn->modes[i];
	int crtc_index = -1;
	for (int e = 0; e < conn->count_encoders && !k->crtc_id; e++) {
		drmModeEncoder *enc = drmModeGetEncoder(k->fd, conn->encoders[e]);
		for (int c = 0; enc && c < res->count_crtcs && !k->crtc_id; c++)
			if (enc->possible_crtcs & (1u << c)) {
				k->crtc_id = res->crtcs[c];
				crtc_index = c;
			}
		drmModeFreeEncoder(enc);
	}
	if (!k->crtc_id) {
		printf("AGXKMS_FAIL call=\"find a CRTC\" errno=0 (none)\n");
		return 1;
	}
	step("drmModeGetPlaneResources");
	drmModePlaneRes *planes = drmModeGetPlaneResources(k->fd);
	if (!planes)
		return kfail("drmModeGetPlaneResources");
	for (uint32_t i = 0; i < planes->count_planes && !k->plane_id; i++) {
		drmModePlane *pl = drmModeGetPlane(k->fd, planes->planes[i]);
		uint64_t type = 99;
		if (!pl)
			continue;
		kprop(k->fd, pl->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
		if (type == DRM_PLANE_TYPE_PRIMARY && (pl->possible_crtcs & (1u << crtc_index)))
			k->plane_id = pl->plane_id;
		drmModeFreePlane(pl);
	}
	if (!k->plane_id) {
		printf("AGXKMS_FAIL call=\"find a primary plane\" errno=0 (none)\n");
		return 1;
	}
	static const char *const pn[10] = { "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H" };
	k->p_conn_crtc = kprop(k->fd, k->conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", NULL);
	k->p_mode = kprop(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", NULL);
	k->p_active = kprop(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", NULL);
	int missing = !k->p_conn_crtc || !k->p_mode || !k->p_active;
	for (int i = 0; i < 10; i++)
		missing |= !(k->pp[i] = kprop(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, pn[i], NULL));
	if (missing) {
		printf("AGXKMS_FAIL call=\"look up KMS properties\" errno=0 (one is missing)\n");
		return 1;
	}
	if (drmModeCreatePropertyBlob(k->fd, &k->mode, sizeof(k->mode), &k->mode_blob))
		return kfail("drmModeCreatePropertyBlob (mode)");
	printf(T "display: connector %u, crtc %u, plane %u, mode %ux%u@%u\n", k->conn_id, k->crtc_id, k->plane_id,
	    k->mode.hdisplay, k->mode.vdisplay, k->mode.vrefresh);
	return 0;
}

// Blocks in read() until a flip-complete event arrives; its sequence number in *seq.
static int kms_wait_flip(struct kms *k, uint32_t *seq)
{
	char ev[256];
	for (;;) {
		ssize_t n = read(k->fd, ev, sizeof(ev));
		if (n < 0 && errno == EINTR) {
			if (stop_requested)
				return 1;
			continue;
		}
		if (n < (ssize_t)sizeof(struct drm_event))
			return kfail("read of the DRM event (card node)");
		for (ssize_t off = 0; off + (ssize_t)sizeof(struct drm_event) <= n;) {
			struct drm_event *e = (struct drm_event *)(ev + off);
			if (e->type == DRM_EVENT_FLIP_COMPLETE) {
				if (seq && e->length >= sizeof(struct drm_event_vblank))
					*seq = ((struct drm_event_vblank *)e)->sequence;
				return 0;
			}
			if (e->length < sizeof(*e))
				break;
			off += e->length;
		}
	}
}

// The first commit sets the mode and blocks; later ones are non-blocking flips of FB_ID only.
static int kms_commit(struct kms *k, const struct kbuf *b)
{
	int ret, tries = 0;
	if (!k->modeset_done) {
		drmModeAtomicReq *req = drmModeAtomicAlloc();
		uint64_t pv[10] = { b->fb, k->crtc_id, 0, 0, (uint64_t)k->mode.hdisplay << 16, (uint64_t)k->mode.vdisplay << 16,
			0, 0, k->mode.hdisplay, k->mode.vdisplay };
		drmModeAtomicAddProperty(req, k->conn_id, k->p_conn_crtc, k->crtc_id);
		drmModeAtomicAddProperty(req, k->crtc_id, k->p_mode, k->mode_blob);
		drmModeAtomicAddProperty(req, k->crtc_id, k->p_active, 1);
		for (int i = 0; i < 10; i++)
			drmModeAtomicAddProperty(req, k->plane_id, k->pp[i], pv[i]);
		step("atomic commit with ALLOW_MODESET, fb %u", b->fb);
		ret = drmModeAtomicCommit(k->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT, NULL);
		drmModeAtomicFree(req);
		if (ret)
			return kfail("drmModeAtomicCommit (ALLOW_MODESET | PAGE_FLIP_EVENT)");
		k->modeset_done = 1;
		return 0;
	}
	for (;;) {
		drmModeAtomicReq *req = drmModeAtomicAlloc();
		drmModeAtomicAddProperty(req, k->plane_id, k->pp[0], b->fb);
		ret = drmModeAtomicCommit(k->fd, req, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK, NULL);
		drmModeAtomicFree(req);
		if (!ret || errno != EBUSY || ++tries > 100)
			break;
		k->ebusy++;
		usleep(1000);
	}
	return ret ? kfail("drmModeAtomicCommit (PAGE_FLIP_EVENT | NONBLOCK)") : 0;
}

static void force_alpha(uint8_t *px)
{
	for (size_t i = 0; i < (size_t)W * H; i++)
		px[4 * i + 3] = 255;
}

static const char *eq(size_t blocks) { return blocks ? "different" : "equal"; }

// The fixed frame, drawn y-flipped (buffer row 0 is the top scanline) into the scanout buffer:
// what GL reads back from it, what the CPU sees in the same memory, and the same frame from an
// ordinary GL-allocated RGBA8 target must be the same picture.  Alpha is ignored (X on screen).
static int kms_check(int scene, struct kbuf *b, uint8_t *gl, uint8_t *cpu, uint8_t *ref)
{
	int bbox[4], first[2] = { 0, 0 };
	char hex[65];
	step("%s: fixed frame %d into the scanout buffer, glFinish", scene_names[scene], HASH_FRAME);
	glBindFramebuffer(GL_FRAMEBUFFER, b->fbo);
	draw_scene_ex(scene, HASH_FRAME, 1, 0);
	glFinish();
	step("%s: glReadPixels of the scanout buffer", scene_names[scene]);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, gl);
	force_alpha(gl);
	for (int y = 0; y < H; y++) {
		const uint8_t *src = b->map + (size_t)y * b->pitch;
		uint8_t *dst = cpu + (size_t)y * W * 4;
		for (int x = 0; x < W; x++) {	// memory is B,G,R,X
			dst[4 * x] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1]; dst[4 * x + 2] = src[4 * x]; dst[4 * x + 3] = 255;
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, fbo8);
	draw_scene_ex(scene, HASH_FRAME, 1, 0);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, ref);
	force_alpha(ref);
	size_t d_cpu = diff_blocks(gl, cpu, bbox, first), flat = 1;
	int cb[4] = { bbox[0], bbox[1], bbox[2], bbox[3] }, cf[2] = { first[0], first[1] };
	size_t d_ref = diff_blocks(gl, ref, bbox, first);
	for (size_t i = 1; i < (size_t)W * H && flat; i++)
		flat = memcmp(gl, gl + 4 * i, 4) == 0;
	sha256_hex(gl, (size_t)W * H * 4, hex);
	printf("AGXKMSCHECK scene=%s frame=%d gl_vs_cpu_map=%s blocks_differ=%zu kms_vs_render_path=%s blocks_differ=%zu of=%d flat=%d sha256=%s result=%s\n",
	    scene_names[scene], HASH_FRAME, eq(d_cpu), d_cpu, eq(d_ref), d_ref, ((W + BLK - 1) / BLK) * ((H + BLK - 1) / BLK),
	    (int)flat, hex, d_cpu || d_ref || flat ? "FAIL" : "ok");
	if (d_cpu) {
		printf("AGXKMSDIFF scene=%s what=gl_vs_cpu_map bbox=%d,%d-%d,%d first_block=%d,%d\n", scene_names[scene], cb[0], cb[1], cb[2], cb[3], cf[0], cf[1]);
		dump_block(scene_names[scene], "kms-gl", gl, cf[0], cf[1]);
		dump_block(scene_names[scene], "kms-cpu", cpu, cf[0], cf[1]);
	}
	if (d_ref) {
		printf("AGXKMSDIFF scene=%s what=kms_vs_render_path bbox=%d,%d-%d,%d first_block=%d,%d\n", scene_names[scene], bbox[0], bbox[1], bbox[2], bbox[3], first[0], first[1]);
		dump_block(scene_names[scene], "kms-gl", gl, first[0], first[1]);
		dump_block(scene_names[scene], "render", ref, first[0], first[1]);
	}
	// against another system: section "rgba8flip" of the reference is its render-path frame
	report_frame(scene_names[scene], "rgba8flip", ref);
	fp_section = "rgba8flip";
	report_frame(scene_names[scene], "kms", gl);
	fp_section = NULL;
	return d_cpu || d_ref || flat;
}

static double ms_at(uint64_t *v, int n, double q)
{
	if (n <= 0)
		return 0;
	qsort(v, n, sizeof(*v), cmp_u64);
	int i = (int)(q * (n - 1) + 0.5);
	return (double)v[i] / 1000.0;
}

static int kms_run(EGLDisplay dpy, const char *scenes, double seconds, int noflip, int first_only,
    uint8_t *buf_a, uint8_t *buf_b, uint8_t *buf_c)
{
	enum { NB = 3, MAXF = 8192 };
	static uint64_t render_us[MAXF], wait_us[MAXF], iv_us[MAXF];
	struct kms k = { .fd = -1, .rfd = -1, .noflip = noflip };
	struct kbuf bufs[NB];
	char path[300];
	int failed = 0, front = -1, pending = -1, nbufs = first_only ? 1 : NB;

	memset(bufs, 0, sizeof(bufs));
	console_fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	print_words("EGL client extensions", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
	print_words("EGL display extensions", eglQueryString(dpy, EGL_EXTENSIONS));
	const char *ext = eglQueryString(dpy, EGL_EXTENSIONS), *glext = (const char *)glGetString(GL_EXTENSIONS);
	int has_import = ext && strstr(ext, "EGL_EXT_image_dma_buf_import") != NULL;
	int has_mod = ext && strstr(ext, "EGL_EXT_image_dma_buf_import_modifiers") != NULL;
	int has_oes = glext && strstr(glext, "GL_OES_EGL_image") != NULL;
	printf(T "EGL_EXT_image_dma_buf_import=%d _modifiers=%d EGL_MESA_image_dma_buf_export=%d GL_OES_EGL_image=%d\n", has_import, has_mod,
	    ext && strstr(ext, "EGL_MESA_image_dma_buf_export") != NULL, has_oes);
	if (has_mod) {
		PFNEGLQUERYDMABUFMODIFIERSEXTPROC qmod = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
		static const uint32_t ask[3] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888, DRM_FORMAT_ABGR8888 };
		for (int f = 0; f < 3 && qmod; f++) {
			EGLuint64KHR mods[16];
			EGLBoolean ext_only[16];
			EGLint n = 0;
			if (!qmod(dpy, (EGLint)ask[f], 16, mods, ext_only, &n)) {
				printf(T "dma-buf format %.4s: not importable (EGL error 0x%x)\n", (const char *)&ask[f], eglGetError());
				continue;
			}
			printf(T "dma-buf format %.4s: %d modifiers:", (const char *)&ask[f], n);
			for (int i = 0; i < n && i < 16; i++)
				printf(" 0x%llx%s", (unsigned long long)mods[i], ext_only[i] ? "(external-only)" : "");
			printf("\n");
		}
	}
	if (!has_import || !has_oes) {
		printf("AGXKMS_FAIL call=\"EGL_EXT_image_dma_buf_import / GL_OES_EGL_image\" errno=0 (extension not advertised)\n");
		return 1;
	}

	k.rfd = open_by_driver("renderD", "asahi", path, sizeof(path));
	if (k.rfd < 0) {
		printf("AGXKMS_FAIL call=\"open the asahi render node\" errno=0 (no /dev/dri/renderD* with driver asahi)\n");
		return 1;
	}
	if (!noflip) {
		k.fd = open_by_driver("card", "apple", path, sizeof(path));
		if (k.fd < 0) {
			printf("AGXKMS_FAIL call=\"open the display card node\" errno=0 (no /dev/dri/card* with driver apple)\n");
			return 1;
		}
		if (kms_setup(&k))
			return 1;
		if ((int)k.mode.hdisplay != W || (int)k.mode.vdisplay != H) {
			printf("AGXKMS_FAIL call=\"mode size\" errno=0 (mode %ux%u, render size %dx%d; pass --size)\n",
			    k.mode.hdisplay, k.mode.vdisplay, W, H);
			return 1;
		}
	}
	for (int i = 0; i < nbufs; i++)
		if (kbuf_create(&k, &bufs[i], i, dpy))
			return 1;

	for (int scene = 0; scene < NSCENES && !stop_requested; scene++) {
		if (!strstr(scenes, scene_names[scene]))
			continue;
		// the fixed frame goes into a buffer that is neither on screen nor about to be
		int f = 0;
		while (f == front || f == pending)
			f++;
		failed |= kms_check(scene, &bufs[f], buf_a, buf_b, buf_c);
		if (noflip && first_only)
			break;
		uint64_t begin = now_us(), end = begin + (uint64_t)(seconds * 1e6), last_flip = 0;
		uint32_t seq_first = 0, seq_last = 0, seq = 0;
		int n = 0, nwait = 0, niv = 0, missed = 0, flips = 0;
		unsigned ebusy0 = k.ebusy;
		// the checked frame is shown first; then frames 0.. of the animation
		for (int frame = -1; !stop_requested; frame++) {
			uint64_t t0 = now_us(), t1, t2;
			if (frame >= 0) {
				f = 0;
				while (f == front || f == pending)
					f++;
				glBindFramebuffer(GL_FRAMEBUFFER, bufs[f].fbo);
				draw_scene_ex(scene, frame, 1, 0);
				glFinish();
			}
			t1 = now_us();
			if (frame >= 0 && n < MAXF)
				render_us[n++] = t1 - t0;
			if (noflip) {
				front = f;
			} else {
				if (pending >= 0) {
					if (kms_wait_flip(&k, &seq))
						return 1;
					t2 = now_us();
					if (nwait < MAXF)
						wait_us[nwait++] = t2 - t1;
					if (last_flip && niv < MAXF) {
						iv_us[niv++] = t2 - last_flip;
						missed += t2 - last_flip > 25000;
					}
					if (!flips++)
						seq_first = seq;
					seq_last = seq;
					last_flip = t2;
					front = pending;
					pending = -1;
				}
				if (first_only)
					step("%s: first commit", scene_names[scene]);
				if (kms_commit(&k, &bufs[f]))
					return 1;
				pending = f;
				if (first_only) {
					step("waiting for the flip-complete event");
					if (kms_wait_flip(&k, &seq))
						return 1;
					printf("AGXKMS_FIRST scene=%s result=flipped sequence=%u check=%s\n", scene_names[scene], seq, failed ? "FAIL" : "ok");
					return failed;
				}
			}
			if (now_us() >= end && frame >= 0)
				break;
		}
		double elapsed = (double)(now_us() - begin) / 1e6;
		uint64_t iv_min = 0, iv_max = 0;
		for (int i = 0; i < niv; i++) {
			if (!i || iv_us[i] < iv_min) iv_min = iv_us[i];
			if (iv_us[i] > iv_max) iv_max = iv_us[i];
		}
		double r_med = ms_at(render_us, n, 0.5), r_p99 = ms_at(render_us, n, 0.99);
		printf("AGXKMS scene=%s frames=%d seconds=%.2f fps=%.1f render_ms_median=%.2f render_ms_p99=%.2f flip_wait_ms_median=%.2f missed_vblanks=%d ebusy_retries=%u flips=%d interval_ms_min=%.2f interval_ms_max=%.2f event_sequence_span=%u present=%s\n",
		    scene_names[scene], n, elapsed, elapsed > 0 ? (noflip ? n : flips) / elapsed : 0.0, r_med, r_p99,
		    ms_at(wait_us, nwait, 0.5), missed, k.ebusy - ebusy0, flips, iv_min / 1000.0, iv_max / 1000.0,
		    flips ? seq_last - seq_first : 0, noflip ? "none" : "kms");
	}
	if (pending >= 0 && !noflip)
		kms_wait_flip(&k, NULL);
	GLenum err = glGetError();
	printf("AGXKMS_DONE status=%s gl_error=0x%x\n", stop_requested ? "interrupted" : failed ? "check-failed" : err ? "gl-error" : "ok", err);
	return failed || err != GL_NO_ERROR || stop_requested;
}

static GLuint make_fbo(GLenum internal, GLuint depth)
{
	GLuint fbo, tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, internal, W, H);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf(T "framebuffer 0x%x incomplete: 0x%x\n", internal, glCheckFramebufferStatus(GL_FRAMEBUFFER));
		return 0;
	}
	return fbo;
}

int main(int argc, char **argv)
{
	const char *require = NULL, *scenes = "plasma,quads,mesh", *dump = NULL;
	int frames = 30, budget_s = 50, no_present = 0, hold_ms = 0, size_given = 0;
	int opt_kms = 0, kms_noflip = 0, kms_first = 0;
	double kms_seconds = 5.0;
	struct sink sink = { 0 };

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--size") && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &W, &H) == 2) { size_given = 1; i++; }
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--budget") && i + 1 < argc) budget_s = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--scenes") && i + 1 < argc) scenes = argv[++i];
		else if (!strcmp(argv[i], "--hold-ms") && i + 1 < argc) hold_ms = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--require-renderer") && i + 1 < argc) require = argv[++i];
		else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "--no-present")) no_present = 1;
		else if (!strcmp(argv[i], "--kms")) opt_kms = no_present = 1;
		else if (!strcmp(argv[i], "--kms-noflip")) opt_kms = kms_noflip = no_present = 1;
		else if (!strcmp(argv[i], "--kms-first")) opt_kms = kms_first = no_present = 1;
		else if (!strcmp(argv[i], "--kms-seconds") && i + 1 < argc) kms_seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--tiles")) opt_tiles = 1;
		else if (!strcmp(argv[i], "--variants")) opt_variants = 1;
		else if (!strcmp(argv[i], "--fresh")) opt_fresh = 1;
		else if (!strcmp(argv[i], "--no-bench")) opt_no_bench = 1;
		else if (!strcmp(argv[i], "--diag")) { opt_tiles = opt_variants = 1; opt_repeat = 3; }
		else if (!strcmp(argv[i], "--repeat-hash") && i + 1 < argc) opt_repeat = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--fp-out") && i + 1 < argc) {
			if (!(fp_out = fopen(argv[++i], "w"))) { printf(T "cannot write %s\n", argv[i]); return 2; }
		} else if (!strcmp(argv[i], "--fp-ref") && i + 1 < argc) {
			FILE *f = fopen(argv[++i], "r");
			long len;
			if (!f || fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) || !(fp_ref = calloc(1, (size_t)len + 1)) ||
			    fread(fp_ref, 1, (size_t)len, f) != (size_t)len) { printf(T "cannot read %s\n", argv[i]); return 2; }
			fclose(f);
		} else if (!strcmp(argv[i], "--pixels") && i + 1 < argc && pix_reqs < 8) {
			char *a = strdup(argv[++i]), *c1 = strchr(a, ':'), *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
			if (!c2 || sscanf(c2 + 1, "%d:%d", &pix_req[pix_reqs].x, &pix_req[pix_reqs].y) != 2) { printf(T "bad --pixels\n"); return 2; }
			*c1 = *c2 = 0;
			pix_req[pix_reqs].scene = a;
			pix_req[pix_reqs++].target = c1 + 1;
		}
		else {
			printf("usage: %s [--size WxH] [--frames N] [--budget s] [--scenes plasma,quads,mesh] "
			    "[--no-present] [--hold-ms N] [--require-renderer substr] [--dump prefix]\n"
			    "  diagnostics: [--tiles] [--repeat-hash N] [--variants] [--fresh] [--diag] [--no-bench]\n"
			    "               [--fp-out file] [--fp-ref file] [--pixels scene:target:x:y]\n"
			    "  KMS present: --kms [--kms-seconds N] [--kms-first] [--kms-noflip]\n", argv[0]);
			return 2;
		}
	}
	if (frames < 5 || frames > 10000 || W < 16 || H < 16 || W > 8192 || H > 8192) {
		printf(T "bad --frames or --size\n");
		return 2;
	}
	(void)size_given;
	setvbuf(stdout, NULL, _IOLBF, 0);
	const uint64_t start = now_us(), deadline = start + (uint64_t)budget_s * 1000000u;

#ifdef HAVE_SFGOP
	if (!no_present) {
		kern_return_t kr = SFGOPOpen(&sink.fb);
		if (kr != KERN_SUCCESS) {
			printf(T "SFGOPOpen failed: 0x%x at %s; presenting to a heap buffer\n", kr, SFGOPLastErrorStage());
		} else if (SFGOPGetServiceKind(&sink.fb) != kSFGOPServiceDirectMemory || sink.fb.bpp != 32 ||
		    sink.fb.componentMasks[0] != 0x3ff00000 || sink.fb.componentMasks[1] != 0x000ffc00 ||
		    sink.fb.componentMasks[2] != 0x000003ff || sink.fb.address == 0 || (sink.fb.address & 3) ||
		    sink.fb.stride < sink.fb.width * 4 || (sink.fb.stride & 3) ||
		    (uint64_t)sink.fb.stride * sink.fb.height > sink.fb.size ||
		    (size_given && ((int)sink.fb.width != W || (int)sink.fb.height != H))) {
			printf(T "framebuffer %ux%u stride %u bpp %u masks %08x/%08x/%08x is not a directly mapped "
			    "x2r10g10b10 panel of the requested size; presenting to a heap buffer\n", sink.fb.width,
			    sink.fb.height, sink.fb.stride, sink.fb.bpp, sink.fb.componentMasks[0],
			    sink.fb.componentMasks[1], sink.fb.componentMasks[2]);
			SFGOPClose(&sink.fb);
		} else {
			W = (int)sink.fb.width;
			H = (int)sink.fb.height;
			sink.base = (uint8_t *)(uintptr_t)sink.fb.address;
			sink.stride = sink.fb.stride;
			sink.kind = "panel";
			sink.open = 1;
		}
	}
#endif
	if (!sink.base && !no_present) {
		sink.stride = (uint32_t)W * 4;
		sink.base = calloc((size_t)sink.stride, H);
		sink.kind = "heap";
	}
	pixels = malloc((size_t)W * H * 4);
	uint8_t *pixels8 = malloc((size_t)W * H * 4);
	uint8_t *scratch = malloc((size_t)W * H * 4);
	sink.row = malloc((size_t)W * 4);
	if (!pixels || !pixels8 || !scratch || !sink.row || (!no_present && !sink.base)) {
		printf(T "out of memory\n");
		return 2;
	}
	if (sink.open) {
		// the console image, read once; every later access to the mapping is a write
		sink.saved = malloc((size_t)W * H * 4);
		if (!sink.saved) {
			printf(T "out of memory\n");
			return 2;
		}
		for (int y = 0; y < H; y++)
			memcpy(sink.saved + (size_t)y * W * 4, sink.base + (size_t)y * sink.stride, (size_t)W * 4);
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
	    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	static const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	EGLint major, minor, ncfg = 0;
	EGLConfig cfg;
	EGLDisplay dpy = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
		printf(T "eglInitialize failed: 0x%x\n", eglGetError());
		return 2;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	eglChooseConfig(dpy, (EGLint[]){ EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE }, &cfg, 1, &ncfg);
	EGLContext ctx = eglCreateContext(dpy, ncfg > 0 ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attribs);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf(T "GLES3 context failed: 0x%x\n", eglGetError());
		return 2;
	}
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	printf(T "GL_RENDERER=%s\n", renderer);
	printf(T "GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
	if (require && (!renderer || !strstr(renderer, require))) {
		printf(T "FAIL: renderer does not contain \"%s\"\n", require);
		return 1;
	}

	if (opt_fresh)
		fresh_gem();	// before Mesa has allocated anything of size
	GLuint depth;
	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
	fbo8 = make_fbo(GL_RGBA8, depth);
	fbo10 = make_fbo(GL_RGB10_A2, depth);
	if (!fbo8)
		return 2;
	// the 10-bit path needs the driver to hand RGB10_A2 back as packed 2:10:10:10 words
	int have10 = 0;
	if (fbo10) {
		GLint fmt = 0, type = 0;
		glBindFramebuffer(GL_FRAMEBUFFER, fbo10);
		glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &fmt);
		glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &type);
		have10 = fmt == GL_RGBA && type == GL_UNSIGNED_INT_2_10_10_10_REV;
		printf(T "RGB10_A2 read format 0x%x type 0x%x: 10-bit path %s\n", fmt, type, have10 ? "on" : "off");
	}
	depth_rb = depth;
	glDisable(GL_DITHER);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	if (opt_fresh) {
		// the two render targets themselves have not been written yet
		glBindFramebuffer(GL_FRAMEBUFFER, fbo8);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, scratch);
		size_t first = 0, n = nonzero_bytes(scratch, (size_t)W * H * 4, &first);
		printf("AGXFRESH kind=fbo-rgba8 size=%dx%d nonzero_bytes=%zu first_offset=0x%zx\n", W, H, n, first);
		if (fbo10) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo10);
			glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, scratch);
			n = nonzero_bytes(scratch, (size_t)W * H * 4, &first);
			printf("AGXFRESH kind=fbo-rgb10 size=%dx%d nonzero_bytes=%zu first_offset=0x%zx\n", W, H, n, first);
		}
		fresh_gl("tex-rgba8", GL_RGBA8, GL_UNSIGNED_BYTE, 1000 < W ? 1000 : W, 1000 < H ? 1000 : H, scratch);
		fresh_gl("tex-rgba8", GL_RGBA8, GL_UNSIGNED_BYTE, 64, 64, scratch);
		fresh_gl("tex-rgb10", GL_RGB10_A2, GL_UNSIGNED_INT_2_10_10_10_REV, 1000 < W ? 1000 : W, 1000 < H ? 1000 : H, scratch);
		// the never-cleared depth buffer, seen through the plane probe (0 everywhere = depth 0)
		depth_probe(fbo8, scratch);
		report_depth("none", "fresh", scratch);
	}
	setup_plasma();
	setup_quads();
	setup_mesh();
	printf(T "size=%dx%d frames=%d budget_s=%d sink=%s stride=%u setup_ms=%llu\n", W, H, frames, budget_s,
	    no_present ? "none" : sink.kind, sink.stride, (unsigned long long)(now_us() - start) / 1000);
	if (opt_kms) {
		int rc = kms_run(dpy, scenes, kms_seconds, kms_noflip, kms_first, pixels, scratch, pixels8);
		if (fp_out)
			fclose(fp_out);
		printf("AGXBENCH_DONE status=%s skipped=0 seconds=%.1f\n", rc ? "kms-failed" : "ok", (double)(now_us() - start) / 1e6);
		return rc ? 1 : 0;
	}

	static const struct { int mode, path; } plan[] = {
		{ MODE_RENDER, PATH_RGBA8 }, { MODE_READBACK, PATH_RGBA8 }, { MODE_PRESENT, PATH_RGBA8 },
		{ MODE_RENDER, PATH_RGB10 }, { MODE_READBACK, PATH_RGB10 }, { MODE_PRESENT, PATH_RGB10 },
		{ MODE_PRESENT, PATH_DIRECT },
	};
	int skipped = 0, checks_failed = 0;
	for (int scene = 0; scene < NSCENES && !stop_requested; scene++) {
		char hex[65];
		if (!strstr(scenes, scene_names[scene]))
			continue;
		// regression hashes: frame HASH_FRAME on both targets, before any timing
		glBindFramebuffer(GL_FRAMEBUFFER, fbo8);
		draw_scene(scene, HASH_FRAME, 0);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels8);
		sha256_hex(pixels8, (size_t)W * H * 4, hex);
		if (dump) {
			char name[512];
			snprintf(name, sizeof(name), "%s-%s.ppm", dump, scene_names[scene]);
			FILE *f = fopen(name, "wb");
			if (f) {
				fprintf(f, "P6\n%d %d\n255\n", W, H);
				for (int y = H - 1; y >= 0; y--)
					for (int x = 0; x < W; x++)
						fwrite(pixels8 + ((size_t)y * W + x) * 4, 1, 3, f);
				fclose(f);
			}
		}
		printf("AGXHASH scene=%s target=rgba8 frame=%d size=%dx%d sha256=%s\n", scene_names[scene], HASH_FRAME, W, H, hex);
		report_frame(scene_names[scene], "rgba8", pixels8);
		if (have10) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo10);
			draw_scene(scene, HASH_FRAME, 1);
			glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels);
			sha256_hex(pixels, (size_t)W * H * 4, hex);
			printf("AGXHASH scene=%s target=rgb10 frame=%d size=%dx%d sha256=%s\n", scene_names[scene], HASH_FRAME, W, H, hex);
			checks_failed += !check_paths(scene, pixels8, pixels);
			report_frame(scene_names[scene], "rgb10", pixels);
		}
		// Diagnostics come after both fixed frames, so those are produced exactly as without them.
		for (int panel = 0; panel <= have10 && opt_repeat > 0; panel++) {
			const uint8_t *base = panel ? pixels : pixels8;
			int bad = 0, shown = 0;
			for (int r = 0; r < opt_repeat; r++) {
				capture(scene, panel, panel ? fbo10 : fbo8, scratch, 0);
				if (!memcmp(base, scratch, (size_t)W * H * 4))
					continue;
				bad++;
				if (!shown++) {
					char what[32];
					snprintf(what, sizeof(what), "repeat=%d", r + 1);
					report_compare("AGXREPEATDIFF", scene_names[scene], panel ? "rgb10" : "rgba8", what, base, scratch);
				}
			}
			printf("AGXREPEAT scene=%s target=%s n=%d differing_runs=%d stable=%s\n", scene_names[scene],
			    panel ? "rgb10" : "rgba8", opt_repeat, bad, bad ? "no" : "yes");
		}
		if (opt_variants && have10) {
			static GLuint fbo10own;
			capture(scene, 1, fbo10, scratch, 1);
			report_compare("AGXVAR", scene_names[scene], "rgb10", "variant=finish-before-read", pixels, scratch);
			if (!fbo10own) {
				GLuint d2;
				glGenRenderbuffers(1, &d2);
				glBindRenderbuffer(GL_RENDERBUFFER, d2);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
				fbo10own = make_fbo(GL_RGB10_A2, d2);
			}
			if (fbo10own) {
				capture(scene, 1, fbo10own, scratch, 0);
				report_compare("AGXVAR", scene_names[scene], "rgb10", "variant=own-depth-buffer", pixels, scratch);
			}
		}
		if (opt_variants && scene == SCENE_MESH) {
			capture(scene, 0, fbo8, scratch, 0);
			depth_probe(fbo8, scratch);
			report_depth(scene_names[scene], "rgba8", scratch);
			if (have10) {
				capture(scene, 1, fbo10, scratch, 0);
				depth_probe(fbo8, scratch);
				report_depth(scene_names[scene], "rgb10", scratch);
			}
		}
		for (unsigned k = 0; k < sizeof(plan) / sizeof(plan[0]) && !stop_requested && !opt_no_bench; k++) {
			if (plan[k].path != PATH_RGBA8 && !have10)
				continue;
			if (plan[k].mode == MODE_PRESENT && no_present)
				continue;
			if (now_us() > deadline) {
				printf("AGXBENCH_SKIP scene=%s mode=%s path=%s reason=budget\n", scene_names[scene],
				    mode_names[plan[k].mode], path_names[plan[k].path]);
				skipped++;
				continue;
			}
			run(scene, plan[k].mode, plan[k].path, frames, 2000000, &sink, hold_ms);
		}
	}
	GLenum err = glGetError();
	if (fp_out)
		fclose(fp_out);
	sink_restore(&sink);
#ifdef HAVE_SFGOP
	if (sink.open)
		SFGOPClose(&sink.fb);
#endif
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	printf("AGXBENCH_DONE status=%s skipped=%d seconds=%.1f\n",
	    stop_requested ? "interrupted" : err != GL_NO_ERROR ? "gl-error" : checks_failed ? "check-failed" : "ok",
	    skipped, (double)(now_us() - start) / 1e6);
	return stop_requested || err != GL_NO_ERROR || checks_failed ? 1 : 0;
}
