// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Video Decoder driver core for SFAppleAVD.kext.
 *
 * Ported from Linux drivers/media/platform/apple/avd/avd-drv.c and avd-v4l2.c
 * (omacom/linux-aurora, integration/aurora, 6a08a08e):
 *
 *   Apple Video Decoder driver
 *   Copyright The Asahi Linux Contributors
 *   Based on rkvdec driver by Collabora, Ltd.
 *   Copyright (C) 2019 Collabora, Ltd.
 *   Based on rkvdec driver by Google LLC. (Tomasz Figa <tfiga@chromium.org>)
 *   Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 *   Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * The codec files (linux/avd-h264.c, avd-hevc.c, avd-vp9.c), the hardware file
 * (linux/avd-hw.c) and the instruction definitions are the Linux files, unchanged. This
 * file replaces the V4L2 device around them: where Linux has a mem2mem context with
 * videobuf2 queues and a control handler filled by the request API, a session here has
 * its decoded buffers, one coded buffer and its controls set by direct calls, and a
 * decode call runs what v4l2-mem2mem's device_run runs and then waits for the decoder.
 *
 * Kept from Linux: the compressed-reference layout (fill_comp), the decoded format and
 * its size computation, buffer allocation, job tables and their submission to the VP's
 * instruction FIFO, the mailbox interrupt protocol (VP done -> kick the PP, PP done ->
 * frame done), the 2 s watchdog with a reset of the block, and the variant table.
 *
 * Not kept: runtime power management (the block is booted while a session exists),
 * several contexts decoding at once (calls are serialised by the caller, as mem2mem
 * serialises jobs), dma-buf, and V4L2's validation of control contents (the kext keeps
 * the device node root-only for that reason).
 */
#include "sf_linux_shim.h"
#include "linux/avd.h"
#include "linux/avd-inst.h"
#include "linux/avd-regs.h"
#include "sf_avd_core.h"

#define AVD_CODEC_INFO_MAX_SIZE	0x40

/* ---- avd-drv.c: compressed reference layout ------------------------------------------- */

static void calc_tile_meta(u32 w, u32 h, u32 bpb, u32 tile_dim,
			   u32 meta_hdr_bytes, u32 *tile, u32 *meta)
{
	u32 tiles_width, tiles_height, meta_tile_w, meta_tile_h, tile_bytes;
	tiles_width = DIV_ROUND_UP(w, tile_dim);
	tiles_height = DIV_ROUND_UP(h, tile_dim);
	tile_bytes = tile_dim * tile_dim * DIV_ROUND_UP(bpb, 8);
	*tile = ALIGN(tiles_width * tiles_height * tile_bytes, 16);

	meta_tile_w = roundup_pow_of_two(tiles_width);
	meta_tile_h = roundup_pow_of_two(tiles_height);

	*meta = ALIGN(meta_tile_w * meta_tile_h * meta_hdr_bytes, 16);
}

void fill_comp(struct avd_comp *comp, enum avd_image_fmt image_fmt, u32 width,
	       u32 height)
{
	u32 y_meta, y, uv_meta, uv;
	int bit_depth;

	switch (image_fmt) {
	case AVD_IMG_FMT_420_10BIT:
	case AVD_IMG_FMT_422_10BIT:
		bit_depth = 10;
		break;
	default:
		bit_depth = 8;
		break;
	}

	/* y has 32x32 tiles and 32 bytes of metadata per tile */
	calc_tile_meta(width, height, bit_depth, 32, 32, &y, &y_meta);
	/* uv has 16x16 tiles and 8 bytes of metadata per tile; 4:2:2 chroma is full height */
	calc_tile_meta(width / 2,
		       (image_fmt == AVD_IMG_FMT_422_8BIT ||
			image_fmt == AVD_IMG_FMT_422_10BIT) ? height : height / 2,
		       bit_depth * 2, 16, 8, &uv, &uv_meta);

	/* output like DCP driver expects */
	comp->offsets[0] = y;
	comp->offsets[1] = 0;
	comp->offsets[2] = y + y_meta + uv;
	comp->offsets[3] = y + y_meta;

	comp->size = y_meta + y + uv_meta + uv;
}

/* ---- avd-drv.c: device memory ---------------------------------------------------------- */

/* struct avd_buf has no room for the OS cookie: a side table by CPU address */
#define SF_AVD_MAX_ALLOCS 8192
struct sf_avd_alloc { void *cpu; void *cookie; };
static struct sf_avd_alloc sf_allocs[SF_AVD_MAX_ALLOCS];
static unsigned int sf_alloc_hint;

int avd_buf_alloc(struct avd_dev *avd, struct avd_buf *buf, size_t size)
{
	void *cookie = NULL;
	u64 iova = 0;
	unsigned int slot;

	/* Keep size equal to the payload length used by the H.264 slice path. */
	avd_buf_free(avd, buf);

	if (size <= 0)
		return -ENOMEM;

	for (slot = 0; slot < SF_AVD_MAX_ALLOCS; slot++)
		if (sf_allocs[(sf_alloc_hint + slot) % SF_AVD_MAX_ALLOCS].cpu == NULL)
			break;
	if (slot == SF_AVD_MAX_ALLOCS)
		return -ENOMEM;
	slot = (sf_alloc_hint + slot) % SF_AVD_MAX_ALLOCS;

	buf->size = size;
	buf->cpu = sf_avd_os_dma_alloc(size, &iova, &cookie);
	if (!buf->cpu) {
		memset(buf, 0, sizeof(*buf));
		return -ENOMEM;
	}
	buf->addr = iova;
	sf_allocs[slot].cpu = buf->cpu;
	sf_allocs[slot].cookie = cookie;
	sf_alloc_hint = slot + 1;
	return 0;
}

void avd_buf_free(struct avd_dev *avd, struct avd_buf *buf)
{
	(void)avd;
	if (buf->cpu) {
		for (unsigned int slot = 0; slot < SF_AVD_MAX_ALLOCS; slot++) {
			if (sf_allocs[slot].cpu == buf->cpu) {
				sf_avd_os_dma_free(sf_allocs[slot].cookie);
				sf_allocs[slot].cpu = NULL;
				sf_allocs[slot].cookie = NULL;
				sf_alloc_hint = slot;
				break;
			}
		}
	}
	memset(buf, 0, sizeof(*buf));
}

