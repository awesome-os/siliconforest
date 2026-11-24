// SPDX-License-Identifier: MIT
// gl-bench: one off-screen GLES2 workload for comparing GL drivers on the same machine (native
// asahi GL against zink on Honeykrisp). Surfaceless EGL, a 1024x1024 RGBA8 framebuffer object,
// per frame a clear and 64 overlapping opaque rectangles (fill-rate bound: ~40 M pixels a frame),
// glFinish after each frame. The rectangles' corners are whole pixels and their colours exact in
// unorm8, so the final image is the same on every conformant driver: its FNV-1a hash is printed
// and compared with the CPU reference computed here.
//   gl-bench [frames]   (default 200; 5 untimed frames first)
// Driver choice from outside: DYLD_LIBRARY_PATH (which libEGL/libgallium), MESA_LOADER_DRIVER_OVERRIDE=zink.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define S 1024
#define N 64

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_UPTIME_RAW, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void rect(int i, int r[4], uint8_t c[4])
{
	// nested-ish rectangles moving across the frame; colours in steps of 1/255
	int m = (i * 7) % 200;
	r[0] = m; r[1] = (i * 13) % 180; r[2] = S - (i * 5) % 220; r[3] = S - (i * 11) % 240;
	c[0] = (uint8_t)(i * 37); c[1] = (uint8_t)(255 - i * 3); c[2] = (uint8_t)(i * 91); c[3] = 255;
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
	uint32_t h = 0x811c9dc5u;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 0x01000193u;
	return h;
}

int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 200;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
		printf("GLBENCH FAIL no surfaceless EGL display\n");
		return 2;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE };
	EGLConfig cfg;
	EGLint n = 0;
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1) {
		EGLint any[] = { EGL_NONE };
		eglChooseConfig(dpy, any, &cfg, 1, &n);
	}
	EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, n ? cfg : NULL, EGL_NO_CONTEXT, ctxattr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf("GLBENCH FAIL no context (0x%x)\n", eglGetError());
		return 2;
	}
	printf("GLBENCH renderer=\"%s\" version=\"%s\"\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	GLuint tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("GLBENCH FAIL framebuffer incomplete\n");
		return 2;
	}
	const char *vs = "attribute vec2 p; void main() { gl_Position = vec4(p / 512.0 - 1.0, 0.0, 1.0); }";
	const char *fs = "precision mediump float; uniform vec4 c; void main() { gl_FragColor = c; }";
	GLuint prog = glCreateProgram();
	const char *src[2] = { vs, fs };
	GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	for (int i = 0; i < 2; i++) {
		GLuint sh = glCreateShader(type[i]);
		glShaderSource(sh, 1, &src[i], NULL);
		glCompileShader(sh);
		glAttachShader(prog, sh);
	}
	glBindAttribLocation(prog, 0, "p");
	glLinkProgram(prog);
	glUseProgram(prog);
	GLint uc = glGetUniformLocation(prog, "c");
	glViewport(0, 0, S, S);
	glEnableVertexAttribArray(0);

	static float v[N][12];
	for (int i = 0; i < N; i++) {
		int r[4];
		uint8_t c[4];
		rect(i, r, c);
		float q[12] = { r[0], r[1], r[2], r[1], r[0], r[3], r[2], r[1], r[2], r[3], r[0], r[3] };
		memcpy(v[i], q, sizeof q);
	}
	double t0 = 0;
	for (int f = 0; f < frames + 5; f++) {
		if (f == 5)
			t0 = now_ms();
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		for (int i = 0; i < N; i++) {
			int r[4];
			uint8_t c[4];
			rect(i, r, c);
			glUniform4f(uc, c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v[i]);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		glFinish();
	}
	double ms = (now_ms() - t0) / frames;
	static uint8_t px[S * S * 4], ref[S * S * 4];
	glReadPixels(0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++) {
			uint8_t *o = ref + (y * S + x) * 4;
			o[0] = o[1] = o[2] = 0; o[3] = 255;
			for (int i = 0; i < N; i++) {
				int r[4];
				uint8_t c[4];
				rect(i, r, c);
				if (x >= r[0] && x < r[2] && y >= r[1] && y < r[3])
					memcpy(o, c, 4);
			}
		}
	uint32_t got = fnv(px, sizeof px), want = fnv(ref, sizeof ref);
	printf("GLBENCH frames=%d ms_per_frame=%.3f fps=%.1f hash=0x%08x expected=0x%08x %s\n", frames, ms, 1000.0 / ms,
	       got, want, got == want ? "PASS" : "FAIL");
	return got == want ? 0 : 1;
}
