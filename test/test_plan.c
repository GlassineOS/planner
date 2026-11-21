// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The core without Wayland: template switches keep every entry, the notice counts what
 * isn't shown, the more mark, the hold while the pen writes, event drags in 30-minute
 * steps, text mode, what each action damages; and an offscreen render of every template
 * in every voice, portrait and landscape, into out/renders/ (never committed).
 */
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "../src/plan.h"
#include "t.h"

static struct plan P;
static long long T = 1000000;

static void tick(long long ms)
{
	T += ms;
	plan_tick(&P, T);
}

static void fresh(const char *dir)
{
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", dir, dir);
	CHECK(system(cmd) == 0);
	char err[200];
	setenv("PLANNER_TODAY", "2031-03-12", 1);
	CHECK(plan_init(&P, "templates", dir, NULL, err, sizeof err));
	plan_resize(&P, 702, 936, 1);
	plan_seed_demo(&P);
	tick(0);
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return strdup("");
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = calloc(n + 1, 1);
	if (fread(b, 1, n, f) != (size_t)n)
		b[0] = 0;
	fclose(f);
	return b;
}

static const struct fbox *field_of(int entry, enum ekind k)
{
	for (int i = 0; i < P.nfb; i++)
		if (P.fb[i].entry == entry && P.fb[i].kind == k)
			return &P.fb[i];
	return NULL;
}

static const struct hit *hit_of(enum act a, int arg)
{
	for (int i = 0; i < P.nhits; i++)
		if (P.hits[i].act == a && (arg < 0 || P.hits[i].a == arg))
			return &P.hits[i];
	return NULL;
}

static int widget_of(const char *type)
{
	for (int i = 0; i < P.nw; i++)
		if (!strcmp(P.cur->n[i].type, type))
			return i;
	return -1;
}

static void switching_keeps_entries(void)
{
	fresh("out/test-plan/switch");
	char *before = slurp(P.entries_path);
	CHECK(strlen(before) > 100);
	const char *day[] = { "day-agenda", "day-lists", "day-full" }, *week[] = { "week-focus", "week-columns" };
	for (int i = 0; i < 3; i++) {
		CHECK(plan_use_template(&P, day[i]));
		tick(10);
	}
	for (int i = 0; i < 2; i++) {
		CHECK(plan_use_template(&P, week[i]));
		tick(10);
	}
	plan_set_day(&P, P_DAY, P.today);
	tick(10);
	char *after = slurp(P.entries_path);
	CHECK_STR(after, before);
	free(before);
	free(after);
	plan_free(&P);
}

static void notice_counts_hidden(void)
{
	fresh("out/test-plan/hidden");
	CHECK(plan_use_template(&P, "day-full"));
	CHECK_INT(P.hidden[E_EVENT] + P.hidden[E_TODO] + P.hidden[E_NOTE], 0);
	CHECK(plan_use_template(&P, "day-agenda"));
	CHECK_INT(P.hidden[E_TODO], 3); /* the day's; the week's to-dos aren't the day's (FR-023) */
	CHECK_INT(P.hidden[E_NOTE], 1);
	CHECK_INT(P.hidden[E_EVENT], 0);
	tick(10);
	CHECK(hit_of(A_HIDDEN, -1) != NULL);
	CHECK(plan_use_template(&P, "day-lists"));
	CHECK_INT(P.hidden[E_EVENT], 4);
	CHECK(plan_use_template(&P, "week-focus"));
	CHECK_INT(P.hidden[E_EVENT], 9);
	CHECK_INT(P.hidden[E_TODO], 3); /* the week holds the day's to-dos; this template has only the week's */
	CHECK(plan_use_template(&P, "week-columns"));
	CHECK_INT(P.hidden[E_EVENT], 0);
	/* ... and the day's template is still the day's own */
	plan_set_day(&P, P_DAY, P.today);
	CHECK_STR(P.cur->id, "day-lists");
	plan_free(&P);
}

static void more_mark(void)
{
	fresh("out/test-plan/more");
	CHECK(plan_use_template(&P, "day-full"));
	for (int i = 0; i < 14; i++) {
		struct entry *e = store_add(&P.st, E_TODO, (struct period){ P_DAY, P.today });
		synth_words(&e->f, 4, 22, 8, 2, 100 + i);
	}
	plan_set_day(&P, P_DAY, P.today);
	tick(10);
	int w = widget_of("todo");
	CHECK(w >= 0 && P.w[w].more > 0);
	CHECK(P.w[w].ln.clipped);
	CHECK(hit_of(A_MORE, -1) != NULL);
	/* every widget still has at least its minimum */
	for (int i = 0; i < P.nw; i++)
		if (P.w[i].ln.kind == LAY_LEAF)
			CHECK(P.w[i].ln.h >= P.w[i].ln.min_h - 0.5);
	plan_free(&P);
}

