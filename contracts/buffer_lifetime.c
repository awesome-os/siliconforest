#include "buffer_lifetime.h"

static bool is_device(dl_lease_kind k) {
    return k == DL_GPU_BIND || k == DL_IN_FLIGHT || k == DL_FIRMWARE;
}
static bool valid_domain(const dl_domain *d) { return d && d->session != 0; }
static dl_status buffer_check(const dl_domain *d, dl_buffer h) {
    if (!valid_domain(d)) return DL_INVALID;
    if (h.session != d->session || h.index >= DL_BUFFER_CAPACITY || !h.serial)
        return DL_STALE;
    const dl_buffer_slot *s = &d->buffers[h.index];
    return s->occupied && s->serial == h.serial ? DL_OK : DL_STALE;
}
static dl_status lease_check(const dl_domain *d, dl_lease h) {
    if (!valid_domain(d)) return DL_INVALID;
    if (h.session != d->session || h.index >= DL_LEASE_CAPACITY || !h.serial)
        return DL_STALE;
    const dl_lease_slot *s = &d->leases[h.index];
    return s->occupied && s->serial == h.serial ? DL_OK : DL_STALE;
}
dl_status dl_init(dl_domain *d, uint64_t session) {
    if (!d || !session) return DL_INVALID;
    /* Avoid a domain-sized automatic temporary in unoptimized kernel builds. */
    d->session=session;
    d->next_buffer=1;
    d->next_lease=1;
    d->reset_sequence=0;
    d->device_lost=false;
    for (unsigned i=0; i<DL_BUFFER_CAPACITY; ++i)
        d->buffers[i]=(dl_buffer_slot){0};
    for (unsigned i=0; i<DL_LEASE_CAPACITY; ++i)
        d->leases[i]=(dl_lease_slot){0};
    return DL_OK;
}
dl_status dl_allocate(dl_domain *d, uint64_t bytes, uint64_t page, dl_buffer *out) {
    if (!valid_domain(d) || !out || !bytes || !page || (page & (page - 1)))
        return DL_INVALID;
    if (d->device_lost) return DL_DEVICE_LOST;
    if (bytes > UINT64_MAX - (page - 1) || d->next_buffer == UINT64_MAX)
        return DL_OVERFLOW;
    for (uint32_t i=0; i<DL_BUFFER_CAPACITY; i++) {
        if (!d->buffers[i].occupied) {
            uint64_t serial=d->next_buffer++;
            d->buffers[i]=(dl_buffer_slot){true, true, serial, (bytes+page-1)&~(page-1)};
            *out=(dl_buffer){i, serial, d->session};
            return DL_OK;
        }
    }
    return DL_CAPACITY;
}
dl_status dl_hold(dl_domain *d, dl_buffer b, dl_lease_kind kind, dl_lease *out) {
    dl_status status=buffer_check(d,b);
    if (status != DL_OK) return status;
    if (!out || kind < DL_CPU_MAP || kind >= DL_KIND_COUNT) return DL_INVALID;
    if (d->device_lost) return DL_DEVICE_LOST;
    if (!d->buffers[b.index].owner_alive) return DL_OWNER_GONE;
    if (d->next_lease == UINT64_MAX) return DL_OVERFLOW;
    for (uint32_t i=0; i<DL_LEASE_CAPACITY; i++) {
        if (!d->leases[i].occupied) {
            uint64_t serial=d->next_lease++;
            d->leases[i]=(dl_lease_slot){true,serial,b,kind};
            *out=(dl_lease){i,serial,d->session};
            return DL_OK;
        }
    }
    return DL_CAPACITY;
}
dl_status dl_close_owner(dl_domain *d, dl_buffer b) {
    dl_status status=buffer_check(d,b);
    if (status != DL_OK) return status;
    if (!d->buffers[b.index].owner_alive) return DL_OWNER_GONE;
    d->buffers[b.index].owner_alive=false;
    return DL_OK;
}
static dl_status release_exact(dl_domain *d, dl_lease l, dl_lease_kind kind) {
    dl_status status=lease_check(d,l);
    if (status != DL_OK) return status;
    if (d->leases[l.index].kind != kind) return DL_INVALID;
    if (is_device(kind) && d->device_lost) return DL_DEVICE_LOST;
    d->leases[l.index].occupied=false;
    return DL_OK;
}
dl_status dl_release_host(dl_domain *d, dl_lease l) {
    dl_status status=lease_check(d,l);
    if (status != DL_OK) return status;
    if (is_device(d->leases[l.index].kind)) return DL_INVALID;
    return release_exact(d,l,d->leases[l.index].kind);
}
dl_status dl_unbind_gpu(dl_domain *d, dl_lease l) {return release_exact(d,l,DL_GPU_BIND);}
dl_status dl_complete_job(dl_domain *d, dl_lease l) {return release_exact(d,l,DL_IN_FLIGHT);}
dl_status dl_ack_firmware_release(dl_domain *d, dl_lease l) {return release_exact(d,l,DL_FIRMWARE);}
dl_status dl_mark_device_lost(dl_domain *d) {
    if (!valid_domain(d)) return DL_INVALID;
    d->device_lost=true;
    return DL_OK;
}
dl_status dl_timeout(dl_domain *d, dl_lease l) {
    dl_status status=lease_check(d,l);
    if (status != DL_OK) return status;
    dl_lease_kind kind=d->leases[l.index].kind;
    if (kind != DL_IN_FLIGHT && kind != DL_FIRMWARE) return DL_INVALID;
    return dl_mark_device_lost(d);
}
dl_status dl_backend_ack_quiescence(dl_domain *d, uint64_t sequence) {
    if (!valid_domain(d)) return DL_INVALID;
    if (!d->device_lost) return DL_BAD_STATE;
    if (!sequence || sequence <= d->reset_sequence) return DL_STALE;
    for (uint32_t i=0; i<DL_LEASE_CAPACITY; i++)
        if (d->leases[i].occupied && is_device(d->leases[i].kind))
            d->leases[i].occupied=false;
    d->reset_sequence=sequence;
    d->device_lost=false;
    return DL_OK;
}
dl_status dl_inspect(const dl_domain *d, dl_buffer b, dl_view *out) {
    dl_status status=buffer_check(d,b);
    if (status != DL_OK) return status;
    if (!out) return DL_INVALID;
    *out=(dl_view){.owner_alive=d->buffers[b.index].owner_alive,
                   .bytes=d->buffers[b.index].bytes};
    for (uint32_t i=0; i<DL_LEASE_CAPACITY; i++) {
        const dl_lease_slot *l=&d->leases[i];
        if (l->occupied && l->buffer.index == b.index && l->buffer.serial == b.serial) {
            if (is_device(l->kind)) out->device++; else out->host++;
        }
    }
    return DL_OK;
}
dl_status dl_reclaim(dl_domain *d, dl_buffer b) {
    dl_view view;
    dl_status status=dl_inspect(d,b,&view);
    if (status != DL_OK) return status;
    if (d->device_lost) return DL_DEVICE_LOST;
    if (view.owner_alive || view.host || view.device) return DL_BUSY;
    d->buffers[b.index].occupied=false;
    return DL_OK;
}
dl_status dl_check_invariants(const dl_domain *d) {
    if (!valid_domain(d) || !d->next_buffer || !d->next_lease) return DL_INVALID;
    for (uint32_t i=0; i<DL_BUFFER_CAPACITY; i++) {
        const dl_buffer_slot *b=&d->buffers[i];
        if (!b->occupied) continue;
        if (!b->serial || b->serial >= d->next_buffer || !b->bytes) return DL_BAD_STATE;
        for (uint32_t j=0; j<i; j++)
            if (d->buffers[j].occupied && d->buffers[j].serial == b->serial)
                return DL_BAD_STATE;
    }
    for (uint32_t i=0; i<DL_LEASE_CAPACITY; i++) {
        const dl_lease_slot *l=&d->leases[i];
        if (!l->occupied) continue;
        if (!l->serial || l->serial >= d->next_lease ||
            l->kind < DL_CPU_MAP || l->kind >= DL_KIND_COUNT ||
            buffer_check(d,l->buffer) != DL_OK) return DL_BAD_STATE;
        for (uint32_t j=0; j<i; j++)
            if (d->leases[j].occupied && d->leases[j].serial == l->serial)
                return DL_BAD_STATE;
    }
    return DL_OK;
}
