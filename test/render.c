// SPDX-License-Identifier: GPL-2.0-or-later
/* Offscreen renders of every shipped template in each voice, upright and turned, and of
 * the overlays (spec 001 acceptance), into out/renders/. The window is the design's screen
 * less the system's status bar: 702 x 896 upright, 936 x 662 turned. Synthetic ink. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/view.h"
#include "sample.h"

static const char *VOICES[] = { "white", "editorial", "stationery" };

static void shot(struct store *s, const char *voice, struct view *v, const char *name)
{
	struct look L;
	look_init(&L, voice);
	/* with the keyboard up the window is as tall as what it leaves (the page isn't laid out again) */
	int h = v->vis_h > 0 ? (int)v->vis_h : (int)v->H;
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)v->W, h);
	cairo_t *cr = cairo_create(sf);
	view_layout(v, s, &L, cr);
	view_paint(v, s, &L, cr);
	char p[256];
	snprintf(p, sizeof p, "out/renders/%s.%s.png", name, voice);
	cairo_surface_write_to_png(sf, p);
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
}

int main(void)
{
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	setenv("GLASSINE_PLANNER_NOW", "860", 1);
	mkdir("out", 0755);
	mkdir("out/renders", 0755);
	struct store s;
	store_init(&s, NULL);
	sample_fill(&s);
	int today = day_today(), n = 0;
	for (int vi = 0; vi < 3; vi++) {
		for (int i = 0; i < pl_ntemplates; i++) {
			const struct tmpl *t = pl_templates[i];
			for (int land = 0; land < 2; land++) {
				struct period p = period_of(t->kind, today);
				store_choose(&s, p, p.start, t->id);
				struct view v = { .per = p, .today = today, .W = land ? 936 : 702, .H = land ? 662 : 896 };
				char name[96];
				snprintf(name, sizeof name, "%s%s", t->id, land ? ".landscape" : "");
				shot(&s, VOICES[vi], &v, name);
				n++;
			}
		}
		struct period d = period_of(P_DAY, today);
		store_choose(&s, d, d.start, "day-lists");
		static const struct { enum overlay ov; const char *name; } OV[] = {
			{ OV_INFO, "info.popup" }, { OV_MENU, "cog.menu" }, { OV_PICKER, "templates.picker" } };
		for (int k = 0; k < 3; k++) {
			struct view v = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV[k].ov };
			shot(&s, VOICES[vi], &v, OV[k].name);
			n++;
		}
		/* text mode: one field, then the whole view, the keyboard up (boards text.field, text.view) */
		{
			struct view w = { .per = d, .today = today, .W = 702, .H = 896 };
			shot(&s, VOICES[vi], &w, "tmp");
			const struct vfield *f = NULL;
			for (int i = 0; i < w.nf; i++)
				if (w.f[i].k == F_ENTRY && w.f[i].h <= 48 && store_get(&s, w.f[i].entry)->kind == E_TODO)
					f = f ? f : &w.f[i];
			static char text[128];
			snprintf(text, sizeof text, "%s", store_get(&s, f->entry)->f.value);
			snprintf(w.tm_name, sizeof w.tm_name, "%s", f->name);
			w.tm = 1, w.tm_text = text, w.tm_cur = (int)strlen(text), w.tm_neaten = true;
			w.vis_h = 896 - 236;
			w.scroll = view_scroll_to(&w, f);
			shot(&s, VOICES[vi], &w, "text.field");
			w.tm = 2;
			shot(&s, VOICES[vi], &w, "text.view");
			n += 2;
		}
		{
			struct view w = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV_SETTINGS };
			shot(&s, VOICES[vi], &w, "cog.settings");
			w.ov = OV_INFO, w.search = true, w.q = "the", w.q_cur = 3;
			shot(&s, VOICES[vi], &w, "info.search");
			n += 2;
		}
		store_choose(&s, d, d.start, "day-agenda");
		/* titles (board event.title): one grows into the free time below it, one shrinks, one
		 * is cut at its block's edge */
		{
			struct entry *g = store_add(&s, E_EVENT, d), *k = store_add(&s, E_EVENT, d), *c = store_add(&s, E_EVENT, d);
			g->start = 11 * 60, g->end = 11 * 60 + 30;
			sample_ink(&g->f, "groceries", 2, 19, 11);
			sample_ink(&g->f, "and the pharmacy", 2, 46, 12);
			k->start = 12 * 60, k->end = 12 * 60 + 30;
			sample_ink(&k->f, "return the library books and pick up the reserved ones on the way", 2, 19, 13);
			c->start = 13 * 60, c->end = 13 * 60 + 30;
			sample_ink(&c->f, "pick up the parcel and the new keys from the neighbour across the road before six, then "
					  "bring the blue folder to the office", 2, 19, 14);
			struct view w = { .per = d, .today = today, .W = 702, .H = 896 };
			shot(&s, VOICES[vi], &w, "event.title");
			w.ov = OV_TITLE, w.title = c->id;
			shot(&s, VOICES[vi], &w, "event.title.whole");
			store_delete(&s, g->id), store_delete(&s, k->id), store_delete(&s, c->id);
			n += 2;
		}
		struct view v = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV_HIDDEN, .hidden_kind = 0 };
		shot(&s, VOICES[vi], &v, "hidden.list");
		struct look L;
		look_init(&L, VOICES[vi]);
		cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 702, 896);
		cairo_t *cr = cairo_create(sf);
		view_paint_refused(&L, cr, 702, 896, NULL);
		char p[128];
		snprintf(p, sizeof p, "out/renders/start.refused.%s.png", VOICES[vi]);
		cairo_surface_write_to_png(sf, p);
		cairo_destroy(cr);
		cairo_surface_destroy(sf);
		n += 2;
	}
	for (int vi = 0; vi < 3; vi++) {
		char p[128];
		snprintf(p, sizeof p, "out/renders/tmp.%s.png", VOICES[vi]);
		remove(p);
	}
	store_free(&s);
	printf("render: %d images in out/renders\n", n);
	return 0;
}
