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

/* the pen as the planner draws it and as the panel previews it while the frame is on its
 * way (glassine_ink_v1 set_brush, the same numbers): diameters in surface px at no and at
 * full pressure, pressure^PL_INK_EXP between */
#define PL_INK_WMIN 1.3
#define PL_INK_WMAX 2.5
#define PL_INK_EXP 1.0
double pl_ink_width(double pressure);

enum overlay { OV_NONE, OV_INFO, OV_MENU, OV_HIDDEN, OV_PICKER, OV_SETTINGS, OV_TITLE, OV_MOVE, OV_DELETE };

/* a handwriting field: one writing region */
enum fkind {
	F_ENTRY,  /* an entry's field (its title, its text) */
	F_NEW,    /* an empty row: the first stroke makes the entry */
	F_FREE,   /* an agenda's free time: reading off, a drawn block makes an event */
	F_NAME,   /* the template editor's name, written with the pen (no entry) */
	F_SLOT,   /* a new slot's name in the template editor (no entry) */
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
	A_OPEN, A_MOVE, A_MOVE_NAV, A_MOVE_TO,  /* the hidden list's Open and Move; Move's calendar: arg -1/+1, a day */
	A_YEAR, A_CELL, A_RANGE_CHANGE, A_RANGE_REMOVE, /* the year planned ahead: Plan ahead, a period (arg its day), a choice (arg its index) */
	A_YEAR_NAV, /* the year's arrows: arg -1/+1 */
	A_EDIT, /* open the template editor: arg 0 edit, 1 new, 2 copy */
	A_ED,   /* an editor action: arg enum ed_op, id its argument + 100 (a widget's index for select) */
	/* an agenda's slip (FR-018), id the event: its tab and its folded corner (a drag moves it,
	 * changes its end), the strip of a slip another lies on (a tap brings it to the top, or
	 * fans out a stack of three or more), a fanned slip (a tap brings it to the top) */
	A_EV_TAB, A_EV_EAR, A_EV_STRIP, A_EV_FAN,
	A_DEL_KEEP, A_DEL_YES, /* the card that deletes an event: Keep (also a tap outside), Delete event */
};
struct vhit {
	double x, y, w, h;
	enum act a;
	int arg;        /* A_EVENT: 1 when its title is cut */
	uint32_t id;
	bool fixed;     /* on the bar above the keyboard: screen coordinates, never scrolled */
};

/* An agenda's timed event as a paper slip on the day sheet (FR-018; boards event.drag,
 * event.delete, event.stack): page coordinates, as laid out. Overlapping events lie as
 * shingled slips, the one on top shifted right per level; one underneath shows a strip */
struct vslip {
	uint32_t id;
	int s0, e0;          /* its minutes on the agenda's day */
	double x, y, w, h;   /* as drawn: its level's place, a fanned slip's place in the fan */
	double sy, sh;       /* the strip of it that shows (sh 0: it lies on top, all of it shows) */
	bool strip_top;      /* the strip is its top (the one above starts later), else its bottom */
	int z, nz;           /* its level in its stack (0 the bottom) and the stack's size (1 alone) */
	int stack;           /* its stack's number in the view */
	bool fan;            /* fanned out: side by side, no tab, no folded corner */
	bool parts;          /* drawn with its tab and its folded corner */
};
#define V_SLIPS 64
/* the slip's parts: its tab (26 x 8, 6 px above its top edge, 30 px in from its right edge),
 * the tab's touch target (44 x 28), its folded corner's size (pulled: while its end is
 * dragged) and touch target (32 x 32 at its lower right), its lifted shadow (its size, 5 px
 * right and down), each as x, y, w, h */
void slip_tab(const struct vslip *s, double r[4]);
void slip_tab_target(const struct vslip *s, double r[4]);
double slip_ear(double h, bool pulled);
void slip_ear_target(const struct vslip *s, double r[4]);
void slip_lift(const struct vslip *s, double r[4]);

#define V_FIELDS 160
/* the header bar's height (logical px): 60, and taller by as much as its title and its
 * subtitle grow with the text size (GlassineOS/shell spec 012), as the last layout found */
