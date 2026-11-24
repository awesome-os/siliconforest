#ifndef DLF_INVENTORY_H
#define DLF_INVENTORY_H
#include <stddef.h>
#include <stdint.h>
#define DLF_ROUTE_LIMIT 128u
#define DLF_DUMP_LIMIT 65536u
struct dlf_route_key {
    uint32_t flags,addresses;
    uint16_t index,length;
    uint8_t body[256];
};
struct dlf_route_set {size_t count;struct dlf_route_key key[DLF_ROUTE_LIMIT];};
/* NET_RT_DUMP IPv4 headers, not a collection of ACKs. Preserve all address bytes,
 * scopes, flags and interface indices; omit counters, metrics, clocks and PID.
 * Output requires separate scratch storage and is unchanged on failure. */
int dlf_inventory(const uint8_t *,size_t,struct dlf_route_set *out,struct dlf_route_set *scratch);
int dlf_inventory_equal(const struct dlf_route_set *,const struct dlf_route_set *);
/* Refuse all unscoped/scoped /0 routes; unscoped GET alone is insufficient.
 * 1=default exists, 0=none, -1=malformed internal inventory. */
int dlf_inventory_has_default(const struct dlf_route_set *);
#endif
