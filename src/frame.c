// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "trace.h"

struct frect frect_union(struct frect a, struct frect b)
{
	if (a.w <= 0 || a.h <= 0)
		return b;
	if (b.w <= 0 || b.h <= 0)
		return a;
	int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
	int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (struct frect){ x0, y0, x1 - x0, y1 - y0 };
}

static void draw(uint32_t *px, int w, int h, const struct frect *clip, frame_paint paint, void *ud)
{
	cairo_surface_t *cs = cairo_image_surface_create_for_data((unsigned char *)px, CAIRO_FORMAT_RGB24, w, h, w * 4);
	double t0 = pl_trace ? pl_tr_now() : 0;
	cairo_t *cr = cairo_create(cs);
	if (clip) {
		cairo_rectangle(cr, clip->x, clip->y, clip->w, clip->h);
		cairo_clip(cr);
	}
	paint(ud, cr);
	cairo_destroy(cr);
	cairo_surface_flush(cs);
	cairo_surface_destroy(cs);
	if (pl_trace)
		pl_tr_paint_ms = pl_tr_now() - t0;
}

/* n pixels differ: two at a time (musl's memcmp goes byte by byte, ~4x slower here) */
static bool px_diff(const uint32_t *a, const uint32_t *b, int n)
{
	int i = 0;
	uint64_t d = 0;
	for (; i + 2 <= n; i += 2) {
		uint64_t x, y;
		memcpy(&x, a + i, 8);
		memcpy(&y, b + i, 8);
		d |= x ^ y;
	}
	if (i < n)
		d |= a[i] ^ b[i];
	return d != 0;
}

static bool tile_diff(const struct frame *f, int tx, int ty, int tw, int th)
{
	for (int y = ty; y < ty + th; y++)
		if (px_diff(f->cur + (size_t)y * f->w + tx, f->alt + (size_t)y * f->w + tx, tw))
			return true;
	return false;
}

/* the changed tiles of r, cur against alt */
static int diff(const struct frame *f, struct frect r, struct frect *runs, int max, struct frect *box)
{
	int n = 0;
	*box = (struct frect){ 0 };
	for (int ty = r.y; ty < r.y + r.h; ty += FRAME_TILE) {
		int th = ty + FRAME_TILE > r.y + r.h ? r.y + r.h - ty : FRAME_TILE;
		for (int tx = r.x; tx < r.x + r.w; tx += FRAME_TILE) {
			int tw = tx + FRAME_TILE > r.x + r.w ? r.x + r.w - tx : FRAME_TILE;
			if (!tile_diff(f, tx, ty, tw, th))
				continue;
			int x0 = tx;
			while (tx + FRAME_TILE < r.x + r.w) { /* the run of changed tiles on this row */
				int t2 = tx + 2 * FRAME_TILE > r.x + r.w ? r.x + r.w - tx - FRAME_TILE : FRAME_TILE;
				if (!tile_diff(f, tx + FRAME_TILE, ty, t2, th))
					break;
				tx += FRAME_TILE;
			}
			int x1 = tx + FRAME_TILE > r.x + r.w ? r.x + r.w : tx + FRAME_TILE;
			struct frect run = { x0, ty, x1 - x0, th };
			*box = frect_union(*box, run);
			if (n < max)
				runs[n] = run;
			n++;
		}
	}
	if (n > max && max > 0) {
		runs[0] = *box;
		n = 1;
	}
	return n;
}

int frame_update(struct frame *f, int w, int h, const struct frect *r, frame_paint paint, void *ud, struct frect *runs,
		 int max, struct frect *box)
{
	*box = (struct frect){ 0 };
	if (w <= 0 || h <= 0)
		return 0;
	if (f->w != w || f->h != h || !f->cur) {
		frame_free(f);
		f->cur = calloc((size_t)w * h, 4);
		f->alt = calloc((size_t)w * h, 4);
		if (!f->cur || !f->alt) {
			frame_free(f);
			return -1;
		}
		f->w = w, f->h = h;
		draw(f->cur, w, h, NULL, paint, ud);
		*box = (struct frect){ 0, 0, w, h };
		if (max > 0)
			runs[0] = *box;
		return 1;
	}
	struct frect a = { 0, 0, w, h };
	if (r) { /* whole tiles inside the frame */
		int x0 = r->x < 0 ? 0 : r->x / FRAME_TILE * FRAME_TILE, y0 = r->y < 0 ? 0 : r->y / FRAME_TILE * FRAME_TILE;
		int x1 = r->x + r->w, y1 = r->y + r->h;
		x1 = x1 > w ? w : (x1 + FRAME_TILE - 1) / FRAME_TILE * FRAME_TILE;
		y1 = y1 > h ? h : (y1 + FRAME_TILE - 1) / FRAME_TILE * FRAME_TILE;
		x1 = x1 > w ? w : x1, y1 = y1 > h ? h : y1;
		if (x1 <= x0 || y1 <= y0)
			return 0;
		a = (struct frect){ x0, y0, x1 - x0, y1 - y0 };
	}
	if (!r) { /* the whole frame into alt, then the two swap: alt holds the old one */
		draw(f->alt, w, h, NULL, paint, ud);
		uint32_t *t = f->cur;
		f->cur = f->alt;
		f->alt = t;
	} else { /* the rect's old pixels into alt, the rect repainted in place */
		for (int y = a.y; y < a.y + a.h; y++)
			memcpy(f->alt + (size_t)y * w + a.x, f->cur + (size_t)y * w + a.x, (size_t)a.w * 4);
		draw(f->cur, w, h, &a, paint, ud);
	}
	double t0 = pl_trace ? pl_tr_now() : 0;
	int n = diff(f, a, runs, max, box);
	if (pl_trace)
		pl_tr_diff_ms = pl_tr_now() - t0;
	return n;
}

void frame_free(struct frame *f)
{
	free(f->cur);
	free(f->alt);
	f->cur = f->alt = NULL;
	f->w = f->h = 0;
}
