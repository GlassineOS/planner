// SPDX-License-Identifier: GPL-2.0-or-later
/* Templates and the fluid grid (grid.h). The shipped templates are the design's
 * (GlassineOS/ui designs/planner). */
#include <math.h>
#include <string.h>

#include "grid.h"

#define W(...) &(struct tnode){ .k = N_WIDGET, .land = N_WIDGET, .period = -1, __VA_ARGS__ }
#define ROW(LAND, ...) .k = N_ROW, .land = LAND, .c = { __VA_ARGS__ }, .nc = sizeof((struct tnode *[]){ __VA_ARGS__ }) / sizeof(struct tnode *)
#define COL(LAND, ...) .k = N_COL, .land = LAND, .c = { __VA_ARGS__ }, .nc = sizeof((struct tnode *[]){ __VA_ARGS__ }) / sizeof(struct tnode *)
#define N(...) &(struct tnode){ __VA_ARGS__ }

static const struct tmpl day_lists = { "day-lists", P_DAY, "Agenda and lists",
	"The hours beside the mini month and the day’s to-dos; the week’s to-dos and notes underneath",
	N(COL(N_ROW,
	      N(.w = 3, ROW(N_ROW, W(.wg = W_AGENDA, .w = 3, .grow = true, .from = 7, .to = 21),
			    N(.w = 2, COL(N_COL, W(.wg = W_MINIMONTH),
					  W(.wg = W_TODO, .period = P_DAY, .title = "Today", .grow = true))))),
	      N(.w = 1, ROW(N_COL, W(.wg = W_TODO, .period = P_WEEK, .title = "This week", .w = 1),
			    W(.wg = W_NOTES, .period = P_DAY, .slot = "notes", .title = "Notes", .grow = true, .w = 1))))) };

static const struct tmpl day_diary = { "day-diary", P_DAY, "Diary",
	"Priorities and habits on top; the hours beside a morning and an evening slot",
	N(COL(N_ROW,
	      N(.w = 1, ROW(N_COL, W(.wg = W_PRIO, .period = P_DAY, .title = "Priorities", .w = 1),
			    W(.wg = W_HABITS, .period = P_DAY, .title = "Habits", .w = 1))),
	      N(.w = 3, .grow = true,
		ROW(N_ROW, W(.wg = W_AGENDA, .from = 7, .to = 21, .w = 2),
		    N(.w = 3, COL(N_COL, W(.wg = W_NOTES, .period = P_DAY, .slot = "morning", .title = "Morning", .grow = true),
				  W(.wg = W_NOTES, .period = P_DAY, .slot = "evening", .title = "Evening", .grow = true))))))) };

static const struct tmpl day_agenda = { "day-agenda", P_DAY, "Agenda only",
	"Only the hours: to-dos and notes are kept and counted as hidden",
	N(COL(N_COL, W(.wg = W_AGENDA, .grow = true, .from = 6, .to = 23))) };

#define CA(D) W(.wg = W_CAGENDA, .day = D, .from = 8, .to = 21)
static const struct tmpl week_columns = { "week-columns", P_WEEK, "Seven columns",
	"A compact agenda per day over the week’s lists",
	N(COL(N_COL, N(.w = 3, .grow = true, ROW(N_ROW, CA(0), CA(1), CA(2), CA(3), CA(4), CA(5), CA(6))),
	      N(.w = 1, ROW(N_ROW, W(.wg = W_TODO, .period = P_WEEK, .title = "This week", .w = 2),
			    W(.wg = W_NOTES, .period = P_WEEK, .slot = "notes", .title = "Notes", .w = 2))))) };

static const struct tmpl week_spread = { "week-spread", P_WEEK, "Spread",
	"The days in short on the left, the week’s lists on the right",
	N(ROW(N_ROW, W(.wg = W_WEEKOV, .w = 3, .grow = true),
	      N(.w = 2, COL(N_COL, W(.wg = W_PRIO, .period = P_WEEK, .title = "Priorities"),
			    W(.wg = W_TODO, .period = P_WEEK, .title = "This week", .grow = true),
			    W(.wg = W_HABITS, .period = P_WEEK, .title = "Habits"))))) };

