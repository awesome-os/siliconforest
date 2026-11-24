/* Original typed data ownership and detached mapping validation. */
#include "data.h"
#include "../m1-gpu-uat-builder/writer_internal.h"
#include <string.h>
static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (an - 1 > UINTPTR_MAX - x || bn - 1 > UINTPTR_MAX - y) return 1;
    return x <= y + bn - 1 && y <= x + an - 1;
}
static enum udata_code enter(struct udata_context *c) {
    if (!c || c->self != c) return UD_ARGUMENT;
    if (c->busy) return UD_STATE;
    c->busy = 1; return UD_OK;
}
static enum udata_code leave(struct udata_context *c, enum udata_code r) { c->busy = 0; return r; }
static int graph_identity(const struct uatb_context *g, struct uatb_ticket ticket) {
    return g && g->self == g && !g->busy && g->phase == UATB_VALID && !g->error &&
        ticket.owner == g && ticket.generation &&
        ticket.generation == g->generation && g->count && g->count <= DUAT_CAPACITY &&
        g->owner.self == &g->owner && !g->owner.failed && !g->owner.stopping;
}
static int graph_valid(const struct udata_context *c) { return graph_identity(c->graph, c->graph_ticket); }
static struct udata_slot *resolve(struct udata_context *c, struct udata_token t) {
    if (t.owner != c || !t.generation || t.slot >= UDATA_CAPACITY) return NULL;
    struct udata_slot *s = &c->slots[t.slot];
    return s->state != UD_EMPTY && s->generation == t.generation ? s : NULL;
}
static int admitted(const struct udata_view *v) {
    return v->bytes && !((uintptr_t)v->bytes % DUAT_BYTES) && v->length == DUAT_BYTES &&
        v->capacity == DUAT_BYTES && v->page_size == DUAT_BYTES && v->physical &&
        !(v->physical % DUAT_BYTES) && v->physical <= DUAT_PA_END - DUAT_BYTES && v->segment >= DUAT_BYTES;
}
static int same(const struct udata_view *a, const struct udata_view *b) {
    return a->bytes == b->bytes && a->length == b->length && a->capacity == b->capacity &&
        a->page_size == b->page_size && a->physical == b->physical && a->segment == b->segment;
}
static enum udata_code retained(struct udata_context *c, struct udata_slot *s) {
    s->state = UD_RETAINED; c->failed = 1; return UD_QUARANTINE;
}
static enum udata_code fresh(struct udata_context *c, struct udata_slot *s, struct udata_view *out) {
    struct udata_view v = {0};
    if (s->state != UD_PREPARED) return UD_STATE;
    if (c->ops.inspect(c->provider, s->handle, &v) || !same(&v, &s->view) || !admitted(&v))
        return retained(c, s);
    *out = v; return UD_OK;
}
static enum udata_code dispose(struct udata_context *c, struct udata_slot *s) {
    if (s->state == UD_EMPTY) return UD_OK;
    if (s->state == UD_RETAINED) return UD_QUARANTINE;
    if (s->state == UD_PREPARED && c->ops.complete(c->provider, s->handle)) return retained(c, s);
    c->ops.release(c->provider, s->handle); memset(s, 0, sizeof(*s)); return UD_OK;
}
/* Check table DATA separation before any zero operation or scratch writes. */
static enum udata_code separate(struct udata_context *c, const struct udata_view *v,
                                const struct udata_slot *skip) {
    if (!graph_valid(c)) return UD_GRAPH;
    for (size_t i = 0; i < c->graph->count; ++i) {
        struct duat_view table; void *handle;
        if (!uatb_owned_view(&c->graph->owner, c->graph->tokens[i], &table, &handle)) return UD_GRAPH;
        if (table.physical == v->physical || overlap(table.bytes, DUAT_BYTES, v->bytes, DUAT_BYTES)) return UD_OVERLAP;
    }
    for (unsigned i = 0; i < UDATA_CAPACITY; ++i) {
        struct udata_slot *s = &c->slots[i];
        if (s == skip || s->state == UD_EMPTY) continue;
        struct udata_view other;
        enum udata_code r = fresh(c, s, &other); if (r) return r;
        if (other.physical == v->physical || overlap(other.bytes, DUAT_BYTES, v->bytes, DUAT_BYTES)) return UD_OVERLAP;
    }
    return UD_OK;
}
enum udata_code udata_init(struct udata_context *c, const struct udata_ops *ops, void *provider,
                           struct uatb_context *graph, struct uatb_ticket ticket) {
    if (!c || !ops || !ops->allocate || !ops->prepare || !ops->inspect || !ops->zero ||
        !ops->complete || !ops->release) return UD_ARGUMENT;
    const unsigned char *b = (const unsigned char *)c;
    for (size_t i = 0; i < sizeof(*c); ++i) if (b[i]) return UD_STATE;
    /* No owner initialized until the exact borrowed graph identity is checked. */
    if (!graph_identity(graph, ticket)) return UD_GRAPH;
    if (overlap(c, sizeof(*c), graph, sizeof(*graph))) return UD_ARGUMENT;
    for (size_t i = 0; i < graph->count; ++i) {
        struct duat_view v; void *h;
        if (!uatb_owned_view(&graph->owner, graph->tokens[i], &v, &h)) return UD_GRAPH;
        if (overlap(c, sizeof(*c), v.bytes, DUAT_BYTES)) return UD_ARGUMENT;
    }
    c->ops = *ops; c->provider = provider; c->graph = graph; c->graph_ticket = ticket;
    c->next_generation = 1; c->self = c; return UD_OK;
}
enum udata_code udata_allocate(struct udata_context *c, struct udata_token *out) {
    enum udata_code r = enter(c); if (r) return r;
    if (!out || overlap(out, sizeof(*out), c, sizeof(*c))) return leave(c, UD_ARGUMENT);
    if (c->failed || c->stopping) return leave(c, UD_STATE);
    if (!graph_valid(c)) return leave(c, UD_GRAPH);
    if (overlap(out, sizeof(*out), c->graph, sizeof(*c->graph))) return leave(c, UD_ARGUMENT);
    if (!c->next_generation || c->next_generation == UINT64_MAX) return leave(c, UD_LIMIT);
    unsigned i; for (i = 0; i < UDATA_CAPACITY && c->slots[i].state != UD_EMPTY; ++i) {}
    if (i == UDATA_CAPACITY) return leave(c, UD_LIMIT);
    /* All table views must still be valid before invoking the allocator. */
    for (size_t j = 0; j < c->graph->count; ++j) {
        struct duat_view v; void *h;
        if (!uatb_owned_view(&c->graph->owner, c->graph->tokens[j], &v, &h)) return leave(c, UD_GRAPH);
        if (overlap(out, sizeof(*out), v.bytes, DUAT_BYTES)) return leave(c, UD_ARGUMENT);
    }
    for (unsigned j = 0; j < UDATA_CAPACITY; ++j)
        if (c->slots[j].state != UD_EMPTY && overlap(out, sizeof(*out), c->slots[j].view.bytes, DUAT_BYTES))
            return leave(c, UD_ARGUMENT);
    void *h = c->ops.allocate(c->provider); if (!h) return leave(c, UD_ALLOC);
    struct udata_slot *s = &c->slots[i];
    s->handle = h; s->state = UD_OWNED; s->generation = c->next_generation++;
    for (size_t j = 0; j < c->graph->count; ++j)
        if (c->graph->tokens[j].slot >= DUAT_CAPACITY ||
            c->graph->owner.slots[c->graph->tokens[j].slot].handle == h)
            return leave(c, retained(c, s));
    for (unsigned j = 0; j < UDATA_CAPACITY; ++j)
        if (j != i && c->slots[j].state != UD_EMPTY && c->slots[j].handle == h) {
            (void)retained(c, &c->slots[j]); return leave(c, retained(c, s));
        }
    if (c->ops.prepare(c->provider, h)) { (void)dispose(c, s); return leave(c, UD_PREPARE); }
    s->state = UD_PREPARED;
    struct udata_view v = {0};
    r = c->ops.inspect(c->provider, h, &v) || !admitted(&v) ? UD_VIEW : UD_OK;
    if (!r && (overlap(v.bytes, DUAT_BYTES, c, sizeof(*c)) ||
        overlap(v.bytes, DUAT_BYTES, c->graph, sizeof(*c->graph)) ||
        overlap(v.bytes, DUAT_BYTES, out, sizeof(*out)))) r = UD_OVERLAP;
    if (!r) r = separate(c, &v, s);
    if (r) { enum udata_code clean = dispose(c, s); return leave(c, clean ? clean : r); }
    s->view = v;
    /* Recheck graph immediately before granting the explicit private zero. */
    if (!graph_valid(c)) { enum udata_code clean = dispose(c, s); return leave(c, clean ? clean : UD_GRAPH); }
    enum udata_zero_result z = c->ops.zero(c->provider, h, &v);
    if (z != UD_ZEROED) {
        if (z != UD_ZERO_FAILED_PRIVATE) return leave(c, retained(c, s));
        enum udata_code clean = dispose(c, s); return leave(c, clean ? clean : UD_ZERO);
    }
    r = fresh(c, s, &v);
    if (r) return leave(c, r);
    for (size_t j = 0; j < DUAT_BYTES; ++j)
        if (v.bytes[j]) { enum udata_code clean = dispose(c, s); return leave(c, clean ? clean : UD_ZERO); }
    r = separate(c, &v, s);
    if (r) { enum udata_code clean = dispose(c, s); return leave(c, clean ? clean : r); }
    *out = (struct udata_token){c, s->generation, i}; return leave(c, UD_OK);
}
enum udata_code udata_release(struct udata_context *c, struct udata_token t) {
    enum udata_code r = enter(c); if (r) return r;
    struct udata_slot *s = resolve(c, t); return leave(c, s ? dispose(c, s) : UD_ARGUMENT);
}
enum udata_code udata_quarantine(struct udata_context *c, struct udata_token t) {
    enum udata_code r = enter(c); if (r) return r;
    struct udata_slot *s = resolve(c, t); return leave(c, s ? retained(c, s) : UD_ARGUMENT);
}
enum udata_code udata_shutdown(struct udata_context *c) {
    enum udata_code r = enter(c); if (r) return r;
    c->stopping = 1;
    for (unsigned i = 0; i < UDATA_CAPACITY; ++i) if (dispose(c, &c->slots[i])) c->failed = 1;
    return leave(c, c->failed ? UD_QUARANTINE : UD_OK);
}
enum udata_code udata_leaf_flags(uint32_t permissions, uint64_t *out) {
    if (!out) return UD_ARGUMENT;
    if (permissions != UD_READ && permissions != (UD_READ | UD_WRITE)) return UD_PERMISSION;
    /* G13 source interpretation only: OS=1, UXN=PXN=1, Shared=2, AF=nG=1,
     * SH=0, AP1 for RO / AP0 for RW. No executable mapping interpretation. */
    *out = UINT64_C(0x00e0000000000c08) | (permissions == UD_READ ? UINT64_C(0x40) : 0);
    return UD_OK;
}
static void store64(uint8_t *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static int target_node(const struct uatb_context *g, struct uat_indices ix) {
    uint32_t parent = UATB_NO_PARENT;
    const unsigned entries[] = {ix.root, ix.l1, ix.l2};
    for (unsigned level = 1; level <= 3; ++level) {
        size_t i;
        for (i = 0; i < g->count; ++i)
            if (g->plan[i].level == level && g->plan[i].parent == parent && g->plan[i].entry == entries[level - 1]) break;
        if (i == g->count) return -1;
        parent = (uint32_t)i;
    }
    return (int)parent;
}
enum udata_code udata_plan(struct udata_context *c, const struct udata_mapping *maps, size_t count,
    struct udata_workspace *w, size_t capacity, struct udata_summary *out) {
    enum udata_code r = enter(c); if (r) return r;
    if (c->failed || c->stopping) return leave(c, UD_STATE);
    if (!graph_valid(c)) return leave(c, UD_GRAPH);
    if (!maps || !count || count > UDATA_CAPACITY || !w || capacity < sizeof(*w) || !out ||
        (uintptr_t)w % _Alignof(struct udata_workspace)) return leave(c, UD_ARGUMENT);
    if (overlap(w, sizeof(*w), c, sizeof(*c)) || overlap(w, sizeof(*w), c->graph, sizeof(*c->graph)) ||
        overlap(w, sizeof(*w), maps, count * sizeof(*maps)) || overlap(w, sizeof(*w), out, sizeof(*out)) ||
        overlap(out, sizeof(*out), c, sizeof(*c)) || overlap(out, sizeof(*out), c->graph, sizeof(*c->graph)) ||
        overlap(out, sizeof(*out), maps, count * sizeof(*maps))) return leave(c, UD_ARGUMENT);
    struct udata_view views[UDATA_CAPACITY];
    unsigned nodes[UDATA_CAPACITY], entries[UDATA_CAPACITY];
    uint64_t flags[UDATA_CAPACITY];
    for (size_t i = 0; i < count; ++i) {
        struct udata_slot *s = resolve(c, maps[i].data);
        if (!s) return leave(c, UD_ARGUMENT);
        r = fresh(c, s, &views[i]); if (r) return leave(c, r);
        r = separate(c, &views[i], s); if (r) return leave(c, r);
        r = udata_leaf_flags(maps[i].permissions, &flags[i]); if (r) return leave(c, r);
        struct uat_indices ix; struct uat_va_span span;
        if (maps[i].va % DUAT_BYTES || uat_split_va(maps[i].va, &ix) != UAT_OK ||
            uat_va_range(maps[i].va, DUAT_BYTES, &span) != UAT_OK) return leave(c, UD_PLAN);
        int node = target_node(c->graph, ix); if (node < 0) return leave(c, UD_PLAN);
        nodes[i] = (unsigned)node; entries[i] = ix.l3;
        for (size_t j = 0; j < i; ++j)
            if (maps[i].va == maps[j].va || maps[i].data.slot == maps[j].data.slot) return leave(c, UD_PLAN);
    }
    /* All alias checks precede the first scratch write (including validator). */
    for (unsigned i = 0; i < UDATA_CAPACITY; ++i) if (c->slots[i].state != UD_EMPTY) {
        struct udata_view v; r = fresh(c, &c->slots[i], &v); if (r) return leave(c, r);
        if (overlap(w, sizeof(*w), v.bytes, DUAT_BYTES) || overlap(out, sizeof(*out), v.bytes, DUAT_BYTES))
            return leave(c, UD_ARGUMENT);
    }
    for (size_t i = 0; i < c->graph->count; ++i) {
        struct duat_view v; void *h;
        if (!uatb_owned_view(&c->graph->owner, c->graph->tokens[i], &v, &h)) return leave(c, UD_GRAPH);
        if (overlap(w, sizeof(*w), v.bytes, DUAT_BYTES) || overlap(out, sizeof(*out), v.bytes, DUAT_BYTES))
            return leave(c, UD_ARGUMENT);
    }
    struct duat_summary table_summary;
    if (uatb_validate(c->graph, c->graph_ticket, &w->graph, &table_summary) != UATB_OK) return leave(c, UD_GRAPH);
    for (size_t i = 0; i < count; ++i) {
        struct duat_view table; void *h;
        if (!uatb_owned_view(&c->graph->owner, c->graph->tokens[nodes[i]], &table, &h)) return leave(c, UD_GRAPH);
        size_t j;
        for (j = 0; j < table_summary.tables; ++j)
            if (w->graph.tables[j].address == table.physical && w->graph.tables[j].level == 3) break;
        if (j == table_summary.tables || w->graph.tables[j].bytes != w->graph.pages[j]) return leave(c, UD_GRAPH);
        uint64_t word;
        if (uat_encode_page(views[i].physical, flags[i], &word) != UAT_OK) return leave(c, UD_PLAN);
        store64(w->graph.pages[j] + entries[i] * 8, word);
        w->data_ranges[i] = (struct uat_range){views[i].physical, DUAT_BYTES};
    }
    const struct uat_snapshot snapshot = {w->graph.roots, sizeof(w->graph.roots), w->graph.tables,
        table_summary.tables, w->graph.ranges, w->data_ranges, table_summary.tables, count};
    struct uat_summary summary = {0};
    if (uat_validate(&snapshot, &summary) != UAT_OK || summary.pages != count ||
        summary.mapped_bytes != count * DUAT_BYTES) return leave(c, UD_PLAN);
    /* Revalidate actual owners without overwriting the detached leaf snapshot. */
    if (!graph_valid(c)) return leave(c, UD_GRAPH);
    for (size_t i = 0; i < count; ++i) {
        struct udata_slot *s = resolve(c, maps[i].data); struct udata_view now;
        if (!s) return leave(c, UD_ARGUMENT);
        r = fresh(c, s, &now); if (r) return leave(c, r);
        if (!same(&now, &views[i])) return leave(c, retained(c, s));
        r = separate(c, &now, s); if (r) return leave(c, r);
    }
    *out = (struct udata_summary){summary.tables, count, summary.mapped_bytes};
    return leave(c, UD_OK);
}
