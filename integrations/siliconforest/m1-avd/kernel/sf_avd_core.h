/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Core of the Apple AVD driver outside V4L2: what Linux's avd-drv.c and avd-v4l2.c do
 * around the codec files, as plain calls. One decode at a time per device; the caller
 * serialises every call below except sf_avd_irq().
 */
#ifndef _SF_AVD_CORE_H
#define _SF_AVD_CORE_H

#include "sf_avd_os.h"
#include "../include/sfavd/sfavd_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sf_avd_device;
struct sf_avd_session;

/* compatible: "apple,t8103-avd". The firmware image stays owned by the caller. */
struct sf_avd_device *sf_avd_device_create(const char *compatible, void *code, void *mbox, void *ctrl,
    const uint8_t *fw, size_t fw_size);
void sf_avd_device_destroy(struct sf_avd_device *dev);
uint32_t sf_avd_device_caps(struct sf_avd_device *dev);        /* SFAVD_CAP_* */
/* Loads the firmware and starts the Cortex-M3; 0 or a negative Linux errno. */
int  sf_avd_device_boot(struct sf_avd_device *dev);
void sf_avd_device_shutdown(struct sf_avd_device *dev);
/* The mailbox interrupt (or a poll of it): true when a message was handled. */
bool sf_avd_irq(struct sf_avd_device *dev);

int  sf_avd_session_create(struct sf_avd_device *dev, struct sf_avd_session **out);
void sf_avd_session_destroy(struct sf_avd_session *s);
/*
 * Coded format and size, then the control that decides the image format (the SPS, or the
 * VP9 frame header): fills *fmt with the decoded layout. Not while buffers exist.
 */
int  sf_avd_session_set_format(struct sf_avd_session *s, uint32_t codec, uint32_t width, uint32_t height,
    uint32_t ctrl_id, const void *ctrl, size_t ctrl_size, struct sfavd_format *fmt);
/* Allocates `count` decoded buffers and the codec's work buffers; starts the codec. */
int  sf_avd_session_alloc(struct sf_avd_session *s, unsigned count);
/* Kernel address, size and OS cookie of decoded buffer `index`. */
int  sf_avd_session_buffer(struct sf_avd_session *s, unsigned index, void **vaddr, size_t *size, void **cookie);
/* Atomic capture-set replacement BEFORE any decode, including HOLD. Caller
 * supplies prepared/mapped OS allocations; on success ownership transfers to
 * the session and old cookies are returned for release. Failure changes none. */
int sf_avd_session_import(struct sf_avd_session *s, unsigned count,
    void *const *cpu, const uint64_t *iova, void *const *cookies, void **old_cookies);
/* Stores one control (copied). */
int  sf_avd_session_set_ctrl(struct sf_avd_session *s, uint32_t id, const void *data, size_t size);
/* Room for `size` bytes of coded data for the next decode; the caller fills it. */
void *sf_avd_session_coded(struct sf_avd_session *s, size_t size);
/*
 * Decodes the coded data into buffer `dst` under the stored controls. With
 * SFAVD_DECODE_HOLD the slice is only queued (H.264 slices before the last of a picture).
 * Otherwise waits for the hardware. 0, or a negative Linux errno (-ETIMEDOUT after the
 * watchdog reset the decoder). *status gets the last mailbox word.
 */
int  sf_avd_session_decode(struct sf_avd_session *s, size_t coded_size, unsigned dst, uint64_t timestamp,
    uint32_t flags, uint32_t *status);

/* supplied by the OS side, in addition to sf_avd_os.h */
/* Blocks until sf_avd_os_job_wake() or the timeout; true when woken. Wakes are counted. */
bool sf_avd_os_job_wait(struct sf_avd_device *dev, uint32_t timeout_ms);
void sf_avd_os_job_wake(struct sf_avd_device *dev);
/* Resets the decoder block (PMGR reset, DART re-attach); the core boots it again. */
int  sf_avd_os_reset(struct sf_avd_device *dev);

/* statistics, for sysctl */
struct sf_avd_stats {
	uint64_t jobs, done, errors, timeouts, stale, irqs, vp_done, unhandled;
	uint32_t last_status;
	uint64_t last_decode_us, max_decode_us, total_decode_us;
};
void sf_avd_device_stats(struct sf_avd_device *dev, struct sf_avd_stats *out);
uint64_t sf_avd_os_now_us(void);

#ifdef __cplusplus
}
#endif
#endif
