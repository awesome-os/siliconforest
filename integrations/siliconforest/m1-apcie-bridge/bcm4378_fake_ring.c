/* Original finite ring/doorbell model, protocol facts from pinned ISC map. */
#include "bcm4378_fake_ring.h"

#if defined(KERNEL) && defined(BCR_FAKE_DEVICE)
#error "BCR_FAKE_DEVICE is host-test only; no kernel ring/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake ring requires shared PHY and PMGR writes compiled out"
#endif

#ifdef BCR_FAKE_DEVICE
#include "register_accessor.h"
#include <string.h>

static int guard(const struct bct_session *t)
{
    uint32_t phy = 0, run = 0, link = 0, pci = 0;
    if (!t || t->phase != BCT_SHARED || t->faulted || !apr_bcm_io_bound(&t->io)) return BCR_STATE;
    if (t->io.read32(t->io.context, APB_PHY, 0, &phy)) return BCR_IO;
    if ((phy & 0x8fu) != 0x0fu) return BCR_PHY_NOT_READY;
    if (t->io.read32(t->io.context, APB_PORT0, 0x804, &run)) return BCR_IO;
    if (!(run & 1)) return BCR_LINK_DOWN;
    if (t->io.read32(t->io.context, APB_PORT0, 0x208, &link)) return BCR_IO;
    if ((link & 0x45u) != 1) return BCR_LINK_DOWN;
    if (t->io.read32(t->io.context, APR_BCM_CONFIG, 0, &pci)) return BCR_IO;
    if (pci != 0x442514e4u) return BCR_OWNER;
    if (t->io.read32(t->io.context, APB_PHY, 0, &phy)) return BCR_IO;
    return (phy & 0x8fu) == 0x0fu ? BCR_FAKE_OK : BCR_PHY_NOT_READY;
}
static int error(struct bcr_ring *r, int e)
{
    if (e == BCR_IO || e == BCR_VERIFY || (e && r->inflight)) r->faulted = 1;
    return e;
}
static int enter(struct bcr_ring *r)
{
    if (!r || !r->bound || r->closed) return BCR_STATE;
    if (r->faulted) return BCR_FAULTED;
    return error(r, guard(r->transport));
}
static int read_word(struct bcr_ring *r, uint32_t address, uint32_t *value)
{
    int e = enter(r);
    if (e) return e;
    return r->transport->io.read32(r->transport->io.context, APR_BCM_BAR2, address, value) ? error(r, BCR_IO) : 0;
}
static int write_word(struct bcr_ring *r, unsigned space, uint32_t address, uint32_t value)
{
    int e = enter(r);
    if (e) return e;
    if (r->register_writes == UINT32_MAX) return error(r, BCR_RANGE);
    r->dirty = r->inflight = 1; r->register_writes++;
    return r->transport->io.write32(r->transport->io.context, space, address, value) ? error(r, BCR_IO) : 0;
}
static int ram_span(const struct bct_session *t, uint32_t address, uint32_t size)
{
    return t->plan.ram_bytes >= size && t->plan.ram_bytes <= BCT_MAX_RAM &&
        !(t->plan.ram_bytes & 3u) && !(address & 3u) && address >= BCT_RAM_BASE &&
        address - BCT_RAM_BASE <= t->plan.ram_bytes - size;
}
static int overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t k)
{ return a < b+k && b < a+n; } /* operands bounded inside <=8MiB BAR2 */
static int pointers_overlap(const void *a, size_t n, const void *b, size_t k)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (n > UINTPTR_MAX-x || k > UINTPTR_MAX-y) return 1;
    return x < y+k && y < x+n;
}
static int metadata(struct bcr_ring *r)
{
    const struct bct_session *t = r->transport;
    uint32_t flags = 0, info = 0, values[3] = {0};
    if (!ram_span(t, t->shared_address, BCR_SHARED_GUARD_BYTES) || !ram_span(t, t->ring_info_address, BCR_INFO_GUARD_BYTES) ||
        !ram_span(t, t->h2d_address, 4) || !ram_span(t, t->d2h_address, 4)) return BCR_RANGE;
    int e = read_word(r, t->shared_address, &flags);
    if (e) return e;
    if (flags != t->shared_flags || (flags & 0x10000u) || (flags & 255u) < 5 || (flags & 255u) > 7)
        return BCR_STATE; /* DMA-index mode and changed firmware metadata unsupported */
    if ((e = read_word(r, t->shared_address+48, &info))) return e;
    if (info != t->ring_info_address) return BCR_OWNER;
    const uint32_t sizes[3] = {16, 4, 4};
    const uint32_t protected_base[5] = {t->shared_address, info, t->h2d_address, t->d2h_address,
                                       BCT_RAM_BASE+t->plan.ram_bytes-4};
    const uint32_t protected_size[5] = {BCR_SHARED_GUARD_BYTES, BCR_INFO_GUARD_BYTES, 4, 4, 4};
    for (unsigned i = 0; i < 5; ++i) for (unsigned j = 0; j < i; ++j)
        if (overlap(protected_base[i], protected_size[i], protected_base[j], protected_size[j])) return BCR_RANGE;
    for (unsigned i = 0; i < 3; ++i) if ((e = read_word(r, info+i*4, &values[i]))) return e;
    for (unsigned i = 0; i < 3; ++i) {
        if (!ram_span(t, values[i], sizes[i])) return BCR_RANGE;
        for (unsigned j = 0; j < 5; ++j)
            if (overlap(values[i], sizes[i], protected_base[j], protected_size[j])) return BCR_RANGE;
        for (unsigned j = 0; j < i; ++j)
            if (overlap(values[i], sizes[i], values[j], sizes[j])) return BCR_RANGE;
    }
    r->descriptor = values[0]; r->write_address = values[1]; r->read_address = values[2];
    r->bell_offset = 0x2000u + ((flags & 0x80000000u) ? 0xa20u : 0x140u);
    return BCR_FAKE_OK;
}
static int indices(struct bcr_ring *r, uint32_t *write_word_value, uint32_t *read_index)
{
    uint32_t w = 0, read = 0;
    int e = read_word(r, r->write_address, &w);
    if (e) return e;
    if ((e = read_word(r, r->read_address, &read))) return e;
    if ((w & 0xffffu) >= r->plan.depth || (read & 0xffffu) >= r->plan.depth ||
        (w & 0xffffu) != r->write_index) return error(r, BCR_VERIFY);
    *write_word_value = w; *read_index = read & 0xffffu;
    return BCR_FAKE_OK;
}