struct avd_decoded_buffer *
avd_get_ref_buf(struct avd_ctx *ctx, struct vb2_v4l2_buffer *dst, u64 timestamp)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf;

	/*
	 * If a ref is unused or invalid, address of current destination
	 * buffer is returned.
	 */
	buf = vb2_find_buffer(cap_q, timestamp);
	if (!buf)
		buf = &dst->vb2_buf;

	return vb2_to_avd_decoded_buf(buf);
}

/* ---- avd-drv.c: jobs ------------------------------------------------------------------- */

static int avd_wait_submission_queue(struct avd_ctx *ctx, int vp)
{
	struct avd_dev *avd = ctx->dev;
	u32 max = readl_relaxed(avd->ctrl +
				avd->variant->submit_queue_max_offset + vp * 4);
	u32 cur = readl_relaxed(
		avd->ctrl + avd->variant->submit_queue_status_offset + vp * 4);

	if (cur == max) {
		dev_err(avd->dev, "instruction que full! %d/%d", cur, max);
		return 1;
	}

	if (cur >= max / 2) {
		if (avd->variant->quirks & AVD_QUIRK_HEVC_V5_HEADER)
			usleep_range(100, 150);
		else
			usleep_range(500, 650);
	}
	return 0;
}

int avd_init_job(struct avd_ctx *ctx, enum avd_codec codec, size_t segments)
{
	int ret = 0;
	struct avd_job *job = &ctx->job;

	job->codec = codec;
	job->num = 0;
	/* A table that was built but never submitted still belongs to us. */
	kvfree(job->segments);
	job->segments = kvcalloc(segments, sizeof(*job->segments), GFP_KERNEL);
	if (!job->segments)
		ret = -ENOMEM;
	return ret;
}

int avd_submit_job(struct avd_ctx *ctx)
{
	struct avd_dev *avd = ctx->dev;
	struct avd_job *sub = &ctx->job;
	struct avd_segment *segments, *seg;
	unsigned long flags;
	size_t num;
	int i, idx = 0, vp = 0;
	void __iomem *reg;
	u32 exec_mask = sub->codec == AVD_CODEC_VP9 &&
					avd->variant->revision == 3 ?
				AVD_OP_EXEC_REV3_VP9_MASK :
				0;
	u32 exec_rev_flag =
		AVD_OP_EXEC_FLAG_START_REV4(avd->variant->revision == 4) |
		AVD_OP_EXEC_FLAG_START_REV3(avd->variant->revision == 3);

	ctx->fifo_idx = 0;
	for (i = 0; i < (int)sub->codec; i++)
		vp += avd->variant->vp_slots[i];
	if (avd->variant->vp_slot_base[sub->codec])
		vp = avd->variant->vp_slot_base[sub->codec];

	segments = sub->segments;
	num = sub->num;
	if (WARN_ON_ONCE(!segments))
		return -EINVAL;

	avd->variant->configure_stream(avd, ctx->inst.addr, ctx->fifo_idx, vp);
	reg = avd->ctrl + avd->variant->vp_slot_offset + vp * 4;

	/* the first segment is always the header (needs special handling) */

	writel(AVD_OP_EXEC | exec_mask | exec_rev_flag |
		       AVD_OP_EXEC_FIFO_IDX(ctx->fifo_idx),
	       reg);
	seg = &segments[idx++];
	for (i = 0; i < (int)seg->num; i++)
		writel(seg->instructions[i], reg);
	for (; idx <= (int)num; idx++) {
		seg = &segments[idx];
		for (i = 0; i < (int)seg->num; i++)
			writel(seg->instructions[i], reg);
		if ((!(avd->variant->quirks & AVD_QUIRK_HEVC_V5_HEADER) ||
		     sub->codec == AVD_CODEC_HEVC) &&
		    avd_wait_submission_queue(ctx, vp))
			break;
		if (idx == (int)num) {
			/* A completion from here on belongs to this job. */
			spin_lock_irqsave(&avd->job_lock, flags);
			avd->job_end_sent = true;
			spin_unlock_irqrestore(&avd->job_lock, flags);
		}
		writel(AVD_OP_EXEC | exec_mask |
			       AVD_OP_EXEC_FLAG_END(idx == (int)num),
		       reg);
	}

	if (sub->segments == segments)
		sub->segments = NULL;
	kvfree(segments);
	return 0;
}

/* ---- avd-drv.c: variants --------------------------------------------------------------- */

static const struct avd_variant avd_t8103_variant = {
	.vp_slots = {
		[AVD_CODEC_HEVC] = 2, /* no sure */
		[AVD_CODEC_H264] = 1,
		[AVD_CODEC_VP9] = 1,
	},
	.fifo_slots = 7,
	.capabilities = AVD_CAPABILITY_HEVC |
			AVD_CAPABILITY_H264 |
			AVD_CAPABILITY_VP9,
	.configure_stream = t8103_configure_stream,
	.fw_name = "apple/avd-fw-v2-t0.bin",
	.revision = 3,
	.quirks = AVD_QUIRK_LSR | AVD_QUIRK_NO_PIPE_STATE,
	.vp_slot_offset = 0x4004,
	.submit_offset = 0x4014,
	.submit_queue_max_offset = 0x4018,
	.submit_queue_status_offset = 0x402c, /* (vp slots + 1) * 4 */
};

/* the other SoCs of the Linux table are not carried: this kext is built for T8103 */

/* ---- avd-v4l2.c: formats --------------------------------------------------------------- */

static const struct avd_decoded_fmt_desc avd_decoded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.image_fmt = AVD_IMG_FMT_420_8BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_P010,
		.image_fmt = AVD_IMG_FMT_420_10BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.image_fmt = AVD_IMG_FMT_422_8BIT,
	},
	{
		/* TODO: missing P210 */
		.fourcc = V4L2_PIX_FMT_P010,
		.image_fmt = AVD_IMG_FMT_422_10BIT,
	},
};

