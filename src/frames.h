/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Two buffers that take turns with the compositor (frames.c, from GlassineOS/home). */
#ifndef PL_FRAMES_H
#define PL_FRAMES_H

#include <stdbool.h>

#include "plan.h"

#define FRAME_BUFFERS 2
struct frames {
	struct { bool busy; struct rect dmg[MAX_DAMAGE]; int ndmg; bool all; } b[FRAME_BUFFERS];
	struct rect sdmg[MAX_DAMAGE];
	int nsdmg;
	bool sall;
	bool owed;
	int cw, ch;
};
void frames_init(struct frames *f);
void frames_canvas(struct frames *f, int w, int h);
void frames_damage(struct frames *f, const struct rect *r, int n);
bool frames_clip(const struct rect *r, int w, int h, int *x0, int *y0, int *x1, int *y1);
int frames_pick(struct frames *f);
void frames_shown(struct frames *f, int i);
void frames_released(struct frames *f, int i);

#endif
