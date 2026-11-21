// SPDX-License-Identifier: GPL-2.0-or-later
/* The template editor (editor.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>
#include <ctype.h>

#include "editor.h"

static void reset(struct editor *e)
{
	const struct store *data = e->data;
	tmpl_free(e->t);
	field_free(&e->name);
	field_free(&e->slot);
	memset(e, 0, sizeof *e);
	e->data = data;
	e->uses = -1;
	e->drop_side = -1;
}

void editor_close(struct editor *e)
{
	reset(e);
	e->data = NULL;
}

void editor_open_edit(struct editor *e, const struct tmpl *t)
{
	reset(e);
	e->t = tmpl_clone(t);
	/* FR-032: an edit applies wherever the template is used; ask first (board templates.used) */
	e->ov = EO_USED;
}

static void make_copy(struct editor *e)
{
	char id[TMPL_ID], name[160];
	tmpl_new_id(id, sizeof id);
	snprintf(name, sizeof name, "%s, copy", e->t->name ? e->t->name : "");
	tmpl_set_str(&e->t->id, id);
	tmpl_set_str(&e->t->name, name);
	char note[200];
	snprintf(note, sizeof note, "A copy of %s", e->t->name);
	note[strlen(note) - 6] = 0; /* without ", copy" */
	tmpl_set_str(&e->t->note, note);
	e->fresh = true;
}

void editor_open_copy(struct editor *e, const struct tmpl *t)
{
	reset(e);
	e->t = tmpl_clone(t);
	make_copy(e);
}

void editor_open_new(struct editor *e, enum pkind k)
{
	reset(e);
	struct tmpl blank = { NULL, k, NULL, NULL, NULL };
	e->t = tmpl_clone(&blank);
	char id[TMPL_ID], name[64];
	tmpl_new_id(id, sizeof id);
	snprintf(name, sizeof name, "New %s template", pkind_name(k));
	tmpl_set_str(&e->t->id, id);
	tmpl_set_str(&e->t->name, name);
	e->fresh = true;
}

/* ---- the tree ---- */

struct tnode *editor_parent(struct tnode *root, const struct tnode *n, int *idx)
{
	if (!root || root == n)
		return NULL;
	for (int i = 0; i < root->nc; i++) {
		if (root->c[i] == n) {
			if (idx)
				*idx = i;
			return root;
		}
		struct tnode *p = editor_parent(root->c[i], n, idx);
		if (p)
			return p;
	}
	return NULL;
}

int editor_widgets(const struct tnode *n)
{
	if (!n)
		return 0;
	if (n->k == N_WIDGET)
		return 1;
	int k = 0;
	for (int i = 0; i < n->nc; i++)
		k += editor_widgets(n->c[i]);
	return k;
}

static void take_out(struct tnode *p, int i)
{
	memmove(&p->c[i], &p->c[i + 1], (p->nc - i - 1) * sizeof *p->c);
	p->nc--;
}

static void put_in(struct tnode *p, int i, struct tnode *n)
{
	memmove(&p->c[i + 1], &p->c[i], (p->nc - i) * sizeof *p->c);
	p->c[i] = n;
	p->nc++;
}

/* n leaves the tree; a row or column left with one child gives way to it, an empty one goes */
static void detach(struct tmpl *t, struct tnode *n)
{
	int i;
	struct tnode *p = editor_parent(t->root, n, &i);
	if (!p)
		return;
	take_out(p, i);
	while (p != t->root && p->nc <= 1) {
		int j;
		struct tnode *g = editor_parent(t->root, p, &j);
		if (!g)
			break;
		if (p->nc == 1) {
			struct tnode *only = p->c[0];
			if (!(only->w > 0))
				only->w = p->w;
			only->grow |= p->grow;
			g->c[j] = only;
			p->nc = 0;
			tnode_free(p);
			break;
		}
		take_out(g, j);
		tnode_free(p);
		p = g;
	}
	/* a root with one row or column under it takes its place */
	if (t->root->nc == 1 && t->root->c[0]->k != N_WIDGET) {
		struct tnode *only = t->root->c[0];
		t->root->nc = 0;
		tnode_free(t->root);
		only->w = 0;
		t->root = only;
	}
}

static enum nkind kind_now(const struct editor *e, const struct tnode *n, bool land)
{
	return land && n->land != N_WIDGET ? n->land : n->k;
}

/* put n beside target: along target's parent when that runs the way asked, else in a new row
 * or column in target's place */