static const struct avd_coded_fmt_desc avd_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 16384,
			.step_width = 64,
			.min_height = 64,
			.max_height = 16384,
			.step_height = 16,
		},
		.ops = &avd_hevc_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(avd_decoded_fmts),
		.decoded_fmts = avd_decoded_fmts,
		.capability = AVD_CAPABILITY_HEVC,
	},
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 64,
			.max_height = 4096,
			.step_height = 16,
		},
		.ops = &avd_h264_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(avd_decoded_fmts),
		.decoded_fmts = avd_decoded_fmts,
		.capability = AVD_CAPABILITY_H264,
	},
	{
		.fourcc = V4L2_PIX_FMT_VP9_FRAME,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 64,
			.max_height = 4096,
			.step_height = 16,
		},
		.ops = &avd_vp9_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(avd_decoded_fmts),
		.decoded_fmts = avd_decoded_fmts,
		.capability = AVD_CAPABILITY_VP9,
	},
};

/*
 * v4l2_fill_pixfmt_mp() for the three decoded formats, with the plane table of
 * drivers/media/v4l2-core/v4l2-common.c of the same tree (P010 there has vdiv 1).
 */
static int sf_fill_pixfmt_mp(struct v4l2_pix_format_mplane *pixfmt, u32 pixelformat, u32 width, u32 height)
{
	u32 bpp0, bpp1, vdiv;

	switch (pixelformat) {
	case V4L2_PIX_FMT_NV12: bpp0 = 1; bpp1 = 2; vdiv = 2; break;
	case V4L2_PIX_FMT_NV16: bpp0 = 1; bpp1 = 2; vdiv = 1; break;
	case V4L2_PIX_FMT_P010: bpp0 = 2; bpp1 = 4; vdiv = 1; break;
	default: return -EINVAL;
	}
	pixfmt->width = width;
	pixfmt->height = height;
	pixfmt->pixelformat = pixelformat;
	pixfmt->num_planes = 1;
	pixfmt->plane_fmt[0].bytesperline = width * bpp0;
	pixfmt->plane_fmt[0].sizeimage = width * bpp0 * height +
	    DIV_ROUND_UP(width, 2) * bpp1 * DIV_ROUND_UP(height, vdiv);
	return 0;
}

static bool avd_image_fmt_match(enum avd_image_fmt fmt1,
				enum avd_image_fmt fmt2)
{
	return fmt1 == fmt2 || fmt2 == AVD_IMG_FMT_ANY ||
	       fmt1 == AVD_IMG_FMT_ANY;
}

static u32 avd_enum_decoded_fmt(struct avd_ctx *ctx, int index,
				enum avd_image_fmt image_fmt)
{
	const struct avd_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	int fmt_idx = -1;
	unsigned int i;

	if (WARN_ON(!desc))
		return 0;

	for (i = 0; i < desc->num_decoded_fmts; i++) {
		if (!avd_image_fmt_match(desc->decoded_fmts[i].image_fmt,
					 image_fmt))
			continue;
		fmt_idx++;
		if (index == fmt_idx)
			return desc->decoded_fmts[i].fourcc;
	}

	return 0;
}

static void avd_fill_decoded_pixfmt(struct avd_ctx *ctx,
				    struct v4l2_pix_format_mplane *pix_mp)
{
	sf_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat, pix_mp->width,
			  pix_mp->height);

	ctx->comp.start_offset = pix_mp->plane_fmt[0].sizeimage;
	fill_comp(&ctx->comp, ctx->image_fmt, pix_mp->width, pix_mp->height);
	pix_mp->plane_fmt[0].sizeimage += ctx->comp.size;
	if (ctx->coded_fmt_desc->ops->adjust_decoded_fmt)
		ctx->coded_fmt_desc->ops->adjust_decoded_fmt(ctx, pix_mp);
}

void avd_reset_decoded_fmt(struct avd_ctx *ctx)
{
	struct v4l2_format *f = &ctx->decoded_fmt;
	u32 fourcc;

	fourcc = avd_enum_decoded_fmt(ctx, 0, ctx->image_fmt);
	memset(f, 0, sizeof(*f));
	f->fmt.pix_mp.pixelformat = fourcc;
	f->fmt.pix_mp.width = ctx->coded_fmt.fmt.pix_mp.width;
	f->fmt.pix_mp.height = ctx->coded_fmt.fmt.pix_mp.height;
	avd_fill_decoded_pixfmt(ctx, &f->fmt.pix_mp);
}

/* ---- the device ------------------------------------------------------------------------ */

struct sf_avd_device {
	struct avd_dev avd;
	struct device device;
	struct firmware fw;
	struct sf_avd_session *current;      /* the session whose job the hardware has */
	bool booted;
	unsigned int sessions;
	/* completion of the running job, written by sf_avd_irq() under job_lock */
	bool done;
	enum vb2_buffer_state done_state;
	struct sf_avd_stats stats;
};

struct sf_avd_session {
	struct sf_avd_device *dev;
	struct avd_ctx ctx;
	struct v4l2_m2m_ctx m2m;
	struct vb2_v4l2_buffer src;
	struct avd_decoded_buffer *cap[SFAVD_MAX_BUFFERS];
	struct vb2_buffer *capvb[SFAVD_MAX_BUFFERS];
	struct avd_buf capmem[SFAVD_MAX_BUFFERS];
	unsigned int ncap;
	bool started;
	bool submitted, imported;
	struct avd_buf coded;        /* the coded ("OUTPUT") buffer, mapped for the decoder as videobuf2's is */
	size_t ctrl_cap[SF_AVD_MAX_CTRLS];
};

static struct sf_avd_device *to_pd(struct avd_dev *avd)
{
	return container_of(avd, struct sf_avd_device, avd);
}

/* strstr() (the XNU kernel's libkern has strnstr only) */
static bool sf_contains(const char *text, const char *word)
{
	size_t n = strlen(word);

	for (; *text; text++)
		if (strncmp(text, word, n) == 0)
			return true;
	return false;
}

