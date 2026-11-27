/* SPDX-License-Identifier: GPL-2.0-or-later
 * The planner's look in each voice, on e-ink: the design's tokens (GlassineOS/ui web/tokens.css
 * and web/bundle.css, scheme E-ink) and the planner boards' own (designs/planner/planner.css),
 * as values for cairo. Nothing here is a decision of its own: change the boards first. */
#ifndef PL_LOOK_H
#define PL_LOOK_H
#include <cairo.h>
#include <glassine-ui.h>

enum { V_WHITE, V_EDITORIAL, V_STATIONERY };

struct look {
	int voice;
	const UiVoice *uv;
	ui_col paper, ink, soft, rule, shade, highlight, pen, gilt;
	ui_col bar_bg, bar_fg;
	const char *text, *head, *mono;
	UiFont label;      /* --v-label */
	UiFont wlabel;     /* a widget's head */
	UiFont tag;        /* a widget's tag, ink-soft */
	UiFont h1, sub;    /* the bar's title and its line under it */
	UiFont num;        /* a priority's rank */
	UiFont more;       /* "+n more" */
	UiFont small;      /* mono 10.5: hours, weekdays */
	UiFont body;       /* the popover's rows, 15px */
	double ui_r, card_r, wg_r;
	double edge;       /* a control's or a widget's keyline */
	ui_col wg_edge;
	double row_w; ui_col row_c; int row_dash;    /* --v-row-rule */
	double bar_rule;   /* 1.5 single, 3 double (editorial), 0 (stationery: a gilt line) */
	int tick_shape;    /* 0 rounded, 1 square, 2 round */
	int ev_bar;        /* editorial: a 3 px bar inside the block's left edge */
};

void look_init(struct look *L, const char *voice);
/* the person's voice: $GLASSINE_PLANNER_VOICE, else the shell's choice
 * ($XDG_CONFIG_HOME or ~/.config, glassine-shell/theme), else white */
void look_voice_name(char *out, int len);

/* drawing helpers in logical px */
void lk_rect(cairo_t *cr, double x, double y, double w, double h, double r);
void lk_fill(cairo_t *cr, ui_col c);
void lk_stroke_box(cairo_t *cr, double x, double y, double w, double h, double r, double lw, ui_col c);
void lk_rule(cairo_t *cr, double x, double y, double w, double lw, ui_col c, int dash);
/* a filled box at whole device pixels (a bracket's line) */
void lk_bar(cairo_t *cr, double x, double y, double w, double h, ui_col c);
/* the design's line icons (24-unit SVG path data), px square at x, y, stroke 1.6 */
void lk_icon(cairo_t *cr, const char *path, double x, double y, double px, ui_col c, double sw);
extern const char *const IC_CHEV_L, *const IC_CHEV_R, *const IC_COG, *const IC_INFO, *const IC_HIDDEN,
	*const IC_KEYBOARD, *const IC_LAYOUT, *const IC_EDIT, *const IC_SLIDERS, *const IC_SEARCH, *const IC_HAND;

#endif
