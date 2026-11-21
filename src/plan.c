// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The planner's core (plan.h): the view from a template, the fluid grid, input (pen,
 * eraser, touch, keys), the hold while the pen writes, the stand-in reading after a
 * pause, painting with damage per widget, and the ledger of what each action cost.
 *
 * Writing: a stroke goes to the field it starts in (spec 005 FR-005). Its segments are
 * drawn straight onto the shown canvas (only their boxes are committed); when the pen
 * lifts nothing else is painted. After a pause (PAUSE_MS without the pen) the held
 * layout settles in one paint, and the stand-in reads the field (FR-007): in ink mode
 * the reading changes nothing on screen (FR-005).
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "plan.h"

#define TILE 16

/* ---- the ledger ------------------------------------------------------------------------ */

void plan_action(struct plan *p, const char *what)
{
	if (p->nled == LEDGER_N) {
		memmove(p->led, p->led + 1, sizeof p->led[0] * (LEDGER_N - 1));
		p->nled--;
	}
	struct ledger_row *l = &p->led[p->nled++];
	memset(l, 0, sizeof *l);
	snprintf(l->what, sizeof l->what, "%s", what);
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: action %s\n", what);
}

static struct ledger_row *cur_row(struct plan *p)
{
	if (!p->nled)
		plan_action(p, "start");
	return &p->led[p->nled - 1];
}

static void flow(struct plan *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void flow(struct plan *p, const char *fmt, ...)
{
	for (int i = 0; i < 3; i++)
		memcpy(p->flow[i], p->flow[i + 1], sizeof p->flow[i]);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->flow[3], sizeof p->flow[3], fmt, ap);
	va_end(ap);
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: flow %s\n", p->flow[3]);
}

static void commit(struct plan *p, const struct rect *r, int n)
{
	if (!n)
		return;
	struct ledger_row *l = cur_row(p);
	double panel = (double)p->W * p->S * p->H * p->S, sum = 0;
	for (int i = 0; i < n; i++) {
		double a = r[i].w * r[i].h;
		sum += a;
		if (a / panel > l->biggest)
			l->biggest = a / panel;
	}
	l->commits++;
	l->area += sum;
	p->commits++;
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: commit %s rects=%d area=%.2f%%\n", l->what, n, 100 * sum / panel);
	if (p->io.commit)
		p->io.commit(p->io.ud, r, n);
}

/* ---- the view: widgets from the template --------------------------------------------- */

static enum wtype wtype_of(const char *s)
{
	if (!strcmp(s, "row"))
		return W_ROW;
	if (!strcmp(s, "column"))
		return W_COL;
	if (!strcmp(s, "agenda"))
		return W_AGENDA;
	if (!strcmp(s, "todo"))
		return W_TODO;
	if (!strcmp(s, "notes"))
		return W_NOTES;
	if (!strcmp(s, "minimonth"))
		return W_MINIMONTH;
	return W_UNKNOWN;
}

#define CARD_PAD 32 /* 2 * (PAD + IN) of paint.c */

/* what each widget declares (FR-041); a template may override per use */
static void declare(struct widget *w, const struct tnode *n)
{
	struct lay_node *l = &w->ln;
	switch (w->type) {
	case W_AGENDA:
		l->min_w = w->compact ? 84 : 200;
		l->min_h = CARD_PAD + (w->compact ? 40 : 24) + (w->to - w->from) * (w->compact ? 12 : 18);
		l->grow = true;
		break;
	case W_TODO:
		l->min_w = 170;
		l->min_h = CARD_PAD + 24 + 2 * 36;
		break;
	case W_NOTES:
		l->min_w = 170;
		l->min_h = CARD_PAD + 24 + 2 * 34;
		l->grow = true;
		break;
	case W_MINIMONTH:
		l->min_w = 196;
		l->pref_w = 230;
		l->min_h = CARD_PAD + 24 + 7 * 22;
		break;
	default:
		break;
	}
	l->weight = tnode_num(n, "weight", 1);
	l->min_w = tnode_num(n, "min_w", l->min_w);
	l->min_h = tnode_num(n, "min_h", l->min_h);
	l->pref_h = tnode_num(n, "pref_h", 0);
	if (tnode_get(n, "grow"))
		l->grow = tnode_flag(n, "grow");
	if (w->type != W_ROW && w->type != W_COL) {
		l->kind = LAY_LEAF;
		l->want_h = widget_want_h;
		l->ud = w;
	}
}

