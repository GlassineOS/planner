/* SPDX-License-Identifier: GPL-2.0-or-later
 * The template editor (spec 001 FR-044, FR-045, FR-049; boards templates.editor, templates.new,
 * templates.used in GlassineOS/ui designs/planner). It works on a copy of the template and
 * only arranges: nothing here takes a store to write, so no editor action can create, change
 * or delete an entry (FR-026). Save hands the copy to the person's templates (tfile.h); Cancel
 * drops it. Every action is a plain function of the editor's state, so the tests drive it
 * without a screen. No cairo, no Wayland. */
#ifndef PL_EDITOR_H
#define PL_EDITOR_H
#include "store.h"
#include "tfile.h"

enum ed_op {
	ED_SELECT,   /* arg: the widget's index in the laid-out grid (the view passes the node) */
	ED_BEFORE,   /* the selected widget before the one ahead of it */
	ED_AFTER,
	ED_LESS,     /* weight */
	ED_MORE,
	ED_SETTINGS, /* the selected widget's settings sheet */
	ED_REMOVE,
	ED_ADD,      /* the list of widgets */
	ED_ADD_W,    /* arg: enum wkind */
	ED_TURN,     /* preview the other orientation */
	ED_NAME,     /* the name becomes a field written with the pen */
	ED_SAVE,
	ED_CANCEL,
	ED_DISCARD,  /* the dialog's "Discard changes" */
	ED_KEEP,     /* the dialog's "Keep editing", a sheet's Close */
	ED_COPY_FIRST, /* the in-use dialog: edit a copy instead */
	ED_EDIT_ALL, /* the in-use dialog: edit it for every period */
	ED_FROM,     /* arg ±1: an agenda's first hour */
	ED_TO,       /* arg ±1: its last hour */
	ED_PERIOD,   /* the next period: day, week, month */
	ED_SLOT,     /* the next slot name */
	ED_GROW,     /* takes leftover space, on or off */
	ED_LAND,     /* its row or column, when turned: the other kind */
	ED_DROP,     /* arg: side (0 left, 1 right, 2 above, 3 below); the target set by editor_target */
};

enum ed_overlay { EO_NONE, EO_ADD, EO_WSET, EO_DISCARD, EO_USED };

struct editor {
	struct tmpl *t;      /* the working copy */
	bool fresh;          /* a new template or a copy: no period uses it yet */
	bool dirty;          /* changed since it opened */
	bool from_picker;    /* Save and Cancel go back to the picker, else to the view */
	bool turned;         /* previewing the other orientation */
	bool naming;
	struct tnode *sel;
	struct tnode *target; /* a drop's target widget */
	enum ed_overlay ov;
	struct field name;   /* the name as written (its reading names the template) */
	const struct store *data; /* read only: the slot names entries use (ED_SLOT) */
	/* set by the view's layout: FR-044 in both orientations */
	bool short_;
	double deficit;
	bool short_turned;   /* the shortfall is in the other orientation */
};

enum ed_result { ER_NONE, ER_CHANGED, ER_CLOSED, ER_SAVED };

/* open: on a template (its edit), a new empty one of kind k, or a copy of one */
void editor_open_edit(struct editor *e, const struct tmpl *t);
void editor_open_new(struct editor *e, enum pkind k);
void editor_open_copy(struct editor *e, const struct tmpl *t);
void editor_close(struct editor *e);

/* one action; dir is where Save writes (NULL: in memory) */
enum ed_result editor_act(struct editor *e, enum ed_op op, int arg, const char *dir);
/* a reading of the written name; empty keeps the name it has */
void editor_name_value(struct editor *e, const char *text);

/* the tree: the parent of n and n's index in it */
struct tnode *editor_parent(struct tnode *root, const struct tnode *n, int *idx);
int editor_widgets(const struct tnode *n);
/* the slot names to choose from: the shipped templates', the person's, and those of entries */
int editor_slots(const struct store *s, const char **out, int max);

#endif
