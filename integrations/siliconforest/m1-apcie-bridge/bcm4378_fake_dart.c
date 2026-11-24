/* Original finite software mapping model; all table bytes are ordinary RAM. */
#include "bcm4378_fake_dart.h"

#if defined(KERNEL) && defined(BFD_FAKE_DEVICE)
#error "BFD_FAKE_DEVICE is host-test only; no kernel DART/MMIO path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "fake DART slice requires shared PHY and PMGR writes compiled out"
#endif

#ifdef BFD_FAKE_DEVICE
#include "register_accessor.h"
#include <string.h>

static int fail_io(struct bfd_map *m) { m->faulted = 1; return BFD_IO; }

static int link_ready(const struct apb_io *io)
{
    uint32_t run = 0, link = 0, identity = 0;
    if (!apr_bcm_io_bound(io)) return BFD_ARGUMENT;
    if (io->read32(io->context, APB_PORT0, 0x804, &run)) return BFD_IO;
    if (!(run & 1u)) return BFD_LINK_DOWN;
    if (io->read32(io->context, APB_PORT0, 0x208, &link)) return BFD_IO;
    if ((link & 0x45u) != 1u) return BFD_LINK_DOWN;
    if (io->read32(io->context, APR_BCM_CONFIG, 0, &identity)) return BFD_IO;
    return identity == 0x442514e4u ? BFD_FAKE_OK : BFD_OWNER;
}

static int enter(struct bfd_map *m)
{
    int e;
    if (!m || m->bound != 1) return BFD_ARGUMENT;
    if (m->faulted) return BFD_STATE;
    e = link_ready(&m->io);
    return e == BFD_IO ? fail_io(m) : e;
}

static int pointers_overlap(const void *a, size_t n, const void *b, size_t k)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    /* Caller promises accessible ordinary objects, not numeric MMIO pointers. */
    if (n > UINTPTR_MAX - x || k > UINTPTR_MAX - y) return 1;
    return x < y + k && y < x + n;
}

int bfd_fake_map_compiled(void) { return 1; }

int bfd_init(struct bfd_map *m, const struct apb_io *io, const struct dld_policy *p,
              const struct bfd_storage *storage, uint32_t rid, uint32_t sid)
{
    struct dld_binding binding = {rid, sid, 0};
    uint32_t ttbr;
    uint64_t table;
    int e;
    if (!m || !p || !storage || !storage->root || !storage->leaf) return BFD_ARGUMENT;
    if (m->bound || m->mapped || m->faulted) return BFD_STATE;
    if (rid != 0x100 || dld_bindings_check(&binding, 1, 1, 1)) return BFD_OWNER;
    if (p->aperture.base != DLD_APCIE_BASE || p->aperture.size != DLD_APCIE_SIZE)
        return DLD_RANGE;
    if (storage->root_bytes != DLD_PAGE || storage->leaf_bytes != DLD_PAGE ||
        storage->root_pa == storage->leaf_pa ||
        pointers_overlap(storage->root, DLD_PAGE, storage->leaf, DLD_PAGE)) return DLD_OVERLAP;
    if ((e = dld_ttbr_encode(storage->root_pa, &ttbr))) return e;
    if ((e = dld_table_encode(storage->leaf_pa, &table))) return e;
    if ((e = link_ready(io))) return e;
    *m = (struct bfd_map){0};
    m->io = *io; m->policy = *p; m->storage = *storage;
    m->binding = binding; m->bound = 1;
    return BFD_FAKE_OK;
}

