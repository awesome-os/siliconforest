#include "bcm4378_identity_fw.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_bcm4378_identity_fw requires assertions"
#endif

/* Explicit SYNTHETIC field profile, not numerically verified by chip.c's
 * excluded headers and never an admitted hardware identity format. */
static const struct bci_profile PROFILE = {0x18000000, 0xffff, 0xf0000, 16, 0xf0000000, 28, 0x4378, 1};
static struct {
    uint32_t run, link, pci, page, raw;
    unsigned reads, writes, fail_at, requests, found_variant, request_error, drop_after_request;
    char names[8][BCI_NAME_BYTES];
} F;

static int fake_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx;
    F.reads++;
    if (F.fail_at == F.reads) { *out = UINT32_MAX; return -1; }
#ifdef BCI_FAKE_DEVICE
    if (r == APB_PORT0 && off == 0x804) *out = F.run;
    else if (r == APB_PORT0 && off == 0x208) *out = F.link;
    else if (r == 9 && off == 0) *out = F.pci;
    else if (r == 9 && off == 0x80) *out = F.page;
    else { assert(r == 10 && off == 0); *out = F.raw; }
#else
    (void)r; (void)off; (void)out;
    assert(!"default/kernel must not read a device");
#endif
    return 0;
}
static int fake_write(void *ctx, unsigned r, uint32_t off, uint32_t value)
{
    (void)ctx; (void)r; (void)off; (void)value;
    F.writes++;
    assert(!"identity/firmware requests must never write device registers");
    return -1;
}
static int fake_request(void *ctx, const char *name, unsigned item, int required)
{
    (void)ctx;
    assert(F.requests < 8 && item < 5 && required == (item == 0));
    assert(strlen(name) < BCI_NAME_BYTES && !strchr(name, '/') && !strchr(name, '\\'));
    strcpy(F.names[F.requests], name);
    unsigned variant = F.requests++;
    if (F.requests == F.drop_after_request) F.link = 0;
    if (F.request_error) return -1;
    return variant == F.found_variant ? 0 : 1;
}
static void reset(unsigned revision)
{
    memset(&F, 0, sizeof F);
    F.run = F.link = 1; F.pci = 0x442514e4; F.page = PROFILE.chipcommon_page;
    F.raw = 0x10004378u | (revision << 16);
}

static void pure_policy(void)
{
    struct bci_identity id = {0};
    id.version = id.valid = 1; id.pci_identity = BCI_PCI_VENDOR_DEVICE;
    id.bus_type = 1; id.chip_id = 0x4378;
    struct bci_fw_plan plan = {0}, saved;
    char name[BCI_NAME_BYTES];
    const char *suffixes[5] = {".bin", ".txt", ".clm_blob", ".txcap_blob", ".sig"};
    for (unsigned rev = 0; rev <= 32; ++rev) {
        id.revision = rev; saved = plan;
        int e = bci_build_fw_plan(&id, NULL, 0, &plan);
        if (rev == 4 || rev >= 32) { assert(e == BCI_UNSUPPORTED && !memcmp(&plan, &saved, sizeof plan)); continue; }
        assert(e == BCI_FAKE_OK);
        for (unsigned item = 0; item < 5; ++item) {
            assert(bci_fw_name(&plan, item, 0, name, sizeof name) == BCI_FAKE_OK);
            const char *stem = rev < 4 ? "brcmfmac4378b1-pcie" : "brcmfmac4378b3-pcie";
            assert(!strncmp(name, stem, strlen(stem)) && !strcmp(name+strlen(stem), suffixes[item]));
        }
    }
    id.revision = 3;
    const char *variants[2] = {"apple,j293-EXPLICIT-TEST", "apple,j293"};
    assert(bci_build_fw_plan(&id, variants, 2, &plan) == BCI_FAKE_OK);
    assert(bci_fw_name(&plan, BCI_EXECUTABLE, 0, name, sizeof name) == BCI_FAKE_OK);
    assert(!strcmp(name, "brcmfmac4378b1-pcie.apple,j293-EXPLICIT-TEST.bin"));
    assert(bci_fw_name(&plan, BCI_BOARD_NVRAM, 1, name, sizeof name) == BCI_FAKE_OK);
    assert(!strcmp(name, "brcmfmac4378b1-pcie.apple,j293.txt"));
    assert(bci_fw_name(&plan, BCI_REGULATORY, 2, name, sizeof name) == BCI_FAKE_OK);
    assert(!strcmp(name, "brcmfmac4378b1-pcie.clm_blob"));
    char untouched[BCI_NAME_BYTES]; memset(untouched, 0x5a, sizeof untouched); memcpy(name, untouched, sizeof name);
    assert(bci_fw_name(&plan, 0, 0, name, 1) == BCI_RANGE && !memcmp(name, untouched, sizeof name));
    assert(bci_fw_name(&plan, 0, 0, name, (uint32_t)strlen("brcmfmac4378b1-pcie.apple,j293-EXPLICIT-TEST.bin")) == BCI_RANGE);
    assert(!memcmp(name, untouched, sizeof name));
    assert(bci_fw_name(&plan, 5, 0, name, sizeof name) == BCI_ARGUMENT);
    assert(bci_fw_name(&plan, 0, 3, name, sizeof name) == BCI_ARGUMENT);
    const char *bad[] = {"", "../escape", "/absolute", "a\\b", "two words", ".hidden", "a..b", "a:b"};
    saved = plan;
    for (unsigned i = 0; i < sizeof bad/sizeof bad[0]; ++i) {
        assert(bci_build_fw_plan(&id, &bad[i], 1, &plan) == BCI_RANGE);
        assert(!memcmp(&plan, &saved, sizeof plan));
    }
    const char *duplicate[2] = {"same", "same"};
    assert(bci_build_fw_plan(&id, duplicate, 2, &plan) == BCI_RANGE);
    id.valid = 0; assert(bci_build_fw_plan(&id, NULL, 0, &plan) == BCI_ARGUMENT);
}

