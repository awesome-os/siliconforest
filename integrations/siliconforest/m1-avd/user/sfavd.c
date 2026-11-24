// SPDX-License-Identifier: MIT
/* libsfavd: see <sfavd/sfavd.h>. */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <sfavd/sfavd.h>
#include <sfavd/sfavd_import_validate.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

struct rec_ctx {
	int      used;
	uint32_t nbuf;
	size_t   size;
	uint64_t pictures;        /* complete (non-HOLD) decodes so far */
	uint8_t *buf[SFAVD_MAX_BUFFERS];
};

struct sfavd_dev {
	int   fd;                 /* the device, or -1 when recording */
	FILE *rec;
	struct rec_ctx ctx[16];
};

sfavd_dev *
sfavd_open(const char *name)
{
	sfavd_dev *dev;

	if (name == NULL)
		name = getenv("SFAVD_DEVICE");
	if (name == NULL || name[0] == 0)
		name = "/dev/sfavd";
	dev = calloc(1, sizeof(*dev));
	if (dev == NULL)
		return NULL;
	dev->fd = -1;
	if (strncmp(name, "record:", 7) == 0) {
		dev->rec = fopen(name + 7, "wb");
		if (dev->rec == NULL) {
			free(dev);
			return NULL;
		}
		return dev;
	}
	dev->fd = open(name, O_RDWR | O_CLOEXEC);
	if (dev->fd < 0) {
		free(dev);
		return NULL;
	}
	return dev;
}

void
sfavd_close(sfavd_dev *dev)
{
	if (dev == NULL)
		return;
	if (dev->rec != NULL) {
		for (unsigned c = 0; c < 16; c++)
			for (unsigned i = 0; i < SFAVD_MAX_BUFFERS; i++)
				free(dev->ctx[c].buf[i]);
		fclose(dev->rec);
	}
	if (dev->fd >= 0)
		close(dev->fd);
	free(dev);
}

static int
rec_head(sfavd_dev *dev, uint32_t type, uint32_t length)
{
	uint32_t head[3] = { SFAVD_REC_MAGIC, type, length };

	return fwrite(head, sizeof(head), 1, dev->rec) == 1 ? 0 : -1;
}

static int
rec_bytes(sfavd_dev *dev, const void *p, size_t n)
{
	return n == 0 || fwrite(p, n, 1, dev->rec) == 1 ? 0 : -1;
}

int
sfavd_info(sfavd_dev *dev, struct sfavd_info *info)
{
	memset(info, 0, sizeof(*info));
	if (dev->rec != NULL) {
		info->abi = SFAVD_ABI_VERSION;
		info->caps = SFAVD_CAP_H264 | SFAVD_CAP_HEVC | SFAVD_CAP_VP9;
		snprintf(info->firmware, sizeof(info->firmware), "recording");
		return 0;
	}
	return ioctl(dev->fd, SFAVD_IOC_INFO, info);
}

/*
 * The decoded layout for a recording. The device computes the real one; a recording only
 * needs sizes that are large enough for a caller that never reads pixels.
 */
static void
rec_format(struct sfavd_create *c)
{
	uint32_t w = (c->width + 63) & ~63u, h = (c->height + 15) & ~15u;

	c->format.pixelformat = SFAVD_PIX_NV12;
	c->format.width = w;
	c->format.height = h;
	c->format.bytesperline = w;
	c->format.uv_offset = w * h;
	c->format.sizeimage = w * h * 2;
}

int
sfavd_create(sfavd_dev *dev, struct sfavd_create *create)
{
	if (dev->rec != NULL) {
		unsigned c;

		if (create->buffers == 0 || create->buffers > SFAVD_MAX_BUFFERS) {
			errno = EINVAL;
			return -1;
		}
		for (c = 0; c < 16 && dev->ctx[c].used; c++)
			;
		if (c == 16) {
			errno = ENOSPC;
			return -1;
		}
		create->ctx = c;
		rec_format(create);
		dev->ctx[c].used = 1;
		dev->ctx[c].nbuf = create->buffers;
		dev->ctx[c].pictures = 0;
		dev->ctx[c].size = create->format.sizeimage;
		if (rec_head(dev, SFAVD_REC_CREATE, (uint32_t)(sizeof(*create) + create->format_ctrl.size)) ||
		    rec_bytes(dev, create, sizeof(*create)) ||
		    rec_bytes(dev, (const void *)(uintptr_t)create->format_ctrl.ptr, create->format_ctrl.size)) {
			errno = EIO;
			return -1;
		}
		fflush(dev->rec);
		return 0;
	}
	return ioctl(dev->fd, SFAVD_IOC_CREATE, create);
}

