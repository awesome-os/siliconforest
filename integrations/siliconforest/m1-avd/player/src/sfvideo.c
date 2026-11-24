/* SPDX-License-Identifier: MIT */
/*
 * sfvideo: a small video player for the SiliconForest desktop on the FFmpeg libraries.
 *
 *   sfvideo [options] <file>
 *
 * Demuxes with libavformat, decodes video and audio with libavcodec, shows the picture in an
 * xdg-toplevel window (wl_shm, XRGB8888, converted and scaled by libswscale to the window's
 * size in device pixels: wp_viewporter + wp_fractional_scale), plays the sound through an
 * AudioQueue (coreaudiod) and keeps the picture on the audio clock. The clock and the frame
 * timing are ffplay's (ffsync.h); late frames are dropped.
 *
 * Hardware decoding uses only FFmpeg's generic interface (avcodec_get_hw_config, hw_device_ctx,
 * get_format, av_hwframe_transfer_data), so a hwaccel added to the FFmpeg build is found by
 * name: --hwdec=sfavd. The default tries `sfavd` and falls back to software.
 *
 * Threads: main (Wayland, frame timing, display), demux, video decode, audio decode, and the
 * audio output's own thread.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#ifdef SFVIDEO_GL
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

#include "ao.h"
#include "ffsync.h"

#define FRAME_QUEUE 4
#define MAX_SHM_BUFFERS 4
#define MAX_QUEUE_BYTES (16 * 1024 * 1024)
#define MIN_QUEUE_PACKETS 50
#define MAX_CONSECUTIVE_DROPS 10    /* a decoder that never catches up still shows a frame now and then */
#define AUDIO_FIFO_MS 400

static double now(void) { return av_gettime_relative() / 1000000.0; }

/* ---------------------------------------------------------------- statistics */

struct stat_ms { double sum, max; long n; };
static void stat_add(struct stat_ms *s, double seconds)
{
    double ms = seconds * 1000.0;
    s->sum += ms;
    if (ms > s->max)
        s->max = ms;
    s->n++;
}
static double stat_mean(const struct stat_ms *s) { return s->n ? s->sum / s->n : 0; }

/* ---------------------------------------------------------------- options */

static struct {
    const char *file, *hwdec, *vo, *ao, *snapshot;
    int no_audio, stats, untimed, framedrop, threads, fullscreen;
    long frames;
    double duration, volume, audio_delay;
    int width, height;
} opt = { .hwdec = "auto", .vo = "shm", .ao = "auto", .framedrop = 1, .volume = 100 };

/* ---------------------------------------------------------------- packet queue */

struct pkt_node { AVPacket *pkt; struct pkt_node *next; };
struct pkt_queue {
    struct pkt_node *first, *last;
    int nb, eof, abort;
    int64_t bytes;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static void pq_init(struct pkt_queue *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void pq_put(struct pkt_queue *q, AVPacket *pkt)   /* takes the packet; NULL: end of stream */
{
    struct pkt_node *n = NULL;

    if (pkt) {
        n = av_mallocz(sizeof(*n));
        if (!n) {
            av_packet_free(&pkt);
            return;
        }
        n->pkt = pkt;
    }
    pthread_mutex_lock(&q->lock);
    if (!n) {
        q->eof = 1;
    } else {
        if (q->last)
            q->last->next = n;
        else
            q->first = n;
        q->last = n;
        q->nb++;
        q->bytes += pkt->size;
    }
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

/* 1: *pkt is a packet; 0: end of stream; -1: aborted */
static int pq_get(struct pkt_queue *q, AVPacket **pkt)
{
    int ret;

    pthread_mutex_lock(&q->lock);
    for (;;) {
        if (q->abort) {
            ret = -1;
            break;
        }
        if (q->first) {
            struct pkt_node *n = q->first;

            q->first = n->next;
            if (!q->first)
                q->last = NULL;
            q->nb--;
            q->bytes -= n->pkt->size;
            *pkt = n->pkt;
            av_free(n);
            ret = 1;
            break;
        }
        if (q->eof) {
            ret = 0;
            break;
        }
        pthread_cond_wait(&q->cond, &q->lock);
    }
    pthread_mutex_unlock(&q->lock);
    return ret;
}

static void pq_abort(struct pkt_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->abort = 1;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

static void pq_flush(struct pkt_queue *q)
{
    while (q->first) {
        struct pkt_node *n = q->first;

        q->first = n->next;
        av_packet_free(&n->pkt);
        av_free(n);
    }
}

/* ---------------------------------------------------------------- player state */

struct vframe { AVFrame *frame; double pts, duration; };

static struct {
    AVFormatContext *ic;
    int vindex, aindex;
    AVStream *vst, *ast;
    AVCodecContext *vctx, *actx;
    struct pkt_queue vq, aq;
    pthread_t demux_thread, video_thread, audio_thread;
    int have_demux, have_video, have_audio;
    volatile sig_atomic_t quit;

    /* hardware decoding */
    AVBufferRef *hw_device;
    enum AVPixelFormat hw_pix_fmt;
    const char *hw_name;        /* the hwaccel in use, or "none" */
    int hw_frames_seen;
    enum AVPixelFormat shown_fmt;

    /* decoded pictures, written by the video thread, read by main */
    struct vframe fq[FRAME_QUEUE];
    int fq_read, fq_count;
    pthread_mutex_t fq_lock;
    pthread_cond_t fq_cond;
    int video_done;             /* the decoder has delivered its last frame */

    /* timing (main thread) */
    Clock vidclk, audclk;
    pthread_mutex_t clock_lock; /* audclk is set from the audio side too */
    double frame_timer, max_frame_duration;
    double last_pts, last_duration;
    int have_last;
    AVFrame *last_frame;        /* the picture on screen, for a redraw after a resize */
    double play_start;

    /* audio */
    const struct ao_driver *aod;
    struct ao *ao;
    int ao_rate, ao_started, audio_done;
    uint8_t *fifo;
    int fifo_size, fifo_fill, fifo_read;
    double fifo_end_pts;
    int fifo_eof;
    pthread_mutex_t fifo_lock;
    pthread_cond_t fifo_cond;

    /* numbers */
    long decoded, shown, drops_early, drops_late, drops_nobuffer, consecutive_drops;
    struct stat_ms decode, transfer, present, ack, drift;
    double drift_max_abs;
    pthread_mutex_t stat_lock;
} P;

static double master_clock(void)
{
    double v;

    pthread_mutex_lock(&P.clock_lock);
    v = get_clock(&P.audclk);
    pthread_mutex_unlock(&P.clock_lock);
    return v;
}

/* ---------------------------------------------------------------- demux */

static void *demux_main(void *arg)
{
    (void)arg;
    for (;;) {
        AVPacket *pkt;
        int ret;

        if (P.quit)
            break;
        /* enough queued: wait (as ffplay does) */
        pthread_mutex_lock(&P.vq.lock);
        int vn = P.vq.nb;
        int64_t vb = P.vq.bytes;
        pthread_mutex_unlock(&P.vq.lock);
        pthread_mutex_lock(&P.aq.lock);
        int an = P.aq.nb;
        int64_t ab = P.aq.bytes;
        pthread_mutex_unlock(&P.aq.lock);
        if (vb + ab > MAX_QUEUE_BYTES ||
            ((!P.have_video || vn > MIN_QUEUE_PACKETS) && (!P.have_audio || an > MIN_QUEUE_PACKETS))) {
            av_usleep(10000);
            continue;
        }
        pkt = av_packet_alloc();
        if (!pkt)
            break;
        ret = av_read_frame(P.ic, pkt);
        if (ret < 0) {
            av_packet_free(&pkt);
            if (ret != AVERROR_EOF)
                fprintf(stderr, "sfvideo: read error: %s\n", av_err2str(ret));
            break;
        }
        if (P.have_video && pkt->stream_index == P.vindex)
            pq_put(&P.vq, pkt);
        else if (P.have_audio && pkt->stream_index == P.aindex)
            pq_put(&P.aq, pkt);
        else
            av_packet_free(&pkt);
    }
    pq_put(&P.vq, NULL);
    pq_put(&P.aq, NULL);
    return NULL;
}

/* ---------------------------------------------------------------- video decode */

static enum AVPixelFormat get_format(AVCodecContext *ctx, const enum AVPixelFormat *fmts)
{
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == P.hw_pix_fmt)
            return *p;
    }
    fprintf(stderr, "sfvideo: the decoder does not offer %s for this stream; decoding in software\n",
            av_get_pix_fmt_name(P.hw_pix_fmt));
    P.hw_name = "none";
    return avcodec_default_get_format(ctx, fmts);
}

/* Chooses the hardware decoder by name; 0 when one is set up. Only FFmpeg's generic API. */
static int setup_hwdec(const AVCodec *codec, AVCodecContext *ctx)
{
    int explicit = strcmp(opt.hwdec, "auto") != 0;
    const char *name = explicit ? opt.hwdec : "sfavd";
    enum AVHWDeviceType type;
    int ret;

    P.hw_name = "none";
    P.hw_pix_fmt = AV_PIX_FMT_NONE;
    if (!strcmp(opt.hwdec, "none") || !strcmp(opt.hwdec, "no"))
        return -1;
    type = av_hwdevice_find_type_by_name(name);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        if (explicit) {
            fprintf(stderr, "sfvideo: this FFmpeg has no hardware device type \"%s\" (it has:", name);
            while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
                fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
            fprintf(stderr, "); decoding in software\n");
        }
        return -1;
    }
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);

