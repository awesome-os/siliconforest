#include "memory_probe.h"
#include "mapping_ticket.h"
#define DLMP_MAGIC UINT64_C(0x444c4d50524f4245)
struct session { uint64_t id; bool bound; dl_buffer buffer; };
struct dl_probe {
    uint64_t magic, next_session, page_size, reclaimed;
    dl_store store;
    dl_probe_ops ops;
    void *context;
    struct session sessions[DLMP_SESSION_CAPACITY];
    dl_map_ticket tickets[DLMP_TICKET_CAPACITY];
    /* Index alone can refer to a later allocation. Only this serial is exported. */
    uint64_t exported_serial[DL_BUFFER_CAPACITY];
    uint64_t collection_quarantined;
    unsigned used_tickets;
    bool in_provider;
};
size_t dl_probe_size(void) { return sizeof(dl_probe); }
size_t dl_probe_alignment(void) { return _Alignof(dl_probe); }
static int ready(dl_probe *p) {
    if (!p || p->magic != DLMP_MAGIC) return DL_INVALID;
    return p->in_provider || p->store.in_callback ? DL_BAD_STATE : DL_OK;
}
static bool quarantined(const dl_probe *p, const dl_backing_slot *slot) {
    return !(p->ops.capabilities & DLMP_CAP_MAPPED_RECLAIM) && slot->occupied &&
           p->exported_serial[slot->buffer.index] == slot->buffer.serial;
}
static void *allocate_backing(void *arg, uint64_t bytes, uint64_t alignment) {
    dl_probe *p = arg;
    return p->ops.backing.allocate(p->context, bytes, alignment);
}
static bool release_backing(void *arg, void *cookie, uint64_t bytes) {
    dl_probe *p = arg;
    /* The store calls this under serialization with its slot still occupied.
     * Match the current index AND generation, independently of provider state.
     * Check all matching slots rather than assuming provider cookies are unique. */
    for (unsigned i=0; i<DL_BUFFER_CAPACITY; ++i) {
        const dl_backing_slot *slot = &p->store.backing[i];
        if (slot->cookie == cookie && quarantined(p, slot)) {
            ++p->collection_quarantined;
            return false; /* store retains ownership and its entire byte charge */
        }
    }
    return p->ops.backing.release(p->context, cookie, bytes);
}
int dl_probe_init(void *memory, size_t size, uint64_t page, uint64_t budget,
                  dl_probe_ops ops, void *context, dl_probe **out) {
    if (!memory || !out || size < sizeof(dl_probe) ||
        (uintptr_t)memory % _Alignof(dl_probe) ||
        (page != 4096 && page != 16384 && page != 65536) ||
        budget < page || budget > DLMP_BUDGET || !ops.pages || !ops.exported ||
        !ops.backing.allocate || !ops.backing.release ||
        !(ops.capabilities & DLMP_CAP_RAM_ONLY) ||
        (ops.capabilities & ~(DLMP_CAP_RAM_ONLY | DLMP_CAP_MAPPED_RECLAIM |
                              DLMP_CAP_KERNEL_BACKEND))) return DL_INVALID;
    dl_probe *p = memory;
    if (p->magic) return DL_BAD_STATE; /* caller supplies fresh zeroed storage */
    /* Initialize in place: a compound literal here can create a ticket-array-
     * sized temporary on a kernel stack, especially in unoptimized builds. */
    __builtin_memset(p, 0, sizeof(*p));
    p->next_session=1; p->page_size=page; p->ops=ops; p->context=context;
    dl_backing_ops backing = {allocate_backing, release_backing};
    int status = dl_store_init(&p->store, 1, page, budget, backing, p);
    if (status != DL_OK) return status;
    p->magic = DLMP_MAGIC;
    *out = p;
    return DL_OK;
}
static struct session *find(dl_probe *p, uint64_t id) {
    if (!id) return NULL;
    for (unsigned i=0; i<DLMP_SESSION_CAPACITY; ++i)
        if (p->sessions[i].id == id) return &p->sessions[i];
    return NULL;
}
int dl_probe_open(dl_probe *p, uint64_t *out) {
    int status = ready(p);
    if (status != DL_OK) return status;
    if (!out) return DL_INVALID;
    if (p->next_session == UINT64_MAX) return DL_OVERFLOW;
    for (unsigned i=0; i<DLMP_SESSION_CAPACITY; ++i) {
        if (p->sessions[i].id) continue;
        p->sessions[i] = (struct session){.id=p->next_session++};
        *out = p->sessions[i].id;
        return DL_OK;
    }
    return DL_CAPACITY;
}
int dl_probe_close(dl_probe *p, uint64_t id) {
    int status = ready(p);
    if (status != DL_OK) return status;
    struct session *s = find(p,id);
    if (!s) return DL_STALE;
    if (s->bound) {
        status = dl_store_close_owner(&p->store, s->buffer);
        if (status != DL_OK) return status;
    }
    *s = (struct session){0};
    return DL_OK; /* destruction never occurs on close */
}
static int stats(dl_probe *p, struct session *s, struct dlmp_stats *out) {
    struct dlmp_stats v = {.version=DLMP_VERSION, .resident_bytes=p->store.resident_bytes,
                           .reclaimed_buffers=p->reclaimed};
    for (unsigned i=0;i<DLMP_SESSION_CAPACITY;i++) if(p->sessions[i].id) ++v.open_sessions;
    for (unsigned i=0;i<DL_BUFFER_CAPACITY;i++) {
        const dl_backing_slot *slot=&p->store.backing[i];
        if(slot->occupied) ++v.live_buffers;
        if(quarantined(p,slot)) ++v.quarantined_buffers;
    }
    for (unsigned i=0;i<p->used_tickets;i++) {
        if(p->tickets[i].phase == DL_MAP_PUBLISHED) { ++v.published_maps; ++v.pending_maps; }
        if(p->tickets[i].phase == DL_MAP_DRAINED) { ++v.published_maps; ++v.drained_maps; }
    }
    if(s->bound) {
        dl_view view;
        int status=dl_store_inspect(&p->store,s->buffer,&view);
        if(status!=DL_OK) return status;
        v.own_bytes=view.bytes; v.own_object=s->buffer.serial;
    }
    *out=v;
    return DL_OK;
}
static int collect(dl_probe *p, struct dlmp_collect *out) {
    struct dlmp_collect v = {.version=DLMP_VERSION};
    for(unsigned i=0;i<p->used_tickets;i++) {
        if(p->tickets[i].phase != DL_MAP_PUBLISHED) continue;
        int status=dl_map_drain(&p->tickets[i]);
        if(status==DL_OK) ++v.drained;
        else if(status!=DL_BUSY) return status;
    }
    dl_collection c;
    p->collection_quarantined=0;
    int status=dl_store_collect(&p->store,&c);
    if(status!=DL_OK && status!=DL_STORE_BACKEND_ERROR) return status;
    v.reclaimed=c.reclaimed; v.retained=c.retained; v.backend_failures=c.backend_failures;
    v.quarantined=p->collection_quarantined;
    p->reclaimed += c.reclaimed;
    *out=v;
    return DL_OK; /* caller must inspect backend_failures; never hidden */
}
int dl_probe_command(dl_probe *p,uint64_t id,unsigned operation,void *message,size_t size) {
    int status=ready(p);
    if(status!=DL_OK) return status;
    struct session *s=find(p,id);
    if(!s) return DL_STALE;
    if(!message || (uintptr_t)message % _Alignof(uint64_t)) return DL_INVALID;
    size_t wanted;
    switch(operation) {
        case DLMP_OP_INFO:wanted=sizeof(struct dlmp_info);break;
        case DLMP_OP_ALLOC:wanted=sizeof(struct dlmp_alloc);break;
        case DLMP_OP_STATS:wanted=sizeof(struct dlmp_stats);break;
        case DLMP_OP_COLLECT:wanted=sizeof(struct dlmp_collect);break;
        default:return DL_INVALID;
    }
    if(size!=wanted) return DL_INVALID;
    /* Read common header without violating aliasing between message types. */
    const unsigned char *raw=message;
    uint32_t version,flags;
    __builtin_memcpy(&version,raw,sizeof version);
    __builtin_memcpy(&flags,raw+sizeof version,sizeof flags);
    if(version!=DLMP_VERSION || flags) return DL_INVALID;
    if(operation==DLMP_OP_INFO) {
        *(struct dlmp_info *)message=(struct dlmp_info){DLMP_VERSION,0,p->page_size,
                                                      DLMP_MAX_ALLOCATION,p->ops.capabilities};
        return DL_OK;
    }
    if(operation==DLMP_OP_STATS) return stats(p,s,message);
    if(operation==DLMP_OP_COLLECT) return collect(p,message);
    struct dlmp_alloc *a=message;
    if(s->bound) return DL_BUSY;
    if(!a->bytes || a->bytes>DLMP_MAX_ALLOCATION || a->object_id || a->reserved[0] || a->reserved[1])
        return DL_INVALID;
    status=dl_store_allocate(&p->store,a->bytes,&s->buffer);
    if(status!=DL_OK) return status;
    s->bound=true;
    dl_view view;
    status=dl_store_inspect(&p->store,s->buffer,&view);
    if(status!=DL_OK) return status;
    a->bytes=view.bytes; a->object_id=s->buffer.serial;
    return DL_OK;
}
struct visit_context {
    dl_probe *p; uint64_t offset,bytes; uint32_t *pages; size_t count; bool export_only;
};
static int visit(void *arg,void *cookie,uint64_t total) {
    struct visit_context *v=arg;
    if(v->offset>total || v->bytes>total-v->offset) return DL_INVALID;
    v->p->in_provider=true;
    int result=DL_OK;
    if(v->export_only) v->p->ops.exported(v->p->context,cookie);
    else result=v->p->ops.pages(v->p->context,cookie,v->offset,v->bytes,
                                v->p->page_size,v->pages,v->count);
    v->p->in_provider=false;
    return result;
}
int dl_probe_map(dl_probe *p,uint64_t id,uint64_t offset,uint64_t bytes,unsigned access,
                 uint32_t *pages,size_t count,void (**release)(void *),void **arg) {
    int status=ready(p);
    if(status!=DL_OK) return status;
    struct session *s=find(p,id);
    if(!s) return DL_STALE;
    if(!s->bound || !pages || !release || !arg || count!=bytes/p->page_size ||
        !bytes || bytes%p->page_size) return DL_INVALID;
    if(p->used_tickets==DLMP_TICKET_CAPACITY) return DL_CAPACITY;
    dl_map_ticket *t=&p->tickets[p->used_tickets];
    dl_map_request request={offset,bytes,access,DL_MAP_READ|DL_MAP_WRITE};
    status=dl_map_prepare(&p->store,s->buffer,request,t);
    if(status!=DL_OK) return status;
    ++p->used_tickets; /* single use, even if the provider rejects enumeration */
    struct visit_context v={p,offset,bytes,pages,count,false};
    status=dl_store_visit_backing(&p->store,s->buffer,visit,&v);
    if(status!=DL_OK) { (void)dl_map_abort(t); return status; }
    v.export_only=true;
    /* Latch engine ownership before any possible exposure. A no-op provider
     * notification cannot authorize reclamation. Unexpected later failures
     * conservatively retain this generation; no API clears the export mark. */
    p->exported_serial[s->buffer.index]=s->buffer.serial;
    status=dl_store_visit_backing(&p->store,s->buffer,visit,&v);
    if(status!=DL_OK) { (void)dl_map_abort(t); return status; }
    dl_map_handoff h;
    status=dl_map_publish(t,&h);
    if(status!=DL_OK) return status; /* retain on unexpected internal failure */
    *release=h.release; *arg=h.release_arg;
    return DL_OK;
}