static bool insert_by(struct editor *e, struct tnode *n, struct tnode *target, int side, bool land)
{
	enum nkind axis = side < 2 ? N_ROW : N_COL;
	bool after = side == 1 || side == 3;
	int i;
	struct tnode *p = editor_parent(e->t->root, target, &i);
	if (!p)
		return false;
	if (kind_now(e, p, land) == axis && p->nc < TN_KIDS) {
		put_in(p, after ? i + 1 : i, n);
		return true;
	}
	struct tnode *w = tnode_new(axis);
	if (!w)
		return false;
	w->land = axis; /* stays the same kind when turned */
	w->w = target->w;
	w->grow = target->grow;
	target->w = 0;
	w->c[0] = after ? target : n;
	w->c[1] = after ? n : target;
	w->nc = 2;
	p->c[i] = w;
	return true;
}

static const char *default_title(enum wkind k, int period)
{
	switch (k) {
	case W_TODO: return period == P_DAY ? "Today" : period == P_WEEK ? "This week" : "This month";
	case W_PRIO: return "Priorities";
	case W_HABITS: return "Habits";
	case W_NOTES: return "Notes";
	default: return NULL;
	}
}

static struct tnode *new_widget(struct editor *e, enum wkind k)
{
	struct tnode *n = tnode_new(N_WIDGET);
	if (!n)
		return NULL;
	n->wg = k;
	if (k == W_AGENDA || k == W_CAGENDA)
		n->from = 7, n->to = 21;
	if (k == W_TODO || k == W_PRIO || k == W_HABITS || k == W_NOTES)
		n->period = e->t->kind;
	if (k == W_NOTES)
		tmpl_set_str(&n->slot, "notes");
	const char *ti = default_title(k, n->period);
	if (ti)
		tmpl_set_str(&n->title, ti);
	return n;
}

static bool add_widget(struct editor *e, enum wkind k)
{
	if (editor_widgets(e->t->root) >= GRID_MAX)
		return false;
	struct tnode *n = new_widget(e, k);
	if (!n)
		return false;
	bool ok = false;
	if (e->sel && editor_parent(e->t->root, e->sel, NULL)) {
		int i;
		struct tnode *p = editor_parent(e->t->root, e->sel, &i);
		if (p->nc < TN_KIDS) {
			put_in(p, i + 1, n);
			ok = true;
		} else
			ok = insert_by(e, n, e->sel, kind_now(e, p, e->turned) == N_ROW ? 3 : 1, e->turned);
	} else if (e->t->root->nc < TN_KIDS) {
		e->t->root->c[e->t->root->nc++] = n;
		ok = true;
	}
	if (!ok) {
		tnode_free(n);
		return false;
	}
	e->sel = n;
	return true;
}

/* ---- slots ---- */

static void slot_add(const char **out, int *n, int max, const char *s)
{
	if (!s || !*s)
		return;
	for (int i = 0; i < *n; i++)
		if (!strcmp(out[i], s))
			return;
	if (*n < max)
		out[(*n)++] = s;
}

static void slots_of(const struct tnode *n, const char **out, int *k, int max)
{
	if (n->k == N_WIDGET && n->wg == W_NOTES)
		slot_add(out, k, max, n->slot ? n->slot : "notes");
	for (int i = 0; i < n->nc; i++)
		slots_of(n->c[i], out, k, max);
}

int editor_slots(const struct store *s, const char **out, int max)
{
	int n = 0;
	for (int i = 0; i < pl_ntemplates; i++)
		slots_of(pl_templates[i]->root, out, &n, max);
	const struct tmpl *ts[64];
	for (int k = 0; k < 3; k++) {
		int m = tmpl_list(k, ts, 64);
		for (int i = 0; i < m; i++)
			slots_of(ts[i]->root, out, &n, max);
	}
	for (int i = 0; s && i < s->n; i++)
		if (s->e[i]->kind == E_NOTE)
			slot_add(out, &n, max, s->e[i]->slot);
	return n;
}

/* ---- actions ---- */

void editor_name_value(struct editor *e, const char *text)
{
	if (!e->t || !text)
		return;
	while (*text == ' ')
		text++;
	if (!*text)
		return;
	char n[120];
	snprintf(n, sizeof n, "%s", text);
	for (size_t l = strlen(n); l && n[l - 1] == ' '; l--)
		n[l - 1] = 0;
	if (!e->t->name || strcmp(e->t->name, n)) {
		tmpl_set_str(&e->t->name, n);
		e->dirty = true;
	}
}

static bool is_widget(const struct editor *e, const struct tnode *n);

