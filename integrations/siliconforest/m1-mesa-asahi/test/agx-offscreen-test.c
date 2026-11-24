// SPDX-License-Identifier: MIT
// agx-offscreen-test: an off-screen GLES2 check for the asahi render node, the same source on
// Linux (the reference) and on SiliconForest.
//
//   1. opens the render node, prints drmGetVersion and DRM_IOCTL_ASAHI_GET_PARAMS (group 0);
//   2. creates a GLES2 context on EGL's surfaceless platform, renders into a 256x256 RGBA8 FBO:
//        frame A: clear only                         -> exact bytes, hash fixed by the source;
//        frame B: clear + gradient triangle + textured quad -> sampled with a tolerance, and
//                 compared exactly with --expect-b <sha256> (the same driver's hash on Linux);
//   3. prints SHA-256 of the RGBA bytes of both frames and a few sample pixels.
//
// Exit status: 0 pass, 1 a check failed, 2 could not set up (no node, no EGL, ...).
// A renderer string is not evidence; the pixel checks are.
//
// usage: agx-offscreen-test [--node /dev/dri/renderD128] [--no-drm] [--expect-b <sha256hex>]
//                           [--require-renderer <substring>] [--dump-b file.rgba]

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#define W 256
#define H 256
#define T "agx-offscreen-test: "

// ---- asahi uAPI subset (include/uapi/drm/asahi_drm.h), kept local so the test builds against
// ---- any libdrm.  Mesa 147a62f6190 knows 600 bytes of params; kernel 7.2.11.aurora6 has 616
// ---- (memory_limit, client_memory_limit appended).  The kernel copies min(asked, known).
#define AGX_MAX_CLUSTERS 64
struct agx_params_global {
	uint64_t features;
	uint32_t gpu_generation, gpu_variant, gpu_revision, chip_id;
	uint32_t num_dies, num_clusters_total, num_cores_per_cluster, max_frequency_khz;
	uint64_t core_masks[AGX_MAX_CLUSTERS];
	uint64_t vm_start, vm_end, vm_kernel_min_size;
	uint32_t max_commands_per_submission, max_attachments;
	uint64_t command_timestamp_frequency_hz;
	uint32_t usc_generation, gpu_hal_generation;
	uint64_t memory_limit, client_memory_limit;	// kernel-only tail, zero when not written
};
struct agx_get_params {
	uint32_t param_group, pad;
	uint64_t pointer, size;
};
_Static_assert(sizeof(struct agx_params_global) == 616, "params_global layout");
_Static_assert(sizeof(struct agx_get_params) == 24, "get_params layout");
// DRM_IOW picks the host's encoding: 0x40186440 on Linux, 0x80186440 on Darwin/BSD
#define AGX_IOCTL_GET_PARAMS DRM_IOW(DRM_COMMAND_BASE + 0, struct agx_get_params)

static const char *const feature_names[] = {
	"SOFT_FAULTS", "VM_STATUS", "INDEPENDENT_QUEUES", "SCHEDULED_QUEUES",
	"RENDER_TIMESTAMP_COPIES", "COMPUTE_TIMESTAMP_COPIES", "FRAGMENT_DEPENDENCY",
	"FRAGMENT_DEPENDENCY_COMPUTE", "COMPUTE_WIDE_VISIBILITY", "CACHED_PROGRAM_PROBE",
	"PER_COMMAND_INPUT_SYNCS", "VM_STATUS_MIRROR", "EXACT_PRIOR_BARRIERS", "FEW_PRIMITIVES",
	"FRAGMENT_BARRIERS", "RENDER_RAY_QUERY", "BOUNDED_AS_BUILD",
};

