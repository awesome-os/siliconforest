// SPDX-License-Identifier: MIT
// /usr/bin/open: start an application bundle on the SiliconForest desktop, as macOS's open(1)
// does for the forms people type:
//   open <path to .app>             open -a <name or path> [--args <arguments...>]
//   open -a <application> <document...>   open <document...>
//   open -n ...  (accepted: every open starts a new instance here)
// Documents (files, folders) go to the application as its NSOpen default (-NSOpen <path>, a list
// for several), which AppKit hands to the application delegate's application:openFile(s):, the
// way macOS hands them over in an Apple Event: `open -a Terminal <folder>` opens a window there,
// `open x.command` runs it in Terminal. Without -a the application is the one in the application
// directories whose CFBundleDocumentTypes name the document's extension (folders: none).
// The bundle's executable (CFBundleExecutable, read by CFBundle) is started in a session of its
// own with the desktop's environment (the compositor's socket: XDG_RUNTIME_DIR and
// WAYLAND_DISPLAY, taken from the caller or the session's defaults), detached from the terminal:
// open returns at once with status 0, or says why the bundle could not be started. What the
// application writes goes to ~/Library/Logs/<name>.log.
//   open --listen <fifo>            (SiliconForest's, for the session: the desktop menu's
//                                   "Applications" rows) makes the named pipe and opens each bundle
//                                   whose path arrives on it as a line, until it is killed. The
//                                   compositor starts no program itself: it writes the path, the
//                                   session's listener starts the application. Only bundles in the
//                                   application directories are opened this way.
// HTTP(S) URLs use LaunchServices' selected handler, passed as literal argv.
// Not macOS's: other URL schemes, a running instance taking the documents (there are no Apple Events), -b, -e,
// -t, -W, -g.
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
// (CODEX-FX's system-patches/0001, merged by TERM8) HTTP(S) URLs go to LaunchServices' handler for the scheme,
// LSCopyDefaultApplicationURLForURL of SiliconForest's CoreServices; CoreServices is loaded only when a URL is
// opened, so every other use of open (the session's listener, open -a) loads what it did before.
#include <dlfcn.h>
typedef CFURLRef (*LSCopyDefaultApplicationURLForURLFn)(CFURLRef, UInt32, CFErrorRef *);

static CFURLRef copy_default_application(CFURLRef url)
{
    static LSCopyDefaultApplicationURLForURLFn fn;
    if (fn == NULL) {
        void *cs = dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY | RTLD_LOCAL);
        fn = cs ? (LSCopyDefaultApplicationURLForURLFn)dlsym(cs, "LSCopyDefaultApplicationURLForURL") : NULL;
        if (fn == NULL) { fprintf(stderr, "open: CoreServices' LSCopyDefaultApplicationURLForURL is not available\n"); return NULL; }
    }
    return fn(url, (UInt32)-1, NULL);
}

static int is_web_url(const char *value)
{
    return strncasecmp(value, "https://", 8) == 0 || strncasecmp(value, "http://", 7) == 0;
}

