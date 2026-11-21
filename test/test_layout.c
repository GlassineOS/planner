// SPDX-License-Identifier: GPL-2.0-or-later
/* The fluid grid's rules (src/layout.h), without drawing. */
#include <math.h>

#include "../src/layout.h"
#include "t.h"

struct content { double h; };
static double want(void *ud, double w)
{
	return ((struct content *)ud)->h;
}

static struct lay_node leaf(double min_h, double pref_h, double weight, bool grow, struct content *c)
{
	return (struct lay_node){ .kind = LAY_LEAF, .min_w = 40, .min_h = min_h, .pref_h = pref_h, .weight = weight,
				  .grow = grow, .want_h = c ? want : NULL, .ud = c };
}

static double sum_h(struct lay_node *col)
{
	double s = 0;
	for (int i = 0; i < col->nkids; i++)
		s += col->kids[i]->h;
	return s;
}

static void sums_and_minimums(void)
{
	struct content ca = { 500 }, cb = { 30 }, cc = { 900 };
	struct lay_node a = leaf(120, 0, 3, true, &ca), b = leaf(80, 300, 1, false, &cb), c = leaf(100, 0, 1, true, &cc);
	struct lay_node col = { .kind = LAY_COL, .kids = { &a, &b, &c }, .nkids = 3 };
	struct lay_report r;
	for (int H = 300; H <= 1400; H += 37) {
		lay_solve(&col, 0, 0, 600, H, 1, &r);
		CHECK_INT(lround(sum_h(&col)), H);
		CHECK(!r.infeasible_h);
		CHECK(a.h >= a.min_h && b.h >= b.min_h && c.h >= c.min_h);
		CHECK(a.h == floor(a.h) && b.h == floor(b.h)); /* whole pixels */
		CHECK(a.y == 0 && b.y == a.h && c.y == a.h + b.h);
	}
	/* the empty list stays at its minimum while the others take the space */
	lay_solve(&col, 0, 0, 600, 1200, 1, &r);
	CHECK_INT(lround(b.h), 80);
	/* too small for the minimums: reported, still sums */
	lay_solve(&col, 0, 0, 600, 250, 1, &r);
	CHECK(r.infeasible_h);
	CHECK_INT(lround(r.short_h), 50);
	CHECK_INT(lround(sum_h(&col)), 250);
	CHECK_INT(lround(lay_min_h(&col, 600)), 300);
}

static void content_moves_space(void)
{
	/* a to-do list (no grow) and an agenda (grows): the list takes what its content needs */
	struct content todo = { 0 }, ag = { 400 };
	struct lay_node t = leaf(90, 0, 1, false, &todo), g = leaf(200, 0, 2, true, &ag);
	struct lay_node col = { .kind = LAY_COL, .kids = { &g, &t }, .nkids = 2 };
	lay_solve(&col, 0, 0, 500, 800, 1, NULL);
	CHECK_INT(lround(t.h), 90);
	todo.h = 300;
	lay_solve(&col, 0, 0, 500, 800, 1, NULL);
	CHECK_INT(lround(t.h), 300);
	CHECK(!t.clipped);
	/* short of space: by weight, the list clipped (its more mark) */
	todo.h = 700;
	ag.h = 700;
	struct lay_report r;
	lay_solve(&col, 0, 0, 500, 800, 1, &r);
	CHECK(t.clipped && g.clipped);
	CHECK_INT(r.clipped, 2);
	/* (800 - 290 minimums) = 510 by weight 2:1 -> 340 : 170 */
	CHECK_INT(lround(g.h), 540);
	CHECK_INT(lround(t.h), 260);
}

static void growing_a_never_grows_b(void)
{
	struct content ca = { 0 }, cb = { 200 }, cc = { 150 };
	struct lay_node a = leaf(60, 0, 1, false, &ca), b = leaf(60, 0, 2, true, &cb), c = leaf(60, 400, 1, false, &cc);
	struct lay_node col = { .kind = LAY_COL, .kids = { &a, &b, &c }, .nkids = 3 };
	double lastb = 1e9, lastc = 1e9;
	for (double h = 0; h <= 1200; h += 13) {
		ca.h = h;
		lay_solve(&col, 0, 0, 400, 700, 1, NULL);
		CHECK(b.h <= lastb + 1); /* rounding may move a pixel */
		CHECK(c.h <= lastc + 1);
		lastb = b.h;
		lastc = c.h;
	}
}

