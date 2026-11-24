#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Append an opt-in external-capture contract to FFmpeg's existing sfavd port.

Applied after the original sfavd patch. Default kernel buffers remain the
fallback. Import is atomic and precedes every MAP/decode. Never free GPU tokens
before a successful kernel-context destroy; ambiguous failure quarantines them.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1])
def replace(text,old,new):
 if text.count(old)!=1:raise ValueError('sfavd source anchor changed: '+old[:90])
 return text.replace(old,new)
p=root/'libavutil/hwcontext_sfavd.h';s=p.read_text()
s=replace(s,'struct AVFrame;','''struct AVFrame;
struct AVPDAVDFramesContext;

/* Optional caller-owned capture allocation. The allocator returns 0 only after
 * filling every requested entry. Partial allocation is released on failure.
 * The opaque token is borrowed by AVPDAVDFrame while that AVFrame is alive. */
typedef struct AVPDAVDExternalBuffer {
    uint8_t *data;
    size_t size;
    void *opaque;
} AVPDAVDExternalBuffer;''')
s=replace(s,'    void *picture_decoded_opaque;\n} AVPDAVDDeviceContext;','''    void *picture_decoded_opaque;
    uint32_t capabilities;
    int (*allocate_external)(void *opaque, const struct AVPDAVDFramesContext *layout,
                             AVPDAVDExternalBuffer *buffers, unsigned count);
    /* Release depends only on its buffer token, never the originating VT session. */
    void (*free_external)(AVPDAVDExternalBuffer *buffer);
    void *external_opaque;
} AVPDAVDDeviceContext;''')
s=replace(s,'    uint64_t timestamp;\n} AVPDAVDFramesContext;','''    uint64_t timestamp;
    /* Nonzero only after atomic kernel import succeeds. */
    unsigned external_buffers;
} AVPDAVDFramesContext;''')
s=replace(s,'    uint64_t       timestamp;\n} AVPDAVDFrame;','''    uint64_t       timestamp;
    /* Borrowed storage token; keep the AVFrame referenced while consuming it. */
    void          *external;
} AVPDAVDFrame;''')
p.write_text(s)
p=root/'libavutil/hwcontext_sfavd.c';s=p.read_text()
s='#include <stdlib.h>\n#include <stdio.h>\n#include "sf_sfavd_capture_pool.h"\n'+s
s=replace(s,'    unsigned next;\n} SFAVDFramesPriv;','''    unsigned next;
    AVPDAVDExternalBuffer external[SFAVD_MAX_BUFFERS];
    void (*release_external)(AVPDAVDExternalBuffer *buffer);
} SFAVDFramesPriv;''')
s=replace(s,'    av_log(ctx, AV_LOG_VERBOSE, "AVD: codecs 0x%x, firmware \\"%s\\"\\n",', '    hwctx->capabilities = info.caps;\n    av_log(ctx, AV_LOG_VERBOSE, "AVD: codecs 0x%x, firmware \\"%s\\"\\n",')
anchor='static AVBufferRef *sfavd_pool_alloc(void *opaque, size_t size)'
helpers='''static void sfavd_external_clear(SFAVDFramesPriv *priv)
{
    for (unsigned i = 0; i < SFAVD_MAX_BUFFERS; ++i) {
        AVPDAVDExternalBuffer *buffer = &priv->external[i];
        if (priv->release_external && (buffer->opaque || buffer->data))
            priv->release_external(buffer);
        memset(buffer, 0, sizeof(*buffer));
    }
    priv->p.external_buffers = 0;
}

static void sfavd_external_import(AVHWFramesContext *hwfc)
{
    SFAVDFramesPriv *priv = hwfc->hwctx;
    AVPDAVDDeviceContext *dev = hwfc->device_ctx->hwctx;
    struct sfavd_import_buffer buffers[SFAVD_MAX_BUFFERS] = { 0 };
    unsigned count = priv->p.nb_buffers;
    int allocated;

    priv->release_external = dev->free_external;
    allocated = dev->allocate_external(dev->external_opaque, &priv->p,
                                      priv->external, count);
    if (allocated != 0)
        goto fallback;
    for (unsigned i = 0; i < count; ++i) {
        const AVPDAVDExternalBuffer *external = &priv->external[i];
        if (!external->data || !external->opaque || external->size < priv->p.sizeimage)
            goto fallback;
        buffers[i].address = (uintptr_t)external->data;
        buffers[i].size = external->size;
    }
    if (sfavd_import_buffers(dev->dev, priv->p.ctx, buffers, count) < 0) {
        av_log(hwfc, AV_LOG_WARNING, "AVD external capture import refused: %s; kernel-buffer fallback\\n",
               av_err2str(AVERROR(errno)));
        goto fallback;
    }
    priv->p.external_buffers = count;
    av_log(hwfc, AV_LOG_VERBOSE, "AVD external capture imported %u buffers before mapping/decoding\\n", count);
    return;

fallback:
    sfavd_external_clear(priv);
    av_log(hwfc, AV_LOG_VERBOSE, "AVD external capture allocation/import unavailable; kernel-buffer fallback\\n");
}

'''
s=replace(s,anchor,helpers+anchor)
old='''    if (sfavd_map(dev->dev, frame->ctx, frame->index, &frame->data, &frame->size) < 0) {
        av_log(hwfc, AV_LOG_ERROR, "Cannot map decoded buffer %u: %s\\n",
               frame->index, av_err2str(AVERROR(errno)));
        av_free(frame);
        return NULL;
    }'''