        if (!config) {
            fprintf(stderr, "sfvideo: decoder %s has no %s hwaccel; decoding in software\n", codec->name, name);
            return -1;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type) {
            P.hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
    if ((ret = av_hwdevice_ctx_create(&P.hw_device, type, NULL, NULL, 0)) < 0) {
        fprintf(stderr, "sfvideo: cannot open the %s device: %s; decoding in software\n", name, av_err2str(ret));
        P.hw_pix_fmt = AV_PIX_FMT_NONE;
        return -1;
    }
    ctx->hw_device_ctx = av_buffer_ref(P.hw_device);
    ctx->get_format = get_format;
    P.hw_name = av_hwdevice_get_type_name(type);
    return 0;
}

/* A hardware frame becomes a software frame (NV12 when the device offers it). */
static int download(AVFrame **frame)
{
    AVFrame *sw = av_frame_alloc();
    enum AVPixelFormat *formats = NULL;
    double t0 = now();
    int ret;

    if (!sw)
        return AVERROR(ENOMEM);
    if (av_hwframe_transfer_get_formats((*frame)->hw_frames_ctx, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0) >= 0) {
        sw->format = formats[0];
        for (enum AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; f++) {
            if (*f == AV_PIX_FMT_NV12) {
                sw->format = *f;
                break;
            }
        }
        av_free(formats);
    }
    if ((ret = av_hwframe_transfer_data(sw, *frame, 0)) < 0 || (ret = av_frame_copy_props(sw, *frame)) < 0) {
        av_frame_free(&sw);
        return ret;
    }
    pthread_mutex_lock(&P.stat_lock);
    stat_add(&P.transfer, now() - t0);
    P.hw_frames_seen++;
    pthread_mutex_unlock(&P.stat_lock);
    av_frame_free(frame);
    *frame = sw;
    return 0;
}

/* blocks while the queue is full; takes the frame */
static int fq_push(AVFrame *frame, double pts, double duration)
{
    pthread_mutex_lock(&P.fq_lock);
    while (P.fq_count >= FRAME_QUEUE && !P.quit)
        pthread_cond_wait(&P.fq_cond, &P.fq_lock);
    if (P.quit) {
        pthread_mutex_unlock(&P.fq_lock);
        av_frame_free(&frame);
        return -1;
    }
    struct vframe *vf = &P.fq[(P.fq_read + P.fq_count) % FRAME_QUEUE];
    vf->frame = frame;
    vf->pts = pts;
    vf->duration = duration;
    P.fq_count++;
    pthread_mutex_unlock(&P.fq_lock);
    return 0;
}

static void *video_main(void *arg)
{
    AVRational tb = P.vst->time_base;
    AVRational fr = av_guess_frame_rate(P.ic, P.vst, NULL);
    double spent = 0;       /* time inside the decoder since the last frame came out */
    int draining = 0;

    (void)arg;
    while (!P.quit) {
        AVPacket *pkt = NULL;
        int got = draining ? 0 : pq_get(&P.vq, &pkt);
        double t0;
        int ret;

        if (got < 0)
            break;
        if (got == 0)
            draining = 1;
        t0 = now();
        ret = avcodec_send_packet(P.vctx, pkt);
        spent += now() - t0;
        av_packet_free(&pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            fprintf(stderr, "sfvideo: video packet refused: %s\n", av_err2str(ret));
        for (;;) {
            AVFrame *frame = av_frame_alloc();
            double pts, duration;

            if (!frame)
                goto out;
            t0 = now();
            ret = avcodec_receive_frame(P.vctx, frame);
            spent += now() - t0;
            if (ret < 0) {
                av_frame_free(&frame);
                if (ret == AVERROR_EOF)
                    goto out;
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "sfvideo: video decode error: %s\n", av_err2str(ret));
                break;
            }
            pthread_mutex_lock(&P.stat_lock);
            P.decoded++;
            stat_add(&P.decode, spent);
            pthread_mutex_unlock(&P.stat_lock);
            spent = 0;
            if (frame->hw_frames_ctx && (ret = download(&frame)) < 0) {
                fprintf(stderr, "sfvideo: av_hwframe_transfer_data failed: %s\n", av_err2str(ret));
                av_frame_free(&frame);
                continue;
            }
            pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? NAN : frame->best_effort_timestamp * av_q2d(tb);
            duration = frame->duration > 0 ? frame->duration * av_q2d(tb) : (fr.num && fr.den ? av_q2d(av_inv_q(fr)) : 0.04);
            /* ffplay's early drop: a frame that is already late and has successors waiting */
            if (opt.framedrop && !opt.untimed && !isnan(pts)) {
                double diff = pts - master_clock();

                pthread_mutex_lock(&P.vq.lock);
                int waiting = P.vq.nb;
                pthread_mutex_unlock(&P.vq.lock);
                pthread_mutex_lock(&P.stat_lock);
                int drop = !isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD && diff < 0 && waiting &&
                           P.consecutive_drops < MAX_CONSECUTIVE_DROPS;
                if (drop) {
                    P.drops_early++;
                    P.consecutive_drops++;
                }
                pthread_mutex_unlock(&P.stat_lock);
                if (drop) {
                    av_frame_free(&frame);
                    continue;
                }
            }
            if (fq_push(frame, pts, duration) < 0)
                goto out;
        }
        if (draining && ret == AVERROR_EOF)
            break;
    }
out:
    pthread_mutex_lock(&P.fq_lock);
    P.video_done = 1;
    pthread_mutex_unlock(&P.fq_lock);
    return NULL;
}

/* ---------------------------------------------------------------- audio */

/* the audio output asks for samples (its own thread) */
static int audio_fill(void *user, uint8_t *dst, int bytes, double *end_pts)
{
    int n;

    (void)user;
    pthread_mutex_lock(&P.fifo_lock);
    n = FFMIN(bytes, P.fifo_fill) & ~3;
    if (n == 0 && P.fifo_eof) {
        pthread_mutex_unlock(&P.fifo_lock);
        return -1;
    }
    for (int done = 0; done < n;) {
        int chunk = FFMIN(n - done, P.fifo_size - P.fifo_read);

        memcpy(dst + done, P.fifo + P.fifo_read, (size_t)chunk);
        P.fifo_read = (P.fifo_read + chunk) % P.fifo_size;
        done += chunk;
    }
    P.fifo_fill -= n;
    *end_pts = n ? P.fifo_end_pts - (double)P.fifo_fill / (P.ao_rate * 4) : NAN;
    pthread_cond_broadcast(&P.fifo_cond);
    pthread_mutex_unlock(&P.fifo_lock);
    return n;
}

static int fifo_write(const uint8_t *src, int bytes, double end_pts)
{
    while (bytes > 0) {
        pthread_mutex_lock(&P.fifo_lock);
        while (P.fifo_fill >= P.fifo_size && !P.quit)
            pthread_cond_wait(&P.fifo_cond, &P.fifo_lock);
        if (P.quit) {
            pthread_mutex_unlock(&P.fifo_lock);
            return -1;
        }
        int at = (P.fifo_read + P.fifo_fill) % P.fifo_size;
        int chunk = FFMIN(FFMIN(bytes, P.fifo_size - P.fifo_fill), P.fifo_size - at);

        memcpy(P.fifo + at, src, (size_t)chunk);
        P.fifo_fill += chunk;
        src += chunk;
        bytes -= chunk;
        P.fifo_end_pts = end_pts - (double)bytes / (P.ao_rate * 4);
        pthread_mutex_unlock(&P.fifo_lock);
    }
    return 0;
}

static void audio_clock_poll(void)
{
    double pts, at;

    if (!P.ao || !P.ao_started || P.audio_done)
        return;
    if (P.aod->clock(P.ao, &pts, &at)) {
        pthread_mutex_lock(&P.clock_lock);
        set_clock_at(&P.audclk, pts + opt.audio_delay, at);
        pthread_mutex_unlock(&P.clock_lock);
    }
}

static void *audio_main(void *arg)
{
    AVRational tb = P.ast->time_base;
    SwrContext *swr = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AVFrame *frame = av_frame_alloc();
    uint8_t *out = NULL;
    int out_cap = 0, in_rate = 0, in_fmt = -1, in_ch = 0;
    double next_pts = NAN;
    int draining = 0;

    (void)arg;
    while (!P.quit && frame) {
        AVPacket *pkt = NULL;
        int got = draining ? 0 : pq_get(&P.aq, &pkt);
        int ret;

        if (got < 0)
            break;
        if (got == 0)
            draining = 1;
        ret = avcodec_send_packet(P.actx, pkt);
        av_packet_free(&pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            fprintf(stderr, "sfvideo: audio packet refused: %s\n", av_err2str(ret));
        while ((ret = avcodec_receive_frame(P.actx, frame)) >= 0) {
            if (!swr || frame->sample_rate != in_rate || frame->format != in_fmt || frame->ch_layout.nb_channels != in_ch) {
                swr_free(&swr);
                if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, P.ao_rate, &frame->ch_layout, frame->format,
                                        frame->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) {
                    fprintf(stderr, "sfvideo: cannot convert the audio format\n");
                    swr_free(&swr);
                    goto out;
                }
                in_rate = frame->sample_rate;
                in_fmt = frame->format;
                in_ch = frame->ch_layout.nb_channels;
            }
            int want = swr_get_out_samples(swr, frame->nb_samples);
            if (want * 4 > out_cap) {
                av_free(out);
                out_cap = want * 4 + 4096;
                out = av_malloc((size_t)out_cap);
                if (!out)
                    goto out;
            }
            int n = swr_convert(swr, &out, out_cap / 4, (const uint8_t **)frame->extended_data, frame->nb_samples);
            double pts = frame->pts == AV_NOPTS_VALUE ? next_pts : frame->pts * av_q2d(tb);
            next_pts = pts + (double)frame->nb_samples / frame->sample_rate;
            /* what the resampler still holds comes out with the next frame */
            double end = next_pts - (double)swr_get_delay(swr, P.ao_rate) / P.ao_rate;
            av_frame_unref(frame);
            if (n > 0 && fifo_write(out, n * 4, end) < 0)
                goto out;
            if (!P.ao_started) {
                pthread_mutex_lock(&P.fifo_lock);
                int ready = P.fifo_fill >= P.fifo_size / 2;
                pthread_mutex_unlock(&P.fifo_lock);
                if (ready) {
                    if (P.aod->start(P.ao) < 0)
                        goto out;
                    P.ao_started = 1;
                }
            }
        }
        if (draining && ret == AVERROR_EOF)
            break;
    }
out:
    pthread_mutex_lock(&P.fifo_lock);
    P.fifo_eof = 1;
    pthread_mutex_unlock(&P.fifo_lock);
    if (!P.ao_started && !P.quit && P.fifo_fill > 0 && P.aod->start(P.ao) == 0)
        P.ao_started = 1;
    av_frame_free(&frame);
    av_free(out);
    swr_free(&swr);
    return NULL;
}

/* A stand-in output that takes samples at the system clock's rate: for --ao=null and for a
 * system without an audio device. The picture is then timed exactly as with real audio. */
struct ao {
    pthread_t thread;
    int rate, stop, ended;
    ao_fill_fn fill;
    void *user;
    pthread_mutex_t lock;
    double pts, at;
    long underruns;
};

static void *null_main(void *arg)
{
    struct ao *ao = arg;
    uint8_t buf[192000 / 100 * 4];
    int bytes = ao->rate / 100 * 4;
    double next = now();

    while (!ao->stop) {
        double end = NAN;
        int n = ao->fill(ao->user, buf, bytes, &end);

        if (n < 0)
            break;
        pthread_mutex_lock(&ao->lock);
        if (n > 0 && !isnan(end)) {
            /* the chunk just taken starts playing now */
            ao->pts = end - (double)n / (ao->rate * 4);
            ao->at = now();
        } else if (!isnan(ao->pts)) {
            ao->underruns++;
        }
        pthread_mutex_unlock(&ao->lock);
        next += 0.01;
        double wait = next - now();
        if (wait > 0)
            av_usleep((unsigned)(wait * 1e6));
        else if (wait < -0.5)
            next = now();
    }
    pthread_mutex_lock(&ao->lock);
    ao->ended = 1;
    pthread_mutex_unlock(&ao->lock);
    return NULL;
}

static int null_rate(void) { return 48000; }
static struct ao *null_open(int rate, double volume, ao_fill_fn fill, void *user)
{
    struct ao *ao = calloc(1, sizeof(*ao));