struct sf_avd_device *sf_avd_device_create(const char *compatible, void *code, void *mbox, void *ctrl,
    const uint8_t *fw, size_t fw_size)
{
	struct sf_avd_device *dev;
	struct avd_dev *avd;
	const char *codec_info = NULL;

	if (strcmp(compatible, "apple,t8103-avd") != 0)
		return NULL;
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	avd = &dev->avd;
	dev->device.name = "avd";
	avd->dev = &dev->device;
	avd->variant = &avd_t8103_variant;
	avd->code = code;
	avd->mbox = mbox;
	avd->ctrl = ctrl;
	dev->fw.data = fw;
	dev->fw.size = fw_size;
	avd->fw = &dev->fw;
	avd->job_lock.os = sf_avd_os_lock_alloc();
	if (!avd->job_lock.os) {
		kfree(dev);
		return NULL;
	}

	/* Determine firmware and HW variant capabilities */
	if (fw_size >= AVD_CODEC_INFO_MAX_SIZE)
		codec_info = (const char *)fw + fw_size - AVD_CODEC_INFO_MAX_SIZE;
	if (codec_info && strncmp(codec_info, "CODECS:", 7) == 0) {
		unsigned int caps = 0;
		unsigned int variant_caps = avd->variant->capabilities;
		char text[AVD_CODEC_INFO_MAX_SIZE + 1];

		memcpy(text, codec_info, AVD_CODEC_INFO_MAX_SIZE);
		text[AVD_CODEC_INFO_MAX_SIZE] = 0;
		if (sf_contains(text, "av1"))
			caps |= variant_caps & AVD_CAPABILITY_AV1;
		if (sf_contains(text, "h264"))
			caps |= variant_caps & AVD_CAPABILITY_H264;
		if (sf_contains(text, "h265"))
			caps |= variant_caps & AVD_CAPABILITY_HEVC;
		if (sf_contains(text, "vp9"))
			caps |= variant_caps & AVD_CAPABILITY_VP9;

		dev_info(avd->dev, "Firmware codecs: %.27s", text + 7);

		avd->capabilities = caps;
	} else {
		/* Backward compatibility to old firmware, do not upstream. */
		avd->capabilities = avd->variant->capabilities;
	}
	return dev;
}

void sf_avd_device_destroy(struct sf_avd_device *dev)
{
	if (!dev)
		return;
	if (dev->booted)
		sf_avd_device_shutdown(dev);
	sf_avd_os_lock_free(dev->avd.job_lock.os);
	kfree(dev);
}

uint32_t sf_avd_device_caps(struct sf_avd_device *dev)
{
	/* SFAVD_CAP_* are the AVD_CAPABILITY_* bits */
	return dev->avd.capabilities;
}

int sf_avd_device_boot(struct sf_avd_device *dev)
{
	int ret = avd_boot(&dev->avd);

	dev->booted = ret == 0;
	if (ret)
		dev_err(dev->avd.dev, "failed to boot: %d (flag0=0x%x)", ret,
			readl(dev->avd.mbox + AVD_REG_FLAG0_SET));
	return ret;
}

void sf_avd_device_shutdown(struct sf_avd_device *dev)
{
	avd_shutdown(&dev->avd);
	dev->booted = false;
}

void sf_avd_device_stats(struct sf_avd_device *dev, struct sf_avd_stats *out)
{
	*out = dev->stats;
}

/*
 * avd_irq_handler(): one message of mailbox 1. Runs in interrupt context in the kext:
 * MMIO, the job lock and a wake-up only. The codec's done() and everything else that
 * avd_job_finish() does runs in the thread that waits in sf_avd_session_decode().
 */
bool sf_avd_irq(struct sf_avd_device *dev)
{
	struct avd_dev *avd = &dev->avd;
	struct sf_avd_session *s;
	enum vb2_buffer_state state;
	unsigned long flags;
	bool finish = false, stale = false;
	u32 status;

	/* Keep mailbox consumption and job attribution in one critical section.
	 * A completed job's owner may publish the next job on another CPU. If it
	 * does so between this read and the ownership check, an old completion
	 * can otherwise finish the new job. Session destruction uses this lock
	 * too, so current remains valid through the VP submit callback. */
	spin_lock_irqsave(&avd->job_lock, flags);
	status = readl(avd->mbox + AVD_REG_MBOX1_RETRIEVE);

	writel(AVD_MBOX1_NOT_EMPTY, avd->mbox + AVD_REG_MBOX_IRQ_CLR);

	dev->stats.irqs++;
	dev->stats.last_status = status;

	if (status & 0x10000) { /* dbg */
		dev->stats.unhandled++;
		writel_relaxed(0, avd->mbox + AVD_REG_MBOX_IRQ_ENABLE);
		spin_unlock_irqrestore(&avd->job_lock, flags);
		return true;
	}

	s = dev->current;
	if (status & 0x1000) {
		/* pp is done ! we are done */
		state = VB2_BUF_STATE_DONE;
	} else if (status & 0x100) {
		/* a vp is done, kick the pp and hope for the best */
		dev->stats.vp_done++;
		if (s && s->ctx.coded_fmt_desc->ops->submit)
			s->ctx.coded_fmt_desc->ops->submit(&s->ctx);
		spin_unlock_irqrestore(&avd->job_lock, flags);
		return true;
	} else {
		/* let watchdog handle */
		dev->stats.errors++;
		spin_unlock_irqrestore(&avd->job_lock, flags);
		return true;
	}

	/*
	 * Finish only a job the hardware owns. While the job is still being set up
	 * or submitted, a completion is left over from an older job unless this
	 * job's END command already went out.
	 */
	if (!s || avd->job_ctx != &s->ctx) {
		stale = true;
	} else if (avd->job_state == AVD_JOB_SUBMITTED) {
		avd->job_state = AVD_JOB_FINISHING;
		finish = true;
	} else if (avd->job_state == AVD_JOB_RUNNING && avd->job_end_sent) {
		avd->job_pending = true;
		avd->job_pending_result = state;
	} else {
		stale = true;
	}
	if (finish) {
		dev->done = true;
		dev->done_state = state;
	}
	if (stale)
		dev->stats.stale++;
	spin_unlock_irqrestore(&avd->job_lock, flags);
	if (finish)
		sf_avd_os_job_wake(dev);
	return true;
}

