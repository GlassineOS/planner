// SPDX-License-Identifier: GPL-2.0-or-later
/* glassine-planner on plain Wayland: xdg-shell, shm buffers drawn with cairo, and the pen
 * interface's writing regions (libglassine-handwriting, GlassineOS/handwriting spec 005): every
 * handwriting field is one region, an agenda's free time one region with reading off. The pen
 * writes through the regions; a finger (wl_touch) or a pen tap outside the fields (tablet-v2)
 * works the controls. Only what changed is damaged, tile by tile, so the panel refreshes only
 * there (FR-061). */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include <glassine-handwriting.h>
#include "tablet-v2-client-protocol.h"
#include "view.h"
#include "xdg-shell-client-protocol.h"

#define TILE 16
#define PAUSE_MS 1200 /* the pause after the pen lifts before the page settles (FR-046) */

struct region {
	struct ghw_region *r;
	char name[96];
	uint32_t entry;       /* 0 until its first stroke makes one (an empty row) */
	struct vfield f;      /* the field it was made for (its kind, what to make) */
	struct fmake mk;      /* what a stroke makes when it has no entry (an empty row, or its
	                       * entry struck out and deleted before the page settles) */
	bool live;            /* still a field of the view */
	double x, y, w, h;    /* as last set */
	struct region *next;
};

struct buffer {
	struct wl_buffer *b;
	void *px;
	size_t size;
	int w, h;
	bool busy;
};

static struct {
	struct wl_display *dpy;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct wl_seat *seat;
	struct wl_touch *touch;
	struct wl_pointer *pointer;
	struct zwp_tablet_manager_v2 *tabman;
	struct wl_surface *surf;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	struct ghw_pen *pen;
	int out_scale;
	int W, H;          /* logical */
	bool configured, running, dirty, relayout;
	struct buffer buf[2];
	uint32_t *last;    /* the last frame committed (for the damage) */
	int last_w, last_h;
	struct store s;
	struct look L;
	char voice[32];
	struct view v;
	bool refused;
	struct vhit refused_close;
	struct region *regions;
	long long pen_up_ms; /* last pen-up (strokes), for the hold */
	bool pen_down;
	/* a tap: finger or pen */
	int touch_id;
	double tx, ty, px, py;
	bool tool_down;
	double tool_x, tool_y;
	/* dragging an event (finger) */
	uint32_t drag;
	int drag_s0, drag_e0, drag_mode; /* 1 move, 2 the lower edge */
	double drag_y0, drag_mpp;
	int ino, inowd;
} P = { .out_scale = 1, .touch_id = -1, .running = true };

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* ---- buffers ---- */

static void buf_release(void *d, struct wl_buffer *b)
{
	((struct buffer *)d)->busy = false;
}
static const struct wl_buffer_listener buf_listener = { buf_release };

static struct buffer *get_buffer(int w, int h)
{
	for (int i = 0; i < 2; i++) {
		struct buffer *b = &P.buf[i];
		if (b->busy)
			continue;
		if (b->b && (b->w != w || b->h != h)) {
			wl_buffer_destroy(b->b);
			munmap(b->px, b->size);
			b->b = NULL;
		}
		if (!b->b) {
			int stride = w * 4;
			b->size = (size_t)stride * h;
			int fd = memfd_create("glassine-planner", MFD_CLOEXEC);
			if (fd < 0 || ftruncate(fd, b->size) < 0) {
				if (fd >= 0)
					close(fd);
				return NULL;
			}
			b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			struct wl_shm_pool *pool = wl_shm_create_pool(P.shm, fd, b->size);
			b->b = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
			wl_shm_pool_destroy(pool);
			close(fd);
			wl_buffer_add_listener(b->b, &buf_listener, b);
			b->w = w, b->h = h;
		}
		return b;
	}
	return NULL;
}

/* ---- regions ---- */

static void save(void)
{
	if (!store_save(&P.s))
		fprintf(stderr, "glassine-planner: could not save the entries\n");
}

static struct region *region_of(struct ghw_region *r)
{
	for (struct region *x = P.regions; x; x = x->next)
		if (x->r == r)
			return x;
	return NULL;
}

static void relayout_later(void)
{
	P.relayout = true;
	P.dirty = true;
}