    (void)volume;
    if (!ao)
        return NULL;
    ao->rate = rate;
    ao->fill = fill;
    ao->user = user;
    ao->pts = NAN;
    pthread_mutex_init(&ao->lock, NULL);
    return ao;
}
static int null_start(struct ao *ao) { return pthread_create(&ao->thread, NULL, null_main, ao) ? -1 : 0; }
static int null_clock(struct ao *ao, double *pts, double *at)
{
    pthread_mutex_lock(&ao->lock);
    *pts = ao->pts;
    *at = ao->at;
    pthread_mutex_unlock(&ao->lock);
    return !isnan(*pts);
}
static int null_drained(struct ao *ao)
{
    pthread_mutex_lock(&ao->lock);
    int e = ao->ended;
    pthread_mutex_unlock(&ao->lock);
    return e;
}
static long null_underruns(struct ao *ao) { return ao->underruns; }
static double null_latency(struct ao *ao) { (void)ao; return 0; }
static void null_close(struct ao *ao)
{
    ao->stop = 1;
    if (P.ao_started)
        pthread_join(ao->thread, NULL);
    free(ao);
}
const struct ao_driver ao_null = {
    .name = "null", .preferred_rate = null_rate, .open = null_open, .start = null_start, .clock = null_clock,
    .drained = null_drained, .underruns = null_underruns, .latency = null_latency, .close = null_close,
};

/* ---------------------------------------------------------------- Wayland window */

struct shm_buffer {
    struct wl_buffer *buffer;
    uint8_t *map;
    size_t size;
    int width, height, busy, stale;
};

static struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_manager;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional;
    struct wl_callback *frame_callback;
    double frame_callback_at;
    uint32_t compositor_version;
    int scale120;               /* the surface's scale in 120ths */
    int fractional_seen, integer_scale;
    int output_scale;
    int configured, closed;
    int pending_w, pending_h;   /* the last xdg_toplevel.configure, surface units, 0 = our choice */
    int bounds_w, bounds_h;
    int output_w, output_h;     /* the output's current mode, device pixels */
    int width, height;          /* surface units */
    int video_w, video_h;       /* display size of the picture in video pixels (SAR applied) */
    int need_redraw;
    struct shm_buffer buffers[MAX_SHM_BUFFERS];
    struct SwsContext *sws;
    int announced_w, announced_h, announced_scale;
    struct shm_buffer *last_buffer;     /* the one committed last */
} W = { .scale120 = 120, .output_scale = 1 };

static int vo_window(void) { return strcmp(opt.vo, "null") != 0; }
static int vo_gl(void) { return !strcmp(opt.vo, "gl"); }
/* 1 when a wl_shm buffer can be had now (the compositor has released one, or a slot is empty) */
static int buffer_available(void)
{
    for (int i = 0; i < MAX_SHM_BUFFERS; i++) {
        if (!W.buffers[i].buffer || !W.buffers[i].busy)
            return 1;
    }
    return 0;
}

/* surface units to device pixels, rounding as wp_fractional_scale_v1 prescribes */
static int to_pixels(int v) { return (v * W.scale120 + 60) / 120; }
static void frame_request(void);

/* the rectangle of the picture in a bw x bh buffer: the largest one of the picture's shape */
static void picture_rect(int bw, int bh, int *dx, int *dy, int *dw, int *dh)
{
    if ((int64_t)bw * W.video_h <= (int64_t)bh * W.video_w) {
        *dw = bw;
        *dh = FFMAX(1, (int)((int64_t)bw * W.video_h / W.video_w));
    } else {
        *dh = bh;
        *dw = FFMAX(1, (int)((int64_t)bh * W.video_w / W.video_h));
    }
    /* a pixel or two of rounding (a fractional scale) is filled, not letterboxed */
    if (bw - *dw <= 2 && bh - *dh <= 2) {
        *dw = bw;
        *dh = bh;
    }
    *dx = (bw - *dw) / 2;
    *dy = (bh - *dh) / 2;
}

static void announce(const char *what, int bw, int bh, int dx, int dy, int dw, int dh)
{
    if (W.announced_w == bw && W.announced_h == bh && W.announced_scale == W.scale120)
        return;
    fprintf(stderr, "sfvideo: window %dx%d surface units, scale %.4g, buffer %dx%d %s, picture %dx%d at %d,%d%s\n",
            W.width, W.height, W.scale120 / 120.0, bw, bh, what, dw, dh, dx, dy,
            W.viewport ? " (wp_viewport destination)" : " (integer buffer scale)");
    W.announced_w = bw;
    W.announced_h = bh;
    W.announced_scale = W.scale120;
}

#ifdef SFVIDEO_GL
/* ---------------------------------------------------------------- GL output
 * The decoded planes go into textures as they are (NV12: Y and interleaved CbCr; planar 4:2:0:
 * Y, Cb, Cr) and a fragment shader converts to RGB with the stream's matrix and range. Anything
 * else is first converted to NV12 by libswscale. GLES 2 only: luminance textures, and a row
 * longer than the picture (linesize) is uploaded whole and cut off by the texture coordinates. */
static struct {
    struct wl_egl_window *window;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    GLuint program, tex[3];
    GLint u_tex[3], u_matrix, u_offset, u_nv12, u_scale_y, u_scale_c, u_max_y, u_max_c, a_pos, a_uv;
    int tex_w[3], tex_h[3];
    int width, height;
    struct SwsContext *to_nv12;
    AVFrame *converted;
} G;

static GLuint gl_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";

        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "sfvideo: shader: %s\n", log);
        return 0;
    }
    return sh;
}

