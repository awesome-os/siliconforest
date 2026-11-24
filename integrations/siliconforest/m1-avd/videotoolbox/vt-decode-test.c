// SPDX-License-Identifier: MIT
// vt-decode-test: decodes the video track of a file through the public VideoToolbox API,
// the way an application for macOS does: a CMVideoFormatDescription from the track's sample
// description atom (avcC / hvcC), a VTDecompressionSession with an output callback, one
// CMSampleBuffer per sample, CVPixelBuffers out. Prints for every picture the MD5 of its
// visible NV12 data (Y rows, then CbCr rows), which is what
// `ffmpeg -pix_fmt nv12 -f framemd5` hashes for a software decode of the same file; lines
// have sfavd-replay's form so that tools/avd_compare.py compares them.
// The file is demultiplexed with libavformat (only that: no libavcodec decoder is used here).
//
//   vt-decode-test [--software] [--planar] <file>
//     --software   asks the session not to use the hardware decoder (Enable... = false);
//                  without it the hardware decoder is required
//
// Output: VT_SESSION ..., SFAVD_FRAME <n> pts <pts> <w>x<h> <md5> us <decode time> st 0x0,
//         VT_SUMMARY ...
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CMSampleBuffer.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavformat/avformat.h>

#include "sf_md5.h"
#include "VideoToolbox.h"

static uint64_t g_frames, g_errors, g_out_of_order, g_last_us, g_callback_us;
static int64_t g_last_pts = INT64_MIN;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

static void output(void *refCon, void *sourceRefCon, OSStatus status, VTDecodeInfoFlags flags, CVImageBufferRef image,
                   CMTime pts, CMTime duration)
{
    struct sf_md5 md5;
    char hex[33];
    size_t w, h;
    uint64_t entered = now_us();

    (void)refCon;
    (void)flags;
    (void)duration;
    if (status != noErr || image == NULL) {
        printf("SFAVD_FAIL %" PRIu64 " pts %" PRId64 " errno %d st 0x0\n", g_frames, (int64_t)pts.value, (int)status);
        g_errors++;
        return;
    }
    // the refCon of each sample is its own presentation time: the session must hand it back with its picture
    if ((int64_t)(intptr_t)sourceRefCon != (int64_t)pts.value)
        g_errors++;
    if ((int64_t)pts.value < g_last_pts)
        g_out_of_order++;
    g_last_pts = pts.value;
    w = CVPixelBufferGetWidth(image);
    h = CVPixelBufferGetHeight(image);
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    sf_md5_init(&md5);
    for (size_t y = 0; y < h; y++)
        sf_md5_update(&md5, (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 0) + y * CVPixelBufferGetBytesPerRowOfPlane(image, 0), w);
    if (CVPixelBufferGetPlaneCount(image) == 2) {
        for (size_t y = 0; y < (h + 1) / 2; y++)
            sf_md5_update(&md5, (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 1) + y * CVPixelBufferGetBytesPerRowOfPlane(image, 1), (w + 1) & ~(size_t)1);
    } else {
        // planar: interleave to what the bi-planar form holds
        uint8_t row[8192];
        for (size_t y = 0; y < (h + 1) / 2; y++) {
            const uint8_t *u = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 1) + y * CVPixelBufferGetBytesPerRowOfPlane(image, 1);
            const uint8_t *v = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 2) + y * CVPixelBufferGetBytesPerRowOfPlane(image, 2);
            for (size_t x = 0; x < (w + 1) / 2 && x < sizeof(row) / 2; x++) {
                row[2 * x] = u[x];
                row[2 * x + 1] = v[x];
            }
            sf_md5_update(&md5, row, (w + 1) & ~(size_t)1);
        }
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    sf_md5_final(&md5, hex);
    printf("SFAVD_FRAME %" PRIu64 " pts %" PRId64 " %zux%zu %s us %" PRIu64 " st 0x0\n", g_frames, (int64_t)pts.value, w, h, hex, g_last_us);
    g_frames++;
    // the hashing here is not the session's time
    g_callback_us += now_us() - entered;
}