/* avd_reset() */
static int avd_reset(struct sf_avd_device *dev)
{
	int ret;

	ret = sf_avd_os_reset(dev);
	if (ret)
		dev_err(dev->avd.dev, "reset: failed: %d", ret);

	ret = avd_boot(&dev->avd);
	if (ret)
		dev_err(dev->avd.dev, "reset: failed to boot");
	dev->booted = ret == 0;
	return ret;
}

/* ---- avd-v4l2.c: per-run glue ---------------------------------------------------------- */

void avd_job_finish_no_pm(struct avd_ctx *ctx, enum vb2_buffer_state result)
{
	struct avd_dev *avd = ctx->dev;
	unsigned long flags;

	spin_lock_irqsave(&avd->job_lock, flags);
	if (avd->job_ctx == ctx) {
		avd->job_state = AVD_JOB_IDLE;
		avd->job_ctx = NULL;
		avd->job_pending = false;
	}
	spin_unlock_irqrestore(&avd->job_lock, flags);

	if (ctx->coded_fmt_desc->ops->done) {
		struct vb2_v4l2_buffer *src_buf, *dst_buf;

		src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
		ctx->coded_fmt_desc->ops->done(ctx, src_buf, dst_buf, result);
	}
	/* v4l2_m2m_buf_done_and_job_finish(): the session records the result */
	ctx->fh.m2m_ctx->sf_result = result;
	ctx->fh.m2m_ctx->sf_finished = true;
}

void avd_job_finish(struct avd_ctx *ctx, enum vb2_buffer_state result)
{
	avd_job_finish_no_pm(ctx, result);
}

void avd_run_preamble(struct avd_ctx *ctx, struct avd_run *run)
{
	struct avd_decoded_buffer *dst;

	memset(run, 0, sizeof(*run));

	run->bufs.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run->bufs.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	run->coded_in =
		vb2_dma_contig_plane_dma_addr(&run->bufs.src->vb2_buf, 0);
	run->y_out = vb2_dma_contig_plane_dma_addr(&run->bufs.dst->vb2_buf, 0);
	run->uv_out = run->y_out +
		      ctx->decoded_fmt.fmt.pix_mp.plane_fmt[0].bytesperline *
			      ctx->decoded_fmt.fmt.pix_mp.height;

	run->comp_out = run->y_out + ctx->comp.start_offset;
	ctx->decomp = true;

	dst = vb2_to_avd_decoded_buf(&run->bufs.dst->vb2_buf);
	memcpy(&dst->comp, &ctx->comp, sizeof(ctx->comp));

	/* the controls were applied by sf_avd_session_set_ctrl() */

	/* v4l2_m2m_buf_copy_metadata() */
	run->bufs.dst->vb2_buf.timestamp = run->bufs.src->vb2_buf.timestamp;
	run->bufs.dst->vb2_buf.copied_timestamp = true;
}

void avd_run_postamble(struct avd_ctx *ctx, struct avd_run *run)
{
	(void)ctx;
	(void)run;
}

/* ---- sessions -------------------------------------------------------------------------- */

static const struct avd_coded_fmt_desc *find_coded(struct sf_avd_device *dev, u32 fourcc)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(avd_coded_fmts); i++)
		if (avd_coded_fmts[i].fourcc == fourcc &&
		    (dev->avd.capabilities & avd_coded_fmts[i].capability))
			return &avd_coded_fmts[i];
	return NULL;
}

int sf_avd_session_create(struct sf_avd_device *dev, struct sf_avd_session **out)
{
	struct sf_avd_session *s;
	int ret;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = dev;
	s->ctx.dev = &dev->avd;
	s->ctx.fh.m2m_ctx = &s->m2m;
	s->m2m.cap_q_ctx.q.bufs = s->capvb;
	s->m2m.sf_src = &s->src;

	/* avd_open() */
	ret = avd_buf_alloc(&dev->avd, &s->ctx.inst, fifo_size());
	if (ret) {
		kfree(s);
		return ret;
	}
	if (dev->sessions == 0 && !dev->booted) {
		ret = sf_avd_device_boot(dev);
		if (ret) {
			avd_buf_free(&dev->avd, &s->ctx.inst);
			kfree(s);
			return ret;
		}
	}
	dev->sessions++;
	*out = s;
	return 0;
}

void sf_avd_session_destroy(struct sf_avd_session *s)
{
	struct sf_avd_device *dev;
	unsigned long flags;

	if (!s)
		return;
	dev = s->dev;
	spin_lock_irqsave(&dev->avd.job_lock, flags);
	if (dev->current == s)
		dev->current = NULL;
	spin_unlock_irqrestore(&dev->avd.job_lock, flags);

	/* avd_stop_streaming() */
	if (s->started && s->ctx.coded_fmt_desc->ops->stop)
		s->ctx.coded_fmt_desc->ops->stop(&s->ctx);
	kvfree(s->ctx.job.segments);
	s->ctx.job.segments = NULL;

	for (unsigned int i = 0; i < s->ncap; i++) {
		avd_buf_free(&dev->avd, &s->capmem[i]);
		kfree(s->cap[i]);
	}
	for (unsigned int i = 0; i < s->ctx.ctrl_hdl.num; i++)
		kfree(s->ctx.ctrl_hdl.ctrls[i].p_cur.p);
	avd_buf_free(&dev->avd, &s->coded);
	/* avd_release() */
	avd_buf_free(&dev->avd, &s->ctx.inst);
	kfree(s);

	if (dev->sessions > 0 && --dev->sessions == 0 && dev->booted)
		sf_avd_device_shutdown(dev);
}