/* a notes widget shows slot `name`; a title that named the old slot follows */
static void set_slot(struct editor *e, struct tnode *s, const char *name)
{
	const char *old = s->slot ? s->slot : "notes";
	bool titled = !s->title || !strcasecmp(s->title, old) || !strcmp(s->title, "Notes");
	tmpl_set_str(&s->slot, name);
	if (titled) {
		char t[32];
		snprintf(t, sizeof t, "%s", name);
		t[0] = (char)toupper((unsigned char)t[0]);
		tmpl_set_str(&s->title, t);
	}
	e->dirty = true;
}

/* FR-027 and the edge case "a template edit removes the only widget showing a slot that has
 * entries": the edit goes ahead, and says what stays stored */
static bool shows_slot(const struct tnode *n, const struct tnode *skip, const char *slot, int period, int kind)
{
	if (n != skip && n->k == N_WIDGET && n->wg == W_NOTES && (n->period < 0 ? kind : n->period) == period &&
	    !strcmp(n->slot ? n->slot : "notes", slot))
		return true;
	for (int i = 0; i < n->nc; i++)
		if (shows_slot(n->c[i], skip, slot, period, kind))
			return true;
	return false;
}

static void removal_notice(struct editor *e, const struct tnode *s)
{
	if (s->wg != W_NOTES)
		return;
	const char *slot = s->slot ? s->slot : "notes";
	int per = s->period < 0 ? (int)e->t->kind : s->period;
	if (shows_slot(e->t->root, s, slot, per, (int)e->t->kind))
		return;
	int n = editor_slot_days(e->data, slot, per);
	if (!n)
		return;
	static const char *pn[] = { "day", "week", "month" };
	snprintf(e->notice, sizeof e->notice, "Notes on %d %s%s stay stored, unchanged.", n, pn[per], n == 1 ? "" : "s");
}

void editor_turned_size(double W, double H, double *tw, double *th)
{
	*tw = H + SYS_BAR;
	*th = W - SYS_BAR;
}

bool editor_edited_shipped(const struct editor *e)
{
	return e->t && !e->fresh && e->t->id && tmpl_is_shipped(e->t->id) && tmpl_user_find(e->t->id);
}

int editor_slot_days(const struct store *s, const char *slot, int k)
{
	int n = 0;
	for (int i = 0; s && i < s->n; i++) {
		const struct entry *x = s->e[i];
		if (x->kind != E_NOTE || (int)x->per.kind != k || strcmp(x->slot[0] ? x->slot : "notes", slot) || field_empty(&x->f))
			continue;
		bool seen = false;
		for (int j = 0; j < i && !seen; j++) {
			const struct entry *y = s->e[j];
			seen = y->kind == E_NOTE && y->per.kind == x->per.kind && y->per.start == x->per.start &&
			       !strcmp(y->slot[0] ? y->slot : "notes", slot) && !field_empty(&y->f);
		}
		n += !seen;
	}
	return n;
}

/* the periods of kind k anything is known about: those holding entries, and those chosen one by one */
static int known_periods(const struct store *s, int k, int **out)
{
	int cap = s->n + s->nc + 1, n = 0;
	int *o = malloc(cap * sizeof *o);
	if (!o)
		return 0;
	for (int i = 0; i < s->n + s->nc; i++) {
		int st;
		if (i < s->n) {
			if ((int)s->e[i]->per.kind > k) /* a week's entry doesn't make its days used */
				continue;
			st = period_of(k, s->e[i]->per.start).start;
		} else {
			const struct choice *c = &s->c[i - s->n];
			if ((int)c->from.kind != k || c->to != c->from.start)
				continue;
			st = c->from.start;
		}
		bool dup = false;
		for (int j = 0; j < n && !dup; j++)
			dup = o[j] == st;
		if (!dup)
			o[n++] = st;
	}
	*out = o;
	return n;
}

int editor_uses(const struct store *s, const struct tmpl *t, bool *def)
{
	if (def)
		*def = false;
	if (!s || !t || !t->id)
		return 0;
	int k = t->kind, n = 0;
	const char *d = s->def[k][0] ? s->def[k] : tmpl_default(k)->id;
	if (def)
		*def = !strcmp(d, t->id);
	int *ps = NULL, np = known_periods(s, k, &ps);
	for (int i = 0; i < np; i++) {
		const char *id = store_template(s, (struct period){ k, ps[i] }, NULL);
		const struct tmpl *u = id && *id ? tmpl_find(id) : NULL;
		if (!u || (int)u->kind != k)
			u = tmpl_default(k);
		n += !strcmp(u->id, t->id);
	}
	free(ps);
	return n;
}

