// Host-only doubles for registry, PM, workloop gates and platform calls.
// The test inserts the actual patched firmware predicate and close body below.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdarg>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define OSDynamicCast(t, p) dynamic_cast<t *>(p)
#define bzero(p, n) std::memset(p, 0, n)
using IOReturn = int;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr int kPEAcquireScreen = 4;
extern "C" {
void host_console_boot(int graphics, int serial);
void host_console_open(void);
void host_console_remap(void);
void host_console_graphics(void);
void host_console_acquire(void);
int host_console_enabled(void);
int host_console_acquired(void);
int host_console_video(void);
}

struct PE_Video {
    unsigned long v_baseAddr;
    unsigned int v_width, v_height, v_rowBytes, v_depth, v_display;
};
struct OSObject { virtual ~OSObject() = default; };
struct OSData : OSObject {
    std::string bytes = std::string("AppleARM\0", 9);
    const void *getBytesNoCopy() const { return bytes.data(); }
    unsigned int getLength() const { return static_cast<unsigned int>(bytes.size()); }
};
struct IORegistryEntry : OSObject {
    static IORegistryEntry *fromPath(const char *, const char *);
    virtual OSObject *getProperty(const char *) { return nullptr; }
    void release() { ++releases; }
    int releases = 0;
};
struct Root : IORegistryEntry {
    OSData compatible;
    OSObject *getProperty(const char *key) override {
        CHECK(std::strcmp(key, "compatible") == 0);
        return &compatible;
    }
} root;
static const char *gIODTPlane = "device-tree-host-double";
IORegistryEntry *IORegistryEntry::fromPath(const char *path, const char *plane) {
    CHECK(std::strcmp(path, "/") == 0 && plane == gIODTPlane);
    return &root;
}
struct IOService : IORegistryEntry {
    bool inactive = false;
    bool isInactive() const { return inactive; }
};
struct IOPlatformDevice : IOService {};
struct IOPCIDevice : IOService {};
static uint32_t text_arg = 1, mirror_arg = 1, gop_arg = 0;
static bool text_arg_present = true;
static void PE_parse_boot_argn(const char *name, void *out, unsigned long size) {
    CHECK(size == sizeof(uint32_t));
    if (std::strcmp(name, "pdtextboot") == 0) {
        if (text_arg_present) *static_cast<uint32_t *>(out) = text_arg;
    } else if (std::strcmp(name, "serial_video_mirror") == 0) {
        *static_cast<uint32_t *>(out) = mirror_arg;
    } else {
        CHECK(std::strcmp(name, "gopconsole") == 0);
        *static_cast<uint32_t *>(out) = gop_arg;
    }
}
static std::vector<std::string> logs;
static void IOLog(const char *format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    CHECK(n > 0 && static_cast<unsigned int>(n) < sizeof(buf));
    logs.emplace_back(buf);
}

struct IOGOPFramebuffer;
struct IOPlatformExpert {
    PE_Video console{0xbd5d5c000UL, 2560, 1600, 10240, 30, 2};
    IOReturn get_result = 0, set_result = 0;
    unsigned requests = 0;
    IOGOPFramebuffer *owner = nullptr;
    IOReturn getConsoleInfo(PE_Video *out) { *out = console; return get_result; }
    IOReturn setConsoleInfo(PE_Video *, unsigned int);
};
struct IOFramebuffer : IOService {
    virtual void close();
    void *fServerConnect = this;
    void *fSharedConnect = nullptr;
    bool captured = true, dead = false, suspended = false;
    bool sys_gate = true, fb_gate = true, parent_finished = false;
    bool retain_server = false, retain_capture = false;
    unsigned power = 0, base_calls = 0, generic_acquires = 0;
    IOService *provider = nullptr;
    IOPlatformExpert *platform = nullptr;
    IOService *getProvider() { return provider; }
    IOPlatformExpert *getPlatform() { return platform; }
    unsigned getPowerState() { return power; }
};
void IOFramebuffer::close() {
    CHECK(sys_gate && fb_gate);
    ++base_calls;
    // Minimal parent double: source checks separately bind its gate and clear
    // ordering. This is not a model of RPC or VM-map quiescence.
    if (power) { ++generic_acquires; host_console_acquire(); }
    captured = retain_capture;
    if (!retain_server) fServerConnect = nullptr;
    parent_finished = true;
}
struct IOGOPFramebuffer : IOFramebuffer {
    using super = IOFramebuffer;
    void *fbBase = reinterpret_cast<void *>(0xbd5d5c000UL);
    unsigned width = 2560, height = 1600, pitch = 10240;
    void close() override;
};
IOReturn IOPlatformExpert::setConsoleInfo(PE_Video *info, unsigned int op) {
    CHECK(owner && owner->sys_gate && owner->fb_gate && owner->parent_finished);
    CHECK(!owner->fServerConnect && !owner->fSharedConnect && !owner->captured);
    CHECK(info == nullptr && op == kPEAcquireScreen);
    ++requests;
    if (!set_result) host_console_acquire();
    return set_result;
}

