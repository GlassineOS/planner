/* SPDX-License-Identifier: GPL-2.0-or-later
 * A title's ink rewrapped by its words (spec 001 FR-018; board event.wrap, GlassineOS/ui
 * designs/planner Planner.dc.html fitTitle): its strokes are split into words at the gaps
 * that can be seen, with nothing read, and the words are placed again onto the lines that fit
 * a width. The stored strokes never change: a placement is only where each stroke is drawn.
 * No cairo, no Wayland. */
#ifndef PL_REWRAP_H
#define PL_REWRAP_H
#include <stdbool.h>
#include "store.h"

/* a gap between strokes of a line wider than this (field px, at the written size) starts a
 * new word; the space put between two words placed on one line; the gap between two placed
 * lines; the least share of its written size a title shrinks to */
#define RW_WORD_GAP 7.0
#define RW_TALL 7.0 /* a stroke at least this tall makes a line; a smaller one (a dot, a cross) joins one */
#define RW_SPACE 9.0
#define RW_LINE_GAP 2.0
#define RW_LEAST 0.7
#define RW_WORDS 64

/* a placement: stroke i of the field is drawn moved by dx[i], dy[i] (field px, before the
 * title's scale); n: the strokes it covers (the field's ns when it was made) */
struct rw_place {
	int n;
	float *dx, *dy;
	int lines;            /* the lines it takes */
	double w, h;          /* its extent at scale 1, from 0, 0 (field px) */
};
void rw_free(struct rw_place *p);

/* the words of a field's ink, in reading order (line by line, left to right): each its box
 * (x0, y0, x1, y1, field px, as drawn) and its line; returns how many (at most max) */
struct rw_word {
	double x0, y0, x1, y1;
	int line;
};
int rw_words(const struct field *f, struct rw_word *w, int max, int *word_of_stroke);

/* place the words of f onto lines of at most width px (at scale 1; <= 0: one line); false when
 * the field has no ink, or has words typed in text mode with no ink of their own (they stay as
 * laid out: the title is shown as written) */
bool rw_place(const struct field *f, double width, struct rw_place *out);

/* fit a title into a lane: w x h px at the written size. The largest scale k from 1 down to
 * RW_LEAST at which the rewrapped words fit the lines h has; none: k RW_LEAST on one line, cut
 * (*cut true). out is the placement to draw at k. */
bool rw_fit(const struct field *f, double w, double h, struct rw_place *out, double *k, bool *cut);

#endif