static int app_for_web_url(const char *text, char *out, size_t size)
{
    CFStringRef value = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    CFURLRef url = value ? CFURLCreateWithString(NULL, value, NULL) : NULL;
    CFURLRef app = url ? copy_default_application(url) : NULL;
    int found = app && CFURLGetFileSystemRepresentation(app, true, (UInt8 *)out, size);
    if (app) CFRelease(app);
    if (url) CFRelease(url);
    if (value) CFRelease(value);
    return found;
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

// <name> or <name>.app in the application directories, or a path as it is
static int find_bundle(const char *name, char *out, size_t size)
{
    static const char *dirs[] = { "/Applications", "/Applications/Utilities", "/System/Applications", "/System/Applications/Utilities", NULL };
    const char *home = getenv("HOME");
    if (strchr(name, '/')) { snprintf(out, size, "%s", name); return is_dir(out); }
    const char *suffix = strlen(name) > 4 && strcmp(name + strlen(name) - 4, ".app") == 0 ? "" : ".app";
    for (int i = 0; dirs[i]; i++) { snprintf(out, size, "%s/%s%s", dirs[i], name, suffix); if (is_dir(out)) return 1; }
    if (home) { snprintf(out, size, "%s/Applications/%s%s", home, name, suffix); if (is_dir(out)) return 1; }
    return 0;
}

static int open_bundle(const char *target, char **args, int nargs);

// the session's listener: one bundle path per line on a named pipe
static int listen_on(const char *fifo)
{
    static const char *dirs[] = { "/Applications/", "/System/Applications/", NULL };
    unlink(fifo);
    if (mkfifo(fifo, 0600) != 0) { fprintf(stderr, "open: %s: %s\n", fifo, strerror(errno)); return 1; }
    // read and write: the pipe stays open between writers, a read never sees an end
    int fd = open(fifo, O_RDWR | O_CLOEXEC);
    FILE *in = fd >= 0 ? fdopen(fd, "r") : NULL;
    if (in == NULL) { fprintf(stderr, "open: %s: %s\n", fifo, strerror(errno)); return 1; }
    char line[PATH_MAX], real[PATH_MAX];
    while (fgets(line, sizeof(line), in)) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') continue;
        line[n - 1] = 0;
        int ok = 0;
        if (realpath(line, real) != NULL && strlen(real) > 4 && strcmp(real + strlen(real) - 4, ".app") == 0)
            for (int i = 0; dirs[i]; i++) if (strncmp(real, dirs[i], strlen(dirs[i])) == 0) ok = 1;
        if (!ok) { fprintf(stderr, "open: not an application bundle of the application directories: %s\n", line); continue; }
        // a child starts it and ends, so the application is nobody's to wait for here
        pid_t pid = fork();
        if (pid == 0) _exit(open_bundle(real, NULL, 0));
        if (pid > 0) { int st; waitpid(pid, &st, 0); }
    }
    return 0;
}

static int is_bundle(const char *p) { size_t n = strlen(p); while (n > 1 && p[n - 1] == '/') n--; return n > 4 && strncmp(p + n - 4, ".app", 4) == 0 && is_dir(p); }

