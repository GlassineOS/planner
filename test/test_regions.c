// SPDX-License-Identifier: GPL-2.0-or-later
/* The writing regions follow the fields: after every layout each live region's rectangle,
 * clip, layer, guides and reading are the field's -- an event stretched or moved, the free
 * time around it, a template or view switch, the keyboard and the page's move, an overlay --
 * and its display follows Set in Autograph (the field's own, else the settings').
 * The handwriting calls are recorded (a fake region per field). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../src/regions.h"
#include "../src/view.h"
#include "sample.h"
#include "t.h"

struct fake {
	double x, y, w, h;
	int cx, cy, cw, ch;
	int layer, reading, display;
	double guide, first;
	int nrect, nclip, ndisplay;
};
static int made, destroyed;

static struct ghw_region *f_make(void *ud, struct region *rg)
{
	struct fake *f = calloc(1, sizeof *f);
	f->layer = f->reading = f->display = -1;
	made++;
	return (struct ghw_region *)f;
}
static void f_destroy(struct ghw_region *r)
{
	destroyed++;
	free(r);
}
static void f_rect(struct ghw_region *r, double x, double y, double w, double h)
{
	struct fake *f = (struct fake *)r;
	f->x = x, f->y = y, f->w = w, f->h = h, f->nrect++;
}
static void f_clip(struct ghw_region *r, int x, int y, int w, int h)
{
	struct fake *f = (struct fake *)r;
	f->cx = x, f->cy = y, f->cw = w > 0 && h > 0 ? w : 0, f->ch = w > 0 && h > 0 ? h : 0, f->nclip++;
}
static void f_layer(struct ghw_region *r, int l) { ((struct fake *)r)->layer = l; }
static void f_guides(struct ghw_region *r, double s, double first)
{
	((struct fake *)r)->guide = s, ((struct fake *)r)->first = first;
}
static void f_reading(struct ghw_region *r, int on) { ((struct fake *)r)->reading = on; }

static void f_display(struct ghw_region *r, int set)
{
	((struct fake *)r)->display = set, ((struct fake *)r)->ndisplay++;
}

static const struct region_ops OPS = { f_make, f_destroy, f_rect, f_clip, f_layer, f_guides, f_reading, f_display, NULL };

static bool cover_all;
static bool covered(const struct vfield *f) { return cover_all; }

static struct region *find_region(struct region *list, const struct vfield *f)
{
	for (struct region *x = list; x; x = x->next)
		if (x->live && ((f->k == F_ENTRY && x->entry == f->entry) || !strcmp(x->name, f->name)))
			return x;
	return NULL;
}

/* every field has a live region, and what was last sent to it is the field */
static int mismatches(struct region *list, const struct view *v, double H)
{
	int bad = 0, live = 0;
	for (struct region *x = list; x; x = x->next)
		live += x->live;
	for (int i = 0; i < v->nf; i++) {
		const struct vfield *f = &v->f[i];
		struct region *rg = find_region(list, f);
		if (!rg) {
			bad++;
			continue;
		}
		struct fake *k = (struct fake *)rg->r;
		double y = f->y - v->scroll;
		bool want_clip = v->scroll > 0 || v->vis_h > 0;
		if (cover_all) {
			if (k->w != 0 || k->h != 0)
				bad++;
		} else if (k->x != f->x || k->y != y || k->w != f->w || k->h != f->h)
			bad++;
		if (!cover_all && want_clip) {
			double bot = v->vis_h > 0 ? v->vis_h - 46 : H, c0 = fmax(y, 0), c1 = fmin(y + f->h, bot);
			if (k->cy != (int)c0 || k->ch != (c1 > c0 ? (int)(c1 - c0) : 1) || k->cw != (int)f->w)
				bad++;
		} else if (k->cw != 0)
			bad++;
		if (k->layer != f->layer || k->reading != (f->k != F_FREE))
			bad++;
		if (fabs(k->guide - (f->guide > 0 ? f->guide : 0)) > 1e-9)
			bad++;
	}
	if (live != v->nf)
		bad++;
	return bad;
}