#ifdef BCI_FAKE_DEVICE
#include "register_accessor.h"
static struct apr_accessor A;
static struct apb_io IO;
static void setup(unsigned revision, struct bci_identity *id)
{
    reset(revision);
    struct apb_io raw = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
    assert(bci_identify_fake(&IO, &PROFILE, id) == BCI_FAKE_OK);
    assert(id->valid && id->chip_id == 0x4378 && id->revision == revision && id->bus_type == 1);
    assert(id->numeric_profile_verified == 0 && F.writes == 0);
}
static void identity_faults_and_profiles(void)
{
    struct bci_identity id;
    setup(3, &id); unsigned count = F.reads;
    for (unsigned fail = 1; fail <= count; ++fail) {
        reset(3); F.fail_at = fail;
        assert(bci_identify_fake(&IO, &PROFILE, &id) == BCI_IO && !id.valid && F.reads == fail && !F.writes);
    }
    setup(3, &id); F.pci = 0x5f6914e4;
    assert(bci_identify_fake(&IO, &PROFILE, &id) == BCI_IDENTITY && !id.valid);
    setup(3, &id); F.raw ^= 1;
    assert(bci_identify_fake(&IO, &PROFILE, &id) == BCI_IDENTITY && !id.valid);
    setup(3, &id); F.raw = 0x20034378;
    assert(bci_identify_fake(&IO, &PROFILE, &id) == BCI_IDENTITY && !id.valid);
    setup(3, &id); F.page ^= 0x1000;
    assert(bci_identify_fake(&IO, &PROFILE, &id) == BCI_IDENTITY && !id.valid);
    struct bci_profile p = PROFILE;
    p.revision_mask |= 1;
    assert(bci_identify_fake(&IO, &p, &id) == BCI_ARGUMENT);
    p = PROFILE; p.revision_shift = 32;
    assert(bci_identify_fake(&IO, &p, &id) == BCI_ARGUMENT);
    p = PROFILE; p.type_mask = 0xa0000000;
    assert(bci_identify_fake(&IO, &p, &id) == BCI_ARGUMENT);
}
static void requests_and_down_link(void)
{
    struct bci_identity id;
    struct bci_fw_plan p;
    struct bci_request_record record;
    const char *q[2] = {"explicit-test-board", "fallback-test-board"};
    for (unsigned item = 0; item < 5; ++item) {
        setup(5, &id); assert(bci_build_fw_plan(&id, q, 2, &p) == BCI_FAKE_OK);
        for (unsigned variant = 0; variant < 4; ++variant) {
            F.requests = 0; F.found_variant = variant;
            int e = bci_request_fw_item(&IO, &p, item, fake_request, NULL, &record);
            assert(e == (variant < 3 ? BCI_FAKE_OK : item == 0 ? BCI_MISSING : BCI_OPTIONAL_MISSING));
            assert(F.requests == (variant < 3 ? variant+1 : 3));
            assert(record.attempts == F.requests && record.found == (variant < 3));
            if (F.requests == 3) assert(strstr(F.names[2], "test-board") == NULL);
        }
        for (unsigned down = 0; down < 4; ++down) {
            F.run = 1; F.link = 1; F.requests = 0;
            if (down == 0) F.run = 0;
            else F.link = down == 1 ? 0 : down == 2 ? 5 : 0x41;
            assert(bci_request_fw_item(&IO, &p, item, fake_request, NULL, &record) == BCI_LINK_DOWN);
            assert(F.requests == 0 && record.attempts == 0 && F.writes == 0);
        }
    }
    setup(5, &id); assert(bci_build_fw_plan(&id, q, 2, &p) == BCI_FAKE_OK);
    F.found_variant = 2; F.drop_after_request = 1;
    assert(bci_request_fw_item(&IO, &p, 0, fake_request, NULL, &record) == BCI_LINK_DOWN && F.requests == 1 && F.writes == 0);
    setup(5, &id); F.request_error = 1;
    assert(bci_request_fw_item(&IO, &p, 0, fake_request, NULL, &record) == BCI_IO && F.requests == 1);
    setup(5, &id); F.pci = UINT32_MAX;
    assert(bci_request_fw_item(&IO, &p, 0, fake_request, NULL, &record) == BCI_IDENTITY && F.requests == 0);
    setup(5, &id);
    for (unsigned fail = 1; fail <= 3; ++fail) {
        F.reads = 0; F.fail_at = fail; F.requests = 0;
        assert(bci_request_fw_item(&IO, &p, 0, fake_request, NULL, &record) == BCI_IO && F.requests == 0);
    }
}
#else
static void default_zero_io(void)
{
    struct apb_io io = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    struct bci_identity id;
    struct bci_request_record record;
    reset(3);
    assert(bci_identify_fake(&io, &PROFILE, &id) == BCI_GATED && !id.valid);
    for (unsigned item = 0; item < 5; ++item)
        assert(bci_request_fw_item(&io, NULL, item, fake_request, NULL, &record) == BCI_GATED && record.attempts == 0);
    assert(!F.reads && !F.writes && !F.requests);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
    pure_policy();
#ifdef BCI_FAKE_DEVICE
    identity_faults_and_profiles(); requests_and_down_link();
#else
    default_zero_io();
#endif
    puts("HOST ONLY PASS: synthetic chip profile, exact firmware request names, down-link refusal, zero writes");
    return 0;
}
