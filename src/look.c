// SPDX-License-Identifier: GPL-2.0-or-later
/* The planner's look (look.h). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "look.h"

#define RGB(x) (0xff000000u | (x))

static UiFont font(const char *fam, int w, int it, float px, int sc, float ls, int tcase)
{
	return (UiFont){ fam, w, it, px, sc, ls, tcase };
}

void look_init(struct look *L, const char *voice)
{
	memset(L, 0, sizeof *L);
	L->voice = voice && !strcmp(voice, "editorial") ? V_EDITORIAL : voice && !strcmp(voice, "stationery") ? V_STATIONERY : V_WHITE;
	L->uv = ui_voice_find(voice);
	/* the E-ink scheme (web/tokens.css: eink, stationery-eink) */
	L->paper = RGB(0xffffff);
	L->ink = RGB(0x111111);
	L->soft = RGB(0x444444);
	L->rule = RGB(0xbbbbbb);
	L->shade = RGB(0xbbbbbb);
	L->highlight = RGB(0xcccccc);
	L->pen = RGB(0x111111);
	L->gilt = RGB(0xbbbbbb);
	L->bar_bg = L->paper;
	L->bar_fg = L->ink;
	L->mono = "IBM Plex Mono";
	L->small = font(L->mono, 500, 0, 10.5f, 0, 0, UI_CASE_NONE);
	switch (L->voice) {
	case V_WHITE:
		L->text = L->head = "IBM Plex Sans";
		L->label = font(L->mono, 500, 0, 11.5f, 0, 0.08f, UI_CASE_UPPER);
		L->wlabel = font(L->mono, 600, 0, 11, 0, 0.08f, UI_CASE_UPPER);
		L->tag = font(L->mono, 400, 0, 11, 0, 0, UI_CASE_NONE);
		L->h1 = font(L->head, 600, 0, 22, 0, -0.02f, UI_CASE_NONE);
		L->sub = font(L->text, 400, 0, 13, 0, 0, UI_CASE_NONE);
		L->num = font(L->head, 600, 0, 14, 0, 0, UI_CASE_NONE);
		L->more = font(L->mono, 500, 0, 12, 0, 0, UI_CASE_NONE);
		L->ui_r = 12;
		L->card_r = 16;
		L->wg_r = 16;
		L->edge = 1.5;
		L->wg_edge = L->ink;
		L->row_w = 1, L->row_c = L->ink, L->row_dash = 2;
		L->bar_rule = 1.5;
		L->tick_shape = 0;
		break;
	case V_EDITORIAL:
		L->text = L->head = "Source Serif 4";
		L->label = font(L->text, 400, 0, 16, 1, 0.1f, UI_CASE_LOWER);
		L->wlabel = font(L->text, 400, 0, 15, 1, 0.1f, UI_CASE_LOWER);
		L->tag = font(L->text, 400, 1, 13, 0, 0, UI_CASE_NONE);
		L->h1 = font(L->head, 600, 0, 22, 0, -0.01f, UI_CASE_NONE);
		L->sub = font(L->text, 400, 0, 13, 0, 0, UI_CASE_NONE);
		L->num = font(L->head, 400, 1, 17, 0, 0, UI_CASE_NONE);
		L->more = font(L->text, 400, 1, 14, 0, 0, UI_CASE_NONE);
		L->edge = 1;
		L->wg_edge = L->ink;
		L->row_w = 1, L->row_c = L->rule, L->row_dash = 0;
		L->bar_rule = 3;
		L->tick_shape = 1;
		L->ev_bar = 1;
		break;
	default:
		L->text = L->head = "EB Garamond";
		L->label = font(L->text, 400, 0, 17, 1, 0.1f, UI_CASE_LOWER);
		L->wlabel = font(L->text, 400, 0, 15, 1, 0.1f, UI_CASE_LOWER);
		L->tag = font(L->text, 400, 1, 13, 0, 0, UI_CASE_NONE);
		L->h1 = font(L->head, 500, 1, 22, 0, 0, UI_CASE_NONE);
		L->sub = font(L->text, 400, 1, 14, 0, 0, UI_CASE_NONE);
		L->num = font(L->head, 400, 1, 17, 0, 0, UI_CASE_NONE);
		L->more = font(L->text, 400, 1, 14, 0, 0, UI_CASE_NONE);
		L->ui_r = 6;
		L->card_r = 3;
		L->wg_r = 3;
		L->edge = 1;
		L->wg_edge = L->soft;
		L->row_w = 1, L->row_c = L->rule, L->row_dash = 0;
		L->bar_rule = 0;
		L->tick_shape = 2;
		/* stationery-eink: the leather bar is ink, its type gilt */
		L->bar_bg = RGB(0x111111);
		L->bar_fg = L->gilt;
		break;
	}
	L->body = font(L->text, 400, 0, 15, 0, 0, UI_CASE_NONE);
}

