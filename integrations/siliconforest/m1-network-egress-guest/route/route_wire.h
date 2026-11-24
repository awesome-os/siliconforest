#ifndef DLE_ROUTE_WIRE_H
#define DLE_ROUTE_WIRE_H
#include <stddef.h>
#include <stdint.h>

/* Pinned SDK ABI guarded by route_abi.c. All IPv4 values are host-order. */
struct dle_route_view {
  uint32_t flags, error, destination, gateway, netmask;
  uint16_t interface_index;
  uint8_t have_destination, have_gateway, have_netmask;
};
/* 0=exact correlated parsed response; 1=unrelated; -1=invalid. Output preserved
 * on nonzero. This decoder neither owns routes nor authorizes a deletion. */
int dle_route_decode(const uint8_t *raw, size_t length, uint32_t pid,
                     uint32_t sequence, uint8_t type,
                     struct dle_route_view *out);
/* Build an exact host-route request using pinned SDK ABI, without syscalls.
 * GET only needs destination; ADD/DELETE also carry gateway, /32 and IFP.
 * Output and length are preserved on failure. */
int dle_route_encode(uint8_t *out, size_t capacity, size_t *length,
                     uint32_t pid, uint32_t sequence, uint8_t type,
                     uint32_t destination, uint32_t gateway,
                     uint16_t interface_index);
#endif
