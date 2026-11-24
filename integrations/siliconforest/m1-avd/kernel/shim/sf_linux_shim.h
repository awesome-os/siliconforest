/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The part of the Linux kernel interface that the Apple AVD driver's codec files
 * (linux/avd-*.c, linux/v4l2-h264.c, linux/v4l2-vp9.c: verbatim from omacom/linux-aurora
 * integration/aurora 6a08a08e) compile against, for SFAppleAVD.kext and for the host test.
 *
 * Macro and type definitions follow the Linux headers of the same names (include/linux/
 * bits.h, bitfield.h, math.h, minmax.h, align.h, container_of.h, array_size.h;
 * include/media/videobuf2-core.h, videobuf2-v4l2.h, v4l2-mem2mem.h, v4l2-ctrls.h;
 * include/uapi/linux/videodev2.h), reduced to what the driver uses. The videobuf2 /
 * mem2mem / control-handler structures are small stand-ins filled by sf_avd_core.c: one
 * current source buffer, one current destination buffer and the set of capture buffers of
 * a context, which is all the codec files look at.
 *
 * Everything the OS must supply is declared in sf_avd_os.h.
 */
#ifndef _SF_LINUX_SHIM_H
#define _SF_LINUX_SHIM_H

#include "sf_avd_os.h"

/* ---- types ------------------------------------------------------------------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int8_t   __s8;
typedef int16_t  __s16;
typedef int32_t  __s32;
typedef int64_t  __s64;
typedef u64 dma_addr_t;
typedef unsigned int gfp_t;

#define __iomem
#define __maybe_unused __attribute__((unused))
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#define noinline_for_stack __attribute__((noinline))
#ifndef static_assert
#define static_assert(c, ...) _Static_assert(c, #c)
#endif
#define __user

/* ---- bits, fields, arithmetic -------------------------------------------------------- */
#ifdef BIT
#undef BIT
#endif
#define BIT(n)            (1UL << (n))
#define BIT_ULL(n)        (1ULL << (n))
#define GENMASK(h, l)     (((~0UL) << (l)) & (~0UL >> (63 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define __bf_shf(x)       (__builtin_ffsll(x) - 1)
#define FIELD_PREP(mask, val) \
	((typeof(mask))((((typeof(mask))(val)) << __bf_shf(mask)) & (mask)))
#define FIELD_GET(mask, reg)  ((typeof(mask))(((reg) & (mask)) >> __bf_shf(mask)))

#ifdef ARRAY_SIZE
#undef ARRAY_SIZE
#endif
#define ARRAY_SIZE(a)     (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define __sf_round_mask(x, y) ((__typeof__(x))((y) - 1))
#ifdef round_up
#undef round_up
#endif
#ifdef round_down
#undef round_down
#endif
#define round_up(x, y)    ((((x) - 1) | __sf_round_mask(x, y)) + 1)
#define round_down(x, y)  ((x) & ~__sf_round_mask(x, y))
#ifdef ALIGN
#undef ALIGN
#endif
#define ALIGN(x, a)       (((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))
#define IS_ALIGNED(x, a)  (((x) & ((typeof(x))(a) - 1)) == 0)

/* the SoC's page size (the DART's and the kernel's), also on the host so sizes agree */
#ifdef PAGE_SIZE
#undef PAGE_SIZE
#endif
#ifdef PAGE_SHIFT
#undef PAGE_SHIFT
#endif
#ifdef PAGE_MASK
#undef PAGE_MASK
#endif
#define PAGE_SHIFT        14
#define PAGE_SIZE         (1UL << PAGE_SHIFT)
#define PAGE_MASK         (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x)     ALIGN(x, PAGE_SIZE)

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define min(a, b)  ({ __typeof__(a) _pa = (a); __typeof__(b) _pb = (b); _pa < _pb ? _pa : _pb; })
#define max(a, b)  ({ __typeof__(a) _pa = (a); __typeof__(b) _pb = (b); _pa > _pb ? _pa : _pb; })
#define min_t(t, a, b) ({ t _pa = (a); t _pb = (b); _pa < _pb ? _pa : _pb; })
#define max_t(t, a, b) ({ t _pa = (a); t _pb = (b); _pa > _pb ? _pa : _pb; })
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define clamp_val(v, lo, hi) clamp_t(__typeof__(v), v, lo, hi)
#define swap(a, b) do { __typeof__(a) _pt = (a); (a) = (b); (b) = _pt; } while (0)
#ifdef abs
#undef abs
#endif
#define abs(x) ({ __typeof__(x) _px = (x); _px < 0 ? -_px : _px; })