static void on_stroke(void *d, struct ghw_region *r, uint32_t k, const struct ghw_point *p, size_t n)
{
	struct region *rg = region_of(r);
	if (!rg || !n)
		return;
	P.pen_up_ms = now_ms();
	if (rg->f.k == F_FREE) {
		/* a block drawn in the free time: an event over that time, in 30-minute steps */
		double y0 = p[0].y, y1 = p[0].y;
		for (size_t i = 1; i < n; i++)
			y0 = fmin(y0, p[i].y), y1 = fmax(y1, p[i].y);
		int s = (int)floor((rg->f.min0 + y0 * rg->f.mpp) / 30) * 30, e = (int)ceil((rg->f.min0 + y1 * rg->f.mpp) / 30) * 30;
		if (e - s < 30)
			e = s + 30;
		struct entry *x = store_add(&P.s, E_EVENT, rg->f.per);
		if (x) {
			x->start = s;
			x->end = e;
			save();
		}
		relayout_later();
		return;
	}
	struct pl_point *q = malloc(n * sizeof *q);
	if (!q)
		return;
	for (size_t i = 0; i < n; i++)
		q[i] = (struct pl_point){ (float)p[i].x, (float)p[i].y, (float)p[i].pressure, p[i].time };
	bool made = !rg->entry || !store_get(&P.s, rg->entry);
	struct entry *e = field_stroke(&P.s, &rg->entry, &rg->mk, k, q, (int)n);
	free(q);
	if (!e)
		return;
	save(); /* FR-024: kept at once */
	P.dirty = true;
	if (made)
		relayout_later(); /* a new row to write in, after the pause */
}

static void on_value(void *d, struct ghw_region *r, uint32_t serial, const char *text, const struct ghw_word *w, size_t nw,
		     const uint32_t *gone, size_t ngone, const void *state, size_t len)
{
	struct region *rg = region_of(r);
	if (!rg || !rg->entry || !store_get(&P.s, rg->entry))
		return;
	if (field_value(&P.s, &rg->entry, text, gone, ngone, state, len))
		relayout_later(); /* all its ink went (struck out): the row is empty again */
	save();
	if (ngone)
		P.dirty = true;
}

static const struct ghw_region_listener region_listener = { .stroke = on_stroke, .value = on_value };

static bool covered(const struct vfield *f)
{
	struct view *v = &P.v;
	if (v->ov == OV_PICKER)
		return true;
	if (v->ov == OV_INFO || v->ov == OV_MENU)
		return f->x < v->pop_x + v->pop_w && f->x + f->w > v->pop_x && f->y < v->pop_y + v->pop_h + 4 && f->y + f->h > v->pop_y;
	if (v->ov == OV_HIDDEN)
		return f->y + f->h > v->H - round((v->H - 60) * 0.66);
	return false;
}

static void restore(struct region *rg, struct entry *e)
{
	if (!e->f.ns && !e->f.value)
		return;
	struct ghw_stored_stroke *st = calloc(e->f.ns ? e->f.ns : 1, sizeof *st);
	struct ghw_point **pts = calloc(e->f.ns ? e->f.ns : 1, sizeof *pts);
	if (!st || !pts) {
		free(st);
		free(pts);
		return;
	}
	for (int i = 0; i < e->f.ns; i++) {
		pts[i] = calloc(e->f.s[i].n ? e->f.s[i].n : 1, sizeof **pts);
		for (int j = 0; pts[i] && j < e->f.s[i].n; j++) {
			const struct pl_point *q = &e->f.s[i].pt[j];
			pts[i][j] = (struct ghw_point){ q->x, q->y, q->t, q->p, 0, 0 };
		}
		st[i] = (struct ghw_stored_stroke){ e->f.s[i].k, pts[i], pts[i] ? (size_t)e->f.s[i].n : 0 };
	}
	ghw_region_restore(rg->r, st, e->f.ns, e->f.value, e->f.state, e->f.state_len);
	for (int i = 0; i < e->f.ns; i++)
		free(pts[i]);
	free(pts);
	free(st);
}

