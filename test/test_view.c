// SPDX-License-Identifier: GPL-2.0-or-later
/* The day view: its fields (one writing region each), its controls and the hidden count. */
#include <stdlib.h>

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
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	store_free(&s);
	return t_done("view");
}
