/* SPDX-License-Identifier: MIT */
/* SiliconForest stage, the part without hardware. See pdsf.h. */

#include "pdsf.h"

#define ALIGN16K(x) (((x) + PDSF_PAGE - 1) & ~(PDSF_PAGE - 1))

static u32 rd32(const u8 *p)
{
    u32 v;
    memcpy(&v, p, 4);
    return v;
}

static u64 rd64(const u8 *p)
{
    u64 v;
    memcpy(&v, p, 8);
    return v;
}

static int streq(const char *a, const char *b)
{
    return !strcmp(a, b);
}

/* ---- container ---- */

int pdsf_scan(const u8 *p, u64 limit, struct pdsf_set *set)
{
    u64 off = 0;

    memset(set, 0, sizeof(*set));
    if (limit < sizeof(struct pdsf_record) || rd32(p) != PDSF_MAGIC)
        return 1;
    for (;;) {
        struct pdsf_record r;

        if (off > limit || limit - off < sizeof(r))
            return -1;
        memcpy(&r, p + off, sizeof(r));
        if (r.magic != PDSF_MAGIC || r.version != PDSF_VERSION || r.type >= PDSF_TYPES)
            return -2;
        if (r.flags & ~PDSF_F_GZIP)
            return -3;
        off += sizeof(r);
        if (r.type == PDSF_END) {
            if (r.stored || r.raw)
                return -4;
            set->end = p + off;
            break;
        }
        if (!r.stored || !r.raw || r.stored > limit - off || set->item[r.type].present)
            return -5;
        if (!(r.flags & PDSF_F_GZIP) && r.raw != r.stored)
            return -6;
        set->item[r.type] = (struct pdsf_item){p + off, r.stored, r.raw, r.flags, r.crc32, 1};
        off += r.stored;
        off = (off + PDSF_ALIGN - 1) & ~(u64)(PDSF_ALIGN - 1);
    }
    return 0;
}

/* ---- configuration ---- */

