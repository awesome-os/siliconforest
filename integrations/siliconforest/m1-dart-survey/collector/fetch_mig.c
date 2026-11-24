/* Target translation unit. Host tests do not compile this file.
 * Root's probe links the qualified tiny archive. Compile exactly:
 *   -std=c11 -Wall -Wextra -Wpedantic -Werror -DIOKIT=1 -DKERNEL=0 -UKERNEL
 * device_types.h defines io_struct_inband_t only under IOKIT and not KERNEL.
 * No handwritten MIG prototype: the declaration comes from device_user.h.
 */
#include "classify.h"
#include "fetch.h"
#include <string.h>
#if !defined(IOKIT) || (IOKIT + 0) != 1
#error darwin-dart-survey requires -DIOKIT=1 -DKERNEL=0 -UKERNEL
#endif
#if defined(KERNEL) && (KERNEL + 0) != 0
#error darwin-dart-survey requires -DKERNEL=0 -UKERNEL
#endif
#include <PDIOKitLib.h>
#include <device_user.h>

_Static_assert(sizeof(io_struct_inband_t) == 4096, "device.defs inband buffer");
_Static_assert(DLS_SNAPSHOT_BYTES <= sizeof(io_struct_inband_t), "764 fits CountInOut");

/* Pinned iokit/IOKit/IOReturn.h iokit_common_err values. */
static const uint32_t DLS_KR_NO_RESOURCES = 0xe00002beu;
static const uint32_t DLS_KR_NO_DEVICE = 0xe00002c0u;

static int kr_is(kern_return_t kr, uint32_t code)
{
    return (uint32_t)kr == code;
}

static int fill_bytes(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || !src || cap == 0) return -1;
    for (i = 0; i < cap; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') return 0;
    }
    return -1;
}

static void release_port(mach_port_t port)
{
    if (port != MACH_PORT_NULL) (void)IOObjectRelease(port);
}

/* CountInOut capacity is 764. IOUserClient.cpp copies OSData bytes only when
 * the provided count is at least the object length; a longer object returns
 * kIOReturnIPCError and is not copied. One call, no retry. */
static int read_property(mach_port_t entry, const char *name, uint8_t *dst, size_t cap, size_t *out_n)
{
    io_name_t prop;
    io_struct_inband_t data;
    mach_msg_type_number_t count = DLS_SNAPSHOT_BYTES;
    kern_return_t kr;
    *out_n = 0;
    if (fill_bytes(prop, sizeof(prop), name) != 0) return DLS_FETCH_MIG;
    memset(data, 0, sizeof(data));
    kr = io_registry_entry_get_property_bytes(entry, prop, data, &count);
    if (kr_is(kr, DLS_KR_NO_RESOURCES)) return DLS_FETCH_MISSING;
    if (kr != KERN_SUCCESS) return DLS_FETCH_MIG;
    if (count > cap || count > DLS_SNAPSHOT_BYTES) return DLS_FETCH_MIG;
    memcpy(dst, data, count);
    *out_n = count;
    if (count != DLS_SNAPSHOT_BYTES) return DLS_FETCH_LENGTH;
    return DLS_FETCH_OK;
}

int dls_fetch_diagnostic(uint8_t *dst, size_t cap, size_t *out_n, int *instance)
{
    mach_port_t master = MACH_PORT_NULL;
    mach_port_t entry = MACH_PORT_NULL;
    mach_port_t iterator = MACH_PORT_NULL;
    mach_port_t service = MACH_PORT_NULL;
    mach_port_t extra = MACH_PORT_NULL;
    io_string_t path;
    io_string_t matching;
    uint8_t other[DLS_SNAPSHOT_BYTES];
    size_t other_n = 0;
    kern_return_t kr;
    int rc = DLS_FETCH_MIG;
    /* Same XML shape as the tiny static IOServiceMatching, fixed to this class. */
    static const char match[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>IOProviderClass</key><string>PDDARTSurvey</string>"
        "</dict></plist>";
    _Static_assert(sizeof(match) <= 512, "matching string fits io_string_t");
    if (out_n) *out_n = 0;
    if (instance) *instance = DLS_INSTANCE_UNREAD;
    if (!dst || !out_n || !instance || cap < DLS_SNAPSHOT_BYTES) goto out;
    if (IOMasterPort(MACH_PORT_NULL, &master) != KERN_SUCCESS || master == MACH_PORT_NULL) goto out;
    memset(path, 0, sizeof(path));
    if (fill_bytes(path, sizeof(path), "IOService:/IOResources") != 0) goto out;
    if (io_registry_entry_from_path(master, path, &entry) != KERN_SUCCESS) goto out;
    if (entry == MACH_PORT_NULL) {
        rc = DLS_FETCH_MISSING;
        goto out;
    }
    rc = read_property(entry, "PDDARTSurveyDiagnosticV1", dst, cap, out_n);
    if (rc != DLS_FETCH_OK) goto out;
    memset(matching, 0, sizeof(matching));
    if (fill_bytes(matching, sizeof(matching), match) != 0) {
        rc = DLS_FETCH_MIG;
        goto out;
    }
    if (io_service_get_matching_services(master, matching, &iterator) != KERN_SUCCESS ||
        iterator == MACH_PORT_NULL) {
        rc = DLS_FETCH_MIG;
        goto out;
    }
    kr = io_iterator_next(iterator, &service);
    if (kr_is(kr, DLS_KR_NO_DEVICE)) {
        *instance = DLS_INSTANCE_ABSENT;
        rc = DLS_FETCH_OK;
        goto out;
    }
    if (kr != KERN_SUCCESS || service == MACH_PORT_NULL) {
        rc = DLS_FETCH_MIG;
        goto out;
    }
    kr = io_iterator_next(iterator, &extra);
    if (!kr_is(kr, DLS_KR_NO_DEVICE)) {
        rc = DLS_FETCH_INSTANCE;
        goto out;
    }
    rc = read_property(service, "PDDARTSurveyObservationV1", other, sizeof(other), &other_n);
    if (rc != DLS_FETCH_OK || other_n != DLS_SNAPSHOT_BYTES ||
        memcmp(other, dst, DLS_SNAPSHOT_BYTES) != 0) {
        rc = DLS_FETCH_INSTANCE;
        goto out;
    }
    *instance = DLS_INSTANCE_EQUAL;
    rc = DLS_FETCH_OK;
out:
    release_port(extra);
    release_port(service);
    release_port(iterator);
    release_port(entry);
    release_port(master);
    return rc;
}