static const struct tmpl week_lists = { "week-lists", P_WEEK, "Lists and notes",
	"No hours: the week’s events are counted as hidden",
	N(COL(N_COL, N(ROW(N_ROW, W(.wg = W_MINIMONTH, .w = 1), W(.wg = W_TODO, .period = P_WEEK, .title = "This week", .w = 2))),
	      W(.wg = W_NOTES, .period = P_WEEK, .slot = "notes", .title = "Notes", .grow = true))) };

static const struct tmpl month_grid = { "month-grid", P_MONTH, "Calendar",
	"The month as a grid of days with their events, its lists beneath",
	N(COL(N_ROW, W(.wg = W_MINIMONTH, .large = true, .grow = true, .w = 3),
	      N(.w = 1, ROW(N_COL, W(.wg = W_TODO, .period = P_MONTH, .title = "This month", .w = 1),
			    W(.wg = W_PRIO, .period = P_MONTH, .title = "Priorities", .w = 1))))) };

static const struct tmpl month_goals = { "month-goals", P_MONTH, "Goals",
	"Priorities and habits for the month, a mini month and notes",
	N(COL(N_COL, N(ROW(N_ROW, W(.wg = W_MINIMONTH, .w = 1), W(.wg = W_PRIO, .period = P_MONTH, .title = "Priorities", .w = 2))),
	      W(.wg = W_HABITS, .period = P_MONTH, .title = "Habits"),
	      W(.wg = W_NOTES, .period = P_MONTH, .slot = "notes", .title = "Notes", .grow = true))) };

const struct tmpl *const pl_templates[] = { &day_lists, &day_diary, &day_agenda, &week_columns, &week_spread,
	&week_lists, &month_grid, &month_goals };
const int pl_ntemplates = sizeof pl_templates / sizeof *pl_templates;

const struct tmpl *tmpl_find(const char *id)
{
	for (int i = 0; id && i < pl_ntemplates; i++)
		if (!strcmp(pl_templates[i]->id, id))
			return pl_templates[i];
	return NULL;
}

const struct tmpl *tmpl_default(enum pkind k)
{
	return k == P_DAY ? &day_lists : k == P_WEEK ? &week_columns : &month_grid;
}

/* ---- the widgets' declarations (FR-041) ---- */

static double min_w(enum wkind k)
{
	static const double v[] = { 200, 84, 180, 200, 240, 170, 260, 180 };
	return v[k];
}

static double pref_w(enum wkind k)
{
	static const double v[] = { 360, 100, 300, 400, 360, 230, 420, 300 };
	return v[k];
}

static double min_h(enum wkind k, struct need c, int hours)
{
	switch (k) {
	case W_AGENDA: return PL_HEAD + (c.allday ? 26 : 0) + 6 * 20;
	case W_CAGENDA: return 30 + 6 * 16;
	case W_HABITS: return PL_HEAD + 22 + 2 * 30 + PL_MORE;
	case W_MINIMONTH: return PL_HEAD + 22 + 6 * 22;
	case W_WEEKOV: return PL_HEAD + 7 * 30;
	default: return PL_HEAD + 2 * PL_ROW + PL_MORE;
	}
}

static double need_h(enum wkind k, struct need c, int hours)
{
	switch (k) {
	case W_AGENDA: return PL_HEAD + (c.allday ? 26 : 0) + hours * 34;
	case W_CAGENDA: return 30 + hours * 26;
	case W_TODO:
	case W_PRIO: return PL_HEAD + (c.n + 1) * PL_ROW + PL_PAD;
	case W_NOTES: return PL_HEAD + (c.n + 1 > 2 ? c.n + 1 : 2) * PL_ROW + PL_PAD;
	case W_HABITS: return PL_HEAD + 22 + (c.n + 1) * 30 + PL_PAD;
	case W_MINIMONTH: return PL_HEAD + 22 + 6 * 26;
	case W_WEEKOV: return PL_HEAD + 7 * 58;
	default: return 0;
	}
}

/* ---- the solver ---- */

struct mm {
	double min, need;
};

