/* Synthetic provider tests; no kernel or GPU execution. */
#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned checks, groups;
struct handle { uint8_t *bytes; uint64_t pa; unsigned prepared; };
struct fake {
    struct handle *live[64];
    unsigned allocations, prepares, completes, releases, inspections;
    unsigned fail_alloc, fail_prepare, fail_complete, fault, change_at, change_kind;
    unsigned duplicate_handle, reenter;
    struct duat_context *context;
};
struct fixture { struct duat_context c; struct fake f; struct duat_workspace *w; };
static void *alloc_page(void *arg) {
    struct fake *f = arg;
    ++f->allocations;
    if (f->reenter) CHECK(duat_shutdown(f->context) == DUAT_STATE);
    if (f->fail_alloc) return NULL;
    if (f->duplicate_handle) return f->live[0];
    struct handle *h = calloc(1, sizeof(*h)); CHECK(h);
    h->bytes = aligned_alloc(DUAT_BYTES, DUAT_BYTES); CHECK(h->bytes);
    memset(h->bytes, 0, DUAT_BYTES);
    h->pa = UINT64_C(0x100000000) + (uint64_t)f->allocations * DUAT_BYTES;
    unsigned i; for (i = 0; i < 64 && f->live[i]; ++i) {}
    CHECK(i < 64); f->live[i] = h;
    return h;
}
static int prepare_page(void *arg, void *page) {
    struct fake *f = arg; struct handle *h = page; ++f->prepares;
    CHECK(!h->prepared);
    if (f->fail_prepare) return -1;
    h->prepared = 1; return 0;
}
static int inspect_page(void *arg, void *page, struct duat_view *v) {
    struct fake *f = arg; struct handle *h = page; ++f->inspections;
    CHECK(h->prepared);
    *v = (struct duat_view){h->bytes, DUAT_BYTES, DUAT_BYTES, DUAT_BYTES, h->pa, DUAT_BYTES};
    switch (f->fault) {
    case 1: return -1;
    case 2: v->bytes = NULL; break;
    case 3: ++v->bytes; break;
    case 4: --v->length; break;
    case 5: ++v->length; break;
    case 6: --v->capacity; break;
    case 7: ++v->capacity; break;
    case 8: v->page_size = 4096; break;
    case 9: v->physical = 0; break;
    case 10: ++v->physical; break;
    case 11: v->physical = DUAT_PA_END; break;
    case 12: v->physical = UINT64_MAX; break;
    case 13: --v->segment; break;
    case 14: v->physical = DUAT_PA_END - DUAT_BYTES; break;
    case 15: v->physical = f->live[0]->pa; break;
    case 16: v->bytes = f->live[0]->bytes; break;
    default: break;
    }
    if (f->change_at == f->inspections) {
        if (f->change_kind == 0) ++v->physical;
        if (f->change_kind == 1) h->bytes[DUAT_BYTES - 1] ^= 1;
        if (f->change_kind == 2) ++f->context->slots[0].generation;
        if (f->change_kind == 3) return -1;
    }
    return 0;
}
static int complete_page(void *arg, void *page) {
    struct fake *f = arg; struct handle *h = page; ++f->completes;
    CHECK(h->prepared); h->prepared = 0;
    return f->fail_complete ? -1 : 0;
}
static void release_page(void *arg, void *page) {
    struct fake *f = arg; struct handle *h = page; ++f->releases;
    CHECK(!h->prepared);
    unsigned i; for (i = 0; i < 64 && f->live[i] != h; ++i) {}
    CHECK(i < 64); f->live[i] = NULL;
    free(h->bytes); free(h);
}
static const struct duat_ops ops = {alloc_page, prepare_page, inspect_page, complete_page, release_page};
static struct fixture *start(void) {
    struct fixture *x = calloc(1, sizeof(*x)); CHECK(x);
    x->w = malloc(sizeof(*x->w)); CHECK(x->w);
    x->f.context = &x->c;
    CHECK(duat_init(&x->c, &ops, &x->f) == DUAT_OK);
    return x;
}
static void finish(struct fixture *x) {
    (void)duat_shutdown(&x->c);
    /* Test harness destroys synthetic allocations after lifetime assertions.
     * This is not production reclamation of quarantined kernel memory. */
    for (unsigned i = 0; i < 64; ++i) if (x->f.live[i]) {
        free(x->f.live[i]->bytes); free(x->f.live[i]);
    }
    free(x->w); free(x);
}
static struct duat_token allocate(struct fixture *x, unsigned level) {
    struct duat_token t = {0};
    CHECK(duat_allocate(&x->c, level, &t) == DUAT_OK); return t;
}
static void store64(uint8_t *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static void unchanged_validation(struct fixture *x, struct duat_token *roots,
                                 enum duat_result expected) {
    struct duat_summary out = {77, 88, 99};
    CHECK(duat_validate(&x->c, roots, x->w, &out) == expected);
    CHECK(out.tables == 77 && out.pages == 88 && out.mapped_bytes == 99);
}
static void test_lifetime(void) {
    ++groups; struct fixture *x = start(); struct duat_token roots[2] = {{0}, {0}};
    struct duat_summary out;
    CHECK(duat_validate(&x->c, roots, x->w, &out) == DUAT_OK);
    CHECK(!out.tables && !out.pages && !out.mapped_bytes);
    x->f.reenter = 1; roots[0] = allocate(x, 1);
    CHECK(duat_validate(&x->c, roots, x->w, &out) == DUAT_OK);
    CHECK(out.tables == 1 && !out.pages && !out.mapped_bytes);
    struct duat_token old = roots[0];
    CHECK(duat_release(&x->c, old) == DUAT_OK);
    CHECK(duat_release(&x->c, old) == DUAT_ARGUMENT);
    roots[0] = allocate(x, 1);
    CHECK(roots[0].slot == old.slot && roots[0].generation != old.generation);
    CHECK(duat_release(&x->c, old) == DUAT_ARGUMENT);
    CHECK(duat_init(&x->c, &ops, &x->f) == DUAT_STATE);
    struct duat_context copy = x->c;
    CHECK(duat_shutdown(&copy) == DUAT_ARGUMENT);
    struct fixture *y = start();
    struct duat_token yt = allocate(y, 1);
    CHECK(duat_release(&y->c, yt) == DUAT_OK); yt = allocate(y, 1);
    CHECK(yt.slot == roots[0].slot && yt.generation == roots[0].generation);
    CHECK(duat_release(&y->c, roots[0]) == DUAT_ARGUMENT); finish(y);
    CHECK(duat_shutdown(&x->c) == DUAT_OK);
    CHECK(duat_shutdown(&x->c) == DUAT_OK);
    CHECK(x->f.prepares == 2 && x->f.completes == 2 && x->f.releases == 2);
    CHECK(duat_allocate(&x->c, 1, &old) == DUAT_STATE);
    unchanged_validation(x, roots, DUAT_STATE); finish(x);
}
static void test_faults(void) {
    ++groups;
    for (unsigned fault = 1; fault <= 14; ++fault) {
        struct fixture *x = start(); x->f.fault = fault;
        struct duat_token out = {NULL, 71, 9};
        enum duat_result r = duat_allocate(&x->c, 1, &out);
        if (fault == 14) { CHECK(r == DUAT_OK); CHECK(duat_release(&x->c, out) == DUAT_OK); }
        else { CHECK(r == DUAT_RANGE); CHECK(!out.owner && out.generation == 71 && out.slot == 9); }
        CHECK(x->f.prepares == 1 && x->f.completes == 1 && x->f.releases == 1);
        finish(x);
    }
    for (unsigned failure = 0; failure < 2; ++failure) {
        struct fixture *x = start();
        x->f.fail_alloc = !failure; x->f.fail_prepare = failure;
        struct duat_token out = {0};
        CHECK(duat_allocate(&x->c, 1, &out) == (failure ? DUAT_PREPARE : DUAT_ALLOC));
        CHECK(!out.owner && !out.generation && !out.slot);
        CHECK(!x->f.completes && x->f.releases == failure); finish(x);
    }
    for (unsigned fault = 15; fault <= 16; ++fault) {
        struct fixture *x = start(); (void)allocate(x, 1); x->f.fault = fault;
        struct duat_token out = {0}; CHECK(duat_allocate(&x->c, 1, &out) == DUAT_RANGE);
        CHECK(x->f.completes == 1 && x->f.releases == 1); finish(x);
    }
}
static void test_quarantine(void) {
    ++groups;
    for (unsigned mode = 0; mode < 4; ++mode) {
        struct fixture *x = start(); struct duat_token t = {0};
        if (mode == 2) {
            x->f.fault = 11; x->f.fail_complete = 1;
            CHECK(duat_allocate(&x->c, 1, &t) == DUAT_QUARANTINE);
            CHECK(!t.owner);
        } else {
            t = allocate(x, 1);
            if (mode == 0) { x->f.fail_complete = 1; CHECK(duat_release(&x->c, t) == DUAT_QUARANTINE); }
            if (mode == 1) CHECK(duat_quarantine(&x->c, t) == DUAT_QUARANTINE);
            if (mode == 3) { x->f.duplicate_handle = 1; struct duat_token second = {0};
                CHECK(duat_allocate(&x->c, 1, &second) == DUAT_QUARANTINE); CHECK(!second.owner); }
        }
        unsigned complete = x->f.completes;
        CHECK(duat_shutdown(&x->c) == DUAT_QUARANTINE);
        CHECK(duat_shutdown(&x->c) == DUAT_QUARANTINE);
        CHECK(x->f.completes == complete && !x->f.releases);
        CHECK(duat_allocate(&x->c, 1, &t) == DUAT_STATE);
        CHECK(duat_init(&x->c, &ops, &x->f) == DUAT_STATE); finish(x);
    }
    struct fixture *x = start(); struct duat_token a = allocate(x, 1); (void)allocate(x, 1);
    CHECK(duat_quarantine(&x->c, a) == DUAT_QUARANTINE);
    CHECK(duat_shutdown(&x->c) == DUAT_QUARANTINE);
    CHECK(x->f.completes == 1 && x->f.releases == 1); finish(x);
}
static void test_limits(void) {
    ++groups; struct fixture *x = start(); struct duat_token t = {0};
    CHECK(duat_allocate(&x->c, 0, &t) == DUAT_ARGUMENT);
    CHECK(duat_allocate(&x->c, 4, &t) == DUAT_ARGUMENT);
    CHECK(!x->f.allocations);
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) (void)allocate(x, 1);
    CHECK(duat_allocate(&x->c, 1, &t) == DUAT_LIMIT); CHECK(x->f.allocations == 16);
    finish(x); x = start(); x->c.next_generation = UINT64_MAX;
    CHECK(duat_allocate(&x->c, 1, &t) == DUAT_LIMIT); CHECK(!x->f.allocations);
    x->c.next_generation = 0;
    CHECK(duat_allocate(&x->c, 1, &t) == DUAT_LIMIT); finish(x);
}
static void test_graphs(void) {
    ++groups; struct fixture *x = start(); struct duat_token roots[2] = {{0}, {0}};
    roots[0] = allocate(x, 1); struct duat_token l2 = allocate(x, 2), l3 = allocate(x, 3);
    unchanged_validation(x, roots, DUAT_GRAPH);
    uint64_t raw;
    CHECK(uat_encode_table(x->c.slots[l2.slot].view.physical, &raw) == UAT_OK);
    store64(x->f.live[0]->bytes, raw);
    CHECK(uat_encode_table(x->c.slots[l3.slot].view.physical, &raw) == UAT_OK);
    store64(x->f.live[1]->bytes, raw);
    struct duat_summary out;
    CHECK(duat_validate(&x->c, roots, x->w, &out) == DUAT_OK); CHECK(out.tables == 3);
    CHECK(uat_encode_page(UINT64_C(0x200000000), 0, &raw) == UAT_OK);
    store64(x->f.live[2]->bytes, raw);
    unchanged_validation(x, roots, DUAT_GRAPH); memset(x->f.live[2]->bytes, 0, DUAT_BYTES);
    roots[1] = roots[0]; unchanged_validation(x, roots, DUAT_GRAPH); roots[1] = (struct duat_token){0};
    struct duat_token saved = roots[0]; roots[0] = l2;
    unchanged_validation(x, roots, DUAT_ARGUMENT); roots[0] = saved;
    ++roots[0].generation; unchanged_validation(x, roots, DUAT_ARGUMENT); roots[0] = saved;
    roots[1].slot = 1; unchanged_validation(x, roots, DUAT_ARGUMENT); roots[1].slot = 0;
    CHECK(duat_release(&x->c, l3) == DUAT_OK); unchanged_validation(x, roots, DUAT_GRAPH);
    finish(x);
    /* Largest admitted graph: 1 L1, 1 L2, 14 L3, no data leaves. */
    x = start(); roots[0] = allocate(x, 1); roots[1] = (struct duat_token){0}; l2 = allocate(x, 2);
    CHECK(uat_encode_table(x->c.slots[l2.slot].view.physical, &raw) == UAT_OK); store64(x->f.live[0]->bytes, raw);
    for (unsigned i = 0; i < 14; ++i) { l3 = allocate(x, 3);
        CHECK(uat_encode_table(x->c.slots[l3.slot].view.physical, &raw) == UAT_OK);
        store64(x->f.live[1]->bytes + 8 * i, raw); }
    CHECK(duat_validate(&x->c, roots, x->w, &out) == DUAT_OK); CHECK(out.tables == 16 && !out.pages);
    finish(x);
}
static void test_snapshot(void) {
    ++groups;
    for (unsigned kind = 0; kind < 4; ++kind) {
        struct fixture *x = start(); struct duat_token roots[2] = {allocate(x, 1), {0}};
        x->f.change_at = x->f.inspections + 2; x->f.change_kind = kind;
        /* Generation mutation happens in the final inspect callback, after
         * its pre-call generation check; must still be refused. */
        unchanged_validation(x, roots, kind == 2 ? DUAT_STATE : DUAT_RANGE);
        finish(x);
    }
    struct fixture *x = start(); struct duat_token roots[2] = {allocate(x, 1), {0}};
    struct duat_summary out;
    CHECK(duat_validate(&x->c, roots, (struct duat_workspace *)&x->c, &out) == DUAT_ARGUMENT);
    CHECK(duat_validate(&x->c, roots, x->w, (struct duat_summary *)x->f.live[0]->bytes) == DUAT_ARGUMENT);
    CHECK(duat_allocate(&x->c, 1, (struct duat_token *)x->f.live[0]->bytes) == DUAT_ARGUMENT);
    x->f.fault = 10; unchanged_validation(x, roots, DUAT_RANGE); finish(x);
}
int main(void) {
    test_lifetime(); test_faults(); test_quarantine(); test_limits(); test_graphs(); test_snapshot();
    printf("PASS %u groups %u checks\n", groups, checks); return 0;
}
