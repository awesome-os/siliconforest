/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Randomised comparison of ../c/sf_xarray.c against a reference model.
 *
 * What is under test is linked exactly as in the kext: the layer's unmodified
 * linux_radix.c and linux_xarray.c plus sf_xarray.c, compiled for the host with the
 * stand-in headers in shim/. The model is a flat table searched linearly, written from
 * the documented behaviour of Linux 7.2's normal XArray API (lib/xarray.c), not from the
 * implementation.
 *
 * Every step applies one random operation to both and compares the result; every
 * CHECK_EVERY steps the whole array is walked with sf_xa_find()/sf_xa_find_after() and
 * compared with the model, and each model entry is read back through sf_xa_load() and
 * LinuxKPI's xa_load(). Operations also go through LinuxKPI's own entry points (as the DRM
 * C core does on the same arrays) to check that both files agree on the representation.
 * Node allocations fail at random; a failed operation must leave the array unchanged, and
 * an operation that may sleep must succeed after waiting. At the end every array is
 * destroyed and the allocator must be back at zero live nodes.
 *
 *     test_xarray [seeds] [steps per seed]
 */
#include <linux/sf_xarray.h>
#include <linux/err.h>
#include <stdio.h>

/* ---- allocator with failure injection ------------------------------------------- */
static long live_allocs, total_allocs, failed_allocs, waits;
static unsigned fail_permille;		/* chance that a node allocation fails */
static int grace;			/* allocations that must succeed (after a wait) */
static uint64_t rng_state;

static uint64_t rnd(void)
{
	/* xorshift64* */
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return (rng_state * 0x2545F4914F6CDD1DULL);
}
static unsigned long rnd_below(unsigned long n) { return (rnd() % n); }

void *sf_host_malloc(size_t size, int flags)
{
	void *p;

	assert((flags & M_WAITOK) == 0);	/* the tree allocates under the array lock */
	total_allocs++;
	if (grace > 0)
		grace--;
	else if (fail_permille != 0 && rnd_below(1000) < fail_permille) {
		failed_allocs++;
		return (NULL);
	}
	p = (flags & M_ZERO) ? calloc(1, size) : (malloc)(size);
	assert(p != NULL);
	live_allocs++;
	return (p);
}
void sf_host_free(void *p)
{
	if (p != NULL)
		live_allocs--;
	(free)(p);
}
void sf_host_vm_wait(void)
{
	waits++;
	grace = 16;	/* more than one insertion can need (11 levels) */
}

/* ---- reference model ---------------------------------------------------------- */
enum { ABSENT, RESERVED, VALUE };
struct ent { unsigned long index; int state; void *val; };
#define	MODEL_MAX	4096
struct model { struct ent e[MODEL_MAX]; int n; uint32_t flags; };

static struct ent *m_find(struct model *m, unsigned long index)
{
	for (int i = 0; i < m->n; i++)
		if (m->e[i].index == index)
			return (&m->e[i]);
	return (NULL);
}
static int m_state(struct model *m, unsigned long index)
{
	struct ent *e = m_find(m, index);
	return (e ? e->state : ABSENT);
}
static void *m_value(struct model *m, unsigned long index)	/* what xa_load() returns */
{
	struct ent *e = m_find(m, index);
	return (e && e->state == VALUE ? e->val : NULL);
}
static void *m_raw(struct model *m, unsigned long index)	/* what the raw API sees */
{
	struct ent *e = m_find(m, index);
	if (e == NULL)
		return (NULL);
	return (e->state == RESERVED ? SF_XA_ZERO_ENTRY : e->val);
}
static void m_set(struct model *m, unsigned long index, int state, void *val)
{
	struct ent *e = m_find(m, index);

	if (state == ABSENT) {
		if (e != NULL)
			*e = m->e[--m->n];
		return;
	}
	if (e == NULL) {
		assert(m->n < MODEL_MAX);
		e = &m->e[m->n++];
		e->index = index;
	}
	e->state = state;
	e->val = val;
}
static void m_set_raw(struct model *m, unsigned long index, void *raw)
{
	if (raw == NULL)
		m_set(m, index, ABSENT, NULL);
	else if (raw == SF_XA_ZERO_ENTRY)
		m_set(m, index, RESERVED, NULL);
	else
		m_set(m, index, VALUE, raw);
}
static bool m_track_free(struct model *m) { return ((m->flags & (XA_FLAGS_ALLOC | XA_FLAGS_ALLOC1)) != 0); }
/* lowest index in [min, max] with no entry and no reservation; false when none */
static bool m_lowest_free(struct model *m, unsigned long min, unsigned long max, unsigned long *out)
{
	if ((m->flags & XA_FLAGS_ALLOC1) && min == 0)
		min = 1;
	for (unsigned long i = min; i <= max; i++)
		if (m_state(m, i) == ABSENT) {
			*out = i;
			return (true);
		}
	return (false);
}
/* lowest VALUE entry in [start, max] */
static struct ent *m_next(struct model *m, unsigned long start, unsigned long max)
{
	struct ent *best = NULL;

