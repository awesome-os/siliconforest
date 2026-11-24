/* Synthetic TABLE and DATA providers are deliberately separate typed owners. */
#include "data.h"
#include "../m1-gpu-uat-builder/writer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned checks, groups;
struct page { uint8_t *bytes; uint64_t pa; unsigned prepared; };
struct arena {
    struct page *pages[16]; uint64_t base;
    unsigned allocs, prepares, inspections, zeros, completes, releases;
    unsigned fail_alloc, fail_prepare, fail_inspect, zero_mode, fail_complete, bad_view, stale_after_zero;
    unsigned duplicate_handle, invalidate_graph_prepare;
    struct arena *tables; struct uatb_context *graph;
};
struct fixture {
    struct uatb_context graph; struct uatb_ticket graph_ticket;
    struct udata_context data; struct arena tables, pages;
    struct duat_workspace *build_work;
    struct udata_workspace *work;
};
static void *alloc_page(void *arg) {
    struct arena *a = arg; ++a->allocs;
    if (a->allocs == a->fail_alloc) return NULL;
    if (a->duplicate_handle == 1) return a->pages[0];
    if (a->duplicate_handle == 2) return a->tables->pages[0];
    struct page *p = calloc(1, sizeof(*p)); CHECK(p);
    p->bytes = aligned_alloc(DUAT_BYTES, DUAT_BYTES); CHECK(p->bytes);
    memset(p->bytes, 0xa5, DUAT_BYTES); p->pa = a->base + a->allocs * DUAT_BYTES;
    unsigned i; for (i = 0; i < 16 && a->pages[i]; ++i) {}
    CHECK(i < 16); a->pages[i] = p; return p;
}
static int prepare(void *arg, void *h) {
    struct arena *a = arg; struct page *p = h; ++a->prepares; CHECK(!p->prepared);
    if (a->prepares == a->fail_prepare) return -1;
    p->prepared = 1;
    if (a->invalidate_graph_prepare) a->graph->phase = UATB_CLOSED;
    return 0;
}
static int complete(void *arg, void *h) {
    struct arena *a = arg; struct page *p = h; ++a->completes; CHECK(p->prepared); p->prepared = 0;
    return a->completes == a->fail_complete ? -1 : 0;
}
static void release(void *arg, void *h) {
    struct arena *a = arg; struct page *p = h; ++a->releases; CHECK(!p->prepared);
    unsigned i; for (i = 0; i < 16 && a->pages[i] != p; ++i) {}
    CHECK(i < 16); a->pages[i] = NULL; free(p->bytes); free(p);
}
static int table_inspect(void *arg, void *h, struct duat_view *v) {
    struct arena *a = arg; struct page *p = h; ++a->inspections; CHECK(p->prepared);
    *v = (struct duat_view){p->bytes, DUAT_BYTES, DUAT_BYTES, DUAT_BYTES, p->pa, DUAT_BYTES};
    if (a->bad_view) v->physical += DUAT_BYTES;
    return 0;
}
static enum uatb_write_result table_write(void *arg, const struct duat_context *owner,
    struct duat_token t, size_t offset, const uint8_t *source, size_t length) {
    (void)arg; struct duat_view v; void *h;
    if (uatb_write_view(owner, t, offset, source, length, &v, &h) != UATB_SPAN_READY) return UATB_UNCERTAIN;
    memcpy(((struct page *)h)->bytes + offset, source, length); return UATB_WRITTEN;
}
static int data_inspect(void *arg, void *h, struct udata_view *v) {
    struct arena *a = arg; struct page *p = h; ++a->inspections; CHECK(p->prepared);
    if (a->inspections == a->fail_inspect) return -1;
    *v = (struct udata_view){p->bytes, DUAT_BYTES, DUAT_BYTES, DUAT_BYTES, p->pa, DUAT_BYTES};
    switch (a->bad_view) {
    case 1: v->physical = 0; break;
    case 2: ++v->physical; break;
    case 3: v->physical = DUAT_PA_END; break;
    case 4: --v->length; break;
    case 5: ++v->capacity; break;
    case 6: v->page_size = 4096; break;
    case 7: --v->segment; break;
    case 8: ++v->bytes; break;
    case 9: v->physical = a->tables->pages[0]->pa; break;
    case 10: v->bytes = a->tables->pages[0]->bytes; break;
    case 11: v->physical = a->pages[0]->pa; break;
    case 12: v->bytes = a->pages[0]->bytes; break;
    case 13: v->physical = DUAT_PA_END - DUAT_BYTES; break;
    default: break;
    }
    if (a->stale_after_zero && a->zeros) v->physical += DUAT_BYTES;
    return 0;
}
static enum udata_zero_result zero_page(void *arg, void *h, const struct udata_view *expected) {
    struct arena *a = arg; struct page *p = h; ++a->zeros;
    CHECK(expected->bytes == p->bytes); /* A table alias must NEVER reach zero. */
    if (a->zero_mode == 2) return UD_ZERO_UNCERTAIN;
    if (a->zero_mode == 3) return UD_ZEROED; /* Lying provider; readback catches it. */
    if (a->zero_mode == 1) { memset(p->bytes, 0, DUAT_BYTES / 2); return UD_ZERO_FAILED_PRIVATE; }
    memset(p->bytes, 0, DUAT_BYTES); return UD_ZEROED;
}
static const struct duat_ops table_ops = {alloc_page, prepare, table_inspect, complete, release};
static const struct udata_ops data_ops = {alloc_page, prepare, data_inspect, zero_page, complete, release};
static const struct uatb_node topology[] = {
    {1, UATB_NO_PARENT, 0, 0}, {2, 0, 0, 0}, {3, 1, 0, 0},
    {1, UATB_NO_PARENT, 1, 0}, {2, 3, 0, 0}, {3, 4, 0, 0}
};
static struct fixture *start(void) {
    struct fixture *x = calloc(1, sizeof(*x)); CHECK(x);
    x->build_work = malloc(sizeof(*x->build_work)); CHECK(x->build_work);
    x->work = malloc(sizeof(*x->work)); CHECK(x->work);
    x->tables.base = UINT64_C(0x100000000); x->pages.base = UINT64_C(0x200000000);
    x->pages.tables = &x->tables; x->pages.graph = &x->graph;
    CHECK(uatb_init(&x->graph, &table_ops, &x->tables, table_write, &x->tables) == UATB_OK);
    CHECK(uatb_build(&x->graph, topology, 6, x->build_work, &x->graph_ticket) == UATB_OK);
    CHECK(udata_init(&x->data, &data_ops, &x->pages, &x->graph, x->graph_ticket) == UD_OK);
    return x;
}
static void destroy_arena(struct arena *a) {
    for (unsigned i = 0; i < 16; ++i) if (a->pages[i]) { free(a->pages[i]->bytes); free(a->pages[i]); }
}
static void finish(struct fixture *x) {
    (void)udata_shutdown(&x->data); (void)uatb_shutdown(&x->graph);
    /* Test-only destruction of synthetic retained storage, not a reclaim API. */
    destroy_arena(&x->pages); destroy_arena(&x->tables);
    free(x->work); free(x->build_work); free(x);
}
static struct udata_token allocate_data(struct fixture *x) {
    struct udata_token t = {0}; CHECK(udata_allocate(&x->data, &t) == UD_OK);
    CHECK(t.owner == &x->data && t.generation); return t;
}
static enum udata_code plan(struct fixture *x, const struct udata_mapping *m, size_t n, struct udata_summary *out) {
    return udata_plan(&x->data, m, n, x->work, sizeof(*x->work), out);
}
static struct uat_snapshot snapshot(struct fixture *x, size_t count) {
    return (struct uat_snapshot){x->work->graph.roots, 16, x->work->graph.tables, 6,
        x->work->graph.ranges, x->work->data_ranges, 6, count};
}
static void reject_plan(struct fixture *x, struct udata_mapping *m, size_t n, enum udata_code why) {
    struct udata_summary out = {77, 88, 99};
    CHECK(plan(x, m, n, &out) == why); CHECK(out.tables == 77 && out.data_pages == 88 && out.mapped_bytes == 99);
}
static void test_valid(void) {
    ++groups; struct fixture *x = start(); struct udata_mapping m[16];
    uint8_t *old = malloc(6 * DUAT_BYTES); CHECK(old);
    for (unsigned i = 0; i < 6; ++i) memcpy(old + i * DUAT_BYTES, x->tables.pages[i]->bytes, DUAT_BYTES);
    for (unsigned i = 0; i < 16; ++i) {
        m[i] = (struct udata_mapping){(uint64_t)i * DUAT_BYTES, allocate_data(x), i % 2 ? UD_READ | UD_WRITE : UD_READ};
        for (size_t b = 0; b < DUAT_BYTES; ++b) CHECK(x->pages.pages[i]->bytes[b] == 0);
    }
    struct udata_token limit = {0}; CHECK(udata_allocate(&x->data, &limit) == UD_LIMIT);
    struct udata_summary out; CHECK(plan(x, m, 16, &out) == UD_OK);
    CHECK(out.tables == 6 && out.data_pages == 16 && out.mapped_bytes == 16 * DUAT_BYTES);
    struct uat_snapshot s = snapshot(x, 16); struct uat_translation translation;
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(uat_lookup(&s, m[i].va, &translation) == UAT_OK);
        CHECK(translation.address == x->pages.pages[i]->pa);
        CHECK(translation.page_flags == (i % 2 ? UINT64_C(0x00e0000000000c08) : UINT64_C(0x00e0000000000c48)));
    }
    for (unsigned i = 0; i < 6; ++i) CHECK(!memcmp(old + i * DUAT_BYTES, x->tables.pages[i]->bytes, DUAT_BYTES));
    m[0].va = UINT64_C(0xffffff8000000000); CHECK(plan(x, m, 16, &out) == UD_OK);
    s = snapshot(x, 16); CHECK(uat_lookup(&s, m[0].va, &translation) == UAT_OK);
    for (unsigned i = 0; i < 6; ++i) CHECK(!memcmp(old + i * DUAT_BYTES, x->tables.pages[i]->bytes, DUAT_BYTES));
    CHECK(udata_shutdown(&x->data) == UD_OK); CHECK(udata_shutdown(&x->data) == UD_OK);
    CHECK(x->pages.prepares == 16 && x->pages.completes == 16 && x->pages.releases == 16);
    free(old); finish(x);
}
static void test_permissions_and_plans(void) {
    ++groups; uint64_t flags = 99;
    CHECK(udata_leaf_flags(UD_READ, &flags) == UD_OK && flags == UINT64_C(0x00e0000000000c48));
    CHECK(udata_leaf_flags(UD_READ | UD_WRITE, &flags) == UD_OK && flags == UINT64_C(0x00e0000000000c08));
    for (unsigned p = 0; p < 256; ++p) if (p != 1 && p != 3) {
        flags = 99; CHECK(udata_leaf_flags(p, &flags) == UD_PERMISSION && flags == 99);
    }
    for (unsigned bit = 3; bit < 32; ++bit) { flags = 99;
        CHECK(udata_leaf_flags(UD_READ | (UINT32_C(1) << bit), &flags) == UD_PERMISSION && flags == 99); }
    struct fixture *x = start(); struct udata_mapping m[2] = {{0, allocate_data(x), UD_READ}, {DUAT_BYTES, allocate_data(x), UD_READ}};
    memset(x->work, 0x6a, sizeof(*x->work));
    m[1].va = 0; reject_plan(x, m, 2, UD_PLAN); m[1].va = DUAT_BYTES;
    CHECK(((uint8_t *)x->work)[0] == 0x6a);
    struct udata_token t = m[1].data; m[1].data = m[0].data; reject_plan(x, m, 2, UD_PLAN); m[1].data = t;
    CHECK(((uint8_t *)x->work)[0] == 0x6a);
    m[0].permissions = UD_READ | UD_EXEC; reject_plan(x, m, 2, UD_PERMISSION); m[0].permissions = UD_READ;
    const uint64_t bad[] = {1, UINT64_C(0x8000000000), UINT64_C(0x10000000000), UINT64_MAX, UINT64_C(1) << 25};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); ++i) { m[0].va = bad[i]; reject_plan(x, m, 2, UD_PLAN); }
    m[0].va = 0; reject_plan(x, m, 0, UD_ARGUMENT); reject_plan(x, m, 17, UD_ARGUMENT);
    memset(x->work, 0x6a, sizeof(*x->work)); struct udata_summary out = {0};
    CHECK(udata_plan(&x->data, m, 2, x->work, sizeof(*x->work) - 1, &out) == UD_ARGUMENT);
    CHECK(((uint8_t *)x->work)[0] == 0x6a && ((uint8_t *)x->work)[sizeof(*x->work) - 1] == 0x6a);
    CHECK(udata_plan(&x->data, m, 2, (struct udata_workspace *)x->tables.pages[0]->bytes, sizeof(*x->work), &out) == UD_ARGUMENT);
    CHECK(udata_plan(&x->data, m, 2, (struct udata_workspace *)x->pages.pages[0]->bytes, sizeof(*x->work), &out) == UD_ARGUMENT);
    CHECK(udata_plan(&x->data, m, 2, x->work, sizeof(*x->work), (struct udata_summary *)x->pages.pages[0]->bytes) == UD_ARGUMENT);
    finish(x);
}
static void test_admission_aliases(void) {
    ++groups;
    for (unsigned mode = 1; mode <= 13; ++mode) {
        struct fixture *x = start(); struct udata_token t = {NULL, 55, 7};
        unsigned old_zeros = 0;
        if (mode == 11 || mode == 12) { (void)allocate_data(x); old_zeros = 1; }
        x->pages.bad_view = mode;
        enum udata_code r = udata_allocate(&x->data, &t);
        if (mode == 13) { CHECK(r == UD_OK); CHECK(x->data.slots[t.slot].view.physical == DUAT_PA_END - DUAT_BYTES); }
        else {
            CHECK(r == (mode >= 9 ? UD_OVERLAP : UD_VIEW));
            CHECK(!t.owner && t.generation == 55 && t.slot == 7);
            CHECK(x->pages.zeros == old_zeros); /* Alias refused BEFORE zero. */
            CHECK(x->pages.completes == 1 && x->pages.releases == 1);
        }
        finish(x);
    }
    struct fixture *x = start(); x->pages.duplicate_handle = 2; struct udata_token t = {0};
    CHECK(udata_allocate(&x->data, &t) == UD_QUARANTINE);
    CHECK(!x->pages.prepares && !x->pages.zeros && !x->tables.completes);
    CHECK(udata_shutdown(&x->data) == UD_QUARANTINE); finish(x);
    x = start(); (void)allocate_data(x); x->pages.duplicate_handle = 1;
    CHECK(udata_allocate(&x->data, &t) == UD_QUARANTINE); CHECK(x->pages.prepares == 1 && x->pages.zeros == 1);
    finish(x);
}
static void test_faults(void) {
    ++groups;
    for (unsigned mode = 0; mode < 8; ++mode) {
        struct fixture *x = start(); struct udata_token t = {NULL, 22, 3}; enum udata_code expected;
        switch (mode) {
        case 0: x->pages.fail_alloc = 1; expected = UD_ALLOC; break;
        case 1: x->pages.fail_prepare = 1; expected = UD_PREPARE; break;
        case 2: x->pages.fail_inspect = 1; expected = UD_VIEW; break;
        case 3: x->pages.zero_mode = 1; expected = UD_ZERO; break;
        case 4: x->pages.zero_mode = 2; expected = UD_QUARANTINE; break;
        case 5: x->pages.zero_mode = 3; expected = UD_ZERO; break;
        case 6: x->pages.stale_after_zero = 1; expected = UD_QUARANTINE; break;
        default: x->pages.zero_mode = 1; x->pages.fail_complete = 1; expected = UD_QUARANTINE; break;
        }
        CHECK(udata_allocate(&x->data, &t) == expected); CHECK(!t.owner && t.generation == 22 && t.slot == 3);
        if (expected == UD_QUARANTINE) {
            unsigned old = x->pages.completes;
            CHECK(udata_shutdown(&x->data) == UD_QUARANTINE); CHECK(udata_shutdown(&x->data) == UD_QUARANTINE);
            CHECK(x->pages.completes == old && !x->pages.releases);
        } else {
            CHECK(x->pages.completes == (mode <= 1 ? 0u : 1u));
            CHECK(x->pages.releases == (mode == 0 ? 0u : 1u));
        }
        finish(x);
    }
    struct fixture *x = start(); struct udata_token t = allocate_data(x); (void)allocate_data(x);
    CHECK(udata_quarantine(&x->data, t) == UD_QUARANTINE);
    CHECK(udata_shutdown(&x->data) == UD_QUARANTINE); CHECK(x->pages.completes == 1 && x->pages.releases == 1);
    finish(x);
}
static void test_tokens_and_graph_lifetime(void) {
    ++groups; struct fixture *x = start(), *y = start();
    struct udata_context *alias = (struct udata_context *)x->tables.pages[2]->bytes;
    CHECK(udata_init(alias, &data_ops, &x->pages, &x->graph, x->graph_ticket) == UD_ARGUMENT);
    CHECK(!x->tables.pages[2]->bytes[0]);
    struct udata_context empty = {0};
    CHECK(udata_init(&empty, &data_ops, &x->pages, &x->graph, y->graph_ticket) == UD_GRAPH);
    struct udata_token t = allocate_data(x), foreign = allocate_data(y);
    CHECK(udata_release(&x->data, foreign) == UD_ARGUMENT);
    struct udata_mapping m = {0, foreign, UD_READ}; reject_plan(x, &m, 1, UD_ARGUMENT);
    CHECK(udata_release(&x->data, t) == UD_OK); CHECK(udata_release(&x->data, t) == UD_ARGUMENT);
    struct udata_token replacement = allocate_data(x); CHECK(replacement.slot == t.slot && replacement.generation != t.generation);
    m.data = t; reject_plan(x, &m, 1, UD_ARGUMENT);
    /* A table token's pointer domain is never accepted as a data owner. */
    m.data = (struct udata_token){(const struct udata_context *)x->graph.tokens[0].owner,
                                x->graph.tokens[0].generation, x->graph.tokens[0].slot};
    reject_plan(x, &m, 1, UD_ARGUMENT);
    m.data = replacement;
    struct udata_context copy = x->data; CHECK(udata_shutdown(&copy) == UD_ARGUMENT);
    CHECK(udata_init(&x->data, &data_ops, &x->pages, &x->graph, x->graph_ticket) == UD_STATE);
    CHECK(uatb_shutdown(&x->graph) == UATB_OK);
    unsigned allocs = x->pages.allocs, zeros = x->pages.zeros;
    CHECK(udata_allocate(&x->data, &t) == UD_GRAPH); CHECK(x->pages.allocs == allocs && x->pages.zeros == zeros);
    reject_plan(x, &m, 1, UD_GRAPH);
    CHECK(udata_shutdown(&x->data) == UD_OK); /* Independent safe data cleanup. */
    finish(y); finish(x);
    x = start(); x->data.graph_ticket.generation++;
    CHECK(udata_allocate(&x->data, &t) == UD_GRAPH); CHECK(!x->pages.allocs && !x->pages.zeros); finish(x);
    x = start(); x->pages.invalidate_graph_prepare = 1;
    CHECK(udata_allocate(&x->data, &t) == UD_GRAPH);
    CHECK(!x->pages.zeros && x->pages.completes == 1 && x->pages.releases == 1); finish(x);
    x = start(); x->tables.bad_view = 1;
    CHECK(udata_allocate(&x->data, &t) == UD_GRAPH); CHECK(!x->pages.allocs && !x->pages.zeros); finish(x);
    for (unsigned i = 0; i < 2; ++i) { x = start(); x->data.next_generation = i ? UINT64_MAX : 0;
        CHECK(udata_allocate(&x->data, &t) == UD_LIMIT); CHECK(!x->pages.allocs); finish(x); }
}
static void test_late_faults(void) {
    ++groups;
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct fixture *x = start();
        struct udata_mapping m = {0, allocate_data(x), UD_READ};
        if (mode == 0) x->pages.fail_inspect = x->pages.inspections + 1;
        if (mode == 1) x->pages.bad_view = 2;
        if (mode == 2) x->tables.pages[2]->bytes[0] = 3;
        reject_plan(x, &m, 1, mode == 2 ? UD_GRAPH : UD_QUARANTINE);
        if (mode != 2) {
            CHECK(udata_shutdown(&x->data) == UD_QUARANTINE);
            CHECK(!x->pages.completes && !x->pages.releases);
        }
        finish(x);
    }
}
int main(void) {
    test_valid(); test_permissions_and_plans(); test_admission_aliases(); test_faults(); test_tokens_and_graph_lifetime();
    test_late_faults();
    printf("PASS %u groups %u checks\n", groups, checks); return 0;
}
