/*
 * Host harness for SFGOP.c: fake SiliconForest IOKit user-client replies drive
 * SFGOPOpen/SFGOPPresent through success and short/invalid-reply scenarios.
 * Built twice by test_m1_wayland.py: against the patched SFGOP.c and against
 * the pinned original (-DPINNED, which has no query functions).
 * Output: one "key=value ..." line per scenario.
 */
#include <SFGOP.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *cls;                /* class the registry "has" */
    int virtio;                     /* IOObjectConformsTo(..., "IOVirtIOGPU") */
    kern_return_t conforms_kr;
    kern_return_t scalar_kr;
    uint32_t scalar_cnt;
    size_t pi_size;                 /* bytes the driver "returns" */
    uint32_t w, h, stride, bpp, bpc, masks[3];
    mach_vm_address_t map_addr;
    mach_vm_size_t map_size;
} cfg_t;

static cfg_t C;
static int n_unmap, n_close, n_release, n_dealloc, n_struct11, n_struct_other;

/* Mirror of SFGOP.c's private IOPixelInformation (same layout as IOGraphics). */
typedef struct {
    uint32_t bytesPerRow, bytesPerPlane, bitsPerPixel, pixelType;
    uint32_t componentCount, bitsPerComponent, componentMasks[16];
    char pixelFormat[64];
    uint32_t flags, activeWidth, activeHeight, reserved[2];
} pi_t;

unsigned int mach_task_self(void) { return 0x103; }
kern_return_t mach_port_deallocate(unsigned int t, unsigned int n) { (void)t; (void)n; n_dealloc++; return 0; }
kern_return_t IOMasterPort(mach_port_t b, mach_port_t *m) { (void)b; *m = 0x10; return 0; }
char *IOServiceMatching(const char *cls) { return (char *)cls; }
kern_return_t IOServiceGetMatchingService(mach_port_t m, char *matching, io_service_t *s)
{
    (void)m;
    if (C.cls != NULL && strcmp(matching, C.cls) == 0) { *s = 0x20; return 0; }
    *s = IO_OBJECT_NULL;
    return KERN_FAILURE;
}
kern_return_t IORegistryEntryGetName(io_object_t e, io_name_t name) { (void)e; snprintf(name, 128, "%s", C.cls); return 0; }
kern_return_t IOObjectConformsTo(io_object_t o, const char *cls, boolean_t *conforms)
{
    (void)o;
    if (C.conforms_kr != 0) return C.conforms_kr;
    *conforms = (strcmp(cls, "IOVirtIOGPU") == 0) ? C.virtio : 0;
    return 0;
}
kern_return_t IOServiceOpen(io_service_t s, task_t t, uint32_t type, io_connect_t *c) { (void)s; (void)t; (void)type; *c = 0x30; return 0; }
kern_return_t IOServiceClose(io_connect_t c) { (void)c; n_close++; return 0; }
kern_return_t IOObjectRelease(io_object_t o) { (void)o; n_release++; return 0; }
kern_return_t IOConnectCallScalarMethod(io_connect_t c, uint32_t sel, const uint64_t *in, uint32_t inCnt,
    uint64_t *out, uint32_t *outCnt)
{
    (void)c; (void)sel; (void)in; (void)inCnt;
    if (C.scalar_kr != 0) return C.scalar_kr;
    if (C.scalar_cnt >= 1) out[0] = 1;
    if (C.scalar_cnt >= 2) out[1] = 0;
    *outCnt = C.scalar_cnt;
    return 0;
}
kern_return_t IOConnectCallMethod(io_connect_t c, uint32_t sel, const uint64_t *in, uint32_t inCnt,
    const void *is, size_t isCnt, uint64_t *out, uint32_t *outCnt, void *os, size_t *osCnt)
{
    pi_t pi;
    (void)c; (void)sel; (void)in; (void)inCnt; (void)is; (void)isCnt; (void)out; (void)outCnt;
    memset(&pi, 0, sizeof(pi));
    pi.bytesPerRow = C.stride; pi.bitsPerPixel = C.bpp; pi.pixelType = 2;
    pi.componentCount = 3; pi.bitsPerComponent = C.bpc;
    pi.componentMasks[0] = C.masks[0]; pi.componentMasks[1] = C.masks[1]; pi.componentMasks[2] = C.masks[2];
    pi.activeWidth = C.w; pi.activeHeight = C.h;
    if (*osCnt < sizeof(pi)) return KERN_FAILURE;
    memcpy(os, &pi, C.pi_size < sizeof(pi) ? C.pi_size : sizeof(pi));
    *osCnt = C.pi_size;
    return 0;
}
kern_return_t IOConnectMapMemory64(io_connect_t c, uint32_t type, task_t t, mach_vm_address_t *a,
    mach_vm_size_t *sz, uint32_t opt)
{
    (void)c; (void)type; (void)t; (void)opt;
    *a = C.map_addr; *sz = C.map_size;
    return 0;
}
kern_return_t IOConnectUnmapMemory64(io_connect_t c, uint32_t type, task_t t, mach_vm_address_t a)
{ (void)c; (void)type; (void)t; (void)a; n_unmap++; return 0; }
kern_return_t IOConnectCallStructMethod(io_connect_t c, uint32_t sel, const void *in, size_t inCnt,
    void *out, size_t *outCnt)
{ (void)c; (void)in; (void)inCnt; (void)out; (void)outCnt; if (sel == 11) n_struct11++; else n_struct_other++; return 0; }