static enum nkind kind_of(const struct tnode *n, bool land)
{
	return land && n->land != N_WIDGET ? n->land : n->k;
}

static struct mm measure(const struct tnode *n, bool axis_w, bool land, need_fn fn, void *ud)
{
	if (n->k == N_WIDGET) {
		if (axis_w)
			return (struct mm){ min_w(n->wg), n->large ? pref_w(n->wg) + 400 : pref_w(n->wg) };
		struct need c = fn ? fn(n, ud) : (struct need){ 0 };
		double mi = min_h(n->wg, c, n->to - n->from), ne = need_h(n->wg, c, n->to - n->from);
		if (ne < mi)
			ne = mi;
		return (struct mm){ mi, n->large ? ne + 300 : ne };
	}
	bool along = (kind_of(n, land) == N_ROW) == axis_w;
	struct mm r = { 0, 0 };
	for (int i = 0; i < n->nc; i++) {
		struct mm k = measure(n->c[i], axis_w, land, fn, ud);
		if (along) {
			r.min += k.min;
			r.need += k.need;
		} else {
			if (k.min > r.min)
				r.min = k.min;
			if (k.need > r.need)
				r.need = k.need;
		}
	}
	if (along && n->nc > 1) {
		r.min += PL_GAP * (n->nc - 1);
		r.need += PL_GAP * (n->nc - 1);
	}
	return r;
}

static bool has_grow(const struct tnode *n)
{
	if (n->grow)
		return true;
	for (int i = 0; i < n->nc; i++)
		if (has_grow(n->c[i]))
			return true;
	return false;
}

static void place(const struct tnode *n, double x, double y, double w, double h, bool land, need_fn fn, void *ud,
		  struct grid *out)
{
	if (n->k == N_WIDGET) {
		if (out->n < GRID_MAX)
			out->p[out->n++] = (struct placed){ n, x, y, w, h };
		return;
	}
	bool axis_w = kind_of(n, land) == N_ROW;
	double total = (axis_w ? w : h) - PL_GAP * (n->nc - 1);
	struct mm it[TN_KIDS];
	double wt[TN_KIDS], s[TN_KIDS];
	bool gr[TN_KIDS];
	double R = total;
	for (int i = 0; i < n->nc; i++) {
		it[i] = measure(n->c[i], axis_w, land, fn, ud);
		wt[i] = n->c[i]->w > 0 ? n->c[i]->w : 1;
		gr[i] = axis_w ? true : has_grow(n->c[i]);
		s[i] = it[i].min;
		R -= s[i];
	}
	if (R < 0) {
		out->short_ = true;
		if (-R > out->deficit)
			out->deficit = round(-R);
	}
	for (int pass = 0; pass < 8 && R > 0.5; pass++) {
		double W = 0, used = 0;
		for (int i = 0; i < n->nc; i++)
			if (s[i] < it[i].need)
				W += wt[i];
		if (W <= 0)
			break;
		for (int i = 0; i < n->nc; i++)
			if (s[i] < it[i].need) {
				double add = R * wt[i] / W;
				if (add > it[i].need - s[i])
					add = it[i].need - s[i];
				s[i] += add;
				used += add;
			}
		R -= used;
	}
	if (R > 0.5) {
		double W = 0;
		for (int i = 0; i < n->nc; i++)
			if (gr[i])
				W += wt[i];
		for (int i = 0; i < n->nc && W > 0; i++)
			if (gr[i])
				s[i] += R * wt[i] / W;
	}
	double p = axis_w ? x : y;
	for (int i = 0; i < n->nc; i++) {
		double sz = s[i] > it[i].min ? s[i] : it[i].min;
		if (axis_w)
			place(n->c[i], round(p), y, round(sz), h, land, fn, ud, out);
		else
			place(n->c[i], x, round(p), w, round(sz), land, fn, ud, out);
		p += sz + PL_GAP;
	}
}

void grid_solve(const struct tmpl *t, double w, double h, bool land, need_fn need, void *ud, struct grid *out)
{
	memset(out, 0, sizeof *out);
	place(t->root, 0, 0, w, h, land, need, ud, out);
}
