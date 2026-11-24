#ifndef B8G_SILICONFOREST_ABI_H
#define B8G_SILICONFOREST_ABI_H

/*
 * Awesome-OS B8G_ABI_MAGIC_V3 + 64-byte aligned B8G_ARENA_V1 C11 bridge header
 * for awesome-os/siliconforest (upstream: chriskearney/siliconforest, MIT).
 *
 * Bridges SiliconForest's XNU, Linux-compat (LinuxKPI), Asahi Rust GPU (AGX),
 * DCP display coprocessor, and driver kexts to b8g-abi across awesome-os/t2xnu,
 * awesome-os/t2linux (ARM64), and awesome-os/b8g-ghost @ cas-v1.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define B8G_ALIGNAS(n) alignas(n)
#define B8G_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#include <stdalign.h>
#define B8G_ALIGNAS(n) _Alignas(n)
#define B8G_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define B8G_ABI_MAGIC_V3            0x33564742u /* "BGV3" */
#define B8G_ARENA_V1_MAGIC          0x31413842u /* "B8A1" */
#define B8G_ARENA_V1_ALIGN          64u
#define B8G_SILICONFOREST_ABI_VER   3u

#if defined(_WIN32) || defined(__CYGWIN__)
#  define B8G_SF_EXPORT __declspec(dllexport)
#else
#  define B8G_SF_EXPORT __attribute__((visibility("default")))
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define B8G_SF_UNUSED __attribute__((unused))
#else
#  define B8G_SF_UNUSED
#endif

typedef enum B8gSiliconForestKextKind {
    B8G_SF_KEXT_XNU_CORE      = 1,
    B8G_SF_KEXT_LINUX_COMPAT  = 2,
    B8G_SF_KEXT_GPU_AGX       = 3,
    B8G_SF_KEXT_DCP_DISPLAY   = 4,
    B8G_SF_KEXT_NVME_STORAGE  = 5,
    B8G_SF_KEXT_WIFI_BWFM     = 6,
    B8G_SF_KEXT_AUDIO_SPEAKER = 7,
    B8G_SF_KEXT_VIDEO_DECODE  = 8,
    B8G_SF_KEXT_CHONKSTEP_UI  = 9
} B8gSiliconForestKextKind;

typedef enum B8gSiliconForestPeerTarget {
    B8G_SF_PEER_T2XNU     = 1, /* awesome-os/t2xnu (T2macOS / Darwin XNU) */
    B8G_SF_PEER_T2LINUX   = 2, /* awesome-os/t2linux (ARM64) */
    B8G_SF_PEER_B8G_GHOST = 3  /* awesome-os/b8g-ghost @ cas-v1 */
} B8gSiliconForestPeerTarget;

typedef enum B8gSiliconForestError {
    B8G_SF_OK                = 0,
    B8G_SF_ERR_INVALID_ARG   = -1,
    B8G_SF_ERR_BAD_MAGIC     = -2,
    B8G_SF_ERR_UNALIGNED     = -3,
    B8G_SF_ERR_UNSUPPORTED   = -4
} B8gSiliconForestError;

/*
 * Canonical 64-byte aligned B8G_ARENA_V1 header shared across XNU kexts,
 * m1n1 boot handoff, t2linux ARM64, and b8g-ghost cas-v1 checkpoints.
 */
typedef struct B8gArenaV1Header {
    B8G_ALIGNAS(64) uint32_t arena_magic; /* B8G_ARENA_V1_MAGIC */
    uint32_t abi_magic;       /* B8G_ABI_MAGIC_V3 */
    uint32_t version;         /* B8G_SILICONFOREST_ABI_VER */
    uint32_t flags;           /* Arena capability flags */
    uint64_t base_phys_addr;  /* Physical base address (64-byte aligned) */
    uint64_t byte_length;     /* Total arena size in bytes */
    uint64_t write_cursor;    /* Monotonic ring/arena write offset */
    uint64_t cas_v1_epoch;    /* b8g-ghost @ cas-v1 checkpoint epoch */
    uint64_t reserved[2];     /* Pad to exact 64-byte cacheline */
} B8gArenaV1Header;