// @FIRMWARE_PREDICATE@
// @PATCHED_CLOSE@

int main(int argc, char **argv) {
    CHECK(argc == 2);
    int scenario = std::atoi(argv[1]);
    IOGOPFramebuffer fb;
    IOPlatformDevice provider;
    IOPCIDevice pci;
    IOService other;
    IOPlatformExpert platform;
    fb.provider = &provider;
    fb.platform = &platform;
    platform.owner = &fb;
    bool eligible = false, generic = false, graphics = false;
    switch (scenario) {
    case 0: eligible = true; break;
    case 1: root.compatible.bytes = std::string("OtherARM\0", 9); break;
    case 2: fb.provider = &pci; break;
    case 3: fb.provider = &other; break;
    case 4: fb.provider = nullptr; break;
    case 5: fb.retain_server = true; break;
    case 6: fb.fSharedConnect = &fb; break;
    case 7: fb.fServerConnect = nullptr; break;
    case 8: fb.inactive = true; break;
    case 9: provider.inactive = true; break;
    case 10: fb.dead = true; break;
    case 11: fb.suspended = true; break;
    case 12: fb.power = 1; generic = true; break;
    case 13: fb.platform = nullptr; break;
    case 14: platform.get_result = -1; break;
    case 15: platform.console.v_baseAddr += 0x4000; break;
    case 16: ++platform.console.v_width; break;
    case 17: ++platform.console.v_height; break;
    case 18: ++platform.console.v_rowBytes; break;
    case 19: platform.console.v_depth = 8; break;
    case 20: platform.console.v_display = 1; graphics = true; break;
    case 21: text_arg = 0; break;
    case 22: mirror_arg = 0; break;
    case 23: mirror_arg = 0; gop_arg = 1; eligible = true; break;
    case 24: eligible = true; break; // reopen/close composition
    case 25: eligible = true; break; // remap cannot take a live server's pixels
    case 26: fb.retain_capture = true; break;
    case 27: fb.fbBase = nullptr; break;
    case 28: fb.width = 0; break;
    case 29: fb.height = 0; break;
    case 30: fb.pitch = 0; break;
    case 31: platform.console.v_baseAddr |= 1; eligible = true; break;
    case 32: platform.console.v_baseAddr |= 3; eligible = true; break;
    case 33: fb.power = 2; generic = true; break;
    case 34: platform.console.v_display = 0; break;
    case 35: eligible = true; break; // explicit later graphics-mode switch wins
    case 36: platform.set_result = -7; eligible = true; break;
    case 37: eligible = true; break; // duplicate close cannot reacquire again
    case 38: fb.fSharedConnect = &fb; break; // last shared close not implemented
    case 39: text_arg_present = false; eligible = true; break;
    case 40: platform.console.v_depth = 32; eligible = true; break;
    default: return 2;
    }
    host_console_boot(graphics, scenario == 22);
    host_console_open();
    CHECK(!host_console_enabled() && !host_console_acquired());
    if (scenario == 25) {
        host_console_remap();
        CHECK(!host_console_enabled() && !host_console_acquired());
    }
    if (scenario == 35) host_console_graphics();
    fb.close();
    CHECK(fb.base_calls == 1);
    CHECK(platform.requests == static_cast<unsigned>(eligible));
    CHECK(fb.generic_acquires == static_cast<unsigned>(generic));
    if ((eligible || generic) && scenario != 35 && scenario != 36) {
        CHECK(host_console_enabled() && host_console_acquired() && host_console_video());
    } else CHECK(!host_console_enabled());
    CHECK(logs.size() <= 1);
    if (eligible) CHECK(logs[0].find("acquire-request result=") != std::string::npos);
    if (scenario == 36) CHECK(logs[0].find("fffffff9") != std::string::npos);
    if (scenario == 24) {
        host_console_open();
        fb.fServerConnect = &fb;
        fb.captured = true;
        fb.parent_finished = false;
        host_console_remap();
        CHECK(!host_console_enabled() && !host_console_acquired());
        fb.close();
        CHECK(platform.requests == 2 && fb.base_calls == 2 && host_console_enabled());
    }
    if (scenario == 37 || scenario == 38) {
        fb.fSharedConnect = nullptr;
        fb.close();
        CHECK(platform.requests == static_cast<unsigned>(eligible));
    }
    return 0;
}