new='''    if (priv->p.external_buffers) {
        frame->data = priv->external[frame->index].data;
        frame->size = priv->external[frame->index].size;
        frame->external = priv->external[frame->index].opaque;
    } else if (sfavd_map(dev->dev, frame->ctx, frame->index, &frame->data, &frame->size) < 0) {
        av_log(hwfc, AV_LOG_ERROR, "Cannot map decoded buffer %u: %s\\n",
               frame->index, av_err2str(AVERROR(errno)));
        av_free(frame);
        return NULL;
    }'''
s=replace(s,old,new)
s=replace(s,'    struct sfavd_create create = { 0 };','''    struct sfavd_create create = { 0 };
    int try_external = dev->allocate_external && dev->free_external &&
                       (dev->capabilities & SFAVD_CAP_EXTERNAL_BUFFERS);''')
s=replace(s,'''    create.buffers = FFMIN(hwfc->initial_pool_size > 0 ? hwfc->initial_pool_size : 20,
                           SFAVD_MAX_BUFFERS);''','''    create.buffers = FFMIN(hwfc->initial_pool_size > 0 ? hwfc->initial_pool_size : 20,
                           SFAVD_MAX_BUFFERS);
    if (try_external) {
        const char *requested = getenv("SF_VT_CAPTURE_BUFFERS");
        unsigned minimum = hwfc->initial_pool_size > 0 ? hwfc->initial_pool_size : 20;
        create.buffers = sf_sfavd_capture_pool(minimum, SFAVD_MAX_BUFFERS, requested);
        if (requested)
            fprintf(stderr, "SF_VT_CAPTURE_POOL codec-minimum=%u maximum=%u selected=%u\\n",
                    minimum, SFAVD_MAX_BUFFERS, create.buffers);
    }
    else if (dev->allocate_external)
        av_log(hwfc, AV_LOG_VERBOSE, "AVD external capture capability/release callback absent; kernel-buffer fallback\\n");''')
s=replace(s,'    ffhwframesctx(hwfc)->pool_internal =\n', '    if (try_external)\n        sfavd_external_import(hwfc);\n\n    ffhwframesctx(hwfc)->pool_internal =\n')
s=replace(s,'''    if (priv->created && dev->dev)
        sfavd_destroy(dev->dev, priv->p.ctx);
    priv->created = 0;''','''    int destroyed = !priv->created;
    if (priv->created && dev->dev)
        destroyed = sfavd_destroy(dev->dev, priv->p.ctx) == 0;
    if (destroyed)
        sfavd_external_clear(priv);
    else if (priv->p.external_buffers)
        av_log(hwfc, AV_LOG_ERROR, "AVD context destroy failed; quarantining external GPU tokens (no early release)\\n");
    priv->created = 0;''')
p.write_text(s)
