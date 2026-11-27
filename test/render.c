// SPDX-License-Identifier: GPL-2.0-or-later
/* Offscreen renders of every shipped template in each voice, upright and turned, and of
 * the overlays (spec 001 acceptance), into out/renders/. The window is the design's screen
 * less the system's status bar: 702 x 896 upright, 936 x 662 turned. Synthetic ink.
 *   render [SCALE [TEXT]]   at SCALE device pixels per design pixel (shell spec 012), laid
 *                           out and painted at it as the program does, the text
 *                           size TEXT percent: into out/renders-SCALE[-tTEXT] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/view.h"
#include "sample.h"

static const char *VOICES[] = { "white", "editorial", "stationery" };
static double SC;      /* 0: as the design, one pixel each */
static int TEXT;       /* 0: the text size not set */
static char DIR[64] = "out/renders";

static void shot(struct store *s, const char *voice, struct view *v, const char *name)
{
	struct look L;
	look_init(&L, voice);
	/* with the keyboard up the window is as tall as what it leaves (the page isn't laid out again) */
	int h = v->vis_h > 0 ? (int)v->vis_h : (int)v->H;
	if (TEXT)
		ui_metrics_voice(L.uv, 0, TEXT / 100.0);
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, SC ? ui_device_px(v->W, SC) : (int)v->W,
							 SC ? ui_device_px(h, SC) : h);
	cairo_t *cr = cairo_create(sf);
	if (SC) {
		view_layout_at(v, s, &L, SC); /* measured at the scale it is painted at, as the program does */
		cairo_scale(cr, SC, SC);
	} else
		view_layout(v, s, &L, cr);
	view_paint(v, s, &L, cr);
	char p[256];
	snprintf(p, sizeof p, "%s/%s.%s.png", DIR, name, voice);
	cairo_surface_write_to_png(sf, p);
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		SC = atof(argv[1]);
		TEXT = argc > 2 ? atoi(argv[2]) : 0;
		if (SC <= 0)
			return 2;
		if (TEXT)
			snprintf(DIR, sizeof DIR, "out/renders-%s-t%d", argv[1], TEXT);
		else
			snprintf(DIR, sizeof DIR, "out/renders-%s", argv[1]);
	}
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	setenv("GLASSINE_PLANNER_NOW", "860", 1);
	mkdir("out", 0755);
	mkdir(DIR, 0755);
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
		/* the template editor: a widget selected, the list to add one, the in-use question, a new one,
		 * a notes widget's slot, a drag's drop marker, a removal's notice, back to the shipped one */
		static const char *ED[] = { "templates.editor", "templates.editor.add", "templates.used", "templates.new",
					    "templates.editor.slot", "templates.editor.move", "templates.editor.remove",
					    "templates.editor.restore" };
		for (int k = 0; k < 8; k++) {
			struct editor e = { 0 };
			if (k == 3)
				editor_open_new(&e, P_DAY);
			else
				editor_open_edit(&e, tmpl_find("day-lists"));
			e.data = &s;
			if (k != 2 && k != 3)
				editor_act(&e, ED_EDIT_ALL, 0, NULL);
			struct view v = { .per = d, .today = today, .W = 702, .H = 896, .ed = &e };
			if (k != 2 && k != 3) {
				struct look L;
				look_init(&L, VOICES[vi]);
				cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
				cairo_t *mc = cairo_create(ms);
				view_layout(&v, &s, &L, mc);
				int want = k >= 4 && k <= 6 ? W_NOTES : W_AGENDA;
				const struct tnode *ag = NULL;
				for (int i = 0; i < v.g.n; i++) {
					if (v.g.p[i].n->wg == want)
						e.target = (struct tnode *)v.g.p[i].n;
					if (v.g.p[i].n->wg == W_AGENDA)
						ag = v.g.p[i].n;
				}
				editor_act(&e, ED_SELECT, 0, NULL);
				if (k == 1)
					editor_act(&e, ED_ADD, 0, NULL);
				if (k == 4) {
					editor_act(&e, ED_SETTINGS, 0, NULL);
					editor_act(&e, ED_SLOTS, 0, NULL);
				}
				if (k == 5)
					e.drop_target = ag, e.drop_side = 3;
				if (k == 6)
					editor_act(&e, ED_REMOVE, 0, NULL);
				if (k == 7) {
					/* an edited shipped template: its person's version, in memory only */
					struct tmpl *c = tmpl_clone(tmpl_find("day-lists"));
					c->root->c[0]->c[0]->w = 4;
					tmpl_user_put(NULL, c);
					editor_close(&e);
					editor_open_edit(&e, tmpl_find("day-lists"));
					e.data = &s;
					editor_act(&e, ED_EDIT_ALL, 0, NULL);
					editor_act(&e, ED_RESTORE, 0, NULL);
				}
				cairo_destroy(mc);
				cairo_surface_destroy(ms);
			}
			shot(&s, VOICES[vi], &v, ED[k]);
			if (k == 7)
				tmpl_user_remove(NULL, "day-lists");
			editor_close(&e);
			n++;
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
		/* slips (boards event.drag, event.delete, event.stack): the sample's dentist at
		 * 14:00 lifted, its corner pulled, crumpled, crossed out, its card; overlapping
		 * events shingled, one brought to the top, three fanned out */
		{
			struct entry *ev[16], *dn = NULL;
			int ne = store_events(&s, d.start, ev, 16);
			for (int i = 0; i < ne; i++)
				if (!ev[i]->allday && ev[i]->start == 14 * 60)
					dn = ev[i];
			if (dn) {
				struct view w = { .per = d, .today = today, .W = 702, .H = 896 };
				w.drag = dn->id, w.drag_s0 = dn->start, w.drag_e0 = dn->end;
				dn->start += 30, dn->end += 30;
				shot(&s, VOICES[vi], &w, "event.drag.moved");
				dn->start -= 30, dn->end += 30;
				w.drag_ear = true;
				shot(&s, VOICES[vi], &w, "event.drag.resize");
				dn->end -= 60;
				w.drag = 0, w.drag_ear = false;
				w.crumple = dn->id;
				shot(&s, VOICES[vi], &w, "event.delete.press");
				w.ov = OV_DELETE, w.title = dn->id;
				shot(&s, VOICES[vi], &w, "event.delete.press-confirm");
				w.crumple = 0;
				w.ov = OV_NONE;
				struct look LL;
				look_init(&LL, VOICES[vi]);
				view_layout_at(&w, &s, &LL, SC ? SC : 1);
				static struct pl_point q[40];
				const struct vslip *sl = view_slip(&w, dn->id);
				if (sl) {
					for (int i = 0; i < 40; i++) {
						double f = i / 39.0;
						q[i] = (struct pl_point){ (float)(sl->x - 16 + (sl->w + 26) * f),
									  (float)(sl->y + sl->h * (0.86 - 0.68 * f) + 3 * sin(f * 6)), 0.5f, 0 };
					}
					w.xout = q, w.nxout = 40;
				}
				w.ov = OV_DELETE, w.title = dn->id;
				shot(&s, VOICES[vi], &w, "event.delete.cross-confirm");
				struct entry *cb = store_add(&s, E_EVENT, d), *ks = store_add(&s, E_EVENT, d);
				cb->start = 14 * 60 + 30, cb->end = 15 * 60 + 30;
				sample_ink(&cb->f, "call the bank", 2, 19, 21);
				struct view u = { .per = d, .today = today, .W = 702, .H = 896 };
				store_delete(&s, ks->id);
				shot(&s, VOICES[vi], &u, "event.stack.shingled");
				view_raise(&u, dn->id);
				shot(&s, VOICES[vi], &u, "event.stack.swapped");
				u.nraise = 0;
				ks = store_add(&s, E_EVENT, d);
				ks->start = 15 * 60, ks->end = 16 * 60;
				sample_ink(&ks->f, "pick up the keys", 2, 19, 22);
				shot(&s, VOICES[vi], &u, "event.stack.stack");
				u.fan = dn->id;
				shot(&s, VOICES[vi], &u, "event.stack.fanned");
				store_delete(&s, cb->id), store_delete(&s, ks->id);
				n += 9;
			}
		}
		struct view v = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV_HIDDEN, .hidden_kind = 0 };
		shot(&s, VOICES[vi], &v, "hidden.list");
		/* an entry of the hidden list opened, and moved (no boards: Open and Move) */
		{
			struct hidden_group g[16];
			struct view w = v;
			shot(&s, VOICES[vi], &w, "tmp");
			int ng = view_hidden(&s, w.t, w.per, g, 16);
			struct entry *he[8];
			int nh = ng ? store_list(&s, g[0].kind, w.per, g[0].kind == E_NOTE ? g[0].slot : NULL, he, 8) : 0;
			if (nh) {
				w.ov = OV_TITLE, w.back = OV_HIDDEN, w.title = he[0]->id;
				shot(&s, VOICES[vi], &w, "hidden.open");
				w.ov = OV_MOVE, w.move = he[0]->id, w.move_at = he[0]->per.start;
				shot(&s, VOICES[vi], &w, "hidden.move");
				n += 2;
			}
		}
		/* the week's picker with where each template is used, and the year planned ahead
		 * (boards templates.picker, templates.year): a range, from a date on, a single week */
		{
			int yr, m, dd;
			day_to_ymd(today, &yr, &m, &dd);
			int nc0 = s.nc;
			store_choose(&s, period_of(P_WEEK, day_from_ymd(yr, 1, 4)), period_end(period_of(P_WEEK, day_from_ymd(yr, 3, 29))),
				     "week-columns");
			store_choose(&s, period_of(P_WEEK, day_from_ymd(yr, 4, 5)), -1, "week-spread");
			struct period w20 = period_of(P_WEEK, day_from_ymd(yr, 5, 17));
			store_choose(&s, w20, w20.start, "week-lists");
			struct period wk = period_of(P_WEEK, today);
			struct view w = { .per = wk, .today = today, .W = 702, .H = 896, .ov = OV_PICKER };
			shot(&s, VOICES[vi], &w, "templates.picker.week");
			w.year = true, w.scope = 1;
			shot(&s, VOICES[vi], &w, "templates.year");
			/* the next year (board templates.year.nav): the from-on choice runs on into it */
			w.year_off = 1;
			shot(&s, VOICES[vi], &w, "templates.year.next");
			/* turned (board templates.year.landscape): two columns of six months; the next year,
			 * then a range being picked for Spread */
			{
				struct view z = w;
				z.W = 936, z.H = 662;
				shot(&s, VOICES[vi], &z, "templates.year.landscape.next");
				z.year_off = 0;
				shot(&s, VOICES[vi], &z, "templates.year.landscape");
				z.rg_n = 1, z.rg0 = day_from_ymd(yr, 3, 22);
				snprintf(z.rg_tmpl, sizeof z.rg_tmpl, "week-spread");
				shot(&s, VOICES[vi], &z, "templates.year.landscape.picking");
				z.rg_n = 0, z.rg_tmpl[0] = 0, z.per = period_of(P_MONTH, today);
				shot(&s, VOICES[vi], &z, "templates.year.months.landscape");
				n += 4;
			}
			while (s.nc > nc0)
				store_unchoose(&s, s.nc - 1);
			/* the days of the year as a wall calendar (board templates.year.days): a range of
			 * days, a single day's own choice; then a range being picked for Diary (today's own
			 * choice set aside meanwhile, as on the board) */
			char dt[40] = "";
			for (int i = 0; i < s.nc; i++)
				if (s.c[i].from.kind == P_DAY && s.c[i].from.start == d.start) {
					snprintf(dt, sizeof dt, "%s", s.c[i].tmpl);
					store_unchoose(&s, i);
					nc0--;
					break;
				}
			store_choose(&s, period_of(P_DAY, day_from_ymd(yr, 4, 12)), day_from_ymd(yr, 4, 23), "day-diary");
			struct period d26 = period_of(P_DAY, day_from_ymd(yr, 3, 26));
			store_choose(&s, d26, d26.start, "day-agenda");
			struct view y = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV_PICKER, .year = true, .scope = 1 };
			shot(&s, VOICES[vi], &y, "templates.year.days");
			y.rg_n = 1, y.rg0 = day_from_ymd(yr, 5, 3);
			snprintf(y.rg_tmpl, sizeof y.rg_tmpl, "day-diary");
			shot(&s, VOICES[vi], &y, "templates.year.days.picking");
			/* turned (board templates.year.days.landscape): four months to a row */
			y.W = 936, y.H = 662;
			shot(&s, VOICES[vi], &y, "templates.year.days.landscape.picking");
			y.rg_n = 0, y.rg_tmpl[0] = 0;
			shot(&s, VOICES[vi], &y, "templates.year.days.landscape");
			while (s.nc > nc0)
				store_unchoose(&s, s.nc - 1);
			if (*dt)
				store_choose(&s, d, d.start, dt);
			n += 6;
		}
		/* Neaten's strength, the row's last word (board cog.strength) */
		{
			bool ne = s.neaten;
			float st = s.strength;
			static const struct { float v; const char *name; } ST[] = {
				{ 0.35f, "cog.strength.light" }, { 0.65f, "cog.strength.medium" }, { 1.0f, "cog.strength.strong" } };
			s.neaten = true;
			for (int k = 0; k < 3; k++) {
				s.strength = ST[k].v;
				struct view w = { .per = d, .today = today, .W = 702, .H = 896, .ov = OV_SETTINGS };
				shot(&s, VOICES[vi], &w, ST[k].name);
				n++;
			}
			s.neaten = ne, s.strength = st;
		}
		struct look L;
		look_init(&L, VOICES[vi]);
		cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, SC ? ui_device_px(702, SC) : 702,
								 SC ? ui_device_px(896, SC) : 896);
		cairo_t *cr = cairo_create(sf);
		if (SC)
			cairo_scale(cr, SC, SC);
		view_paint_refused(&L, cr, 702, 896, NULL);
		char p[128];
		snprintf(p, sizeof p, "%s/start.refused.%s.png", DIR, VOICES[vi]);
		cairo_surface_write_to_png(sf, p);
		cairo_destroy(cr);
		cairo_surface_destroy(sf);
		n += 2;
	}
	for (int vi = 0; vi < 3; vi++) {
		char p[128];
		snprintf(p, sizeof p, "%s/tmp.%s.png", DIR, VOICES[vi]);
		remove(p);
	}
	store_free(&s);
	printf("render: %d images in %s\n", n, DIR);
	return 0;
}