static void sync_regions(void)
{
	if (!P.pen)
		return;
	for (struct region *x = P.regions; x; x = x->next)
		x->live = false;
	for (int i = 0; i < P.v.nf; i++) {
		const struct vfield *f = &P.v.f[i];
		struct region *rg = NULL;
		for (struct region *x = P.regions; x && !rg; x = x->next)
			if (!x->live && ((f->k == F_ENTRY && x->entry == f->entry) || !strcmp(x->name, f->name)))
				rg = x;
		if (rg && f->k == F_NEW && rg->entry) /* an empty row that has become an entry */
			rg = NULL;
		if (!rg) {
			rg = calloc(1, sizeof *rg);
			if (!rg)
				continue;
			snprintf(rg->name, sizeof rg->name, "%s", f->name);
			/* names are unique among live regions; an older one of the same name goes */
			for (struct region **pp = &P.regions; *pp;)
				if (!strcmp((*pp)->name, rg->name)) {
					struct region *o = *pp;
					*pp = o->next;
					ghw_region_destroy(o->r);
					free(o);
				} else
					pp = &(*pp)->next;
			rg->r = ghw_region_new(P.pen, rg->name, &region_listener, rg);
			if (!rg->r) {
				free(rg);
				continue;
			}
			rg->f = *f;
			rg->entry = f->k == F_ENTRY ? f->entry : 0;
			rg->mk = (struct fmake){ f->k == F_NEW ? f->ekind : E_NKINDS, f->per, "", f->rank };
			snprintf(rg->mk.slot, sizeof rg->mk.slot, "%s", f->slot);
			ghw_region_set_context(rg->r, GHW_NORMAL, 0, "");
			ghw_region_set_reading(rg->r, f->k != F_FREE);
			ghw_region_set_layer(rg->r, f->layer);
			if (f->guide > 0)
				ghw_region_set_guides(rg->r, f->guide, f->first);
			rg->x = rg->y = rg->w = rg->h = -1;
			rg->next = P.regions;
			P.regions = rg;
			struct entry *e = rg->entry ? store_get(&P.s, rg->entry) : NULL;
			if (e)
				restore(rg, e);
		}
		rg->live = true;
		rg->f = *f;
		struct entry *me = f->k == F_ENTRY ? store_get(&P.s, f->entry) : NULL;
		if (me) { /* struck out and deleted, its row makes the same entry again */
			rg->mk = (struct fmake){ me->kind == E_EVENT ? E_NKINDS : me->kind, me->per, "", me->rank };
			snprintf(rg->mk.slot, sizeof rg->mk.slot, "%s", me->slot);
		}
		if (rg->entry && f->k == F_NEW)
			rg->f.k = F_ENTRY;
		double x = f->x, y = f->y, w = f->w, h = f->h;
		if (covered(f))
			x = y = w = h = 0;
		if (x != rg->x || y != rg->y || w != rg->w || h != rg->h) {
			ghw_region_set_rect(rg->r, x, y, w, h);
			rg->x = x, rg->y = y, rg->w = w, rg->h = h;
		}
	}
	for (struct region **pp = &P.regions; *pp;) {
		struct region *x = *pp;
		/* a region still writing its first stroke into a new row is kept until the pause */
		if (!x->live && !(x->entry && P.pen_down)) {
			*pp = x->next;
			ghw_region_destroy(x->r);
			free(x);
		} else
			pp = &x->next;
	}
}

/* ---- drawing ---- */

static void layout(void)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	cairo_t *cr = cairo_create(s);
	P.v.today = day_today();
	P.v.W = P.W, P.v.H = P.H;
	view_layout(&P.v, &P.s, &P.L, cr);
	cairo_destroy(cr);
	cairo_surface_destroy(s);
	sync_regions();
}

