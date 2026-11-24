/* Thin Darwin MIG wrapper for darwin-trackpad-enum. License pending.
 * Two registry reads at most: the IOResources provider copy (once) and, only if
 * the published PDM1SPIHID instance exists, its observation copy (once).
 * Build (root-owned recipe): -DIOKIT=1 -DKERNEL=0 -UKERNEL with the tiny IOKit
 * include directory (PDIOKitLib.h, device_user.h) and its libIOKit.a.
 */
#include <PDIOKitLib.h>
#include <mach/mach.h>
#include <string.h>
#include "device_user.h"
#include "trackpad_collector.h"
#include "observation_layout.h"

/* kIOReturnNoResources = iokit_common_err(0x2be): property absent on the entry. */
#define TPEC_IO_RETURN_NO_RESOURCES ((kern_return_t)0xe00002be)

struct registry { mach_port_t main_port; int attempted; kern_return_t main_error; };

static int get_bytes(void *context, enum tpec_copy which, uint8_t *buf, size_t cap,
                     size_t *len, int32_t *detail)
{
    struct registry *r = context;
    io_string_t path;
    io_name_t key;
    mach_port_t entry = MACH_PORT_NULL;
    mach_msg_type_number_t count;
    kern_return_t kr;
    if (cap != sizeof(io_struct_inband_t)) { *detail = 0; return TPEC_GET_ERROR; }
    if (!r->attempted) {                       /* one host_get_io_master, never retried */
        r->attempted = 1;
        r->main_error = IOMasterPort(MACH_PORT_NULL, &r->main_port);
    }
    if (r->main_error != KERN_SUCCESS || r->main_port == MACH_PORT_NULL) {
        *detail = r->main_error; return TPEC_GET_ERROR;
    }
    memset(path, 0, sizeof(path));
    memset(key, 0, sizeof(key));
    strcpy(path, which == TPEC_PROVIDER ? "IOService:/IOResources" : "IOService:/IOResources/PDM1SPIHID");
    strcpy(key, M1TP_OBSERVATION_KEY);
    kr = io_registry_entry_from_path(r->main_port, path, &entry);
    *detail = kr;
    if (kr != KERN_SUCCESS) return TPEC_GET_ERROR;
    if (entry == MACH_PORT_NULL) return TPEC_GET_NO_ENTRY;   /* XNU: success with no entry */
    count = (mach_msg_type_number_t)cap;
    kr = io_registry_entry_get_property_bytes(entry, key, (char *)buf, &count);
    IOObjectRelease(entry);
    *detail = kr;
    if (kr == TPEC_IO_RETURN_NO_RESOURCES) return TPEC_GET_NO_PROPERTY;
    if (kr != KERN_SUCCESS || count > cap) return TPEC_GET_ERROR;
    *len = count;
    return TPEC_GET_OK;
}

int main(int argc, char **argv)
{
    struct registry r = {MACH_PORT_NULL, 0, KERN_SUCCESS};
    return tpec_run(argc, argv, get_bytes, &r, stdout);
}