static int parse_u32(const char *s, u64 len, u32 max, u32 *out)
{
    u64 v = 0;

    if (!len || len > 9)
        return -1;
    for (u64 i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (u64)(s[i] - '0');
    }
    if (v > max)
        return -1;
    *out = (u32)v;
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int key_is(const char *line, u64 klen, const char *key)
{
    return (u64)strlen(key) == klen && !memcmp(line, key, klen);
}

int pdsf_config_parse(const char *text, u64 len, struct pdsf_config *cfg, const char **err)
{
    u64 pos = 0;
    int have_bootargs = 0;

    memset(cfg, 0, sizeof(*cfg));
    cfg->display = 1;
    cfg->usb_remove = PDSF_USB_KEEP;
    cfg->sprr = cfg->amx = 1;
    cfg->reserve_mb = 64;
    cfg->serial = 1;
    *err = "";

    while (pos < len) {
        u64 end = pos, eq;
        const char *line = text + pos, *val;
        u64 vlen;

        while (end < len && text[end] != '\n')
            end++;
        u64 llen = end - pos;
        pos = end + 1;
        if (!llen || line[0] == '#')
            continue;
        for (eq = 0; eq < llen && line[eq] != '='; eq++)
            ;
        if (eq == llen) {
            *err = "a configuration line has no '='";
            return -1;
        }
        val = line + eq + 1;
        vlen = llen - eq - 1;

        if (key_is(line, eq, "bootargs")) {
            if (vlen >= PDSF_CMDLINE_MAX) {
                *err = "bootargs too long";
                return -1;
            }
            for (u64 i = 0; i < vlen; i++)
                if (val[i] < 0x20 || val[i] > 0x7e) {
                    *err = "bootargs: not printable ASCII";
                    return -1;
                }
            memcpy(cfg->bootargs, val, vlen);
            cfg->bootargs[vlen] = 0;
            have_bootargs = 1;
        } else if (key_is(line, eq, "display")) {
            if (parse_u32(val, vlen, 2, &cfg->display)) {
                *err = "display is 0..2";
                return -1;
            }
        } else if (key_is(line, eq, "usb-nodes")) {
            if (vlen == 4 && !memcmp(val, "keep", 4))
                cfg->usb_remove = PDSF_USB_KEEP;
            else if (vlen == 7 && !memcmp(val, "remove", 6) && (val[6] == '0' || val[6] == '1'))
                cfg->usb_remove = val[6] - '0';
            else {
                *err = "usb-nodes is keep, remove0 or remove1";
                return -1;
            }
        } else if (key_is(line, eq, "sprr")) {
            if (parse_u32(val, vlen, 1, &cfg->sprr)) {
                *err = "sprr is 0 or 1";
                return -1;
            }
        } else if (key_is(line, eq, "amx")) {
            if (parse_u32(val, vlen, 1, &cfg->amx)) {
                *err = "amx is 0 or 1";
                return -1;
            }
        } else if (key_is(line, eq, "serial")) {
            if (parse_u32(val, vlen, 1, &cfg->serial)) {
                *err = "serial is 0 or 1";
                return -1;
            }
        } else if (key_is(line, eq, "wdt")) {
            if (parse_u32(val, vlen, 170, &cfg->wdt) || (cfg->wdt && cfg->wdt < 30)) {
                *err = "wdt is 0 or 30..170 seconds";
                return -1;
            }
        } else if (key_is(line, eq, "reserve-mb")) {
            if (parse_u32(val, vlen, 4096, &cfg->reserve_mb) || cfg->reserve_mb < 16) {
                *err = "reserve-mb is 16..4096";
                return -1;
            }
        } else if (key_is(line, eq, "xnu-base")) {
            u64 v = 0;

            if (vlen < 3 || vlen > 18 || val[0] != '0' || val[1] != 'x') {
                *err = "xnu-base is a hexadecimal address";
                return -1;
            }
            for (u64 i = 2; i < vlen; i++) {
                int d = hexval(val[i]);

                if (d < 0) {
                    *err = "xnu-base is a hexadecimal address";
                    return -1;
                }
                v = v << 4 | (u64)d;
            }
            if (!v || (v & (PDSF_PAGE - 1))) {
                *err = "xnu-base is not on a 16 KiB boundary";
                return -1;
            }
            cfg->xnu_base = v;
        } else if (key_is(line, eq, "seed-hex")) {
            if (vlen != 2 * PDSF_SEED_BYTES) {
                *err = "seed-hex is 512 hexadecimal digits";
                return -1;
            }
            for (u32 i = 0; i < PDSF_SEED_BYTES; i++) {
                int hi = hexval(val[2 * i]), lo = hexval(val[2 * i + 1]);
                if (hi < 0 || lo < 0) {
                    *err = "seed-hex is 512 hexadecimal digits";
                    return -1;
                }
                cfg->seed[i] = (u8)(hi << 4 | lo);
            }
            cfg->have_seed = 1;
        } else {
            *err = "unknown configuration key";
            return -1;
        }
    }
    if (!have_bootargs) {
        *err = "no bootargs= line";
        return -1;
    }
    return 0;
}

static int cmdline_token(const char *cmdline, const char *prefix, int exact, const char **value)
{
    u64 plen = strlen(prefix);
    const char *p = cmdline;
    int found = 0;

    while (*p) {
        const char *start;

        while (*p == ' ')
            p++;
        start = p;
        while (*p && *p != ' ')
            p++;
        u64 tlen = (u64)(p - start);
        if (tlen >= plen && !memcmp(start, prefix, plen) && (!exact || tlen == plen)) {
            found = 1;
            if (value)
                *value = start + plen;
        }
    }
    return found;
}

int pdsf_cmdline_cpus(const char *cmdline)
{
    const char *v = NULL;

    if (!cmdline_token(cmdline, "cpus=", 0, &v))
        return 0;
    if (v[0] < '1' || v[0] > '9' || (v[1] && v[1] != ' '))
        return -1;
    return v[0] - '0';
}

int pdsf_cmdline_has(const char *cmdline, const char *token)
{
    return cmdline_token(cmdline, token, 1, NULL);
}

/*
 * The root a command line names, and whether this stage may boot it. A kernel that waits
 * for a root which never appears is the one thing a stage must not start (it ends in the
 * kernel's recovery path), so the rule is closed:
 *   rd=md0                      only when the image carries a RAM root record;
 *   rd=uuid boot-uuid=<U>       only when <U> is, character for character, the filesystem
 *                               UUID this stage was built for (PDSF_ROOT_UUID; a build
 *                               without one refuses every disk root);
 * exactly one rd= token, and with rd=uuid exactly one boot-uuid= token (a second one would
 * leave it to the kernel's parser which of them counts); nothing else.
 * Returns 0 and the kind (1 RAM root, 2 disk root) or -1 with a reason.
 */
int pdsf_root_check(const char *cmdline, int have_ramdisk, const char *built_for, int *kind, const char **err)
{
    const char *p = cmdline, *rd = NULL, *uuid = NULL;
    u64 rd_len = 0, uuid_len = 0;
    int rds = 0, uuids = 0;

    *err = "";
    if (kind)
        *kind = 0;
    while (*p) {
        const char *start;
        u64 len;

        while (*p == ' ')
            p++;
        start = p;
        while (*p && *p != ' ')
            p++;
        len = (u64)(p - start);
        if (len >= 3 && !memcmp(start, "rd=", 3)) {
            rds++;
            rd = start + 3;
            rd_len = len - 3;
        } else if (len >= 10 && !memcmp(start, "boot-uuid=", 10)) {
            uuids++;
            uuid = start + 10;
            uuid_len = len - 10;
        }
    }
    if (rds != 1) {
        *err = rds ? "more than one rd= argument" : "no rd= argument: the kernel would wait for a root";
        return -1;
    }
    if (rd_len == 3 && !memcmp(rd, "md0", 3)) {
        if (uuids) {
            *err = "rd=md0 together with boot-uuid=";
            return -1;
        }
        if (!have_ramdisk) {
            *err = "rd=md0 without a RAM root record in the image";
            return -1;
        }
        if (kind)
            *kind = 1;
        return 0;
    }
    if (rd_len == 4 && !memcmp(rd, "uuid", 4)) {
        u64 want = built_for ? strlen(built_for) : 0;

        if (uuids != 1) {
            *err = uuids ? "more than one boot-uuid= argument" : "rd=uuid without boot-uuid=";
            return -1;
        }
        if (want == 0) {
            *err = "rd=uuid, and this stage was built for no disk root";
            return -1;
        }
        if (uuid_len != want || memcmp(uuid, built_for, want)) {
            *err = "boot-uuid= is not the filesystem this stage was built for";
            return -1;
        }
        if (kind)
            *kind = 2;
        return 0;
    }
    *err = "rd= is neither md0 nor uuid";
    return -1;
}

/* ---- Mach-O ---- */

#define MH_MAGIC_64    0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000cu
#define MH_FILESET     0xcu
#define LC_SEGMENT_64  0x19u
#define LC_UNIXTHREAD  0x5u
#define ARM_THREAD64   6u
#define MAX_IMAGE      (256ull << 20)

int pdsf_macho_info(const u8 *kc, u64 size, struct pdsf_macho *m, const char **err)
{
    u32 ncmds, sizeofcmds;
    u64 off = 32;

    memset(m, 0, sizeof(*m));
    m->vmin = ~0ull;
    *err = "";
    if (size < 32 || rd32(kc) != MH_MAGIC_64 || rd32(kc + 4) != CPU_TYPE_ARM64) {
        *err = "not an arm64 Mach-O";
        return -1;
    }
    if (rd32(kc + 12) != MH_FILESET) {
        *err = "not an MH_FILESET collection";
        return -1;
    }
    ncmds = rd32(kc + 16);
    sizeofcmds = rd32(kc + 20);
    if (sizeofcmds > size - 32 || ncmds > 4096) {
        *err = "load commands exceed the file";
        return -1;
    }
    for (u32 i = 0; i < ncmds; i++) {
        u32 cmd, cmdsize;

        if (off + 8 > 32 + (u64)sizeofcmds) {
            *err = "truncated load command";
            return -1;
        }
        cmd = rd32(kc + off);
        cmdsize = rd32(kc + off + 4);
        if (cmdsize < 8 || (cmdsize & 7) || cmdsize > 32 + (u64)sizeofcmds - off) {
            *err = "bad load command size";
            return -1;
        }
        if (cmd == LC_SEGMENT_64) {
            u64 vmaddr, vmsize, fileoff, filesize;

            if (cmdsize < 72) {
                *err = "short segment command";
                return -1;
            }
            vmaddr = rd64(kc + off + 24);
            vmsize = rd64(kc + off + 32);
            fileoff = rd64(kc + off + 40);
            filesize = rd64(kc + off + 48);
            if (vmsize > MAX_IMAGE || vmaddr + vmsize < vmaddr || filesize > vmsize ||
                fileoff > size || filesize > size - fileoff) {
                *err = "segment outside the file or its memory";
                return -1;
            }
            if (vmaddr < m->vmin)
                m->vmin = vmaddr;
            if (vmaddr + vmsize > m->vmax)
                m->vmax = vmaddr + vmsize;
            m->segments++;
        } else if (cmd == LC_UNIXTHREAD) {
            /* cmd, cmdsize, flavor, count, x0..x28, fp, lr, sp, pc, cpsr */
            if (cmdsize < 16 + 33 * 8 || rd32(kc + off + 8) != ARM_THREAD64 || m->entry) {
                *err = "unexpected LC_UNIXTHREAD";
                return -1;
            }
            m->entry = rd64(kc + off + 16 + 32 * 8);
        }
        off += cmdsize;
    }
    if (!m->segments || m->vmax <= m->vmin || m->vmax - m->vmin > MAX_IMAGE ||
        (m->vmin & (PDSF_PAGE - 1))) {
        *err = "no usable segments";
        return -1;
    }
    if (m->entry < m->vmin || m->entry >= m->vmax || (m->entry & 3)) {
        *err = "entry point outside the image";
        return -1;
    }
    return 0;
}

int pdsf_macho_load(const u8 *kc, u64 size, const struct pdsf_macho *m, u8 *dest)
{
    u32 ncmds = rd32(kc + 16);
    u64 off = 32;

    (void)size;
    memset(dest, 0, m->vmax - m->vmin);
    for (u32 i = 0; i < ncmds; i++) {
        u32 cmd = rd32(kc + off), cmdsize = rd32(kc + off + 4);

        if (cmd == LC_SEGMENT_64) {
            u64 vmaddr = rd64(kc + off + 24), fileoff = rd64(kc + off + 40);
            u64 filesize = rd64(kc + off + 48);

            if (filesize)
                memcpy(dest + (vmaddr - m->vmin), kc + fileoff, filesize);
        }
        off += cmdsize;
    }
    return 0;
}

/* ---- layout ---- */

int pdsf_layout(const struct pdsf_layout_in *in, struct pdsf_layout *out)
{
    const u64 l2 = 32ull << 20;
    u64 image;

    memset(out, 0, sizeof(*out));
    if (!in->image_size || !in->devtree_size || in->adt_capacity < in->devtree_size ||
        (in->adt_capacity & (PDSF_PAGE - 1)) || in->stage_top > in->ram_end ||
        in->reserve > in->ram_end - in->stage_top)
        return -1;
    if (in->fixed_base) {
        if ((in->fixed_base & (PDSF_PAGE - 1)) || in->fixed_base < in->stage_top + in->reserve)
            return -3;
        out->phys_base = in->fixed_base;
    } else {
        out->phys_base = (in->stage_top + in->reserve + l2 - 1) & ~(l2 - 1);
    }
    out->virt_base = 0xfffffe0010000000ull + (out->phys_base & (l2 - 1));
    out->guest_base =
        out->phys_base + (16ull << 20) + ALIGN16K(in->devtree_size) + ALIGN16K(in->tc_size);
    image = ALIGN16K(in->image_size);
    out->sepfw = out->guest_base + image;
    image += ALIGN16K(in->sepfw_size);
    out->preoslog = out->guest_base + image;
    image += ALIGN16K(in->preoslog_size);
    out->bootargs = out->guest_base + image;
    out->adt = out->bootargs + PDSF_PAGE;
    out->tc = out->adt + in->adt_capacity;
    out->ramdisk = out->tc + ALIGN16K(in->tc_size);
    out->top = out->ramdisk + ALIGN16K(in->ramdisk_size);
    if (out->phys_base >= in->ram_end || out->top < out->phys_base || out->top > in->ram_end)
        return -2;
    out->mem_size = in->ram_end - out->phys_base;
    return 0;
}

/* ---- device tree ---- */

#define ADT_NAME    32
#define ADT_LEN(x)  ((x) & 0x7fffffffu)
#define ADT_PAD(x)  (((x) + 3u) & ~3u)
#define MAX_DEPTH   12
#define MAX_PATH    192
#define OSLOG_ENTRY 32

struct rebuild {
    const u8 *src;
    u64 srclen;
    u8 *dst;
    u64 cap, out;
    struct pdsf_adt_rules *rules;
};

/* One property at off: 0 and its fields, or -1 */
static int prop_at(const u8 *src, u64 srclen, u64 off, const char **name, u32 *size,
                   const u8 **value, u64 *next)
{
    u32 raw;

    if (off > srclen || srclen - off < ADT_NAME + 4)
        return -1;
    if (!memchr(src + off, 0, ADT_NAME))
        return -1;
    raw = rd32(src + off + ADT_NAME);
    if (ADT_PAD(ADT_LEN(raw)) > srclen - off - ADT_NAME - 4)
        return -1;
    *name = (const char *)(src + off);
    *size = raw;
    *value = src + off + ADT_NAME + 4;
    *next = off + ADT_NAME + 4 + ADT_PAD(ADT_LEN(raw));
    return 0;
}

/* Offset after the node at off, or 0 */
static u64 node_end(const u8 *src, u64 srclen, u64 off, int depth)
{
    u32 nprops, nchildren;

    if (depth > MAX_DEPTH || off > srclen || srclen - off < 8)
        return 0;
    nprops = rd32(src + off);
    nchildren = rd32(src + off + 4);
    off += 8;
    for (u32 i = 0; i < nprops; i++) {
        const char *name;
        const u8 *value;
        u32 size;

        if (prop_at(src, srclen, off, &name, &size, &value, &off))
            return 0;
    }
    for (u32 i = 0; i < nchildren; i++) {
        off = node_end(src, srclen, off, depth + 1);
        if (!off)
            return 0;
    }
    return off;
}

long pdsf_adt_size(const u8 *src, u64 srclen)
{
    u64 end = node_end(src, srclen, 0, 0);

    return end ? (long)end : -1;
}

/* A property of the node at off, by name: its value and length, or NULL */
static const u8 *node_prop(const u8 *src, u64 srclen, u64 off, const char *want, u32 *len)
{
    u32 nprops = rd32(src + off);

    off += 8;
    for (u32 i = 0; i < nprops; i++) {
        const char *name;
        const u8 *value;
        u32 size;

        if (prop_at(src, srclen, off, &name, &size, &value, &off))
            return NULL;
        if (streq(name, want)) {
            *len = ADT_LEN(size);
            return value;
        }
    }
    return NULL;
}

static int emit(struct rebuild *rb, const void *data, u64 len)
{
    if (len > rb->cap - rb->out)
        return -1;
    memcpy(rb->dst + rb->out, data, len);
    rb->out += len;
    return 0;
}

static int emit_prop(struct rebuild *rb, const char *name, u32 rawsize, const u8 *value, u32 len)
{
    u8 head[ADT_NAME + 4];
    u8 zero[4] = {0, 0, 0, 0};
    u32 raw = (rawsize & 0x80000000u) | len;

    memset(head, 0, sizeof(head));
    memcpy(head, name, strlen(name));
    memcpy(head + ADT_NAME, &raw, 4);
    if (emit(rb, head, sizeof(head)) || emit(rb, value, len) ||
        emit(rb, zero, ADT_PAD(len) - len))
        return -1;
    return 0;
}

/* Index of "__OS_LOG" in a ';'-separated, NUL-terminated list of len bytes, or -1 */
static int oslog_index(const u8 *names, u32 len)
{
    int idx = 0;
    u32 start = 0, end = len;

    while (end && !names[end - 1])
        end--;
    for (u32 i = 0; i <= end; i++) {
        if (i == end || names[i] == ';') {
            if (i - start == 8 && !memcmp(names + start, "__OS_LOG", 8))
                return idx;
            idx++;
            start = i + 1;
        }
    }
    return -1;
}

static int emit_names_without(struct rebuild *rb, const char *name, u32 raw, const u8 *names,
                              u32 len, int skip)
{
    u8 buf[512];
    u32 n = 0, start = 0, end = len;
    int idx = 0;

    while (end && !names[end - 1])
        end--;
    if (end + 1 > sizeof(buf))
        return -1;
    for (u32 i = 0; i <= end; i++) {
        if (i == end || names[i] == ';') {
            if (idx != skip) {
                if (n)
                    buf[n++] = ';';
                memcpy(buf + n, names + start, i - start);
                n += i - start;
            }
            idx++;
            start = i + 1;
        }
    }
    buf[n++] = 0;
    return emit_prop(rb, name, raw, buf, n);
}

/* dfp-endpoints: NUL-terminated names one after the other */
static int emit_list_without(struct rebuild *rb, const char *name, u32 raw, const u8 *list,
                             u32 len, const char *drop, int *dropped)
{
    u8 buf[512];
    u32 n = 0, pos = 0;

    if (len > sizeof(buf))
        return -1;
    while (pos < len) {
        const u8 *nul = memchr(list + pos, 0, len - pos);
        u32 l = nul ? (u32)(nul - (list + pos)) + 1 : len - pos;

        if (nul && l > 1 && streq((const char *)list + pos, drop)) {
            *dropped = 1;
        } else {
            memcpy(buf + n, list + pos, l);
            n += l;
        }
        pos += l;
    }
    return emit_prop(rb, name, raw, buf, n);
}

static int removed_path(const struct pdsf_adt_rules *r, const char *path)
{
    for (u32 i = 0; i < r->nremove; i++)
        if (streq(r->remove[i], path))
            return 1;
    return 0;
}

static int starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

/*
 * Copy the node at *off (path: its full path, "" for the root) and its children.
 * depth: 0 for the root.
 */
static int copy_node(struct rebuild *rb, u64 *off, const char *path, int depth)
{
    struct pdsf_adt_rules *r = rb->rules;
    const u8 *src = rb->src;
    u64 pos = *off, head = rb->out;
    u32 nprops, nchildren, out_props = 0, out_children = 0;
    u8 zero[8] = {0};
    int oslog = -1, under_armio, crossbar;
    u32 len = 0;
    u64 taken = 0; /* bit i: set[i] replaced a property here */

    if (depth > MAX_DEPTH || pos > rb->srclen || rb->srclen - pos < 8 || r->nset > 64)
        return -1;
    nprops = rd32(src + pos);
    nchildren = rd32(src + pos + 4);
    if (emit(rb, zero, 8))
        return -1;

    under_armio = starts_with(path, "/arm-io/") && (depth == 2 || depth == 3);
    crossbar = depth == 2 && starts_with(path, "/arm-io/display-crossbar");
    if (r->strip_oslog && under_armio) {
        const u8 *names = node_prop(src, rb->srclen, pos, "segment-names", &len);
        u32 rlen = 0;
        const u8 *ranges = node_prop(src, rb->srclen, pos, "segment-ranges", &rlen);

        if (names) {
            oslog = oslog_index(names, len);
            if (oslog >= 0 && (!ranges || (u64)(oslog + 1) * OSLOG_ENTRY > rlen))
                return -2;
            if (oslog >= 0)
                r->oslog_stripped++;
        }
    }

    pos += 8;
    for (u32 i = 0; i < nprops; i++) {
        const char *name;
        const u8 *value;
        u32 size;
        int done = 0;

        if (prop_at(src, rb->srclen, pos, &name, &size, &value, &pos))
            return -1;
        len = ADT_LEN(size);
        for (u32 s = 0; s < r->nset && !done; s++) {
            if (streq(r->set[s].node, path) && streq(r->set[s].prop, name)) {
                if (emit_prop(rb, name, 0, r->set[s].value, r->set[s].len))
                    return -3;
                r->set[s].used++;
                taken |= 1ull << s;
                done = 1;
            }
        }
        if (!done && oslog >= 0 && streq(name, "segment-names")) {
            if (emit_names_without(rb, name, size, value, len, oslog))
                return -3;
            done = 1;
        }
        if (!done && oslog >= 0 && streq(name, "segment-ranges")) {
            u8 buf[1024];
            u32 cut = (u32)oslog * OSLOG_ENTRY;

            if (len > sizeof(buf))
                return -2;
            memcpy(buf, value, cut);
            memcpy(buf + cut, value + cut + OSLOG_ENTRY, len - cut - OSLOG_ENTRY);
            if (emit_prop(rb, name, size, buf, len - OSLOG_ENTRY))
                return -3;
            done = 1;
        }
        if (!done && crossbar && r->dfp_remove && streq(name, "dfp-endpoints")) {
            int dropped = 0;

            if (emit_list_without(rb, name, size, value, len, r->dfp_remove, &dropped))
                return -3;
            r->dfp_edited += (u32)dropped;
            done = 1;
        }
        if (!done && emit(rb, src + (pos - ADT_NAME - 4 - ADT_PAD(len)),
                          ADT_NAME + 4 + ADT_PAD(len)))
            return -3;
        out_props++;
    }
    for (u32 s = 0; s < r->nset; s++) {
        if ((taken & (1ull << s)) || r->set[s].only_existing || !streq(r->set[s].node, path))
            continue;
        if (emit_prop(rb, r->set[s].prop, 0, r->set[s].value, r->set[s].len))
            return -3;
        r->set[s].used++;
        out_props++;
    }

    for (u32 i = 0; i < nchildren; i++) {
        char child[MAX_PATH];
        const u8 *name;
        u32 nlen = 0;
        u64 plen = strlen(path);
        int drop;

        if (pos > rb->srclen || rb->srclen - pos < 8)
            return -1;
        name = node_prop(src, rb->srclen, pos, "name", &nlen);
        if (!name || !nlen || !memchr(name, 0, nlen) || plen + 1 + nlen > sizeof(child))
            return -4;
        memcpy(child, path, plen);
        child[plen] = '/';
        memcpy(child + plen + 1, name, nlen);

        drop = removed_path(r, child);
        if (!drop && r->prune_cpus && streq(path, "/cpus")) {
            u32 slen = 0;
            const u8 *state = node_prop(src, rb->srclen, pos, "state", &slen);

            drop = !(state && slen >= 8 && !memcmp(state, "running", 8));
        }
        if (drop) {
            pos = node_end(src, rb->srclen, pos, depth + 1);
            if (!pos)
                return -1;
            r->removed++;
            continue;
        }
        if (copy_node(rb, &pos, child, depth + 1))
            return -5;
        out_children++;
    }
    memcpy(rb->dst + head, &out_props, 4);
    memcpy(rb->dst + head + 4, &out_children, 4);
    *off = pos;
    return 0;
}

long pdsf_adt_rebuild(const u8 *src, u64 srclen, u8 *dst, u64 cap, struct pdsf_adt_rules *rules)
{
    struct rebuild rb = {src, srclen, dst, cap, 0, rules};
    u64 off = 0;
    int ret;

    rules->removed = rules->oslog_stripped = rules->dfp_edited = 0;
    for (u32 s = 0; s < rules->nset; s++)
        rules->set[s].used = 0;
    if (pdsf_adt_size(src, srclen) < 0)
        return -1;
    ret = copy_node(&rb, &off, "", 0);
    if (ret)
        return ret < 0 ? ret : -1;
    return (long)rb.out;
}
