// SPDX-License-Identifier: GPL-2.0-or-later
/* The day view: its fields (one writing region each), its controls and the hidden count. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/hold.h"
#include "../src/view.h"
#include "sample.h"
#include "t.h"

static const struct vhit *find(const struct view *v, enum act a, int arg)
{
	for (int i = 0; i < v->nh; i++)
		if (v->hit[i].a == a && (arg < 0 || v->hit[i].arg == arg))
			return &v->hit[i];
	return NULL;
}

static uint32_t px(cairo_surface_t *sf, double x, double y)
{
	const unsigned char *d = cairo_image_surface_get_data(sf);
	return ((const uint32_t *)(d + (int)y * cairo_image_surface_get_stride(sf)))[(int)x] & 0xffffff;
}

/* the box (x0, y0, x1, y1) where two paints differ; false when they don't */
static bool diff_box(cairo_surface_t *a, cairo_surface_t *b, double r[4])
{
	int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
	r[0] = r[1] = 1e9, r[2] = r[3] = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (px(a, x, y) != px(b, x, y))
				r[0] = fmin(r[0], x), r[1] = fmin(r[1], y), r[2] = fmax(r[2], x + 1), r[3] = fmax(r[3], y + 1);
	return r[2] >= 0;
}

/* a stroke for the free time's tests: n points from (x0, y0) to (x1, y1), a zig-zag of
 * width wob across the line when wob > 0 (writing) */
static int line_pts(struct pl_point *q, int n, double x0, double y0, double x1, double y1, double wob)
{
	for (int i = 0; i < n; i++) {
		double f = (double)i / (n - 1);
		q[i] = (struct pl_point){ (float)(x0 + (x1 - x0) * f + (i % 2 ? wob : 0)), (float)(y0 + (y1 - y0) * f), 0.5f,
					  (uint32_t)(1000 + 7 * i) };
	}
	return n;
}

struct relay {
	struct view *v;
	struct store *s;
	const struct look *L;
	cairo_t *cr;
	int n;
};
static void relay_out(void *ud)
{
	struct relay *r = ud;
	view_layout(r->v, r->s, r->L, r->cr);
	r->n++;
}

