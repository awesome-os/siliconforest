/* Original Darwin Lab reference policy. Project license selection pending.
 * No kernel APIs, allocation, MMIO, DMA, locks, or hardware operations.
 * This is a single-threaded executable specification, NOT a GPU driver.
 */
#ifndef DARWIN_LAB_BUFFER_LIFETIME_H
#define DARWIN_LAB_BUFFER_LIFETIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DL_BUFFER_CAPACITY 16u
#define DL_LEASE_CAPACITY 64u

typedef enum {
    DL_OK, DL_INVALID, DL_STALE, DL_CAPACITY, DL_BUSY, DL_DEVICE_LOST,
    DL_OWNER_GONE, DL_BAD_STATE, DL_OVERFLOW
} dl_status;
typedef enum {
    DL_CPU_MAP, DL_KERNEL_MAP, DL_SHARED_REF, DL_GPU_BIND,
    DL_IN_FLIGHT, DL_FIRMWARE, DL_KIND_COUNT
} dl_lease_kind;
typedef struct { uint32_t index; uint64_t serial, session; } dl_buffer;
typedef struct { uint32_t index; uint64_t serial, session; } dl_lease;
typedef struct {
    bool occupied, owner_alive;
    uint64_t serial, bytes;
} dl_buffer_slot;
typedef struct {
    bool occupied;
    uint64_t serial;
    dl_buffer buffer;
    dl_lease_kind kind;
} dl_lease_slot;
typedef struct {
    uint64_t session, next_buffer, next_lease, reset_sequence;
    bool device_lost;
    dl_buffer_slot buffers[DL_BUFFER_CAPACITY];
    dl_lease_slot leases[DL_LEASE_CAPACITY];
} dl_domain;
typedef struct { unsigned host, device; bool owner_alive; uint64_t bytes; } dl_view;

/* Initialize only a NEW domain. A session must never be reused for a domain
 * whose handles could survive. Does not make resetting a real device safe.
 * Caller serializes all operations. No concurrency guarantees are supplied.
 */
dl_status dl_init(dl_domain *, uint64_t session);
dl_status dl_allocate(dl_domain *, uint64_t bytes, uint64_t page_size, dl_buffer *out);
dl_status dl_hold(dl_domain *, dl_buffer, dl_lease_kind, dl_lease *out);
dl_status dl_close_owner(dl_domain *, dl_buffer);
dl_status dl_release_host(dl_domain *, dl_lease);
/* Backend calls these only AFTER the corresponding real unmap/fence/firmware
 * acknowledgement, not when requesting it. They do not perform that work.
 */
dl_status dl_unbind_gpu(dl_domain *, dl_lease);
dl_status dl_complete_job(dl_domain *, dl_lease);
dl_status dl_ack_firmware_release(dl_domain *, dl_lease);
dl_status dl_timeout(dl_domain *, dl_lease);
dl_status dl_mark_device_lost(dl_domain *);
/* CRITICAL integration obligation: call ONLY after an external backend has
 * verified that all device/firmware access has stopped and translations are
 * invalidated. sequence is an ordering token, NOT proof of that fact.
 * Clears device leases, never CPU/kernel mappings or shared references.
 * No such hardware backend is implemented in this repository.
 */
dl_status dl_backend_ack_quiescence(dl_domain *, uint64_t sequence);
/* Retires metadata only; neither frees memory nor changes hardware mappings. */
dl_status dl_reclaim(dl_domain *, dl_buffer);
dl_status dl_inspect(const dl_domain *, dl_buffer, dl_view *out);
dl_status dl_check_invariants(const dl_domain *);
#endif
