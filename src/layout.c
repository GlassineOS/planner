// SPDX-License-Identifier: GPL-2.0-or-later
/* The fluid grid (layout.h). */
#include <math.h>
#include <string.h>

#include "layout.h"

static double wt(const struct lay_node *n)
{
	return n->weight > 0 ? n->weight : 1;
}

double lay_min_w(const struct lay_node *n)
{
	if (n->kind == LAY_LEAF)
		return n->min_w;
	double s = 0;
	for (int i = 0; i < n->nkids; i++) {
		double m = lay_min_w(n->kids[i]);
		if (n->kind == LAY_ROW)
			s += m;
		else if (m > s)
			s = m;
	}
	return s > n->min_w ? s : n->min_w;
}

static double pref_w(const struct lay_node *n)
{
	if (n->kind == LAY_LEAF)
		return n->pref_w > n->min_w ? n->pref_w : n->min_w;
	double s = 0;
	for (int i = 0; i < n->nkids; i++) {
		double p = pref_w(n->kids[i]);
		if (n->kind == LAY_ROW)
			s += p;
		else if (p > s)
			s = p;
	}
	return s;
}

/*
 * Hands out avail among n parts: min, then towards need by weight, then the rest to the
 * growers by weight. Returns the shortfall when the minimums don't fit (then every part
 * gets its min scaled down in proportion).
 */
static double share(int n, double avail, const double *mn, const double *need, const double *w, const bool *grow,
		    double *out)
{
	double smin = 0;
	for (int i = 0; i < n; i++)
		smin += mn[i];
	if (smin > avail) {
		for (int i = 0; i < n; i++)
			out[i] = smin > 0 ? mn[i] * avail / smin : avail / n;
		return smin - avail;
	}
	double left = avail - smin;
	bool active[LAY_MAX_KIDS];
	for (int i = 0; i < n; i++) {
		out[i] = mn[i];
		active[i] = need[i] > mn[i];
	}
	/* phase 2: water filling towards need */
	for (int round = 0; round < n + 1 && left > 1e-9; round++) {
		double tw = 0;
		for (int i = 0; i < n; i++)
			if (active[i])
				tw += w[i];
		if (tw <= 0)
			break;
		double given = 0;
		bool capped = false;
		for (int i = 0; i < n; i++) {
			if (!active[i])
				continue;
			double s = left * w[i] / tw;
			if (out[i] + s >= need[i]) {
				s = need[i] - out[i];
				active[i] = false;
				capped = true;
			}
			out[i] += s;
			given += s;
		}
		left -= given;
		if (!capped)
			break; /* everyone took their full share: nothing left */
	}
	if (left <= 1e-9)
		return 0;
	/* phase 3: the rest to the growers (to everyone when none grows) */
	double tw = 0;
	bool any = false;
	for (int i = 0; i < n; i++)
		if (grow[i]) {
			tw += w[i];
			any = true;
		}
	if (!any)
		for (int i = 0; i < n; i++)
			tw += w[i];
	for (int i = 0; i < n; i++)
		if (!any || grow[i])
			out[i] += left * w[i] / tw;
	return 0;
}

/* whole pixels that still sum to total (largest remainder), then the quantum */
static void integerize(int n, double total, double *v, const double *mn, const bool *grow, double quantum)
{
	double fl[LAY_MAX_KIDS], sum = 0;
	for (int i = 0; i < n; i++) {
		fl[i] = floor(v[i] + 1e-9);
		sum += fl[i];
	}
	int rest = (int)lround(floor(total + 1e-9) - sum);
	while (rest > 0) {
		int best = -1;
		double bf = -1;
		for (int i = 0; i < n; i++) {
			double f = v[i] - fl[i];
			if (f > bf) {
				bf = f;
				best = i;
			}
		}
		if (best < 0)
			break;
		fl[best] += 1;
		v[best] = fl[best]; /* its remainder is used */
		rest--;
	}
	if (quantum > 1 && n > 1) {
		int last = n - 1;
		for (int i = n - 1; i >= 0; i--)
			if (grow[i]) {
				last = i;
				break;
			}
		double moved = 0;
		for (int i = 0; i < n; i++) {
			if (i == last)
				continue;
			double q = floor(fl[i] / quantum) * quantum;
			if (q < mn[i])
				q = fl[i]; /* never under the minimum for a round number */
			moved += fl[i] - q;
			fl[i] = q;
		}
		fl[last] += moved;
	}
	for (int i = 0; i < n; i++)
		v[i] = fl[i];
}

/* heights: need at width w (sets n->w for leaves' want_h) */
static void measure(struct lay_node *n, double w)
{
	n->w = w;
	if (n->kind == LAY_LEAF) {
		double c = n->want_h ? n->want_h(n->ud, w) : n->min_h;
		n->content_h = c;
		double need = c < n->min_h ? n->min_h : c;
		if (n->pref_h > 0 && need > n->pref_h)
			need = n->pref_h > n->min_h ? n->pref_h : n->min_h;
		n->need_h = need;
		return;
	}
	double need = 0;
	for (int i = 0; i < n->nkids; i++) {
		struct lay_node *k = n->kids[i];
		/* a row's kids' widths are decided in place(); measure them at those */
		measure(k, n->kind == LAY_ROW ? k->w : w);
		if (n->kind == LAY_COL)
			need += k->need_h;
		else if (k->need_h > need)
			need = k->need_h;
	}
	n->need_h = need;
}