static void build(struct plan *p)
{
	memset(p->w, 0, sizeof p->w);
	p->nw = p->cur ? p->cur->nn : 0;
	for (int i = 0; i < p->nw; i++) {
		const struct tnode *n = &p->cur->n[i];
		struct widget *w = &p->w[i];
		w->tn = i;
		/* a node may change kind with the orientation: `column landscape=row` */
		const char *alt = tnode_get(n, p->W > p->H ? "landscape" : "portrait");
		w->type = wtype_of(alt ? alt : n->type);
		w->day = (int)tnode_num(n, "day", 0);
		w->from = (int)tnode_num(n, "from", 7);
		w->to = (int)tnode_num(n, "to", 21);
		if (w->to <= w->from)
			w->to = w->from + 1;
		w->compact = tnode_flag(n, "compact");
		const char *per = tnode_get(n, "period");
		w->per = per && pkind_parse(per) >= 0 ? (enum pkind)pkind_parse(per) : p->per.kind;
		snprintf(w->title, sizeof w->title, "%s", tnode_get(n, "title") ? tnode_get(n, "title") : "");
		w->ln.kind = w->type == W_ROW ? LAY_ROW : w->type == W_COL ? LAY_COL : LAY_LEAF;
		declare(w, n);
		for (int k = 0; k < n->nkids; k++)
			w->ln.kids[w->ln.nkids++] = &p->w[n->kids[k]].ln;
	}
	p->layout_valid = false;
}

/* FR-023: entries of the view's period that no widget shows */
static bool shown_by(const struct plan *p, const struct entry *e)
{
	for (int i = 0; i < p->nw; i++) {
		const struct widget *w = &p->w[i];
		if (w->type == W_AGENDA && e->kind == E_EVENT && e->per.start == p->per.start + w->day)
			return true;
		if ((w->type == W_TODO && e->kind == E_TODO) || (w->type == W_NOTES && e->kind == E_NOTE)) {
			struct period wp = period_of(w->per, p->per.start);
			if (e->per.kind == wp.kind && e->per.start == wp.start)
				return true;
		}
	}
	return false;
}

static void count_hidden(struct plan *p)
{
	memset(p->hidden, 0, sizeof p->hidden);
	for (int i = 0; i < p->st.n; i++) {
		const struct entry *e = &p->st.e[i];
		if (e->deleted || !period_contains(p->per, e->per))
			continue;
		if (!shown_by(p, e))
			p->hidden[e->kind]++;
	}
}

static void relayout(struct plan *p)
{
	if (!p->nw || !p->W)
		return;
	count_hidden(p);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(s);
	paint_view(p, cr, true); /* the measuring context for widget_want_h */
	for (int pass = 0; pass < 2; pass++) {
		double top = paint_top(p);
		lay_solve(&p->w[0].ln, 0, top + 4, p->W, p->H - top - 8, 1, &p->rep);
		if (pass == 0 && paint_top(p) == top)
			break; /* the notice didn't appear or go */
	}
	cairo_destroy(cr);
	cairo_surface_destroy(s);
	p->layout_valid = true;
	p->relayouts++;
	cur_row(p)->layout++;
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: layout %s clipped=%d short=%.0f/%.0f\n", p->cur ? p->cur->id : "-", p->rep.clipped,
			p->rep.short_w, p->rep.short_h);
}

/* a layout change caused by content: held while the pen writes (FR-046) */
static void want_layout(struct plan *p)
{
	if (lay_hold_request(&p->hold, p->now))
		relayout(p);
	else if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: layout held\n");
}

/* ---- painting ---------------------------------------------------------------------------- */

static bool tile_differs(cairo_surface_t *a, cairo_surface_t *b, int x0, int y0, int x1, int y1)
{
	int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
	const unsigned char *da = cairo_image_surface_get_data(a), *db = cairo_image_surface_get_data(b);
	for (int y = y0; y < y1; y++)
		if (memcmp(da + (size_t)y * sa + x0 * 4, db + (size_t)y * sb + x0 * 4, (size_t)(x1 - x0) * 4))
			return true;
	return false;
}

static struct rect isect(struct rect a, struct rect b)
{
	double x0 = fmax(a.x, b.x), y0 = fmax(a.y, b.y), x1 = fmin(a.x + a.w, b.x + b.w), y1 = fmin(a.y + a.h, b.y + b.h);
	return x1 > x0 && y1 > y0 ? (struct rect){ x0, y0, x1 - x0, y1 - y0 } : (struct rect){ 0, 0, 0, 0 };
}

