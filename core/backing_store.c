#include "backing_store.h"

static int ready(const dl_store *s) {
    if (!s || !s->policy.session || !s->ops.allocate || !s->ops.release)
        return DL_INVALID;
    return s->in_callback ? DL_BAD_STATE : DL_OK;
}

int dl_store_init(dl_store *s, uint64_t session, uint64_t page,
                  uint64_t limit, dl_backing_ops ops, void *context) {
    if (!s || !session || !page || (page & (page - 1)) || limit < page ||
        !ops.allocate || !ops.release) return DL_INVALID;
    /* Initialize in place rather than materializing the entire store on stack. */
    s->ops=ops;
    s->context=context;
    s->page_size=page;
    s->byte_limit=limit;
    s->resident_bytes=0;
    s->in_callback=false;
    for (unsigned i=0; i<DL_BUFFER_CAPACITY; ++i)
        s->backing[i]=(dl_backing_slot){0};
    return dl_init(&s->policy, session);
}

int dl_store_allocate(dl_store *s, uint64_t bytes, dl_buffer *out) {
    int status=ready(s);
    if (status != DL_OK) return status;
    if (!out || !bytes) return DL_INVALID;
    if (s->policy.device_lost) return DL_DEVICE_LOST;
    if (bytes > UINT64_MAX - (s->page_size - 1)) return DL_OVERFLOW;
    uint64_t rounded=(bytes+s->page_size-1)&~(s->page_size-1);
    if (s->resident_bytes > s->byte_limit || rounded > s->byte_limit-s->resident_bytes)
        return DL_CAPACITY;
    dl_buffer b;
    status=dl_allocate(&s->policy,bytes,s->page_size,&b);
    if (status != DL_OK) return status;
    /* Reserve metadata before entering the backend. Failure consumes a handle
     * generation but never publishes it, so stale tokens cannot be resurrected. */
    s->in_callback=true;
    void *cookie=s->ops.allocate(s->context,rounded,s->page_size);
    s->in_callback=false;
    if (!cookie) {
        (void)dl_close_owner(&s->policy,b);
        (void)dl_reclaim(&s->policy,b);
        return DL_STORE_BACKEND_ERROR;
    }
    s->backing[b.index]=(dl_backing_slot){.occupied=true,.buffer=b,.cookie=cookie};
    s->resident_bytes+=rounded;
    *out=b;
    return DL_OK;
}

int dl_store_hold(dl_store *s, dl_buffer b, dl_lease_kind kind, dl_lease *out) {
    int status=ready(s);
    if (status != DL_OK) return status;
    return dl_hold(&s->policy,b,kind,out);
}
int dl_store_close_owner(dl_store *s, dl_buffer b) {
    int status=ready(s);
    if (status != DL_OK) return status;
    return dl_close_owner(&s->policy,b);
}
#define LEASE_ACK(name, policy_function) \
int name(dl_store *s, dl_lease l) { \
    int status=ready(s); \
    if (status != DL_OK) return status; \
    return policy_function(&s->policy,l); \
}
LEASE_ACK(dl_store_release_host,dl_release_host)
LEASE_ACK(dl_store_unbind_gpu,dl_unbind_gpu)
LEASE_ACK(dl_store_complete_job,dl_complete_job)
LEASE_ACK(dl_store_ack_firmware_release,dl_ack_firmware_release)
LEASE_ACK(dl_store_timeout,dl_timeout)
#undef LEASE_ACK
int dl_store_mark_device_lost(dl_store *s) {
    int status=ready(s);
    if (status != DL_OK) return status;
    return dl_mark_device_lost(&s->policy);
}
int dl_store_backend_ack_quiescence(dl_store *s, uint64_t sequence) {
    int status=ready(s);
    if (status != DL_OK) return status;
    return dl_backend_ack_quiescence(&s->policy,sequence);
}
int dl_store_inspect(const dl_store *s, dl_buffer b, dl_view *out) {
    int status=ready(s);
    if (status != DL_OK) return status;
    return dl_inspect(&s->policy,b,out);
}

int dl_store_collect(dl_store *s, dl_collection *out) {
    int status=ready(s);
    if (status != DL_OK) return status;
    if (!out) return DL_INVALID;
    *out=(dl_collection){0};
    if (s->policy.device_lost) return DL_DEVICE_LOST;
    status=dl_store_check_invariants(s);
    if (status != DL_OK) return status;
    for (uint32_t i=0; i<DL_BUFFER_CAPACITY; i++) {
        dl_backing_slot *slot=&s->backing[i];
        if (!slot->occupied) continue;
        dl_view v;
        status=dl_inspect(&s->policy,slot->buffer,&v);
        if (status != DL_OK) return status;
        if (v.owner_alive || v.host || v.device) { out->retained++; continue; }
        /* No locks or allocation needed by this coordinator. The caller has
         * selected a context where this possibly blocking callback is legal. */
        s->in_callback=true;
        bool freed=s->ops.release(s->context,slot->cookie,v.bytes);
        s->in_callback=false;
        if (!freed) { out->backend_failures++; out->retained++; continue; }
        status=dl_reclaim(&s->policy,slot->buffer);
        if (status != DL_OK) return status; /* impossible under the API contract */
        *slot=(dl_backing_slot){0};
        s->resident_bytes-=v.bytes;
        out->reclaimed++;
        out->reclaimed_bytes+=v.bytes;
    }
    return out->backend_failures ? DL_STORE_BACKEND_ERROR : DL_OK;
}

int dl_store_check_invariants(const dl_store *s) {
    int status=ready(s);
    if (status != DL_OK) return status;
    status=dl_check_invariants(&s->policy);
    if (status != DL_OK) return status;
    if (!s->page_size || (s->page_size & (s->page_size-1)) ||
        s->byte_limit < s->page_size) return DL_BAD_STATE;
    uint64_t sum=0;
    for (uint32_t i=0; i<DL_BUFFER_CAPACITY; i++) {
        const dl_backing_slot *slot=&s->backing[i];
        if (slot->occupied != s->policy.buffers[i].occupied) return DL_BAD_STATE;
        if (!slot->occupied) { if (slot->cookie) return DL_BAD_STATE; continue; }
        dl_view v;
        if (!slot->cookie || slot->buffer.index != i ||
            dl_inspect(&s->policy,slot->buffer,&v) != DL_OK ||
            (v.bytes & (s->page_size-1)) || v.bytes > UINT64_MAX-sum)
            return DL_BAD_STATE;
        sum+=v.bytes;
    }
    return sum == s->resident_bytes && sum <= s->byte_limit ? DL_OK : DL_BAD_STATE;
}

int dl_store_visit_backing(dl_store *s, dl_buffer b, dl_backing_visitor visit,
                           void *arg) {
    int status = ready(s);
    if (status != DL_OK) return status;
    if (!visit) return DL_INVALID;
    dl_view view;
    status = dl_inspect(&s->policy, b, &view);
    if (status != DL_OK) return status;
    if (!view.host && !view.device) return DL_BUSY;
    dl_backing_slot *slot = &s->backing[b.index];
    if (!slot->occupied || !slot->cookie) return DL_BAD_STATE;
    s->in_callback = true;
    status = visit(arg, slot->cookie, view.bytes);
    s->in_callback = false;
    return status;
}