// ---- SHA-256 ----
struct sha256 { uint32_t h[8]; uint8_t buf[64]; uint64_t len; };
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
static void sha256_block(struct sha256 *s, const uint8_t *p)
{
	uint32_t w[64], a[8];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
		uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
		w[i] = w[i-16] + s0 + w[i-7] + s1;
	}
	memcpy(a, s->h, sizeof(a));
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
		s->h[i] += a[i];
}
static void sha256_hex(const uint8_t *data, size_t n, char out[65])
{
	struct sha256 s = { { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 }, { 0 }, 0 };
	uint8_t tail[128] = { 0 };
	size_t full = n / 64, rem = n % 64, tl = rem < 56 ? 64 : 128;
	for (size_t i = 0; i < full; i++)
		sha256_block(&s, data + 64 * i);
	memcpy(tail, data + 64 * full, rem);
	tail[rem] = 0x80;
	for (int i = 0; i < 8; i++)
		tail[tl - 1 - i] = (uint8_t)(((uint64_t)n * 8) >> (8 * i));
	sha256_block(&s, tail);
	if (tl == 128)
		sha256_block(&s, tail + 64);
	for (int i = 0; i < 8; i++)
		sprintf(out + 8 * i, "%08x", s.h[i]);
}

// ---- part 1: the render node ----
static int drm_report(const char *node_arg)
{
	char path[64];
	int fd = -1;

	// SiliconForest numbers render nodes system-wide (renderD128 + provider index), Linux per card
	for (int n = 128; n < 136 && fd < 0; n++) {
		drmVersionPtr v;

		if (node_arg)
			snprintf(path, sizeof(path), "%s", node_arg);
		else
			snprintf(path, sizeof(path), "/dev/dri/renderD%d", n);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			if (node_arg) {
				printf(T "open %s: %s\n", path, strerror(errno));
				return -1;
			}
			continue;
		}
		v = drmGetVersion(fd);
		if (!v) {
			printf(T "%s: drmGetVersion failed: %s\n", path, strerror(errno));
		} else {
			if (node_arg || !strcmp(v->name, "asahi")) {
				printf(T "node %s\n", path);
				printf(T "drm version name=%s %d.%d.%d date=%s desc=\"%s\"\n", v->name,
				    v->version_major, v->version_minor, v->version_patchlevel,
				    v->date ? v->date : "", v->desc ? v->desc : "");
				drmFreeVersion(v);
				break;
			}
			drmFreeVersion(v);
		}
		close(fd);
		fd = -1;
		if (node_arg)
			return -1;
	}
	if (fd < 0) {
		printf(T "no asahi render node under /dev/dri (renderD128..135)\n");
		return -1;
	}

	struct agx_params_global p;
	struct agx_get_params gp = { .param_group = 0, .pointer = (uintptr_t)&p, .size = sizeof(p) };
	memset(&p, 0, sizeof(p));
	if (drmIoctl(fd, AGX_IOCTL_GET_PARAMS, &gp)) {
		printf(T "DRM_IOCTL_ASAHI_GET_PARAMS (0x%08lx) failed: %s\n",
		    (unsigned long)AGX_IOCTL_GET_PARAMS, strerror(errno));
		close(fd);
		return -1;
	}
	printf(T "params ioctl=0x%08lx asked=%zu\n", (unsigned long)AGX_IOCTL_GET_PARAMS, sizeof(p));
	printf(T "params gpu_generation=%u gpu_variant=%c(0x%x) gpu_revision=0x%x chip_id=0x%x\n",
	    p.gpu_generation, p.gpu_variant >= 0x20 && p.gpu_variant < 0x7f ? (int)p.gpu_variant : '?',
	    p.gpu_variant, p.gpu_revision, p.chip_id);
	printf(T "params num_dies=%u num_clusters_total=%u num_cores_per_cluster=%u max_frequency_khz=%u\n",
	    p.num_dies, p.num_clusters_total, p.num_cores_per_cluster, p.max_frequency_khz);
	for (unsigned i = 0; i < AGX_MAX_CLUSTERS; i++)
		if (p.core_masks[i])
			printf(T "params core_masks[%u]=0x%llx\n", i, (unsigned long long)p.core_masks[i]);
	printf(T "params vm_start=0x%llx vm_end=0x%llx vm_kernel_min_size=0x%llx\n",
	    (unsigned long long)p.vm_start, (unsigned long long)p.vm_end,
	    (unsigned long long)p.vm_kernel_min_size);
	printf(T "params max_commands_per_submission=%u max_attachments=%u command_timestamp_frequency_hz=%llu\n",
	    p.max_commands_per_submission, p.max_attachments,
	    (unsigned long long)p.command_timestamp_frequency_hz);
	printf(T "params usc_generation=%u gpu_hal_generation=%u memory_limit=0x%llx client_memory_limit=0x%llx\n",
	    p.usc_generation, p.gpu_hal_generation, (unsigned long long)p.memory_limit,
	    (unsigned long long)p.client_memory_limit);
	printf(T "params features=0x%llx", (unsigned long long)p.features);
	for (unsigned b = 0; b < 64; b++) {
		if (!(p.features >> b & 1))
			continue;
		if (b < sizeof(feature_names) / sizeof(feature_names[0]))
			printf(" %s", feature_names[b]);
		else
			printf(" bit%u", b);
	}
	printf("\n");
	close(fd);
	return 0;
}