/* element size of a control, 0 for an unknown one; *array when several may be passed */
static size_t ctrl_elem_size(u32 id, bool *array)
{
	*array = false;
	switch (id) {
	case V4L2_CID_STATELESS_H264_SPS: return sizeof(struct v4l2_ctrl_h264_sps);
	case V4L2_CID_STATELESS_H264_PPS: return sizeof(struct v4l2_ctrl_h264_pps);
	case V4L2_CID_STATELESS_H264_SCALING_MATRIX: return sizeof(struct v4l2_ctrl_h264_scaling_matrix);
	case V4L2_CID_STATELESS_H264_PRED_WEIGHTS: return sizeof(struct v4l2_ctrl_h264_pred_weights);
	case V4L2_CID_STATELESS_H264_SLICE_PARAMS: return sizeof(struct v4l2_ctrl_h264_slice_params);
	case V4L2_CID_STATELESS_H264_DECODE_PARAMS: return sizeof(struct v4l2_ctrl_h264_decode_params);
	case V4L2_CID_STATELESS_HEVC_SPS: return sizeof(struct v4l2_ctrl_hevc_sps);
	case V4L2_CID_STATELESS_HEVC_PPS: return sizeof(struct v4l2_ctrl_hevc_pps);
	case V4L2_CID_STATELESS_HEVC_SLICE_PARAMS: *array = true; return sizeof(struct v4l2_ctrl_hevc_slice_params);
	case V4L2_CID_STATELESS_HEVC_SCALING_MATRIX: return sizeof(struct v4l2_ctrl_hevc_scaling_matrix);
	case V4L2_CID_STATELESS_HEVC_DECODE_PARAMS: return sizeof(struct v4l2_ctrl_hevc_decode_params);
	case V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS: *array = true; return sizeof(u32);
	case V4L2_CID_STATELESS_VP9_FRAME: return sizeof(struct v4l2_ctrl_vp9_frame);
	case V4L2_CID_STATELESS_VP9_COMPRESSED_HDR: return sizeof(struct v4l2_ctrl_vp9_compressed_hdr);
	default: return 0;
	}
}

/* the limits the V4L2 control framework gives the two array controls (avd-v4l2.c) */
static size_t ctrl_max_elems(u32 id)
{
	switch (id) {
	case V4L2_CID_STATELESS_HEVC_SLICE_PARAMS: return 600;
	case V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS: return 256;
	default: return 1;
	}
}

int sf_avd_session_set_ctrl(struct sf_avd_session *s, uint32_t id, const void *data, size_t size)
{
	struct v4l2_ctrl_handler *h = &s->ctx.ctrl_hdl;
	const struct avd_coded_fmt_desc *desc = s->ctx.coded_fmt_desc;
	struct v4l2_ctrl *ctrl = NULL;
	bool array;
	size_t elem = ctrl_elem_size(id, &array);
	unsigned int i;

	if (elem == 0 || size == 0 || size % elem != 0 || size / elem > ctrl_max_elems(id))
		return -EINVAL;
	for (i = 0; i < h->num; i++)
		if (h->ctrls[i].id == id)
			ctrl = &h->ctrls[i];
	if (!ctrl) {
		if (h->num == SF_AVD_MAX_CTRLS)
			return -EINVAL;
		i = h->num;
		ctrl = &h->ctrls[i];
		memset(ctrl, 0, sizeof(*ctrl));
		ctrl->handler = h;
		ctrl->id = id;
		ctrl->elem_size = (u32)elem;
		s->ctrl_cap[i] = 0;
		h->num++;
	} else {
		i = (unsigned int)(ctrl - h->ctrls);
	}
	if (s->ctrl_cap[i] < size) {
		/* an array control gets its largest size at once */
		size_t want = array ? elem * ctrl_max_elems(id) : size;
		void *p = kzalloc(want, GFP_KERNEL);

		if (!p)
			return -ENOMEM;
		kfree(ctrl->p_cur.p);
		ctrl->p_cur.p = p;
		s->ctrl_cap[i] = want;
	}
	memcpy(ctrl->p_cur.p, data, size);
	ctrl->p_new = ctrl->p_cur;
	ctrl->elems = ctrl->new_elems = (u32)(size / elem);
	if (id == V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS) {
		/* in Linux a fixed array of 256 (avd-v4l2.c): what was not given is zero */
		memset((u8 *)ctrl->p_cur.p + size, 0, s->ctrl_cap[i] - size);
		ctrl->elems = ctrl->new_elems = (u32)ctrl_max_elems(id);
	}

	/* avd_try_ctrl() */
	if (desc && desc->ops->try_ctrl)
		return desc->ops->try_ctrl(&s->ctx, ctrl);
	return 0;
}

int sf_avd_session_set_format(struct sf_avd_session *s, uint32_t codec, uint32_t width, uint32_t height,
    uint32_t ctrl_id, const void *ctrl_data, size_t ctrl_size, struct sfavd_format *fmt)
{
	struct avd_ctx *ctx = &s->ctx;
	const struct avd_coded_fmt_desc *desc;
	struct v4l2_pix_format_mplane *pix;
	struct v4l2_ctrl *ctrl;
	enum avd_image_fmt image_fmt;
	int ret;

	if (s->ncap || s->started)
		return -EBUSY;
	desc = find_coded(s->dev, codec);
	if (!desc)
		return -EINVAL;
	if (width == 0 || height == 0 || width > desc->frmsize.max_width || height > desc->frmsize.max_height)
		return -EINVAL;

	/* avd_s_output_fmt(): v4l2_apply_frmsize_constraints() */
	width = round_up(clamp(width, desc->frmsize.min_width, desc->frmsize.max_width), desc->frmsize.step_width);
	height = round_up(clamp(height, desc->frmsize.min_height, desc->frmsize.max_height), desc->frmsize.step_height);
	ctx->coded_fmt_desc = desc;
	memset(&ctx->coded_fmt, 0, sizeof(ctx->coded_fmt));
	ctx->coded_fmt.fmt.pix_mp.pixelformat = codec;
	ctx->coded_fmt.fmt.pix_mp.width = width;
	ctx->coded_fmt.fmt.pix_mp.height = height;
	ctx->coded_fmt.fmt.pix_mp.num_planes = 1;
	ctx->image_fmt = AVD_IMG_FMT_ANY;
	avd_reset_decoded_fmt(ctx);

	if (codec == V4L2_PIX_FMT_HEVC_SLICE) {
		/* a control that always exists in Linux; the codec refuses to run without it */
		static const u32 none[1];

		ret = sf_avd_session_set_ctrl(s, V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS, none, sizeof(none));
		if (ret)
			return ret;
	}

	/* the SPS / frame header: avd_s_ctrl() */
	ret = sf_avd_session_set_ctrl(s, ctrl_id, ctrl_data, ctrl_size);
	if (ret)
		return ret;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, ctrl_id);
	if (!ctrl || !desc->ops->get_image_fmt)
		return -EINVAL;
	image_fmt = desc->ops->get_image_fmt(ctx, ctrl);
	if (image_fmt == AVD_IMG_FMT_ANY)
		return -EINVAL;
	ctx->image_fmt = image_fmt;
	avd_reset_decoded_fmt(ctx);

	pix = &ctx->decoded_fmt.fmt.pix_mp;
	if (pix->pixelformat == 0)
		return -EINVAL;
	fmt->pixelformat = pix->pixelformat;
	fmt->width = pix->width;
	fmt->height = pix->height;
	fmt->bytesperline = pix->plane_fmt[0].bytesperline;
	fmt->uv_offset = pix->plane_fmt[0].bytesperline * pix->height;
	fmt->sizeimage = pix->plane_fmt[0].sizeimage;
	return 0;
}

