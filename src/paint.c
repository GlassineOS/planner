// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The view, painted with GlassineOS/ui in the current voice: the header, the notice
 * of what isn't shown (FR-023), the widgets in their layout rectangles (day agenda,
 * to-do list, free notes, mini month) and the debug overlay. Every tappable thing
 * becomes a hit, every handwriting field an fbox, every widget a region (plan.h).
 *
 * The widgets' sizes (FR-041) live here next to their drawing, so what a widget asks
 * for and what it draws can't drift apart: widget_want_h measures with the same row
 * rules paint uses.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plan.h"

#define HEAD_H 56
#define NOTICE_H 28
#define PAD 6        /* half the gap between widgets */
#define IN 10        /* inside a widget's card */
#define LBL_H 24     /* a widget's title row */
#define ROW 36       /* a to-do row, a notes line */
#define LINE 34      /* ruled line pitch for notes */
#define CHECK_W 28

static struct plan *P;
static UiGc G;
static ui_col INK, SUB, PAPER;

static void hit(struct rect r, enum act a, int x, int y)
{
	if (G.paint && P->nhits < MAX_HITS)
		P->hits[P->nhits++] = (struct hit){ r, a, x, y };
}

static void fbox(struct rect r, double ox, double oy, int entry, enum ekind k, struct period per, int wi)
{
	if (G.paint && P->nfb < MAX_FIELDS)
		P->fb[P->nfb++] = (struct fbox){ r, ox, oy, entry, k, per, wi };
}

static void region(struct rect r)
{
	if (G.paint && P->nregions < (int)(sizeof P->regions / sizeof *P->regions))
		P->regions[P->nregions++] = r;
}

static double text_h(const UiFont *f)
{
	double h = 0;
	ui_text_w(&G, f, "Ag", &h);
	return h;
}

static UiFont field_font(void)
{
	return ui_font_with(&P->v->titleFont, 400);
}

static double btn_r(void)
{
	return P->v->btn.r[0] == UI_HALF ? 20 : P->v->btn.r[0];
}

static bool is_text(const struct entry *e)
{
	return P->text_view || e->f.textmode;
}

/* ---- entries a widget shows ------------------------------------------------------- */

static struct period widget_period(const struct widget *w)
{
	if (w->type == W_AGENDA)
		return (struct period){ P_DAY, P->per.start + w->day };
	return period_of(w->per, P->per.start + (w->type == W_TODO || w->type == W_NOTES ? 0 : 0));
}

static int todo_cmp(const void *a, const void *b)
{
	const struct entry *x = *(const struct entry *const *)a, *y = *(const struct entry *const *)b;
	return x->order - y->order;
}

/* the to-dos (or notes) of a widget, in order; returns the count */
static int entries_of(const struct widget *w, enum ekind k, struct entry **out, int max)
{
	struct period per = widget_period(w);
	int n = 0;
	for (int i = 0; i < P->st.n && n < max; i++) {
		struct entry *e = &P->st.e[i];
		if (e->deleted || e->kind != k)
			continue;
		if (k == E_EVENT) {
			if (e->per.start == per.start)
				out[n++] = e;
		} else if (e->per.kind == per.kind && e->per.start == per.start)
			out[n++] = e;
	}
	if (k == E_TODO)
		qsort(out, n, sizeof *out, todo_cmp);
	return n;
}

/* ---- measuring ------------------------------------------------------------------------ */

static double field_text_h(const struct entry *e, double w)
{
	UiFont f = field_font();
	const char *t = field_text(&e->f);
	int saved = G.paint;
	G.paint = 0;
	double h = ui_text_wrap(&G, &f, *t ? t : " ", 0, 0, w, 6, 1.3, INK);
	G.paint = saved;
	return h;
}

static double todo_row_h(const struct entry *e, double fw)
{
	if (is_text(e))
		return fmax(ROW, field_text_h(e, fw - 8) + 14);
	double r, b;
	field_extent(&e->f, &r, &b);
	return fmax(ROW, ceil(b + 8)); /* FR-047: the row is never shorter than its ink */
}