void plan_paint(struct plan *p)
{
	if (!p->W)
		return;
	if (!p->layout_valid)
		relayout(p);
	int dw = p->W * p->S, dh = p->H * p->S;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, dw, dh);
	cairo_t *cr = cairo_create(s);
	cairo_scale(cr, p->S, p->S);
	paint_view(p, cr, false);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	p->paints++;
	p->dirty = false;
	cairo_surface_t *old = p->cur_canvas;
	p->cur_canvas = s;
	struct rect dmg[MAX_DAMAGE];
	int nd = 0;
	if (!old || cairo_image_surface_get_width(old) != dw || cairo_image_surface_get_height(old) != dh) {
		dmg[nd++] = (struct rect){ 0, 0, dw, dh };
	} else {
		int tx = (dw + TILE - 1) / TILE, ty = (dh + TILE - 1) / TILE;
		unsigned char *ch = calloc((size_t)tx * ty, 1);
		int any = 0;
		for (int j = 0; j < ty; j++)
			for (int i = 0; i < tx; i++) {
				int x0 = i * TILE, y0 = j * TILE, x1 = x0 + TILE > dw ? dw : x0 + TILE, y1 = y0 + TILE > dh ? dh : y0 + TILE;
				ch[j * tx + i] = tile_differs(old, s, x0, y0, x1, y1);
				any += ch[j * tx + i];
			}
		/* per region: the box of its changed tiles; tiles of no region: one box */
		unsigned char *used = calloc((size_t)tx * ty, 1);
		for (int r = 0; r < p->nregions && any; r++) {
			struct rect rg = { p->regions[r].x * p->S, p->regions[r].y * p->S, p->regions[r].w * p->S,
					   p->regions[r].h * p->S };
			double bx0 = 1e9, by0 = 1e9, bx1 = -1, by1 = -1;
			for (int j = (int)(rg.y / TILE); j < ty && j * TILE < rg.y + rg.h; j++)
				for (int i = (int)(rg.x / TILE); i < tx && i * TILE < rg.x + rg.w; i++) {
					if (!ch[j * tx + i])
						continue;
					struct rect t = isect((struct rect){ i * TILE, j * TILE, TILE, TILE }, rg);
					if (!t.w)
						continue;
					bx0 = fmin(bx0, t.x);
					by0 = fmin(by0, t.y);
					bx1 = fmax(bx1, t.x + t.w);
					by1 = fmax(by1, t.y + t.h);
					/* a tile wholly inside the region is accounted for */
					if (t.w == TILE && t.h == TILE)
						used[j * tx + i] = 1;
				}
			if (bx1 > bx0 && nd < MAX_DAMAGE - 1)
				dmg[nd++] = (struct rect){ floor(bx0), floor(by0), ceil(bx1 - bx0), ceil(by1 - by0) };
		}
		double bx0 = 1e9, by0 = 1e9, bx1 = -1, by1 = -1;
		for (int j = 0; j < ty; j++)
			for (int i = 0; i < tx; i++)
				if (ch[j * tx + i] && !used[j * tx + i]) {
					bool inside = false;
					for (int k = 0; k < nd && !inside; k++)
						inside = isect((struct rect){ i * TILE, j * TILE, TILE, TILE }, dmg[k]).w >= TILE - 0.5 &&
							 isect((struct rect){ i * TILE, j * TILE, TILE, TILE }, dmg[k]).h >= TILE - 0.5;
					if (inside)
						continue;
					bx0 = fmin(bx0, i * TILE);
					by0 = fmin(by0, j * TILE);
					bx1 = fmax(bx1, fmin(dw, (i + 1) * TILE));
					by1 = fmax(by1, fmin(dh, (j + 1) * TILE));
				}
		if (bx1 > bx0)
			dmg[nd++] = (struct rect){ bx0, by0, bx1 - bx0, by1 - by0 };
		free(ch);
		free(used);
	}
	if (old)
		cairo_surface_destroy(old);
	commit(p, dmg, nd);
}

/* ---- setup --------------------------------------------------------------------------------- */

static const struct tmpl *find_tmpl(struct plan *p, const char *id, int kind)
{
	for (int i = 0; i < p->ntm; i++)
		if (!strcmp(p->tm[i].id, id) && p->tm[i].kind == kind)
			return &p->tm[i];
	for (int i = 0; i < p->ntm; i++)
		if (p->tm[i].kind == kind)
			return &p->tm[i];
	return NULL;
}

static void save(struct plan *p)
{
	if (p->entries_path[0] && !store_save_entries(&p->st, p->entries_path))
		fprintf(stderr, "planner: can't write %s: %s\n", p->entries_path, strerror(errno));
}

static void save_choices(struct plan *p)
{
	if (p->choices_path[0])
		store_save_choices(&p->st, p->choices_path);
}

void plan_set_day(struct plan *p, enum pkind k, int day)
{
	p->per = period_of(k, day);
	p->cur = find_tmpl(p, store_choice(&p->st, p->per), k);
	p->focus = 0;
	build(p);
	relayout(p);
	p->dirty = true;
}

bool plan_use_template(struct plan *p, const char *id)
{
	for (int i = 0; i < p->ntm; i++)
		if (!strcmp(p->tm[i].id, id)) {
			if (p->tm[i].kind != (int)p->per.kind)
				plan_set_day(p, p->tm[i].kind, p->per.start);
			store_choose(&p->st, p->per, id);
			save_choices(p);
			p->cur = &p->tm[i];
			build(p);
			relayout(p);
			p->dirty = true;
			return true;
		}
	return false;
}

void plan_next_template(struct plan *p)
{
	int at = p->cur ? (int)(p->cur - p->tm) : -1;
	for (int k = 1; k <= p->ntm; k++) {
		const struct tmpl *t = &p->tm[(at + k) % p->ntm];
		if (t->kind == (int)p->per.kind) {
			plan_use_template(p, t->id);
			return;
		}
	}
}