B8G_STATIC_ASSERT(sizeof(B8gArenaV1Header) == 64u,
                  "B8gArenaV1Header must be exactly 64 bytes");
B8G_STATIC_ASSERT(alignof(B8gArenaV1Header) == 64u,
                  "B8gArenaV1Header must be 64-byte aligned");

/*
 * 64-byte aligned descriptor bridging a SiliconForest kext / GPU / DCP
 * subsystem into the universal b8g-abi runtime.
 */
typedef struct B8gSiliconForestBridgeDesc {
    B8G_ALIGNAS(64) uint32_t abi_magic;   /* B8G_ABI_MAGIC_V3 */
    uint32_t abi_version;     /* B8G_SILICONFOREST_ABI_VER */
    uint32_t kext_kind;       /* B8gSiliconForestKextKind */
    uint32_t peer_target;     /* B8gSiliconForestPeerTarget */
    uint64_t fb_phys_addr;    /* DCP / GPU framebuffer physical base */
    uint32_t fb_width;        /* Active display width in pixels */
    uint32_t fb_height;       /* Active display height in pixels */
    uint32_t fb_stride;       /* Scanline stride in bytes */
    uint32_t fb_format;       /* DRM fourcc / pixel format */
    uint64_t arena_phys_addr; /* Attached B8G_ARENA_V1 physical base */
    uint64_t cas_digest_lo;   /* Low 64 bits of b8g-ghost cas-v1 digest */
    uint64_t cas_digest_hi;   /* High 64 bits of b8g-ghost cas-v1 digest */
} B8gSiliconForestBridgeDesc;

B8G_STATIC_ASSERT(sizeof(B8gSiliconForestBridgeDesc) == 64u,
                  "B8gSiliconForestBridgeDesc must be exactly 64 bytes");
B8G_STATIC_ASSERT(alignof(B8gSiliconForestBridgeDesc) == 64u,
                  "B8gSiliconForestBridgeDesc must be 64-byte aligned");

static inline B8G_SF_UNUSED int32_t b8g_sf_validate_arena(const B8gArenaV1Header *hdr) {
    if (!hdr) {
        return B8G_SF_ERR_INVALID_ARG;
    }
    if (((uintptr_t)hdr & (B8G_ARENA_V1_ALIGN - 1u)) != 0u) {
        return B8G_SF_ERR_UNALIGNED;
    }
    if (hdr->arena_magic != B8G_ARENA_V1_MAGIC ||
        hdr->abi_magic != B8G_ABI_MAGIC_V3) {
        return B8G_SF_ERR_BAD_MAGIC;
    }
    return B8G_SF_OK;
}

static inline B8G_SF_UNUSED int32_t b8g_sf_validate_desc(const B8gSiliconForestBridgeDesc *desc) {
    if (!desc) {
        return B8G_SF_ERR_INVALID_ARG;
    }
    if (((uintptr_t)desc & (B8G_ARENA_V1_ALIGN - 1u)) != 0u) {
        return B8G_SF_ERR_UNALIGNED;
    }
    if (desc->abi_magic != B8G_ABI_MAGIC_V3 ||
        desc->abi_version != B8G_SILICONFOREST_ABI_VER) {
        return B8G_SF_ERR_BAD_MAGIC;
    }
    return B8G_SF_OK;
}

B8G_SF_EXPORT uint32_t b8g_sf_abi_magic(void);
B8G_SF_EXPORT int32_t b8g_sf_register_bridge(const B8gSiliconForestBridgeDesc *desc,
                                             B8gArenaV1Header *arena);

#undef B8G_ALIGNAS
#undef B8G_STATIC_ASSERT

#ifdef __cplusplus
}
#endif

#endif /* B8G_SILICONFOREST_ABI_H */