static double notes_body_h(const struct entry *e, double w)
{
	if (!e)
		return 3 * LINE;
	if (is_text(e))
		return fmax(3 * LINE, field_text_h(e, w - 8) + 16);
	double r, b;
	field_extent(&e->f, &r, &b);
	/* the ink and one free line under it, at least three lines */
	return fmax(3 * LINE, (floor(b / LINE) + 2) * LINE);
}

static double agenda_pitch_pref(const struct widget *w)
{
	return w->compact ? 26 : 44;
}

static double agenda_head(const struct widget *w)
{
	return w->compact ? 40 : LBL_H;
}

double widget_want_h(void *ud, double width)
{
	struct widget *w = ud;
	double inner = width - 2 * PAD - 2 * IN;
	switch (w->type) {
	case W_AGENDA: {
		/* an agenda's content is its events: an empty day asks for its minimum pitch, a
		 * busy one (four events or more in its hours) for the full one */
		struct entry *ev[64];
		int n = entries_of(w, E_EVENT, ev, 64), in_range = 0;
		for (int i = 0; i < n; i++)
			in_range += ev[i]->end > w->from * 60 && ev[i]->start < w->to * 60;
		double lo = w->compact ? 12 : 18, busy = fmin(1, in_range / 4.0);
		return 2 * PAD + 2 * IN + agenda_head(w) + (w->to - w->from) * (lo + (agenda_pitch_pref(w) - lo) * busy);
	}
	case W_TODO: {
		struct entry *l[STORE_MAX];
		int n = entries_of(w, E_TODO, l, STORE_MAX);
		double h = 2 * PAD + 2 * IN + LBL_H;
		for (int i = 0; i < n; i++)
			h += todo_row_h(l[i], inner - CHECK_W);
		return h + ROW; /* the empty row to write a new one */
	}
	case W_NOTES: {
		struct entry *l[4];
		int n = entries_of(w, E_NOTE, l, 4);
		return 2 * PAD + 2 * IN + LBL_H + notes_body_h(n ? l[0] : NULL, inner);
	}
	case W_MINIMONTH:
		return 2 * PAD + 2 * IN + LBL_H + 7 * 22;
	default:
		return 0;
	}
}

/* ---- pieces ---------------------------------------------------------------------------- */

static void card(struct rect r)
{
	const UiVoice *v = P->v;
	UiBox b = { .bg = v->card.bg ? v->card.bg : PAPER, .bw = 1.5, .bc = INK };
	for (int i = 0; i < 4; i++)
		b.r[i] = (float)fmin(btn_r(), 14);
	ui_box(&G, r.x, r.y, r.w, r.h, &b);
}

static void label(const char *s, double x, double y, double w)
{
	ui_text(&G, &P->v->lbl, s, x, y + (LBL_H - 4 - text_h(&P->v->lbl)) / 2, 0, w, P->v->lblC, NULL);
}

static void button(struct rect r, const char *txt, bool on, enum act a, int arg)
{
	ui_box(&G, r.x, r.y, r.w, r.h, on ? &P->v->sel : &P->v->btn);
	const UiFont *f = &P->v->titleFont;
	ui_text(&G, f, txt, r.x + r.w / 2, r.y + (r.h - text_h(f)) / 2, 1, r.w - 6, on ? P->v->selFg : INK, NULL);
	hit(r, a, arg, 0);
}

static void more_mark(double x, double y, double w, int n, int wi)
{
	char s[32];
	snprintf(s, sizeof s, n > 0 ? "+%d more" : "more", n);
	UiFont f = ui_font_px(&P->v->numFont, 12);
	double tw = ui_text_w(&G, &f, s, NULL) + 14;
	struct rect r = { x + w - tw, y, tw, 20 };
	ui_stroke_rrect(&G, r.x, r.y, r.w, r.h, 10, 1, INK, 0);
	ui_text(&G, &f, s, r.x + r.w / 2, r.y + (20 - text_h(&f)) / 2, 1, 0, INK, NULL);
	hit(r, A_MORE, 0, wi);
}