int editor_drop_at(const struct grid *g, const struct tnode *sel, double x, double y, const struct tnode **target)
{
	for (int i = 0; i < g->n; i++) {
		const struct placed *p = &g->p[i];
		if (p->n == sel || x < p->x || x >= p->x + p->w || y < p->y || y >= p->y + p->h)
			continue;
		double d[4] = { x - p->x, p->x + p->w - x, y - p->y, p->y + p->h - y };
		int side = 0;
		for (int k = 1; k < 4; k++)
			if (d[k] < d[side])
				side = k;
		if (target)
			*target = p->n;
		return side;
	}
	if (target)
		*target = NULL;
	return -1;
}

int editor_edge_steps(double dx, double dy)
{
	return (int)lround((fabs(dx) > fabs(dy) ? dx : dy) / 60);
}

void editor_ink(struct editor *e, int which, uint32_t k, const struct pl_point *p, int n)
{
	field_add_stroke(which ? &e->slot : &e->name, k, p, n);
}

/* a slot's name as read: lower case, letters, digits, spaces and dashes, at most 31 bytes */
static void slot_clean(char *o, int len, const char *t)
{
	int j = 0;
	for (; *t == ' '; t++)
		;
	for (; *t && j < len - 1; t++) {
		unsigned char c = (unsigned char)*t;
		if (c >= 0x80 || isalnum(c) || c == ' ' || c == '-')
			o[j++] = c < 0x80 ? (char)tolower(c) : (char)c;
	}
	while (j && o[j - 1] == ' ')
		j--;
	/* a cut UTF-8 sequence goes */
	while (j && ((unsigned char)o[j - 1] & 0xC0) == 0x80)
		j--;
	if (j && (unsigned char)o[j - 1] >= 0xC0)
		j--;
	o[j] = 0;
}

void editor_ink_value(struct editor *e, int which, const char *text, const uint32_t *gone, int ngone, const void *state,
		      size_t len)
{
	struct field *f = which ? &e->slot : &e->name;
	for (int i = 0; i < ngone; i++)
		field_remove_stroke(f, gone[i]);
	field_set_value(f, text, state, len);
	if (!which) {
		editor_name_value(e, text);
		return;
	}
	struct tnode *s = is_widget(e, e->sel) ? e->sel : NULL;
	char name[32];
	slot_clean(name, sizeof name, text ? text : "");
	if (s && s->wg == W_NOTES && *name && strcmp(s->slot ? s->slot : "notes", name))
		set_slot(e, s, name);
}

static bool is_widget(const struct editor *e, const struct tnode *n)
{
	return n && n->k == N_WIDGET && editor_parent(e->t->root, n, NULL);
}