static void idempotent(void)
{
	struct content ca = { 333 }, cb = { 77 };
	struct lay_node a = leaf(100, 0, 2, true, &ca), b = leaf(50, 0, 1, false, &cb);
	struct lay_node r1 = { .kind = LAY_ROW, .kids = { &a, &b }, .nkids = 2 };
	struct content cc = { 10 };
	struct lay_node c = leaf(40, 0, 1, true, &cc);
	struct lay_node col = { .kind = LAY_COL, .kids = { &r1, &c }, .nkids = 2 };
	lay_solve(&col, 10, 20, 640, 900, 8, NULL);
	struct lay_node s[3] = { a, b, c };
	lay_solve(&col, 10, 20, 640, 900, 8, NULL);
	CHECK(a.x == s[0].x && a.y == s[0].y && a.w == s[0].w && a.h == s[0].h);
	CHECK(b.x == s[1].x && b.w == s[1].w && c.y == s[2].y && c.h == s[2].h);
	/* a row: kids side by side, the row as tall as its tallest need */
	CHECK(a.x == 10 && b.x == a.x + a.w && a.w + b.w == 640);
	CHECK(a.h == b.h);
	/* the quantum: the row's height is a multiple of 8 (the grower c takes the rest) */
	CHECK_INT(lround(r1.h) % 8, 0);
	CHECK_INT(lround(r1.h + c.h), 900);
}

static void widths_by_weight(void)
{
	struct lay_node d[7];
	struct lay_node row = { .kind = LAY_ROW, .nkids = 7 };
	for (int i = 0; i < 7; i++) {
		d[i] = (struct lay_node){ .kind = LAY_LEAF, .min_w = 110, .min_h = 50, .weight = 1 };
		row.kids[i] = &d[i];
	}
	struct lay_report r;
	/* seven day columns of at least 110 px: fit in landscape (936), not in portrait (702) */
	lay_solve(&row, 0, 0, 936, 400, 1, &r);
	CHECK(!r.infeasible_w);
	lay_solve(&row, 0, 0, 702, 400, 1, &r);
	CHECK(r.infeasible_w);
	CHECK_INT(lround(r.short_w), 7 * 110 - 702);
	double s = 0;
	for (int i = 0; i < 7; i++)
		s += d[i].w;
	CHECK_INT(lround(s), 702);
	/* weights share the spare width */
	struct lay_node a = { .kind = LAY_LEAF, .min_w = 100, .weight = 1 }, b = { .kind = LAY_LEAF, .min_w = 100, .weight = 3 };
	struct lay_node r2 = { .kind = LAY_ROW, .kids = { &a, &b }, .nkids = 2 };
	lay_solve(&r2, 0, 0, 600, 100, 1, NULL);
	CHECK_INT(lround(a.w), 200);
	CHECK_INT(lround(b.w), 400);
}

static void ink_feeds_need(void)
{
	/* a notes field's content is its ink's extent: more ink, more height, never scaled */
	struct content ink = { 40 };
	struct lay_node n = leaf(120, 0, 1, false, &ink), g = leaf(100, 0, 1, true, NULL);
	struct lay_node col = { .kind = LAY_COL, .kids = { &n, &g }, .nkids = 2 };
	lay_solve(&col, 0, 0, 300, 700, 1, NULL);
	CHECK_INT(lround(n.h), 120);
	ink.h = 260;
	lay_solve(&col, 0, 0, 300, 700, 1, NULL);
	CHECK_INT(lround(n.h), 260);
	CHECK(!n.clipped);
	/* declared pref caps the need: past it, the more mark */
	n.pref_h = 200;
	lay_solve(&col, 0, 0, 300, 700, 1, NULL);
	CHECK_INT(lround(n.h), 200);
	CHECK(n.clipped);
}

static void hold(void)
{
	struct lay_hold h;
	lay_hold_init(&h, 600);
	CHECK(lay_hold_request(&h, 1000)); /* nothing writing: at once */
	lay_hold_pen(&h, true, 2000);
	CHECK(!lay_hold_request(&h, 2100));
	CHECK(!lay_hold_due(&h, 2200));
	CHECK_INT(lay_hold_wait(&h, 2200), -1);
	lay_hold_pen(&h, false, 2300);
	CHECK(!lay_hold_due(&h, 2500)); /* not paused long enough */
	CHECK_INT(lay_hold_wait(&h, 2500), 400);
	lay_hold_pen(&h, true, 2600); /* the next stroke: still held */
	lay_hold_pen(&h, false, 2700);
	CHECK(!lay_hold_due(&h, 3000));
	CHECK(lay_hold_due(&h, 3300));
	CHECK(!lay_hold_due(&h, 3400)); /* once */
	CHECK_INT(h.held, 1);
	CHECK(lay_hold_request(&h, 4000));
}

int main(void)
{
	sums_and_minimums();
	content_moves_space();
	growing_a_never_grows_b();
	idempotent();
	widths_by_weight();
	ink_feeds_need();
	hold();
	return t_done("layout");
}