static inline unsigned long roundup_pow_of_two(unsigned long n)
{
	unsigned long r = 1;
	while (r < n)
		r <<= 1;
	return r;
}
#ifndef KERNEL   /* the XNU kernel has its own */
static inline int fls(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
#endif
static inline int ilog2(unsigned long x) { return 63 - __builtin_clzl(x); }
static inline u64 div_u64(u64 a, u32 b) { return a / b; }
static inline u32 get_unaligned_be32(const void *p)
{
	const u8 *b = (const u8 *)p;
	return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}
static inline u16 get_unaligned_le16(const void *p)
{
	const u8 *b = (const u8 *)p;
	return (u16)(b[0] | (b[1] << 8));
}

#ifdef container_of
#undef container_of
#endif
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define IS_ENABLED(x) 0
#define IS_ERR(p)   ((unsigned long)(p) >= (unsigned long)-4095)
#define PTR_ERR(p)  ((long)(p))
#define WARN_ON(c)  ({ int _pw = !!(c); if (_pw) sf_avd_os_log(1, "WARN_ON(%s) at %s:%d", #c, __func__, __LINE__); _pw; })
#define WARN_ON_ONCE(c) WARN_ON(c)
#define WARN(c, ...) WARN_ON(c)
#define BUILD_BUG_ON(c) _Static_assert(!(c), #c)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL(x)
#define MODULE_LICENSE(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_AUTHOR(x)
#define VISIBLE_IF_KUNIT static
#define fallthrough __attribute__((fallthrough))

/* errno values are the host's (sf_avd_os.h includes errno.h); the core returns them negated */

/* ---- memory, logging, time, locks, MMIO ----------------------------------------------- */
#define GFP_KERNEL 0
static inline void *kzalloc(size_t n, gfp_t f) { (void)f; return sf_avd_os_zalloc(n); }
static inline void *kmalloc(size_t n, gfp_t f) { (void)f; return sf_avd_os_zalloc(n); }
static inline void *kcalloc(size_t n, size_t s, gfp_t f)
{
	(void)f;
	if (s != 0 && n > (size_t)-1 / s)
		return NULL;
	return sf_avd_os_zalloc(n * s);
}
#define kvcalloc kcalloc
#define kmalloc_array kcalloc
static inline void kfree(const void *p) { sf_avd_os_free((void *)p); }
#define kvfree kfree
static inline void *kmemdup(const void *src, size_t n, gfp_t f)
{
	void *p = kmalloc(n ? n : 1, f);
	if (p)
		memcpy(p, src, n);
	return p;
}
#ifndef KERNEL   /* the XNU kernel has its own */
static inline int scnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static inline int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (size == 0)
		return 0;
	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n < 0 ? 0 : ((size_t)n >= size ? (int)size - 1 : n);
}
#endif

struct device { const char *name; };
#define dev_err(d, fmt, ...)   sf_avd_os_log(0, fmt, ##__VA_ARGS__)
#define dev_warn(d, fmt, ...)  sf_avd_os_log(1, fmt, ##__VA_ARGS__)
#define dev_info(d, fmt, ...)  sf_avd_os_log(2, fmt, ##__VA_ARGS__)
#define dev_dbg(d, fmt, ...)   sf_avd_os_log(3, fmt, ##__VA_ARGS__)
#define dev_err_ratelimited    dev_err
#define dev_warn_ratelimited   dev_warn
#define dev_dbg_ratelimited    dev_dbg
#define dev_info_once          dev_info
#define pr_err(fmt, ...)       sf_avd_os_log(0, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)      sf_avd_os_log(1, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)      sf_avd_os_log(2, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)     sf_avd_os_log(3, fmt, ##__VA_ARGS__)

static inline void usleep_range(unsigned long lo, unsigned long hi) { (void)hi; sf_avd_os_usleep((u32)lo); }

static inline u32 readl(const void __iomem *a) { return sf_avd_os_readl(a); }
static inline void writel(u32 v, void __iomem *a) { sf_avd_os_writel(v, a); }
#define readl_relaxed  readl
#define writel_relaxed writel
static inline void memcpy_toio(void __iomem *to, const void *from, size_t n) { sf_avd_os_memcpy_toio(to, from, n); }
/* polls every sleep_us; 0 or -ETIMEDOUT */
#define readl_poll_timeout(addr, val, cond, sleep_us, timeout_us) ({ \
	u64 _waited = 0; int _ret = 0; \
	for (;;) { \
		(val) = readl(addr); \
		if (cond) break; \
		if (_waited >= (u64)(timeout_us)) { (val) = readl(addr); _ret = (cond) ? 0 : -ETIMEDOUT; break; } \
		sf_avd_os_usleep(sleep_us); _waited += (sleep_us); \
	} _ret; })