	for (int i = 0; i < m->n; i++) {
		struct ent *e = &m->e[i];
		if (e->state == VALUE && e->index >= start && e->index <= max &&
		    (best == NULL || e->index < best->index))
			best = e;
	}
	return (best);
}

/* ---- the comparison ------------------------------------------------------------ */
static unsigned long checks;
#define	CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s:%d: %s (seed %llu step %lu op %s)\n", __FILE__, __LINE__, \
		    #cond, (unsigned long long)cur_seed, cur_step, cur_op);	\
		exit(1);						\
	}								\
} while (0)
static uint64_t cur_seed;
static unsigned long cur_step;
static const char *cur_op = "";

static const unsigned long edge[] = {
	63, 64, 4095, 4096, 262143, 262144, 0x00ffffffUL, 0x01000000UL, 0x3fffffffUL,
	0x40000000UL, 0xfffffffeUL, 0xffffffffUL,
};
static unsigned long rnd_index(void)
{
	unsigned r = rnd_below(100);

	if (r < 55)
		return (rnd_below(24));			/* dense, so that collisions happen */
	if (r < 85)
		return (rnd_below(300));
	if (r < 93)
		return (edge[rnd_below(sizeof(edge) / sizeof(edge[0]))]);
	return (rnd_below(0x100000000UL));
}
static void *rnd_ptr(void)
{
	/* like Rust's into_foreign(): at least 4-byte aligned, never NULL */
	return ((void *)(uintptr_t)(0x100000 + rnd_below(1 << 20) * 8));
}
static gfp_t rnd_gfp(void) { return (rnd_below(2) ? GFP_KERNEL : GFP_NOWAIT); }

static void check_all(struct xarray *xa, struct model *m)
{
	unsigned long index = 0, start = 0, n = 0;
	void *p;
	struct ent *e;
	unsigned saved = fail_permille;

	fail_permille = 0;
	p = sf_xa_find(xa, &index, ULONG_MAX, SF_XA_PRESENT);
	for (;;) {
		e = m_next(m, start, ULONG_MAX);
		if (e == NULL) {
			CHECK(p == NULL);
			break;
		}
		CHECK(p == e->val);
		CHECK(index == e->index);
		n++;
		if (index == ULONG_MAX)
			break;
		start = index + 1;
		p = sf_xa_find_after(xa, &index, ULONG_MAX, SF_XA_PRESENT);
	}
	for (int i = 0; i < m->n; i++) {
		CHECK(sf_xa_load(xa, m->e[i].index) == m_value(m, m->e[i].index));
		CHECK(xa_load(xa, (uint32_t)m->e[i].index) == m_value(m, m->e[i].index));
	}
	CHECK(xa_empty(xa) == (m->n == 0));
	CHECK(xa->xa_lock.depth == 0);
	fail_permille = saved;
}

