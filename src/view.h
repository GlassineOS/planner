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
#include "lanes.h"
#include "look.h"
#include "store.h"

/* the pen as the planner draws it and as the panel previews it while the frame is on its
 * way (glassine_ink_v1 set_brush, the same numbers): diameters in surface px at no and at
 * full pressure, pressure^PL_INK_EXP between */
#define PL_INK_WMIN 1.3
#define PL_INK_WMAX 2.5
#define PL_INK_EXP 1.0
double pl_ink_width(double pressure);

/* OV_CARD: an event's card (a tap on its title); OV_DELETE: the card that asks before an event
 * is deleted; OV_PAGE: the event's own page, the whole screen (board event.open) */
enum overlay { OV_NONE, OV_INFO, OV_MENU, OV_HIDDEN, OV_PICKER, OV_SETTINGS, OV_TITLE, OV_MOVE, OV_CARD, OV_DELETE, OV_PAGE };

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
	int sub;            /* F_ENTRY: which of the entry's fields (EF_*: an event's page has more) */
	enum ekind ekind;   /* F_NEW: what to make */
	struct period per;  /* F_NEW: its period; F_FREE: the day */
	char slot[32];
	int rank;
	double x, y, w, h;  /* surface coordinates */
	double guide, first; /* ruled lines in region coordinates (0: none) */
	int layer;
	/* F_FREE: minutes of the day at y and per px (its first hour's), and its scale: minutes to y
	 * (from the field's top) and back, taller where a group is drawn in rows */
	double min0, mpp;
	struct tmap tm;
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
	/* an agenda's event (FR-018), id the event: its start's and its end's handle (a finger's
	 * drag moves that end, held still the card that deletes it) */
	A_EV_TOP, A_EV_BOT,
	A_EV_ASK,                /* its card's Delete event: the card that asks */
	A_EV_OPEN,               /* its card's Open: its page */
	A_DEL_KEEP, A_DEL_YES,   /* the asking card: Keep (also a tap outside), Delete event */
	/* the event's page (board event.open): Back, the day (the month to pick from), a time's
	 * − or + (arg 0 start −, 1 start +, 2 end −, 3 end +), Repeat's and Remind's choices (arg
	 * the choice), Delete event (it asks first: arg 1 the dialog's Delete event, 2 its Keep) */
	A_PG_BACK, A_PG_DAY, A_PG_TIME, A_PG_REPEAT, A_PG_REMIND, A_PG_DELETE,
	A_FOLD, /* a small agenda's fold bar, arg its day: the whole day, every event in it (board event.more) */
	A_WHOLE_BACK, /* Back in the bar of the whole day opened from a fold bar: the page it was opened from */
};
struct vhit {
	double x, y, w, h;
	enum act a;
	int arg;        /* A_EVENT: 1 when its title is cut */
	uint32_t id;
	bool fixed;     /* on the bar above the keyboard: screen coordinates, never scrolled */
};

/* A timed event of a day agenda as its pair of brackets (FR-018; boards event.stack,
 * event.drag, event.title): page coordinates, as laid out */
