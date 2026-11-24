/* Original fake-device identity/firmware-request policy. No firmware bytes,
 * kernel backend or upstream implementation is included. */
#ifndef DL_BCM4378_IDENTITY_FW_H
#define DL_BCM4378_IDENTITY_FW_H
#include "bringup.h"
#ifdef __cplusplus
extern "C" {
#endif

#define BCI_PCI_VENDOR_DEVICE UINT32_C(0x442514e4)
#define BCI_CHIPID_OFFSET 0u
#define BCI_MAX_QUALIFIERS 6u
#define BCI_QUALIFIER_BYTES 64u
#define BCI_NAME_BYTES 128u
#define BCI_ITEM_COUNT 5u

enum bci_result {
    BCI_FAKE_OK = 0, BCI_GATED = 1, BCI_LINK_DOWN = 2, BCI_OPTIONAL_MISSING = 3,
    BCI_ARGUMENT = -1, BCI_IO = -2, BCI_IDENTITY = -3, BCI_UNSUPPORTED = -4,
    BCI_RANGE = -5, BCI_MISSING = -6
};
enum bci_item { BCI_EXECUTABLE, BCI_BOARD_NVRAM, BCI_REGULATORY, BCI_TX_CAPABILITY, BCI_SIGNATURE };

/* chip.c's CHIPID register is at offset0 on the selected ChipCommon page.
 * CID_ID_MASK, CID_REV_MASK/SHIFT, CID_TYPE_MASK/SHIFT and SI_ENUM_BASE_DEFAULT
 * are defined in excluded headers, NOT numerically established by chip.c.
 * Thus these explicit inputs are a synthetic fake profile, never an admitted
 * hardware format. expected_type must be1 (AI), which chip.c does define.
 * All fields must be nonempty contiguous, nonoverlapping and in32 bits. */
struct bci_profile {
    uint32_t chipcommon_page, id_mask, revision_mask, revision_shift;
    uint32_t type_mask, type_shift, expected_id, expected_type;
};
struct bci_identity {
    uint32_t version, valid, pci_identity, raw_chipid, chip_id, revision, bus_type;
    uint32_t numeric_profile_verified; /* always0: supplied fake fields only */
    int32_t result;
};
struct bci_fw_plan {
    uint32_t version, valid, revision, qualifier_count;
    char qualifiers[BCI_MAX_QUALIFIERS][BCI_QUALIFIER_BYTES];
};
struct bci_request_record {
    uint32_t version, item, required, attempts, found, optional_missing;
    char name[BCI_NAME_BYTES];
    int32_t result;
};

/* Fake callback:0 = synthetic item present,1 = absent,other = I/O failure.
 * Bounded NUL-terminated leaf name; no path/directory, payload or loader API.
 * It must not open files/devices or download anything. Caller serializes and
 * owns the checked accessor, fake device, callback and all outputs. */
typedef int (*bci_fake_request_fn)(void *, const char *name, unsigned item, int required);

/* Reads RUN/LINKSTS, endpoint PCI ID, current backplane selector and BAR0+0
 * only. Writes nothing. Selector must already equal explicit profile page.
 * Default/KERNEL returns GATED without I/O. */
int bci_identify_fake(const struct apb_io *, const struct bci_profile *, struct bci_identity *);

/* Pure metadata operation. Firmware families follow the previous pinned
 * transport map: rev0..3 B1,5..31 B3;4 and>=32 unsupported. No model->board
 * qualifier guessed: caller supplies an ordered, bounded list or none.
 * Both pure functions preserve outputs on failure; no MMIO in any build. */
int bci_build_fw_plan(const struct bci_identity *, const char *const *qualifiers,
                      uint32_t count, struct bci_fw_plan *);
int bci_fw_name(const struct bci_fw_plan *, unsigned item, unsigned variant,
                 char *out, uint32_t capacity);

/* Every variant request rechecks current fake RUN/LINKSTS and PCI identity;
 * down link -> no callback and zero writes. Optional absence is not a valid
 * board calibration, firmware completion or permission to start a chip.
 * Default/KERNEL never invokes I/O or request callbacks and returns GATED. */
int bci_request_fw_item(const struct apb_io *, const struct bci_fw_plan *, unsigned item,
                        bci_fake_request_fn, void *, struct bci_request_record *);
#ifdef __cplusplus
}
#endif
#endif
