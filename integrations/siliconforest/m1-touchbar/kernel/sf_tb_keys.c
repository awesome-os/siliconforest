/* SPDX-License-Identifier: MIT */
/*
 * The Touch Bar's key strip. See sf_tb_keys.h. Layout numbers and colours follow
 * tiny-dfr (AsahiLinux/tiny-dfr, MIT, Copyright The Asahi Linux Contributors):
 * BUTTON_SPACING_PX 16, button grey 0.2 idle and 0.4 active, icons 48 px.
 */
#include "sf_tb_keys.h"

#define SPACING     16
#define KEY_TOP     4
#define KEY_BOTTOM  56          /* exclusive */
#define RADIUS      8
#define COLOR_BACK  0x00000000u
#define COLOR_IDLE  0x00333333u
#define COLOR_HELD  0x00666666u
#define COLOR_INK   0x00ffffffu

enum {
	G_F1, G_F2, G_F3, G_F4, G_F5, G_F6, G_F7, G_F8, G_F9, G_F10, G_F11, G_F12,
	G_DIM, G_BRIGHT, G_MUTE, G_QUIETER, G_LOUDER, G_COUNT
};

static const char *const kLabels[G_F12 + 1] = {
	"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
};

/* 5x7 digits and F, one byte per row, bit 4 the left column */
static const struct { char c; uint8_t rows[7]; } kFont[] = {
	{ '0', { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } },
	{ '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } },
	{ '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
	{ '4', { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } },
	{ '5', { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e } },
	{ '6', { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e } },
	{ '7', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e } },
	{ '9', { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c } },
	{ 'F', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 } },
};
#define FONT_SCALE 4
#define FONT_GAP   4

/* 16x16 icons, drawn three times as large */
#define ICON_SCALE 3
static const char *const kIcons[G_COUNT - G_DIM][16] = {
	{	/* G_DIM: a small sun */
		"................",
		"................",
		".......#........",
		"...#.......#....",
		"................",
		"......###.......",
		".....#####......",
		"..#..#####..#...",
		".....#####......",
		"......###.......",
		"................",
		"...#.......#....",
		".......#........",
		"................",
		"................",
		"................",
	}, {	/* G_BRIGHT: a large sun */
		".......#........",
		".......#........",
		"..#....#....#...",
		"...#.......#....",
		"......###.......",
		".....#####......",
		"....#######.....",
		"###.#######.###.",
		"....#######.....",
		".....#####......",
		"......###.......",
		"...#.......#....",
		"..#....#....#...",
		".......#........",
		".......#........",
		"................",
	}, {	/* G_MUTE: a speaker, crossed */
		"................",
		"......#.........",
		".....##.........",
		"....###.........",
		"#######..#...#..",
		"#######..##.##..",
		"#######...###...",
		"#######....#....",
		"#######...###...",
		"#######..##.##..",
		"#######..#...#..",
		"....###.........",
		".....##.........",
		"......#.........",
		"................",
		"................",
	}, {	/* G_QUIETER: a speaker, one wave */
		"................",
		"......#.........",
		".....##.........",
		"....###.........",
		"#######.........",
		"#######..#......",
		"#######...#.....",
		"#######...#.....",
		"#######...#.....",
		"#######..#......",
		"#######.........",
		"....###.........",
		".....##.........",
		"......#.........",
		"................",
		"................",
	}, {	/* G_LOUDER: a speaker, three waves */
		"................",
		"......#......#..",
		".....##.......#.",
		"....###....#..#.",
		"#######.....#..#",
		"#######..#..#..#",
		"#######...#.#..#",
		"#######...#.#..#",
		"#######...#.#..#",
		"#######..#..#..#",
		"#######.....#..#",
		"....###....#..#.",
		".....##.......#.",
		"......#......#..",
		"................",
		"................",
	},
};

/* usage, glyph of each layer's keys from left to right */
static const uint8_t kLayerKeys[TB_LAYERS][TB_MAX_KEYS][2] = {
	{
		{ 0x3a, G_F1 }, { 0x3b, G_F2 }, { 0x3c, G_F3 }, { 0x3d, G_F4 }, { 0x3e, G_F5 }, { 0x3f, G_F6 },
		{ 0x40, G_F7 }, { 0x41, G_F8 }, { 0x42, G_F9 }, { 0x43, G_F10 }, { 0x44, G_F11 }, { 0x45, G_F12 },
	}, {
		/*
		 * The system takes brightness from F1/F2 and mute and volume from F10/F11/F12
		 * (the keys printed with those symbols on a MacBook's top row); its key queue
		 * carries keyboard-page usages only. The media layer sends those same usages
		 * under their symbols.
		 */
		{ 0x3a, G_DIM }, { 0x3b, G_BRIGHT }, { 0x43, G_MUTE }, { 0x44, G_QUIETER }, { 0x45, G_LOUDER },
	},
};
static const uint8_t kLayerCount[TB_LAYERS] = { 12, 5 };

