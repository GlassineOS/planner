// SPDX-License-Identifier: GPL-2.0-or-later
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rewrap.h"

void rw_free(struct rw_place *p)
{
	free(p->dx), free(p->dy);
	memset(p, 0, sizeof *p);
}

struct sbox {
	int i;
	double x0, y0, x1, y1;
};

/* a stroke as drawn: its points moved by its word's layout */
static bool stroke_box(const struct field *f, int i, struct sbox *b)
{
	const struct pl_stroke *s = &f->s[i];
	if (!s->n)
		return false;
	int wi = field_word_of(f, s->k);
	double dx = wi >= 0 ? f->w[wi].dx : 0, dy = wi >= 0 ? f->w[wi].dy : 0;
	b->i = i, b->x0 = b->y0 = 1e9, b->x1 = b->y1 = -1e9;
	for (int j = 0; j < s->n; j++) {
		double x = s->pt[j].x + dx, y = s->pt[j].y + dy;
		b->x0 = fmin(b->x0, x), b->x1 = fmax(b->x1, x), b->y0 = fmin(b->y0, y), b->y1 = fmax(b->y1, y);
	}
	return true;
}

static int by_mid_y(const void *a, const void *b)
{
	const struct sbox *x = a, *y = b;
	double mx = x->y0 + x->y1, my = y->y0 + y->y1;
	return mx < my ? -1 : mx > my;
}

/* a stroke's line, then its x */
struct lbox {
	struct sbox b;
	int line;
};
static int by_line_x(const void *a, const void *b)
{
	const struct lbox *x = a, *y = b;
	if (x->line != y->line)
		return x->line - y->line;
	return x->b.x0 < y->b.x0 ? -1 : x->b.x0 > y->b.x0;
}

int rw_words(const struct field *f, struct rw_word *w, int max, int *word_of_stroke)
{
	if (word_of_stroke)
		for (int i = 0; i < f->ns; i++)
			word_of_stroke[i] = -1;
	struct sbox *b = malloc((f->ns ? f->ns : 1) * sizeof *b);
	struct lbox *lb = malloc((f->ns ? f->ns : 1) * sizeof *lb);
	double (*band)[2] = malloc((f->ns ? f->ns : 1) * sizeof *band);
	if (!b || !lb || !band) {
		free(b), free(lb), free(band);
		return 0;
	}
	int n = 0, tall = 0;
	for (int i = 0; i < f->ns; i++)
		if (stroke_box(f, i, &b[n]))
			tall += b[n].y1 - b[n].y0 >= RW_TALL, n++;
	/* lines from the strokes of a letter's height, taken by their middle: one whose middle lies
	 * inside the line's band so far joins it, else it starts the next line */
	qsort(b, n, sizeof *b, by_mid_y);
	int nl = 0;
	for (int i = 0; i < n; i++) {
		if (tall && b[i].y1 - b[i].y0 < RW_TALL)
			continue;
		double m = (b[i].y0 + b[i].y1) / 2;
		if (nl && m <= band[nl - 1][1] + 2 && m >= band[nl - 1][0] - 2) {
			band[nl - 1][0] = fmin(band[nl - 1][0], b[i].y0), band[nl - 1][1] = fmax(band[nl - 1][1], b[i].y1);
			lb[i] = (struct lbox){ b[i], nl - 1 };
		} else {
			band[nl][0] = b[i].y0, band[nl][1] = b[i].y1;
			lb[i] = (struct lbox){ b[i], nl++ };
		}
	}
	/* the small strokes (a dot, a cross, an accent) join the line nearest them */
	for (int i = 0; i < n; i++) {
		if (!tall || b[i].y1 - b[i].y0 >= RW_TALL)
			continue;
		double m = (b[i].y0 + b[i].y1) / 2, bd = 1e9;
		int bl = 0;
		for (int l = 0; l < nl; l++) {
			double d = m < band[l][0] ? band[l][0] - m : m > band[l][1] ? m - band[l][1] : 0;
			if (d < bd)
				bd = d, bl = l;
		}
		lb[i] = (struct lbox){ b[i], bl };
	}
	/* the words of each line: left to right, a new one past a visible gap */
	qsort(lb, n, sizeof *lb, by_line_x);
	int nw = 0;
	double right = -1e9;
	for (int j = 0; j < n; j++) {
		const struct sbox *s = &lb[j].b;
		bool fresh = nw == 0 || w[nw - 1].line != lb[j].line || s->x0 - right > RW_WORD_GAP;
		if (fresh && nw < max) {
			w[nw++] = (struct rw_word){ s->x0, s->y0, s->x1, s->y1, lb[j].line };
			right = s->x1;
		} else { /* past max: the rest of it joins the last word */
			struct rw_word *x = &w[nw - 1];
			x->x0 = fmin(x->x0, s->x0), x->y0 = fmin(x->y0, s->y0), x->x1 = fmax(x->x1, s->x1), x->y1 = fmax(x->y1, s->y1);
			right = fmax(right, s->x1);
		}
		if (word_of_stroke)
			word_of_stroke[s->i] = nw - 1;
	}
	free(b), free(lb), free(band);
	return nw;
}

