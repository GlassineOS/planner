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
		field_value(&t, &ent, 0, "call the bank", wd, 3, NULL, 0, NULL, 0);
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
		field_value(&t, &ent, 0, "call", wd, 1, NULL, 0, NULL, 0);
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
	/* titles (FR-018): one that fits is whole (a tap opens its card), a long one in a short
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
		/* its card shows it whole: as wide as the screen allows */
		w.ov = OV_CARD;
		w.title = e->id;
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_CLOSE, -1) != NULL && find(&w, A_EV_OPEN, -1) != NULL);
		CHECK(w.pop_w > 340 && w.pop_x >= 8 && w.pop_x + w.pop_w <= 702 - 8);
		store_delete(&s, e->id);
	}
	/* events as bracket pairs in lanes (FR-018; boards event.stack, event.delete, event.open) */
	{
		struct period d = period_of(P_DAY, today);
		store_choose(&s, d, d.start, "day-agenda");
		struct entry *ev[64], *den = NULL;
		int ne = store_list(&s, E_EVENT, d, NULL, ev, 64);
		for (int i = 0; i < ne; i++)
			if (ev[i]->start == 14 * 60)
				den = ev[i];
		CHECK(den != NULL);
		struct entry *bank = store_add(&s, E_EVENT, d), *cafe = store_add(&s, E_EVENT, d);
		bank->start = 14 * 60 + 30, bank->end = 15 * 60 + 30;
		sample_ink(&bank->f, "call the bank", 2, 19, 21);
		cafe->start = 13 * 60, cafe->end = 16 * 60;
		sample_ink(&cafe->f, "work from the café", 2, 19, 22);
		struct view w = { .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
		view_layout(&w, &s, &L, cr);
		const struct vev *vd = view_ev(&w, den->id), *vb = view_ev(&w, bank->id), *vc = view_ev(&w, cafe->id);
		CHECK(vd && vb && vc);
		if (vd && vb && vc) {
			/* the café first, then the dentist, then the bank; three equal lanes, ] [ 12 px apart */
			CHECK(vc->ln.lane == 0 && vd->ln.lane == 1 && vb->ln.lane == 2);
			CHECK(vc->ln.lanes == 3 && vd->ln.lanes == 3 && vb->ln.lanes == 3);
			CHECK(vc->w == vd->w && vd->w == vb->w);
			CHECK(fabs(vd->spine - (vc->spine + vc->w) - BK_GAP) < 1e-9 && fabs(vb->spine - (vd->spine + vd->w) - BK_GAP) < 1e-9);
			CHECK(vc->h > vd->h && vd->y < vb->y);
			/* each title's field in its own lane, none over another's */
			const struct vfield *fd = view_field_of(&w, den->id), *fb = view_field_of(&w, bank->id), *fc = view_field_of(&w, cafe->id);
			CHECK(fd && fb && fc);
			if (fd && fb && fc) {
				CHECK(fc->x + fc->w <= vc->spine + vc->w && fd->x >= vd->spine && fd->x + fd->w <= vd->spine + vd->w);
				CHECK(fb->x >= vb->spine && fb->x + fb->w <= vb->spine + vb->w);
			}
			/* the ends' handles before the titles; a later lane's end handle only over the gap
			 * and the serif: a pen-down just inside the neighbour's "]" is its title's */
			int top = -1, ttl = -1;
			for (int i = 0; i < w.nh; i++) {
				if (w.hit[i].id == bank->id && w.hit[i].a == A_EV_TOP && top < 0)
					top = i;
				if (w.hit[i].id == bank->id && w.hit[i].a == A_EVENT && ttl < 0)
					ttl = i;
			}
			CHECK(top >= 0 && ttl > top);
			const struct vhit *h = view_hit(&w, vb->spine + 2, vb->y + 1);
			CHECK(h && h->a == A_EV_TOP && h->id == bank->id);
			h = view_hit(&w, vb->spine - BK_GAP - 4, vb->y + 1); /* the dentist's lane, left of the gap */
			CHECK(h && h->id == den->id);
			h = view_hit(&w, vb->spine + BK_TITLE + 4, vb->y + 1); /* the bank's title, right of its "[" */
			CHECK(h && h->a == A_EVENT && h->id == bank->id);
			h = view_hit(&w, vc->spine - 20, vc->y + 2); /* the first lane's: over the hour margin */
			CHECK(h && h->a == A_EV_TOP && h->id == cafe->id);
			CHECK(view_hit(&w, vb->spine + vb->w / 2, vb->y + vb->h / 2)->id == bank->id);
			/* a pen-down point belongs to one lane, never two */
			CHECK(view_lane_at(&w, vd->spine + 3, vb->y + 5) == den->id);
			CHECK(view_lane_at(&w, vb->spine + 3, vb->y + 5) == bank->id);
			CHECK(view_lane_at(&w, vb->spine - BK_GAP / 2, vb->y + 5) == 0); /* the gap: loose ink */
			CHECK(view_lane_at(&w, vc->spine + 5, vc->y + vc->h - 4) == cafe->id);
			/* the pen in the bank's lane: the café's and the dentist's brackets grey, the bank's
			 * and the run's (another cluster) in the ink */
			w.focus = bank->id;
			view_paint(&w, &s, &L, cr);
			cairo_surface_flush(sf);
			uint32_t ink = px(sf, vb->spine, vb->y + vb->h / 2), quiet = px(sf, vc->spine, vc->y + vc->h / 2);
			CHECK(ink < 0x404040 && quiet > 0x808080 && quiet < 0xf0f0f0);
			CHECK(px(sf, vd->spine, vd->y + vd->h / 2) == quiet);
			w.focus = 0;
			view_paint(&w, &s, &L, cr);
			cairo_surface_flush(sf);
			CHECK(px(sf, vc->spine, vc->y + vc->h / 2) < 0x404040);
			/* struck through: from before the first letter to past the last, through it */
			struct pl_point q[24];
			double my = vd->ink[1] + vd->ink[3] / 2;
			int n = line_pts(q, 24, vd->tx - 3, my, vd->ink[0] + vd->ink[2] + 8, my + 2, 0);
			CHECK(view_strike(&w, q, n) == den->id);
			n = line_pts(q, 24, vd->ink[0] + 4, my, vd->ink[0] + vd->ink[2] + 8, my + 2, 0); /* from inside: writing */
			CHECK(view_strike(&w, q, n) == 0);
			n = line_pts(q, 24, vd->tx - 3, my, vd->ink[0] + vd->ink[2] * 0.6, my, 0); /* not across all of it */
			CHECK(view_strike(&w, q, n) == 0);
			n = line_pts(q, 24, vd->tx - 3, my, vd->ink[0] + vd->ink[2] + 8, my + 40, 0); /* steep: writing */
			CHECK(view_strike(&w, q, n) == 0);
			n = line_pts(q, 24, vd->tx - 3, vd->ink[1] - 30, vd->ink[0] + vd->ink[2] + 8, vd->ink[1] - 30, 0); /* above it */
			CHECK(view_strike(&w, q, n) == 0);
			/* its card: Delete event and Open; the asking card: Keep and Delete event; no field
			 * open under either */
			w.ov = OV_CARD, w.title = den->id;
			view_layout(&w, &s, &L, cr);
			CHECK(find(&w, A_EV_OPEN, -1) && find(&w, A_EV_ASK, -1) && find(&w, A_CLOSE, -1));
			CHECK(view_covered(&w, fd) && view_field_at(&w, fd->x + 4, fd->y + 4) == NULL);
			w.ov = OV_DELETE;
			view_layout(&w, &s, &L, cr);
			CHECK(find(&w, A_DEL_YES, -1) && find(&w, A_DEL_KEEP, -1));
			/* a tap outside the asking card keeps the event */
			CHECK(view_hit(&w, 2, w.H - 4)->a == A_DEL_KEEP);
			CHECK(view_strike(&w, q, n) == 0);
		}
		/* a line down the margin: from the half hour it began in to the one it reached, up or
		 * down, at least 30 minutes; the margin is where it may start */
		{
			const struct vfield *ff = NULL;
			for (int i = 0; i < w.nf; i++)
				if (w.f[i].k == F_FREE)
					ff = &w.f[i];
			CHECK(ff != NULL);
			if (ff) {
				int st, en;
				double y16 = ff->y + (16 * 60 - ff->min0) / ff->mpp, hr = 60 / ff->mpp;
				view_margin_span(ff, y16 + 2, y16 + hr - 2, &st, &en);
				CHECK(st == 16 * 60 && en == 17 * 60);
				view_margin_span(ff, y16 + hr - 2, y16 + 2, &st, &en);
				CHECK(st == 16 * 60 && en == 17 * 60);
				view_margin_span(ff, y16 + 1, y16 + 2, &st, &en);
				CHECK(st == 16 * 60 && en == 16 * 60 + 30);
				w.ov = OV_NONE;
				view_layout(&w, &s, &L, cr);
				CHECK(view_margin_at(&w, ff->x - 20, y16) != NULL && view_margin_at(&w, ff->x + 4, y16) == NULL);
			}
		}
		/* the event's page: the title and Where, Notes, With written (one region each), the
		 * times' − and +, the choices, Delete event that asks */
		w.ov = OV_PAGE, w.title = den->id, w.page_ask = false;
		view_layout(&w, &s, &L, cr);
		int subs = 0;
		for (int i = 0; i < w.nf; i++)
			if (w.f[i].k == F_ENTRY && w.f[i].entry == den->id)
				subs |= 1 << w.f[i].sub;
		CHECK_INT(subs, 0xf);
		for (int i = 0; i < w.nf; i++)
			for (int j = 0; j < i; j++)
				CHECK(strcmp(w.f[i].name, w.f[j].name));
		for (int k = 0; k < 4; k++)
			CHECK(find(&w, A_PG_TIME, k) != NULL);
		for (int k = 0; k < 5; k++)
			CHECK(find(&w, A_PG_REPEAT, k) && find(&w, A_PG_REMIND, k));
		CHECK(find(&w, A_PG_BACK, -1) && find(&w, A_PG_DAY, -1) && find(&w, A_PG_DELETE, 0));
		CHECK(!find(&w, A_VIEW, -1)); /* its bar: only Back */
		const struct vfield *pf = NULL;
		for (int i = 0; i < w.nf; i++)
			if (w.f[i].sub == EF_NOTES)
				pf = &w.f[i];
		CHECK(pf && view_field_at(&w, pf->x + 5, pf->y + 5) == pf && pf->guide == 36);
		w.page_ask = true;
		view_layout(&w, &s, &L, cr);
		CHECK(find(&w, A_PG_DELETE, 1) && find(&w, A_PG_DELETE, 2));
		CHECK(pf && view_field_at(&w, pf->x + 5, pf->y + 5) == NULL);
		/* routing (board event.route): a pen-down in the gap before the dentist's "[" is the
		 * dentist's; the gap between the café's "]" and the dentist's "[" goes to the nearer one,
		 * the "[" when as near; inside a lane or in the margin it is no gap */
		w.ov = OV_NONE, w.page_ask = false;
		view_layout(&w, &s, &L, cr);
		vd = view_ev(&w, den->id), vc = view_ev(&w, cafe->id), vb = view_ev(&w, bank->id);
		if (vd && vc && vb) {
			double gy = vd->y + 10, mid = (vc->spine + vc->w + vd->spine) / 2;
			CHECK(view_gap_at(&w, vd->spine - 3, gy) == den->id);
			CHECK(view_gap_at(&w, vc->spine + vc->w + 3, gy) == cafe->id);
			CHECK(view_gap_at(&w, mid, gy) == den->id);
			CHECK(view_gap_at(&w, vd->spine + 20, gy) == 0 && view_gap_at(&w, vc->spine - 10, gy) == 0);
			/* below the dentist, in its lane's empty space: free time (no event's) */
			CHECK(view_lane_at(&w, vd->spine + 20, vd->y + vd->h + 30) == 0 &&
			      view_gap_at(&w, vd->spine + 20, vd->y + vd->h + 30) == 0);
		}
		/* the now line crosses no ink: its dot in the margin, the line broken around every
		 * event at its height (board event.more) */
		setenv("GLASSINE_PLANNER_NOW", "860", 1);
		view_layout(&w, &s, &L, cr);
		CHECK(w.now_y > 0 && w.now_dot < BK_SPINE + 12 + 2);
		{
			bool clear = true;
			for (int i = 0; i < w.nev; i++) {
				const struct vev *e = &w.ev[i];
				double bot = fmax(e->y + fmax(e->h, e->th), e->ink[2] > 0 ? e->ink[1] + e->ink[3] : 0);
				if (w.now_y < e->y - 2 || w.now_y > bot + 2)
					continue;
				for (int j = 0; j < w.nnow; j++)
					if (w.now_seg[j][0] < e->spine + e->w + 2 && w.now_seg[j][0] + w.now_seg[j][1] > e->spine - 1)
						clear = false;
			}
			CHECK(clear && w.nnow >= 1);
		}
		/* five at once on a 702 px wide day: one row each, in start order, the hours drawn
		 * taller; no title crosses a rule (each row's ink inside its own line); no end handles;
		 * the now line only its dot */
		struct entry *pl = store_add(&s, E_EVENT, d), *pa = store_add(&s, E_EVENT, d);
		pl->start = 13 * 60 + 30, pl->end = 15 * 60;
		sample_ink(&pl->f, "the plumber comes", 2, 19, 33);
		pa->start = 14 * 60 + 30, pa->end = 15 * 60;
		sample_ink(&pa->f, "sign for the parcel", 2, 19, 34);
		view_layout(&w, &s, &L, cr);
		{
			const struct entry *order[] = { cafe, pl, den, bank, pa };
			bool rows = true, inside = true, sorted = true, ends = false;
			double prev = -1;
			for (int i = 0; i < 5; i++) {
				const struct vev *e = view_ev(&w, order[i]->id);
				if (!e) {
					rows = false;
					continue;
				}
				rows = rows && e->ln.rows && e->ln.order == i && e->ln.lanes == 1;
				sorted = sorted && e->y > prev;
				prev = e->y;
				/* the row's rules: ROW_GAP / 2 above its top, ROW_GAP / 2 below its brackets */
				if (e->ink[2] > 0)
					inside = inside && e->ink[1] > e->y - ROW_GAP / 2.0 && e->ink[1] + e->ink[3] < e->y + e->h + ROW_GAP / 2.0 &&
						 e->ink[0] + e->ink[2] <= e->spine + e->w;
				for (int j = 0; j < w.nh; j++)
					if ((w.hit[j].a == A_EV_TOP || w.hit[j].a == A_EV_BOT) && w.hit[j].id == order[i]->id)
						ends = true;
			}
			CHECK(rows && sorted && inside && !ends);
			/* no rule as drawn crosses a row's title ink: the hour rules of the group stay in the
			 * margin, each row has its own */
			{
				bool crossed = false, margin = true;
				for (int i = 0; i < 5; i++) {
					const struct vev *e = view_ev(&w, order[i]->id);
					if (!e || e->ink[2] <= 0)
						continue;
					for (int j = 0; j < w.nrule; j++) {
						const struct vbox *r = &w.rule[j];
						if (r->y + r->h > e->ink[1] && r->y < e->ink[1] + e->ink[3] && r->x < e->ink[0] + e->ink[2] &&
						    r->x + r->w > e->ink[0])
							crossed = true;
						if (r->y > e->y && r->y < e->y + e->h && r->x + r->w > e->spine)
							margin = false; /* inside a row only the margin is ruled */
					}
				}
				CHECK(!crossed && margin && w.nrule > 20);
			}
			const struct vev *ec = view_ev(&w, cafe->id), *ea = view_ev(&w, pa->id);
			/* at least twice as tall: three hours over 2 x 3 x the pitch the other hours had */
			const struct vfield *ff = NULL;
			for (int i = 0; i < w.nf; i++)
				if (w.f[i].k == F_FREE)
					ff = &w.f[i];
			CHECK(ff && ec && ea);
			if (ff && ec && ea) {
				double span = tmap_y(&ff->tm, 16 * 60) - tmap_y(&ff->tm, 13 * 60);
				CHECK(span >= ROW_SCALE * 3 * (60 / ff->mpp) - 1);
				CHECK(ea->y + ea->h <= ff->y + tmap_y(&ff->tm, 16 * 60) + 1);
				CHECK(fabs(tmap_y(&ff->tm, 23 * 60) - ff->h) < 1e-6);
				/* minutes and px round trip through the scale, a line drawn down the margin in it
				 * spans the time at its scale */
				int st, en;
				view_margin_span(ff, ff->y + tmap_y(&ff->tm, 13 * 60), ff->y + tmap_y(&ff->tm, 14 * 60), &st, &en);
				CHECK(st == 13 * 60 && en == 14 * 60);
			}
			CHECK(w.now_y > 0 && w.nnow == 0); /* only the dot */
			/* the margin of the stretched hours is shaded: the band's grey under the hour labels */
			const struct vhit *tt = find(&w, A_EVENT, -1);
			CHECK(tt != NULL);
			/* the smallest lane is a setting: at 110 px the same five fit in lanes */
			s.min_lane = 110;
			view_layout(&w, &s, &L, cr);
			ec = view_ev(&w, cafe->id);
			CHECK(ec && !ec->ln.rows && ec->ln.lanes == 5);
			s.min_lane = 160;
			/* turned, 936 px: five lanes of 160 px fit (board event.more land) */
			struct view tl = { .per = d, .today = today, .W = 936, .H = 662, .hidden_kind = -1 };
			view_layout(&tl, &s, &L, cr);
			ec = view_ev(&tl, cafe->id);
			CHECK(ec && !ec->ln.rows && ec->ln.lanes == 5 && ec->w >= 160);
			/* the small widget keeps its hours and folds, never quietly: a bar, named in the head,
			 * whose tap opens the day */
			store_choose(&s, d, d.start, "day-diary");
			struct view sw = { .per = d, .today = today, .W = 936, .H = 420, .hidden_kind = -1 };
			view_layout(&sw, &s, &L, cr);
			CHECK(sw.folded > 0 && sw.nfold == 1);
			if (sw.nfold) {
				const struct vbox *fb = &sw.fold[0];
				const struct vhit *th = view_hit(&sw, fb->x + fb->w / 2, fb->y + fb->h / 2);
				CHECK(th && th->a == A_FOLD && th->arg == d.start);
				CHECK(fb->h == FOLD_H2 || fb->h == FOLD_H);
				CHECK(fb->w >= FOLD_TAP_W ? fb->h == FOLD_H : fb->h == FOLD_H2);
				/* the now line keeps clear of the bar */
				for (int j = 0; j < sw.nnow; j++)
					CHECK(!(sw.now_y > fb->y - 2 && sw.now_y < fb->y + fb->h + 2 && sw.now_seg[j][0] < fb->x + fb->w &&
						sw.now_seg[j][0] + sw.now_seg[j][1] > fb->x));
			}
			/* the widget at a larger size: rows first, the rest in a one-line bar */
			struct view sb = { .per = d, .today = today, .W = 1100, .H = 640, .hidden_kind = -1 };
			view_layout(&sb, &s, &L, cr);
			ec = view_ev(&sb, cafe->id);
			CHECK(sb.nfold == 1 && sb.folded > 0 && sb.folded < 5 && ec && ec->ln.rows);
			if (sb.nfold)
				CHECK(sb.fold[0].w >= FOLD_TAP_W && sb.fold[0].h == FOLD_H);
			/* its tap opens the whole day: every event of the group, none folded */
			sw.whole = true, sw.whole_day = d.start;
			view_layout(&sw, &s, &L, cr);
			CHECK(sw.folded == 0 && sw.nfold == 0);
			for (int i = 0; i < 5; i++)
				CHECK(view_ev(&sw, order[i]->id) != NULL);
			/* the day view never hides an event */
			CHECK(w.folded == 0);
			store_choose(&s, d, d.start, "day-agenda");
		}
		unsetenv("GLASSINE_PLANNER_NOW");
		store_delete(&s, pl->id), store_delete(&s, pa->id);
		store_delete(&s, bank->id), store_delete(&s, cafe->id);
		/* a long title moved beside the dentist: its words rewrapped to the lane, each inside
		 * the brackets, the strokes as stored; in free time again, as written (board event.wrap) */
		{
			struct entry *co = store_add(&s, E_EVENT, d);
			co->start = 16 * 60, co->end = 17 * 60;
			sample_ink(&co->f, "take the coats to the cleaner", 2, 19, 35);
			view_layout(&w, &s, &L, cr);
			const struct vev *e = view_ev(&w, co->id);
			double one = e ? e->ink[2] : 0;
			float x5 = co->f.s[5].pt[0].x;
			co->start = 14 * 60 + 30, co->end = 15 * 60 + 30;
			view_layout(&w, &s, &L, cr);
			e = view_ev(&w, co->id);
			CHECK(e && e->ln.lanes == 2 && !e->ln.rows);
			if (e) {
				CHECK(e->ink[2] > 0 && e->ink[2] <= e->tw && e->ink[2] < one);
				CHECK(e->ink[1] >= e->y && e->ink[1] + e->ink[3] <= e->y + e->h + 0.5);
				CHECK(!e->cut);
			}
			CHECK(co->f.s[5].pt[0].x == x5);
			/* held (being moved): as written, cut at the lane */
			w.drag = co->id, w.drag_end = 0;
			view_layout(&w, &s, &L, cr);
			e = view_ev(&w, co->id);
			CHECK(e && e->cut);
			w.drag = 0;
			/* half an hour with the café around both: shrunk to 70 %, then cut */
			co->end = 15 * 60;
			struct entry *c2 = store_add(&s, E_EVENT, d);
			c2->start = 13 * 60, c2->end = 16 * 60;
			view_layout(&w, &s, &L, cr);
			e = view_ev(&w, co->id);
			CHECK(e && e->cut && e->ink[3] <= e->h);
			store_delete(&s, c2->id);
			co->start = 16 * 60, co->end = 17 * 60;
			view_layout(&w, &s, &L, cr);
			e = view_ev(&w, co->id);
			CHECK(e && fabs(e->ink[2] - one) < 1e-6);
			store_delete(&s, co->id);
		}
		/* back to back: the first's end handle and the second's start handle meet halfway */
		struct entry *a1 = store_add(&s, E_EVENT, d), *a2 = store_add(&s, E_EVENT, d);
		a1->start = 19 * 60, a1->end = 20 * 60, a2->start = 20 * 60, a2->end = 21 * 60;
		w.ov = OV_NONE, w.page_ask = false;
		view_layout(&w, &s, &L, cr);
		const struct vev *v1 = view_ev(&w, a1->id), *v2 = view_ev(&w, a2->id);
		CHECK(v1 && v2 && v1->ln.lanes == 1 && v2->ln.lanes == 1);
		if (v1 && v2) {
			double mid = (v1->y + v1->h + v2->y) / 2;
			const struct vhit *h1 = view_hit(&w, v1->spine, mid - 3), *h2 = view_hit(&w, v2->spine, mid + 3);
			CHECK(h1 && h1->a == A_EV_BOT && h1->id == a1->id);
			CHECK(h2 && h2->a == A_EV_TOP && h2->id == a2->id);
		}
		store_delete(&s, a1->id), store_delete(&s, a2->id);
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
