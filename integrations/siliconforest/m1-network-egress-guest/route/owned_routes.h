#ifndef DLE_OWNED_ROUTES_H
#define DLE_OWNED_ROUTES_H
#include <stddef.h>
#include <stdint.h>
struct dle_owned_route { uint32_t destination; int owned; };
struct dle_routes {
  int fd, sequence;
  uint16_t index;
  uint32_t pid;
  struct dle_owned_route entries[2];
  void (*record)(const char *, const uint8_t *, size_t, void *);
  void *context;
};
/* Own only two unscoped IPv4 /32 routes via10.0.2.2 on an existing ownedutun.
 * No default route or host OS route is touched. A nonzero return is a failure.
 * Caller must close routes before closing utun, including error paths. */
int dle_routes_open(struct dle_routes *, const char *,
                    void (*)(const char *,const uint8_t *,size_t,void *),void *);
int dle_routes_add(struct dle_routes *, unsigned slot, uint32_t destination);
int dle_routes_close(struct dle_routes *);
#endif
