/* SPDX-License-Identifier: GPL-2.0-or-later
 * Templates and the fluid grid (spec 001 FR-030 to FR-048). A template is a tree of rows and
 * columns whose leaves are widgets; a node may change kind with the orientation. Widgets
 * declare their width (minimum, preferred) and their height (minimum, and what their content
 * needs). Space along each axis is shared in three steps: every child its minimum, then
 * towards its need by weight, then the rest to the children that grow. No cairo, no Wayland. */
#ifndef PL_GRID_H
#define PL_GRID_H
#include <stdbool.h>
#include "store.h"

enum wkind { W_AGENDA, W_CAGENDA, W_TODO, W_NOTES, W_HABITS, W_MINIMONTH, W_WEEKOV, W_PRIO, W_NWIDGETS };
enum nkind { N_ROW, N_COL, N_WIDGET };

#define TN_KIDS 8
struct tnode {
	enum nkind k;
	enum nkind land;     /* its kind when the screen is turned (N_WIDGET: the same) */
	double w;            /* weight, 1 when 0 */
	bool grow;
	enum wkind wg;
	int period;          /* -1: the view's own; else enum pkind */
	const char *slot;    /* notes */
	const char *title;
	int from, to;        /* agenda hours */
	int day;             /* compact agenda: the day of the week, 0 = the first */
	bool large;          /* mini month as the month grid */
	struct tnode *c[TN_KIDS];
	int nc;
};

struct tmpl {
	const char *id;
	enum pkind kind;
	const char *name, *note;
	struct tnode *root;
};

extern const struct tmpl *const pl_templates[];
extern const int pl_ntemplates;
const struct tmpl *tmpl_find(const char *id);
const struct tmpl *tmpl_default(enum pkind k);

/* what a widget's content needs, given by the caller (counts of rows, an all-day line) */
struct need {
	int n;       /* rows of content */
	bool allday; /* the agenda has an all-day line */
};
typedef struct need (*need_fn)(const struct tnode *n, void *ud);

struct placed {
	const struct tnode *n;
	double x, y, w, h;
};

#define GRID_MAX 32
struct grid {
	struct placed p[GRID_MAX];
	int n;
	bool short_;   /* the minimums don't fit */
	double deficit;
};

void grid_solve(const struct tmpl *t, double w, double h, bool land, need_fn need, void *ud, struct grid *out);

/* widget metrics shared with the drawing */
#define PL_ROW 34
#define PL_HEAD 34
#define PL_MORE 24
#define PL_PAD 10
#define PL_GAP 10

#endif
