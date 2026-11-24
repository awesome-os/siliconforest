/* Host checks. Assertions stay enabled. */
#ifdef NDEBUG
#error M1TP_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN
#endif
#define _DEFAULT_SOURCE
#include "m1tp_enum.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const uint8_t KEYS[5][2] = {
    {0x01, 0xd0}, {0x02, 0x01}, {0x10, 0x01}, {0x02, 0x02}, {0x10, 0x02}
};
static const uint8_t BITS[5] = {
    M1TP_BIT_INFO, M1TP_BIT_IFACE1, M1TP_BIT_DESC1, M1TP_BIT_IFACE2, M1TP_BIT_DESC2
};

static int known_key(unsigned report, unsigned subdevice)
{
    int i;
    for (i = 0; i < 5; i++) {
        if (KEYS[i][0] == report && KEYS[i][1] == subdevice) return 1;
    }
    return 0;
}

static void check_order(const uint8_t *order)
{
    uint8_t used = 0;
    uint8_t expect = 0;
    int i;
    for (i = 0; i < 5; i++) {
        uint8_t slot = order[i];
        assert(m1tp_claim_request(&used, KEYS[slot][0], KEYS[slot][1]) == 1);
        expect = (uint8_t)(expect | BITS[slot]);
        assert(used == expect);
        assert(m1tp_claim_request(&used, KEYS[slot][0], KEYS[slot][1]) == 0);
        assert(used == expect);
    }
    assert(used == M1TP_CLAIM_MASK);
}

static void permute(uint8_t *idx, int n, int *seen)
{
    int i;
    if (n == 5) {
        check_order(idx);
        (*seen)++;
        return;
    }
    for (i = n; i < 5; i++) {
        uint8_t tmp = idx[n];
        idx[n] = idx[i];
        idx[i] = tmp;
        permute(idx, n + 1, seen);
        tmp = idx[n];
        idx[n] = idx[i];
        idx[i] = tmp;
    }
}

static void test_claim(void)
{
    uint8_t idx[5] = {0, 1, 2, 3, 4};
    uint8_t used;
    unsigned report, sub;
    unsigned mask;
    int seen = 0;

    permute(idx, 0, &seen);
    assert(seen == 120);
    assert(m1tp_claim_request(NULL, 0x01, 0xd0) == 0);

    for (mask = 0; mask < 256u; mask++) {
        int outside = (mask & ~31u) != 0;
        for (report = 0; report < 256u; report++) {
            for (sub = 0; sub < 256u; sub++) {
                if (!outside && known_key(report, sub)) continue;
                used = (uint8_t)mask;
                assert(m1tp_claim_request(&used, (uint8_t)report, (uint8_t)sub) == 0);
                assert(used == (uint8_t)mask);
            }
        }
    }
}

static void put_u16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void fill_header(uint8_t *p, unsigned iface, unsigned country,
                        unsigned input, unsigned output, unsigned control)
{
    memset(p, 0xa5, 16);
    p[1] = (uint8_t)iface;
    p[5] = (uint8_t)country;
    put_u16(p + 6, input);
    put_u16(p + 8, output);
    put_u16(p + 10, control);
    put_u16(p + 12, 16);
    put_u16(p + 14, 8);
}

static void poison_info(struct m1tp_interface_info *info)
{
    memset(info, 0x5a, sizeof *info);
}

static void test_interface(void)
{
    uint8_t header[32];
    struct m1tp_interface_info info;
    struct m1tp_interface_info saved;
    unsigned countries[3] = {0u, 33u, 255u};
    unsigned outputs[3] = {0u, 4086u, 65535u};
    unsigned controls[3] = {4086u, 0u, 65535u};
    unsigned i;

    for (i = 0; i < 3u; i++) {
        fill_header(header, 2u, countries[i], i == 0 ? 1u : 2038u, outputs[i], controls[i]);
        poison_info(&info);
        assert(m1tp_interface_info(header, 16u, &info) == M1TP_OK);
        assert(info.interface == 2u);
        assert(info.country == (uint8_t)countries[i]);
        assert(info.max_input == (i == 0 ? 1u : 2038u));
        assert(info.max_output == (uint16_t)outputs[i]);
        assert(info.max_control == (uint16_t)controls[i]);
        poison_info(&info);
        assert(m1tp_interface_info(header, 2038u, &info) == M1TP_OK);
        assert(info.max_input == (i == 0 ? 1u : 2038u));
        assert(info.country == (uint8_t)countries[i]);
    }

    fill_header(header, 2u, 33u, 0x1234u, 0u, 4086u);
    poison_info(&info);
    memcpy(&saved, &info, sizeof info);
    assert(m1tp_interface_info(NULL, 16u, &info) == M1TP_ERR_ARGUMENT);
    assert(memcmp(&info, &saved, sizeof info) == 0);
    assert(m1tp_interface_info(header, 16u, NULL) == M1TP_ERR_ARGUMENT);
    assert(m1tp_interface_info(header, 15u, &info) == M1TP_ERR_LENGTH);
    assert(m1tp_interface_info(header, 2039u, &info) == M1TP_ERR_LENGTH);
    assert(m1tp_interface_info(header, 0u, &info) == M1TP_ERR_LENGTH);
    assert(memcmp(&info, &saved, sizeof info) == 0);
    fill_header(header, 1u, 33u, 0u, 0u, 0u);
    assert(m1tp_interface_info(header, 15u, &info) == M1TP_ERR_LENGTH);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INTERFACE);
    fill_header(header, 0u, 33u, 10u, 0u, 0u);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INTERFACE);
    fill_header(header, 3u, 33u, 10u, 0u, 0u);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INTERFACE);
    fill_header(header, 0xd0u, 33u, 10u, 0u, 0u);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INTERFACE);
    fill_header(header, 2u, 33u, 0u, 1u, 1u);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INPUT);
    fill_header(header, 2u, 33u, 2039u, 1u, 1u);
    assert(m1tp_interface_info(header, 16u, &info) == M1TP_ERR_INPUT);
    assert(memcmp(&info, &saved, sizeof info) == 0);
}

