// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sf_dcp_drm72.c - the atomic-commit names of Linux 7.2 on the layer's DRM core (6.14).
 *
 * 7.2 renamed struct drm_atomic_state to struct drm_atomic_commit and its helpers with it.
 * The driver is compiled with `#define drm_atomic_commit drm_atomic_state`
 * (include/sf_dcp_compat.h), which also turns its calls to the function drm_atomic_commit()
 * into calls to a function named drm_atomic_state(). This file is compiled WITHOUT that
 * define and supplies that function and the renamed helpers.
 */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_of.h>
#include <linux/component.h>
#include <linux/of.h>

/* the driver's drm_atomic_commit(state) */
int drm_atomic_state(struct drm_atomic_state *state)
{
	return drm_atomic_commit(state);
}

struct drm_atomic_state *drm_atomic_commit_alloc(struct drm_device *dev)
{
	return drm_atomic_state_alloc(dev);
}

int drm_atomic_commit_init(struct drm_device *dev, struct drm_atomic_state *state)
{
	return drm_atomic_state_init(dev, state);
}

void drm_atomic_commit_clear(struct drm_atomic_state *state)
{
	drm_atomic_state_clear(state);
}

void drm_atomic_commit_default_clear(struct drm_atomic_state *state)
{
	drm_atomic_state_default_clear(state);
}

void drm_atomic_commit_default_release(struct drm_atomic_state *state)
{
	drm_atomic_state_default_release(state);
}

struct drm_atomic_state *drm_atomic_commit_get(struct drm_atomic_state *state)
{
	return drm_atomic_state_get(state);
}

void drm_atomic_commit_put(struct drm_atomic_state *state)
{
	drm_atomic_state_put(state);
}

/* drivers/gpu/drm/drm_atomic_helper.c at the pin (6.15+), with the 6.14 names */
int drm_atomic_helper_reset_crtc(struct drm_crtc *crtc, struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	crtc_state->connectors_changed = true;

	ret = drm_atomic_commit(state);
out:
	drm_atomic_state_put(state);

	return ret;
}

/* drivers/gpu/drm/drm_of.c at the pin */
static void sf_component_release_of(struct device *dev, void *data)
{
	of_node_put(data);
}

void drm_of_component_match_add(struct device *master, struct component_match **matchptr,
				int (*compare)(struct device *, void *), struct device_node *node)
{
	of_node_get(node);
	component_match_add_release(master, matchptr, sf_component_release_of, compare, node);
}

/*
 * drm_mode_size_dumb() (drivers/gpu/drm/drm_dumb_buffers.c, 6.16): pitch and size of a dumb
 * buffer. Reduced: the pin first asks the driver's colour-mode table for a fourcc and takes
 * that format's minimum pitch; this takes the pin's fallback, width * bytes per pixel, for
 * every bpp. Equal for the XRGB8888/ARGB8888 buffers a test client creates (bpp 32).
 */
#include <drm/drm_device.h>
#include <linux/mm.h>
#include <uapi/drm/drm_mode.h>

int drm_mode_size_dumb(struct drm_device *dev, struct drm_mode_create_dumb *args,
		       unsigned long hw_pitch_align, unsigned long hw_size_align)
{
	u64 pitch, size;

	if (!args->width || !args->height || !args->bpp)
		return -EINVAL;
	pitch = (u64)args->width * DIV_ROUND_UP(args->bpp, 8);
	if (hw_pitch_align)
		pitch = roundup(pitch, hw_pitch_align);
	if (!pitch || pitch > U32_MAX)
		return -EINVAL;
	size = pitch * args->height;
	if (!hw_size_align)
		hw_size_align = PAGE_SIZE;
	else if (!IS_ALIGNED(hw_size_align, PAGE_SIZE))
		return -EINVAL;
	size = ALIGN(size, hw_size_align);
	if (!size || size > U32_MAX)
		return -EINVAL;
	args->pitch = pitch;
	args->size = size;
	return 0;
}

/* the commit gate, see include/sf_dcp_compat.h */
void kprintf(const char *, ...);
extern int sf_dcp_mode;

int sf_dcp_atomic_helper_commit(struct drm_device *dev, struct drm_atomic_state *state, bool nonblock)
{
	static int said;

	if (sf_dcp_mode < 2) {
		if (!said) {
			said = 1;
			kprintf("SFDcp: atomic commit refused: sfdcp=%d (2 allows modesets and flips)\n", sf_dcp_mode);
		}
		return -EPERM;
	}
	return drm_atomic_helper_commit(dev, state, nonblock);
}
