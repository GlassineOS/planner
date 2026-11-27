// SPDX-License-Identifier: GPL-2.0-or-later
/* The day view: its fields (one writing region each), its controls and the hidden count. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	store_free(&s);
	return t_done("view");
}