static void stroke(double x, double y, int n, double dx, double dy)
{
	plan_down(&P, T_PEN, x, y, 0.6);
	for (int i = 1; i <= n; i++) {
		T += 8;
		P.now = T;
		plan_motion(&P, x + dx * i, y + dy * i + 3 * sin(i), 0.6);
	}
	plan_up(&P);
}

static void hold_while_writing(void)
{
	fresh("out/test-plan/hold");
	CHECK(plan_use_template(&P, "day-lists"));
	tick(10);
	int ntodo = 0;
	for (int i = 0; i < P.st.n; i++)
		ntodo += P.st.e[i].kind == E_TODO;
	const struct fbox *empty = NULL;
	for (int i = 0; i < P.nfb; i++)
		if (!P.fb[i].entry && P.fb[i].kind == E_TODO && P.fb[i].per.kind == P_DAY)
			empty = &P.fb[i];
	CHECK(empty != NULL);
	if (!empty)
		return;
	struct rect er = empty->r;
	int relayouts = P.relayouts, paints = P.paints;
	plan_action(&P, "test strokes");
	stroke(er.x + 10, er.y + 18, 12, 4, 0);
	tick(200);
	/* a second stroke, in the same place, below the row's line: the row will grow */
	stroke(er.x + 70, er.y + 14, 12, 3, 2.4);
	tick(200);
	int now = 0;
	for (int i = 0; i < P.st.n; i++)
		now += P.st.e[i].kind == E_TODO;
	CHECK_INT(now, ntodo + 1);           /* one new to-do, both strokes in it */
	CHECK_INT(P.relayouts, relayouts);   /* nothing moved while writing */
	CHECK_INT(P.paints, paints);
	CHECK_INT(P.led[P.nled - 1].layout, 0);
	CHECK(P.led[P.nled - 1].commits >= 12); /* the second stroke's segments */
	CHECK(P.led[P.nled - 1].biggest < 0.002); /* each a segment's box */
	tick(PAUSE_MS);
	CHECK_INT(P.relayouts, relayouts + 1); /* settled once, after the pause */
	CHECK_INT(P.paints, paints + 1);
	CHECK_STR(P.led[P.nled - 1].what, "settle after pause");
	CHECK(P.led[P.nled - 1].biggest < 0.6); /* the settle redraws the widgets that changed, not the panel */
	/* the new to-do has a reading, and a new empty row is below it */
	struct entry *e = &P.st.e[P.st.n - 1];
	CHECK(e->f.reading[0] != 0);
	CHECK_INT(e->f.ns, 2);
	int empties = 0;
	for (int i = 0; i < P.nfb; i++)
		if (!P.fb[i].entry && P.fb[i].kind == E_TODO && P.fb[i].per.kind == P_DAY && P.fb[i].r.y > er.y)
			empties++;
	CHECK_INT(empties, 1);
	/* the ink is anchored to its field, unscaled: the row grew to hold it */
	const struct fbox *f = field_of(e->id, E_TODO);
	double r, b;
	field_extent(&e->f, &r, &b);
	CHECK(f && f->r.h >= b);
	plan_free(&P);
}

