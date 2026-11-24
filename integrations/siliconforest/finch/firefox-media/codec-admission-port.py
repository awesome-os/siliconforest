#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extend our MIT public-VT demux client with admission-only P010 controls.

It never hashes P010 with the old 8-bit hash routine: admission mode exits
before feeding any samples. Report clean refusal as refusal, not decode PASS.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
def replace(old,new):
    global s
    if s.count(old)!=1:raise ValueError('VT CLI anchor changed: '+old[:100])
    s=s.replace(old,new)
replace('int software = 0, planar = 0, video = -1;', 'int software = 0, planar = 0, video = -1, p010 = 0, admission = 0;')
replace('''        if (!strcmp(argv[i], "--software"))''','''        if (!strcmp(argv[i], "--admission-only"))
            admission = 1;
        else if (!strcmp(argv[i], "--p010"))
            p010 = 1;
        else if (!strcmp(argv[i], "--software"))''')
replace('''    if (path == NULL || avformat_open_input''','''    if (!admission || (p010 && planar)) {
        fprintf(stderr,"Admission-only client: require --admission-only; P010 cannot be planar8\\n");return 64;
    }
    if (path == NULL || avformat_open_input''')
replace('''        int32_t pixel = planar ?''','''        int32_t pixel = p010 ? (int32_t)0x78343230 : planar ?''')
replace('''    if (err != noErr || session == NULL) {
        printf("VT_SESSION_FAILED''','''    if (admission) {
        printf("VT_ADMISSION status=%d session-present=%d requested-fourcc=%08x codec=%08x input-profile=%d extradata-bytes=%d width=%d height=%d hardware-required=%d decode-attempted=0\\n",
            (int)err,session!=NULL,(unsigned)(p010?0x78343230:planar?kCVPixelFormatType_420YpCbCr8Planar:kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (unsigned)type,par->profile,par->extradata_size,par->width,par->height,!software);
        int inconsistent=(err==noErr)!=(session!=NULL);
        if(session){VTDecompressionSessionInvalidate(session);CFRelease(session);}
        CFRelease(attributes);CFRelease(spec);CFRelease(format);CFRelease(atoms);CFRelease(extensions);
        av_packet_free(&packet);avformat_close_input(&in);
        if(inconsistent)fprintf(stderr,"VT_ADMISSION_FAIL status/session invariant\\n");
        return inconsistent?1:0;
    }
    if (err != noErr || session == NULL) {
        printf("VT_SESSION_FAILED''')
p.write_text(s)