static void test_strings_unread(void)
{
    long page = sysconf(_SC_PAGESIZE);
    uint8_t *map;
    uint8_t *header;
    struct m1tp_interface_info info;

    assert(page >= 16);
    map = mmap(NULL, (size_t)page * 2u, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(map != MAP_FAILED);
    assert(mprotect(map + page, (size_t)page, PROT_NONE) == 0);
    header = map + page - 16;
    fill_header(header, 2u, 33u, 1u, 0u, 4086u);
    poison_info(&info);
    assert(m1tp_interface_info(header, 2038u, &info) == M1TP_OK);
    assert(info.interface == 2u);
    assert(info.country == 33u);
    assert(info.max_input == 1u);
    assert(info.max_output == 0u);
    assert(info.max_control == 4086u);
    assert(munmap(map, (size_t)page * 2u) == 0);
}

static void test_descriptor(void)
{
    uint8_t src[514];
    uint8_t dst[600];
    uint8_t saved[600];
    uint32_t length;
    uint32_t i;

    for (i = 0; i < 514u; i++) src[i] = (uint8_t)(i * 3u + 1u);
    memset(dst, 0xa5, sizeof dst);
    memcpy(saved, dst, sizeof dst);
    length = 0xa5a5a5a5u;
    assert(m1tp_copy_descriptor(NULL, 512u, &length, src, 4u) == M1TP_ERR_ARGUMENT);
    assert(length == 0xa5a5a5a5u);
    assert(m1tp_copy_descriptor(dst, 512u, NULL, src, 4u) == M1TP_ERR_ARGUMENT);
    assert(m1tp_copy_descriptor(dst, 512u, &length, NULL, 4u) == M1TP_ERR_ARGUMENT);
    assert(memcmp(dst, saved, sizeof dst) == 0);
    assert(m1tp_copy_descriptor(dst, 512u, &length, src, 0u) == M1TP_ERR_LENGTH);
    assert(m1tp_copy_descriptor(dst, 600u, &length, src, 513u) == M1TP_ERR_LENGTH);
    assert(m1tp_copy_descriptor(dst, 511u, &length, src, 512u) == M1TP_ERR_LENGTH);
    assert(length == 0xa5a5a5a5u);
    assert(memcmp(dst, saved, sizeof dst) == 0);

    assert(m1tp_copy_descriptor(dst, 512u, &length, src, 4u) == M1TP_OK);
    assert(length == 4u);
    assert(memcmp(dst, src, 4) == 0);
    assert(dst[4] == 0xa5);
    memset(dst, 0xa5, sizeof dst);
    length = 0;
    assert(m1tp_copy_descriptor(dst, 512u, &length, src, 512u) == M1TP_OK);
    assert(length == 512u);
    assert(memcmp(dst, src, 512) == 0);
    assert(dst[512] == 0xa5);

    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *map = mmap(NULL, (size_t)page * 2u, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uint8_t one[8];
        uint8_t *byte;
        memset(one, 0xa5, sizeof one);
        length = 7u;
        assert(map != MAP_FAILED);
        assert(mprotect(map + page, (size_t)page, PROT_NONE) == 0);
        byte = map + page - 1;
        *byte = 0x5c;
        assert(m1tp_copy_descriptor(one, sizeof one, &length, byte, 1u) == M1TP_OK);
        assert(length == 1u);
        assert(one[0] == 0x5c);
        assert(one[1] == 0xa5);
        assert(munmap(map, (size_t)page * 2u) == 0);
    }
}

int main(void)
{
    test_claim();
    test_interface();
    test_strings_unread();
    test_descriptor();
    puts("m1tp enum host checks passed");
    return 0;
}