void look_voice_name(char *out, int len)
{
	snprintf(out, len, "white");
	const char *e = getenv("GLASSINE_PLANNER_VOICE");
	if (e && *e) {
		snprintf(out, len, "%s", e);
		return;
	}
	char p[1200];
	const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
	if (x && *x)
		snprintf(p, sizeof p, "%s/glassine-shell/theme", x);
	else
		snprintf(p, sizeof p, "%s/.config/glassine-shell/theme", h ? h : "");
	FILE *f = fopen(p, "re");
	if (f) {
		char name[32];
		if (fscanf(f, "%31s", name) == 1)
			snprintf(out, len, "%s", name);
		fclose(f);
	}
}

/* ---- drawing ---- */

static void src(cairo_t *cr, ui_col c)
{
	cairo_set_source_rgba(cr, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0, (c & 255) / 255.0, (c >> 24 & 255) / 255.0);
}

void lk_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	cairo_new_sub_path(cr);
	if (r <= 0) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

void lk_fill(cairo_t *cr, ui_col c)
{
	src(cr, c);
	cairo_fill(cr);
}

/* an inset keyline (CSS inset box-shadow): lw inside the box */
void lk_stroke_box(cairo_t *cr, double x, double y, double w, double h, double r, double lw, ui_col c)
{
	cairo_save(cr);
	lk_rect(cr, x + lw / 2, y + lw / 2, w - lw, h - lw, r > lw / 2 ? r - lw / 2 : 0);
	src(cr, c);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void lk_rule(cairo_t *cr, double x, double y, double w, double lw, ui_col c, int dash)
{
	if (lw <= 0 || w <= 0)
		return;
	cairo_save(cr);
	src(cr, c);
	if (dash == 0) {
		cairo_rectangle(cr, x, y, w, lw);
		cairo_fill(cr);
	} else {
		double on = dash == 2 ? lw : 3 * lw, off = dash == 2 ? lw * 1.5 : 2 * lw;
		/* dotted (CSS): square dots one line width wide */
		for (double p = x; p < x + w; p += on + off) {
			cairo_rectangle(cr, p, y, fmin(on, x + w - p), lw);
		}
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* ---- the design's icons: SVG path data (M L H V C S Q A Z, absolute and relative) ---- */

static void arc_seg(cairo_t *cr, double x0, double y0, double rx, double ry, double phi, int large, int sweep, double x,
		    double y)
{
	if (rx == 0 || ry == 0) {
		cairo_line_to(cr, x, y);
		return;
	}
	double cp = cos(phi), sp = sin(phi);
	double dx = (x0 - x) / 2, dy = (y0 - y) / 2;
	double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
	rx = fabs(rx), ry = fabs(ry);
	double l = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
	if (l > 1)
		rx *= sqrt(l), ry *= sqrt(l);
	double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
	double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	double co = sqrt(fmax(0, num / den)) * (large == sweep ? -1 : 1);
	double cx1 = co * rx * y1 / ry, cy1 = -co * ry * x1 / rx;
	double cx = cp * cx1 - sp * cy1 + (x0 + x) / 2, cy = sp * cx1 + cp * cy1 + (y0 + y) / 2;
	double t1 = atan2((y1 - cy1) / ry, (x1 - cx1) / rx), t2 = atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx);
	double dt = t2 - t1;
	if (sweep && dt < 0)
		dt += 2 * M_PI;
	else if (!sweep && dt > 0)
		dt -= 2 * M_PI;
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, phi);
	cairo_scale(cr, rx, ry);
	if (dt > 0)
		cairo_arc(cr, 0, 0, 1, t1, t1 + dt);
	else
		cairo_arc_negative(cr, 0, 0, 1, t1, t1 + dt);
	cairo_restore(cr);
}

static int num(const char **p, double *v)
{
	const char *s = *p;
	while (*s == ' ' || *s == ',')
		s++;
	char *e;
	*v = strtod(s, &e);
	if (e == s)
		return 0;
	*p = e;
	return 1;
}

static int flag(const char **p, int *v)
{
	const char *s = *p;
	while (*s == ' ' || *s == ',')
		s++;
	if (*s != '0' && *s != '1')
		return 0;
	*v = *s - '0';
	*p = s + 1;
	return 1;
}

static void path(cairo_t *cr, const char *d)
{
	double cx = 0, cy = 0, sx = 0, sy = 0, lx = 0, ly = 0;
	char cmd = 0;
	const char *p = d;
	cairo_new_path(cr);
	for (;;) {
		while (*p == ' ' || *p == ',')
			p++;
		if (!*p)
			break;
		if (isalpha((unsigned char)*p))
			cmd = *p++;
		int rel = islower((unsigned char)cmd);
		double a, b, c, e, f, g;
		int fl, fs;
		switch (toupper((unsigned char)cmd)) {
		case 'M':
			if (!num(&p, &a) || !num(&p, &b))
				return;
			if (rel)
				a += cx, b += cy;
			cairo_move_to(cr, a, b);
			cx = sx = lx = a, cy = sy = ly = b;
			cmd = rel ? 'l' : 'L';
			break;
		case 'L':
			if (!num(&p, &a) || !num(&p, &b))
				return;
			if (rel)
				a += cx, b += cy;
			if (a == cx && b == cy)
				a += 0.01; /* a dot: a round cap draws it */
			cairo_line_to(cr, a, b);
			cx = lx = a, cy = ly = b;
			break;
		case 'H':
			if (!num(&p, &a))
				return;
			if (rel)
				a += cx;
			cairo_line_to(cr, a, cy);
			cx = lx = a;
			break;
		case 'V':
			if (!num(&p, &a))
				return;
			if (rel)
				a += cy;
			cairo_line_to(cr, cx, a);
			cy = ly = a;
			break;
		case 'C':
			if (!num(&p, &a) || !num(&p, &b) || !num(&p, &c) || !num(&p, &e) || !num(&p, &f) || !num(&p, &g))
				return;
			if (rel)
				a += cx, b += cy, c += cx, e += cy, f += cx, g += cy;
			cairo_curve_to(cr, a, b, c, e, f, g);
			lx = c, ly = e, cx = f, cy = g;
			break;
		case 'S':
			if (!num(&p, &c) || !num(&p, &e) || !num(&p, &f) || !num(&p, &g))
				return;
			if (rel)
				c += cx, e += cy, f += cx, g += cy;
			a = 2 * cx - lx, b = 2 * cy - ly;
			cairo_curve_to(cr, a, b, c, e, f, g);
			lx = c, ly = e, cx = f, cy = g;
			break;
		case 'Q':
			if (!num(&p, &a) || !num(&p, &b) || !num(&p, &f) || !num(&p, &g))
				return;
			if (rel)
				a += cx, b += cy, f += cx, g += cy;
			cairo_curve_to(cr, cx + 2.0 / 3 * (a - cx), cy + 2.0 / 3 * (b - cy), f + 2.0 / 3 * (a - f),
				       g + 2.0 / 3 * (b - g), f, g);
			lx = a, ly = b, cx = f, cy = g;
			break;
		case 'A':
			if (!num(&p, &a) || !num(&p, &b) || !num(&p, &c) || !flag(&p, &fl) || !flag(&p, &fs) || !num(&p, &f) ||
			    !num(&p, &g))
				return;
			if (rel)
				f += cx, g += cy;
			arc_seg(cr, cx, cy, a, b, c * M_PI / 180, fl, fs, f, g);
			cx = lx = f, cy = ly = g;
			break;
		case 'Z':
			cairo_close_path(cr);
			cx = lx = sx, cy = ly = sy;
			break;
		default:
			return;
		}
	}
}

void lk_icon(cairo_t *cr, const char *d, double x, double y, double px, ui_col c, double sw)
{
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, px / 24, px / 24);
	path(cr, d);
	src(cr, c);
	cairo_set_line_width(cr, sw > 0 ? sw : 1.6);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);
	cairo_restore(cr);
}