// the application in the application directories whose CFBundleDocumentTypes list this extension
static int app_for_document(const char *doc, char *out, size_t size)
{
    static const char *dirs[] = { "/Applications", "/Applications/Utilities", "/System/Applications", "/System/Applications/Utilities", NULL };
    const char *dot = strrchr(doc, '.'), *slash = strrchr(doc, '/');
    if (dot == NULL || (slash && dot < slash) || is_dir(doc)) return 0;
    CFStringRef ext = CFStringCreateWithCString(NULL, dot + 1, kCFStringEncodingUTF8);
    int found = 0;
    for (int i = 0; dirs[i] && !found; i++) {
        CFURLRef dir = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)dirs[i], (CFIndex)strlen(dirs[i]), true);
        CFArrayRef bundles = dir ? CFBundleCreateBundlesFromDirectory(NULL, dir, CFSTR("app")) : NULL;
        for (CFIndex b = 0; bundles && b < CFArrayGetCount(bundles) && !found; b++) {
            CFBundleRef bundle = (CFBundleRef)CFArrayGetValueAtIndex(bundles, b);
            CFArrayRef types = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleDocumentTypes"));
            // the type identifiers the bundle declares for this extension (UTExportedTypeDeclarations,
            // UTImportedTypeDeclarations: Terminal's .command is com.apple.terminal.shell-script)
            CFMutableArrayRef utis = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
            CFStringRef declKeys[] = { CFSTR("UTExportedTypeDeclarations"), CFSTR("UTImportedTypeDeclarations") };
            for (int k = 0; k < 2; k++) {
                CFArrayRef decls = CFBundleGetValueForInfoDictionaryKey(bundle, declKeys[k]);
                for (CFIndex d = 0; decls && CFGetTypeID(decls) == CFArrayGetTypeID() && d < CFArrayGetCount(decls); d++) {
                    CFDictionaryRef decl = CFArrayGetValueAtIndex(decls, d), tags;
                    CFStringRef uti;
                    CFArrayRef exts;
                    if (CFGetTypeID(decl) != CFDictionaryGetTypeID()) continue;
                    uti = CFDictionaryGetValue(decl, CFSTR("UTTypeIdentifier"));
                    tags = CFDictionaryGetValue(decl, CFSTR("UTTypeTagSpecification"));
                    exts = tags && CFGetTypeID(tags) == CFDictionaryGetTypeID() ? CFDictionaryGetValue(tags, CFSTR("public.filename-extension")) : NULL;
                    if (exts && CFGetTypeID(exts) == CFStringGetTypeID()) {
                        if (uti && CFStringCompare((CFStringRef)exts, ext, kCFCompareCaseInsensitive) == kCFCompareEqualTo) CFArrayAppendValue(utis, uti);
                    } else
                        for (CFIndex e = 0; uti && exts && CFGetTypeID(exts) == CFArrayGetTypeID() && e < CFArrayGetCount(exts); e++)
                            if (CFStringCompare(CFArrayGetValueAtIndex(exts, e), ext, kCFCompareCaseInsensitive) == kCFCompareEqualTo) CFArrayAppendValue(utis, uti);
                }
            }
            for (CFIndex t = 0; types && CFGetTypeID(types) == CFArrayGetTypeID() && t < CFArrayGetCount(types) && !found; t++) {
                CFDictionaryRef type = CFArrayGetValueAtIndex(types, t);
                if (CFGetTypeID(type) != CFDictionaryGetTypeID()) continue;
                CFArrayRef exts = CFDictionaryGetValue(type, CFSTR("CFBundleTypeExtensions"));
                CFArrayRef contents = CFDictionaryGetValue(type, CFSTR("LSItemContentTypes"));
                int match = 0;
                for (CFIndex e = 0; exts && CFGetTypeID(exts) == CFArrayGetTypeID() && e < CFArrayGetCount(exts); e++)
                    if (CFStringCompare(CFArrayGetValueAtIndex(exts, e), ext, kCFCompareCaseInsensitive) == kCFCompareEqualTo) match = 1;
                for (CFIndex c = 0; !match && contents && CFGetTypeID(contents) == CFArrayGetTypeID() && c < CFArrayGetCount(contents); c++)
                    if (CFArrayContainsValue(utis, CFRangeMake(0, CFArrayGetCount(utis)), CFArrayGetValueAtIndex(contents, c))) match = 1;
                if (match) {
                    CFURLRef url = CFBundleCopyBundleURL(bundle);
                    found = url && CFURLGetFileSystemRepresentation(url, true, (UInt8 *)out, (CFIndex)size);
                    if (url) CFRelease(url);
                }
            }
            CFRelease(utis);
        }
        if (bundles) CFRelease(bundles);
        if (dir) CFRelease(dir);
    }
    CFRelease(ext);
    return found;
}

int main(int argc, char **argv)
{
    const char *target = NULL; char **args = NULL; int nargs = 0;
    char **docs = calloc((size_t)argc, sizeof(char *)); int ndocs = 0;
    char **urls = calloc((size_t)argc, sizeof(char *)); int nurls = 0;
    if (argc == 3 && strcmp(argv[1], "--listen") == 0) return listen_on(argv[2]);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--args") == 0) { args = argv + i + 1; nargs = argc - i - 1; break; }
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) target = argv[++i];
        else if (strcmp(argv[i], "-n") == 0) continue;
        else if (argv[i][0] == '-') { fprintf(stderr, "open: option %s is not supported here\n", argv[i]); return 1; }
        else if (is_web_url(argv[i])) urls[nurls++] = argv[i];
        else if (target == NULL && ndocs == 0 && is_bundle(argv[i])) target = argv[i];
        else {
            char real[PATH_MAX];
            if (realpath(argv[i], real) == NULL) { fprintf(stderr, "open: %s: %s\n", argv[i], strerror(errno)); return 1; }
            docs[ndocs++] = strdup(real);
        }
    }
    static char app[PATH_MAX];
    if (target == NULL && nurls > 0) {
        if (ndocs > 0) { fprintf(stderr, "open: use -a when mixing documents and URLs\n"); return 1; }
        if (!app_for_web_url(urls[0], app, sizeof(app))) {
            fprintf(stderr, "open: no registered browser for %s (use open -a <application> ...)\n", urls[0]); return 1;
        }
        for (int i = 1; i < nurls; i++) {
            char other[PATH_MAX];
            if (!app_for_web_url(urls[i], other, sizeof(other)) || strcmp(app, other) != 0) {
                fprintf(stderr, "open: URLs have different handlers; use -a\n"); return 1;
            }
        }
        target = app;
    }
    if (target == NULL && ndocs > 0) {
        if (!app_for_document(docs[0], app, sizeof(app))) { fprintf(stderr, "open: no application here opens %s (use open -a <application> ...)\n", docs[0]); return 1; }
        target = app;
    }
    if (target == NULL) { fprintf(stderr, "usage: open <application bundle> | open [-a <application>] <document...> [--args ...]\n"); return 1; }
    if (ndocs > 0) {
        // -NSOpen <path>, or -NSOpen (<path>, ...) for several, before the caller's own arguments
        char **all = calloc((size_t)(nargs + 2), sizeof(char *));
        all[0] = "-NSOpen";
        if (ndocs == 1) all[1] = docs[0];
        else {
            size_t len = 3; for (int i = 0; i < ndocs; i++) len += strlen(docs[i]) * 2 + 4;
            char *list = malloc(len), *p = list; *p++ = '(';
            for (int i = 0; i < ndocs; i++) {
                if (i) *p++ = ',';
                *p++ = '"';
                for (const char *c = docs[i]; *c; c++) { if (*c == '"' || *c == '\\') *p++ = '\\'; *p++ = *c; }
                *p++ = '"';
            }
            *p++ = ')'; *p = 0; all[1] = list;
        }
        for (int i = 0; i < nargs; i++) all[i + 2] = args[i];
        args = all; nargs += 2;
    }
    if (nurls > 0) {
        char **all = calloc((size_t)(nargs + nurls), sizeof(char *));
        if (!all) return 1;
        for (int i = 0; i < nargs; i++) all[i] = args[i];
        for (int i = 0; i < nurls; i++) all[nargs + i] = urls[i];
        args = all; nargs += nurls;
    }
    return open_bundle(target, args, nargs);
}

