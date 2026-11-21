// SPDX-License-Identifier: GPL-2.0-or-later
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "regions.h"

static void drop(struct region **list, struct region *o, const struct region_ops *ops)
{
	for (struct region **pp = list; *pp; pp = &(*pp)->next)
		if (*pp == o) {
			*pp = o->next;
			break;
		}
	ops->destroy(o->r);
	free(o);
}

/* where the field lies for the pen: its rectangle on the surface, and the part of it the
 * keyboard and the page's move leave visible (no clip when neither is there) */
static void place(const struct region_env *e, const struct vfield *f, double *r, int *c)
{
	const struct view *v = e->v;
	double x = f->x, y = f->y - v->scroll, w = f->w, h = f->h;
	if (e->covered && e->covered(f))
		x = y = w = h = 0;
	r[0] = x, r[1] = y, r[2] = w, r[3] = h;
	c[0] = c[1] = c[2] = c[3] = 0;
	if ((v->scroll > 0 || v->vis_h > 0) && w > 0 && h > 0) {
		double bot = v->vis_h > 0 ? v->vis_h - 46 : e->H;
		double cy0 = fmax(y, 0), cy1 = fmin(y + h, bot);
		c[0] = (int)x, c[1] = (int)cy0, c[2] = (int)w, c[3] = cy1 > cy0 ? (int)(cy1 - cy0) : 1;
	}
}

void regions_sync(struct region **list, const struct region_env *e, const struct region_ops *ops)
{
	const struct view *v = e->v;
	for (struct region *x = *list; x; x = x->next)
		x->live = false;
	for (int i = 0; i < v->nf; i++) {
		const struct vfield *f = &v->f[i];
		struct region *rg = NULL;
		for (struct region *x = *list; x && !rg; x = x->next)
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
			for (struct region *x = *list, *nx; x; x = nx) {
				nx = x->next;
				if (!strcmp(x->name, rg->name))
					drop(list, x, ops);
			}
			rg->f = *f;
			rg->entry = f->k == F_ENTRY ? f->entry : 0;
			rg->mk = (struct fmake){ f->k == F_NEW ? f->ekind : E_NKINDS, f->per, "", f->rank };
			snprintf(rg->mk.slot, sizeof rg->mk.slot, "%s", f->slot);
			rg->x = rg->y = rg->w = rg->h = -1;
			rg->cx = rg->cy = rg->cw = rg->ch = -1;
			rg->layer = rg->reading = -1;
			rg->guide = -1;
			rg->r = ops->make(ops->ud, rg);
			if (!rg->r) {
				free(rg);
				continue;
			}
			rg->next = *list;
			*list = rg;
		}
		rg->live = true;
		rg->f = *f;
		struct entry *me = f->k == F_ENTRY ? store_get(e->s, f->entry) : NULL;
		if (me) { /* struck out and deleted, its row makes the same entry again */
			rg->mk = (struct fmake){ me->kind == E_EVENT ? E_NKINDS : me->kind, me->per, "", me->rank };
			snprintf(rg->mk.slot, sizeof rg->mk.slot, "%s", me->slot);
		}
		if (rg->entry && f->k == F_NEW)
			rg->f.k = F_ENTRY;
		/* what the field says now; each part sent when it differs from what was last set
		 * (a region kept across a layout may now be another widget's field) */
		int reading = f->k != F_FREE;
		if (reading != rg->reading)
			ops->reading(rg->r, rg->reading = reading);
		if (f->layer != rg->layer)
			ops->layer(rg->r, rg->layer = f->layer);
		double g = f->guide > 0 ? f->guide : 0, first = f->guide > 0 ? f->first : 0;
		if (g != rg->guide || first != rg->first) {
			if (g > 0 || rg->guide > 0)
				ops->guides(rg->r, g, first);
			rg->guide = g, rg->first = first;
		}
		double r[4];
		int c[4];
		place(e, f, r, c);
		if (r[0] != rg->x || r[1] != rg->y || r[2] != rg->w || r[3] != rg->h) {
			ops->rect(rg->r, r[0], r[1], r[2], r[3]);
			rg->x = r[0], rg->y = r[1], rg->w = r[2], rg->h = r[3];
		}
		if (c[0] != rg->cx || c[1] != rg->cy || c[2] != rg->cw || c[3] != rg->ch) {
			ops->clip(rg->r, c[0], c[1], c[2], c[3]);
			rg->cx = c[0], rg->cy = c[1], rg->cw = c[2], rg->ch = c[3];
		}
	}
	for (struct region *x = *list, *nx; x; x = nx) {
		nx = x->next;
		/* a region still writing its first stroke into a new row is kept until the pause */
		if (!x->live && !(x->entry && e->pen_down))
			drop(list, x, ops);
	}
}
