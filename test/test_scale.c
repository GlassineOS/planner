// SPDX-License-Identifier: GPL-2.0-or-later
/* The size of the interface (GlassineOS/shell spec 012): the planner draws at its output's
 * device pixels per design pixel.
 * - At a whole scale its own boxes and rules are the values as given: the same pixels as
 *   cairo alone draws for them.
 * - At a fractional scale a rule is whole device pixels of its colour alone, never under
 *   one, and a box's straight edges are between two pixels (no pixel a mix of two colours).
 * - On a display of 8.943 pixels per millimetre at the interface size Default (two device
 *   pixels per design pixel) with the text size 100 %, a page is the pixels it is without
 *   any of it; a larger text size draws other pixels and a taller header bar. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/view.h"
#include "sample.h"
#include "t.h"

#define SW 240
#define SH 120
static const ui_col INK = 0xff111111u, PAPER = 0xffffffffu;

static cairo_t *sheet(double s, cairo_surface_t **sf)
{
	*sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)ceil(SW * s), (int)ceil(SH * s));
	cairo_t *cr = cairo_create(*sf);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_scale(cr, s, s);
	return cr;
}

static uint32_t px(cairo_surface_t *sf, int x, int y)
{
	uint32_t v;
	cairo_surface_flush(sf);
	memcpy(&v, cairo_image_surface_get_data(sf) + (size_t)y * cairo_image_surface_get_stride(sf) + x * 4, 4);
	return v | 0xff000000u;
}

static bool same(cairo_surface_t *a, cairo_surface_t *b)
{
	cairo_surface_flush(a);
	cairo_surface_flush(b);
	int h = cairo_image_surface_get_height(a), st = cairo_image_surface_get_stride(a);
	return h == cairo_image_surface_get_height(b) && st == cairo_image_surface_get_stride(b) &&
	       !memcmp(cairo_image_surface_get_data(a), cairo_image_surface_get_data(b), (size_t)h * st);
}

/* the shapes of the test: a keyline box, a filled box, a solid and a dotted rule */
static void shapes(cairo_t *cr)
{
	lk_stroke_box(cr, 10.3, 8.6, 81.4, 40.7, 0, 1.5, INK);
	lk_rect(cr, 110.3, 8.6, 81.4, 40.7, 0);
	lk_fill(cr, INK);
	lk_rule(cr, 10.3, 70.3, 180.4, 1, INK, 0);
	lk_rule(cr, 10.3, 90.3, 180.4, 1.5, INK, 2);
}
/* the same with cairo alone, as given */
static void shapes_plain(cairo_t *cr)
{
	cairo_set_source_rgb(cr, 0x11 / 255.0, 0x11 / 255.0, 0x11 / 255.0);
	cairo_rectangle(cr, 10.3 + 0.75, 8.6 + 0.75, 81.4 - 1.5, 40.7 - 1.5);
	cairo_set_line_width(cr, 1.5);
	cairo_stroke(cr);
	cairo_rectangle(cr, 110.3, 8.6, 81.4, 40.7);
	cairo_fill(cr);
	cairo_rectangle(cr, 10.3, 70.3, 180.4, 1);
	cairo_fill(cr);
	for (double p = 10.3; p < 10.3 + 180.4; p += 1.5 + 1.5 * 1.5)
		cairo_rectangle(cr, p, 90.3, fmin(1.5, 10.3 + 180.4 - p), 1.5);
	cairo_fill(cr);
}

/* the pixels with ink on a column's rows [y0, y1): how many, and whether each is the colour alone */
static int rows(cairo_surface_t *sf, int x, int y0, int y1, bool *pure)
{
	int n = 0;
	*pure = true;
	for (int y = y0; y < y1; y++) {
		uint32_t v = px(sf, x, y);
		if (v == PAPER)
			continue;
		n++;
		*pure = *pure && v == INK;
	}
	return n;
}
static int cols(cairo_surface_t *sf, int y, int x0, int x1, bool *pure)
{
	int n = 0;
	*pure = true;
	for (int x = x0; x < x1; x++) {
		uint32_t v = px(sf, x, y);
		if (v == PAPER)
			continue;
		n++;
		*pure = *pure && v == INK;
	}
	return n;
}

static void whole(double s)
{
	cairo_surface_t *a, *b;
	cairo_t *ca = sheet(s, &a), *cb = sheet(s, &b);
	shapes(ca);
	shapes_plain(cb);
	CHECK(same(a, b));
	cairo_destroy(ca), cairo_destroy(cb);
	cairo_surface_destroy(a), cairo_surface_destroy(b);
}

/* a length in device pixels: whole, one or more */
static int dev(double len, double s)
{
	int d = (int)floor(len * s + 0.5);
	return d < 1 ? 1 : d;
}

