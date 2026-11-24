/* Original code; no device operations or writes to allocated table pages. */
#include "private.h"
#include <string.h>

static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (an - 1 > UINTPTR_MAX - x || bn - 1 > UINTPTR_MAX - y) return 1;
    return x <= y + bn - 1 && y <= x + an - 1;
}
static enum duat_result enter(struct duat_context *c) {
    if (!c || c->self != c) return DUAT_ARGUMENT;
    if (c->busy) return DUAT_STATE;
    c->busy = 1;
    return DUAT_OK;
}
static enum duat_result leave(struct duat_context *c, enum duat_result r) {
    c->busy = 0;
    return r;
}
static struct duat_slot *resolve(struct duat_context *c, struct duat_token t) {
    if (t.owner != c || !t.generation || t.slot >= DUAT_CAPACITY) return NULL;
    struct duat_slot *s = &c->slots[t.slot];
    return s->state != DUAT_EMPTY && s->generation == t.generation ? s : NULL;
}
static enum duat_result dispose(struct duat_context *c, struct duat_slot *s) {
    if (s->state == DUAT_EMPTY) return DUAT_OK;
    if (s->state == DUAT_RETAINED) return DUAT_QUARANTINE;
    if (s->state == DUAT_PREPARED && c->ops.complete(c->provider, s->handle)) {
        s->state = DUAT_RETAINED;
        c->failed = 1;
        return DUAT_QUARANTINE;
    }
    c->ops.release(c->provider, s->handle);
    memset(s, 0, sizeof(*s));
    return DUAT_OK;
}
static int admitted(const struct duat_view *v) {
    return v->bytes && !((uintptr_t)v->bytes % DUAT_BYTES) &&
        v->length == DUAT_BYTES && v->capacity == DUAT_BYTES &&
        v->page_size == DUAT_BYTES && v->physical &&
        !(v->physical % DUAT_BYTES) && v->segment >= DUAT_BYTES &&
        v->physical <= DUAT_PA_END - DUAT_BYTES;
}
static int same_view(const struct duat_view *a, const struct duat_view *b) {
    return a->bytes == b->bytes && a->length == b->length &&
        a->capacity == b->capacity && a->page_size == b->page_size &&
        a->physical == b->physical && a->segment == b->segment;
}
enum duat_result duat_init(struct duat_context *c, const struct duat_ops *o, void *p) {
    if (!c || !o || !o->allocate || !o->prepare || !o->inspect ||
        !o->complete || !o->release) return DUAT_ARGUMENT;
    /* Read object representation: caller must supply completely zeroed RAM. */
    const unsigned char *bytes = (const unsigned char *)c;
    for (size_t i = 0; i < sizeof(*c); ++i)
        if (bytes[i]) return DUAT_STATE;
    c->ops = *o;
    c->provider = p;
    c->next_generation = 1;
    c->self = c;
    return DUAT_OK;
}
enum duat_result duat_allocate(struct duat_context *c, unsigned level,
                             struct duat_token *out) {
    if (!out || level < 1 || level > 3 || !c ||
        overlap(out, sizeof(*out), c, sizeof(*c))) return DUAT_ARGUMENT;
    enum duat_result r = enter(c);
    if (r) return r;
    if (c->failed || c->stopping) return leave(c, DUAT_STATE);
    if (!c->next_generation || c->next_generation == UINT64_MAX)
        return leave(c, DUAT_LIMIT);
    unsigned i;
    for (i = 0; i < DUAT_CAPACITY && c->slots[i].state != DUAT_EMPTY; ++i) {}
    if (i == DUAT_CAPACITY) return leave(c, DUAT_LIMIT);
    /* Reject outputs within existing table storage before calling provider. */
    for (unsigned j = 0; j < DUAT_CAPACITY; ++j)
        if (c->slots[j].state != DUAT_EMPTY && c->slots[j].view.bytes &&
            overlap(out, sizeof(*out), c->slots[j].view.bytes, DUAT_BYTES))
            return leave(c, DUAT_ARGUMENT);
    void *h = c->ops.allocate(c->provider);
    if (!h) return leave(c, DUAT_ALLOC);
    struct duat_slot *s = &c->slots[i];
    s->handle = h;
    s->generation = c->next_generation++;
    s->level = level;
    s->state = DUAT_OWNED;
    /* Provider contract violation: do not prepare/complete a repeated handle. */
    for (unsigned j = 0; j < DUAT_CAPACITY; ++j)
        if (j != i && c->slots[j].state != DUAT_EMPTY && c->slots[j].handle == h) {
            c->slots[j].state = s->state = DUAT_RETAINED;
            c->failed = 1;
            return leave(c, DUAT_QUARANTINE);
        }
    if (c->ops.prepare(c->provider, h)) {
        (void)dispose(c, s);
        return leave(c, DUAT_PREPARE);
    }
    s->state = DUAT_PREPARED;
    struct duat_view v = {0};
    r = DUAT_OK;
    if (c->ops.inspect(c->provider, h, &v) || !admitted(&v)) r = DUAT_RANGE;
    if (!r && (overlap(v.bytes, DUAT_BYTES, c, sizeof(*c)) ||
               overlap(v.bytes, DUAT_BYTES, out, sizeof(*out)))) r = DUAT_RANGE;
    for (unsigned j = 0; !r && j < DUAT_CAPACITY; ++j) {
        const struct duat_slot *other = &c->slots[j];
        if (j != i && other->state != DUAT_EMPTY &&
            (other->view.physical == v.physical ||
             overlap(other->view.bytes, DUAT_BYTES, v.bytes, DUAT_BYTES))) r = DUAT_RANGE;
    }
    if (r) {
        enum duat_result clean = dispose(c, s);
        return leave(c, clean ? clean : r);
    }
    s->view = v;
    *out = (struct duat_token){c, s->generation, i};
    return leave(c, DUAT_OK);
}
enum duat_result duat_release(struct duat_context *c, struct duat_token t) {
    enum duat_result r = enter(c);
    if (r) return r;
    struct duat_slot *s = resolve(c, t);
    return leave(c, s ? dispose(c, s) : DUAT_ARGUMENT);
}
enum duat_result duat_quarantine(struct duat_context *c, struct duat_token t) {
    enum duat_result r = enter(c);
    if (r) return r;
    struct duat_slot *s = resolve(c, t);
    if (!s) return leave(c, DUAT_ARGUMENT);
    s->state = DUAT_RETAINED;
    c->failed = 1;
    return leave(c, DUAT_QUARANTINE);
}
enum duat_result duat_shutdown(struct duat_context *c) {
    enum duat_result r = enter(c);
    if (r) return r;
    c->stopping = 1;
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i)
        if (dispose(c, &c->slots[i])) c->failed = 1;
    return leave(c, c->failed ? DUAT_QUARANTINE : DUAT_OK);
}
static void store64(uint8_t *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
enum duat_result duat_validate(struct duat_context *c,
    const struct duat_token roots[2], struct duat_workspace *w, struct duat_summary *out) {
    if (!c || !roots || !w || !out ||
        overlap(w, sizeof(*w), c, sizeof(*c)) ||
        overlap(w, sizeof(*w), roots, 2 * sizeof(*roots)) ||
        overlap(out, sizeof(*out), c, sizeof(*c)) ||
        overlap(out, sizeof(*out), w, sizeof(*w))) return DUAT_ARGUMENT;
    enum duat_result r = enter(c);
    if (r) return r;
    if (c->failed || c->stopping) return leave(c, DUAT_STATE);
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) {
        struct duat_slot *s = &c->slots[i];
        if (s->state == DUAT_EMPTY) continue;
        if (s->state != DUAT_PREPARED) return leave(c, DUAT_STATE);
        if (overlap(w, sizeof(*w), s->view.bytes, DUAT_BYTES) ||
            overlap(out, sizeof(*out), s->view.bytes, DUAT_BYTES))
            return leave(c, DUAT_ARGUMENT);
    }
    uint64_t raw[2] = {0, 0};
    for (unsigned i = 0; i < 2; ++i) {
        if (!roots[i].owner) {
            if (roots[i].generation || roots[i].slot) return leave(c, DUAT_ARGUMENT);
        } else {
            struct duat_slot *s = resolve(c, roots[i]);
            if (!s || s->level != 1 ||
                uat_encode_root(s->view.physical, 0, &raw[i]) != UAT_OK)
                return leave(c, DUAT_ARGUMENT);
        }
    }
    store64(w->roots, raw[0]); store64(w->roots + 8, raw[1]);
    size_t n = 0;
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) {
        struct duat_slot *s = &c->slots[i];
        w->generations[i] = s->generation;
        if (s->state == DUAT_EMPTY) continue;
        struct duat_view now = {0};
        if (c->ops.inspect(c->provider, s->handle, &now) || !same_view(&now, &s->view))
            return leave(c, DUAT_RANGE);
        memcpy(w->pages[n], now.bytes, DUAT_BYTES);
        w->tables[n] = (struct uat_table){s->view.physical, s->level, w->pages[n],
                                        s->level == 1 ? 64 : DUAT_BYTES};
        w->ranges[n] = (struct uat_range){s->view.physical, DUAT_BYTES};
        ++n;
    }
    struct uat_snapshot snapshot = {w->roots, sizeof(w->roots), w->tables, n,
                                    w->ranges, NULL, n, 0};
    struct uat_summary summary = {0};
    if (uat_validate(&snapshot, &summary) != UAT_OK || summary.pages || summary.mapped_bytes)
        return leave(c, DUAT_GRAPH);
    /* Serialized provider bytes are immutable; detect a broken provider that
     * changes view or content across snapshotting. This is not a race protocol. */
    n = 0;
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) {
        struct duat_slot *s = &c->slots[i];
        if (s->generation != w->generations[i]) return leave(c, DUAT_STATE);
        if (s->state == DUAT_EMPTY) continue;
        struct duat_view now = {0};
        if (s->state != DUAT_PREPARED || c->ops.inspect(c->provider, s->handle, &now) ||
            !same_view(&now, &s->view) || memcmp(w->pages[n], now.bytes, DUAT_BYTES))
            return leave(c, DUAT_RANGE);
        ++n;
    }
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i)
        if (c->slots[i].generation != w->generations[i]) return leave(c, DUAT_STATE);
    *out = (struct duat_summary){summary.tables, summary.pages, summary.mapped_bytes};
    return leave(c, DUAT_OK);
}
