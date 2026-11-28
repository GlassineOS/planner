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

int lanes_fit(double x0, double x1, double min_lane)
{
	int k = (int)floor((x1 - x0 + BK_GAP) / (min_lane + BK_GAP));
	return k < 1 ? 1 : k;
}

void lanes_assign(const int *start, const int *end, int n, int kmax, struct lane *out)
{
	if (n <= 0)
		return;
	struct ord *o = malloc(n * sizeof *o);
	int *cols = malloc(n * sizeof *cols), *grp = malloc(n * sizeof *grp);
	if (!o || !cols || !grp) {
		for (int i = 0; i < n; i++)
			out[i] = (struct lane){ 0, 1, i, 1, false, 0, 1 };
		free(o), free(cols), free(grp);
		return;
	}
	for (int i = 0; i < n; i++)
		o[i] = (struct ord){ i, start[i], end[i] > start[i] ? end[i] : start[i] + 1 };
	qsort(o, n, sizeof *o, by_start);
	int ncols = 0, ng = 0, cl = 0, cend = 0;
	bool open = false;
	for (int k = 0; k <= n; k++) {
		if (open && (k == n || o[k].s >= cend)) { /* the group closes: its lanes, or rows */
			bool rows = ncols > (kmax < 1 ? 1 : kmax);
			for (int j = 0; j < ng; j++) {
				struct lane *l = &out[grp[j]];
				l->lanes = rows ? 1 : ncols, l->group = cl, l->cols = ncols, l->rows = rows, l->order = j, l->count = ng;
				if (rows)
					l->lane = 0;
			}
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
		out[o[k].i].lane = c;
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

void tmap_linear(struct tmap *t, double m0, double m1, double pitch)
{
	if (m1 <= m0)
		m1 = m0 + 60;
	t->n = 2;
	t->m[0] = m0, t->y[0] = 0;
	t->m[1] = m1, t->y[1] = (m1 - m0) / 60 * pitch;
}

/* the segment of the knots a value lies in, extended at both ends */
static int seg(const double *v, int n, double x)
{
	int i = 0;
	while (i < n - 2 && x > v[i + 1])
		i++;
	return i;
}

double tmap_y(const struct tmap *t, double m)
{
	if (t->n < 2)
		return 0;
	int i = seg(t->m, t->n, m);
	return t->y[i] + (m - t->m[i]) * (t->y[i + 1] - t->y[i]) / (t->m[i + 1] - t->m[i]);
}

double tmap_min(const struct tmap *t, double y)
{
	if (t->n < 2)
		return 0;
	int i = seg(t->y, t->n, y);
	return t->m[i] + (y - t->y[i]) * (t->m[i + 1] - t->m[i]) / (t->y[i + 1] - t->y[i]);
}

double tmap_ppm(const struct tmap *t, double m)
{
	if (t->n < 2)
		return 1;
	int i = seg(t->m, t->n, m);
	return (t->y[i + 1] - t->y[i]) / (t->m[i + 1] - t->m[i]);
}

void tmap_stretch(struct tmap *t, double m0, double m1, double pitch, const int *gs, const int *ge, const double *need,
		  int ng)
{
	tmap_linear(t, m0, m1, pitch);
	if (ng <= 0 || ng > (TMAP_N - 2) / 2)
		return;
	double ppm = pitch / 60, inside = 0, extra = 0;
	for (int i = 0; i < ng; i++) {
		double len = (ge[i] - gs[i]) * ppm, want = fmax(need[i], ROW_SCALE * len);
		inside += len;
		extra += fmax(0, want - len);
	}
	double total = (m1 - m0) * ppm, free = total - inside;
	double room = fmax(0, free - free * fmin(1, ROW_MIN_PITCH / pitch));
	double k = extra > room ? room / extra : 1, c = free > 0 ? (free - fmin(extra, room)) / free : 1;
	int n = 0;
	double y = 0, last = m0;
	t->m[n] = m0, t->y[n++] = 0;
	for (int i = 0; i < ng; i++) {
		double len = (ge[i] - gs[i]) * ppm, want = fmax(need[i], ROW_SCALE * len);
		y += (gs[i] - last) * ppm * c;
		if (gs[i] > t->m[n - 1])
			t->m[n] = gs[i], t->y[n++] = y;
		y += len + fmax(0, want - len) * k;
		t->m[n] = ge[i], t->y[n++] = y;
		last = ge[i];
	}
	if (m1 > last) {
		y += (m1 - last) * ppm * c;
		t->m[n] = m1, t->y[n++] = y;
	}
	t->n = n;
}

void rows_place(double y0, double y1, int n, double rh, double bar, bool fill, double *p, int *shown, double *bar_y)
{
	double space = y1 - y0;
	*shown = n;
	*p = rh;
	if (fill && n > 0)
		*p = fmax(rh, floor((space - 2 * ROW_PAD) / n));
	if (n * *p + 2 * ROW_PAD > space + 0.5) {
		*p = rh;
		int s = (int)floor((space - bar - 2 * ROW_PAD - ROW_GAP) / rh);
		*shown = s < 0 ? 0 : s > n ? n : s;
	}
	*bar_y = y0 + ROW_PAD + *shown * *p;
}
