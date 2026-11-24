/* Original finite request planning/identity model; reference is data only. */
#include "bcm4378_identity_fw.h"
#include <string.h>

#if defined(KERNEL) && defined(BCI_FAKE_DEVICE)
#error "BCI_FAKE_DEVICE is host-test only; no kernel device/firmware path"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "identity/firmware slice requires shared PHY and PMGR writes compiled out"
#endif

static int revision_ok(uint32_t revision) { return revision < 32 && revision != 4; }

static int qualifier_length(const char *p, uint32_t *out)
{
    uint32_t n;
    if (!p) return 0;
    for (n = 0; n < BCI_QUALIFIER_BYTES && p[n]; ++n) {
        unsigned char c = (unsigned char)p[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == ',' || c == '-' || c == '_' || c == '.')) return 0;
        if (c == '.' && (n == 0 || p[n-1] == '.')) return 0;
    }
    if (!n || n == BCI_QUALIFIER_BYTES) return 0;
    *out = n;
    return 1;
}

static int plan_ok(const struct bci_fw_plan *p)
{
    uint32_t length;
    if (!p || p->version != 1 || p->valid != 1 || !revision_ok(p->revision) || p->qualifier_count > BCI_MAX_QUALIFIERS)
        return 0;
    for (uint32_t i = 0; i < p->qualifier_count; ++i) {
        if (!qualifier_length(p->qualifiers[i], &length)) return 0;
        for (uint32_t j = 0; j < i; ++j) if (!strcmp(p->qualifiers[i], p->qualifiers[j])) return 0;
    }
    return 1;
}

int bci_build_fw_plan(const struct bci_identity *identity, const char *const *qualifiers,
                      uint32_t count, struct bci_fw_plan *out)
{
    struct bci_fw_plan next = {0};
    if (!identity || !out || identity->valid != 1 || identity->version != 1 ||
        identity->result != BCI_FAKE_OK || identity->pci_identity != BCI_PCI_VENDOR_DEVICE ||
        identity->bus_type != 1 || count > BCI_MAX_QUALIFIERS || (count && !qualifiers)) return BCI_ARGUMENT;
    if (!revision_ok(identity->revision)) return BCI_UNSUPPORTED;
    next.version = next.valid = 1; next.revision = identity->revision; next.qualifier_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t n;
        if (!qualifier_length(qualifiers[i], &n)) return BCI_RANGE;
        memcpy(next.qualifiers[i], qualifiers[i], n + 1);
    }
    if (!plan_ok(&next)) return BCI_RANGE;
    *out = next;
    return BCI_FAKE_OK;
}

int bci_fw_name(const struct bci_fw_plan *p, unsigned item, unsigned variant, char *out, uint32_t capacity)
{
    const char *stem, *extension;
    char name[BCI_NAME_BYTES];
    size_t n = 0, bytes;
    if (!out || !plan_ok(p) || item >= BCI_ITEM_COUNT || variant > p->qualifier_count) return BCI_ARGUMENT;
    stem = p->revision < 4 ? "brcmfmac4378b1-pcie" : "brcmfmac4378b3-pcie";
    switch (item) {
    case BCI_EXECUTABLE: extension = ".bin"; break;
    case BCI_BOARD_NVRAM: extension = ".txt"; break;
    case BCI_REGULATORY: extension = ".clm_blob"; break;
    case BCI_TX_CAPABILITY: extension = ".txcap_blob"; break;
    default: extension = ".sig"; break;
    }
    bytes = strlen(stem); memcpy(name, stem, bytes); n += bytes;
    if (variant < p->qualifier_count) {
        name[n++] = '.';
        bytes = strlen(p->qualifiers[variant]); memcpy(name+n, p->qualifiers[variant], bytes); n += bytes;
    }
    bytes = strlen(extension);
    if (n + bytes >= sizeof name || n + bytes >= capacity) return BCI_RANGE;
    memcpy(name+n, extension, bytes + 1);
    memcpy(out, name, n + bytes + 1);
    return BCI_FAKE_OK;
}

#ifdef BCI_FAKE_DEVICE
#include "register_accessor.h"
static int link_and_device(const struct apb_io *io)
{
    uint32_t run = 0, link = 0, pci = 0;
    if (!apr_bcm_io_bound(io)) return BCI_ARGUMENT;
    if (io->read32(io->context, APB_PORT0, 0x804, &run)) return BCI_IO;
    if (!(run & 1)) return BCI_LINK_DOWN;
    if (io->read32(io->context, APB_PORT0, 0x208, &link)) return BCI_IO;
    if ((link & 0x45) != 1) return BCI_LINK_DOWN;
    if (io->read32(io->context, APR_BCM_CONFIG, 0, &pci)) return BCI_IO;
    return pci == BCI_PCI_VENDOR_DEVICE ? BCI_FAKE_OK : BCI_IDENTITY;
}