static int today_from_env(void)
{
	int d = day_parse(getenv("PLANNER_TODAY"));
	if (d >= 0)
		return d;
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return day_from_ymd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

bool plan_init(struct plan *p, const char *tmpl_dir, const char *data_dir, const struct plan_io *io, char *err, int errlen)
{
	memset(p, 0, sizeof *p);
	if (io)
		p->io = *io;
	p->v = ui_voice_find(NULL);
	p->S = 1;
	p->stroke_field = -1;
	lay_hold_init(&p->hold, PAUSE_MS);
	store_init(&p->st);
	p->ntm = tmpl_load_dir(p->tm, MAX_TMPL, tmpl_dir, err, errlen);
	if (p->ntm <= 0) {
		if (p->ntm == 0)
			snprintf(err, errlen, "%s: no templates", tmpl_dir);
		return false;
	}
	if (data_dir) {
		mkdir(data_dir, 0700);
		snprintf(p->entries_path, sizeof p->entries_path, "%s/entries.txt", data_dir);
		snprintf(p->choices_path, sizeof p->choices_path, "%s/choices.txt", data_dir);
		store_load(&p->st, p->entries_path, p->choices_path);
	}
	p->today = today_from_env();
	plan_set_day(p, P_DAY, p->today);
	snprintf(p->flow[3], sizeof p->flow[3], "write in a field: its ink, the stand-in reading and the stored entry show here");
	return true;
}

void plan_free(struct plan *p)
{
	store_free(&p->st);
	if (p->cur_canvas)
		cairo_surface_destroy(p->cur_canvas);
	free(p->cur_stroke.pt);
}

void plan_resize(struct plan *p, int W, int H, int S)
{
	if (W == p->W && H == p->H && S == p->S)
		return;
	p->W = W;
	p->H = H;
	p->S = S < 1 ? 1 : S;
	build(p); /* nodes may change kind with the orientation */
	relayout(p);
	p->dirty = true;
}

void plan_set_voice(struct plan *p, const char *name)
{
	p->v = ui_voice_find(name);
	relayout(p);
	p->dirty = true;
}

/* ---- demo data (synthetic ink only) --------------------------------------------------------- */

static void demo_field(struct field *f, int words, int lines, double xh, unsigned seed)
{
	for (int l = 0; l < lines; l++)
		synth_words(f, 4, 22 + l * 34, xh, words, seed * 31 + l);
	field_read_stub(f);
}

void plan_seed_demo(struct plan *p)
{
	if (p->st.n)
		return;
	int d = p->today, wk = period_of(P_WEEK, d).start;
	struct { int day, s, e, words; } ev[] = {
		{ d, 9 * 60, 10 * 60, 1 },        { d, 12 * 60 + 30, 13 * 60 + 30, 2 }, { d, 15 * 60, 16 * 60 + 30, 2 },
		{ d, 22 * 60, 23 * 60, 1 },       { wk, 10 * 60, 11 * 60, 1 },          { wk + 1, 14 * 60, 15 * 60 + 30, 2 },
		{ wk + 3, 8 * 60 + 30, 9 * 60 + 30, 1 }, { wk + 4, 16 * 60, 17 * 60, 1 }, { wk + 5, 11 * 60, 13 * 60, 2 },
	};
	unsigned seed = 1;
	for (size_t i = 0; i < sizeof ev / sizeof *ev; i++) {
		struct entry *e = store_add(&p->st, E_EVENT, (struct period){ P_DAY, ev[i].day });
		e->start = ev[i].s;
		e->end = ev[i].e;
		synth_words(&e->f, 3, 13, 6, ev[i].words, seed++);
		field_read_stub(&e->f);
	}
	for (int i = 0; i < 3; i++) {
		struct entry *e = store_add(&p->st, E_TODO, (struct period){ P_DAY, d });
		demo_field(&e->f, 2 + i, 1, 8, seed++);
		e->done = i == 1;
	}
	struct period w = period_of(P_WEEK, d);
	for (int i = 0; i < 6; i++) {
		struct entry *e = store_add(&p->st, E_TODO, w);
		/* the fourth is a long one: wider than a narrow column */
		demo_field(&e->f, i == 3 ? 7 : 2 + i % 3, 1, 8, seed++);
		e->done = i < 2;
	}
	struct entry *n = store_add(&p->st, E_NOTE, (struct period){ P_DAY, d });
	demo_field(&n->f, 4, 3, 8, seed++);
	n = store_add(&p->st, E_NOTE, w);
	demo_field(&n->f, 5, 2, 8, seed++);
	save(p);
	build(p);
	relayout(p);
	p->dirty = true;
}

/* ---- input ------------------------------------------------------------------------------------- */

static bool in(struct rect r, double x, double y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static const struct hit *hit_at(struct plan *p, double x, double y, bool agenda)
{
	for (int i = p->nhits - 1; i >= 0; i--) {
		const struct hit *h = &p->hits[i];
		if ((h->act == A_AGENDA) != agenda)
			continue;
		if (in(h->r, x, y))
			return h;
	}
	return NULL;
}

static int field_at(struct plan *p, double x, double y)
{
	for (int i = p->nfb - 1; i >= 0; i--)
		if (in(p->fb[i].r, x, y))
			return i;
	return -1;
}

static int snap(int min)
{
	return (int)lround(min / (double)STEP_MIN) * STEP_MIN;
}

static int agenda_minute(const struct widget *w, double y)
{
	return w->from * 60 + (int)((y - w->body.y) / w->pitch * 60);
}

static void text_focus(struct plan *p, int entry)
{
	p->focus = entry;
	if (!p->io.text_input)
		return;
	struct entry *e = entry ? store_find(&p->st, entry) : NULL;
	if (!e) {
		p->io.text_input(p->io.ud, false, NULL, NULL);
		return;
	}
	struct rect r = field_rect_for(p, entry);
	p->io.text_input(p->io.ud, true, &r, field_text(&e->f));
}

void plan_down(struct plan *p, enum tool t, double x, double y, double pressure)
{
	if (p->down)
		plan_cancel(p);
	p->down = true;
	p->tool = t;
	p->sx0 = p->lastx = x;
	p->sy0 = p->lasty = y;
	p->moved = false;
	p->press = (struct hit){ .act = A_NONE };
	p->stroke_field = -1;
	const struct hit *h = hit_at(p, x, y, false);
	int fi = field_at(p, x, y);
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: down %s %.0f,%.0f hit=%s field=%d\n", t == T_PEN ? "pen" : t == T_ERASER ? "eraser" : "touch",
			x, y, h ? (h->act == A_EV_MOVE ? "ev-move" : h->act == A_EV_SIZE ? "ev-size" : "other") : "-",
			fi >= 0 ? p->fb[fi].entry : -1);
	struct entry *fe = fi >= 0 && p->fb[fi].entry ? store_find(&p->st, p->fb[fi].entry) : NULL;
	bool text = fe && (p->text_view || fe->f.textmode);
	/* touch anywhere on an event's block moves it */
	if (t == T_TOUCH && !h && fe && fe->kind == E_EVENT && !text)
		h = &(struct hit){ p->fb[fi].r, A_EV_MOVE, fe->id, p->fb[fi].widget };
	if (h && !(fi >= 0 && h->act == A_EV_SIZE && t != T_TOUCH && y < h->r.y + 4)) {
		p->press = *h;
		if (h->act == A_EV_MOVE || h->act == A_EV_SIZE) {
			struct entry *e = store_find(&p->st, h->a);
			p->drag_start = e ? e->start : 0;
			p->drag_end = e ? e->end : 0;
			p->drag_steps = 0;
			plan_action(p, h->act == A_EV_MOVE ? "event drag" : "event stretch");
		}
		return;
	}
	if (fi >= 0 && (t == T_TOUCH || text)) {
		/* a finger (or the pen on a text field) taps a field: its text mode, on release */
		p->press = (struct hit){ p->fb[fi].r, A_NONE, p->fb[fi].entry, fi };
		p->stroke_field = -2 - fi;
		return;
	}
	if (fi >= 0) {
		/* a stroke: it belongs to this field, wherever it ends (spec 005 FR-005) */
		struct fbox *b = &p->fb[fi];
		plan_action(p, t == T_ERASER ? "erase stroke" : "pen stroke");
		lay_hold_pen(&p->hold, true, p->now);
		if (!b->entry) {
			if (t == T_ERASER)
				return;
			struct entry *e = store_add(&p->st, b->kind, b->per);
			if (!e)
				return;
			b->entry = e->id; /* the next strokes in this place join it */
			flow(p, "new %s #%d (%s %d): made by the first stroke in the empty field", ekind_name(b->kind, false), e->id,
			     pkind_name(b->per.kind), b->per.start);
		}
		p->stroke_field = fi;
		p->stroke_entry = b->entry;
		p->cur_stroke.n = 0;
		plan_motion(p, x, y, pressure);
		return;
	}
	h = hit_at(p, x, y, true);
	if (h && t != T_ERASER) {
		/* the pen in the agenda's free time draws a block: a new event */
		p->press = *h;
		struct widget *w = &p->w[h->b];
		p->drag_start = (int)floor(agenda_minute(w, y) / (double)STEP_MIN) * STEP_MIN;
		p->drag_end = p->drag_start + STEP_MIN;
		plan_action(p, "draw event block");
	}
}

static void stroke_point(struct plan *p, double x, double y, double pr)
{
	struct stroke *s = &p->cur_stroke;
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 128;
		s->pt = realloc(s->pt, sizeof *s->pt * s->cap);
	}
	struct fbox *b = &p->fb[p->stroke_field];
	s->pt[s->n++] = (struct pt){ (float)(x - b->ox), (float)(y - b->oy), (float)pr };
	if (!p->cur_canvas)
		return;
	/* the segment, straight onto the shown canvas: only its box is committed */
	const struct pt *a = s->n > 1 ? &s->pt[s->n - 2] : &s->pt[s->n - 1], *c = &s->pt[s->n - 1];
	cairo_t *cr = cairo_create(p->cur_canvas);
	cairo_scale(cr, p->S, p->S);
	if (p->tool == T_ERASER) {
		struct pt aa = *a, cc = *c;
		aa.p = cc.p = 4;
		ui_col keep = p->v->ink;
		UiVoice tmp = *p->v;
		tmp.ink = p->v->card.bg ? p->v->card.bg : UI_PAPER;
		const UiVoice *sv = p->v;
		p->v = &tmp;
		paint_stroke_segment(p, cr, b->ox, b->oy, &aa, &cc);
		p->v = sv;
		(void)keep;
	} else {
		paint_stroke_segment(p, cr, b->ox, b->oy, a, c);
	}
	cairo_destroy(cr);
	cairo_surface_flush(p->cur_canvas);
	double m = p->tool == T_ERASER ? 10 : 4;
	double x0 = fmin(a->x, c->x) + b->ox - m, y0 = fmin(a->y, c->y) + b->oy - m;
	double x1 = fmax(a->x, c->x) + b->ox + m, y1 = fmax(a->y, c->y) + b->oy + m;
	struct rect r = { floor(x0 * p->S), floor(y0 * p->S), ceil((x1 - x0) * p->S), ceil((y1 - y0) * p->S) };
	commit(p, &r, 1);
}