typedef struct { void *os; } spinlock_t;
#define spin_lock_irqsave(l, flags)      do { (flags) = sf_avd_os_lock((l)->os); } while (0)
#define spin_unlock_irqrestore(l, flags) sf_avd_os_unlock((l)->os, flags)
struct mutex { void *os; };
struct delayed_work { int unused; };
struct work_struct { int unused; };

void sort(void *base, size_t num, size_t size, int (*cmp)(const void *, const void *),
    void (*swap_fn)(void *, void *, int));
void sort_r(void *base, size_t num, size_t size, int (*cmp)(const void *, const void *, const void *),
    void (*swap_fn)(void *, void *, int, const void *), const void *priv);

/* ---- what avd.h names but this port does not use -------------------------------------- */
struct firmware { size_t size; const u8 *data; };
struct platform_device { int unused; };
struct iommu_domain { int unused; };
struct reset_control { int unused; };
struct v4l2_device { struct device *dev; };
struct media_device { int unused; };
struct video_device { int unused; };
struct v4l2_m2m_dev { int unused; };
struct v4l2_ctrl_config { u32 id; };
struct file { int unused; };

/* ---- include/uapi/linux/videodev2.h, reduced ----------------------------------------- */
#define v4l2_fourcc(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define V4L2_PIX_FMT_NV12       v4l2_fourcc('N', 'V', '1', '2')
#define V4L2_PIX_FMT_NV16       v4l2_fourcc('N', 'V', '1', '6')
#define V4L2_PIX_FMT_P010       v4l2_fourcc('P', '0', '1', '0')
#define V4L2_PIX_FMT_H264_SLICE v4l2_fourcc('S', '2', '6', '4')
#define V4L2_PIX_FMT_HEVC_SLICE v4l2_fourcc('S', '2', '6', '5')
#define V4L2_PIX_FMT_VP9_FRAME  v4l2_fourcc('V', 'P', '9', 'F')
#define V4L2_PIX_FMT_AV1_FRAME  v4l2_fourcc('A', 'V', '1', 'F')
#define V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF 0x00000200
#define VIDEO_MAX_PLANES 8

struct v4l2_plane_pix_format {
	__u32 sizeimage;
	__u32 bytesperline;
	__u16 reserved[6];
} __attribute__((packed));

struct v4l2_pix_format_mplane {
	__u32 width;
	__u32 height;
	__u32 pixelformat;
	__u32 field;
	__u32 colorspace;
	struct v4l2_plane_pix_format plane_fmt[VIDEO_MAX_PLANES];
	__u8 num_planes;
	__u8 flags;
	__u8 ycbcr_enc;
	__u8 quantization;
	__u8 xfer_func;
	__u8 reserved[7];
} __attribute__((packed));

struct v4l2_format {
	__u32 type;
	union {
		struct v4l2_pix_format_mplane pix_mp;
		__u8 raw_data[200];
	} fmt;
};

struct v4l2_frmsize_stepwise {
	__u32 min_width, max_width, step_width, min_height, max_height, step_height;
};

/* ---- videobuf2 / mem2mem stand-ins ---------------------------------------------------- */
enum vb2_buffer_state {
	VB2_BUF_STATE_DEQUEUED, VB2_BUF_STATE_IN_REQUEST, VB2_BUF_STATE_PREPARING, VB2_BUF_STATE_QUEUED,
	VB2_BUF_STATE_ACTIVE, VB2_BUF_STATE_DONE, VB2_BUF_STATE_ERROR,
};