int main(void)
{
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	struct store s;
	store_init(&s, NULL);
	sample_fill(&s);
	struct look L;
	look_init(&L, "white");
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 702, 884);
	cairo_t *cr = cairo_create(sf);
	int today = day_today();
	struct view v = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
	view_layout(&v, &s, &L, cr);
	CHECK_STR(v.t->id, "day-lists");
	CHECK_INT(v.how, 3);
	int free_ = 0, entries = 0, news = 0;
	for (int i = 0; i < v.nf; i++) {
		free_ += v.f[i].k == F_FREE;
		entries += v.f[i].k == F_ENTRY;
		news += v.f[i].k == F_NEW;
		CHECK(v.f[i].x >= 0 && v.f[i].y >= 60 && v.f[i].x + v.f[i].w <= 702 && v.f[i].y + v.f[i].h <= 884);
		for (int j = 0; j < i; j++)
			CHECK(strcmp(v.f[i].name, v.f[j].name)); /* region names are unique */
	}
	CHECK_INT(free_, 1);
	CHECK(entries >= 8);
	CHECK(news >= 2);
	/* the free time's minutes per px map the agenda's hours */
	for (int i = 0; i < v.nf; i++)
		if (v.f[i].k == F_FREE)
			CHECK(v.f[i].min0 == 7 * 60 && v.f[i].mpp > 0 && v.f[i].h / (60 / v.f[i].mpp) > 13.9);
	/* the bar's controls */
	CHECK(find(&v, A_VIEW, P_WEEK) && find(&v, A_PREV, -1) && find(&v, A_NEXT, -1) && find(&v, A_TODAY, -1));
	CHECK(find(&v, A_INFO, -1));
	/* day-lists shows no priorities, no habits and only the "notes" slot */
	CHECK(v.hidden_count > 0);
	struct hidden_group g[16];
	int n = view_hidden(&s, v.t, v.per, g, 16);
	int prio = 0, notes = 0;
	for (int i = 0; i < n; i++)
		prio += g[i].kind == E_PRIO ? g[i].n : 0, notes += g[i].kind == E_NOTE ? g[i].n : 0;
	CHECK_INT(prio, 2);
	CHECK_INT(notes, 2);
	/* the agenda shows every kind of event: none hidden */
	for (int i = 0; i < n; i++)
		CHECK(g[i].kind != E_EVENT);
	/* the cog's menu: its targets lie on top, inside the popover */
	v.ov = OV_MENU;
	view_layout(&v, &s, &L, cr);
	const struct vhit *h = find(&v, A_TEXTMODE, -1);
	CHECK(h && h->x >= v.pop_x && h->y >= v.pop_y);
	CHECK(find(&v, A_PICKER, -1) != NULL);
	CHECK(view_hit(&v, h->x + 5, h->y + 5) == h);
	/* the picker's and the sheet's own targets win over their catch-all */
	v.ov = OV_PICKER;
	view_layout(&v, &s, &L, cr);
	h = find(&v, A_CLOSE, -1);
	CHECK(h && view_hit(&v, h->x + h->w / 2, h->y + h->h / 2) == h);
	h = find(&v, A_PICK, -1);
	CHECK(h && view_hit(&v, h->x + h->w / 2, h->y + h->h / 2)->a == A_PICK);
	h = find(&v, A_SCOPE, 3);
	CHECK(h && view_hit(&v, h->x + 4, h->y + 4)->a == A_SCOPE);
	v.ov = OV_HIDDEN;
	v.hidden_kind = 0;
	view_layout(&v, &s, &L, cr);
	h = find(&v, A_CLOSE, 0);
	CHECK(h && view_hit(&v, h->x + h->w / 2, h->y + h->h / 2) == h);
	h = find(&v, A_CLOSE, 1);
	CHECK(h && h->id && view_hit(&v, h->x + h->w / 2, h->y + h->h / 2) == h);
	v.ov = OV_NONE;
	v.hidden_kind = -1;
	/* every empty row has a name of its own (one region each) */
	view_layout(&v, &s, &L, cr);
	for (int i = 0; i < v.nf; i++)
		for (int j = 0; j < i; j++)
			CHECK(strcmp(v.f[i].name, v.f[j].name));
	/* every template lays out with fields in each orientation */
	for (int i = 0; i < pl_ntemplates; i++) {
		struct period p = period_of(pl_templates[i]->kind, today);
		store_choose(&s, p, p.start, pl_templates[i]->id);
		for (int land = 0; land < 2; land++) {
			struct view w = { .per = p, .today = today, .W = land ? 936 : 702, .H = land ? 650 : 884, .hidden_kind = -1 };
			view_layout(&w, &s, &L, cr);
			CHECK(w.t == pl_templates[i]);
			CHECK(w.nf > 0);
		}
	}
	/* ---- text mode (FR-012 to FR-017) ---- */
	{
		struct view w = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		store_choose(&s, w.per, w.per.start, "day-lists");
		view_layout(&w, &s, &L, cr);
		/* the lowest entry field: under the keyboard once it is up */
		const struct vfield *lo = NULL;
		for (int i = 0; i < w.nf; i++)
			if (w.f[i].k == F_ENTRY && w.f[i].h <= 48 && (!lo || w.f[i].y > lo->y))
				lo = &w.f[i];
		CHECK(lo != NULL);
		struct vfield before = *lo;
		int nf = w.nf;
		w.tm = 1;
		snprintf(w.tm_name, sizeof w.tm_name, "%s", lo->name);
		w.tm_text = "typed words";
		w.tm_cur = 5;
		w.vis_h = 884 - 236;
		w.scroll = view_scroll_to(&w, lo);
		CHECK(w.scroll > 0 && (int)w.scroll % PL_ROW == 0);
		view_layout(&w, &s, &L, cr);
		/* the keyboard doesn't lay the page out again: every field where it was */
		CHECK_INT(w.nf, nf);
		lo = NULL;
		for (int i = 0; i < w.nf; i++)
			if (!strcmp(w.f[i].name, before.name))
				lo = &w.f[i];
		CHECK(lo && lo->y == before.y && lo->h == before.h && lo->x == before.x);
		/* the field lies above the bar over the keyboard, below the view's bar */
		CHECK(lo->y + 40 - w.scroll <= w.vis_h - 46 && lo->y - w.scroll >= 60);
		CHECK(view_field_at(&w, lo->x + 5, lo->y - w.scroll + 5) == lo);
		/* the bar's switches and its way out, on the screen (never scrolled) */
		const struct vhit *d = find(&w, A_TM_DONE, -1), *ne = find(&w, A_TM_NEATEN, -1), *au = find(&w, A_TM_AUTOGRAPH, -1);
		CHECK(d && ne && au && d->fixed && d->y >= w.vis_h - 46 && d->y + d->h <= w.vis_h);
		CHECK(view_hit(&w, d->x + 2, d->y + 2) == d && view_hit(&w, ne->x + 2, ne->y + 2) == ne);
		CHECK(ne->x + ne->w <= au->x + 0.5 && au->x + au->w <= d->x + 0.5); /* Neaten, Set in Autograph, Done */
		/* nothing under the bar writes */
		CHECK(view_field_at(&w, d->x + 2, d->y + 2) == NULL);
		/* the whole view: Back to ink, no switches */
		w.tm = 2;
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_TM_DONE, -1) && !find(&w, A_TM_NEATEN, -1));
		cairo_save(cr);
		view_paint(&w, &s, &L, cr);
		cairo_restore(cr);
	}
	/* a value with one edited word: that word set in Autograph (its outline filled), the
	 * others' ink as written (FR-014) */
	{
		struct store t;
		store_init(&t, NULL);
		int d0 = today;
		struct entry *e = store_add(&t, E_TODO, period_of(P_DAY, d0));
		e->rank = 1;
		sample_ink(&e->f, "call the", 2, 24, 3);
		uint32_t k0 = e->f.s[0].k, k1 = e->f.s[1].k;
		float path[] = { 0, 120, 8, 1, 150, 8, 1, 150, 26, 1, 120, 26, 4 };
		struct pl_word wd[3] = {
			{ 0, 4, false, &k0, 1, 0, 0, { 0 }, NULL, 0 },
			{ 5, 3, false, &k1, 1, 0, 0, { 0 }, NULL, 0 },
			{ 9, 4, true, NULL, 0, 0, 0, { 120, 8, 30, 18 }, path, 13 },
		};
		uint32_t ent = e->id;
		field_value(&t, &ent, "call the bank", wd, 3, NULL, 0, NULL, 0);
		struct view w = { .per = period_of(P_DAY, d0), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		view_layout(&w, &t, &L, cr);
		const struct vfield *f = view_field_of(&w, ent);
		CHECK(f != NULL);
		cairo_save(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_paint(cr);
		view_paint(&w, &t, &L, cr);
		cairo_restore(cr);
		cairo_surface_flush(sf);
		uint32_t *px = (uint32_t *)cairo_image_surface_get_data(sf);
		int stride = cairo_image_surface_get_stride(sf) / 4;
#define DARK(X, Y) ((px[(int)(Y) * stride + (int)(X)] & 0xff) < 0x60)
		if (f) {
			CHECK(DARK(f->x + 135, f->y + 17)); /* the set word, filled */
			int ink = 0;
			for (int yy = 0; yy < 30; yy++)
				for (int xx = 0; xx < 60; xx++)
					ink += DARK(f->x + xx, f->y + yy);
			CHECK(ink > 10); /* "call" as written */
		}
#undef DARK
		store_free(&t);
	}
	/* Set in Autograph (FR-011): a word that was read, shown set (its outline, with its strokes
	 * kept), is filled from the outline while the switch is on and drawn as its ink while off */
	{
		struct store t;
		store_init(&t, NULL);
		int d0 = today;
		struct entry *e = store_add(&t, E_TODO, period_of(P_DAY, d0));
		e->rank = 1;
		sample_ink(&e->f, "call", 2, 24, 3);
		uint32_t k0 = e->f.s[0].k;
		float path[] = { 0, 120, 8, 1, 150, 8, 1, 150, 26, 1, 120, 26, 4 };
		struct pl_word wd[1] = { { 0, 4, true, &k0, 1, 0, 0, { 120, 8, 30, 18 }, path, 13 } };
		uint32_t ent = e->id;
		field_value(&t, &ent, "call", wd, 1, NULL, 0, NULL, 0);
		CHECK(e->f.ns == 1 && e->f.nw == 1 && e->f.w[0].nks == 1); /* the strokes stay */
		for (int on = 0; on < 2; on++) {
			t.autograph = on;
			struct view w = { .per = period_of(P_DAY, d0), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
			view_layout(&w, &t, &L, cr);
			const struct vfield *f = view_field_of(&w, ent);
			CHECK(f != NULL);
			cairo_save(cr);
			cairo_set_source_rgb(cr, 1, 1, 1);
			cairo_paint(cr);
			view_paint(&w, &t, &L, cr);
			cairo_restore(cr);
			cairo_surface_flush(sf);
			uint32_t *px = (uint32_t *)cairo_image_surface_get_data(sf);
			int stride = cairo_image_surface_get_stride(sf) / 4;
#define DARK(X, Y) ((px[(int)(Y) * stride + (int)(X)] & 0xff) < 0x60)
			if (f) {
				int ink = 0;
				for (int yy = 0; yy < 30; yy++)
					for (int xx = 0; xx < 60; xx++)
						ink += DARK(f->x + xx, f->y + yy);
				CHECK(on ? DARK(f->x + 135, f->y + 17) : !DARK(f->x + 135, f->y + 17));
				CHECK(on ? ink == 0 : ink > 10); /* the strokes: not drawn while shown set */
			}
#undef DARK
		}
		store_free(&t);
	}
	/* titles (FR-018): one that fits is whole (a tap opens text mode), a long one in a short
	 * block shrinks to the smallest readable size, then is cut (a tap shows it whole) */
	{
		struct period d = period_of(P_DAY, today);
		store_choose(&s, d, d.start, "day-agenda");
		struct entry *e = store_add(&s, E_EVENT, d);
		e->start = 12 * 60, e->end = 12 * 60 + 30;
		/* written on and on past the block's edge: wider than its block even at 11 px */
		sample_ink(&e->f, "pick up the parcel and the new keys from the neighbour across the road before six, then "
				  "bring the blue folder to the office and ask about the shelf, the bike light and the plants", 2, 19, 5);
		struct view w = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		view_layout(&w, &s, &L, cr);
		int cut = -1, whole = -1;
		for (int i = 0; i < w.nh; i++)
			if (w.hit[i].a == A_EVENT && w.hit[i].id == e->id)
				cut = w.hit[i].arg;
			else if (w.hit[i].a == A_EVENT)
				whole = whole < 0 ? w.hit[i].arg : whole;
		CHECK_INT(cut, 1);
		CHECK_INT(whole, 0);
		/* its card shows it whole */
		w.ov = OV_TITLE;
		w.title = e->id;
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_CLOSE, -1) != NULL);
		store_delete(&s, e->id);
	}
	/* search in the (i) popover (FR-050) */
	{
		struct period d = period_of(P_DAY, today);
		struct view w = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_INFO };
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_SEARCH, -1) != NULL);
		w.search = true;
		w.q = "PASSPORT";
		view_layout(&w, &s, &L, cr);
		const struct vhit *r = find(&w, A_RESULT, -1);
		struct entry *re = r ? store_get(&s, r->id) : NULL;
		CHECK(re && strstr(re->f.value, "passport") && re->per.kind == P_MONTH);
		CHECK(view_hit(&w, r->x + 4, r->y + 4) == r);
		w.q = "zebra";
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_RESULT, -1) == NULL);
	}
	/* the picker: four scopes, Plan ahead; the year: a cell per week, the choices listed */
	{
		struct period wk = period_of(P_WEEK, today);
		int nc0 = s.nc;
		store_choose(&s, period_of(P_WEEK, today - 70), period_end(period_of(P_WEEK, today - 14)), "week-columns");
		store_choose(&s, wk, wk.start, "week-lists");
		struct view w = { .per = wk, .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_PICKER };
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_SCOPE, 0) && find(&w, A_SCOPE, 1) && find(&w, A_SCOPE, 2) && find(&w, A_SCOPE, 3));
		const struct vhit *ya = find(&w, A_YEAR, -1);
		CHECK(ya && view_hit(&w, ya->x + 4, ya->y + 4) == ya);
		w.year = true;
		view_layout(&w, &s, &L, cr);
		int cells = 0, now = 0;
		for (int i = 0; i < w.nh; i++)
			if (w.hit[i].a == A_CELL) {
				cells++;
				now += w.hit[i].arg == wk.start;
			}
		CHECK(cells >= 52 && cells <= 53);
		CHECK_INT(now, 1);
		/* the range: Change; the single week: Remove */
		const struct vhit *ch = find(&w, A_RANGE_CHANGE, -1), *rm = find(&w, A_RANGE_REMOVE, -1);
		CHECK(ch && rm && s.c[ch->arg].to != s.c[ch->arg].from.start && s.c[rm->arg].to == s.c[rm->arg].from.start);
		/* a range being chosen: a template picked first, then two weeks */
		w.rg_n = 1, w.rg0 = wk.start, snprintf(w.rg_tmpl, sizeof w.rg_tmpl, "week-spread");
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_CELL, -1) != NULL);
		while (s.nc > nc0)
			store_unchoose(&s, s.nc - 1);
		CHECK_INT(s.nc, nc0);
	}
	/* the names of choices (the picker's rows, the year's list): week ranges by number and month */
	{
		char b[96];
		int y0 = 2027;
		view_choice_name(P_WEEK, day_from_ymd(y0, 1, 4), day_from_ymd(y0, 4, 4), y0, b, sizeof b);
		CHECK_STR(b, "weeks 1–13 · Jan–Mar");
		view_choice_name(P_WEEK, day_from_ymd(y0, 3, 8), day_from_ymd(y0, 3, 21), y0, b, sizeof b);
		CHECK_STR(b, "weeks 10–11 · Mar");
		view_choice_name(P_WEEK, day_from_ymd(y0, 4, 5), -1, y0, b, sizeof b);
		CHECK_STR(b, "from week 14 on · from Apr");
		view_choice_name(P_WEEK, day_from_ymd(y0, 4, 5), -1, y0 + 1, b, sizeof b);
		CHECK_STR(b, "from week 14 on · from Apr 2027");
		view_choice_name(P_WEEK, day_from_ymd(y0, 5, 17), day_from_ymd(y0, 5, 17), y0, b, sizeof b);
		CHECK_STR(b, "week 20");
		view_choice_name(P_DAY, day_from_ymd(y0, 4, 12), day_from_ymd(y0, 4, 23), y0, b, sizeof b);
		CHECK_STR(b, "12 Apr to 23 Apr");
		view_choice_name(P_MONTH, day_from_ymd(y0, 4, 1), -1, y0 + 1, b, sizeof b);
		CHECK_STR(b, "from April 2027 on");
	}
	/* the year's arrows; the next year lists only the choices that reach into it */
	{
		struct period wk = period_of(P_WEEK, today);
		int nc0 = s.nc, y0 = 2027;
		store_choose(&s, period_of(P_WEEK, day_from_ymd(y0, 1, 4)), day_from_ymd(y0, 4, 4), "week-columns");
		store_choose(&s, period_of(P_WEEK, day_from_ymd(y0, 4, 5)), -1, "week-spread");
		struct view w = { .per = wk, .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_PICKER, .year = true };
		view_layout(&w, &s, &L, cr);
		const struct vhit *pv = find(&w, A_YEAR_NAV, -1), *nx = find(&w, A_YEAR_NAV, 1);
		CHECK(pv && nx && pv->x < nx->x && view_hit(&w, nx->x + 4, nx->y + 4) == nx);
		int changes = 0;
		for (int i = 0; i < w.nh; i++)
			changes += w.hit[i].a == A_RANGE_CHANGE;
		CHECK_INT(changes, 2);
		w.year_off = 1;
		view_layout(&w, &s, &L, cr);
		int cells = 0, ch = -1;
		changes = 0;
		for (int i = 0; i < w.nh; i++) {
			cells += w.hit[i].a == A_CELL;
			if (w.hit[i].a == A_RANGE_CHANGE)
				changes++, ch = w.hit[i].arg;
		}
		CHECK(cells >= 52 && cells <= 53);
		CHECK_INT(changes, 1);
		CHECK(ch >= 0 && s.c[ch].to < 0);
		CHECK(find(&w, A_CELL, wk.start) == NULL);
		while (s.nc > nc0)
			store_unchoose(&s, s.nc - 1);
	}
	/* the days of a year: a wall calendar, every day its own target */
	{
		struct period d = period_of(P_DAY, today);
		struct view w = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_PICKER, .year = true };
		view_layout(&w, &s, &L, cr);
		int cells = 0, own = 0;
		for (int i = 0; i < w.nh; i++)
			if (w.hit[i].a == A_CELL) {
				cells++;
				const struct vhit *h = &w.hit[i];
				own += view_hit(&w, h->x + h->w / 2, h->y + h->h / 2) == h;
			}
		CHECK_INT(cells, 365);
		CHECK_INT(own, 365);
		CHECK(find(&w, A_CELL, day_from_ymd(2027, 12, 31)) != NULL);
		CHECK(find(&w, A_NONE, -1) != NULL); /* the catch-all still fits */
	}
	/* turned (board templates.year.days.landscape): four months to a row, every day its own
	 * target inside the window, in every voice and in years whose rows each hold a six-week
	 * month; the arrows on the scopes' row, and a range and a single day's choice listed */
	{
		static const char *VO[] = { "white", "editorial", "stationery" };
		struct period d = period_of(P_DAY, today);
		int nc0 = s.nc;
		store_choose(&s, period_of(P_DAY, day_from_ymd(2027, 4, 12)), day_from_ymd(2027, 4, 23), "day-diary");
		struct period d26 = period_of(P_DAY, day_from_ymd(2027, 3, 26));
		store_choose(&s, d26, d26.start, "day-agenda");
		for (int vi = 0; vi < 3; vi++) {
			struct look LT;
			look_init(&LT, VO[vi]);
			for (int off = 0; off < 12; off++) {
				struct view w = { .per = d, .today = today, .W = 936, .H = 662, .hidden_kind = -1, .ov = OV_PICKER,
						  .year = true, .scope = 1, .year_off = off };
				view_layout(&w, &s, &LT, cr);
				int yr = 2027 + off, nd = day_from_ymd(yr, 12, 31) - day_from_ymd(yr, 1, 1) + 1;
				int cells = 0, own = 0, inside = 0;
				for (int i = 0; i < w.nh; i++)
					if (w.hit[i].a == A_CELL) {
						const struct vhit *h = &w.hit[i];
						cells++;
						own += view_hit(&w, h->x + h->w / 2, h->y + h->h / 2) == h;
						inside += h->x >= 0 && h->x + h->w <= w.W && h->y + h->h <= w.H;
						CHECK(h->w >= 28 && h->h >= 18); /* the upright cell's size, gaps included */
					}
				CHECK_INT(cells, nd);
				CHECK_INT(own, nd);
				CHECK_INT(inside, nd);
				/* four to a row: the 1st of January, April, May and December */
				const struct vhit *jan = find(&w, A_CELL, day_from_ymd(yr, 1, 1)), *apr = find(&w, A_CELL, day_from_ymd(yr, 4, 1)),
						  *may = find(&w, A_CELL, day_from_ymd(yr, 5, 1)), *dec = find(&w, A_CELL, day_from_ymd(yr, 12, 31));
				CHECK(jan && apr && may && dec);
				if (jan && apr && may && dec) {
					CHECK(apr->x > w.W * 3 / 4 - 20 && apr->y < jan->y + 6 * 18); /* April in January's row */
					CHECK(may->x < jan->x + 7 * 32 && may->y > jan->y + 3 * 18); /* May starts the second row */
					CHECK(dec->x > w.W * 3 / 4 - 20);
				}
				/* the arrows sit on the scopes' row, right of the last scope */
				const struct vhit *pv = find(&w, A_YEAR_NAV, -1), *nx = find(&w, A_YEAR_NAV, 1), *sc = find(&w, A_SCOPE, 3);
				CHECK(pv && nx && sc);
				if (pv && nx && sc)
					CHECK(pv->x > sc->x + sc->w && pv->y < sc->y + sc->h && pv->y + pv->h > sc->y);
				/* the range and the single day show in their year, each with its button */
				int ch = 0, rm = 0;
				for (int i = 0; i < w.nh; i++) {
					ch += w.hit[i].a == A_RANGE_CHANGE;
					rm += w.hit[i].a == A_RANGE_REMOVE;
					if (w.hit[i].a == A_RANGE_CHANGE || w.hit[i].a == A_RANGE_REMOVE)
						CHECK(w.hit[i].y + w.hit[i].h <= w.H);
				}
				if (off == 0) {
					CHECK_INT(ch, 1);
					CHECK_INT(rm, 1);
				}
				if (apr) /* the gap around a cell answers for it too */
					CHECK(view_hit(&w, apr->x + 1, apr->y + 1) == apr);
			}
		}
		/* weeks and months turned (board templates.year.landscape): two columns of six
		 * months, January to June and July to December, every period its own target inside
		 * the window, the arrows on the scopes' row, the choices listed inside the window */
		for (int kk = 0; kk < 2; kk++) {
			enum pkind pk = kk ? P_MONTH : P_WEEK;
			struct period pp = period_of(pk, today);
			int nc1 = s.nc;
			if (pk == P_WEEK) {
				store_choose(&s, period_of(P_WEEK, day_from_ymd(2027, 1, 4)), period_end(period_of(P_WEEK, day_from_ymd(2027, 3, 29))),
					     "week-columns");
				store_choose(&s, period_of(P_WEEK, day_from_ymd(2027, 4, 5)), -1, "week-spread");
				struct period w20 = period_of(P_WEEK, day_from_ymd(2027, 5, 17));
				store_choose(&s, w20, w20.start, "week-lists");
			} else
				store_choose(&s, pp, pp.start, "month-goals");
			for (int vi = 0; vi < 3; vi++) {
				struct look LT;
				look_init(&LT, VO[vi]);
				for (int off = 0; off < 12; off++) {
					struct view w = { .per = pp, .today = today, .W = 936, .H = 662, .hidden_kind = -1, .ov = OV_PICKER,
							  .year = true, .scope = 1, .year_off = off };
					view_layout(&w, &s, &LT, cr);
					int yr = 2027 + off, want = 0, cells = 0, own = 0, inside = 0;
					for (int d0 = day_from_ymd(yr, 1, 1); d0 <= day_from_ymd(yr, 12, 31); d0++)
						want += pk == P_MONTH ? d0 == period_of(P_MONTH, d0).start : period_of(P_WEEK, d0).start == d0;
					for (int i = 0; i < w.nh; i++)
						if (w.hit[i].a == A_CELL) {
							const struct vhit *h = &w.hit[i];
							cells++;
							own += view_hit(&w, h->x + h->w / 2, h->y + h->h / 2) == h;
							inside += h->x >= 0 && h->x + h->w <= w.W && h->y + h->h <= w.H;
							CHECK(h->w >= 40 && h->h >= 26);
						}
					CHECK_INT(cells, want);
					CHECK_INT(own, want);
					CHECK_INT(inside, want);
					/* January and July share the first row; June sits above the choices */
					int jl = day_from_ymd(yr, 7, 1), ja = day_from_ymd(yr, 1, 1);
					const struct vhit *jan = NULL, *jul = NULL;
					for (int i = 0; i < w.nh; i++)
						if (w.hit[i].a == A_CELL) {
							int st = w.hit[i].arg;
							if (!jan || (st >= ja && st < ja + 7 && w.hit[i].x < jan->x))
								jan = st >= ja && st < ja + 31 ? &w.hit[i] : jan;
							if (st >= jl && st < jl + 31 && (!jul || w.hit[i].x < jul->x))
								jul = &w.hit[i];
						}
					CHECK(jan && jul);
					if (jan && jul)
						CHECK(jul->x > w.W / 2 && jan->x < w.W / 2 && jul->y < jan->y + jan->h && jul->y + jul->h > jan->y);
					const struct vhit *pv = find(&w, A_YEAR_NAV, -1), *sc = find(&w, A_SCOPE, 3);
					CHECK(pv && sc);
					if (pv && sc)
						CHECK(pv->x > sc->x + sc->w && pv->y < sc->y + sc->h && pv->y + pv->h > sc->y);
					int rows = 0;
					for (int i = 0; i < w.nh; i++)
						if (w.hit[i].a == A_RANGE_CHANGE || w.hit[i].a == A_RANGE_REMOVE) {
							rows++;
							CHECK(w.hit[i].y + w.hit[i].h <= w.H);
						}
					if (off == 0) { /* every choice of the year shows (the ones set here and any before) */
						int kinds = 0;
						for (int i = 0; i < s.nc; i++)
							kinds += s.c[i].from.kind == pk;
						CHECK(rows >= (pk == P_WEEK ? 3 : 1));
						CHECK_INT(rows, kinds);
					}
				}
			}
			while (s.nc > nc1)
				store_unchoose(&s, s.nc - 1);
		}
		/* upright stays three to a row */
		struct view u = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_PICKER, .year = true };
		view_layout(&u, &s, &L, cr);
		const struct vhit *ua = find(&u, A_CELL, day_from_ymd(2027, 4, 1)), *uj = find(&u, A_CELL, day_from_ymd(2027, 1, 1));
		CHECK(ua && uj && ua->x < 702 / 3 && ua->y > uj->y + 3 * 18);
		while (s.nc > nc0)
			store_unchoose(&s, s.nc - 1);
	}
	/* the hidden list: Open, Move and Delete per entry; Move shows the month's days */
	{
		struct period d = period_of(P_DAY, today);
		store_choose(&s, d, d.start, "day-agenda");
		struct view w = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = 0, .ov = OV_HIDDEN };
		view_layout(&w, &s, &L, cr);
		const struct vhit *op = find(&w, A_OPEN, -1), *mv = find(&w, A_MOVE, -1), *dl = find(&w, A_CLOSE, 1);
		CHECK(op && mv && dl && op->id == mv->id && op->x < mv->x && mv->x < dl->x);
		CHECK(view_hit(&w, mv->x + 4, mv->y + 4) == mv);
		struct entry *me = mv ? store_get(&s, mv->id) : NULL;
		CHECK(me != NULL);
		if (me) {
			w.ov = OV_MOVE, w.back = OV_HIDDEN, w.move = me->id, w.move_at = me->per.start;
			view_layout(&w, &s, &L, cr);
			int days = 0, y, m, dd;
			for (int i = 0; i < w.nh; i++)
				days += w.hit[i].a == A_MOVE_TO;
			day_to_ymd(me->per.start, &y, &m, &dd);
			CHECK_INT(days, days_in_month(y, m));
			CHECK(find(&w, A_MOVE_NAV, -1) && find(&w, A_CLOSE, -1));
			w.ov = OV_TITLE, w.title = me->id;
			view_layout(&w, &s, &L, cr);
			CHECK(find(&w, A_CLOSE, -1) != NULL);
		}
		store_choose(&s, d, d.start, "day-lists");
	}
	/* the mini month on a day template shades the day on screen, also after a tap moves to
	 * another day (today keeps its ring); a week view shades its week */
	{
		int y, m, dd;
		day_to_ymd(today, &y, &m, &dd);
		int other = dd < days_in_month(y, m) ? today + 1 : today - 1;
		struct view w = { .per = period_of(P_DAY, other), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		view_layout(&w, &s, &L, cr);
		CHECK_STR(w.t->id, "day-lists");
		view_paint(&w, &s, &L, cr);
		cairo_surface_flush(sf);
		const struct vhit *sel = NULL, *now = NULL;
		for (int i = 0; i < w.nh; i++)
			if (w.hit[i].a == A_DAY && w.hit[i].arg == other)
				sel = &w.hit[i];
			else if (w.hit[i].a == A_DAY && w.hit[i].arg == today)
				now = &w.hit[i];
		CHECK(sel && now);
		if (sel && now) {
			uint32_t hl = L.highlight & 0xffffff;
			CHECK_INT(px(sf, sel->x + 2, sel->y + 2), hl);
			CHECK(px(sf, now->x + 2, now->y + 2) != hl);
		}
		/* back on today: today is shaded, the other day is not */
		w.per = period_of(P_DAY, today);
		view_layout(&w, &s, &L, cr);
		view_paint(&w, &s, &L, cr);
		cairo_surface_flush(sf);
		sel = now = NULL;
		for (int i = 0; i < w.nh; i++)
			if (w.hit[i].a == A_DAY && w.hit[i].arg == other)
				sel = &w.hit[i];
			else if (w.hit[i].a == A_DAY && w.hit[i].arg == today)
				now = &w.hit[i];
		CHECK(sel && now);
		if (sel && now) {
			CHECK_INT(px(sf, now->x + 2, now->y + 2), L.highlight & 0xffffff);
			CHECK(px(sf, sel->x + 2, sel->y + 2) != (L.highlight & 0xffffff));
		}
	}
	/* the settings */
	{
		struct view w = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 884, .hidden_kind = -1, .ov = OV_SETTINGS };
		view_layout(&w, &s, &L, cr);
		const struct vhit *n = find(&w, A_SET_NEATEN, -1), *a = find(&w, A_SET_AUTOGRAPH, -1), *k = find(&w, A_SET_STRENGTH, -1);
		CHECK(n && a && k && find(&w, A_CLOSE, -1));
		CHECK(view_hit(&w, n->x + 4, n->y + 4) == n && view_hit(&w, k->x + 4, k->y + 4) == k);
		CHECK(view_field_at(&w, 300, 500) == NULL); /* nothing writes under the sheet */
		w.ov = OV_MENU;
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_SETTINGS, -1) != NULL);
	}
	/* a wrapped text's lines lh apart at every scale, as at scale 1 (pango alone spaced
	 * them lh times the scale apart: an empty line in a dialog's paragraph at scale 2): glassine-ui 1.5.1 spaces them so */
	{
		static const char *const voices[] = { "white", "editorial", "stationery" };
		static const double scales[] = { 1, 1.25, 1.6, 2, 3 };
		static const double lhs[] = { 1.2, 1.35, 1.45 };
		for (int vi = 0; vi < 3; vi++)
			for (int si = 0; si < 5; si++)
				for (int li = 0; li < 3; li++) {
					struct look K;
					look_init(&K, voices[vi]);
					cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
					cairo_t *mc = cairo_create(ms);
					cairo_scale(mc, scales[si], scales[si]);
					UiGc g = { mc, 0, NULL };
					const UiFont *fs[] = { &K.body, &K.sub, &K.h1 };
					for (int fi = 0; fi < 3; fi++) {
						double one = ui_text_wrap(&g, fs[fi], "Ag", 0, 0, 1e4, 1, 0, 0);
						double two = ui_text_wrap(&g, fs[fi], "Ag\nAg", 0, 0, 1e4, 2, lhs[li], 0);
						bool ok = fabs((two - one) - lhs[li] * one) < 1;
						if (!ok)
							fprintf(stderr, "%s at %g, lh %g: lines %.1f apart, want %.1f\n", voices[vi], scales[si],
								lhs[li], two - one, lhs[li] * one);
						CHECK(ok);
					}
					cairo_destroy(mc);
					cairo_surface_destroy(ms);
				}
	}
	/* FR-018: events as paper slips (boards event.drag, event.delete, event.stack) */
	{
		struct store ss;
		store_init(&ss, NULL);
		struct period d = period_of(P_DAY, today);
		store_choose(&ss, d, d.start, "day-agenda");
		struct entry *a = store_add(&ss, E_EVENT, d);
		a->start = 10 * 60, a->end = 11 * 60;
		sample_ink(&a->f, "dentist", 2, 19, 3);
		struct view w = { .per = d, .today = today, .W = 702, .H = 896, .hidden_kind = -1 };
		view_layout(&w, &ss, &L, cr);
		const struct vslip *sa = view_slip(&w, a->id);
		CHECK(sa && sa->nz == 1 && sa->z == 0 && sa->parts && !sa->fan && sa->sh == 0);
		if (sa) {
			/* the tab: 26 x 8, 6 px above the top edge, 30 px in from the right; its target 44 x 28 */
			double r[4];
			slip_tab(sa, r);
			CHECK(r[0] == sa->x + sa->w - 56 && r[1] == sa->y - 6 && r[2] == 26 && r[3] == 8);
			slip_tab_target(sa, r);
			CHECK(r[0] == sa->x + sa->w - 65 && r[1] == sa->y - 16 && r[2] == 44 && r[3] == 28);
			/* the folded corner: max(8, min(14, h - 6)), pulled min(22, h - 4); its target 32 x 32 */
			CHECK(slip_ear(40, false) == 14 && slip_ear(18, false) == 12 && slip_ear(10, false) == 8);
			CHECK(slip_ear(40, true) == 22 && slip_ear(20, true) == 16);
			slip_ear_target(sa, r);
			CHECK(r[0] == sa->x + sa->w - 32 && r[1] == sa->y + sa->h - 32 && r[2] == 32 && r[3] == 32);
			/* lifted: a shadow its size, 5 px right and down */
			slip_lift(sa, r);
			CHECK(r[0] == sa->x + 5 && r[1] == sa->y + 5 && r[2] == sa->w && r[3] == sa->h);
			/* the corner before the tab before the slip; the tab's target reaches above it */
			const struct vhit *h = view_hit(&w, sa->x + sa->w - 6, sa->y + sa->h - 6);
			CHECK(h && h->a == A_EV_EAR && h->id == a->id);
			h = view_hit(&w, sa->x + sa->w - 43, sa->y - 3);
			CHECK(h && h->a == A_EV_TAB && h->id == a->id);
			h = view_hit(&w, sa->x + sa->w - 43, sa->y - 14);
			CHECK(h && h->a == A_EV_TAB);
			h = view_hit(&w, sa->x + 40, sa->y + sa->h / 2);
			CHECK(h && h->a == A_EVENT && h->id == a->id);
			/* the lower edge still changes the end: the slip's target reaches it */
			h = view_hit(&w, sa->x + 40, sa->y + sa->h - 3);
			CHECK(h && h->a == A_EVENT && h->y + h->h - 10 < sa->y + sa->h - 3);
			/* crossed out: edge to edge (either way) and corner to corner delete; a stroke
			 * that starts inside the slip, ends inside it, or touches no slip doesn't */
			struct pl_point q[16];
			double L0 = sa->x, R0 = sa->x + sa->w, T0 = sa->y, B0 = sa->y + sa->h;
			int n = line_pts(q, 16, L0 - 16, T0 + sa->h * 0.86, R0 + 10, T0 + sa->h * 0.18, 0);
			CHECK_INT(view_cross_out(&w, q, n), a->id);
			n = line_pts(q, 16, R0 + 4, T0 + sa->h * 0.5, L0 - 6, T0 + sa->h * 0.4, 0);
			CHECK_INT(view_cross_out(&w, q, n), a->id);
			n = line_pts(q, 16, L0 + 8, T0 - 8, R0 - 8, B0 + 8, 0);
			CHECK_INT(view_cross_out(&w, q, n), a->id);
			n = line_pts(q, 16, L0 + 8, B0 + 6, R0 - 6, T0 - 6, 0);
			CHECK_INT(view_cross_out(&w, q, n), a->id);
			n = line_pts(q, 16, L0 + 30, T0 + sa->h * 0.5, R0 + 10, T0 + sa->h * 0.5, 0);
			CHECK_INT(view_cross_out(&w, q, n), 0); /* starts inside: writing */
			n = line_pts(q, 16, L0 - 16, T0 + sa->h * 0.5, R0 - 30, T0 + sa->h * 0.5, 0);
			CHECK_INT(view_cross_out(&w, q, n), 0); /* ends inside */
			n = line_pts(q, 16, L0 - 16, B0 + 60, R0 + 10, B0 + 70, 0);
			CHECK_INT(view_cross_out(&w, q, n), 0); /* touches no slip */
			n = line_pts(q, 16, L0 - 16, T0 + 4, L0 + 60, T0 - 40, 0);
			CHECK_INT(view_cross_out(&w, q, n), 0); /* through a corner only */

			/* the card that deletes it: 340 wide, right-aligned to the slip, 6 px below it;
			 * Keep, Delete event; a tap outside keeps it; nothing under it takes the pen */
			w.ov = OV_DELETE, w.title = a->id;
			view_layout(&w, &ss, &L, cr);
			sa = view_slip(&w, a->id);
			CHECK(sa && w.pop_w == 340 && w.pop_x + w.pop_w == sa->x + sa->w + 4 && w.pop_y == sa->y + sa->h + 6);
			const struct vhit *keep = find(&w, A_DEL_KEEP, -1), *del = find(&w, A_DEL_YES, -1);
			CHECK(keep && del && keep->x < del->x && del->id == a->id);
			CHECK(keep && keep->x >= w.pop_x && keep->y >= w.pop_y && keep->y + keep->h <= w.pop_y + w.pop_h);
			CHECK(del && del->x + del->w <= w.pop_x + w.pop_w && view_hit(&w, del->x + 4, del->y + 4) == del);
			h = view_hit(&w, w.pop_x + 20, w.pop_y + 20);
			CHECK(h && h->a == A_NONE);
			h = view_hit(&w, 30, 700);
			CHECK(h && h->a == A_DEL_KEEP);
			int cov = 0;
			for (int i = 0; i < w.nf; i++)
				cov += view_covered(&w, &w.f[i]);
			CHECK_INT(cov, w.nf);
			/* no room below: above it */
			a->start = 21 * 60 + 30, a->end = 22 * 60 + 30;
			view_layout(&w, &ss, &L, cr);
			sa = view_slip(&w, a->id);
			CHECK(sa && w.pop_y + w.pop_h + 6 == sa->y);
			a->start = 10 * 60, a->end = 11 * 60;
			w.ov = OV_NONE;
		}
		/* what a slip's states repaint: only its place (and its shadow, the card) */
		{
			cairo_surface_t *s0 = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 702, 896),
					*s1 = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 702, 896);
			cairo_t *c0 = cairo_create(s0), *c1 = cairo_create(s1);
			view_layout(&w, &ss, &L, c0);
			view_paint(&w, &ss, &L, c0);
			cairo_surface_flush(s0);
			sa = view_slip(&w, a->id);
			struct vslip at = sa ? *sa : (struct vslip){ 0 };
			double b[4];
			/* lifted: the shadow, 5 px right and down, ink and paper only (no greys); the
			 * gutter shows the readout as before */
			w.drag = a->id, w.drag_s0 = a->start, w.drag_e0 = a->end;
			view_layout(&w, &ss, &L, c1);
			view_paint(&w, &ss, &L, c1);
			cairo_surface_flush(s1);
			CHECK(diff_box(s0, s1, b) && b[0] >= at.x - 40 && b[1] >= at.y - 12 && b[2] <= at.x + at.w + 6 &&
			      b[3] <= at.y + at.h + 6);
			int ink = 0, grey = 0;
			for (int y = (int)at.y + 8; y < (int)(at.y + at.h); y++)
				for (int x = (int)(at.x + at.w) + 1; x < (int)(at.x + at.w) + 5; x++) {
					uint32_t p = px(s1, x, y), was = px(s0, x, y);
					ink += p == (L.ink & 0xffffff);
					grey += p != was && p != (L.ink & 0xffffff);
				}
			CHECK(ink > 0 && grey == 0);
			w.drag = 0;
			/* crumpled: its place */
			w.crumple = a->id;
			view_layout(&w, &ss, &L, c1);
			view_paint(&w, &ss, &L, c1);
			cairo_surface_flush(s1);
			CHECK(diff_box(s0, s1, b) && b[0] >= at.x && b[1] >= at.y - 7 && b[2] <= at.x + at.w + 1 &&
			      b[3] <= at.y + at.h + 1);
			/* its tab and corner go with it */
			CHECK(find(&w, A_EV_TAB, -1) == NULL && find(&w, A_EV_EAR, -1) == NULL);
			/* the card: one rectangle, its shadow below it */
			w.ov = OV_DELETE, w.title = a->id;
			view_layout(&w, &ss, &L, c0);
			view_paint(&w, &ss, &L, c0);
			cairo_surface_flush(s0);
			CHECK(diff_box(s1, s0, b) && b[0] >= w.pop_x - 4 && b[1] >= w.pop_y - 4 && b[2] <= w.pop_x + w.pop_w + 4 &&
			      b[3] <= w.pop_y + w.pop_h + 4);
			/* deleted: only the slip's place and the card */
			struct vslip was = at;
			w.ov = OV_NONE, w.crumple = 0;
			view_layout(&w, &ss, &L, c1);
			view_paint(&w, &ss, &L, c1);
			cairo_surface_flush(s1);
			store_delete(&ss, a->id);
			view_layout(&w, &ss, &L, c0);
			view_paint(&w, &ss, &L, c0);
			cairo_surface_flush(s0);
			CHECK(view_slip(&w, a->id) == NULL);
			CHECK(diff_box(s1, s0, b) && b[0] >= was.x && b[1] >= was.y - 7 && b[2] <= was.x + was.w + 1 &&
			      b[3] <= was.y + was.h + 1);
			cairo_destroy(c0), cairo_destroy(c1);
			cairo_surface_destroy(s0), cairo_surface_destroy(s1);
		}
		/* overlapping: shingled, the later on top 14 px right; the one underneath shows its
		 * top (18 px at least) while the one above starts later, its bottom once it doesn't */
		{
			struct entry *x = store_add(&ss, E_EVENT, d), *y = store_add(&ss, E_EVENT, d);
			x->start = 14 * 60, x->end = 15 * 60;
			y->start = 14 * 60 + 30, y->end = 15 * 60 + 30;
			sample_ink(&x->f, "dentist", 2, 19, 4);
			sample_ink(&y->f, "call the bank", 2, 19, 5);
			view_layout(&w, &ss, &L, cr);
			const struct vslip *sx = view_slip(&w, x->id), *sy = view_slip(&w, y->id);
			CHECK(sx && sy && sx->nz == 2 && sy->nz == 2 && sx->stack == sy->stack);
			if (sx && sy) {
				CHECK(sx->z == 0 && sy->z == 1 && sy->x == sx->x + 14 && sy->w == sx->w - 14);
				CHECK(sy->sh == 0 && sx->sh >= 18 && sx->strip_top && sx->sy == sx->y && sx->sh == sy->y - sx->y);
				/* the strip is its target, and its field: the top one's takes the pen above it */
				const struct vhit *h = view_hit(&w, sx->x + 4, sx->y + sx->sh / 2);
				CHECK(h && h->a == A_EV_STRIP && h->id == x->id && h->arg == 2);
				h = view_hit(&w, sy->x + 40, sy->y + sy->h / 2);
				CHECK(h && h->a == A_EVENT && h->id == y->id);
				const struct vfield *fx = view_field_of(&w, x->id), *fy = view_field_of(&w, y->id);
				CHECK(fx && fy && fx->layer == 1 && fy->layer == 2 && fx->y == sx->sy && fx->h == sx->sh);
				CHECK(view_field_at(&w, sy->x + 40, sy->y + 6) == fy);
				CHECK(view_field_at(&w, sx->x + 40, sx->y + 6) == fx);
				/* crossed out through both: the one on top */
				struct pl_point q[16];
				int n = line_pts(q, 16, sx->x - 16, sy->y + 10, sx->x + sx->w + 10, sy->y + 10, 0);
				CHECK_INT(view_cross_out(&w, q, n), y->id);
			}
			/* a tap on the strip brings it to the top: now it is shifted, the other shows its bottom */
			view_raise(&w, x->id);
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id), sy = view_slip(&w, y->id);
			CHECK(sx && sy && sx->z == 1 && sy->z == 0 && sx->x == sy->x + 14);
			CHECK(sx && sy && !sy->strip_top && sy->sy == sx->y + sx->h && sy->sh >= 18);
			const struct vfield *fx = view_field_of(&w, x->id), *fy = view_field_of(&w, y->id);
			CHECK(fx && fy && fx->layer == 2 && fy->layer == 1);
			/* equal times: still a strip of 18 px */
			y->start = x->start, y->end = x->end;
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id), sy = view_slip(&w, y->id);
			CHECK(sx && sy && (sx->sh >= 18 || sy->sh >= 18) && (sx->sh == 0) != (sy->sh == 0));
			/* nested (starts later, ends earlier) under the longer: its bottom reaches 18 px past */
			x->start = 14 * 60, x->end = 16 * 60;
			y->start = 14 * 60 + 30, y->end = 15 * 60;
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id), sy = view_slip(&w, y->id);
			CHECK(sx && sy && sx->z == 1 && sy->z == 0 && !sy->strip_top && sy->sh >= 18);
			w.nraise = 0;
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id), sy = view_slip(&w, y->id);
			CHECK(sx && sy && sx->z == 0 && sy->z == 1 && sx->strip_top && sx->sh >= 18);
			/* three: a tap on a strip fans them out, each a third, 10 px apart, at their own
			 * times, no tab or corner; a fanned slip's target brings it to the top */
			struct entry *z = store_add(&ss, E_EVENT, d);
			x->start = 14 * 60, x->end = 15 * 60;
			y->start = 14 * 60 + 30, y->end = 15 * 60 + 30;
			z->start = 15 * 60, z->end = 16 * 60;
			sample_ink(&z->f, "keys", 2, 19, 6);
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id);
			CHECK(sx && sx->nz == 3);
			const struct vhit *h = find(&w, A_EV_STRIP, 3);
			CHECK(h != NULL);
			double x0 = sx ? sx->x : 0, w0 = sx ? sx->w : 0;
			w.fan = h ? h->id : x->id;
			view_layout(&w, &ss, &L, cr);
			const struct vslip *f3[3] = { view_slip(&w, x->id), view_slip(&w, y->id), view_slip(&w, z->id) };
			CHECK(f3[0] && f3[1] && f3[2]);
			if (f3[0] && f3[1] && f3[2]) {
				double fw = floor((w0 - 20) / 3);
				for (int i = 0; i < 3; i++) {
					CHECK(f3[i]->fan && !f3[i]->parts && f3[i]->w == fw && f3[i]->x == x0 + i * (fw + 10));
					const struct vfield *ff = view_field_of(&w, f3[i]->id);
					CHECK(ff && ff->x == f3[i]->x && ff->w == fw && ff->y == f3[i]->y);
					const struct vhit *fh = view_hit(&w, f3[i]->x + 10, f3[i]->y + 10);
					CHECK(fh && fh->a == A_EV_FAN && fh->id == f3[i]->id);
				}
				CHECK(f3[1]->y > f3[0]->y && f3[2]->y > f3[1]->y);
				CHECK(find(&w, A_EV_TAB, -1) == NULL && find(&w, A_EV_EAR, -1) == NULL);
			}
			/* a fanned slip tapped: on top, the fan closed */
			view_raise(&w, x->id);
			w.fan = 0;
			view_layout(&w, &ss, &L, cr);
			sx = view_slip(&w, x->id);
			CHECK(sx && !sx->fan && sx->z == 2 && sx->sh == 0 && view_field_of(&w, x->id)->layer == 3);
			/* the stack gone: a fan asked for closes */
			store_delete(&ss, z->id);
			w.fan = x->id;
			view_layout(&w, &ss, &L, cr);
			CHECK(w.fan == 0);
			w.nraise = 0;
		}
		/* a finger held still: due after 0.5 s, once; moved past 12 px first, it is a drag */
		{
			struct hold hd;
			hold_begin(&hd, 7, 100, 100, 1000);
			CHECK_INT(hold_left(&hd, 1200), 300);
			CHECK(hold_move(&hd, 108, 92));
			CHECK(!hold_fire(&hd, 1499));
			CHECK(hold_fire(&hd, 1500) && hd.fired && hd.id == 7);
			CHECK(!hold_fire(&hd, 1600) && hold_left(&hd, 1600) == -1);
			hold_begin(&hd, 7, 100, 100, 1000);
			CHECK(!hold_move(&hd, 113, 100));
			CHECK(!hold_fire(&hd, 2000) && hold_left(&hd, 2000) == -1);
			hold_end(&hd);
			CHECK(hd.id == 0);
		}
		store_free(&ss);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	store_free(&s);
	/* FR-018: a stroke into the free time. Written (a letter's size): an event from the half
	 * hour it starts in, an hour long, the stroke its title's first ink on the pixels it was
	 * written on, and the title's field there at once for the next stroke */
	{
		struct store fs;
		store_init(&fs, NULL);
		struct view fv = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		view_layout(&fv, &fs, &L, cr);
		const struct vfield *ff = NULL;
		for (int i = 0; i < fv.nf; i++)
			if (fv.f[i].k == F_FREE)
				ff = &fv.f[i];
		CHECK(ff != NULL);
		if (ff) {
			struct vfield f = *ff;
			double pitch = 60 / f.mpp, y = (14 * 60 + 10 - f.min0) / f.mpp;
			struct relay r = { &fv, &fs, &L, cr, 0 };
			struct pl_point q[16];
			/* a tall letter (an l), as tall as a block's least, less a pixel */
			int n = line_pts(q, 12, 120, y, 122, y + 3 * 19.0 - 1, 0);
			int st, en;
			CHECK(!view_free_block(&f, q, n, &st, &en));
			bool kept;
			struct entry *e = view_free_stroke(&fv, &fs, &f, 7, q, n, relay_out, &r, &kept);
			CHECK(e && kept && r.n == 1);
			if (e) {
				CHECK_INT(e->start, 14 * 60);
				CHECK_INT(e->end, 15 * 60);
				CHECK_INT(e->f.ns, 1);
				CHECK_INT(e->f.s[0].k, 7);
				const struct vfield *t = view_field_of(&fv, e->id);
				CHECK(t && t->layer == 1);
				if (t && e->f.ns == 1) {
					/* the same surface pixels, inside the title's field */
					CHECK(fabs(t->x + e->f.s[0].pt[0].x - (f.x + q[0].x)) < 1e-3);
					CHECK(fabs(t->y + e->f.s[0].pt[0].y - (f.y + q[0].y)) < 1e-3);
					CHECK(f.x + q[0].x >= t->x && f.y + q[0].y >= t->y && f.y + q[0].y < t->y + t->h);
					/* the next stroke beside it lands in the title, not the free time */
					CHECK(view_field_at(&fv, f.x + q[0].x + 30, f.y + q[0].y + 10) == t);
				}
			}
			/* a written word low in the hour: from that half hour */
			n = line_pts(q, 12, 120, (14 * 60 + 40 - f.min0) / f.mpp, 200, (14 * 60 + 40 - f.min0) / f.mpp + 14, 6);
			CHECK(!view_free_block(&f, q, n, &st, &en));
			CHECK_INT(st, 14 * 60 + 30);
			CHECK_INT(en, 15 * 60 + 30);
			/* a block: a narrow stroke down over two hours makes the event over that time and
			 * leaves no ink */
			double y16 = (16 * 60 - f.min0) / f.mpp;
			n = line_pts(q, 16, 300, y16 + 2, 304, y16 + 2 * pitch - 2, 0);
			CHECK(view_free_block(&f, q, n, &st, &en));
			e = view_free_stroke(&fv, &fs, &f, 8, q, n, relay_out, &r, &kept);
			CHECK(e && !kept);
			if (e) {
				CHECK_INT(e->start, 16 * 60);
				CHECK_INT(e->end, 18 * 60);
				CHECK_INT(e->f.ns, 0);
			}
		}
		store_free(&fs);
	}
	return t_done("view");
}