int
sfavd_map(sfavd_dev *dev, uint32_t ctx, uint32_t index, const uint8_t **address, size_t *size)
{
	struct sfavd_map map;

	if (dev->rec != NULL) {
		struct rec_ctx *c;

		if (ctx >= 16 || !dev->ctx[ctx].used || index >= dev->ctx[ctx].nbuf) {
			errno = EINVAL;
			return -1;
		}
		c = &dev->ctx[ctx];
		if (c->buf[index] == NULL) {
			c->buf[index] = malloc(c->size);
			if (c->buf[index] == NULL)
				return -1;
			/* grey, so that a viewer of a recording run shows something neutral */
			memset(c->buf[index], 0x80, c->size);
		}
		*address = c->buf[index];
		*size = c->size;
		return 0;
	}
	memset(&map, 0, sizeof(map));
	map.ctx = ctx;
	map.index = index;
	if (ioctl(dev->fd, SFAVD_IOC_MAP, &map) != 0)
		return -1;
	*address = (const uint8_t *)(uintptr_t)map.address;
	*size = (size_t)map.size;
	return 0;
}

int
sfavd_decode(sfavd_dev *dev, struct sfavd_decode *decode, const struct sfavd_meta *meta)
{
	if (dev->rec != NULL) {
		struct sfavd_meta none;
		uint32_t length = (uint32_t)(sizeof(*decode) + sizeof(*meta)) + decode->size;

		if (decode->nctrls > SFAVD_MAX_CTRLS) {
			errno = EINVAL;
			return -1;
		}
		memset(&none, 0, sizeof(none));
		none.pts = INT64_MIN;
		for (uint32_t i = 0; i < decode->nctrls; i++)
			length += decode->ctrls[i].size;
		decode->status = 0x1000;
		decode->decode_us = 0;
		/*
		 * The picture's number in decode order goes into the first bytes of its
		 * buffer: a program that outputs the "decoded" frames of a recording run
		 * thereby tells in which order it would present the pictures
		 * (tools/avd_record.sh makes <clip>.order from it).
		 */
		if (!(decode->flags & SFAVD_DECODE_HOLD) && decode->ctx < 16 && dev->ctx[decode->ctx].used) {
			struct rec_ctx *c = &dev->ctx[decode->ctx];
			const uint8_t *address;
			size_t size;

			if (decode->dst < c->nbuf && sfavd_map(dev, decode->ctx, decode->dst, &address, &size) == 0 && size >= 8)
				memcpy(c->buf[decode->dst], &c->pictures, 8);
			c->pictures++;
		}
		if (rec_head(dev, SFAVD_REC_DECODE, length) || rec_bytes(dev, decode, sizeof(*decode)) ||
		    rec_bytes(dev, meta ? meta : &none, sizeof(none)))
			goto fail;
		for (uint32_t i = 0; i < decode->nctrls; i++)
			if (rec_bytes(dev, (const void *)(uintptr_t)decode->ctrls[i].ptr, decode->ctrls[i].size))
				goto fail;
		if (rec_bytes(dev, (const void *)(uintptr_t)decode->data, decode->size))
			goto fail;
		fflush(dev->rec);
		return 0;
fail:
		errno = EIO;
		return -1;
	}
	(void)meta;
	return ioctl(dev->fd, SFAVD_IOC_DECODE, decode);
}

int
sfavd_destroy(sfavd_dev *dev, uint32_t ctx)
{
	struct sfavd_destroy destroy = { ctx };

	if (dev->rec != NULL) {
		if (ctx >= 16 || !dev->ctx[ctx].used) {
			errno = EINVAL;
			return -1;
		}
		for (unsigned i = 0; i < SFAVD_MAX_BUFFERS; i++) {
			free(dev->ctx[ctx].buf[i]);
			dev->ctx[ctx].buf[i] = NULL;
		}
		dev->ctx[ctx].used = 0;
		if (rec_head(dev, SFAVD_REC_DESTROY, sizeof(destroy)) || rec_bytes(dev, &destroy, sizeof(destroy))) {
			errno = EIO;
			return -1;
		}
		fflush(dev->rec);
		return 0;
	}
	return ioctl(dev->fd, SFAVD_IOC_DESTROY, &destroy);
}

/* ABI1 optional extension. Never pretend a recording is a GPU import. */
int sfavd_import_buffers(sfavd_dev *dev, uint32_t ctx,
                        const struct sfavd_import_buffer *buffers, uint32_t count)
{
    struct sfavd_import req = { .ctx = ctx, .count = count };
    if (!dev || !buffers || !count || count > SFAVD_MAX_BUFFERS) {
        errno = EINVAL;
        return -1;
    }
    memcpy(req.buffers, buffers, count * sizeof(*buffers));
    int error = sfavd_import_validate(&req, count, 1);
    if (error) { errno = error; return -1; }
    if (dev->fd < 0) { errno = ENOTSUP; return -1; }
    return ioctl(dev->fd, SFAVD_IOC_IMPORT, &req);
}
