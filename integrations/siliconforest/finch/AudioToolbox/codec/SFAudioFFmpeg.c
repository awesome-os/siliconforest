/* SPDX-License-Identifier: MIT */
/* Public FFmpeg codec/demux API bridge; no Apple headers or codec binaries. */
#include "SFAudioFFmpeg.h"
#include "SFAACCookie.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/samplefmt.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define SF_MAX_BYTES (256u * 1024u * 1024u)
#define SF_MAX_PACKETS (1u << 20)
static enum AVCodecID codec_id(AudioFormatID id) {
    switch (id) {
        case kAudioFormatMPEG4AAC: return AV_CODEC_ID_AAC;
        case kAudioFormatMPEGLayer3: return AV_CODEC_ID_MP3;
        case kAudioFormatAppleLossless: return AV_CODEC_ID_ALAC;
        default: return AV_CODEC_ID_NONE;
    }
}
bool SFCompressedFormat(AudioFormatID id) { return codec_id(id) != AV_CODEC_ID_NONE; }
struct Reader { AudioFile_ReadProc read; void *user; int64_t size, position; };
static int read_bytes(void *user, uint8_t *data, int bytes) {
    struct Reader *r = user; UInt32 got = 0;
    OSStatus status = r->read(r->user, r->position, (UInt32)bytes, data, &got);
    if (status || got > (UInt32)bytes) return AVERROR(EIO);
    r->position += got;
    return got ? (int)got : AVERROR_EOF;
}
static int64_t seek_bytes(void *user, int64_t offset, int whence) {
    struct Reader *r = user;
    if (whence & AVSEEK_SIZE) return r->size;
    whence &= ~AVSEEK_FORCE;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? r->position : whence == SEEK_END ? r->size : -1;
    if (base < 0 || offset < -base || offset > r->size - base) return AVERROR(EINVAL);
    r->position = base + offset; return r->position;
}
OSStatus SFCompressedOpen(AudioFile_ReadProc read, void *user, int64_t size, SFCompressedFile *out) {
    if (!out || !read || size <= 0 || size > SF_MAX_BYTES) return kAudioFileUnsupportedFileTypeError;
    memset(out, 0, sizeof(*out));
    OSStatus status = kAudioFileInvalidFileError;
    struct Reader reader = {read, user, size, 0};
    unsigned char *buffer = av_malloc(32768);
    AVIOContext *io = buffer ? avio_alloc_context(buffer, 32768, 0, &reader, read_bytes, NULL, seek_bytes) : NULL;
    AVFormatContext *format = avformat_alloc_context();
    AVPacket *packet = av_packet_alloc();
    if (!io || !format || !packet) { status = -108; goto done; }
    format->pb = io; format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&format, NULL, NULL, NULL) < 0 || avformat_find_stream_info(format, NULL) < 0) goto done;
    int index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (index < 0) goto done;
    AVStream *stream = format->streams[index]; AVCodecParameters *p = stream->codecpar;
    AudioFormatID id = p->codec_id == AV_CODEC_ID_AAC ? kAudioFormatMPEG4AAC :
        p->codec_id == AV_CODEC_ID_MP3 ? kAudioFormatMPEGLayer3 : p->codec_id == AV_CODEC_ID_ALAC ? kAudioFormatAppleLossless : 0;
    if (!id || p->sample_rate <= 0 || p->ch_layout.nb_channels < 1 || p->ch_layout.nb_channels > 8) { status = kAudioFileUnsupportedDataFormatError; goto done; }
    out->type = p->codec_id == AV_CODEC_ID_MP3 ? kAudioFileMP3Type : kAudioFileM4AType;
    UInt32 framesPerPacket = p->frame_size > 0 ? p->frame_size : p->codec_id == AV_CODEC_ID_MP3 ? (p->sample_rate <= 24000 ? 576 : 1152) : p->codec_id == AV_CODEC_ID_ALAC ? 4096 : 1024;
    out->format = (AudioStreamBasicDescription){p->sample_rate, id, 0, 0, framesPerPacket, 0, (UInt32)p->ch_layout.nb_channels, (UInt32)p->bits_per_raw_sample, 0};
    if (p->extradata_size > 0) out->cookie = CFDataCreate(NULL, p->extradata, p->extradata_size);
    int64_t frames = 0; int error;
    while ((error = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == index) {
            if (packet->size < 0 || (size_t)packet->size > SF_MAX_BYTES - out->dataSize || out->packetCount >= SF_MAX_PACKETS) goto done;
            uint8_t *data = realloc(out->data, out->dataSize + packet->size + 1);
            if (!data) { status = -108; goto done; } out->data = data;
            AudioStreamPacketDescription *packets = realloc(out->packets, (out->packetCount + 1) * sizeof(*packets));
            if (!packets) { status = -108; goto done; } out->packets = packets;
            int64_t packetFrames = packet->duration > 0 ? av_rescale_q(packet->duration, stream->time_base, (AVRational){1,p->sample_rate}) : framesPerPacket;
            if (packetFrames < 0 || packetFrames > UINT32_MAX || frames > INT64_MAX - packetFrames) goto done;
            // AAC's last demux duration can be shortened by an edit; the decoder
            // still emits a complete transform frame. Valid length is tracked below.
            if (p->codec_id == AV_CODEC_ID_AAC) packetFrames = framesPerPacket;
            out->packets[out->packetCount++] = (AudioStreamPacketDescription){(SInt64)out->dataSize,(UInt32)packetFrames,(UInt32)packet->size};
            memcpy(out->data + out->dataSize, packet->data, packet->size); out->dataSize += packet->size;
            if ((UInt32)packet->size > out->maximumPacket) out->maximumPacket = packet->size;
            size_t sideSize = 0; const uint8_t *side = av_packet_get_side_data(packet, AV_PKT_DATA_SKIP_SAMPLES, &sideSize);
            if (side && sideSize >= 10) {
                uint32_t leading = AV_RL32(side), trailing = AV_RL32(side+4);
                if (leading > INT32_MAX || trailing > INT32_MAX) goto done;
                if (out->packetCount == 1) out->packetInfo.mPrimingFrames = leading;
                if (trailing) out->packetInfo.mRemainderFrames = trailing;
            }
            frames += packetFrames;
        }
        av_packet_unref(packet);
    }
    if (error != AVERROR_EOF || !out->packetCount) goto done;
    out->packetInfo.mNumberValidFrames = frames - out->packetInfo.mPrimingFrames - out->packetInfo.mRemainderFrames;
    if (p->codec_id != AV_CODEC_ID_MP3 && stream->duration > 0) {
        int64_t duration = av_rescale_q(stream->duration, stream->time_base, (AVRational){1,p->sample_rate});
        if (duration <= frames - out->packetInfo.mPrimingFrames && duration >= 0) {
            int64_t remainder = frames - out->packetInfo.mPrimingFrames - duration;
            if (remainder > INT32_MAX) goto done;
            out->packetInfo.mNumberValidFrames = duration; out->packetInfo.mRemainderFrames = (SInt32)remainder;
        }
    }
    if (out->packetInfo.mNumberValidFrames < 0) goto done;
    status = noErr;
