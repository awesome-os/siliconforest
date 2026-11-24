/* SPDX-License-Identifier: MIT */
/* Process-local loader diagnostics. PD libSystem's dlerror() discards dyld's
 * detailed dependency error; query the existing engine directly, without
 * replacing any public loader entry point or installing a core library. */
#include <dlfcn.h>
extern int _dyld_func_lookup(const char *, void **);
static int sf_vlc_module_probe(void) {
    const char *path = getenv("SF_VLC_PREFLIGHT_MODULE");
    if (!path || !*path) return -1;
    void *image = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!image) {
        char *(*engine_error)(void) = NULL;
        const char *message = NULL;
        if (_dyld_func_lookup("__dyld_dlerror", (void **)&engine_error) && engine_error)
            message = engine_error();
        if (!message) message = dlerror();
        fprintf(stderr, "SF_VLC_MODULE load=failed path=%s error=%s\n", path,
                message ? message : "no detailed loader error available");
        return 1;
    }
    fprintf(stderr, "SF_VLC_MODULE load=ok path=%s\n", path);
    /* Finite preflight process exits immediately; don't unload ObjC classes. */
    return 0;
}
