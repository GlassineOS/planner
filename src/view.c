// SPDX-License-Identifier: GPL-2.0-or-later
/* One screen of the planner (view.h), after the boards in GlassineOS/ui designs/planner
 * (planner.css, data/planner.js, Planner.dc.html). One function both lays out and paints:
 * with paint off it only measures, collects the fields and the tap targets. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace.h"
#include "view.h"
#include "rewrap.h"

int pl_trace;
double pl_tr_text_ms, pl_tr_ink_ms, pl_tr_paint_ms, pl_tr_diff_ms;
int pl_tr_text_n, pl_tr_ink_n;

#define PAD 12          /* the page's inset */
#define BAR V_BAR       /* the header bar */
#define GUTTER 40       /* the agenda's hour gutter */
#define INK_SIZE 19.0   /* the size ink is written at in a field; smaller places scale it */
#define TITLE_MIN (INK_SIZE * RW_LEAST) /* the least a title shrinks to: about 70 % of its written size */
#define MAXE 256

static const char *DOW[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
static const char *DOW2[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static const char *DOW3[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *MON[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
	"October", "November", "December" };

struct ctx {
	struct view *v;
	struct store *s;
	const struct look *L;
	cairo_t *cr;
	UiGc g;
	int paint;
	/* the day agendas' events as this pass lays them out (into the view's after a layout) */
	struct vev *evb;
	int *nevb;
	bool *evs_out;
};

/* ---- small helpers ---- */

static void col(cairo_t *cr, ui_col c)
{
	cairo_set_source_rgba(cr, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0, (c & 255) / 255.0, (c >> 24 & 255) / 255.0);
}

static UiFont fpx(UiFont f, float px)
{
	f.px = px;
	return f;
}

static UiFont fw(UiFont f, int w)
{
	f.weight = w;
	return f;
}

static UiFont fam(const char *family, int w, int it, float px)
{
	return (UiFont){ family, w, it, px, 0, 0, UI_CASE_NONE };
}

#define TR_IN() double tr0_ = pl_trace ? pl_tr_now() : 0
#define TR_OUT(ms, n) (pl_trace ? ((ms) += pl_tr_now() - tr0_, (n)++) : 0)

static double tw(struct ctx *c, const UiFont *f, const char *s)
{
	double h;
	TR_IN();
	double w = ui_text_w(&c->g, f, s, &h);
	TR_OUT(pl_tr_text_ms, pl_tr_text_n);
	return w;
}

/* text with its vertical centre at cy */
static double tc(struct ctx *c, const UiFont *f, const char *s, double x, double cy, int align, double maxw, ui_col cl)
{
	double h;
	TR_IN();
	double w = ui_text_w(&c->g, f, s, &h);
	if (c->paint)
		ui_text(&c->g, f, s, x, cy - h / 2, align, maxw, cl, &h);
	TR_OUT(pl_tr_text_ms, pl_tr_text_n);
	return w;
}

/* text with its baseline-ish bottom at by (the line box's bottom) */
static double tb(struct ctx *c, const UiFont *f, const char *s, double x, double by, int align, double maxw, ui_col cl)
{
	double h;
	TR_IN();
	double w = ui_text_w(&c->g, f, s, &h);
	if (c->paint)
		ui_text(&c->g, f, s, x, by - h, align, maxw, cl, &h);
	TR_OUT(pl_tr_text_ms, pl_tr_text_n);
	return w;
}

static void hit(struct ctx *c, double x, double y, double w, double h, enum act a, int arg, uint32_t id)
{
	struct view *v = c->v;
	if (c->paint || v->nh >= V_HITS)
		return;
	v->hit[v->nh++] = (struct vhit){ x, y, w, h, a, arg, id, false };
}

/* a target on the bar above the keyboard: screen coordinates */
static void hitf(struct ctx *c, double x, double y, double w, double h, enum act a, int arg)
{
	struct view *v = c->v;
	if (c->paint || v->nh >= V_HITS)
		return;
	v->hit[v->nh++] = (struct vhit){ x, y, w, h, a, arg, 0, true };
}

static struct vfield *field(struct ctx *c, enum fkind k, double x, double y, double w, double h)
{
	struct view *v = c->v;
	if (c->paint || v->nf >= V_FIELDS)
		return NULL;
	struct vfield *f = &v->f[v->nf++];
	memset(f, 0, sizeof *f);
	f->k = k;
	f->x = x, f->y = y, f->w = w, f->h = h;
	return f;
}

static struct vfield *entry_field(struct ctx *c, const struct entry *e, double x, double y, double w, double h,
				  double guide, double first)
{
	struct vfield *f = field(c, F_ENTRY, x, y, w, h);
	if (f) {
		f->entry = e->id;
		snprintf(f->name, sizeof f->name, "%s", e->uid);
		f->guide = guide;
		f->first = first;
	}
	return f;
}

static void new_field(struct ctx *c, enum ekind k, struct period per, const char *slot, int rank, const char *where,
		      double x, double y, double w, double h, double guide, double first)
{
	struct vfield *f = field(c, F_NEW, x, y, w, h);
	if (!f)
		return;
	f->ekind = k;
	f->per = per;
	f->rank = rank;
	snprintf(f->slot, sizeof f->slot, "%s", slot ? slot : "");
	snprintf(f->name, sizeof f->name, "new:%s:%d:%d:%d:%s:%d", where, per.kind, per.start, k, slot ? slot : "", rank);
	f->guide = guide;
	f->first = first;
}

double pl_ink_width(double p)
{
	p = p < 0 ? 0 : p > 1 ? 1 : p;
	return PL_INK_WMIN + (PL_INK_WMAX - PL_INK_WMIN) * (PL_INK_EXP == 1.0 ? p : pow(p, PL_INK_EXP));
}

/* the ink of a field at ox, oy, scaled by k (1 in its own field: ink is never scaled there),
 * word by word as its region laid it out (FR-014): an ink word's strokes moved by its dx, dy,
 * neatened while the field's Neaten is on (FR-011); a set word (edited in text mode) filled
 * from its outline in Autograph; while the field's Set in Autograph is on, a word that was
 * read filled from its outline in Autograph too (its strokes kept, not drawn); ink that is no
 * word (a drawing) as written */
static void ink_(struct ctx *c, const struct field *f, double ox, double oy, double k);
static void ink(struct ctx *c, const struct field *f, double ox, double oy, double k)
{
	TR_IN();
	ink_(c, f, ox, oy, k);
	TR_OUT(pl_tr_ink_ms, pl_tr_ink_n);
}
/* the strokes and set words of a field, with the source and transform cr has */
static void ink_paths(cairo_t *cr, const struct field *f, double ox, double oy, double k, bool neat, bool ag,
		      const struct rw_place *pl)
{
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	for (int i = 0; i < f->ns; i++) {
		const struct pl_stroke *raw = &f->s[i], *s = raw;
		if (neat && field_neat(f, raw->k) && field_neat(f, raw->k)->n)
			s = field_neat(f, raw->k);
		if (!s->n)
			continue;
		int wi = field_word_of(f, raw->k);
		if (wi >= 0 && ag && f->w[wi].set && f->w[wi].npath)
			continue; /* shown set in Autograph */
		double dx = wi >= 0 ? f->w[wi].dx * k : 0, dy = wi >= 0 ? f->w[wi].dy * k : 0;
		if (pl && i < pl->n) /* rewrapped: where its word is placed */
			dx += pl->dx[i] * k, dy += pl->dy[i] * k;
		double ks = k < 1 ? fmax(0.6, k) : 1;
#define W_AT(a, b) fmax(0.9, pl_ink_width(((a).p + (b).p) / 2) * ks)
		if (s->n == 1) {
			cairo_set_line_width(cr, W_AT(s->pt[0], s->pt[0]));
			cairo_move_to(cr, ox + dx + s->pt[0].x * k, oy + dy + s->pt[0].y * k);
			cairo_line_to(cr, ox + dx + s->pt[0].x * k + 0.01, oy + dy + s->pt[0].y * k);
			cairo_stroke(cr);
			continue;
		}
		/* the width follows the pressure along the stroke, as the panel's preview draws it:
		 * runs of segments of about one width */
		for (int j = 1; j < s->n;) {
			double w0 = W_AT(s->pt[j - 1], s->pt[j]);
			cairo_set_line_width(cr, w0);
			cairo_move_to(cr, ox + dx + s->pt[j - 1].x * k, oy + dy + s->pt[j - 1].y * k);
			do {
				cairo_line_to(cr, ox + dx + s->pt[j].x * k, oy + dy + s->pt[j].y * k);
				j++;
			} while (j < s->n && fabs(W_AT(s->pt[j - 1], s->pt[j]) - w0) < 0.12);
			cairo_stroke(cr);
		}
#undef W_AT
	}
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	for (int i = 0; i < f->nw; i++) {
		const struct pl_word *w = &f->w[i];
		if (!w->set || !w->npath || (w->nks && !ag))
			continue; /* a read word shown set only while Set in Autograph is on */
		cairo_new_path(cr);
		double pdx = 0, pdy = 0; /* rewrapped: moved as the word's first stroke */
		for (int j = 0; pl && w->nks && j < f->ns && j < pl->n; j++)
			if (f->s[j].k == w->ks[0]) {
				pdx = pl->dx[j] * k, pdy = pl->dy[j] * k;
				break;
			}
		/* ops as GHW_PATH_*: 0 move, 1 line, 2 quad, 3 cubic, 4 close, each followed by its
		 * coordinates (region coordinates) */
		for (int j = 0; j < w->npath;) {
			int op = (int)lroundf(w->path[j++]);
#define PX(n) (ox + pdx + w->path[j + (n)] * k)
#define PY(n) (oy + pdy + w->path[j + (n)] * k)
			if (op == 0 && j + 2 <= w->npath) {
				cairo_move_to(cr, PX(0), PY(1));
				j += 2;
			} else if (op == 1 && j + 2 <= w->npath) {
				cairo_line_to(cr, PX(0), PY(1));
				j += 2;
			} else if (op == 2 && j + 4 <= w->npath) {
				double x0, y0;
				if (!cairo_has_current_point(cr))
					cairo_move_to(cr, PX(0), PY(1));
				cairo_get_current_point(cr, &x0, &y0);
				double cx = PX(0), cy = PY(1), x1 = PX(2), y1 = PY(3);
				cairo_curve_to(cr, x0 + 2.0 / 3 * (cx - x0), y0 + 2.0 / 3 * (cy - y0), x1 + 2.0 / 3 * (cx - x1),
					       y1 + 2.0 / 3 * (cy - y1), x1, y1);
				j += 4;
			} else if (op == 3 && j + 6 <= w->npath) {
				cairo_curve_to(cr, PX(0), PY(1), PX(2), PY(3), PX(4), PY(5));
				j += 6;
			} else if (op == 4)
				cairo_close_path(cr);
			else
				break;
#undef PX
#undef PY
		}
		cairo_fill(cr);
	}
	}

/* Painted ink, kept: a field's ink as coverage (A8) in device pixels, for the field's version
 * (field.gen), its scale, the display choices and the part of a device pixel it starts at.
 * Painting it again is one mask instead of every stroke (a day's ink ~50 ms on the device).
 * The mask gives the frame the strokes give, to rounding. */
#define INK_CACHE 64
#define INK_CACHE_BYTES (24 << 20)
static struct inkc {
	uint32_t gen;
	float k, fx, fy, sc;
	bool neat, ag;
	cairo_surface_t *m;
	int x, y; /* the mask's top left, against the field's origin in whole device pixels */
	size_t bytes;
	unsigned long used;
} inkc[INK_CACHE];
static unsigned long inkc_clock;
static bool inkc_off;

void view_ink_cache(bool on)
{
	inkc_off = !on;
	for (int i = 0; i < INK_CACHE; i++) {
		if (inkc[i].m)
			cairo_surface_destroy(inkc[i].m);
		inkc[i] = (struct inkc){ 0 };
	}
}

static struct inkc *inkc_get(cairo_t *cr, const struct field *f, double fx, double fy, double sc, double k, bool neat,
			     bool ag)
{
	struct inkc *e = NULL;
	for (int i = 0; i < INK_CACHE && !e; i++)
		if (inkc[i].m && inkc[i].gen == f->gen && inkc[i].k == (float)k && inkc[i].fx == (float)fx &&
		    inkc[i].fy == (float)fy && inkc[i].sc == (float)sc && inkc[i].neat == neat && inkc[i].ag == ag)
			e = &inkc[i];
	if (e) {
		e->used = ++inkc_clock;
		return e;
	}
	/* what the strokes and set words cover, in field units, then in device pixels with a
	 * stroke's half width around it */
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
#define BOX(x, y) (x0 = fmin(x0, x), y0 = fmin(y0, y), x1 = fmax(x1, x), y1 = fmax(y1, y))
	for (int i = 0; i < f->ns; i++) {
		const struct pl_stroke *raw = &f->s[i], *s = raw;
		if (neat && field_neat(f, raw->k) && field_neat(f, raw->k)->n)
			s = field_neat(f, raw->k);
		int wi = field_word_of(f, raw->k);
		if (wi >= 0 && ag && f->w[wi].set && f->w[wi].npath)
			continue;
		double dx = wi >= 0 ? f->w[wi].dx : 0, dy = wi >= 0 ? f->w[wi].dy : 0;
		for (int j = 0; j < s->n; j++)
			BOX(s->pt[j].x + dx, s->pt[j].y + dy);
	}
	for (int i = 0; i < f->nw; i++) {
		const struct pl_word *w = &f->w[i];
		if (!w->set || !w->npath || (w->nks && !ag))
			continue;
		for (int j = 0; j < w->npath;) { /* the points and control points: curves stay inside them */
			int op = (int)lroundf(w->path[j++]), np = op == 0 || op == 1 ? 2 : op == 2 ? 4 : op == 3 ? 6 : 0;
			if (op == 4)
				continue;
			if (!np || j + np > w->npath)
				break;
			for (int q = 0; q < np; q += 2)
				BOX(w->path[j + q], w->path[j + q + 1]);
			j += np;
		}
	}
#undef BOX
	if (x1 < x0)
		return NULL;
	double pad = (PL_INK_WMAX / 2 + 1) * sc + 2, ks = k * sc;
	int ix = (int)floor(fx + x0 * ks - pad), iy = (int)floor(fy + y0 * ks - pad);
	int iw = (int)ceil(fx + x1 * ks + pad) - ix, ih = (int)ceil(fy + y1 * ks + pad) - iy;
	if (iw <= 0 || ih <= 0 || (size_t)iw * ih > INK_CACHE_BYTES / 4)
		return NULL;
	cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_A8, iw, ih);
	cairo_t *mc = cairo_create(m);
	cairo_set_antialias(mc, cairo_get_antialias(cr));
	cairo_translate(mc, fx - ix, fy - iy);
	cairo_scale(mc, sc, sc);
	cairo_set_source_rgba(mc, 0, 0, 0, 1);
	ink_paths(mc, f, 0, 0, k, neat, ag, NULL);
	cairo_destroy(mc);
	cairo_surface_flush(m);
	size_t bytes = (size_t)cairo_image_surface_get_stride(m) * ih, total = bytes;
	for (int i = 0; i < INK_CACHE; i++)
		total += inkc[i].bytes;
	/* the least recently painted go, for a free slot and the memory */
	for (;;) {
		struct inkc *lru = NULL, *free_ = NULL;
		for (int i = 0; i < INK_CACHE; i++) {
			if (!inkc[i].m)
				free_ = free_ ? free_ : &inkc[i];
			else if (!lru || inkc[i].used < lru->used)
				lru = &inkc[i];
		}
		if (free_ && total <= INK_CACHE_BYTES) {
			e = free_;
			break;
		}
		if (!lru)
			break;
		total -= lru->bytes;
		cairo_surface_destroy(lru->m);
		*lru = (struct inkc){ 0 };
	}
	if (!e) {
		cairo_surface_destroy(m);
		return NULL;
	}
	*e = (struct inkc){ f->gen, (float)k, (float)fx, (float)fy, (float)sc, neat, ag, m, ix, iy, bytes, ++inkc_clock };
	return e;
}

static void ink_(struct ctx *c, const struct field *f, double ox, double oy, double k)
{
	if (!c->paint || (!f->ns && !f->nw))
		return;
	cairo_t *cr = c->cr;
	{ /* nothing to draw outside what is being repainted */
		double r, b, cx0, cy0, cx1, cy1;
		field_extent(f, &r, &b);
		cairo_clip_extents(cr, &cx0, &cy0, &cx1, &cy1);
		if (ox - 8 > cx1 || oy - 8 > cy1 || ox + r * k + 8 < cx0 || oy + b * k + 8 < cy0)
			return;
	}
	bool neat = field_neaten(c->s, f), ag = field_autograph(c->s, f);
	cairo_matrix_t mx;
	cairo_get_matrix(cr, &mx);
	if (!inkc_off && f->gen && mx.xy == 0 && mx.yx == 0 && mx.xx > 0 && mx.xx == mx.yy) {
		/* where the field starts in device pixels: whole pixels, and the part of one */
		double dx = mx.xx * ox + mx.x0, dy = mx.yy * oy + mx.y0;
		double bx = floor(dx), by = floor(dy);
		double fx = round((dx - bx) * 64) / 64, fy = round((dy - by) * 64) / 64;
		struct inkc *e = inkc_get(cr, f, fx, fy, mx.xx, k, neat, ag);
		if (e) {
			cairo_save(cr);
			cairo_identity_matrix(cr);
			col(cr, c->L->pen);
			cairo_mask_surface(cr, e->m, bx + e->x, by + e->y);
			cairo_restore(cr);
			return;
		}
	}
	cairo_save(cr);
	col(cr, c->L->pen);
	ink_paths(cr, f, ox, oy, k, neat, ag, NULL);
	cairo_restore(cr);
}

/* the top of a field's ink, as laid out */
static double ink_top(const struct field *f)
{
	double top = 1e9;
	for (int i = 0; i < f->ns; i++) {
		int wi = field_word_of(f, f->s[i].k);
		double dy = wi >= 0 ? f->w[wi].dy : 0;
		for (int j = 0; j < f->s[i].n; j++)
			top = fmin(top, f->s[i].pt[j].y + dy);
	}
	for (int i = 0; i < f->nw; i++)
		if (f->w[i].set)
			top = fmin(top, f->w[i].box[1]);
	return top;
}

/* a field's ink in a place smaller than where it was written: fitted to the line height h */
static void ink_fit(struct ctx *c, const struct field *f, double x, double y, double w, double h)
{
	double r, b;
	field_extent(f, &r, &b);
	double top = ink_top(f);
	if ((!f->ns && !f->nw) || b <= top)
		return;
	double k = fmin(1, h / fmax(b - top, INK_SIZE));
	cairo_save(c->cr);
	cairo_rectangle(c->cr, x, y, w, h);
	cairo_clip(c->cr);
	ink(c, f, x, y - top * k, k);
	cairo_restore(c->cr);
}

static void box(struct ctx *c, double x, double y, double w, double h, double r, double lw, ui_col edge, ui_col bg)
{
	if (!c->paint)
		return;
	if (bg) {
		lk_rect(c->cr, x, y, w, h, r);
		lk_fill(c->cr, bg);
	}
	if (lw > 0)
		lk_stroke_box(c->cr, x, y, w, h, r, lw, edge);
}

static void rule(struct ctx *c, double x, double y, double w)
{
	if (c->paint)
		lk_rule(c->cr, x, y, w, c->L->row_w, c->L->row_c, c->L->row_dash);
}

static void icon(struct ctx *c, const char *d, double cx, double cy, double px, ui_col cl)
{
	if (c->paint)
		lk_icon(c->cr, d, cx - px / 2, cy - px / 2, px, cl, 1.6);
}

static void hm(char *o, size_t n, int min)
{
	snprintf(o, n, "%02d:%02d", (min / 60) % 24, min % 60);
}

/* ---- what the template shows, and what it hides (FR-028) ---- */

static void shown(const struct tnode *n, enum pkind view, unsigned *mask, char slots[][32], int *ns)
{
	if (n->k != N_WIDGET) {
		for (int i = 0; i < n->nc; i++)
			shown(n->c[i], view, mask, slots, ns);
		return;
	}
	int per = n->period < 0 ? (int)view : n->period;
	switch (n->wg) {
	case W_AGENDA:
	case W_CAGENDA:
	case W_WEEKOV:
		*mask |= 1u << E_EVENT;
		break;
	case W_MINIMONTH:
		if (n->large)
			*mask |= 1u << E_EVENT;
		break;
	case W_TODO:
		if (per == (int)view)
			*mask |= 1u << E_TODO;
		break;
	case W_PRIO:
		if (per == (int)view)
			*mask |= 1u << E_PRIO;
		break;
	case W_HABITS:
		*mask |= 1u << E_MARK;
		break;
	case W_NOTES:
		if (per == (int)view && *ns < 8)
			snprintf(slots[(*ns)++], 32, "%s", n->slot ? n->slot : "notes");
		break;
	default:
		break;
	}
}

static const char *slot_label(const char *slot, int n, char *o, size_t len)
{
	if (!strcmp(slot, "notes"))
		snprintf(o, len, "%d %s", n, n == 1 ? "note" : "notes");
	else
		snprintf(o, len, "%d %s %s", n, slot, n == 1 ? "note" : "notes");
	return o;
}

int view_hidden(struct store *s, const struct tmpl *t, struct period per, struct hidden_group *out, int max)
{
	unsigned mask = 0;
	char slots[8][32];
	int ns = 0, n = 0;
	shown(t->root, per.kind, &mask, slots, &ns);
	struct entry *e[MAXE];
	if (!(mask & 1u << E_EVENT)) {
		int k = 0;
		for (int d = per.start; d <= period_end(per); d++) {
			int m = store_events(s, d, e, MAXE);
			for (int i = 0; i < m; i++)
				if (e[i]->per.start == d || d == per.start)
					k++;
		}
		if (k && n < max) {
			out[n] = (struct hidden_group){ E_EVENT, "", k, "" };
			snprintf(out[n].label, sizeof out[n].label, "%d %s", k, k == 1 ? "event" : "events");
			n++;
		}
	}
	if (!(mask & 1u << E_TODO)) {
		int k = store_list(s, E_TODO, per, NULL, e, MAXE);
		if (k && n < max) {
			out[n] = (struct hidden_group){ E_TODO, "", k, "" };
			snprintf(out[n].label, sizeof out[n].label, "%d %s", k, k == 1 ? "to-do" : "to-dos");
			n++;
		}
	}
	/* notes, per slot */
	int k = store_list(s, E_NOTE, per, NULL, e, MAXE);
	for (int i = 0; i < k; i++) {
		bool sh = false;
		for (int j = 0; j < ns; j++)
			if (!strcmp(slots[j], e[i]->slot))
				sh = true;
		if (sh || field_empty(&e[i]->f))
			continue;
		bool dup = false;
		for (int j = 0; j < n; j++)
			if (out[j].kind == E_NOTE && !strcmp(out[j].slot, e[i]->slot)) {
				out[j].n++;
				slot_label(out[j].slot, out[j].n, out[j].label, sizeof out[j].label);
				dup = true;
			}
		if (!dup && n < max) {
			out[n] = (struct hidden_group){ E_NOTE, "", 1, "" };
			snprintf(out[n].slot, sizeof out[n].slot, "%s", e[i]->slot);
			slot_label(e[i]->slot, 1, out[n].label, sizeof out[n].label);
			n++;
		}
	}
	if (!(mask & 1u << E_PRIO)) {
		int m = store_list(s, E_PRIO, per, NULL, e, MAXE);
		if (m && n < max) {
			out[n] = (struct hidden_group){ E_PRIO, "", m, "" };
			snprintf(out[n].label, sizeof out[n].label, "%d %s", m, m == 1 ? "priority" : "priorities");
			n++;
		}
	}
	if (!(mask & 1u << E_MARK)) {
		int m = store_list(s, E_MARK, per, NULL, e, MAXE);
		if (m && n < max) {
			out[n] = (struct hidden_group){ E_MARK, "", m, "" };
			snprintf(out[n].label, sizeof out[n].label, "%d %s", m, m == 1 ? "habit mark" : "habit marks");
			n++;
		}
	}
	return n;
}

static int count_hidden(struct store *s, const struct tmpl *t, struct period per)
{
	struct hidden_group g[16];
	int n = view_hidden(s, t, per, g, 16), k = 0;
	for (int i = 0; i < n; i++)
		k += g[i].n;
	return k;
}

/* ---- the header bar (planner.css .pl-bar) ---- */

static void title_of(const struct view *v, char *main, size_t nm, char *sub, size_t ns)
{
	int y, m, d;
	day_to_ymd(v->per.start, &y, &m, &d);
	if (v->per.kind == P_DAY) {
		snprintf(main, nm, "%s %d", DOW[day_weekday(v->per.start)], d);
		snprintf(sub, ns, "%s · week %d", MON[m - 1], week_number(v->per.start));
	} else if (v->per.kind == P_WEEK) {
		int e = period_end(v->per), y2, m2, d2;
		day_to_ymd(e, &y2, &m2, &d2);
		snprintf(main, nm, "Week %d", week_number(v->per.start));
		if (m2 == m)
			snprintf(sub, ns, "%d–%d %s", d, d2, MON[m - 1]);
		else
			snprintf(sub, ns, "%d %s – %d %s", d, MON[m - 1], d2, MON[m2 - 1]);
	} else {
		snprintf(main, nm, "%s", MON[m - 1]);
		int w = (day_weekday(v->per.start) + days_in_month(y, m) + 6) / 7;
		snprintf(sub, ns, "%d weeks", w);
	}
	if (y != (v->today >= 0 ? 0 : 0)) {
		int ty, tm, td;
		day_to_ymd(v->today, &ty, &tm, &td);
		if (ty != y) {
			size_t l = strlen(sub);
			snprintf(sub + l, ns - l, " %d", y);
		}
	}
}

static void bar(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, BAR);
		lk_fill(c->cr, L->bar_bg);
		if (L->bar_rule == 3) { /* 3px double */
			lk_rule(c->cr, 0, BAR - 3, W, 1, L->ink, 0);
			lk_rule(c->cr, 0, BAR - 1, W, 1, L->ink, 0);
		} else if (L->bar_rule > 0)
			lk_rule(c->cr, 0, BAR - L->bar_rule, W, L->bar_rule, L->ink, 0);
		else /* stationery: inset 0 -6px 0 -4.5px gilt, a 1.5px gilt line at the foot */
			lk_rule(c->cr, 0, BAR - 1.5, W, 1.5, L->gilt, 0);
	}
	ui_col fg = L->bar_fg, bg = L->bar_bg;
	double cy = BAR / 2.0;
	/* from the right: the (i) */
	double x = W - 12;
	bool badge = v->hidden_count > 0;
	double toolw = badge ? 48 : 40;
	x -= toolw;
	double ix = x, open = v->ov == OV_INFO || v->ov == OV_MENU;
	if (open)
		box(c, ix, cy - 20, toolw, 40, L->ui_r, L->edge, fg, 0);
	icon(c, IC_INFO, badge ? ix + 7 + 11 : ix + 20, cy, 22, fg);
	if (badge) {
		char b[12];
		snprintf(b, sizeof b, "%d", v->hidden_count);
		UiFont bf = fam(L->mono, 600, 0, 10.5f);
		double bw = fmax(17, tw(c, &bf, b) + 8), bx = ix + 25, by = cy - 20 + 4;
		if (c->paint) {
			lk_rect(c->cr, bx - 2, by - 2, bw + 4, 21, 10.5);
			lk_fill(c->cr, bg);
			lk_rect(c->cr, bx, by, bw, 17, 8.5);
			lk_fill(c->cr, fg);
		}
		tc(c, &bf, b, bx + bw / 2, by + 8.5, 1, 0, bg);
	}
	hit(c, ix, cy - 20, toolw, 40, A_INFO, 0, 0);
	x -= 2 + 12;
	/* previous, today, next */
	x -= 40;
	box(c, x, cy - 20, 40, 40, L->ui_r, L->edge, fg, 0);
	icon(c, IC_CHEV_R, x + 20, cy, 20, fg);
	hit(c, x, cy - 20, 40, 40, A_NEXT, 0, 0);
	UiFont lf = L->label;
	double tdw = fmax(40, tw(c, &lf, "Today") + 20);
	x -= 6 + tdw;
	bool here = v->today >= v->per.start && v->today <= period_end(v->per);
	if (!here)
		box(c, x, cy - 20, tdw, 40, L->ui_r, L->edge, fg, 0);
	if (c->paint && here)
		cairo_push_group(c->cr);
	tc(c, &lf, "Today", x + tdw / 2, cy, 1, 0, fg);
	if (c->paint && here) {
		cairo_pop_group_to_source(c->cr);
		cairo_paint_with_alpha(c->cr, 0.75);
	}
	hit(c, x, cy - 20, tdw, 40, A_TODAY, 0, 0);
	x -= 6 + 40;
	box(c, x, cy - 20, 40, 40, L->ui_r, L->edge, fg, 0);
	icon(c, IC_CHEV_L, x + 20, cy, 20, fg);
	hit(c, x, cy - 20, 40, 40, A_PREV, 0, 0);
	/* Month, Week, Day */
	static const char *seg[] = { "Month", "Week", "Day" };
	static const int segk[] = { P_MONTH, P_WEEK, P_DAY };
	double sw[3], st = 0;
	for (int i = 0; i < 3; i++) {
		sw[i] = fmax(58, tw(c, &lf, seg[i]) + 20);
		st += sw[i];
	}
	x -= 12 + st;
	double sx = x;
	if (c->paint) {
		cairo_save(c->cr);
		lk_rect(c->cr, sx, cy - 20, st, 40, L->ui_r);
		cairo_clip(c->cr);
	}
	for (int i = 0; i < 3; i++) {
		bool on = (int)v->per.kind == segk[i];
		if (c->paint && on) {
			cairo_rectangle(c->cr, x, cy - 20, sw[i], 40);
			lk_fill(c->cr, fg);
		}
		if (c->paint && i) {
			cairo_rectangle(c->cr, x, cy - 20, L->voice == V_EDITORIAL ? 1 : 1.5, 40);
			lk_fill(c->cr, fg);
		}
		tc(c, &lf, seg[i], x + sw[i] / 2, cy, 1, 0, on ? bg : fg);
		hit(c, x, cy - 20, sw[i], 40, A_VIEW, segk[i], 0);
		x += sw[i];
	}
	if (c->paint) {
		cairo_restore(c->cr);
		lk_stroke_box(c->cr, sx, cy - 20, st, 40, L->ui_r, L->voice == V_EDITORIAL ? 1 : 1.5, fg);
	}
	/* the period, as much as fits */
	char main[64], sub[96];
	title_of(v, main, sizeof main, sub, sizeof sub);
	double maxw = sx - 12 - 16;
	double h1, h2;
	UiFont f1 = L->h1, f2 = L->sub;
	ui_text_w(&c->g, &f1, main, &h1);
	ui_text_w(&c->g, &f2, sub, &h2);
	double top = cy - (h1 + 1 + h2) / 2;
	if (c->paint) {
		ui_text(&c->g, &f1, main, 16, top, 0, maxw, fg, &h1);
		cairo_push_group(c->cr);
		ui_text(&c->g, &f2, sub, 16, top + h1 + 1, 0, maxw, fg, &h2);
		cairo_pop_group_to_source(c->cr);
		cairo_paint_with_alpha(c->cr, 0.85);
	}
}