// ---- part 2: the scene ----
static const float CLEAR[4] = { 0.2f, 0.4f, 0.6f, 1.0f };	// 51,102,153,255: exact in unorm8
static const uint8_t CLEAR8[4] = { 51, 102, 153, 255 };

// triangle on the left half, NDC; window coordinates are (ndc + 1) * 128
static const float TRI[3][2] = { { -0.875f, -0.75f }, { -0.125f, -0.75f }, { -0.5f, 0.75f } };
// quad on the right half: window x 144..240, y 32..224, a 4x4 nearest texture: 24x48 px per texel
#define QX0 144
#define QX1 240
#define QY0 32
#define QY1 224

static uint8_t texel(int tx, int ty, int ch)
{
	static const uint8_t v[4] = { 0, 85, 170, 255 };
	return ch == 0 ? v[tx] : ch == 1 ? v[ty] : ch == 2 ? v[(tx + ty) & 3] : 255;
}

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf(T "shader compile failed: %s\n", log);
		return 0;
	}
	return s;
}

static GLuint link_program(const char *vs, const char *fs)
{
	GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs), p;
	GLint ok = 0;
	if (!v || !f)
		return 0;
	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glBindAttribLocation(p, 0, "a_pos");
	glBindAttribLocation(p, 1, "a_attr");
	glLinkProgram(p);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		printf(T "program link failed: %s\n", log);
		return 0;
	}
	return p;
}

static const char VS[] =
	"attribute vec2 a_pos;\nattribute vec4 a_attr;\nvarying vec4 v_attr;\n"
	"void main() { v_attr = a_attr; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char FS_COLOR[] =
	"precision mediump float;\nvarying vec4 v_attr;\n"
	"void main() { gl_FragColor = vec4(v_attr.rgb, 1.0); }\n";
static const char FS_TEX[] =
	"precision mediump float;\nvarying vec4 v_attr;\nuniform sampler2D u_tex;\n"
	"void main() { gl_FragColor = texture2D(u_tex, v_attr.xy); }\n";

static const uint8_t *px(const uint8_t *img, int x, int y)
{
	return img + 4 * (y * W + x);
}

static int near(const uint8_t *got, const int want[4], int tol)
{
	for (int i = 0; i < 4; i++)
		if (abs((int)got[i] - want[i]) > tol)
			return 0;
	return 1;
}

static int check(const char *what, const uint8_t *img, int x, int y, const int want[4], int tol)
{
	const uint8_t *g = px(img, x, y);
	int ok = near(g, want, tol);
	printf(T "  %-9s (%3d,%3d) got %3u,%3u,%3u,%3u want %3d,%3d,%3d,%3d tol %d %s\n", what, x, y,
	    g[0], g[1], g[2], g[3], want[0], want[1], want[2], want[3], tol, ok ? "ok" : "MISMATCH");
	return ok;
}

