// SPDX-License-Identifier: GPL-2.0-or-later
#include <math.h>
#include <stdlib.h>

#include "lanes.h"

struct ord {
	int i, s, e;
};

static int by_start(const void *a, const void *b)
{
	const struct ord *x = a, *y = b;
	if (x->s != y->s)
		return x->s < y->s ? -1 : 1;
	if (x->e != y->e) /* the longer first */
		return x->e > y->e ? -1 : 1;
	return x->i - y->i;
}

void lanes_assign(const int *start, const int *end, int n, struct lane *out)
{
	if (n <= 0)
		return;
	struct ord *o = malloc(n * sizeof *o);
	int *cols = malloc(n * sizeof *cols), *grp = malloc(n * sizeof *grp);
	if (!o || !cols || !grp) {
		for (int i = 0; i < n; i++)
			out[i] = (struct lane){ 0, 1, i };
		free(o), free(cols), free(grp);
		return;
	}
	for (int i = 0; i < n; i++)
		o[i] = (struct ord){ i, start[i], end[i] > start[i] ? end[i] : start[i] + 1 };
	qsort(o, n, sizeof *o, by_start);
	int ncols = 0, ng = 0, cl = 0, cend = 0;
	bool open = false;
	for (int k = 0; k <= n; k++) {
		if (open && (k == n || o[k].s >= cend)) { /* the cluster closes: its lanes */
			int lanes = ncols < BK_LANES ? ncols : BK_LANES;
			for (int j = 0; j < ng; j++)
				out[grp[j]].lanes = lanes < 1 ? 1 : lanes, out[grp[j]].cluster = cl;
			cl++, ng = 0, ncols = 0, open = false;
		}
		if (k == n)
			break;
		int c = 0;
		while (c < ncols && cols[c] > o[k].s)
			c++;
		if (c == ncols)
			cols[ncols++] = 0;
		cols[c] = o[k].e;
		out[o[k].i].lane = c < BK_LANES ? c : BK_LANES - 1;
		grp[ng++] = o[k].i;
		cend = open && cend > o[k].e ? cend : o[k].e;
		open = true;
	}
	free(o), free(cols), free(grp);
}

void lane_x(double x0, double x1, int l, int k, double *spine, double *w)
{
	if (k < 1)
		k = 1;
	double lw = floor((x1 - x0 - (k - 1) * BK_GAP) / k);
	*spine = x0 + l * (lw + BK_GAP);
	*w = lw;
}

void lane_end_hit(double spine, int lane, double *x, double *w)
{
	if (lane) { /* over the gap and the serif only */
		*x = spine - BK_GAP + 1;
		*w = BK_GAP + BK_SERIF;
	} else {
		*x = spine + 6 - BK_HIT_W;
		*w = BK_HIT_W;
	}
}