static void step(struct xarray *xa, struct model *m)
{
	unsigned long index = rnd_index(), max, got, want;
	void *p = rnd_ptr(), *old, *ret, *raw;
	gfp_t gfp = rnd_gfp();
	struct xa_limit limit;
	struct ent *e;
	uint32_t id;
	int rc, st;

	switch (rnd_below(16)) {
	case 0:
		cur_op = "load";
		CHECK(sf_xa_load(xa, index) == m_value(m, index));
		CHECK(xa_load(xa, (uint32_t)index) == m_value(m, index));
		break;
	case 1:
		cur_op = "store";
		if (rnd_below(8) == 0)
			p = NULL;
		old = m_value(m, index);
		xa_lock(xa);
		ret = sf___xa_store(xa, index, p, gfp);
		xa_unlock(xa);
		if (IS_ERR(ret)) {
			/* only memory can fail, and only for a caller that may not sleep */
			CHECK(PTR_ERR(ret) == -ENOMEM && gfp == GFP_NOWAIT);
			break;
		}
		CHECK(ret == old);
		if (p != NULL)
			m_set(m, index, VALUE, p);
		else
			m_set(m, index, m_track_free(m) ? RESERVED : ABSENT, NULL);
		break;
	case 2:
		cur_op = "insert";
		if (rnd_below(6) == 0)
			p = NULL;	/* reserve */
		st = m_state(m, index);
		xa_lock(xa);
		rc = sf___xa_insert(xa, index, p, gfp);
		xa_unlock(xa);
		if (st != ABSENT) {
			CHECK(rc == -EBUSY);
		} else if (rc != 0) {
			CHECK(rc == -ENOMEM && gfp == GFP_NOWAIT);
		} else {
			m_set(m, index, p ? VALUE : RESERVED, p);
		}
		break;
	case 3:
	case 4:
		cur_op = "erase";
		old = m_value(m, index);
		xa_lock(xa);
		ret = sf___xa_erase(xa, index);
		xa_unlock(xa);
		CHECK(ret == old);
		m_set(m, index, ABSENT, NULL);
		break;
	case 5:
	case 6:
		cur_op = "cmpxchg_raw";
		raw = m_raw(m, index);
		switch (rnd_below(5)) {
		case 0: old = NULL; break;
		case 1: old = SF_XA_ZERO_ENTRY; break;
		case 2: old = rnd_ptr(); break;
		default: old = raw; break;
		}
		switch (rnd_below(4)) {
		case 0: p = NULL; break;
		case 1: p = SF_XA_ZERO_ENTRY; break;
		default: break;
		}
		xa_lock(xa);
		ret = sf___xa_cmpxchg_raw(xa, index, old, p, gfp);
		xa_unlock(xa);
		if (IS_ERR(ret)) {
			/* allocation is only needed to put something where nothing was */
			CHECK(PTR_ERR(ret) == -ENOMEM && gfp == GFP_NOWAIT);
			CHECK(raw == NULL && old == NULL && p != NULL);
			break;
		}
		CHECK(ret == raw);
		if (raw == old)
			m_set_raw(m, index, p);
		break;
	case 7:
	case 8:
	case 9:
		cur_op = "alloc";
		if (rnd_below(5) == 0)
			p = NULL;	/* reserve_limit() */
		switch (rnd_below(6)) {
		case 0: limit = XA_LIMIT(0, UINT32_MAX); break;
		case 1: limit = XA_LIMIT(1, UINT32_MAX); break;
		case 2: limit.min = limit.max = rnd_below(24); break;		/* one candidate */
		case 3: limit.min = rnd_below(24); limit.max = limit.min + rnd_below(8); break;
		case 4: limit.min = rnd_below(300); limit.max = rnd_below(300); break;	/* may be empty */
		default: limit.min = 0; limit.max = rnd_below(40); break;
		}
		xa_lock(xa);
		id = 0xdeadbeef;
		rc = sf___xa_alloc(xa, &id, p, limit, gfp);
		xa_unlock(xa);
		if (!m_track_free(m)) {
			CHECK(rc == -EINVAL);
			break;
		}
		if (!m_lowest_free(m, limit.min, limit.max, &want)) {
			CHECK(rc == -EBUSY);
			break;
		}
		if (rc != 0) {
			CHECK(rc == -ENOMEM && gfp == GFP_NOWAIT);
			break;
		}
		CHECK(id == want);
		m_set(m, want, p ? VALUE : RESERVED, p);
		break;
	case 10:
	case 11:
		max = rnd_below(3) ? ULONG_MAX : rnd_index();
		got = index;
		if (rnd_below(2)) {
			cur_op = "find";
			ret = sf_xa_find(xa, &got, max, SF_XA_PRESENT);
			e = m_next(m, index, max);
		} else {
			cur_op = "find_after";	/* starts one further */
			ret = sf_xa_find_after(xa, &got, max, SF_XA_PRESENT);
			e = index == ULONG_MAX ? NULL : m_next(m, index + 1, max);
		}
		if (e == NULL) {
			CHECK(ret == NULL);
			CHECK(got == index);	/* untouched when nothing is found */
		} else {
			CHECK(ret == e->val);
			CHECK(got == e->index);
		}
		break;
	case 12:
		cur_op = "lkpi xa_store";	/* what C callers of the same array do */
		if (rnd_below(8) == 0)
			p = NULL;
		old = m_value(m, index);
		ret = xa_store(xa, (uint32_t)index, p, GFP_KERNEL);
		CHECK(ret == old);
		/* LinuxKPI reserves on NULL whatever the array's flags */
		m_set(m, index, p ? VALUE : RESERVED, p);
		break;
	case 13:
		cur_op = "lkpi xa_erase";
		old = m_value(m, index);
		CHECK(xa_erase(xa, (uint32_t)index) == old);
		m_set(m, index, ABSENT, NULL);
		break;
	case 14:
		cur_op = "lkpi xa_insert";
		if (rnd_below(4) == 0)
			p = NULL;
		st = m_state(m, index);
		rc = xa_insert(xa, (uint32_t)index, p, GFP_KERNEL);
		if (st != ABSENT) {
			CHECK(rc == -EEXIST);	/* LinuxKPI's answer; Linux says -EBUSY */
		} else {
			CHECK(rc == 0);
			m_set(m, index, p ? VALUE : RESERVED, p);
		}
		break;
	default:
		cur_op = "out of range";
		index = 0x100000000UL + rnd_below(1UL << 40);
		xa_lock(xa);
		CHECK(PTR_ERR(sf___xa_store(xa, index, p, gfp)) == -EINVAL);
		CHECK(sf___xa_insert(xa, index, p, gfp) == -EINVAL);
		CHECK(PTR_ERR(sf___xa_cmpxchg_raw(xa, index, NULL, p, gfp)) == -EINVAL);
		CHECK(sf___xa_erase(xa, index) == NULL);
		/* values the array cannot hold */
		CHECK(PTR_ERR(sf___xa_store(xa, 5, (void *)0x1, gfp)) == -EINVAL);
		CHECK(PTR_ERR(sf___xa_store(xa, 5, SF_XA_ZERO_ENTRY, gfp)) == -EINVAL);
		CHECK(sf___xa_insert(xa, 5, ERR_PTR(-ENOMEM), gfp) == -EINVAL);
		xa_unlock(xa);
		CHECK(sf_xa_load(xa, index) == NULL);
		got = index;
		CHECK(sf_xa_find(xa, &got, ULONG_MAX, SF_XA_PRESENT) == NULL);
		CHECK(sf_xa_find(xa, &(unsigned long){ 0 }, ULONG_MAX, 0 /* XA_MARK_0 */) == NULL);
		break;
	}
}