static int gl_init(void)
{
    static const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE };
    static const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    static const char *vs =
        "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_y; varying vec2 v_c;\n"
        "uniform vec2 u_scale_y, u_scale_c;\n"
        "void main() { v_y = a_uv * u_scale_y; v_c = a_uv * u_scale_c; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision highp float; varying vec2 v_y; varying vec2 v_c;\n"
        "uniform sampler2D u_y, u_c0, u_c1; uniform mat3 u_matrix; uniform vec3 u_offset; uniform float u_nv12;\n"
        "uniform vec2 u_max_y, u_max_c;\n"
        "void main() {\n"
        "  vec3 yuv;\n"
        "  vec2 pc = min(v_c, u_max_c);\n"      /* never a filtered sample of the row padding */
        "  yuv.x = texture2D(u_y, min(v_y, u_max_y)).r;\n"
        "  vec4 c = texture2D(u_c0, pc);\n"
        "  yuv.yz = mix(vec2(c.r, texture2D(u_c1, pc).r), c.ra, u_nv12);\n"
        "  gl_FragColor = vec4(clamp(u_matrix * (yuv + u_offset), 0.0, 1.0), 1.0);\n"
        "}\n";
    EGLConfig config;
    EGLint n = 0, major, minor;
    GLuint v, f;
    GLint ok = 0;

    G.width = to_pixels(W.width);
    G.height = to_pixels(W.height);
    {
        /*
         * The Wayland platform by name: eglGetDisplay() has to guess the platform from the
         * pointer, and on Darwin Mesa's guess is not Wayland (boot av20: no window
         * configuration at all). m1-mesa-asahi/test/agx-wl-demo.c opens it the same way.
         */
        typedef EGLDisplay (*get_platform_display)(EGLenum, void *, const EGLint *);
        get_platform_display get = (get_platform_display)eglGetProcAddress("eglGetPlatformDisplayEXT");

        G.display = get ? get(0x31D8 /* EGL_PLATFORM_WAYLAND_KHR */, W.display, NULL) : EGL_NO_DISPLAY;
        if (G.display == EGL_NO_DISPLAY)
            G.display = eglGetDisplay((EGLNativeDisplayType)W.display);
    }
    if (G.display == EGL_NO_DISPLAY || !eglInitialize(G.display, &major, &minor)) {
        fprintf(stderr, "sfvideo: EGL does not start on this Wayland display (error 0x%x)\n", eglGetError());
        return -1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(G.display, config_attrs, &config, 1, &n) || n < 1) {
        fprintf(stderr, "sfvideo: no EGL window configuration (error 0x%x)\n", eglGetError());
        return -1;
    }
    G.context = eglCreateContext(G.display, config, EGL_NO_CONTEXT, context_attrs);
    G.window = wl_egl_window_create(W.surface, G.width, G.height);
    G.surface = G.window ? eglCreateWindowSurface(G.display, config, (EGLNativeWindowType)G.window, NULL) : EGL_NO_SURFACE;
    if (G.context == EGL_NO_CONTEXT || G.surface == EGL_NO_SURFACE || !eglMakeCurrent(G.display, G.surface, G.surface, G.context)) {
        fprintf(stderr, "sfvideo: no EGL window surface or context (error 0x%x)\n", eglGetError());
        return -1;
    }
    eglSwapInterval(G.display, 0);      /* the player times its frames itself */
    v = gl_shader(GL_VERTEX_SHADER, vs);
    f = gl_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return -1;
    G.program = glCreateProgram();
    glAttachShader(G.program, v);
    glAttachShader(G.program, f);
    glLinkProgram(G.program);
    glGetProgramiv(G.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "sfvideo: the shader program does not link\n");
        return -1;
    }
    glUseProgram(G.program);
    G.u_tex[0] = glGetUniformLocation(G.program, "u_y");
    G.u_tex[1] = glGetUniformLocation(G.program, "u_c0");
    G.u_tex[2] = glGetUniformLocation(G.program, "u_c1");
    G.u_matrix = glGetUniformLocation(G.program, "u_matrix");
    G.u_offset = glGetUniformLocation(G.program, "u_offset");
    G.u_nv12 = glGetUniformLocation(G.program, "u_nv12");
    G.u_scale_y = glGetUniformLocation(G.program, "u_scale_y");
    G.u_scale_c = glGetUniformLocation(G.program, "u_scale_c");
    G.u_max_y = glGetUniformLocation(G.program, "u_max_y");
    G.u_max_c = glGetUniformLocation(G.program, "u_max_c");
    G.a_pos = glGetAttribLocation(G.program, "a_pos");
    G.a_uv = glGetAttribLocation(G.program, "a_uv");
    glGenTextures(3, G.tex);
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, G.tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glUniform1i(G.u_tex[i], i);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    fprintf(stderr, "sfvideo: GL: EGL %d.%d, %s, %s\n", major, minor, (const char *)glGetString(GL_RENDERER),
            (const char *)glGetString(GL_VERSION));
    return 0;
}

static void gl_upload(int unit, GLenum format, const uint8_t *data, int texels_per_row, int rows)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, G.tex[unit]);
    if (G.tex_w[unit] != texels_per_row || G.tex_h[unit] != rows) {
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, texels_per_row, rows, 0, format, GL_UNSIGNED_BYTE, data);
        G.tex_w[unit] = texels_per_row;
        G.tex_h[unit] = rows;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texels_per_row, rows, format, GL_UNSIGNED_BYTE, data);
    }
}

/* YCbCr (as sampled, 0..1) to RGB for the frame's matrix and range: rgb = M * (yuv + offset) */
static void gl_colour(const AVFrame *f, float m[9], float off[3])
{
    int hd = f->height >= 720 || f->width >= 1280;
    int cs = f->colorspace == AVCOL_SPC_UNSPECIFIED ? (hd ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M) : f->colorspace;
    double kr = 0.299, kb = 0.114;
    double ys = 1.0, cscale = 1.0;

    if (cs == AVCOL_SPC_BT709) {
        kr = 0.2126;
        kb = 0.0722;
    } else if (cs == AVCOL_SPC_BT2020_NCL || cs == AVCOL_SPC_BT2020_CL) {
        kr = 0.2627;
        kb = 0.0593;
    }
    double kg = 1.0 - kr - kb;
    if (f->color_range != AVCOL_RANGE_JPEG) {
        ys = 255.0 / 219.0;
        cscale = 255.0 / 224.0;
        off[0] = (float)(-16.0 / 255.0);
    } else {
        off[0] = 0;
    }
    off[1] = off[2] = (float)(-128.0 / 255.0);
    /* column-major: columns are the Y, Cb and Cr contributions */
    m[0] = (float)ys;  m[1] = (float)ys;                                   m[2] = (float)ys;
    m[3] = 0;          m[4] = (float)(-2.0 * kb * (1.0 - kb) / kg * cscale); m[5] = (float)(2.0 * (1.0 - kb) * cscale);
    m[6] = (float)(2.0 * (1.0 - kr) * cscale); m[7] = (float)(-2.0 * kr * (1.0 - kr) / kg * cscale); m[8] = 0;
}