done:
    av_packet_free(&packet); avformat_close_input(&format);
    if (io) { av_freep(&io->buffer); avio_context_free(&io); } else av_free(buffer);
    if (status) { free(out->data); free(out->packets); if(out->cookie) CFRelease(out->cookie); memset(out,0,sizeof(*out)); }
    return status;
}
struct SFDecoder {
    AudioStreamBasicDescription input;
    AVCodecContext *codec; AVFrame *frame; AVPacket *packet;
    uint8_t *cookie; UInt32 cookieSize;
    float *pcm; size_t pcmCapacity; int frameOffset;
    bool opened, eof, flushed; uint64_t skip;
};
SFDecoder *SFDecoderNew(const AudioStreamBasicDescription *input) {
    if (!input || !SFCompressedFormat(input->mFormatID) || input->mChannelsPerFrame < 1 || input->mChannelsPerFrame > 8) return NULL;
    SFDecoder *d = calloc(1,sizeof(*d)); if (!d) return NULL;
    d->input = *input; d->frame = av_frame_alloc(); d->packet = av_packet_alloc();
    if (!d->frame || !d->packet) { SFDecoderDispose(d); return NULL; } return d;
}
void SFDecoderDispose(SFDecoder *d) {
    if (!d) return; avcodec_free_context(&d->codec); av_frame_free(&d->frame); av_packet_free(&d->packet);
    free(d->cookie); free(d->pcm); free(d);
}
void SFDecoderReset(SFDecoder *d) {
    if (!d) return; if (d->codec) avcodec_flush_buffers(d->codec);
    av_frame_unref(d->frame); av_packet_unref(d->packet); d->frameOffset=0; d->eof=false; d->flushed=false; d->skip=0;
}
void SFDecoderSkip(SFDecoder *d, uint64_t frames) { if (d) d->skip=frames; }
OSStatus SFDecoderCookie(SFDecoder *d, UInt32 size, const void *cookie) {
    if (!d || d->opened || !cookie || size > (1u << 20)) return kAudio_ParamError;
    if (d->input.mFormatID == kAudioFormatMPEG4AAC) {
        const uint8_t *config = NULL; size_t length = 0;
        if (!sf_aac_cookie(cookie, size, &config, &length)) return kAudio_ParamError;
        cookie = config;
        size = (UInt32)length;
    }
    uint8_t *copy=malloc(size ? size : 1); if (!copy) return -108;
    memcpy(copy,cookie,size); free(d->cookie); d->cookie=copy; d->cookieSize=size; return noErr;
}
static OSStatus open_decoder(SFDecoder *d) {
    const AVCodec *codec=avcodec_find_decoder(codec_id(d->input.mFormatID));
    if (!codec) return kAudioConverterErr_FormatNotSupported;
    d->codec=avcodec_alloc_context3(codec); if (!d->codec) return -108;
    d->codec->sample_rate=(int)d->input.mSampleRate;
    av_channel_layout_default(&d->codec->ch_layout,d->input.mChannelsPerFrame);
    if (d->cookieSize) {
        UInt32 size=d->cookieSize;
        // ALAC decoder expects its 12-byte atom prefix followed by the 24-byte config.
        UInt32 prefix=d->input.mFormatID==kAudioFormatAppleLossless && size==24 ? 12 : 0;
        d->codec->extradata=av_mallocz(size+prefix+AV_INPUT_BUFFER_PADDING_SIZE);
        if (!d->codec->extradata) return -108;
        memcpy(d->codec->extradata+prefix,d->cookie,size); d->codec->extradata_size=size+prefix;
    }
    if (avcodec_open2(d->codec,codec,NULL)<0) return kAudioConverterErr_FormatNotSupported;
    d->opened=true; return noErr;
}
static double sample(const AVFrame *frame, int channel, int index) {
    enum AVSampleFormat fmt=av_get_packed_sample_fmt(frame->format);
    int planar=av_sample_fmt_is_planar(frame->format), width=av_get_bytes_per_sample(frame->format);
    const uint8_t *p=frame->extended_data[planar ? channel : 0]+(size_t)(index*(planar ? 1:frame->ch_layout.nb_channels)+(planar ? 0:channel))*width;
    switch (fmt) {
        case AV_SAMPLE_FMT_FLT: {float v;memcpy(&v,p,4);return v;}
        case AV_SAMPLE_FMT_DBL: {double v;memcpy(&v,p,8);return v;}
        case AV_SAMPLE_FMT_S16: {int16_t v;memcpy(&v,p,2);return v/32768.0;}
        case AV_SAMPLE_FMT_S32: {int32_t v;memcpy(&v,p,4);return v/2147483648.0;}
        case AV_SAMPLE_FMT_U8: return (*p-128)/128.0;
        default: return 0; // validated by SFDecoderRead before this function
    }
}
OSStatus SFDecoderRead(SFDecoder *d, AudioConverterRef converter, AudioConverterComplexInputDataProc callback, void *user, UInt32 *frames, AudioBufferList *out) {
    if (!d || !frames || !out) return kAudio_ParamError;
    if (!d->opened) {OSStatus e=open_decoder(d);if(e)return e;}
    UInt32 wanted=*frames,channels=d->input.mChannelsPerFrame; *frames=0;
    if (wanted > UINT32_MAX/(channels*sizeof(float))) return -50;
    size_t bytes=(size_t)wanted*channels*sizeof(float);
    if (bytes>d->pcmCapacity) {void *p=realloc(d->pcm,bytes);if(!p)return -108;d->pcm=p;d->pcmCapacity=bytes;}
    for (;;) {
        if (d->frameOffset<d->frame->nb_samples) {
            enum AVSampleFormat fmt=av_get_packed_sample_fmt(d->frame->format);
            if (d->frame->ch_layout.nb_channels != channels || d->frame->sample_rate != (int)d->input.mSampleRate ||
                (fmt!=AV_SAMPLE_FMT_FLT && fmt!=AV_SAMPLE_FMT_DBL && fmt!=AV_SAMPLE_FMT_S16 && fmt!=AV_SAMPLE_FMT_S32 && fmt!=AV_SAMPLE_FMT_U8)) return kAudioConverterErr_FormatNotSupported;
            UInt32 n=d->frame->nb_samples-d->frameOffset;
            if(d->skip) {UInt32 drop=d->skip<n?(UInt32)d->skip:n;d->skip-=drop;d->frameOffset+=drop;continue;}
            if(n>wanted)n=wanted;
            for(UInt32 i=0;i<n;i++)for(UInt32 c=0;c<channels;c++)d->pcm[i*channels+c]=(float)sample(d->frame,c,d->frameOffset+i);
            d->frameOffset+=n;*frames=n;out->mNumberBuffers=1;out->mBuffers[0]=(AudioBuffer){channels,n*channels*sizeof(float),d->pcm};return noErr;
        }
        av_frame_unref(d->frame); d->frameOffset=0;
        int result=avcodec_receive_frame(d->codec,d->frame);
        if(result==0)continue;
        if(result==AVERROR_EOF){out->mNumberBuffers=1;out->mBuffers[0]=(AudioBuffer){channels,0,NULL};return noErr;}
        if(result!=AVERROR(EAGAIN))return kAudioConverterErr_InvalidInputSize;
        if(d->eof) {
            if(d->flushed)return kAudioConverterErr_InvalidInputSize;
            if(avcodec_send_packet(d->codec,NULL)<0)return kAudioConverterErr_InvalidInputSize;
            d->flushed=true;continue;
        }
        UInt32 packets=1;AudioBufferList input={1,{{channels,0,NULL}}};AudioStreamPacketDescription *descs=NULL;
        OSStatus status=callback(converter,&packets,&input,&descs,user);if(status)return status;
        if(!packets){d->eof=true;continue;}
        if(packets!=1 || input.mNumberBuffers!=1 || !input.mBuffers[0].mData)return kAudioConverterErr_InvalidInputSize;
        UInt32 size=descs?descs[0].mDataByteSize:input.mBuffers[0].mDataByteSize;
        int64_t offset=descs?descs[0].mStartOffset:0;
        if(offset<0 || (uint64_t)offset>input.mBuffers[0].mDataByteSize || size>input.mBuffers[0].mDataByteSize-(uint64_t)offset || size>INT_MAX)return kAudioConverterErr_InvalidInputSize;
        if(av_new_packet(d->packet,size)<0)return -108;
        memcpy(d->packet->data,(uint8_t*)input.mBuffers[0].mData+offset,size);
        result=avcodec_send_packet(d->codec,d->packet);av_packet_unref(d->packet);
        if(result<0)return kAudioConverterErr_InvalidInputSize;
    }
}
OSStatus SFDecoderCopyCookie(SFDecoder *d, UInt32 *size, void *out) {
    if (!d || !size) return -50;
    if (out && *size<d->cookieSize) return kAudioConverterErr_BadPropertySizeError;
    if (out && d->cookieSize) memcpy(out,d->cookie,d->cookieSize);
    *size=d->cookieSize; return noErr;
}
