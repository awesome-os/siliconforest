/* Read-only guest inventory. Never opens a HID queue or changes its console grab. */
#include <PDIOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <sys/stat.h>

static void quoted(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32 || *p > 126) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

int main(void)
{
    const char *classes[] = {"IOHIDDevice", "IOHIDKeyboard", "AppleEmbeddedKeyboard",
                            "PDSerialHID", "PDM1SPIHID", "IOGOPFramebuffer"};
    const char *devices[] = {"/dev/usb_hid_kbd", "/dev/usb_hid_mouse"};
    mach_port_t master = MACH_PORT_NULL;
    kern_return_t kr = IOMasterPort(MACH_PORT_NULL, &master);
    if (kr != KERN_SUCCESS || master == MACH_PORT_NULL) {
        printf("{\"status\":\"INVENTORY_FAILED\",\"master_error\":%d}\n", kr);
        return 1;
    }
    int failed = 0;
    printf("{\"status\":\"GUEST_REGISTRY_OBSERVATION\",\"HID_open_performed\":false,\"classes\":[");
    for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        io_iterator_t iterator = IO_OBJECT_NULL;
        kr = IOServiceGetMatchingServices(master, IOServiceMatching(classes[i]), &iterator);
        if (i) putchar(',');
        printf("{\"requested_class\":"); quoted(classes[i]);
        printf(",\"match_error\":%d,\"services\":[", kr);
        unsigned count = 0;
        if (kr == KERN_SUCCESS && iterator != IO_OBJECT_NULL) {
            io_object_t service;
            while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
                if (count == 64) { IOObjectRelease(service); failed = 1; break; }
                io_name_t name = {0}, kind = {0};
                kern_return_t name_error = IORegistryEntryGetName(service, name);
                kern_return_t class_error = IOObjectGetClass(service, kind);
                name[sizeof(name)-1] = 0; kind[sizeof(kind)-1] = 0;
                if (count++) putchar(',');
                printf("{\"name\":"); quoted(name); printf(",\"class\":"); quoted(kind);
                printf(",\"name_error\":%d,\"class_error\":%d}", name_error, class_error);
                if (name_error != KERN_SUCCESS || class_error != KERN_SUCCESS) failed = 1;
                IOObjectRelease(service);
            }
            IOObjectRelease(iterator);
        } else failed = 1;
        printf("],\"count\":%u}", count);
    }
    printf("],\"nodes\":[");
    for (unsigned i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        struct stat st;
        int found = stat(devices[i], &st) == 0;
        if (i) putchar(',');
        printf("{\"path\":"); quoted(devices[i]);
        printf(",\"exists\":%s,\"character_device\":%s}", found ? "true" : "false",
               found && S_ISCHR(st.st_mode) ? "true" : "false");
    }
    printf("],\"query_errors\":%s,\"native_keyboard_working_claimed\":false}\n", failed ? "true" : "false");
    mach_port_deallocate(mach_task_self(), master);
    return failed ? 1 : 0;
}