static void event_drag(void)
{
	fresh("out/test-plan/drag");
	CHECK(plan_use_template(&P, "day-full"));
	tick(10);
	struct entry *ev = NULL;
	for (int i = 0; i < P.st.n; i++)
		if (P.st.e[i].kind == E_EVENT && P.st.e[i].per.start == P.today && P.st.e[i].start == 9 * 60)
			ev = &P.st.e[i];
	CHECK(ev != NULL);
	if (!ev)
		return;
	const struct hit *h = hit_of(A_EV_MOVE, ev->id);
	CHECK(h != NULL);
	if (!h)
		return;
	double pitch = P.w[h->b].pitch, x = h->r.x + h->r.w / 2, y = h->r.y + h->r.h / 2;
	int paints = P.paints;
	plan_down(&P, T_PEN, x, y, 0.5);
	for (int i = 1; i <= 20; i++)
		plan_motion(&P, x, y + pitch * 1.6 * i / 20, 0.5);
	plan_up(&P);
	CHECK_INT(ev->start, 10 * 60 + 30); /* 96 minutes -> 90 */
	CHECK_INT(ev->end, 11 * 60 + 30);
	CHECK_INT(P.paints - paints, 3);     /* a redraw per step: 30, 60, 90 */
	CHECK(P.led[P.nled - 1].biggest < 0.3);
	tick(10);
	/* stretch its lower edge by 40 minutes -> 30 */
	h = hit_of(A_EV_SIZE, ev->id);
	CHECK(h != NULL);
	if (h) {
		plan_down(&P, T_PEN, h->r.x + 20, h->r.y + 6, 0.5);
		plan_motion(&P, h->r.x + 20, h->r.y + 6 + pitch * 40 / 60, 0.5);
		plan_up(&P);
		CHECK_INT(ev->end, 12 * 60);
		CHECK_INT(ev->start, 10 * 60 + 30);
	}
	tick(10);
	/* drawing a block in free time: a new event */
	int w = widget_of("agenda");
	int n = P.st.n;
	double by = P.w[w].body.y + (17 - P.w[w].from) * P.w[w].pitch + 2;
	plan_down(&P, T_PEN, P.w[w].body.x + 30, by, 0.5);
	plan_motion(&P, P.w[w].body.x + 30, by + P.w[w].pitch * 1.4, 0.5);
	plan_up(&P);
	CHECK_INT(P.st.n, n + 1);
	CHECK_INT(P.st.e[P.st.n - 1].start, 17 * 60);
	CHECK_INT(P.st.e[P.st.n - 1].end, 18 * 60 + 30);
	plan_free(&P);
}

static void text_mode(void)
{
	fresh("out/test-plan/text");
	CHECK(plan_use_template(&P, "day-full"));
	tick(10);
	struct entry *t = NULL;
	for (int i = 0; i < P.st.n; i++)
		if (P.st.e[i].kind == E_TODO && P.st.e[i].per.kind == P_DAY && !t)
			t = &P.st.e[i];
	const struct fbox *f = field_of(t->id, E_TODO);
	CHECK(f != NULL);
	if (!f)
		return;
	char reading[FIELD_TEXT];
	snprintf(reading, sizeof reading, "%s", t->f.reading);
	plan_down(&P, T_TOUCH, f->r.x + 20, f->r.y + 10, 0);
	plan_up(&P);
	CHECK(t->f.textmode);
	CHECK_INT(P.focus, t->id);
	plan_text(&P, " x");
	plan_backspace(&P);
	plan_text(&P, "now");
	char want[FIELD_TEXT + 8];
	snprintf(want, sizeof want, "%s now", reading);
	CHECK_STR(field_text(&t->f), want);
	CHECK_STR(t->f.reading, reading); /* the reading stays; the text is the edit */
	tick(10);
	plan_key_done(&P);
	CHECK(!t->f.textmode);
	CHECK_INT(P.focus, 0);
	CHECK(strstr(slurp(P.entries_path), want) != NULL);
	/* the whole view */
	plan_key(&P, "text");
	CHECK(P.text_view);
	tick(10);
	plan_key(&P, "text");
	plan_free(&P);
}

static void renders(void)
{
	mkdir("out/renders", 0755);
	const char *voices[] = { "white", "editorial", "stationery" };
	fresh("out/test-plan/renders");
	for (int t = 0; t < P.ntm; t++)
		for (int v = 0; v < 3; v++)
			for (int o = 0; o < 2; o++) {
				plan_set_voice(&P, voices[v]);
				plan_resize(&P, o ? 936 : 702, o ? 702 : 936, 2);
				plan_set_day(&P, P.tm[t].kind, P.today);
				CHECK(plan_use_template(&P, P.tm[t].id));
				plan_paint(&P);
				char path[200];
				snprintf(path, sizeof path, "out/renders/%s-%s-%s.png", P.tm[t].id, voices[v], o ? "landscape" : "portrait");
				CHECK(cairo_surface_write_to_png(P.cur_canvas, path) == CAIRO_STATUS_SUCCESS);
				/* every widget its minimum, unless the template reports it can't fit */
				for (int i = 0; i < P.nw; i++)
					if (P.w[i].ln.kind == LAY_LEAF && !P.rep.infeasible_h)
						CHECK(P.w[i].ln.h >= P.w[i].ln.min_h - 0.5);
				if (v == 0)
					printf("render %-13s %-9s widths %s, heights %s, %d clipped\n", P.tm[t].id, o ? "landscape" : "portrait",
					       P.rep.infeasible_w ? "SHORT" : "fit", P.rep.infeasible_h ? "SHORT" : "fit", P.rep.clipped);
			}
	plan_free(&P);
}

int main(void)
{
	ui_draw_init();
	switching_keeps_entries();
	notice_counts_hidden();
	more_mark();
	hold_while_writing();
	event_drag();
	text_mode();
	renders();
	return t_done("plan");
}