double lay_min_h(const struct lay_node *n, double w)
{
	if (n->kind == LAY_LEAF)
		return n->min_h;
	double s = 0;
	for (int i = 0; i < n->nkids; i++) {
		double m = lay_min_h(n->kids[i], w);
		if (n->kind == LAY_COL)
			s += m;
		else if (m > s)
			s = m;
	}
	return s > n->min_h ? s : n->min_h;
}

static bool grows(const struct lay_node *n)
{
	if (n->grow)
		return true;
	for (int i = 0; i < n->nkids; i++)
		if (grows(n->kids[i]))
			return true;
	return false;
}

/* widths, top down (they don't depend on content) */
static void widths(struct lay_node *n, double w, struct lay_report *rep)
{
	n->w = w;
	if (n->kind == LAY_LEAF)
		return;
	if (n->kind == LAY_COL) {
		for (int i = 0; i < n->nkids; i++)
			widths(n->kids[i], w, rep);
		return;
	}
	int k = n->nkids;
	double mn[LAY_MAX_KIDS] = { 0 }, need[LAY_MAX_KIDS] = { 0 }, ww[LAY_MAX_KIDS] = { 0 }, out[LAY_MAX_KIDS] = { 0 };
	bool gr[LAY_MAX_KIDS] = { 0 };
	for (int i = 0; i < k; i++) {
		mn[i] = lay_min_w(n->kids[i]);
		need[i] = pref_w(n->kids[i]);
		ww[i] = wt(n->kids[i]);
		gr[i] = true; /* spare width goes to everyone, by weight */
	}
	double s = share(k, w, mn, need, ww, gr, out);
	if (s > 0) {
		rep->infeasible_w = true;
		if (s > rep->short_w)
			rep->short_w = s;
	}
	integerize(k, w, out, mn, gr, 1);
	for (int i = 0; i < k; i++)
		widths(n->kids[i], out[i], rep);
}

static void place(struct lay_node *n, double x, double y, double h, double quantum, struct lay_report *rep)
{
	n->x = x;
	n->y = y;
	n->h = h;
	if (n->kind == LAY_LEAF) {
		n->clipped = n->content_h > h + 0.5;
		if (n->clipped)
			rep->clipped++;
		return;
	}
	if (n->kind == LAY_ROW) {
		double cx = x;
		for (int i = 0; i < n->nkids; i++) {
			place(n->kids[i], cx, y, h, quantum, rep);
			cx += n->kids[i]->w;
		}
		return;
	}
	int k = n->nkids;
	double mn[LAY_MAX_KIDS] = { 0 }, need[LAY_MAX_KIDS] = { 0 }, ww[LAY_MAX_KIDS] = { 0 }, out[LAY_MAX_KIDS] = { 0 };
	bool gr[LAY_MAX_KIDS] = { 0 };
	for (int i = 0; i < k; i++) {
		mn[i] = lay_min_h(n->kids[i], n->w);
		need[i] = n->kids[i]->need_h;
		ww[i] = wt(n->kids[i]);
		gr[i] = grows(n->kids[i]);
	}
	double s = share(k, h, mn, need, ww, gr, out);
	if (s > 0) {
		rep->infeasible_h = true;
		if (s > rep->short_h)
			rep->short_h = s;
	}
	integerize(k, h, out, mn, gr, s > 0 ? 1 : quantum);
	double cy = y;
	for (int i = 0; i < k; i++) {
		place(n->kids[i], x, cy, out[i], quantum, rep);
		cy += out[i];
	}
}

void lay_solve(struct lay_node *root, double x, double y, double w, double h, double quantum, struct lay_report *rep)
{
	struct lay_report dummy;
	if (!rep)
		rep = &dummy;
	memset(rep, 0, sizeof *rep);
	widths(root, w, rep);
	measure(root, w);
	place(root, x, y, h, quantum, rep);
	/* x of nested kids: place() sets x top down, widths() only w */
}

/* ---- hold while the pen writes ------------------------------------------------------ */

void lay_hold_init(struct lay_hold *h, long long pause_ms)
{
	memset(h, 0, sizeof *h);
	h->pause_ms = pause_ms;
	h->pen_up_ms = -1000000000LL;
}

void lay_hold_pen(struct lay_hold *h, bool down, long long now_ms)
{
	h->pen_down = down;
	if (!down)
		h->pen_up_ms = now_ms;
}

static bool quiet(const struct lay_hold *h, long long now_ms)
{
	return !h->pen_down && now_ms - h->pen_up_ms >= h->pause_ms;
}

bool lay_hold_request(struct lay_hold *h, long long now_ms)
{
	if (quiet(h, now_ms) && !h->pending)
		return true;
	if (!h->pending)
		h->held++;
	h->pending = true;
	return false;
}

bool lay_hold_due(struct lay_hold *h, long long now_ms)
{
	if (h->pending && quiet(h, now_ms)) {
		h->pending = false;
		return true;
	}
	return false;
}

long long lay_hold_wait(const struct lay_hold *h, long long now_ms)
{
	if (!h->pending || h->pen_down)
		return -1;
	long long w = h->pen_up_ms + h->pause_ms - now_ms;
	return w < 0 ? 0 : w;
}
