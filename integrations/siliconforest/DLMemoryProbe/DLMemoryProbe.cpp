/* Original Darwin Lab code. License selection pending.
 * Experimental RAM-only provider for PureDarwin PDDRMDri. UNBUILT against XNU.
 * Explicit boot opt-in, privileged opens, bounded resources, no unload.
 */
#include "provider_iokit.h"
#include "memory_probe_ioctl.h"
#include "pd_dri_cdev.h"
#include <IOKit/IOLib.h>
#include <kern/locks.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <mach/vm_prot.h>
#include <vm/memory_types.h>
#include <machine/memory_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/kauth.h>

static dl_probe *service;
static void *storage;
static lck_grp_t *group;
static lck_mtx_t *lock;
static bool published;
struct Handle { uint64_t id; };
static_assert(sizeof(ppnum_t) == sizeof(uint32_t), "PDDRMDri page-number ABI changed");
static_assert(sizeof(dlmp_info) == 32 && sizeof(dlmp_alloc) == 40 &&
              sizeof(dlmp_stats) == 88 && sizeof(dlmp_collect) == 48,
              "probe protocol layout changed");

static int as_errno(int status) {
    switch (status) {
        case DL_OK: return 0;
        case DL_INVALID: return EINVAL;
        case DL_STALE: case DL_OWNER_GONE: return ENXIO;
        case DL_CAPACITY: return ENOSPC;
        case DL_BUSY: return EBUSY;
        case DL_OVERFLOW: return EOVERFLOW;
        default: return EIO;
    }
}
static int open_probe(int, int, void **cookie) {
    if (!cookie || !service) return ENXIO;
    // PDDRMDri publishes world-readable nodes. Enforce authorization here.
    if (!kauth_cred_issuser(kauth_cred_get())) return EPERM;
    auto *h = static_cast<Handle *>(IOMallocZero(sizeof(Handle)));
    if (!h) return ENOMEM;
    lck_mtx_lock(lock);
    int status = dl_probe_open(service, &h->id);
    lck_mtx_unlock(lock);
    if (status != DL_OK) { IOFree(h, sizeof(Handle)); return as_errno(status); }
    *cookie = h;
    return 0;
}
static void close_probe(void *cookie) {
    auto *h = static_cast<Handle *>(cookie);
    if (!h) return;
    lck_mtx_lock(lock);
    int status = dl_probe_close(service, h->id);
    lck_mtx_unlock(lock);
    if (status != DL_OK) IOLog("DLMemoryProbe: close failed (%d)\n", status);
    IOFree(h, sizeof(Handle)); // mapping tickets do NOT reference this handle
}
static int ioctl_probe(void *cookie, unsigned int cmd, void *data, size_t size) {
    if (!cookie) return ENXIO;
    unsigned operation;
    switch (cmd) {
        case DLMP_INFO: operation = DLMP_OP_INFO; break;
        case DLMP_ALLOC: operation = DLMP_OP_ALLOC; break;
        case DLMP_STATS: operation = DLMP_OP_STATS; break;
        case DLMP_COLLECT: operation = DLMP_OP_COLLECT; break;
        default: return ENOTTY;
    }
    lck_mtx_lock(lock);
    int status = dl_probe_command(service, static_cast<Handle *>(cookie)->id,
                                  operation, data, size);
    lck_mtx_unlock(lock);
    return as_errno(status);
}
static int mmap_probe(void *cookie, off_t offset, size_t bytes, int prot,
                      uint32_t *pages, unsigned int *wimg,
                      void (**release)(void *), void **release_arg) {
    if (!cookie || offset < 0 || !wimg ||
        (prot & ~(VM_PROT_READ | VM_PROT_WRITE))) return EINVAL;
    unsigned access = 0;
    if (prot & VM_PROT_READ) access |= 1u;
    if (prot & VM_PROT_WRITE) access |= 2u;
    lck_mtx_lock(lock);
    int status = dl_probe_map(service, static_cast<Handle *>(cookie)->id,
                              static_cast<uint64_t>(offset), bytes, access,
                              pages, bytes / PAGE_SIZE, release, release_arg);
    lck_mtx_unlock(lock);
    // Take the cache constant from THIS architecture, not a copied x86 value.
    if (status == DL_OK) *wimg = VM_WIMG_DEFAULT;
    return as_errno(status);
}
static long read_probe(void *, void *, size_t, int) { return -ENOTSUP; }
static int poll_probe(void *, int, void *) { return 0; }
static const pd_dri_ops ops = {
    open_probe, close_probe, read_probe, ioctl_probe, poll_probe, mmap_probe
};
extern "C" kern_return_t dlmp_start(kmod_info_t *, void *);
extern "C" kern_return_t dlmp_stop(kmod_info_t *, void *);
extern "C" kern_return_t dlmp_start(kmod_info_t *, void *) {
    int enable = 0;
    if (!PE_parse_boot_argn("dlmemtest", &enable, sizeof(enable)) || enable != 1)
        return KERN_SUCCESS;
    if (service) return KERN_SUCCESS;
    group = lck_grp_alloc_init("darwin-lab-ram-probe", LCK_GRP_ATTR_NULL);
    if (!group) return KERN_FAILURE;
    lock = lck_mtx_alloc_init(group, LCK_ATTR_NULL);
    if (!lock) { lck_grp_free(group); group = nullptr; return KERN_FAILURE; }
    storage = IOMallocZero(dl_probe_size());
    if (!storage || dl_probe_init(storage, dl_probe_size(), PAGE_SIZE, DLMP_BUDGET,
                                  dl_iokit_probe_ops(), nullptr, &service) != DL_OK) {
        if (storage) IOFree(storage, dl_probe_size());
        storage = nullptr; lck_mtx_free(lock, group); lck_grp_free(group);
        lock = nullptr; group = nullptr; return KERN_FAILURE;
    }
    int error = pd_dri_register(&ops);
    if (error) {
        // No callback exposure if registration was rejected. Preserve everything
        // anyway until reboot: no unverified unregister/recovery mechanism.
        IOLog("DLMemoryProbe: registration rejected (%d); disabled\n", error);
        return KERN_SUCCESS;
    }
    published = true;
    // A non-card/non-render minor gives /dev/dri/dlmemtest. PDDRMDri owns cloning.
    error = pd_dri_make_node(&ops, 64, "dlmemtest", nullptr);
    IOLog("DLMemoryProbe: RAM probe publication %d; mapped reclamation BLOCKED\n", error);
    // Never return failure after publishing function pointers: unloading would
    // leave dangling callbacks. A failed node publication requires a VM restart.
    return KERN_SUCCESS;
}
extern "C" kern_return_t dlmp_stop(kmod_info_t *, void *) {
    (void)published;
    return KERN_FAILURE; // code, service, tickets and quarantined pages live to reboot
}
