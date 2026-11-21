/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * glassine-planner (spike): the core. Takes sizes, input and time; gives back damage
 * rectangles to commit and the text-input state. wl.c (Wayland) and the tests only
 * feed it. Painting follows GlassineOS/home: every paint draws the whole view into a
 * fresh canvas, compares it with the last one in tiles and damages, per widget, the
 * box of the tiles that changed. Ink is the exception: a stroke's segments are drawn
 * straight onto the shown canvas and only their boxes are damaged.
 *
 * Every action is counted in the ledger: how many commits it caused and how much of the
 * panel they covered (the e-ink cost, counted on the host: not measured panel refreshes).
 */
#ifndef PL_PLAN_H
#define PL_PLAN_H

#include <stdbool.h>
#include <cairo.h>
#include <glassine-ui.h>

#include "layout.h"
#include "model.h"
#include "tmpl.h"

#define MAX_TMPL 16
#define MAX_HITS 256
#define MAX_FIELDS 128
#define MAX_WIDGETS TMPL_MAX_NODES
#define MAX_DAMAGE 32
#define PAUSE_MS 700   /* the pen's pause: the reading and a held layout wait this long */
#define STEP_MIN 30    /* FR-017: events move and stretch in 30-minute steps */
#define LEDGER_N 12

struct rect { double x, y, w, h; };

enum wtype { W_ROW, W_COL, W_AGENDA, W_TODO, W_NOTES, W_MINIMONTH, W_UNKNOWN };

/* a widget in the current view: its template node, settings and layout node */
struct widget {
	enum wtype type;
	int tn;                 /* node in the template */
	struct lay_node ln;
	/* settings (FR-042) */
	int day;                /* agenda: day offset in the view's period */
	int from, to;           /* agenda: hours shown */
	bool compact;
	enum pkind per;         /* to-do, notes: whose period */
	char title[48];
	/* the last paint */
	int more;               /* entries not shown for want of space (its more mark) */
	struct rect body;       /* agenda: the hours' area */
	double pitch;           /* agenda: px per hour */
};

enum act {
	A_NONE, A_PREV, A_NEXT, A_TODAY, A_VIEW_DAY, A_VIEW_WEEK, A_TMPL, A_TEXT_VIEW, A_DEBUG,
	A_DAY,        /* a: day */
	A_CHECK,      /* a: entry */
	A_EV_MOVE,    /* a: entry, b: widget */
	A_EV_SIZE,    /* a: entry, b: widget */
	A_AGENDA,     /* b: widget: draw a block = a new event */
	A_HIDDEN,     /* the notice of what isn't shown: next template that shows it */
	A_MORE,       /* b: widget */
};

struct hit { struct rect r; enum act act; int a, b; };

/* a handwriting field on screen: where strokes start (r), where its ink is anchored (ox, oy) */
struct fbox {
	struct rect r;
	double ox, oy;
	int entry;          /* 0: the empty row that makes a new to-do */
	enum ekind kind;
	struct period per;  /* for a new entry */
	int widget;
};

struct ledger_row {
	char what[40];
	int commits;
	double area;        /* device px damaged, summed over the commits */
	double biggest;     /* the largest commit, as a fraction of the panel */
	int layout;         /* relayouts it caused */
};

struct plan_io {
	void (*commit)(void *ud, const struct rect *r, int n); /* device px; one commit */
	/* the focused text field changed: enabled, its rectangle (logical px), its text */
	void (*text_input)(void *ud, bool on, const struct rect *r, const char *text);
	void *ud;
};

enum tool { T_PEN, T_ERASER, T_TOUCH };

struct plan {
	int W, H, S;                /* logical size, device px per logical px */
	const UiVoice *v;
	struct plan_io io;

	struct store st;
	char entries_path[1024], choices_path[1024];
	struct tmpl tm[MAX_TMPL];
	int ntm;

	int today;
	struct period per;          /* the view's period */
	const struct tmpl *cur;
	struct widget w[MAX_WIDGETS];
	int nw;
	struct lay_report rep;
	bool layout_valid;          /* rects are from a solve of the current content */
	struct lay_hold hold;
	int hidden[E_NKINDS];       /* FR-023 */
	int relayouts;

	bool text_view;             /* the whole view in text mode */
	int focus;                  /* entry whose text field has the keyboard, 0 none */
	bool debug;

	/* the last paint */
	cairo_surface_t *cur_canvas;
	struct hit hits[MAX_HITS];
	int nhits;
	struct fbox fb[MAX_FIELDS];
	int nfb;
	struct rect regions[MAX_WIDGETS + 4];
	int nregions;
	bool dirty;
	int paints, commits;

	/* input */
	bool down;
	enum tool tool;
	struct hit press;           /* the hit pressed, act A_NONE: none */
	int stroke_field;           /* fbox index, -1 none */
	int stroke_entry;
	struct stroke cur_stroke;
	double sx0, sy0, lastx, lasty;
	bool moved;
	int drag_start, drag_end;   /* an event drag or a new block: minutes */
	int drag_steps;
	long long now;

	/* the flow from ink to data (the debug overlay) */
	char flow[4][160];
	struct ledger_row led[LEDGER_N];
	int nled;                   /* rows used; led[nled-1] is the current action */
};

bool plan_init(struct plan *p, const char *tmpl_dir, const char *data_dir, const struct plan_io *io, char *err, int errlen);
void plan_free(struct plan *p);
void plan_resize(struct plan *p, int W, int H, int S);
void plan_set_voice(struct plan *p, const char *name);
void plan_set_day(struct plan *p, enum pkind k, int day);
/* the view's template: by id; false if there is none of the view's kind */
bool plan_use_template(struct plan *p, const char *id);
void plan_next_template(struct plan *p);
/* demo data (synthetic ink), for the renders and the smoke run */
void plan_seed_demo(struct plan *p);

/* input, logical px */
void plan_down(struct plan *p, enum tool t, double x, double y, double pressure);
void plan_motion(struct plan *p, double x, double y, double pressure);
void plan_up(struct plan *p);
void plan_cancel(struct plan *p);
/* text: a string typed (commit_string or a key), a backspace, enter/escape leave the field */
void plan_text(struct plan *p, const char *s);
void plan_backspace(struct plan *p);
void plan_key_done(struct plan *p);
void plan_key(struct plan *p, const char *name); /* F2: debug, ... */

/* time: paints what is due; returns ms until the next deadline (-1: none) */
int plan_tick(struct plan *p, long long now_ms);
/* paints now (when dirty) */
void plan_paint(struct plan *p);

/* the ledger: start a new action (its row collects the commits that follow) */
void plan_action(struct plan *p, const char *what);
/* the hits and fields as text (the smoke run reads it) */
void plan_dump(struct plan *p, const char *path);

/* paint.c */
void paint_view(struct plan *p, cairo_t *cr, bool measure_only);
double widget_want_h(void *ud, double w);
void paint_stroke_segment(struct plan *p, cairo_t *cr, double ox, double oy, const struct pt *a, const struct pt *b);
struct rect field_rect_for(struct plan *p, int entry);
double paint_top(struct plan *p);

#endif