/* ---- widgets ---- */

struct wbox {
	double x, y, w, h;
	const struct tnode *n;
	struct period per; /* the period its data belong to */
};

static struct period wper(const struct view *v, const struct tnode *n)
{
	enum pkind k = n->period < 0 ? v->per.kind : (enum pkind)n->period;
	return period_of(k, v->per.start);
}

static void head(struct ctx *c, struct wbox *b, const char *label, const char *tag, double hh)
{
	const struct look *L = c->L;
	double w = tc(c, &L->wlabel, label, b->x + 10, b->y + hh / 2, 0, b->w - 20, L->ink);
	if (tag && *tag)
		tc(c, &L->tag, tag, b->x + 10 + w + 8, b->y + hh / 2, 0, b->w - w - 28, L->soft);
	rule(c, b->x, b->y + hh - L->row_w, b->w);
}

static void card(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	box(c, b->x, b->y, b->w, b->h, L->wg_r, L->edge, L->wg_edge, L->paper);
}

static void card_edge(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	if (c->paint)
		lk_stroke_box(c->cr, b->x, b->y, b->w, b->h, L->wg_r, L->edge, L->wg_edge);
}

static const char *of_the(enum pkind k)
{
	return k == P_DAY ? "of the day" : k == P_WEEK ? "of the week" : "of the month";
}

/* rows that fit (FR-048): the empty row stays, done rows give way first, then the oldest open */
static int fit_rows(struct entry **e, int n, int cap, bool *drop)
{
	memset(drop, 0, n * sizeof *drop);
	if (n + 1 <= cap)
		return 0;
	int keep = cap - 1 > 0 ? cap - 1 : 0, gone = n - keep;
	for (int pass = 0; pass < 2 && gone > 0; pass++) {
		/* pass 0: done ones, oldest first; pass 1: open ones, oldest first */
		for (;;) {
			int best = -1;
			for (int i = 0; i < n; i++)
				if (!drop[i] && (pass == 1 || e[i]->done) && (best < 0 || e[i]->age < e[best]->age))
					best = i;
			if (best < 0 || gone == 0)
				break;
			drop[best] = true;
			gone--;
		}
	}
	return n - keep;
}

static void w_list(struct ctx *c, struct wbox *b, bool todo)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	char tag[32];
	snprintf(tag, sizeof tag, "%s", of_the(b->per.kind));
	card(c, b);
	head(c, b, b->n->title ? b->n->title : todo ? "To-do list" : "Priorities", tag, PL_HEAD);
	struct entry *e[MAXE];
	int n = store_list(c->s, todo ? E_TODO : E_PRIO, b->per, NULL, e, MAXE);
	int cap = (int)floor((b->h - PL_HEAD - PL_PAD + 3) / PL_ROW);
	if (cap < 1)
		cap = 1;
	bool drop[MAXE];
	int more = fit_rows(e, n, cap, drop);
	if (more) {
		int capm = (int)floor((b->h - PL_HEAD - PL_MORE + 3) / PL_ROW);
		more = fit_rows(e, n, capm < 1 ? 1 : capm, drop);
	}
	double y = b->y + PL_HEAD;
	double fx = todo ? 10 + 17 + 8 : 10 + 16 + 8;
	for (int i = 0; i <= n; i++) {
		if (i < n && drop[i])
			continue;
		bool empty = i == n;
		struct entry *x = empty ? NULL : e[i];
		double by = y + PL_ROW - 5;
		rule(c, b->x, y + PL_ROW - L->row_w, b->w);
		if (todo) {
			double tx = b->x + 10, ty = by - 2 - 17;
			double r = L->tick_shape == 0 ? 3 : L->tick_shape == 1 ? 0 : 8.5;
			box(c, tx, ty, 17, 17, r, 1.5, empty ? L->soft : L->ink, 0);
			if (x && x->done && c->paint) {
				cairo_t *cr = c->cr;
				cairo_save(cr);
				col(cr, L->ink);
				cairo_set_line_width(cr, 2);
				cairo_move_to(cr, tx + 4, ty + 9);
				cairo_line_to(cr, tx + 7.5, ty + 12.5);
				cairo_line_to(cr, tx + 13, ty + 4.5);
				cairo_stroke(cr);
				cairo_restore(cr);
			}
			if (x)
				hit(c, b->x, y, fx, PL_ROW, A_TICK, 0, x->id);
		} else {
			char num[8];
			snprintf(num, sizeof num, "%d", i + 1);
			UiFont nf = empty ? fw(L->num, 400) : L->num;
			tb(c, &nf, num, b->x + 10, by - 3 + 3, 0, 0, empty ? L->soft : L->ink);
		}
		double ax = b->x + fx, aw = b->w - fx - 8;
		if (x) {
			entry_field(c, x, ax, y, aw, PL_ROW, PL_ROW, PL_ROW - 7);
			if (c->paint) {
				cairo_save(c->cr);
				cairo_rectangle(c->cr, ax, y, aw, PL_ROW);
				cairo_clip(c->cr);
				ink(c, &x->f, ax, y, 1);
				if (x->done) { /* struck through, as the board: ink with a line */
					double r, bt;
					field_extent(&x->f, &r, &bt);
					if (r > 0)
						lk_rule(c->cr, ax, y + PL_ROW - 14, r, 1.5, L->ink, 0);
				}
				cairo_restore(c->cr);
			}
		} else {
			int rank = n ? e[n - 1]->rank + 1 : 1;
			new_field(c, todo ? E_TODO : E_PRIO, b->per, NULL, rank, todo ? "todo" : "prio", ax, y, aw, PL_ROW, PL_ROW,
				  PL_ROW - 7);
		}
		y += PL_ROW;
	}
	if (more) {
		char m[24];
		snprintf(m, sizeof m, "+%d more", more);
		tc(c, &L->more, m, b->x + 10, y + 6 + 6, 0, 0, L->soft);
		hit(c, b->x, y, b->w, PL_MORE, A_MORE, todo ? E_TODO : E_PRIO, 0);
	}
	(void)v;
}

static void w_notes(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	const char *slot = b->n->slot ? b->n->slot : "notes";
	char tag[48];
	snprintf(tag, sizeof tag, "slot · %s", slot);
	card(c, b);
	head(c, b, b->n->title ? b->n->title : "Notes", tag, PL_HEAD);
	struct entry *e[8];
	int n = store_list(c->s, E_NOTE, b->per, slot, e, 8);
	double top = b->y + PL_HEAD;
	int lines = (int)floor((b->h - PL_HEAD - PL_PAD) / PL_ROW);
	if (lines < 1)
		lines = 1;
	for (int i = 0; i < lines; i++) {
		double gy = top + (i + 1) * PL_ROW - 6;
		if (c->paint) {
			if (L->voice == V_WHITE)
				lk_rule(c->cr, b->x + 10, gy, b->w - 20, 1.5, L->rule, 2);
			else
				lk_rule(c->cr, b->x + 10, gy, b->w - 20, L->row_w, L->row_c, L->row_dash);
		}
	}
	double fx = b->x + 12, fw_ = b->w - 22, fh = b->h - PL_HEAD - 4;
	if (n) {
		entry_field(c, e[0], fx, top, fw_, fh, PL_ROW, PL_ROW - 6);
		if (c->paint) {
			cairo_save(c->cr);
			cairo_rectangle(c->cr, b->x + 1, top, b->w - 2, b->h - PL_HEAD - 1);
			cairo_clip(c->cr);
			ink(c, &e[0]->f, fx, top, 1);
			cairo_restore(c->cr);
		}
	} else
		new_field(c, E_NOTE, b->per, slot, 0, "note", fx, top, fw_, fh, PL_ROW, PL_ROW - 6);
}

static void w_habits(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	card(c, b);
	head(c, b, b->n->title ? b->n->title : "Habits", of_the(b->per.kind), PL_HEAD);
	int days[31], nd = 0;
	if (b->per.kind == P_DAY)
		days[nd++] = b->per.start;
	else {
		int last = period_end(b->per), fit = b->per.kind == P_MONTH ? (int)floor((b->w - 120) / 15) : 7;
		for (int d = b->per.start; d <= last && nd < fit && nd < 31; d++)
			days[nd++] = d;
	}
	double cw = b->per.kind == P_MONTH ? 15 : b->per.kind == P_DAY ? 40 : 26;
	double cx0 = b->x + b->w - 8 - cw * nd;
	double y = b->y + PL_HEAD;
	for (int i = 0; i < nd; i++) {
		char t[12];
		int yy, m, d;
		day_to_ymd(days[i], &yy, &m, &d);
		if (b->per.kind == P_MONTH)
			snprintf(t, sizeof t, "%s", d % 5 == 1 ? (snprintf(t, sizeof t, "%d", d), t) : "");
		else
			snprintf(t, sizeof t, "%s", b->per.kind == P_DAY ? "today" : DOW2[day_weekday(days[i])]);
		UiFont hf = days[i] == v->today ? fw(L->small, 700) : L->small;
		tc(c, &hf, t, cx0 + cw * i + cw / 2, y + 11, 1, 0, days[i] == v->today ? L->ink : L->soft);
	}
	y += 22;
	struct entry *h[32], *mk[MAXE];
	int nh = store_list(c->s, E_HABIT, b->per, NULL, h, 32);
	int nm = store_list(c->s, E_MARK, b->per, NULL, mk, MAXE);
	int cap = (int)floor((b->h - PL_HEAD - 22 - PL_PAD + 3) / 30);
	int shown_ = nh + 1 <= cap ? nh : (cap - 2 > 0 ? cap - 2 : 0);
	for (int i = 0; i <= shown_; i++) {
		bool add = i == shown_;
		if (i)
			rule(c, b->x, y, b->w);
		double nx = b->x + 10, nw = cx0 - nx - 4;
		if (add) {
			struct entry *any[1];
			(void)any;
			tc(c, &(UiFont){ L->mono, 400, 0, 12, 0, 0, UI_CASE_NONE }, "+ a habit", nx, y + 15, 0, 0, L->soft);
			new_field(c, E_HABIT, b->per, NULL, nh + 1, "habit", nx, y, b->w - 20, 30, 30, 23);
		} else {
			entry_field(c, h[i], nx, y, nw, 30, 30, 23);
			if (c->paint) {
				cairo_save(c->cr);
				cairo_rectangle(c->cr, nx, y, nw, 30);
				cairo_clip(c->cr);
				ink(c, &h[i]->f, nx, y, 1);
				cairo_restore(c->cr);
			}
			for (int j = 0; j < nd; j++) {
				bool on = false;
				for (int k = 0; k < nm; k++)
					if (mk[k]->habit == h[i]->id && mk[k]->per.start == days[j])
						on = true;
				double ccx = cx0 + cw * j + cw / 2, ccy = y + 15;
				bool ahead = days[j] > v->today;
				double r = L->voice == V_EDITORIAL ? 0 : 6.5;
				if (on)
					box(c, ccx - 6.5, ccy - 6.5, 13, 13, r, 0, 0, L->ink);
				else
					box(c, ccx - 6.5, ccy - 6.5, 13, 13, r, ahead ? 1 : 1.5, ahead ? L->soft : L->ink, 0);
				hit(c, ccx - cw / 2, y, cw, 30, A_HABIT, days[j], h[i]->id);
			}
		}
		y += 30;
	}
	if (nh > shown_) {
		char m[32];
		snprintf(m, sizeof m, "+%d more habits", nh - shown_);
		tc(c, &L->more, m, b->x + 10, y + 12, 0, 0, L->soft);
	}
}

static bool day_has(struct store *s, int day)
{
	struct entry *e[4];
	return store_events(s, day, e, 4) > 0;
}

static void w_minimonth(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	int y, m, d;
	day_to_ymd(v->per.start, &y, &m, &d);
	int first = day_from_ymd(y, m, 1), nd = days_in_month(y, m), lead = day_weekday(first);
	int rows = (lead + nd + 6) / 7;
	bool large = b->n->large;
	card(c, b);
	head(c, b, MON[m - 1], "", PL_HEAD);
	double gx = b->x + 8, gw = b->w - 16, cw = gw / 7;
	double top = b->y + PL_HEAD;
	for (int i = 0; i < 7; i++)
		tc(c, &L->small, DOW2[i], gx + cw * i + cw / 2, top + 11, 1, 0, L->soft);
	double ry = top + 22, rh = (b->y + b->h - 6 - ry) / rows;
	struct period wk = period_of(P_WEEK, v->per.start);
	for (int i = 0; i < nd; i++) {
		int day = first + i, cell = lead + i;
		double x = gx + cw * (cell % 7), yy = ry + rh * (cell / 7);
		bool today = day == v->today;
		/* the period on screen is shaded: the week in a week view, the day in a day view */
		if ((v->per.kind == P_WEEK && day >= wk.start && day <= wk.start + 6) ||
		    (v->per.kind == P_DAY && day == v->per.start))
			box(c, x, yy, cw, rh, 0, 0, 0, L->highlight);
		char t[4];
		snprintf(t, sizeof t, "%d", i + 1);
		UiFont df = fam(L->text, today ? 700 : 500, 0, 13);
		if (!large) {
			if (today)
				box(c, x + cw / 2 - 12.5, yy + rh / 2 - 12.5, 25, 25, 12.5, 1.5, L->ink, 0);
			tc(c, &df, t, x + cw / 2, yy + rh / 2, 1, 0, L->ink);
			if (day_has(c->s, day))
				box(c, x + cw / 2 - 2, yy + rh - 5, 4, 4, 2, 0, 0, L->ink);
		} else {
			rule(c, x, yy, cw);
			if (today) {
				box(c, x + 4, yy + 3, 22, 20, 10, 1.5, L->ink, 0);
				tc(c, &df, t, x + 4 + 11, yy + 13, 1, 0, L->ink);
			} else
				tc(c, &df, t, x + 6, yy + 13, 0, 0, L->ink);
			struct entry *e[16];
			int ne = store_events(c->s, day, e, 16);
			int cap = (int)floor((rh - 22) / 20);
			if (cap < 0)
				cap = 0;
			int shown_ = ne <= cap ? ne : (cap > 0 ? cap - 1 : 0);
			double ey = yy + 23;
			for (int k = 0; k < shown_; k++) {
				ink_fit(c, &e[k]->f, x + 4, ey, cw - 6, 18);
				ey += 20;
			}
			if (ne > shown_) {
				char mm[8];
				snprintf(mm, sizeof mm, "+%d", ne - shown_);
				tc(c, &L->small, mm, x + 4, ey + 6, 0, 0, L->soft);
			}
		}
		hit(c, x, yy, cw, rh, A_DAY, day, 0);
	}
}

static void w_weekov(struct ctx *c, struct wbox *b)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct period wk = period_of(P_WEEK, v->per.start);
	char t[24];
	snprintf(t, sizeof t, "Week %d", week_number(wk.start));
	card(c, b);
	head(c, b, t, "", PL_HEAD);
	double rh = floor((b->h - PL_HEAD - PL_PAD) / 7), y = b->y + PL_HEAD;
	for (int i = 0; i < 7; i++) {
		int day = wk.start + i, yy, m, d;
		day_to_ymd(day, &yy, &m, &d);
		rule(c, b->x, y + rh - L->row_w, b->w);
		double dx = b->x + 10;
		double w1 = tb(c, &(UiFont){ L->mono, 500, 0, 11, 0, 0, UI_CASE_NONE }, DOW3[i], dx, y + 2 + 4 + 14, 0, 0, L->soft);
		char n[4];
		snprintf(n, sizeof n, "%d", d);
		UiFont nf = fam(L->head, 600, 0, 16);
		double w2 = tb(c, &nf, n, dx + w1 + 5, y + 2 + 4 + 16, 0, 0, L->ink);
		if (day == v->today)
			box(c, dx + w1 + 5, y + 2 + 4 + 16 + 3, w2, 2, 0, 0, 0, L->ink);
		struct entry *e[16];
		int ne = store_events(c->s, day, e, 16), cap = (int)floor((rh - 4) / 22);
		if (cap < 1)
			cap = 1;
		double ey = y + 2;
		for (int k = 0; k < ne && k < cap; k++) {
			char when[16];
			if (e[k]->allday)
				snprintf(when, sizeof when, "all day");
			else
				hm(when, sizeof when, e[k]->start);
			double ex = b->x + 10 + 54 + 8;
			tb(c, &(UiFont){ L->mono, 400, 0, 11, 0, 0, UI_CASE_NONE }, when, ex, ey + 22 - 4, 0, 0, L->soft);
			ink_fit(c, &e[k]->f, ex + 48, ey + 2, b->x + b->w - 10 - ex - 48, 19);
			ey += 22;
		}
		if (ne > cap) {
			char mm[16];
			snprintf(mm, sizeof mm, "+%d more", ne - cap);
			tc(c, &L->small, mm, b->x + b->w - 10, y + 12, 2, 0, L->soft);
		}
		hit(c, b->x, y, 10 + 54, rh, A_DAY, day, 0);
		y += rh;
	}
}

/* ---- an agenda's events as bracket pairs (FR-018; boards event.stack, event.drag) ---- */

/* the box of a field's ink, in field coordinates (x1 <= x0: none) */
static void ink_box(const struct field *f, double *x0, double *y0, double *x1, double *y1)
{
	double l = 1e9, t = 1e9, r, b;
	for (int i = 0; i < f->ns; i++) {
		int wi = field_word_of(f, f->s[i].k);
		if (wi >= 0 && f->w[wi].set && f->w[wi].npath)
			continue;
		double dx = wi >= 0 ? f->w[wi].dx : 0, dy = wi >= 0 ? f->w[wi].dy : 0;
		for (int j = 0; j < f->s[i].n; j++)
			l = fmin(l, f->s[i].pt[j].x + dx), t = fmin(t, f->s[i].pt[j].y + dy);
	}
	for (int i = 0; i < f->nw; i++)
		if (f->w[i].set)
			l = fmin(l, f->w[i].box[0]), t = fmin(t, f->w[i].box[1]);
	field_extent(f, &r, &b);
	if (l > r) {
		*x0 = *y0 = *x1 = *y1 = 0;
		return;
	}
	*x0 = l, *y0 = t, *x1 = r, *y1 = b;
}

/* a line of a bracket, solid or dashed along its length (planner.css .pl-bk: 3 on 3 off, the
 * stationery's 2 on 2 off) */
static void bk_line(struct ctx *c, double x, double y, double w, double h, bool dashed, ui_col cl)
{
	if (!dashed) {
		lk_bar(c->cr, x, y, w, h, cl);
		return;
	}
	double on = c->L->voice == V_STATIONERY ? 2 : 3;
	if (h > w)
		for (double p = y; p < y + h; p += 2 * on)
			lk_bar(c->cr, x, p, w, fmin(on, y + h - p), cl);
	else
		for (double p = x; p < x + w; p += 2 * on)
			lk_bar(c->cr, p, y, fmin(on, x + w - p), h, cl);
}

/* an event's brackets: "[" at spine, "]" at spine + w, from row y to y + h, the ends turned in
 * as serifs; in the ink at whole pixels, 2 px (the stationery's 1 px); dashed while the event
 * is moved or drawn; held 1: the upper serifs thicken (its start held), 2 the lower ones */
static void brackets(struct ctx *c, double spine, double w, double y, double h, bool dashed, int held, ui_col cl)
{
	if (!c->paint)
		return;
	double lw = c->L->voice == V_STATIONERY ? 1 : 2;
	for (int side = 0; side < 2; side++) {
		double lx = side ? spine + w : spine, vx = lx - floor(lw / 2);
		double sx = side ? lx - BK_SERIF - 1 : lx - 1, sw = BK_SERIF + 2;
		double tl = held == 1 ? lw + 1 : lw, bl = held == 2 ? lw + 1 : lw;
		bk_line(c, vx, y, lw, h, dashed, cl);
		bk_line(c, sx, y, sw, tl, dashed, cl);
		bk_line(c, sx, y + h - bl, sw, bl, dashed, cl);
	}
}

/* the time readout in the hour margin while an event is drawn or dragged (.pl-readout): its
 * box's top at y */
static void readout(struct ctx *c, double x, double y, const char *t)
{
	if (!c->paint)
		return;
	const struct look *L = c->L;
	UiFont rf = fam(L->mono, 600, 0, 10);
	double th, w = ui_text_w(&c->g, &rf, t, &th);
	lk_rect(c->cr, x + 2, y, w + 6, th + 4, 3);
	lk_fill(c->cr, L->ink);
	ui_text(&c->g, &rf, t, x + 5, y + 2, 0, 0, L->paper, &th);
}

const struct vev *view_ev(const struct view *v, uint32_t id)
{
	for (int i = 0; id && i < v->nev; i++)
		if (v->ev[i].id == id)
			return &v->ev[i];
	return NULL;
}

/* a title's ink drawn from x, y at scale k, placed by pl (NULL: as written), in colour cl, inside
 * x0..x1 and y0..y1; fade: the last fade px before x1 fade out (a title cut at its "]", with no
 * "…") */
static void title_ink(struct ctx *c, const struct field *f, double x, double y, double k, const struct rw_place *pl,
		      ui_col cl, double x0, double y0, double x1, double y1, double fade)
{
	if (!c->paint || (!f->ns && !f->nw))
		return;
	cairo_t *cr = c->cr;
	cairo_save(cr);
	cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
	cairo_clip(cr);
	if (!pl && !fade && cl == c->L->pen) {
		ink(c, f, x, y, k);
		cairo_restore(cr);
		return;
	}
	bool neat = field_neaten(c->s, f), ag = field_autograph(c->s, f);
	if (fade > 0)
		cairo_push_group(cr);
	col(cr, cl);
	ink_paths(cr, f, x, y, k, neat, ag, pl);
	if (fade > 0) {
		cairo_pattern_t *g = cairo_pop_group(cr), *m = cairo_pattern_create_linear(x1 - fade, 0, x1, 0);
		cairo_pattern_add_color_stop_rgba(m, 0, 0, 0, 0, 1);
		cairo_pattern_add_color_stop_rgba(m, 1, 0, 0, 0, 0);
		cairo_set_source(cr, g);
		cairo_mask(cr, m);
		cairo_pattern_destroy(m);
		cairo_pattern_destroy(g);
	}
	cairo_restore(cr);
}

/* the fade at the end of a cut title (planner.css .pl-evt.is-cut, 30 px) and of a fold bar's
 * title (20 px) */
#define CUT_FADE 30
#define FOLD_FADE 20
/* the now line (Component.NOW): its dot's middle from the agenda's left edge, how far it keeps
 * clear of an event across and above or below, the shortest piece of it that is drawn */
#define NOW_X 26
#define NOW_CLEAR 4
#define NOW_CLEAR_Y 6
#define NOW_LEAST 16

/* The plan of a day agenda's timed events between its hours ws and we (FR-018; boards
 * event.stack, event.more): which show, their lanes, the groups drawn in rows, and the scale
 * of its hours (taller where a group is in rows). */
#define AP_EVS 64
#define AP_ROWS 16
struct agrp {
	int group;          /* the lanes' group number */
	int gs, ge;         /* its minutes, inside the hours */
	double y0, y1;      /* its span, px from the hours' top, at the new scale */
	double p;           /* each row's pitch */
	int n, shown;       /* its events, those shown (the rest folded) */
	double bar_y, bar_h; /* the fold bar (shown < n) */
	bool two;           /* the bar on two lines (a narrow widget) */
	double scale;       /* how much taller its hours are drawn */
};
struct aplan {
	struct entry *vis[AP_EVS];
	int s0[AP_EVS], e0[AP_EVS], nv;
	struct lane ln[AP_EVS];
	struct tmap tm;
	bool fixed;         /* its hours can't grow: a small widget at its set size; its rows fold */
	struct agrp g[AP_ROWS];
	int ng, folded, timed;
	double x0, x1;      /* the first lane's "[" and the last "]" */
};

static const struct agrp *ap_group(const struct aplan *a, int group)
{
	for (int i = 0; i < a->ng; i++)
		if (a->g[i].group == group)
			return &a->g[i];
	return NULL;
}