void plan_motion(struct plan *p, double x, double y, double pressure)
{
	if (!p->down)
		return;
	if (fabs(x - p->sx0) > 6 || fabs(y - p->sy0) > 6)
		p->moved = true;
	p->lastx = x;
	p->lasty = y;
	if (p->stroke_field >= 0) {
		stroke_point(p, x, y, pressure > 0 ? pressure : 0.5);
		return;
	}
	if (p->press.act == A_EV_MOVE || p->press.act == A_EV_SIZE) {
		struct widget *w = &p->w[p->press.b];
		struct entry *e = store_find(&p->st, p->press.a);
		if (!e || w->pitch <= 0)
			return;
		int delta = snap((int)((y - p->sy0) / w->pitch * 60));
		int s = e->start, en = e->end;
		if (p->press.act == A_EV_MOVE) {
			s += delta;
			en += delta;
			if (s < 0) {
				en -= s;
				s = 0;
			}
			if (en > 24 * 60) {
				s -= en - 24 * 60;
				en = 24 * 60;
			}
		} else {
			en += delta;
			if (en < s + STEP_MIN)
				en = s + STEP_MIN;
			if (en > 24 * 60)
				en = 24 * 60;
		}
		if (s != p->drag_start || en != p->drag_end) {
			p->drag_start = s;
			p->drag_end = en;
			p->drag_steps++;
			plan_paint(p); /* one local redraw per 30-minute step */
		}
		return;
	}
	if (p->press.act == A_AGENDA && p->moved) {
		struct widget *w = &p->w[p->press.b];
		int a = (int)floor(agenda_minute(w, fmin(y, p->sy0)) / (double)STEP_MIN) * STEP_MIN;
		int b = (int)ceil(agenda_minute(w, fmax(y, p->sy0)) / (double)STEP_MIN) * STEP_MIN;
		if (b <= a)
			b = a + STEP_MIN;
		if (a != p->drag_start || b != p->drag_end) {
			p->drag_start = a;
			p->drag_end = b;
			plan_paint(p);
		}
	}
}