struct vev {
	uint32_t id;
	int s0, e0;            /* its minutes on the agenda's day */
	struct lane ln;        /* its lane in its cluster */
	double y, h;           /* the brackets' rows: the top serif at y, the lower at y + h */
	double spine, w;       /* its "[" (2 px from spine - 1) and its lane's width: "]" at spine + w */
	double tx, tw, th;     /* its title's field: from tx, tw wide, th tall from y */
	double ink[4];         /* its title's ink on the page, x, y, w, h (w 0: none) */
	bool cut;              /* its title cut at the closing bracket */
};
#define V_EVS 64
#define V_FOLDS 8
#define V_RULES 160
struct vbox {
	double x, y, w, h;
};

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
	uint32_t drag; int drag_s0, drag_e0; /* an event being dragged (its brackets dashed), and where it was */
	int drag_end;       /* 0 it is moved by its title, 1 its start is held, 2 its end (that serif thickens) */
	/* the day agendas' events as laid out */
	struct vev ev[V_EVS];
	int nev;
	/* the now line as the last layout drew it (FR-018, board event.more): its y (0: none), its
	 * dot's middle, and the pieces of it left between the events and fold bars it breaks
	 * around (x, width); the events folded into a small agenda's bars */
	double now_y, now_dot;
	double now_seg[16][2];
	int nnow;
	int folded;
	struct vbox fold[V_FOLDS]; /* the fold bars */
	struct vbox rule[V_RULES]; /* the day agendas' hour, half-hour and row rules as drawn (h: their width) */
	int nrule;
	int nfold;
	/* the whole day opened from a fold bar: day whole_day shows the day agenda's template
	 * (every event of the day) until another period is chosen; Back in its bar returns to
	 * whole_from, the period the fold bar was on, at its scroll (whole_scroll, kept while
	 * the keyboard that moved the page, whole_vis, is still the same) */
	bool whole;
	int whole_day;
	struct period whole_from;
	double whole_scroll, whole_vis;
	/* the event the pen writes in, or hovers over: the other brackets of its cluster grey */
	uint32_t focus;
	/* a bracket being drawn with the pen down the hour margin (board event.draw): its day, its
	 * minutes so far (draw_on false: none) */
	bool draw_on;
	int draw_day, draw_s, draw_e;
	/* OV_DELETE: a stroke that struck the title through, page coordinates (owned by the
	 * caller), shown while the card asks; OV_PAGE: its dialog asking before Delete */
	const struct pl_point *xout;
	int nxout;
	bool page_ask;
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
/* The whole day opened from a fold bar (A_FOLD, board event.more): day shows every event in
 * the day agenda's template, and the period shown before (with its scroll) is kept for Back.
 * view_whole_shown: that day is what is shown (its bar has Back). view_whole_back: Back,
 * the page the fold bar was on again (false: no whole day shown, nothing changed). Choosing
 * any other period clears it (whole false) */
void view_whole_open(struct view *v, int day);
bool view_whole_shown(const struct view *v);
bool view_whole_back(struct view *v);
/* the laid out event id (NULL: none) */
const struct vev *view_ev(const struct view *v, uint32_t id);
/* A pen stroke (page coordinates) that strikes an event's title through (FR-018, board
 * event.delete): it starts before the title's first letter and runs past its last, along the
 * title's ink. The event (whose lane holds the stroke's start), else 0: a stroke that starts
 * inside the title, or doesn't cross all of it, is writing */
uint32_t view_strike(const struct view *v, const struct pl_point *p, int n);
/* The day agenda's hour margin at x, y (page coordinates): its free time field there (NULL:
 * none). A pen stroke down the margin draws a bracket: an event over its rows (board
 * event.draw) */
const struct vfield *view_margin_at(const struct view *v, double x, double y);
/* the event whose lane holds x, y (page coordinates) and the field it writes into there, else 0 */
uint32_t view_lane_at(const struct view *v, double x, double y);
/* the event whose bracket is nearest x, y (page coordinates) when x lies in the gap between
 * one lane's "]" and the next lane's "[" of a group at that y (the "[" at the same distance),
 * else 0 (board event.route) */
uint32_t view_gap_at(const struct view *v, double x, double y);
/* a line drawn down the hour margin of free time f from page y0 to y1 (either way): the
 * event's minutes, from the half hour it began in to the half hour it reached, at least 30
 * (board event.draw) */
void view_margin_span(const struct vfield *f, double y0, double y1, int *start, int *end);
/* under an overlay: the field takes no strokes */
bool view_covered(const struct view *v, const struct vfield *f);
/* the event page's Remind choices, in minutes before its start (-1: none) */
extern const int VIEW_REMIND_MIN[5];
/* a choice of template as the picker and the year list name it: from the period of kind k at
 * day from to the one at day to (to < 0: on from there, to == from: that period only), its year
 * added where it isn't shown_year: "weeks 1–13 · Jan–Mar", "from week 14 on · from Apr",
 * "week 20", "12 Apr to 23 Apr", "from April 2027 on" */
void view_choice_name(enum pkind k, int from, int to, int shown_year, char *o, size_t n);

#endif
