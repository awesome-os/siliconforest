#ifndef DLE_QUERY_ROUTES_H
#define DLE_QUERY_ROUTES_H
#include "route_wire.h"
/* Read-only RTM_GET. Return0 means a correlated kernel reply was parsed;
 * view.error still needs classification (e.g. ESRCH for absence). Caller
 * cannot infer ownership from a snapshot. No ADD, DELETE or retry exists. */
int dle_route_query(uint32_t destination, struct dle_route_view *view,
                     void (*record)(const char *,const uint8_t *,size_t,void *),
                     void *context);
#endif
