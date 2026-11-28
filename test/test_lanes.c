// SPDX-License-Identifier: GPL-2.0-or-later
/* An agenda's events as bracket pairs in lanes or rows (FR-018; boards event.stack, event.more),
 * the scale of its hours, a title's words rewrapped (board event.wrap), and the finger held still
 * on an event (board event.delete). */
#include <math.h>

#include "../src/hold.h"
#include "../src/lanes.h"
#include "../src/rewrap.h"
#include <stdlib.h>
#include "t.h"

static void assign_k(const int *s, const int *e, int n, int kmax, struct lane *o)
{
	for (int i = 0; i < n; i++)
		o[i] = (struct lane){ -1, -1, -1, -1, true, -1, -1 };
	lanes_assign(s, e, n, kmax, o);
}
static void assign(const int *s, const int *e, int n, struct lane *o)
{
	assign_k(s, e, n, 4, o);
}

/* a written word: a few short strokes from x, each w wide, h tall at y */
static void word(struct field *f, uint32_t *k, double x, double y, int letters)
{
	for (int l = 0; l < letters; l++) {
		struct pl_point p[3] = { { (float)(x + l * 9), (float)(y + 14), 0.5f, 0 },
					 { (float)(x + l * 9 + 3), (float)y, 0.5f, 0 },
					 { (float)(x + l * 9 + 6), (float)(y + 14), 0.5f, 0 } };
		field_add_stroke(f, (*k)++, p, 3);
	}
}