double view_bar_h(void);
#define V_BAR view_bar_h()
#define V_HITS 600 /* the days of a year: a target per day */
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
	bool drag_ear;      /* its folded corner is pulled (its end dragged) */
	/* the agenda's slips as laid out; the z-order the person chose (view state, never
	 * saved: a slip brought to the top is last), a fanned stack (one of its slips), a
	 * crumpled slip (held still) */
	struct vslip slip[V_SLIPS];
	int nslip;
	uint32_t raise[16];
	int nraise;
	uint32_t fan;
	uint32_t crumple;
	/* OV_DELETE: the event in title; a stroke that crossed it out, page coordinates (owned
	 * by the caller), shown over it while the card is open */
	const struct pl_point *xout;
	int nxout;
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
	uint32_t title;     /* OV_TITLE: the event whose cut title shows whole, or an entry opened */
	enum overlay back;  /* OV_TITLE, OV_MOVE: where Close goes back to */
	uint32_t move;      /* OV_MOVE: the entry being moved */
	int move_at;        /* OV_MOVE: a day of the month (the year, for a month's entry) shown */
	/* OV_PICKER's year planned ahead (board templates.year) and a range being chosen: rg_n
	 * periods tapped (rg0 first, rg1 last), for the template rg_tmpl ("" none yet) */
	bool year;
	int year_off;       /* the year shown: the view's year plus this (the arrows) */
	int rg_n, rg0, rg1;
	char rg_tmpl[40];
	bool from_set;      /* "From … on" of another date than the view's (a choice changed) */
	int from_day;
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
/* view_layout measured as view_paint will draw at scale (device px per logical px): text is
 * measured at the size it is painted, so what is laid out (targets, fields, a dialog's
 * buttons) lies where it is painted. A measure at another scale wraps and fits text
 * differently, and a target can land off its painted control */
void view_layout_at(struct view *v, struct store *s, const struct look *L, double scale);
void view_paint(struct view *v, struct store *s, const struct look *L, cairo_t *cr);
/* keep painted ink per field (on by default); off drops what is kept */
void view_ink_cache(bool on);
/* the refused screen (FR-001): no writing regions on this system */
void view_paint_refused(const struct look *L, cairo_t *cr, double W, double H, struct vhit *close);
/* the target at x, y on the screen (the page's targets moved up by scroll) */
const struct vhit *view_hit(const struct view *v, double x, double y);
/* the handwriting field at x, y on the screen, not under an overlay (NULL: none) */
const struct vfield *view_field_at(const struct view *v, double x, double y);
/* the page's scroll that brings field f above the bar over the keyboard (whole rows) */
double view_scroll_to(const struct view *v, const struct vfield *f);
const struct vfield *view_field_of(const struct view *v, uint32_t entry);
/* A stroke drawn into an agenda's free time f (FR-018), its points in f's coordinates. A
 * tall narrow stroke over an hour or more is the block drawn over the event's time (true):
 * start and end are its extent in 30-minute steps. Any other stroke is writing (false): the
 * event starts at the half hour it starts in and lasts an hour */
bool view_free_block(const struct vfield *f, const struct pl_point *p, int n, int *start, int *end);
/* The event that stroke makes, laid out at once (relayout lays out v again): a block's
 * stroke goes; writing's stroke is the title's first ink (*kept), stroke k of the event's
 * field, moved into it as the layout placed it, so it stays where it was written. NULL: none */
struct entry *view_free_stroke(struct view *v, struct store *s, const struct vfield *f, uint32_t k,
			       const struct pl_point *p, int n, void (*relayout)(void *ud), void *ud, bool *kept);
/* the slip of event id as laid out (NULL: none) */
const struct vslip *view_slip(const struct view *v, uint32_t id);
/* bring event id to the top of its stack (view state) */
void view_raise(struct view *v, uint32_t id);
/* A pen stroke (page coordinates) that crosses out a slip (FR-018): it starts and ends
 * outside the slip, runs through it, and goes from edge to edge (from outside its left edge
 * to past its right edge, either way) or between opposite corners. The slip it crosses out
 * (the topmost, when it crosses several), else 0: a stroke that starts inside a slip is
 * writing */
uint32_t view_cross_out(const struct view *v, const struct pl_point *p, int n);
/* under an overlay: the field takes no strokes */
bool view_covered(const struct view *v, const struct vfield *f);
/* a choice of template as the picker and the year list name it: from the period of kind k at
 * day from to the one at day to (to < 0: on from there, to == from: that period only), its year
 * added where it isn't shown_year: "weeks 1–13 · Jan–Mar", "from week 14 on · from Apr",
 * "week 20", "12 Apr to 23 Apr", "from April 2027 on" */
void view_choice_name(enum pkind k, int from, int to, int shown_year, char *o, size_t n);

#endif