static void agenda_plan(struct ctx *c, struct wbox *b, struct entry **ev, int ne, int day, int ws, int we, double pitch,
			int *before, int *after, struct aplan *a)
{
	a->nv = a->ng = a->folded = a->timed = 0;
	for (int i = 0; i < ne && a->nv < AP_EVS; i++) {
		struct entry *x = ev[i];
		if (x->allday)
			continue;
		a->timed++;
		int s = (x->per.start - day) * 1440 + x->start, e = (x->per.start - day) * 1440 + x->end;
		if (e <= ws * 60) {
			(*before)++;
			continue;
		}
		if (s >= we * 60) {
			(*after)++;
			continue;
		}
		a->vis[a->nv] = x, a->s0[a->nv] = s, a->e0[a->nv] = e, a->nv++;
	}
	a->x0 = b->x + BK_SPINE, a->x1 = b->x + b->w - BK_RIGHT;
	int ml = c->s->min_lane > 0 ? c->s->min_lane : BK_MIN_LANE;
	lanes_assign(a->s0, a->e0, a->nv, lanes_fit(a->x0, a->x1, ml), a->ln);
	/* the groups in rows, in the order of their start */
	int gs[AP_ROWS], ge[AP_ROWS];
	double need[AP_ROWS];
	for (int i = 0; i < a->nv; i++) {
		if (!a->ln[i].rows)
			continue;
		struct agrp *g = (struct agrp *)ap_group(a, a->ln[i].group);
		if (!g) {
			if (a->ng == AP_ROWS)
				continue;
			g = &a->g[a->ng++];
			*g = (struct agrp){ .group = a->ln[i].group, .gs = 1 << 30, .ge = -1, .n = a->ln[i].count };
		}
		g->gs = g->gs < a->s0[i] ? g->gs : a->s0[i];
		g->ge = g->ge > a->e0[i] ? g->ge : a->e0[i];
	}
	for (int i = 0; i < a->ng; i++) { /* by start (the groups are apart) */
		for (int j = i + 1; j < a->ng; j++)
			if (a->g[j].gs < a->g[i].gs) {
				struct agrp t = a->g[i];
				a->g[i] = a->g[j], a->g[j] = t;
			}
	}
	for (int i = 0; i < a->ng; i++) {
		struct agrp *g = &a->g[i];
		g->gs = g->gs < ws * 60 ? ws * 60 : g->gs;
		g->ge = g->ge > we * 60 ? we * 60 : g->ge;
		gs[i] = g->gs, ge[i] = g->ge, need[i] = g->n * ROW_H + 2 * ROW_PAD;
	}
	/* a page's agenda draws the rows' hours taller; a widget at its set size keeps its hours */
	a->fixed = !b->n->grow;
	if (a->fixed)
		tmap_linear(&a->tm, ws * 60, we * 60, pitch);
	else
		tmap_stretch(&a->tm, ws * 60, we * 60, pitch, gs, ge, need, a->ng);
	double w = a->x1 - a->x0 + 2;
	for (int i = 0; i < a->ng; i++) {
		struct agrp *g = &a->g[i];
		g->y0 = round(tmap_y(&a->tm, g->gs)), g->y1 = round(tmap_y(&a->tm, g->ge));
		double was = (g->ge - g->gs) / 60.0 * pitch;
		g->scale = was > 0 ? round((g->y1 - g->y0) / was * 2) / 2 : 1;
		g->two = a->fixed && w < FOLD_TAP_W;
		g->bar_h = g->two ? FOLD_H2 : FOLD_H;
		if (a->fixed)
			rows_place(g->y0, g->y1, g->n, ROW_H_FIXED, g->bar_h, false, &g->p, &g->shown, &g->bar_y);
		else {
			/* the day never hides an event: rows as low as a widget's before any fold */
			rows_place(g->y0, g->y1, g->n, ROW_H, g->bar_h, true, &g->p, &g->shown, &g->bar_y);
			if (g->shown < g->n)
				rows_place(g->y0, g->y1, g->n, ROW_H_FIXED, g->bar_h, true, &g->p, &g->shown, &g->bar_y);
		}
		a->folded += g->n - g->shown;
	}
}

/* a rule drawn across an agenda at y, w wide from x, lw thick: kept for the checks */
static void agenda_rule(struct view *v, double x, double y, double w, double lw)
{
	if (v->nrule < V_RULES)
		v->rule[v->nrule++] = (struct vbox){ x, y, w, lw };
}

/* is y (px from the hours' top) inside a group in rows, clear of its ends */
static bool ap_inside(const struct aplan *a, double y)
{
	for (int i = 0; i < a->ng; i++)
		if (y > a->g[i].y0 + 3 && y < a->g[i].y1 - 3)
			return true;
	return false;
}

/* The timed events of a day agenda as bracket pairs (FR-018), as planned: its box b, its
 * hours' top y0; edge 1 or 2: its hours scrolled up or down while an event is held past them */
static void agenda_events(struct ctx *c, struct wbox *b, const struct aplan *a, int day, int ws, int we, double y0,
			  double pitch, int edge)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	int nv = a->nv;
	const struct lane *ln = a->ln;
	double x0 = a->x0, x1 = a->x1, ybot = y0 + tmap_y(&a->tm, we * 60);
	int focus_cl = -1;
	for (int i = 0; i < nv; i++)
		if (v->focus && a->vis[i]->id == v->focus)
			focus_cl = ln[i].group;
	double sp[AP_EVS], lw[AP_EVS], yy[AP_EVS], hh[AP_EVS];
	bool fold[AP_EVS];
	for (int i = 0; i < nv; i++) {
		const struct agrp *g = ln[i].rows ? ap_group(a, ln[i].group) : NULL;
		fold[i] = g && ln[i].order >= g->shown;
		if (g) { /* its row, in order of start */
			yy[i] = round(y0 + g->y0 + ROW_PAD + ln[i].order * g->p), hh[i] = g->p - ROW_GAP;
			lane_x(x0, x1, 0, 1, &sp[i], &lw[i]);
			continue;
		}
		double y1 = y0 + tmap_y(&a->tm, fmax(a->s0[i], ws * 60)), y2 = y0 + tmap_y(&a->tm, fmin(a->e0[i], we * 60));
		yy[i] = round(y1) + 1, hh[i] = fmax(8, round(y2 - y1) - 2);
		lane_x(x0, x1, ln[i].lane, ln[i].lanes, &sp[i], &lw[i]);
	}
	UiFont wf = L->voice == V_WHITE ? fam(L->mono, 400, 0, 10) : fam(L->text, 400, 1, 12);
	int first = (*c->nevb);
	for (int i = 0; i < nv; i++) {
		if (fold[i])
			continue;
		struct entry *x = a->vis[i];
		bool rows = ln[i].rows, lane = !rows && ln[i].lanes > 1, narrow = lane && lw[i] < BK_NARROW;
		double tx = sp[i] + BK_TITLE, tw_ = sp[i] + lw[i] - BK_SERIF - 3 - tx;
		/* the time at the right of its first line, before the "]"; in a narrow lane under the
		 * title; in a row at the right of its line */
		char when[20] = "";
		double whw = 0, th;
		{
			char w1[8], w2[8];
			hm(w1, sizeof w1, a->s0[i] < 0 ? 0 : a->s0[i]);
			hm(w2, sizeof w2, a->e0[i]);
			snprintf(when, sizeof when, "%s–%s", w1, w2);
			whw = ui_text_w(&c->g, &wf, when, &th);
		}
		double fw = narrow ? tw_ : tw_ - (whw > 0 ? whw + 6 + 4 : 0);
		double r, bt, gh = hh[i], k = 1, fh = hh[i], ty = yy[i];
		bool cut = false, grown = false;
		struct rw_place pl = { 0 };
		bool placed = false, writing = v->focus == x->id, moved = v->drag == x->id && !v->drag_end, show_when = !narrow;
		field_extent(&x->f, &r, &bt);
		double needw = r + 4, needh = bt + 4;
		if (rows) {
			/* a row: its title on one line at a row's size, cut with a fade (board event.more) */
			k = writing ? 1 : (a->fixed ? ROW_FS_FIXED : ROW_FS) / INK_SIZE;
			if (!writing && rw_place(&x->f, 0, &pl)) {
				placed = true;
				cut = pl.w * k > fw;
				ty = yy[i] + hh[i] - 5 - pl.h * k;
			} else
				cut = needw * k > fw;
		} else if (lane) {
			/* beside another event: the title's words rewrapped to its lane, smaller down to
			 * about 70 %, then cut (board event.wrap); while it is written in or moved, as written,
			 * cut at the lane; it doesn't grow into the group's free time */
			double ah = hh[i] - 6;
			bool ok = false;
			if (!writing && !moved && narrow) { /* the time under it only when the whole title fits above */
				ok = rw_fit(&x->f, fw, ah - th - 4, &pl, &k, &cut);
				if (ok && (cut || k < 1)) {
					rw_free(&pl);
					ok = false;
				} else if (ok)
					show_when = true;
			}
			if (!ok && !writing && !moved)
				ok = rw_fit(&x->f, fw, ah, &pl, &k, &cut);
			if (ok) {
				placed = true;
				ty = yy[i] + 4;
			} else
				cut = needw > fw;
		} else if (pitch < 30) {
			/* a small agenda (a widget at its smallest): the title keeps to one line beside
			 * its event */
			double limit = ybot;
			for (int j = 0; j < nv; j++)
				if (j != i && !fold[j] && a->s0[j] >= a->e0[i] && sp[j] < sp[i] + lw[i] && sp[j] + lw[j] > sp[i])
					limit = fmin(limit, y0 + tmap_y(&a->tm, a->s0[j]));
			double room = fmax(hh[i], round(limit) - yy[i] - 1);
			gh = fmin(room, fmax(hh[i], pitch));
			k = fmax(TITLE_MIN / INK_SIZE, fmin(1, gh / fmax(needh, 1)));
			cut = needw * k > fw;
			fh = gh;
		} else {
			/* the title's room (board event.title): its rows, and the free time below them
			 * down to the next event that stands under it or the agenda's end. A title that
			 * needs more grows its field into that room (a dotted lower edge); one that doesn't
			 * fit shrinks evenly to about 70 % of its written size, then is cut with a fade */
			double limit = ybot;
			for (int j = 0; j < nv; j++)
				if (j != i && !fold[j] && a->s0[j] >= a->e0[i] && sp[j] < sp[i] + lw[i] && sp[j] + lw[j] > sp[i])
					limit = fmin(limit, j < nv && ln[j].rows ? yy[j] : y0 + tmap_y(&a->tm, a->s0[j]));
			double room = fmax(hh[i], round(limit) - yy[i] - 1);
			if (needw > fw || needh > hh[i]) {
				k = fmin(1, fmin(fw / fmax(needw, 1), room / fmax(needh, 1)));
				if (k < TITLE_MIN / INK_SIZE) {
					k = TITLE_MIN / INK_SIZE;
					cut = true;
				}
				gh = fmin(room, fmax(hh[i], ceil(needh * k)));
				grown = gh > hh[i];
			}
			/* while written, the field reaches a line below its ink, into the room */
			fh = k < 1 ? gh : fmin(room, fmax(gh, bt > hh[i] - 6 ? bt + PL_ROW : hh[i]));
		}
		bool quiet = focus_cl >= 0 && ln[i].group == focus_cl && x->id != v->focus;
		brackets(c, sp[i], lw[i], yy[i], hh[i], moved, v->drag == x->id ? v->drag_end : 0, quiet ? L->rule : L->ink);
		if (grown && c->paint) /* the grown part's lower edge is dotted */
			lk_rule(c->cr, tx, yy[i] + gh - 1.5, fw, 1.5, L->ink, 2);
		double ink_h = placed ? pl.h * k : bt * k;
		if (when[0] && c->paint) {
			if (rows)
				ui_text(&c->g, &wf, when, tx + tw_ - 4, yy[i] + hh[i] - 5 - th, 2, 0, L->ink, &th);
			else if (narrow) { /* under the title, when the event has room for it */
				double wy = yy[i] + 4 + fmax(ink_h, 14) + 2;
				if (show_when && wy + th <= yy[i] + hh[i] - 2)
					ui_text(&c->g, &wf, when, tx, wy, 0, 0, L->ink, &th);
			} else
				ui_text(&c->g, &wf, when, tx + tw_ - 4, yy[i] + 4, 2, 0, L->ink, &th);
		}
		struct vfield *ef = entry_field(c, x, tx, yy[i], fw, rows || lane ? hh[i] : fh, 0, 0);
		if (ef)
			ef->layer = 1;
		if (field_empty(&x->f) && c->paint) /* a new event: a dotted line where its title goes */
			lk_rule(c->cr, tx, yy[i] + fmin(hh[i], PL_ROW) - 1.5, fw, 1.5, L->ink, 2);
		{
			/* as written while the pen writes in it: from its lane's gap, so ink begun there shows */
			double cx0 = writing || !placed ? fmin(tx, sp[i] - BK_GAP + 2) : tx, cy1 = yy[i] + (rows || lane ? hh[i] : gh);
			title_ink(c, &x->f, tx, placed ? ty : yy[i], k, placed ? &pl : NULL, L->pen, cx0, yy[i], tx + fw, cy1,
				  cut ? CUT_FADE : 0);
		}
		if (c->evb && (*c->nevb) < V_EVS) {
			struct vev *e = &c->evb[(*c->nevb)++];
			*e = (struct vev){ x->id, a->s0[i], a->e0[i], ln[i], yy[i], hh[i], sp[i], lw[i], tx, fw,
					   rows || lane ? hh[i] : fmax(gh, hh[i]), { 0 }, cut };
			if (placed) {
				e->ink[0] = tx, e->ink[1] = ty, e->ink[2] = fmin(pl.w * k, fw), e->ink[3] = pl.h * k;
			} else {
				double a0, b0, a1, b1;
				ink_box(&x->f, &a0, &b0, &a1, &b1);
				if (a1 > a0)
					e->ink[0] = tx + a0 * k, e->ink[1] = yy[i] + b0 * k, e->ink[2] = fmin((a1 - a0) * k, fw - a0 * k),
					e->ink[3] = fmin((b1 - b0) * k, e->th - b0 * k);
			}
		}
		if (placed)
			rw_free(&pl);
	}
	/* the rows that don't fit a small widget: one black bar inside the group, never quiet: their
	 * number, their times and the first one's title; a tap opens the day (board event.more) */
	for (int gi = 0; gi < a->ng; gi++) {
		const struct agrp *g = &a->g[gi];
		if (g->shown >= g->n)
			continue;
		int s = 1 << 30, e = -1, nf = g->n - g->shown;
		const struct entry *f1 = NULL;
		for (int i = 0; i < nv; i++)
			if (ln[i].rows && ln[i].group == g->group && fold[i]) {
				s = s < a->s0[i] ? s : a->s0[i], e = e > a->e0[i] ? e : a->e0[i];
				if (ln[i].order == g->shown)
					f1 = a->vis[i];
			}
		double bx = x0 - 1, bw = x1 - x0 + 2, by = round(y0 + g->bar_y), bh = g->bar_h;
		hit(c, bx, by, bw, bh, A_FOLD, day, 0);
		if (c->v->nfold < V_FOLDS)
			c->v->fold[c->v->nfold++] = (struct vbox){ bx, by, bw, bh };
		if (!c->paint)
			continue;
		lk_rect(c->cr, bx, by, bw, bh, 3);
		lk_fill(c->cr, L->ink);
		char t[24], w[24], w1[8], w2[8];
		snprintf(t, sizeof t, "+%d more", nf);
		hm(w1, sizeof w1, s < 0 ? 0 : s);
		hm(w2, sizeof w2, e);
		snprintf(w, sizeof w, "%s–%s", w1, w2);
		UiFont nf_ = fam(L->mono, 600, 0, 11.5f), wf2 = fam(L->mono, 400, 0, 10), tf = fam(L->mono, 500, 0, 10);
		double h1, h2, h3;
		double nw = ui_text_w(&c->g, &nf_, t, &h1), ww = ui_text_w(&c->g, &wf2, w, &h2);
		double ly = g->two ? by + 6 : by + bh / 2 - h1 / 2;
		ui_text(&c->g, &nf_, t, bx + 8, ly, 0, 0, L->paper, &h1);
		ui_text(&c->g, &wf2, w, bx + 8 + nw + 8, ly + (h1 - h2) / 2, 0, 0, L->paper, &h2);
		double ix = g->two ? bx + 8 : bx + 8 + nw + 8 + ww + 8, iy, ir = bx + bw - 8;
		if (!g->two) {
			double tw = ui_text_w(&c->g, &tf, "tap to show", &h3);
			ui_text(&c->g, &tf, "tap to show", bx + bw - 8, by + bh / 2 - h3 / 2, 2, 0, L->paper, &h3);
			ir = bx + bw - 8 - tw - 8;
		}
		if (f1 && ir > ix + 8) {
			double kf = FOLD_FS / INK_SIZE, fr, fb;
			field_extent(&f1->f, &fr, &fb);
			struct rw_place fp;
			bool ok = rw_place(&f1->f, 0, &fp);
			iy = g->two ? by + 6 + h1 + 4 : by + bh / 2 - (ok ? fp.h : 14) * kf / 2;
			title_ink(c, &f1->f, ix, iy, kf, ok ? &fp : NULL, L->paper, ix, by, ir, by + bh,
				  (ok ? fp.w : fr) * kf > ir - ix ? FOLD_FADE : 0);
			if (ok)
				rw_free(&fp);
		}
	}
	/* the touch targets: each event's ends first (they lie over the title's corners), then its
	 * title, which takes its lane between the brackets; a row has no ends */
	for (int i = first; i < (*c->nevb); i++) {
		const struct vev *e = &c->evb[i];
		if (e->ln.rows)
			continue;
		double hx, hw;
		lane_end_hit(e->spine, e->ln.lane, &hx, &hw);
		/* an event that ends where another in its place starts: their handles meet halfway */
		double t0 = e->y - BK_HIT_H / 2, b1 = e->y + e->h + BK_HIT_H / 2;
		for (int j = first; j < (*c->nevb); j++) {
			const struct vev *o = &c->evb[j];
			if (j == i || fabs(o->spine - e->spine) > 1)
				continue;
			double mo = (o->y + o->h + e->y) / 2, me = (e->y + e->h + o->y) / 2;
			if (o->y + o->h <= e->y + 1 && e->y - (o->y + o->h) < BK_HIT_H)
				t0 = fmax(t0, mo);
			if (o->y >= e->y + e->h - 1 && o->y - (e->y + e->h) < BK_HIT_H)
				b1 = fmin(b1, me);
		}
		hit(c, hx, t0, hw, e->y + BK_HIT_H / 2 - t0, A_EV_TOP, 0, e->id);
		hit(c, hx, e->y + e->h - BK_HIT_H / 2, hw, b1 - (e->y + e->h - BK_HIT_H / 2), A_EV_BOT, 0, e->id);
	}
	for (int i = first; i < (*c->nevb); i++) {
		const struct vev *e = &c->evb[i];
		hit(c, e->spine + 2, e->y, e->w - 4, e->th, A_EVENT, e->cut, e->id);
	}
	/* an event being moved or stretched: the margin's readout at the end that moves (above the
	 * start, below the end); one held past the first or last hour, at that edge */
	const struct vev *dg = NULL;
	for (int i = first; v->drag && i < (*c->nevb); i++)
		if (c->evb[i].id == v->drag)
			dg = &c->evb[i];
	if (dg) {
		char t[16];
		bool end = v->drag_end == 2;
		int m = end ? dg->e0 : dg->s0;
		hm(t, sizeof t, m < 0 ? 0 : m);
		double ry = end ? dg->y + dg->h + 3 : dg->y - 1 - 17;
		if (edge == 1) {
			snprintf(t + strlen(t), sizeof t - strlen(t), " ↑");
			ry = y0 - 1;
		} else if (edge == 2) {
			snprintf(t + strlen(t), sizeof t - strlen(t), " ↓");
			ry = ybot - 15;
		}
		readout(c, b->x, ry, t);
	}
	/* a bracket being drawn down the margin: dashed, the whole width, its start's and its
	 * end's readouts (board event.draw) */
	if (v->draw_on && v->draw_day == day) {
		double y1 = y0 + tmap_y(&a->tm, v->draw_s), y2 = y0 + tmap_y(&a->tm, v->draw_e);
		double sx, sw;
		lane_x(x0, x1, 0, 1, &sx, &sw);
		brackets(c, sx, sw, round(y1) + 1, fmax(8, round(y2 - y1) - 2), true, 0, L->ink);
		char t[8];
		hm(t, sizeof t, v->draw_s);
		readout(c, b->x, round(y1) - 17, t);
		hm(t, sizeof t, v->draw_e);
		readout(c, b->x, round(y2) + 3, t);
	}
}

/* the now line at y (FR-018, board event.more): its dot in the hour margin, the line broken
 * around every event and fold bar it would cross, a piece shorter than NOW_LEAST left out */
static void now_line(struct ctx *c, struct wbox *b, double y, int first)
{
	struct view *v = c->v;
	double cut[V_EVS + V_FOLDS][2];
	int nc = 0;
	for (int i = first; i < (*c->nevb); i++) {
		const struct vev *e = &c->evb[i];
		double bot = fmax(e->y + fmax(e->h, e->th), e->ink[2] > 0 ? e->ink[1] + e->ink[3] : 0);
		double l = fmin(e->spine - 1, e->ink[2] > 0 ? e->ink[0] : 1e9);
		if (y >= e->y - NOW_CLEAR_Y && y <= bot + NOW_CLEAR_Y)
			cut[nc][0] = l - NOW_CLEAR, cut[nc++][1] = e->spine + e->w + 2 + NOW_CLEAR;
	}
	for (int i = 0; i < v->nfold; i++) {
		const struct vbox *f = &v->fold[i];
		if (y >= f->y - NOW_CLEAR_Y && y <= f->y + f->h + NOW_CLEAR_Y)
			cut[nc][0] = f->x - NOW_CLEAR, cut[nc++][1] = f->x + f->w + NOW_CLEAR;
	}
	for (int i = 0; i < nc; i++) /* by their left end */
		for (int j = i + 1; j < nc; j++)
			if (cut[j][0] < cut[i][0]) {
				double t0 = cut[i][0], t1 = cut[i][1];
				cut[i][0] = cut[j][0], cut[i][1] = cut[j][1], cut[j][0] = t0, cut[j][1] = t1;
			}
	v->now_y = y, v->now_dot = b->x + NOW_X, v->nnow = 0;
	double x = b->x + NOW_X, end = b->x + b->w;
	for (int i = 0; i <= nc; i++) {
		double to = i < nc ? cut[i][0] : end;
		if (to - x >= NOW_LEAST && v->nnow < 16)
			v->now_seg[v->nnow][0] = x, v->now_seg[v->nnow][1] = to - x, v->nnow++;
		if (i < nc)
			x = fmax(x, cut[i][1]);
	}
	if (!c->paint)
		return;
	for (int i = 0; i < v->nnow; i++)
		lk_rule(c->cr, v->now_seg[i][0], y - 1, v->now_seg[i][1], 2, c->L->ink, 0);
	cairo_arc(c->cr, b->x + NOW_X, y, 4, 0, 2 * M_PI);
	lk_fill(c->cr, c->L->ink);
}

/* a group in rows, in the hour margin (board event.more): its stretched hours shaded, a break
 * across the margin at both ends and its scale at the foot ("2×") */
static void stretch_marks(struct ctx *c, struct wbox *b, const struct agrp *g, double y0)
{
	if (!c->paint)
		return;
	const struct look *L = c->L;
	double top = y0 + g->y0, h = g->y1 - g->y0, mw = BK_SPINE - 2;
	lk_bar(c->cr, b->x, top, mw, h, L->rule);
	cairo_save(c->cr);
	col(c->cr, L->ink);
	cairo_set_line_width(c->cr, L->voice == V_STATIONERY ? 2 : 2.5);
	cairo_set_line_join(c->cr, CAIRO_LINE_JOIN_MITER);
	for (int end = 0; end < 2; end++) {
		double zy = end ? top + h : top;
		cairo_move_to(c->cr, b->x, zy);
		for (int i = 0; i < 6; i++)
			cairo_rel_line_to(c->cr, mw / 6, i % 2 ? 5 : -5);
		cairo_stroke(c->cr);
	}
	cairo_restore(c->cr);
	char t[12];
	if (fabs(g->scale - round(g->scale)) < 0.01)
		snprintf(t, sizeof t, "%.0f×", g->scale);
	else
		snprintf(t, sizeof t, "%.1f×", g->scale);
	UiFont f = fam(L->mono, 700, 0, 10);
	double th, tw = ui_text_w(&c->g, &f, t, &th);
	double cx = b->x + mw - 2 - tw - 6, cy = top + h - 8 - th - 4;
	lk_rect(c->cr, cx, cy, tw + 6, th + 4, 2);
	lk_fill(c->cr, L->ink);
	ui_text(&c->g, &f, t, cx + 3, cy + 2, 0, 0, L->paper, &th);
}