static void draw(void)
{
	if (!P.configured)
		return;
	int S = P.out_scale < 1 ? 1 : P.out_scale, bw = P.W * S, bh = P.H * S;
	struct buffer *b = get_buffer(bw, bh);
	if (!b)
		return; /* both are on screen: the next release draws */
	cairo_surface_t *cs = cairo_image_surface_create_for_data(b->px, CAIRO_FORMAT_RGB24, bw, bh, bw * 4);
	cairo_t *cr = cairo_create(cs);
	cairo_scale(cr, S, S);
	if (P.refused)
		view_paint_refused(&P.L, cr, P.W, P.H, &P.refused_close);
	else
		view_paint(&P.v, &P.s, &P.L, cr);
	cairo_destroy(cr);
	cairo_surface_flush(cs);
	cairo_surface_destroy(cs);
	wl_surface_set_buffer_scale(P.surf, S);
	wl_surface_attach(P.surf, b->b, 0, 0);
	/* damage only the tiles that changed (FR-061) */
	uint32_t *px = b->px;
	int n = 0;
	if (P.last && P.last_w == bw && P.last_h == bh) {
		for (int ty = 0; ty < bh; ty += TILE)
			for (int tx = 0; tx < bw; tx += TILE) {
				int w = tx + TILE > bw ? bw - tx : TILE, h = ty + TILE > bh ? bh - ty : TILE;
				bool diff = false;
				for (int y = ty; y < ty + h && !diff; y++)
					diff = memcmp(px + (size_t)y * bw + tx, P.last + (size_t)y * bw + tx, w * 4) != 0;
				if (diff) {
					/* the run of changed tiles on this row */
					int x0 = tx;
					while (tx + TILE < bw) {
						int w2 = tx + 2 * TILE > bw ? bw - tx - TILE : TILE;
						bool d2 = false;
						for (int y = ty; y < ty + h && !d2; y++)
							d2 = memcmp(px + (size_t)y * bw + tx + TILE, P.last + (size_t)y * bw + tx + TILE, w2 * 4) != 0;
						if (!d2)
							break;
						tx += TILE;
					}
					int x1 = tx + TILE > bw ? bw : tx + TILE;
					wl_surface_damage_buffer(P.surf, x0, ty, x1 - x0, h);
					n++;
				}
			}
	} else {
		wl_surface_damage_buffer(P.surf, 0, 0, bw, bh);
		n = 1;
		free(P.last);
		P.last = malloc((size_t)bw * bh * 4);
		P.last_w = bw, P.last_h = bh;
	}
	if (P.last)
		memcpy(P.last, px, (size_t)bw * bh * 4);
	if (n) {
		b->busy = true;
		wl_surface_commit(P.surf);
	}
	P.dirty = false;
}

/* ---- actions ---- */

static void choose_period(struct period p)
{
	P.v.per = p;
	P.v.ov = OV_NONE;
	relayout_later();
}

static void tap(double x, double y, bool finger)
{
	struct view *v = &P.v;
	if (P.refused) {
		struct vhit *h = &P.refused_close;
		if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h)
			P.running = false;
		return;
	}
	const struct vhit *h = view_hit(v, x, y);
	bool in_pop = (v->ov == OV_INFO || v->ov == OV_MENU) && x >= v->pop_x && x < v->pop_x + v->pop_w && y >= v->pop_y &&
		      y < v->pop_y + v->pop_h;
	if ((v->ov == OV_INFO || v->ov == OV_MENU) && !in_pop && !(h && h->a == A_INFO)) {
		/* a tap anywhere else closes the popover, and does nothing else */
		v->ov = OV_NONE;
		relayout_later();
		return;
	}
	if (!h)
		return;
	struct entry *e;
	switch (h->a) {
	case A_VIEW: choose_period(period_of(h->arg, v->per.kind == (enum pkind)h->arg ? v->per.start : (v->today >= v->per.start && v->today <= period_end(v->per) ? v->today : v->per.start))); break;
	case A_PREV: choose_period(period_step(v->per, -1)); break;
	case A_NEXT: choose_period(period_step(v->per, 1)); break;
	case A_TODAY: choose_period(period_of(v->per.kind, v->today)); break;
	case A_INFO: v->ov = v->ov == OV_NONE || v->ov == OV_HIDDEN ? OV_INFO : OV_NONE; relayout_later(); break;
	case A_COG: v->ov = OV_MENU; relayout_later(); break;
	case A_BACK: v->ov = OV_INFO; relayout_later(); break;
	case A_HIDDEN: v->ov = OV_HIDDEN; v->hidden_kind = h->arg; relayout_later(); break;
	case A_PICKER: v->ov = OV_PICKER; v->scope = 0; relayout_later(); break;
	case A_SCOPE: v->scope = h->arg; relayout_later(); break;
	case A_PICK: {
		const struct tmpl *t = pl_templates[h->arg];
		if (v->scope == 3)
			snprintf(P.s.def[v->per.kind], sizeof P.s.def[0], "%s", t->id);
		else
			store_choose(&P.s, v->per, v->scope == 2 ? -1 : v->per.start, t->id);
		store_save_choices(&P.s);
		v->ov = OV_NONE;
		relayout_later();
		break;
	}
	case A_CLOSE:
		if (h->arg == 1 && h->id) { /* delete, from the hidden list */
			store_delete(&P.s, h->id);
			save();
			struct hidden_group g[16];
			if (view_hidden(&P.s, v->t, v->per, g, 16) <= v->hidden_kind)
				v->ov = OV_NONE;
		} else
			v->ov = OV_NONE;
		relayout_later();
		break;
	case A_TICK:
		if ((e = store_get(&P.s, h->id))) {
			e->done = !e->done;
			save();
			relayout_later();
		}
		break;
	case A_HABIT: {
		struct entry *m[64];
		struct period d = { P_DAY, h->arg };
		int n = store_list(&P.s, E_MARK, d, NULL, m, 64);
		bool had = false;
		for (int i = 0; i < n; i++)
			if (m[i]->habit == h->id) {
				store_delete(&P.s, m[i]->id);
				had = true;
				break;
			}
		if (!had && (e = store_add(&P.s, E_MARK, d)))
			e->habit = h->id;
		save();
		relayout_later();
		break;
	}
	case A_DAY: choose_period((struct period){ P_DAY, h->arg }); break;
	default: break;
	}
	(void)finger;
}

