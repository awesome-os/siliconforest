/* SPDX-License-Identifier: MIT */
/*
 * Host control for fsck-clean.patch: the driver's own ext4_alloc_inode_locked(), compiled from
 * the patched or unpatched source text (test-source.py pastes it in below), against a
 * buffer layer that does what XNU's does with a buffer of the journal read cache: the journaled
 * write releases it, and the storage of an invalid metadata buffer is freed on release. The
 * freed storage is poisoned with zeroes here, as a zone that clears on free does.
 *
 * Prints the bg_itable_unused the allocation stored for a group with every inode in use but
 * the one being allocated. Correct: 0. With the read after release: 8192.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXT4_BG_INODE_UNINIT 1
#define ENOSPC 28
enum vtype { VREG = 1, VDIR = 2 };
typedef uint64_t ino_t64;
#define ino_t ino_t64
#define le16(x) ((uint16_t)(x))
#define le32(x) ((uint32_t)(x))
struct ext4_group_desc {
	uint32_t bg_block_bitmap_lo, bg_inode_bitmap_lo, bg_inode_table_lo;
	uint16_t bg_free_blocks_count_lo, bg_free_inodes_count_lo, bg_used_dirs_count_lo, bg_flags;
	uint32_t bg_exclude_bitmap_lo;
	uint16_t bg_block_bitmap_csum_lo, bg_inode_bitmap_csum_lo, bg_itable_unused_lo, bg_checksum;
	uint32_t bg_block_bitmap_hi, bg_inode_bitmap_hi, bg_inode_table_hi;
	uint16_t bg_free_blocks_count_hi, bg_free_inodes_count_hi, bg_used_dirs_count_hi, bg_itable_unused_hi;
	uint32_t bg_exclude_bitmap_hi;
	uint16_t bg_block_bitmap_csum_hi, bg_inode_bitmap_csum_hi;
	uint32_t bg_reserved;
};
struct ext4_super_block { uint32_t s_first_ino, s_free_inodes_count; };
struct ext4mount {
	struct ext4_super_block em_sb;
	uint32_t em_groups_count, em_inodes_per_group, em_inodes_count, em_blocksize, em_desc_size;
	struct { uint64_t alloc_inodes; } em_stats;
};
struct buf { uint8_t *data; int invalid; };
typedef struct buf *buf_t;

static struct ext4_group_desc disk_gd;
static uint8_t disk_bitmap[4096];
static int from_journal_cache;

static void *buf_dataptr(buf_t bp) { return bp->data; }
static void buf_brelse(buf_t bp)
{
	if (bp->invalid) { memset(bp->data, 0, 4096); free(bp->data); }   /* buf_free_meta_store() */
	else free(bp->data);                                               /* host: no cache to return it to */
	free(bp);
}
static int ext4_blkread(struct ext4mount *emp, uint64_t pblk, buf_t *bpp)
{
	buf_t bp = calloc(1, sizeof(*bp));
	(void)emp; (void)pblk;
	bp->data = malloc(4096); memcpy(bp->data, disk_bitmap, 4096);
	bp->invalid = from_journal_cache;      /* ext4_blkread(): buf_markinvalid() on a journal hit */
	*bpp = bp;
	return 0;
}
static int ext4_meta_bwrite(struct ext4mount *emp, buf_t bp)
{
	(void)emp;
	memcpy(disk_bitmap, bp->data, 4096);   /* ext4_jnl_log_block() copies the image */
	if (!bp->invalid) {
		/* a valid buffer goes back to the cache with its contents: keep the storage alive */
		free(bp);
		return 0;
	}
	buf_brelse(bp);
	return 0;
}
static int ext4_read_group_desc(struct ext4mount *emp, uint32_t grp, struct ext4_group_desc *gd) { (void)emp; (void)grp; *gd = disk_gd; return 0; }
static int ext4_write_group_desc(struct ext4mount *emp, uint32_t grp, const struct ext4_group_desc *gd) { (void)emp; (void)grp; disk_gd = *gd; return 0; }
static int ext4_write_super(struct ext4mount *emp) { (void)emp; return 0; }
static uint64_t ext4_gd_inode_bitmap(struct ext4mount *emp, const struct ext4_group_desc *gd) { (void)emp; return gd->bg_inode_bitmap_lo; }
static int ext4_ino_is_live(struct ext4mount *emp, ino_t ino) { (void)emp; (void)ino; return 0; }
static void ext4_inode_bitmap_csum_set(struct ext4mount *emp, struct ext4_group_desc *gd, const uint8_t *map) { (void)emp; (void)gd; (void)map; }
static int ext4_inode_zero_tail(struct ext4mount *emp, ino_t ino) { (void)emp; (void)ino; return 0; }

@HELPERS@

@ALLOC@

int main(void)
{
	int status = 0;
	for (from_journal_cache = 0; from_journal_cache < 2; from_journal_cache++) {
		struct ext4mount m = { .em_sb = { 11, 1 }, .em_groups_count = 1, .em_inodes_per_group = 8192,
		    .em_inodes_count = 8192, .em_blocksize = 4096, .em_desc_size = 64 };
		ino_t ino = 0;
		memset(disk_bitmap, 0xff, sizeof(disk_bitmap));
		disk_bitmap[1023] = 0x7f;                       /* the last inode of the group is free */
		memset(&disk_gd, 0, sizeof(disk_gd));
		disk_gd.bg_free_inodes_count_lo = 1; disk_gd.bg_itable_unused_lo = 1;
		if (ext4_alloc_inode_locked(&m, VREG, &ino) != 0 || ino != 8192) { printf("alloc failed\n"); return 2; }
		printf("journal_cache_hit=%d bg_itable_unused=%u\n", from_journal_cache, (unsigned)disk_gd.bg_itable_unused_lo);
		if (disk_gd.bg_itable_unused_lo != 0) status = 1;
	}
	return status;
}