/* The call sequence of rust/kernel/xarray.rs as drivers/gpu/drm/asahi/file.rs uses it. */
static void scenario_rust_wrapper(void)
{
	struct xarray xa;
	unsigned long index;
	uint32_t id, id2;
	void *a = (void *)0x1000, *b = (void *)0x2000;

	cur_op = "scenario";
	fail_permille = 0;
	xa_init_flags(&xa, XA_FLAGS_ALLOC1);				/* XArray::new(Alloc1) */

	xa_lock(&xa);							/* lock().reserve_limit(1..=u32::MAX) */
	CHECK(sf___xa_alloc(&xa, &id, NULL, XA_LIMIT(1, UINT32_MAX), GFP_KERNEL) == 0);
	xa_unlock(&xa);
	CHECK(id == 1);							/* ids start at 1, never 0 */
	CHECK(sf_xa_load(&xa, 1) == NULL);				/* a reservation reads as nothing */
	index = 0;
	CHECK(sf_xa_find(&xa, &index, ULONG_MAX, SF_XA_PRESENT) == NULL);	/* and is not iterated */

	xa_lock(&xa);							/* a second id while the first is only reserved */
	CHECK(sf___xa_alloc(&xa, &id2, NULL, XA_LIMIT(1, UINT32_MAX), GFP_KERNEL) == 0);
	CHECK(id2 == 2);
	/* Reservation::fill(): cmpxchg the zero entry for the value */
	CHECK(sf___xa_cmpxchg_raw(&xa, id, SF_XA_ZERO_ENTRY, a, 0) == SF_XA_ZERO_ENTRY);
	CHECK(xa_load(&xa, id) == a);					/* Guard::get(), lock held: recursion */
	/* filling twice fails the way the wrapper detects it (ret != old -> EBUSY) */
	CHECK(sf___xa_cmpxchg_raw(&xa, id, SF_XA_ZERO_ENTRY, b, 0) == a);
	/* Reservation dropped unfilled: cmpxchg the zero entry for NULL releases the id */
	CHECK(sf___xa_cmpxchg_raw(&xa, id2, SF_XA_ZERO_ENTRY, NULL, 0) == SF_XA_ZERO_ENTRY);
	CHECK(sf___xa_alloc(&xa, &id2, b, XA_LIMIT(1, UINT32_MAX), GFP_KERNEL) == 0);
	CHECK(id2 == 2);						/* released ids are handed out again */
	/* Guard::insert() on a taken id */
	CHECK(sf___xa_insert(&xa, id, b, GFP_KERNEL) == -EBUSY);
	xa_unlock(&xa);

	index = 0;							/* XArray::find(0, usize::MAX) */
	CHECK(sf_xa_find(&xa, &index, ULONG_MAX, SF_XA_PRESENT) == a && index == 1);
	CHECK(sf_xa_find_after(&xa, &index, ULONG_MAX, SF_XA_PRESENT) == b && index == 2);
	CHECK(sf_xa_find_after(&xa, &index, ULONG_MAX, SF_XA_PRESENT) == NULL);

	xa_lock(&xa);							/* XArray::remove() */
	CHECK(sf___xa_erase(&xa, 1) == a);
	CHECK(sf___xa_erase(&xa, 1) == NULL);
	xa_unlock(&xa);
	CHECK(sf_xa_trylock(&xa) == 1);					/* try_lock() */
	xa_unlock(&xa);

	/* a one-id range: full after one allocation */
	xa_lock(&xa);
	CHECK(sf___xa_alloc(&xa, &id, a, XA_LIMIT(7, 7), GFP_KERNEL) == 0 && id == 7);
	CHECK(sf___xa_alloc(&xa, &id, a, XA_LIMIT(7, 7), GFP_KERNEL) == -EBUSY);
	/* an ALLOC1 array still accepts an explicit store at 0, it only never allocates it */
	CHECK(sf___xa_alloc(&xa, &id, a, XA_LIMIT(0, 0), GFP_KERNEL) == -EBUSY);
	CHECK(sf___xa_store(&xa, 0, a, GFP_KERNEL) == NULL);
	xa_unlock(&xa);

	xa_destroy(&xa);						/* PinnedDrop */
	CHECK(xa_empty(&xa));
	CHECK(live_allocs == 0);
}

