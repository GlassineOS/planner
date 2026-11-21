// SPDX-License-Identifier: GPL-2.0-or-later
/* One screen of the planner (view.h), after the boards in GlassineOS/ui designs/planner
 * (planner.css, data/planner.js, Planner.dc.html). One function both lays out and paints:
 * with paint off it only measures, collects the fields and the tap targets. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "view.h"

#define PAD 12          /* the page's inset */
#define BAR 60          /* the header bar */
#define GUTTER 40       /* the agenda's hour gutter */
#define INK_SIZE 19.0   /* the size ink is written at in a field; smaller places scale it */
#define TITLE_MIN 11.0  /* the smallest readable size a title shrinks to (the board's 11 px) */
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

static double tw(struct ctx *c, const UiFont *f, const char *s)
{
	double h;
	return ui_text_w(&c->g, f, s, &h);
}

/* text with its vertical centre at cy */
static double tc(struct ctx *c, const UiFont *f, const char *s, double x, double cy, int align, double maxw, ui_col cl)
{
	double h;
	double w = ui_text_w(&c->g, f, s, &h);
	if (c->paint)
		ui_text(&c->g, f, s, x, cy - h / 2, align, maxw, cl, &h);
	return w;
}

/* text with its baseline-ish bottom at by (the line box's bottom) */
static double tb(struct ctx *c, const UiFont *f, const char *s, double x, double by, int align, double maxw, ui_col cl)
{
	double h;
	double w = ui_text_w(&c->g, f, s, &h);
	if (c->paint)
		ui_text(&c->g, f, s, x, by - h, align, maxw, cl, &h);
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
 * from its outline in Autograph; ink that is no word (a drawing) as written */
static void ink(struct ctx *c, const struct field *f, double ox, double oy, double k)
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
	cairo_save(cr);
	col(cr, c->L->pen);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	bool neat = field_neaten(c->s, f);
	for (int i = 0; i < f->ns; i++) {
		const struct pl_stroke *raw = &f->s[i], *s = raw;
		if (neat && field_neat(f, raw->k) && field_neat(f, raw->k)->n)
			s = field_neat(f, raw->k);
		if (!s->n)
			continue;
		int wi = field_word_of(f, raw->k);
		double dx = wi >= 0 ? f->w[wi].dx * k : 0, dy = wi >= 0 ? f->w[wi].dy * k : 0;
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
		if (!w->set || !w->npath)
			continue;
		cairo_new_path(cr);
		/* ops as GHW_PATH_*: 0 move, 1 line, 2 quad, 3 cubic, 4 close, each followed by its
		 * coordinates (region coordinates) */
		for (int j = 0; j < w->npath;) {
			int op = (int)lroundf(w->path[j++]);
#define PX(n) (ox + w->path[j + (n)] * k)
#define PY(n) (oy + w->path[j + (n)] * k)
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
		if (v->per.kind == P_WEEK && day >= wk.start && day <= wk.start + 6)
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
	} else {
		char tag[32];
		snprintf(tag, sizeof tag, "%02d:00–%02d:00", b->n->from, b->n->to);
		head(c, b, "Agenda", tag, hh);
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
	double by = b->y + hh, gut = compact ? 16 : GUTTER;
	if (c->paint) {
		cairo_save(c->cr);
		lk_rect(c->cr, b->x, b->y, b->w, b->h, L->wg_r);
		cairo_clip(c->cr);
	}
	for (int h = ws; h <= we; h++) {
		double y = round(by + top + (h - ws) * pitch);
		if (c->paint)
			lk_rule(c->cr, b->x, y, b->w, 1, L->rule, 0);
		if (h < we) {
			double hy = round(by + top + (h - ws + 0.5) * pitch);
			if (c->paint)
				lk_rule(c->cr, b->x + gut, hy, b->w - gut, 1, L->rule, 2);
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
				ui_text_w(&c->g, &L->small, t, &th);
				if (c->paint)
					ui_text(&c->g, &L->small, t, b->x + 6, y + 2, 0, 0, L->soft, &th);
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
		struct vfield *f = field(c, F_FREE, b->x + GUTTER, by + top, b->w - GUTTER - 6, (we - ws) * pitch);
		if (f) {
			snprintf(f->name, sizeof f->name, "free:%d:%d", day, (int)(b->x * 1000 + b->y));
			f->per = (struct period){ P_DAY, day };
			f->min0 = ws * 60;
			f->mpp = 60 / pitch;
		}
	}
	/* now */
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
		if (now >= ws * 60 && now <= we * 60) {
			double ny = by + top + (now / 60.0 - ws) * pitch;
			if (c->paint) {
				lk_rule(c->cr, b->x + 34, ny - 1, b->w - 34, 2, L->ink, 0);
				cairo_arc(c->cr, b->x + 34, ny, 4, 0, 2 * M_PI);
				lk_fill(c->cr, L->ink);
			}
		}
	}
	/* timed events as blocks */
	int before = 0, after = 0;
	for (int i = 0; i < ne; i++) {
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
		double bx = b->x + gut, bw = b->w - gut - (compact ? 2 : 6), byy = round(y1) + 1, bh = fmax(8, round(y2 - y1) - 2);
		/* the title's room (FR-018): its block, and the free time below it down to the next
		 * event or the agenda's end. A title that needs more grows its block into that room
		 * (a dotted lower edge); one that doesn't fit shrinks evenly down to the smallest
		 * readable size, then is cut at the block's edge */
		double gh = bh, k = 1, fh = bh;
		bool cut = false, grown = false;
		if (!compact) {
			double limit = by + top + (we - ws) * pitch;
			for (int j = 0; j < ne; j++) {
				if (j == i || ev[j]->allday)
					continue;
				int sj = (ev[j]->per.start - day) * 1440 + ev[j]->start;
				if (sj >= e0 && sj < we * 60)
					limit = fmin(limit, by + top + (sj / 60.0 - ws) * pitch);
			}
			double room = fmax(bh, round(limit) - byy - 1), r, bt;
			field_extent(&x->f, &r, &bt);
			double needw = r + 4, needh = bt + 4;
			if (needw > bw || needh > bh) {
				k = fmin(1, fmin(bw / fmax(needw, 1), room / fmax(needh, 1)));
				if (k < TITLE_MIN / INK_SIZE) {
					k = TITLE_MIN / INK_SIZE;
					cut = true;
				}
				gh = fmin(room, fmax(bh, ceil(needh * k)));
				grown = gh > bh;
			}
			/* while written, the field reaches a line below its ink, into the room */
			fh = k < 1 ? gh : fmin(room, fmax(gh, bt > bh - 6 ? bt + PL_ROW : bh));
		}
		box(c, bx, byy, bw, gh, compact ? fmin(L->ui_r, 8) : L->ui_r, L->voice == V_STATIONERY ? 1 : L->voice == V_EDITORIAL ? 1 : 1.5,
		    L->voice == V_STATIONERY ? L->soft : L->ink, L->paper);
		if (grown && c->paint) { /* the grown part's lower edge is dotted */
			double lw = L->voice == V_STATIONERY || L->voice == V_EDITORIAL ? 1 : 1.5;
			cairo_rectangle(c->cr, bx, byy + gh - 3, bw, 3);
			lk_fill(c->cr, L->paper);
			lk_rule(c->cr, bx, byy + gh - 2, bw, 2, L->ink, 2);
			cairo_rectangle(c->cr, bx, byy + gh - 3, lw, 3);
			cairo_rectangle(c->cr, bx + bw - lw, byy + gh - 3, lw, 3);
			lk_fill(c->cr, L->voice == V_STATIONERY ? L->soft : L->ink);
		}
		if (L->ev_bar)
			box(c, bx, byy, 3, gh, 0, 0, 0, L->ink);
		if (!compact && y2 - y1 >= 24) {
			char w1[8], w2[8], when[20];
			hm(w1, sizeof w1, s0 < 0 ? 0 : s0);
			hm(w2, sizeof w2, e0);
			snprintf(when, sizeof when, "%s–%s", w1, w2);
			UiFont wf = fam(L->mono, 400, 0, 10);
			double th;
			ui_text_w(&c->g, &wf, when, &th);
			if (c->paint)
				ui_text(&c->g, &wf, when, bx + bw - 6, byy + 3, 2, 0, L->soft, &th);
		}
		if (compact) {
			ink_fit(c, &x->f, bx + 3, byy + 1, bw - 4, fmin(bh - 2, 15));
			hit(c, bx, byy, bw, bh, A_DAY, day, 0);
		} else {
			struct vfield *ef = entry_field(c, x, bx, byy, bw, fh, 0, 0);
			if (ef)
				ef->layer = 1;
			if (c->paint) {
				cairo_save(c->cr);
				cairo_rectangle(c->cr, bx + 1, byy, bw - 2, gh);
				cairo_clip(c->cr);
				ink(c, &x->f, bx, byy, k);
				cairo_restore(c->cr);
			}
			hit(c, bx, byy, bw, gh, A_EVENT, cut, x->id);
		}
	}
	/* an event being dragged (FR-018): a dashed outline where it was, and in the gutter the
	 * time it would land on */
	struct entry *dg = v->drag && !compact ? store_get(c->s, v->drag) : NULL;
	if (dg && dg->per.start == day && c->paint) {
		double y1 = by + top + (v->drag_s0 / 60.0 - ws) * pitch, y2 = by + top + (v->drag_e0 / 60.0 - ws) * pitch;
		double bx = b->x + gut, bw = b->w - gut - 6;
		cairo_save(c->cr);
		double dash[] = { 4, 3 };
		cairo_set_dash(c->cr, dash, 2, 0);
		lk_stroke_box(c->cr, bx, round(y1) + 1, bw, fmax(8, round(y2 - y1) - 2), L->ui_r, 1, L->soft);
		cairo_restore(c->cr);
		char t[8];
		hm(t, sizeof t, dg->start);
		UiFont rf = fam(L->mono, 600, 0, 10);
		double th, w = ui_text_w(&c->g, &rf, t, &th);
		double ry = by + top + (dg->start / 60.0 - ws) * pitch;
		lk_rect(c->cr, b->x + 2, ry - th / 2 - 2, w + 6, th + 4, 3);
		lk_fill(c->cr, L->ink);
		ui_text(&c->g, &rf, t, b->x + 5, ry - th / 2, 0, 0, L->paper, &th);
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
	if (!c->paint)
		grid_solve(v->t, pw, ph, v->land, need_of, c, &v->g);
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
				ink_fit(c, &h->f, 16, yy + 6, W - 260, 34);
		} else if (c->paint) {
			cairo_save(c->cr);
			cairo_rectangle(c->cr, 16, yy, W - 260, rh);
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
		double dw = tw(c, &af, "Delete");
		double dx = W - 16 - 6 - dw;
		tc(c, &af, "Delete", dx, yy + rh / 2 + 4, 0, 0, L->ink);
		if (c->paint)
			lk_rule(c->cr, dx, yy + rh / 2 + 4 + 9, dw, 1, L->ink, 0);
		hit(c, dx - 6, yy, dw + 12, rh, A_CLOSE, 1, x->id); /* arg 1: delete */
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
	struct entry *e = f->k == F_ENTRY ? store_get(c->s, f->entry) : NULL;
	return e && e->f.value ? e->f.value : "";
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
	if (f) {
		double r, b;
		field_extent(&e->f, &r, &b);
		double w = fmin(v->W - 16, fmax(f->w, r + 20)), h = b + 16, x = fmin(f->x, v->W - 8 - w), y = fmin(f->y, v->H - 8 - h);
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

static void picker_edit_acts(struct ctx *c, double y)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	int nh = v->nh;
	double x = 16;
	static const struct { const char *t; int arg; } A[] = { { "New", 1 }, { "Copy", 2 }, { "Edit", 0 } };
	const char *ic[] = { IC_PLUS, IC_COPY, IC_EDIT };
	for (int i = 0; i < 3; i++) {
		x += ed_btn(c, x, y, A[i].t, ic[i], 0, L->ink, L->paper, ED_SELECT, 0, 0) + 8;
		if (!c->paint && v->nh > nh) /* the button's target opens the editor */
			v->hit[v->nh - 1].a = A_EDIT, v->hit[v->nh - 1].arg = A[i].arg, v->hit[v->nh - 1].id = 0;
		nh = v->nh;
	}
}

static void picker(struct ctx *c)
{
	const struct look *L = c->L;
	struct view *v = c->v;
	double W = v->W;
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
	/* the scope: what the choice applies to */
	char only[80], from[80];
	snprintf(only, sizeof only, "Only %s", v->per.kind == P_DAY ? main : v->per.kind == P_WEEK ? "this week" : "this month");
	snprintf(from, sizeof from, "From %s on", v->per.kind == P_DAY ? main : v->per.kind == P_WEEK ? "this week" : "this month");
	const char *sc[] = { only, from, "The default" };
	const int scv[] = { 0, 2, 3 };
	double x = 16, y = 102;
	for (int i = 0; i < 3; i++) {
		double w = tw(c, &lf, sc[i]) + 24;
		bool on = v->scope == scv[i];
		box(c, x, y, w, 38, L->ui_r, 1.5, L->ink, on ? L->ink : 0);
		tc(c, &lf, sc[i], x + 12, y + 19, 0, 0, on ? L->paper : L->ink);
		hit(c, x, y, w, 38, A_SCOPE, scv[i], 0);
		x += w + 8;
	}
	y += 38 + 22;
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
		if (tp == tmpl_default(v->per.kind) && !c->s->def[v->per.kind][0])
			tb(c, &(UiFont){ L->mono, 400, 0, 11, 0, 0, UI_CASE_NONE }, "the default", 114, ty + h1 + 3 + h2 + 16, 0, 0, L->soft);
		else if (!strcmp(c->s->def[v->per.kind], tp->id))
			tb(c, &(UiFont){ L->mono, 400, 0, 11, 0, 0, UI_CASE_NONE }, "the default", 114, ty + h1 + 3 + h2 + 16, 0, 0, L->soft);
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

static void run(struct view *v, struct store *s, const struct look *L, cairo_t *cr, int paint)
{
	struct ctx c = { v, s, L, cr, { cr, paint, L->uv }, paint };
	if (!paint) {
		v->nf = 0;
		v->nh = 0;
		int how = 3;
		const char *id = store_template(s, v->per, &how);
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
	if (!paint) {
		/* the overlays' targets first: they lie on top */
		if (v->tm)
			tbar(&c);
		if (v->ov == OV_TITLE)
			title_card(&c);
		if (v->ov == OV_INFO || v->ov == OV_MENU)
			popover(&c);
		if (v->ov == OV_HIDDEN)
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
	if (v->ov == OV_HIDDEN)
		sheet_hidden(&c);
	if (v->ov == OV_SETTINGS)
		settings(&c);
	if (v->ov == OV_TITLE)
		title_card(&c);
	if (v->tm)
		text_fields(&c);
	cairo_restore(cr);
	if (v->tm)
		tbar(&c);
}

void view_layout(struct view *v, struct store *s, const struct look *L, cairo_t *measure)
{
	ui_draw_init();
	run(v, s, L, measure, 0);
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
	if (v->ov != OV_NONE)
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
	char sub[160];
	const char *k = pkind_name(v->per.kind);
	snprintf(sub, sizeof sub, "A %s template · shown with this %s’s entries · %s", k, k,
		 e->naming ? "its name, written with the pen" : e->fresh ? "used by no period yet" : "applies wherever it is used");
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
	x += ed_btn(c, x, y + 8.5, e->turned ? "Upright" : "Turned", IC_ROTATE, 0, L->ink, L->paper, ED_TURN, 0, 0) + 12;
	UiFont nf = fam(L->mono, 400, 0, 12);
	const char *note = e->turned ? "previewing turned" : !editor_widgets(e->t->root) ? "empty: Add widget puts the first one in"
								  : "tap a widget to select it · drag it, or its edge";
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
	if (sel)
		ed_tag(c, sel, ox, oy);
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
			ed_row(c, y, "Slot", "the slot’s name, shared by every template that uses it", b, 2, ED_SLOT, ED_SLOT, false);
		}
	}
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
	grid_solve(e->t, v->H - 2 * PAD, v->W - BAR - 2 * PAD, !land, need_of, c, &g);
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
		if (e->ov == EO_USED || e->ov == EO_DISCARD)
			ed_dialog(c);
		else if (e->ov == EO_ADD)
			ed_add_sheet(c);
		else if (e->ov == EO_WSET)
			ed_wset_sheet(c);
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
	else if (e->ov == EO_USED || e->ov == EO_DISCARD)
		ed_dialog(c);
}
