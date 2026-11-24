/* SPDX-License-Identifier: MIT */
/*
 * /dev/sfavd: the Apple Video Decoder of Apple Silicon as a stateless decoder.
 *
 * The model is Linux's V4L2 stateless ("request") decoder interface, without V4L2: the
 * caller parses the bitstream and passes, per picture, the codec's parameter structures
 * (the `struct v4l2_ctrl_*` of <sfavd/v4l2-controls.h>, the layout Linux uses, identified
 * by their V4L2_CID_STATELESS_* numbers) together with the coded data and the index of the
 * decoded buffer to write. Reference pictures are named by the timestamp of the decode
 * that produced them, as in V4L2.
 *
 * A context is created per stream. Decoded buffers belong to the context, live in the
 * kernel (mapped for the decoder's DART) and are mapped read-only into the caller.
 */
#ifndef _SFAVD_IOCTL_H
#define _SFAVD_IOCTL_H

#include <stdint.h>
#ifdef __APPLE__
#include <sys/ioccom.h>
#else
#include <sys/ioctl.h>   /* the host tests only use the structures */
#endif

#define SFAVD_ABI_VERSION 1

#define SFAVD_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
/* coded formats (the V4L2 fourccs of the stateless formats) */
#define SFAVD_CODEC_H264 SFAVD_FOURCC('S', '2', '6', '4')
#define SFAVD_CODEC_HEVC SFAVD_FOURCC('S', '2', '6', '5')
#define SFAVD_CODEC_VP9  SFAVD_FOURCC('V', 'P', '9', 'F')
/* decoded formats */
#define SFAVD_PIX_NV12   SFAVD_FOURCC('N', 'V', '1', '2')   /* 8 bit 4:2:0, Y then interleaved CbCr */
#define SFAVD_PIX_NV16   SFAVD_FOURCC('N', 'V', '1', '6')   /* 8 bit 4:2:2 */
#define SFAVD_PIX_P010   SFAVD_FOURCC('P', '0', '1', '0')   /* 10 bit in 16-bit little-endian words, high bits */

#define SFAVD_CAP_HEVC (1u << 0)
#define SFAVD_CAP_H264 (1u << 1)
#define SFAVD_CAP_VP9  (1u << 2)
#define SFAVD_CAP_AV1  (1u << 3)
/* Optional ABI1 extension: atomic user capture-buffer import before MAP/decode. */
#define SFAVD_CAP_EXTERNAL_BUFFERS (1u << 16)

#define SFAVD_MAX_BUFFERS 32
#define SFAVD_MAX_CTRLS   8

struct sfavd_info {
	uint32_t abi;            /* SFAVD_ABI_VERSION */
	uint32_t caps;           /* SFAVD_CAP_* */
	uint32_t booted;         /* the decoder's firmware is running */
	uint32_t contexts;       /* contexts in use */
	char     firmware[48];   /* the "CODECS:" text of the firmware image, if any */
};

struct sfavd_format {
	uint32_t pixelformat;    /* SFAVD_PIX_* */
	uint32_t width, height;  /* of the decoded buffer: coded size rounded up (64 x 16) */
	uint32_t bytesperline;   /* luma stride; the chroma plane has the same stride */
	uint32_t uv_offset;      /* offset of the CbCr plane in the buffer */
	uint32_t sizeimage;      /* bytes per decoded buffer, including the decoder's private tail */
};

struct sfavd_ctrl {
	uint32_t id;             /* V4L2_CID_STATELESS_* */
	uint32_t size;           /* bytes; a multiple of the element size for array controls */
	uint64_t ptr;            /* user address */
};

struct sfavd_create {
	uint32_t          codec;        /* in: SFAVD_CODEC_* */
	uint32_t          width, height;/* in: coded size in luma samples */
	uint32_t          buffers;      /* in: decoded buffers to allocate (<= SFAVD_MAX_BUFFERS) */
	struct sfavd_ctrl format_ctrl;  /* in: the SPS (H.264, HEVC) or the VP9 frame header */
	uint32_t          ctx;          /* out: context number */
	struct sfavd_format format;     /* out */
};

struct sfavd_map {
	uint32_t ctx;
	uint32_t index;          /* decoded buffer */
	uint64_t address;        /* out: read-only mapping in the caller, valid until the context is destroyed */
	uint64_t size;           /* out */
};

#define SFAVD_DECODE_HOLD (1u << 0)   /* more slices of this picture follow: queue only */

struct sfavd_decode {
	uint32_t          ctx;
	uint32_t          flags;        /* SFAVD_DECODE_* */
	uint32_t          dst;          /* decoded buffer to write */
	uint32_t          nctrls;
	uint64_t          timestamp;    /* names the picture for later references */
	uint64_t          data;         /* user address of the coded data (one slice for H.264) */
	uint32_t          size;
	uint32_t          status;       /* out: last mailbox word of the decoder */
	uint64_t          decode_us;    /* out: time from submission to completion */
	struct sfavd_ctrl ctrls[SFAVD_MAX_CTRLS];
};

/* Import the complete capture set. Addresses belong to the calling task and
 * must be writable, 16KiB-aligned mappings retained until context destruction.
 * size includes the driver's private decoded tail, rounded to16KiB. This is
 * an optional ioctl: unsupported kernels return ENOTTY, never a fake import. */
struct sfavd_import_buffer {
    uint64_t address;
    uint64_t size;
};
struct sfavd_import {
    uint32_t ctx;
    uint32_t count;
    uint32_t flags;       /* must be0 */
    uint32_t reserved;    /* must be0 */
    struct sfavd_import_buffer buffers[SFAVD_MAX_BUFFERS];
};

struct sfavd_destroy {
	uint32_t ctx;
};

#define SFAVD_IOC_INFO    _IOR('V', 1, struct sfavd_info)
#define SFAVD_IOC_CREATE  _IOWR('V', 2, struct sfavd_create)
#define SFAVD_IOC_MAP     _IOWR('V', 3, struct sfavd_map)
#define SFAVD_IOC_DECODE  _IOWR('V', 4, struct sfavd_decode)
#define SFAVD_IOC_DESTROY _IOW('V', 5, struct sfavd_destroy)
#define SFAVD_IOC_IMPORT  _IOW('V', 6, struct sfavd_import)

#endif
