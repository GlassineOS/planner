/* SPDX-License-Identifier: GPL-2.0-or-later
 * One screen of the planner (the design's boards, GlassineOS/ui designs/planner): the header
 * bar (navigation and the (i)), the period's widgets on the fluid grid, and the overlays (the
 * (i) popover, the cog's menu inside it, a hidden-entries list, the template picker). The view
 * is laid out into fields (each one writing region) and tap targets, then painted with cairo.
 * No Wayland: wl.c turns the fields into regions and the taps into actions. */
#ifndef PL_VIEW_H
#define PL_VIEW_H
#include <cairo.h>
#include <stdbool.h>
#include "editor.h"
#include "grid.h"
#include "look.h"
#include "store.h"

enum overlay { OV_NONE, OV_INFO, OV_MENU, OV_HIDDEN, OV_PICKER, OV_SETTINGS, OV_TITLE };

/* a handwriting field: one writing region */
enum fkind {
	F_ENTRY,  /* an entry's field (its title, its text) */
	F_NEW,    /* an empty row: the first stroke makes the entry */
	F_FREE,   /* an agenda's free time: reading off, a drawn block makes an event */
	F_NAME,   /* the template editor's name, written with the pen (no entry) */
};
struct vfield {
	enum fkind k;
	char name[96];      /* the region's name, unique on the surface */
	uint32_t entry;     /* F_ENTRY */
	enum ekind ekind;   /* F_NEW: what to make */
	struct period per;  /* F_NEW: its period; F_FREE: the day */
	char slot[32];
	int rank;
	double x, y, w, h;  /* surface coordinates */
	double guide, first; /* ruled lines in region coordinates (0: none) */
	int layer;
	/* F_FREE: minutes of the day at y and per px */
	double min0, mpp;
};

enum act {
	A_NONE, A_VIEW, A_PREV, A_NEXT, A_TODAY, A_INFO, A_COG, A_BACK, A_CLOSE, A_TICK, A_DAY, A_HIDDEN, A_TEXTMODE,
	A_PICKER, A_PICK, A_SCOPE, A_HABIT, A_EVENT, A_MORE,
	A_SETTINGS, A_SET_NEATEN, A_SET_STRENGTH, A_SET_AUTOGRAPH, /* the settings sheet */
	A_TM_NEATEN, A_TM_AUTOGRAPH, A_TM_DONE,                   /* the bar above the keyboard */
	A_SEARCH, A_RESULT,                                         /* search in the (i) popover */
	A_EDIT, /* open the template editor: arg 0 edit, 1 new, 2 copy */
	A_ED,   /* an editor action: arg enum ed_op, id its argument + 100 (a widget's index for select) */
};
struct vhit {
	double x, y, w, h;
	enum act a;
	int arg;        /* A_EVENT: 1 when its title is cut */
	uint32_t id;
	bool fixed;     /* on the bar above the keyboard: screen coordinates, never scrolled */
};

#define V_FIELDS 160
#define V_HITS 200
struct view {
	/* what is shown */
	struct period per;
	int today;
	const struct tmpl *t;
	int how;          /* how the period got its template (store_template) */
	enum overlay ov;
	int hidden_kind;  /* OV_HIDDEN: which row of the popover */
	int scope;        /* OV_PICKER: 0 only this period, 3 the default */
	bool text_view;   /* the cog's text mode for the whole view (shown as on) */
	double W, H;      /* the surface, logical px */
	bool land;
	/* laid out */
	struct grid g;
	double page_y;    /* the page's top (below the bar) */
	struct vfield f[V_FIELDS];
	int nf;
	struct vhit hit[V_HITS];
	int nh;
	int hidden_count;
	double pop_x, pop_y, pop_w, pop_h; /* the open popover's rectangle */
	uint32_t drag; int drag_s0, drag_e0; /* an event being dragged: where it was (dashed outline, gutter readout) */
	/* text mode (FR-012 to FR-017): the layout stays; the keyboard covers the screen below
	 * vis_h and the page moves up by scroll (whole rows) to reach a field under it */
	int tm;             /* 0 ink mode, 1 one field, 2 the whole view */
	char tm_name[96];   /* the field being edited (its name), "" none */
	const char *tm_text; /* its text, the caret at tm_cur (bytes); owned by the caller */
	int tm_cur;
	bool tm_neaten, tm_autograph; /* its two switches as shown */
	/* the whole view: a field's current text (its reading with the edits); NULL: its value */
	const char *(*text_of)(void *ud, const struct vfield *f);
	void *text_ud;
	double vis_h;       /* 0: all of H */
	double scroll;
	/* search, at the top of the (i) popover (FR-050) */
	bool search;
	const char *q;      /* the words searched for; owned by the caller */
	int q_cur;
	uint32_t title;     /* OV_TITLE: the event whose cut title shows whole */
	struct editor *ed;  /* the template editor, when open: it takes the whole screen */
	char ring[40];      /* the picker rings this template (a new one just saved), else the one in use */
};

struct hidden_group {
	enum ekind kind;
	char slot[32];
	int n;
	char label[64];
};
int view_hidden(struct store *s, const struct tmpl *t, struct period per, struct hidden_group *out, int max);

void view_layout(struct view *v, struct store *s, const struct look *L, cairo_t *measure);
void view_paint(struct view *v, struct store *s, const struct look *L, cairo_t *cr);
/* the refused screen (FR-001): no writing regions on this system */
void view_paint_refused(const struct look *L, cairo_t *cr, double W, double H, struct vhit *close);
/* the target at x, y on the screen (the page's targets moved up by scroll) */
const struct vhit *view_hit(const struct view *v, double x, double y);
/* the handwriting field at x, y on the screen, not under an overlay (NULL: none) */
const struct vfield *view_field_at(const struct view *v, double x, double y);
/* the page's scroll that brings field f above the bar over the keyboard (whole rows) */
double view_scroll_to(const struct view *v, const struct vfield *f);
const struct vfield *view_field_of(const struct view *v, uint32_t entry);

#endif