static int open_bundle(const char *target, char **args, int nargs)
{
    char bundle[PATH_MAX], real[PATH_MAX], exe[PATH_MAX];
    if (!find_bundle(target, bundle, sizeof(bundle)) || realpath(bundle, real) == NULL) { fprintf(stderr, "open: no application bundle %s\n", target); return 1; }
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)real, (CFIndex)strlen(real), true);
    CFBundleRef b = url ? CFBundleCreate(NULL, url) : NULL;
    CFURLRef exeURL = b ? CFBundleCopyExecutableURL(b) : NULL;
    if (exeURL == NULL || !CFURLGetFileSystemRepresentation(exeURL, true, (UInt8 *)exe, sizeof(exe)) || access(exe, X_OK) != 0) {
        fprintf(stderr, "open: %s has no executable (CFBundleExecutable)\n", real); return 1;
    }
    // the desktop's environment where the caller has none
    setenv("XDG_RUNTIME_DIR", "/private/var/run/gpu-desktop", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    if (getenv("HOME") == NULL) setenv("HOME", "/var/root", 0);
    char sock[PATH_MAX]; snprintf(sock, sizeof(sock), "%s/%s", getenv("XDG_RUNTIME_DIR"), getenv("WAYLAND_DISPLAY"));
    if (access(sock, F_OK) != 0) { fprintf(stderr, "open: no desktop to open it on (%s: %s)\n", sock, strerror(errno)); return 1; }
    // its output: ~/Library/Logs/<executable name>.log
    char log[PATH_MAX]; const char *leaf = strrchr(exe, '/'); leaf = leaf ? leaf + 1 : exe;
    snprintf(log, sizeof(log), "%s/Library", getenv("HOME")); mkdir(log, 0755);
    snprintf(log, sizeof(log), "%s/Library/Logs", getenv("HOME")); mkdir(log, 0755);
    snprintf(log, sizeof(log), "%s/Library/Logs/%s.log", getenv("HOME"), leaf);

    char **child = calloc((size_t)nargs + 2, sizeof(char *));
    child[0] = exe; for (int i = 0; i < nargs; i++) child[i + 1] = args[i];
    posix_spawn_file_actions_t fa; posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa); posix_spawnattr_init(&at);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);          // its own session: it outlives the terminal it was typed in
    pid_t pid = 0;
    int rc = posix_spawn(&pid, exe, &fa, &at, child, environ);
    if (rc != 0) { fprintf(stderr, "open: %s: %s\n", exe, strerror(rc)); return 1; }
    return 0;
}