int main(int argc, char **argv)
{
	unsigned long seeds = argc > 1 ? strtoul(argv[1], NULL, 0) : 120;
	unsigned long steps = argc > 2 ? strtoul(argv[2], NULL, 0) : 20000;
	static const uint32_t kinds[] = { 0, XA_FLAGS_ALLOC, XA_FLAGS_ALLOC1 };
	static const unsigned fails[] = { 0, 0, 30, 300 };
	static struct model m;
	enum { CHECK_EVERY = 257 };

	rng_state = 1;
	scenario_rust_wrapper();

	for (uint64_t seed = 1; seed <= seeds; seed++) {
		struct xarray xa;

		cur_seed = seed;
		rng_state = seed * 0x9E3779B97F4A7C15ULL + 1;
		memset(&m, 0, sizeof(m));
		m.flags = kinds[seed % 3];
		fail_permille = fails[(seed / 3) % 4];
		grace = 0;
		xa_init_flags(&xa, m.flags);
		for (cur_step = 0; cur_step < steps; cur_step++) {
			step(&xa, &m);
			/* keep the model small enough for its linear scans */
			if (m.n > MODEL_MAX - 8) {
				cur_op = "trim";
				while (m.n > MODEL_MAX / 2) {
					xa_erase(&xa, (uint32_t)m.e[m.n - 1].index);
					m.n--;
				}
			}
			if (cur_step % CHECK_EVERY == 0)
				check_all(&xa, &m);
		}
		check_all(&xa, &m);
		fail_permille = 0;
		xa_destroy(&xa);
		CHECK(xa_empty(&xa));
		CHECK(live_allocs == 0);	/* no node leaked, also not on the failure paths */
	}
	printf("test_xarray: ok: %lu seeds x %lu steps, %lu checks, %ld node allocations "
	    "(%ld failed by injection, %ld waits), 0 live at exit\n",
	    seeds, steps, checks, total_allocs, failed_allocs, waits);
	return (0);
}
