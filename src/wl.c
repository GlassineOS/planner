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
#include <xkbcommon/xkbcommon.h>

#include <glassine-handwriting.h>
#include "tablet-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "view.h"
#include "xdg-shell-client-protocol.h"

#define TILE 16
#define PAUSE_MS 1200 /* the pause after the pen lifts before the page settles (FR-046) */
#define KBD_GONE_MS 1500 /* text mode left with the keyboard still up: then the page fits what is left */
#define TEXT_MAX 2048

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
	/* the template editor, and a widget or its edge dragged in it (1 the widget, 2 its edge) */
	struct editor ed;
	int ed_drag;
	double ed_x0, ed_y0;
	int ino, inowd;
	int lay_h;         /* the height the page is laid out for; the keyboard covers, never relays */
	/* text mode (FR-012, FR-013): the text being edited, its caret; edits of the whole view's
	 * other fields, sent to their regions when the view goes back to ink */
	char tm_buf[TEXT_MAX];
	int tm_cur;
	struct tedit {
		char name[96];
		uint32_t entry;
		char *text;
		uint8_t neaten, autograph; /* the field's own choice made in text mode, SHOW_* */
	} *tedits;
	int nted;
	long long tm_left_ms;
	char q[256];       /* search */
	uint32_t find;     /* a search result opened: shown in the hidden list when its view has no place for it */
	/* the keyboard: the system's on-screen keyboard types through wl_keyboard; text-input-v3
	 * says a text field has the focus, which brings it up */
	struct zwp_text_input_manager_v3 *tim;
	struct zwp_text_input_v3 *ti;
	bool ti_in, ti_on;
	struct wl_keyboard *kbd;
	struct xkb_context *xkb;
	struct xkb_keymap *km;
	struct xkb_state *ks;
	char *ti_commit;
	uint32_t ti_before, ti_after;
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
	if (rg->f.k == F_NAME) { /* the template's name: kept with the editor, never an entry */
		struct pl_point *q = malloc(n * sizeof *q);
		if (q) {
			for (size_t i = 0; i < n; i++)
				q[i] = (struct pl_point){ (float)p[i].x, (float)p[i].y, (float)p[i].pressure, p[i].time };
			field_add_stroke(&P.ed.name, k, q, (int)n);
			free(q);
		}
		P.dirty = true;
		return;
	}
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
	if (rg && rg->f.k == F_NAME) {
		for (size_t i = 0; i < ngone; i++)
			field_remove_stroke(&P.ed.name, gone[i]);
		field_set_value(&P.ed.name, text, state, len);
		editor_name_value(&P.ed, text);
		P.dirty = true;
		return;
	}
	if (!rg || !rg->entry || !store_get(&P.s, rg->entry))
		return;
	/* the word layout: unchanged words' ink moved along the line, edited words set in
	 * Autograph (FR-014) */
	struct pl_word *pw = nw ? calloc(nw, sizeof *pw) : NULL;
	float *buf = NULL;
	size_t nf = 0;
	for (size_t i = 0; pw && i < nw; i++)
		nf += w[i].npath;
	buf = nf ? malloc(nf * sizeof *buf) : NULL;
	size_t o = 0;
	for (size_t i = 0; pw && i < nw; i++) {
		pw[i] = (struct pl_word){ w[i].start, w[i].length, w[i].set != 0, (uint32_t *)w[i].strokes, (int)w[i].nstrokes,
			(float)w[i].dx, (float)w[i].dy, { (float)w[i].x, (float)w[i].y, (float)w[i].width, (float)w[i].height }, NULL, 0 };
		if (buf && w[i].npath) {
			pw[i].path = buf + o;
			pw[i].npath = (int)w[i].npath;
			for (size_t j = 0; j < w[i].npath; j++)
				buf[o++] = (float)w[i].path[j];
		}
	}
	bool emptied = field_value(&P.s, &rg->entry, text, pw, pw ? (int)nw : 0, gone, ngone, state, len);
	free(pw);
	free(buf);
	P.dirty = true;
	if (emptied)
		relayout_later(); /* all its ink went (struck out): the row is empty again */
	save();
	if (ngone)
		P.dirty = true;
}