static void w_agenda(struct ctx *c, struct wbox *b, bool compact)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	int day = compact ? period_of(P_WEEK, v->per.start).start + b->n->day : v->per.start;
	struct entry *ev[64];
	int ne = store_events(c->s, day, ev, 64), nad = 0;
	for (int i = 0; i < ne; i++)
		nad += ev[i]->allday;
	double hh = compact ? 30 : PL_HEAD;
	card(c, b);
	if (compact) {
		int y, m, d;
		day_to_ymd(day, &y, &m, &d);
		char t[16];
		snprintf(t, sizeof t, "%s %d", DOW2[day_weekday(day)], d);
		head(c, b, t, "", hh);
		hit(c, b->x, b->y, b->w, hh, A_DAY, day, 0);
	}
	double top = !compact && nad ? 30 : 6;
	double minp = compact ? 16 : 20, avail = b->h - hh - top - 6;
	int ws = b->n->from, we = b->n->to;
	double pitch = avail / (we - ws);
	if (pitch < minp) {
		pitch = minp;
		int vis = (int)fmax(1, floor(avail / minp));
		ws = b->n->from > (b->n->to - vis < 13 ? b->n->to - vis : 13) ? b->n->from : (b->n->to - vis < 13 ? b->n->to - vis : 13);
		we = ws + vis;
	}
	/* an event held past the first or last hour: the hours scroll with it, a step per step
	 * (board event.drag edge); on release they go back */
	int edge = 0;
	struct entry *held = !compact && v->drag ? store_get(c->s, v->drag) : NULL;
	if (held && held->per.start == day && !held->allday) {
		int sh = 0;
		if (held->start < ws * 60)
			sh = -((ws * 60 - held->start + 59) / 60), edge = 1;
		else if (held->end > we * 60)
			sh = (held->end - we * 60 + 59) / 60, edge = 2;
		if (ws + sh < 0)
			sh = -ws;
		if (we + sh > 24)
			sh = 24 - we;
		ws += sh, we += sh;
	}
	/* its timed events planned first: a group in rows draws its hours taller */
	static struct aplan ap;
	int before = 0, after = 0;
	if (!compact)
		agenda_plan(c, b, ev, ne, day, ws, we, pitch, &before, &after, &ap);
	else
		tmap_linear(&ap.tm, ws * 60, we * 60, pitch), ap.ng = 0, ap.folded = 0;
	if (!compact) { /* its hours, as they show while an event is held past them; a fold is named */
		char tag[40];
		if (ap.folded)
			snprintf(tag, sizeof tag, "%d events, %d folded", ap.timed, ap.folded);
		else
			snprintf(tag, sizeof tag, "%02d:00–%02d:00", edge ? ws : b->n->from, edge ? we : b->n->to);
		head(c, b, "Agenda", tag, hh);
		v->folded += ap.folded;
	}
	double by = b->y + hh, gut = compact ? 16 : GUTTER;
	if (c->paint) {
		cairo_save(c->cr);
		lk_rect(c->cr, b->x, b->y, b->w, b->h, L->wg_r);
		cairo_clip(c->cr);
	}
	for (int i = 0; !compact && !ap.fixed && i < ap.ng; i++) /* the stretched hours marked in the margin */
		stretch_marks(c, b, &ap.g[i], by + top);
	/* inside a group in rows the hour rules stay in the margin: in a page's agenda bold ticks
	 * at the new scale, in a widget plain (board event.more) */
	for (int h = ws; h <= we; h++) {
		double ry = tmap_y(&ap.tm, h * 60), y = round(by + top + ry);
		bool in = ap_inside(&ap, ry), bold = in && !ap.fixed;
		if (c->paint)
			lk_rule(c->cr, b->x, y - (bold ? 0.5 : 0), in ? BK_SPINE - 2 : b->w, bold ? 2 : 1, bold ? L->ink : L->rule, 0);
		if (!compact)
			agenda_rule(v, b->x, y, in ? BK_SPINE - 2 : b->w, bold ? 2 : 1);
		if (h < we) {
			double rh = tmap_y(&ap.tm, h * 60 + 30), hy = round(by + top + rh);
			bool hin = ap_inside(&ap, rh), hb = hin && !ap.fixed;
			if (c->paint) {
				if (hin)
					lk_rule(c->cr, b->x + 18, hy - (hb ? 0.5 : 0), 16, hb ? 2 : 1, hb ? L->ink : L->rule, hb ? 0 : 2);
				else
					lk_rule(c->cr, b->x + gut, hy, b->w - gut, 1, L->rule, 2);
			}
			if (!compact)
				agenda_rule(v, hin ? b->x + 18 : b->x + gut, hy, hin ? 16 : b->w - gut, hb ? 2 : 1);
			char t[4];
			if (compact) {
				if (h % 3 == 0) {
					snprintf(t, sizeof t, "%d", h);
					UiFont f = fpx(L->small, 9.5f);
					double th;
					ui_text_w(&c->g, &f, t, &th);
					if (c->paint)
						ui_text(&c->g, &f, t, b->x + 3, y + 2, 0, 0, L->soft, &th);
				}
			} else {
				snprintf(t, sizeof t, "%02d", h);
				double th;
				UiFont hf = L->small;
				if (bold)
					hf.weight = 700;
				ui_text_w(&c->g, &hf, t, &th);
				if (c->paint)
					ui_text(&c->g, &hf, t, b->x + 6, y + 2, 0, 0, bold ? L->ink : L->soft, &th);
			}
		}
	}
	/* all-day events above the hours */
	if (!compact && nad) {
		double ax = b->x + GUTTER;
		for (int i = 0; i < ne; i++) {
			if (!ev[i]->allday)
				continue;
			double r, bt;
			field_extent(&ev[i]->f, &r, &bt);
			double w = fmax(70, fmin(r * 15.0 / INK_SIZE + 16, b->x + b->w - 6 - ax));
			box(c, ax, by + 5, w, 20, 10, 1, L->ink, 0);
			ink_fit(c, &ev[i]->f, ax + 8, by + 7, w - 12, 17);
			ax += w + 6;
		}
	}
	/* the free time: drawing a block there makes an event (FR-018) */
	if (!compact) {
		struct vfield *f = field(c, F_FREE, b->x + GUTTER, by + top, b->w - GUTTER - 6, tmap_y(&ap.tm, we * 60));
		if (f) {
			snprintf(f->name, sizeof f->name, "free:%d:%d", day, (int)(b->x * 1000 + b->y));
			f->per = (struct period){ P_DAY, day };
			f->min0 = ws * 60;
			f->mpp = 60 / pitch;
			f->tm = ap.tm;
		}
		/* each row on its own ruled line across the writing area; one that would double the
		 * group's last hour rule is left out */
		for (int i = 0; i < ap.ng; i++) {
			const struct agrp *g = &ap.g[i];
			for (int r = 1; r <= g->shown; r++) {
				double ry = round(by + top + g->y0 + ROW_PAD + r * g->p - ceil(ROW_GAP / 2.0));
				if (r < g->n || by + top + g->y1 - ry > 6) {
					if (c->paint)
						lk_rule(c->cr, ap.x0, ry, ap.x1 - ap.x0, 1, L->rule, 0);
					agenda_rule(v, ap.x0, ry, ap.x1 - ap.x0, 1);
				}
			}
		}
	}
	int first_ev = *c->nevb;
	/* now */
	double now_y = 0;
	if (day == v->today && !compact) {
		const char *e = getenv("GLASSINE_PLANNER_NOW"); /* renders: minutes of the day */
		int now;
		if (e)
			now = atoi(e);
		else {
			time_t t = time(NULL);
			struct tm tm;
			localtime_r(&t, &tm);
			now = tm.tm_hour * 60 + tm.tm_min;
		}
		if (now >= ws * 60 && now <= we * 60)
			now_y = by + top + tmap_y(&ap.tm, now);
	}
	/* timed events: a compact agenda's as blocks, a full one's as bracket pairs in lanes */
	if (!compact)
		agenda_events(c, b, &ap, day, ws, we, by + top, pitch, edge);
	if (now_y > 0) /* after the events: it breaks around them */
		now_line(c, b, now_y, first_ev);
	for (int i = 0; compact && i < ne; i++) {
		struct entry *x = ev[i];
		if (x->allday)
			continue;
		int s0 = (x->per.start - day) * 1440 + x->start, e0 = (x->per.start - day) * 1440 + x->end;
		if (e0 <= ws * 60) {
			before++;
			continue;
		}
		if (s0 >= we * 60) {
			after++;
			continue;
		}
		double y1 = by + top + (fmax(s0, ws * 60) / 60.0 - ws) * pitch, y2 = by + top + (fmin(e0, we * 60) / 60.0 - ws) * pitch;
		double bx = b->x + gut, bw = b->w - gut - 2, byy = round(y1) + 1, bh = fmax(8, round(y2 - y1) - 2);
		box(c, bx, byy, bw, bh, fmin(L->ui_r, 8), L->voice == V_STATIONERY ? 1 : L->voice == V_EDITORIAL ? 1 : 1.5,
		    L->voice == V_STATIONERY ? L->soft : L->ink, L->paper);
		if (L->ev_bar)
			box(c, bx, byy, 3, bh, 0, 0, 0, L->ink);
		ink_fit(c, &x->f, bx + 3, byy + 1, bw - 4, fmin(bh - 2, 15));
		hit(c, bx, byy, bw, bh, A_DAY, day, 0);
	}
	if (before || after) {
		char t[24];
		UiFont ef = L->small;
		if (before) {
			snprintf(t, sizeof t, compact ? "↑%d" : "↑ %d earlier", before);
			tc(c, &ef, t, b->x + b->w - 8, by + top + 9, 2, 0, L->soft);
		}
		if (after) {
			snprintf(t, sizeof t, compact ? "↓%d" : "↓ %d later", after);
			tc(c, &ef, t, b->x + b->w - 8, b->y + b->h - 12, 2, 0, L->soft);
		}
	}
	if (c->paint)
		cairo_restore(c->cr);
	card_edge(c, b);
}

/* ---- the template's content ---- */

static struct need need_of(const struct tnode *n, void *ud)
{
	struct ctx *c = ud;
	struct period per = wper(c->v, n);
	struct entry *e[MAXE];
	struct need r = { 0, false };
	switch (n->wg) {
	case W_AGENDA: {
		int k = store_events(c->s, c->v->per.start, e, MAXE);
		for (int i = 0; i < k; i++)
			r.allday |= e[i]->allday;
		break;
	}
	case W_TODO: r.n = store_list(c->s, E_TODO, per, NULL, e, MAXE); break;
	case W_PRIO: r.n = store_list(c->s, E_PRIO, per, NULL, e, MAXE); break;
	case W_HABITS: r.n = store_list(c->s, E_HABIT, per, NULL, e, MAXE); break;
	case W_NOTES: {
		int k = store_list(c->s, E_NOTE, per, n->slot ? n->slot : "notes", e, 4);
		if (k) {
			double rr, bb;
			field_extent(&e[0]->f, &rr, &bb);
			r.n = (int)ceil(bb / PL_ROW);
		}
		break;
	}
	default: break;
	}
	return r;
}

static void page(struct ctx *c)
{
	struct view *v = c->v;
	double pw = v->W - 2 * PAD, ph = v->H - BAR - 2 * PAD;
	if (!c->paint) {
		grid_solve(v->t, pw, ph, v->land, need_of, c, &v->g);
		*c->evs_out = true; /* the agendas' events laid out anew */
	}
	v->nfold = v->nnow = v->folded = v->nrule = 0, v->now_y = 0;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, BAR, v->W, v->H - BAR);
		lk_fill(c->cr, c->L->paper);
	}
	for (int i = 0; i < v->g.n; i++) {
		const struct placed *p = &v->g.p[i];
		struct wbox b = { PAD + p->x, BAR + PAD + p->y, p->w, p->h, p->n, wper(v, p->n) };
		switch (p->n->wg) {
		case W_AGENDA: w_agenda(c, &b, false); break;
		case W_CAGENDA: w_agenda(c, &b, true); break;
		case W_TODO: w_list(c, &b, true); break;
		case W_PRIO: w_list(c, &b, false); break;
		case W_NOTES: w_notes(c, &b); break;
		case W_HABITS: w_habits(c, &b); break;
		case W_MINIMONTH: w_minimonth(c, &b); break;
		case W_WEEKOV: w_weekov(c, &b); break;
		default: break;
		}
	}
}

/* ---- the (i) popover and the cog's menu (planner.css .pl-pop, .pl-mi) ---- */

static void pop_frame(struct ctx *c, double x, double y, double w, double h)
{
	const struct look *L = c->L;
	if (!c->paint)
		return;
	cairo_t *cr = c->cr;
	if (L->voice == V_WHITE) { /* inset keyline, keycap 0 3px 0 #bbb */
		lk_rect(cr, x, y + 3, w, h, L->card_r);
		lk_fill(cr, L->shade);
		lk_rect(cr, x, y, w, h, L->card_r);
		lk_fill(cr, L->paper);
		lk_stroke_box(cr, x, y, w, h, L->card_r, 1.5, L->ink);
	} else if (L->voice == V_EDITORIAL) { /* inset 1px ink, offset 3px 3px 0 ink */
		cairo_rectangle(cr, x + 3, y + 3, w, h);
		lk_fill(cr, L->ink);
		cairo_rectangle(cr, x, y, w, h);
		lk_fill(cr, L->paper);
		lk_stroke_box(cr, x, y, w, h, 0, 1, L->ink);
	} else { /* 0 0 0 1px ink, 0 3px 0 shade */
		lk_rect(cr, x - 1, y + 2, w + 2, h + 2, L->card_r + 1);
		lk_fill(cr, L->shade);
		lk_rect(cr, x - 1, y - 1, w + 2, h + 2, L->card_r + 1);
		lk_fill(cr, L->ink);
		lk_rect(cr, x, y, w, h, L->card_r);
		lk_fill(cr, L->paper);
	}
}


/* ---- search, at the top of the (i) popover (board info.search, answer B; FR-050) ---- */

static void when_of(const struct entry *e, char *o, size_t n)
{
	int y, m, d;
	day_to_ymd(e->per.start, &y, &m, &d);
	if (e->per.kind == P_WEEK)
		snprintf(o, n, "week %d", week_number(e->per.start));
	else if (e->per.kind == P_MONTH)
		snprintf(o, n, "%s %d", MON[m - 1], y);
	else
		snprintf(o, n, "%s %d %.3s", DOW3[day_weekday(e->per.start)], d, MON[m - 1]);
}

/* text with a caret at byte cur (-1: none), from x on the line whose vertical centre is cy,
 * clipped to w; moved left when the caret would be past the right edge */
static void caret_text(struct ctx *c, const UiFont *f, const char *t, int cur, double x, double cy, double w, ui_col cl)
{
	t = t ? t : "";
	double h, full = ui_text_w(&c->g, f, t, &h), cx = 0;
	if (cur >= 0) {
		char pre[1024];
		snprintf(pre, sizeof pre, "%.*s", cur, t);
		cx = ui_text_w(&c->g, f, pre, &h);
	}
	double sh = cx > w - 4 ? cx - (w - 4) : 0;
	if (!c->paint)
		return;
	cairo_save(c->cr);
	cairo_rectangle(c->cr, x, cy - h, w, 2 * h);
	cairo_clip(c->cr);
	ui_text(&c->g, f, t, x - sh, cy - h / 2, 0, 0, cl, &h);
	if (cur >= 0) {
		cairo_rectangle(c->cr, x - sh + cx, cy - 10, 2, 20);
		lk_fill(c->cr, c->L->ink);
	}
	cairo_restore(c->cr);
	(void)full;
}

static double search_box(struct ctx *c, double x, double y, double w)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double bx = x + 12, by = y + 10, bw = w - 24;
	box(c, bx, by, bw, 40, L->ui_r, 1.5, L->ink, 0);
	icon(c, IC_SEARCH, bx + 12 + 9, by + 20, 18, L->ink);
	UiFont f = fam(L->text, 400, 0, 15);
	bool typed = v->search && v->q && *v->q;
	caret_text(c, &f, typed ? v->q : "Search the planner", v->search ? (typed ? v->q_cur : 0) : -1, bx + 12 + 18 + 10, by + 20,
		   bw - 12 - 18 - 10 - 8, typed ? L->ink : L->soft);
	hit(c, bx, by, bw, 40, A_SEARCH, 0, 0);
	return by + 40 + 4;
}

#define RESULTS 8
static double results(struct ctx *c, double x, double y, double w)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct entry *e[RESULTS + 1];
	int n = store_search(c->s, v->q, e, RESULTS + 1);
	char head_[48];
	if (n > RESULTS)
		snprintf(head_, sizeof head_, "More than %d found", RESULTS);
	else
		snprintf(head_, sizeof head_, n ? "%d found" : "Nothing found", n);
	UiFont lf = L->label;
	double kh;
	ui_text_w(&c->g, &lf, "X", &kh);
	if (c->paint)
		ui_text(&c->g, &lf, head_, x + 16, y + 12, 0, w - 32, L->soft, &kh);
	y += 12 + kh + 4;
	UiFont qf = fam(L->mono, 400, 0, 11.5f);
	double vh = v->vis_h > 0 && v->vis_h < v->H ? v->vis_h : v->H; /* above the keyboard */
	for (int i = 0; i < n && i < RESULTS && y + 46 < vh - 8; i++) {
		char when[40];
		when_of(e[i], when, sizeof when);
		double ww = tw(c, &qf, when);
		if (e[i]->kind == E_MARK) {
			struct entry *h = store_get(c->s, e[i]->habit);
			if (h)
				ink_fit(c, &h->f, x + 16, y + 6, w - 32 - ww - 10, 34);
		} else
			ink_fit(c, &e[i]->f, x + 16, y + 6, w - 32 - ww - 10, 34);
		tc(c, &qf, when, x + w - 16, y + 23, 2, 0, L->soft);
		rule(c, x + 16, y + 46 - L->row_w, w - 32);
		hit(c, x, y, w, 46, A_RESULT, 0, e[i]->id);
		y += 46;
	}
	return y;
}

static double pop_content(struct ctx *c, double x, double y, double w)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	bool menu = v->ov == OV_MENU;
	double y0 = y;
	/* the head: (‹) the label, the cog */
	double hx = x + 16;
	if (menu) {
		icon(c, IC_CHEV_L, x + 4 + 16 - 12 + 4, y + 23, 20, L->ink);
		hit(c, x, y, 40, 46, A_BACK, 0, 0);
		hx = x + 16 + 20;
	}
	UiFont lf = L->label;
	tc(c, &lf, menu ? "Planner" : "This page", hx, y + 23, 0, 0, L->soft);
	double gx = x + w - 4 - 40;
	if (menu) {
		box(c, gx, y + 3, 40, 40, L->ui_r, 1.5, L->ink, 0);
		icon(c, IC_COG, gx + 20, y + 23, 21, L->ink);
	} else
		icon(c, IC_COG, gx + 20, y + 23, 19, L->soft);
	hit(c, gx, y + 3, 40, 40, menu ? A_BACK : A_COG, 0, 0);
	y += 46;
	rule(c, x, y - L->row_w, w);
	if (!menu) {
		y = search_box(c, x, y, w);
		if (v->search && v->q && *v->q)
			return results(c, x, y, w) + 6 - y0;
	}
	if (!menu) {
		/* the template and how this period got it */
		static const char *how[] = { "chosen for this %s", "from a range of %ss", "for every %s from a date on",
			"the default for %ss" };
		char hw[64];
		snprintf(hw, sizeof hw, how[v->how < 0 || v->how > 3 ? 3 : v->how], pkind_name(v->per.kind));
		UiFont kf = fam(L->mono, 500, 0, 11), vf = fam(L->text, 400, 0, 16), sf = fam(L->text, 400, 0, 12.5f);
		double h1, h2, hk;
		ui_text_w(&c->g, &vf, v->t->name, &h1);
		ui_text_w(&c->g, &sf, hw, &h2);
		ui_text_w(&c->g, &kf, "Template", &hk);
		if (c->paint) {
			ui_text(&c->g, &kf, "Template", x + 16, y + 10 + 2, 0, 74, L->soft, &hk);
			ui_text(&c->g, &vf, v->t->name, x + 16 + 74 + 12, y + 10, 0, w - 118, L->ink, &h1);
			ui_text(&c->g, &sf, hw, x + 16 + 74 + 12, y + 10 + h1 + 2, 0, w - 118, L->soft, &h2);
		}
		y += 10 + h1 + 2 + h2 + 8;
		rule(c, x, y - L->row_w, w);
		struct hidden_group g[16];
		int n = view_hidden(c->s, v->t, v->per, g, 16);
		double kh;
		ui_text_w(&c->g, &lf, "X", &kh);
		if (c->paint)
			ui_text(&c->g, &lf, n ? "Not shown in this layout" : "Nothing hidden", x + 16, y + 12, 0, w - 32, L->soft, &kh);
		y += 12 + kh + 4;
		for (int i = 0; i < n; i++) {
			if (i)
				rule(c, x, y, w);
			icon(c, IC_HIDDEN, x + 16 + 9, y + 20, 18, L->soft);
			tc(c, &L->body, g[i].label, x + 16 + 18 + 10, y + 20, 0, w - 90, L->ink);
			tc(c, &(UiFont){ L->mono, 400, 0, 12, 0, 0, UI_CASE_NONE }, "›", x + w - 16, y + 20, 2, 0, L->soft);
			hit(c, x, y, w, 40, A_HIDDEN, i, 0);
			y += 40;
		}
		if (!n) {
			char t[80];
			snprintf(t, sizeof t, "Every entry of this %s has a place in this layout.", pkind_name(v->per.kind));
			UiFont nf = fam(L->text, 400, 0, 14);
			double th = ui_text_wrap(&c->g, &nf, t, x + 16, y + 6, w - 32, 3, 0, c->paint ? L->soft : 0);
			if (!c->paint)
				th = 40;
			y += 6 + th + 8;
		}
	} else {
		struct {
			const char *ic, *t, *sub;
			int tog;
			enum act a;
		} it[] = {
			{ IC_KEYBOARD, "Text mode for this view", "every field as text, with the keyboard", 1, A_TEXTMODE },
			{ IC_LAYOUT, "Template", NULL, 0, A_PICKER },
			{ IC_EDIT, "Edit this template", "add, move and resize widgets", 0, A_EDIT },
			{ IC_SLIDERS, "Settings", "how ink is shown, the agenda’s hours", 0, A_SETTINGS },
		};
		char tsub[96];
		snprintf(tsub, sizeof tsub, "%s · choose another, or plan ahead", v->t->name);
		it[1].sub = tsub;
		UiFont tf = fam(L->text, 400, 0, L->voice == V_STATIONERY ? 18 : 16), sf = fam(L->text, 400, 0, 12.5f);
		for (int i = 0; i < 4; i++) {
			if (i)
				rule(c, x, y, w);
			double h1, h2;
			ui_text_w(&c->g, &tf, it[i].t, &h1);
			double tw_ = w - 16 - 20 - 12 - 16 - (it[i].tog ? 52 : 20);
			UiGc mg = c->g;
			mg.paint = 0;
			h2 = ui_text_wrap(&mg, &sf, it[i].sub, 0, 0, tw_, 2, 0, L->soft);
			double ih = fmax(48, 6 + h1 + 2 + h2 + 6);
			icon(c, it[i].ic, x + 16 + 10, y + ih / 2, 20, L->ink);
			double tx = x + 16 + 20 + 12, ty = y + (ih - h1 - 2 - h2) / 2;
			if (c->paint) {
				ui_text(&c->g, &tf, it[i].t, tx, ty, 0, tw_, L->ink, &h1);
				ui_text_wrap(&c->g, &sf, it[i].sub, tx, ty + h1 + 2, tw_, 2, 0, L->soft);
			}
			if (it[i].tog) {
				double gx2 = x + w - 16 - 40, gy = y + ih / 2 - 11;
				double r = L->voice == V_EDITORIAL ? 0 : 11;
				bool on = v->text_view;
				box(c, gx2, gy, 40, 22, r, 1.5, L->ink, on ? L->ink : 0);
				box(c, gx2 + (on ? 22 : 4), gy + 4, 14, 14, L->voice == V_EDITORIAL ? 0 : 7, 0, 0, on ? L->paper : L->soft);
			} else
				tc(c, &(UiFont){ L->mono, 400, 0, 12, 0, 0, UI_CASE_NONE }, "›", x + w - 16, y + ih / 2, 2, 0, L->soft);
			hit(c, x, y, w, ih, it[i].a, 0, 0);
			y += ih;
		}
	}
	return y + 6 - y0;
}

static void popover(struct ctx *c)
{
	struct view *v = c->v;
	double w = 340, x = v->W - 8 - w, y = BAR + 4;
	if (c->paint) {
		pop_frame(c, x, y, w, v->pop_h);
		pop_content(c, x, y, w);
	} else {
		/* tap targets of the popover go first: it covers the page */
		int nh0 = v->nh;
		double h = pop_content(c, x, y, w);
		v->pop_x = x, v->pop_y = y, v->pop_w = w, v->pop_h = h;
		(void)nh0;
	}
}

/* ---- a bottom sheet: the hidden entries of one kind (board hidden.list) ---- */

static void sheet_hidden(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double ph = v->H - BAR, sh = round(ph * 0.66), y = v->H - sh, W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, W, sh);
		lk_fill(c->cr, L->paper);
		if (L->voice == V_EDITORIAL) {
			lk_rule(c->cr, 0, y, W, 1, L->ink, 0);
			lk_rule(c->cr, 0, y + 2, W, 1, L->ink, 0);
		} else
			lk_rule(c->cr, 0, y, W, 1.5, L->ink, 0);
	}
	UiFont h2 = fpx(L->h1, 21);
	tc(c, &h2, "Not shown in this layout", 16, y + 14 + 20, 0, W - 140, L->ink);
	UiFont bf = L->voice == V_WHITE ? fam(L->mono, 600, 0, 11.5f) : L->label;
	if (L->voice != V_WHITE)
		bf.px = L->voice == V_EDITORIAL ? 16 : 17;
	bf.tcase = UI_CASE_UPPER;
	bf.ls = 0.08f;
	if (L->voice != V_WHITE)
		bf.tcase = UI_CASE_LOWER, bf.smallcaps = 1;
	double bw = tw(c, &bf, "Close") + 28;
	box(c, W - 16 - bw, y + 14, bw, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, "Close", W - 16 - bw / 2, y + 34, 1, 0, L->ink);
	hit(c, W - 16 - bw, y + 14, bw, 40, A_CLOSE, 0, 0);
	char lead[200];
	snprintf(lead, sizeof lead, "Kept as written. They show again in any %s template that has a place for them; or delete one here.",
		 pkind_name(v->per.kind));
	UiFont lf = fam(L->text, 400, 0, 14);
	double lh = c->paint ? ui_text_wrap(&c->g, &lf, lead, 16, y + 64, W - 32, 3, 1.4, L->soft) : 40;
	if (!c->paint) {
		UiGc mg = c->g;
		lh = ui_text_wrap(&mg, &lf, lead, 16, y + 64, W - 32, 3, 1.4, L->soft);
	}
	double yy = y + 64 + lh + 16;
	struct hidden_group g[16];
	int n = view_hidden(c->s, v->t, v->per, g, 16);
	if (v->hidden_kind < 0 || v->hidden_kind >= n) {
		hit(c, 0, y, W, sh, A_NONE, 0, 0);
		return;
	}
	struct hidden_group *hg = &g[v->hidden_kind];
	char head_[64], cnt[8];
	if (hg->kind == E_NOTE)
		snprintf(head_, sizeof head_, "%s", !strcmp(hg->slot, "notes") ? "Notes" : hg->slot);
	else
		snprintf(head_, sizeof head_, "%s", hg->kind == E_EVENT ? "Events" : hg->kind == E_TODO ? "To-dos" : hg->kind == E_PRIO ? "Priorities" : "Habit marks");
	if (hg->kind == E_NOTE && strcmp(hg->slot, "notes"))
		snprintf(head_ + strlen(head_), sizeof head_ - strlen(head_), " notes");
	snprintf(cnt, sizeof cnt, "%d", hg->n);
	double hw = tc(c, &L->wlabel, head_, 16, yy + 6, 0, 0, L->ink);
	tc(c, &L->tag, cnt, 16 + hw + 8, yy + 6, 0, 0, L->soft);
	yy += 16;
	struct entry *e[MAXE];
	int ne = 0;
	if (hg->kind == E_EVENT) {
		for (int d = v->per.start; d <= period_end(v->per) && ne < MAXE; d++) {
			struct entry *t[64];
			int k = store_events(c->s, d, t, 64);
			for (int i = 0; i < k && ne < MAXE; i++)
				if (t[i]->per.start == d || d == v->per.start)
					e[ne++] = t[i];
		}
	} else
		ne = store_list(c->s, hg->kind, v->per, hg->kind == E_NOTE ? hg->slot : NULL, e, MAXE);
	UiFont qf = fam(L->mono, 400, 0, 12), af = fam(L->mono, 500, 0, 12);
	for (int i = 0; i < ne && yy + 46 < v->H; i++) {
		double rh = 46;
		struct entry *x = e[i];
		if (hg->kind == E_MARK) {
			struct entry *h = store_get(c->s, x->habit);
			if (h)
				ink_fit(c, &h->f, 16, yy + 6, W - 330, 34);
		} else if (c->paint) {
			cairo_save(c->cr);
			cairo_rectangle(c->cr, 16, yy, W - 330, rh);
			cairo_clip(c->cr);
			double r, bt;
			field_extent(&x->f, &r, &bt);
			ink(c, &x->f, 16, yy + rh - 6 - (bt > 0 ? fmin(bt, 40) : 0), 1);
			cairo_restore(c->cr);
		}
		char when[32] = "";
		if (x->kind == E_TODO && x->done)
			snprintf(when, sizeof when, "done");
		else if (x->kind == E_NOTE)
			snprintf(when, sizeof when, "%s", x->slot);
		else if (x->kind == E_PRIO)
			snprintf(when, sizeof when, "%d", i + 1);
		else if (x->kind == E_EVENT) {
			if (x->allday)
				snprintf(when, sizeof when, "all day");
			else {
				char a[8], b[8];
				hm(a, sizeof a, x->start);
				hm(b, sizeof b, x->end);
				snprintf(when, sizeof when, "%s–%s", a, b);
			}
		}
		/* Open (the whole entry), Move (to another period), Delete */
		static const char *const ACT[] = { "Delete", "Move", "Open" };
		double dx = W - 16 - 6;
		for (int a = 0; a < (x->kind == E_MARK ? 1 : 3); a++) {
			double dw = tw(c, &af, ACT[a]);
			dx -= dw;
			tc(c, &af, ACT[a], dx, yy + rh / 2 + 4, 0, 0, L->ink);
			if (c->paint)
				lk_rule(c->cr, dx, yy + rh / 2 + 4 + 9, dw, 1, L->ink, 0);
			if (a == 0)
				hit(c, dx - 6, yy, dw + 12, rh, A_CLOSE, 1, x->id); /* arg 1: delete */
			else
				hit(c, dx - 6, yy, dw + 12, rh, a == 1 ? A_MOVE : A_OPEN, 0, x->id);
			dx -= 14;
		}
		dx += 14;
		if (*when)
			tc(c, &qf, when, dx - 16, yy + rh / 2 + 4, 2, 0, L->soft);
		rule(c, 16, yy + rh, W - 32);
		yy += rh;
	}
	hit(c, 0, y, W, sh, A_NONE, 0, 0); /* the sheet takes its taps: after its own targets */
}

/* ---- text mode (boards text.field, text.view; FR-012 to FR-017) ---- */

#define TBAR 46

static UiFont plex(float px)
{
	return fam("IBM Plex Sans", 400, 0, px);
}

static double vis_of(const struct view *v)
{
	return v->vis_h > 0 && v->vis_h < v->H ? v->vis_h : v->H;
}

static const char *text_of(struct ctx *c, const struct vfield *f)
{
	struct view *v = c->v;
	if (v->text_of)
		return v->text_of(v->text_ud, f);
	const struct field *ff = f->k == F_ENTRY ? entry_sub(store_get(c->s, f->entry), f->sub) : NULL;
	return ff && ff->value ? ff->value : "";
}

/* wrapped lines of t in w: byte start and length of each */
struct tline {
	int s, n;
};
static int wrap(struct ctx *c, const UiFont *f, const char *t, double w, struct tline *out, int max)
{
	int n = 0, len = (int)strlen(t), ls = 0, last = -1;
	char buf[1024];
	for (int i = 0; i <= len && n < max; i++) {
		if (i < len && t[i] != ' ' && t[i] != '\n')
			continue;
		double h;
		snprintf(buf, sizeof buf, "%.*s", i - ls, t + ls);
		if (ui_text_w(&c->g, f, buf, &h) > w && last > ls) {
			out[n++] = (struct tline){ ls, last - ls };
			ls = last + 1;
			i = last;
			last = -1;
			continue;
		}
		if (i == len || t[i] == '\n') {
			out[n++] = (struct tline){ ls, i - ls };
			ls = i + 1;
			last = -1;
		} else
			last = i;
	}
	if (!n)
		out[n++] = (struct tline){ 0, 0 };
	return n;
}

/* one field as an ordinary text field: the focused one with a caret and a 2 px edge, the
 * others (the whole view) as quiet boxes over their row */
