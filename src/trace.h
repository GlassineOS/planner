// SPDX-License-Identifier: GPL-2.0-or-later
/* The timing trace (GLASSINE_PLANNER_TRACE=1): what an action costs, stage by stage, on
 * stderr in the clock of glassine-epdd-desktop's update log (CLOCK_MONOTONIC ms, mod 10^6).
 * Off, it costs one branch per stage. */
#ifndef PL_TRACE_H
#define PL_TRACE_H

#include <stdio.h>
#include <time.h>

extern int pl_trace;
/* summed while tracing: text measured / drawn, ink drawn (ms, calls) */
extern double pl_tr_text_ms, pl_tr_ink_ms;
extern int pl_tr_text_n, pl_tr_ink_n;
/* the last frame_update: painting, comparing (ms) */
extern double pl_tr_paint_ms, pl_tr_diff_ms;

static inline double pl_tr_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

#define PL_TR(...)                                                                                                      \
	do {                                                                                                            \
		if (pl_trace) {                                                                                         \
			double t_ = pl_tr_now();                                                                        \
			fprintf(stderr, "%10.1f planner ", t_ - (double)((long long)(t_ / 1e6)) * 1e6);              \
			fprintf(stderr, __VA_ARGS__);                                                                   \
			fputc('\n', stderr);                                                                            \
		}                                                                                                       \
	} while (0)

#endif