int sf_avd_session_alloc(struct sf_avd_session *s, unsigned count)
{
	struct avd_ctx *ctx = &s->ctx;
	u32 sizeimage = ctx->decoded_fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	int ret;

	if (!ctx->coded_fmt_desc || sizeimage == 0 || s->ncap || count == 0 || count > SFAVD_MAX_BUFFERS)
		return -EINVAL;
	for (unsigned int i = 0; i < count; i++) {
		struct avd_decoded_buffer *db = kzalloc(sizeof(*db), GFP_KERNEL);
		struct vb2_buffer *vb;

		if (!db)
			return -ENOMEM;
		ret = avd_buf_alloc(&s->dev->avd, &s->capmem[i], sizeimage);
		if (ret) {
			kfree(db);
			return ret;
		}
		vb = &db->base.vb.vb2_buf;
		vb->vb2_queue = &s->m2m.cap_q_ctx.q;
		vb->index = i;
		vb->planes[0].length = sizeimage;
		vb->planes[0].bytesused = sizeimage;
		db->base.vb.planes[0] = vb->planes[0];
		vb->sf_dma = s->capmem[i].addr;
		vb->sf_vaddr = s->capmem[i].cpu;
		s->cap[i] = db;
		s->capvb[i] = vb;
		s->ncap = i + 1;
		s->m2m.cap_q_ctx.q.num = s->ncap;
	}

	/* avd_start_streaming() */
	if (ctx->coded_fmt_desc->ops->start) {
		ret = ctx->coded_fmt_desc->ops->start(ctx);
		if (ret)
			return ret;
	}
	s->started = true;
	return 0;
}

int sf_avd_session_buffer(struct sf_avd_session *s, unsigned index, void **vaddr, size_t *size, void **cookie)
{
	if (index >= s->ncap)
		return -EINVAL;
	*vaddr = s->capmem[index].cpu;
	*size = s->capmem[index].size;
	*cookie = NULL;
	for (unsigned int slot = 0; slot < SF_AVD_MAX_ALLOCS; slot++)
		if (sf_allocs[slot].cpu == s->capmem[index].cpu)
			*cookie = sf_allocs[slot].cookie;
	return *cookie ? 0 : -EINVAL;
}

int sf_avd_session_import(struct sf_avd_session *s, unsigned count,
    void *const *cpu, const uint64_t *iova, void *const *cookies, void **old_cookies)
{
    unsigned slots[SFAVD_MAX_BUFFERS];
    if (!s || !count || count != s->ncap || count > SFAVD_MAX_BUFFERS ||
        !cpu || !iova || !cookies || !old_cookies)
        return -EINVAL;
    if (s->submitted || s->imported) return -EBUSY;
    /* Complete preflight: no allocation or possible failure after mutation. */
    for (unsigned i=0;i<count;i++) {
        if (!cpu[i] || !cookies[i] || !iova[i] || (iova[i]&16383)) return -EINVAL;
        for (unsigned j=0;j<i;j++)
            if (cpu[i]==cpu[j] || iova[i]==iova[j] || cookies[i]==cookies[j]) return -EINVAL;
        unsigned slot;
        for (slot=0;slot<SF_AVD_MAX_ALLOCS;slot++) {
            if (sf_allocs[slot].cpu==cpu[i]) return -EINVAL;
        }
        /* Re-find the old record rather than rely on an uninitialized slot. */
        for (slot=0;slot<SF_AVD_MAX_ALLOCS;slot++)
            if (sf_allocs[slot].cpu==s->capmem[i].cpu) break;
        if (slot==SF_AVD_MAX_ALLOCS || !sf_allocs[slot].cookie) return -EINVAL;
        slots[i]=slot;
    }
    for (unsigned i=0;i<count;i++) {
        old_cookies[i]=sf_allocs[slots[i]].cookie;
        sf_allocs[slots[i]].cpu=cpu[i];sf_allocs[slots[i]].cookie=cookies[i];
        s->capmem[i].cpu=cpu[i];s->capmem[i].addr=iova[i];
        s->capvb[i]->sf_vaddr=cpu[i];s->capvb[i]->sf_dma=iova[i];
    }
    s->imported=true;
    return 0;
}

void *sf_avd_session_coded(struct sf_avd_session *s, size_t size)
{
	if (size == 0 || size > (64u << 20))
		return NULL;
	if (s->coded.size < size) {
		/* in steps of 1 MiB, so that a stream settles on one buffer */
		if (avd_buf_alloc(&s->dev->avd, &s->coded, ALIGN(size, 1u << 20)))
			return NULL;
	}
	return s->coded.cpu;
}

/* avd_buf_finish(): 4:0:0 has no chroma to decode; the picture is grey */
static void grey_chroma(struct sf_avd_session *s, struct avd_decoded_buffer *buf)
{
	size_t luma = buf->grey_chroma_offset;
	u8 *y = buf->base.vb.vb2_buf.sf_vaddr;

	buf->grey_chroma_offset = 0;
	(void)s;
	if (!luma || !y)
		return;
	if (buf->grey_chroma_10bit) {
		/* P010: 16-bit little-endian samples, 512 << 6. */
		for (size_t i = 0; i + 1 < luma / 2; i += 2) {
			y[luma + i] = 0x00;
			y[luma + i + 1] = 0x80;
		}
	} else {
		memset(y + luma, 0x80, luma / 2);
	}
}