/* draws the frame into the back buffer (black when there is none); 0 when drawn */
static int gl_draw(AVFrame *f, int *bw_out, int *bh_out)
{
    int bw = to_pixels(W.width), bh = to_pixels(W.height);
    int dx, dy, dw, dh;

    if (bw != G.width || bh != G.height) {
        wl_egl_window_resize(G.window, bw, bh, 0, 0);
        G.width = bw;
        G.height = bh;
    }
    picture_rect(bw, bh, &dx, &dy, &dw, &dh);
    glViewport(0, 0, bw, bh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (f && f->format != AV_PIX_FMT_NV12 && f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
        /* other layouts and depths: NV12 through libswscale */
        if (!G.converted || G.converted->width != f->width || G.converted->height != f->height) {
            av_frame_free(&G.converted);
            G.converted = av_frame_alloc();
            if (!G.converted)
                return -1;
            G.converted->format = AV_PIX_FMT_NV12;
            G.converted->width = f->width;
            G.converted->height = f->height;
            if (av_frame_get_buffer(G.converted, 0) < 0)
                return -1;
        }
        G.to_nv12 = sws_getCachedContext(G.to_nv12, f->width, f->height, f->format, f->width, f->height, AV_PIX_FMT_NV12,
                                         SWS_POINT, NULL, NULL, NULL);
        if (!G.to_nv12)
            return -1;
        sws_scale(G.to_nv12, (const uint8_t *const *)f->data, f->linesize, 0, f->height, G.converted->data, G.converted->linesize);
        G.converted->colorspace = f->colorspace;
        G.converted->color_range = f->color_range;
        f = G.converted;
    }
    if (f) {
        int nv12 = f->format == AV_PIX_FMT_NV12;
        int cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
        float m[9], off[3];
        /* rows are uploaded with their padding: the picture is the left part of each texture */
        float row_y = (float)f->linesize[0], row_c = nv12 ? f->linesize[1] / 2.0f : (float)f->linesize[1];
        static const float quad[] = { -1, -1, 0, 1,   1, -1, 1, 1,   -1, 1, 0, 0,   1, 1, 1, 0 };

        glUniform2f(G.u_scale_y, f->width / row_y, 1.0f);
        glUniform2f(G.u_scale_c, cw / row_c, 1.0f);
        glUniform2f(G.u_max_y, (f->width - 0.5f) / row_y, 1.0f);
        glUniform2f(G.u_max_c, (cw - 0.5f) / row_c, 1.0f);

        gl_upload(0, GL_LUMINANCE, f->data[0], f->linesize[0], f->height);
        if (nv12) {
            gl_upload(1, GL_LUMINANCE_ALPHA, f->data[1], f->linesize[1] / 2, ch);
        } else {
            gl_upload(1, GL_LUMINANCE, f->data[1], f->linesize[1], ch);
            gl_upload(2, GL_LUMINANCE, f->data[2], f->linesize[2], ch);
        }
        gl_colour(f, m, off);
        glUniformMatrix3fv(G.u_matrix, 1, GL_FALSE, m);
        glUniform3fv(G.u_offset, 1, off);
        glUniform1f(G.u_nv12, nv12 ? 1.0f : 0.0f);
        glViewport(dx, bh - dy - dh, dw, dh);
        glVertexAttribPointer((GLuint)G.a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad);
        glVertexAttribPointer((GLuint)G.a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad + 2);
        glEnableVertexAttribArray((GLuint)G.a_pos);
        glEnableVertexAttribArray((GLuint)G.a_uv);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    announce("GL (EGL window)", bw, bh, dx, dy, dw, dh);
    *bw_out = bw;
    *bh_out = bh;
    return 0;
}

static int gl_show(AVFrame *f)
{
    int bw, bh;

    if (gl_draw(f, &bw, &bh) < 0)
        return -1;
    if (W.viewport) {
        wl_surface_set_buffer_scale(W.surface, 1);
        wp_viewport_set_destination(W.viewport, W.width, W.height);
    } else {
        wl_surface_set_buffer_scale(W.surface, W.scale120 / 120);
    }
    frame_request();
    /* attaches the rendered buffer and commits the surface */
    if (!eglSwapBuffers(G.display, G.surface)) {
        fprintf(stderr, "sfvideo: eglSwapBuffers failed (error 0x%x)\n", eglGetError());
        return -1;
    }
    wl_display_flush(W.display);
    return 0;
}

/* the last picture drawn once more and read back: what the GL path produces, as a PPM */
static void gl_snapshot(const char *path, AVFrame *last)
{
    int bw, bh;
    uint8_t *px;
    FILE *out;

    if (gl_draw(last, &bw, &bh) < 0 || !(px = malloc((size_t)bw * bh * 4)))
        return;
    glReadPixels(0, 0, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if ((out = fopen(path, "wb"))) {
        fprintf(out, "P6\n%d %d\n255\n", bw, bh);
        for (int y = bh - 1; y >= 0; y--) {
            for (int x = 0; x < bw; x++)
                fwrite(px + ((size_t)y * bw + x) * 4, 1, 3, out);
        }
        fclose(out);
    }
    free(px);
}
#endif /* SFVIDEO_GL */

static int shm_file(size_t size)
{
    static int serial;
    char name[64];
    int fd = -1;

    for (int tries = 0; tries < 16 && fd < 0; tries++) {
        snprintf(name, sizeof(name), "/pdv-%d-%d", (int)getpid(), serial++);   /* Darwin: at most 31 bytes */
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            shm_unlink(name);
        else if (errno != EEXIST)
            break;
    }
    if (fd < 0) {
        const char *dir = getenv("XDG_RUNTIME_DIR");
        char path[512];

        snprintf(path, sizeof(path), "%s/sfvideo-XXXXXX", dir && *dir ? dir : "/tmp");
        fd = mkstemp(path);
        if (fd >= 0)
            unlink(path);
    }
    if (fd < 0)
        return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *b)
{
    struct shm_buffer *sb = data;

    (void)b;
    sb->busy = 0;
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void buffer_destroy(struct shm_buffer *sb)
{
    if (sb->buffer)
        wl_buffer_destroy(sb->buffer);
    if (sb->map)
        munmap(sb->map, sb->size);
    if (W.last_buffer == sb)
        W.last_buffer = NULL;
    memset(sb, 0, sizeof(*sb));
}

static struct shm_buffer *buffer_get(int w, int h)
{
    struct shm_buffer *free_slot = NULL;

    for (int i = 0; i < MAX_SHM_BUFFERS; i++) {
        struct shm_buffer *sb = &W.buffers[i];

        if (sb->buffer && !sb->busy && (sb->width != w || sb->height != h))
            buffer_destroy(sb);
        if (sb->buffer && (sb->width != w || sb->height != h))
            sb->stale = 1;      /* still held by the compositor; goes when released */
        if (sb->buffer && !sb->busy && !sb->stale)
            return sb;
        if (!sb->buffer && !free_slot)
            free_slot = sb;
    }
    if (!free_slot)
        return NULL;
    size_t size = (size_t)w * h * 4;
    int fd = shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "sfvideo: cannot make a shared memory file: %s\n", strerror(errno));
        return NULL;
    }
    uint8_t *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "sfvideo: mmap of %zu bytes failed: %s\n", size, strerror(errno));
        close(fd);
        return NULL;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(W.shm, fd, (int32_t)size);
    free_slot->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    free_slot->map = map;
    free_slot->size = size;
    free_slot->width = w;
    free_slot->height = h;
    wl_buffer_add_listener(free_slot->buffer, &buffer_listener, free_slot);
    return free_slot;
}

/* the last committed buffer as a PPM: what the compositor was given, byte for byte */
static void window_snapshot(const char *path)
{
    struct shm_buffer *sb = W.last_buffer;
    FILE *f;

    if (!sb || !(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n%d %d\n255\n", sb->width, sb->height);
    for (size_t i = 0; i < (size_t)sb->width * sb->height; i++) {
        const uint8_t *px = sb->map + i * 4;       /* B G R X */
        uint8_t rgb[3] = { px[2], px[1], px[0] };

        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}


static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)data; (void)time;
    wl_callback_destroy(cb);
    if (cb == W.frame_callback) {
        W.frame_callback = NULL;
        stat_add(&P.ack, now() - W.frame_callback_at);
    }
}
static const struct wl_callback_listener frame_listener = { frame_done };

/* asks for the compositor's frame callback of the next commit, when none is outstanding */
static void frame_request(void)
{
    if (!W.frame_callback) {
        W.frame_callback = wl_surface_frame(W.surface);
        wl_callback_add_listener(W.frame_callback, &frame_listener, NULL);
        W.frame_callback_at = now();
    }
}

/* Converts the picture into a buffer of the window's size in device pixels (letterboxed) and
 * commits it. Returns 0 when it was shown. */
static int window_show(AVFrame *f)
{
    int bw = to_pixels(W.width), bh = to_pixels(W.height);
    struct shm_buffer *sb;
    int dw, dh, dx, dy;

    if (!W.configured || bw <= 0 || bh <= 0)
        return -1;
#ifdef SFVIDEO_GL
    if (vo_gl())
        return gl_show(f);
#endif
    if (!(sb = buffer_get(bw, bh)))
        return -1;
    picture_rect(bw, bh, &dx, &dy, &dw, &dh);
    if (dw != bw || dh != bh)
        memset(sb->map, 0, sb->size);
    if (f) {
        const uint8_t *const *src = (const uint8_t *const *)f->data;
        uint8_t *dst[4] = { sb->map + (size_t)dy * bw * 4 + (size_t)dx * 4, NULL, NULL, NULL };
        int dst_stride[4] = { bw * 4, 0, 0, 0 };
        int hd = f->height >= 720 || f->width >= 1280;
        int cs = f->colorspace == AVCOL_SPC_UNSPECIFIED ? (hd ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M) : f->colorspace;
        int full = f->color_range == AVCOL_RANGE_JPEG;

        W.sws = sws_getCachedContext(W.sws, f->width, f->height, f->format, dw, dh, AV_PIX_FMT_BGR0,
                                     dw == f->width && dh == f->height ? SWS_POINT : SWS_BILINEAR, NULL, NULL, NULL);
        if (!W.sws)
            return -1;
        /* the stream's matrix and range; RGB out is full range */
        sws_setColorspaceDetails(W.sws, sws_getCoefficients(cs), full, sws_getCoefficients(cs), 1, 0, 1 << 16, 1 << 16);
        sws_scale(W.sws, src, f->linesize, 0, f->height, dst, dst_stride);
    } else {
        memset(sb->map, 0, sb->size);
    }
    wl_surface_attach(W.surface, sb->buffer, 0, 0);
    if (W.viewport) {
        wl_surface_set_buffer_scale(W.surface, 1);
        wp_viewport_set_destination(W.viewport, W.width, W.height);
    } else {
        wl_surface_set_buffer_scale(W.surface, W.scale120 / 120);
    }
    if (W.compositor_version >= 4)
        wl_surface_damage_buffer(W.surface, 0, 0, bw, bh);
    else
        wl_surface_damage(W.surface, 0, 0, W.width, W.height);
    frame_request();
    wl_surface_commit(W.surface);
    sb->busy = 1;
    W.last_buffer = sb;
    wl_display_flush(W.display);
    announce("XRGB8888 (wl_shm)", bw, bh, dx, dy, dw, dh);
    return 0;
}

/* the window's size when the compositor leaves it to us: one video pixel per device pixel,
 * shrunk to the bounds the compositor gave, else to the output (less room for a title bar),
 * else to 1280x720 surface units */
static void window_default_size(int *w, int *h)
{
    int max_w = 1280, max_h = 720;

    if (W.bounds_w > 0 && W.bounds_h > 0) {
        max_w = W.bounds_w;
        max_h = W.bounds_h;
    } else if (W.output_w > 0 && W.output_h > 0) {
        max_w = W.output_w * 120 / W.scale120;
        max_h = FFMAX(64, W.output_h * 120 / W.scale120 - 48);
    }
    double sw = W.video_w * 120.0 / W.scale120, sh = W.video_h * 120.0 / W.scale120;
    double k = FFMIN(1.0, FFMIN(max_w / sw, max_h / sh));

    if (opt.width > 0 && opt.height > 0) {
        *w = opt.width;
        *h = opt.height;
        return;
    }
    *w = FFMAX(1, (int)lrint(sw * k));
    *h = FFMAX(1, (int)lrint(sh * k));
}

static void xdg_surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
    int w = W.pending_w, h = W.pending_h;

    (void)data;
    xdg_surface_ack_configure(s, serial);
    if (w <= 0 || h <= 0) {
        if (W.configured) {
            w = W.width;
            h = W.height;
        } else {
            window_default_size(&w, &h);
        }
    }
    if (w != W.width || h != W.height || !W.configured)
        W.need_redraw = 1;
    W.width = w;
    W.height = h;
    W.configured = 1;
}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
    (void)data; (void)t; (void)states;
    W.pending_w = w;
    W.pending_h = h;
}
static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    (void)data; (void)t;
    W.closed = 1;
}
static void toplevel_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h)
{
    (void)data; (void)t;
    W.bounds_w = w;
    W.bounds_h = h;
}
static void toplevel_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *caps)
{
    (void)data; (void)t; (void)caps;
}
static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close, toplevel_bounds, toplevel_capabilities,
};