static void tfield(struct ctx *c, const struct vfield *f, const char *t, int cur, bool focus)
{
	const struct look *L = c->L;
	if (!c->paint)
		return;
	if (f->h <= 48) { /* a row */
		if (focus) {
			double x = f->x - 5, y = f->y - 3, w = f->w + 9;
			box(c, x, y, w, 40, L->ui_r, 2, L->ink, L->paper);
			UiFont pf = plex(16);
			caret_text(c, &pf, t, cur, x + 10, y + 20, w - 20, L->ink);
		} else {
			double x = f->x - 1, y = f->y + f->h - 4 - 26, w = f->w + 1;
			box(c, x, y, w, 26, 4, 1, L->ink, L->paper);
			UiFont pf = plex(14);
			caret_text(c, &pf, t, -1, x + 8, y + 13, w - 16, L->ink);
		}
		return;
	}
	/* a tall field (notes, an event's title): the text wrapped in a box over it */
	double x = f->x - 4, y = f->y + 2, w = f->w + 4, h = fmax(40, f->h - 4);
	box(c, x, y, w, h, focus ? L->ui_r : 4, focus ? 2 : 1, L->ink, L->paper);
	UiFont pf = plex(focus ? 16 : 14);
	struct tline ln[64];
	int n = wrap(c, &pf, t, w - 20, ln, 64);
	double lh = pf.px * 1.45, ty = y + 8;
	cairo_save(c->cr);
	cairo_rectangle(c->cr, x, y, w, h);
	cairo_clip(c->cr);
	for (int i = 0; i < n; i++) {
		char b[1024];
		snprintf(b, sizeof b, "%.*s", ln[i].n, t + ln[i].s);
		bool here = cur >= ln[i].s && cur <= ln[i].s + ln[i].n && (i == n - 1 || cur < ln[i + 1].s);
		caret_text(c, &pf, b, here ? cur - ln[i].s : -1, x + 10, ty + lh / 2, w - 20, L->ink);
		ty += lh;
	}
	cairo_restore(c->cr);
}

static const struct vfield *field_named(const struct view *v, const char *name)
{
	for (int i = 0; name && *name && i < v->nf; i++)
		if (!strcmp(v->f[i].name, name))
			return &v->f[i];
	return NULL;
}

static void text_fields(struct ctx *c)
{
	struct view *v = c->v;
	const struct vfield *fo = field_named(v, v->tm_name);
	if (v->tm == 2)
		for (int i = 0; i < v->nf; i++)
			if (v->f[i].k == F_ENTRY && &v->f[i] != fo)
				tfield(c, &v->f[i], text_of(c, &v->f[i]), -1, false);
	if (fo)
		tfield(c, fo, v->tm_text ? v->tm_text : text_of(c, fo), v->tm_cur, true);
}

static const char *kind_word(enum ekind k)
{
	return k == E_EVENT ? "event" : k == E_TODO ? "to-do" : k == E_NOTE ? "note" : k == E_PRIO ? "priority" : "habit";
}

static UiFont btn_font(const struct look *L); /* planner.css .pl-btn (with the editor's buttons) */

/* a switch (planner.css .pl-tog) at x, cy */
static void tog(struct ctx *c, double x, double cy, bool on)
{
	const struct look *L = c->L;
	double r = L->voice == V_EDITORIAL ? 0 : 11;
	box(c, x, cy - 11, 40, 22, r, 1.5, L->ink, on ? L->ink : 0);
	box(c, x + (on ? 22 : 4), cy - 7, 14, 14, L->voice == V_EDITORIAL ? 0 : 7, 0, 0, on ? L->paper : L->soft);
}

/* the bar above the keyboard (.pl-tbar): what is being edited, the field's two switches and
 * the way out; at the foot of what the keyboard leaves */
static void tbar(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double W = v->W, y = vis_of(v) - TBAR, cy = y + TBAR / 2.0;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, W, TBAR);
		lk_fill(c->cr, L->paper);
		lk_rule(c->cr, 0, y, W, L->row_w, L->row_c, L->row_dash);
	}
	UiFont bf = btn_font(L), qf = bf;
	char lbl[96];
	const struct vfield *fo = field_named(v, v->tm_name);
	double x = W - 10;
	if (v->tm == 2) {
		snprintf(lbl, sizeof lbl, "Text mode for the whole view: every field shows its text");
		double w = tw(c, &bf, "Back to ink") + 28;
		x -= w;
		box(c, x, cy - 20, w, 40, L->ui_r, L->edge, L->ink, L->ink);
		tc(c, &bf, "Back to ink", x + w / 2, cy, 1, 0, L->paper);
		hitf(c, x, cy - 20, w, 40, A_TM_DONE, 0);
	} else {
		struct entry *e = fo && fo->k == F_ENTRY ? store_get(c->s, fo->entry) : NULL;
		snprintf(lbl, sizeof lbl, "Text mode for this %s", kind_word(e ? e->kind : fo ? fo->ekind : E_NOTE));
		double w = tw(c, &bf, "Done") + 28;
		x -= w;
		box(c, x, cy - 20, w, 40, L->ui_r, L->edge, L->ink, L->ink);
		tc(c, &bf, "Done", x + w / 2, cy, 1, 0, L->paper);
		hitf(c, x, cy - 20, w, 40, A_TM_DONE, 0);
		static const char *const NAME[] = { "Set in Autograph", "Neaten" };
		for (int i = 0; i < 2; i++) {
			bool on = i == 0 ? v->tm_autograph : v->tm_neaten;
			double tw_ = tw(c, &qf, NAME[i]), bw = 6 + 40 + 8 + tw_ + 6;
			x -= 8 + bw;
			tog(c, x + 6, cy, on);
			tc(c, &qf, NAME[i], x + 6 + 40 + 8, cy, 0, 0, L->ink);
			if (c->paint)
				lk_rule(c->cr, x + 6 + 40 + 8, cy + 9, tw_, 1, L->ink, 0);
			hitf(c, x, cy - 20, bw, 40, i == 0 ? A_TM_AUTOGRAPH : A_TM_NEATEN, 0);
		}
	}
	UiFont lf = fam(L->mono, 400, 0, 12);
	if (L->voice != V_WHITE)
		lf = fam(L->text, 400, 1, 14);
	tc(c, &lf, lbl, 10, cy, 0, fmax(40, x - 18), L->soft);
	hitf(c, 0, y, W, TBAR, A_NONE, 0); /* the bar takes its taps: after its own targets */
}

/* ---- the settings (board cog.settings) ---- */

static const char *strength_name(float s)
{
	return s < 0.5f ? "Light" : s < 0.85f ? "Medium" : "Strong";
}

static void settings(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double sh = v->H - BAR - 20, y = v->H - sh, W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, W, sh);
		lk_fill(c->cr, L->paper);
		if (L->voice == V_EDITORIAL) {
			lk_rule(c->cr, 0, y, W, 1, L->ink, 0);
			lk_rule(c->cr, 0, y + 2, W, 1, L->ink, 0);
		} else
			lk_rule(c->cr, 0, y, W, 1.5, L->ink, 0);
	}
	UiFont h2 = fpx(L->h1, 21);
	tc(c, &h2, "Planner settings", 16, y + 14 + 20, 0, W - 140, L->ink);
	UiFont bf = btn_font(L);
	double bw = tw(c, &bf, "Close") + 28;
	box(c, W - 16 - bw, y + 14, bw, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, "Close", W - 16 - bw / 2, y + 34, 1, 0, L->ink);
	hit(c, W - 16 - bw, y + 14, bw, 40, A_CLOSE, 0, 0);
	UiFont lf = fam(L->text, 400, 0, 14);
	tc(c, &lf, "Ink is always kept as written; these change only how it is shown.", 16, y + 64 + 9, 0, W - 32, L->soft);
	double yy = y + 64 + 28;
	/* the agenda's hours: of this template's first agenda */
	char hours[64] = "", hend[16] = "";
	for (int i = 0; i < v->g.n && !hours[0]; i++)
		if (v->g.p[i].n->wg == W_AGENDA || v->g.p[i].n->wg == W_CAGENDA) {
			snprintf(hours, sizeof hours, "per template; this one: %02d:00–%02d:00", v->g.p[i].n->from, v->g.p[i].n->to);
			snprintf(hend, sizeof hend, "%02d–%02d", v->g.p[i].n->from, v->g.p[i].n->to);
		}
	if (!hours[0])
		snprintf(hours, sizeof hours, "per template; this one has no agenda");
	char nsub[96];
	snprintf(nsub, sizeof nsub, "straighter lines, a steady slant, even sizes · %s", strength_name(c->s->strength));
	struct {
		const char *head, *t, *sub, *end;
		int tog; /* 1 Neaten, 2 Set in Autograph */
		enum act a;
	} it[] = {
		{ "Ink in every field", "Neaten", nsub, "", 1, A_SET_NEATEN },
		{ NULL, "Set in Autograph", "each word that was read, in your font", "", 2, A_SET_AUTOGRAPH },
		{ NULL, "One field’s own choice wins", "set it in that field’s text mode", "", 0, A_NONE },
		{ "Agenda", "Hours shown", hours, hend, 0, A_NONE },
		{ NULL, "Steps", "moving and stretching an event", "30 min", 0, A_NONE },
		{ "From the system", "First day of the week, date and time", "follow Settings › Language and region", "Monday", 0, A_NONE },
	};
	UiFont kf = L->label, tf = fam(L->text, 400, 0, L->voice == V_STATIONERY ? 18 : 16), sf = fam(L->text, 400, 0, 12.5f),
	       ef = fam(L->mono, 400, 0, 12);
	if (L->voice == V_WHITE)
		kf = fam(L->mono, 600, 0, 11), kf.tcase = UI_CASE_UPPER, kf.ls = 0.08f;
	for (size_t i = 0; i < sizeof it / sizeof *it; i++) {
		if (it[i].head) {
			double kh;
			ui_text_w(&c->g, &kf, it[i].head, &kh);
			if (c->paint)
				ui_text(&c->g, &kf, it[i].head, 16, yy + 10, 0, W - 32, L->ink, &kh);
			yy += 10 + kh + 2;
		} else
			rule(c, 16, yy, W - 32);
		double h1, h2, ew = *it[i].end ? tw(c, &ef, it[i].end) + 12 : 0;
		double tw_ = W - 32 - (it[i].tog ? 52 : 0) - ew;
		ui_text_w(&c->g, &tf, it[i].t, &h1);
		UiGc mg = c->g;
		mg.paint = 0;
		h2 = ui_text_wrap(&mg, &sf, it[i].sub, 0, 0, tw_, 2, 0, L->soft);
		double ih = fmax(48, 6 + h1 + 2 + h2 + 6), ty = yy + (ih - h1 - 2 - h2) / 2;
		if (c->paint) {
			ui_text(&c->g, &tf, it[i].t, 16, ty, 0, tw_, L->ink, &h1);
			if (it[i].tog == 1 && c->s->neaten) {
				/* the strength (.pl-str): the row's last word, in ink, dotted beneath */
				const char *sw = strength_name(c->s->strength);
				double hs, ww = ui_text_w(&c->g, &sf, sw, &hs), all = ui_text_w(&c->g, &sf, it[i].sub, &hs);
				double sx = 16 + all - ww, sy = ty + h1 + 2;
				char pre[96];
				snprintf(pre, sizeof pre, "%.*s", (int)(strlen(it[i].sub) - strlen(sw)), it[i].sub);
				ui_text(&c->g, &sf, pre, 16, sy, 0, 0, L->soft, &hs);
				ui_text(&c->g, &sf, sw, sx, sy, 0, 0, L->ink, &hs);
				lk_rule(c->cr, sx, sy + hs, ww, 1, L->ink, 2);
			} else
				ui_text_wrap(&c->g, &sf, it[i].sub, 16, ty + h1 + 2, tw_, 2, 0, L->soft);
		}
		if (it[i].tog) {
			tog(c, W - 16 - 40, yy + ih / 2, it[i].tog == 1 ? c->s->neaten : c->s->autograph);
			hit(c, W - 16 - 56, yy, 56, ih, it[i].a, 0, 0);
			if (it[i].tog == 1) /* the row's text: the strength, Light, Medium, Strong */
				hit(c, 16, yy, W - 32 - 56, ih, A_SET_STRENGTH, 0, 0);
		}
		if (*it[i].end)
			tc(c, &ef, it[i].end, W - 16, yy + ih / 2, 2, 0, L->soft);
		yy += ih;
	}
	hit(c, 0, y, W, sh, A_NONE, 0, 0); /* the sheet takes its taps: after its own targets */
}

/* ---- a cut title, whole (FR-018): a card over its block ---- */

static void title_card(struct ctx *c)
{
	struct view *v = c->v;
	struct entry *e = store_get(c->s, v->title);
	const struct vfield *f = e ? view_field_of(v, e->id) : NULL;
	if (e && (f || v->back == OV_HIDDEN)) {
		double r, b;
		field_extent(&e->f, &r, &b);
		double fx = f ? f->x : 16, fy = f ? f->y : v->H / 3, fw = f ? f->w : v->W - 32;
		double w = fmin(v->W - 16, fmax(fw, r + 20)), h = fmin(v->H - 16, b + 16), x = fmin(fx, v->W - 8 - w),
		       y = fmax(8, fmin(fy, v->H - 8 - h));
		pop_frame(c, x, y, w, h);
		if (c->paint) {
			cairo_save(c->cr);
			cairo_rectangle(c->cr, x, y, w, h);
			cairo_clip(c->cr);
			ink(c, &e->f, x + 8, y + 8, 1);
			cairo_restore(c->cr);
		}
	}
	hit(c, 0, 0, v->W, v->H, A_CLOSE, 0, 0); /* a tap anywhere closes it */
}

/* ---- Move: another day, week or month for an entry (from the hidden list) ---- */

static void sheet_move(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct entry *e = store_get(c->s, v->move);
	double sh = round((v->H - BAR) * 0.66), y = v->H - sh, W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, W, sh);
		lk_fill(c->cr, L->paper);
		lk_rule(c->cr, 0, y, W, 1.5, L->ink, 0);
	}
	enum pkind k = e ? e->per.kind : P_DAY;
	const char *kn = k == P_DAY ? "day" : k == P_WEEK ? "week" : "month";
	char t[64];
	snprintf(t, sizeof t, "Move to another %s", kn);
	UiFont h2 = fpx(L->h1, 21), bf = btn_font(L);
	tc(c, &h2, t, 16, y + 14 + 20, 0, W - 140, L->ink);
	double bw = tw(c, &bf, "Close") + 28;
	box(c, W - 16 - bw, y + 14, bw, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, "Close", W - 16 - bw / 2, y + 34, 1, 0, L->ink);
	hit(c, W - 16 - bw, y + 14, bw, 40, A_CLOSE, 0, 0);
	if (!e) {
		hit(c, 0, y, W, sh, A_NONE, 0, 0);
		return;
	}
	if (e->kind != E_MARK)
		ink_fit(c, &e->f, 16, y + 62, W - 32, 34);
	/* the month (the year, for a month's entry) and its arrows */
	int yr, m, d;
	day_to_ymd(v->move_at, &yr, &m, &d);
	char head_[48];
	if (k == P_MONTH)
		snprintf(head_, sizeof head_, "%d", yr);
	else
		snprintf(head_, sizeof head_, "%s %d", MON[m - 1], yr);
	double hy = y + 112;
	box(c, 16, hy, 40, 40, L->ui_r, L->edge, L->ink, L->paper);
	icon(c, IC_CHEV_L, 36, hy + 20, 20, L->ink);
	hit(c, 16, hy, 40, 40, A_MOVE_NAV, -1, 0);
	box(c, W - 56, hy, 40, 40, L->ui_r, L->edge, L->ink, L->paper);
	icon(c, IC_CHEV_R, W - 36, hy + 20, 20, L->ink);
	hit(c, W - 56, hy, 40, 40, A_MOVE_NAV, 1, 0);
	tc(c, &(UiFont){ L->mono, 600, 0, 14, 0, 0, UI_CASE_NONE }, head_, W / 2, hy + 20, 1, 0, L->ink);
	double gy = hy + 56, gx = 16, gw = W - 32;
	UiFont cf = fam(L->mono, 500, 0, 13);
	int cur = e->per.start;
	if (k == P_MONTH) { /* twelve months, three to a row */
		for (int i = 0; i < 12; i++) {
			double cw = gw / 3, cx = gx + (i % 3) * cw, cy = gy + (i / 3) * 44;
			int day = day_from_ymd(yr, i + 1, 1);
			box(c, cx + 2, cy + 2, cw - 4, 40, L->ui_r, day == cur ? 2.5 : 1, L->ink, L->paper);
			tc(c, &cf, MON[i], cx + cw / 2, cy + 22, 1, 0, L->ink);
			hit(c, cx, cy, cw, 44, A_MOVE_TO, day, 0);
		}
	} else if (k == P_WEEK) { /* the weeks that start in the month */
		int first = day_from_ymd(yr, m, 1), n = days_in_month(yr, m), row = 0;
		for (int dd = first; dd < first + n; dd++) {
			if (period_of(P_WEEK, dd).start != dd)
				continue;
			int a1, a2, b1, b2, y1, y2;
			day_to_ymd(dd, &y1, &a1, &b1);
			day_to_ymd(dd + 6, &y2, &a2, &b2);
			char w[64];
			snprintf(w, sizeof w, "week %d · %d %.3s – %d %.3s", week_number(dd), b1, MON[a1 - 1], b2, MON[a2 - 1]);
			double cy = gy + row * 44;
			box(c, gx, cy + 2, gw, 40, L->ui_r, dd == cur ? 2.5 : 1, L->ink, L->paper);
			tc(c, &cf, w, gx + 12, cy + 22, 0, 0, L->ink);
			hit(c, gx, cy, gw, 44, A_MOVE_TO, dd, 0);
			row++;
		}
	} else { /* the month's days, Monday first */
		double cw = gw / 7;
		for (int i = 0; i < 7; i++)
			tc(c, &(UiFont){ L->mono, 400, 0, 11, 0, 0, UI_CASE_NONE }, DOW3[i], gx + i * cw + cw / 2, gy + 8, 1, 0, L->soft);
		int first = day_from_ymd(yr, m, 1), n = days_in_month(yr, m), off = day_weekday(first);
		for (int i = 0; i < n; i++) {
			int col = (off + i) % 7, row = (off + i) / 7, day = first + i;
			double cx = gx + col * cw, cy = gy + 20 + row * 40;
			char num[8];
			snprintf(num, sizeof num, "%d", i + 1);
			if (day == cur)
				box(c, cx + cw / 2 - 16, cy + 2, 32, 32, 16, 2, L->ink, 0);
			tc(c, &cf, num, cx + cw / 2, cy + 18, 1, 0, L->ink);
			hit(c, cx, cy, cw, 40, A_MOVE_TO, day, 0);
		}
	}
	hit(c, 0, y, W, sh, A_NONE, 0, 0); /* the sheet takes its taps: after its own targets */
}

/* ---- the template picker (board templates.picker) ---- */

static void thumb(struct ctx *c, const struct tmpl *t, double x, double y, double w, double h, bool use)
{
	const struct look *L = c->L;
	box(c, x, y, w, h, 2, use ? 2.5 : 1.5, L->ink, L->paper);
	struct grid g;
	grid_solve(t, 702 - 24, 936 - 100 - 24, false, NULL, NULL, &g);
	double kx = (w - 8) / (702 - 24), ky = (h - 8) / (936 - 124);
	for (int i = 0; i < g.n; i++) {
		double bx = x + 4 + g.p[i].x * kx, by = y + 4 + g.p[i].y * ky, bw = fmax(2, g.p[i].w * kx - 2), bh = fmax(2, g.p[i].h * ky - 2);
		box(c, bx, by, bw, bh, 1, 1, L->ink, 0);
		if (g.p[i].n->wg == W_AGENDA || g.p[i].n->wg == W_CAGENDA)
			box(c, bx + 1, by + 1, 5, bh - 2, 0, 0, 0, L->rule);
	}
}

/* the picker's New, Copy and Edit (board templates.picker): they open the template editor on
 * the ringed template (FR-049) */
static double ed_btn(struct ctx *c, double x, double y, const char *label, const char *ic, int style, ui_col fg, ui_col bg,
		     enum ed_op op, int arg, int align);
static const char *const IC_PLUS = "M12 5v14 M5 12h14";
static const char *const IC_COPY = "M8 8h12v12H8z M4 16V4h12";
static const char *const IC_CAL = "M4 6h16v14H4z M4 10h16 M8 3v5 M16 3v5";

static void picker_edit_acts(struct ctx *c, double y)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	int nh = v->nh;
	double x = 16;
	static const struct { const char *t; int arg; } A[] = { { "New", 1 }, { "Copy", 2 }, { "Edit", 0 }, { "Plan ahead", -1 } };
	const char *ic[] = { IC_PLUS, IC_COPY, IC_EDIT, IC_CAL };
	for (int i = 0; i < 4; i++) {
		x += ed_btn(c, x, y, A[i].t, ic[i], 0, L->ink, L->paper, ED_SELECT, 0, 0) + 8;
		if (!c->paint && v->nh > nh) { /* the button's target opens the editor, or the year */
			struct vhit *h = &v->hit[v->nh - 1];
			h->a = A[i].arg < 0 ? A_YEAR : A_EDIT, h->arg = A[i].arg < 0 ? 0 : A[i].arg, h->id = 0;
		}
		nh = v->nh;
	}
}

/* a period's short name: "Thu 11 Mar", "week 10", "March" */
static void per_name(enum pkind k, int day, char *o, size_t n)
{
	int y, m, d;
	struct period p = period_of(k, day);
	day_to_ymd(p.start, &y, &m, &d);
	if (k == P_DAY)
		snprintf(o, n, "%s %d %.3s", DOW3[day_weekday(p.start)], d, MON[m - 1]);
	else if (k == P_WEEK)
		snprintf(o, n, "week %d", week_number(p.start));
	else
		snprintf(o, n, "%s", MON[m - 1]);
}

/* first to last (both periods of kind k): "11 Mar to 17 Mar", "weeks 12 to 18", "March to June" */
static void range_name(enum pkind k, int first, int last, char *o, size_t n)
{
	int y, m, d, y2, m2, d2;
	day_to_ymd(period_of(k, first).start, &y, &m, &d);
	day_to_ymd(period_of(k, last).start, &y2, &m2, &d2);
	if (k == P_DAY)
		snprintf(o, n, "%d %.3s to %d %.3s", d, MON[m - 1], d2, MON[m2 - 1]);
	else if (k == P_WEEK)
		snprintf(o, n, "weeks %d to %d", week_number(period_of(k, first).start), week_number(period_of(k, last).start));
	else
		snprintf(o, n, "%s to %s", MON[m - 1], MON[m2 - 1]);
}

/* the choice's name (view.h): the picker's rows and the year's list */
void view_choice_name(enum pkind k, int from, int to, int shown_year, char *o, size_t n)
{
	int y, m, d, y2, m2, d2;
	struct period a = period_of(k, from);
	day_to_ymd(a.start, &y, &m, &d);
	char yb[16] = "";
	if (y != shown_year)
		snprintf(yb, sizeof yb, " %d", y);
	char one[48];
	per_name(k, from, one, sizeof one);
	if (to >= 0 && period_of(k, to).start == a.start) { /* that period only */
		if (k == P_WEEK && *yb)
			snprintf(o, n, "%s, %d", one, y);
		else
			snprintf(o, n, "%s%s", one, yb);
		return;
	}
	if (to < 0) { /* on from there: a week by its number and its month */
		if (k == P_WEEK)
			snprintf(o, n, "from week %d on · from %.3s%s", week_number(a.start), MON[m - 1], yb);
		else
			snprintf(o, n, "from %s%s on", one, yb);
		return;
	}
	struct period b = period_of(k, to);
	day_to_ymd(b.start, &y2, &m2, &d2);
	char yb2[16] = "";
	if (y2 != shown_year)
		snprintf(yb2, sizeof yb2, " %d", y2);
	if (k == P_DAY)
		snprintf(o, n, "%d %.3s%s to %d %.3s%s", d, MON[m - 1], yb, d2, MON[m2 - 1], yb2);
	else if (k == P_WEEK) {
		/* each week in the month it starts in, as the year's rows hold it */
		char mo[40];
		if (m == m2 && y == y2)
			snprintf(mo, sizeof mo, "%.3s%s", MON[m - 1], yb);
		else
			snprintf(mo, sizeof mo, "%.3s%s–%.3s%s", MON[m - 1], yb, MON[m2 - 1], yb2);
		snprintf(o, n, "weeks %d–%d · %s", week_number(a.start), week_number(b.start), mo);
	} else
		snprintf(o, n, "%s%s to %s%s", MON[m - 1], yb, MON[m2 - 1], yb2);
}

/* the template a period uses, by id (the kind's default when none valid is chosen) */
static const char *uses_id(const struct store *s, struct period p)
{
	const char *id = store_template(s, p, NULL);
	const struct tmpl *t = id && *id ? tmpl_find(id) : NULL;
	if (!t || t->kind != p.kind)
		t = tmpl_default(p.kind);
	return t->id;
}

/* where a template is used (the picker's rows): "the default · 143 days", "28 days",
 * "weeks 1–13 · Jan–Mar", "from week 14 on · from Apr", "week 20 only", "not used yet". Days count the periods
 * with entries or a choice of their own that use it */
static void usage(struct ctx *c, const struct tmpl *t, enum pkind k, char *o, size_t n)
{
	const struct store *s = c->s;
	bool def = !strcmp(t->id, tmpl_default(k)->id);
	char parts[200] = "", one[48] = "";
	int singles = 0, vy, vm, vd;
	day_to_ymd(c->v->per.start, &vy, &vm, &vd);
	for (int i = 0; i < s->nc; i++) {
		const struct choice *ch = &s->c[i];
		if (ch->from.kind != k || strcmp(ch->tmpl, t->id))
			continue;
		char b[64];
		if (ch->to == ch->from.start) {
			singles++;
			view_choice_name(k, ch->from.start, ch->to, vy, one, sizeof one);
			continue;
		}
		view_choice_name(k, ch->from.start, ch->to, vy, b, sizeof b);
		snprintf(parts + strlen(parts), sizeof parts - strlen(parts), "%s%s", *parts ? " · " : "", b);
	}
	/* the periods that use it: those with entries, and those with a choice of their own */
	static int seen[4096];
	int ns = 0, count = 0;
	for (int i = 0; i < s->n + s->nc && ns < 4096; i++) {
		struct period p;
		if (i < s->n) {
			if (s->e[i]->per.kind != k)
				continue;
			p = s->e[i]->per;
		} else {
			const struct choice *ch = &s->c[i - s->n];
			if (ch->from.kind != k || ch->to != ch->from.start)
				continue;
			p = ch->from;
		}
		bool dup = false;
		for (int j = 0; j < ns && !dup; j++)
			dup = seen[j] == p.start;
		if (dup)
			continue;
		seen[ns++] = p.start;
		count += !strcmp(uses_id(s, p), t->id);
	}
	const char *unit = k == P_DAY ? "day" : k == P_WEEK ? "week" : "month";
	char cnt[32] = "";
	if (count)
		snprintf(cnt, sizeof cnt, "%d %s%s", count, unit, count == 1 ? "" : "s");
	if (def)
		snprintf(o, n, "the default%s%s%s%s", *parts ? " · " : "", parts, *cnt ? " · " : "", cnt);
	else if (*parts)
		snprintf(o, n, "%s", parts);
	else if (singles == 1 && count <= 1)
		snprintf(o, n, "%s only", one);
	else if (count)
		snprintf(o, n, "%s", cnt);
	else
		snprintf(o, n, "not used yet");
}


