/* SPDX-License-Identifier: MIT */
/*
 * The Touch Bar's key strip: layout, picture and touch-to-key logic. Plain C with no
 * kernel dependency, so the same object runs in the kext and in the host tests
 * (../host-test). Modelled on tiny-dfr (AsahiLinux/tiny-dfr, MIT): a row of buttons
 * 16 px apart, grey when idle and lighter while touched, two layers. The strip has no
 * Escape and no layer key: this machine has both as real keys, and the keyboard's Fn
 * key chooses the layer (the driver calls tb_set_layer).
 *
 * Coordinates: the strip is TB_W x TB_H pixels as a person sees it (x from the left end
 * to the right end, y from the top edge down). The panel shows TB_H pixels of each of
 * TB_W frame buffer lines; a line is TB_FB_LINE pixels long, because the display pipe
 * takes its line length in multiples of 64 bytes (tiny-dfr allocates a 64-pixel-wide
 * buffer for the 60-pixel mode). tiny-dfr's transform (translate(height, 0), rotate 90
 * degrees) puts strip pixel (x, y) at frame buffer line x, column TB_H - 1 - y; columns
 * TB_H and up are never shown and stay zero.
 * The digitizer reports x in 0..TB_TOUCH_X along the strip and y in 0..TB_TOUCH_Y.
 */
#ifndef _SF_TB_KEYS_H
#define _SF_TB_KEYS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TB_W        2008
#define TB_H        60
#define TB_FB_LINE  64                  /* pixels per frame buffer line */
#define TB_FB_BYTES (TB_W * TB_FB_LINE * 4)     /* XRGB8888 */
#define TB_TOUCH_X  23045               /* touchscreen-size-x of the J293 device tree */
#define TB_TOUCH_Y  640

#define TB_LAYER_FN    0                /* F1..F12 */
#define TB_LAYER_MEDIA 1                /* brightness down and up, mute, quieter, louder */
#define TB_LAYERS      2
#define TB_MAX_KEYS    12
#define TB_FINGERS     8

struct tb_key {
	uint16_t x, w;          /* strip pixels */
	uint8_t  usage;         /* HID keyboard-page usage the key sends */
	uint8_t  glyph;         /* index of the label or icon */
};

struct tb_event {
	uint8_t usage;
	uint8_t down;
};

struct tb_state {
	uint8_t layer;
	uint8_t holds[TB_MAX_KEYS];     /* fingers on each key of the current layer */
	struct {
		uint8_t used, id;
		int8_t  key;            /* -1: not on a key (or its key was let go) */
	} fingers[TB_FINGERS];
};

void tb_init(struct tb_state *s);
unsigned tb_key_count(unsigned layer);
const struct tb_key *tb_key(unsigned layer, unsigned index);
/* Key under strip pixel x of the layer, -1 in a gap. */
int tb_key_at(unsigned layer, int x);
/* Digitizer x of the middle of a key. */
unsigned tb_key_touch_x(const struct tb_key *k);

/*
 * One finger of a digitizer report. `touching` is nonzero while the finger is down
 * (started or moved). Appends the key events this causes to out[] (at most max) and
 * returns their number; *redraw is set when the picture changed.
 *   - a finger that comes down on a key presses it, leaving the key or lifting releases it;
 *   - a finger that slides onto another key presses nothing.
 */
int tb_touch(struct tb_state *s, unsigned finger, int touching, unsigned raw_x, unsigned raw_y,
    struct tb_event *out, int max, int *redraw);
/* Lets go of every key; the fingers stay known, so one still resting on the strip presses nothing. */
int tb_release_all(struct tb_state *s, struct tb_event *out, int max, int *redraw);
/* The same and forgets the fingers too: for fingers whose lift was never reported. */
int tb_drop_all(struct tb_state *s, struct tb_event *out, int max, int *redraw);
/* Switches layers, releasing held keys first. */
int tb_set_layer(struct tb_state *s, unsigned layer, struct tb_event *out, int max, int *redraw);

/* Draws the strip into a frame buffer of TB_W lines of TB_FB_LINE XRGB8888 pixels. */
void tb_render(const struct tb_state *s, uint32_t *fb);
/* CRC-32 (IEEE, as zlib) */
uint32_t tb_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif
