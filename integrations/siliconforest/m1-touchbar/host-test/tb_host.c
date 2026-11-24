/* SPDX-License-Identifier: MIT */
/*
 * Host checks of ../kernel/sf_tb_keys.c: the touch-to-key logic, and the picture of
 * both layers written as PGM files (as a person sees the strip) with the CRC-32 of the
 * frame buffer, which the kext logs for the buffer the panel scans out.
 * Usage: tb_host <output directory>
 */
#include "../kernel/sf_tb_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static unsigned
mid(unsigned layer, uint8_t usage)
{
	for (unsigned i = 0; i < tb_key_count(layer); i++)
		if (tb_key(layer, i)->usage == usage)
			return tb_key_touch_x(tb_key(layer, i));
	return ~0u;
}

static void
picture(const struct tb_state *s, const char *dir, const char *name)
{
	static uint32_t fb[TB_W * TB_FB_LINE];
	char path[512];
	tb_render(s, fb);
	printf("TBHOST picture %s crc32=%08x\n", name, tb_crc32(fb, sizeof(fb)));
	snprintf(path, sizeof(path), "%s/%s.pgm", dir, name);
	FILE *f = fopen(path, "wb");
	if (f == NULL) { perror(path); exit(2); }
	fprintf(f, "P5\n%d %d\n255\n", TB_W, TB_H);
	for (int y = 0; y < TB_H; y++)
		for (int x = 0; x < TB_W; x++)
			fputc(fb[x * TB_FB_LINE + (TB_H - 1 - y)] & 0xff, f);
	fclose(f);
	/* the columns the panel does not show are zero */
	for (int x = 0; x < TB_W; x++)
		for (int c = TB_H; c < TB_FB_LINE; c++)
			CHECK(fb[x * TB_FB_LINE + c] == 0);
}

int
main(int argc, char **argv)
{
	struct tb_state s;
	struct tb_event ev[16];
	int redraw = 0, n;
	const char *dir = argc > 1 ? argv[1] : ".";

	tb_init(&s);
	CHECK(tb_key_count(TB_LAYER_FN) == 12 && tb_key_count(TB_LAYER_MEDIA) == 5);
	for (unsigned l = 0; l < TB_LAYERS; l++) {
		unsigned count = tb_key_count(l);
		printf("TBHOST layer %u:", l);
		for (unsigned i = 0; i < count; i++) {
			const struct tb_key *k = tb_key(l, i);
			printf(" %02x@%u+%u(touch %u)", k->usage, k->x, k->w, tb_key_touch_x(k));
			CHECK(k->usage != 0x29 && k->usage != 0);       /* no Escape, no layer key */
			CHECK(k->x + k->w <= TB_W);
			CHECK(i == 0 || k->x == tb_key(l, i - 1)->x + tb_key(l, i - 1)->w + 16);
			CHECK(tb_key_at(l, k->x) == (int)i && tb_key_at(l, k->x + k->w - 1) == (int)i);
			CHECK(tb_key_at(l, k->x + k->w) == -1);
		}
		/* the keys use the whole strip: less than one spacing left over at the ends */
		CHECK(tb_key(l, 0)->x < 16 && TB_W - (tb_key(l, count - 1)->x + tb_key(l, count - 1)->w) < 16);
		printf("\n");
	}
	/* a tap on F11: down, then up */
	n = tb_touch(&s, 1, 1, mid(0, 0x44), 300, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x44 && ev[0].down == 1 && redraw == 1);
	n = tb_touch(&s, 1, 1, mid(0, 0x44) + 30, 310, ev, 16, &redraw);  /* moves within the key */
	CHECK(n == 0);
	n = tb_touch(&s, 1, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x44 && ev[0].down == 0);
	/* sliding off a key releases it; sliding onto another presses nothing */
	n = tb_touch(&s, 2, 1, mid(0, 0x3a), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x3a && ev[0].down == 1);
	n = tb_touch(&s, 2, 1, mid(0, 0x3b), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x3a && ev[0].down == 0);
	n = tb_touch(&s, 2, 1, mid(0, 0x3c), 0, ev, 16, &redraw);
	CHECK(n == 0);
	n = tb_touch(&s, 2, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 0);
	/* two fingers on one key: one down, one up */
	n = tb_touch(&s, 3, 1, mid(0, 0x45), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].down == 1);
	n = tb_touch(&s, 4, 1, mid(0, 0x45) + 20, 0, ev, 16, &redraw);
	CHECK(n == 0);
	n = tb_touch(&s, 3, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 0);
	n = tb_touch(&s, 4, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x45 && ev[0].down == 0);
	/* a gap presses nothing */
	n = tb_touch(&s, 5, 1, (tb_key(0, 0)->x + tb_key(0, 0)->w + 8) * TB_TOUCH_X / TB_W, 0, ev, 16, &redraw);
	CHECK(n == 0);
	n = tb_touch(&s, 5, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 0);
	picture(&s, dir, "layer-fn");
	/* a held key shows lighter */
	tb_touch(&s, 6, 1, mid(0, 0x43), 0, ev, 16, &redraw);
	picture(&s, dir, "layer-fn-f10-held");
	/* a layer change (the keyboard's Fn key) releases what is held; the resting finger presses nothing */
	n = tb_set_layer(&s, TB_LAYER_MEDIA, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x43 && ev[0].down == 0 && s.layer == TB_LAYER_MEDIA);
	n = tb_touch(&s, 6, 1, mid(0, 0x43), 0, ev, 16, &redraw);
	CHECK(n == 0);
	n = tb_touch(&s, 6, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 0);
	picture(&s, dir, "layer-media");
	/* the media layer's louder key sends F12 */
	n = tb_touch(&s, 1, 1, mid(1, 0x45), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x45 && ev[0].down == 1);
	n = tb_release_all(&s, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x45 && ev[0].down == 0);
	n = tb_touch(&s, 1, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 0);
	/* a finger whose lift is never reported: dropped, and the same finger number presses again */
	n = tb_touch(&s, 9, 1, mid(1, 0x43), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x43 && ev[0].down == 1);
	picture(&s, dir, "layer-media-mute-held");
	n = tb_drop_all(&s, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x43 && ev[0].down == 0);
	n = tb_touch(&s, 9, 1, mid(1, 0x44), 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x44 && ev[0].down == 1);
	n = tb_touch(&s, 9, 0, 0, 0, ev, 16, &redraw);
	CHECK(n == 1 && ev[0].usage == 0x44 && ev[0].down == 0);
	n = tb_set_layer(&s, TB_LAYER_FN, ev, 16, &redraw);
	CHECK(n == 0 && s.layer == TB_LAYER_FN);
	/* more fingers than slots: ignored, nothing stuck */
	for (unsigned f = 10; f < 10 + TB_FINGERS + 3; f++)
		tb_touch(&s, f, 1, mid(0, 0x3c), 0, ev, 16, &redraw);
	for (unsigned f = 10; f < 10 + TB_FINGERS + 3; f++)
		n = tb_touch(&s, f, 0, 0, 0, ev, 16, &redraw);
	for (unsigned k = 0; k < TB_MAX_KEYS; k++)
		CHECK(s.holds[k] == 0);
	printf("TBHOST %s (%d failures)\n", failures ? "FAILED" : "PASS", failures);
	return failures != 0;
}