struct t {
	struct store s;
	struct look L;
	struct view v;
	cairo_t *cr;
	struct region *list;
};

static void relayout(struct t *t)
{
	view_layout(&t->v, &t->s, &t->L, t->cr);
	struct region_env env = { &t->v, &t->s, t->v.H, false, covered };
	regions_sync(&t->list, &env, &OPS);
}

/* the agenda's timed event's field, and its block (its tap target) */
static const struct vfield *event_field(struct t *t, uint32_t id)
{
	for (int i = 0; i < t->v.nf; i++)
		if (t->v.f[i].k == F_ENTRY && t->v.f[i].entry == id)
			return &t->v.f[i];
	return NULL;
}
static const struct vhit *event_block(struct t *t, uint32_t id)
{
	for (int i = 0; i < t->v.nh; i++)
		if (t->v.hit[i].a == A_EVENT && t->v.hit[i].id == id)
			return &t->v.hit[i];
	return NULL;
}

int main(void)
{
	setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
	struct t t = { 0 };
	store_init(&t.s, NULL);
	sample_fill(&t.s);
	look_init(&t.L, "white");
	int today = day_today();
	cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	t.cr = cairo_create(ms);
	struct period d = period_of(P_DAY, today);
	store_choose(&t.s, d, d.start, "day-lists"); /* the agenda */
	t.v = (struct view){ .per = d, .today = today, .W = 702, .H = 884, .hidden_kind = -1 };
	relayout(&t);
	CHECK(t.v.nf > 3);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);

	/* an event of the agenda, with no ink near its lower edge */
	struct entry *ev = store_add(&t.s, E_EVENT, d);
	ev->start = 13 * 60, ev->end = 14 * 60;
	sample_ink(&ev->f, "call", 2, 19, 1);
	relayout(&t);
	const struct vfield *ef = event_field(&t, ev->id);
	CHECK(ef != NULL);
	if (!ef)
		return t_done("regions");
	CHECK_INT(ef->layer, 1);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	struct region *rg = find_region(t.list, ef);
	struct fake *k = (struct fake *)rg->r;
	double h0 = k->h, y0 = k->y;
	const struct vhit *blk = event_block(&t, ev->id);
	CHECK(blk && ef->y == blk->y && ef->h == blk->h); /* the field is the block drawn */

	/* stretched an hour longer: the same region, the block's new height (lanes of 80 px: the
	 * event it then overlaps stays beside it in a lane, not in rows) */
	t.s.min_lane = 80;
	int n0 = made;
	ev->end += 60;
	relayout(&t);
	ef = event_field(&t, ev->id);
	blk = event_block(&t, ev->id);
	CHECK_INT(made, n0);
	CHECK(find_region(t.list, ef) == rg);
	CHECK(k->h > h0 + 30 && k->y == y0);
	CHECK(blk && k->h == blk->h && k->y == blk->y);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	/* shorter again (the title still fits): the region shrinks with the block */
	double h2 = k->h;
	ev->end -= 30;
	relayout(&t);
	blk = event_block(&t, ev->id);
	CHECK(k->h < h2 && blk && k->h == blk->h);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	/* shorter than its title (FR-018): the block grows to the title, and the field reaches a
	 * writing line past the ink into the free time below -- more than the block, never less */
	ev->end = ev->start + 30;
	relayout(&t);
	blk = event_block(&t, ev->id);
	ef = event_field(&t, ev->id);
	CHECK(blk && k->y == blk->y && k->h >= blk->h && k->h <= blk->h + PL_ROW);
	CHECK(blk && blk->h < h0);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	ev->end = ev->start + 60;
	/* moved (dragged) two hours later: the same height, two hours lower */
	relayout(&t);
	double hb = k->h, yb = k->y;
	ev->start += 120, ev->end += 120;
	relayout(&t);
	CHECK(fabs(k->h - hb) < 1 && k->y > yb + 60);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	/* the free time around it: one region under all the events (layer 0, no reading), the
	 * event above it */
	const struct vfield *fr = NULL;
	for (int i = 0; i < t.v.nf; i++)
		if (t.v.f[i].k == F_FREE)
			fr = &t.v.f[i];
	CHECK(fr != NULL);
	if (fr) {
		struct fake *fk = (struct fake *)find_region(t.list, fr)->r;
		CHECK(fk->layer == 0 && fk->reading == 0);
		CHECK(fk->y <= k->y && fk->y + fk->h >= k->y + k->h);
	}

	/* the keyboard up and the page moved (text mode): every region clipped to what shows;
	 * the keyboard gone with the rectangles the same: the clips go too */
	t.v.vis_h = 520;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	t.v.scroll = 2 * PL_ROW;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	t.v.scroll = 0;
	relayout(&t);
	int nc = k->nclip;
	t.v.vis_h = 0;
	relayout(&t);
	CHECK(k->nclip == nc + 1 && k->cw == 0); /* same rectangle, clip cleared */
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);

	/* an overlay over the page, then gone */
	cover_all = true;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	cover_all = false;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);

	/* another template for the day: the event in another widget (or none) */
	store_choose(&t.s, d, d.start, "day-diary");
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	store_choose(&t.s, d, d.start, "day-agenda");
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	/* the week and the month; the week straight back to the day: the event's region is
	 * kept with its ink, now an agenda block above the free time (layer 1) */
	t.v.per = period_of(P_WEEK, today);
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	t.v.per = d;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	t.v.per = period_of(P_WEEK, today);
	relayout(&t);
	t.v.per = period_of(P_MONTH, today);
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	/* turned: another width and height */
	t.v.W = 884, t.v.H = 702;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);
	t.v.per = d, t.v.W = 702, t.v.H = 884;
	relayout(&t);
	CHECK_INT(mismatches(t.list, &t.v, t.v.H), 0);

	/* Set in Autograph: off, every region shows ink; on in the settings, every entry's region
	 * and every empty row's is set, once; a field's own Off wins over the settings' On, its
	 * own On over Off; the free time is never set */
	relayout(&t);
	ef = event_field(&t, ev->id);
	rg = find_region(t.list, ef);
	k = (struct fake *)rg->r;
	CHECK_INT(k->display, 0);
	t.s.autograph = true;
	relayout(&t);
	CHECK_INT(k->display, 1);
	int nd = k->ndisplay, sets = 0, frees = 0;
	for (int i = 0; i < t.v.nf; i++) {
		struct fake *x = (struct fake *)find_region(t.list, &t.v.f[i])->r;
		if (t.v.f[i].k == F_ENTRY || t.v.f[i].k == F_NEW)
			sets += x->display == 1;
		else
			frees += x->display == 0;
	}
	int entries = 0, others = 0;
	for (int i = 0; i < t.v.nf; i++)
		t.v.f[i].k == F_ENTRY || t.v.f[i].k == F_NEW ? entries++ : others++;
	CHECK(entries > 1 && sets == entries && frees == others);
	relayout(&t);
	CHECK_INT(k->ndisplay, nd); /* unchanged: not sent again */
	ev->f.autograph = SHOW_OFF;
	relayout(&t);
	CHECK_INT(k->display, 0);
	t.s.autograph = false, ev->f.autograph = SHOW_ON;
	relayout(&t);
	CHECK_INT(k->display, 1);
	ev->f.autograph = SHOW_FOLLOW;
	relayout(&t);
	CHECK_INT(k->display, 0);

	/* a page with no fields: every region goes */
	t.v.nf = 0;
	struct region_env env = { &t.v, &t.s, t.v.H, false, covered };
	regions_sync(&t.list, &env, &OPS);
	CHECK(t.list == NULL && made == destroyed);
	cairo_destroy(t.cr);
	cairo_surface_destroy(ms);
	store_free(&t.s);
	return t_done("regions");
}
