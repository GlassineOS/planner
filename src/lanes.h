/* SPDX-License-Identifier: GPL-2.0-or-later
 * The day agenda's events as bracket pairs (spec 001 FR-018; boards event.stack, event.drag,
 * event.more, GlassineOS/ui designs/planner Planner.dc.html toBrackets): which lane each timed
 * event stands in, and where its brackets lie. No cairo, no Wayland.
 *
 * Events that overlap, one through the next (a group), split the width into equal lanes for
 * the group's whole span: sorted by start (the longer first at the same start), each takes
 * the first lane free at its start, the first start lane 1. No lane is narrower than the
 * agenda's smallest lane (BK_MIN_LANE logical px unless the view sets its own): a group that
 * would need more lanes than fit is drawn in rows instead, one line across the width per
 * event, in start order. */
#ifndef PL_LANES_H
#define PL_LANES_H
#include <stdbool.h>

/* in px of the agenda: the first lane's "[" from the agenda's left edge (in the hour margin,
 * right of the hours), the last "]" from its right edge, the gap between one lane's "]" and
 * the next lane's "[", the serifs' length, the title's left edge from its lane's "[" (the
 * board's 44 - 36), the end handles' touch target (lane 1: it ends at the serif's tip and
 * reaches left over the margin; a later lane: only the gap and the serif, so a pen-down in
 * the neighbour's lane always writes there), the smallest lane (about 4 cm on the panel), a
 * lane under which its time goes under its title, how long the other brackets of a group stay
 * grey after the pen lifts (ms) */
#define BK_SPINE 36
#define BK_RIGHT 10
#define BK_GAP 12
#define BK_SERIF 5
#define BK_TITLE 8
#define BK_HIT_W 44
#define BK_HIT_H 28
#define BK_MIN_LANE 160
#define BK_NARROW 190
#define BK_QUIET_MS 1000

/* a group in rows (board event.more, Component.ROWS): a row's height in a page agenda and in a
 * small widget whose hours are fixed, the gap below each row, the room above the first and
 * below the last, the smallest pitch the other hours give room back down to, how much taller
 * the group's hours are drawn at least, the title's size in a row (page, widget), the fold bar's
 * height on one line and on two, its title's size, the width from which it says "tap to show" */
#define ROW_H 32
#define ROW_H_FIXED 26
#define ROW_GAP 4
#define ROW_PAD 4
#define ROW_MIN_PITCH 20
#define ROW_SCALE 2
#define ROW_FS 17.0
#define ROW_FS_FIXED 14.0
#define FOLD_H 24
#define FOLD_H2 40
#define FOLD_FS 12.0
#define FOLD_TAP_W 280

struct lane {
	int lane;    /* 0 the first (in rows: 0) */
	int lanes;   /* its group's lanes (1: alone, or in rows) */
	int group;   /* its group's number, 0 the earliest */
	int cols;    /* the lanes its group would need */
	bool rows;   /* its group is drawn in rows */
	int order;   /* its place in its group, in start order (the row it is in) */
	int count;   /* its group's events */
};

/* how many lanes of at least min_lane fit between x0 (the first "[") and x1 (the last "]") */
int lanes_fit(double x0, double x1, double min_lane);

/* the lanes of n events (start, end in minutes; end > start), out[i] for event i; a group that
 * would need more than kmax lanes is in rows */
void lanes_assign(const int *start, const int *end, int n, int kmax, struct lane *out);

/* lane l of k between x0 (the first "[") and x1 (the last "]"): its "[" and its width (its
 * "]" at *spine + *w) */
void lane_x(double x0, double x1, int l, int k, double *spine, double *w);

/* an end handle's touch target (x, w) for a lane whose "[" is at spine */
void lane_end_hit(double spine, int lane, double *x, double *w);

/* The agenda's scale: minutes of the day to y (px from the hours' top) and back, piecewise
 * linear through its knots (strictly increasing in both). A group in rows draws its hours
 * taller; the other hours give the room back evenly, never below ROW_MIN_PITCH. */
#define TMAP_N 34
struct tmap {
	int n;
	double m[TMAP_N], y[TMAP_N];
};
/* one pitch (px per hour) from minute m0 at y 0 */
void tmap_linear(struct tmap *t, double m0, double m1, double pitch);
double tmap_y(const struct tmap *t, double m);
double tmap_min(const struct tmap *t, double y);
/* px per minute at minute m (the knot segment it lies in) */
double tmap_ppm(const struct tmap *t, double m);

/* The hours from m0 to m1 at pitch, with ng groups in rows (gs[i]..ge[i] minutes, ascending and
 * apart, inside m0..m1), each needing need[i] px: each group drawn at least ROW_SCALE times
 * taller and at least its need, the room taken evenly from the other hours down to
 * ROW_MIN_PITCH (less than asked when there isn't more: then every group gets the same share of
 * what it asked). */
void tmap_stretch(struct tmap *t, double m0, double m1, double pitch, const int *gs, const int *ge, const double *need,
		  int ng);

/* where the rows of a group n events in start order stand in its span (y0..y1, px): each row
 * P px (its brackets P - ROW_GAP), from y0 + ROW_PAD; rows that don't fit with rh px each are
 * folded into a bar of bar px: *shown of n stand, the bar (when any are folded) at *bar_y.
 * fill: the rows grow to fill the span (a page agenda), else they keep rh (a small widget) */
void rows_place(double y0, double y1, int n, double rh, double bar, bool fill, double *p, int *shown, double *bar_y);

#endif
