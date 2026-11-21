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
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)v->W, (int)v->H);
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
		/* the template editor: a widget selected, the list to add one, the in-use question, a new one */
		static const char *ED[] = { "templates.editor", "templates.editor.add", "templates.used", "templates.new" };
		for (int k = 0; k < 4; k++) {
			struct editor e = { 0 };
			if (k == 3)
				editor_open_new(&e, P_DAY);
			else
				editor_open_edit(&e, tmpl_find("day-lists"));
			e.data = &s;
			if (k < 2)
				editor_act(&e, ED_EDIT_ALL, 0, NULL);
			struct view v = { .per = d, .today = today, .W = 702, .H = 896, .ed = &e };
			if (k < 2) {
				struct look L;
				look_init(&L, VOICES[vi]);
				cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
				cairo_t *mc = cairo_create(ms);
				view_layout(&v, &s, &L, mc);
				for (int i = 0; i < v.g.n; i++)
					if (v.g.p[i].n->wg == W_AGENDA)
						e.target = (struct tnode *)v.g.p[i].n;
				editor_act(&e, ED_SELECT, 0, NULL);
				if (k == 1)
					editor_act(&e, ED_ADD, 0, NULL);
				cairo_destroy(mc);
				cairo_surface_destroy(ms);
			}
			shot(&s, VOICES[vi], &v, ED[k]);
			editor_close(&e);
			n++;
		}
		store_choose(&s, d, d.start, "day-agenda");
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
	store_free(&s);
	printf("render: %d images in out/renders\n", n);
	return 0;
}
