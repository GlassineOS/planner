// SPDX-License-Identifier: GPL-2.0-or-later
/* The frame: a stroke repaints only its field, and the result is the frame a whole repaint
 * makes; the damage is the tiles that changed, inside that field. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/frame.h"
#include "../src/view.h"
#include "sample.h"
#include "t.h"

struct page {
	struct view *v;
	struct store *s;
	const struct look *L;
};

static void paint(void *ud, cairo_t *cr)
{
	struct page *p = ud;
	view_paint(p->v, p->s, p->L, cr);
}

/* as the planner paints on the device: twice the size */
static void paint2(void *ud, cairo_t *cr)
{
	cairo_scale(cr, 2, 2);
	paint(ud, cr);
}

/* how far two frames differ: the pixels that differ at all, and the most one channel does */
static void frame_delta(const uint32_t *a, const uint32_t *b, size_t n, size_t *px, int *most)
{
	*px = 0, *most = 0;
	for (size_t i = 0; i < n; i++) {
		if (a[i] == b[i])
			continue;
		(*px)++;
		for (int sh = 0; sh < 24; sh += 8) {
			int d = abs((int)(a[i] >> sh & 255) - (int)(b[i] >> sh & 255));
			*most = d > *most ? d : *most;
		}
	}
}

/* the page with painted ink kept, then painted anew: the same frame, to rounding */
static void ink_kept(void (*pf)(void *, cairo_t *), struct page *pg, int W, int H)
{
	struct frect runs[64], box;
	struct frame a = { 0 }, b = { 0 };
	view_ink_cache(true);
	frame_update(&a, W, H, NULL, pf, pg, runs, 64, &box); /* kept here */
	frame_free(&a);
	a = (struct frame){ 0 };
	frame_update(&a, W, H, NULL, pf, pg, runs, 64, &box); /* painted from what was kept */
	view_ink_cache(false);
	frame_update(&b, W, H, NULL, pf, pg, runs, 64, &box);
	size_t px;
	int most;
	frame_delta(a.cur, b.cur, (size_t)W * H, &px, &most);
	if (getenv("T_VERBOSE"))
		fprintf(stderr, "ink kept %dx%d: %zu pixels differ, by at most %d\n", W, H, px, most);
	CHECK(px < (size_t)W * H / 2000);
	CHECK(most <= 16);
	view_ink_cache(true);
	frame_free(&a);
	frame_free(&b);
}

/* one pixel at (dot_x, dot_y), on white; dot_x < 0: none */
static int dot_x = -1, dot_y;
static void paint_dot(void *ud, cairo_t *cr)
{
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	if (dot_x >= 0) {
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_rectangle(cr, dot_x, dot_y, 1, 1);
		cairo_fill(cr);
	}
}