/* the scopes (planner.css .pl-scope): what a choice applies to */
static double scopes(struct ctx *c, double y, double *xend)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	char main[64], sub[96];
	title_of(v, main, sizeof main, sub, sizeof sub);
	const char *kn = v->per.kind == P_DAY ? "day" : v->per.kind == P_WEEK ? "week" : "month";
	char only[80], range[80], from[96], f[48], pn[64];
	/* the period by its name: "Thursday 11", "week 10", "March" */
	if (v->per.kind == P_DAY)
		snprintf(pn, sizeof pn, "%s", main);
	else
		per_name(v->per.kind, v->per.start, pn, sizeof pn);
	snprintf(only, sizeof only, "Only %.60s", pn);
	if (v->rg_n == 2) {
		range_name(v->per.kind, v->rg0, v->rg1, range, sizeof range);
		range[0] = (char)toupper((unsigned char)range[0]);
	} else
		snprintf(range, sizeof range, "A range of %ss", kn);
	if (v->from_set) {
		per_name(v->per.kind, v->from_day, f, sizeof f);
		snprintf(from, sizeof from, "From %s on", f);
	} else
		snprintf(from, sizeof from, "From %s on", pn);
	const char *sc[] = { only, range, from, "The default" };
	UiFont lf = fam(L->text, 400, 0, 14);
	double x = 16;
	for (int i = 0; i < 4; i++) {
		double w = tw(c, &lf, sc[i]) + 24;
		if (x + w > v->W - 16 && x > 16) /* wraps */
			x = 16, y += 38 + 8;
		bool on = v->scope == i;
		box(c, x, y, w, 38, L->ui_r, 1.5, L->ink, on ? L->ink : 0);
		tc(c, &lf, sc[i], x + 12, y + 19, 0, 0, on ? L->paper : L->ink);
		hit(c, x, y, w, 38, A_SCOPE, i, 0);
		x += w + 8;
	}
	if (xend)
		*xend = x - 8;
	return y + 38 + 12;
}

/* ---- the year planned ahead (board templates.year) ---- */

/* the periods of kind k that start in month m of year yr: their first days */
static int cells_of(enum pkind k, int yr, int m, int *out, int max)
{
	int first = day_from_ymd(yr, m, 1), n = 0, len = days_in_month(yr, m);
	if (k == P_MONTH) {
		out[0] = first;
		return 1;
	}
	for (int d = first; d < first + len && n < max; d++)
		if (k == P_DAY || period_of(P_WEEK, d).start == d)
			out[n++] = d;
	return n;
}

/* a cell of the year (.pl-ym .w, .pl-ydm .w): its template's letter and fill, the heavier
 * edge for a period's own choice, today ringed, dotted beneath while it is being picked */
static void year_cell(struct ctx *c, struct period p, double x, double y, double w, double h, double r, const UiFont *cf,
		      const struct tmpl *const *ts, int nts, const char *letter, double gap)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	enum pkind k = p.kind;
	int how = 3;
	store_template(c->s, p, &how);
	const char *id = uses_id(c->s, p);
	int j = 0;
	while (j < nts && strcmp(ts[j]->id, id))
		j++;
	char lt[2] = { j < nts && letter[j] ? letter[j] : '?', 0 };
	int f = (lt[0] - 'A') % 3;
	box(c, x, y, w, h, r, how == 0 ? 2.5 : 1, L->ink, f == 0 ? L->paper : f == 1 ? L->highlight : L->ink);
	if (w >= 8)
		tc(c, cf, lt, x + w / 2, y + h / 2, 1, 0, f == 2 ? L->paper : L->ink);
	if (v->today >= p.start && v->today <= period_end(p) && c->paint) /* is-now: an outline outside */
		lk_stroke_box(c->cr, x - 2, y - 2, w + 4, h + 4, r + 1, 2, L->ink);
	bool sel = (v->rg_n == 1 && p.start == period_of(k, v->rg0).start) ||
		   (v->rg_n == 2 && p.start >= period_of(k, v->rg0).start && p.start <= period_of(k, v->rg1).start);
	if (sel && c->paint) /* being chosen: dotted beneath */
		lk_rule(c->cr, x, y + h + (k == P_DAY ? 1 : 2), w, 2, L->ink, 2);
	hit(c, x - gap / 2, y - gap / 2, w + gap, h + gap, A_CELL, p.start, 0);
}

static void year(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double W = v->W;
	enum pkind k = v->per.kind;
	const char *kn = k == P_DAY ? "day" : k == P_WEEK ? "week" : "month";
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, v->H);
		lk_fill(c->cr, L->paper);
	}
	char t[96];
	snprintf(t, sizeof t, "Plan the %ss ahead", kn);
	UiFont hf = fpx(L->h1, 21);
	tc(c, &hf, t, 16, 14 + 21, 0, W - 140, L->ink);
	UiFont bf = btn_font(L);
	double bw = tw(c, &bf, "Close") + 28;
	box(c, W - 16 - bw, 14, bw, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, "Close", W - 16 - bw / 2, 34, 1, 0, L->ink);
	hit(c, W - 16 - bw, 14, bw, 40, A_CLOSE, 0, 0);
	char lead[240];
	const struct tmpl *pend = v->rg_tmpl[0] ? tmpl_find(v->rg_tmpl) : NULL;
	if (pend)
		snprintf(lead, sizeof lead, v->rg_n ? "Now the last %s for %s." : "Tap the first and the last %s for %s.", kn, pend->name);
	else
		snprintf(lead, sizeof lead,
			 "Each %s shows the template planned for it. A single %s’s own choice wins over a range; change any %s at any "
			 "time, past %ss too.",
			 kn, kn, kn, kn);
	UiFont lf = fam(L->text, 400, 0, 14);
	UiGc mg = c->g;
	mg.paint = 0;
	double lh = ui_text_wrap(&mg, &lf, lead, 16, 64, W - 32, 3, 1.4, L->soft);
	if (c->paint)
		ui_text_wrap(&c->g, &lf, lead, 16, 64, W - 32, 3, 1.4, L->soft);
	double sx;
	double y = scopes(c, 64 + lh + 12, &sx);
	/* the year shown and its arrows (.pl-ynav): ‹ 2027 ›; turned, they go to the end of the
	 * scopes' row (.pl-ynav.is-in) when they fit there, so that the twelve months and the
	 * choices below them fit the window */
	const double NAVW = 40 + 8 + 56 + 8 + 40;
	bool turned = W > v->H, nav_in = turned && sx + 14 + NAVW <= W - 16;
	int yr, m0, d0;
	day_to_ymd(v->per.start, &yr, &m0, &d0);
	yr += v->year_off;
	int ys = day_from_ymd(yr, 1, 1), ye = day_from_ymd(yr, 12, 31);
	char yt[8];
	snprintf(yt, sizeof yt, "%d", yr);
	double ny = nav_in ? y - 12 - 38 - 1 : y, /* centred on the chips’ row */
	        nx = nav_in ? W - 16 - NAVW : 16;
	box(c, nx, ny, 40, 40, L->ui_r, L->edge, L->ink, L->paper);
	icon(c, IC_CHEV_L, nx + 20, ny + 20, 20, L->ink);
	hit(c, nx, ny, 40, 40, A_YEAR_NAV, -1, 0);
	box(c, W - 56, ny, 40, 40, L->ui_r, L->edge, L->ink, L->paper);
	icon(c, IC_CHEV_R, W - 36, ny + 20, 20, L->ink);
	hit(c, W - 56, ny, 40, 40, A_YEAR_NAV, 1, 0);
	tc(c, &(UiFont){ L->mono, 600, 0, 14, 0, 0, UI_CASE_NONE }, yt, (nx + W - 16) / 2, ny + 20, 1, 0, L->ink);
	if (!nav_in)
		y += 40 + 10;
	/* the letters, in the picker's order, the same in every year: the default and every
	 * template a choice of this kind names */
	const struct tmpl *ts[64];
	int nts = tmpl_list(k, ts, 64);
	bool named[64] = { 0 }, used[64] = { 0 };
	const char *def = tmpl_default(k)->id;
	for (int j = 0; j < nts; j++) {
		named[j] = !strcmp(ts[j]->id, def);
		for (int i = 0; i < c->s->nc && !named[j]; i++)
			named[j] = c->s->c[i].from.kind == k && !strcmp(c->s->c[i].tmpl, ts[j]->id);
	}
	char letter[64];
	int nl = 0;
	for (int j = 0; j < nts; j++)
		letter[j] = named[j] ? (char)('A' + nl++ % 26) : 0;
	/* the key names the templates the shown year uses */
	for (int m = 1; m <= 12; m++) {
		int cl[32], nc = cells_of(k, yr, m, cl, 32);
		for (int i = 0; i < nc; i++) {
			const char *id = uses_id(c->s, period_of(k, cl[i]));
			for (int j = 0; j < nts; j++)
				if (!strcmp(ts[j]->id, id))
					used[j] = true;
		}
	}
	/* the key (.pl-ykey) */
	UiFont kf = fam(L->text, 400, 0, 13), cf = fam(L->mono, 600, 0, 11), kl = fam(L->mono, 600, 0, 10);
	double x = 16;
	y += 6;
	for (int j = 0; j < nts; j++) {
		if (!letter[j] || !used[j])
			continue;
		double w = 22 + 6 + tw(c, &kf, ts[j]->name);
		if (x + w > W - 16 && x > 16)
			x = 16, y += 22;
		int f = (letter[j] - 'A') % 3;
		box(c, x, y, 22, 16, 3, 1, L->ink, f == 0 ? L->paper : f == 1 ? L->highlight : L->ink);
		char lt[2] = { letter[j], 0 };
		tc(c, &kl, lt, x + 11, y + 8, 1, 0, f == 2 ? L->paper : L->ink);
		tc(c, &kf, ts[j]->name, x + 28, y + 8, 0, 0, L->ink);
		x += w + 14;
	}
	y += 16 + 10;
	if (k == P_DAY) {
		/* the days (.pl-yd): a wall calendar, a month each, three to a row upright and four
		 * turned, Monday first */
		UiFont mf = { L->mono, 500, 0, 12, 0, 0, UI_CASE_NONE }, hf9 = fam(L->mono, 500, 0, 9), df = fam(L->mono, 600, 0, 9);
		static const char *HD[] = { "M", "T", "W", "T", "F", "S", "S" };
		int per = turned ? 4 : 3;
		double colw = (W - 32 - (per - 1) * 14) / per, dw = (colw - 6 * 3) / 7;
		for (int r = 0; r < 12 / per; r++) {
			double rowh = 0;
			for (int q = 0; q < per; q++) {
				int m = r * per + q + 1;
				double mx = 16 + q * (colw + 14), my = y;
				char mn[4];
				snprintf(mn, sizeof mn, "%.3s", MON[m - 1]);
				tc(c, &mf, mn, mx, my + 6, 0, 0, L->soft);
				double gy = my + 12 + 2 + 2;
				for (int i = 0; i < 7; i++)
					tc(c, &hf9, HD[i], mx + i * (dw + 3) + dw / 2, gy + 5.5, 1, 0, L->soft);
				int first = day_from_ymd(yr, m, 1), lead_ = day_weekday(first), nd = days_in_month(yr, m);
				int rows = (lead_ + nd + 6) / 7;
				for (int dd = 0; dd < nd; dd++) {
					int cell = lead_ + dd;
					double cx = mx + (cell % 7) * (dw + 3), cy = gy + 11 + 3 + (cell / 7) * (15 + 3);
					year_cell(c, period_of(P_DAY, first + dd), cx, cy, dw, 15, 2, &df, ts, nts, letter, 3);
				}
				rowh = fmax(rowh, 12 + 2 + 2 + 11 + rows * (3 + 15));
			}
			y += rowh + 8;
		}
		y -= 8;
	} else {
		/* the months (.pl-year, .pl-ym): a row each, a cell per period; turned, two columns
		 * of six months (January to June, July to December) */
		int cols = turned ? 2 : 1;
		double colw = (W - 32 - (cols - 1) * 14) / cols;
		for (int m = 1; m <= 12; m++) {
			int col = (m - 1) / (12 / cols), row = (m - 1) % (12 / cols);
			double mx = 16 + col * (colw + 14), my = y + row * (34 + 4);
			char mn[4];
			snprintf(mn, sizeof mn, "%.3s", MON[m - 1]);
			tc(c, &(UiFont){ L->mono, 500, 0, 12, 0, 0, UI_CASE_NONE }, mn, mx, my + 17, 0, 34, L->soft);
			int cl[32], nc = cells_of(k, yr, m, cl, 32);
			double cx = mx + 34 + 8, cw = (mx + colw - cx - 3 * (nc - 1)) / (nc ? nc : 1);
			for (int i = 0; i < nc; i++)
				year_cell(c, period_of(k, cl[i]), cx + i * (cw + 3), my + 4, cw, 26, 3, &cf, ts, nts, letter, 3);
		}
		y += (12 / cols) * (34 + 4);
	}
	/* the choices that reach into the shown year (.pl-ranges): each with Change or Remove;
	 * one that started in another year is named with its year */
	y += 4;
	rule(c, 16, y, W - 32);
	UiFont rf = fam(L->text, 400, 0, 14), sf = fam(L->text, 400, 0, 12);
	for (int i = 0; i < c->s->nc && y + 44 <= v->H; i++) {
		const struct choice *ch = &c->s->c[i];
		if (ch->from.kind != k)
			continue;
		int to = ch->to < 0 ? ch->to : period_end(period_of(k, ch->to));
		if (ch->from.start > ye || (to >= 0 && to < ys))
			continue;
		const struct tmpl *tp = tmpl_find(ch->tmpl);
		char a[96], line[200], sub[96];
		bool single = ch->to == ch->from.start;
		view_choice_name(k, ch->from.start, ch->to, yr, a, sizeof a);
		if (single) {
			/* inside a range: a single period's own choice wins over it */
			bool in = false;
			for (int j = 0; j < c->s->nc && !in; j++)
				in = j != i && c->s->c[j].from.kind == k && c->s->c[j].to != c->s->c[j].from.start &&
				     ch->from.start >= c->s->c[j].from.start && (c->s->c[j].to < 0 || ch->from.start <= c->s->c[j].to);
			snprintf(sub, sizeof sub, "that %s only%s", kn, in ? " · wins over the range" : "");
		} else if (ch->to < 0)
			snprintf(sub, sizeof sub, "every %s from a date", kn);
		else
			snprintf(sub, sizeof sub, "a range");
		a[0] = (char)toupper((unsigned char)a[0]);
		snprintf(line, sizeof line, "%s: %s", a, tp ? tp->name : ch->tmpl);
		/* Change, Remove (.pl-btn.is-quiet): the button's type, underlined */
		const char *act = single ? "Remove" : "Change";
		double aw = tw(c, &bf, act);
		tc(c, &rf, line, 16, y + 15, 0, W - 32 - aw - 22, L->ink);
		tc(c, &sf, sub, 16, y + 32, 0, W - 32 - aw - 22, L->soft);
		tc(c, &bf, act, W - 16 - 6 - aw, y + 22, 0, 0, L->ink);
		if (c->paint)
			lk_rule(c->cr, W - 16 - 6 - aw, y + 22 + (L->voice == V_WHITE ? 8 : 10), aw, 1, L->ink, 0);
		hit(c, W - 16 - aw - 12, y, aw + 12, 44, single ? A_RANGE_REMOVE : A_RANGE_CHANGE, i, 0);
		y += 44;
		rule(c, 16, y, W - 32);
	}
	hit(c, 0, 0, W, v->H, A_NONE, 0, 0); /* it covers the page: after its own targets */
}

static void picker(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double W = v->W;
	if (v->year) {
		year(c);
		return;
	}
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, v->H);
		lk_fill(c->cr, L->paper);
	}
	char main[64], sub[96], t[128];
	title_of(v, main, sizeof main, sub, sizeof sub);
	if (v->per.kind == P_DAY)
		snprintf(t, sizeof t, "Template for %s", main);
	else
		snprintf(t, sizeof t, "Template for %s%s", v->per.kind == P_WEEK ? "" : "", main);
	UiFont hf = fpx(L->h1, 21);
	tc(c, &hf, t, 16, 14 + 21, 0, W - 140, L->ink);
	UiFont bf = L->voice == V_WHITE ? fam(L->mono, 600, 0, 11.5f) : L->label;
	if (L->voice == V_WHITE)
		bf.tcase = UI_CASE_UPPER, bf.ls = 0.08f;
	double bw = tw(c, &bf, "Close") + 28;
	box(c, W - 16 - bw, 14, bw, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, "Close", W - 16 - bw / 2, 34, 1, 0, L->ink);
	hit(c, W - 16 - bw, 14, bw, 40, A_CLOSE, 0, 0);
	UiFont lf = fam(L->text, 400, 0, 14);
	tc(c, &lf, "Switching shows the same entries in another layout; nothing written changes.", 16, 82, 0, W - 32, L->soft);
	double y = scopes(c, 102, NULL) + 10;
	const char *cur = store_template(c->s, v->per, NULL);
	const struct tmpl *ct = tmpl_find(cur) ? tmpl_find(cur) : tmpl_default(v->per.kind);
	const struct tmpl *ts[64];
	int nts = tmpl_list(v->per.kind, ts, 64);
	const struct tmpl *ring = v->ring[0] && tmpl_find(v->ring) ? tmpl_find(v->ring) : ct;
	/* every template gets a row: rows shrink to fit, and past that the list starts so that the
	 * ringed one shows (a template just saved is listed last) */
	double avail = v->H - 64 - y, rh = fmax(64, fmin(112, avail / (nts ? nts : 1) - 12));
	int rows = (int)(avail / (rh + 12)), first = 0;
	for (int i = 0; i < nts; i++)
		if (ts[i] == ring && i >= rows)
			first = i - rows + 1;
	int lines = rh < 96 ? 1 : 2;
	for (int i = first; i < nts && i < first + rows; i++) {
		const struct tmpl *tp = ts[i];
		bool use = tp == ct;
		thumb(c, tp, 16, y, fmin(84, (rh - 2) * 0.76), rh - 2, tp == ring);
		UiFont nf = fam(L->head, 600, 0, 17), df = fam(L->text, 400, 0, 13);
		double h1, h2;
		ui_text_w(&c->g, &nf, tp->name, &h1);
		UiGc mg = c->g;
		mg.paint = 0;
		h2 = ui_text_wrap(&mg, &df, tp->note, 0, 0, W - 16 - 114 - 90, lines, 0, L->soft);
		double ty = y + (rh - h1 - 3 - h2 - 18) / 2;
		if (c->paint) {
			ui_text(&c->g, &nf, tp->name, 114, ty, 0, W - 220, L->ink, &h1);
			ui_text_wrap(&c->g, &df, tp->note, 114, ty + h1 + 3, W - 16 - 114 - 90, lines, 0, L->soft);
		}
		char us[200];
		usage(c, tp, v->per.kind, us, sizeof us);
		tb(c, &(UiFont){ L->mono, 400, 0, 11.5f, 0, 0, UI_CASE_NONE }, us, 114, ty + h1 + 3 + h2 + 16, 0, W - 16 - 114 - 90, L->soft);
		if (use) {
			UiFont uf = fam(L->mono, 600, 0, 10.5f);
			double uw = tw(c, &uf, "in use") + 16;
			box(c, W - 16 - uw, y + rh / 2 - 10, uw, 20, 10, 0, 0, L->ink);
			tc(c, &uf, "in use", W - 16 - uw / 2, y + rh / 2, 1, 0, L->paper);
		}
		hit(c, 16, y, W - 32, rh, A_PICK, i, 0);
		y += rh + 10;
		if (c->paint)
			lk_rule(c->cr, 16, y - 6, W - 32, 1.5, L->ink, 2);
		y += 2;
	}
	picker_edit_acts(c, y + 4);
	hit(c, 0, 0, W, v->H, A_NONE, 0, 0); /* it covers the page: after its own targets */
}

/* ---- the whole screen ---- */

static void ed_run(struct ctx *c);

static void run_(struct ctx *c);
static void event_card(struct ctx *c);
static void event_page(struct ctx *c);
static void page_ask(struct ctx *c);
static void run(struct view *v, struct store *s, const struct look *L, cairo_t *cr, int paint)
{
	static struct vev evb[V_EVS];
	int nevb = 0;
	bool out = false;
	struct ctx c = { v, s, L, cr, { cr, paint, L->uv }, paint, evb, &nevb, &out };
	run_(&c);
	if (!paint && out) {
		memcpy(v->ev, evb, nevb * sizeof *evb);
		v->nev = nevb;
	}
}

static void run_(struct ctx *c_)
{
	struct ctx c = *c_;
	struct view *v = c.v;
	struct store *s = c.s;
	cairo_t *cr = c.cr;
	int paint = c.paint;
	if (!paint) {
		v->nf = 0;
		v->nh = 0;
		int how = 3;
		const char *id = store_template(s, v->per, &how);
		if (v->whole && v->per.kind == P_DAY && v->per.start == v->whole_day)
			id = "day-agenda"; /* opened from a fold bar: the whole day */
		v->t = tmpl_find(id);
		if (!v->t || v->t->kind != v->per.kind) {
			v->t = tmpl_default(v->per.kind);
			how = 3;
		}
		v->how = how;
		v->hidden_count = count_hidden(s, v->t, v->per);
		v->land = v->W > v->H;
	}
	if (v->ed && v->ed->t) {
		ed_run(&c);
		return;
	}
	if (v->ov == OV_PICKER) {
		picker(&c);
		return;
	}
	if (v->ov == OV_PAGE || (v->ov == OV_MOVE && v->back == OV_PAGE)) { /* an event's page: the whole screen */
		if (!paint) {
			if (v->tm)
				tbar(&c);
			if (v->ov == OV_MOVE)
				sheet_move(&c);
			else if (v->page_ask)
				page_ask(&c);
			event_page(&c);
			return;
		}
		cairo_save(cr);
		cairo_translate(cr, 0, -v->scroll);
		event_page(&c);
		if (v->ov == OV_MOVE)
			sheet_move(&c);
		else if (v->page_ask)
			page_ask(&c);
		if (v->tm)
			text_fields(&c);
		cairo_restore(cr);
		if (v->tm)
			tbar(&c);
		return;
	}
	if (!paint) {
		/* the overlays' targets first: they lie on top */
		if (v->tm)
			tbar(&c);
		if (v->ov == OV_CARD || v->ov == OV_DELETE)
			event_card(&c);
		if (v->ov == OV_TITLE)
			title_card(&c);
		if (v->ov == OV_MOVE)
			sheet_move(&c);
		if (v->ov == OV_INFO || v->ov == OV_MENU)
			popover(&c);
		if (v->ov == OV_HIDDEN || ((v->ov == OV_TITLE || v->ov == OV_MOVE) && v->back == OV_HIDDEN))
			sheet_hidden(&c);
		if (v->ov == OV_SETTINGS)
			settings(&c);
		bar(&c);
		page(&c);
		return;
	}
	cairo_save(cr);
	cairo_translate(cr, 0, -v->scroll);
	page(&c);
	bar(&c);
	if (v->ov == OV_INFO || v->ov == OV_MENU)
		popover(&c);
	if (v->ov == OV_HIDDEN || (v->ov == OV_TITLE && v->back == OV_HIDDEN))
		sheet_hidden(&c);
	if (v->ov == OV_SETTINGS)
		settings(&c);
	if (v->ov == OV_TITLE)
		title_card(&c);
	if (v->ov == OV_MOVE)
		sheet_move(&c);
	if (v->ov == OV_CARD || v->ov == OV_DELETE)
		event_card(&c);
	if (v->tm)
		text_fields(&c);
	cairo_restore(cr);
	if (v->tm)
		tbar(&c);
}

static double bar_h = 60;
double view_bar_h(void)
{
	return bar_h;
}

void view_layout(struct view *v, struct store *s, const struct look *L, cairo_t *measure)
{
	ui_draw_init();
	bar_h = ui_grow(ui_grow(60, &L->h1, 1, 1), &L->sub, 1, 1); /* 60 while the text keeps its size */
	run(v, s, L, measure, 0);
}

void view_layout_at(struct view *v, struct store *s, const struct look *L, double scale)
{
	/* the same kind of surface the page is painted into, and its scale */
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	cairo_t *cr = cairo_create(sf);
	cairo_scale(cr, scale > 0 ? scale : 1, scale > 0 ? scale : 1);
	view_layout(v, s, L, cr);
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
}

void view_paint(struct view *v, struct store *s, const struct look *L, cairo_t *cr)
{
	ui_draw_init();
	run(v, s, L, cr, 1);
}

const struct vhit *view_hit(const struct view *v, double x, double y)
{
	for (int i = 0; i < v->nh; i++) {
		const struct vhit *h = &v->hit[i];
		double yy = h->fixed ? y : y + v->scroll;
		if (x >= h->x && x < h->x + h->w && yy >= h->y && yy < h->y + h->h)
			return h;
	}
	return NULL;
}

const struct vfield *view_field_at(const struct view *v, double x, double y)
{
	if (v->ov != OV_NONE && !(v->ov == OV_PAGE && !v->page_ask))
		return NULL;
	if (v->tm && y >= vis_of(v) - TBAR)
		return NULL;
	y += v->scroll;
	if (y < BAR)
		return NULL;
	const struct vfield *best = NULL;
	for (int i = 0; i < v->nf; i++) {
		const struct vfield *f = &v->f[i];
		if (f->k == F_FREE || x < f->x || x >= f->x + f->w || y < f->y || y >= f->y + f->h)
			continue;
		if (!best || f->layer >= best->layer)
			best = f;
	}
	return best;
}

double view_scroll_to(const struct view *v, const struct vfield *f)
{
	double vis = vis_of(v) - TBAR, sc = v->scroll, bottom = f->y + fmin(f->h, 40) + 6;
	if (bottom - sc > vis)
		sc = ceil((bottom - vis) / PL_ROW) * PL_ROW;
	if (f->y - 4 - sc < BAR)
		sc = fmax(0, floor((f->y - 4 - BAR) / PL_ROW) * PL_ROW);
	return fmax(0, fmin(sc, v->H - TBAR));
}

const struct vfield *view_field_of(const struct view *v, uint32_t entry)
{
	for (int i = 0; i < v->nf; i++)
		if (v->f[i].k == F_ENTRY && v->f[i].entry == entry)
			return &v->f[i];
	return NULL;
}

/* ---- a stroke into the free time (FR-018) ---- */

/* a drawn block: a stroke at least this tall (px; taller than a written letter at the size
 * ink is written at) over at least an hour, and three times as tall as it is wide */
#define BLOCK_MIN (3 * INK_SIZE)

/* the minute at y (field coordinates) of free time f, at its scale */
static double free_min(const struct vfield *f, double y)
{
	return f->tm.n >= 2 ? tmap_min(&f->tm, y) : f->min0 + y * f->mpp;
}

bool view_free_block(const struct vfield *f, const struct pl_point *p, int n, int *start, int *end)
{
	double x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
	for (int i = 1; i < n; i++)
		x0 = fmin(x0, p[i].x), x1 = fmax(x1, p[i].x), y0 = fmin(y0, p[i].y), y1 = fmax(y1, p[i].y);
	double h = y1 - y0;
	double m0 = free_min(f, fmax(0, y0)), m1 = free_min(f, y1);
	bool block = h >= BLOCK_MIN && m1 - m0 >= 60 && h >= 3 * (x1 - x0);
	int s = (int)floor(m0 / 30) * 30;
	int e = (int)ceil(m1 / 30) * 30;
	if (!block) /* writing: an hour from the half hour it starts in (its title grows below) */
		e = s + 60;
	if (e - s < 30)
		e = s + 30;
	if (s < 0)
		s = 0;
	if (e > 24 * 60)
		e = 24 * 60;
	if (s >= e)
		s = e - 30;
	*start = s, *end = e;
	return block;
}

struct entry *view_free_stroke(struct view *v, struct store *s, const struct vfield *free_, uint32_t k,
			       const struct pl_point *p, int n, void (*relayout)(void *ud), void *ud, bool *kept)
{
	*kept = false;
	if (!n)
		return NULL;
	struct vfield f = *free_; /* the view's fields are laid out again below */
	int st, en;
	bool block = view_free_block(&f, p, n, &st, &en);
	struct entry *x = store_add(s, E_EVENT, f.per);
	if (!x)
		return NULL;
	x->start = st, x->end = en;
	uint32_t id = x->id;
	relayout(ud);
	x = store_get(s, id);
	if (block || !x)
		return x;
	/* the stroke is the title's first ink, on the pixels it was written on */
	const struct vfield *t = view_field_of(v, id);
	double dx = t ? f.x - t->x : 0, dy = t ? f.y - t->y : 0;
	struct pl_point *q = malloc(n * sizeof *q);
	if (!q)
		return x;
	for (int i = 0; i < n; i++)
		q[i] = (struct pl_point){ (float)(p[i].x + dx), (float)(p[i].y + dy), p[i].p, p[i].t };
	field_add_stroke(&x->f, k, q, n);
	free(q);
	*kept = true;
	return x;
}

/* ---- FR-001: without writing regions (board start.refused) ---- */