static void wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_ping };

static void set_scale(int scale120)
{
    if (scale120 > 0 && scale120 != W.scale120) {
        W.scale120 = scale120;
        W.need_redraw = 1;
    }
}

static void fractional_preferred(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale)
{
    (void)data; (void)f;
    W.fractional_seen = 1;
    set_scale((int)scale);
}
static const struct wp_fractional_scale_v1_listener fractional_listener = { fractional_preferred };

static void surface_enter(void *d, struct wl_surface *s, struct wl_output *o) { (void)d; (void)s; (void)o; }
static void surface_leave(void *d, struct wl_surface *s, struct wl_output *o) { (void)d; (void)s; (void)o; }
static void surface_preferred_scale(void *d, struct wl_surface *s, int32_t factor)
{
    (void)d; (void)s;
    W.integer_scale = factor;
    if (!W.fractional_seen)
        set_scale(factor * 120);
}
static void surface_preferred_transform(void *d, struct wl_surface *s, uint32_t t) { (void)d; (void)s; (void)t; }
static const struct wl_surface_listener surface_listener = {
    surface_enter, surface_leave, surface_preferred_scale, surface_preferred_transform,
};

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
                            const char *make, const char *model, int32_t tr)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)tr;
}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
    (void)d; (void)o; (void)refresh;
    if ((flags & WL_OUTPUT_MODE_CURRENT) && w > W.output_w) {
        W.output_w = w;
        W.output_h = h;
    }
}
static void output_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void output_scale(void *d, struct wl_output *o, int32_t factor)
{
    (void)d; (void)o;
    if (factor > W.output_scale)
        W.output_scale = factor;
    if (!W.fractional_seen && !W.integer_scale)
        set_scale(W.output_scale * 120);
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry, .mode = output_mode, .done = output_done, .scale = output_scale,
};

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size)
{
    (void)d; (void)k; (void)format; (void)size;
    close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys)
{
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
    (void)d; (void)k; (void)serial; (void)s;
}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)d; (void)k; (void)serial; (void)time;
    /* evdev codes: Esc 1, Q 16 */
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && (key == 1 || key == 16))
        P.quit = 1;
}
static void kb_modifiers(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group)
{
    (void)d; (void)k; (void)serial; (void)dep; (void)lat; (void)lock; (void)group;
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) { (void)d; (void)k; (void)rate; (void)delay; }
static const struct wl_keyboard_listener keyboard_listener = { kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat };

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps)
{
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !W.keyboard) {
        W.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(W.keyboard, &keyboard_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *seat, const char *name) { (void)d; (void)seat; (void)name; }
static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

static void registry_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        W.compositor_version = FFMIN(version, 6);
        W.compositor = wl_registry_bind(r, name, &wl_compositor_interface, W.compositor_version);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        W.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        W.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, FFMIN(version, 5));
        xdg_wm_base_add_listener(W.wm_base, &wm_base_listener, NULL);
    } else if (!strcmp(iface, wp_viewporter_interface.name)) {
        W.viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    } else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name)) {
        W.fractional_manager = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
        W.decoration_manager = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (!strcmp(iface, wl_seat_interface.name) && !W.seat) {
        W.seat = wl_registry_bind(r, name, &wl_seat_interface, FFMIN(version, 4));
        wl_seat_add_listener(W.seat, &seat_listener, NULL);
    } else if (!strcmp(iface, wl_output_interface.name)) {
        struct wl_output *o = wl_registry_bind(r, name, &wl_output_interface, FFMIN(version, 2));
        wl_output_add_listener(o, &output_listener, NULL);
    }
}
static void registry_remove(void *data, struct wl_registry *r, uint32_t name) { (void)data; (void)r; (void)name; }
static const struct wl_registry_listener registry_listener = { registry_global, registry_remove };

