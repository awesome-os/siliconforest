/* Original private table construction. No hardware publication or data leaves. */
#include "builder.h"
#include "writer_internal.h"
#include <string.h>

static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (an - 1 > UINTPTR_MAX - x || bn - 1 > UINTPTR_MAX - y) return 1;
    return x <= y + bn - 1 && y <= x + an - 1;
}
static enum uatb_code enter(struct uatb_context *b) {
    if (!b || b->self != b) return UATB_ARGUMENT;
    if (b->busy) return UATB_STATE;
    b->busy = 1; return UATB_OK;
}
static enum uatb_code leave(struct uatb_context *b, enum uatb_code r) {
    b->busy = 0; return r;
}
int uatb_owned_view(const struct duat_context *c, struct duat_token t,
                    struct duat_view *out, void **handle) {
    if (!c || c->self != c || c->busy || c->stopping || c->failed || !out || !handle ||
        t.owner != c || !t.generation || t.slot >= DUAT_CAPACITY) return 0;
    const struct duat_slot *s = &c->slots[t.slot];
    if (s->state != DUAT_PREPARED || s->generation != t.generation || !s->handle) return 0;
    struct duat_view v = {0};
    if (c->ops.inspect(c->provider, s->handle, &v) ||
        s->state != DUAT_PREPARED || s->generation != t.generation ||
        v.bytes != s->view.bytes || v.length != s->view.length ||
        v.capacity != s->view.capacity || v.page_size != s->view.page_size ||
        v.physical != s->view.physical || v.segment != s->view.segment ||
        !v.bytes || (uintptr_t)v.bytes % DUAT_BYTES || v.length != DUAT_BYTES ||
        v.capacity != DUAT_BYTES || v.page_size != DUAT_BYTES || !v.physical ||
        v.physical % DUAT_BYTES || v.physical > DUAT_PA_END - DUAT_BYTES ||
        v.segment < DUAT_BYTES) return 0;
    *out = v; *handle = s->handle; return 1;
}
enum uatb_span_result uatb_write_view(const struct duat_context *owner, struct duat_token token,
    size_t offset, const uint8_t *source, size_t length, struct duat_view *out, void **handle) {
    if (!source || !out || !handle || !length || offset > DUAT_BYTES || length > DUAT_BYTES - offset)
        return UATB_SPAN_BAD;
    struct duat_view v; void *h;
    if (!uatb_owned_view(owner, token, &v, &h)) return UATB_SPAN_UNCERTAIN;
    if (overlap(source, length, v.bytes, DUAT_BYTES)) return UATB_SPAN_BAD;
    *out = v; *handle = h; return UATB_SPAN_READY;
}
static void quarantine_all(struct uatb_context *b) {
    /* Enumerate the actual owner, including an allocation that failed admission
     * before its token could be returned. Never retry completion. */
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) {
        struct duat_slot *s = &b->owner.slots[i];
        if (s->state != DUAT_EMPTY)
            (void)duat_quarantine(&b->owner, (struct duat_token){&b->owner, s->generation, i});
    }
    b->error = UATB_QUARANTINE;
}
static enum uatb_code abort_build(struct uatb_context *b, enum uatb_code why, int uncertain) {
    b->phase = UATB_FAILED;
    if (!b->error) b->error = why;
    if (uncertain) quarantine_all(b);
    if (duat_shutdown(&b->owner) != DUAT_OK) b->error = UATB_QUARANTINE;
    return b->error;
}
static int valid_plan(const struct uatb_node *p, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (p[i].permissions || p[i].level < 1 || p[i].level > 3) return 0;
        if (p[i].level == 1) {
            if (p[i].parent != UATB_NO_PARENT || p[i].entry >= 2) return 0;
        } else {
            if (p[i].parent >= i || p[p[i].parent].level + 1 != p[i].level ||
                p[i].entry >= (p[i].level == 2 ? 8u : 2048u)) return 0;
        }
        for (size_t j = 0; j < i; ++j)
            if (p[i].parent == p[j].parent && p[i].entry == p[j].entry) return 0;
    }
    return 1;
}
static void store64(uint8_t *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static enum uatb_code stage(struct uatb_context *b, struct duat_workspace *w) {
    if (!valid_plan(b->plan, b->count)) return UATB_PLAN;
    for (size_t i = 0; i < b->count; ++i) {
        struct duat_view v; void *handle;
        if (!uatb_owned_view(&b->owner, b->tokens[i], &v, &handle)) return UATB_VIEW;
        if (b->owner.slots[b->tokens[i].slot].level != b->plan[i].level) return UATB_VIEW;
        if (overlap(w, sizeof(*w), v.bytes, DUAT_BYTES)) return UATB_ARGUMENT;
    }
    for (size_t i = 0; i < b->count; ++i) memset(w->pages[i], 0, DUAT_BYTES);
    for (size_t i = 0; i < b->count; ++i) {
        if (b->plan[i].level == 1) continue;
        struct duat_view v; void *handle; uint64_t raw;
        if (!uatb_owned_view(&b->owner, b->tokens[i], &v, &handle)) return UATB_VIEW;
        if (uat_encode_table(v.physical, &raw) != UAT_OK) return UATB_VIEW;
        store64(w->pages[b->plan[i].parent] + b->plan[i].entry * 8, raw);
    }
    return UATB_OK;
}
static enum uatb_code match_pages(struct uatb_context *b, struct duat_workspace *w) {
    for (size_t i = 0; i < b->count; ++i) {
        struct duat_view v; void *handle;
        if (!uatb_owned_view(&b->owner, b->tokens[i], &v, &handle)) return UATB_VIEW;
        if (memcmp(v.bytes, w->pages[i], DUAT_BYTES)) return UATB_VALIDATE;
    }
    return UATB_OK;
}
enum uatb_code uatb_init(struct uatb_context *b, const struct duat_ops *ops, void *provider,
                        uatb_write_fn write, void *writer) {
    if (!b || !ops || !write) return UATB_ARGUMENT;
    const unsigned char *p = (const unsigned char *)b;
    for (size_t i = 0; i < sizeof(*b); ++i) if (p[i]) return UATB_STATE;
    if (duat_init(&b->owner, ops, provider) != DUAT_OK) return UATB_ARGUMENT;
    b->self = b; b->write = write; b->writer = writer;
    b->generation = 1; b->phase = UATB_READY; return UATB_OK;
}
enum uatb_code uatb_build(struct uatb_context *b, const struct uatb_node *plan, size_t count,
                        struct duat_workspace *w, struct uatb_ticket *out) {
    enum uatb_code r = enter(b); if (r) return r;
    if (b->phase != UATB_READY) return leave(b, UATB_STATE);
    if (!plan || !w || !out || !count) return leave(b, abort_build(b, UATB_ARGUMENT, 0));
    if (count > DUAT_CAPACITY) return leave(b, abort_build(b, UATB_LIMIT, 0));
    if (overlap(plan, count * sizeof(*plan), b, sizeof(*b)) ||
        overlap(w, sizeof(*w), b, sizeof(*b)) || overlap(out, sizeof(*out), b, sizeof(*b)) ||
        overlap(w, sizeof(*w), plan, count * sizeof(*plan)) ||
        overlap(w, sizeof(*w), out, sizeof(*out)) ||
        overlap(out, sizeof(*out), plan, count * sizeof(*plan)))
        return leave(b, abort_build(b, UATB_ARGUMENT, 0));
    if (!valid_plan(plan, count)) return leave(b, abort_build(b, UATB_PLAN, 0));
    b->phase = UATB_BUILDING; b->count = count;
    memcpy(b->plan, plan, count * sizeof(*plan));
    for (size_t i = 0; i < count; ++i) {
        enum duat_result allocated = duat_allocate(&b->owner, b->plan[i].level, &b->tokens[i]);
        if (allocated != DUAT_OK) return leave(b, abort_build(b,
            allocated == DUAT_QUARANTINE ? UATB_QUARANTINE : UATB_ALLOC, 0));
        ++b->allocated;
        if (b->plan[i].level == 1) b->roots[b->plan[i].entry] = b->tokens[i];
    }
    for (size_t i = 0; i < count; ++i)
        if (overlap(out, sizeof(*out), b->owner.slots[b->tokens[i].slot].view.bytes, DUAT_BYTES))
            return leave(b, abort_build(b, UATB_ARGUMENT, 0));
    r = stage(b, w);
    if (r) return leave(b, abort_build(b, r, r == UATB_VIEW));
    /* Children first. Every byte is written through the separately checked
     * mutable bridge; the frozen allocation API remains read-only. */
    for (size_t i = count; i-- > 0;) {
        enum uatb_write_result wr = b->write(b->writer, &b->owner, b->tokens[i], 0,
                                           w->pages[i], DUAT_BYTES);
        if (wr != UATB_WRITTEN) return leave(b, abort_build(b, UATB_WRITE,
            wr != UATB_FAILED_PRIVATE));
    }
    r = match_pages(b, w);
    if (r) return leave(b, abort_build(b, r, r == UATB_VIEW));
    struct duat_summary summary;
    if (duat_validate(&b->owner, b->roots, w, &summary) != DUAT_OK ||
        summary.tables != count || summary.pages || summary.mapped_bytes)
        return leave(b, abort_build(b, UATB_VALIDATE, 0));
    b->phase = UATB_VALID;
    *out = (struct uatb_ticket){b, b->generation};
    return leave(b, UATB_OK);
}
enum uatb_code uatb_validate(struct uatb_context *b, struct uatb_ticket ticket,
                            struct duat_workspace *w, struct duat_summary *out) {
    enum uatb_code r = enter(b); if (r) return r;
    if (b->phase != UATB_VALID) return leave(b, UATB_STATE);
    if (ticket.owner != b || ticket.generation != b->generation || !ticket.generation)
        return leave(b, UATB_ARGUMENT);
    if (!w || !out || overlap(w, sizeof(*w), b, sizeof(*b)) ||
        overlap(out, sizeof(*out), b, sizeof(*b)) || overlap(out, sizeof(*out), w, sizeof(*w)))
        return leave(b, UATB_ARGUMENT);
    for (size_t i = 0; i < b->count; ++i)
        if (b->tokens[i].slot >= DUAT_CAPACITY)
            return leave(b, abort_build(b, UATB_VIEW, 1));
    for (size_t i = 0; i < b->count; ++i)
        if (overlap(out, sizeof(*out), b->owner.slots[b->tokens[i].slot].view.bytes, DUAT_BYTES))
            return leave(b, UATB_ARGUMENT);
    r = stage(b, w);
    if (!r) r = match_pages(b, w);
    if (r) return leave(b, abort_build(b, r, r == UATB_VIEW));
    struct duat_summary summary;
    if (duat_validate(&b->owner, b->roots, w, &summary) != DUAT_OK ||
        summary.tables != b->count || summary.pages || summary.mapped_bytes)
        return leave(b, abort_build(b, UATB_VALIDATE, 0));
    *out = summary; return leave(b, UATB_OK);
}
enum uatb_code uatb_quarantine(struct uatb_context *b, struct uatb_ticket ticket) {
    enum uatb_code r = enter(b); if (r) return r;
    if (b->phase != UATB_VALID || ticket.owner != b || !ticket.generation ||
        ticket.generation != b->generation) return leave(b, UATB_ARGUMENT);
    return leave(b, abort_build(b, UATB_QUARANTINE, 1));
}
enum uatb_code uatb_shutdown(struct uatb_context *b) {
    enum uatb_code r = enter(b); if (r) return r;
    if (duat_shutdown(&b->owner) != DUAT_OK) b->error = UATB_QUARANTINE;
    b->phase = b->error ? UATB_FAILED : UATB_CLOSED;
    return leave(b, b->error);
}