enum ed_result editor_act(struct editor *e, enum ed_op op, int arg, const char *dir)
{
	if (!e->t)
		return ER_NONE;
	struct tnode *s = is_widget(e, e->sel) ? e->sel : NULL;
	if (op != ED_NAME)
		e->naming = false;
	e->notice[0] = 0;
	switch (op) {
	case ED_SELECT:
		e->sel = is_widget(e, e->target) ? e->target : NULL;
		e->ov = EO_NONE;
		return ER_CHANGED;
	case ED_BEFORE:
	case ED_AFTER: {
		int i;
		struct tnode *p = s ? editor_parent(e->t->root, s, &i) : NULL;
		int j = op == ED_BEFORE ? i - 1 : i + 1;
		if (!p || j < 0 || j >= p->nc)
			return ER_NONE;
		p->c[i] = p->c[j];
		p->c[j] = s;
		e->dirty = true;
		return ER_CHANGED;
	}
	case ED_LESS:
	case ED_MORE: {
		if (!s)
			return ER_NONE;
		double w = s->w > 0 ? s->w : 1;
		w += op == ED_MORE ? 1 : -1;
		if (w < 1 || w > 9)
			return ER_NONE;
		s->w = w;
		e->dirty = true;
		return ER_CHANGED;
	}
	case ED_SETTINGS:
		if (!s)
			return ER_NONE;
		e->ov = EO_WSET;
		return ER_CHANGED;
	case ED_REMOVE:
		if (!s)
			return ER_NONE;
		removal_notice(e, s);
		detach(e->t, s);
		tnode_free(s);
		e->sel = NULL;
		e->dirty = true;
		return ER_CHANGED;
	case ED_ADD:
		e->ov = EO_ADD;
		return ER_CHANGED;
	case ED_ADD_W:
		if (arg < 0 || arg >= W_NWIDGETS || !add_widget(e, arg))
			return ER_NONE;
		e->ov = EO_NONE;
		e->dirty = true;
		return ER_CHANGED;
	case ED_TURN:
		e->turned = !e->turned;
		return ER_CHANGED;
	case ED_NAME:
		e->naming = true;
		e->ov = EO_NONE;
		return ER_CHANGED;
	case ED_SAVE: {
		if (e->short_)
			return ER_NONE; /* FR-044: not until it fits */
		if (e->name.value && *e->name.value)
			editor_name_value(e, e->name.value);
		struct tmpl *k = tmpl_clone(e->t);
		if (!k)
			return ER_NONE;
		tmpl_user_put(dir, k);
		return ER_SAVED;
	}
	case ED_CANCEL:
		if (e->dirty && e->ov != EO_DISCARD) {
			e->ov = EO_DISCARD;
			return ER_CHANGED;
		}
		return ER_CLOSED;
	case ED_DISCARD:
		return ER_CLOSED;
	case ED_KEEP:
		if (e->ov == EO_USED)
			return ER_CLOSED; /* the in-use dialog's Cancel: nothing was changed */
		e->ov = e->ov == EO_SLOT ? EO_WSET : EO_NONE;
		return ER_CHANGED;
	case ED_RESTORE:
		if (!editor_edited_shipped(e))
			return ER_NONE;
		e->ov = EO_RESTORE;
		return ER_CHANGED;
	case ED_RESTORE_YES:
		if (e->ov != EO_RESTORE || !editor_edited_shipped(e))
			return ER_NONE;
		tmpl_user_remove(dir, e->t->id);
		return ER_SAVED;
	case ED_COPY_FIRST:
		make_copy(e);
		e->ov = EO_NONE;
		return ER_CHANGED;
	case ED_EDIT_ALL:
		e->ov = EO_NONE;
		return ER_CHANGED;
	case ED_FROM:
	case ED_TO: {
		if (!s || (s->wg != W_AGENDA && s->wg != W_CAGENDA))
			return ER_NONE;
		int f = s->from + (op == ED_FROM ? arg : 0), t = s->to + (op == ED_TO ? arg : 0);
		if (f < 0 || t > 24 || t - f < 2)
			return ER_NONE;
		s->from = f, s->to = t;
		e->dirty = true;
		return ER_CHANGED;
	}
	case ED_PERIOD: {
		if (!s || !(s->wg == W_TODO || s->wg == W_PRIO || s->wg == W_HABITS || s->wg == W_NOTES))
			return ER_NONE;
		int cur = s->period < 0 ? (int)e->t->kind : s->period;
		const char *old = default_title(s->wg, cur);
		s->period = (cur + 1) % 3;
		if (s->wg == W_TODO && (!s->title || (old && !strcmp(s->title, old))))
			tmpl_set_str(&s->title, default_title(s->wg, s->period));
		e->dirty = true;
		return ER_CHANGED;
	}
	case ED_SLOTS:
		if (!s || s->wg != W_NOTES)
			return ER_NONE;
		field_free(&e->slot);
		e->ov = EO_SLOT;
		return ER_CHANGED;
	case ED_SLOT_PICK: {
		const char *sl[32];
		int n = editor_slots(e->data, sl, 32);
		if (!s || s->wg != W_NOTES || arg < 0 || arg >= n)
			return ER_NONE;
		char name[32];
		snprintf(name, sizeof name, "%s", sl[arg]);
		set_slot(e, s, name);
		e->ov = EO_WSET;
		return ER_CHANGED;
	}
	case ED_GROW:
		if (!s)
			return ER_NONE;
		s->grow = !s->grow;
		e->dirty = true;
		return ER_CHANGED;
	case ED_LAND: {
		struct tnode *p = s ? editor_parent(e->t->root, s, NULL) : NULL;
		if (!p)
			return ER_NONE;
		p->land = (p->land == N_WIDGET ? p->k : p->land) == N_ROW ? N_COL : N_ROW;
		e->dirty = true;
		return ER_CHANGED;
	}
	case ED_DROP: {
		if (!s || !is_widget(e, e->target) || e->target == s || arg < 0 || arg > 3)
			return ER_NONE;
		struct tnode *t = e->target;
		detach(e->t, s);
		if (!insert_by(e, s, t, arg, e->turned)) {
			/* nowhere to put it: back at the end of the root */
			if (e->t->root->nc < TN_KIDS)
				e->t->root->c[e->t->root->nc++] = s;
			else {
				tnode_free(s);
				e->sel = NULL;
			}
		}
		e->dirty = true;
		return ER_CHANGED;
	}
	}
	return ER_NONE;
}