bool rw_place(const struct field *f, double width, struct rw_place *out)
{
	memset(out, 0, sizeof *out);
	if (!f->ns)
		return false;
	for (int i = 0; i < f->nw; i++)
		if (f->w[i].set && !f->w[i].nks)
			return false; /* a typed word: no strokes to place */
	struct rw_word w[RW_WORDS];
	int *wos = malloc(f->ns * sizeof *wos);
	if (!wos)
		return false;
	int nw = rw_words(f, w, RW_WORDS, wos);
	if (!nw) {
		free(wos);
		return false;
	}
	/* the line pitch: the tallest written line, and its words' tops kept as written in it */
	double lh = 0, top[RW_WORDS];
	int nl = w[nw - 1].line + 1;
	for (int l = 0; l < nl; l++) {
		double t = 1e9, b = -1e9;
		for (int i = 0; i < nw; i++)
			if (w[i].line == l)
				t = fmin(t, w[i].y0), b = fmax(b, w[i].y1);
		for (int i = 0; i < nw; i++)
			if (w[i].line == l)
				top[i] = t;
		lh = fmax(lh, b - t);
	}
	double ox[RW_WORDS], oy[RW_WORDS], x = 0, wmax = 0;
	int line = 0;
	for (int i = 0; i < nw; i++) {
		double ww = w[i].x1 - w[i].x0;
		if (i > 0) {
			if (width > 0 && x + RW_SPACE + ww > width)
				line++, x = 0;
			else
				x += RW_SPACE;
		}
		ox[i] = x - w[i].x0;
		oy[i] = line * (lh + RW_LINE_GAP) - top[i];
		x += ww;
		wmax = fmax(wmax, x);
	}
	out->n = f->ns;
	out->dx = calloc(f->ns, sizeof *out->dx);
	out->dy = calloc(f->ns, sizeof *out->dy);
	if (!out->dx || !out->dy) {
		free(wos);
		rw_free(out);
		return false;
	}
	for (int i = 0; i < f->ns; i++)
		if (wos[i] >= 0)
			out->dx[i] = (float)ox[wos[i]], out->dy[i] = (float)oy[wos[i]];
	out->lines = line + 1;
	out->w = wmax;
	out->h = (line + 1) * lh + line * RW_LINE_GAP;
	free(wos);
	return true;
}

bool rw_fit(const struct field *f, double w, double h, struct rw_place *out, double *k, bool *cut)
{
	*cut = false;
	for (double s = 1; s >= RW_LEAST - 1e-6; s -= 0.025) {
		struct rw_place p;
		if (!rw_place(f, w / s, &p))
			return false;
		if (p.h * s <= h + 0.5 && p.w * s <= w + 0.5) {
			*out = p, *k = s;
			return true;
		}
		rw_free(&p);
	}
	*k = RW_LEAST, *cut = true;
	return rw_place(f, 0, out);
}