struct vb2_buffer;
struct vb2_queue {
	unsigned int        num;       /* capture buffers of the context */
	struct vb2_buffer **bufs;
};
struct vb2_plane { unsigned int length; unsigned int bytesused; };
struct media_request;
struct vb2_buffer {
	struct vb2_queue *vb2_queue;
	unsigned int      index;
	u64               timestamp;
	bool              copied_timestamp;   /* a decode targeted this buffer: references find it */
	struct vb2_plane  planes[1];
	struct { struct media_request *req; } req_obj;
	dma_addr_t        sf_dma;
	void             *sf_vaddr;
};
struct vb2_v4l2_buffer {
	struct vb2_buffer vb2_buf;
	u32               flags;
	struct vb2_plane  planes[1];          /* as videobuf2 fills it when a buffer is queued */
};
struct v4l2_m2m_buffer { struct vb2_v4l2_buffer vb; };
struct v4l2_m2m_ctx {
	struct { struct vb2_queue q; } cap_q_ctx;
	struct vb2_v4l2_buffer *sf_src, *sf_dst;
	bool sf_finished;                 /* the job of this run was finished ... */
	enum vb2_buffer_state sf_result;  /* ... with this result */
};
struct v4l2_fh { struct v4l2_m2m_ctx *m2m_ctx; };
struct v4l2_fh *file_to_v4l2_fh(struct file *filp);

static inline dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *vb, unsigned int plane)
{
	(void)plane;
	return vb->sf_dma;
}
static inline void *vb2_plane_vaddr(struct vb2_buffer *vb, unsigned int plane) { (void)plane; return vb->sf_vaddr; }
static inline unsigned long vb2_get_plane_payload(struct vb2_buffer *vb, unsigned int plane)
{
	(void)plane;
	return vb->planes[0].bytesused;
}
static inline struct vb2_buffer *vb2_find_buffer(struct vb2_queue *q, u64 timestamp)
{
	for (unsigned int i = 0; i < q->num; i++)
		if (q->bufs[i] && q->bufs[i]->copied_timestamp && q->bufs[i]->timestamp == timestamp)
			return q->bufs[i];
	return NULL;
}
static inline struct vb2_v4l2_buffer *v4l2_m2m_next_src_buf(struct v4l2_m2m_ctx *m) { return m->sf_src; }
static inline struct vb2_v4l2_buffer *v4l2_m2m_next_dst_buf(struct v4l2_m2m_ctx *m) { return m->sf_dst; }

/* ---- controls -------------------------------------------------------------------------- */
#include "../../include/sfavd/v4l2-controls.h"

union v4l2_ctrl_ptr {
	void *p;
	const void *p_const;
	u32 *p_u32;
	struct v4l2_ctrl_h264_sps *p_h264_sps;
	struct v4l2_ctrl_h264_pps *p_h264_pps;
	struct v4l2_ctrl_h264_scaling_matrix *p_h264_scaling_matrix;
	struct v4l2_ctrl_h264_slice_params *p_h264_slice_params;
	struct v4l2_ctrl_h264_decode_params *p_h264_decode_params;
	struct v4l2_ctrl_h264_pred_weights *p_h264_pred_weights;
	struct v4l2_ctrl_hevc_sps *p_hevc_sps;
	struct v4l2_ctrl_hevc_pps *p_hevc_pps;
	struct v4l2_ctrl_hevc_slice_params *p_hevc_slice_params;
	struct v4l2_ctrl_hevc_scaling_matrix *p_hevc_scaling_matrix;
	struct v4l2_ctrl_hevc_decode_params *p_hevc_decode_params;
	struct v4l2_ctrl_vp9_compressed_hdr *p_vp9_compressed_hdr_probs;
	struct v4l2_ctrl_vp9_frame *p_vp9_frame;
	struct v4l2_ctrl_av1_sequence *p_av1_sequence;
	struct v4l2_ctrl_av1_frame *p_av1_frame;
};

struct v4l2_ctrl_handler;
struct v4l2_ctrl {
	struct v4l2_ctrl_handler *handler;
	u32 id;
	u32 elems;          /* elements present of a dynamic array */
	u32 new_elems;
	u32 elem_size;
	union v4l2_ctrl_ptr p_cur, p_new;
};
#define SF_AVD_MAX_CTRLS 12
struct v4l2_ctrl_handler {
	unsigned int     num;
	struct v4l2_ctrl ctrls[SF_AVD_MAX_CTRLS];
	int              error;
};
static inline struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *h, u32 id)
{
	for (unsigned int i = 0; i < h->num; i++)
		if (h->ctrls[i].id == id && h->ctrls[i].p_cur.p != NULL)
			return &h->ctrls[i];
	return NULL;
}
static inline void v4l2_ctrl_lock(struct v4l2_ctrl *c) { (void)c; }
static inline void v4l2_ctrl_unlock(struct v4l2_ctrl *c) { (void)c; }
struct v4l2_ctrl_ops { int unused; };
struct v4l2_ioctl_ops { int unused; };
struct vb2_ops { int unused; };

#endif
