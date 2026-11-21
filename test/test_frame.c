// SPDX-License-Identifier: GPL-2.0-or-later
/* The frame: a stroke repaints only its field, and the result is the frame a whole repaint
 * makes; the damage is the tiles that changed, inside that field. */
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
	frame_free(&f);
	frame_free(&ref);
	cairo_destroy(mc);
	cairo_surface_destroy(ms);
	store_free(&s);
	return t_done("frame");
}
