// SPDX-License-Identifier: GPL-2.0-or-later
/* What a view change costs the planner's CPU, stage by stage: the layout, the whole frame
 * painted into the frame buffer (text, ink), and the comparison with the frame before, at a
 * screen's size and scale. Each action runs REPS times; the median is printed.
 *   bench [DATA_DIR]   a planner's data directory (read only), else the sample entries
 * Env: BENCH_W, BENCH_H (logical px, default 702 x 884), BENCH_SCALE (2), BENCH_REPS (9),
 * BENCH_NO_INK_CACHE=1 (every field's ink painted anew each time). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/frame.h"
#include "../src/tfile.h"
#include "../src/trace.h"
#include "../src/view.h"
#include "sample.h"

static struct view V;
static struct store S;
static struct look L;
static int SC = 2;

static void paint(void *ud, cairo_t *cr)
{
	cairo_scale(cr, SC, SC);
	view_paint(&V, &S, &L, cr);
}

static int cmpd(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static double med(double *a, int n)
{
	qsort(a, n, sizeof *a, cmpd);
	return a[n / 2];
}

/* from the view as it is to the one set by go(), REPS times (back to the first each time) */
static void act(const char *name, struct frame *fr, void (*from)(void), void (*go)(void), int reps)
{
	double lay[64], pnt[64], txt[64], ink[64], dif[64], all[64];
	int bw = (int)V.W * SC, bh = (int)V.H * SC, runs_n = 0;
	struct frect runs[128], box;
	cairo_surface_t *ms = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	cairo_t *mcr = cairo_create(ms);
	for (int r = 0; r < reps; r++) {
		from();
		view_layout(&V, &S, &L, mcr);
		frame_update(fr, bw, bh, NULL, paint, NULL, runs, 128, &box);
		go();
		double t0 = pl_tr_now();
		pl_tr_text_ms = 0, pl_tr_text_n = 0;
		view_layout(&V, &S, &L, mcr);
		double t1 = pl_tr_now();
		pl_tr_text_ms = 0, pl_tr_text_n = 0, pl_tr_ink_ms = 0, pl_tr_ink_n = 0;
		runs_n = frame_update(fr, bw, bh, NULL, paint, NULL, runs, 128, &box);
		double t2 = pl_tr_now();
		lay[r] = t1 - t0, pnt[r] = pl_tr_paint_ms, txt[r] = pl_tr_text_ms, ink[r] = pl_tr_ink_ms, dif[r] = pl_tr_diff_ms;
		all[r] = t2 - t0;
	}
	printf("%-26s total %6.1f  layout %6.1f  paint %6.1f (text %6.1f, %4d; ink %5.1f, %3d)  diff %5.1f  runs %3d box %dx%d\n", name,
	       med(all, reps), med(lay, reps), med(pnt, reps), med(txt, reps), pl_tr_text_n, med(ink, reps), pl_tr_ink_n,
	       med(dif, reps), runs_n, box.w, box.h);
	cairo_destroy(mcr);
	cairo_surface_destroy(ms);
}

static int today;
static void day0(void) { V.per = period_of(P_DAY, today), V.ov = OV_NONE; }
static void day1(void) { V.per = period_step(period_of(P_DAY, today), 1), V.ov = OV_NONE; }
static void week0(void) { V.per = period_of(P_WEEK, today), V.ov = OV_NONE; }
static void week1(void) { V.per = period_step(period_of(P_WEEK, today), 1), V.ov = OV_NONE; }
static void month0(void) { V.per = period_of(P_MONTH, today), V.ov = OV_NONE; }
static void info(void) { V.per = period_of(P_DAY, today), V.ov = OV_INFO; }
static void same(void) {}

int main(int argc, char **argv)
{
	pl_trace = 1;
	if (argc > 1) {
		store_init(&S, argv[1]);
		store_load(&S);
		tmpl_user_load(argv[1]);
	} else {
		setenv("GLASSINE_PLANNER_TODAY", SAMPLE_TODAY, 1);
		store_init(&S, NULL);
		sample_fill(&S);
	}
	const char *e;
	int reps = (e = getenv("BENCH_REPS")) ? atoi(e) : 9;
	view_ink_cache(!getenv("BENCH_NO_INK_CACHE")); /* painted ink kept (the planner keeps it) */
	reps = reps < 1 ? 1 : reps > 64 ? 64 : reps;
	SC = (e = getenv("BENCH_SCALE")) ? atoi(e) : 2;
	char voice[64];
	look_voice_name(voice, sizeof voice);
	look_init(&L, voice);
	today = day_today();
	V.today = today;
	V.hidden_kind = -1;
	V.W = (e = getenv("BENCH_W")) ? atoi(e) : 702;
	V.H = (e = getenv("BENCH_H")) ? atoi(e) : 884;
	struct frame fr = { 0 };
	printf("voice %s, %gx%g x%d, %d reps, ms (median)\n", voice, V.W, V.H, SC, reps);
	act("day -> next day", &fr, day0, day1, reps);
	act("day -> week", &fr, day0, week0, reps);
	act("week -> next week", &fr, week0, week1, reps);
	act("week -> month", &fr, week0, month0, reps);
	act("month -> day", &fr, month0, day0, reps);
	act("day -> (i) popover", &fr, day0, info, reps);
	act("day, repainted unchanged", &fr, day0, same, reps);
	frame_free(&fr);
	return 0;
}