void view_paint_refused(const struct look *L, cairo_t *cr, double W, double H, struct vhit *close)
{
	ui_draw_init();
	UiGc g = { cr, 1, L->uv };
	cairo_rectangle(cr, 0, 0, W, H);
	lk_fill(cr, L->paper);
	double x = 56, y = H / 2 - 170;
	lk_icon(cr, IC_HAND, x, y, 64, L->ink, 1.2);
	y += 64 + 18;
	UiFont h = fpx(L->h1, 30);
	double hh = ui_text_wrap(&g, &h, "The planner needs handwriting", x, y, W - 112, 3, 1.2, L->ink);
	y += hh + 18;
	UiFont b = fam(L->text, 400, 0, L->voice == V_WHITE ? 17 : L->voice == V_EDITORIAL ? 18.5f : 20);
	y += ui_text_wrap(&g, &b,
			  "This system has no handwriting service. Everything in the planner is written with the pen, and the "
			  "planner can’t read what you write without it.",
			  x, y, fmin(520, W - 112), 6, L->voice == V_WHITE ? 1.65 : 1.6, L->ink) + 18;
	UiFont s = fam(L->text, 400, 0, 15);
	y += ui_text_wrap(&g, &s, "There is no keyboard-only planner. Install handwriting from Software, then open the planner again.",
			  x, y, fmin(520, W - 112), 4, 1.5, L->soft) + 24;
	UiFont bf = L->voice == V_WHITE ? (UiFont){ L->mono, 600, 0, 11.5f, 0, 0.08f, UI_CASE_UPPER } : L->label;
	double hb;
	double bw = ui_text_w(&g, &bf, "Close", &hb) + 28;
	lk_rect(cr, x, y, bw, 40, L->ui_r);
	lk_fill(cr, L->paper);
	lk_stroke_box(cr, x, y, bw, 40, L->ui_r, L->edge, L->ink);
	ui_text(&g, &bf, "Close", x + bw / 2, y + 20 - hb / 2, 1, 0, L->ink, &hb);
	if (close)
		*close = (struct vhit){ x, y, bw, 40, A_CLOSE, 0, 0 };
}

/* ---- the template editor (boards templates.editor, templates.new, templates.used) ----
 * The page shows the template with the period's real entries, but no field and no control of
 * the page reaches the editor's screen: its taps select widgets, and no entry can be written,
 * ticked or moved here (FR-045). */

#define ED_BAR 57 /* the editor's bar at the foot */

static const char *const IC_TRASH = "M5 7h14 M9 7V4h6v3 M7 7l1 13h8l1-13";
static const char *const IC_ROTATE = "M4 9h11v11H4z M14 3a7 7 0 0 1 6 6 M20 5v4h-4";
static const char *const IC_UNDO = "M9 14L4 9l5-5 M4 9h10a6 6 0 0 1 0 12h-3";
static const char *const IC_W[W_NWIDGETS] = {
	"M4 5h16v15H4z M8 3v4 M16 3v4 M8 11h5 M8 15h8", "M4 5h16v15H4z M8 3v4 M16 3v4 M8 11h5 M8 15h8",
	"M4 6h3v3H4z M10 7.5h10 M4 14h3v3H4z M10 15.5h10", "M5 4h14v16H5z M8 9h8 M8 13h8 M8 17h5",
	"M5 7a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z M11 7a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z M17 7a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z M5 14a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z M11 14a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z",
	"M4 6h16v14H4z M4 10h16 M8 14h.01 M12 14h.01 M16 14h.01 M8 17h.01 M12 17h.01", "M3 5h18v14H3z M3 9h18 M3 13h18 M3 17h18",
	"M5 6h2 M10 6h9 M5 12h2 M10 12h9 M5 18h2 M10 18h9" };
static const char *const W_NAME[W_NWIDGETS] = { "Day agenda", "Day agenda (compact)", "To-do list", "Notes", "Habit tracker",
	"Mini month", "Week overview", "Priorities" };
static const char *const W_SUB[W_NWIDGETS] = { "compresses · min 200 w", "compresses · min 84 w", "truncates · min 180 w",
	"a named slot · min 200 w", "truncates · min 240 w", "compresses · min 170 w", "truncates · min 260 w", "ranked · min 180 w" };

/* ---- an event's card, the card that asks before it is deleted (boards event.open, event.delete) ---- */

#define EC_W 340

/* a button (.pl-btn), its right edge at x: 0 plain, 1 primary; its width */
static double btn_r(struct ctx *c, double x, double y, const char *label, int style, enum act a, int arg, uint32_t id)
{
	const struct look *L = c->L;
	UiFont bf = btn_font(L);
	double w = tw(c, &bf, label) + 28;
	x -= w;
	if (style == 1)
		box(c, x, y, w, 40, L->ui_r, 0, 0, L->ink);
	else
		box(c, x, y, w, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &bf, label, x + w / 2, y + 20, 1, 0, style == 1 ? L->paper : L->ink);
	hit(c, x, y, w, 40, a, arg, id);
	return w;
}

/* the stroke that struck the title through, as the pen drew it */
static void xout_paint(struct ctx *c)
{
	struct view *v = c->v;
	if (!v->xout || v->nxout < 2 || !c->paint)
		return;
	cairo_t *cr = c->cr;
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	col(cr, c->L->pen);
	for (int i = 1; i < v->nxout; i++) {
		const struct pl_point *a = &v->xout[i - 1], *b = &v->xout[i];
		cairo_set_line_width(cr, pl_ink_width((a->p + b->p) / 2));
		cairo_move_to(cr, a->x, a->y);
		cairo_line_to(cr, b->x, b->y);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* "14:00–15:00 · Thursday 11 March" */
static void ev_fact(const struct entry *e, char *o, size_t n)
{
	char w1[8], w2[8];
	int y, m, d;
	day_to_ymd(e->per.start, &y, &m, &d);
	hm(w1, sizeof w1, e->start);
	hm(w2, sizeof w2, e->end);
	snprintf(o, n, "%s–%s · %s %d %s", w1, w2, DOW[day_weekday(e->per.start)], d, MON[m - 1]);
}

/* OV_CARD: the title as written, its time and day, Delete event and Open; OV_DELETE: "Delete
 * this event?" above the same, Keep and Delete event. Under the title, above it when there is
 * no room below; a tap outside closes it (the asking card: Keep) */
static void event_card(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct entry *e = store_get(c->s, v->title);
	const struct vev *ve = view_ev(v, v->title);
	bool ask = v->ov == OV_DELETE;
	UiFont qf = fpx(L->h1, 17);
	/* the whole title, as written: the card as wide as it needs (at most the screen), the ink
	 * smaller only when even that is too narrow */
	double r = 0, bt = 0, top = 0, k = 1;
	if (e) {
		field_extent(&e->f, &r, &bt);
		top = ink_top(&e->f);
	}
	double CW = fmax(EC_W, fmin(v->W - 16, r + 24));
	k = r + 24 > CW ? (CW - 24) / fmax(r, 1) : 1;
	double qh = ask ? 20 + 8 : 0, inkh = fmax(30, (bt - top) * k + 4), hgt = 8 + qh + inkh + 8 + 31 + 60;
	double x = ve ? ve->tx : v->W - 8 - CW, y = ve ? ve->y + ve->th + 6 : v->H / 3;
	if (ve && y + hgt > v->H - 8) /* no room below: above the title */
		y = ve->y - 6 - hgt;
	x = fmax(8, fmin(x, v->W - 8 - CW));
	y = fmax(BAR + 4, fmin(y, v->H - 8 - hgt));
	v->pop_x = x, v->pop_y = y, v->pop_w = CW, v->pop_h = hgt;
	if (ask)
		xout_paint(c);
	pop_frame(c, x, y, CW, hgt);
	double yy = y + 8;
	if (ask) {
		double th;
		ui_text_w(&c->g, &qf, "Delete this event?", &th);
		if (c->paint)
			ui_text(&c->g, &qf, "Delete this event?", x + 10, yy - 2, 0, CW - 20, L->ink, &th);
		yy += qh;
	}
	if (e && c->paint && bt > top) {
		cairo_save(c->cr);
		cairo_rectangle(c->cr, x + 8, yy, CW - 16, inkh);
		cairo_clip(c->cr);
		ink(c, &e->f, x + 10, yy + 2 - top * k, k);
		cairo_restore(c->cr);
	}
	yy += inkh + 8;
	if (c->paint)
		lk_rule(c->cr, x, yy, CW, 1.5, L->ink, 2);
	if (e) {
		char t[80];
		ev_fact(e, t, sizeof t);
		UiFont ff = fam(L->mono, 400, 0, 12);
		tc(c, &ff, t, x + 12, yy + 16, 0, CW - 24, L->ink);
	}
	yy += 31;
	if (c->paint)
		lk_rule(c->cr, x, yy, CW, 1.5, L->ink, 2);
	double bx = x + CW - 12;
	if (ask) {
		bx -= btn_r(c, bx, yy + 10, "Delete event", 1, A_DEL_YES, 0, v->title) + 8;
		btn_r(c, bx, yy + 10, "Keep", 0, A_DEL_KEEP, 0, v->title);
	} else {
		bx -= btn_r(c, bx, yy + 10, "Open", 1, A_EV_OPEN, 0, v->title) + 8;
		btn_r(c, bx, yy + 10, "Delete event", 0, A_EV_ASK, 0, v->title);
	}
	hit(c, x, y, CW, hgt, A_NONE, 0, 0);
	/* a tap outside closes it, and the asking card keeps the event */
	hit(c, 0, 0, v->W, v->H + v->scroll, ask ? A_DEL_KEEP : A_CLOSE, 0, v->title);
}

/* ---- the event's own page (board event.open) ---- */

static const char *const REPEAT[] = { "Never", "Daily", "Weekly", "Monthly", "Yearly" };
static const char *const REMIND[] = { "None", "At start", "15 min", "1 h", "1 day" };
const int VIEW_REMIND_MIN[5] = { -1, 0, 15, 60, 1440 };

/* a row's label (.pl-evp-r .k) */
static void pg_key(struct ctx *c, const char *t, double x, double y)
{
	const struct look *L = c->L;
	UiFont k = L->label;
	if (L->voice == V_WHITE)
		k = fam(L->mono, 600, 0, 11), k.tcase = UI_CASE_UPPER, k.ls = 0.08f;
	double th;
	ui_text_w(&c->g, &k, t, &th);
	if (c->paint)
		ui_text(&c->g, &k, t, x, y, 0, 74, L->ink, &th);
}

/* a handwriting field of the page: lines rows of 36 px (the title's one of 46), each on a
 * dotted rule in the ink */
static void pg_field(struct ctx *c, struct entry *e, int sub, double x, double y, double w, int lines, double row)
{
	const struct look *L = c->L;
	for (int i = 1; i <= lines && c->paint; i++)
		lk_rule(c->cr, x, y + i * row - 1.5, w, 1.5, L->ink, 2);
	struct field *f = entry_sub(e, sub);
	struct vfield *vf = field(c, F_ENTRY, x, y, w, lines * row);
	if (vf) {
		static const char *const NM[] = { "title", "where", "notes", "with" };
		vf->entry = e->id;
		vf->sub = sub;
		snprintf(vf->name, sizeof vf->name, "%s:%s", NM[sub], e->uid);
		if (lines > 1)
			vf->guide = row, vf->first = row - 6;
	}
	if (f && c->paint) {
		cairo_save(c->cr);
		cairo_rectangle(c->cr, x, y, w, lines * row);
		cairo_clip(c->cr);
		ink(c, f, x, y, 1);
		cairo_restore(c->cr);
	}
}

/* a row of choices (.pl-evp-seg), the one on filled */
static void pg_seg(struct ctx *c, const char *const *t, int n, int on, double x, double y, double w, enum act a)
{
	const struct look *L = c->L;
	UiFont bf = btn_font(L);
	double lw = L->voice == V_WHITE ? 1.5 : 1, sw = w / n;
	if (c->paint) {
		cairo_save(c->cr);
		lk_rect(c->cr, x, y, w, 40, L->ui_r);
		cairo_clip(c->cr);
	}
	for (int i = 0; i < n; i++) {
		double sx = x + i * sw;
		if (c->paint && i == on) {
			cairo_rectangle(c->cr, sx, y, sw, 40);
			lk_fill(c->cr, L->ink);
		}
		if (c->paint && i)
			lk_bar(c->cr, sx, y, lw, 40, L->ink);
		tc(c, &bf, t[i], sx + sw / 2, y + 20, 1, sw - 8, i == on ? L->paper : L->ink);
		hit(c, sx, y, sw, 40, a, i, c->v->title);
	}
	if (c->paint) {
		cairo_restore(c->cr);
		lk_stroke_box(c->cr, x, y, w, 40, L->ui_r, lw, L->ink);
	}
}

/* a square button of the times (− or +) */
static void pg_step(struct ctx *c, const char *t, double x, double y, int arg)
{
	const struct look *L = c->L;
	box(c, x, y, 40, 40, L->ui_r, L->edge, L->ink, L->paper);
	tc(c, &(UiFont){ L->mono, 600, 0, 18, 0, 0, UI_CASE_NONE }, t, x + 20, y + 20, 1, 0, L->ink);
	hit(c, x, y, 40, 40, A_PG_TIME, arg, c->v->title);
}

static void event_page(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct entry *e = store_get(c->s, v->title);
	double W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, v->H);
		lk_fill(c->cr, L->paper);
	}
	/* the bar: only Back, the page's name and its day */
	ui_col fg = L->bar_fg;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, BAR);
		lk_fill(c->cr, L->bar_bg);
		if (L->bar_rule == 3) {
			lk_rule(c->cr, 0, BAR - 3, W, 1, L->ink, 0);
			lk_rule(c->cr, 0, BAR - 1, W, 1, L->ink, 0);
		} else if (L->bar_rule > 0)
			lk_rule(c->cr, 0, BAR - L->bar_rule, W, L->bar_rule, L->ink, 0);
		else
			lk_rule(c->cr, 0, BAR - 1.5, W, 1.5, L->gilt, 0);
	}
	double cy = BAR / 2.0;
	UiFont bf = btn_font(L);
	double bw = tw(c, &bf, "Back") + 28 + 14;
	box(c, 16, cy - 20, bw, 40, L->ui_r, L->edge, fg, 0);
	icon(c, IC_CHEV_L, 16 + 15, cy, 16, fg);
	tc(c, &bf, "Back", 16 + 24 + (bw - 24) / 2 - 2, cy, 1, 0, fg);
	hit(c, 16, cy - 20, bw, 40, A_PG_BACK, 0, v->title);
	char sub[64] = "";
	int yr = 0, mo = 1, dd = 1;
	if (e) {
		day_to_ymd(e->per.start, &yr, &mo, &dd);
		snprintf(sub, sizeof sub, "%s %d %s", DOW[day_weekday(e->per.start)], dd, MON[mo - 1]);
	}
	{
		double h1, h2;
		UiFont f1 = L->h1, f2 = L->sub;
		ui_text_w(&c->g, &f1, "Event", &h1);
		ui_text_w(&c->g, &f2, sub, &h2);
		double top = cy - (h1 + 1 + h2) / 2, tx = 16 + bw + 12;
		if (c->paint) {
			ui_text(&c->g, &f1, "Event", tx, top, 0, W - tx - 16, fg, &h1);
			ui_text(&c->g, &f2, sub, tx, top + h1 + 1, 0, W - tx - 16, fg, &h2);
		}
	}
	if (!e)
		return;
	/* the rows (.pl-evp): a label 74 px wide, 14 px to the value */
	double x0 = 18, kx = x0 + 2, vx = x0 + 74 + 14, vw = W - 18 - vx, y = BAR + 4;
	/* Title: written with the pen, a finger types it (text mode) */
	pg_key(c, "Title", kx, y + 6 + 18);
	pg_field(c, e, EF_TITLE, vx, y + 6, vw, 1, 46);
	y += 6 + 46 + 12;
	/* When: the day (a tap: the month), the start and the end, 30 minutes a step */
	pg_key(c, "When", kx, y + 6 + 16);
	{
		double wy = y + 6 + 4;
		char t[40];
		snprintf(t, sizeof t, "%s %d %s", DOW3[day_weekday(e->per.start)], dd, MON[mo - 1]);
		double dw = tw(c, &bf, t) + 50;
		box(c, vx, wy, dw, 40, L->ui_r, L->edge, L->ink, L->paper);
		icon(c, IC_CAL, vx + 14 + 9, wy + 20, 18, L->ink);
		tc(c, &bf, t, vx + dw / 2 + 11, wy + 20, 1, 0, L->ink);
		hit(c, vx, wy, dw, 40, A_PG_DAY, 0, v->title);
		double ty = wy + 40 + 8, tx = vx;
		UiFont lf = L->voice == V_WHITE ? fam(L->mono, 400, 0, 12) : fam(L->text, 400, 1, 15);
		UiFont nf = fam(L->mono, 600, 0, 18);
		for (int k = 0; k < 2; k++) {
			tx += tc(c, &lf, k ? "to" : "from", tx, ty + 20, 0, 0, L->ink) + 6;
			pg_step(c, "−", tx, ty, k * 2);
			tx += 40 + 6;
			hm(t, sizeof t, k ? e->end : e->start);
			double nw = fmax(54, tw(c, &nf, t));
			tc(c, &nf, t, tx + nw / 2, ty + 20, 1, 0, L->ink);
			tx += nw + 6;
			pg_step(c, "+", tx, ty, k * 2 + 1);
			tx += 40 + 14;
		}
		int dur = e->end - e->start;
		if (dur >= 60 && dur % 60)
			snprintf(t, sizeof t, "%d h %d min", dur / 60, dur % 60);
		else if (dur >= 60)
			snprintf(t, sizeof t, "%d h", dur / 60);
		else
			snprintf(t, sizeof t, "%d min", dur);
		tc(c, &(UiFont){ L->mono, 400, 0, 12, 0, 0, UI_CASE_NONE }, t, tx, ty + 20, 0, 0, L->ink);
		y = ty + 40 + 12;
	}
	/* Where, Notes, With (the participants): written */
	pg_key(c, "Where", kx, y + 6 + 16);
	pg_field(c, e, EF_WHERE, vx, y + 6, vw, 1, 36);
	y += 6 + 36 + 12;
	pg_key(c, "Notes", kx, y + 6 + 16);
	pg_field(c, e, EF_NOTES, vx, y + 6, vw, 4, 36);
	y += 6 + 4 * 36 + 12;
	pg_key(c, "With", kx, y + 6 + 16);
	pg_field(c, e, EF_WITH, vx, y + 6, vw, 2, 36);
	y += 6 + 2 * 36 + 12;
	/* Repeat and Remind: choices */
	pg_key(c, "Repeat", kx, y + 6 + 16);
	pg_seg(c, REPEAT, 5, e->repeat >= 0 && e->repeat < 5 ? e->repeat : 0, vx, y + 6 + 4, vw, A_PG_REPEAT);
	y += 6 + 44 + 12;
	int ri = 0;
	for (int i = 0; i < 5; i++)
		if (VIEW_REMIND_MIN[i] == e->remind)
			ri = i;
	pg_key(c, "Remind", kx, y + 6 + 16);
	pg_seg(c, REMIND, 5, ri, vx, y + 6 + 4, vw, A_PG_REMIND);
	y += 6 + 44 + 12;
	/* Delete event, at its foot: it asks first */
	{
		double dw = tw(c, &bf, "Delete event") + 50;
		box(c, x0, y + 14, dw, 40, L->ui_r, L->edge, L->ink, L->paper);
		icon(c, IC_TRASH, x0 + 14 + 9, y + 34, 18, L->ink);
		tc(c, &bf, "Delete event", x0 + dw / 2 + 11, y + 34, 1, 0, L->ink);
		hit(c, x0, y + 14, dw, 40, A_PG_DELETE, 0, v->title);
	}
}

/* the page's dialog before Delete: what goes with the event, Keep and Delete event */
static void page_ask(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct entry *e = store_get(c->s, v->title);
	double w = v->W - 2 * 40, x = 40, y = BAR + 200, h = 196;
	v->pop_x = x, v->pop_y = y, v->pop_w = w, v->pop_h = h;
	pop_frame(c, x, y, w, h);
	UiFont hf = fpx(L->h1, 20), bf = L->body, sf = fpx(L->body, 13);
	double th;
	ui_text_w(&c->g, &hf, "Delete this event?", &th);
	if (c->paint)
		ui_text(&c->g, &hf, "Delete this event?", x + 20, y + 18, 0, w - 40, L->ink, &th);
	char t[200] = "", w1[8], w2[8];
	if (e) {
		int yr, m, d;
		day_to_ymd(e->per.start, &yr, &m, &d);
		hm(w1, sizeof w1, e->start);
		hm(w2, sizeof w2, e->end);
		snprintf(t, sizeof t, "This event, %s–%s on %s %d %s, leaves the planner, with its place, notes and participants.",
			 w1, w2, DOW[day_weekday(e->per.start)], d, MON[m - 1]);
	}
	struct tline ln[4];
	int n = wrap(c, &bf, t, w - 40, ln, 4);
	double ly = y + 18 + th + 10;
	for (int i = 0; i < n; i++) {
		char line_[200];
		snprintf(line_, sizeof line_, "%.*s", ln[i].n, t + ln[i].s);
		double lh;
		ui_text_w(&c->g, &bf, line_, &lh);
		if (c->paint)
			ui_text(&c->g, &bf, line_, x + 20, ly, 0, w - 40, L->ink, &lh);
		ly += lh + 2;
	}
	ly += 8;
	{
		double lh;
		ui_text_w(&c->g, &sf, "Nothing else changes.", &lh);
		if (c->paint)
			ui_text(&c->g, &sf, "Nothing else changes.", x + 20, ly, 0, w - 40, L->soft, &lh);
	}
	double bx = x + w - 20;
	bx -= btn_r(c, bx, y + h - 20 - 40, "Delete event", 1, A_PG_DELETE, 1, v->title) + 10;
	btn_r(c, bx, y + h - 20 - 40, "Keep", 0, A_PG_DELETE, 2, v->title);
	hit(c, x, y, w, h, A_NONE, 0, 0);
	hit(c, 0, 0, v->W, v->H, A_PG_DELETE, 2, v->title); /* a tap outside keeps it */
}

/* ---- the pen on an agenda's events (FR-018) ---- */

uint32_t view_strike(const struct view *v, const struct pl_point *p, int n)
{
	if (n < 2 || v->ov != OV_NONE)
		return 0;
	double x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y, my = 0;
	for (int i = 0; i < n; i++) {
		x0 = fmin(x0, p[i].x), x1 = fmax(x1, p[i].x), y0 = fmin(y0, p[i].y), y1 = fmax(y1, p[i].y);
		my += p[i].y;
	}
	my /= n;
	/* along a line: much wider than tall */
	if (x1 - x0 < 24 || y1 - y0 > fmax(16, 0.35 * (x1 - x0)))
		return 0;
	for (int i = 0; i < v->nev; i++) {
		const struct vev *e = &v->ev[i];
		if (e->ink[2] <= 0)
			continue;
		double l = e->ink[0], r = e->ink[0] + e->ink[2], t = e->ink[1], b = e->ink[1] + e->ink[3];
		/* its pen-down in this event's lane (the first's reaches over the hour margin),
		 * before the first letter */
		double lane0 = e->ln.lane ? e->spine - BK_GAP : e->spine - BK_SPINE;
		if (p[0].x >= l || p[0].x < lane0 || p[0].y < t - 8 || p[0].y > b + 8)
			continue;
		/* past the last letter, through the ink */
		if (x1 <= r + 2 || my < t - 4 || my > b + 4)
			continue;
		return e->id;
	}
	return 0;
}

const struct vfield *view_margin_at(const struct view *v, double x, double y)
{
	if (v->ov != OV_NONE || v->tm || v->ed)
		return NULL;
	for (int i = 0; i < v->nf; i++) {
		const struct vfield *f = &v->f[i];
		if (f->k == F_FREE && x >= f->x - GUTTER && x < f->x && y >= f->y && y < f->y + f->h)
			return f;
	}
	return NULL;
}

void view_margin_span(const struct vfield *f, double y0, double y1, int *start, int *end)
{
	y0 -= f->y, y1 -= f->y;
	if (y1 < y0) {
		double t = y0;
		y0 = y1, y1 = t;
	}
	int s = (int)floor(free_min(f, y0) / 30) * 30, e = (int)ceil(free_min(f, y1) / 30) * 30;
	s = s < 0 ? 0 : s > 24 * 60 - 30 ? 24 * 60 - 30 : s;
	e = e > 24 * 60 ? 24 * 60 : e;
	if (e < s + 30)
		e = s + 30;
	*start = s, *end = e;
}

uint32_t view_lane_at(const struct view *v, double x, double y)
{
	for (int i = 0; i < v->nev; i++) {
		const struct vev *e = &v->ev[i];
		if (x >= e->spine - 1 && x < e->spine + e->w + 1 && y >= e->y && y < e->y + fmax(e->h, e->th))
			return e->id;
	}
	return 0;
}

uint32_t view_gap_at(const struct view *v, double x, double y)
{
	uint32_t best = 0;
	double bd = 1e9;
	for (int i = 0; i < v->nev; i++) {
		const struct vev *e = &v->ev[i];
		if (e->ln.rows || e->ln.lanes < 2 || y < e->y || y >= e->y + fmax(e->h, e->th))
			continue;
		/* left of its "[" (the gap after the lane before) or right of its "]" (before the next) */
		double dl = e->spine - 1 - x, dr = x - (e->spine + e->w + 1);
		double d = dl >= 0 && dl <= BK_GAP && e->ln.lane > 0 ? dl : dr >= 0 && dr <= BK_GAP && e->ln.lane < e->ln.lanes - 1 ? dr + 0.01 : 1e9;
		if (d < bd)
			bd = d, best = e->id;
	}
	return best;
}

bool view_covered(const struct view *v, const struct vfield *f)
{
	if (v->ed)
		return false; /* the editor's only field is its name */
	if (v->ov == OV_PICKER || v->ov == OV_TITLE || v->ov == OV_SETTINGS || v->ov == OV_DELETE || v->ov == OV_CARD)
		return true;
	if (v->ov == OV_PAGE)
		return v->page_ask;
	if (v->ov == OV_INFO || v->ov == OV_MENU)
		return f->x < v->pop_x + v->pop_w && f->x + f->w > v->pop_x && f->y < v->pop_y + v->pop_h + 4 && f->y + f->h > v->pop_y;
	if (v->ov == OV_HIDDEN || v->ov == OV_MOVE)
		return f->y + f->h > v->H - round((v->H - 60) * 0.66);
	return false;
}

static void ed_hit(struct ctx *c, double x, double y, double w, double h, enum ed_op op, int arg)
{
	hit(c, x, y, w, h, A_ED, op, (uint32_t)(arg + 100));
}

static UiFont btn_font(const struct look *L)
{
	UiFont bf = L->voice == V_WHITE ? fam(L->mono, 600, 0, 11.5f) : L->label;
	if (L->voice == V_WHITE)
		bf.tcase = UI_CASE_UPPER, bf.ls = 0.08f;
	return bf;
}

/* a button (planner.css .pl-btn): 0 plain, 1 primary, 2 off, 3 quiet; its width */
static double ed_btn(struct ctx *c, double x, double y, const char *label, const char *ic, int style, ui_col fg, ui_col bg,
		     enum ed_op op, int arg, int align)
{
	const struct look *L = c->L;
	UiFont bf = btn_font(L);
	double w = tw(c, &bf, label) + (ic ? 50 : 28);
	if (align == 2)
		x -= w;
	if (style == 1)
		box(c, x, y, w, 40, L->ui_r, 0, 0, fg);
	else if (style == 2) {
		if (c->paint) {
			lk_rect(c->cr, x + 0.75, y + 0.75, w - 1.5, 38.5, L->ui_r);
			cairo_set_dash(c->cr, (double[]){ 4, 3 }, 2, 0);
			cairo_set_line_width(c->cr, 1.5);
			col(c->cr, L->soft);
			cairo_stroke(c->cr);
			cairo_set_dash(c->cr, NULL, 0, 0);
		}
	} else if (style == 0)
		box(c, x, y, w, 40, L->ui_r, L->edge, fg, bg);
	ui_col tc_ = style == 1 ? bg : style == 2 ? L->soft : fg;
	double tx = x + w / 2;
	if (ic) {
		icon(c, ic, x + 14 + 9, y + 20, 18, tc_);
		tx += 11;
	}
	tc(c, &bf, label, tx, y + 20, 1, 0, tc_);
	if (style == 3 && c->paint)
		lk_rule(c->cr, x + 14, y + 28, w - 28, 1, tc_, 0);
	if (style != 2)
		ed_hit(c, x, y, w, 40, op, arg);
	return w;
}