static struct tb_key gKeys[TB_LAYERS][TB_MAX_KEYS];
static int gKeysReady;

static void
layout(void)
{
	if (gKeysReady)
		return;
	for (unsigned l = 0; l < TB_LAYERS; l++) {
		unsigned n = kLayerCount[l];
		unsigned w = (TB_W - SPACING * (n - 1)) / n;
		unsigned left = (TB_W - (n * w + SPACING * (n - 1))) / 2;
		for (unsigned i = 0; i < n; i++) {
			gKeys[l][i].x = (uint16_t)(left + i * (w + SPACING));
			gKeys[l][i].w = (uint16_t)w;
			gKeys[l][i].usage = kLayerKeys[l][i][0];
			gKeys[l][i].glyph = kLayerKeys[l][i][1];
		}
	}
	gKeysReady = 1;
}

void
tb_init(struct tb_state *s)
{
	layout();
	for (size_t i = 0; i < sizeof(*s); i++)
		((uint8_t *)s)[i] = 0;
	for (unsigned i = 0; i < TB_FINGERS; i++)
		s->fingers[i].key = -1;
}

unsigned
tb_key_count(unsigned layer)
{
	return layer < TB_LAYERS ? kLayerCount[layer] : 0;
}

const struct tb_key *
tb_key(unsigned layer, unsigned index)
{
	layout();
	return layer < TB_LAYERS && index < kLayerCount[layer] ? &gKeys[layer][index] : NULL;
}

int
tb_key_at(unsigned layer, int x)
{
	layout();
	if (layer >= TB_LAYERS)
		return -1;
	for (unsigned i = 0; i < kLayerCount[layer]; i++) {
		if (x >= gKeys[layer][i].x && x < gKeys[layer][i].x + gKeys[layer][i].w)
			return (int)i;
	}
	return -1;
}

unsigned
tb_key_touch_x(const struct tb_key *k)
{
	return (unsigned)(((uint64_t)(k->x + k->w / 2) * TB_TOUCH_X + TB_W / 2) / TB_W);
}

static int
emit(struct tb_event *out, int n, int max, uint8_t usage, int down)
{
	if (n < max) {
		out[n].usage = usage;
		out[n].down = (uint8_t)down;
		return n + 1;
	}
	return n;
}

/* One finger fewer on key k. */
static int
let_go(struct tb_state *s, int k, struct tb_event *out, int n, int max, int *redraw)
{
	if (k < 0 || k >= TB_MAX_KEYS || s->holds[k] == 0)
		return n;
	if (--s->holds[k] == 0) {
		n = emit(out, n, max, gKeys[s->layer][k].usage, 0);
		*redraw = 1;
	}
	return n;
}

int
tb_release_all(struct tb_state *s, struct tb_event *out, int max, int *redraw)
{
	int n = 0;

	layout();
	for (unsigned k = 0; k < TB_MAX_KEYS; k++) {
		if (s->holds[k] != 0) {
			s->holds[k] = 0;
			n = emit(out, n, max, gKeys[s->layer][k].usage, 0);
			*redraw = 1;
		}
	}
	/* the fingers stay known, so that one still resting on the strip presses nothing */
	for (unsigned i = 0; i < TB_FINGERS; i++)
		s->fingers[i].key = -1;
	return n;
}

int
tb_drop_all(struct tb_state *s, struct tb_event *out, int max, int *redraw)
{
	int n = tb_release_all(s, out, max, redraw);

	for (unsigned i = 0; i < TB_FINGERS; i++)
		s->fingers[i].used = 0;
	return n;
}

int
tb_set_layer(struct tb_state *s, unsigned layer, struct tb_event *out, int max, int *redraw)
{
	int n;

	if (layer >= TB_LAYERS || layer == s->layer)
		return 0;
	n = tb_release_all(s, out, max, redraw);
	s->layer = (uint8_t)layer;
	*redraw = 1;
	return n;
}

int
tb_touch(struct tb_state *s, unsigned finger, int touching, unsigned raw_x, unsigned raw_y,
    struct tb_event *out, int max, int *redraw)
{
	int slot = -1, spare = -1, n = 0;
	int x, k;