/* avd_device_run() and the wait that v4l2-mem2mem and the interrupt do between them */
int sf_avd_session_decode(struct sf_avd_session *s, size_t coded_size, unsigned dst, uint64_t timestamp,
    uint32_t flags_in, uint32_t *status)
{
	struct sf_avd_device *dev = s->dev;
	struct avd_dev *avd = &dev->avd;
	struct avd_ctx *ctx = &s->ctx;
	const struct avd_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	enum vb2_buffer_state result = VB2_BUF_STATE_ERROR;
	unsigned long flags;
	bool finish = false, wait = false;
	u64 seq, t0;
	int ret;

	if (status)
		*status = 0;
	if (!desc || !s->started || dst >= s->ncap || !s->coded.cpu || coded_size == 0 || coded_size > s->coded.size)
		return -EINVAL;
	if (!dev->booted)
		return -ENODEV;

	s->submitted = true; /* freezes capture backing even for a HOLD slice */
	/* the source buffer of this run */
	memset(&s->src, 0, sizeof(s->src));
	s->src.vb2_buf.timestamp = timestamp;
	s->src.vb2_buf.copied_timestamp = true;
	s->src.vb2_buf.planes[0].length = (unsigned int)s->coded.size;
	s->src.vb2_buf.planes[0].bytesused = (unsigned int)coded_size;
	s->src.vb2_buf.sf_vaddr = s->coded.cpu;
	s->src.vb2_buf.sf_dma = s->coded.addr;
	s->src.planes[0] = s->src.vb2_buf.planes[0];
	s->src.flags = (flags_in & SFAVD_DECODE_HOLD) ? V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF : 0;
	s->m2m.sf_dst = &s->cap[dst]->base.vb;
	s->m2m.sf_finished = false;
	s->cap[dst]->grey_chroma_offset = 0;

	spin_lock_irqsave(&avd->job_lock, flags);
	seq = ++avd->job_seq;
	avd->job_ctx = ctx;
	avd->job_state = AVD_JOB_RUNNING;
	avd->job_end_sent = false;
	avd->job_pending = false;
	dev->current = s;
	dev->done = false;
	spin_unlock_irqrestore(&avd->job_lock, flags);
	dev->stats.jobs++;
	t0 = sf_avd_os_now_us();

	ret = desc->ops->run(ctx);
	if (ret) {
		if (!s->m2m.sf_finished)
			avd_job_finish(ctx, VB2_BUF_STATE_ERROR);
		dev->stats.errors++;
		return ret;
	}

	/*
	 * The run either finished the job itself (held capture buffer) or handed
	 * it to the hardware: wait for it, or finish right away if the
	 * completion already arrived.
	 */
	spin_lock_irqsave(&avd->job_lock, flags);
	if (avd->job_seq == seq && avd->job_state == AVD_JOB_RUNNING) {
		if (avd->job_pending) {
			avd->job_state = AVD_JOB_FINISHING;
			result = avd->job_pending_result;
			finish = true;
		} else {
			avd->job_state = AVD_JOB_SUBMITTED;
			wait = true;
		}
	}
	spin_unlock_irqrestore(&avd->job_lock, flags);

	if (wait) {
		/* the watchdog: 2 s, as Linux arms it */
		bool woken;

		for (;;) {
			woken = sf_avd_os_job_wait(dev, 2000);
			spin_lock_irqsave(&avd->job_lock, flags);
			if (dev->done) {
				result = dev->done_state;
				finish = true;
			} else if (!woken && avd->job_state == AVD_JOB_SUBMITTED) {
				avd->job_state = AVD_JOB_FINISHING;
			}
			spin_unlock_irqrestore(&avd->job_lock, flags);
			if (finish || !woken)
				break;
		}
		if (!finish) {
			/* avd_watchdog_func() */
			dev_err(avd->dev, "Frame processing timed out!");
			avd_status(avd, 0);
			avd_status(avd, 1);
			avd_status(avd, 2);
			dev->stats.timeouts++;
			writel(0, avd->mbox + AVD_REG_MBOX_IRQ_ENABLE);
			ret = avd_reset(dev);
			if (ret)
				dev_err(avd->dev, "failed to reset: %d", ret);
			avd_job_finish(ctx, VB2_BUF_STATE_ERROR);
			if (status)
				*status = dev->stats.last_status;
			return -ETIMEDOUT;
		}
	}

	if (finish)
		avd_job_finish(ctx, result);
	if (status)
		*status = dev->stats.last_status;

	if (s->m2m.sf_finished && s->m2m.sf_result == VB2_BUF_STATE_DONE) {
		if (!(flags_in & SFAVD_DECODE_HOLD)) {
			u64 us = sf_avd_os_now_us() - t0;

			grey_chroma(s, s->cap[dst]);
			dev->stats.done++;
			dev->stats.last_decode_us = us;
			dev->stats.total_decode_us += us;
			if (us > dev->stats.max_decode_us)
				dev->stats.max_decode_us = us;
		}
		return 0;
	}
	dev->stats.errors++;
	return -EIO;
}

/* ---- lib/sort.c stand-in (the reference list builder sorts at most 32 entries) --------- */

void sort_r(void *base, size_t num, size_t size, int (*cmp)(const void *, const void *, const void *),
    void (*swap_fn)(void *, void *, int, const void *), const void *priv)
{
	u8 tmp[64];
	u8 *b = base;

	(void)swap_fn;
	if (size > sizeof(tmp))
		return;
	for (size_t i = 1; i < num; i++) {
		memcpy(tmp, b + i * size, size);
		size_t j = i;
		while (j > 0 && cmp(b + (j - 1) * size, tmp, priv) > 0) {
			memcpy(b + j * size, b + (j - 1) * size, size);
			j--;
		}
		memcpy(b + j * size, tmp, size);
	}
}

struct v4l2_fh *file_to_v4l2_fh(struct file *filp)
{
	(void)filp;
	return NULL;
}
