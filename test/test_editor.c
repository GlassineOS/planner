// SPDX-License-Identifier: GPL-2.0-or-later
/* The template editor and the person's template files (spec 001 FR-026, FR-031, FR-032,
 * FR-044, FR-045, FR-049, SC-002): the file format round trip, every editor action, and that
 * nothing the editor does changes an entry, in memory or on disk. */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/view.h"
#include "sample.h"
#include "t.h"

static char *slurp(const char *p)
{
	FILE *f = fopen(p, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	char *b = malloc(n + 1);
	b[fread(b, 1, n, f)] = 0;
	fclose(f);
	return b;
}

static bool same_tree(const struct tnode *a, const struct tnode *b)
{
	if (a->k != b->k || a->nc != b->nc || (a->w > 0 ? a->w : 1) != (b->w > 0 ? b->w : 1) || a->grow != b->grow)
		return false;
	if (a->k == N_WIDGET)
		return a->wg == b->wg && a->period == b->period && a->large == b->large &&
		       !strcmp(a->slot ? a->slot : "", b->slot ? b->slot : "") && !strcmp(a->title ? a->title : "", b->title ? b->title : "") &&
		       ((a->wg != W_AGENDA && a->wg != W_CAGENDA) || (a->from == b->from && a->to == b->to && a->day == b->day));
	enum nkind la = a->land == N_WIDGET ? a->k : a->land, lb = b->land == N_WIDGET ? b->k : b->land;
	if (la != lb)
		return false;
	for (int i = 0; i < a->nc; i++)
		if (!same_tree(a->c[i], b->c[i]))
			return false;
	return true;
}

static const struct vhit *find(const struct view *v, enum act a, int arg)
{
	for (int i = 0; i < v->nh; i++)
		if (v->hit[i].a == a && (arg < 0 || v->hit[i].arg == arg))
			return &v->hit[i];
	return NULL;
}

/* the editor's select action on the widget of kind k */
static void select_kind(struct editor *e, struct view *v, enum wkind k, int nth)
{
	e->target = NULL;
	for (int i = 0; i < v->g.n; i++)
		if (v->g.p[i].n->wg == k && nth-- == 0) {
			e->target = (struct tnode *)v->g.p[i].n;
			break;
		}
	editor_act(e, ED_SELECT, 0, NULL);
}

/* dark at device pixel x, y of an RGB24 surface */
static bool dark_at(cairo_surface_t *sf, double x, double y)
{
	int w = cairo_image_surface_get_width(sf), h = cairo_image_surface_get_height(sf);
	int ix = (int)x, iy = (int)y;
	if (ix < 0 || iy < 0 || ix >= w || iy >= h)
		return false;
	cairo_surface_flush(sf);
	const uint32_t px = *(const uint32_t *)(cairo_image_surface_get_data(sf) + (size_t)iy * cairo_image_surface_get_stride(sf) + ix * 4);
	int r = px >> 16 & 255, g = px >> 8 & 255, b = px & 255;
	return r * 3 + g * 6 + b < 128 * 10;
}

/* The editor's dialogs on a scaled output (the device's is 2): laid out with view_layout_at
 * at the scale they are painted at, each button's target lies on the button painted.
 * Measured unscaled, the dialog's text wrapped otherwise than painted and the buttons'
 * targets lay a line above them: a tap on a painted button reached only the modal cover,
 * and the editor took no tap at all. */
static void dialogs_scaled(struct store *s, struct view *v0)
{
	static const char *const voices[] = { "white", "editorial", "stationery" };
	static const enum ed_overlay dlg[] = { EO_USED, EO_DISCARD, EO_RESTORE };
	static const enum ed_op primary[] = { ED_EDIT_ALL, ED_DISCARD, ED_RESTORE_YES };
	static const double sizes[][2] = { { 702, 884 }, { 702, 896 }, { 936, 662 }, { 600, 800 } };
	for (int vi = 0; vi < 3; vi++)
		for (int si = 0; si < 4; si++)
			for (int scale = 1; scale <= 3; scale++)
				for (int di = 0; di < 3; di++) {
					struct look L;
					look_init(&L, voices[vi]);
					struct view v = { .per = period_of(P_MONTH, v0->today), .today = v0->today, .W = sizes[si][0],
						.H = sizes[si][1], .hidden_kind = -1 };
					view_layout_at(&v, s, &L, scale);
					struct editor e = { 0 };
					e.data = s;
					editor_open_edit(&e, v.t);
					e.ov = dlg[di];
					v.ed = &e;
					view_layout_at(&v, s, &L, scale);
					cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)v.W * scale, (int)v.H * scale);
					cairo_t *cr = cairo_create(sf);
					cairo_scale(cr, scale, scale);
					view_paint(&v, s, &L, cr);
					/* the primary button is filled with ink: its target's ends are ink */
					const struct vhit *h = find(&v, A_ED, primary[di]);
					CHECK(h != NULL);
					if (h) {
						bool ok = dark_at(sf, (h->x + 6) * scale, (h->y + 6) * scale) &&
							  dark_at(sf, (h->x + h->w - 6) * scale, (h->y + h->h - 6) * scale) &&
							  dark_at(sf, (h->x + 6) * scale, (h->y + h->h - 6) * scale);
						if (!ok)
							fprintf(stderr, "dialog %d, %s, %gx%g at %d: target %.0f,%.0f %.0fx%.0f off its button\n", di,
								voices[vi], v.W, v.H, scale, h->x, h->y, h->w, h->h);
						CHECK(ok);
						/* a tap at its painted centre is the button's, not the cover's */
						const struct vhit *t = view_hit(&v, h->x + h->w / 2, h->y + h->h / 2);
						CHECK(t && t->a == A_ED && t->arg == (int)primary[di]);
					}
					cairo_destroy(cr);
					cairo_surface_destroy(sf);
					v.ed = NULL;
					editor_close(&e);
				}
}