static void free_block(void *refCon, void *block, size_t size)
{
    (void)refCon;
    (void)size;
    free(block);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int software = 0, planar = 0, video = -1;
    AVFormatContext *in = NULL;
    AVPacket *packet = av_packet_alloc();
    CMVideoFormatDescriptionRef format = NULL;
    VTDecompressionSessionRef session = NULL;
    VTDecompressionOutputCallbackRecord record = { output, NULL };
    CFMutableDictionaryRef extensions, atoms, spec, attributes;
    CMVideoCodecType type;
    const char *atom;
    CFTypeRef hw = NULL;
    uint64_t samples = 0, total_us = 0, max_us = 0;
    OSStatus err;

    setvbuf(stdout, NULL, _IOLBF, 0);       /* whole lines, also next to the framework's stderr */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--software"))
            software = 1;
        else if (!strcmp(argv[i], "--planar"))
            planar = 1;
        else
            path = argv[i];
    }
    if (path == NULL || avformat_open_input(&in, path, NULL, NULL) < 0 || avformat_find_stream_info(in, NULL) < 0) {
        printf("VT_NO_INPUT %s\n", path ? path : "(none)");
        return 2;
    }
    for (unsigned i = 0; i < in->nb_streams && video < 0; i++)
        if (in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            video = (int)i;
    if (video < 0)
        return 2;
    AVCodecParameters *par = in->streams[video]->codecpar;
    switch (par->codec_id) {
    case AV_CODEC_ID_H264: type = kCMVideoCodecType_H264; atom = "avcC"; break;
    case AV_CODEC_ID_HEVC: type = kCMVideoCodecType_HEVC; atom = "hvcC"; break;
    case AV_CODEC_ID_VP9: type = kCMVideoCodecType_VP9; atom = "vpcC"; break;
    default: printf("VT_UNSUPPORTED_CODEC\n"); return 2;
    }
    printf("VT_HARDWARE_SUPPORTED %d\n", (int)VTIsHardwareDecodeSupported(type));

    extensions = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    atoms = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (par->extradata_size > 0) {
        CFStringRef key = CFStringCreateWithCString(NULL, atom, kCFStringEncodingASCII);
        CFDataRef data = CFDataCreate(NULL, par->extradata, par->extradata_size);
        CFDictionarySetValue(atoms, key, data);
        CFRelease(key);
        CFRelease(data);
    }
    CFDictionarySetValue(extensions, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms, atoms);
    err = CMVideoFormatDescriptionCreate(NULL, type, par->width, par->height, extensions, &format);
    if (err != noErr) {
        printf("VT_FORMAT_FAILED %d\n", (int)err);
        return 1;
    }
    spec = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (software)
        CFDictionarySetValue(spec, kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder, kCFBooleanFalse);
    else
        CFDictionarySetValue(spec, kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder, kCFBooleanTrue);
    attributes = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    {
        int32_t pixel = planar ? kCVPixelFormatType_420YpCbCr8Planar : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &pixel);
        CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, n);
        CFRelease(n);
    }
    err = VTDecompressionSessionCreate(NULL, format, spec, attributes, &record, &session);
    if (err != noErr || session == NULL) {
        printf("VT_SESSION_FAILED %d\n", (int)err);
        return 1;
    }
    VTSessionCopyProperty(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hw);
    printf("VT_SESSION codec %.4s %dx%d hardware %d\n", atom, par->width, par->height, hw == kCFBooleanTrue);

    while (av_read_frame(in, packet) >= 0) {
        if (packet->stream_index == video) {
            CMBlockBufferCustomBlockSource source = { 0, NULL, free_block, NULL };
            CMSampleTimingInfo timing;
            CMBlockBufferRef block = NULL;
            CMSampleBufferRef sample = NULL;
            size_t size = packet->size;
            void *copy = malloc(size);
            uint64_t t0;

            memcpy(copy, packet->data, size);
            timing.presentationTimeStamp = CMTimeMake(packet->pts, in->streams[video]->time_base.den);
            timing.decodeTimeStamp = CMTimeMake(packet->dts, in->streams[video]->time_base.den);
            timing.duration = CMTimeMake(packet->duration, in->streams[video]->time_base.den);
            if (CMBlockBufferCreateWithMemoryBlock(NULL, copy, size, NULL, &source, 0, size, 0, &block) != noErr ||
                CMSampleBufferCreate(NULL, block, true, NULL, NULL, format, 1, 1, &timing, 1, &size, &sample) != noErr) {
                printf("VT_SAMPLE_FAILED\n");
                return 1;
            }
            t0 = now_us();
            g_last_us = 0;
            g_callback_us = 0;
            err = VTDecompressionSessionDecodeFrame(session, sample, 0, (void *)(intptr_t)packet->pts, NULL);
            g_last_us = now_us() - t0 - g_callback_us;
            total_us += g_last_us;
            if (g_last_us > max_us)
                max_us = g_last_us;
            if (err != noErr) {
                printf("VT_DECODE_FAILED sample %" PRIu64 " %d\n", samples, (int)err);
                g_errors++;
            }
            samples++;
            CFRelease(sample);
            CFRelease(block);
        }
        av_packet_unref(packet);
    }
    VTDecompressionSessionWaitForAsynchronousFrames(session);
    VTDecompressionSessionFinishDelayedFrames(session);
    VTSessionCopyProperty(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hw);
    printf("VT_SUMMARY samples %" PRIu64 " pictures %" PRIu64 " errors %" PRIu64 " hardware %d callbacks_not_in_presentation_order %" PRIu64
           " decode_call_us_mean %" PRIu64 " max %" PRIu64 " (callback time excluded)\n", samples, g_frames, g_errors, hw == kCFBooleanTrue, g_out_of_order,
           samples ? total_us / samples : 0, max_us);
    VTDecompressionSessionInvalidate(session);
    CFRelease(session);
    CFRelease(format);
    avformat_close_input(&in);
    return g_errors || g_frames == 0 ? 1 : 0;
}