/* the free time field of the agenda an event is in: its minutes per px */
static double mpp_of(uint32_t id)
{
	struct entry *e = store_get(&P.s, id);
	for (int i = 0; e && i < P.v.nf; i++)
		if (P.v.f[i].k == F_FREE && P.v.f[i].per.start == e->per.start)
			return P.v.f[i].mpp;
	return 0;
}

static void finger_down(double x, double y)
{
	P.tx = x, P.ty = y;
	P.drag = 0;
	const struct vhit *h = P.refused ? NULL : view_hit(&P.v, x, y);
	if (h && h->a == A_EVENT && P.v.ov == OV_NONE) {
		struct entry *e = store_get(&P.s, h->id);
		double mpp = mpp_of(h->id);
		if (e && mpp > 0) {
			P.drag = e->id;
			P.drag_s0 = e->start, P.drag_e0 = e->end;
			P.drag_y0 = y;
			P.drag_mpp = mpp;
			P.drag_mode = y > h->y + h->h - 10 ? 2 : 1;
		}
	}
}

static void finger_motion(double x, double y)
{
	if (!P.drag)
		return;
	struct entry *e = store_get(&P.s, P.drag);
	if (!e)
		return;
	int steps = (int)lround((y - P.drag_y0) * P.drag_mpp / 30);
	int s = P.drag_s0, en = P.drag_e0;
	if (P.drag_mode == 1)
		s += steps * 30, en += steps * 30;
	else
		en = en + steps * 30 > s + 30 ? en + steps * 30 : s + 30;
	if (s < 0)
		en -= s, s = 0;
	if (s != e->start || en != e->end) {
		e->start = s, e->end = en;
		P.v.drag = P.drag;
		P.v.drag_s0 = P.drag_s0, P.v.drag_e0 = P.drag_e0;
		relayout_later(); /* once per step, only where the block leaves and enters */
	}
}

static void finger_up(double x, double y)
{
	if (P.drag) {
		struct entry *e = store_get(&P.s, P.drag);
		bool moved = e && (e->start != P.drag_s0 || e->end != P.drag_e0);
		P.drag = 0;
		P.v.drag = 0;
		if (moved) {
			save();
			relayout_later();
			return;
		}
	}
	if (fabs(x - P.tx) < 12 && fabs(y - P.ty) < 12)
		tap(x, y, true);
}

/* ---- seat: the finger (touch), a mouse (pointer), the pen outside the fields (tablet) ---- */

