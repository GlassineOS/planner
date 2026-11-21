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
	CHECK_INT(editor_act(&e, ED_SLOT, 0, NULL), ER_CHANGED);
	CHECK(strcmp(e.sel->slot, "notes"));
	view_layout(&v, &s, &L, cr);
	CHECK_INT(editor_act(&e, ED_SAVE, 0, dir), ER_SAVED);
	editor_close(&e);
	v.ed = NULL;
	CHECK_INT(tmpl_list(P_DAY, ls, 64), 5);

	/* ---- choosing the copy, then switching back: the entries never change ---- */
	store_choose(&s, v.per, v.per.start, cid);
	view_layout(&v, &s, &L, cr);
	CHECK(v.t == cp);
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

	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	tmpl_user_clear();
	store_free(&s);
	return t_done("editor");
}
