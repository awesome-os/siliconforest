/* Synthetic allocations and CPU writes only. */
#include "builder.h"
#include "writer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned checks, groups;
struct page { uint8_t *bytes; uint64_t pa; unsigned prepared; };
struct fake {
    struct page *pages[DUAT_CAPACITY];
    unsigned allocations, prepares, inspections, writes, completes, releases;
    unsigned fail_alloc, fail_prepare, fail_write, uncertain_write, fail_complete;
    unsigned corrupt_write, changed_view, reenter, stale_write;
    unsigned bad_pa, corrupt_validation, postwrite_inspections;
    unsigned written_slots[DUAT_CAPACITY];
    struct uatb_context *builder;
};
struct fixture { struct uatb_context b; struct fake f; struct duat_workspace *w; };
static void *allocate(void *arg) {
    struct fake *f = arg; ++f->allocations;
    if (f->allocations == f->fail_alloc) return NULL;
    struct page *p = calloc(1, sizeof(*p)); CHECK(p);
    p->bytes = aligned_alloc(DUAT_BYTES, DUAT_BYTES); CHECK(p->bytes);
    memset(p->bytes, 0xa5, DUAT_BYTES); /* Builder must initialize every byte. */
    p->pa = UINT64_C(0x100000000) + (uint64_t)f->allocations * DUAT_BYTES;
    unsigned i; for (i = 0; i < DUAT_CAPACITY && f->pages[i]; ++i) {}
    CHECK(i < DUAT_CAPACITY); f->pages[i] = p; return p;
}
static int prepare(void *arg, void *h) {
    struct fake *f = arg; struct page *p = h; ++f->prepares;
    CHECK(!p->prepared);
    if (f->prepares == f->fail_prepare) return -1;
    p->prepared = 1; return 0;
}
static int inspect(void *arg, void *h, struct duat_view *out) {
    struct fake *f = arg; struct page *p = h; ++f->inspections; CHECK(p->prepared);
    *out = (struct duat_view){p->bytes, DUAT_BYTES, DUAT_BYTES, DUAT_BYTES, p->pa, DUAT_BYTES};
    if (f->bad_pa == 1) out->physical = 0;
    if (f->bad_pa == 2) out->physical++;
    if (f->bad_pa == 3) out->physical = DUAT_PA_END;
    if (f->bad_pa == 4) out->physical = DUAT_PA_END - DUAT_BYTES;
    if (f->bad_pa == 5 && f->allocations > 1) out->physical = f->pages[0]->pa;
    if (f->corrupt_validation && f->writes == 3 && ++f->postwrite_inspections == 5)
        p->bytes[0] = 0xff; /* After final writer's view and three plan checks. */
    if (f->changed_view) {
        switch (f->changed_view) {
        case 1: out->physical += DUAT_BYTES; break;
        case 2: out->bytes++; break;
        case 3: out->length--; break;
        case 4: out->segment--; break;
        case 5: out->capacity++; break;
        case 6: out->page_size = 4096; break;
        default: return -1;
        }
    }
    return 0;
}
static int complete(void *arg, void *h) {
    struct fake *f = arg; struct page *p = h; ++f->completes;
    CHECK(p->prepared); p->prepared = 0;
    return f->completes == f->fail_complete ? -1 : 0;
}
static void release(void *arg, void *h) {
    struct fake *f = arg; struct page *p = h; ++f->releases; CHECK(!p->prepared);
    unsigned i; for (i = 0; i < DUAT_CAPACITY && f->pages[i] != p; ++i) {}
    CHECK(i < DUAT_CAPACITY); f->pages[i] = NULL; free(p->bytes); free(p);
}
static enum uatb_write_result write_page(void *arg, const struct duat_context *owner,
    struct duat_token token, size_t offset, const uint8_t *source, size_t length) {
    struct fake *f = arg; ++f->writes;
    if (f->reenter) CHECK(uatb_shutdown(f->builder) == UATB_STATE);
    CHECK(offset == 0 && length == DUAT_BYTES);
    if (f->stale_write) --token.generation;
    struct duat_view v; void *h;
    enum uatb_span_result span = uatb_write_view(owner, token, offset, source, length, &v, &h);
    if (span == UATB_SPAN_BAD) return UATB_FAILED_PRIVATE;
    if (span != UATB_SPAN_READY) return UATB_UNCERTAIN;
    struct page *p = h;
    f->written_slots[f->writes - 1] = token.slot;
    if (f->writes == f->uncertain_write) return UATB_UNCERTAIN;
    if (f->writes == f->fail_write) {
        memcpy(p->bytes, source, length / 2); return UATB_FAILED_PRIVATE;
    }
    memcpy(p->bytes, source, length);
    if (f->corrupt_write && token.slot == 0) {
        /* Still codec-valid topology at another parent entry: exact plan check
         * must refuse, not just rely on the codec's graph format validator. */
        memcpy(p->bytes + 8, p->bytes, 8); memset(p->bytes, 0, 8);
    }
    return UATB_WRITTEN;
}
static const struct duat_ops ops = {allocate, prepare, inspect, complete, release};
static const struct uatb_node basic[] = {
    {1, UATB_NO_PARENT, 0, 0}, {2, 0, 0, 0}, {3, 1, 0, 0}
};
static struct fixture *start(void) {
    struct fixture *x = calloc(1, sizeof(*x)); CHECK(x);
    x->w = malloc(sizeof(*x->w)); CHECK(x->w);
    x->f.builder = &x->b;
    CHECK(uatb_init(&x->b, &ops, &x->f, write_page, &x->f) == UATB_OK); return x;
}
static void finish(struct fixture *x) {
    (void)uatb_shutdown(&x->b);
    /* Synthetic test storage only: production retained memory is not freed. */
    for (unsigned i = 0; i < DUAT_CAPACITY; ++i) if (x->f.pages[i]) {
        free(x->f.pages[i]->bytes); free(x->f.pages[i]);
    }
    free(x->w); free(x);
}
static struct uatb_ticket build(struct fixture *x, const struct uatb_node *p, size_t n) {
    struct uatb_ticket t = {0}; CHECK(uatb_build(&x->b, p, n, x->w, &t) == UATB_OK);
    CHECK(t.owner == &x->b && t.generation); return t;
}
static uint64_t load64(const uint8_t *p) {
    uint64_t x = 0; for (unsigned i = 0; i < 8; ++i) x |= (uint64_t)p[i] << (8 * i); return x;
}
static void test_valid(void) {
    ++groups; struct fixture *x = start(); x->f.reenter = 1;
    struct uatb_ticket t = build(x, basic, 3); struct duat_summary out;
    CHECK(x->f.writes == 3 && x->f.written_slots[0] == 2 && x->f.written_slots[2] == 0);
    CHECK(load64(x->f.pages[0]->bytes) == (x->f.pages[1]->pa | 3));
    CHECK(load64(x->f.pages[1]->bytes) == (x->f.pages[2]->pa | 3));
    for (size_t i = 0; i < DUAT_BYTES; ++i) CHECK(x->f.pages[2]->bytes[i] == 0);
    for (size_t i = 8; i < DUAT_BYTES; ++i) CHECK(x->f.pages[0]->bytes[i] == 0);
    CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_OK);
    CHECK(out.tables == 3 && !out.pages && !out.mapped_bytes);
    CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_STATE);
    CHECK(x->f.writes == 3); CHECK(uatb_shutdown(&x->b) == UATB_OK);
    CHECK(uatb_shutdown(&x->b) == UATB_OK);
    CHECK(x->f.completes == 3 && x->f.releases == 3);
    CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_STATE);
    CHECK(uatb_init(&x->b, &ops, &x->f, write_page, &x->f) == UATB_STATE); finish(x);
    x = start(); struct uatb_node plan[16] = {{1, UATB_NO_PARENT, 0, 0}, {1, UATB_NO_PARENT, 1, 0},
        {2, 0, 7, 0}, {2, 1, 7, 0}};
    for (unsigned i = 4; i < 16; ++i) plan[i] = (struct uatb_node){3, i % 2 ? 3 : 2, 2047 - i, 0};
    t = build(x, plan, 16); CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_OK);
    CHECK(out.tables == 16 && x->f.allocations == 16);
    CHECK(x->b.roots[0].owner == &x->b.owner && x->b.roots[1].owner == &x->b.owner);
    finish(x);
}
static void reject_plan(struct uatb_node *p, size_t count, enum uatb_code why) {
    struct fixture *x = start(); struct uatb_ticket t = {NULL, 123};
    CHECK(uatb_build(&x->b, p, count, x->w, &t) == why);
    CHECK(!t.owner && t.generation == 123 && !x->f.allocations && !x->f.writes);
    CHECK(uatb_shutdown(&x->b) == why); finish(x);
}
static void test_plans(void) {
    ++groups;
    for (unsigned mode = 0; mode < 11; ++mode) {
        struct uatb_node p[3]; memcpy(p, basic, sizeof(p));
        switch (mode) {
        case 0: p[1].permissions = 1; break;
        case 1: p[0].entry = 2; break;
        case 2: p[1].entry = 8; break;
        case 3: p[2].entry = 2048; break;
        case 4: p[1].parent = 1; break; /* self cycle */
        case 5: p[1].parent = 2; p[2].parent = 1; break; /* cycle */
        case 6: p[2] = p[1]; break; /* repeated parent entry */
        case 7: p[2] = p[0]; break; /* repeated detached root */
        case 8: p[2].level = 2; break; /* wrong parent level */
        case 9: p[2].level = 4; break; /* no data/leaf level */
        case 10: p[0].parent = 0; break;
        }
        reject_plan(p, 3, UATB_PLAN);
    }
    struct uatb_node p[17] = {{0}};
    reject_plan(p, 17, UATB_LIMIT); reject_plan(p, 0, UATB_ARGUMENT);
    p[0] = (struct uatb_node){0, UATB_NO_PARENT, 0, 0}; reject_plan(p, 1, UATB_PLAN);
    p[0] = (struct uatb_node){1, UATB_NO_PARENT, 0, UINT32_MAX}; reject_plan(p, 1, UATB_PLAN);
    /* Acyclic but not in the required parent-first order. */
    struct uatb_node forward[] = {{3, 1, 0, 0}, {2, 2, 0, 0}, {1, UATB_NO_PARENT, 0, 0}};
    reject_plan(forward, 3, UATB_PLAN);
}
static void test_failures(void) {
    ++groups;
    for (unsigned mode = 0; mode < 3; ++mode) for (unsigned nth = 1; nth <= 3; ++nth) {
        struct fixture *x = start(); struct uatb_ticket t = {NULL, 44};
        if (mode == 0) x->f.fail_alloc = nth;
        if (mode == 1) x->f.fail_prepare = nth;
        if (mode == 2) x->f.fail_write = nth;
        enum uatb_code expected = mode == 2 ? UATB_WRITE : UATB_ALLOC;
        CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == expected);
        CHECK(!t.owner && t.generation == 44);
        CHECK(x->f.completes == (mode == 2 ? 3 : nth - 1));
        CHECK(x->f.releases == (mode == 2 ? 3 : mode == 1 ? nth : nth - 1));
        unsigned completed = x->f.completes, released = x->f.releases;
        CHECK(uatb_shutdown(&x->b) == expected);
        CHECK(x->f.completes == completed && x->f.releases == released);
        CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_STATE); finish(x);
    }
    struct fixture *x = start(); x->f.corrupt_write = 1; struct uatb_ticket t = {0};
    CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_VALIDATE);
    CHECK(!t.owner && x->f.completes == 3 && x->f.releases == 3); finish(x);
    x = start(); x->f.corrupt_validation = 1;
    CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_VALIDATE);
    CHECK(x->f.completes == 3 && x->f.releases == 3); finish(x);
}
static void test_quarantine(void) {
    ++groups;
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct fixture *x = start(); struct uatb_ticket t = {0};
        if (mode == 0) { x->f.fail_write = 2; x->f.fail_complete = 1; }
        if (mode == 1) x->f.uncertain_write = 2;
        if (mode == 2) x->f.stale_write = 1;
        CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_QUARANTINE);
        CHECK(!t.owner);
        CHECK(x->f.completes == (mode == 0 ? 3u : 0u));
        CHECK(x->f.releases == (mode == 0 ? 2u : 0u));
        unsigned complete = x->f.completes;
        CHECK(uatb_shutdown(&x->b) == UATB_QUARANTINE);
        CHECK(uatb_shutdown(&x->b) == UATB_QUARANTINE);
        CHECK(x->f.completes == complete); finish(x);
    }
    struct fixture *x = start(); struct uatb_ticket t = build(x, basic, 3);
    CHECK(uatb_quarantine(&x->b, t) == UATB_QUARANTINE);
    CHECK(!x->f.completes && !x->f.releases); finish(x);
    x = start(); t = build(x, basic, 3); x->f.fail_complete = 2;
    CHECK(uatb_shutdown(&x->b) == UATB_QUARANTINE);
    CHECK(x->f.completes == 3 && x->f.releases == 2); finish(x);
}
static void test_stale_views(void) {
    ++groups;
    for (unsigned mode = 1; mode <= 7; ++mode) {
        struct fixture *x = start(); struct uatb_ticket t = build(x, basic, 3);
        x->f.changed_view = mode; struct duat_summary out = {9, 10, 11};
        CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_QUARANTINE);
        CHECK(out.tables == 9 && out.pages == 10 && out.mapped_bytes == 11);
        CHECK(!x->f.completes && !x->f.releases); finish(x);
    }
    struct fixture *x = start(); struct uatb_ticket t = build(x, basic, 3);
    struct duat_summary out;
    struct fixture *y = start(); struct uatb_ticket other = build(y, basic, 3);
    CHECK(uatb_validate(&x->b, other, x->w, &out) == UATB_ARGUMENT);
    other = t; ++other.generation;
    CHECK(uatb_validate(&x->b, other, x->w, &out) == UATB_ARGUMENT);
    struct duat_view v; void *h;
    CHECK(!uatb_owned_view(&x->b.owner, y->b.tokens[0], &v, &h));
    struct duat_token stale = x->b.tokens[0]; ++stale.generation;
    CHECK(!uatb_owned_view(&x->b.owner, stale, &v, &h));
    stale = x->b.tokens[0]; stale.slot = DUAT_CAPACITY;
    CHECK(!uatb_owned_view(&x->b.owner, stale, &v, &h));
    struct uatb_context copy = x->b; CHECK(uatb_shutdown(&copy) == UATB_ARGUMENT);
    x->f.pages[2]->bytes[0] = 3; /* A forbidden leaf in live private storage. */
    CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_VALIDATE);
    CHECK(x->f.completes == 3 && x->f.releases == 3); finish(y); finish(x);
}
static void test_write_spans(void) {
    ++groups; struct fixture *x = start(); (void)build(x, basic, 3);
    struct duat_token t = x->b.tokens[0]; struct duat_view v; void *h;
    const uint8_t *source = x->w->pages[0];
    CHECK(uatb_write_view(&x->b.owner, t, 0, source, DUAT_BYTES, &v, &h) == UATB_SPAN_READY);
    CHECK(uatb_write_view(&x->b.owner, t, DUAT_BYTES - 1, source, 1, &v, &h) == UATB_SPAN_READY);
    CHECK(uatb_write_view(&x->b.owner, t, DUAT_BYTES, source, 1, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, DUAT_BYTES + 1, source, 1, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, SIZE_MAX, source, 1, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 1, source, SIZE_MAX, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 0, source, 0, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 0, NULL, 1, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 0, (const uint8_t *)UINTPTR_MAX, 2, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 0, x->f.pages[0]->bytes, 1, &v, &h) == UATB_SPAN_BAD);
    CHECK(uatb_write_view(&x->b.owner, t, 0, x->f.pages[0]->bytes + DUAT_BYTES - 1, 1, &v, &h) == UATB_SPAN_BAD);
    ++t.generation;
    CHECK(uatb_write_view(&x->b.owner, t, 0, source, 1, &v, &h) == UATB_SPAN_UNCERTAIN);
    finish(x);
}
static void test_physical_admission(void) {
    ++groups;
    for (unsigned mode = 1; mode <= 5; ++mode) {
        struct fixture *x = start(); x->f.bad_pa = mode; struct uatb_ticket t = {0};
        if (mode == 4) {
            t = build(x, basic, 1); struct duat_summary out;
            CHECK(uatb_validate(&x->b, t, x->w, &out) == UATB_OK); CHECK(out.tables == 1);
        } else {
            CHECK(uatb_build(&x->b, basic, 3, x->w, &t) == UATB_ALLOC);
            CHECK(!t.owner && !x->f.writes);
            CHECK(x->f.completes == (mode == 5 ? 2u : 1u));
            CHECK(x->f.releases == x->f.completes);
        }
        finish(x);
    }
}
int main(void) {
    test_valid(); test_plans(); test_failures(); test_quarantine(); test_stale_views(); test_write_spans(); test_physical_admission();
    printf("PASS %u groups %u checks\n", groups, checks); return 0;
}