int main(void)
{
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	struct store s;
	store_init(&s, NULL);
	sample_fill(&s);
	struct look L;
	look_init(&L, "white");
	int today = day_today(), W = 702, H = 884;
	struct view v = { .per = period_of(P_DAY, today), .today = today, .W = W, .H = H, .hidden_kind = -1 };
	cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	cairo_t *mc = cairo_create(ms);
	view_layout(&v, &s, &L, mc);
	struct page pg = { &v, &s, &L };
	struct frame f = { 0 };
	struct frect runs[64], box;

	/* the first frame: all of it */
	int n = frame_update(&f, W, H, NULL, paint, &pg, runs, 64, &box);
	CHECK_INT(n, 1);
	CHECK(box.w == W && box.h == H);
	/* nothing changed: no damage, whole or in part */
	n = frame_update(&f, W, H, NULL, paint, &pg, runs, 64, &box);
	CHECK_INT(n, 0);
	CHECK_INT(box.w, 0);

	/* a stroke into a to-do's field */
	const struct vfield *fd = NULL;
	for (int i = 0; i < v.nf && !fd; i++) {
		struct entry *e = v.f[i].k == F_ENTRY ? store_get(&s, v.f[i].entry) : NULL;
		if (e && e->kind == E_TODO)
			fd = &v.f[i];
	}
	CHECK(fd != NULL);
	if (!fd)
		return t_done("frame");
	struct entry *e = store_get(&s, fd->entry);
	struct pl_point pt[3] = { { 200, 10, 0.2f, 0 }, { 230, 20, 0.6f, 8 }, { 260, 12, 0.9f, 16 } };
	field_add_stroke(&e->f, 900, pt, 3);
	struct frect r = { (int)fd->x, (int)fd->y, (int)fd->w, (int)fd->h };
	n = frame_update(&f, W, H, &r, paint, &pg, runs, 64, &box);
	CHECK(n > 0);
	/* the damage: inside the field (widened to whole tiles), where the stroke is */
	CHECK(box.x >= r.x / FRAME_TILE * FRAME_TILE && box.y >= r.y / FRAME_TILE * FRAME_TILE);
	CHECK(box.x + box.w <= r.x + r.w + FRAME_TILE && box.y + box.h <= r.y + r.h + FRAME_TILE);
	CHECK(box.x <= (int)fd->x + 200 && box.x + box.w >= (int)fd->x + 260);
	CHECK(box.w < W / 2); /* not the whole row of the page */
	for (int i = 0; i < n; i++)
		CHECK(runs[i].x >= box.x && runs[i].y >= box.y && runs[i].x + runs[i].w <= box.x + box.w &&
		      runs[i].y + runs[i].h <= box.y + box.h);
	/* the part repainted is what a whole repaint makes */
	struct frame ref = { 0 };
	frame_update(&ref, W, H, NULL, paint, &pg, runs, 64, &box);
	CHECK(memcmp(ref.cur, f.cur, (size_t)W * H * 4) == 0);
	/* and the same part again: nothing changed */
	n = frame_update(&f, W, H, &r, paint, &pg, runs, 64, &box);
	CHECK_INT(n, 0);
	/* a rect outside the frame repaints nothing */
	struct frect out = { W + 10, 0, 20, 20 };
	CHECK_INT(frame_update(&f, W, H, &out, paint, &pg, runs, 64, &box), 0);
	/* many changed tiles past max: one run, their box */
	field_remove_stroke(&e->f, 900);
	n = frame_update(&f, W, H, NULL, paint, &pg, runs, 1, &box);
	CHECK(n == 1 && runs[0].w == box.w && runs[0].h == box.h);
	/* the stroke's width follows its pressure, and matches the panel's preview */
	CHECK(pl_ink_width(0) == PL_INK_WMIN && pl_ink_width(1) == PL_INK_WMAX && pl_ink_width(0.5) > PL_INK_WMIN);
	struct frect a = { 0, 0, 0, 0 }, b = { 4, 5, 6, 7 };
	CHECK(frect_union(a, b).w == 6 && frect_union(b, (struct frect){ 0, 0, 2, 2 }).w == 10);
	/* the (i) popover opened, then closed: repainting the bar and its rectangle (as the
	 * planner does) makes the frame a whole repaint makes */
	for (int k = 0; k < 2; k++) {
		double px = v.pop_x, py = v.pop_y, pw = v.pop_w, ph = v.pop_h;
		v.ov = k == 0 ? OV_INFO : OV_NONE;
		view_layout(&v, &s, &L, mc);
		if (k == 0)
			px = v.pop_x, py = v.pop_y, pw = v.pop_w, ph = v.pop_h;
		CHECK(pw > 0 && ph > 0);
		struct frect pr = frect_union((struct frect){ 0, 0, W, V_BAR + 8 },
					      (struct frect){ (int)px - 8, (int)py - 8, (int)pw + 16, (int)ph + 16 });
		n = frame_update(&f, W, H, &pr, paint, &pg, runs, 64, &box);
		CHECK(n > 0 && box.w < W * 3 / 4);
		struct frame whole = { 0 };
		frame_update(&whole, W, H, NULL, paint, &pg, runs, 64, &box);
		CHECK(memcmp(whole.cur, f.cur, (size_t)W * H * 4) == 0);
		frame_free(&whole);
	}
	/* a single changed pixel is found anywhere, in a frame of an odd width (the last pixel
	 * of a row, a tile cut by the edge) */
	{
		struct frame d = { 0 };
		static const int at[][2] = { { 0, 0 }, { 16, 3 }, { 99, 50 }, { 100, 50 }, { 100, 60 }, { 17, 61 } };
		dot_x = -1;
		frame_update(&d, 101, 61, NULL, paint_dot, NULL, runs, 64, &box);
		for (size_t k = 0; k < sizeof at / sizeof *at; k++) {
			dot_x = at[k][0], dot_y = at[k][1] < 61 ? at[k][1] : 60;
			n = frame_update(&d, 101, 61, NULL, paint_dot, NULL, runs, 64, &box);
			CHECK_INT(n, 1);
			CHECK(box.x <= dot_x && dot_x < box.x + box.w && box.y <= dot_y && dot_y < box.y + box.h);
			dot_x = -1;
			n = frame_update(&d, 101, 61, NULL, paint_dot, NULL, runs, 64, &box);
			CHECK_INT(n, 1);
		}
		frame_free(&d);
	}
	/* painted ink kept: the frame the strokes make, at the planner's size and twice it, and a
	 * field's new stroke shows where its kept ink was painted (no stale ink) */
	{
		int inked = 0;
		for (int i = 0; i < v.nf; i++) {
			struct entry *e = v.f[i].k == F_ENTRY ? store_get(&s, v.f[i].entry) : NULL;
			inked += e && e->f.ns > 0;
		}
		CHECK(inked > 0);
		ink_kept(paint, &pg, W, H);
		ink_kept(paint2, &pg, W * 2, H * 2);
		struct frame k = { 0 };
		view_ink_cache(true);
		frame_update(&k, W * 2, H * 2, NULL, paint2, &pg, runs, 64, &box);
		struct entry *e = store_get(&s, fd->entry);
		uint32_t gen = e->f.gen;
		struct pl_point pt[2] = { { 40, 4, 0.7f, 0 }, { 90, 6, 0.7f, 30 } };
		field_add_stroke(&e->f, 4242, pt, 2);
		CHECK(e->f.gen != gen);
		n = frame_update(&k, W * 2, H * 2, NULL, paint2, &pg, runs, 64, &box);
		CHECK(n > 0);
		CHECK(box.x <= (int)(fd->x + 40) * 2 && box.x + box.w >= (int)(fd->x + 90) * 2);
		ink_kept(paint2, &pg, W * 2, H * 2);
		frame_free(&k);
	}
	frame_free(&f);
	frame_free(&ref);
	cairo_destroy(mc);
	cairo_surface_destroy(ms);
	store_free(&s);
	return t_done("frame");
}