static void erase(struct plan *p, struct entry *e, const struct stroke *er)
{
	int removed = 0;
	for (int i = 0; i < e->f.ns; i++) {
		bool hitit = false;
		for (int k = 0; k < e->f.s[i].n && !hitit; k++)
			for (int j = 0; j < er->n && !hitit; j++)
				hitit = hypot(e->f.s[i].pt[k].x - er->pt[j].x, e->f.s[i].pt[k].y - er->pt[j].y) < 8;
		if (hitit) {
			free(e->f.s[i].pt);
			memmove(&e->f.s[i], &e->f.s[i + 1], sizeof e->f.s[0] * (e->f.ns - i - 1));
			e->f.ns--;
			i--;
			removed++;
		}
	}
	if (removed) {
		e->f.ink_ms = p->now;
		e->f.read_strokes = -1;
		flow(p, "#%d: %d strokes erased", e->id, removed);
	}
}

static void tap(struct plan *p, const struct hit *h)
{
	switch (h->act) {
	case A_PREV:
	case A_NEXT: {
		int dir = h->act == A_PREV ? -1 : 1, d = p->per.start;
		if (p->per.kind == P_DAY)
			d += dir;
		else if (p->per.kind == P_WEEK)
			d += 7 * dir;
		plan_action(p, "period change");
		plan_set_day(p, p->per.kind, d);
		break;
	}
	case A_TODAY:
		plan_action(p, "today");
		plan_set_day(p, p->per.kind, p->today);
		break;
	case A_VIEW_DAY:
	case A_VIEW_WEEK:
		plan_action(p, "view change");
		plan_set_day(p, h->act == A_VIEW_DAY ? P_DAY : P_WEEK, p->per.kind == P_DAY ? p->per.start : p->today);
		break;
	case A_DAY:
		plan_action(p, "open day");
		plan_set_day(p, P_DAY, h->a);
		break;
	case A_TMPL:
	case A_HIDDEN:
		plan_action(p, "template switch");
		plan_next_template(p);
		break;
	case A_TEXT_VIEW:
		plan_action(p, "text mode (view)");
		p->text_view = !p->text_view;
		text_focus(p, 0);
		relayout(p);
		p->dirty = true;
		break;
	case A_DEBUG:
		plan_action(p, "debug overlay");
		p->debug = !p->debug;
		p->dirty = true;
		break;
	case A_CHECK: {
		struct entry *e = store_find(&p->st, h->a);
		if (e) {
			plan_action(p, "tick to-do");
			e->done = !e->done;
			save(p);
			flow(p, "#%d to-do: done=%d, stored", e->id, e->done);
			p->dirty = true;
		}
		break;
	}
	case A_MORE:
		plan_action(p, "more mark");
		flow(p, "more: a tap opens the widget's whole content (not built in the spike)");
		p->dirty = true;
		break;
	default:
		break;
	}
}

