// SPDX-License-Identifier: GPL-2.0-or-later
/* A finger held still (FR-018, board event.delete): on an event's title or one of its ends
 * for HOLD_MS, moving no more than HOLD_SLOP px from where it came down, it asks to delete
 * the event. Moving further first makes it a drag. No clock here: the caller passes the time
 * in ms. */
#ifndef PL_HOLD_H
#define PL_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#define HOLD_MS 500
#define HOLD_SLOP 8

struct hold {
	uint32_t id;      /* what is held (0: nothing) */
	double x, y;      /* where the finger came down */
	long long t;      /* when */
	bool fired;       /* it has fired: the finger's lift opens what it leads to */
};

/* the finger comes down on id at x, y */
void hold_begin(struct hold *h, uint32_t id, double x, double y, long long now);
/* the finger moved to x, y: past the slop the hold is off (false) */
bool hold_move(struct hold *h, double x, double y);
/* ms until it fires (0: due), -1 when nothing is held or it has fired */
long long hold_left(const struct hold *h, long long now);
/* true once, when it is due: then it has fired */
bool hold_fire(struct hold *h, long long now);
/* the finger lifted or the touch ended: nothing is held */
void hold_end(struct hold *h);

#endif