const char *const IC_CHEV_L = "M15 5l-7 7 7 7";
const char *const IC_CHEV_R = "M9 5l7 7-7 7";
const char *const IC_COG = "M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z M19.4 13.4l1.7 1.3-1.9 3.3-2-.8a7.4 7.4 0 0 1-1.8 1l-.3 2.1h-3.8l-.3-2.1a7.4 7.4 0 0 1-1.8-1l-2 .8-1.9-3.3 1.7-1.3a7.6 7.6 0 0 1 0-2.8L3.3 9.3l1.9-3.3 2 .8a7.4 7.4 0 0 1 1.8-1l.3-2.1h3.8l.3 2.1a7.4 7.4 0 0 1 1.8 1l2-.8 1.9 3.3-1.7 1.3a7.6 7.6 0 0 1 0 2.8z";
const char *const IC_INFO = "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z M12 11v6 M12 7.5h.01";
const char *const IC_HIDDEN = "M3 3l18 18 M10.6 6.1A9.8 9.8 0 0 1 12 6c5 0 9 6 9 6a15.6 15.6 0 0 1-2.6 3.1 M6.6 7.6C4.3 9.2 3 12 3 12s4 6 9 6a8.7 8.7 0 0 0 4-1 M9.9 9.9a3 3 0 0 0 4.2 4.2";
const char *const IC_KEYBOARD = "M3 6h18v12H3z M7 10h.01 M11 10h.01 M15 10h.01 M7 14h10";
const char *const IC_LAYOUT = "M4 4h16v16H4z M4 10h16 M10 10v10";
const char *const IC_EDIT = "M4 20h4L19 9l-4-4L4 16z M13.5 6.5l4 4";
const char *const IC_SLIDERS = "M4 7h10 M18 7h2 M4 17h4 M12 17h8 M16 5v4 M10 15v4";
const char *const IC_SEARCH = "M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13z M15.3 15.3L20 20";
const char *const IC_HAND = "M3 21h12 M4 17c2-4 3.5-4 4.5-.5s3 2.5 5-1.5 M14 13l6.5-6.5 2 2L16 15l-3 1z";
