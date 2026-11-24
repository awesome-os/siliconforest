// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Host test of src/sf_dart_table.c: tables are heap pages under made-up physical addresses. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sf_dart_table.h"

#define MAXT 16
static struct { uint64_t phys; uint64_t *va; } tabs[MAXT];
static int ntabs, alloc_budget = 8;
static uint64_t next_phys = 0x87aa00000ULL;

static uint64_t *t_table(void *ctx, uint64_t phys)
{
	for (int i = 0; i < ntabs; i++)
		if (tabs[i].phys == phys)
			return tabs[i].va;
	return NULL;
}

static uint64_t add_table(uint64_t phys)
{
	assert(ntabs < MAXT);
	tabs[ntabs].phys = phys;
	tabs[ntabs].va = calloc(SF_DART_PTES, 8);
	return tabs[ntabs++].phys;
}

static uint64_t t_alloc(void *ctx)
{
	if (alloc_budget-- <= 0)
		return 0;
	next_phys += SF_DART_PAGE;
	return add_table(next_phys);
}

#define LEAF(pa) (((pa) & SF_DART_PTE_PADDR) | SF_DART_PTE_SP_END | SF_DART_PTE_SP_DIS | SF_DART_PTE_VALID)

int main(void)
{
	struct sf_dart_tables t = { .ops = { NULL, t_table, t_alloc } };
	struct sf_dart_walk w;
	uint64_t iova;

	/* the shape boot dc11 found: a root at 0xbfff0c000 with firmware tables in slots 0..2 and 8 */
	add_table(0xbfff0c000ULL);
	t.l1 = tabs[0].va;
	add_table(0xbfff10000ULL);
	t.l1[0] = 0xbfff10001ULL;
	t.l1[8] = 0x8096bc001ULL; /* m1n1's table: not reachable in this test */
	tabs[1].va[153] = 0xbd9de0003ULL; /* crashlog page, as read in dc11 */
	for (int i = 0; i < 0xc0; i++)    /* firmware data: iova 0 -> 0xbd9b7c000 */
		if (i != 153 && i != 151)
			tabs[1].va[i] = LEAF(0xbd9b7c000ULL + ((uint64_t)i << 14));

	/* walk: dc11's numbers */
	assert(sf_dart_table_walk(&t, 0x267000, &w) == 0);
	assert(w.l1e == 0xbfff10001ULL && w.l2_phys == 0xbfff10000ULL && w.pte == 0xbd9de0003ULL);
	assert(w.phys == 0xbd9de3000ULL);
	assert(sf_dart_table_walk(&t, 0x12000000, &w) == 0 && w.l1e == 0 && w.phys == 0);
	/* a valid first-level entry whose table cannot be reached is an error, not "unmapped" */
	assert(sf_dart_table_walk(&t, 0x10000000, &w) < 0);

	/* map in an empty slot: one first-level write, the leaf exactly as dc11 wrote it */
	assert(sf_dart_table_map(&t, 0x12000000, 0x87aa18000ULL, 0x4000, SF_DART_PROT_READ | SF_DART_PROT_WRITE) == 0);
	assert(t.l1_writes == 1 && t.tables_added == 1 && t.leaf_writes == 1);
	assert(sf_dart_table_walk(&t, 0x12000000, &w) == 0 && w.pte == 0xfff087aa18003ULL && w.phys == 0x87aa18000ULL);
	assert((t.l1[9] & SF_DART_PTE_VALID) && (t.l1[9] & ~1ULL) == w.l2_phys);
	/* second mapping in the same slot: no further first-level write */
	assert(sf_dart_table_map(&t, 0x12004000, 0x87aa10000ULL, 0x8000, 3) == 0);
	assert(t.l1_writes == 1 && t.leaf_writes == 3);
	/* read-only mapping sets NO_WRITE */
	assert(sf_dart_table_map(&t, 0x12010000, 0x87ab00000ULL, 0x4000, SF_DART_PROT_READ) == 0);
	assert(sf_dart_table_walk(&t, 0x12010000, &w) == 0 && (w.pte & SF_DART_PTE_NO_WRITE) && !(w.pte & SF_DART_PTE_NO_READ));

	/* never over an existing entry: firmware's leaf, our own leaf; and the failed call leaves no trace */
	uint64_t before = t.leaf_writes - t.leaf_clears;
	assert(sf_dart_table_map(&t, 0x264000, 0x87ac00000ULL, 0x4000, 3) < 0);
	assert(tabs[1].va[153] == 0xbd9de0003ULL);
	assert(sf_dart_table_map(&t, 0x12000000 - 0x4000 + 0x4000, 0x87ac00000ULL, 0x4000, 3) < 0);
	/* a range that runs into a used leaf: the pages written before it are taken back */
	assert(sf_dart_table_map(&t, 0x1200c000, 0x87ac00000ULL, 0x8000, 3) < 0);
	assert(sf_dart_table_walk(&t, 0x1200c000, &w) == 0 && w.pte == 0);
	assert(t.leaf_writes - t.leaf_clears == before);
	/* a first-level slot that is nonzero but not valid is not ours either */
	t.l1[20] = 0x1d1cb8cULL;
	assert(sf_dart_table_map(&t, 20ULL << 25, 0x87ac00000ULL, 0x4000, 3) < 0 && t.l1[20] == 0x1d1cb8cULL);
	/* m1n1's unreachable table: refused, slot untouched */
	assert(sf_dart_table_map(&t, 0x10000000, 0x87ac00000ULL, 0x4000, 3) < 0 && t.l1[8] == 0x8096bc001ULL);
	/* out of tables */
	alloc_budget = 0;
	assert(sf_dart_table_map(&t, 30ULL << 25, 0x87ac00000ULL, 0x4000, 3) < 0 && t.l1[30] == 0);
	alloc_budget = 8;
	/* arguments */
	assert(sf_dart_table_map(&t, 0x12020001, 0x87ac00000ULL, 0x4000, 3) < 0);
	assert(sf_dart_table_map(&t, 0x12020000, 0x1087ac00000ULL, 0x4000, 3) < 0);
	assert(sf_dart_table_map(&t, 0x12020000, 0x87ac00000ULL, 0, 3) < 0);

	/* a range across two first-level slots */
	assert(sf_dart_table_map(&t, (11ULL << 25) - 0x8000, 0x880000000ULL, 0x10000, 3) == 0);
	assert(sf_dart_table_walk(&t, (11ULL << 25) + 0x4000, &w) == 0 && w.phys == 0x88000c000ULL);
	assert(sf_dart_table_range_free(&t, (11ULL << 25) + 0x8000, 0x4000) == 1);
	assert(sf_dart_table_range_free(&t, (11ULL << 25) - 0x8000, 0x4000) == 0);

	/* find: the firmware data segment, a range across tables, and something that is not there */
	assert(sf_dart_table_find(&t, 0xbd9b7c000ULL, 0x40000, 0, 1ULL << 32, &iova) == 0 && iova == 0);
	assert(sf_dart_table_find(&t, 0x880000000ULL, 0x10000, 0, 1ULL << 32, &iova) == 0 && iova == (11ULL << 25) - 0x8000);
	assert(sf_dart_table_find(&t, 0x880004123ULL, 0x100, 0, 1ULL << 32, &iova) == 0 && iova == (11ULL << 25) - 0x4000 + 0x123);
	assert(sf_dart_table_find(&t, 0x990000000ULL, 0x4000, 0, 1ULL << 32, &iova) < 0);
	/* contiguity is required: 151 and 153 break the data segment's run */
	assert(sf_dart_table_find(&t, 0xbd9b7c000ULL, 0x300000, 0, 1ULL << 32, &iova) < 0);

	/* unmap: only what is mapped is counted; firmware entries next to it stay */
	assert(sf_dart_table_unmap(&t, 0x12004000, 0x8000) == 0x8000);
	assert(sf_dart_table_unmap(&t, 0x12004000, 0x8000) == 0);
	assert(sf_dart_table_walk(&t, 0x12000000, &w) == 0 && w.phys == 0x87aa18000ULL);
	assert(tabs[1].va[152] == LEAF(0xbd9b7c000ULL + (152ULL << 14)));

	printf("sf_dart_table: all checks passed (%u first-level writes, %u leaf writes, %u leaf clears, %u tables added)\n",
	       t.l1_writes, t.leaf_writes, t.leaf_clears, t.tables_added);
	return 0;
}
