#ifndef DLE_INTERFACE_INTERNAL_H
#define DLE_INTERFACE_INTERNAL_H
#include "owned_interfaces.h"
#include "../route/route_wire.h"
/* Internal fault seam for host state tests; native public API installs one fixed
 * operations table. No plugin/transport/backend selection exists in that API. */
struct dle_interface_ops {
  int (*inventory)(void *, struct dle_interfaces *, int capture);
  int (*query)(void *, uint32_t, struct dle_route_view *);
  int (*create)(void *, struct dle_interfaces *);
  int (*configure)(void *, struct dle_interfaces *);
  int (*inspect)(void *, struct dle_interfaces *);
  int (*close_fd)(void *, int);
  uint64_t (*now)(void *);
  int (*pause)(void *, unsigned);
};
int dle_interfaces_open_core(struct dle_interfaces *, uint32_t,
                             const struct dle_interface_ops *, void *);
int dle_interfaces_close_core(struct dle_interfaces *,
                              const struct dle_interface_ops *, void *);
#endif
