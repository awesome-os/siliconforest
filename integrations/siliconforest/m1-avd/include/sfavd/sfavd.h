/* SPDX-License-Identifier: MIT */
/*
 * libsfavd: access to the Apple Video Decoder through /dev/sfavd (SFAppleAVD.kext).
 *
 * The decoder is stateless (see <sfavd/sfavd_ioctl.h>): the caller parses the bitstream.
 * Programs normally do not use this library directly but FFmpeg's `sfavd` hwaccel, which
 * is built on it and does the parsing, reference handling and reordering.
 *
 * A device name of the form "record:<file>" does not open the decoder: every call is
 * written to <file> instead (decoded buffers are plain zeroed memory), so that a decode
 * session can be captured on any machine and replayed against the driver's host model or
 * against the real decoder (tools sfavd-host and sfavd-replay). The environment variable
 * SFAVD_DEVICE overrides the default name "/dev/sfavd".
 */
#ifndef _SFAVD_H
#define _SFAVD_H

#include <stddef.h>
#include <stdint.h>

#include "sfavd_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sfavd_dev sfavd_dev;

/* What the caller knows about a picture and the decoder does not; kept with recordings. */
struct sfavd_meta {
	int64_t  pts;             /* presentation time, in the caller's units */
	uint32_t width, height;   /* visible size (top-left aligned crop of the decoded buffer) */
	uint32_t crop_left, crop_top;
	uint32_t flags;           /* SFAVD_META_* */
	uint32_t reserved;
};
#define SFAVD_META_KEY (1u << 0)

/* NULL: $SFAVD_DEVICE, else /dev/sfavd. NULL with errno set on failure. */
sfavd_dev *sfavd_open(const char *name);
void sfavd_close(sfavd_dev *dev);
int  sfavd_info(sfavd_dev *dev, struct sfavd_info *info);
/* The calls below return 0, or -1 with errno set. */
int  sfavd_create(sfavd_dev *dev, struct sfavd_create *create);
/* Read-only view of decoded buffer `index`: Y plane at 0, CbCr at format.uv_offset. */
int  sfavd_map(sfavd_dev *dev, uint32_t ctx, uint32_t index, const uint8_t **address, size_t *size);
/* `meta` may be NULL; it should be given with the last (non-HOLD) call of a picture. */
int  sfavd_decode(sfavd_dev *dev, struct sfavd_decode *decode, const struct sfavd_meta *meta);
/* Optional atomic import. Caller mappings live until DESTROY. Recording mode
 * refuses ENOTSUP; older kernels refuse ENOTTY. Query the optional cap first. */
int  sfavd_import_buffers(sfavd_dev *dev, uint32_t ctx,
                         const struct sfavd_import_buffer *buffers, uint32_t count);
int  sfavd_destroy(sfavd_dev *dev, uint32_t ctx);

/* ---- recordings ----------------------------------------------------------------------- */
#define SFAVD_REC_MAGIC   0x4a414450u   /* "PDAJ" */
enum { SFAVD_REC_CREATE = 1, SFAVD_REC_DECODE = 2, SFAVD_REC_DESTROY = 3 };
/*
 * A recording is a sequence of records: { u32 magic, u32 type, u32 length } then `length`
 * bytes:
 *   CREATE:  struct sfavd_create, then the bytes of its format control
 *   DECODE:  struct sfavd_decode, struct sfavd_meta, then for each control its bytes, then
 *            the coded data
 *   DESTROY: struct sfavd_destroy
 * (the user addresses inside the structures are meaningless in a recording).
 */

#ifdef __cplusplus
}
#endif
#endif