int main(void)
{
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	char dir[] = "out/test-editor-XXXXXX";
	mkdir("out", 0755);
	CHECK(mkdtemp(dir) != NULL);

	/* ---- the file format: every shipped template round-trips ---- */
	for (int i = 0; i < pl_ntemplates; i++) {
		char *a = tmpl_to_text(pl_templates[i]);
		struct tmpl *t = tmpl_from_text(a);
		CHECK(t != NULL);
		if (!t)
			continue;
		CHECK_STR(t->id, pl_templates[i]->id);
		CHECK_STR(t->name, pl_templates[i]->name);
		CHECK_INT(t->kind, pl_templates[i]->kind);
		CHECK(same_tree(t->root, pl_templates[i]->root));
		char *b = tmpl_to_text(t);
		CHECK_STR(b, a);
		free(a);
		free(b);
		tmpl_free(t);
	}
	/* quotes, and files that give no template */
	struct tmpl *q = tmpl_from_text("id own-9\nkind week\nname \"Say \\\"hi\\\" \\\\ there\"\nrow\n todo period=week title=\"A b\"\n");
	CHECK(q && !strcmp(q->name, "Say \"hi\" \\ there") && q->root->nc == 1 && !strcmp(q->root->c[0]->title, "A b"));
	tmpl_free(q);
	CHECK(tmpl_from_text("kind day\nrow\n agenda\n") == NULL);           /* no id */
	CHECK(tmpl_from_text("id x y\nkind day\nrow\n agenda\n") == NULL);   /* an id with a space */
	CHECK(tmpl_from_text("id a\nkind year\nrow\n agenda\n") == NULL);    /* no such kind */
	CHECK(tmpl_from_text("id a\nkind day\nrow\n agenda\ncol\n todo\n") == NULL); /* two roots */
	q = tmpl_from_text("id a\nkind day\nagenda from=30 to=2\n");          /* a widget at the root: wrapped */
	CHECK(q && q->root->k == N_COL && q->root->nc == 1 && q->root->c[0]->from == 7 && q->root->c[0]->to == 21);
	tmpl_free(q);
	char big[4096] = "id a\nkind day\nrow\n";
	for (int i = 0; i < 12; i++)
		strcat(big, " todo\n");
	q = tmpl_from_text(big); /* a row holds at most TN_KIDS */
	CHECK(q && q->root->nc == TN_KIDS);
	tmpl_free(q);

	/* ---- the entries, saved; the editor must leave them byte for byte ---- */
	struct store s;
	store_init(&s, dir);
	sample_fill(&s);
	CHECK(store_save(&s));
	CHECK(store_save_choices(&s));
	char path[512], cpath[512];
	snprintf(path, sizeof path, "%s/planner.ics", dir);
	snprintf(cpath, sizeof cpath, "%s/templates.conf", dir);
	char *ics0 = store_ics(&s), *disk0 = slurp(path), *conf0 = slurp(cpath);
	tmpl_user_load(dir);

	struct look L;
	look_init(&L, "white");
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 702, 896);
	cairo_t *cr = cairo_create(sf);
	int today = day_today();
	struct view v = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 896, .hidden_kind = -1 };
	view_layout(&v, &s, &L, cr);
	const struct tmpl *orig = v.t;
	CHECK_STR(orig->id, "day-lists");
	/* the menu and the picker open the editor */
	v.ov = OV_MENU;
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_EDIT, 0) != NULL);
	v.ov = OV_PICKER;
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_EDIT, 0) && find(&v, A_EDIT, 1) && find(&v, A_EDIT, 2));
	v.ov = OV_NONE;

	/* ---- Edit: asks first (FR-032), then shows the period with no field and no control of the page ---- */
	struct editor e = { 0 };
	editor_open_edit(&e, orig);
	e.data = &s;
	CHECK_INT(e.ov, EO_USED);
	v.ed = &e;
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_ED, ED_EDIT_ALL) && find(&v, A_ED, ED_COPY_FIRST) && find(&v, A_ED, ED_KEEP));
	CHECK_INT(editor_act(&e, ED_EDIT_ALL, 0, dir), ER_CHANGED);
	view_layout(&v, &s, &L, cr);
	for (int i = 0; i < v.nf; i++)
		CHECK(v.f[i].k != F_ENTRY && v.f[i].k != F_NEW && v.f[i].k != F_FREE);
	for (int i = 0; i < v.nh; i++)
		CHECK(v.hit[i].a == A_ED || v.hit[i].a == A_NONE);
	CHECK(find(&v, A_ED, ED_SAVE) && find(&v, A_ED, ED_CANCEL) && find(&v, A_ED, ED_ADD));
	CHECK(v.g.n == 5 && !e.short_);
	/* the head counts the periods using it: the default, so every day with entries */
	{
		bool def = false;
		int u = editor_uses(&s, e.t, &def);
		CHECK(def && u >= 1 && e.uses == u && e.is_default);
		CHECK(!find(&v, A_ED, ED_RESTORE)); /* shipped and never edited: nothing to go back to */
	}
	/* select the agenda: its tag has a target for every action */
	select_kind(&e, &v, W_AGENDA, 0);
	view_layout(&v, &s, &L, cr);
	CHECK(e.sel && e.sel->wg == W_AGENDA);
	CHECK(find(&v, A_ED, ED_BEFORE) && find(&v, A_ED, ED_AFTER) && find(&v, A_ED, ED_LESS) && find(&v, A_ED, ED_MORE) &&
	      find(&v, A_ED, ED_SETTINGS));
	const struct vhit *h = find(&v, A_ED, ED_MORE);
	CHECK(view_hit(&v, h->x + h->w / 2, h->y + h->h / 2) == h); /* the tag lies over the widget's own target */
	/* weight, hours, settings sheet */
	CHECK_INT(editor_act(&e, ED_MORE, 0, NULL), ER_CHANGED);
	CHECK(e.sel->w == 4 && e.dirty);
	CHECK_INT(editor_act(&e, ED_FROM, -1, NULL), ER_CHANGED);
	CHECK_INT(e.sel->from, 6);
	CHECK_INT(editor_act(&e, ED_TO, -20, NULL), ER_NONE); /* at least two hours */
	editor_act(&e, ED_SETTINGS, 0, NULL);
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_ED, ED_FROM) && find(&v, A_ED, ED_GROW) && find(&v, A_ED, ED_LAND) && find(&v, A_ED, ED_KEEP));
	editor_act(&e, ED_KEEP, 0, NULL);
	/* move the notes below the agenda by a drop (Scenario 8), and the to-do of the week before its neighbour */
	view_layout(&v, &s, &L, cr);
	select_kind(&e, &v, W_NOTES, 0);
	struct tnode *notes = e.sel;
	view_layout(&v, &s, &L, cr);
	for (int i = 0; i < v.g.n; i++)
		if (v.g.p[i].n->wg == W_AGENDA)
			e.target = (struct tnode *)v.g.p[i].n;
	/* a finger dragging the notes: over the agenda's lower part, the marker says below it */
	{
		const struct placed *ag = NULL, *no = NULL;
		for (int i = 0; i < v.g.n; i++) {
			if (v.g.p[i].n->wg == W_AGENDA)
				ag = &v.g.p[i];
			if (v.g.p[i].n == notes)
				no = &v.g.p[i];
		}
		const struct tnode *t = NULL;
		CHECK_INT(editor_drop_at(&v.g, notes, ag->x + ag->w / 2, ag->y + ag->h - 5, &t), 3);
		CHECK(t == ag->n);
		CHECK_INT(editor_drop_at(&v.g, notes, ag->x + 3, ag->y + ag->h / 2, &t), 0);
		CHECK_INT(editor_drop_at(&v.g, notes, ag->x + ag->w - 3, ag->y + ag->h / 2, &t), 1);
		CHECK_INT(editor_drop_at(&v.g, notes, ag->x + ag->w / 2, ag->y + 3, &t), 2);
		CHECK_INT(editor_drop_at(&v.g, notes, no->x + no->w / 2, no->y + no->h / 2, &t), -1); /* over itself: nowhere */
		CHECK(t == NULL);
		CHECK_INT(editor_drop_at(&v.g, notes, -50, -50, &t), -1);
		e.drop_target = ag->n;
		e.drop_side = 3;
		view_layout(&v, &s, &L, cr);
		CHECK(!find(&v, A_ED, ED_MORE)); /* the tag gives way to the marker */
		view_paint(&v, &s, &L, cr);
		e.drop_target = NULL;
		e.drop_side = -1;
		view_layout(&v, &s, &L, cr);
		/* an edge dragged: a weight step per 60 px, along the longer axis */
		CHECK_INT(editor_edge_steps(130, 10), 2);
		CHECK_INT(editor_edge_steps(5, -70), -1);
		CHECK_INT(editor_edge_steps(20, 20), 0);
	}
	CHECK_INT(editor_act(&e, ED_DROP, 3, NULL), ER_CHANGED);
	view_layout(&v, &s, &L, cr);
	const struct placed *pa = NULL, *pn = NULL;
	for (int i = 0; i < v.g.n; i++) {
		if (v.g.p[i].n->wg == W_AGENDA)
			pa = &v.g.p[i];
		if (v.g.p[i].n == notes)
			pn = &v.g.p[i];
	}
	CHECK(pa && pn && pn->y > pa->y + pa->h - 1 && pn->x < pa->x + pa->w);
	CHECK_INT(v.g.n, 5);
	/* add a habit tracker after the selected notes; remove it again */
	CHECK_INT(editor_act(&e, ED_ADD, 0, NULL), ER_CHANGED);
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_ED, ED_ADD_W) != NULL);
	CHECK_INT(editor_act(&e, ED_ADD_W, W_HABITS, NULL), ER_CHANGED);
	CHECK(e.sel && e.sel->wg == W_HABITS && e.ov == EO_NONE);
	CHECK_INT(editor_widgets(e.t->root), 6);
	CHECK_INT(editor_act(&e, ED_REMOVE, 0, NULL), ER_CHANGED);
	CHECK_INT(editor_widgets(e.t->root), 5);
	CHECK(e.sel == NULL);
	/* the template in use is not touched before Save */
	CHECK(tmpl_find("day-lists") == orig);
	/* Cancel after a change asks; Keep editing stays */
	CHECK_INT(editor_act(&e, ED_CANCEL, 0, NULL), ER_CHANGED);
	CHECK_INT(e.ov, EO_DISCARD);
	CHECK_INT(editor_act(&e, ED_KEEP, 0, NULL), ER_CHANGED);
	/* FR-044: too many widgets for the page, in either orientation: Save does nothing */
	struct tnode *keep_sel = NULL;
	for (int i = 0; i < 7; i++) {
		view_layout(&v, &s, &L, cr);
		select_kind(&e, &v, W_AGENDA, 0);
		editor_act(&e, ED_ADD_W, W_WEEKOV, NULL);
		keep_sel = e.sel;
	}
	view_layout(&v, &s, &L, cr);
	CHECK(e.short_ && e.deficit > 0);
	CHECK_INT(editor_act(&e, ED_SAVE, 0, dir), ER_NONE);
	h = find(&v, A_ED, ED_SAVE);
	CHECK(h == NULL); /* Save is off */
	(void)keep_sel;
	/* turned, the status bar stays on top: the window turned is the one the planner gets */
	{
		double tw, th;
		editor_turned_size(702, 896, &tw, &th);
		CHECK(tw == 936 && th == 662);
		editor_turned_size(936, 662, &tw, &th);
		CHECK(tw == 702 && th == 896);
	}
	/* a wide row fits upright but not turned: a short turned shows too */
	{
		struct editor w = { 0 };
		editor_open_new(&w, P_DAY);
		struct view vw = v;
		vw.ed = &w;
		for (int i = 0; i < 3; i++)
			editor_act(&w, ED_ADD_W, W_AGENDA, NULL);
		w.t->root->k = N_ROW;
		w.t->root->land = N_COL;
		view_layout(&vw, &s, &L, cr);
		CHECK(!w.short_); /* 3 x 200 across 678 upright */
		for (int i = 0; i < 2; i++)
			editor_act(&w, ED_ADD_W, W_AGENDA, NULL);
		view_layout(&vw, &s, &L, cr);
		CHECK(w.short_);
		editor_close(&w);
	}
	/* Discard: nothing saved, nothing changed */
	CHECK_INT(editor_act(&e, ED_CANCEL, 0, NULL), ER_CHANGED);
	CHECK_INT(editor_act(&e, ED_DISCARD, 0, NULL), ER_CLOSED);
	editor_close(&e);
	v.ed = NULL;
	CHECK(tmpl_find("day-lists") == orig);
	char tp[600];
	snprintf(tp, sizeof tp, "%s/templates/day-lists.tmpl", dir);
	CHECK(access(tp, F_OK) != 0);

	/* ---- an edit saved: the shipped template is replaced wherever it is used ---- */
	editor_open_edit(&e, orig);
	e.data = &s;
	editor_act(&e, ED_EDIT_ALL, 0, NULL);
	v.ed = &e;
	view_layout(&v, &s, &L, cr);
	select_kind(&e, &v, W_AGENDA, 0);
	editor_act(&e, ED_MORE, 0, NULL);
	view_layout(&v, &s, &L, cr);
	CHECK_INT(editor_act(&e, ED_SAVE, 0, dir), ER_SAVED);
	editor_close(&e);
	v.ed = NULL;
	CHECK(access(tp, F_OK) == 0);
	const struct tmpl *ed1 = tmpl_find("day-lists");
	CHECK(ed1 && ed1 != orig && tmpl_default(P_DAY) == ed1);
	view_layout(&v, &s, &L, cr);
	CHECK(v.t == ed1);
	/* read back from the file */
	tmpl_user_load(dir);
	CHECK(tmpl_find("day-lists") && tmpl_find("day-lists")->root->c[0]->c[0]->w == 4);

	/* ---- back to the shipped version: asks, then the person's file goes ---- */
	editor_open_edit(&e, tmpl_find("day-lists"));
	e.data = &s;
	editor_act(&e, ED_EDIT_ALL, 0, NULL);
	v.ed = &e;
	view_layout(&v, &s, &L, cr);
	CHECK(editor_edited_shipped(&e) && find(&v, A_ED, ED_RESTORE));
	CHECK_INT(editor_act(&e, ED_RESTORE_YES, 0, dir), ER_NONE); /* not without asking */
	CHECK_INT(editor_act(&e, ED_RESTORE, 0, NULL), ER_CHANGED);
	CHECK_INT(e.ov, EO_RESTORE);
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_ED, ED_RESTORE_YES) && find(&v, A_ED, ED_KEEP));
	CHECK_INT(editor_act(&e, ED_KEEP, 0, NULL), ER_CHANGED); /* Keep mine */
	CHECK_INT(e.ov, EO_NONE);
	CHECK(access(tp, F_OK) == 0);
	editor_act(&e, ED_RESTORE, 0, NULL);
	CHECK_INT(editor_act(&e, ED_RESTORE_YES, 0, dir), ER_SAVED);
	editor_close(&e);
	v.ed = NULL;
	CHECK(access(tp, F_OK) != 0);
	CHECK(tmpl_find("day-lists") == orig && tmpl_default(P_DAY) == orig);
	view_layout(&v, &s, &L, cr);
	CHECK(v.t == orig);
	tmpl_user_load(dir);
	CHECK(tmpl_find("day-lists") == orig);
	CHECK(!tmpl_user_remove(dir, "day-lists")); /* nothing left to remove */

	/* ---- Copy first, from the in-use dialog; renamed by a reading ---- */
	editor_open_edit(&e, tmpl_find("day-lists"));
	e.data = &s;
	CHECK_INT(editor_act(&e, ED_COPY_FIRST, 0, dir), ER_CHANGED);
	CHECK(e.fresh && !strcmp(e.t->name, "Agenda and lists, copy") && strcmp(e.t->id, "day-lists"));
	CHECK_INT(editor_act(&e, ED_NAME, 0, NULL), ER_CHANGED);
	v.ed = &e;
	view_layout(&v, &s, &L, cr);
	int nn = 0;
	for (int i = 0; i < v.nf; i++)
		nn += v.f[i].k == F_NAME;
	CHECK_INT(nn, 1);
	editor_name_value(&e, "  Workdays ");
	CHECK_STR(e.t->name, "Workdays");
	editor_name_value(&e, ""); /* no reading yet: the name stays */
	CHECK_STR(e.t->name, "Workdays");
	char cid[TMPL_ID];
	snprintf(cid, sizeof cid, "%s", e.t->id);
	CHECK_INT(editor_act(&e, ED_SAVE, 0, dir), ER_SAVED);
	editor_close(&e);
	v.ed = NULL;
	const struct tmpl *cp = tmpl_find(cid);
	CHECK(cp && !strcmp(cp->name, "Workdays"));
	/* a copy is used by no period until chosen: the day still shows day-lists */
	view_layout(&v, &s, &L, cr);
	CHECK_STR(v.t->id, "day-lists");
	const struct tmpl *ls[64];
	int nl = tmpl_list(P_DAY, ls, 64);
	CHECK_INT(nl, 4);
	CHECK(ls[nl - 1] == cp);
	/* Save returns to the picker with the copy ringed: it is listed and can be chosen, upright and turned */
	for (int land = 0; land < 2; land++) {
		struct view pv = { .per = v.per, .today = today, .W = land ? 936 : 702, .H = land ? 662 : 896, .ov = OV_PICKER, .hidden_kind = -1 };
		snprintf(pv.ring, sizeof pv.ring, "%s", cid);
		view_layout(&pv, &s, &L, cr);
		CHECK(find(&pv, A_PICK, nl - 1) != NULL);
		CHECK(find(&pv, A_EDIT, 2) != NULL);
		/* with many more, the ringed last one still shows */
		for (int k = 0; k < 6; k++) {
			struct editor x = { 0 };
			editor_open_copy(&x, orig);
			editor_act(&x, ED_SAVE, 0, NULL);
			editor_close(&x);
		}
		int nm = tmpl_list(P_DAY, ls, 64);
		snprintf(pv.ring, sizeof pv.ring, "%s", ls[nm - 1]->id);
		view_layout(&pv, &s, &L, cr);
		const struct vhit *ph = find(&pv, A_PICK, nm - 1);
		CHECK(ph && ph->y + ph->h <= pv.H);
		snprintf(pv.ring, sizeof pv.ring, "%s", "");
		tmpl_user_load(dir); /* the in-memory copies go again */
	}
	nl = tmpl_list(P_DAY, ls, 64);
	CHECK_INT(nl, 4);
	cp = tmpl_find(cid);

	/* ---- New: empty, a name from the start; one widget; saved ---- */
	editor_open_new(&e, P_DAY);
	e.data = &s;
	CHECK(e.fresh && editor_widgets(e.t->root) == 0 && !strcmp(e.t->name, "New day template"));
	CHECK(strcmp(e.t->id, cid)); /* an id of its own */
	v.ed = &e;
	view_layout(&v, &s, &L, cr);
	CHECK_INT(v.g.n, 0);
	CHECK(!e.short_);
	CHECK_INT(editor_act(&e, ED_CANCEL, 0, NULL), ER_CLOSED); /* nothing changed: no question */
	editor_open_new(&e, P_DAY);
	editor_act(&e, ED_ADD_W, W_NOTES, NULL);
	CHECK(e.sel && e.sel->slot && !strcmp(e.sel->slot, "notes") && e.sel->period == P_DAY);
	e.data = &s;
	struct tnode *nw = e.sel;
	CHECK_INT(editor_act(&e, ED_SETTINGS, 0, NULL), ER_CHANGED);
	view_layout(&v, &s, &L, cr);
	CHECK(find(&v, A_ED, ED_SLOTS) != NULL);
	CHECK_INT(editor_act(&e, ED_SLOTS, 0, NULL), ER_CHANGED);
	CHECK_INT(e.ov, EO_SLOT);
	view_layout(&v, &s, &L, cr);
	{
		const char *sl[32];
		int ns = editor_slots(&s, sl, 32);
		CHECK(ns >= 1 && find(&v, A_ED, ED_SLOT_PICK) && find(&v, A_ED, ED_KEEP));
		int nslot = 0;
		for (int i = 0; i < v.nf; i++)
			nslot += v.f[i].k == F_SLOT;
		CHECK_INT(nslot, 1); /* the new slot's field */
		for (int i = 0; i < v.nf; i++)
			CHECK(v.f[i].k == F_SLOT || v.f[i].k == F_NAME);
		CHECK_INT(editor_slot_days(&s, "notes", P_DAY), 1);
		CHECK_INT(editor_slot_days(&s, "nothing", P_DAY), 0);
		CHECK_INT(editor_act(&e, ED_SLOT_PICK, ns, NULL), ER_NONE);
	}
	/* a new slot written with the pen: ink, then its reading names the slot (and the title that named it) */
	struct pl_point pts[3] = { { 10, 10, 1, 0 }, { 20, 12, 1, 8 }, { 30, 11, 1, 16 } };
	editor_ink(&e, 1, 7, pts, 3);
	editor_ink(&e, 1, 8, pts, 3);
	CHECK_INT(e.slot.ns, 2);
	editor_ink_value(&e, 1, "", NULL, 0, NULL, 0); /* nothing read yet */
	CHECK_STR(nw->slot, "notes");
	uint32_t gone = 8;
	editor_ink_value(&e, 1, "  Gratitude! ", &gone, 1, NULL, 0);
	CHECK_INT(e.slot.ns, 1);
	CHECK_STR(nw->slot, "gratitude");
	CHECK_STR(nw->title, "Gratitude");
	CHECK(e.dirty);
	view_layout(&v, &s, &L, cr);
	CHECK_INT(editor_act(&e, ED_KEEP, 0, NULL), ER_CHANGED); /* back to the widget's settings */
	CHECK_INT(e.ov, EO_WSET);
	/* or one in use, picked */
	editor_act(&e, ED_SLOTS, 0, NULL);
	CHECK_INT(e.slot.ns, 0); /* the field starts empty each time */
	{
		const char *sl[32];
		int ns = editor_slots(&s, sl, 32), at = -1;
		for (int i = 0; i < ns; i++)
			if (!strcmp(sl[i], "notes"))
				at = i;
		CHECK_INT(editor_act(&e, ED_SLOT_PICK, at, NULL), ER_CHANGED);
	}
	CHECK_STR(nw->slot, "notes");
	CHECK_STR(nw->title, "Notes");
	CHECK_INT(e.ov, EO_WSET);
	editor_act(&e, ED_KEEP, 0, NULL);
	/* the name, written with the pen the same way */
	editor_act(&e, ED_NAME, 0, NULL);
	editor_ink(&e, 0, 9, pts, 3);
	editor_ink_value(&e, 0, "Journal", NULL, 0, NULL, 0);
	CHECK_STR(e.t->name, "Journal");
	CHECK_INT(e.name.ns, 1);
	/* a slot for anything but notes: none */
	editor_act(&e, ED_ADD_W, W_TODO, NULL);
	CHECK_INT(editor_act(&e, ED_SLOTS, 0, NULL), ER_NONE);
	editor_act(&e, ED_REMOVE, 0, NULL);
	CHECK(!e.notice[0]); /* a to-do list's entries never needed it */
	/* the only notes widget for a slot with entries: removed, and it says the notes stay */
	e.sel = nw;
	CHECK_INT(editor_act(&e, ED_REMOVE, 0, NULL), ER_CHANGED);
	CHECK_STR(e.notice, "Notes on 1 day stay stored, unchanged.");
	view_layout(&v, &s, &L, cr);
	view_paint(&v, &s, &L, cr);
	editor_act(&e, ED_ADD, 0, NULL); /* the next action: the notice goes */
	CHECK(!e.notice[0]);
	editor_act(&e, ED_ADD_W, W_NOTES, NULL);
	/* a second widget for the slot: removing one says nothing, also when one names the
	 * template's own period and the other leaves it to the template */
	struct tnode *n1 = e.sel;
	editor_act(&e, ED_ADD_W, W_NOTES, NULL);
	n1->period = -1;
	e.sel->period = P_DAY;
	editor_act(&e, ED_REMOVE, 0, NULL);
	CHECK(!e.notice[0]);
	/* a slot of several words, with an umlaut: read, saved in the template file, read back */
	editor_act(&e, ED_SELECT, 0, NULL);
	e.sel = n1;
	editor_act(&e, ED_SLOTS, 0, NULL);
	editor_ink(&e, 1, 11, pts, 3);
	editor_ink_value(&e, 1, "Für später, ok", NULL, 0, NULL, 0);
	CHECK_STR(n1->slot, "für später ok");
	CHECK_STR(n1->title, "Für später ok");
	{
		char *txt = tmpl_to_text(e.t);
		struct tmpl *back = tmpl_from_text(txt);
		CHECK(back && same_tree(back->root, e.t->root));
		tmpl_free(back);
		free(txt);
		/* a reading too long for a slot is cut on a whole character */
		editor_ink_value(&e, 1, "äääääääääääääääääääääääää", NULL, 0, NULL, 0);
		CHECK(strlen(n1->slot) <= 31 && strlen(n1->slot) % 2 == 0);
	}
	editor_act(&e, ED_KEEP, 0, NULL);
	editor_act(&e, ED_KEEP, 0, NULL);
	/* the editor over the cog's text mode for the whole view: still no entry field */
	v.tm = 2;
	view_layout(&v, &s, &L, cr);
	for (int i = 0; i < v.nf; i++)
		CHECK(v.f[i].k != F_ENTRY && v.f[i].k != F_NEW && v.f[i].k != F_FREE);
	for (int i = 0; i < v.nh; i++)
		CHECK(v.hit[i].a == A_ED || v.hit[i].a == A_NONE);
	v.tm = 0;
	CHECK(!editor_edited_shipped(&e)); /* a new one has no shipped version */
	view_layout(&v, &s, &L, cr);
	CHECK_INT(editor_act(&e, ED_SAVE, 0, dir), ER_SAVED);
	editor_close(&e);
	v.ed = NULL;
	CHECK_INT(tmpl_list(P_DAY, ls, 64), 5);

	/* ---- choosing the copy, then switching back: the entries never change ---- */
	store_choose(&s, v.per, v.per.start, cid);
	view_layout(&v, &s, &L, cr);
	CHECK(v.t == cp);
	{
		bool def = true;
		int before = editor_uses(&s, orig, NULL);
		CHECK_INT(editor_uses(&s, cp, &def), 1); /* this day, chosen */
		CHECK(!def);
		store_choose(&s, period_of(P_DAY, v.per.start + 40), v.per.start + 40, cid); /* a day without entries, chosen */
		CHECK_INT(editor_uses(&s, cp, NULL), 2);
		CHECK_INT(editor_uses(&s, orig, NULL), before);
	}
	store_choose(&s, v.per, v.per.start, "day-lists");
	/* a broken file falls back to the kind's default */
	FILE *bf = fopen(tp, "w");
	fputs("id day-lists\nkind day\nrow\n agenda\ncol\n", bf);
	fclose(bf);
	tmpl_user_load(dir);
	view_layout(&v, &s, &L, cr);
	CHECK(v.t == orig);

	char *ics1 = store_ics(&s), *disk1 = slurp(path), *conf1 = slurp(cpath);
	CHECK_STR(ics1, ics0);
	CHECK_STR(disk1, disk0); /* the editor never wrote the entries' file */
	CHECK_STR(conf1, conf0); /* nor the choices */
	free(ics0), free(ics1), free(disk0), free(disk1), free(conf0), free(conf1);

	dialogs_scaled(&s, &v);

	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	tmpl_user_clear();
	store_free(&s);
	return t_done("editor");
}