// barycentric colour of the gradient triangle at a pixel centre
static void tri_expect(int x, int y, int want[4])
{
	double p[3][2], l[3];
	double sx = x + 0.5, sy = y + 0.5;
	for (int i = 0; i < 3; i++) {
		p[i][0] = (TRI[i][0] + 1.0) * (W / 2);
		p[i][1] = (TRI[i][1] + 1.0) * (H / 2);
	}
	double den = (p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
	l[0] = ((p[1][1] - p[2][1]) * (sx - p[2][0]) + (p[2][0] - p[1][0]) * (sy - p[2][1])) / den;
	l[1] = ((p[2][1] - p[0][1]) * (sx - p[2][0]) + (p[0][0] - p[2][0]) * (sy - p[2][1])) / den;
	l[2] = 1.0 - l[0] - l[1];
	for (int i = 0; i < 3; i++)
		want[i] = (int)(l[i] * 255.0 + 0.5);
	want[3] = 255;
}

int main(int argc, char **argv)
{
	const char *node = NULL, *expect_b = NULL, *require = NULL, *dump_b = NULL;
	int no_drm = 0, fail = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--node") && i + 1 < argc)
			node = argv[++i];
		else if (!strcmp(argv[i], "--expect-b") && i + 1 < argc)
			expect_b = argv[++i];
		else if (!strcmp(argv[i], "--require-renderer") && i + 1 < argc)
			require = argv[++i];
		else if (!strcmp(argv[i], "--dump-b") && i + 1 < argc)
			dump_b = argv[++i];
		else if (!strcmp(argv[i], "--no-drm"))
			no_drm = 1;
		else {
			printf("usage: %s [--node path] [--no-drm] [--expect-b sha256] "
			    "[--require-renderer substr] [--dump-b file]\n", argv[0]);
			return 2;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (!no_drm && drm_report(node) != 0)
		return 2;

	// EGL, surfaceless: Mesa picks the device itself through drmGetDevices2
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
	    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	static const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLint major, minor, n = 0;
	EGLConfig cfg;
	EGLDisplay dpy;
	EGLContext ctx;

	if (!get_display) {
		printf(T "no eglGetPlatformDisplayEXT\n");
		return 2;
	}
	dpy = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
		printf(T "eglInitialize failed: 0x%x\n", eglGetError());
		return 2;
	}
	printf(T "EGL %d.%d vendor=%s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
	eglBindAPI(EGL_OPENGL_ES_API);
	eglChooseConfig(dpy, (EGLint[]){ EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE }, &cfg, 1, &n);
	ctx = eglCreateContext(dpy, n > 0 ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attribs);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf(T "context failed: 0x%x\n", eglGetError());
		return 2;
	}
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	printf(T "GL_VENDOR=%s\n", (const char *)glGetString(GL_VENDOR));
	printf(T "GL_RENDERER=%s\n", renderer);
	printf(T "GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
	if (require && (!renderer || !strstr(renderer, require))) {
		printf(T "FAIL: renderer does not contain \"%s\"\n", require);
		fail = 1;
	}

	GLuint fbo, color;
	glGenTextures(1, &color);
	glBindTexture(GL_TEXTURE_2D, color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf(T "framebuffer incomplete: 0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
		return 2;
	}
	glViewport(0, 0, W, H);
	glDisable(GL_DITHER);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	static uint8_t a[W * H * 4], b[W * H * 4];
	char hex[65];

	// frame A: clear only
	memset(a, 0xee, sizeof(a));
	glClearColor(CLEAR[0], CLEAR[1], CLEAR[2], CLEAR[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
	{
		static uint8_t want[W * H * 4];
		char want_hex[65];
		size_t bad = 0;
		for (size_t i = 0; i < W * H; i++)
			memcpy(want + 4 * i, CLEAR8, 4);
		for (size_t i = 0; i < W * H; i++)
			bad += memcmp(a + 4 * i, CLEAR8, 4) != 0;
		sha256_hex(a, sizeof(a), hex);
		sha256_hex(want, sizeof(want), want_hex);
		printf(T "frame A (clear) sha256=%s\n", hex);
		printf(T "frame A expected sha256=%s, %zu of %d pixels differ: %s\n", want_hex, bad, W * H,
		    bad ? "FAIL" : "ok");
		printf(T "frame A sample (0,0)=%u,%u,%u,%u (128,128)=%u,%u,%u,%u (255,255)=%u,%u,%u,%u\n",
		    px(a,0,0)[0], px(a,0,0)[1], px(a,0,0)[2], px(a,0,0)[3],
		    px(a,128,128)[0], px(a,128,128)[1], px(a,128,128)[2], px(a,128,128)[3],
		    px(a,255,255)[0], px(a,255,255)[1], px(a,255,255)[2], px(a,255,255)[3]);
		fail |= bad != 0;
	}

	// frame B: clear, gradient triangle, textured quad
	GLuint prog_color = link_program(VS, FS_COLOR), prog_tex = link_program(VS, FS_TEX);
	if (!prog_color || !prog_tex)
		return 1;

	uint8_t tex_data[4 * 4 * 4];
	for (int ty = 0; ty < 4; ty++)
		for (int tx = 0; tx < 4; tx++)
			for (int ch = 0; ch < 4; ch++)
				tex_data[4 * (ty * 4 + tx) + ch] = texel(tx, ty, ch);
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	static const float tri_attr[3][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 } };
	const float qx0 = QX0 / 128.0f - 1.0f, qx1 = QX1 / 128.0f - 1.0f;
	const float qy0 = QY0 / 128.0f - 1.0f, qy1 = QY1 / 128.0f - 1.0f;
	const float quad_pos[4][2] = { { qx0, qy0 }, { qx1, qy0 }, { qx0, qy1 }, { qx1, qy1 } };
	static const float quad_attr[4][4] = { { 0, 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 1, 1, 0, 1 } };

	memset(b, 0xee, sizeof(b));
	glClear(GL_COLOR_BUFFER_BIT);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glUseProgram(prog_color);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, TRI);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, tri_attr);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glUseProgram(prog_tex);
	glUniform1i(glGetUniformLocation(prog_tex, "u_tex"), 0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad_pos);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, quad_attr);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glFinish();
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf(T "FAIL: GL error 0x%x\n", err);
		fail = 1;
	}
	sha256_hex(b, sizeof(b), hex);
	printf(T "frame B (triangle + textured quad) sha256=%s\n", hex);

	int ok = 1, want[4];
	// background: corners and the gap between the two shapes
	for (int i = 0; i < 4; i++)
		want[i] = CLEAR8[i];
	ok &= check("clear", b, 2, 2, want, 0);
	ok &= check("clear", b, 253, 253, want, 0);
	ok &= check("clear", b, 128, 128, want, 0);
	ok &= check("clear", b, 20, 200, want, 0);
	// triangle interior, well inside the edges
	static const int tri_pts[][2] = { { 64, 96 }, { 40, 48 }, { 88, 48 }, { 64, 180 }, { 64, 40 } };
	for (unsigned i = 0; i < sizeof(tri_pts) / sizeof(tri_pts[0]); i++) {
		tri_expect(tri_pts[i][0], tri_pts[i][1], want);
		ok &= check("triangle", b, tri_pts[i][0], tri_pts[i][1], want, 3);
	}
	// quad: the centre of every texel's 24x48 block
	for (int ty = 0; ty < 4; ty++) {
		for (int tx = 0; tx < 4; tx++) {
			for (int ch = 0; ch < 4; ch++)
				want[ch] = texel(tx, ty, ch);
			ok &= check("quad", b, QX0 + 24 * tx + 12, QY0 + 48 * ty + 24, want, 1);
		}
	}
	// coverage: pixels that are not background; the shapes' exact areas are 9216 and 18432
	size_t covered = 0;
	for (size_t i = 0; i < W * H; i++)
		covered += memcmp(b + 4 * i, CLEAR8, 4) != 0;
	int cov_ok = covered >= 27648 - 600 && covered <= 27648 + 600;
	printf(T "  coverage %zu non-background pixels, want 27648 +/- 600 %s\n", covered,
	    cov_ok ? "ok" : "MISMATCH");
	ok &= cov_ok;
	printf(T "frame B tolerance checks: %s\n", ok ? "ok" : "FAIL");
	fail |= !ok;

	if (expect_b) {
		int same = !strcmp(expect_b, hex);
		printf(T "frame B exact hash vs --expect-b: %s\n", same ? "ok" : "FAIL");
		fail |= !same;
	} else {
		printf(T "frame B exact hash not checked (pass --expect-b <sha256 from the Linux asahi run>)\n");
	}
	if (dump_b) {
		FILE *f = fopen(dump_b, "wb");
		if (f) {
			fwrite(b, 1, sizeof(b), f);
			fclose(f);
		}
	}

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	printf(T "%s\n", fail ? "FAIL" : "PASS");
	return fail ? 1 : 0;
}