static int field_ok(uint32_t mask, uint32_t shift)
{
    if (!mask || shift >= 32 || (mask & ((UINT32_C(1) << shift) - 1u))) return 0;
    uint32_t field = mask >> shift;
    return !(field & (field + 1u));
}

int bci_identify_fake(const struct apb_io *io, const struct bci_profile *p, struct bci_identity *out)
{
    uint32_t page = 0, raw = 0;
    int e;
    if (!out) return BCI_ARGUMENT;
    *out = (struct bci_identity){0}; out->version = 1; out->result = BCI_ARGUMENT;
    if (!p || !p->chipcommon_page || (p->chipcommon_page & 0xfffu) ||
        !field_ok(p->id_mask, 0) || !field_ok(p->revision_mask, p->revision_shift) ||
        !field_ok(p->type_mask, p->type_shift) || (p->id_mask & p->revision_mask) ||
        (p->id_mask & p->type_mask) || (p->revision_mask & p->type_mask) ||
        !p->expected_id || (p->expected_id & ~p->id_mask) || p->expected_type != 1)
        return BCI_ARGUMENT;
    if ((e = link_and_device(io))) { out->result = e; return e; }
    if (io->read32(io->context, APR_BCM_CONFIG, 0x80, &page)) { out->result = BCI_IO; return BCI_IO; }
    if (page != p->chipcommon_page) { out->result = BCI_IDENTITY; return BCI_IDENTITY; }
    if ((e = link_and_device(io))) { out->result = e; return e; }
    if (io->read32(io->context, APR_BCM_BAR0, BCI_CHIPID_OFFSET, &raw)) { out->result = BCI_IO; return BCI_IO; }
    out->pci_identity = BCI_PCI_VENDOR_DEVICE; out->raw_chipid = raw;
    out->chip_id = raw & p->id_mask;
    out->revision = (raw & p->revision_mask) >> p->revision_shift;
    out->bus_type = (raw & p->type_mask) >> p->type_shift;
    if (raw == UINT32_MAX || out->chip_id != p->expected_id || out->bus_type != p->expected_type) {
        out->result = BCI_IDENTITY; return BCI_IDENTITY;
    }
    out->valid = 1; out->result = BCI_FAKE_OK;
    return BCI_FAKE_OK;
}

int bci_request_fw_item(const struct apb_io *io, const struct bci_fw_plan *p, unsigned item,
                        bci_fake_request_fn request, void *context, struct bci_request_record *out)
{
    int e;
    if (!out) return BCI_ARGUMENT;
    *out = (struct bci_request_record){0}; out->version = 1; out->result = BCI_ARGUMENT;
    if (!plan_ok(p) || item >= BCI_ITEM_COUNT || !request) return BCI_ARGUMENT;
    out->item = item; out->required = item == BCI_EXECUTABLE;
    for (unsigned variant = 0; variant <= p->qualifier_count; ++variant) {
        if ((e = link_and_device(io))) { out->result = e; return e; }
        if ((e = bci_fw_name(p, item, variant, out->name, sizeof out->name))) { out->result = e; return e; }
        out->attempts++;
        e = request(context, out->name, item, (int)out->required);
        if (!e) { out->found = 1; out->result = BCI_FAKE_OK; return BCI_FAKE_OK; }
        if (e != 1) { out->result = BCI_IO; return BCI_IO; }
    }
    out->optional_missing = !out->required;
    out->result = out->required ? BCI_MISSING : BCI_OPTIONAL_MISSING;
    return out->result;
}
#else
int bci_identify_fake(const struct apb_io *io, const struct bci_profile *p, struct bci_identity *out)
{
    (void)io; (void)p;
    if (!out) return BCI_ARGUMENT;
    *out = (struct bci_identity){0}; out->version = 1; out->result = BCI_GATED;
    return BCI_GATED;
}
int bci_request_fw_item(const struct apb_io *io, const struct bci_fw_plan *p, unsigned item,
                        bci_fake_request_fn request, void *context, struct bci_request_record *out)
{
    (void)io; (void)p; (void)item; (void)request; (void)context;
    if (!out) return BCI_ARGUMENT;
    *out = (struct bci_request_record){0}; out->version = 1; out->result = BCI_GATED;
    return BCI_GATED;
}
#endif