int bcr_fake_ring_compiled(void) { return 1; }
int bcr_open(struct bcr_ring *r, struct bct_session *t, const struct bcr_plan *p)
{
    struct bcr_ring next = {0};
    int e;
    if (!r || !t || !p || !p->buffer) return BCR_ARGUMENT;
    if (r->bound || r->closed || r->faulted) return BCR_STATE;
    if (p->depth < 2 || p->depth > BCR_MAX_DEPTH || p->item_bytes < 4 ||
        p->item_bytes > BCR_MAX_ITEM || (p->item_bytes & 3u) ||
        p->capacity != (size_t)p->depth*p->item_bytes || !p->fake_bus_label ||
        (p->fake_bus_label & 3u) || p->capacity > UINT64_MAX-p->fake_bus_label) return BCR_RANGE;
    if (pointers_overlap(r, sizeof *r, p->buffer, p->capacity) ||
        pointers_overlap(t, sizeof *t, p->buffer, p->capacity)) return BCR_ARGUMENT;
    if ((e = guard(t))) return e;
    next.bound = 1; next.transport = t; next.plan = *p;
    if ((e = metadata(&next))) return e;
    *r = next;
    return BCR_FAKE_OK;
}

int bcr_configure(struct bcr_ring *r)
{
    uint32_t w = 0, read = 0, word = 0;
    int e = enter(r);
    if (e) return e;
    if (r->configured) return BCR_STATE;
    for (unsigned i = 0; i < 4; ++i) {
        if ((e = read_word(r, r->descriptor+i*4, &word))) return e;
        if (word) return BCR_OWNER; /* never adopt another owner's descriptor */
    }
    if ((e = indices(r, &w, &read))) return e;
    if (read || (w & 0xffffu)) return BCR_BUSY;
    if ((e = write_word(r, APR_BCM_BAR2, r->descriptor+8, (uint32_t)r->plan.fake_bus_label))) return e;
    if ((e = write_word(r, APR_BCM_BAR2, r->descriptor+12, (uint32_t)(r->plan.fake_bus_label >> 32)))) return e;
    word = r->plan.depth | (r->plan.item_bytes << 16);
    if ((e = write_word(r, APR_BCM_BAR2, r->descriptor+4, word))) return e;
    uint32_t expected[3] = {word, (uint32_t)r->plan.fake_bus_label, (uint32_t)(r->plan.fake_bus_label >> 32)};
    for (unsigned i = 0; i < 3; ++i) {
        if ((e = read_word(r, r->descriptor+4+i*4, &word))) return e;
        if (word != expected[i]) return error(r, BCR_VERIFY);
    }
    r->configured = 1; r->inflight = 0;
    return BCR_FAKE_OK;
}

