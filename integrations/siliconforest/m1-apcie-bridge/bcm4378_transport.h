/* Original software-only BCM4378 PCIe protocol slice, not a driver.
 * The three pinned ISC files are a facts map only; see BCM4378_TRANSPORT.md. */
#ifndef DL_BCM4378_TRANSPORT_H
#define DL_BCM4378_TRANSPORT_H
#include "bringup.h"
#ifdef __cplusplus
extern "C" {
#endif

#define BCT_RAM_BASE 0x352000u
#define BCT_MAX_RAM 0x400000u
enum bct_result {
    BCT_FAKE_OK = 0, BCT_GATED = 1, BCT_LINK_DOWN = 2, BCT_WAIT_FIRMWARE = 3,
    BCT_BUSY = 4, BCT_ARGUMENT = -1, BCT_ORDER = -2, BCT_IDENTITY = -3,
    BCT_IO = -4, BCT_RANGE = -5, BCT_UNSUPPORTED = -6, BCT_VERIFY = -7, BCT_FAULTED = -8
};
enum bct_phase { BCT_EMPTY, BCT_BOUND, BCT_IDENTIFIED, BCT_WINDOW, BCT_SHARED, BCT_MAILBOX, BCT_POSTED };

/* All values are explicit fake-fixture inputs, not discovered hardware facts.
 * RAM size/core revision require a future genuine discovery/ownership path.
 * window_page is a 4K-aligned backplane page; no default numeric core base
 * is inferred from excluded reference headers. initial_tail is the pre-FW
 * tail word; an unchanged/zero word never qualifies as a shared handoff. */
struct bct_plan { uint32_t ram_bytes, window_page, pcie_core_revision, initial_tail; };
struct bct_session {
    struct apb_io io;
    struct bct_plan plan;
    uint32_t phase, faulted, dirty, reads_attempted, writes_attempted, pending_writes;
    uint32_t pci_identity, window_word, chip_word;
    uint32_t shared_address, shared_flags, h2d_address, d2h_address, ring_info_address;
    uint32_t d2h_word, mailbox_mask_offset;
    int32_t result;
};

/* Caller owns and serializes session/accessor/fake file for their full lifetime;
 * all calls are synchronous, no callbacks may reenter. Any I/O error or partial
 * write becomes a sticky fault. No automatic retry/reset/restore/free exists.
 * Reinit is allowed only on an EMPTY session, never as fault recovery. */
int bct_init(struct bct_session *, const struct apb_io *checked_fake, const struct bct_plan *);
int bct_identify(struct bct_session *);
int bct_select_window(struct bct_session *);
int bct_discover_shared(struct bct_session *);
int bct_setup_mailbox(struct bct_session *);
/* One nonzero opaque fake test word only; not a claimed firmware command API.
 * Pending H2D refuses immediately. <=13 core revisions require two config
 * doorbells; newer ones one. No ring/DMA/IRQ allocation or enable. */
int bct_post_mailbox(struct bct_session *, uint32_t word);

/* Default/KERNEL implementations return BCT_GATED without touching io.
 * BCT_FAKE_BAR_FILE is refused in a KERNEL build. */
int bct_fake_transport_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