void plan_up(struct plan *p)
{
	if (!p->down)
		return;
	p->down = false;
	if (p->stroke_field >= 0) {
		lay_hold_pen(&p->hold, false, p->now);
		struct entry *e = store_find(&p->st, p->stroke_entry);
		if (e && p->cur_stroke.n) {
			if (p->tool == T_ERASER) {
				erase(p, e, &p->cur_stroke);
				p->dirty = true; /* the ink must go: paint the field (the layout stays held) */
			} else {
				field_add_stroke(&e->f, &p->cur_stroke);
				e->f.ink_ms = p->now;
				double r, b;
				field_extent(&e->f, &r, &b);
				flow(p, "#%d %s: stroke %d stored, ink %.0fx%.0f px (unscaled)", e->id, ekind_name(e->kind, false), e->f.ns, r, b);
			}
			save(p); /* FR-051: written as soon as it is written */
			want_layout(p);
		}
		p->stroke_field = -1;
		return;
	}
	if (p->stroke_field <= -2) {
		/* a field tapped by a finger, or by the pen in text mode */
		int fi = -2 - p->stroke_field;
		p->stroke_field = -1;
		if (p->moved || fi >= p->nfb)
			return;
		struct entry *e = p->fb[fi].entry ? store_find(&p->st, p->fb[fi].entry) : NULL;
		if (!e) {
			struct fbox *b = &p->fb[fi];
			e = store_add(&p->st, b->kind, b->per);
			if (!e)
				return;
			b->entry = e->id;
		}
		plan_action(p, "text mode (field)");
		if (!p->text_view)
			e->f.textmode = true;
		relayout(p);
		plan_paint(p);
		text_focus(p, e->id);
		p->dirty = true;
		return;
	}
	struct hit h = p->press;
	p->press.act = A_NONE;
	if (h.act == A_EV_MOVE || h.act == A_EV_SIZE) {
		struct entry *e = store_find(&p->st, h.a);
		if (e && (p->drag_start != e->start || p->drag_end != e->end)) {
			e->start = p->drag_start;
			e->end = p->drag_end;
			save(p);
			flow(p, "#%d event: %d:%02d-%d:%02d after %d steps, stored", e->id, e->start / 60, e->start % 60, e->end / 60,
			     e->end % 60, p->drag_steps);
		} else if (e && !p->moved && h.act == A_EV_MOVE && p->tool == T_TOUCH) {
			/* a finger tap on an event: its title in text mode */
			plan_action(p, "text mode (field)");
			if (!p->text_view)
				e->f.textmode = true;
			relayout(p);
			plan_paint(p);
			text_focus(p, e->id);
		}
		p->dirty = true;
		return;
	}
	if (h.act == A_AGENDA) {
		if (p->moved && p->drag_end > p->drag_start) {
			struct entry *e = store_add(&p->st, E_EVENT, (struct period){ P_DAY, h.a });
			if (e) {
				e->start = p->drag_start;
				e->end = p->drag_end;
				save(p);
				flow(p, "new event #%d %d:%02d-%d:%02d: write its title in the block", e->id, e->start / 60,
				     e->start % 60, e->end / 60, e->end % 60);
			}
		}
		p->dirty = true;
		return;
	}
	if (h.act != A_NONE && !p->moved)
		tap(p, &h);
}

void plan_cancel(struct plan *p)
{
	if (p->stroke_field >= 0)
		lay_hold_pen(&p->hold, false, p->now);
	p->down = false;
	p->stroke_field = -1;
	p->press.act = A_NONE;
	p->dirty = true;
}

/* ---- text ------------------------------------------------------------------------------------- */

static struct entry *focused(struct plan *p)
{
	struct entry *e = p->focus ? store_find(&p->st, p->focus) : NULL;
	if (e && !e->f.edited) {
		snprintf(e->f.text, sizeof e->f.text, "%s", e->f.reading);
		e->f.edited = true;
	}
	return e;
}

static void text_changed(struct plan *p, struct entry *e)
{
	save(p);
	flow(p, "#%d text mode: \"%s\" (edited text: the data's value now)", e->id, e->f.text);
	relayout(p); /* no pen: the layout follows at once */
	p->dirty = true;
	if (p->io.text_input) {
		struct rect r = field_rect_for(p, e->id);
		p->io.text_input(p->io.ud, true, &r, e->f.text);
	}
}