int bcr_submit(struct bcr_ring *r, const uint8_t *item, size_t bytes)
{
    uint32_t w = 0, read = 0, next, verify = 0;
    int e = enter(r);
    if (e) return e;
    if (!r->configured) return BCR_STATE;
    if (!item || bytes != r->plan.item_bytes || pointers_overlap(item, bytes, r->plan.buffer, r->plan.capacity))
        return BCR_ARGUMENT;
    if (r->buffer_writes == UINT32_MAX || r->index_write_attempts == UINT32_MAX ||
        r->index_published == UINT32_MAX || r->bell_attempts == UINT32_MAX || r->completed == UINT32_MAX)
        return BCR_RANGE;
    if ((e = indices(r, &w, &read))) return e;
    next = (r->write_index+1) % r->plan.depth;
    if (next == read) return BCR_FULL;
    if ((e = enter(r))) return e;
    r->dirty = r->inflight = 1; r->buffer_writes++;
    memcpy(r->plan.buffer+r->write_index*r->plan.item_bytes, item, bytes);
    /* Fresh word snapshot preserves high16 and detects a competing producer.
     * This RMW models a16-bit write only in the serialized fake file. */
    if ((e = indices(r, &w, &read))) return e;
    if ((w & 0xffffu) != r->write_index) return error(r, BCR_VERIFY);
    r->index_write_attempts++;
    if ((e = write_word(r, APR_BCM_BAR2, r->write_address, (w & 0xffff0000u) | next))) return e;
    if ((e = read_word(r, r->write_address, &verify))) return e;
    if ((verify & 0xffffu) != next) return error(r, BCR_VERIFY);
    r->write_index = next; r->index_published++;
    r->bell_attempts++;
    if ((e = write_word(r, APR_BCM_BAR0, r->bell_offset, 1))) return e;
    r->completed++; r->inflight = 0;
    return BCR_FAKE_OK;
}

int bcr_close(struct bcr_ring *r)
{
    uint32_t w = 0, read = 0;
    int e = enter(r);
    if (e) return e;
    if (!r->configured) return BCR_STATE;
    if ((e = indices(r, &w, &read))) return e;
    if ((w & 0xffffu) != read) return BCR_BUSY;
    r->closed = 1; r->configured = 0; /* retain descriptor/buffer; not DMA drain */
    return BCR_FAKE_OK;
}
#else
int bcr_fake_ring_compiled(void) { return 0; }
int bcr_open(struct bcr_ring *r, struct bct_session *t, const struct bcr_plan *p)
{ (void)r;(void)t;(void)p; return BCR_GATED; }
int bcr_configure(struct bcr_ring *r) { (void)r; return BCR_GATED; }
int bcr_submit(struct bcr_ring *r, const uint8_t *p, size_t n)
{ (void)r;(void)p;(void)n; return BCR_GATED; }
int bcr_close(struct bcr_ring *r) { (void)r; return BCR_GATED; }
#endif
