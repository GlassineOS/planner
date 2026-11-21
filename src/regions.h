/* SPDX-License-Identifier: GPL-2.0-or-later
 * The view's fields as the handwriting service's writing regions: one region per field,
 * kept across layouts (its ink stays) and told where the field now lies. After every layout
 * each live region's rectangle, clip, layer, guides and reading equal the field's (a moved
 * or stretched event, a template or view switch, the keyboard, the page's move). No Wayland
 * here: wl.c passes the handwriting calls, the tests pass recorders. */
#ifndef PL_REGIONS_H
#define PL_REGIONS_H
#include <stdbool.h>
#include "view.h"

struct ghw_region;

struct region {
	struct ghw_region *r;
	char name[96];
	uint32_t entry;       /* 0 until its first stroke makes one (an empty row) */
	struct vfield f;      /* the field it was made for (its kind, what to make) */
	struct fmake mk;      /* what a stroke makes when it has no entry (an empty row, or its
	                       * entry struck out and deleted before the page settles) */
	bool live;            /* still a field of the view */
	double x, y, w, h;    /* the rectangle as last set */
	int cx, cy, cw, ch;   /* the clip as last set (cw 0: none) */
	int layer, reading;   /* as last set (-1: not yet) */
	double guide, first;  /* as last set (guide -1: not yet) */
	struct region *next;
};

struct region_ops {
	/* a new region for rg (named rg->name; rg->f, rg->entry, rg->mk set): it sets what only
	 * a new region needs (context, neaten, the entry's ink back); NULL when it can't */
	struct ghw_region *(*make)(void *ud, struct region *rg);
	void (*destroy)(struct ghw_region *r);
	void (*rect)(struct ghw_region *r, double x, double y, double w, double h);
	void (*clip)(struct ghw_region *r, int x, int y, int w, int h);
	void (*layer)(struct ghw_region *r, int layer);
	void (*guides)(struct ghw_region *r, double spacing, double first);
	void (*reading)(struct ghw_region *r, int on);
	void *ud;
};

struct region_env {
	const struct view *v;
	struct store *s;
	double H;                                /* the surface's height */
	bool pen_down;                           /* a stroke is being written */
	bool (*covered)(const struct vfield *f); /* under an overlay: takes no strokes */
};

/* the regions after a layout of e->v (its fields): made, moved, set, or gone */
void regions_sync(struct region **list, const struct region_env *e, const struct region_ops *ops);

#endif