static int window_open(const char *title)
{
    W.display = wl_display_connect(NULL);
    if (!W.display) {
        fprintf(stderr, "sfvideo: cannot connect to the Wayland display (WAYLAND_DISPLAY=%s, XDG_RUNTIME_DIR=%s)\n",
                getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "");
        return -1;
    }
    W.registry = wl_display_get_registry(W.display);
    wl_registry_add_listener(W.registry, &registry_listener, NULL);
    wl_display_roundtrip(W.display);
    wl_display_roundtrip(W.display);       /* the outputs' scale, the seat's capabilities */
    if (!W.compositor || !W.shm || !W.wm_base) {
        fprintf(stderr, "sfvideo: the compositor lacks wl_compositor, wl_shm or xdg_wm_base\n");
        return -1;
    }
    W.surface = wl_compositor_create_surface(W.compositor);
    if (W.compositor_version >= 6)
        wl_surface_add_listener(W.surface, &surface_listener, NULL);
    if (W.viewporter)
        W.viewport = wp_viewporter_get_viewport(W.viewporter, W.surface);
    if (W.fractional_manager && W.viewport) {
        W.fractional = wp_fractional_scale_manager_v1_get_fractional_scale(W.fractional_manager, W.surface);
        wp_fractional_scale_v1_add_listener(W.fractional, &fractional_listener, NULL);
    }
    W.xdg_surface = xdg_wm_base_get_xdg_surface(W.wm_base, W.surface);
    xdg_surface_add_listener(W.xdg_surface, &xdg_surface_listener, NULL);
    W.toplevel = xdg_surface_get_toplevel(W.xdg_surface);
    xdg_toplevel_add_listener(W.toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(W.toplevel, title);
    xdg_toplevel_set_app_id(W.toplevel, "org.siliconforest.sfvideo");
    if (W.decoration_manager) {
        struct zxdg_toplevel_decoration_v1 *deco =
            zxdg_decoration_manager_v1_get_toplevel_decoration(W.decoration_manager, W.toplevel);
        zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    if (opt.fullscreen)
        xdg_toplevel_set_fullscreen(W.toplevel, NULL);
    wl_surface_commit(W.surface);
    /* the first configure; a preferred scale usually comes with it */
    for (int i = 0; i < 50 && !W.configured; i++) {
        if (wl_display_roundtrip(W.display) < 0)
            return -1;
    }
    if (!W.configured) {
        fprintf(stderr, "sfvideo: the compositor did not configure the window\n");
        return -1;
    }
    fprintf(stderr, "sfvideo: Wayland: viewporter=%s fractional_scale=%s server_decorations=%s scale=%.4g\n",
            W.viewporter ? "yes" : "no", W.fractional_manager ? "yes" : "no", W.decoration_manager ? "yes" : "no",
            W.scale120 / 120.0);
    return 0;
}

/* dispatches what has arrived, waits at most `timeout` seconds for more */
static int window_pump(double timeout)
{
    struct pollfd pfd = { wl_display_get_fd(W.display), POLLIN, 0 };

    while (wl_display_prepare_read(W.display) != 0)
        wl_display_dispatch_pending(W.display);
    if (wl_display_flush(W.display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(W.display);
        return -1;
    }
    if (poll(&pfd, 1, timeout <= 0 ? 0 : (int)ceil(timeout * 1000)) > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_read_events(W.display) < 0)
            return -1;
    } else {
        wl_display_cancel_read(W.display);
        if (pfd.revents & (POLLERR | POLLHUP))
            return -1;
    }
    return wl_display_dispatch_pending(W.display) < 0 ? -1 : 0;
}

/* ---------------------------------------------------------------- presentation */

static void fq_next(void)
{
    pthread_mutex_lock(&P.fq_lock);
    P.fq_read = (P.fq_read + 1) % FRAME_QUEUE;
    P.fq_count--;
    pthread_cond_signal(&P.fq_cond);
    pthread_mutex_unlock(&P.fq_lock);
}

/* puts the frame on screen and makes it the current one; takes the frame */
static void present(AVFrame *frame, double pts, double duration)
{
    double t0 = now();
    int ok = 0;

    if (vo_window())
        ok = window_show(frame) == 0;
    else
        ok = 1;
    if (!P.have_audio && !isnan(pts) && isnan(master_clock())) {
        /* no audio: the system clock, started by the first picture */
        pthread_mutex_lock(&P.clock_lock);
        set_clock(&P.audclk, pts);
        pthread_mutex_unlock(&P.clock_lock);
    }
    if (ok) {
        double drift = pts - master_clock();

        stat_add(&P.present, now() - t0);
        P.shown++;
        if (!isnan(drift) && !opt.untimed) {
            stat_add(&P.drift, drift);      /* signed: mean() is the mean drift */
            P.drift_max_abs = FFMAX(P.drift_max_abs, fabs(drift) * 1000.0);
        }
        pthread_mutex_lock(&P.stat_lock);
        P.consecutive_drops = 0;
        pthread_mutex_unlock(&P.stat_lock);
    } else {
        P.drops_nobuffer++;
    }
    P.shown_fmt = frame->format;
    av_frame_free(&P.last_frame);
    P.last_frame = frame;
    P.last_pts = pts;
    P.last_duration = duration;
    P.have_last = 1;
}

/* ffplay's video_refresh: shows the next frame when it is due, drops the ones that are late */
static void video_refresh(double *remaining)
{
    for (;;) {
        struct vframe vf, next;
        int count;
        double time, delay, last_duration;

        pthread_mutex_lock(&P.fq_lock);
        count = P.fq_count;
        if (count > 0)
            vf = P.fq[P.fq_read];
        if (count > 1)
            next = P.fq[(P.fq_read + 1) % FRAME_QUEUE];
        pthread_mutex_unlock(&P.fq_lock);
        if (count == 0)
            return;
        if (opt.untimed) {
            /* nothing is dropped here: wait for the compositor to give a buffer back */
            if (vo_window() && !vo_gl() && !buffer_available()) {
                *remaining = 0.002;
                return;
            }
            present(vf.frame, vf.pts, vf.duration);
            fq_next();
            *remaining = 0;
            return;
        }
        if (!P.have_last)
            P.frame_timer = now();
        /* nominal duration of the frame on screen */
        last_duration = P.have_last ? vp_duration(P.last_pts, P.last_duration, vf.pts, P.max_frame_duration) : 0;
        delay = compute_target_delay(last_duration, get_clock(&P.vidclk), master_clock(), P.max_frame_duration);
        time = now();
        if (time < P.frame_timer + delay) {
            *remaining = FFMIN(P.frame_timer + delay - time, *remaining);
            return;
        }
        P.frame_timer += delay;
        if (delay > 0 && time - P.frame_timer > AV_SYNC_THRESHOLD_MAX)
            P.frame_timer = time;
        if (!isnan(vf.pts))
            set_clock(&P.vidclk, vf.pts);
        if (count > 1 && opt.framedrop) {
            double duration = vp_duration(vf.pts, vf.duration, next.pts, P.max_frame_duration);

            pthread_mutex_lock(&P.stat_lock);
            int drop = time > P.frame_timer + duration && P.consecutive_drops < MAX_CONSECUTIVE_DROPS;
            if (drop) {
                P.drops_late++;
                P.consecutive_drops++;
            }
            pthread_mutex_unlock(&P.stat_lock);
            if (drop) {
                /* it still becomes the reference for the next frame's duration */
                P.last_pts = vf.pts;
                P.last_duration = vf.duration;
                P.have_last = 1;
                av_frame_free(&vf.frame);
                fq_next();
                continue;
            }
        }
        present(vf.frame, vf.pts, vf.duration);
        fq_next();
        return;
    }
}

/* ---------------------------------------------------------------- setup */

static int open_stream(enum AVMediaType type, int *index, AVStream **st, AVCodecContext **ctx)
{
    const AVCodec *codec = NULL;
    int ret = av_find_best_stream(P.ic, type, -1, -1, &codec, 0);

    if (ret < 0 || !codec)
        return -1;
    *index = ret;
    *st = P.ic->streams[ret];
    *ctx = avcodec_alloc_context3(codec);
    if (!*ctx || avcodec_parameters_to_context(*ctx, (*st)->codecpar) < 0)
        return -1;
    (*ctx)->pkt_timebase = (*st)->time_base;
    if (type == AVMEDIA_TYPE_VIDEO) {
        if (setup_hwdec(codec, *ctx) < 0) {
            (*ctx)->thread_count = opt.threads;     /* 0: one per processor */
        }
    }
    if ((ret = avcodec_open2(*ctx, codec, NULL)) < 0) {
        fprintf(stderr, "sfvideo: cannot open the %s decoder: %s\n", codec->name, av_err2str(ret));
        avcodec_free_context(ctx);
        return -1;
    }
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    P.quit = 1;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: sfvideo [options] <file>\n"
        "  --hwdec=NAME      hardware decoder (an FFmpeg hw device type, e.g. sfavd); auto (default): sfavd when\n"
        "                    this FFmpeg has it and the device opens, else software; none: software\n"
        "  --vo=shm|gl|null  window through wl_shm (default); through EGL/GLES 2 with the YCbCr planes as\n"
        "                    textures (builds with GL only); or no window (decode and timing only)\n"
        "  --ao=auto|null    AudioQueue when there is an output device, else a clock-paced stand-in (default);\n"
        "                    null: the stand-in\n"
        "  --no-audio        ignore the audio stream (the picture runs on the system clock)\n"
        "  --volume=0..100   AudioQueue volume (default 100)\n"
        "  --audio-delay=MS  added to the audio clock\n"
        "  --frames=N        stop after N pictures have been shown or dropped\n"
        "  --duration=S      stop after S seconds of playing\n"
        "  --untimed         show every picture as soon as it is decoded: no sync, no drops (decoder throughput)\n"
        "  --no-framedrop    never drop a late picture\n"
        "  --threads=N       software decoder threads (default: one per processor)\n"
        "  --size=WxH        window size in surface units when the compositor leaves it to the client\n"
        "  --fullscreen\n"
        "  --stats           also print a status line every second (the totals are always printed at exit)\n"
        "  --snapshot=FILE   at exit write the last buffer given to the compositor as a PPM file\n"
        "  q or Esc in the window, or SIGINT/SIGTERM, ends it.\n");
}

static int parse_options(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        char *a = argv[i], *v = NULL, key[64];

        if (a[0] != '-' || a[1] != '-') {
            if (opt.file)
                return -1;
            opt.file = a;
            continue;
        }
        const char *eq = strchr(a, '=');
        snprintf(key, sizeof(key), "%.*s", eq ? (int)(eq - a - 2) : 63, a + 2);
        v = eq ? (char *)eq + 1 : NULL;
#define NEED() do { if (!v) { if (i + 1 >= argc) return -1; v = argv[++i]; } } while (0)
        if (!strcmp(key, "hwdec")) { NEED(); opt.hwdec = v; }
        else if (!strcmp(key, "vo")) { NEED(); opt.vo = v; }
        else if (!strcmp(key, "ao")) { NEED(); opt.ao = v; }
        else if (!strcmp(key, "no-audio")) opt.no_audio = 1;
        else if (!strcmp(key, "volume")) { NEED(); opt.volume = atof(v); }
        else if (!strcmp(key, "audio-delay")) { NEED(); opt.audio_delay = atof(v) / 1000.0; }
        else if (!strcmp(key, "frames")) { NEED(); opt.frames = atol(v); }
        else if (!strcmp(key, "duration")) { NEED(); opt.duration = atof(v); }
        else if (!strcmp(key, "untimed")) opt.untimed = 1;
        else if (!strcmp(key, "no-framedrop")) opt.framedrop = 0;
        else if (!strcmp(key, "threads")) { NEED(); opt.threads = atoi(v); }
        else if (!strcmp(key, "size")) { NEED(); if (sscanf(v, "%dx%d", &opt.width, &opt.height) != 2) return -1; }
        else if (!strcmp(key, "fullscreen")) opt.fullscreen = 1;
        else if (!strcmp(key, "stats")) opt.stats = 1;
        else if (!strcmp(key, "snapshot")) { NEED(); opt.snapshot = v; }
        else return -1;
#undef NEED
    }
    if (!opt.file || (strcmp(opt.ao, "auto") && strcmp(opt.ao, "null")))
        return -1;
    if (strcmp(opt.vo, "shm") && strcmp(opt.vo, "null")) {
#ifdef SFVIDEO_GL
        if (strcmp(opt.vo, "gl"))
            return -1;
#else
        if (!strcmp(opt.vo, "gl"))
            fprintf(stderr, "sfvideo: this build has no GL output (build-sfvideo.nix: gl = true)\n");
        return -1;
#endif
    }
    opt.volume = FFMIN(100, FFMAX(0, opt.volume));
    return 0;
}

static double tv(struct timeval t) { return t.tv_sec + t.tv_usec / 1e6; }