static void fractional(double s)
{
	cairo_surface_t *a;
	cairo_t *cr = sheet(s, &a);
	shapes(cr);
	bool pure;
	int sw = cairo_image_surface_get_width(a), sh = cairo_image_surface_get_height(a);
	/* the keyline box: down and across its middle, two lines of whole pixels each */
	int n = rows(a, (int)(50 * s), 0, (int)(60 * s), &pure);
	CHECK(pure);
	CHECK_INT(n, 2 * dev(1.5, s));
	n = cols(a, (int)(29 * s), 0, (int)(100 * s), &pure);
	CHECK(pure);
	CHECK_INT(n, 2 * dev(1.5, s));
	/* the filled box: its edges between two pixels */
	n = rows(a, (int)(150 * s), 0, (int)(60 * s), &pure);
	CHECK(pure);
	CHECK(abs(n - (int)lround(40.7 * s)) <= 1);
	cols(a, (int)(29 * s), (int)(100 * s), sw, &pure);
	CHECK(pure);
	/* the solid rule: whole device pixels of its colour, one or more; its ends too */
	n = rows(a, (int)(100 * s), (int)(60 * s), (int)(80 * s), &pure);
	CHECK(pure);
	CHECK_INT(n, dev(1, s));
	int ry = -1;
	for (int y = (int)(60 * s); y < (int)(80 * s) && ry < 0; y++)
		if (px(a, (int)(100 * s), y) != PAPER)
			ry = y;
	CHECK(ry >= 0);
	if (ry >= 0) {
		cols(a, ry, 0, sw, &pure);
		CHECK(pure);
	}
	/* the dotted rule: square dots of whole device pixels (the last is cut at the rule's end) */
	int dy = -1, dots = 0;
	for (int y = (int)(80 * s); y < sh && dy < 0; y++)
		for (int x = 0; x < sw && dy < 0; x++)
			if (px(a, x, y) != PAPER)
				dy = y;
	CHECK(dy >= 0);
	for (int x = 0, run = 0; dy >= 0 && x < (int)(180 * s); x++) {
		uint32_t v = px(a, x, dy);
		CHECK(v == PAPER || v == INK);
		if (v == INK)
			run++;
		else if (run) {
			CHECK_INT(run, dev(1.5, s));
			run = 0, dots++;
		}
	}
	CHECK(dots > 20);
	n = rows(a, (int)lround(10.3 * s), (int)(80 * s), sh, &pure);
	CHECK(pure);
	CHECK_INT(n, dev(1.5, s));
	cairo_destroy(cr);
	cairo_surface_destroy(a);
}

/* a day's page in a voice at two device pixels per design pixel, laid out and painted as the
 * program does; *dark: how much ink it has */
static cairo_surface_t *page(struct store *s, const char *voice, long *dark)
{
	struct look L;
	look_init(&L, voice);
	int today = day_today();
	struct view v = { .per = period_of(P_DAY, today), .today = today, .W = 702, .H = 896 };
	view_ink_cache(true);
	view_layout_at(&v, s, &L, 2);
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1404, 1792);
	cairo_t *cr = cairo_create(sf);
	cairo_scale(cr, 2, 2);
	view_paint(&v, s, &L, cr);
	cairo_destroy(cr);
	*dark = 0;
	for (int y = 0; y < 1792; y += 3)
		for (int x = 0; x < 1404; x++)
			*dark += (px(sf, x, y) & 255) < 128;
	return sf;
}

int main(void)
{
	whole(1);
	whole(2);
	whole(3);
	static const double F[] = { 0.8, 1.6, 1.8, 2.4, 1.25, 2.6667 };
	for (unsigned i = 0; i < sizeof F / sizeof *F; i++)
		fractional(F[i]);

	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	setenv("GLASSINE_PLANNER_NOW", "860", 1);
	struct store s;
	store_init(&s, NULL);
	sample_fill(&s);
	static const char *VOICES[] = { "white", "editorial", "stationery" };
	for (int vi = 0; vi < 3; vi++) {
		const UiVoice *uv = ui_voice_find(VOICES[vi]);
		long d0, d1, d2;
		ui_metrics_set(0, 1, 0, 0);
		cairo_surface_t *plain = page(&s, VOICES[vi], &d0);
		/* 1404 x 1872 pixels on 157 x 209 mm at Default: the factor is 2 */
		double density = 1404 / 157.0, f = ui_factor(density, 100);
		CHECK(f == 2);
		ui_metrics_voice(uv, ui_mm_per_px(f, density), 1.0);
		cairo_surface_t *def = page(&s, VOICES[vi], &d1);
		CHECK(same(plain, def));
		CHECK(view_bar_h() == 60);
		ui_metrics_voice(uv, ui_mm_per_px(f, density), 1.5);
		cairo_surface_t *big = page(&s, VOICES[vi], &d2);
		CHECK(!same(plain, big));
		CHECK(view_bar_h() > 60);
		(void)d1, (void)d2;
		cairo_surface_destroy(plain), cairo_surface_destroy(def), cairo_surface_destroy(big);
	}
	ui_metrics_set(0, 1, 0, 0);
	store_free(&s);
	return t_done("scale");
}
