#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
a='    uint64_t send_us, create_us, copy_us, callback_us;';assert s.count(a)==1
s=s.replace(a,a+'\n    Boolean zeroRequested,zeroRequired;\n    uint64_t zeroFrames,copiedBytes;\n    struct SFFrameLeaseGroup *captureGroup;')
a='static void picture_decoded(void *opaque, const AVFrame *frame);';assert s.count(a)==1
s=s.replace(a,'#include "SFGPUProducer.h"\n\n'+a)
a='    s->timing = false;';assert s.count(a)==1
s=s.replace(a,'''    if (s->zeroRequested || getenv("SF_VT_COPY_COUNTS")) fprintf(stderr,"SF_VT_GPU frames=%llu copied-bytes=%llu required=%d\\n",
        (unsigned long long)s->zeroFrames,(unsigned long long)s->copiedBytes,(int)s->zeroRequired);
'''+a)
a='            avd->picture_decoded = picture_decoded;';assert s.count(a)==1
s=s.replace(a,'''            if (sf_gpu_configure(s,avd)<0) {CFRelease(s);return kVTPropertyNotSupportedErr;}
'''+a)
a='    if (decoded->hw_frames_ctx != NULL) {';assert s.count(a)==1
s=s.replace(a,'''    if (s->zeroRequested) {
        CVPixelBufferRef shared=sf_gpu_image(s,decoded);
        if (shared) {
            s->zeroFrames++;attach_colour(shared,decoded);
            if(s->timing)s->create_us+=now_us()-t0;
            return shared;
        }
        if(s->zeroRequired)return NULL;
    }
'''+a)
a='    CVPixelBufferUnlockBaseAddress(buffer, 0);';assert s.count(a)==1
s=s.replace(a,'    s->copiedBytes+=(uint64_t)w*h+2*(uint64_t)cw*ch;\n'+a)
a='    s->format = (CMVideoFormatDescriptionRef)CFRetain(format);';assert s.count(a)==1
s=s.replace(a,a+'\n    sf_gpu_modes(s);')
a='    av_frame_free(&s->software);';assert s.count(a)==1
s=s.replace(a,a+'''\n    if (s->captureGroup) {
        SFFrameLeaseGroup *group=s->captureGroup;s->captureGroup=NULL;
        SFVTFrameLeaseGroupRetire(group);
    }''')
a='    if (s->device == NULL && require) {';assert s.count(a)==1
s=s.replace(a,'    if (s->device == NULL && (require || s->zeroRequired)) {')
p.write_text(s)
