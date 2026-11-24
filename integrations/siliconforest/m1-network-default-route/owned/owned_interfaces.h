#ifndef DLE_OWNED_INTERFACES_H
#define DLE_OWNED_INTERFACES_H
#include <stddef.h>
#include <stdint.h>
#define DLE_INTERFACE_NAME 16u
#define DLE_INTERFACE_LIMIT 64u
#define DLE_LOCAL_IPV4 UINT32_C(0x0a00020f)
#define DLE_PEER_IPV4 UINT32_C(0x0a000202)
enum dle_interface_stage {
  DLE_IF_IDLE, DLE_IF_PREFLIGHT, DLE_IF_CREATE, DLE_IF_CONFIGURE,
  DLE_IF_READBACK, DLE_IF_ACTIVE, DLE_IF_CLEANUP, DLE_IF_CLOSED
};
struct dle_interface_identity { uint32_t index; char name[DLE_INTERFACE_NAME]; };
struct dle_interfaces {
  /* Borrow fd only while open returned0; never close/dup it. */
  int fd;
  char name[DLE_INTERFACE_NAME];
  uint32_t index, peer;
  unsigned phase_count;
  int failed, first_error, cleanup_complete;
  enum dle_interface_stage stage;
  /* Private lifecycle fields: caller must not modify these. */
  int _owned_fd, _identity_known, _baseline_known, _exclusive, _descriptor_uncertain;
  size_t _baseline_count;
  struct dle_interface_identity _baseline[DLE_INTERFACE_LIMIT];
  void (*_record)(const char *, const uint8_t *, size_t, void *);
  void *_context;
};
/* One single-threaded session. exclusive_fresh_guest must be1 and represents a
 * caller-established execution contract, not a discovered kernel ownership fact.
 * No other thread/agent may configure guest networking or use this session.
 */
int dle_interfaces_init(struct dle_interfaces *, int exclusive_fresh_guest,
    void (*record)(const char *, const uint8_t *, size_t, void *), void *context);
/* First phase requires10.0.2.3; second requires a global-unicast A independently
 * validated by caller's DNS validator. This helper also refuses special ranges.
 * Failed open closes its owned descriptors best-effort; failure remains sticky.
 */
int dle_interfaces_open(struct dle_interfaces *, uint32_t peer_host_order);
/* Caller first closes all application sockets/joins workers. Always call on
 * every error path. Returns0 only for healthy session with verified cleanup;
 * cleanup_complete may be1 with return-1 when an earlier failure was sticky.
 * Closed phase2 + cleanup_complete + !failed is lifecycle evidence only.
 */
int dle_interfaces_close(struct dle_interfaces *);
#endif
