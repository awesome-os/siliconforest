/* Read-only raw MIG boundary. No CF decoding or userclient/service open. */
#include "collector.h"
#include <PDIOKitLib.h>
#include <device_user.h>
#include <string.h>
#if !defined(IOKIT) || (IOKIT + 0) != 1 ||                                     \
    (defined(KERNEL) && (KERNEL + 0) != 0)
#error requires IOKIT=1 KERNEL=0
#endif
_Static_assert(sizeof(io_struct_inband_t) == APC_CAP,
               "actual MIG inband capacity");
_Static_assert(APC_BYTES == 84, "observation layout contract");
static int fetch(void *unused, uint8_t *out, size_t cap, size_t *length,
                 int32_t *detail, int32_t *cleanup) {
  mach_port_t master = MACH_PORT_NULL, entry = MACH_PORT_NULL;
  io_string_t path = {0};
  io_name_t key = {0};
  io_struct_inband_t raw = {0};
  mach_msg_type_number_t count = APC_CAP;
  kern_return_t kr;
  int status = APC_GET_RPC;
  (void)unused;
  *length = 0;
  *detail = 0;
  *cleanup = 0;
  if (cap != APC_CAP)
    return status;
  kr = IOMasterPort(MACH_PORT_NULL, &master);
  *detail = kr;
  if (kr != KERN_SUCCESS || master == MACH_PORT_NULL)
    goto done;
  memcpy(path, "IOService:/IOResources", sizeof("IOService:/IOResources"));
  memcpy(key, APC_PROPERTY, sizeof(APC_PROPERTY));
  kr = io_registry_entry_from_path(master, path, &entry);
  *detail = kr;
  if (kr != KERN_SUCCESS)
    goto done;
  if (entry == MACH_PORT_NULL) {
    status = APC_GET_MISSING;
    goto done;
  }
  kr = io_registry_entry_get_property_bytes(entry, key, raw, &count);
  *detail = kr;
  if ((uint32_t)kr == UINT32_C(0xe00002be)) {
    status = APC_GET_MISSING;
    goto done;
  }
  if (kr != KERN_SUCCESS || count > cap)
    goto done;
  memcpy(out, raw, count);
  *length = count;
  status = APC_GET_OK;
done:
  if (entry != MACH_PORT_NULL) {
    kr = IOObjectRelease(entry);
    if (kr != KERN_SUCCESS && !*cleanup)
      *cleanup = kr;
  }
  if (master != MACH_PORT_NULL) {
    kr = IOObjectRelease(master);
    if (kr != KERN_SUCCESS && !*cleanup)
      *cleanup = kr;
  }
  return status;
}
int main(int argc, char **argv) {
  return apc_run(argc, argv, fetch, 0, stdout);
}
