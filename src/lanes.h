/* SPDX-License-Identifier: GPL-2.0-or-later
 * The day agenda's events as bracket pairs (spec 001 FR-018; boards event.stack, event.drag,
 * GlassineOS/ui designs/planner Planner.dc.html toBrackets): which lane each timed event
 * stands in, and where its brackets lie. No cairo, no Wayland.
 *
 * Events that overlap, one through the next (a cluster), split the width into equal lanes for
 * the cluster's whole span: sorted by start (the longer first at the same start), each takes
 * the first lane free at its start, the first start lane 1, at most BK_LANES lanes (a fourth
 * at the same time shares the last lane). */
#ifndef PL_LANES_H
#define PL_LANES_H
#include <stdbool.h>

/* in px of the agenda: the first lane's "[" from the agenda's left edge (in the hour margin,
 * right of the hours), the last "]" from its right edge, the gap between one lane's "]" and
 * the next lane's "[", the serifs' length, the title's left edge from its lane's "[" (the
 * board's 44 - 36), the end handles' touch target (lane 1: it ends at the serif's tip and
 * reaches left over the margin; a later lane: only the gap and the serif, so a pen-down in
 * the neighbour's lane always writes there), the most lanes side by side, how long the
 * other brackets of a cluster stay grey after the pen lifts (ms) */
#define BK_SPINE 36
#define BK_RIGHT 10
#define BK_GAP 12
#define BK_SERIF 5
#define BK_TITLE 8
#define BK_HIT_W 44
#define BK_HIT_H 28
#define BK_LANES 3
#define BK_QUIET_MS 1000

struct lane {
	int lane;    /* 0 the first */
	int lanes;   /* its cluster's lanes (1: alone) */
	int cluster; /* its cluster's number, 0 the earliest */
};

/* the lanes of n events (start, end in minutes; end > start), out[i] for event i */
void lanes_assign(const int *start, const int *end, int n, struct lane *out);

/* lane l of k between x0 (the first "[") and x1 (the last "]"): its "[" and its width (its
 * "]" at *spine + *w) */
void lane_x(double x0, double x1, int l, int k, double *spine, double *w);

/* an end handle's touch target (x, w) for a lane whose "[" is at spine */
void lane_end_hit(double spine, int lane, double *x, double *w);

#endif