int bfd_map_pages(struct bfd_map *m, struct bfd_workspace *w, uint64_t iova,
                   uint64_t pa, uint64_t bytes, unsigned permissions)
{
    struct dld_indices start, last;
    struct dld_table tables[2];
    struct dld_image image = {0};
    struct dld_stats stats;
    uint64_t word;
    uint32_t ttbr;
    int e = enter(m);
    if (e) return e;
    if (!w || m->mapped) return BFD_STATE;
    if (!bytes || ((iova | pa | bytes) & (DLD_PAGE - 1))) return DLD_ALIGNMENT;
    if (bytes / DLD_PAGE > DLD_MAX_LEAVES) return BFD_LIMIT;
    if ((e = dld_range_check((struct dld_range){iova, bytes}, DLD_IOVA_LIMIT))) return e;
    if ((e = dld_range_check((struct dld_range){pa, bytes}, DLD_PA_LIMIT))) return e;
    if (!permissions || (permissions & ~(DLD_READ | DLD_WRITE))) return DLD_ARGUMENT;
    if (pointers_overlap(w, sizeof *w, m->storage.root, DLD_PAGE) ||
        pointers_overlap(w, sizeof *w, m->storage.leaf, DLD_PAGE)) return DLD_OVERLAP;
    if ((e = dld_split_iova(iova, &start))) return e;
    if ((e = dld_split_iova(iova + bytes - 1, &last))) return e;
    if (start.l1 != last.l1) return BFD_LIMIT;
    if ((e = dld_ttbr_encode(m->storage.root_pa, &ttbr))) return e;
    if ((e = dld_table_encode(m->storage.leaf_pa, &word))) return e;
    memset(w->root, 0, DLD_PAGE); memset(w->leaf, 0, DLD_PAGE);
    if ((e = dld_word_store(word, w->root + start.l1 * 8, 8))) return e;
    for (uint64_t n = 0; n < bytes / DLD_PAGE; ++n) {
        if ((e = dld_leaf_encode(pa + n * DLD_PAGE, permissions, &word))) return e;
        if ((e = dld_word_store(word, w->leaf + (start.l2 + n) * 8, 8))) return e;
    }
    tables[0] = (struct dld_table){m->storage.root_pa, w->root, DLD_PAGE, DLD_ROOT};
    tables[1] = (struct dld_table){m->storage.leaf_pa, w->leaf, DLD_PAGE, DLD_LEAF_TABLE};
    image.ttbr[0] = ttbr; image.tables = tables; image.table_count = 2;
    if ((e = dld_validate(&m->policy, &image, &w->scratch, &stats))) return e;
    /* Second live-fake check after staging and before any output-table write.
     * Caller serialization prevents async fake mutations during the CPU copy. */
    if ((e = enter(m))) return e;
    memcpy(m->storage.root, w->root, DLD_PAGE);
    memcpy(m->storage.leaf, w->leaf, DLD_PAGE);
    m->tables[0] = tables[0]; m->tables[0].bytes = m->storage.root;
    m->tables[1] = tables[1]; m->tables[1].bytes = m->storage.leaf;
    m->image = image; m->image.tables = m->tables;
    m->mapped_pages = (uint32_t)stats.leaf_count;
    m->table_commits = 1; m->mapped = 1;
    return BFD_FAKE_OK;
}

int bfd_translate(struct bfd_map *m, struct bfd_workspace *w, uint32_t rid, uint32_t sid,
                   uint64_t iova, unsigned access, uint64_t *pa)
{
    int e = enter(m);
    if (e) return e;
    if (!w || !pa) return BFD_ARGUMENT;
    if (rid != m->binding.rid || sid != m->binding.sid) return BFD_OWNER;
    if (!m->mapped) return DLD_UNMAPPED;
    if (pointers_overlap(w, sizeof *w, m->storage.root, DLD_PAGE) ||
        pointers_overlap(w, sizeof *w, m->storage.leaf, DLD_PAGE)) return DLD_OVERLAP;
    return dld_translate(&m->policy, &m->image, &w->scratch, iova, access, pa);
}
#else
int bfd_fake_map_compiled(void) { return 0; }
int bfd_init(struct bfd_map *m, const struct apb_io *io, const struct dld_policy *p,
              const struct bfd_storage *s, uint32_t rid, uint32_t sid)
{ (void)m; (void)io; (void)p; (void)s; (void)rid; (void)sid; return BFD_GATED; }
int bfd_map_pages(struct bfd_map *m, struct bfd_workspace *w, uint64_t iova,
                   uint64_t pa, uint64_t bytes, unsigned permissions)
{ (void)m; (void)w; (void)iova; (void)pa; (void)bytes; (void)permissions; return BFD_GATED; }
int bfd_translate(struct bfd_map *m, struct bfd_workspace *w, uint32_t rid, uint32_t sid,
                   uint64_t iova, unsigned access, uint64_t *pa)
{ (void)m; (void)w; (void)rid; (void)sid; (void)iova; (void)access; (void)pa; return BFD_GATED; }
#endif