static const char *w_title(const struct tnode *n)
{
	return n->title ? n->title : n->wg == W_AGENDA || n->wg == W_CAGENDA ? "Agenda" : W_NAME[n->wg];
}

/* FR-032: how many periods use the template (those holding entries; the default one also every
 * period without a choice) */
static void ed_used(struct editor *e, const char *k, char *o, size_t len)
{
	if (e->fresh) {
		snprintf(o, len, "used by no %s yet", k);
		return;
	}
	if (e->uses < 0)
		e->uses = editor_uses(e->data, e->t, &e->is_default);
	if (e->is_default)
		snprintf(o, len, "the default · used by %d %s%s", e->uses, k, e->uses == 1 ? "" : "s");
	else if (e->uses)
		snprintf(o, len, "used by %d %s%s", e->uses, k, e->uses == 1 ? "" : "s");
	else
		snprintf(o, len, "used by no %s with entries", k);
}

static void ed_head(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	double W = v->W, cy = BAR / 2.0;
	ui_col fg = L->bar_fg, bg = L->bar_bg;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, 0, W, BAR);
		lk_fill(c->cr, bg);
		if (L->bar_rule > 0)
			lk_rule(c->cr, 0, BAR - fmin(L->bar_rule, 1.5), W, fmin(L->bar_rule, 1.5), L->ink, 0);
		else
			lk_rule(c->cr, 0, BAR - 1.5, W, 1.5, L->gilt, 0);
	}
	double x = W - 12;
	x -= ed_btn(c, x, cy - 20, "Save", NULL, e->short_ ? 2 : 1, fg, bg, ED_SAVE, 0, 2) + 8;
	x -= ed_btn(c, x, cy - 20, "Cancel", NULL, 3, fg, bg, ED_CANCEL, 0, 2) + 12;
	char sub[200], used[80];
	const char *k = pkind_name(v->per.kind);
	ed_used(e, k, used, sizeof used);
	snprintf(sub, sizeof sub, "A %s template · shown with this %s’s entries · %s", k, k, e->naming ? "its name, written with the pen" : used);
	UiFont f1 = L->h1, f2 = L->sub;
	double h1, h2, maxw = x - 16;
	ui_text_w(&c->g, &f1, "Ag", &h1);
	ui_text_w(&c->g, &f2, sub, &h2);
	double top = cy - (h1 + 1 + h2) / 2;
	if (e->naming) {
		double fh = 34, fy = top - 4;
		box(c, 16, fy, maxw, fh, L->ui_r, 2, fg, bg);
		struct vfield *f = field(c, F_NAME, 16, fy, maxw, fh);
		if (f)
			snprintf(f->name, sizeof f->name, "tmpl-name:%s", e->t->id ? e->t->id : "");
		ink(c, &e->name, 16, fy, 1);
		if (!e->name.ns)
			tc(c, &f1, e->t->name, 24, fy + fh / 2, 0, maxw - 16, L->soft);
		top = fy + fh + 2 - h1 - 1;
	} else {
		double w = 0;
		if (c->paint)
			ui_text(&c->g, &f1, e->t->name, 16, top, 0, maxw, fg, &h1);
		w = fmin(maxw, tw(c, &f1, e->t->name));
		if (c->paint)
			lk_rule(c->cr, 16, top + h1 + 1, w, 1.5, fg, 2);
		ed_hit(c, 16, 0, fmax(w, 120), cy + 6, ED_NAME, 0);
	}
	if (c->paint) {
		cairo_push_group(c->cr);
		ui_text(&c->g, &f2, sub, 16, top + h1 + 1, 0, maxw, fg, &h2);
		cairo_pop_group_to_source(c->cr);
		cairo_paint_with_alpha(c->cr, 0.85);
	}
}

static void ed_footbar(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	double y = v->H - ED_BAR;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, v->W, ED_BAR);
		lk_fill(c->cr, L->paper);
		lk_rule(c->cr, 0, y, v->W, 1.5, L->ink, 0);
	}
	double x = 12;
	x += ed_btn(c, x, y + 8.5, "Add widget", IC_PLUS, 0, L->ink, L->paper, ED_ADD, 0, 0) + 8;
	x += ed_btn(c, x, y + 8.5, "Remove", IC_TRASH, e->sel ? 0 : 2, L->ink, L->paper, ED_REMOVE, 0, 0) + 8;
	x += ed_btn(c, x, y + 8.5, e->turned ? "Upright" : "Turned", IC_ROTATE, 0, L->ink, L->paper, ED_TURN, 0, 0) + 8;
	if (editor_edited_shipped(e))
		x += ed_btn(c, x, y + 8.5, "Shipped", IC_UNDO, 0, L->ink, L->paper, ED_RESTORE, 0, 0) + 8;
	x += 4;
	UiFont nf = fam(L->mono, 400, 0, 12);
	const char *note = e->notice[0] ? e->notice : e->drop_target ? "release over a widget’s edge to put it there" : e->turned ? "previewing turned"
			   : !editor_widgets(e->t->root) ? "empty: Add widget puts the first one in"
							  : "tap a widget to select it · drag it, or its edge";
	if (e->notice[0])
		nf = fam(L->mono, 600, 0, 12);
	if (c->paint)
		ui_text_wrap(&c->g, &nf, note, x, y + 14, v->W - x - 12, 2, 1.3, L->soft);
}

/* the selected widget's tag (planner.css .pl-ed-tag) */
static void ed_tag(struct ctx *c, const struct placed *p, double ox, double oy)
{
	const struct look *L = c->L;
	const struct tnode *n = p->n;
	char t[96];
	snprintf(t, sizeof t, "%s · weight %g", w_title(n), n->w > 0 ? n->w : 1);
	UiFont tf = fam(L->mono, 500, 0, 12);
	static const char *lb[] = { "‹", "›", "−", "+", "⋯" };
	static const enum ed_op op[] = { ED_BEFORE, ED_AFTER, ED_LESS, ED_MORE, ED_SETTINGS };
	double x = ox + p->x + 10, y = oy + p->y + 42, tww = tw(c, &tf, t), w = 10 + tww + 6 + 5 * 36 + 2;
	if (x + w > c->v->W - 4)
		x = c->v->W - 4 - w;
	box(c, x, y, w, 38, 19, 0, 0, L->ink);
	tc(c, &tf, t, x + 10, y + 19, 0, 0, L->paper);
	double bx = x + 10 + tww + 6;
	UiFont bf = fam(L->text, 500, 0, 16);
	for (int i = 0; i < 5; i++) {
		if (c->paint)
			lk_stroke_box(c->cr, bx, y + 4, 30, 30, 15, 1.5, L->paper);
		tc(c, &bf, lb[i], bx + 15, y + 19, 1, 0, L->paper);
		ed_hit(c, bx - 3, y, 36, 38, op[i], 0);
		bx += 36;
	}
}

static void ed_marks(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	double ox = PAD, oy = BAR + PAD;
	const struct placed *sel = NULL;
	for (int i = 0; i < v->g.n; i++) {
		const struct placed *p = &v->g.p[i];
		if (p->n == e->sel) {
			sel = p;
			continue;
		}
		if (c->paint) {
			lk_rect(c->cr, ox + p->x + 1, oy + p->y + 1, p->w - 2, p->h - 2, L->wg_r);
			cairo_set_dash(c->cr, (double[]){ 4, 3 }, 2, 0);
			cairo_set_line_width(c->cr, 1.5);
			col(c->cr, L->soft);
			cairo_stroke(c->cr);
			cairo_set_dash(c->cr, NULL, 0, 0);
		}
	}
	if (sel && c->paint) {
		double x = ox + sel->x, y = oy + sel->y;
		lk_stroke_box(c->cr, x + 1.75, y + 1.75, sel->w - 3.5, sel->h - 3.5, L->wg_r, 3.5, L->ink);
		lk_rect(c->cr, x + sel->w / 2 - 22, y + sel->h - 6, 44, 12, 6);
		lk_fill(c->cr, L->ink);
		lk_rect(c->cr, x - 6, y + sel->h / 2 - 22, 12, 44, 6);
		lk_fill(c->cr, L->ink);
		lk_rect(c->cr, x + sel->w - 6, y + sel->h / 2 - 22, 12, 44, 6);
		lk_fill(c->cr, L->ink);
	}
	if (sel && !e->drop_target)
		ed_tag(c, sel, ox, oy);
	for (int i = 0; e->drop_target && i < v->g.n; i++) { /* board event.drag move: where it lands */
		const struct placed *p = &v->g.p[i];
		if (p->n != e->drop_target)
			continue;
		double x = ox + p->x, y = oy + p->y, mw = 8;
		double r[4][4] = { { x - mw / 2 - 2, y, mw, p->h }, { x + p->w - mw / 2 + 2, y, mw, p->h },
				   { x, y - mw / 2 - 2, p->w, mw }, { x, y + p->h - mw / 2 + 2, p->w, mw } };
		double *q = r[e->drop_side & 3];
		if (c->paint) {
			lk_stroke_box(c->cr, x + 1.75, y + 1.75, p->w - 3.5, p->h - 3.5, L->wg_r, 2, L->ink);
			cairo_rectangle(c->cr, q[0], q[1], q[2], q[3]);
			lk_fill(c->cr, L->ink);
		}
	}
	if (!v->g.n) {
		double w = v->W - 2 * PAD;
		if (c->paint) {
			lk_rect(c->cr, ox, oy, w, 220, 0);
			cairo_set_dash(c->cr, (double[]){ 6, 4 }, 2, 0);
			cairo_set_line_width(c->cr, 2);
			col(c->cr, L->ink);
			cairo_stroke(c->cr);
			cairo_set_dash(c->cr, NULL, 0, 0);
		}
		UiFont nf = fam(L->text, 400, 0, 15);
		tc(c, &nf, "A new template starts empty: Add widget puts the first one here.", ox + w / 2, oy + 110, 1, w - 40, L->soft);
		ed_hit(c, ox, oy, w, 220, ED_ADD, 0);
	}
	if (e->short_) {
		char t[160], sub[200];
		snprintf(t, sizeof t, "These widgets need %.0f px more than the page has%s.", e->deficit,
			 e->short_turned ? " when the screen is turned" : "");
		snprintf(sub, sizeof sub, "Every widget keeps its minimum, so the template can’t be saved like this: remove a widget or put two side by side.");
		double x = PAD, y = BAR + PAD, w = v->W - 2 * PAD;
		UiFont bf = fam(L->text, 600, 0, 14), sf = fam(L->text, 400, 0, 14);
		UiGc mg = c->g;
		mg.paint = 0;
		double h = 10 + ui_text_wrap(&mg, &bf, t, 0, 0, w - 54, 2, 1.35, L->ink) + ui_text_wrap(&mg, &sf, sub, 0, 0, w - 54, 3, 1.35, L->ink) + 10;
		box(c, x, y, w, h, L->ui_r, 2.5, L->ink, L->paper);
		icon(c, IC_INFO, x + 22, y + 20, 20, L->ink);
		if (c->paint) {
			double hh = ui_text_wrap(&c->g, &bf, t, x + 42, y + 10, w - 54, 2, 1.35, L->ink);
			ui_text_wrap(&c->g, &sf, sub, x + 42, y + 10 + hh, w - 54, 3, 1.35, L->ink);
		}
	}
}

/* a bottom sheet's frame and head (planner.css .pl-sheet); its top */
static double ed_sheet(struct ctx *c, double sh, const char *title, const char *lead)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double y = v->H - ED_BAR - sh, W = v->W;
	if (c->paint) {
		cairo_rectangle(c->cr, 0, y, W, sh);
		lk_fill(c->cr, L->paper);
		lk_rule(c->cr, 0, y, W, 1.5, L->ink, 0);
	}
	UiFont h2 = fpx(L->h1, 21);
	tc(c, &h2, title, 16, y + 14 + 20, 0, W - 140, L->ink);
	ed_btn(c, W - 16, y + 14, "Close", NULL, 0, L->ink, L->paper, ED_KEEP, 0, 2);
	UiFont lf = fam(L->text, 400, 0, 14);
	double lh = ui_text_wrap(&c->g, &lf, lead, 16, y + 66, W - 32, 3, 1.4, L->soft);
	return y + 66 + lh + 12;
}

static void ed_add_sheet(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double sh = round((v->H - BAR - ED_BAR) * 0.6);
	double y = ed_sheet(c, sh, "Add a widget",
			    "It goes after the selected widget; move it elsewhere afterwards. Each one takes its minimum, then grows with its content and its weight.");
	static const int order[] = { W_AGENDA, W_TODO, W_NOTES, W_HABITS, W_MINIMONTH, W_WEEKOV, W_PRIO, W_CAGENDA };
	double cw = (v->W - 32 - 10) / 2, chh = 56;
	UiFont nf = fam(L->text, 600, 0, 15), sf = fam(L->mono, 400, 0, 11);
	for (int i = 0; i < 8; i++) {
		double x = 16 + (i % 2) * (cw + 10), yy = y + (i / 2) * (chh + 10);
		if (yy + chh > v->H - ED_BAR - 4)
			break;
		box(c, x, yy, cw, chh, L->ui_r, L->voice == V_EDITORIAL ? 1 : 1.5, L->ink, L->paper);
		icon(c, IC_W[order[i]], x + 12 + 11, yy + 21, 22, L->ink);
		tc(c, &nf, W_NAME[order[i]], x + 44, yy + 20, 0, cw - 52, L->ink);
		tc(c, &sf, W_SUB[order[i]], x + 44, yy + 39, 0, cw - 52, L->soft);
		ed_hit(c, x, yy, cw, chh, ED_ADD_W, order[i]);
	}
	hit(c, 0, v->H - ED_BAR - sh, v->W, sh, A_NONE, 0, 0); /* the sheet takes its taps */
}

/* a settings row: mode 0 a stepper (a less, b more), 1 a switch (a), 2 a value a tap changes (a) */
static void ed_row(struct ctx *c, double y, const char *t, const char *sub, const char *val, int mode, enum ed_op a,
		   enum ed_op b, bool on)
{
	const struct look *L = c->L;
	double W = c->v->W;
	UiFont tf = fam(L->text, 400, 0, 16), sf = fam(L->text, 400, 0, 12.5f), vf = fam(L->mono, 500, 0, 13);
	tc(c, &tf, t, 16, y + (sub ? 18 : 26), 0, W - 200, L->ink);
	if (sub)
		tc(c, &sf, sub, 16, y + 38, 0, W - 200, L->soft);
	if (mode == 1) {
		double x = W - 16 - 40;
		box(c, x, y + 15, 40, 22, 11, 1.5, L->ink, on ? L->ink : 0);
		box(c, on ? x + 22 : x + 4, y + 19, 14, 14, 7, 0, 0, on ? L->paper : L->soft);
		ed_hit(c, 0, y, W, 52, a, 0);
	} else if (mode == 0) {
		double x = W - 16, vw = fmax(24, tw(c, &vf, val));
		box(c, x - 40, y + 6, 40, 40, L->ui_r, L->edge, L->ink, 0);
		tc(c, &vf, "+", x - 20, y + 26, 1, 0, L->ink);
		ed_hit(c, x - 40, y, 40, 52, b, 1);
		tc(c, &vf, val, x - 40 - 8 - vw / 2, y + 26, 1, 0, L->ink);
		double lx = x - 40 - 16 - vw - 40;
		box(c, lx, y + 6, 40, 40, L->ui_r, L->edge, L->ink, 0);
		tc(c, &vf, "−", lx + 20, y + 26, 1, 0, L->ink);
		ed_hit(c, lx, y, 40, 52, a, -1);
	} else {
		tc(c, &vf, val, W - 16, y + 26, 2, 0, L->ink);
		ed_hit(c, 0, y, W, 52, a, 0);
	}
	rule(c, 16, y + 52, W - 32);
}

static void ed_wset_sheet(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	struct tnode *n = e->sel;
	if (!n)
		return;
	double sh = round((v->H - BAR - ED_BAR) * 0.8);
	char title[96];
	snprintf(title, sizeof title, "%s in this template", w_title(n));
	double y = ed_sheet(c, sh, title, "These change what the widget shows in this template only; no entry changes.");
	char b[32];
	UiFont kf = btn_font(L);
	if (n->wg == W_AGENDA || n->wg == W_CAGENDA) {
		tc(c, &kf, "Hours", 16, y + 8, 0, 0, L->soft);
		y += 18;
		snprintf(b, sizeof b, "%02d", n->from);
		ed_row(c, y, "First hour", "− and + in whole hours", b, 0, ED_FROM, ED_FROM, false);
		y += 54;
		snprintf(b, sizeof b, "%02d", n->to);
		ed_row(c, y, "Last hour", "events outside show as a count at the edge", b, 0, ED_TO, ED_TO, false);
		y += 62;
	}
	tc(c, &kf, "Size", 16, y + 8, 0, 0, L->soft);
	y += 18;
	snprintf(b, sizeof b, "%g", n->w > 0 ? n->w : 1);
	ed_row(c, y, "Weight", "its share of what is left after every minimum", b, 0, ED_LESS, ED_MORE, false);
	y += 54;
	ed_row(c, y, "Takes leftover space", NULL, NULL, 1, ED_GROW, ED_GROW, n->grow);
	y += 62;
	struct tnode *p = editor_parent(e->t->root, n, NULL);
	if (p) {
		enum nkind lk = p->land == N_WIDGET ? p->k : p->land;
		tc(c, &kf, "Turned", 16, y + 8, 0, 0, L->soft);
		y += 18;
		ed_row(c, y, p->k == N_ROW ? "Its row, when the screen is turned" : "Its column, when the screen is turned",
		       "stays as it is, or becomes the other kind", lk == N_ROW ? "Row" : "Column", 2, ED_LAND, ED_LAND, false);
		y += 62;
	}
	if (n->wg == W_TODO || n->wg == W_PRIO || n->wg == W_HABITS || n->wg == W_NOTES) {
		tc(c, &kf, n->wg == W_NOTES ? "For notes" : "For lists", 16, y + 8, 0, 0, L->soft);
		y += 18;
		int per = n->period < 0 ? (int)e->t->kind : n->period;
		static const char *pn[] = { "Day", "Week", "Month" };
		ed_row(c, y, "Period", "of the day, the week or the month", pn[per], 2, ED_PERIOD, ED_PERIOD, false);
		y += 54;
		if (n->wg == W_NOTES && y + 54 < v->H - ED_BAR) {
			snprintf(b, sizeof b, "%s ›", n->slot ? n->slot : "notes");
			ed_row(c, y, "Slot", "the slot’s name, shared by every template that uses it", b, 2, ED_SLOTS, ED_SLOTS, false);
		}
	}
	hit(c, 0, v->H - ED_BAR - sh, v->W, sh, A_NONE, 0, 0);
}

/* board templates.editor slot: the slots in use, and a new one written with the pen */
static void ed_slot_sheet(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	struct tnode *n = e->sel;
	if (!n)
		return;
	double sh = round((v->H - BAR - ED_BAR) * 0.7), bottom = v->H - ED_BAR;
	double y = ed_sheet(c, sh, "Slot for these notes",
			    "Notes are kept per slot. Every template that names a slot shows the same entries; a slot no widget shows keeps its entries, named in the (i).");
	UiFont kf = btn_font(L), tf = fam(L->text, 500, 0, 16), sf = fam(L->text, 400, 0, 12.5f);
	const char *sl[32];
	int ns = editor_slots(e->data, sl, 32);
	int per = n->period < 0 ? (int)e->t->kind : n->period;
	static const char *pn[] = { "day", "week", "month" };
	/* the new slot's field below the list: room kept for it */
	double room = 18 + 6 + 40 + 40;
	tc(c, &kf, "Slots in use", 16, y + 8, 0, 0, L->soft);
	y += 18;
	for (int i = 0; i < ns && y + 52 <= bottom - room; i++) {
		int d = editor_slot_days(e->data, sl[i], per);
		char sub[80];
		if (d)
			snprintf(sub, sizeof sub, "%d %s%s", d, pn[per], d == 1 ? " holds entries" : "s hold entries");
		else
			snprintf(sub, sizeof sub, "no %s holds entries", pn[per]);
		tc(c, &tf, sl[i], 16, y + 18, 0, v->W - 80, L->ink);
		tc(c, &sf, sub, 16, y + 38, 0, v->W - 80, L->soft);
		if (!strcmp(sl[i], n->slot ? n->slot : "notes"))
			tc(c, &tf, "✓", v->W - 16, y + 26, 2, 0, L->ink);
		ed_hit(c, 0, y, v->W, 52, ED_SLOT_PICK, i);
		rule(c, 16, y + 52, v->W - 32);
		y += 54;
	}
	tc(c, &kf, "A new slot", 16, y + 14, 0, 0, L->soft);
	y += 24;
	double fh = 40, fw = v->W - 32;
	box(c, 16, y, fw, fh, L->ui_r, 2, L->ink, L->paper);
	struct vfield *f = field(c, F_SLOT, 16, y, fw, fh);
	if (f)
		snprintf(f->name, sizeof f->name, "tmpl-slot:%s", e->t->id ? e->t->id : "");
	ink(c, &e->slot, 16, y, 1);
	if (!e->slot.ns)
		tc(c, &sf, "write its name with the pen", 26, y + fh / 2, 0, fw - 20, L->soft);
	if (y + fh + 36 <= bottom)
		ui_text_wrap(&c->g, &sf, "Read as you write; the widget shows that slot as soon as the name is read.", 16, y + fh + 8, v->W - 32, 2,
			     1.35, L->soft);
	hit(c, 0, v->H - ED_BAR - sh, v->W, sh, A_NONE, 0, 0);
}

static void ed_dialog(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	struct editor *e = v->ed;
	const char *k = pkind_name(v->per.kind);
	char title[200], body[240], soft[300], all[64];
	if (e->ov == EO_USED) {
		snprintf(title, sizeof title, "Edit “%s” for every %s?", e->t->name, k);
		snprintf(body, sizeof body, "An edit changes every %s that uses this template, past %ss included.", k, k);
		snprintf(soft, sizeof soft, "Copy it first to keep the old arrangement for some %ss; give those %ss the copy. Nothing written on any %s changes either way.", k, k, k);
		snprintf(all, sizeof all, "Edit for every %s", k);
	} else if (e->ov == EO_RESTORE) {
		snprintf(title, sizeof title, "Back to the shipped “%s”?", e->t->name);
		snprintf(body, sizeof body, "Your arrangement is replaced by the one the planner came with, wherever the template is used, past %ss included.", k);
		snprintf(soft, sizeof soft, "Nothing written on any %s changes; what the shipped arrangement doesn’t show is named in the (i). Copy it first to keep your arrangement as a template of its own.", k);
	} else {
		snprintf(title, sizeof title, "Leave without saving?");
		snprintf(body, sizeof body, "The changes to “%s” are dropped; the template stays as it was.", e->t->name);
		snprintf(soft, sizeof soft, "Nothing written on any %s changes either way: the editor never touches an entry.", k);
	}
	double x = 40, w = v->W - 80, y = BAR + 110;
	UiFont hf = fpx(L->h1, 21), bf = fam(L->text, 400, 0, 15), sf = fam(L->text, 400, 0, 13.5f);
	UiGc mg = c->g;
	mg.paint = 0;
	double hh = ui_text_wrap(&mg, &hf, title, 0, 0, w - 40, 2, 1.2, L->ink), bh = ui_text_wrap(&mg, &bf, body, 0, 0, w - 40, 3, 1.45, L->ink),
	       sfh = ui_text_wrap(&mg, &sf, soft, 0, 0, w - 40, 4, 1.45, L->soft);
	double h = 18 + hh + 8 + bh + 8 + sfh + 16 + 40 + 18;
	if (!c->paint)
		v->pop_x = x, v->pop_y = y, v->pop_w = w, v->pop_h = h;
	pop_frame(c, x, y, w, h);
	if (c->paint) {
		ui_text_wrap(&c->g, &hf, title, x + 20, y + 18, w - 40, 2, 1.2, L->ink);
		ui_text_wrap(&c->g, &bf, body, x + 20, y + 18 + hh + 8, w - 40, 3, 1.45, L->ink);
		ui_text_wrap(&c->g, &sf, soft, x + 20, y + 18 + hh + 8 + bh + 8, w - 40, 4, 1.45, L->soft);
	}
	double by = y + h - 18 - 40, bx = x + w - 20;
	if (e->ov == EO_USED) {
		bx -= ed_btn(c, bx, by, all, NULL, 1, L->ink, L->paper, ED_EDIT_ALL, 0, 2) + 10;
		bx -= ed_btn(c, bx, by, "Copy first", IC_COPY, 0, L->ink, L->paper, ED_COPY_FIRST, 0, 2) + 10;
		ed_btn(c, bx, by, "Cancel", NULL, 3, L->ink, L->paper, ED_KEEP, 0, 2);
	} else if (e->ov == EO_RESTORE) {
		bx -= ed_btn(c, bx, by, "Back to the shipped one", NULL, 1, L->ink, L->paper, ED_RESTORE_YES, 0, 2) + 10;
		ed_btn(c, bx, by, "Keep mine", NULL, 0, L->ink, L->paper, ED_KEEP, 0, 2);
	} else {
		bx -= ed_btn(c, bx, by, "Discard changes", NULL, 1, L->ink, L->paper, ED_DISCARD, 0, 2) + 10;
		ed_btn(c, bx, by, "Keep editing", NULL, 0, L->ink, L->paper, ED_KEEP, 0, 2);
	}
	hit(c, 0, 0, v->W, v->H, A_NONE, 0, 0); /* modal: the rest takes no tap */
}

/* FR-044: the template in the view's page, upright and turned */
static void ed_fit(struct ctx *c)
{
	struct view *v = c->v;
	struct editor *e = v->ed;
	bool land = v->W > v->H;
	struct grid g;
	grid_solve(e->t, v->W - 2 * PAD, v->H - BAR - 2 * PAD, land, need_of, c, &g);
	e->short_ = g.short_;
	e->deficit = g.deficit;
	e->short_turned = false;
	double tw, th;
	editor_turned_size(v->W, v->H, &tw, &th);
	grid_solve(e->t, tw - 2 * PAD, th - BAR - 2 * PAD, !land, need_of, c, &g);
	if (g.short_ && !e->short_) {
		e->short_ = true;
		e->deficit = g.deficit;
		e->short_turned = true;
	}
}

static void ed_run(struct ctx *c)
{
	struct view *v = c->v;
	struct editor *e = v->ed;
	v->t = e->t;
	bool land = (v->W > v->H) ^ e->turned;
	double H = v->H;
	if (!c->paint) {
		v->land = land;
		ed_fit(c);
		/* the page lays out as in the view, less the foot bar; its own fields and controls go */
		v->H = H - ED_BAR;
		page(c);
		v->H = H;
		v->nf = 0;
		v->nh = 0;
		v->hidden_count = 0;
		v->pop_w = 0;
		if (e->ov == EO_USED || e->ov == EO_DISCARD || e->ov == EO_RESTORE)
			ed_dialog(c);
		else if (e->ov == EO_ADD)
			ed_add_sheet(c);
		else if (e->ov == EO_WSET)
			ed_wset_sheet(c);
		else if (e->ov == EO_SLOT)
			ed_slot_sheet(c);
		ed_head(c);
		ed_footbar(c);
		ed_marks(c);
		double ox = PAD, oy = BAR + PAD;
		for (int i = 0; i < v->g.n; i++)
			ed_hit(c, ox + v->g.p[i].x, oy + v->g.p[i].y, v->g.p[i].w, v->g.p[i].h, ED_SELECT, i);
		ed_hit(c, 0, BAR, v->W, v->H - BAR - ED_BAR, ED_SELECT, -1);
		return;
	}
	v->H = H - ED_BAR;
	page(c);
	v->H = H;
	ed_marks(c);
	ed_head(c);
	ed_footbar(c);
	if (e->ov == EO_ADD)
		ed_add_sheet(c);
	else if (e->ov == EO_WSET)
		ed_wset_sheet(c);
	else if (e->ov == EO_SLOT)
		ed_slot_sheet(c);
	else if (e->ov == EO_USED || e->ov == EO_DISCARD || e->ov == EO_RESTORE)
		ed_dialog(c);
}