/* a neatened stroke: shown while the field's Neaten is on (FR-011), the raw one kept */
static void on_neat(void *d, struct ghw_region *r, uint32_t k, int how, const struct ghw_point *p, size_t n)
{
	struct region *rg = region_of(r);
	struct entry *e = rg && rg->entry ? store_get(&P.s, rg->entry) : NULL;
	if (!e || !n)
		return;
	struct pl_point *q = malloc(n * sizeof *q);
	if (!q)
		return;
	for (size_t i = 0; i < n; i++)
		q[i] = (struct pl_point){ (float)p[i].x, (float)p[i].y, (float)p[i].pressure, p[i].time };
	field_set_neat(&e->f, k, q, (int)n);
	free(q);
	save();
	P.dirty = true;
}

static const struct ghw_region_listener region_listener = { .stroke = on_stroke, .neat = on_neat, .value = on_value };

static bool covered(const struct vfield *f)
{
	struct view *v = &P.v;
	if (v->ed)
		return false; /* the editor's only field is its name */
	if (v->ov == OV_PICKER || v->ov == OV_TITLE || v->ov == OV_SETTINGS)
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
			if (f->k != F_FREE) {
				struct entry *ne = f->k == F_ENTRY ? store_get(&P.s, f->entry) : NULL;
				bool on = ne ? field_neaten(&P.s, &ne->f) : P.s.neaten;
				ghw_region_set_neaten(rg->r, on, P.s.strength);
			}
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
		double x = f->x, y = f->y - P.v.scroll, w = f->w, h = f->h;
		if (covered(f))
			x = y = w = h = 0;
		if (x != rg->x || y != rg->y || w != rg->w || h != rg->h) {
			ghw_region_set_rect(rg->r, x, y, w, h);
			/* the part the keyboard and the page's move leave visible */
			double bot = P.v.vis_h > 0 ? P.v.vis_h - 46 : P.H;
			double cy0 = fmax(y, 0), cy1 = fmin(y + h, bot);
			if (P.v.scroll > 0 || P.v.vis_h > 0)
				ghw_region_set_clip(rg->r, (int)x, (int)cy0, (int)w, cy1 > cy0 ? (int)(cy1 - cy0) : 1);
			else
				ghw_region_set_clip(rg->r, 0, 0, 0, 0);
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
	P.v.W = P.W, P.v.H = P.lay_h ? P.lay_h : P.H;
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

/* ---- text mode (FR-012 to FR-017) ---- */

static struct region *region_for(const struct vfield *f)
{
	for (struct region *x = P.regions; x; x = x->next)
		if (x->live && ((f->k == F_ENTRY && x->entry == f->entry) || !strcmp(x->name, f->name)))
			return x;
	return NULL;
}

static struct tedit *edit_of(const char *name)
{
	for (int i = 0; i < P.nted; i++)
		if (!strcmp(P.tedits[i].name, name))
			return &P.tedits[i];
	return NULL;
}

/* a field's current text in the whole view: its edit, else its value */
static const char *text_of(void *ud, const struct vfield *f)
{
	struct tedit *t = edit_of(f->name);
	if (t)
		return t->text;
	struct entry *e = f->k == F_ENTRY ? store_get(&P.s, f->entry) : NULL;
	return e && e->f.value ? e->f.value : "";
}

static const struct vfield *focused(void)
{
	for (int i = 0; P.v.tm_name[0] && i < P.v.nf; i++)
		if (!strcmp(P.v.f[i].name, P.v.tm_name))
			return &P.v.f[i];
	return NULL;
}

/* the text field with the focus, told to the system's text input (the keyboard comes up) */
static void ti_state(void)
{
	if (!P.ti || !P.ti_in)
		return;
	bool want = (P.v.tm && P.v.tm_name[0]) || P.v.search || P.v.tm == 2;
	if (!want) {
		if (P.ti_on) {
			zwp_text_input_v3_disable(P.ti);
			zwp_text_input_v3_commit(P.ti);
			P.ti_on = false;
		}
		return;
	}
	if (!P.ti_on)
		zwp_text_input_v3_enable(P.ti);
	P.ti_on = true;
	const char *t = P.v.search ? P.q : P.tm_buf;
	int cur = P.v.search ? (int)strlen(P.q) : P.tm_cur;
	zwp_text_input_v3_set_surrounding_text(P.ti, t, cur, cur);
	zwp_text_input_v3_set_text_change_cause(P.ti, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
	zwp_text_input_v3_set_content_type(P.ti, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
	const struct vfield *f = focused();
	if (f)
		zwp_text_input_v3_set_cursor_rectangle(P.ti, (int)f->x, (int)(f->y - P.v.scroll), (int)f->w, (int)fmin(f->h, 40));
	zwp_text_input_v3_commit(P.ti);
}

/* the focused field's text into the edits */
static void keep_edit(void)
{
	const struct vfield *f = focused();
	if (!f)
		return;
	struct tedit *t = edit_of(f->name);
	if (!t) {
		struct tedit *n = realloc(P.tedits, (P.nted + 1) * sizeof *n);
		if (!n)
			return;
		P.tedits = n;
		t = &P.tedits[P.nted++];
		memset(t, 0, sizeof *t);
		snprintf(t->name, sizeof t->name, "%s", f->name);
		t->entry = f->k == F_ENTRY ? f->entry : 0;
	}
	free(t->text);
	t->text = strdup(P.tm_buf);
	struct entry *e = t->entry ? store_get(&P.s, t->entry) : NULL;
	bool n = e ? field_neaten(&P.s, &e->f) : P.s.neaten, a = e ? field_autograph(&P.s, &e->f) : P.s.autograph;
	if (P.v.tm_neaten != n || t->neaten)
		t->neaten = P.v.tm_neaten ? SHOW_ON : SHOW_OFF;
	if (P.v.tm_autograph != a || t->autograph)
		t->autograph = P.v.tm_autograph ? SHOW_ON : SHOW_OFF;
}

static void tm_focus(const struct vfield *f)
{
	keep_edit();
	P.v.tm_name[0] = 0;
	if (f) {
		snprintf(P.v.tm_name, sizeof P.v.tm_name, "%s", f->name);
		struct tedit *t = edit_of(f->name);
		snprintf(P.tm_buf, sizeof P.tm_buf, "%s", t ? t->text : text_of(NULL, f));
		P.tm_cur = (int)strlen(P.tm_buf);
		struct entry *e = f->k == F_ENTRY ? store_get(&P.s, f->entry) : NULL;
		P.v.tm_neaten = t && t->neaten ? t->neaten == SHOW_ON : e ? field_neaten(&P.s, &e->f) : P.s.neaten;
		P.v.tm_autograph = t && t->autograph ? t->autograph == SHOW_ON : e ? field_autograph(&P.s, &e->f) : P.s.autograph;
		double sc = view_scroll_to(&P.v, f);
		if (sc != P.v.scroll) {
			P.v.scroll = sc;
			layout(); /* the regions follow the page's move; nothing is laid out again */
		}
	}
	P.v.tm_text = P.tm_buf;
	P.v.tm_cur = P.tm_cur;
	P.dirty = true;
	ti_state();
}

/* a finger tap on a field (f), or the cog's text mode for the whole view (f NULL) */
static void tm_open(const struct vfield *f)
{
	P.tm_left_ms = 0;
	P.v.tm = f ? 1 : 2;
	P.v.text_of = text_of;
	if (f) {
		struct region *rg = region_for(f);
		if (rg)
			ghw_region_set_mode(rg->r, GHW_MODE_TEXT);
	} else if (P.pen)
		ghw_pen_set_mode(P.pen, GHW_MODE_TEXT);
	tm_focus(f);
}

/* back to ink: each changed text goes to its region before the region goes back to ink mode
 * (its value comes back with the changed words set in Autograph); unchanged words keep their
 * ink. An empty row with text typed into it becomes its entry */
static void tm_close(void)
{
	if (!P.v.tm)
		return;
	keep_edit();
	bool made = false;
	for (int i = 0; i < P.nted; i++) {
		struct tedit *t = &P.tedits[i];
		struct region *rg = NULL;
		for (struct region *x = P.regions; x && !rg; x = x->next)
			if ((t->entry && x->entry == t->entry) || !strcmp(x->name, t->name))
				rg = x;
		struct entry *e = t->entry ? store_get(&P.s, t->entry) : NULL;
		const char *was = e && e->f.value ? e->f.value : "";
		bool changed = strcmp(was, t->text) != 0;
		if (!e && rg && *t->text) {
			e = field_typed(&P.s, &rg->entry, &rg->mk, t->text);
			made = e != NULL;
		} else if (e && changed)
			field_typed(&P.s, &t->entry, NULL, t->text); /* search finds it at once */
		if (e) {
			if (t->neaten)
				e->f.neaten = t->neaten;
			if (t->autograph)
				e->f.autograph = t->autograph;
		}
		if (rg && changed)
			ghw_region_edit(rg->r, t->text);
		if (rg && e)
			ghw_region_set_neaten(rg->r, field_neaten(&P.s, &e->f), P.s.strength);
		free(t->text);
	}
	free(P.tedits);
	P.tedits = NULL;
	P.nted = 0;
	if (P.v.tm == 2 && P.pen)
		ghw_pen_set_mode(P.pen, GHW_MODE_INK);
	else
		for (struct region *x = P.regions; x; x = x->next)
			ghw_region_set_mode(x->r, GHW_MODE_INK);
	save();
	P.v.tm = 0;
	P.v.tm_name[0] = 0;
	P.v.text_of = NULL;
	P.tm_left_ms = P.v.vis_h > 0 ? now_ms() : 0;
	ti_state();
	P.dirty = true;
	if (made)
		relayout_later(); /* a new row to write in */
}

/* the keyboard's text into the field (or the search) with the focus */
static void type_text(const char *in, int before, int after)
{
	char *b = P.v.search ? P.q : P.tm_buf;
	size_t cap = P.v.search ? sizeof P.q : sizeof P.tm_buf;
	int cur = P.v.search ? (int)strlen(P.q) : P.tm_cur, len = (int)strlen(b);
	if (!P.v.search && !focused())
		return;
	/* delete before and after the caret, whole UTF-8 characters */
	while (before-- > 0 && cur > 0) {
		int k = cur - 1;
		while (k > 0 && ((unsigned char)b[k] & 0xc0) == 0x80)
			k--;
		memmove(b + k, b + cur, len - cur + 1);
		len -= cur - k;
		cur = k;
	}
	while (after-- > 0 && cur < len) {
		int k = cur + 1;
		while (k < len && ((unsigned char)b[k] & 0xc0) == 0x80)
			k++;
		memmove(b + cur, b + k, len - k + 1);
		len -= k - cur;
	}
	size_t n = in ? strlen(in) : 0;
	if (n && len + n < cap) {
		memmove(b + cur + n, b + cur, len - cur + 1);
		memcpy(b + cur, in, n);
		cur += (int)n;
	}
	if (P.v.search) {
		P.v.q = P.q;
		P.v.q_cur = cur;
		layout(); /* the results' targets */
	} else {
		P.tm_cur = cur;
		P.v.tm_text = P.tm_buf;
		P.v.tm_cur = cur;
	}
	P.dirty = true;
	ti_state();
}

static void caret_move(int d)
{
	if (P.v.search || !focused())
		return;
	int len = (int)strlen(P.tm_buf), c = P.tm_cur;
	if (d == -2)
		c = 0;
	else if (d == 2)
		c = len;
	else if (d < 0 && c > 0)
		for (c--; c > 0 && ((unsigned char)P.tm_buf[c] & 0xc0) == 0x80; c--)
			;
	else if (d > 0 && c < len)
		for (c++; c < len && ((unsigned char)P.tm_buf[c] & 0xc0) == 0x80; c++)
			;
	P.tm_cur = P.v.tm_cur = c;
	P.dirty = true;
	ti_state();
}

/* ---- actions ---- */

static void choose_period(struct period p)
{
	P.v.per = p;
	P.v.ov = OV_NONE;
	relayout_later();
}

/* ---- the template editor (FR-045, FR-049): it only arranges, it is given no store to write ---- */

/* laid out at once, even in the pause after the pen: the editor's tree (or the editor itself)
 * may just have been freed, and the next frame must not paint the old layout */
static void editor_relayout(void)
{
	if (P.configured)
		layout();
	else
		P.relayout = true;
	P.dirty = true;
}

static void editor_enter(int how)
{
	struct view *v = &P.v;
	bool picker = v->ov == OV_PICKER;
	const struct tmpl *t = v->t;
	if (picker && v->ring[0] && tmpl_find(v->ring))
		t = tmpl_find(v->ring);
	if (how == 1)
		editor_open_new(&P.ed, v->per.kind);
	else if (how == 2 && t)
		editor_open_copy(&P.ed, t);
	else if (t)
		editor_open_edit(&P.ed, t);
	if (!P.ed.t)
		return;
	P.ed.from_picker = picker;
	P.ed.data = &P.s;
	v->ed = &P.ed;
	v->ov = OV_NONE;
	editor_relayout();
}

static void editor_leave(const char *ring)
{
	struct view *v = &P.v;
	v->ov = P.ed.from_picker ? OV_PICKER : OV_NONE;
	snprintf(v->ring, sizeof v->ring, "%s", ring && P.ed.from_picker ? ring : "");
	v->ed = NULL;
	editor_close(&P.ed);
	editor_relayout();
}

static void editor_tap(int op, int arg)
{
	struct view *v = &P.v;
	if (op == ED_SELECT)
		P.ed.target = arg >= 0 && arg < v->g.n ? (struct tnode *)v->g.p[arg].n : NULL;
	char id[TMPL_ID];
	snprintf(id, sizeof id, "%s", P.ed.t && P.ed.t->id ? P.ed.t->id : "");
	switch (editor_act(&P.ed, op, arg, P.s.dir)) {
	case ER_SAVED: editor_leave(id); break;
	case ER_CLOSED: editor_leave(NULL); break;
	case ER_CHANGED: editor_relayout(); break;
	default: break;
	}
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
	if (v->tm) {
		/* text mode: the bar's switches and its way out; in the whole view a finger moves
		 * the focus from field to field; a tap anywhere else goes back to ink (one field) */
		const struct vfield *f = finger ? view_field_at(v, x, y) : NULL;
		if (h && h->fixed) {
			if (h->a == A_TM_NEATEN)
				v->tm_neaten = !v->tm_neaten, P.dirty = true;
			else if (h->a == A_TM_AUTOGRAPH)
				v->tm_autograph = !v->tm_autograph, P.dirty = true;
			else if (h->a == A_TM_DONE)
				tm_close();
			return;
		}
		if (v->tm == 2 && f)
			tm_focus(f);
		else if (!(v->tm == 1 && f && !strcmp(f->name, v->tm_name)))
			tm_close();
		return;
	}
	if (v->search && !(h && (h->a == A_SEARCH || h->a == A_RESULT))) {
		v->search = false; /* the search ends: its words stay for the next */
		ti_state();
		P.dirty = true;
	}
	bool in_pop = (v->ov == OV_INFO || v->ov == OV_MENU) && x >= v->pop_x && x < v->pop_x + v->pop_w && y >= v->pop_y &&
		      y < v->pop_y + v->pop_h;
	if ((v->ov == OV_INFO || v->ov == OV_MENU) && !in_pop && !(h && h->a == A_INFO)) {
		/* a tap anywhere else closes the popover, and does nothing else */
		v->ov = OV_NONE;
		relayout_later();
		return;
	}
	if (finger && !v->ed && v->ov == OV_NONE && (!h || h->a == A_EVENT || h->a == A_NONE)) {
		/* a finger tap on a field opens its text mode (FR-012); the pen never does. A cut
		 * title shows whole first (FR-018) */
		if (h && h->a == A_EVENT && h->arg) {
			v->title = h->id;
			v->ov = OV_TITLE;
			relayout_later();
			return;
		}
		const struct vfield *f = view_field_at(v, x, y);
		if (f) {
			tm_open(f);
			return;
		}
	}
	if (!h)
		return;
	struct entry *e;
	switch (h->a) {
	case A_SETTINGS: v->ov = OV_SETTINGS; relayout_later(); break;
	case A_TEXTMODE: /* the cog's text mode for the whole view (FR-013) */
		v->ov = OV_NONE;
		tm_open(NULL);
		relayout_later();
		break;
	case A_SET_NEATEN:
	case A_SET_AUTOGRAPH:
	case A_SET_STRENGTH:
		if (h->a == A_SET_NEATEN)
			P.s.neaten = !P.s.neaten;
		else if (h->a == A_SET_AUTOGRAPH)
			P.s.autograph = !P.s.autograph;
		else /* Light, Medium, Strong */
			P.s.strength = P.s.strength < 0.5f ? 0.65f : P.s.strength < 0.85f ? 1.0f : 0.35f;
		store_save_settings(&P.s);
		for (struct region *x = P.regions; x; x = x->next) {
			struct entry *re = x->entry ? store_get(&P.s, x->entry) : NULL;
			if (x->f.k != F_FREE)
				ghw_region_set_neaten(x->r, re ? field_neaten(&P.s, &re->f) : P.s.neaten, P.s.strength);
		}
		P.dirty = true;
		relayout_later();
		break;
	case A_SEARCH:
		v->search = true;
		v->q = P.q;
		v->q_cur = (int)strlen(P.q);
		ti_state();
		relayout_later();
		break;
	case A_RESULT:
		if ((e = store_get(&P.s, h->id))) {
			v->search = false;
			ti_state();
			P.find = e->id;
			choose_period(e->per);
		}
		break;
	case A_VIEW: choose_period(period_of(h->arg, v->per.kind == (enum pkind)h->arg ? v->per.start : (v->today >= v->per.start && v->today <= period_end(v->per) ? v->today : v->per.start))); break;
	case A_PREV: choose_period(period_step(v->per, -1)); break;
	case A_NEXT: choose_period(period_step(v->per, 1)); break;
	case A_TODAY: choose_period(period_of(v->per.kind, v->today)); break;
	case A_INFO: v->ov = v->ov == OV_NONE || v->ov == OV_HIDDEN ? OV_INFO : OV_NONE; relayout_later(); break;
	case A_COG: v->ov = OV_MENU; relayout_later(); break;
	case A_BACK: v->ov = OV_INFO; relayout_later(); break;
	case A_HIDDEN: v->ov = OV_HIDDEN; v->hidden_kind = h->arg; relayout_later(); break;
	case A_PICKER: v->ov = OV_PICKER; v->scope = 0; v->ring[0] = 0; relayout_later(); break;
	case A_EDIT: editor_enter(h->arg); break;
	case A_ED: editor_tap(h->arg, (int)h->id - 100); break;
	case A_SCOPE: v->scope = h->arg; relayout_later(); break;
	case A_PICK: {
		const struct tmpl *ts[64];
		if (h->arg >= tmpl_list(v->per.kind, ts, 64))
			break;
		const struct tmpl *t = ts[h->arg];
		v->ring[0] = 0;
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
	P.ed_drag = 0;
	if (P.v.ed && P.ed.sel && P.ed.ov == EO_NONE) {
		/* the selected widget, or its right or lower edge: a drag moves or resizes it */
		for (int i = 0; i < P.v.g.n; i++) {
			const struct placed *p = &P.v.g.p[i];
			if (p->n != P.ed.sel)
				continue;
			double px = 12 + p->x, py = 60 + 12 + p->y;
			if (x >= px && x < px + p->w + 12 && y >= py && y < py + p->h + 12)
				P.ed_drag = x > px + p->w - 14 || y > py + p->h - 14 ? 2 : 1;
		}
		P.ed_x0 = x, P.ed_y0 = y;
		return;
	}
	const struct vhit *h = P.refused || P.v.tm ? NULL : view_hit(&P.v, x, y);
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

static void editor_drag_end(double x, double y)
{
	struct view *v = &P.v;
	double dx = x - P.ed_x0, dy = y - P.ed_y0;
	if (P.ed_drag == 2) { /* the edge: a weight step per 60 px along the drag */
		int steps = (int)lround((fabs(dx) > fabs(dy) ? dx : dy) / 60);
		for (int i = 0; i < abs(steps); i++)
			editor_act(&P.ed, steps > 0 ? ED_MORE : ED_LESS, 0, NULL);
		editor_relayout();
		return;
	}
	for (int i = 0; i < v->g.n; i++) { /* the widget: onto the nearest edge of the one under the finger */
		const struct placed *p = &v->g.p[i];
		double px = 12 + p->x, py = 60 + 12 + p->y;
		if (p->n == P.ed.sel || x < px || x >= px + p->w || y < py || y >= py + p->h)
			continue;
		double d[4] = { x - px, px + p->w - x, y - py, py + p->h - y };
		int side = 0;
		for (int k = 1; k < 4; k++)
			if (d[k] < d[side])
				side = k;
		P.ed.target = (struct tnode *)p->n;
		editor_act(&P.ed, ED_DROP, side, NULL);
		editor_relayout();
		return;
	}
}

static void finger_up(double x, double y)
{
	if (P.ed_drag && (fabs(x - P.tx) >= 24 || fabs(y - P.ty) >= 24)) {
		editor_drag_end(x, y);
		P.ed_drag = 0;
		return;
	}
	P.ed_drag = 0;
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
	y += P.v.scroll;
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
	if (P.v.tm)
		tm_close(); /* writing with the pen goes back to ink (FR-012) */
	P.tool_down = true;
	P.pen_down = true;
	down_x = P.tool_x, down_y = P.tool_y;
}
static void tt_up(void *d, struct zwp_tablet_tool_v2 *t)
{
	P.tool_down = false;
	P.pen_down = false;
	P.pen_up_ms = now_ms();
	if (fabs(P.tool_x - down_x) < 8 && fabs(P.tool_y - down_y) < 8 && !in_field(down_x, down_y) && !P.v.tm)
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


/* ---- the keyboard: the system's on-screen keyboard (wl_keyboard), an input method
 * (text-input-v3) ---- */

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
	if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *m = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (m == MAP_FAILED)
		return;
	if (!P.xkb)
		P.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *km = P.xkb ? xkb_keymap_new_from_string(P.xkb, m, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
	munmap(m, size);
	if (!km)
		return;
	if (P.ks)
		xkb_state_unref(P.ks);
	if (P.km)
		xkb_keymap_unref(P.km);
	P.km = km;
	P.ks = xkb_state_new(km);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys) {}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) {}
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t st)
{
	if (st != WL_KEYBOARD_KEY_STATE_PRESSED || !P.ks || !(P.v.tm || P.v.search))
		return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(P.ks, key + 8);
	const struct vfield *f = focused();
	switch (sym) {
	case XKB_KEY_BackSpace: type_text(NULL, 1, 0); return;
	case XKB_KEY_Delete: type_text(NULL, 0, 1); return;
	case XKB_KEY_Left: caret_move(-1); return;
	case XKB_KEY_Right: caret_move(1); return;
	case XKB_KEY_Home: caret_move(-2); return;
	case XKB_KEY_End: caret_move(2); return;
	case XKB_KEY_Escape:
		if (P.v.search)
			P.v.search = false, ti_state(), P.dirty = true;
		else
			tm_close();
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (!P.v.search && f && f->h > 48)
			type_text("\n", 0, 0); /* a note's next line */
		else if (!P.v.search)
			tm_close();
		return;
	default: break;
	}
	char u[16];
	if (xkb_state_key_get_utf8(P.ks, key + 8, u, sizeof u) > 0 && (unsigned char)u[0] >= 0x20 && u[0] != 0x7f)
		type_text(u, 0, 0);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{
	if (P.ks)
		xkb_state_update_mask(P.ks, dep, lat, lock, 0, 0, grp);
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener kbd_listener = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
	P.ti_in = true;
	P.ti_on = false;
	ti_state();
}
static void ti_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
	P.ti_in = false;
	P.ti_on = false;
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *t, const char *text, int32_t b, int32_t e) {}
static void ti_commit(void *d, struct zwp_text_input_v3 *t, const char *text)
{
	free(P.ti_commit);
	P.ti_commit = text ? strdup(text) : NULL;
}
static void ti_delete(void *d, struct zwp_text_input_v3 *t, uint32_t before, uint32_t after)
{
	P.ti_before = before, P.ti_after = after;
}
static void ti_done(void *d, struct zwp_text_input_v3 *t, uint32_t serial)
{
	if (P.ti_commit || P.ti_before || P.ti_after)
		type_text(P.ti_commit, (int)P.ti_before, (int)P.ti_after); /* bytes; whole characters here */
	free(P.ti_commit);
	P.ti_commit = NULL;
	P.ti_before = P.ti_after = 0;
}
static const struct zwp_text_input_v3_listener ti_listener = { ti_enter, ti_leave, ti_preedit, ti_commit, ti_delete, ti_done };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !P.kbd) {
		P.kbd = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(P.kbd, &kbd_listener, NULL);
	}
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
	/* the keyboard came up over a text field: the window is shorter, the page is not laid
	 * out again; it moves up by whole rows to keep the field above the keyboard (FR-013) */
	int lh = P.lay_h ? P.lay_h : P.H;
	if (w > 0 && h > 0 && w == P.W && h < lh && (P.v.tm || P.v.search || P.lay_h)) {
		P.lay_h = lh;
		P.v.vis_h = h;
		P.H = h;
		free(P.last);
		P.last = NULL;
		const struct vfield *f = focused();
		P.v.scroll = f ? view_scroll_to(&P.v, f) : P.v.scroll;
		if (P.configured)
			layout();
		P.dirty = true;
		return;
	}
	if (w > 0 && h > 0 && P.lay_h && w == P.W && h >= P.lay_h) {
		/* the keyboard went: all of the page again */
		P.lay_h = 0;
		P.v.vis_h = 0;
		P.v.scroll = 0;
		P.tm_left_ms = 0;
	}
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
	else if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
		P.tim = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
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
	tmpl_user_load(dir);
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
	if (P.tim && P.seat) {
		P.ti = zwp_text_input_manager_v3_get_text_input(P.tim, P.seat);
		zwp_text_input_v3_add_listener(P.ti, &ti_listener, NULL);
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
		if (P.tm_left_ms && !P.v.tm && !P.v.search && now_ms() - P.tm_left_ms > KBD_GONE_MS) {
			/* text mode was left and the keyboard stays: the page fits what is left */
			P.tm_left_ms = 0;
			P.lay_h = 0, P.v.vis_h = 0, P.v.scroll = 0;
			relayout_later();
		}
		if (P.find && !P.relayout) {
			/* a search result whose view has no place for it: the hidden list (FR-050) */
			uint32_t id = P.find;
			P.find = 0;
			struct entry *fe = store_get(&P.s, id);
			if (fe && !view_field_of(&P.v, id) && fe->kind != E_MARK) {
				struct hidden_group g[16];
				int ng = view_hidden(&P.s, P.v.t, P.v.per, g, 16);
				for (int i = 0; i < ng; i++)
					if (g[i].kind == fe->kind && !strcmp(g[i].slot, fe->slot)) {
						P.v.ov = OV_HIDDEN;
						P.v.hidden_kind = i;
						relayout_later();
					}
			}
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
		if (P.tm_left_ms) {
			long long left = KBD_GONE_MS - (now_ms() - P.tm_left_ms);
			int t2 = left > 0 ? (int)left + 5 : 5;
			timeout = timeout < 0 || t2 < timeout ? t2 : timeout;
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
