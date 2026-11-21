/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The fluid grid (spike): a tree of rows and columns whose leaves are widgets.
 * No cairo, no Wayland, no clock: the caller passes sizes, content and time.
 *
 * Declarations per node (along the axis of its parent):
 *   weight   its share when space is short (phase 2) or spare (phase 3)
 *   min      never less (FR-044), unless the whole template can't fit (infeasible)
 *   pref     the most it asks for its content (0 = no cap); width: the width it likes
 *   grow     takes spare space (free notes, the agenda); others stay at their need
 *
 * Widths come from the declarations alone (top down). Heights follow content: a
 * leaf's need is its content height at its width (want_h), clamped to [min, pref].
 * A column hands out its height in three phases:
 *   1. every child gets its min;
 *   2. what is left goes towards each child's need, by weight (water filling);
 *   3. what is left after that goes to the growing children, by weight (all children
 *      by weight when none grows).
 * A row's height is its tallest child's; every child of a row gets the row's height.
 * Sizes are whole pixels (largest remainder); a quantum > 1 rounds the share of every
 * child of a column down to multiples of it and gives the rest to the last grower.
 *
 * A leaf whose content is taller than its height is clipped (its "more" mark).
 */
#ifndef PL_LAYOUT_H
#define PL_LAYOUT_H

#include <stdbool.h>

enum lay_kind { LAY_LEAF, LAY_ROW, LAY_COL };

#define LAY_MAX_KIDS 16

struct lay_node {
	enum lay_kind kind;
	double weight;
	double min_w, min_h;
	double pref_w, pref_h; /* 0: none */
	bool grow;
	/* leaf: content height at width w (NULL: min_h) */
	double (*want_h)(void *ud, double w);
	void *ud;
	struct lay_node *kids[LAY_MAX_KIDS];
	int nkids;

	/* out */
	double x, y, w, h;
	double need_h;    /* content height clamped to [min, pref] (containers: of the kids) */
	double content_h; /* leaf: content height unclamped */
	bool clipped;     /* leaf: content_h > h */
};

struct lay_report {
	bool infeasible_w, infeasible_h; /* the minimums don't fit: every node got less, in proportion */
	double short_w, short_h;         /* by how much */
	int clipped;                     /* leaves with a more mark */
};

void lay_solve(struct lay_node *root, double x, double y, double w, double h, double quantum, struct lay_report *rep);

/* the minimum size of a tree (template editor: FR-044) */
double lay_min_w(const struct lay_node *n);
double lay_min_h(const struct lay_node *n, double w);

/*
 * Hold while the pen writes (FR-046): layout changes asked for while a stroke is on
 * or within pause_ms of its end wait; lay_hold_due says when to apply them.
 */
struct lay_hold {
	bool pen_down;
	long long pen_up_ms;
	bool pending;
	long long pause_ms;
	int held; /* requests that waited (tests, the ledger) */
};
void lay_hold_init(struct lay_hold *h, long long pause_ms);
void lay_hold_pen(struct lay_hold *h, bool down, long long now_ms);
/* a layout change is wanted: true = apply it now, false = it waits */
bool lay_hold_request(struct lay_hold *h, long long now_ms);
/* a waiting change is due now; clears it */
bool lay_hold_due(struct lay_hold *h, long long now_ms);
/* ms until a waiting change is due (-1: nothing waits or the pen is down) */
long long lay_hold_wait(const struct lay_hold *h, long long now_ms);

#endif