void paint_stroke_segment(struct plan *p, cairo_t *cr, double ox, double oy, const struct pt *a, const struct pt *b)
{
	double lw = 1.1 + 1.6 * (a->p + b->p) / 2;
	ui_col c = p->v->ink;
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, 1);
	cairo_set_line_width(cr, lw);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, ox + a->x, oy + a->y);
	cairo_line_to(cr, ox + b->x, oy + b->y);
	cairo_stroke(cr);
}

/* a field's ink, anchored at ox, oy, clipped to clip (never scaled: FR-047) */
static void ink(const struct field *f, double ox, double oy, struct rect clip)
{
	if (!G.paint || !f->ns)
		return;
	cairo_save(G.cr);
	cairo_rectangle(G.cr, clip.x, clip.y, clip.w, clip.h);
	cairo_clip(G.cr);
	for (int i = 0; i < f->ns; i++) {
		const struct stroke *s = &f->s[i];
		if (s->n == 1)
			paint_stroke_segment(P, G.cr, ox, oy, &s->pt[0], &s->pt[0]);
		for (int k = 1; k < s->n; k++)
			paint_stroke_segment(P, G.cr, ox, oy, &s->pt[k - 1], &s->pt[k]);
	}
	cairo_restore(G.cr);
}

/* a field in text mode: its current text as a plain text field */
static void text_field(const struct entry *e, struct rect r)
{
	bool focus = P->focus == e->id;
	ui_stroke_rrect(&G, r.x, r.y + 2, r.w, r.h - 4, 6, focus ? 2 : 1, focus ? INK : SUB, focus ? 0 : 1);
	UiFont f = field_font();
	const char *t = field_text(&e->f);
	double h = ui_text_wrap(&G, &f, *t ? t : " ", r.x + 6, r.y + 7, r.w - 12, 6, 1.3, INK);
	if (focus && G.paint) {
		/* the caret after the text (the spike edits at the end only) */
		double lh = text_h(&f), cw = 0;
		const char *nl = t;
		(void)nl;
		cw = ui_text_w(&G, &f, t, NULL);
		int lines = (int)round(h / (f.px * 1.3));
		double cx = lines <= 1 ? r.x + 6 + cw + 1 : r.x + r.w - 10;
		ui_fill_rect(&G, fmin(cx, r.x + r.w - 8), r.y + 7, 1.5, lh, INK);
	}
}

/* ---- the agenda ------------------------------------------------------------------------ */

static double minute_y(const struct widget *w, struct rect body, double pitch, int min)
{
	return body.y + (min - w->from * 60) / 60.0 * pitch;
}

