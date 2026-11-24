/* SPDX-License-Identifier: MIT */
/* Host driver for src/pdsf.c (built with -DPDSF_HOST by test_b1.py). No hardware. */
#include <stdio.h>
#include <stdlib.h>

#include "pdsf.h"

static u8 *slurp(const char *path, u64 *len)
{
    FILE *f = fopen(path, "rb");
    u8 *buf;
    long n;

    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
        exit(3);
    buf = malloc((size_t)n + 64);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(3);
    memset(buf + n, 0, 64);
    fclose(f);
    *len = (u64)n;
    return buf;
}

static void spit(const char *path, const u8 *data, u64 len)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(data, 1, len, f) != len || fclose(f))
        exit(3);
}

static u8 *unhex(const char *s, u32 *len)
{
    u32 n = (u32)strlen(s) / 2;
    u8 *out = malloc(n + 1);

    for (u32 i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (u8)v;
    }
    *len = n;
    return out;
}

int main(int argc, char **argv)
{
    u64 len;
    const char *err = "";

    if (argc >= 4 && !strcmp(argv[1], "scan")) {
        u8 *data = slurp(argv[2], &len);
        u64 off = strtoull(argv[3], NULL, 0);
        struct pdsf_set set;
        int ret = pdsf_scan(data + off, len - off, &set);

        printf("ret %d\n", ret);
        if (ret)
            return 0;
        for (int t = 1; t < PDSF_TYPES; t++)
            if (set.item[t].present)
                printf("%d %u %llu %llu %u %llu\n", t, set.item[t].flags,
                       (unsigned long long)set.item[t].stored, (unsigned long long)set.item[t].raw,
                       set.item[t].crc32, (unsigned long long)(set.item[t].data - data));
        printf("end %llu\n", (unsigned long long)(set.end - data));
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "config")) {
        u8 *data = slurp(argv[2], &len);
        static struct pdsf_config cfg;
        int ret = pdsf_config_parse((const char *)data, len, &cfg, &err);

        printf("ret %d %s\n", ret, err);
        if (ret)
            return 0;
        printf("bootargs %s\ndisplay %u\nusb %d\nsprr %u\namx %u\nwdt %u\nreserve %u\nserial %u\n"
               "seed %d %02x%02x\ncpus %d\nheld %d\nbase %llx\n",
               cfg.bootargs, cfg.display, cfg.usb_remove, cfg.sprr, cfg.amx, cfg.wdt,
               cfg.reserve_mb, cfg.serial, cfg.have_seed, cfg.seed[0], cfg.seed[255],
               pdsf_cmdline_cpus(cfg.bootargs), pdsf_cmdline_has(cfg.bootargs, "sfcpuheld=1"),
               (unsigned long long)cfg.xnu_base);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "root")) {
        /* root <have ramdisk 0|1> <built for> <command line> */
        const char *why = "";
        int kind = 0, rc = pdsf_root_check(argv[4], argv[2][0] == '1', argv[3], &kind, &why);

        printf("rc=%d kind=%d %s\n", rc, kind, why);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "macho")) {
        u8 *kc = slurp(argv[2], &len);
        struct pdsf_macho m;
        int ret = pdsf_macho_info(kc, len, &m, &err);

        printf("ret %d %s\n", ret, err);
        if (ret)
            return 0;
        printf("%llu %llu %llu %u\n", (unsigned long long)m.vmin, (unsigned long long)m.vmax,
               (unsigned long long)m.entry, m.segments);
        if (argc >= 4) {
            u8 *dest = malloc(m.vmax - m.vmin);

            memset(dest, 0xa5, m.vmax - m.vmin);
            pdsf_macho_load(kc, len, &m, dest);
            spit(argv[3], dest, m.vmax - m.vmin);
        }
        return 0;
    }
    if (argc == 13 && !strcmp(argv[1], "layout")) {
        struct pdsf_layout_in in;
        struct pdsf_layout l;
        u64 *field = (u64 *)&in;

        for (int i = 0; i < 11; i++)
            field[i] = strtoull(argv[2 + i], NULL, 0);
        int ret = pdsf_layout(&in, &l);

        printf("ret %d\n", ret);
        field = (u64 *)&l;
        for (unsigned i = 0; !ret && i < sizeof(l) / 8; i++)
            printf("%llu\n", (unsigned long long)field[i]);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "adt")) {
        /* adt <in> <out> <capacity> [prune] [oslog] [dfp <name>] [remove <path>]...
         *     [set|setx <node> <prop> <hex>]... */
        u8 *src = slurp(argv[2], &len);
        u64 cap = strtoull(argv[4], NULL, 0);
        u8 *dst = malloc(cap + 16);
        static struct pdsf_adt_set sets[32];
        static const char *remove[64];
        struct pdsf_adt_rules rules;

        memset(&rules, 0, sizeof(rules));
        rules.set = sets;
        rules.remove = remove;
        for (int i = 5; i < argc; i++) {
            if (!strcmp(argv[i], "prune"))
                rules.prune_cpus = 1;
            else if (!strcmp(argv[i], "oslog"))
                rules.strip_oslog = 1;
            else if (!strcmp(argv[i], "dfp") && i + 1 < argc)
                rules.dfp_remove = argv[++i];
            else if (!strcmp(argv[i], "remove") && i + 1 < argc)
                remove[rules.nremove++] = argv[++i];
            else if ((!strcmp(argv[i], "set") || !strcmp(argv[i], "setx")) && i + 3 < argc) {
                struct pdsf_adt_set *s = &sets[rules.nset++];

                s->only_existing = argv[i][3] == 'x';
                s->node = argv[i + 1];
                s->prop = argv[i + 2];
                s->value = unhex(argv[i + 3], &s->len);
                i += 3;
            } else
                return 2;
        }
        memset(dst, 0xa5, cap + 16);
        long n = pdsf_adt_rebuild(src, len, dst, cap, &rules);

        printf("ret %ld size %ld removed %u oslog %u dfp %u\n", n, pdsf_adt_size(src, len),
               rules.removed, rules.oslog_stripped, rules.dfp_edited);
        for (u32 i = 0; i < rules.nset; i++)
            printf("used %d\n", sets[i].used);
        for (int i = 0; i < 16; i++)
            if (dst[cap + i] != 0xa5)
                return 4; /* wrote past its capacity */
        if (n > 0)
            spit(argv[3], dst, (u64)n);
        return 0;
    }
    return 2;
}