	(void)raw_y;            /* the strip is one row of keys: any height counts */
	layout();
	for (int i = 0; i < TB_FINGERS; i++) {
		if (s->fingers[i].used && s->fingers[i].id == (uint8_t)finger)
			slot = i;
		else if (!s->fingers[i].used && spare < 0)
			spare = i;
	}
	if (!touching) {
		if (slot >= 0) {
			n = let_go(s, s->fingers[slot].key, out, n, max, redraw);
			s->fingers[slot].used = 0;
			s->fingers[slot].key = -1;
		}
		return n;
	}
	if (raw_x > TB_TOUCH_X)
		raw_x = TB_TOUCH_X;
	x = (int)(((uint64_t)raw_x * TB_W) / (TB_TOUCH_X + 1));
	k = tb_key_at(s->layer, x);
	if (slot >= 0) {
		/* a finger already down: it can only leave its key */
		if (s->fingers[slot].key >= 0 && s->fingers[slot].key != k) {
			n = let_go(s, s->fingers[slot].key, out, n, max, redraw);
			s->fingers[slot].key = -1;
		}
		return n;
	}
	if (spare < 0)
		return 0;       /* more fingers than the strip has use for */
	s->fingers[spare].used = 1;
	s->fingers[spare].id = (uint8_t)finger;
	s->fingers[spare].key = -1;
	if (k < 0)
		return 0;
	s->fingers[spare].key = (int8_t)k;
	if (s->holds[k]++ == 0) {
		n = emit(out, n, max, gKeys[s->layer][k].usage, 1);
		*redraw = 1;
	}
	return n;
}

/* ---- picture -------------------------------------------------------------------------- */

static inline void
put(uint32_t *fb, int x, int y, uint32_t color)
{
	if (x >= 0 && x < TB_W && y >= 0 && y < TB_H)
		fb[(size_t)x * TB_FB_LINE + (size_t)(TB_H - 1 - y)] = color;
}

static void
block(uint32_t *fb, int x, int y, int w, int h, uint32_t color)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			put(fb, x + i, y + j, color);
}

/* A filled rectangle with rounded corners. */
static void
button(uint32_t *fb, int x, int y, int w, int h, uint32_t color)
{
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			int cx = i < RADIUS ? RADIUS - 1 - i : i >= w - RADIUS ? i - (w - RADIUS) : -1;
			int cy = j < RADIUS ? RADIUS - 1 - j : j >= h - RADIUS ? j - (h - RADIUS) : -1;
			/* corner squares keep the quarter disc: (cx + 0.5)^2 + (cy + 0.5)^2 <= r^2 */
			if (cx >= 0 && cy >= 0 &&
			    (2 * cx + 1) * (2 * cx + 1) + (2 * cy + 1) * (2 * cy + 1) > 4 * RADIUS * RADIUS)
				continue;
			put(fb, x + i, y + j, color);
		}
	}
}

static const uint8_t *
glyph_rows(char c)
{
	for (size_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); i++) {
		if (kFont[i].c == c)
			return kFont[i].rows;
	}
	return NULL;
}

static void
label(uint32_t *fb, const struct tb_key *key, const char *text)
{
	int len = 0;
	while (text[len])
		len++;
	int w = len * 5 * FONT_SCALE + (len - 1) * FONT_GAP;
	int x = key->x + (key->w - w) / 2;
	int y = (TB_H - 7 * FONT_SCALE) / 2;

	for (int c = 0; c < len; c++) {
		const uint8_t *rows = glyph_rows(text[c]);
		for (int r = 0; rows != NULL && r < 7; r++) {
			for (int b = 0; b < 5; b++) {
				if (rows[r] & (0x10 >> b))
					block(fb, x + b * FONT_SCALE, y + r * FONT_SCALE, FONT_SCALE, FONT_SCALE, COLOR_INK);
			}
		}
		x += 5 * FONT_SCALE + FONT_GAP;
	}
}

static void
icon(uint32_t *fb, const struct tb_key *key, unsigned which)
{
	int x = key->x + (key->w - 16 * ICON_SCALE) / 2;
	int y = (TB_H - 16 * ICON_SCALE) / 2;

	for (int r = 0; r < 16; r++) {
		for (int c = 0; c < 16; c++) {
			if (kIcons[which][r][c] == '#')
				block(fb, x + c * ICON_SCALE, y + r * ICON_SCALE, ICON_SCALE, ICON_SCALE, COLOR_INK);
		}
	}
}

void
tb_render(const struct tb_state *s, uint32_t *fb)
{
	layout();
	for (size_t i = 0; i < (size_t)TB_W * TB_FB_LINE; i++)
		fb[i] = COLOR_BACK;
	for (unsigned i = 0; i < kLayerCount[s->layer]; i++) {
		const struct tb_key *key = &gKeys[s->layer][i];
		button(fb, key->x, KEY_TOP, key->w, KEY_BOTTOM - KEY_TOP, s->holds[i] ? COLOR_HELD : COLOR_IDLE);
		if (key->glyph <= G_F12)
			label(fb, key, kLabels[key->glyph]);
		else
			icon(fb, key, key->glyph - G_DIM);
	}
}

uint32_t
tb_crc32(const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t crc = 0xffffffffu;

	while (len--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
	}
	return ~crc;
}