void plan_text(struct plan *p, const char *s)
{
	struct entry *e = focused(p);
	if (!e || !*s)
		return;
	plan_action(p, "type");
	size_t l = strlen(e->f.text);
	snprintf(e->f.text + l, sizeof e->f.text - l, "%s", s);
	text_changed(p, e);
}

void plan_backspace(struct plan *p)
{
	struct entry *e = focused(p);
	if (!e || !e->f.text[0])
		return;
	plan_action(p, "type");
	size_t l = strlen(e->f.text);
	while (l > 0 && (e->f.text[l - 1] & 0xc0) == 0x80)
		l--; /* a whole UTF-8 character */
	if (l > 0)
		l--;
	e->f.text[l] = 0;
	text_changed(p, e);
}

void plan_key_done(struct plan *p)
{
	struct entry *e = p->focus ? store_find(&p->st, p->focus) : NULL;
	if (!e)
		return;
	plan_action(p, "leave text mode");
	e->f.textmode = false;
	text_focus(p, 0);
	relayout(p);
	p->dirty = true;
}

void plan_key(struct plan *p, const char *name)
{
	if (!strcmp(name, "debug")) {
		plan_action(p, "debug overlay");
		p->debug = !p->debug;
		p->dirty = true;
	} else if (!strcmp(name, "template")) {
		plan_action(p, "template switch");
		plan_next_template(p);
	} else if (!strcmp(name, "text")) {
		tap(p, &(struct hit){ .act = A_TEXT_VIEW });
	}
}

/* ---- time ------------------------------------------------------------------------------------- */

int plan_tick(struct plan *p, long long now_ms)
{
	p->now = now_ms;
	long long next = -1;
	/* the stand-in reads a field once its ink has rested (FR-007) */
	for (int i = 0; i < p->st.n; i++) {
		struct entry *e = &p->st.e[i];
		if (e->f.read_strokes == e->f.ns)
			continue;
		if (p->down && p->stroke_entry == e->id)
			continue;
		long long due = e->f.ink_ms + PAUSE_MS - now_ms;
		if (due > 0) {
			if (next < 0 || due < next)
				next = due;
			continue;
		}
		char old[FIELD_TEXT];
		snprintf(old, sizeof old, "%s", e->f.reading);
		field_read_stub(&e->f);
		save(p);
		flow(p, "#%d reading (stub, after the pause): \"%s\" -> stored with the entry%s", e->id, e->f.reading,
		     p->debug ? "" : " (nothing on screen changes)");
		if (p->debug)
			p->dirty = true;
	}
	if (lay_hold_due(&p->hold, now_ms)) {
		plan_action(p, "settle after pause");
		relayout(p);
		p->dirty = true;
	} else {
		long long w = lay_hold_wait(&p->hold, now_ms);
		if (w >= 0 && (next < 0 || w < next))
			next = w;
	}
	if (p->dirty && !(p->down && p->stroke_field >= 0))
		plan_paint(p);
	return (int)next;
}

/* ---- the dump (the smoke run finds its targets here) --------------------------------------- */

static const char *act_name(enum act a)
{
	static const char *const n[] = { "none", "prev", "next", "today", "view-day", "view-week", "template", "text-view",
					 "debug", "day", "check", "ev-move", "ev-size", "agenda", "hidden", "more" };
	return n[a];
}

void plan_dump(struct plan *p, const char *path)
{
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return;
	fprintf(f, "size %d %d %d\ntemplate %s\nperiod %s %d\nhidden %d %d %d\nshort %.0f %.0f\nclipped %d\nrelayouts %d\n",
		p->W, p->H, p->S, p->cur ? p->cur->id : "-", pkind_name(p->per.kind), p->per.start, p->hidden[0], p->hidden[1],
		p->hidden[2], p->rep.short_w, p->rep.short_h, p->rep.clipped, p->relayouts);
	for (int i = 0; i < p->nhits; i++) {
		const struct hit *h = &p->hits[i];
		fprintf(f, "hit %s %d %d %.0f %.0f %.0f %.0f\n", act_name(h->act), h->a, h->b, h->r.x, h->r.y, h->r.w, h->r.h);
	}
	for (int i = 0; i < p->nfb; i++) {
		const struct fbox *b = &p->fb[i];
		fprintf(f, "field %d %s %d %.0f %.0f %.0f %.0f\n", b->entry, ekind_name(b->kind, false), b->widget, b->r.x, b->r.y,
			b->r.w, b->r.h);
	}
	for (int i = 0; i < p->nw; i++) {
		const struct widget *w = &p->w[i];
		if (w->ln.kind == LAY_LEAF)
			fprintf(f, "widget %d %s %.0f %.0f %.0f %.0f more=%d clipped=%d pitch=%.2f from=%d\n", i, p->cur->n[i].type,
				w->ln.x, w->ln.y, w->ln.w, w->ln.h, w->more, w->ln.clipped, w->pitch, w->from);
	}
	for (int i = 0; i < p->nled; i++)
		fprintf(f, "ledger %s|%d|%.0f|%.4f|%d\n", p->led[i].what, p->led[i].commits, p->led[i].area, p->led[i].biggest,
			p->led[i].layout);
	fclose(f);
	rename(tmp, path);
}
