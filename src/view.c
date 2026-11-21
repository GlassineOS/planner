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
	v->hit[v->nh++] = (struct vhit){ x, y, w, h, a, arg, id };
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

/* the ink of a field at ox, oy, scaled by k (1 in its own field: ink is never scaled there) */
static void ink(struct ctx *c, const struct field *f, double ox, double oy, double k)
{
	if (!c->paint || !f->ns)
		return;
	cairo_t *cr = c->cr;
	cairo_save(cr);
	col(cr, c->L->pen);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	for (int i = 0; i < f->ns; i++) {
		const struct pl_stroke *s = &f->s[i];
		if (!s->n)
			continue;
		double p = 0;
		for (int j = 0; j < s->n; j++)
			p += s->pt[j].p;
		p /= s->n;
		cairo_set_line_width(cr, fmax(0.9, (1.3 + 1.2 * p) * (k < 1 ? fmax(0.6, k) : 1)));
		cairo_move_to(cr, ox + s->pt[0].x * k, oy + s->pt[0].y * k);
		if (s->n == 1)
			cairo_line_to(cr, ox + s->pt[0].x * k + 0.01, oy + s->pt[0].y * k);
		for (int j = 1; j < s->n; j++)
			cairo_line_to(cr, ox + s->pt[j].x * k, oy + s->pt[j].y * k);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* a field's ink in a place smaller than where it was written: fitted to the line height h */
static void ink_fit(struct ctx *c, const struct field *f, double x, double y, double w, double h)
{
	double r, b;
	field_extent(f, &r, &b);
	double top = 1e9;
	for (int i = 0; i < f->ns; i++)
		for (int j = 0; j < f->s[i].n; j++)
			if (f->s[i].pt[j].y < top)
				top = f->s[i].pt[j].y;
	if (!f->ns || b <= top)
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
		box(c, bx, byy, bw, bh, compact ? fmin(L->ui_r, 8) : L->ui_r, L->voice == V_STATIONERY ? 1 : L->voice == V_EDITORIAL ? 1 : 1.5,
		    L->voice == V_STATIONERY ? L->soft : L->ink, L->paper);
		if (L->ev_bar)
			box(c, bx, byy, 3, bh, 0, 0, 0, L->ink);
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
			struct vfield *ef = entry_field(c, x, bx, byy, bw, bh, 0, 0);
			if (ef)
				ef->layer = 1;
			if (c->paint) {
				cairo_save(c->cr);
				cairo_rectangle(c->cr, bx + 1, byy, bw - 2, bh);
				cairo_clip(c->cr);
				ink(c, &x->f, bx, byy, 1);
				cairo_restore(c->cr);
			}
			hit(c, bx, byy, bw, bh, A_EVENT, 0, x->id);
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
			{ IC_EDIT, "Edit this template", "add, move and resize widgets", 0, A_NONE },
			{ IC_SLIDERS, "Settings", "how ink is shown, the agenda’s hours", 0, A_NONE },
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
	for (int i = 0; i < pl_ntemplates; i++) {
		const struct tmpl *tp = pl_templates[i];
		if (tp->kind != v->per.kind)
			continue;
		bool use = tp == ct;
		double rh = 112;
		thumb(c, tp, 16, y, 84, rh - 2, use);
		UiFont nf = fam(L->head, 600, 0, 17), df = fam(L->text, 400, 0, 13);
		double h1, h2;
		ui_text_w(&c->g, &nf, tp->name, &h1);
		UiGc mg = c->g;
		mg.paint = 0;
		h2 = ui_text_wrap(&mg, &df, tp->note, 0, 0, W - 16 - 114 - 90, 2, 0, L->soft);
		double ty = y + (rh - h1 - 3 - h2 - 18) / 2;
		if (c->paint) {
			ui_text(&c->g, &nf, tp->name, 114, ty, 0, W - 220, L->ink, &h1);
			ui_text_wrap(&c->g, &df, tp->note, 114, ty + h1 + 3, W - 16 - 114 - 90, 2, 0, L->soft);
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
	hit(c, 0, 0, W, v->H, A_NONE, 0, 0); /* it covers the page: after its own targets */
}

/* ---- the whole screen ---- */

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
	if (v->ov == OV_PICKER) {
		picker(&c);
		return;
	}
	if (!paint) {
		/* the overlays' targets first: they lie on top */
		if (v->ov == OV_INFO || v->ov == OV_MENU)
			popover(&c);
		if (v->ov == OV_HIDDEN)
			sheet_hidden(&c);
		bar(&c);
		page(&c);
		return;
	}
	page(&c);
	bar(&c);
	if (v->ov == OV_INFO || v->ov == OV_MENU)
		popover(&c);
	if (v->ov == OV_HIDDEN)
		sheet_hidden(&c);
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
		if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h)
			return h;
	}
	return NULL;
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
