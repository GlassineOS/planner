// SPDX-License-Identifier: GPL-2.0-or-later
/* An agenda's events as bracket pairs in lanes (FR-018; board event.stack), and the finger
 * held still on one (board event.delete). */
#include <math.h>

#include "../src/hold.h"
#include "../src/lanes.h"
#include "t.h"

static void assign(const int *s, const int *e, int n, struct lane *o)
{
	for (int i = 0; i < n; i++)
		o[i] = (struct lane){ -1, -1, -1 };
	lanes_assign(s, e, n, o);
}

int main(void)
{
	struct lane o[8];
	/* alone: one lane each, a cluster each */
	{
		int s[] = { 420, 570, 1050 }, e[] = { 450, 630, 1080 };
		assign(s, e, 3, o);
		for (int i = 0; i < 3; i++)
			CHECK(o[i].lane == 0 && o[i].lanes == 1 && o[i].cluster == i);
	}
	/* the dentist and the bank: two lanes, the first start in the first */
	{
		int s[] = { 870, 840 }, e[] = { 930, 900 };
		assign(s, e, 2, o);
		CHECK(o[1].lane == 0 && o[0].lane == 1 && o[0].lanes == 2 && o[1].lanes == 2 && o[0].cluster == o[1].cluster);
	}
	/* the café around both: three lanes for the whole span, the café (first, longest) in the
	 * first */
	{
		int s[] = { 840, 870, 780 }, e[] = { 900, 930, 960 };
		assign(s, e, 3, o);
		CHECK(o[2].lane == 0 && o[0].lane == 1 && o[1].lane == 2);
		for (int i = 0; i < 3; i++)
			CHECK(o[i].lanes == 3 && o[i].cluster == 0);
	}
	/* the same start: the longer first; a lane free again is taken (a chain stays one cluster) */
	{
		int s[] = { 600, 600, 660, 700 }, e[] = { 630, 720, 690, 760 };
		assign(s, e, 4, o);
		CHECK(o[1].lane == 0 && o[0].lane == 1);
		CHECK(o[2].lane == 1); /* 11:00: the first lane holds 10:00–12:00, the second is free at 10:30 */
		CHECK(o[3].lane == 1);
		for (int i = 0; i < 4; i++)
			CHECK(o[i].lanes == 2 && o[i].cluster == 0);
	}
	/* touching ends don't overlap: a new cluster */
	{
		int s[] = { 600, 660 }, e[] = { 660, 720 };
		assign(s, e, 2, o);
		CHECK(o[0].lanes == 1 && o[1].lanes == 1 && o[0].cluster != o[1].cluster && o[1].lane == 0);
	}
	/* four at once: at most three lanes, the fourth shares the last */
	{
		int s[] = { 600, 600, 600, 600 }, e[] = { 660, 661, 662, 663 };
		assign(s, e, 4, o);
		int seen[3] = { 0 };
		for (int i = 0; i < 4; i++) {
			CHECK(o[i].lanes == BK_LANES && o[i].lane >= 0 && o[i].lane < BK_LANES);
			seen[o[i].lane]++;
		}
		CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 2);
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