static cfg_t base(void)
{
    cfg_t c = { "IOGOPFramebuffer", 0, 0, 0, 2, sizeof(pi_t), 2560, 1600, 10240, 32, 10,
                { 0x3ff00000, 0x000ffc00, 0x000003ff }, 0x200000000ull, 0xfa0000 };
    return c;
}

static int zeroed(const SFGOPFramebuffer *fb)
{
    static const SFGOPFramebuffer z;
    return memcmp(fb, &z, sizeof(z)) == 0;
}

static void run(const char *name, cfg_t c)
{
    SFGOPFramebuffer fb;
    kern_return_t kr;

    C = c;
    n_unmap = n_close = n_release = n_dealloc = n_struct11 = n_struct_other = 0;
    memset(&fb, 0xA5, sizeof(fb));
    kr = SFGOPOpen(&fb);
    printf("%s open_rc=%d stage=%s zeroed=%d unmap=%d close=%d release=%d dealloc=%d",
           name, kr, SFGOPLastErrorStage(), zeroed(&fb), n_unmap, n_close, n_release, n_dealloc);
    if (kr == KERN_SUCCESS) {
        printf(" w=%u h=%u stride=%u bpp=%u size=0x%llx", fb.width, fb.height, fb.stride, fb.bpp,
               (unsigned long long)fb.size);
#ifndef PINNED
        printf(" kind=%d fmt=%d", (int)SFGOPGetServiceKind(&fb), (int)SFGOPGetPixelFormat(&fb));
#endif
        n_struct11 = n_struct_other = 0;
        kr = SFGOPPresent(&fb, 0, 0, fb.width, fb.height);
        printf(" present_rc=%d sel11=%d other=%d", kr, n_struct11, n_struct_other);
        SFGOPClose(&fb);
#ifndef PINNED
        printf(" after_close_kind=%d", (int)SFGOPGetServiceKind(&fb));
#endif
    }
    printf("\n");
}

int main(void)
{
    cfg_t c;

    printf("abi sizeof=%zu align=%zu off_address=%zu off_size=%zu off_width=%zu off_bpp=%zu off_masks=%zu\n",
           sizeof(SFGOPFramebuffer), _Alignof(SFGOPFramebuffer), offsetof(SFGOPFramebuffer, address),
           offsetof(SFGOPFramebuffer, size), offsetof(SFGOPFramebuffer, width),
           offsetof(SFGOPFramebuffer, bpp), offsetof(SFGOPFramebuffer, componentMasks));

    run("ok_iogop_2101010", base());
    c = base(); c.cls = "IOVirtIOGPU"; c.virtio = 1; c.bpc = 8;
    c.masks[0] = 0x00ff0000; c.masks[1] = 0x0000ff00; c.masks[2] = 0x000000ff;
    run("ok_virtio_8888", c);
    c = base(); c.cls = "IOFramebuffer"; c.bpp = 16; c.stride = 5120;
    run("ok_unknown_format", c);
    c = base(); c.scalar_cnt = 1; run("short_scalar", c);
    c = base(); c.scalar_cnt = 0; run("zero_scalar", c);
    c = base(); c.pi_size = 100; run("short_pixelinfo", c);
    c = base(); c.w = 0; run("zero_width", c);
    c = base(); c.bpp = 0; run("zero_bpp", c);
    c = base(); c.bpp = 12; run("odd_bpp", c);
    c = base(); c.stride = 2560 * 4 - 4; run("short_stride", c);
    c = base(); c.map_size = 0xfa0000 - 0x4000; run("short_map", c);
    c = base(); c.map_addr = 0; run("null_map", c);
    c = base(); c.conforms_kr = KERN_FAILURE; run("conforms_error", c);
    c = base(); c.cls = "NoSuchClass"; run("no_service", c);
    return 0;
}