int main(void)
{
	struct lane o[8];
	/* alone: one lane each, a cluster each */
	{
		int s[] = { 420, 570, 1050 }, e[] = { 450, 630, 1080 };
		assign(s, e, 3, o);
		for (int i = 0; i < 3; i++)
			CHECK(o[i].lane == 0 && o[i].lanes == 1 && o[i].group == i);
	}
	/* the dentist and the bank: two lanes, the first start in the first */
	{
		int s[] = { 870, 840 }, e[] = { 930, 900 };
		assign(s, e, 2, o);
		CHECK(o[1].lane == 0 && o[0].lane == 1 && o[0].lanes == 2 && o[1].lanes == 2 && o[0].group == o[1].group);
	}
	/* the café around both: three lanes for the whole span, the café (first, longest) in the
	 * first */
	{
		int s[] = { 840, 870, 780 }, e[] = { 900, 930, 960 };
		assign(s, e, 3, o);
		CHECK(o[2].lane == 0 && o[0].lane == 1 && o[1].lane == 2);
		for (int i = 0; i < 3; i++)
			CHECK(o[i].lanes == 3 && o[i].group == 0);
	}
	/* the same start: the longer first; a lane free again is taken (a chain stays one cluster) */
	{
		int s[] = { 600, 600, 660, 700 }, e[] = { 630, 720, 690, 760 };
		assign(s, e, 4, o);
		CHECK(o[1].lane == 0 && o[0].lane == 1);
		CHECK(o[2].lane == 1); /* 11:00: the first lane holds 10:00–12:00, the second is free at 10:30 */
		CHECK(o[3].lane == 1);
		for (int i = 0; i < 4; i++)
			CHECK(o[i].lanes == 2 && o[i].group == 0);
	}
	/* touching ends don't overlap: a new cluster */
	{
		int s[] = { 600, 660 }, e[] = { 660, 720 };
		assign(s, e, 2, o);
		CHECK(o[0].lanes == 1 && o[1].lanes == 1 && o[0].group != o[1].group && o[1].lane == 0);
	}
	/* four at once where three lanes fit: the whole group in rows, one each in start order */
	{
		int s[] = { 600, 610, 600, 620 }, e[] = { 660, 661, 662, 663 };
		assign_k(s, e, 4, 3, o);
		for (int i = 0; i < 4; i++)
			CHECK(o[i].rows && o[i].lanes == 1 && o[i].lane == 0 && o[i].cols == 4 && o[i].count == 4);
		CHECK(o[2].order == 0 && o[0].order == 1 && o[1].order == 2 && o[3].order == 3);
		/* where four fit: lanes */
		assign_k(s, e, 4, 4, o);
		for (int i = 0; i < 4; i++)
			CHECK(!o[i].rows && o[i].lanes == 4);
	}
	/* rows are a group's: a group apart keeps its lanes */
	{
		int s[] = { 600, 600, 600, 900, 900 }, e[] = { 660, 660, 660, 960, 960 };
		assign_k(s, e, 5, 2, o);
		CHECK(o[0].rows && o[1].rows && o[2].rows && !o[3].rows && !o[4].rows && o[3].lanes == 2);
	}
	/* lanes of at least 160 px: three on 646 px, four on 676 px, on a 360 px wide
	 * widget's 314 px one */
	CHECK_INT(lanes_fit(36, 682, 160), 3);
	CHECK_INT(lanes_fit(36, 36 + 4 * 160 + 3 * BK_GAP, 160), 4);
	CHECK_INT(lanes_fit(36, 350, 160), 1);
	CHECK_INT(lanes_fit(36, 50, 160), 1);
	/* the scale: linear, and a group in rows at least twice as tall, the other hours giving the
	 * room back evenly; the whole stays as tall; minute -> y -> minute round trips */
	{
		struct tmap t;
		tmap_linear(&t, 360, 1380, 40);
		CHECK(fabs(tmap_y(&t, 420) - 40) < 1e-9 && fabs(tmap_min(&t, 80) - 480) < 1e-9);
		int gs[] = { 780 }, ge[] = { 960 };
		double need[] = { 5 * ROW_H + 2 * ROW_PAD };
		tmap_stretch(&t, 360, 1380, 40, gs, ge, need, 1);
		double h0 = tmap_y(&t, 780), h1 = tmap_y(&t, 960);
		CHECK(h1 - h0 >= ROW_SCALE * 3 * 40 - 0.5);
		CHECK(fabs(tmap_y(&t, 1380) - 17 * 40) < 1e-6);
		CHECK(tmap_y(&t, 420) < 40 && tmap_y(&t, 420) >= ROW_MIN_PITCH - 1e-9);
		bool rt = true;
		for (int m = 360; m <= 1380; m += 7)
			rt = rt && fabs(tmap_min(&t, tmap_y(&t, m)) - m) < 1e-6;
		CHECK(rt);
		CHECK(fabs(tmap_ppm(&t, 800) * 60 - (h1 - h0) / 3) < 1e-6);
		/* not enough room: the other hours at the smallest pitch, never smaller */
		tmap_stretch(&t, 360, 1380, 22, gs, ge, need, 1);
		CHECK(tmap_y(&t, 420) >= ROW_MIN_PITCH - 1e-9 && fabs(tmap_y(&t, 1380) - 17 * 22) < 1e-6);
	}
	/* rows: each a row of its own, filling the group; in a widget the rest folded into a bar */
	{
		double p, by;
		int shown;
		rows_place(0, 240, 5, ROW_H, FOLD_H, true, &p, &shown, &by);
		CHECK(shown == 5 && p >= ROW_H && 5 * p + 2 * ROW_PAD <= 240);
		rows_place(0, 90, 5, ROW_H_FIXED, FOLD_H, false, &p, &shown, &by);
		CHECK(shown == 2 && p == ROW_H_FIXED && by + FOLD_H <= 90);
		rows_place(0, 60, 5, ROW_H_FIXED, FOLD_H2, false, &p, &shown, &by);
		CHECK(shown == 0 && by + FOLD_H2 <= 60);
	}
	/* a title's words: split at the gaps, wrapped to a width, the strokes never changed */
	{
		struct field f = { 0 };
		uint32_t k = 1;
		word(&f, &k, 0, 0, 4);   /* "take" 0..33 */
		word(&f, &k, 45, 0, 3);  /* "the" 45..69 */
		word(&f, &k, 81, 0, 5);  /* "coats" 81..123 */
		word(&f, &k, 135, 2, 2); /* "to" */
		struct rw_word w[8];
		int ws_[32];
		CHECK_INT(rw_words(&f, w, 8, ws_), 4);
		CHECK(w[0].x0 == 0 && w[0].x1 == 33 && w[1].x0 == 45 && w[3].line == 0);
		float before = f.s[5].pt[1].x;
		struct rw_place pl;
		CHECK(rw_place(&f, 80, &pl));
		CHECK_INT(pl.lines, 2);
		CHECK(pl.w <= 80 && f.s[5].pt[1].x == before);
		/* every word inside the width, on its line, none over another */
		bool in = true;
		for (int i = 0; i < f.ns; i++) {
			int wi = ws_[i];
			for (int j = 0; j < f.s[i].n; j++) {
				double x = f.s[i].pt[j].x + pl.dx[i], y = f.s[i].pt[j].y + pl.dy[i];
				in = in && x >= -1e-6 && x <= 80 + 1e-6 && y >= -1e-6 && y <= pl.h + 1e-6;
			}
			(void)wi;
		}
		CHECK(in);
		rw_free(&pl);
		CHECK(rw_place(&f, 0, &pl) && pl.lines == 1);
		rw_free(&pl);
		/* fitted: shrinks onto the lines there are, to no less than 70 %, then cut */
		double kk;
		bool cut;
		CHECK(rw_fit(&f, 90, 40, &pl, &kk, &cut) && !cut && kk <= 1 && kk >= RW_LEAST);
		CHECK(pl.w * kk <= 90.5 && pl.h * kk <= 40.5);
		rw_free(&pl);
		CHECK(rw_fit(&f, 60, 12, &pl, &kk, &cut) && cut && fabs(kk - RW_LEAST) < 1e-9 && pl.lines == 1);
		rw_free(&pl);
		field_free(&f);
	}
	/* a dot above a letter, a cross stroke and an accent stay with their word, on its line */
	{
		struct field f = { 0 };
		uint32_t k = 1;
		word(&f, &k, 0, 4, 3);
		struct pl_point dot[1] = { { 12, 0, 0.5f, 0 } };
		field_add_stroke(&f, k++, dot, 1);
		struct pl_point cross[2] = { { 2, 9, 0.5f, 0 }, { 10, 9, 0.5f, 0 } };
		field_add_stroke(&f, k++, cross, 2);
		word(&f, &k, 40, 4, 2);
		struct pl_point acc[2] = { { 44, 0, 0.5f, 0 }, { 47, -2, 0.5f, 0 } };
		field_add_stroke(&f, k++, acc, 2);
		struct rw_word w[8];
		int ws_[16];
		CHECK_INT(rw_words(&f, w, 8, ws_), 2);
		CHECK(w[0].line == 0 && w[1].line == 0 && ws_[3] == 0 && ws_[4] == 0 && ws_[7] == 1);
		field_free(&f);
	}
	/* a title written on two lines: two lines of words, in reading order */
	{
		struct field f = { 0 };
		uint32_t k = 1;
		word(&f, &k, 0, 0, 5);
		word(&f, &k, 0, 27, 3);
		word(&f, &k, 40, 27, 3);
		struct rw_word w[8];
		CHECK_INT(rw_words(&f, w, 8, NULL), 3);
		CHECK(w[0].line == 0 && w[1].line == 1 && w[2].line == 1 && w[1].x0 < w[2].x0);
		struct rw_place pl;
		CHECK(rw_place(&f, 0, &pl) && pl.lines == 1); /* unwrapped onto one line */
		rw_free(&pl);
		CHECK(rw_place(&f, 1000, &pl) && pl.lines == 1);
		rw_free(&pl);
		field_free(&f);
	}
	/* the lanes' brackets: equal widths, neighbours BK_GAP apart, the last "]" at x1 or within
	 * a px of it */
	for (int k = 1; k <= 3; k++) {
		double x0 = 36, x1 = 692, prev = 0;
		for (int l = 0; l < k; l++) {
			double sp, w;
			lane_x(x0, x1, l, k, &sp, &w);
			CHECK(w == floor((x1 - x0 - (k - 1) * BK_GAP) / k));
			if (l)
				CHECK(fabs(sp - prev - BK_GAP) < 1e-9);
			else
				CHECK(sp == x0);
			prev = sp + w;
			if (l == k - 1)
				CHECK(prev <= x1 && prev > x1 - k);
		}
	}
	/* the end handles: the first lane's reaches left over the margin, a later lane's covers only
	 * the gap and the serif, so it ends before its title */
	{
		double x, w;
		lane_end_hit(36, 0, &x, &w);
		CHECK(x == 36 + 6 - BK_HIT_W && w == BK_HIT_W);
		lane_end_hit(300, 1, &x, &w);
		CHECK(x == 300 - BK_GAP + 1 && w == BK_GAP + BK_SERIF && x + w <= 300 + BK_TITLE);
		CHECK(x > 300 - BK_GAP); /* not over the neighbour's "]" */
	}
	/* held still for 0.5 s it fires once; moved past the slop it is a drag */
	{
		struct hold hd = { 0 };
		hold_begin(&hd, 7, 100, 100, 1000);
		CHECK_INT(hold_left(&hd, 1200), 300);
		CHECK(hold_move(&hd, 100 + HOLD_SLOP, 100 - HOLD_SLOP));
		CHECK(!hold_fire(&hd, 1499));
		CHECK(hold_fire(&hd, 1500) && hd.fired && hd.id == 7);
		CHECK(!hold_fire(&hd, 1600) && hold_left(&hd, 1600) == -1);
		hold_begin(&hd, 7, 100, 100, 1000);
		CHECK(!hold_move(&hd, 100 + HOLD_SLOP + 1, 100));
		CHECK(!hold_fire(&hd, 2000) && hold_left(&hd, 2000) == -1);
		hold_end(&hd);
		CHECK_INT(hold_left(&hd, 0), -1);
	}
	return t_done("lanes");
}
