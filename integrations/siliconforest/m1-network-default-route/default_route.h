/* Original bounded default-route lifetime. Caller serializes all operations. */
#ifndef DLF_DEFAULT_ROUTE_H
#define DLF_DEFAULT_ROUTE_H
#include <stddef.h>
#include <stdint.h>
#define DLF_LOCAL UINT32_C(0x0a00020f)
#define DLF_PEER UINT32_C(0x0a000202)
#define DLF_REQUIRED_FLAGS UINT32_C(0x40010843)
#define DLF_ALLOWED_FLAGS DLF_REQUIRED_FLAGS
struct dlf_owner { uint16_t index; char name[16]; };
struct dlf_route {
    uint32_t error,flags,destination,gateway,netmask,ifa;
    uint16_t index,ifp_index;
    unsigned have_destination,have_gateway,have_netmask,have_ifp,have_ifa;
    char ifp_name[16];
};
enum dlf_error {
    DLF_NONE, DLF_ARGUMENT, DLF_CONTEXT, DLF_PREFLIGHT, DLF_OWNER_OPEN,
    DLF_OWNER_IDENTITY, DLF_ADD, DLF_READBACK, DLF_OWNER_CLOSE,
    DLF_ROUTE_REMAINS, DLF_BASELINE
};
enum dlf_stage { DLF_NEW, DLF_OPENING, DLF_ADDING, DLF_ACTIVE, DLF_CLOSING, DLF_CLOSED };
/* Each nonzero callback result is a failure, including unknown ADD outcome.
 * open_owner/close_owner share the provider's private descriptor ownership.
 * close_owner must close even a partially opened owner, never a foreign fd.
 * It must verify asynchronous interface disappearance and peer-route removal.
 * No callback performs RTM_DELETE; interface detach owns route reclamation.
 */
struct dlf_ops {
    int (*get_default)(void *,struct dlf_route *);
    int (*open_owner)(void *,struct dlf_owner *);
    int (*add_default)(void *,const struct dlf_owner *);
    int (*close_owner)(void *);
    int (*baseline_equal)(void *);
};
struct dlf_session {
    const struct dlf_ops *ops; void *context;
    struct dlf_owner owner;
    enum dlf_stage stage; enum dlf_error first_error;
    unsigned exclusive,owner_started,close_attempted,add_attempted,cleanup_complete;
};
/* Stable zero-initialized, noncopied storage. Never reset/reinitialize an owner. */
int dlf_init(struct dlf_session *,const struct dlf_ops *,void *,int exclusive);
int dlf_open(struct dlf_session *);
int dlf_close(struct dlf_session *);
int dlf_route_matches(const struct dlf_route *,const struct dlf_owner *);
#endif