static void t_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, struct wl_surface *s, int32_t id,
		   wl_fixed_t x, wl_fixed_t y)
{
	if (P.touch_id >= 0)
		return;
	P.touch_id = id;
	finger_down(wl_fixed_to_double(x), wl_fixed_to_double(y));
	P.px = wl_fixed_to_double(x), P.py = wl_fixed_to_double(y);
}
static void t_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
	if (id != P.touch_id)
		return;
	P.touch_id = -1;
	finger_up(P.px, P.py);
}
static void t_motion(void *d, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	if (id != P.touch_id)
		return;
	P.px = wl_fixed_to_double(x), P.py = wl_fixed_to_double(y);
	finger_motion(P.px, P.py);
}
static void t_frame(void *d, struct wl_touch *t) {}
static void t_cancel(void *d, struct wl_touch *t)
{
	P.touch_id = -1;
	if (P.drag) {
		struct entry *e = store_get(&P.s, P.drag);
		if (e)
			e->start = P.drag_s0, e->end = P.drag_e0;
		P.drag = 0;
		P.v.drag = 0;
		relayout_later();
	}
}
static void t_shape(void *d, struct wl_touch *t, int32_t id, wl_fixed_t a, wl_fixed_t b) {}
static void t_orient(void *d, struct wl_touch *t, int32_t id, wl_fixed_t o) {}
static const struct wl_touch_listener touch_listener = { t_down, t_up, t_motion, t_frame, t_cancel, t_shape, t_orient };

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	P.px = wl_fixed_to_double(x), P.py = wl_fixed_to_double(y);
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) {}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	P.px = wl_fixed_to_double(x), P.py = wl_fixed_to_double(y);
	if (P.drag)
		finger_motion(P.px, P.py);
}
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
	if (st == WL_POINTER_BUTTON_STATE_PRESSED)
		finger_down(P.px, P.py);
	else
		finger_up(P.px, P.py);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static const struct wl_pointer_listener pointer_listener = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame,
	p_axis_source, p_axis_stop, p_axis_discrete };

/* the pen: its strokes reach the fields through the regions; here only a tap outside them */
static bool in_field(double x, double y)
{
	for (int i = 0; i < P.v.nf; i++) {
		const struct vfield *f = &P.v.f[i];
		if (x >= f->x && x < f->x + f->w && y >= f->y && y < f->y + f->h && !covered(f))
			return true;
	}
	return false;
}
static void tt_type(void *d, struct zwp_tablet_tool_v2 *t, uint32_t type) {}
static void tt_serial(void *d, struct zwp_tablet_tool_v2 *t, uint32_t hi, uint32_t lo) {}
static void tt_wacom(void *d, struct zwp_tablet_tool_v2 *t, uint32_t hi, uint32_t lo) {}
static void tt_cap(void *d, struct zwp_tablet_tool_v2 *t, uint32_t c) {}
static void tt_done(void *d, struct zwp_tablet_tool_v2 *t) {}
static void tt_removed(void *d, struct zwp_tablet_tool_v2 *t)
{
	zwp_tablet_tool_v2_destroy(t);
}
static void tt_in(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s, struct zwp_tablet_v2 *tab, struct wl_surface *sf) {}
static void tt_out(void *d, struct zwp_tablet_tool_v2 *t) {}
static double down_x, down_y;
static void tt_down(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s)
{
	P.tool_down = true;
	P.pen_down = true;
	down_x = P.tool_x, down_y = P.tool_y;
}
static void tt_up(void *d, struct zwp_tablet_tool_v2 *t)
{
	P.tool_down = false;
	P.pen_down = false;
	P.pen_up_ms = now_ms();
	if (fabs(P.tool_x - down_x) < 8 && fabs(P.tool_y - down_y) < 8 && !in_field(down_x, down_y))
		tap(down_x, down_y, false);
}
static void tt_motion(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t x, wl_fixed_t y)
{
	P.tool_x = wl_fixed_to_double(x), P.tool_y = wl_fixed_to_double(y);
}
static void tt_pressure(void *d, struct zwp_tablet_tool_v2 *t, uint32_t p) {}
static void tt_distance(void *d, struct zwp_tablet_tool_v2 *t, uint32_t p) {}
static void tt_tilt(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t a, wl_fixed_t b) {}
static void tt_rot(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t a) {}
static void tt_slider(void *d, struct zwp_tablet_tool_v2 *t, int32_t a) {}
static void tt_wheel(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t a, int32_t b) {}
static void tt_button(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s, uint32_t b, uint32_t st) {}
static void tt_frame(void *d, struct zwp_tablet_tool_v2 *t, uint32_t time) {}
static const struct zwp_tablet_tool_v2_listener tool_listener = { tt_type, tt_serial, tt_wacom, tt_cap, tt_done,
	tt_removed, tt_in, tt_out, tt_down, tt_up, tt_motion, tt_pressure, tt_distance, tt_tilt, tt_rot, tt_slider,
	tt_wheel, tt_button, tt_frame };