static void paint_agenda(int wi, struct rect r)
{
	struct widget *w = &P->w[wi];
	const UiVoice *v = P->v;
	struct rect in = { r.x + IN, r.y + IN, r.w - 2 * IN, r.h - 2 * IN };
	struct period day = widget_period(w);
	int y, m, d;
	day_to_ymd(day.start, &y, &m, &d);
	static const char *const wd[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	if (w->compact) {
		char s[8];
		snprintf(s, sizeof s, "%d", d);
		label(wd[day_weekday(day.start)], in.x, in.y, in.w);
		UiFont nf = ui_font_px(&v->headFont, 18);
		bool today = day.start == P->today;
		if (today)
			ui_rrect(&G, in.x - 2, in.y + 18, 30, 22, 6, UI_HIGHLIGHT);
		ui_text(&G, &nf, s, in.x, in.y + 18, 0, in.w, INK, NULL);
		hit((struct rect){ in.x, in.y, in.w, 40 }, A_DAY, day.start, 0);
	} else {
		label(w->title[0] ? w->title : "Agenda", in.x, in.y, in.w);
	}
	double gutter = w->compact ? 0 : 30;
	struct rect body = { in.x + gutter, in.y + agenda_head(w), in.w - gutter, in.h - agenda_head(w) };
	double pitch = body.h / (w->to - w->from);
	w->body = body;
	w->pitch = pitch;
	UiFont hf = ui_font_px(&v->numFont, w->compact ? 9 : 11);
	for (int hr = w->from; hr <= w->to; hr++) {
		double yy = body.y + (hr - w->from) * pitch;
		ui_hline(&G, body.x, yy, body.w, 1, hr == w->from || hr == w->to ? INK : UI_RULE, hr == w->from || hr == w->to ? 0 : 2);
		if (!w->compact && hr < w->to) {
			char s[8];
			snprintf(s, sizeof s, "%d", hr);
			ui_text(&G, &hf, s, in.x + gutter - 6, yy + 2, 2, 0, SUB, NULL);
		} else if (w->compact && hr < w->to && wi == 0) {
			/* nothing: compact columns carry no labels */
		}
	}
	/* drawing a block in the free time = a new event (FR-017) */
	hit(body, A_AGENDA, day.start, wi);
	struct entry *ev[64];
	int n = entries_of(w, E_EVENT, ev, 64), above = 0, below = 0;
	w->more = 0;
	for (int i = 0; i < n; i++) {
		struct entry *e = ev[i];
		int s = e->start, en = e->end;
		bool drag = P->down && P->press.a == e->id && (P->press.act == A_EV_MOVE || P->press.act == A_EV_SIZE);
		if (drag) {
			s = P->drag_start;
			en = P->drag_end;
		}
		if (en <= w->from * 60) {
			above++;
			continue;
		}
		if (s >= w->to * 60) {
			below++;
			continue;
		}
		double y0 = fmax(body.y, minute_y(w, body, pitch, s)), y1 = fmin(body.y + body.h, minute_y(w, body, pitch, en));
		struct rect b = { body.x + 2, y0 + 1, body.w - 4, fmax(8, y1 - y0 - 2) };
		UiBox box = { .bg = PAPER, .bw = drag ? 2.5 : 1.5, .bc = INK, .dashed = drag };
		for (int k = 0; k < 4; k++)
			box.r[k] = 5;
		ui_box(&G, b.x, b.y, b.w, b.h, &box);
		double grip = w->compact ? 7 : 9;
		/* the grip (move) along the top, the lower edge (stretch) */
		ui_fill_rect(&G, b.x + b.w / 2 - 9, b.y + 3, 18, 2, SUB);
		hit((struct rect){ b.x, b.y - 2, b.w, grip + 2 }, A_EV_MOVE, e->id, wi);
		hit((struct rect){ b.x, b.y + b.h - 7, b.w, 12 }, A_EV_SIZE, e->id, wi);
		struct rect fr = { b.x + 3, b.y + grip, b.w - 6, fmax(4, b.h - grip - 6) };
		if (!w->compact) {
			char t[24];
			snprintf(t, sizeof t, "%d:%02d", s / 60, s % 60);
			UiFont tf = ui_font_px(&v->numFont, w->compact ? 9 : 10);
			ui_text(&G, &tf, t, b.x + b.w - 4, b.y + 3, 2, 0, SUB, NULL);
		}
		if (is_text(e))
			text_field(e, fr);
		else {
			double ir, ib;
			field_extent(&e->f, &ir, &ib);
			ink(&e->f, fr.x, fr.y, fr);
			if (ib > fr.h + 2 || ir > fr.w + 2) {
				/* the ink is bigger than the time allows: clipped, marked */
				ui_fill_rect(&G, b.x + b.w - 12, b.y + b.h - 6, 8, 2, INK);
				ui_fill_rect(&G, b.x + b.w - 22, b.y + b.h - 6, 8, 2, INK);
			}
		}
		fbox(fr, fr.x, fr.y, e->id, E_EVENT, day, wi);
	}
	/* a block being drawn */
	if (P->down && P->press.act == A_AGENDA && P->press.b == wi && P->moved) {
		double y0 = minute_y(w, body, pitch, P->drag_start), y1 = minute_y(w, body, pitch, P->drag_end);
		ui_stroke_rrect(&G, body.x + 2, y0, body.w - 4, y1 - y0, 5, 2, INK, 1);
	}
	UiFont cf = ui_font_px(&v->numFont, 11);
	if (above) {
		char s[16];
		snprintf(s, sizeof s, "↑%d", above);
		ui_text(&G, &cf, s, body.x + body.w - 2, body.y + 2, 2, 0, INK, NULL);
	}
	if (below) {
		char s[16];
		snprintf(s, sizeof s, "↓%d", below);
		ui_text(&G, &cf, s, body.x + body.w - 2, body.y + body.h - 14, 2, 0, INK, NULL);
	}
	w->more = above + below;
}

/* ---- the to-do list ------------------------------------------------------------------- */

static void paint_todo(int wi, struct rect r)
{
	struct widget *w = &P->w[wi];
	const UiVoice *v = P->v;
	struct rect in = { r.x + IN, r.y + IN, r.w - 2 * IN, r.h - 2 * IN };
	struct entry *l[STORE_MAX];
	int n = entries_of(w, E_TODO, l, STORE_MAX), done = 0;
	for (int i = 0; i < n; i++)
		done += l[i]->done;
	char t[80];
	snprintf(t, sizeof t, "%s", w->title[0] ? w->title : "To-do");
	label(t, in.x, in.y, in.w - 40);
	if (n) {
		char c[16];
		snprintf(c, sizeof c, "%d/%d", done, n);
		UiFont nf = ui_font_px(&v->numFont, 12);
		ui_text(&G, &nf, c, in.x + in.w, in.y + 3, 2, 0, SUB, NULL);
	}
	double y = in.y + LBL_H, bottom = in.y + in.h;
	double fw = in.w - CHECK_W;
	/*
	 * Which rows fit. The empty row to write the next one in always keeps its place, and
	 * so does what was written last: when rows must give way, done ones go first, then the
	 * oldest open ones (the more mark counts them). Otherwise a to-do written into the
	 * last free row could vanish into "more" the moment the layout settles.
	 */
	double need = 0, rh_[STORE_MAX];
	bool vis[STORE_MAX];
	for (int i = 0; i < n; i++) {
		rh_[i] = todo_row_h(l[i], fw);
		need += rh_[i];
		vis[i] = true;
	}
	bool all = y + need + ROW <= bottom + 0.5;
	double room = (all ? bottom : bottom - ROW - 22) - y;
	for (int pass = 0; pass < 2 && need > room + 0.5; pass++)
		for (int i = 0; i < n && need > room + 0.5; i++)
			if (vis[i] && (pass == 1 || l[i]->done)) {
				vis[i] = false;
				need -= rh_[i];
			}
	int shown = 0;
	for (int i = 0; i < n; i++) {
		if (!vis[i])
			continue;
		double rh = rh_[i];
		struct entry *e = l[i];
		struct rect cb = { in.x + 1, y + 9, 18, 18 };
		ui_stroke_rrect(&G, cb.x, cb.y, cb.w, cb.h, 4, 1.5, INK, 0);
		if (e->done) {
			cairo_t *cr = G.cr;
			if (G.paint) {
				cairo_set_source_rgb(cr, ((INK >> 16) & 255) / 255.0, ((INK >> 8) & 255) / 255.0, (INK & 255) / 255.0);
				cairo_set_line_width(cr, 2);
				cairo_move_to(cr, cb.x + 4, cb.y + 9);
				cairo_line_to(cr, cb.x + 8, cb.y + 13);
				cairo_line_to(cr, cb.x + 15, cb.y + 4);
				cairo_stroke(cr);
			}
		}
		hit((struct rect){ in.x - 4, y, CHECK_W, rh }, A_CHECK, e->id, wi);
		struct rect fr = { in.x + CHECK_W, y, fw, rh };
		ui_hline(&G, fr.x, y + rh - 1, fr.w, 1, UI_RULE, 2);
		if (is_text(e))
			text_field(e, fr);
		else {
			ink(&e->f, fr.x, fr.y, (struct rect){ fr.x, fr.y, fr.w, rh });
			if (e->done && G.paint) {
				double ir, ib;
				field_extent(&e->f, &ir, &ib);
				ui_hline(&G, fr.x, y + fmin(ib, rh) * 0.6, fmin(ir + 4, fr.w), 1.5, SUB, 0);
			}
		}
		fbox(fr, fr.x, fr.y, e->id, E_TODO, widget_period(w), wi);
		y += rh;
		shown++;
	}
	w->more = n - shown;
	if (w->more > 0)
		more_mark(in.x, bottom - 20, in.w, w->more, wi);
	/* the empty row: writing here makes a new to-do */
	if (y + ROW <= bottom + 0.5) {
		struct rect fr = { in.x + CHECK_W, y, fw, ROW };
		ui_stroke_rrect(&G, in.x + 1, y + 9, 18, 18, 4, 1, UI_RULE, 1);
		ui_hline(&G, fr.x, y + ROW - 1, fr.w, 1, UI_RULE, 2);
		fbox(fr, fr.x, fr.y, 0, E_TODO, widget_period(w), wi);
	}
}

/* ---- free notes ------------------------------------------------------------------------- */

static void paint_notes(int wi, struct rect r)
{
	struct widget *w = &P->w[wi];
	struct rect in = { r.x + IN, r.y + IN, r.w - 2 * IN, r.h - 2 * IN };
	label(w->title[0] ? w->title : "Notes", in.x, in.y, in.w);
	struct entry *l[4];
	int n = entries_of(w, E_NOTE, l, 4);
	struct entry *e = n ? l[0] : NULL;
	struct rect body = { in.x, in.y + LBL_H, in.w, in.h - LBL_H };
	for (double y = body.y + LINE; y < body.y + body.h - 2; y += LINE)
		ui_hline(&G, body.x, y, body.w, 1, UI_RULE, 2);
	w->more = 0;
	if (e && is_text(e))
		text_field(e, (struct rect){ body.x, body.y, body.w, fmin(body.h, field_text_h(e, body.w - 12) + 16) });
	else if (e) /* whole written lines only: a line cut in half reads as damage */
		ink(&e->f, body.x, body.y, (struct rect){ body.x, body.y, body.w, floor(body.h / LINE) * LINE });
	if (e && !is_text(e)) {
		double ir, ib;
		field_extent(&e->f, &ir, &ib);
		if (ib > body.h) {
			w->more = 1;
			more_mark(in.x, in.y + in.h - 20, in.w, 0, wi);
		}
	}
	/* one field for the whole body: more writing goes into the same note */
	fbox(body, body.x, body.y, e ? e->id : 0, E_NOTE, widget_period(w), wi);
}

/* ---- the mini month --------------------------------------------------------------------- */

static void paint_minimonth(int wi, struct rect r)
{
	const UiVoice *v = P->v;
	struct rect in = { r.x + IN, r.y + IN, r.w - 2 * IN, r.h - 2 * IN };
	int y, m, d;
	day_to_ymd(P->per.start, &y, &m, &d);
	static const char *const mn[] = { "January", "February", "March", "April", "May", "June", "July", "August",
					  "September", "October", "November", "December" };
	char t[32];
	snprintf(t, sizeof t, "%s %d", mn[m - 1], y);
	label(t, in.x, in.y, in.w);
	int first = day_from_ymd(y, m, 1), lead = day_weekday(first), nd = days_in_month(y, m);
	double cw = in.w / 7, ch = fmin(22, (in.h - LBL_H) / 7);
	UiFont f = ui_font_px(&v->numFont, 11);
	static const char *const L[] = { "M", "T", "W", "T", "F", "S", "S" };
	double top = in.y + LBL_H;
	for (int c = 0; c < 7; c++)
		ui_text(&G, &f, L[c], in.x + c * cw + cw / 2, top + (ch - text_h(&f)) / 2, 1, 0, SUB, NULL);
	int pend = period_end(P->per);
	for (int i = 0; i < nd; i++) {
		int row = (lead + i) / 7 + 1, col = (lead + i) % 7, day = first + i;
		struct rect c = { in.x + col * cw, top + row * ch, cw, ch };
		if (c.y + c.h > in.y + in.h + 0.5)
			break;
		bool inview = day >= P->per.start && day <= pend;
		if (inview)
			ui_rrect(&G, c.x + 1, c.y + 1, c.w - 2, c.h - 2, 4, UI_HIGHLIGHT);
		char s[4];
		snprintf(s, sizeof s, "%d", i + 1);
		UiFont df = day == P->today ? ui_font_with(&f, 700) : f;
		ui_text(&G, &df, s, c.x + c.w / 2, c.y + (c.h - text_h(&f)) / 2 - 1, 1, 0, INK, NULL);
		bool any = false;
		for (int k = 0; k < P->st.n && !any; k++)
			any = !P->st.e[k].deleted && P->st.e[k].per.kind == P_DAY && P->st.e[k].per.start == day;
		if (any)
			ui_fill_rect(&G, c.x + c.w / 2 - 1.5, c.y + c.h - 3.5, 3, 2, INK);
		hit(c, A_DAY, day, 0);
	}
}

/* ---- the header and the notice ---------------------------------------------------------- */

static void paint_header(void)
{
	const UiVoice *v = P->v;
	struct rect hr = { 0, 0, P->W, HEAD_H };
	region(hr);
	button((struct rect){ 12, 8, 40, 40 }, "‹", false, A_PREV, 0);
	button((struct rect){ 58, 8, 40, 40 }, "›", false, A_NEXT, 0);
	double xr = P->W - 12;
	struct rect b = { xr - 44, 8, 44, 40 };
	button(b, "dbg", P->debug, A_DEBUG, 0);
	b.x -= 50;
	button(b, "Aa", P->text_view, A_TEXT_VIEW, 0);
	b.x -= 72;
	b.w = 66;
	button(b, "Layout", false, A_TMPL, 0);
	b.x -= 62;
	b.w = 56;
	button(b, "Week", P->per.kind == P_WEEK, A_VIEW_WEEK, 0);
	b.x -= 60;
	button(b, "Day", P->per.kind == P_DAY, A_VIEW_DAY, 0);
	double tx = 110, tw = b.x - 8 - tx;
	char t[80];
	int y, m, d, y2, m2, d2;
	static const char *const mn[] = { "January", "February", "March", "April", "May", "June", "July", "August",
					  "September", "October", "November", "December" };
	static const char *const wd[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
	day_to_ymd(P->per.start, &y, &m, &d);
	if (P->per.kind == P_DAY)
		snprintf(t, sizeof t, "%s %d %s", wd[day_weekday(P->per.start)], d, mn[m - 1]);
	else {
		day_to_ymd(period_end(P->per), &y2, &m2, &d2);
		if (m == m2)
			snprintf(t, sizeof t, "%d–%d %s", d, d2, mn[m - 1]);
		else
			snprintf(t, sizeof t, "%d %.3s – %d %.3s", d, mn[m - 1], d2, mn[m2 - 1]);
	}
	double h1 = text_h(&v->headFont);
	UiFont sf = ui_font_px(&v->dateFont, 12.5);
	double h2 = text_h(&sf), top = HEAD_H / 2.0 - (h1 + h2) / 2;
	ui_text(&G, &v->headFont, t, tx, top, 0, tw, INK, NULL);
	ui_text(&G, &sf, P->cur ? P->cur->name : "", tx, top + h1, 0, tw, SUB, NULL);
	hit((struct rect){ tx, 4, tw, HEAD_H - 8 }, A_TODAY, 0, 0);
	ui_hline(&G, 0, HEAD_H - v->headRuleW, P->W, v->headRuleW, v->headRuleC, 0);
}

static bool notice_needed(void)
{
	return P->hidden[E_EVENT] + P->hidden[E_TODO] + P->hidden[E_NOTE] > 0 || P->rep.infeasible_h || P->rep.infeasible_w;
}

static void paint_notice(void)
{
	struct rect r = { 0, HEAD_H, P->W, NOTICE_H };
	region(r);
	char s[160] = "";
	size_t len = 0;
	if (P->hidden[E_EVENT] + P->hidden[E_TODO] + P->hidden[E_NOTE] > 0) {
		len += snprintf(s, sizeof s, "Not shown here:");
		bool first = true;
		for (int k = 0; k < E_NKINDS; k++)
			if (P->hidden[k]) {
				len += snprintf(s + len, sizeof s - len, "%s %d %s", first ? "" : ",", P->hidden[k],
						ekind_name(k, P->hidden[k] != 1));
				first = false;
			}
		hit(r, A_HIDDEN, 0, 0);
	}
	if (P->rep.infeasible_w || P->rep.infeasible_h)
		snprintf(s + len, sizeof s - len, "%sLayout too small: %.0f px short", len ? "  ·  " : "",
			 fmax(P->rep.short_w, P->rep.short_h));
	UiFont f = ui_font_px(&P->v->dateFont, 13);
	ui_fill_rect(&G, 0, r.y, r.w, r.h, UI_HIGHLIGHT);
	ui_text(&G, &f, s, 16, r.y + (r.h - text_h(&f)) / 2, 0, r.w - 32, INK, NULL);
}

static void paint_debug(void)
{
	const UiVoice *v = P->v;
	double h = 196, y = P->H - h - 8;
	struct rect r = { 8, y, P->W - 16, h };
	region(r);
	UiBox b = { .bg = PAPER, .bw = 2, .bc = INK };
	for (int i = 0; i < 4; i++)
		b.r[i] = 8;
	ui_box(&G, r.x, r.y, r.w, r.h, &b);
	UiFont f = ui_font_px(&v->numFont, 11);
	double lh = 14, ty = y + 8;
	ui_text(&G, &v->lbl, "ink → reading (stub) → data", r.x + 10, ty, 0, r.w - 20, v->lblC, NULL);
	ty += 18;
	for (int i = 0; i < 4; i++, ty += lh)
		ui_text(&G, &f, P->flow[i], r.x + 10, ty, 0, r.w - 20, INK, NULL);
	ty += 4;
	ui_text(&G, &v->lbl, "e-ink cost per action (host count)", r.x + 10, ty, 0, r.w - 20, v->lblC, NULL);
	ty += 18;
	int from = P->nled > 5 ? P->nled - 5 : 0;
	for (int i = from; i < P->nled; i++, ty += lh) {
		char s[160];
		const struct ledger_row *l = &P->led[i];
		snprintf(s, sizeof s, "%-22s %3d commits  %5.1f%% panel  biggest %4.1f%%  relayouts %d", l->what, l->commits,
			 100.0 * l->area / ((double)P->W * P->S * P->H * P->S), 100.0 * l->biggest, l->layout);
		ui_text(&G, &f, s, r.x + 10, ty, 0, r.w - 20, INK, NULL);
	}
}

/* ---- the view --------------------------------------------------------------------------- */

struct rect field_rect_for(struct plan *p, int entry)
{
	for (int i = 0; i < p->nfb; i++)
		if (p->fb[i].entry == entry && entry)
			return p->fb[i].r;
	return (struct rect){ 0, 0, 0, 0 };
}

void paint_view(struct plan *p, cairo_t *cr, bool measure_only)
{
	P = p;
	G = (UiGc){ .cr = cr, .paint = !measure_only, .t = p->v };
	INK = p->v->ink;
	SUB = p->v->sub;
	PAPER = p->v->card.bg ? p->v->card.bg : UI_PAPER;
	if (measure_only)
		return;
	p->nhits = p->nfb = p->nregions = 0;
	ui_fill_rect(&G, 0, 0, p->W, p->H, p->v->screenBg);
	paint_header();
	if (notice_needed())
		paint_notice();
	for (int i = 0; i < p->nw; i++) {
		struct widget *w = &p->w[i];
		if (w->type == W_ROW || w->type == W_COL || w->type == W_UNKNOWN)
			continue;
		struct rect r = { w->ln.x + PAD, w->ln.y + PAD, w->ln.w - 2 * PAD, w->ln.h - 2 * PAD };
		region((struct rect){ w->ln.x, w->ln.y, w->ln.w, w->ln.h });
		card(r);
		cairo_save(cr);
		cairo_rectangle(cr, r.x, r.y, r.w, r.h);
		cairo_clip(cr);
		switch (w->type) {
		case W_AGENDA: paint_agenda(i, r); break;
		case W_TODO: paint_todo(i, r); break;
		case W_NOTES: paint_notes(i, r); break;
		case W_MINIMONTH: paint_minimonth(i, r); break;
		default: break;
		}
		cairo_restore(cr);
	}
	if (p->debug)
		paint_debug();
}

/* the top of the widgets' area (below the header and the notice) */
double paint_top(struct plan *p)
{
	P = p;
	return HEAD_H + (notice_needed() ? NOTICE_H : 0);
}
