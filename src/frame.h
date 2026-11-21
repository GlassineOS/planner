// SPDX-License-Identifier: GPL-2.0-or-later
/* The frame: the page as last drawn, repainted whole or only where something changed, and
 * which tiles of it changed (the damage, FR-061). No copy of the whole frame per update. */
#ifndef PL_FRAME_H
#define PL_FRAME_H

#include <cairo.h>
#include <stdint.h>

#define FRAME_TILE 16

struct frect {
	int x, y, w, h;
};

struct frame {
	int w, h;        /* device pixels */
	uint32_t *cur;   /* the frame (XRGB) */
	uint32_t *alt;   /* scratch: the next whole frame, or a repainted rect's old pixels */
};

/* paints the page with cr; cr is clipped to the part being repainted (device pixels) */
typedef void (*frame_paint)(void *ud, cairo_t *cr);

/* repaints r (device pixels, widened to whole tiles; NULL, or a new size: all of it) with
 * paint. The changed tiles go to runs (a row's run of changed tiles per entry, at most
 * max; past that one run, their bounding box), their bounding box to *box (w 0: nothing
 * changed). Returns the number of runs, -1 when out of memory. */
int frame_update(struct frame *f, int w, int h, const struct frect *r, frame_paint paint, void *ud, struct frect *runs,
		 int max, struct frect *box);
void frame_free(struct frame *f);

/* a ∪ b (an empty rect has w or h 0) */
struct frect frect_union(struct frect a, struct frect b);

#endif