static void ts_tablet(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_v2 *t) {}
static void ts_tool(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_tool_v2 *t)
{
	zwp_tablet_tool_v2_add_listener(t, &tool_listener, NULL);
}
static void ts_pad(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_pad_v2 *p) {}
static const struct zwp_tablet_seat_v2_listener tseat_listener = { ts_tablet, ts_tool, ts_pad };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !P.touch) {
		P.touch = wl_seat_get_touch(s);
		wl_touch_add_listener(P.touch, &touch_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !P.pointer) {
		P.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(P.pointer, &pointer_listener, NULL);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

/* ---- output, shell ---- */

static void o_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
		       const char *mk, const char *md, int32_t t) {}
static void o_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r) {}
static void o_done(void *d, struct wl_output *o) {}
static void o_scale(void *d, struct wl_output *o, int32_t f)
{
	if (f > P.out_scale)
		P.out_scale = f;
	P.dirty = true;
}
static const struct wl_output_listener output_listener = { o_geometry, o_mode, o_done, o_scale };

static void wm_ping(void *d, struct xdg_wm_base *wm, uint32_t serial)
{
	xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
	xdg_surface_ack_configure(xs, serial);
	P.configured = true;
	relayout_later();
}
static const struct xdg_surface_listener xs_listener = { xs_configure };

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	if (w > 0 && h > 0 && (w != P.W || h != P.H)) {
		P.W = w, P.H = h;
		free(P.last);
		P.last = NULL;
	}
}
static void top_close(void *d, struct xdg_toplevel *t)
{
	P.running = false;
}
static void top_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) {}
static void top_caps(void *d, struct xdg_toplevel *t, struct wl_array *c) {}
static const struct xdg_toplevel_listener top_listener = { top_configure, top_close, top_bounds, top_caps };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	if (!strcmp(iface, wl_compositor_interface.name))
		P.comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		P.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		P.wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(P.wm, &wm_listener, NULL);
	} else if (!strcmp(iface, wl_seat_interface.name) && !P.seat) {
		P.seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
		wl_seat_add_listener(P.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, wl_output_interface.name)) {
		struct wl_output *o = wl_registry_bind(r, name, &wl_output_interface, ver < 2 ? ver : 2);
		wl_output_add_listener(o, &output_listener, NULL);
	} else if (!strcmp(iface, zwp_tablet_manager_v2_interface.name))
		P.tabman = wl_registry_bind(r, name, &zwp_tablet_manager_v2_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = { global, global_remove };

static void pen_caps(void *d, uint32_t caps)
{
	bool ok = caps & GHW_CAP_REGIONS;
	if (ok == !P.refused)
		return;
	P.refused = !ok;
	relayout_later();
}
static const struct ghw_pen_listener pen_listener = { .capabilities = pen_caps };

/* ---- the data directory, the voice ---- */

static void data_dir(char *out, size_t n)
{
	const char *e = getenv("GLASSINE_PLANNER_DATA"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
	if (e && *e)
		snprintf(out, n, "%s", e);
	else if (x && *x)
		snprintf(out, n, "%s/glassine-planner", x);
	else
		snprintf(out, n, "%s/.local/share/glassine-planner", h ? h : "");
}

static void watch_voice(void)
{
	P.ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (P.ino < 0)
		return;
	char d[1100];
	const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
	if (x && *x)
		snprintf(d, sizeof d, "%s/glassine-shell", x);
	else
		snprintf(d, sizeof d, "%s/.config/glassine-shell", h ? h : "");
	P.inowd = inotify_add_watch(P.ino, d, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}

static void reload_voice(void)
{
	char v[32];
	look_voice_name(v, sizeof v);
	if (strcmp(v, P.voice)) {
		snprintf(P.voice, sizeof P.voice, "%s", v);
		look_init(&P.L, v);
		relayout_later(); /* FR-060: the voice changes with the system's */
	}
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--help")) {
			printf("usage: glassine-planner\n  the pen-first planner; its entries are kept in "
			       "$XDG_DATA_HOME/glassine-planner (GLASSINE_PLANNER_DATA overrides)\n");
			return 0;
		}
	char dir[PATH_MAX];
	data_dir(dir, sizeof dir);
	store_init(&P.s, dir);
	store_load(&P.s);
	look_voice_name(P.voice, sizeof P.voice);
	look_init(&P.L, P.voice);
	P.v.today = day_today();
	P.v.per = period_of(P_DAY, P.v.today);
	P.v.hidden_kind = -1;

	P.dpy = wl_display_connect(NULL);
	if (!P.dpy) {
		fprintf(stderr, "glassine-planner: no Wayland display\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(P.dpy);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(P.dpy);
	if (!P.comp || !P.shm || !P.wm) {
		fprintf(stderr, "glassine-planner: the compositor lacks wl_compositor, wl_shm or xdg_wm_base\n");
		return 1;
	}
	if (P.tabman && P.seat) {
		struct zwp_tablet_seat_v2 *ts = zwp_tablet_manager_v2_get_tablet_seat(P.tabman, P.seat);
		zwp_tablet_seat_v2_add_listener(ts, &tseat_listener, NULL);
	}
	wl_display_roundtrip(P.dpy);
	P.surf = wl_compositor_create_surface(P.comp);
	P.pen = ghw_pen_new(P.dpy, P.surf, &pen_listener, NULL);
	/* FR-001: without writing regions read by the service, the planner doesn't start */
	P.refused = !P.pen || !(ghw_pen_capabilities(P.pen) & GHW_CAP_REGIONS);
	if (P.refused)
		fprintf(stderr, "glassine-planner: this system has no writing regions (the handwriting service): not starting\n");
	P.xs = xdg_wm_base_get_xdg_surface(P.wm, P.surf);
	xdg_surface_add_listener(P.xs, &xs_listener, NULL);
	P.top = xdg_surface_get_toplevel(P.xs);
	xdg_toplevel_add_listener(P.top, &top_listener, NULL);
	xdg_toplevel_set_app_id(P.top, "glassine-planner");
	xdg_toplevel_set_title(P.top, "Planner");
	P.W = 702, P.H = 884;
	wl_surface_commit(P.surf);
	watch_voice();

	while (P.running) {
		if (P.relayout && !(P.pen_down || now_ms() - P.pen_up_ms < PAUSE_MS) && P.configured) {
			P.relayout = false;
			if (!P.refused)
				layout();
			P.dirty = true;
		} else if (P.relayout && P.configured && !P.refused && (P.drag || P.v.ov != OV_NONE || P.pen_up_ms == 0)) {
			P.relayout = false;
			layout();
		}
		if (P.dirty)
			draw();
		wl_display_flush(P.dpy);
		struct pollfd fds[2] = { { wl_display_get_fd(P.dpy), POLLIN, 0 }, { P.ino, POLLIN, 0 } };
		int timeout = -1;
		if (P.relayout) {
			long long left = PAUSE_MS - (now_ms() - P.pen_up_ms);
			timeout = left > 0 ? (int)left + 5 : 50;
		}
		if (wl_display_prepare_read(P.dpy) < 0) {
			wl_display_dispatch_pending(P.dpy);
			continue;
		}
		int r = poll(fds, P.ino >= 0 ? 2 : 1, timeout);
		if (r > 0 && (fds[0].revents & POLLIN)) {
			if (wl_display_read_events(P.dpy) < 0)
				break;
		} else
			wl_display_cancel_read(P.dpy);
		if (wl_display_dispatch_pending(P.dpy) < 0)
			break;
		if (r > 0 && P.ino >= 0 && (fds[1].revents & POLLIN)) {
			char buf[4096];
			while (read(P.ino, buf, sizeof buf) > 0)
				;
			reload_voice();
		}
	}
	/* every change was saved when it was made (FR-024); no save on the way out, which would
	 * write this instance's view of the entries over another's */
	for (struct region *x = P.regions; x;) {
		struct region *n = x->next;
		ghw_region_destroy(x->r);
		free(x);
		x = n;
	}
	if (P.pen)
		ghw_pen_destroy(P.pen);
	wl_display_disconnect(P.dpy);
	store_free(&P.s);
	return 0;
}