int main(int argc, char **argv)
{
    struct rusage ru0, ru1;
    double wall0 = now(), last_status = 0, video_end = 0;
    int ret = 1;

    getrusage(RUSAGE_SELF, &ru0);
    if (parse_options(argc, argv) < 0) {
        usage();
        return 2;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    av_log_set_level(AV_LOG_WARNING);
    pq_init(&P.vq);
    pq_init(&P.aq);
    pthread_mutex_init(&P.fq_lock, NULL);
    pthread_cond_init(&P.fq_cond, NULL);
    pthread_mutex_init(&P.clock_lock, NULL);
    pthread_mutex_init(&P.fifo_lock, NULL);
    pthread_cond_init(&P.fifo_cond, NULL);
    pthread_mutex_init(&P.stat_lock, NULL);
    init_clock(&P.vidclk);
    init_clock(&P.audclk);
    P.hw_name = "none";
    P.shown_fmt = AV_PIX_FMT_NONE;

    if ((ret = avformat_open_input(&P.ic, opt.file, NULL, NULL)) < 0) {
        fprintf(stderr, "sfvideo: %s: %s\n", opt.file, av_err2str(ret));
        return 1;
    }
    if ((ret = avformat_find_stream_info(P.ic, NULL)) < 0) {
        fprintf(stderr, "sfvideo: %s: no stream information: %s\n", opt.file, av_err2str(ret));
        return 1;
    }
    P.max_frame_duration = (P.ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    P.have_video = open_stream(AVMEDIA_TYPE_VIDEO, &P.vindex, &P.vst, &P.vctx) == 0;
    if (!P.have_video) {
        fprintf(stderr, "sfvideo: %s: no video stream that can be decoded\n", opt.file);
        return 1;
    }
    if (!opt.no_audio && !opt.untimed)
        P.have_audio = open_stream(AVMEDIA_TYPE_AUDIO, &P.aindex, &P.ast, &P.actx) == 0;

    /* the picture's display size: the sample aspect ratio stretches the width */
    AVRational sar = av_guess_sample_aspect_ratio(P.ic, P.vst, NULL);
    W.video_w = P.vctx->width;
    W.video_h = P.vctx->height;
    if (sar.num > 0 && sar.den > 0 && sar.num != sar.den)
        W.video_w = (int)av_rescale(W.video_w, sar.num, sar.den);
    if (W.video_w <= 0 || W.video_h <= 0) {
        fprintf(stderr, "sfvideo: %s: no picture size\n", opt.file);
        return 1;
    }

    if (P.have_audio) {
#ifdef __APPLE__
        if (!strcmp(opt.ao, "auto") && ao_audioqueue.preferred_rate() > 0)
            P.aod = &ao_audioqueue;
#endif
        for (int attempt = 0; attempt < 2 && !P.ao; attempt++) {
            if (!P.aod)
                P.aod = &ao_null;
            P.ao_rate = P.aod->preferred_rate();
            P.fifo_size = P.ao_rate * 4 * AUDIO_FIFO_MS / 1000;
            P.fifo = av_malloc((size_t)P.fifo_size);
            P.ao = P.fifo ? P.aod->open(P.ao_rate, opt.volume / 100.0, audio_fill, NULL) : NULL;
            if (!P.ao) {
                av_freep(&P.fifo);
                P.aod = NULL;       /* the stand-in */
            }
        }
        if (!P.ao)
            P.have_audio = 0;
    }

    fprintf(stderr, "sfvideo: %s: video %s %dx%d %s, hwdec %s%s; audio %s%s%s\n", opt.file, P.vctx->codec->name,
            P.vctx->width, P.vctx->height, av_get_pix_fmt_name(P.vctx->pix_fmt) ? av_get_pix_fmt_name(P.vctx->pix_fmt) : "?",
            P.hw_name, strcmp(P.hw_name, "none") ? "" : (P.vctx->thread_count ? "" : " (software, one thread per processor)"),
            P.have_audio ? P.actx->codec->name : "none", P.have_audio ? " through " : "", P.have_audio ? P.aod->name : "");
    if (P.have_audio)
        fprintf(stderr, "sfvideo: audio output %s at %d Hz, device latency %.0f ms\n", P.aod->name, P.ao_rate,
                P.aod->latency(P.ao) * 1000.0);

    if (vo_window()) {
        const char *base = strrchr(opt.file, '/');
        char title[256];

        snprintf(title, sizeof(title), "%s - sfvideo", base ? base + 1 : opt.file);
        if (window_open(title) < 0)
            goto done;
#ifdef SFVIDEO_GL
        if (vo_gl() && gl_init() < 0)
            goto done;
#endif
        window_show(NULL);      /* black until the first picture */
        W.need_redraw = 0;
    }

    getrusage(RUSAGE_SELF, &ru0);
    wall0 = now();
    P.play_start = wall0;
    if (pthread_create(&P.demux_thread, NULL, demux_main, NULL))
        goto done;
    P.have_demux = 1;
    if (pthread_create(&P.video_thread, NULL, video_main, NULL))
        goto done;
    if (P.have_audio && pthread_create(&P.audio_thread, NULL, audio_main, NULL))
        P.have_audio = 0;

    while (!P.quit) {
        double remaining = REFRESH_RATE;
        int count, done;

        audio_clock_poll();
        if (P.have_audio && !P.audio_done && P.ao_started && P.aod->drained(P.ao))
            P.audio_done = 1;       /* the clock now runs on by itself */
        video_refresh(&remaining);
        if (vo_window() && W.need_redraw && W.configured) {
            window_show(P.last_frame);
            W.need_redraw = 0;
        }
        pthread_mutex_lock(&P.fq_lock);
        count = P.fq_count;
        done = P.video_done;
        pthread_mutex_unlock(&P.fq_lock);
        if (done && count == 0) {
            /* the last picture stays for its duration, the sound plays out */
            if (!video_end)
                video_end = now() + (opt.untimed ? 0 : P.last_duration);
            if (now() >= video_end && (!P.have_audio || P.audio_done || !P.ao_started))
                break;
        }
        long handled = P.shown + P.drops_late + P.drops_early + P.drops_nobuffer;
        if (opt.frames > 0 && handled >= opt.frames)
            break;
        if (opt.duration > 0 && now() - P.play_start >= opt.duration)
            break;
        if (opt.stats && now() - last_status >= 1.0) {
            last_status = now();
            fprintf(stderr, "sfvideo: t=%.2f A-V=%+.0f ms decoded=%ld shown=%ld dropped=%ld\n", master_clock(),
                    P.have_last ? (master_clock() - P.last_pts) * 1000.0 : 0.0, P.decoded, P.shown,
                    P.drops_late + P.drops_early + P.drops_nobuffer);
        }
        if (vo_window()) {
            if (window_pump(remaining) < 0) {
                fprintf(stderr, "sfvideo: the Wayland connection ended\n");
                break;
            }
            if (W.closed)
                break;
        } else if (remaining > 0) {
            av_usleep((unsigned)(remaining * 1e6));
        }
    }
    ret = 0;

done:
    P.quit = 1;
    double wall1 = now();
    getrusage(RUSAGE_SELF, &ru1);
    pq_abort(&P.vq);
    pq_abort(&P.aq);
    pthread_mutex_lock(&P.fq_lock);
    pthread_cond_broadcast(&P.fq_cond);
    pthread_mutex_unlock(&P.fq_lock);
    pthread_mutex_lock(&P.fifo_lock);
    pthread_cond_broadcast(&P.fifo_cond);
    pthread_mutex_unlock(&P.fifo_lock);
    if (P.have_demux) {
        pthread_join(P.demux_thread, NULL);
        pthread_join(P.video_thread, NULL);
        if (P.have_audio)
            pthread_join(P.audio_thread, NULL);
    }
#ifdef SFVIDEO_GL
    if (opt.snapshot && vo_gl() && G.program)
        gl_snapshot(opt.snapshot, P.last_frame);
    else
#endif
    if (opt.snapshot && vo_window())
        window_snapshot(opt.snapshot);
    long underruns = P.ao ? P.aod->underruns(P.ao) : 0;
    if (P.ao)
        P.aod->close(P.ao);

    double user = tv(ru1.ru_utime) - tv(ru0.ru_utime), sys = tv(ru1.ru_stime) - tv(ru0.ru_stime), wall = wall1 - wall0;
    long dropped = P.drops_late + P.drops_early + P.drops_nobuffer;
    const char *base = strrchr(opt.file, '/');
    printf("SFVIDEO_STATS file=%s codec=%s size=%dx%d hwdec=%s hw_frames=%d shown_fmt=%s vo=%s audio=%s "
           "decoded=%ld shown=%ld dropped=%ld dropped_late=%ld dropped_early=%ld dropped_nobuffer=%ld "
           "decode_ms_mean=%.3f decode_ms_max=%.3f transfer_ms_mean=%.3f transfer_ms_max=%.3f "
           "present_ms_mean=%.3f present_ms_max=%.3f ack_ms_mean=%.3f ack_ms_max=%.3f "
           "av_drift_ms_mean=%.1f av_drift_ms_maxabs=%.1f audio_underruns=%ld "
           "cpu_user_s=%.3f cpu_sys_s=%.3f wall_s=%.3f cpu_percent=%.1f\n",
           base ? base + 1 : opt.file, P.vctx ? P.vctx->codec->name : "?", P.vctx ? P.vctx->width : 0, P.vctx ? P.vctx->height : 0,
           P.hw_frames_seen ? P.hw_name : "none", P.hw_frames_seen,
           P.shown_fmt == AV_PIX_FMT_NONE ? "none" : av_get_pix_fmt_name(P.shown_fmt), opt.vo,
           P.have_audio ? P.aod->name : "none", P.decoded, P.shown, dropped, P.drops_late, P.drops_early, P.drops_nobuffer,
           stat_mean(&P.decode), P.decode.max, stat_mean(&P.transfer), P.transfer.max,
           stat_mean(&P.present), P.present.max, stat_mean(&P.ack), P.ack.max,
           stat_mean(&P.drift), P.drift_max_abs, underruns, user, sys, wall, wall > 0 ? (user + sys) / wall * 100.0 : 0.0);
    fflush(stdout);
    /* the process ends here: decoder contexts, the window and the queues go with it */
    pq_flush(&P.vq);
    pq_flush(&P.aq);
    return ret;
}
